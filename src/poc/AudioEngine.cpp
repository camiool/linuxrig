#include "AudioEngine.h"

#include <thread>

AudioEngine::AudioEngine()
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
}

AudioEngine::~AudioEngine()
{
    unloadPlugin();
}

//==============================================================================
juce::String AudioEngine::loadPlugin (const juce::File& vst3Bundle)
{
    if (! vst3Bundle.exists())
        return "No existe: " + vst3Bundle.getFullPathName();

    auto* format = formatManager.getFormat (0);
    juce::OwnedArray<juce::PluginDescription> types;
    format->findAllTypesForFile (types, vst3Bundle.getFullPathName());

    if (types.isEmpty())
        return "No se encontró ningún plugin VST3 en " + vst3Bundle.getFileName();

    double rate;
    int size;
    {
        const std::lock_guard lock (prepareMutex);
        rate = sampleRate;
        size = blockSize;
    }

    juce::String error;
    auto instance = formatManager.createPluginInstance (*types[0], rate, size, error);

    if (instance == nullptr)
        return error.isNotEmpty() ? error : "El plugin no se pudo instanciar";

    configureBuses (*instance);
    instance->setNonRealtime (false);
    publish (std::move (instance));
    return {};
}

void AudioEngine::unloadPlugin()
{
    publish (nullptr);
}

void AudioEngine::publish (std::unique_ptr<juce::AudioPluginInstance> next)
{
    const std::lock_guard lock (prepareMutex);

    if (next != nullptr)
    {
        next->setRateAndBufferSizeDetails (sampleRate, blockSize);

        if (deviceRunning)
            next->prepareToPlay (sampleRate, blockSize);
    }

    active.store (next.get());
    waitForAudioThread();

    // A partir de aquí el hilo de audio ya no puede tener el plugin anterior.
    auto old = std::exchange (plugin, std::move (next));

    if (old != nullptr && deviceRunning)
        old->releaseResources();
}

void AudioEngine::waitForAudioThread() const
{
    const auto seen = epoch.load();

    if ((seen & 1) == 0)
        return; // ningún callback en curso

    const auto deadline = juce::Time::getMillisecondCounter() + 1000;

    while (epoch.load() == seen && juce::Time::getMillisecondCounter() < deadline)
        std::this_thread::yield();
}

void AudioEngine::configureBuses (juce::AudioPluginInstance& instance)
{
    using Set = juce::AudioChannelSet;

    // Guitarra = mono. Preferimos mono → estéreo; si no, lo que el plugin acepte.
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

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const std::lock_guard lock (prepareMutex);

    sampleRate = device->getCurrentSampleRate();
    blockSize  = device->getCurrentBufferSizeSamples();

    // Margen por si el driver entrega bloques mayores que los anunciados.
    scratch.setSize (maxChannels, juce::jmax (blockSize, 2048), false, true, false);
    midi.ensureSize (1024);
    budgetTicks = (juce::int64) ((double) blockSize / sampleRate
                                 * (double) juce::Time::getHighResolutionTicksPerSecond());

    if (plugin != nullptr)
    {
        plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
    }

    deviceRunning = true;
}

void AudioEngine::audioDeviceStopped()
{
    const std::lock_guard lock (prepareMutex);

    if (plugin != nullptr)
        plugin->releaseResources();

    deviceRunning = false;
}

//==============================================================================
static float peakOf (const float* data, int numSamples) noexcept
{
    auto range = juce::FloatVectorOperations::findMinAndMax (data, numSamples);
    return juce::jmax (-range.getStart(), range.getEnd());
}

static void storeMax (std::atomic<float>& target, float value) noexcept
{
    if (value > target.load (std::memory_order_relaxed))
        target.store (value, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const auto startTicks = juce::Time::getHighResolutionTicks();
    epoch.fetch_add (1); // impar: callback en curso

    auto* processor = active.load();

    if (numSamples > scratch.getNumSamples())
    {
        // No debería ocurrir; preferimos silencio a reservar memoria aquí.
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

        epoch.fetch_add (1);
        return;
    }

    const bool usePlugin = processor != nullptr && ! bypass.load() && ! processor->isSuspended();
    const int pluginIns  = usePlugin ? processor->getTotalNumInputChannels()  : 1;
    const int pluginOuts = usePlugin ? processor->getTotalNumOutputChannels() : 1;
    const int numChannels = juce::jlimit (1, maxChannels, juce::jmax (pluginIns, pluginOuts));

    // 1) Entrada seleccionada × ganancia → todos los canales de entrada del plugin.
    const int inCh = inputChannel.load();
    const float* input = (inCh >= 0 && inCh < numInputChannels) ? inputChannelData[inCh] : nullptr;
    const float inGain = inputGain.load();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* dest = scratch.getWritePointer (ch);

        if (input != nullptr && ch < juce::jmax (1, pluginIns))
            juce::FloatVectorOperations::copyWithMultiply (dest, input, inGain, numSamples);
        else
            juce::FloatVectorOperations::clear (dest, numSamples);
    }

    storeMax (inputPeak, peakOf (scratch.getReadPointer (0), numSamples));

    // 2) Plugin. El buffer referencia memoria ya reservada (≤ 32 canales: sin heap).
    if (usePlugin)
    {
        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), numChannels, numSamples);
        midi.clear();
        processor->processBlock (view, midi);
    }

    // 3) Salidas. Si el plugin es mono (o no hay plugin), el canal 0 va a todas.
    const float outGain = outputGain.load();
    const int sourceChannels = juce::jmax (1, pluginOuts);
    float outPeak = 0.0f;

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        auto* dest = outputChannelData[ch];

        if (dest == nullptr)
            continue;

        const auto* src = scratch.getReadPointer (juce::jmin (ch, sourceChannels - 1));
        juce::FloatVectorOperations::copyWithMultiply (dest, src, outGain, numSamples);
        outPeak = juce::jmax (outPeak, peakOf (dest, numSamples));
    }

    storeMax (outputPeak, outPeak);

    epoch.fetch_add (1); // par: callback terminado

    if (budgetTicks > 0 && juce::Time::getHighResolutionTicks() - startTicks > budgetTicks)
        overloads.fetch_add (1, std::memory_order_relaxed);
}
