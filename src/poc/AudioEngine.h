#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <mutex>

// Motor de la Fase 0: entrada (1 canal) → 1 plugin → todas las salidas.
//
// El hilo de audio no reserva memoria ni toma locks. El plugin activo se publica
// con un puntero atómico; para retirarlo, el hilo de mensajes espera a que el
// callback en curso termine (contador "epoch": impar = callback en marcha) y
// solo entonces lo destruye.
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // --- Hilo de mensajes ---------------------------------------------------
    // Devuelve un mensaje de error; vacío si se cargó bien.
    juce::String loadPlugin (const juce::File& vst3Bundle);
    void unloadPlugin();
    juce::AudioPluginInstance* getPlugin() const noexcept { return plugin.get(); }

    // Valores que lee el hilo de audio.
    std::atomic<int>   inputChannel { 0 };   // índice dentro de los canales de entrada activos
    std::atomic<float> inputGain  { 1.0f };
    std::atomic<float> outputGain { 1.0f };
    std::atomic<bool>  bypass { false };

    // Medidores (el hilo de mensajes los lee y los pone a cero).
    float takeInputPeak() noexcept  { return inputPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f); }

    // Callbacks que tardaron más que la duración del buffer (el plugin no llega a tiempo).
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
    void publish (std::unique_ptr<juce::AudioPluginInstance> next);
    void waitForAudioThread() const;
    static void configureBuses (juce::AudioPluginInstance& instance);

    static constexpr int maxChannels = 16;

    juce::AudioPluginFormatManager formatManager;

    std::mutex prepareMutex;   // solo fuera del hilo RT: carga de plugin vs. arranque/parada del dispositivo
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    bool   deviceRunning = false;
    double sampleRate = 48000.0;
    int    blockSize = 128;

    // Compartido con el hilo de audio.
    std::atomic<juce::AudioPluginInstance*> active { nullptr };
    std::atomic<juce::uint64> epoch { 0 };
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<int> overloads { 0 };

    // Propiedad del hilo de audio mientras el dispositivo está en marcha.
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    juce::int64 budgetTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
