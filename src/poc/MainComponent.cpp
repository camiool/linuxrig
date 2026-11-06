#include "MainComponent.h"

namespace
{
    constexpr auto defaultPluginPath = "~/.vst3/TONE3000.vst3";

    float dbToGain (double db) { return juce::Decibels::decibelsToGain ((float) db, -60.0f); }

    juce::String ms (double samples, double rate)
    {
        return rate > 0 ? juce::String (1000.0 * samples / rate, 2) + " ms" : "-";
    }
}

//==============================================================================
class MainComponent::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, std::function<void()> onCloseFn)
        : DocumentWindow (editor->getAudioProcessor()->getName(),
                          juce::Colours::darkgrey, DocumentWindow::closeButton),
          onClose (std::move (onCloseFn))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { onClose(); }

private:
    std::function<void()> onClose;
};

//==============================================================================
MainComponent::MainComponent()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "poc";
    options.folderName          = ".config/LinuxRig";   // en Linux JUCE lo cuelga de ~
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters (options);
    auto* settings = properties.getUserSettings();

    // Dispositivo: el guardado, o JACK (PipeWire) por defecto.
    auto savedAudio = settings->getXmlValue ("audio");
    deviceManager.initialise (2, 2, savedAudio.get(), true);

    if (savedAudio == nullptr)
        selectDefaultJackDevices();

    deviceManager.addChangeListener (this);

    for (auto* b : { &audioButton, &loadButton, &editorButton, &resetButton })
        addAndMakeVisible (b);

    addAndMakeVisible (bypassButton);
    audioButton.onClick  = [this] { showAudioSettings(); };
    loadButton.onClick   = [this] { choosePlugin(); };
    editorButton.onClick = [this] { showPluginEditor(); };
    resetButton.onClick  = [this] { resetStats(); };
    bypassButton.onClick = [this] { engine.bypass = bypassButton.getToggleState(); };

    addAndMakeVisible (inputLabel);
    addAndMakeVisible (inputBox);
    inputBox.onChange = [this]
    {
        engine.inputChannel = inputBox.getSelectedId() - 1;
        properties.getUserSettings()->setValue ("inputChannel", inputBox.getText());
    };

    for (auto [slider, label, key, initial] : { std::tuple { &inGainSlider, &inGainLabel, "inGainDb", 0.0 },
                                                std::tuple { &outGainSlider, &outGainLabel, "outGainDb", 0.0 } })
    {
        slider->setRange (-60.0, 24.0, 0.1);
        slider->setTextValueSuffix (" dB");
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        slider->setDoubleClickReturnValue (true, 0.0);
        slider->setValue (settings->getDoubleValue (key, initial), juce::dontSendNotification);
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    inGainSlider.onValueChange  = [this] { engine.inputGain  = dbToGain (inGainSlider.getValue()); };
    outGainSlider.onValueChange = [this] { engine.outputGain = dbToGain (outGainSlider.getValue()); };
    engine.inputGain  = dbToGain (inGainSlider.getValue());
    engine.outputGain = dbToGain (outGainSlider.getValue());

    pluginLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (pluginLabel);

    statsLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    statsLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (statsLabel);

    rebuildInputList();
    deviceManager.addAudioCallback (&engine);

    loadPlugin (juce::File (settings->getValue ("pluginPath", defaultPluginPath)));
    resetStats();

    setSize (820, 520);
    startTimerHz (15);
}

MainComponent::~MainComponent()
{
    saveSettings();
    pluginWindow = nullptr;
    deviceManager.removeAudioCallback (&engine);
    deviceManager.removeChangeListener (this);
    engine.unloadPlugin();
}

void MainComponent::saveSettings()
{
    auto* settings = properties.getUserSettings();

    if (auto state = deviceManager.createStateXml())
        settings->setValue ("audio", state.get());

    settings->setValue ("inGainDb", inGainSlider.getValue());
    settings->setValue ("outGainDb", outGainSlider.getValue());
    properties.saveIfNeeded();
}

