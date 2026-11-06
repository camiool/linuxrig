#pragma once

#include "ChainProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>

// Puente entre el dispositivo de audio y la cadena: un canal de entrada (la
// guitarra) → ganancia → cadena estéreo → volumen → todas las salidas.
// El callback no reserva memoria ni toma locks.
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine() = default;

    ChainProcessor& getChain() noexcept { return chain; }

    // Valores que lee el hilo de audio.
    std::atomic<int>   inputChannel { 0 };   // índice dentro de los canales de entrada activos
    std::atomic<float> inputGain  { 1.0f };
    std::atomic<float> outputGain { 1.0f };

    // Medidores (el hilo de mensajes los lee y los pone a cero).
    float takeInputPeak() noexcept  { return inputPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f); }

    // Callbacks que tardaron más que la duración del buffer.
    int getOverloadCount() const noexcept { return overloads.load(); }
    void resetOverloadCount() noexcept    { overloads.store (0); }

    // --- AudioIODeviceCallback ---------------------------------------------
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    ChainProcessor chain;

    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<int> overloads { 0 };

    // Propiedad del hilo de audio mientras el dispositivo está en marcha.
    juce::AudioBuffer<float> work;
    juce::int64 budgetTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
