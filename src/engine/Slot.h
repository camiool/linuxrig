#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <atomic>
#include <memory>

// Un eslabón de la cadena: un procesador (plugin) con bypass por fundido.
//
// Se crea, prepara y destruye fuera del hilo de audio; process() corre en el
// hilo de audio. Sin procesador (un plugin que falta al cargar un preset) deja
// pasar la señal tal cual.
class Slot final
{
public:
    explicit Slot (std::unique_ptr<juce::AudioProcessor> processorToUse);
    ~Slot();

    juce::AudioProcessor* getProcessor() const noexcept { return processor.get(); }

    std::atomic<bool> bypassed { false };

    // --- Fuera del hilo de audio --------------------------------------------
    void prepare (double sampleRate, int maxBlockSize);
    void release();
    bool isPreparedFor (double sampleRate, int maxBlockSize) const noexcept;

    // --- Hilo de audio ------------------------------------------------------
    // `work` trae la señal estéreo en los canales 0 y 1 y tiene al menos
    // maxChannels canales (los extra son para plugins con más buses).
    void process (juce::AudioBuffer<float>& work, int numSamples, juce::MidiBuffer& midi) noexcept;

    static constexpr int maxChannels = 16;
    static constexpr double fadeSeconds = 0.010;

private:
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::AudioBuffer<float> dry;
    double preparedRate = 0.0;
    int preparedBlock = 0;

    // 0 = seco (bypass), 1 = procesado. Tras prepare() solo lo toca el hilo de audio.
    float mix = 1.0f;
    float mixStep = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Slot)
};
