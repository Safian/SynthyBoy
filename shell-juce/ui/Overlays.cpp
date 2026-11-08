// SynthyBoy - UI: mod ring and value bubble.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Overlays.h"

#include <cmath>

namespace synthyboy::ui
{
namespace
{
constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.25f; // 7:30, measured clockwise from 12 o'clock
constexpr float kRotaryEnd = juce::MathConstants<float>::pi * 2.75f;   // 4:30

float angleFor (float normalised)
{
    return kRotaryStart + juce::jlimit (0.0f, 1.0f, normalised) * (kRotaryEnd - kRotaryStart);
}
} // namespace

ModRing::ModRing (const Theme& themeToUse, Shape shapeToUse) : theme (themeToUse), shape (shapeToUse)
{
    setInterceptsMouseClicks (false, false);
}

void ModRing::setState (State newState)
{
    current = std::move (newState);
    repaint();
}

std::pair<float, float> ModRing::spanFor (const ModEntry& e, float position) noexcept
{
    const float d = e.amount / 100.0f;
    float lo, hi;
    if (e.bipolar)
    {
        lo = position - std::fabs (d);
        hi = position + std::fabs (d);
    }
    else
    {
        lo = juce::jmin (position, position + d);
        hi = juce::jmax (position, position + d);
    }
    return { juce::jlimit (0.0f, 1.0f, lo), juce::jlimit (0.0f, 1.0f, hi) };
}

void ModRing::paint (juce::Graphics& g)
{
    const Palette& c = theme.colours;
    const auto bounds = getLocalBounds().toFloat();
    const float stroke = juce::jmax (2.0f, theme.px (3.0f));

    if (shape == Shape::ring)
    {
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - stroke;

        if (current.target)
        {
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, kRotaryStart, kRotaryEnd, true);
            g.setColour (c.mod.withAlpha (0.45f));
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.0f, stroke * 0.5f)));
        }

        for (const auto& e : current.entries)
        {
            const bool armed = current.armedSource == 0 || current.armedSource == e.source || e.slot == current.activeSlot;
            const auto span = spanFor (e, current.position);
            if (span.second - span.first < 0.002f)
                continue;
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleFor (span.first), angleFor (span.second), true);
            if (e.bypass)
            {
                juce::Path dashed;
                const float dash[] = { 3.0f, 3.0f };
                juce::PathStrokeType (stroke * 0.7f).createDashedStroke (dashed, p, dash, 2);
                g.setColour (c.modIdle);
                g.fillPath (dashed);
                continue;
            }
            g.setColour (! armed ? c.modIdle.withAlpha (0.7f) : (e.amount >= 0.0f ? c.mod : c.modNegative));
            g.strokePath (p, juce::PathStrokeType (armed ? stroke : stroke * 0.6f));
        }
    }
    else
    {
        const float y = bounds.getBottom() - stroke;
        const float x0 = bounds.getX(), w = bounds.getWidth();
        if (current.target)
        {
            g.setColour (c.mod.withAlpha (0.45f));
            g.fillRect (x0, y + stroke * 0.25f, w, stroke * 0.5f);
        }
        for (const auto& e : current.entries)
        {
            const bool armed = current.armedSource == 0 || current.armedSource == e.source || e.slot == current.activeSlot;
            const auto span = spanFor (e, current.position);
            g.setColour (e.bypass ? c.modIdle : (! armed ? c.modIdle.withAlpha (0.7f) : (e.amount >= 0.0f ? c.mod : c.modNegative)));
            g.fillRoundedRectangle (x0 + span.first * w, y, juce::jmax (2.0f, (span.second - span.first) * w), stroke, stroke * 0.5f);
        }
    }

    if (! current.entries.empty())
    {
        // count badge, top left
        const float d = theme.px (14.0f);
        const juce::Rectangle<float> badge (bounds.getX(), bounds.getY(), d, d);
        g.setColour (c.mod);
        g.fillEllipse (badge);
        g.setColour (c.onMod);
        g.setFont (theme.font (d * 0.72f, true));
        g.drawText (juce::String (static_cast<int> (current.entries.size())), badge.toNearestInt(), juce::Justification::centred, false);
    }
}

// ---- ValueBubble ------------------------------------------------------------------------------------------------------------------

void ValueBubble::setContent (const juce::String& valueText, FineZone activeZone)
{
    text = valueText;
    zone = activeZone;
    repaint();
}

juce::Rectangle<int> ValueBubble::idealSize() const
{
    const int textW = juce::roundToInt (Theme::textWidth (theme.valueFont(), text));
    return { 0, 0, juce::jmax (theme.ipx (120.0f), textW + theme.ipx (24.0f)), theme.ipx (50.0f) };
}

void ValueBubble::paint (juce::Graphics& g)
{
    const Palette& c = theme.colours;
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (area, theme.px (8.0f));
    g.setColour (c.accent);
    g.drawRoundedRectangle (area, theme.px (8.0f), 1.5f);

    auto inner = getLocalBounds().reduced (theme.ipx (6.0f), theme.ipx (4.0f));
    g.setColour (c.text);
    g.setFont (theme.valueFont());
    g.drawText (text, inner.removeFromTop (inner.getHeight() / 2 + 2), juce::Justification::centred, false);

    // the ladder: slide the finger sideways to step down (x1, x0.25, x0.05)
    static const char* const names[] = { "x1", "x0.25", "x0.05" };
    const int zoneWidth = inner.getWidth() / 3;
    g.setFont (theme.font (theme.px (11.0f)));
    for (int i = 0; i < 3; ++i)
    {
        const auto cell = inner.removeFromLeft (zoneWidth);
        g.setColour (static_cast<int> (zone) == i ? c.accent : c.textDim);
        g.drawText (names[i], cell, juce::Justification::centred, false);
    }
}
} // namespace synthyboy::ui