// Primer arranque: con JACK sobre PipeWire cada nodo es un "dispositivo" y JUCE
// toma el primero (p. ej. un micrófono USB). Preferimos la interfaz de audio;
// "hw_USB" es el nodo crudo de la tarjeta ALSA "USB" (la Scarlett).
void MainComponent::selectDefaultJackDevices()
{
    deviceManager.setCurrentAudioDeviceType ("JACK", true);
    auto* type = deviceManager.getCurrentDeviceTypeObject();

    if (type == nullptr || type->getTypeName() != "JACK")
        return;

    auto pick = [type] (bool forInput) -> juce::String
    {
        for (auto& name : type->getDeviceNames (forInput))
            if (name.containsIgnoreCase ("Scarlett") || name.containsIgnoreCase ("hw_USB"))
                return name;

        return {};
    };

    auto setup = deviceManager.getAudioDeviceSetup();

    if (auto in = pick (true); in.isNotEmpty())
    {
        setup.inputDeviceName = in;
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, 2, true);
    }

    if (auto out = pick (false); out.isNotEmpty())
        setup.outputDeviceName = out;

    deviceManager.setAudioDeviceSetup (setup, true);
}

//==============================================================================
void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 1, 16, 1, 16,
                                                             false, false, false, false);
    selector->setSize (520, 420);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Configuración de audio";
    o.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildInputList();
    resetStats();
    saveSettings();
}

void MainComponent::rebuildInputList()
{
    inputBox.clear (juce::dontSendNotification);
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    // El callback recibe solo los canales activos, compactados en orden.
    const auto names  = device->getInputChannelNames();
    const auto active = device->getActiveInputChannels();
    int packed = 0;

    for (int i = 0; i < names.size(); ++i)
        if (active[i])
            inputBox.addItem (names[i], ++packed);

    if (packed == 0)
        return;

    // Por defecto la entrada 2 (instrumento en la Scarlett Solo) si existe.
    const auto saved = properties.getUserSettings()->getValue ("inputChannel");
    int id = packed >= 2 ? 2 : 1;

    for (int i = 0; i < inputBox.getNumItems(); ++i)
        if (inputBox.getItemText (i) == saved)
            id = inputBox.getItemId (i);

    inputBox.setSelectedId (id, juce::dontSendNotification);
    engine.inputChannel = id - 1;
}

//==============================================================================
void MainComponent::choosePlugin()
{
    chooser = std::make_unique<juce::FileChooser> ("Elegir plugin VST3",
                                                   juce::File ("~/.vst3"), "*.vst3");

    // Un .vst3 en Linux es una carpeta (bundle), por eso se permite elegir directorios.
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() != juce::File())
                                  loadPlugin (fc.getResult());
                          });
}

void MainComponent::loadPlugin (const juce::File& bundle)
{
    pluginWindow = nullptr;
    const auto error = engine.loadPlugin (bundle);

    if (error.isNotEmpty())
    {
        pluginLabel.setText ("Sin plugin  —  " + error, juce::dontSendNotification);
        return;
    }

    auto* plugin = engine.getPlugin();
    pluginLabel.setText (plugin->getName() + "  (" + bundle.getFullPathName() + ")",
                         juce::dontSendNotification);
    properties.getUserSettings()->setValue ("pluginPath", bundle.getFullPathName());
    resetStats();
}

void MainComponent::showPluginEditor()
{
    if (pluginWindow != nullptr)
    {
        pluginWindow->toFront (true);
        return;
    }

    auto* plugin = engine.getPlugin();

    if (plugin == nullptr || ! plugin->hasEditor())
        return;

    if (auto* editor = plugin->createEditorAndMakeActive())
        pluginWindow = std::make_unique<PluginWindow> (editor, [this] { pluginWindow = nullptr; });
}

//==============================================================================
void MainComponent::resetStats()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    xrunBaseline = device != nullptr ? juce::jmax (0, device->getXRunCount()) : 0;
    engine.resetOverloadCount();
    testStartMs = juce::Time::getMillisecondCounter();
}

