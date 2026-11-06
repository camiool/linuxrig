#include "PluginCatalog.h"

namespace
{
    constexpr auto scanFlag = "--scan-plugin";
    constexpr auto scanRootTag = "LINUXRIG_SCAN";
    constexpr int scanTimeoutMs = 60000;   // yabridge tarda en el primer arranque de Wine

    // Guitarra = mono. Preferimos mono → estéreo; si no, lo que el plugin acepte.
    void configureBuses (juce::AudioPluginInstance& instance)
    {
        using Set = juce::AudioChannelSet;

        for (auto [in, out] : { std::pair { Set::mono(), Set::stereo() },
                                std::pair { Set::mono(), Set::mono() },
                                std::pair { Set::stereo(), Set::stereo() } })
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (in);
            layout.outputBuses.add (out);

            if (instance.checkBusesLayoutSupported (layout) && instance.setBusesLayout (layout))
                return;
        }
    }
}

PluginCatalog::PluginCatalog (juce::File cache)
    : juce::Thread ("Escaneo de plugins"), cacheFile (std::move (cache))
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    if (auto xml = juce::parseXML (cacheFile))
        known.recreateFromXml (*xml);

    numBlacklisted = known.getBlacklistedFiles().size();
}

PluginCatalog::~PluginCatalog()
{
    stopThread (5000);
}

//==============================================================================
void PluginCatalog::startScan (bool full)
{
    if (isThreadRunning())
        return;

    fullRescan = full;
    startThread();
}

juce::String PluginCatalog::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

void PluginCatalog::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (statusLock);
    status = s;
}

juce::Array<juce::PluginDescription> PluginCatalog::getPlugins() const
{
    auto types = known.getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });
    return types;
}

int PluginCatalog::getNumBlacklisted() const
{
    return numBlacklisted.load();
}

void PluginCatalog::save() const
{
    if (auto xml = known.createXml())
    {
        cacheFile.getParentDirectory().createDirectory();
        xml->writeTo (cacheFile);
    }
}

//==============================================================================
void PluginCatalog::run()
{
    if (fullRescan)
    {
        known.clear();
        known.clearBlacklistedFiles();
    }

    auto& format = *formatManager.getFormat (0);

    auto paths = format.getDefaultLocationsToSearch();
    paths.addIfNotAlreadyThere (juce::File ("~/.vst3"));
    paths.removeNonExistentPaths();

    for (const auto& type : known.getTypes())
        if (! format.doesPluginStillExist (type))
            known.removeType (type);

    const auto files = format.searchPathsForPlugins (paths, true, false);
    int failures = 0;

    for (const auto& file : files)
    {
        if (threadShouldExit())
            return;

        if (known.getBlacklistedFiles().contains (file) || known.isListingUpToDate (file, format))
            continue;

        setStatus ("Escaneando " + juce::File (file).getFileNameWithoutExtension() + "...");
        const auto result = scanInChildProcess (file);

        if (result == ScanResult::cancelled)
            return;

        if (result == ScanResult::failed)
        {
            known.addToBlacklist (file);
            ++failures;
        }
    }

    numBlacklisted = known.getBlacklistedFiles().size();
    save();
    setStatus (failures > 0 ? juce::String (failures) + " plugin(s) fallaron al escanear" : juce::String());
}

PluginCatalog::ScanResult PluginCatalog::scanInChildProcess (const juce::String& fileOrIdentifier)
{
    const auto output = juce::File::createTempFile (".xml");
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    juce::ChildProcess child;

    // Sin flags de stream: la salida del hijo va a /dev/null y no puede bloquearlo.
    if (! child.start (juce::StringArray { exe.getFullPathName(), scanFlag, fileOrIdentifier, output.getFullPathName() }, 0))
        return ScanResult::failed;

    const auto deadline = juce::Time::getMillisecondCounter() + scanTimeoutMs;

    while (! child.waitForProcessToFinish (100))
    {
        if (threadShouldExit() || juce::Time::getMillisecondCounter() > deadline)
        {
            child.kill();
            output.deleteFile();
            return threadShouldExit() ? ScanResult::cancelled : ScanResult::failed;
        }
    }

    const auto xml = juce::parseXML (output);
    output.deleteFile();

    if (xml == nullptr || ! xml->hasTagName (scanRootTag) || xml->getNumChildElements() == 0)
        return ScanResult::failed; // caído, o un bundle sin plugins

    for (const auto& type : known.getTypes())
        if (type.fileOrIdentifier == fileOrIdentifier)
            known.removeType (type);

    for (auto* element : xml->getChildIterator())
    {
        juce::PluginDescription description;

        if (description.loadFromXml (*element))
            known.addType (description);
    }

    return ScanResult::ok;
}

//==============================================================================
std::optional<juce::PluginDescription> PluginCatalog::find (const juce::String& identifier, const juce::String& file)
{
    if (auto d = known.getTypeForIdentifierString (identifier))
        return *d;

    if (file.isEmpty() || ! juce::File (file).exists())
        return std::nullopt;

    if (auto d = known.getTypeForFile (file))
        return *d;

    juce::OwnedArray<juce::PluginDescription> types;
    formatManager.getFormat (0)->findAllTypesForFile (types, file);

    if (types.isEmpty())
        return std::nullopt;

    known.addType (*types[0]);
    return *types[0];
}

std::unique_ptr<juce::AudioPluginInstance> PluginCatalog::createInstance (const juce::PluginDescription& description,
                                                                          double sampleRate, int blockSize,
                                                                          juce::String& error)
{
    auto instance = formatManager.createPluginInstance (description, sampleRate, blockSize, error);

    if (instance == nullptr)
    {
        if (error.isEmpty())
            error = "No se pudo instanciar " + description.name;

        return nullptr;
    }

    configureBuses (*instance);
    instance->setNonRealtime (false);
    return instance;
}

//==============================================================================
bool PluginCatalog::isScannerCommandLine (const juce::StringArray& args)
{
    return args.size() == 3 && args[0] == scanFlag;
}

int PluginCatalog::runScanner (const juce::StringArray& args)
{
    juce::AudioPluginFormatManager manager;
    manager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    juce::OwnedArray<juce::PluginDescription> types;
    manager.getFormat (0)->findAllTypesForFile (types, args[1]);

    juce::XmlElement root (scanRootTag);

    for (auto* type : types)
        root.addChildElement (type->createXml().release());

    return root.writeTo (juce::File (args[2])) ? 0 : 1;
}
