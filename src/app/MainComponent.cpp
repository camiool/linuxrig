#include "MainComponent.h"

namespace
{
    constexpr auto defaultPresetName = "Sin nombre";
    constexpr juce::uint32 sessionSaveDelayMs = 2000;      // tras un cambio en la cadena
    constexpr juce::uint32 sessionSaveIntervalMs = 60000;  // captura cambios hechos dentro de los plugins

    float dbToGain (double db) { return juce::Decibels::decibelsToGain ((float) db, -60.0f); }

    juce::String ms (double samples, double rate)
    {
        return rate > 0 ? juce::String (1000.0 * samples / rate, 2) + " ms" : "-";
    }

    void showWarning (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
    }
}

//==============================================================================
class MainComponent::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, const juce::String& title, std::function<void()> onCloseFn)
        : DocumentWindow (title, juce::Colours::darkgrey, DocumentWindow::closeButton),
          onClose (std::move (onCloseFn))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { onClose(); }

private:
    std::function<void()> onClose;
};

//==============================================================================
MainComponent::MainComponent()
    : dataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LinuxRig")),
      catalog (dataFolder.getChildFile ("plugins.xml")),
      rig (engine.getChain(), catalog)
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "app";
    options.folderName          = ".config/LinuxRig";   // en Linux JUCE lo cuelga de ~
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters (options);
    auto* settings = properties.getUserSettings();

    // Dispositivo: el guardado, o JACK (PipeWire) con la interfaz por defecto.
    auto savedAudio = settings->getXmlValue ("audio");
    deviceManager.initialise (2, 2, savedAudio.get(), true);

    if (savedAudio == nullptr)
        selectDefaultJackDevices();

    deviceManager.addChangeListener (this);

    // Barra superior.
    for (auto* b : { &audioButton, &presetButton, &saveButton, &saveAsButton, &newButton, &rescanButton })
        addAndMakeVisible (b);

    audioButton.onClick  = [this] { showAudioSettings(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick   = [this] { savePreset(); };
    saveAsButton.onClick = [this] { askPresetName(); };
    newButton.onClick    = [this] { newPreset(); };
    rescanButton.onClick = [this] { catalog.startScan (true); };
    rescanButton.setTooltip ("Escanea de nuevo todos los plugins, incluidos los que fallaron");

    // Pedalera.
    pedalboard.onOpenEditor = [this] (int index) { openEditor (index); };
    pedalboard.onAddPlugin  = [this] (int index) { showAddPluginMenu (index); };
    pedalboardViewport.setViewedComponent (&pedalboard, false);
    pedalboardViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (pedalboardViewport);

    // Entrada y ganancias.
    addAndMakeVisible (inputLabel);
    addAndMakeVisible (inputBox);
    inputBox.onChange = [this]
    {
        engine.inputChannel = inputBox.getSelectedId() - 1;
        properties.getUserSettings()->setValue ("inputChannel", inputBox.getText());
    };

    for (auto [slider, label, key] : { std::tuple { &inGainSlider, &inGainLabel, "inGainDb" },
                                       std::tuple { &outGainSlider, &outGainLabel, "outGainDb" } })
    {
        slider->setRange (-60.0, 24.0, 0.1);
        slider->setTextValueSuffix (" dB");
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        slider->setDoubleClickReturnValue (true, 0.0);
        slider->setValue (settings->getDoubleValue (key, 0.0), juce::dontSendNotification);
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    inGainSlider.onValueChange  = [this] { engine.inputGain  = dbToGain (inGainSlider.getValue()); };
    outGainSlider.onValueChange = [this] { engine.outputGain = dbToGain (outGainSlider.getValue()); };
    engine.inputGain  = dbToGain (inGainSlider.getValue());
    engine.outputGain = dbToGain (outGainSlider.getValue());

    statusLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    statusLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (statusLabel);

    rig.addListener (this);
    rebuildInputList();
    deviceManager.addAudioCallback (&engine);

    catalog.startScan (false);
    restoreSession();

    setSize (1000, 600);
    startTimerHz (15);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveSession();
    saveSettings();

    editors.clear();          // los editores antes que sus plugins
    rig.removeListener (this);
    rig.clear();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.removeChangeListener (this);
}

void MainComponent::saveSettings()
{
    auto* settings = properties.getUserSettings();

    if (auto state = deviceManager.createStateXml())
        settings->setValue ("audio", state.get());

    settings->setValue ("inGainDb", inGainSlider.getValue());
    settings->setValue ("outGainDb", outGainSlider.getValue());
    properties.saveIfNeeded();
}

//==============================================================================
void MainComponent::saveSession()
{
    rig.toPreset (presetName).save (dataFolder.getChildFile ("session.json"));

    auto* settings = properties.getUserSettings();
    settings->setValue ("presetFile", presetFile.getFullPathName());
    settings->setValue ("presetName", presetName);
    properties.saveIfNeeded();

    sessionDirty = false;
    lastSessionSaveMs = juce::Time::getMillisecondCounter();
}

void MainComponent::restoreSession()
{
    auto* settings = properties.getUserSettings();
    presetFile = juce::File (settings->getValue ("presetFile"));
    presetName = settings->getValue ("presetName", defaultPresetName);

    Preset session;

    if (Preset::load (dataFolder.getChildFile ("session.json"), session).wasOk())
    {
        applyPreset (session);

        if (session.name.isNotEmpty())
            presetName = session.name;
    }

    updatePresetButton();
    sessionDirty = false;
    lastSessionSaveMs = juce::Time::getMillisecondCounter();
}

//==============================================================================
void MainComponent::rigChanged()
{
    pedalboard.refresh();
    sessionDirty = true;
    lastChangeMs = juce::Time::getMillisecondCounter();
}

void MainComponent::slotAboutToBeDeleted (const Slot& slot)
{
    editors.erase (&slot);
}

void MainComponent::openEditor (int index)
{
    if (! juce::isPositiveAndBelow (index, rig.size()))
        return;

    const auto& entry = rig[index];
    const Slot* key = entry.slot.get();
    auto* processor = entry.slot->getProcessor();

    if (auto it = editors.find (key); it != editors.end())
    {
        it->second->toFront (true);
        return;
    }

    if (processor == nullptr || ! processor->hasEditor())
        return;

    if (auto* editor = processor->createEditorAndMakeActive())
    {
        // El cierre se difiere: la ventana no debe borrarse dentro de su propio callback.
        editors[key] = std::make_unique<PluginWindow> (editor, entry.getName(), [this, key]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), key]
            {
                if (safe != nullptr)
                    safe->editors.erase (key);
            });
        });
    }
}

