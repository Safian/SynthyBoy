// SynthyBoy - UI: the oscillator chip row (docs/ui-design/02-oscillators.md section 2.1): [OSC1*][OSC2][OSC3], a power dot per chip.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tap a chip = that oscillator is the only oscillator panel shown (UiContext::view->oscTab); long-press a chip = the module sheet
// (copy / paste / swap / init); drag a chip onto another chip = swap the two oscillators (Alt held = copy, Alt + Shift = copy with
// modulation); swipe left / right on the chip row (or two fingers on the oscillator view) = the next / previous oscillator.
// The power dot toggles `oscN.on` and the whole oscillator panel dims while it is off.
#pragma once

#include <array>
#include <memory>

#include "../GestureKit.h"
#include "../Panel.h"

namespace synthyboy::ui
{
constexpr int kNumOscTabs = 3;

class OscTabsPanel final : public Panel
{
public:
    explicit OscTabsPanel (UiContext& context);
    ~OscTabsPanel() override;

    juce::String title() const override { return "Oscillators"; }
    std::vector<std::string> keys() const override { return {}; }
    void build() override;
    int preferredHeight (int width) const override;
    void resized() override;
    void paint (juce::Graphics&) override;

    int selectedTab() const noexcept { return ctx.view->oscTab; }
    void selectTab (int tab);
    /** Moves the selection by `direction` (swipe); stays at the ends. */
    void stepTab (int direction);
    juce::Rectangle<int> chipBounds (int tab) const;
    juce::Rectangle<int> powerBounds (int tab) const;
    /** Toggles `oscN.on` of tab `tab` (the power dot calls it). */
    void togglePower (int tab);
    /** End of a chip drag (the chip calls it on mouse up; the tests call it directly): swap / copy onto the chip at `overTab`. */
    bool dragEnded (int from, int overTab, bool copy, bool withMods);
    /** Long-press of a chip: opens the module sheet of that oscillator. */
    void chipLongPressed (int tab);

private:
    class Chip;
    class PowerDot;
    int tabAt (juce::Point<int> panelPos) const;

    std::array<std::unique_ptr<Chip>, kNumOscTabs> chips;
    std::array<std::unique_ptr<PowerDot>, kNumOscTabs> dots;
    int dragFrom = -1, dragOver = -1;
    juce::String hint;
};
} // namespace synthyboy::ui
