#include "Slot.h"

Slot::Slot (std::unique_ptr<juce::AudioProcessor> processorToUse)
    : processor (std::move (processorToUse))
{
}

Slot::~Slot()
{
    release();
}

void Slot::prepare (double sampleRate, int maxBlockSize)
{
    if (processor != nullptr)
    {
        processor->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
        processor->prepareToPlay (sampleRate, maxBlockSize);
    }

    // Margen por si el driver entrega bloques mayores que los anunciados.
    dry.setSize (2, juce::jmax (maxBlockSize, 2048), false, true, false);
    mixStep = (float) (1.0 / (fadeSeconds * sampleRate));
    mix = (processor != nullptr && ! bypassed.load()) ? 1.0f : 0.0f;

    preparedRate  = sampleRate;
    preparedBlock = maxBlockSize;
}

void Slot::release()
{
    if (preparedRate > 0.0 && processor != nullptr)
        processor->releaseResources();

    preparedRate  = 0.0;
    preparedBlock = 0;
}

bool Slot::isPreparedFor (double sampleRate, int maxBlockSize) const noexcept
{
    return juce::exactlyEqual (preparedRate, sampleRate) && preparedBlock == maxBlockSize;
}

void Slot::process (juce::AudioBuffer<float>& work, int numSamples, juce::MidiBuffer& midi) noexcept
{
    const bool wet = processor != nullptr && ! bypassed.load() && ! processor->isSuspended();
    const float target = wet ? 1.0f : 0.0f;

    // Sin espacio para guardar la señal seca no hay fundido: salto directo.
    if (numSamples > dry.getNumSamples())
        mix = target;

    if (juce::exactlyEqual (mix, 0.0f) && juce::exactlyEqual (target, 0.0f))
        return; // bypass completo: el plugin no procesa

    const bool fading = ! juce::exactlyEqual (mix, target);

    if (fading)
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom (ch, 0, work, ch, 0, numSamples);

    // El buffer referencia memoria ya reservada (≤ 32 canales: sin heap).
    const int ins  = processor->getTotalNumInputChannels();
    const int outs = processor->getTotalNumOutputChannels();
    const int numChannels = juce::jlimit (1, maxChannels, juce::jmax (ins, outs));

    for (int ch = 2; ch < numChannels; ++ch)
        work.clear (ch, 0, numSamples);

    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), numChannels, numSamples);
    midi.clear();
    processor->processBlock (view, midi);

    // Salida mono → a los dos canales del bus.
    if (outs < 2)
        work.copyFrom (1, 0, work, 0, 0, numSamples);

    if (fading)
    {
        float m = mix;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* out = work.getWritePointer (ch);
            const auto* in = dry.getReadPointer (ch);
            m = mix;

            for (int i = 0; i < numSamples; ++i)
            {
                m = target > m ? juce::jmin (target, m + mixStep) : juce::jmax (target, m - mixStep);
                out[i] = in[i] + (out[i] - in[i]) * m;
            }
        }

        mix = m;
    }
}
