#pragma once

#include "AudioEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Ventana principal de la Fase 0: dispositivo, plugin, ganancias y diagnóstico
// (latencia, xruns, sobrecargas, CPU y cronómetro para la prueba de 30 min).
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PluginWindow;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void selectDefaultJackDevices();
    void showAudioSettings();
    void choosePlugin();
    void loadPlugin (const juce::File& bundle);
    void showPluginEditor();
    void rebuildInputList();
    void resetStats();
    void saveSettings();

    juce::ApplicationProperties properties;
    juce::AudioDeviceManager deviceManager;
    AudioEngine engine;

    juce::TextButton audioButton  { "Audio..." };
    juce::TextButton loadButton   { "Cargar VST3..." };
    juce::TextButton editorButton { "Editor" };
    juce::ToggleButton bypassButton { "Bypass" };
    juce::TextButton resetButton  { "Reiniciar contadores" };

    juce::Label inputLabel { {}, "Entrada" };
    juce::ComboBox inputBox;
    juce::Label inGainLabel { {}, "Ganancia entrada" }, outGainLabel { {}, "Volumen salida" };
    juce::Slider inGainSlider, outGainSlider;

    juce::Label pluginLabel;
    juce::Label statsLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<PluginWindow> pluginWindow;

    float inLevel = 0.0f, outLevel = 0.0f;
    int xrunBaseline = 0;
    juce::uint32 testStartMs = 0;
    juce::Rectangle<int> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
