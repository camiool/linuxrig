#pragma once

#include "../engine/AudioEngine.h"
#include "Pedalboard.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>

// Ventana principal: barra de presets, pedalera, entrada/ganancias y estado.
// La sesión (la cadena actual con el estado de cada plugin) se guarda sola en
// ~/.config/LinuxRig/session.json y se recupera al abrir.
class MainComponent final : public juce::Component,
                            private Rig::Listener,
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

    // Rig::Listener
    void rigChanged() override;
    void slotAboutToBeDeleted (const Slot&) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // Audio
    void selectDefaultJackDevices();
    void showAudioSettings();
    void rebuildInputList();

    // Presets
    juce::File getPresetsFolder() const;
    void showPresetMenu();
    void loadPresetFile (const juce::File&);
    void applyPreset (const Preset&);
    void savePreset();
    void askPresetName();
    void writePreset (const juce::File&, const juce::String& name);
    void newPreset();
    void updatePresetButton();

    // Plugins
    void showAddPluginMenu (int insertIndex);
    void openEditor (int index);

    void saveSession();
    void restoreSession();
    void saveSettings();

    const juce::File dataFolder;
    juce::ApplicationProperties properties;
    juce::AudioDeviceManager deviceManager;
    AudioEngine engine;
    PluginCatalog catalog;
    Rig rig;

    juce::TextButton audioButton { "Audio..." };
    juce::TextButton presetButton;
    juce::TextButton saveButton { "Guardar" };
    juce::TextButton saveAsButton { "Guardar como..." };
    juce::TextButton newButton { "Nuevo" };
    juce::TextButton rescanButton { "Reescanear plugins" };

    Pedalboard pedalboard { rig };
    juce::Viewport pedalboardViewport;

    juce::Label inputLabel { {}, "Entrada" };
    juce::ComboBox inputBox;
    juce::Label inGainLabel { {}, "Ganancia" }, outGainLabel { {}, "Volumen" };
    juce::Slider inGainSlider, outGainSlider;
    juce::Label statusLabel;

    std::map<const Slot*, std::unique_ptr<PluginWindow>> editors;

    juce::File presetFile;
    juce::String presetName;

    bool sessionDirty = false;
    juce::uint32 lastChangeMs = 0, lastSessionSaveMs = 0;

    float inLevel = 0.0f, outLevel = 0.0f;
    int xrunBaseline = 0;
    juce::Rectangle<int> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
