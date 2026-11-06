// Tests del motor (cadena, bypass con fundido, cambios en caliente) y de los presets.
// Sin interfaz de audio ni plugins: usan un procesador de ganancia de prueba.

#include "../src/core/Preset.h"
#include "../src/engine/ChainProcessor.h"

#include <thread>

namespace
{
    // Entrada mono; salida mono o estéreo (la entrada copiada) × ganancia.
    class GainProcessor final : public juce::AudioProcessor
    {
    public:
        GainProcessor (float gainToUse, bool monoOutput)
            : AudioProcessor (BusesProperties()
                                  .withInput ("In", juce::AudioChannelSet::mono())
                                  .withOutput ("Out", monoOutput ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo())),
              gain (gainToUse)
        {
        }

        using AudioProcessor::processBlock;

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            const int n = buffer.getNumSamples();

            for (int ch = 1; ch < getTotalNumOutputChannels(); ++ch)
                buffer.copyFrom (ch, 0, buffer, 0, 0, n);

            for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch)
                buffer.applyGain (ch, 0, n, gain);

            ++processCount;
        }

        const juce::String getName() const override                   { return "Gain"; }
        void prepareToPlay (double, int) override                     {}
        void releaseResources() override                              {}
        double getTailLengthSeconds() const override                  { return 0.0; }
        bool acceptsMidi() const override                             { return false; }
        bool producesMidi() const override                            { return false; }
        juce::AudioProcessorEditor* createEditor() override           { return nullptr; }
        bool hasEditor() const override                               { return false; }
        int getNumPrograms() override                                 { return 1; }
        int getCurrentProgram() override                              { return 0; }
        void setCurrentProgram (int) override                         {}
        const juce::String getProgramName (int) override              { return {}; }
        void changeProgramName (int, const juce::String&) override    {}
        void getStateInformation (juce::MemoryBlock&) override        {}
        void setStateInformation (const void*, int) override          {}

        std::atomic<int> processCount { 0 };

    private:
        const float gain;
    };

    std::shared_ptr<Slot> gainSlot (float gain, bool monoOutput = false)
    {
        return std::make_shared<Slot> (std::make_unique<GainProcessor> (gain, monoOutput));
    }

    void fill (juce::AudioBuffer<float>& buffer, int numSamples, float left, float right)
    {
        juce::FloatVectorOperations::fill (buffer.getWritePointer (0), left, numSamples);
        juce::FloatVectorOperations::fill (buffer.getWritePointer (1), right, numSamples);
    }

    constexpr double rate = 48000.0;
    constexpr int block = 64;
}

//==============================================================================
class ChainTests final : public juce::UnitTest
{
public:
    ChainTests() : juce::UnitTest ("Cadena", "LinuxRig") {}

    void runTest() override
    {
        ChainProcessor chain;
        chain.prepare (rate, block);
        juce::AudioBuffer<float> work (Slot::maxChannels, block);

        beginTest ("Cadena vacia deja pasar la senal");
        fill (work, block, 0.5f, 0.25f);
        chain.process (work, block);
        expectEquals (work.getSample (0, 10), 0.5f);
        expectEquals (work.getSample (1, 10), 0.25f);

        beginTest ("Dos slots en serie multiplican");
        chain.setSlots ({ gainSlot (2.0f), gainSlot (3.0f) });
        fill (work, block, 0.5f, 0.5f);
        chain.process (work, block);
        expectWithinAbsoluteError (work.getSample (0, 0), 3.0f, 1.0e-6f);
        expectWithinAbsoluteError (work.getSample (1, block - 1), 3.0f, 1.0e-6f);

        beginTest ("Plugin con salida mono llena los dos canales");
        chain.setSlots ({ gainSlot (2.0f, true) });
        fill (work, block, 0.5f, 0.1f);
        chain.process (work, block);
        expectWithinAbsoluteError (work.getSample (1, 5), 1.0f, 1.0e-6f);

        beginTest ("Slot sin plugin deja pasar");
        chain.setSlots ({ std::make_shared<Slot> (nullptr) });
        fill (work, block, 0.5f, 0.5f);
        chain.process (work, block);
        expectEquals (work.getSample (0, 0), 0.5f);

        testBypassFade (chain, work);
        testHotSwap (chain);
    }

private:
    void testBypassFade (ChainProcessor& chain, juce::AudioBuffer<float>& work)
    {
        beginTest ("Bypass con fundido de 10 ms sin saltos");

        auto slot = gainSlot (2.0f);
        auto* gain = dynamic_cast<GainProcessor*> (slot->getProcessor());
        chain.setSlots ({ slot });

        auto runBlocks = [&] (int numBlocks)
        {
            std::vector<float> out;

            for (int b = 0; b < numBlocks; ++b)
            {
                fill (work, block, 1.0f, 1.0f);
                chain.process (work, block);
                out.insert (out.end(), work.getReadPointer (0), work.getReadPointer (0) + block);
            }

            return out;
        };

        expectWithinAbsoluteError (runBlocks (1).back(), 2.0f, 1.0e-6f);

        const int fadeSamples = juce::roundToInt (Slot::fadeSeconds * rate);
        const float maxStep = 1.0f / (float) fadeSamples + 1.0e-5f;   // de 2 a 1 en fadeSamples

        auto expectSmooth = [&] (const std::vector<float>& out, float from, float to)
        {
            float previous = from, largest = 0.0f;

            for (auto v : out)
            {
                largest = juce::jmax (largest, std::abs (v - previous));
                previous = v;
            }

            expectLessOrEqual (largest, maxStep, "salto entre muestras durante el fundido");
            expectWithinAbsoluteError (out[(size_t) fadeSamples], to, 1.0e-5f);
            expectWithinAbsoluteError (out.back(), to, 1.0e-6f);
        };

        slot->bypassed = true;
        expectSmooth (runBlocks (12), 2.0f, 1.0f);

        const int callsWhileBypassed = gain->processCount;
        runBlocks (4);
        expectEquals (gain->processCount.load(), callsWhileBypassed, "un plugin en bypass no debe procesar");

        slot->bypassed = false;
        expectSmooth (runBlocks (12), 1.0f, 2.0f);
    }

