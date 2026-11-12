// SynthyBoy - UI: common definitions, math helpers and base components for FX panels.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Panel.h"
#include "../ParamHandle.h"
#include "../Theme.h"
#include "../UiContext.h"
#include "synthyboy/quality.h"

namespace synthyboy::ui
{
// Module names and abbreviations corresponding to fx.order choices 0..15
inline const char* const kFxModuleNames[16] = {
    "Chorus", "Compressor", "Delay", "Distortion",
    "EQ", "FX Filter", "Flanger", "Phaser",
    "Reverb", "Hyper/Dimension", "Utility", "Bode",
    "Convolve", "Splitter L/H", "Splitter L/M/H", "Splitter M/S"
};

inline const char* const kFxModuleShortNames[16] = {
    "Chor", "Comp", "Delay", "Dist",
    "EQ", "Filt", "Flg", "Phs",
    "Rev", "HypD", "Util", "Bode",
    "Conv", "SpLH", "SpLMH", "SpMS"
};

inline const char* const kFxRackNames[10] = {
    "Main", "Bus 1", "Bus 2", "SpLH Lo", "SpLH Hi",
    "SpLMH Lo", "SpLMH Mid", "SpLMH Hi", "SpMS Mid", "SpMS Side"
};

inline const char* const kFxOnKeys[16] = {
    "chorus.on", "compressor.on", "delay.on", "distortion.on",
    "eq.on", "fxfilter.on", "flanger.on", "phaser.on",
    "reverb.on", "hyperdim.on", "utility.on", "bode.on",
    "convolve.on", "splitter_lh.on", "splitter_lmh.on", "splitter_ms.on"
};

inline const char* const kFxRackKeys[16] = {
    "chorus.rack", "compressor.rack", "delay.rack", "distortion.rack",
    "eq.rack", "fxfilter.rack", "flanger.rack", "phaser.rack",
    "reverb.rack", "hyperdim.rack", "utility.rack", "bode.rack",
    "convolve.rack", "splitter_lh.rack", "splitter_lmh.rack", "splitter_ms.rack"
};

/** Normalizes a frequency in Hz (20..20000) to 0..1 logarithmic scale. */
inline float fxFreqToNorm (float hz, float minHz = 20.0f, float maxHz = 20000.0f) noexcept
{
    hz = juce::jlimit (minHz, maxHz, hz);
    return std::log (hz / minHz) / std::log (maxHz / minHz);
}

/** Denormalizes a 0..1 logarithmic value to Hz (20..20000). */
inline float fxNormToFreq (float norm, float minHz = 20.0f, float maxHz = 20000.0f) noexcept
{
    norm = juce::jlimit (0.0f, 1.0f, norm);
    return minHz * std::pow (maxHz / minHz, norm);
}

/** Converts dB (-30..+30) to 0..1 linear scale. */
inline float fxDbToNorm (float db, float minDb = -30.0f, float maxDb = 30.0f) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

/** Converts 0..1 linear scale to dB (-30..+30). */
inline float fxNormToDb (float norm, float minDb = -30.0f, float maxDb = 30.0f) noexcept
{
    norm = juce::jlimit (0.0f, 1.0f, norm);
    return minDb + norm * (maxDb - minDb);
}

/** Pure helper: repair duplicate or out-of-range slots in fx.order.1..16 to form a valid permutation of 0..15.
    The repair rule assigns the lowest unplaced effect index to any duplicate or invalid slot. */
inline std::array<int, 16> repairFxPermutation (const std::array<int, 16>& raw)
{
    std::array<int, 16> result = raw;
    std::array<bool, 16> used {};

    for (size_t i = 0; i < 16; ++i)
    {
        const int val = result[i];
        if (val >= 0 && val < 16 && ! used[static_cast<size_t> (val)])
        {
            used[static_cast<size_t> (val)] = true;
        }
        else
        {
            result[i] = -1; // marked for repair
        }
    }

    int nextAvailable = 0;
    for (size_t i = 0; i < 16; ++i)
    {
        if (result[i] < 0)
        {
            while (nextAvailable < 16 && used[static_cast<size_t> (nextAvailable)])
                ++nextAvailable;
            if (nextAvailable < 16)
            {
                result[i] = nextAvailable;
                used[static_cast<size_t> (nextAvailable)] = true;
            }
        }
    }
    return result;
}

/** Common visual base component for FX panels: handles frame painting, grid lines, tier badges, and node drawing. */
class FxVisualBase : public juce::Component
{
public:
    FxVisualBase (UiContext& context, const juce::String& visualTitle)
        : ctx (context), title (visualTitle)
    {
        setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
    }

    void setTierBypassed (bool bypassed)
    {
        if (tierBypassed != bypassed)
        {
            tierBypassed = bypassed;
            repaint();
        }
    }

    bool isTierBypassed() const noexcept { return tierBypassed; }

protected:
    void paintBaseBackground (juce::Graphics& g)
    {
        const auto& t = ctx.theme;
        const auto r = getLocalBounds().toFloat().reduced (t.px (1.0f));

        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (r, t.px (6.0f));

        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (r, t.px (6.0f), 1.0f);

        if (title.isNotEmpty())
        {
            g.setColour (t.colours.textDim);
            g.setFont (t.labelFont());
            g.drawText (title, getLocalBounds().reduced (t.ipx (8.0f), t.ipx (4.0f)),
                        juce::Justification::topLeft, false);
        }

        if (tierBypassed)
        {
            g.setColour (t.colours.warn);
            g.setFont (t.labelFont().boldened());
            g.drawText ("[BYPASSED BY LOW TIER]", getLocalBounds().reduced (t.ipx (8.0f), t.ipx (4.0f)),
                        juce::Justification::topRight, false);
        }
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> area, int xDivs = 4, int yDivs = 3)
    {
        const auto& t = ctx.theme;
        g.setColour (t.colours.outline.withAlpha (0.35f));

        for (int i = 1; i < xDivs; ++i)
        {
            const float x = area.getX() + area.getWidth() * (static_cast<float> (i) / static_cast<float> (xDivs));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        }

        for (int i = 1; i < yDivs; ++i)
        {
            const float y = area.getY() + area.getHeight() * (static_cast<float> (i) / static_cast<float> (yDivs));
            g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        }
    }

    void drawNode (juce::Graphics& g, juce::Point<float> pos, bool active, const juce::String& label = {})
    {
        const auto& t = ctx.theme;
        const float radius = t.px (active ? 7.0f : 5.0f);

        if (active)
        {
            g.setColour (t.colours.accentDim.withAlpha (0.4f));
            g.fillEllipse (pos.x - radius * 1.8f, pos.y - radius * 1.8f, radius * 3.6f, radius * 3.6f);
        }

        g.setColour (t.colours.accent);
        g.fillEllipse (pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f);

        g.setColour (t.colours.panel);
        g.drawEllipse (pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f, t.px (1.5f));

        if (label.isNotEmpty())
        {
            g.setColour (t.colours.text);
            g.setFont (t.labelFont());
            g.drawText (label, juce::Rectangle<float> (pos.x + radius + t.px (4.0f), pos.y - t.px (8.0f), t.px (60.0f), t.px (16.0f)),
                        juce::Justification::centredLeft, false);
        }
    }

    UiContext& ctx;
    juce::String title;
    bool tierBypassed = false;
};
} // namespace synthyboy::ui
