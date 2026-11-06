#pragma once

#include "../core/Rig.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Fila de pedales: un recuadro por slot de la cadena y un "+" al final.
// Va dentro de un Viewport; su ancho crece con la cantidad de slots.
class Pedalboard final : public juce::Component,
                         private juce::AsyncUpdater
{
public:
    explicit Pedalboard (Rig&);
    ~Pedalboard() override;

    void refresh();   // tras cada cambio del Rig

    std::function<void (int index)> onOpenEditor;
    std::function<void (int insertIndex)> onAddPlugin;

    void resized() override;

    static constexpr int tileWidth = 180, tileHeight = 150, gap = 12;

private:
    class Tile;

    void handleAsyncUpdate() override;

    Rig& rig;
    juce::OwnedArray<Tile> tiles;
    juce::TextButton addButton { "+" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pedalboard)
};
