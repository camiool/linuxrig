#include "ChainProcessor.h"

#include <thread>

ChainProcessor::ChainProcessor()
    : current (std::make_unique<Snapshot>())
{
    active.store (current.get());
}

ChainProcessor::~ChainProcessor()
{
    setSlots ({});
}

//==============================================================================
void ChainProcessor::prepare (double newSampleRate, int maxBlockSize)
{
    const std::lock_guard lock (mutex);

    sampleRate = newSampleRate;
    blockSize  = maxBlockSize;
    midi.ensureSize (2048);

    for (auto& slot : current->slots)
        slot->prepare (sampleRate, blockSize);

    prepared = true;
}

void ChainProcessor::release()
{
    const std::lock_guard lock (mutex);

    for (auto& slot : current->slots)
        slot->release();

    prepared = false;
}

void ChainProcessor::setSlots (SlotList newSlots)
{
    auto next = std::make_unique<Snapshot>();
    next->slots = std::move (newSlots);
    std::unique_ptr<Snapshot> old;

    {
        const std::lock_guard lock (mutex);

        if (prepared)
            for (auto& slot : next->slots)
                if (! slot->isPreparedFor (sampleRate, blockSize))
                    slot->prepare (sampleRate, blockSize);

        active.store (next.get());
        waitForAudioThread();
        old = std::exchange (current, std::move (next));
    }

    // `old` se destruye aquí: los slots que ya no estén en ninguna lista
    // liberan y destruyen su plugin, fuera del hilo de audio.
}

void ChainProcessor::waitForAudioThread() const
{
    const auto seen = epoch.load();

    if ((seen & 1) == 0)
        return; // ningún callback en curso

    const auto deadline = juce::Time::getMillisecondCounter() + 1000;

    while (epoch.load() == seen && juce::Time::getMillisecondCounter() < deadline)
        std::this_thread::yield();
}

double ChainProcessor::getSampleRate() const
{
    const std::lock_guard lock (mutex);
    return sampleRate;
}

int ChainProcessor::getBlockSize() const
{
    const std::lock_guard lock (mutex);
    return blockSize;
}

int ChainProcessor::getLatencySamples() const
{
    const std::lock_guard lock (mutex);
    int total = 0;

    for (auto& slot : current->slots)
        if (auto* p = slot->getProcessor())
            total += p->getLatencySamples();

    return total;
}

//==============================================================================
void ChainProcessor::process (juce::AudioBuffer<float>& work, int numSamples) noexcept
{
    epoch.fetch_add (1); // impar: callback en curso

    for (auto& slot : active.load()->slots)
        slot->process (work, numSamples, midi);

    epoch.fetch_add (1); // par: callback terminado
}