void MainComponent::showAddPluginMenu (int insertIndex)
{
    const auto types = catalog.getPlugins();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Agregar plugin");

    for (int i = 0; i < types.size(); ++i)
        menu.addItem (i + 1, types[i].name + "   (" + types[i].manufacturerName + ")");

    if (types.isEmpty())
        menu.addItem (-1, catalog.isScanning() ? "Escaneando plugins..." : "No hay plugins en ~/.vst3 ni /usr/lib/vst3", false);

    menu.addSeparator();
    menu.addItem (1000, "Buscar plugins nuevos");

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [this, types, insertIndex] (int result)
                        {
                            if (result > 0 && result <= types.size())
                            {
                                if (auto error = rig.addPlugin (types[result - 1], insertIndex); error.isNotEmpty())
                                    showWarning ("No se pudo agregar el plugin", error);
                                else
                                    openEditor (insertIndex < 0 ? rig.size() - 1 : insertIndex);
                            }
                            else if (result == 1000)
                            {
                                catalog.startScan (false);
                            }
                        });
}

//==============================================================================
juce::File MainComponent::getPresetsFolder() const
{
    return dataFolder.getChildFile ("presets");
}

void MainComponent::updatePresetButton()
{
    presetButton.setButtonText ("Preset: " + presetName + "  v");
}

void MainComponent::showPresetMenu()
{
    auto files = getPresetsFolder().findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();

    juce::PopupMenu menu;

    for (int i = 0; i < files.size(); ++i)
        menu.addItem (i + 1, files[i].getFileNameWithoutExtension(), true, files[i] == presetFile);

    if (files.isEmpty())
        menu.addItem (-1, "(no hay presets guardados)", false);

    menu.addSeparator();
    menu.addItem (1000, "Abrir carpeta de presets");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [this, files] (int result)
                        {
                            if (result > 0 && result <= files.size())
                            {
                                loadPresetFile (files[result - 1]);
                            }
                            else if (result == 1000)
                            {
                                getPresetsFolder().createDirectory();
                                getPresetsFolder().startAsProcess();
                            }
                        });
}

void MainComponent::loadPresetFile (const juce::File& file)
{
    Preset preset;

    if (auto result = Preset::load (file, preset); result.failed())
    {
        showWarning ("No se pudo abrir el preset", result.getErrorMessage());
        return;
    }

    applyPreset (preset);
    presetFile = file;
    presetName = preset.name.isNotEmpty() ? preset.name : file.getFileNameWithoutExtension();
    updatePresetButton();
    saveSession();
}