void MainComponent::timerCallback()
{
    // Medidores con caída suave.
    inLevel  = juce::jmax (engine.takeInputPeak(),  inLevel  * 0.85f);
    outLevel = juce::jmax (engine.takeOutputPeak(), outLevel * 0.85f);
    repaint (meterArea);

    auto* device = deviceManager.getCurrentAudioDevice();
    juce::StringArray lines;

    if (device == nullptr || ! device->isPlaying())
    {
        lines.add ("Sin dispositivo de audio activo. Pulsa \"Audio...\".");
        statsLabel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
        return;
    }

    const double rate = device->getCurrentSampleRate();
    const int buffer  = device->getCurrentBufferSizeSamples();
    const int inLat   = device->getInputLatencyInSamples();
    const int outLat  = device->getOutputLatencyInSamples();
    const int plugLat = engine.getPlugin() != nullptr ? engine.getPlugin()->getLatencySamples() : 0;

    // Si el driver no informa latencias, estimamos 1 buffer de entrada + 1 de salida.
    const bool reported = inLat + outLat > 0;
    const double total  = (reported ? inLat + outLat : 2 * buffer) + plugLat;

    const int xruns = device->getXRunCount();
    const auto elapsed = (juce::Time::getMillisecondCounter() - testStartMs) / 1000;

    lines.add ("Driver       " + deviceManager.getCurrentAudioDeviceType() + "  ·  " + device->getName());
    lines.add ("Formato      " + juce::String (rate, 0) + " Hz  ·  buffer " + juce::String (buffer)
               + " muestras (" + ms (buffer, rate) + ")");
    lines.add ("Latencia     entrada " + ms (inLat, rate) + "  ·  salida " + ms (outLat, rate)
               + "  ·  plugin " + ms (plugLat, rate));
    lines.add ("Ida y vuelta " + ms (total, rate) + (reported ? "" : "  (estimada: 2 × buffer)")
               + (total * 1000.0 / rate < 10.0 ? "   OK < 10 ms" : "   >= 10 ms"));
    lines.add ("Xruns        " + (xruns >= 0 ? juce::String (xruns - xrunBaseline) : juce::String ("n/d (el driver no lo informa)")));
    lines.add ("Sobrecargas  " + juce::String (engine.getOverloadCount())
               + "  (callbacks más lentos que el buffer)");
    lines.add ("CPU audio    " + juce::String (deviceManager.getCpuUsage() * 100.0, 1) + " %");
    lines.add ("Prueba       " + juce::String::formatted ("%02d:%02d:%02d", elapsed / 3600, (elapsed / 60) % 60, elapsed % 60)
               + (elapsed >= 1800 ? "   30 min cumplidos" : ""));

    statsLabel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto drawMeter = [&g] (juce::Rectangle<int> r, float peak, const juce::String& name)
    {
        const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
        const float fill = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);

        g.setColour (juce::Colours::black);
        g.fillRect (r);
        g.setColour (peak >= 1.0f ? juce::Colours::red : db > -6.0f ? juce::Colours::orange : juce::Colours::limegreen);
        g.fillRect (r.withWidth (juce::roundToInt ((float) r.getWidth() * fill)));
        g.setColour (juce::Colours::white);
        g.drawText (name + "  " + juce::String (db, 1) + " dB", r.reduced (6, 0), juce::Justification::centredLeft);
    };

    auto area = meterArea;
    drawMeter (area.removeFromTop (22), inLevel, "Entrada");
    area.removeFromTop (6);
    drawMeter (area.removeFromTop (22), outLevel, "Salida");
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto row = area.removeFromTop (30);
    for (auto* b : std::initializer_list<juce::Component*> { &audioButton, &loadButton, &editorButton, &bypassButton })
    {
        b->setBounds (row.removeFromLeft (130));
        row.removeFromLeft (8);
    }
    resetButton.setBounds (row.removeFromRight (170));

    area.removeFromTop (10);
    pluginLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    row = area.removeFromTop (26);
    inputLabel.setBounds (row.removeFromLeft (130));
    inputBox.setBounds (row.removeFromLeft (300));

    for (auto [slider, label] : { std::pair { &inGainSlider, &inGainLabel }, std::pair { &outGainSlider, &outGainLabel } })
    {
        area.removeFromTop (6);
        row = area.removeFromTop (26);
        label->setBounds (row.removeFromLeft (130));
        slider->setBounds (row);
    }

    area.removeFromTop (12);
    meterArea = area.removeFromTop (50);
    area.removeFromTop (12);
    statsLabel.setBounds (area);
}
