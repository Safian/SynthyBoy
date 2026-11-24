// SynthyBoy - UI: EnvelopePanel - 6 envelopes with node dragging (docs/ui-design/05-envelopes-lfos.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "../Panel.h"
#include "EnvCurveView.h"

namespace synthyboy::ui
{
/** A source tab (E1..E6): tap = select that envelope, long-press = its destinations (bypass / remove), drag onto a control = assign that
    source to the control (the source arms while dragging, so the targets glow; design 05 / 06). */
class SourceTabButton final : public juce::TextButton
{
public:
    SourceTabButton (UiContext& context, int sourceChoiceIn) : ctx (context), sourceChoice (sourceChoiceIn)
    {
        setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
    }

    std::function<void()> onLongPress;
    /** The drag ended over `target` (may be null). */
    std::function<void (juce::Component* target)> onDropped;

    int source() const noexcept { return sourceChoice; }
    LongPressDetector& longPress() noexcept { return press; }
    bool isDragging() const noexcept { return dragging; }

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    /** True once after a long-press or a drag: the click that follows the release is not a tap. */
    bool consumeSuppressedClick() noexcept
    {
        const bool v = suppressClick;
        suppressClick = false;
        return v;
    }

private:
    UiContext& ctx;
    int sourceChoice;
    LongPressDetector press;
    bool dragging = false, suppressClick = false, armedByDrag = false;
};


/** Envelope panel base class for Envelopes 1..6.
    Combines the tab chips [E1*]..[E6] with destination badges, zoom lock,
    interactive EnvCurveView visual, 4 primary knobs (Attack, Decay, Sustain, Release),
    and More row for delay, hold, curves, units, and beats parameters. */
class EnvelopePanel : public Panel,
                      private juce::ChangeListener
{
public:
    EnvelopePanel (UiContext& context, int envIndex);
    ~EnvelopePanel() override;

    int envelopeIndex() const noexcept { return envIdx; }

    juce::String title() const override;
    /** Only the envelope chosen on the tabs takes part in the layout (like the LFO tabs). */
    bool hiddenInLayout() const override { return ctx.view->envTab != envIdx - 1; }
    void selectEnvelope (int envelope);
    /** Assigns envelope `sourceChoice` (1..6) to the control `target` or the control it sits in (touch flow: arm, drag to 50 %, release). */
    bool dropSourceOn (int sourceChoice, juce::Component* target);
    SourceTabButton* tabButton (int envelope) noexcept { return tabButtons[static_cast<size_t> (juce::jlimit (1, 6, envelope) - 1)].get(); }
    std::string modulePrefix() const override { return "env" + std::to_string (envIdx); }
    std::vector<std::string> keys() const override;
    void build() override;

    int preferredHeight (int width) const override;
    void resized() override;

    EnvCurveView& curveView() noexcept { return *visual; }
    /** The magnifier slider next to Lock: the visible alternative of the pinch (enabled while the zoom lock is off). */
    juce::Slider& magnifierSlider() noexcept { return magnifier; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateTabBadges();
    int tabsPerRow (int width) const;
    int tabRowHeight (int width) const;

    int envIdx = 1;
    std::unique_ptr<EnvCurveView> visual;

    // Header tabs row
    juce::Component tabRow;
    std::array<std::unique_ptr<SourceTabButton>, 6> tabButtons;
    juce::TextButton zoomLockButton { "Lock" };
    juce::Slider magnifier { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
};

} // namespace synthyboy::ui
