// SynthyBoy - UI panel: Key & Scale Quantizer (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <array>
#include <vector>
#include "../Panel.h"
#include "../ParamHandle.h"

namespace synthyboy::ui
{
namespace
{
// Bitmasks for the 12 chromatic intervals (bit 0 = root, bit 1 = semitone, etc.)
constexpr std::array<uint16_t, 20> kScaleBitmasks = {
    0x0FFF, // 0: Chromatic (all 12)
    0x0AB5, // 1: Major: 0, 2, 4, 5, 7, 9, 11
    0x05AD, // 2: Natural Minor: 0, 2, 3, 5, 7, 8, 10
    0x09AD, // 3: Harmonic Minor: 0, 2, 3, 5, 7, 8, 11
    0x0AB1, // 4: Melodic Minor: 0, 2, 3, 5, 7, 9, 11
    0x06AD, // 5: Dorian: 0, 2, 3, 5, 7, 9, 10
    0x056B, // 6: Phrygian: 0, 1, 3, 5, 7, 8, 10
    0x0ACD, // 7: Lydian: 0, 2, 4, 6, 7, 9, 11
    0x06B5, // 8: Mixolydian: 0, 2, 4, 5, 7, 9, 10
    0x055B, // 9: Locrian: 0, 1, 3, 5, 6, 8, 10
    0x02A5, // 10: Major Pentatonic: 0, 2, 4, 7, 9
    0x04A9, // 11: Minor Pentatonic: 0, 3, 5, 7, 10
    0x04EB, // 12: Blues: 0, 3, 5, 6, 7, 10
    0x0555, // 13: Whole Tone: 0, 2, 4, 6, 8, 10
    0x066F, // 14: Diminished (H-W): 0, 1, 3, 4, 6, 7, 9, 10
    0x0B6D, // 15: Diminished (W-H): 0, 2, 3, 5, 6, 8, 9, 11
    0x0569, // 16: Phrygian Dominant: 0, 1, 4, 5, 7, 8, 10
    0x099D, // 17: Hungarian Minor: 0, 2, 3, 6, 7, 8, 11
    0x0969, // 18: Double Harmonic: 0, 1, 4, 5, 7, 8, 11
    0x018D  // 19: Hirajoshi: 0, 2, 3, 7, 8
};

const char* const kNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

bool isNoteInScaleMask (int note, int root, uint16_t mask) noexcept
{
    const int interval = ((note - root) % 12 + 12) % 12;
    return (mask & (1u << interval)) != 0;
}

/** Conforms a note 0..11 to the scale. Nearest in-scale note; ties round down (Assumption A7). */
int conformNoteClass (int note, int root, uint16_t mask) noexcept
{
    if (isNoteInScaleMask (note, root, mask))
        return note;
    for (int dist = 1; dist <= 6; ++dist)
    {
        const int down = ((note - dist) % 12 + 12) % 12;
        if (isNoteInScaleMask (down, root, mask))
            return down;
        const int up = ((note + dist) % 12 + 12) % 12;
        if (isNoteInScaleMask (up, root, mask))
            return up;
    }
    return note;
}
} // namespace

/** Direct manipulation interactive visual for Key & Scale: shows the 12 chromatic degrees,
    scale membership, conforming mapping, and lets user tap a key to pick root note. */
class GlbKeyScaleVisual final : public juce::Component
{
public:
    explicit GlbKeyScaleVisual (UiContext& context)
        : ctx (context),
          keyHandle (context, "master.scale.key"),
          typeHandle (context, "master.scale.type"),
          modeHandle (context, "master.scale.mode")
    {
        const auto update = [this] { repaint(); };
        keyHandle.onChange = update;
        typeHandle.onChange = update;
        modeHandle.onChange = update;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto& c = t.colours;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (2.0f));

        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        const int root = juce::jlimit (0, 11, static_cast<int> (keyHandle.plain()));
        const int scaleIdx = juce::jlimit (0, 19, static_cast<int> (typeHandle.plain()));
        const bool isConform = (modeHandle.plain() <= 0.5f); // 0 = Conform, 1 = Mark only
        const uint16_t mask = kScaleBitmasks[static_cast<size_t> (scaleIdx)];

        // Header info
        g.setFont (t.labelFont());
        g.setColour (c.textDim);
        const float pad = t.px (8.0f);

        juce::String scaleName = (scaleIdx < static_cast<int> (typeHandle.info().numChoices))
                               ? typeHandle.info().choices[static_cast<size_t> (scaleIdx)]
                               : "Scale";

