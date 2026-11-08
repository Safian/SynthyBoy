// SynthyBoy - UI: top bar, macro row, keyboard strip.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Strip.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::ui
{
// ---- StereoMeter ------------------------------------------------------------------------------------------------------------------

StereoMeter::StereoMeter (UiContext& context) : ctx (context)
{
    startTimerHz (30);
}

StereoMeter::~StereoMeter()
{
    stopTimer();
}

void StereoMeter::refresh()
{
    const float pL = ctx.host.outputPeakL();
    const float pR = ctx.host.outputPeakR();
    levelL = juce::jmax (pL, levelL * 0.82f);
    levelR = juce::jmax (pR, levelR * 0.82f);
    repaint();
}

void StereoMeter::timerCallback()
{
    refresh();
}

void StereoMeter::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const auto r = getLocalBounds().toFloat().reduced (t.px (2.0f));
    const float gap = t.px (2.0f);
    const float chW = juce::jmax (2.0f, (r.getWidth() - gap) * 0.5f);

    const float dbL = juce::Decibels::gainToDecibels (levelL, -60.0f);
    const float dbR = juce::Decibels::gainToDecibels (levelR, -60.0f);

    const float normL = juce::jlimit (0.0f, 1.0f, (dbL + 60.0f) / 66.0f);
    const float normR = juce::jlimit (0.0f, 1.0f, (dbR + 60.0f) / 66.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = r.getX() + static_cast<float> (ch) * (chW + gap);
        const juce::Rectangle<float> track (x, r.getY(), chW, r.getHeight());
        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (track, t.px (2.0f));

        const float norm = ch == 0 ? normL : normR;
        if (norm > 0.001f)
        {
            const float barH = track.getHeight() * norm;
            const juce::Rectangle<float> bar (x, track.getBottom() - barH, chW, barH);
            const juce::Colour barCol = norm > 0.91f ? c.danger
                                      : (norm > 0.82f ? c.warn : c.accent);
            g.setColour (barCol);
            g.fillRoundedRectangle (bar, t.px (2.0f));
        }
    }
}

// ---- TopBar -----------------------------------------------------------------------------------------------------------------------

TopBar::TopBar (UiContext& context) : ctx (context)
{
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    undo.setTooltip ("Undo (Cmd/Ctrl+Z)");
    redo.setTooltip ("Redo (Cmd/Ctrl+Shift+Z)");
    ab.setTooltip ("A/B compare: swap with the stored other state");
    assign.setTooltip ("Modulation sources: tap a source, then drag a knob");
    assign.setClickingTogglesState (true);
    menu.setTooltip ("Menu");

    prev.onClick = [this] { ctx.host.stepPreset (-1); };
    next.onClick = [this] { ctx.host.stepPreset (+1); };
    undo.onClick = [this] { if (onUndo) onUndo(); };
    redo.onClick = [this] { if (onRedo) onRedo(); };
    ab.onClick = [this] { if (onAbToggle) onAbToggle(); };
    assign.onClick = [this] { if (onAssignToggle) onAssignToggle(); };
    menu.onClick = [this] { if (onMenu) onMenu(); };

    undo.setEnabled (false);
    redo.setEnabled (false);

    for (auto* b : { &prev, &next, &undo, &redo, &ab, &assign, &menu })
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

void TopBar::updateUndoRedo (bool canUndo, bool canRedo)
{
    undo.setEnabled (canUndo);
    redo.setEnabled (canRedo);
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
    redo.setBounds (r.removeFromRight (h));
    undo.setBounds (r.removeFromRight (h));
    next.setBounds (r.removeFromRight (h));
}

void TopBar::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const int h = t.touch();
    auto nameArea = juce::Rectangle<int> (prev.getRight(), 0, juce::jmax (0, next.getX() - prev.getRight()), getHeight());
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
        macroStrip.addAndMakeVisible (*knob);
        macros.push_back (std::move (knob));
    }

    viewport.setViewedComponent (&macroStrip, false);
    viewport.setScrollBarsShown (false, false, false, true);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    addAndMakeVisible (viewport);

    mainKnob = std::make_unique<ParamKnob> (ctx, "master.volume", "MAIN");
    mainKnob->setCompact (true);
    addAndMakeVisible (*mainKnob);

    meter = std::make_unique<StereoMeter> (ctx);
    addAndMakeVisible (*meter);
}

