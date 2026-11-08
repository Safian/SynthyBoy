// SynthyBoy - UI: top bar, macro row, keyboard strip.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Strip.h"

namespace synthyboy::ui
{
// ---- TopBar -----------------------------------------------------------------------------------------------------------------------

TopBar::TopBar (UiContext& context) : ctx (context)
{
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    ab.setTooltip ("A/B compare: swap with the stored other state");
    assign.setTooltip ("Modulation sources: tap a source, then drag a knob");
    assign.setClickingTogglesState (true);
    menu.setTooltip ("Menu");

    prev.onClick = [this] { ctx.host.stepPreset (-1); };
    next.onClick = [this] { ctx.host.stepPreset (+1); };
    ab.onClick = [this] { if (onAbToggle) onAbToggle(); };
    assign.onClick = [this] { if (onAssignToggle) onAssignToggle(); };
    menu.onClick = [this] { if (onMenu) onMenu(); };

    for (auto* b : { &prev, &next, &ab, &assign, &menu })
        addAndMakeVisible (*b);
}

void TopBar::setPresetName (const juce::String& name)
{
    if (name != presetName)
    {
        presetName = name;
        repaint();
    }
}

void TopBar::resized()
{
    const int h = ctx.theme.touch();
    auto r = getLocalBounds();
    r = r.withSizeKeepingCentre (r.getWidth(), juce::jmax (h, r.getHeight()));
    prev.setBounds (r.removeFromLeft (h));
    menu.setBounds (r.removeFromRight (h));
    assign.setBounds (r.removeFromRight (juce::jmax (h, ctx.theme.ipx (52.0f))));
    ab.setBounds (r.removeFromRight (h));
    next.setBounds (r.removeFromRight (h));
}

void TopBar::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const int h = t.touch();
    auto nameArea = juce::Rectangle<int> (prev.getRight(), 0, next.getX() - prev.getRight(), getHeight());
    g.setColour (t.colours.text);
    g.setFont (t.titleFont());
    g.drawFittedText (presetName, nameArea.withHeight (juce::jmax (h, getHeight())), juce::Justification::centred, 1);
}

// ---- MacroRow ---------------------------------------------------------------------------------------------------------------------

MacroRow::MacroRow (UiContext& context) : ctx (context)
{
    for (int n = 1; n <= 8; ++n)
    {
        auto knob = std::make_unique<ParamKnob> (ctx, macroKey (n), "M" + juce::String (n));
        knob->setCompact (true);
        addAndMakeVisible (*knob);
        macros.push_back (std::move (knob));
    }
}

void MacroRow::resized()
{
    const int n = static_cast<int> (macros.size());
    const int w = getWidth() / n;
    for (int i = 0; i < n; ++i)
        macros[static_cast<size_t> (i)]->setBounds (i * w, 0, w, getHeight());
}

// ---- KeyboardStrip ----------------------------------------------------------------------------------------------------------------

KeyboardStrip::KeyboardStrip (UiContext& context) : ctx (context)
{
    keys = std::make_unique<juce::MidiKeyboardComponent> (ctx.host.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard);
    const Palette& c = ctx.theme.colours;
    keys->setColour (juce::MidiKeyboardComponent::whiteNoteColourId, c.keyWhite);
    keys->setColour (juce::MidiKeyboardComponent::blackNoteColourId, c.keyBlack);
    keys->setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, c.outline);
    keys->setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, c.keyDown.withAlpha (0.8f));
    keys->setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, c.keyDown.withAlpha (0.25f));
    keys->setColour (juce::MidiKeyboardComponent::shadowColourId, c.outline);
    keys->setScrollButtonWidth (0); // the oct buttons scroll
    keys->setVelocity (0.8f, true); // touch position inside the key sets the velocity: lower = louder
    keys->setLowestVisibleKey (48);
    keys->setWantsKeyboardFocus (false);
    addAndMakeVisible (*keys);

    octDown.setTooltip ("Keys one octave down");
    octUp.setTooltip ("Keys one octave up");
    octDown.onClick = [this] { shiftOctave (-1); };
    octUp.onClick = [this] { shiftOctave (+1); };
    addAndMakeVisible (octDown);
    addAndMakeVisible (octUp);
}

void KeyboardStrip::shiftOctave (int octaves)
{
    keys->setLowestVisibleKey (juce::jlimit (0, 96, static_cast<int> (keys->getLowestVisibleKey()) + 12 * octaves));
}

void KeyboardStrip::resized()
{
    auto r = getLocalBounds();
    const int bw = ctx.theme.touch();
    auto left = r.removeFromLeft (bw);
    octUp.setBounds (left.removeFromTop (left.getHeight() / 2));
    octDown.setBounds (left);

    const auto mode = layoutModeForWidth (getWidth());
    const int whiteKeys = mode == LayoutMode::phone ? 10 : (mode == LayoutMode::tablet ? 14 : 21);
    keys->setBounds (r);
    keys->setKeyWidth (static_cast<float> (r.getWidth()) / static_cast<float> (whiteKeys));
}
} // namespace synthyboy::ui