        juce::String header = "Root: " + juce::String (kNoteNames[root])
                            + " | " + scaleName
                            + " | Mode: " + (isConform ? "Conform" : "Mark Only");
        g.drawText (header, juce::Rectangle<float> (bounds.getX() + pad, bounds.getY() + pad, bounds.getWidth() - 2 * pad, t.px (16.0f)),
                    juce::Justification::centredLeft, true);

        // Draw 12 note pads horizontally
        const float padsY = bounds.getY() + pad + t.px (22.0f);
        const float padsH = bounds.getBottom() - pad - padsY;
        const float padGap = t.px (3.0f);
        const float padW = (bounds.getWidth() - 2 * pad - 11.0f * padGap) / 12.0f;

        for (int i = 0; i < 12; ++i)
        {
            const float px = bounds.getX() + pad + static_cast<float> (i) * (padW + padGap);
            const juce::Rectangle<float> padRect (px, padsY, padW, padsH);

            const bool inScale = isNoteInScaleMask (i, root, mask);
            const bool isRoot = (i == root);
            const bool isBlackKey = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);

            // Background of note pad
            if (isRoot)
            {
                g.setColour (c.accent);
                g.fillRoundedRectangle (padRect, t.px (4.0f));
            }
            else if (inScale)
            {
                g.setColour (c.accentDim);
                g.fillRoundedRectangle (padRect, t.px (4.0f));
            }
            else
            {
                g.setColour (isBlackKey ? c.track.withAlpha (0.4f) : c.track.withAlpha (0.7f));
                g.fillRoundedRectangle (padRect, t.px (4.0f));
            }

            // Outline
            g.setColour (isRoot ? c.accent : c.outline.withAlpha (0.6f));
            g.drawRoundedRectangle (padRect, t.px (4.0f), 1.0f);

            // Note label
            g.setFont (t.labelFont());
            g.setColour (isRoot ? c.textOnAccent : (inScale ? c.text : c.textDim));
            g.drawText (kNoteNames[i], padRect.withTrimmedBottom (padsH * 0.4f),
                        juce::Justification::centred, false);

            // Conformed indicator in Conform mode
            if (! inScale && isConform)
            {
                const int conformed = conformNoteClass (i, root, mask);
                g.setFont (t.font (t.px (9.0f)));
                g.setColour (c.warn);
                g.drawText (juce::String ("->") + kNoteNames[conformed],
                            padRect.withTrimmedTop (padsH * 0.55f),
                            juce::Justification::centred, false);
            }
            else if (isRoot)
            {
                g.setFont (t.font (t.px (9.0f), true));
                g.setColour (c.textOnAccent);
                g.drawText ("ROOT", padRect.withTrimmedTop (padsH * 0.55f),
                            juce::Justification::centred, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (ctx.theme.px (2.0f));
        const float pad = ctx.theme.px (8.0f);
        const float padsY = bounds.getY() + pad + ctx.theme.px (22.0f);
        const float padsH = bounds.getBottom() - pad - padsY;
        const float padGap = ctx.theme.px (3.0f);
        const float padW = (bounds.getWidth() - 2 * pad - 11.0f * padGap) / 12.0f;

        if (e.position.y >= padsY && e.position.y <= padsY + padsH)
        {
            const float localX = e.position.x - (bounds.getX() + pad);
            const int clickedIndex = juce::jlimit (0, 11, static_cast<int> (localX / (padW + padGap)));
            keyHandle.setPlainComplete (static_cast<float> (clickedIndex));
            repaint();
        }
        else if (e.position.y < padsY)
        {
            // Tap top area toggles Conform / Mark only
            const float curMode = modeHandle.plain();
            modeHandle.setPlainComplete (curMode >= 0.5f ? 0.0f : 1.0f);
            repaint();
        }
    }

private:
    UiContext& ctx;
    ParamHandle keyHandle;
    ParamHandle typeHandle;
    ParamHandle modeHandle;
};

class GlbKeyScalePanel final : public Panel
{
public:
    explicit GlbKeyScalePanel (UiContext& context)
        : Panel (context), visual (context)
    {
        addAndMakeVisible (visual);
    }

    juce::String title() const override { return "Key & Scale"; }

    std::vector<std::string> keys() const override
    {
        return { "master.scale.key",
                 "master.scale.type",
                 "master.scale.mode",
                 "master.transpose" };
    }

    void build() override
    {
        // 4 primary controls next to the visual (none behind More)
        addControl ("master.scale.key");
        addControl ("master.scale.type");
        addControl ("master.scale.mode");
        addControl ("master.transpose");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (110.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
             + const_cast<GlbKeyScalePanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (110.0f);
        visual.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

private:
    GlbKeyScaleVisual visual;
};

SYNTHYBOY_REGISTER_PANEL (GlbKeyScalePanel, Page::glb, 30)
} // namespace synthyboy::ui
