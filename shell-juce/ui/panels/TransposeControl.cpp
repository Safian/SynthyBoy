// SynthyBoy - UI: OCT segments + SEM wheel (see TransposeControl.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TransposeControl.h"

#include <cmath>

#include "../model/GestureModel.h"

namespace synthyboy::ui
{
namespace
{
constexpr float kPixelsPerSemitone = 14.0f;
}

TransposeControl::TransposeControl (UiContext& context, const std::string& key, const juce::String& labelOverride)
    : ParamControl (context, key, labelOverride)
{
    setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
}

juce::Point<int> TransposeControl::preferredSize() const
{
    const auto& t = ctx.theme;
    return { kNumOctSegments * t.touch() + (kNumOctSegments - 1) * t.gap() / 2, t.ipx (16.0f) + 2 * t.touch() + t.gap() / 2 };
}

int TransposeControl::octave() const
{
    return handle.valid() ? splitTranspose (juce::roundToInt (handle.plain())).oct : 0;
}

int TransposeControl::semitone() const
{
    return handle.valid() ? splitTranspose (juce::roundToInt (handle.plain())).sem : 0;
}

juce::Rectangle<int> TransposeControl::octBounds (int segment) const
{
    const auto& t = ctx.theme;
    const auto row = getLocalBounds().withTrimmedTop (t.ipx (16.0f)).withHeight (t.touch());
    const int w = row.getWidth() / kNumOctSegments;
    return { row.getX() + segment * w, row.getY(), segment == kNumOctSegments - 1 ? row.getRight() - (row.getX() + segment * w) : w, row.getHeight() };
}

juce::Rectangle<int> TransposeControl::semMinusBounds() const
{
    const auto& t = ctx.theme;
    return { 0, getHeight() - t.touch(), t.touch(), t.touch() };
}

juce::Rectangle<int> TransposeControl::semPlusBounds() const
{
    const auto& t = ctx.theme;
    return { getWidth() - t.touch(), getHeight() - t.touch(), t.touch(), t.touch() };
}

juce::Rectangle<int> TransposeControl::semWheelBounds() const
{
    return { semMinusBounds().getRight(), getHeight() - ctx.theme.touch(), semPlusBounds().getX() - semMinusBounds().getRight(), ctx.theme.touch() };
}

void TransposeControl::setOctave (int oct)
{
    if (handle.valid())
        handle.setPlainComplete (static_cast<float> (joinTranspose (oct, semitone())));
}

void TransposeControl::stepSemitone (int delta)
{
    if (handle.valid())
        handle.setPlainComplete (static_cast<float> (joinTranspose (0, juce::roundToInt (handle.plain()) + delta)));
}

TransposeControl::Hit TransposeControl::hitAt (juce::Point<int> p, int& octSegment) const
{
    for (int i = 0; i < kNumOctSegments; ++i)
        if (octBounds (i).contains (p))
        {
            octSegment = i - 2;
            return Hit::oct;
        }
    if (semMinusBounds().contains (p))
        return Hit::minus;
    if (semPlusBounds().contains (p))
        return Hit::plus;
    if (semWheelBounds().contains (p))
        return Hit::wheel;
    return Hit::none;
}

void TransposeControl::paint (juce::Graphics& g)
{
    if (! handle.valid())
    {
        paintUnknownKey (g);
        return;
    }
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const int total = juce::roundToInt (handle.plain());
    const auto split = splitTranspose (total);

    g.setFont (t.labelFont());
    g.setColour (c.textDim);
    g.drawFittedText ("OCT / SEM   " + juce::String (total >= 0 ? "+" : "") + juce::String (total) + " st", 0, 0, getWidth(), t.ipx (16.0f),
                      juce::Justification::centredLeft, 1);

    for (int i = 0; i < kNumOctSegments; ++i)
    {
        const int oct = i - 2;
        const auto r = octBounds (i).toFloat().reduced (t.px (1.5f));
        const bool on = oct == split.oct;
        g.setColour (on ? c.accent : c.panelRaised);
        g.fillRoundedRectangle (r, t.px (6.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (r, t.px (6.0f), 1.0f);
        g.setColour (on ? c.textOnAccent : c.text);
        g.setFont (t.valueFont());
        g.drawFittedText ((oct > 0 ? "+" : "") + juce::String (oct), octBounds (i), juce::Justification::centred, 1);
    }

    const auto wheel = semWheelBounds().toFloat().reduced (t.px (1.5f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (wheel, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (wheel, t.px (6.0f), 1.0f);
    g.setColour (c.text);
    g.setFont (t.valueFont());
    g.drawFittedText ("SEM " + juce::String (split.sem >= 0 ? "+" : "") + juce::String (split.sem), wheel.toNearestInt(), juce::Justification::centred, 1);
    for (const bool plus : { false, true })
    {
        const auto b = (plus ? semPlusBounds() : semMinusBounds()).toFloat().reduced (t.px (1.5f));
        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (b, t.px (6.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (b, t.px (6.0f), 1.0f);
        g.setColour (c.text);
        g.setFont (t.titleFont());
        g.drawFittedText (plus ? "+" : "-", b.toNearestInt(), juce::Justification::centred, 1);
    }
}

void TransposeControl::mouseDown (const juce::MouseEvent& e)
{
    if (! handle.valid())
        return;
    if (e.mods.isPopupMenu())
    {
        consumedByMenu = true;
        showSheet();
        return;
    }
    downHit = hitAt (e.getPosition(), downOct);
    downPos = e.position;
    downTranspose = juce::roundToInt (handle.plain());
    dragging = false;
    beginPress();
}

void TransposeControl::mouseDrag (const juce::MouseEvent& e)
{
    if (downHit != Hit::wheel || ! handle.valid() || consumedByMenu)
        return;
    if (! dragging)
    {
        if ((e.position - downPos).getDistanceFromOrigin() <= kLongPressSlopPx)
            return;
        dragging = true;
        cancelLongPress();
        handle.beginGesture();
    }
    const int steps = juce::roundToInt ((downPos.y - e.position.y) / kPixelsPerSemitone); // up = higher
    const float norm = handle.info().max > handle.info().min
                           ? (static_cast<float> (joinTranspose (0, downTranspose + steps)) - handle.info().min) / (handle.info().max - handle.info().min)
                           : 0.0f;
    handle.setNormalised (norm);
}

void TransposeControl::mouseUp (const juce::MouseEvent& e)
{
    cancelLongPress();
    if (dragging)
    {
        dragging = false;
        handle.endGesture();
        downHit = Hit::none;
        return;
    }
    if (consumedByMenu || ! handle.valid())
    {
        downHit = Hit::none;
        return;
    }
    int oct = 0;
    const auto upHit = hitAt (e.getPosition(), oct);
    if (upHit == downHit)
    {
        if (upHit == Hit::oct)
            setOctave (oct);
        else if (upHit == Hit::minus)
            stepSemitone (-1);
        else if (upHit == Hit::plus)
            stepSemitone (+1);
    }
    downHit = Hit::none;
}

// --- TransposeSnapControl (Row 02-10) -------------------------------------------------------------
namespace
{
constexpr const char* kSnapNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr bool isSnapBlackKey (int s) { return s == 1 || s == 3 || s == 6 || s == 8 || s == 10; }
}

TransposeSnapControl::TransposeSnapControl (UiContext& context, const std::string& key, const juce::String& labelOverride)
    : ParamControl (context, key, labelOverride.isNotEmpty() ? labelOverride : juce::String ("TRANSPOSE SNAP"))
{
}

juce::Point<int> TransposeSnapControl::preferredSize() const
{
    const auto& t = ctx.theme;
    return { kNumNotes * t.touch() / 2, t.ipx (16.0f) + 2 * t.touch() + t.gap() };
}

int TransposeSnapControl::rawMask() const
{
    return handle.valid() ? juce::roundToInt (handle.plain()) : 0;
}

void TransposeSnapControl::setMask (int mask)
{
    if (handle.valid())
        handle.setPlainComplete (static_cast<float> (mask));
}

bool TransposeSnapControl::isKeyActive (int semitone) const
{
    return (rawMask() & (1 << semitone)) != 0;
}

bool TransposeSnapControl::isGlobalActive() const
{
    return (rawMask() & kGlobalSnapBit) != 0;
}

void TransposeSnapControl::toggleKey (int semitone)
{
    if (semitone >= 0 && semitone < 12)
        setMask (rawMask() ^ (1 << semitone));
}

void TransposeSnapControl::toggleGlobal()
{
    setMask (rawMask() ^ kGlobalSnapBit);
}

void TransposeSnapControl::clear()
{
    setMask (0);
}

juce::Rectangle<int> TransposeSnapControl::noteBounds (int semitone) const
{
    const auto& t = ctx.theme;
    const auto row = getLocalBounds().withTrimmedTop (t.ipx (16.0f)).withHeight (t.touch());
    const int w = row.getWidth() / kNumNotes;
    return { row.getX() + semitone * w, row.getY(), semitone == kNumNotes - 1 ? row.getRight() - (row.getX() + semitone * w) : w, row.getHeight() };
}

juce::Rectangle<int> TransposeSnapControl::clearBounds() const
{
    const auto& t = ctx.theme;
    const int y = getHeight() - t.touch();
    const int totalW = getWidth();
    return { 0, y, (totalW - t.gap()) / 3, t.touch() };
}

juce::Rectangle<int> TransposeSnapControl::globalBounds() const
{
    const auto& t = ctx.theme;
    const int y = getHeight() - t.touch();
    const int clearW = clearBounds().getWidth();
    return { clearW + t.gap(), y, getWidth() - clearW - t.gap(), t.touch() };
}

TransposeSnapControl::Hit TransposeSnapControl::hitAt (juce::Point<int> p, int& noteIndex) const
{
    for (int i = 0; i < kNumNotes; ++i)
        if (noteBounds (i).contains (p))
        {
            noteIndex = i;
            return Hit::note;
        }
    if (clearBounds().contains (p))
        return Hit::clear;
    if (globalBounds().contains (p))
        return Hit::global;
    return Hit::none;
}

void TransposeSnapControl::paint (juce::Graphics& g)
{
    if (! handle.valid())
    {
        paintUnknownKey (g);
        return;
    }
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const int mask = rawMask();

    g.setFont (t.labelFont());
    g.setColour (c.textDim);
    juce::String status = mask == 0 ? "Off" : (isGlobalActive() ? "Global Snap" : "Custom Scale");
    g.drawFittedText ("TRANSPOSE SNAP   " + status, 0, 0, getWidth(), t.ipx (16.0f), juce::Justification::centredLeft, 1);

    // Purple depth colour c.mod as specified in Vital and row 02-10 ("lit keys purple")
    for (int i = 0; i < kNumNotes; ++i)
    {
        const auto r = noteBounds (i).toFloat().reduced (t.px (1.0f));
        const bool active = isKeyActive (i);
        const bool black = isSnapBlackKey (i);

        g.setColour (active ? c.mod : (black ? c.panel : c.panelRaised));
        g.fillRoundedRectangle (r, t.px (4.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (r, t.px (4.0f), 1.0f);

        g.setColour (active ? c.onMod : (black ? c.textDim : c.text));
        g.setFont (t.valueFont());
        g.drawFittedText (kSnapNoteNames[i], noteBounds (i), juce::Justification::centred, 1);
    }

    // Clear button
    const auto clr = clearBounds().toFloat().reduced (t.px (1.5f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (clr, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (clr, t.px (6.0f), 1.0f);
    g.setColour (c.text);
    g.setFont (t.valueFont());
    g.drawFittedText ("CLEAR", clr.toNearestInt(), juce::Justification::centred, 1);

    // Global Snap button
    const auto glb = globalBounds().toFloat().reduced (t.px (1.5f));
    const bool globalOn = isGlobalActive();
    g.setColour (globalOn ? c.mod : c.panelRaised);
    g.fillRoundedRectangle (glb, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (glb, t.px (6.0f), 1.0f);
    g.setColour (globalOn ? c.onMod : c.text);
    g.setFont (t.valueFont());
    g.drawFittedText (globalOn ? "GLOBAL SNAP: ON" : "GLOBAL SNAP: OFF", glb.toNearestInt(), juce::Justification::centred, 1);
}

void TransposeSnapControl::mouseDown (const juce::MouseEvent& e)
{
    if (! handle.valid())
        return;
    if (e.mods.isPopupMenu())
    {
        consumedByMenu = true;
        showSheet();
        return;
    }
    downHit = hitAt (e.getPosition(), downNote);
    beginPress();
}

void TransposeSnapControl::mouseUp (const juce::MouseEvent& e)
{
    cancelLongPress();
    if (consumedByMenu || ! handle.valid())
    {
        downHit = Hit::none;
        return;
    }
    int note = -1;
    const auto upHit = hitAt (e.getPosition(), note);
    if (upHit == downHit)
    {
        if (upHit == Hit::note && note >= 0)
            toggleKey (note);
        else if (upHit == Hit::clear)
            clear();
        else if (upHit == Hit::global)
            toggleGlobal();
    }
    downHit = Hit::none;
}
} // namespace synthyboy::ui
