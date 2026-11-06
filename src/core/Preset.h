#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// Un preset: la cadena de plugins con el estado de cada uno, en JSON legible.
// Formato: ver docs/PROPUESTA.md (§ Modelo de datos). Snapshots y MIDI llegan en la Fase 2.
struct SlotPreset
{
    juce::String id;                 // "s1", "s2", ... único dentro del preset
    juce::String pluginName;
    juce::String pluginFormat;       // "VST3"
    juce::String pluginIdentifier;   // PluginDescription::createIdentifierString()
    juce::String pluginFile;         // ruta del bundle, para encontrarlo si el identificador cambia
    juce::MemoryBlock state;         // getStateInformation() del plugin
    bool bypass = false;

    bool operator== (const SlotPreset&) const = default;
};

struct Preset
{
    static constexpr int currentVersion = 1;

    juce::String name;
    std::vector<SlotPreset> chain;

    juce::var toVar() const;
    static juce::Result fromVar (const juce::var& json, Preset& result);

    juce::Result save (const juce::File& file) const;
    static juce::Result load (const juce::File& file, Preset& result);

    bool operator== (const Preset&) const = default;
};