void MainComponent::applyPreset (const Preset& preset)
{
    if (const auto warnings = rig.loadPreset (preset); ! warnings.isEmpty())
        showWarning ("Algunos plugins no se cargaron", warnings.joinIntoString ("\n"));
}

void MainComponent::savePreset()
{
    if (presetFile.existsAsFile())
        writePreset (presetFile, presetName);
    else
        askPresetName();
}

void MainComponent::askPresetName()
{
    auto* dialog = new juce::AlertWindow ("Guardar preset", "Nombre del preset:", juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("name", presetName == defaultPresetName ? juce::String() : presetName);
    dialog->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, dialog] (int result)
    {
        const auto name = dialog->getTextEditorContents ("name").trim();

        if (result != 1 || name.isEmpty())
            return;

        const auto file = getPresetsFolder().getChildFile (juce::File::createLegalFileName (name) + ".json");

        if (! file.existsAsFile() || file == presetFile)
        {
            writePreset (file, name);
            return;
        }

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Reemplazar preset")
                                          .withMessage ("Ya existe un preset \"" + name + juce::String (L"\". ¿Reemplazarlo?"))
                                          .withButton ("Reemplazar")
                                          .withButton ("Cancelar"),
                                      [this, file, name] (int choice)
                                      {
                                          if (choice == 1)
                                              writePreset (file, name);
                                      });
    }), true);
}

void MainComponent::writePreset (const juce::File& file, const juce::String& name)
{
    if (auto result = rig.toPreset (name).save (file); result.failed())
    {
        showWarning ("No se pudo guardar el preset", result.getErrorMessage());
        return;
    }

    presetFile = file;
    presetName = name;
    updatePresetButton();
    saveSession();
}

void MainComponent::newPreset()
{
    auto reset = [this]
    {
        rig.clear();
        presetFile = juce::File();
        presetName = defaultPresetName;
        updatePresetButton();
        saveSession();
    };

    if (rig.size() == 0)
    {
        reset();
        return;
    }

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Nuevo preset")
                                      .withMessage (juce::String (L"Se vaciará la cadena. Lo que no hayas guardado en un preset se pierde."))
                                      .withButton ("Vaciar")
                                      .withButton ("Cancelar"),
                                  [reset] (int choice)
                                  {
                                      if (choice == 1)
                                          reset();
                                  });
}

//==============================================================================
// Primer arranque: con JACK sobre PipeWire cada nodo es un "dispositivo" y JUCE
// toma el primero (p. ej. un micrófono USB). Preferimos la interfaz de audio;
// "hw_USB" es el nodo crudo de la tarjeta ALSA "USB" (la Scarlett).
void MainComponent::selectDefaultJackDevices()
{
    deviceManager.setCurrentAudioDeviceType ("JACK", true);
    auto* type = deviceManager.getCurrentDeviceTypeObject();

    if (type == nullptr || type->getTypeName() != "JACK")
        return;

    auto pick = [type] (bool forInput) -> juce::String
    {
        for (auto& name : type->getDeviceNames (forInput))
            if (name.containsIgnoreCase ("Scarlett") || name.containsIgnoreCase ("hw_USB"))
                return name;

        return {};
    };

    auto setup = deviceManager.getAudioDeviceSetup();

    if (auto in = pick (true); in.isNotEmpty())
    {
        setup.inputDeviceName = in;
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, 2, true);
    }

    if (auto out = pick (false); out.isNotEmpty())
        setup.outputDeviceName = out;

    deviceManager.setAudioDeviceSetup (setup, true);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 1, 16, 1, 16,
                                                             false, false, false, false);
    selector->setSize (520, 420);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = juce::String (L"Configuración de audio");
    o.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildInputList();
    saveSettings();

    auto* device = deviceManager.getCurrentAudioDevice();
    xrunBaseline = device != nullptr ? juce::jmax (0, device->getXRunCount()) : 0;
    engine.resetOverloadCount();
}

void MainComponent::rebuildInputList()
{
    inputBox.clear (juce::dontSendNotification);
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    // El callback recibe solo los canales activos, compactados en orden.
    const auto names  = device->getInputChannelNames();
    const auto active = device->getActiveInputChannels();
    int packed = 0;

    for (int i = 0; i < names.size(); ++i)
        if (active[i])
            inputBox.addItem (names[i], ++packed);

    if (packed == 0)
        return;

    // Por defecto la entrada 2 (instrumento en la Scarlett Solo) si existe.
    const auto saved = properties.getUserSettings()->getValue ("inputChannel");
    int id = packed >= 2 ? 2 : 1;

    for (int i = 0; i < inputBox.getNumItems(); ++i)
        if (inputBox.getItemText (i) == saved)
            id = inputBox.getItemId (i);

    inputBox.setSelectedId (id, juce::dontSendNotification);
    engine.inputChannel = id - 1;
}