void MacroRow::resized()
{
    auto r = getLocalBounds();
    const int h = getHeight();
    const int touch = ctx.theme.touch();

    const int meterW = ctx.theme.ipx (14.0f);
    meter->setBounds (r.removeFromRight (meterW).reduced (0, ctx.theme.gap()));

    const int mainW = juce::jmax (touch, ctx.theme.ipx (52.0f));
    mainKnob->setBounds (r.removeFromRight (mainW));

    viewport.setBounds (r);
    const int n = static_cast<int> (macros.size());
    const int idealW = r.getWidth() / n;
    const int actualW = juce::jmax (touch, idealW);
    macroStrip.setSize (actualW * n, h);
    for (int i = 0; i < n; ++i)
        macros[static_cast<size_t> (i)]->setBounds (i * actualW, 0, actualW, h);
}

// ---- PitchWheel -------------------------------------------------------------------------------------------------------------------

PitchWheel::PitchWheel (UiContext& context) : ctx (context)
{
    setTooltip ("Pitch bend wheel (spring to centre, range: master.bend_range)");
}

void PitchWheel::setValue (float v)
{
    pos = juce::jlimit (-1.0f, 1.0f, v);
    sendPitchBend (pos);
    repaint();
}

void PitchWheel::resetToCenter()
{
    pos = 0.0f;
    sendPitchBend (0.0f);
    repaint();
}

void PitchWheel::sendPitchBend (float v)
{
    const int pb = juce::jlimit (0, 16383, 8192 + juce::roundToInt (v * 8191.0f));
    ctx.host.injectMidiMessage (juce::MidiMessage::pitchWheel (1, pb));
}

