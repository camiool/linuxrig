#pragma once

#include "Slot.h"

#include <mutex>
#include <vector>

// La cadena de slots en serie.
//
// La cadena activa es una lista inmutable que se publica con un puntero atómico.
// Para cambiarla (insertar, quitar, reordenar) se arma una lista nueva fuera del
// hilo de audio y se intercambia de golpe; la anterior se libera cuando el
// callback en curso terminó (contador "epoch": impar = callback en marcha).
// Los slots se comparten entre listas, así reordenar no recrea plugins.
class ChainProcessor final
{
public:
    using SlotList = std::vector<std::shared_ptr<Slot>>;

    ChainProcessor();
    ~ChainProcessor();

    // --- Fuera del hilo de audio --------------------------------------------
    void prepare (double sampleRate, int maxBlockSize);
    void release();
    void setSlots (SlotList newSlots);

    double getSampleRate() const;
    int getBlockSize() const;
    int getLatencySamples() const;

    // --- Hilo de audio ------------------------------------------------------
    // `work`: señal en los canales 0 y 1, al menos Slot::maxChannels canales.
    void process (juce::AudioBuffer<float>& work, int numSamples) noexcept;

private:
    struct Snapshot
    {
        SlotList slots;
    };

    void waitForAudioThread() const;

    mutable std::mutex mutex;   // nunca en el hilo de audio
    std::unique_ptr<Snapshot> current;
    bool prepared = false;
    double sampleRate = 48000.0;
    int blockSize = 128;

    std::atomic<Snapshot*> active { nullptr };
    std::atomic<juce::uint64> epoch { 0 };
    juce::MidiBuffer midi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainProcessor)
};
