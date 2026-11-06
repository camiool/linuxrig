#pragma once

#include "../engine/ChainProcessor.h"
#include "PluginCatalog.h"
#include "Preset.h"

#include <optional>

// La cadena tal como la ve la UI (hilo de mensajes): qué plugin hay en cada
// slot, operaciones de edición y conversión desde/hacia un Preset. Cada cambio
// se publica en el ChainProcessor sin cortar el audio.
class Rig final
{
public:
    struct Entry
    {
        juce::String id;
        std::shared_ptr<Slot> slot;
        juce::PluginDescription description;
        std::optional<SlotPreset> missing;   // plugin no encontrado: se conserva para no perderlo al guardar

        juce::String getName() const;
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void rigChanged() = 0;
        // Antes de destruir el plugin del slot (p. ej. para cerrar su editor).
        virtual void slotAboutToBeDeleted (const Slot&) {}
    };

    Rig (ChainProcessor&, PluginCatalog&);
    ~Rig();

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    int size() const noexcept { return (int) entries.size(); }
    const Entry& operator[] (int index) const { return entries[(size_t) index]; }

    // Devuelve un mensaje de error; vacío si salió bien. index < 0 = al final.
    juce::String addPlugin (const juce::PluginDescription&, int index = -1);
    void remove (int index);
    void move (int from, int to);
    void setBypassed (int index, bool shouldBeBypassed);
    void clear();

    Preset toPreset (const juce::String& name) const;
    // Devuelve avisos (plugins que faltan o no cargaron); la carga sigue con el resto.
    juce::StringArray loadPreset (const Preset&);

private:
    void publish();
    void replaceEntries (std::vector<Entry>);
    juce::String nextId() const;

    ChainProcessor& chain;
    PluginCatalog& catalog;
    std::vector<Entry> entries;
    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Rig)
};
