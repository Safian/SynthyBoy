// SynthyBoy - UI: modulation source chips and the drawer that holds them (assign by touch, docs/ui-design/06 2.1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>

#include "UiContext.h"

namespace synthyboy::ui
{
/** A source chip. Tap: arm / disarm the source (purple highlight; the modulatable controls show target rings). Long-press or right-click:
    list of the destinations of this source with bypass and remove. Dimmed with a tooltip if the engine lacks the source (caps). */
class ModSourceChip final : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener, private juce::Timer
{
public:
    ModSourceChip (UiContext& context, int sourceChoice);
    ~ModSourceChip() override;

    int source() const noexcept { return sourceChoice; }
    bool isArmed() const;
    bool isAvailable() const noexcept { return available; }
    juce::Point<int> preferredSize() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void showAssignments();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;

    UiContext& ctx;
    int sourceChoice;
    bool available = true;
    bool pressed = false, consumed = false;
};

/** The source drawer: a Done button, the status line and the scrolling row of chips for all sources of the mod matrix. */
class ModSourceBar final : public juce::Component, private juce::ChangeListener
{
public:
    explicit ModSourceBar (UiContext& context);
    ~ModSourceBar() override;

    int preferredHeight() const;
    void resized() override;
    void paint (juce::Graphics&) override;
    ModSourceChip* chipFor (int sourceChoice) const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    UiContext& ctx;
    juce::TextButton done { "Done" };
    juce::Viewport viewport;
    class Row;
    std::unique_ptr<Row> row;
    juce::String status;
};
} // namespace synthyboy::ui
