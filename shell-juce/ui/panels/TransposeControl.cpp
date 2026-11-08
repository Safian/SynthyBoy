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
} // namespace synthyboy::ui