//==============================================================================
void MainComponent::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();

    if ((sessionDirty && now - lastChangeMs > sessionSaveDelayMs) || now - lastSessionSaveMs > sessionSaveIntervalMs)
        saveSession();

    // Medidores con caída suave.
    inLevel  = juce::jmax (engine.takeInputPeak(),  inLevel  * 0.85f);
    outLevel = juce::jmax (engine.takeOutputPeak(), outLevel * 0.85f);
    repaint (meterArea);

    juce::StringArray lines;
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr || ! device->isPlaying())
    {
        lines.add ("Sin dispositivo de audio activo. Pulsa \"Audio...\".");
    }
    else
    {
        const double rate = device->getCurrentSampleRate();
        const int buffer  = device->getCurrentBufferSizeSamples();
        const int inLat   = device->getInputLatencyInSamples();
        const int outLat  = device->getOutputLatencyInSamples();
        const int chainLat = engine.getChain().getLatencySamples();

        // Si el driver no informa latencias, estimamos 1 buffer de entrada + 1 de salida.
        const bool reported = inLat + outLat > 0;
        const double total  = (reported ? inLat + outLat : 2 * buffer) + chainLat;
        const int xruns = device->getXRunCount();
        const juce::String sep (L"  ·  ");

        lines.add (deviceManager.getCurrentAudioDeviceType() + sep + device->getName() + sep
                   + juce::String (rate, 0) + " Hz" + sep + "buffer " + juce::String (buffer) + " (" + ms (buffer, rate) + ")");
        lines.add ("Ida y vuelta " + ms (total, rate) + (reported ? "" : " (estimada)")
                   + "  (plugins " + ms (chainLat, rate) + ")"
                   + sep + "Xruns " + (xruns >= 0 ? juce::String (xruns - xrunBaseline) : juce::String ("n/d"))
                   + sep + "Sobrecargas " + juce::String (engine.getOverloadCount())
                   + sep + "CPU " + juce::String (deviceManager.getCpuUsage() * 100.0, 1) + " %");
    }

    auto scan = catalog.getStatus();

    if (catalog.getNumBlacklisted() > 0 && ! catalog.isScanning())
        scan << (scan.isEmpty() ? "" : " - ") << juce::String (catalog.getNumBlacklisted())
             << " en lista negra (\"Reescanear plugins\" los reintenta)";

    if (scan.isNotEmpty())
        lines.add (scan);

    statusLabel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto drawMeter = [&g] (juce::Rectangle<int> r, float peak, const juce::String& name)
    {
        const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
        const float fill = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);

        g.setColour (juce::Colours::black);
        g.fillRect (r);
        g.setColour (peak >= 1.0f ? juce::Colours::red : db > -6.0f ? juce::Colours::orange : juce::Colours::limegreen);
        g.fillRect (r.withWidth (juce::roundToInt ((float) r.getWidth() * fill)));
        g.setColour (juce::Colours::white);
        g.drawText (name + "  " + juce::String (db, 1) + " dB", r.reduced (6, 0), juce::Justification::centredLeft);
    };

    auto area = meterArea;
    drawMeter (area.removeFromTop (20), inLevel, "Entrada");
    area.removeFromTop (4);
    drawMeter (area.removeFromTop (20), outLevel, "Salida");
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto row = area.removeFromTop (30);
    audioButton.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (16);
    presetButton.setBounds (row.removeFromLeft (280));
    row.removeFromLeft (8);

    for (auto* b : { &saveButton, &saveAsButton, &newButton })
    {
        b->setBounds (row.removeFromLeft (b == &saveAsButton ? 130 : 90));
        row.removeFromLeft (8);
    }

    rescanButton.setBounds (row.removeFromRight (160));

    area.removeFromTop (12);
    pedalboardViewport.setBounds (area.removeFromTop (Pedalboard::tileHeight + 2 * Pedalboard::gap + 14));
    area.removeFromTop (12);

    row = area.removeFromTop (26);
    inputLabel.setBounds (row.removeFromLeft (100));
    inputBox.setBounds (row.removeFromLeft (320));

    for (auto [slider, label] : { std::pair { &inGainSlider, &inGainLabel }, std::pair { &outGainSlider, &outGainLabel } })
    {
        area.removeFromTop (6);
        row = area.removeFromTop (26);
        label->setBounds (row.removeFromLeft (100));
        slider->setBounds (row);
    }

    area.removeFromTop (12);
    meterArea = area.removeFromTop (44);
    area.removeFromTop (10);
    statusLabel.setBounds (area);
}
