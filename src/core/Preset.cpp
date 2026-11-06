#include "Preset.h"

juce::var Preset::toVar() const
{
    juce::Array<juce::var> slots;

    for (const auto& slot : chain)
    {
        auto plugin = juce::DynamicObject::Ptr (new juce::DynamicObject());
        plugin->setProperty ("format", slot.pluginFormat);
        plugin->setProperty ("uid", slot.pluginIdentifier);
        plugin->setProperty ("name", slot.pluginName);
        plugin->setProperty ("file", slot.pluginFile);

        auto entry = juce::DynamicObject::Ptr (new juce::DynamicObject());
        entry->setProperty ("id", slot.id);
        entry->setProperty ("plugin", plugin.get());
        entry->setProperty ("state", juce::Base64::toBase64 (slot.state.getData(), slot.state.getSize()));
        entry->setProperty ("bypass", slot.bypass);
        slots.add (entry.get());
    }

    auto root = juce::DynamicObject::Ptr (new juce::DynamicObject());
    root->setProperty ("version", currentVersion);
    root->setProperty ("name", name);
    root->setProperty ("chain", slots);
    return root.get();
}

juce::Result Preset::fromVar (const juce::var& json, Preset& result)
{
    if (! json.isObject())
        return juce::Result::fail ("El preset no es un objeto JSON");

    const int version = json.getProperty ("version", 0);

    if (version < 1 || version > currentVersion)
        return juce::Result::fail (L"Versión de preset no soportada: " + juce::String (version));

    Preset preset;
    preset.name = json.getProperty ("name", {}).toString();

    if (const auto* slots = json.getProperty ("chain", {}).getArray())
    {
        for (const auto& entry : *slots)
        {
            const auto plugin = entry.getProperty ("plugin", {});

            SlotPreset slot;
            slot.id               = entry.getProperty ("id", {}).toString();
            slot.pluginFormat     = plugin.getProperty ("format", "VST3").toString();
            slot.pluginIdentifier = plugin.getProperty ("uid", {}).toString();
            slot.pluginName       = plugin.getProperty ("name", {}).toString();
            slot.pluginFile       = plugin.getProperty ("file", {}).toString();
            slot.bypass           = entry.getProperty ("bypass", false);

            juce::MemoryOutputStream state;

            if (! juce::Base64::convertFromBase64 (state, entry.getProperty ("state", {}).toString()))
                return juce::Result::fail (L"Estado inválido en el slot " + slot.id);

            slot.state = state.getMemoryBlock();
            preset.chain.push_back (std::move (slot));
        }
    }

    result = std::move (preset);
    return juce::Result::ok();
}

juce::Result Preset::save (const juce::File& file) const
{
    if (auto r = file.getParentDirectory().createDirectory(); r.failed())
        return r;

    // Escribe en un temporal y lo renombra: un corte a mitad no deja el preset roto.
    juce::TemporaryFile temp (file);
    const auto text = juce::JSON::toString (toVar(), juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine));

    if (! temp.getFile().replaceWithText (text) || ! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("No se pudo escribir " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result Preset::load (const juce::File& file, Preset& result)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("No existe " + file.getFullPathName());

    juce::var json;

    if (auto r = juce::JSON::parse (file.loadFileAsString(), json); r.failed())
        return juce::Result::fail (file.getFileName() + ": " + r.getErrorMessage());

    return fromVar (json, result);
}