void PitchWheel::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const auto r = getLocalBounds().toFloat().reduced (t.px (2.0f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (r, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (r, t.px (6.0f), 1.0f);

    const float midY = r.getCentreY();
    g.setColour (c.outline.brighter (0.2f));
    g.drawHorizontalLine (juce::roundToInt (midY), r.getX() + 2.0f, r.getRight() - 2.0f);

    if (std::abs (pos) > 0.005f)
    {
        const float barH = (r.getHeight() * 0.44f) * std::abs (pos);
        const float barY = pos > 0.0f ? (midY - barH) : midY;
        const juce::Rectangle<float> bar (r.getX() + 3.0f, barY, r.getWidth() - 6.0f, barH);
        g.setColour (c.accent.withAlpha (0.7f));
        g.fillRoundedRectangle (bar, t.px (3.0f));
    }

    const float thumbH = t.px (10.0f);
    const float thumbY = juce::jlimit (r.getY() + 1.0f, r.getBottom() - thumbH - 1.0f, midY - (pos * (r.getHeight() * 0.44f)) - thumbH * 0.5f);
    const juce::Rectangle<float> thumb (r.getX() + 2.0f, thumbY, r.getWidth() - 4.0f, thumbH);
    g.setColour (c.text);
    g.fillRoundedRectangle (thumb, t.px (3.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (thumb, t.px (3.0f), 1.0f);

    g.setColour (c.textDim);
    g.setFont (t.font (10.0f, true));
    g.drawText ("P", r.withTrimmedTop (r.getHeight() - 14.0f).toNearestInt(), juce::Justification::centred, false);
}

void PitchWheel::mouseDown (const juce::MouseEvent& e)
{
    dragStartPos = pos;
    mouseDownPos = e.position;
}

void PitchWheel::mouseDrag (const juce::MouseEvent& e)
{
    const float dy = mouseDownPos.y - e.position.y;
    const float rangePx = juce::jmax (30.0f, static_cast<float> (getHeight()) * 0.45f);
    pos = juce::jlimit (-1.0f, 1.0f, dragStartPos + dy / rangePx);
    sendPitchBend (pos);
    repaint();
}

void PitchWheel::mouseUp (const juce::MouseEvent&)
{
    resetToCenter();
}

void PitchWheel::mouseDoubleClick (const juce::MouseEvent&)
{
    resetToCenter();
}

// ---- ModWheel ---------------------------------------------------------------------------------------------------------------------

ModWheel::ModWheel (UiContext& context) : ctx (context)
{
    setTooltip ("Modulation wheel (CC 1)");
}

void ModWheel::setValue (float v)
{
    pos = juce::jlimit (0.0f, 1.0f, v);
    sendModWheel (pos);
    repaint();
}

void ModWheel::resetToZero()
{
    pos = 0.0f;
    sendModWheel (0.0f);
    repaint();
}

void ModWheel::sendModWheel (float v)
{
    const int val = juce::jlimit (0, 127, juce::roundToInt (v * 127.0f));
    ctx.host.injectMidiMessage (juce::MidiMessage::controllerEvent (1, 1, val));
}

void ModWheel::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const auto r = getLocalBounds().toFloat().reduced (t.px (2.0f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (r, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (r, t.px (6.0f), 1.0f);

    if (pos > 0.005f)
    {
        const float barH = (r.getHeight() - 8.0f) * pos;
        const juce::Rectangle<float> bar (r.getX() + 3.0f, r.getBottom() - 4.0f - barH, r.getWidth() - 6.0f, barH);
        g.setColour (c.mod.withAlpha (0.7f));
        g.fillRoundedRectangle (bar, t.px (3.0f));
    }

    const float thumbH = t.px (10.0f);
    const float thumbY = juce::jlimit (r.getY() + 2.0f, r.getBottom() - thumbH - 2.0f, r.getBottom() - 4.0f - (pos * (r.getHeight() - 14.0f)) - thumbH);
    const juce::Rectangle<float> thumb (r.getX() + 2.0f, thumbY, r.getWidth() - 4.0f, thumbH);
    g.setColour (c.text);
    g.fillRoundedRectangle (thumb, t.px (3.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (thumb, t.px (3.0f), 1.0f);

    g.setColour (c.textDim);
    g.setFont (t.font (10.0f, true));
    g.drawText ("M", r.withTrimmedTop (r.getHeight() - 14.0f).toNearestInt(), juce::Justification::centred, false);
}

void ModWheel::mouseDown (const juce::MouseEvent& e)
{
    dragStartPos = pos;
    mouseDownPos = e.position;
}

void ModWheel::mouseDrag (const juce::MouseEvent& e)
{
    const float dy = mouseDownPos.y - e.position.y;
    const float rangePx = juce::jmax (30.0f, static_cast<float> (getHeight()) - 16.0f);
    pos = juce::jlimit (0.0f, 1.0f, dragStartPos + dy / rangePx);
    sendModWheel (pos);
    repaint();
}

void ModWheel::mouseDoubleClick (const juce::MouseEvent&)
{
    resetToZero();
}

// ---- LatchingKeyboard -------------------------------------------------------------------------------------------------------------

class LatchingKeyboard final : public juce::MidiKeyboardComponent
{
public:
    LatchingKeyboard (juce::MidiKeyboardState& state, UiContext& context)
        : juce::MidiKeyboardComponent (state, juce::MidiKeyboardComponent::horizontalKeyboard),
          ctx (context),
          keyState (state)
    {
    }

    void setHoldActive (bool active)
    {
        holdActive = active;
        if (! active)
            releaseHeldNotes();
    }

    bool isHoldActive() const noexcept { return holdActive; }

    void releaseHeldNotes()
    {
        for (const int note : heldNotes)
            keyState.noteOff (1, note, 0.0f);
        heldNotes.clear();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (holdActive)
        {
            const int hitNote = getNoteAndVelocityAtPosition (e.position).note;

            if (hitNote >= 0)
            {
                auto it = std::find (heldNotes.begin(), heldNotes.end(), hitNote);
                if (it != heldNotes.end())
                {
                    keyState.noteOff (1, hitNote, 0.0f);
                    heldNotes.erase (it);
                }
                else
                {
                    keyState.noteOn (1, hitNote, 0.8f);
                    heldNotes.push_back (hitNote);
                }
                return;
            }
        }
        juce::MidiKeyboardComponent::mouseDown (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (holdActive)
            return;
        juce::MidiKeyboardComponent::mouseUp (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (holdActive)
            return;
        juce::MidiKeyboardComponent::mouseDrag (e);
    }

    void drawWhiteNote (int midiNote, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override
    {
        juce::MidiKeyboardComponent::drawWhiteNote (midiNote, g, area, isDown, isOver, lineColour, textColour);
        drawScaleOverlay (midiNote, g, area);
    }

    void drawBlackNote (int midiNote, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour fillColour) override
    {
        juce::MidiKeyboardComponent::drawBlackNote (midiNote, g, area, isDown, isOver, fillColour);
        drawScaleOverlay (midiNote, g, area);
    }

private:
    void drawScaleOverlay (int midiNote, juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto* kRaw = ctx.apvts().getRawParameterValue ("master.scale.key");
        const auto* tRaw = ctx.apvts().getRawParameterValue ("master.scale.type");
        if (kRaw == nullptr || tRaw == nullptr)
            return;

        const int scaleKey = juce::roundToInt (kRaw->load());
        const int scaleType = juce::roundToInt (tRaw->load());
        if (scaleType <= 0)
            return;

        if (ScaleInfo::isRoot (scaleKey, scaleType, midiNote))
        {
            g.setColour (ctx.theme.colours.accent.withAlpha (0.42f));
            g.fillRect (area.reduced (1.0f));
        }
        else if (ScaleInfo::inScale (scaleKey, scaleType, midiNote))
        {
            g.setColour (ctx.theme.colours.accent.withAlpha (0.16f));
            g.fillRect (area.reduced (1.0f));
        }
    }

    UiContext& ctx;
    juce::MidiKeyboardState& keyState;
    bool holdActive = false;
    std::vector<int> heldNotes;
};

// ---- KeyboardStrip ----------------------------------------------------------------------------------------------------------------

KeyboardStrip::KeyboardStrip (UiContext& context) : ctx (context)
{
    pitchWheel = std::make_unique<PitchWheel> (ctx);
    modWheel = std::make_unique<ModWheel> (ctx);
    keys = std::make_unique<LatchingKeyboard> (ctx.host.keyboardState(), ctx);

    const Palette& c = ctx.theme.colours;
    keys->setColour (juce::MidiKeyboardComponent::whiteNoteColourId, c.keyWhite);
    keys->setColour (juce::MidiKeyboardComponent::blackNoteColourId, c.keyBlack);
    keys->setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, c.outline);
    keys->setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, c.keyDown.withAlpha (0.8f));
    keys->setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, c.keyDown.withAlpha (0.25f));
    keys->setColour (juce::MidiKeyboardComponent::shadowColourId, c.outline);
    keys->setScrollButtonWidth (0);
    keys->setVelocity (0.8f, true);
    keys->setLowestVisibleKey (48);
    keys->setWantsKeyboardFocus (false);

    octDown.setTooltip ("Keys one octave down");
    octUp.setTooltip ("Keys one octave up");
    octDown.onClick = [this] { shiftOctave (-1); };
    octUp.onClick = [this] { shiftOctave (+1); };

    transposeHandle = std::make_unique<ParamHandle> (ctx, "master.transpose");
    scaleKeyHandle = std::make_unique<ParamHandle> (ctx, "master.scale.key");
    scaleTypeHandle = std::make_unique<ParamHandle> (ctx, "master.scale.type");
    scaleModeHandle = std::make_unique<ParamHandle> (ctx, "master.scale.mode");

    transposeDown.setTooltip ("Transpose -1 semitone");
    transposeUp.setTooltip ("Transpose +1 semitone");
    transposeValue.setTooltip ("Transpose (semitones, double click to reset to 0)");

    transposeDown.onClick = [this]
    {
        if (transposeHandle->valid())
            transposeHandle->setPlainComplete (juce::jlimit (-48.0f, 48.0f, transposeHandle->plain() - 1.0f));
    };
    transposeUp.onClick = [this]
    {
        if (transposeHandle->valid())
            transposeHandle->setPlainComplete (juce::jlimit (-48.0f, 48.0f, transposeHandle->plain() + 1.0f));
    };
    transposeValue.onClick = [this]
    {
        if (transposeHandle->valid())
            transposeHandle->resetToDefault();
    };

    transposeHandle->onChange = [this] { updateTransposeButton(); };
    updateTransposeButton();

    holdButton.setTooltip ("Hold notes: tap a key to latch, tap again to release");
    holdButton.setClickingTogglesState (true);
    holdButton.onClick = [this]
    {
        const bool active = holdButton.getToggleState();
        keys->setHoldActive (active);
    };

    scaleButton.setTooltip ("Key and scale setup");
    scaleButton.onClick = [this] { showScaleMenu(); };

    scaleKeyHandle->onChange = [this] { updateScaleButton(); keys->repaint(); };
    scaleTypeHandle->onChange = [this] { updateScaleButton(); keys->repaint(); };
    scaleModeHandle->onChange = [this] { keys->repaint(); };
    updateScaleButton();

    addAndMakeVisible (*pitchWheel);
    addAndMakeVisible (*modWheel);
    addAndMakeVisible (*keys);

    for (auto* b : { &octDown, &octUp, &transposeDown, &transposeValue, &transposeUp, &holdButton, &scaleButton })
        addAndMakeVisible (*b);
}

KeyboardStrip::~KeyboardStrip() = default;

juce::MidiKeyboardComponent& KeyboardStrip::keyboard() noexcept
{
    return *keys;
}

void KeyboardStrip::shiftOctave (int octaves)
{
    keys->setLowestVisibleKey (juce::jlimit (0, 96, static_cast<int> (keys->getLowestVisibleKey()) + 12 * octaves));
}

void KeyboardStrip::updateTransposeButton()
{
    if (transposeHandle->valid())
    {
        const int val = juce::roundToInt (transposeHandle->plain());
        transposeValue.setButtonText ((val > 0 ? "+" : "") + juce::String (val));
    }
}

void KeyboardStrip::updateScaleButton()
{
    if (scaleTypeHandle->valid() && scaleKeyHandle->valid())
    {
        const int t = juce::roundToInt (scaleTypeHandle->plain());
        const int k = juce::roundToInt (scaleKeyHandle->plain());
        if (t == 0)
            scaleButton.setButtonText ("SCALE");
        else
            scaleButton.setButtonText (juce::String (ScaleInfo::keyName (k)) + " " + juce::String (ScaleInfo::scaleName (t)));
    }
}

void KeyboardStrip::showScaleMenu()
{
    juce::PopupMenu menu;

    juce::PopupMenu keyMenu;
    const int currentKey = scaleKeyHandle->valid() ? juce::roundToInt (scaleKeyHandle->plain()) : 0;
    for (int i = 0; i < ScaleInfo::kNumKeys; ++i)
        keyMenu.addItem (100 + i, ScaleInfo::keyName (i), true, i == currentKey);
    menu.addSubMenu ("Root Key", keyMenu);

    juce::PopupMenu scaleMenu;
    const int currentScale = scaleTypeHandle->valid() ? juce::roundToInt (scaleTypeHandle->plain()) : 0;
    for (int i = 0; i < ScaleInfo::kNumScales; ++i)
        scaleMenu.addItem (200 + i, ScaleInfo::scaleName (i), true, i == currentScale);
    menu.addSubMenu ("Scale", scaleMenu);

    juce::PopupMenu modeMenu;
    const int currentMode = scaleModeHandle->valid() ? juce::roundToInt (scaleModeHandle->plain()) : 0;
    modeMenu.addItem (300, "Conform", true, currentMode == 0);
    modeMenu.addItem (301, "Mark only", true, currentMode == 1);
    menu.addSubMenu ("Mode", modeMenu);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleButton),
                        [safe = juce::Component::SafePointer<KeyboardStrip> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result >= 100 && result < 200)
                                safe->scaleKeyHandle->setPlainComplete (static_cast<float> (result - 100));
                            else if (result >= 200 && result < 300)
                                safe->scaleTypeHandle->setPlainComplete (static_cast<float> (result - 200));
                            else if (result >= 300 && result < 400)
                                safe->scaleModeHandle->setPlainComplete (static_cast<float> (result - 300));
                        });
}

void KeyboardStrip::resized()
{
    auto r = getLocalBounds();
    const auto& t = ctx.theme;
    const int touch = t.touch();
    const bool tall = getHeight() >= 96;

    if (tall)
    {
        // Header sub-row at the top of the strip
        auto topRow = r.removeFromTop (touch);
        octDown.setBounds (topRow.removeFromLeft (touch));
        octUp.setBounds (topRow.removeFromLeft (touch));

        transposeDown.setBounds (topRow.removeFromLeft (touch));
        transposeValue.setBounds (topRow.removeFromLeft (juce::jmax (touch, t.ipx (44.0f))));
        transposeUp.setBounds (topRow.removeFromLeft (touch));

        scaleButton.setBounds (topRow.removeFromRight (juce::jmax (touch, t.ipx (96.0f))));
        holdButton.setBounds (topRow.removeFromRight (juce::jmax (touch, t.ipx (56.0f))));

        // Bottom row: pitch and mod wheels on the left, then keys
        pitchWheel->setBounds (r.removeFromLeft (touch));
        modWheel->setBounds (r.removeFromLeft (touch));

        const auto mode = layoutModeForWidth (getWidth());
        const int whiteKeys = mode == LayoutMode::phone ? 10 : (mode == LayoutMode::tablet ? 14 : 21);
        keys->setBounds (r);
        keys->setKeyWidth (static_cast<float> (r.getWidth()) / static_cast<float> (whiteKeys));
    }
    else
    {
        // Compact single row
        pitchWheel->setBounds (r.removeFromLeft (touch));
        modWheel->setBounds (r.removeFromLeft (touch));

        scaleButton.setBounds (r.removeFromRight (juce::jmax (touch, t.ipx (72.0f))));
        holdButton.setBounds (r.removeFromRight (touch));
        transposeValue.setBounds (r.removeFromRight (touch));
        octUp.setBounds (r.removeFromRight (touch));
        octDown.setBounds (r.removeFromRight (touch));

        transposeDown.setBounds (0, 0, 0, 0);
        transposeUp.setBounds (0, 0, 0, 0);

        const auto mode = layoutModeForWidth (getWidth());
        const int whiteKeys = mode == LayoutMode::phone ? 10 : (mode == LayoutMode::tablet ? 14 : 21);
        keys->setBounds (r);
        keys->setKeyWidth (static_cast<float> (r.getWidth()) / static_cast<float> (whiteKeys));
    }
}
} // namespace synthyboy::ui
