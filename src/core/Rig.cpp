#include "Rig.h"

juce::String Rig::Entry::getName() const
{
    if (missing.has_value())
        return missing->pluginName + " (falta)";

    return description.name;
}

Rig::Rig (ChainProcessor& c, PluginCatalog& p)
    : chain (c), catalog (p)
{
}

Rig::~Rig()
{
    clear();
}

//==============================================================================
juce::String Rig::addPlugin (const juce::PluginDescription& description, int index)
{
    juce::String error;
    auto instance = catalog.createInstance (description, chain.getSampleRate(), chain.getBlockSize(), error);

    if (instance == nullptr)
        return error;

    Entry entry { nextId(), std::make_shared<Slot> (std::move (instance)), description, std::nullopt };
    const auto position = index < 0 || index > size() ? entries.end() : entries.begin() + index;
    entries.insert (position, std::move (entry));

    publish();
    return {};
}

void Rig::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;

    listeners.call ([&] (Listener& l) { l.slotAboutToBeDeleted (*entries[(size_t) index].slot); });
    entries.erase (entries.begin() + index);
    publish();
}

void Rig::move (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, size()) || ! juce::isPositiveAndBelow (to, size()) || from == to)
        return;

    auto entry = std::move (entries[(size_t) from]);
    entries.erase (entries.begin() + from);
    entries.insert (entries.begin() + to, std::move (entry));
    publish();
}

void Rig::setBypassed (int index, bool shouldBeBypassed)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;

    entries[(size_t) index].slot->bypassed = shouldBeBypassed;
    listeners.call ([] (Listener& l) { l.rigChanged(); });
}

void Rig::clear()
{
    replaceEntries ({});
}

//==============================================================================
Preset Rig::toPreset (const juce::String& name) const
{
    Preset preset;
    preset.name = name;

    for (const auto& entry : entries)
    {
        if (entry.missing.has_value())
        {
            auto copy = *entry.missing;
            copy.bypass = entry.slot->bypassed;
            preset.chain.push_back (std::move (copy));
            continue;
        }

        SlotPreset slot;
        slot.id               = entry.id;
        slot.pluginName       = entry.description.name;
        slot.pluginFormat     = entry.description.pluginFormatName;
        slot.pluginIdentifier = entry.description.createIdentifierString();
        slot.pluginFile       = entry.description.fileOrIdentifier;
        slot.bypass           = entry.slot->bypassed;
        entry.slot->getProcessor()->getStateInformation (slot.state);
        preset.chain.push_back (std::move (slot));
    }

    return preset;
}

juce::StringArray Rig::loadPreset (const Preset& preset)
{
    juce::StringArray warnings;
    std::vector<Entry> loaded;

    // Los plugins nuevos se crean mientras suena la cadena anterior.
    for (const auto& slot : preset.chain)
    {
        Entry entry;
        entry.id = slot.id;

        if (auto description = catalog.find (slot.pluginIdentifier, slot.pluginFile))
        {
            juce::String error;

            if (auto instance = catalog.createInstance (*description, chain.getSampleRate(), chain.getBlockSize(), error))
            {
                if (slot.state.getSize() > 0)
                    instance->setStateInformation (slot.state.getData(), (int) slot.state.getSize());

                entry.description = *description;
                entry.slot = std::make_shared<Slot> (std::move (instance));
            }
            else
            {
                warnings.add (slot.pluginName + ": " + error);
            }
        }
        else
        {
            warnings.add (slot.pluginName + ": no encontrado (" + slot.pluginFile + ")");
        }

        if (entry.slot == nullptr)
        {
            entry.slot = std::make_shared<Slot> (nullptr);
            entry.missing = slot;
        }

        entry.slot->bypassed = slot.bypass;
        loaded.push_back (std::move (entry));
    }

    replaceEntries (std::move (loaded));
    return warnings;
}

//==============================================================================
void Rig::replaceEntries (std::vector<Entry> newEntries)
{
    for (const auto& entry : entries)
        listeners.call ([&] (Listener& l) { l.slotAboutToBeDeleted (*entry.slot); });

    entries = std::move (newEntries);
    publish();
}

void Rig::publish()
{
    ChainProcessor::SlotList slots;

    for (const auto& entry : entries)
        slots.push_back (entry.slot);

    chain.setSlots (std::move (slots));
    listeners.call ([] (Listener& l) { l.rigChanged(); });
}

juce::String Rig::nextId() const
{
    int highest = 0;

    for (const auto& entry : entries)
        highest = juce::jmax (highest, entry.id.substring (1).getIntValue());

    return "s" + juce::String (highest + 1);
}