    void testHotSwap (ChainProcessor& chain)
    {
        beginTest ("Cambiar la cadena mientras suena");

        std::atomic<bool> running { true };
        std::atomic<int> badSamples { 0 }, callbacks { 0 };

        // Ganancia 1 en todos los slots: la salida debe ser siempre exactamente 1.
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> w (Slot::maxChannels, block);

            while (running)
            {
                fill (w, block, 1.0f, 1.0f);
                chain.process (w, block);
                ++callbacks;

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (! juce::exactlyEqual (w.getSample (ch, i), 1.0f))
                            ++badSamples;
            }
        });

        for (int i = 0; i < 2000; ++i)
        {
            ChainProcessor::SlotList slots;

            for (int k = 0; k < i % 5; ++k)
                slots.push_back (gainSlot (1.0f, k % 2 == 0));

            chain.setSlots (std::move (slots));
        }

        running = false;
        audio.join();

        expectGreaterThan (callbacks.load(), 0);
        expectEquals (badSamples.load(), 0);
    }
};

//==============================================================================
class PresetTests final : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Presets", "LinuxRig") {}

    void runTest() override
    {
        Preset preset;
        preset.name = juce::String (L"Lead Crunch ñ");

        SlotPreset amp;
        amp.id = "s1";
        amp.pluginName = "TONE3000";
        amp.pluginFormat = "VST3";
        amp.pluginIdentifier = "VST3-TONE3000-abcdef01-12345678";
        amp.pluginFile = "/home/user/.vst3/TONE3000.vst3";
        amp.bypass = true;

        for (int i = 0; i < 768; ++i)
        {
            const auto byte = (juce::uint8) (i % 256);
            amp.state.append (&byte, 1);
        }

        SlotPreset empty;
        empty.id = "s2";
        empty.pluginName = "Delay";
        empty.pluginFormat = "VST3";

        preset.chain = { amp, empty };

        beginTest ("JSON ida y vuelta");
        Preset back;
        expect (Preset::fromVar (preset.toVar(), back).wasOk());
        expect (back == preset);

        beginTest ("Guardar y cargar un archivo");
        juce::TemporaryFile temp (".json");
        expect (preset.save (temp.getFile()).wasOk());
        Preset loaded;
        expect (Preset::load (temp.getFile(), loaded).wasOk());
        expect (loaded == preset);

        beginTest ("Rechaza versiones desconocidas");
        auto json = preset.toVar();
        json.getDynamicObject()->setProperty ("version", 99);
        expect (Preset::fromVar (json, loaded).failed());

        beginTest ("Rechaza JSON roto");
        expect (temp.getFile().replaceWithText ("{ esto no es json"));
        expect (Preset::load (temp.getFile(), loaded).failed());
    }
};

static ChainTests chainTests;
static PresetTests presetTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("LinuxRig");

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    return failures > 0 ? 1 : 0;
}
