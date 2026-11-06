#include "Pedalboard.h"

class Pedalboard::Tile final : public juce::Component
{
public:
    Tile (Pedalboard& owner, int slotIndex)
        : board (owner), index (slotIndex)
    {
        const auto& entry = board.rig[index];
        const bool missing = entry.missing.has_value();
        bypassed = entry.slot->bypassed;
        colour = missing ? juce::Colour (0xff5a2a2a) : bypassed ? juce::Colour (0xff3a3f44) : juce::Colour (0xff2f4f6f);

        nameButton.setButtonText (entry.getName());
        nameButton.setTooltip ("Abrir el editor del plugin");
        nameButton.setEnabled (! missing);
        nameButton.onClick = [this] { board.onOpenEditor (index); };

        bypassButton.setButtonText (bypassed ? "BYPASS" : "ON");
        bypassButton.setColour (juce::TextButton::buttonColourId, bypassed ? juce::Colours::darkgrey : juce::Colours::darkgreen);
        bypassButton.onClick = [this] { board.rig.setBypassed (index, ! bypassed); };

        leftButton.onClick   = [this] { board.rig.move (index, index - 1); };
        rightButton.onClick  = [this] { board.rig.move (index, index + 1); };
        removeButton.onClick = [this] { board.rig.remove (index); };
        leftButton.setEnabled (index > 0);
        rightButton.setEnabled (index < board.rig.size() - 1);
        removeButton.setTooltip ("Quitar de la cadena");

        for (auto* c : { &nameButton, &bypassButton, &leftButton, &rightButton, &removeButton })
            addAndMakeVisible (c);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (12.0f);
        g.drawText (juce::String (index + 1), getLocalBounds().reduced (8, 6), juce::Justification::topLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (14);
        nameButton.setBounds (area.removeFromTop (44));
        area.removeFromTop (8);
        bypassButton.setBounds (area.removeFromTop (30));
        area.removeFromTop (8);

        auto row = area.removeFromTop (26);
        const int w = row.getWidth() / 3;
        leftButton.setBounds (row.removeFromLeft (w).reduced (2, 0));
        removeButton.setBounds (row.removeFromRight (w).reduced (2, 0));
        rightButton.setBounds (row.reduced (2, 0));
    }

private:
    Pedalboard& board;
    const int index;
    bool bypassed = false;
    juce::Colour colour;

    juce::TextButton nameButton, bypassButton;
    juce::TextButton leftButton { "<" }, rightButton { ">" }, removeButton { "X" };
};

//==============================================================================
Pedalboard::Pedalboard (Rig& r)
    : rig (r)
{
    addButton.setTooltip ("Agregar un plugin al final de la cadena");
    addButton.onClick = [this] { onAddPlugin (-1); };
    addAndMakeVisible (addButton);
    handleAsyncUpdate();
}

Pedalboard::~Pedalboard() = default;

void Pedalboard::refresh()
{
    // Diferido: el clic que cambió el Rig puede venir de un botón de estos recuadros.
    triggerAsyncUpdate();
}

void Pedalboard::handleAsyncUpdate()
{
    tiles.clear();

    for (int i = 0; i < rig.size(); ++i)
        addAndMakeVisible (tiles.add (new Tile (*this, i)));

    setSize ((tiles.size() + 1) * (tileWidth + gap) + gap, tileHeight + 2 * gap);
    resized();
}

void Pedalboard::resized()
{
    auto area = getLocalBounds().reduced (gap);

    for (auto* tile : tiles)
    {
        tile->setBounds (area.removeFromLeft (tileWidth).withHeight (tileHeight));
        area.removeFromLeft (gap);
    }

    addButton.setBounds (area.removeFromLeft (tileWidth / 2).withHeight (tileHeight));
}
