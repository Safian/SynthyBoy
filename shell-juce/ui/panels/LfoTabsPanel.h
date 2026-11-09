// SynthyBoy - UI: the tab chip row of the LFO page (docs/ui-design/05-envelopes-lfos.md section 2.2): L1..L10 | R1..R4.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tap a chip = that LFO is the only LFO panel shown on the page (UiContext::view->lfoTab). Drag a chip onto another chip of the same
// kind = copy all settings (and the custom shape) of the dragged LFO onto the other one (design 05 section 3, [S p.189]).
#pragma once

#include <array>
#include <memory>

#include "../Panel.h"

namespace synthyboy::ui
{
constexpr int kNumLfoTabs = 14; // 0..9 = LFO 1..10, 10..13 = random LFO 1..4

class LfoTabsPanel final : public Panel
{
public:
    explicit LfoTabsPanel (UiContext& context);
    ~LfoTabsPanel() override;

    juce::String title() const override { return "LFO"; }
    std::vector<std::string> keys() const override { return {}; }
    void build() override;
    int preferredHeight (int width) const override;
    void resized() override;
    void paint (juce::Graphics&) override;

    int selectedTab() const noexcept { return ctx.view->lfoTab; }
    void selectTab (int tab);
    /** Copies tab `from` onto tab `to` (both LFO or both random LFO); false for anything else. */
    bool copyTab (int from, int to);
    juce::Rectangle<int> chipBounds (int tab) const;
    /** End of a chip drag (the chip calls it on mouse up; the tests call it directly): copies onto the chip at `overTab`. */
    void dragEnded (int from, int overTab);
    /** GROUPD WP-16 (manual p.198, "Option-drag an LFO tab to a wavetable"): the chip of LFO `from` (0..9) was dropped with Alt / Option held at the screen position
        `screenPos`; when a wavetable display of an oscillator lies there, the LFO shape is copied into that oscillator's table. True when it was copied. */
    bool dropOnWavetable (int from, juce::Point<int> screenPos);

    static juce::String tabLabel (int tab);

private:
    class Chip;
    int layoutChips (int width, bool apply) const;
    int tabAt (juce::Point<int> panelPos) const;

    std::array<std::unique_ptr<Chip>, kNumLfoTabs> chips;
    int dragFrom = -1, dragOver = -1;
    juce::String hint;
};
} // namespace synthyboy::ui
