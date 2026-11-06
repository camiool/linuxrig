#include "AudioEngine.h"

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sampleRate = device->getCurrentSampleRate();
    const int blockSize = device->getCurrentBufferSizeSamples();

    work.setSize (Slot::maxChannels, juce::jmax (blockSize, 2048), false, true, false);
    budgetTicks = (juce::int64) ((double) blockSize / sampleRate
                                 * (double) juce::Time::getHighResolutionTicksPerSecond());
    chain.prepare (sampleRate, blockSize);
}

void AudioEngine::audioDeviceStopped()
{
    chain.release();
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

    if (numSamples > work.getNumSamples())
    {
        // No debería ocurrir; preferimos silencio a reservar memoria aquí.
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

        return;
    }

    // 1) Entrada seleccionada × ganancia → los dos canales del bus.
    const int inCh = inputChannel.load();
    const float* input = (inCh >= 0 && inCh < numInputChannels) ? inputChannelData[inCh] : nullptr;

    if (input != nullptr)
        juce::FloatVectorOperations::copyWithMultiply (work.getWritePointer (0), input, inputGain.load(), numSamples);
    else
        work.clear (0, 0, numSamples);

    work.copyFrom (1, 0, work, 0, 0, numSamples);
    storeMax (inputPeak, peakOf (work.getReadPointer (0), numSamples));

    // 2) Cadena.
    chain.process (work, numSamples);

    // 3) Salidas: L/R del bus; si hay más salidas, repiten el canal derecho.
    const float outGain = outputGain.load();
    float outPeak = 0.0f;

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        auto* dest = outputChannelData[ch];

        if (dest == nullptr)
            continue;

        juce::FloatVectorOperations::copyWithMultiply (dest, work.getReadPointer (juce::jmin (ch, 1)), outGain, numSamples);
        outPeak = juce::jmax (outPeak, peakOf (dest, numSamples));
    }

    storeMax (outputPeak, outPeak);

    if (budgetTicks > 0 && juce::Time::getHighResolutionTicks() - startTicks > budgetTicks)
        overloads.fetch_add (1, std::memory_order_relaxed);
}
