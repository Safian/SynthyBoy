// SynthyBoy - UI panel: Delay (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"
#include "../model/FxModel.h"

namespace synthyboy::ui
{
class FxDelayView final : public FxVisualBase
{
public:
    explicit FxDelayView (UiContext& context)
        : FxVisualBase (context, "Echo Timeline & Filter Pad (Drag X: Cutoff, Y: Spread, 2x tap: FFT)"),
          timeHandle (context, "delay.time"),
          time2Handle (context, "delay.time2"),
          feedHandle (context, "delay.feedback"),
          cutoffHandle (context, "delay.filter_cutoff"),
          spreadHandle (context, "delay.filter_spread"),
          offsetHandle (context, "delay.offset")
    {
        offsetHandle.onChange = [this] { repaint(); };
        timeHandle.onChange = [this] { repaint(); };
        time2Handle.onChange = [this] { repaint(); };
        feedHandle.onChange = [this] { repaint(); };
        cutoffHandle.onChange = [this] { repaint(); };
        spreadHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        const float halfH = area.getHeight() * 0.45f;
        const auto timelineArea = area.removeFromTop (halfH);
        const auto filterArea = area.withTrimmedTop (t.px (4.0f));

        // 1. Echo impulses (L top, R bottom)
        const float midY = timelineArea.getCentreY();
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (midY), timelineArea.getX(), timelineArea.getRight());

        const float fb = juce::jlimit (0.0f, 0.95f, std::abs (feedHandle.plain()) / 100.0f);
        const float t1 = juce::jlimit (20.0f, 2000.0f, timeHandle.plain());
        const float t2 = juce::jlimit (20.0f, 2000.0f, time2Handle.plain() * offsetHandle.plain()); // (delay.offset scales the right time)

        // Draw decaying pulses
        float ampL = 1.0f;
        for (float ms = t1; ms < 2000.0f && ampL > 0.05f; ms += t1)
        {
            const float x = timelineArea.getX() + timelineArea.getWidth() * (ms / 2000.0f);
            const float h = (midY - timelineArea.getY()) * ampL;
            g.setColour (t.colours.accent);
            g.drawLine (x, midY, x, midY - h, t.px (2.0f));
            ampL *= fb;
        }

        float ampR = 1.0f;
        for (float ms = t2; ms < 2000.0f && ampR > 0.05f; ms += t2)
        {
            const float x = timelineArea.getX() + timelineArea.getWidth() * (ms / 2000.0f);
            const float h = (timelineArea.getBottom() - midY) * ampR;
            g.setColour (t.colours.track);
            g.drawLine (x, midY, x, midY + h, t.px (2.0f));
            ampR *= fb;
        }

        // 2. Filter XY Pad
        g.setColour (t.colours.panel);
        g.fillRoundedRectangle (filterArea, t.px (4.0f));
        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (filterArea, t.px (4.0f), 1.0f);

        drawGrid (g, filterArea, 4, 2);

        if (showFftOverlay)
        {
            // Simulated live FFT curve
            juce::Path fft;
            fft.startNewSubPath (filterArea.getX(), filterArea.getBottom());
            for (int i = 0; i < 20; ++i)
            {
                const float norm = static_cast<float> (i) / 19.0f;
                const float x = filterArea.getX() + filterArea.getWidth() * norm;
                const float wave = std::sin (norm * 12.0f) * 0.3f + std::cos (norm * 5.0f) * 0.2f + 0.4f;
                const float y = filterArea.getBottom() - filterArea.getHeight() * juce::jlimit (0.1f, 0.9f, wave);
                fft.lineTo (x, y);
            }
            g.setColour (t.colours.warn.withAlpha (0.4f));
            g.strokePath (fft, juce::PathStrokeType (1.0f));
        }

        // Filter Cutoff & Spread node
        const float normCutoff = fxFreqToNorm (cutoffHandle.plain());
        const float normSpread = juce::jlimit (0.0f, 1.0f, spreadHandle.plain() / 100.0f);
        const juce::Point<float> nodePos (filterArea.getX() + filterArea.getWidth() * normCutoff,
                                          filterArea.getY() + filterArea.getHeight() * (1.0f - normSpread));

        drawNode (g, nodePos, draggingPad, juce::String (juce::roundToInt (cutoffHandle.plain())) + " Hz");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.getNumberOfClicks() >= 2)
        {
            showFftOverlay = ! showFftOverlay;
            repaint();
            return;
        }

        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        const auto filterArea = area.withTrimmedTop (area.getHeight() * 0.45f + t.px (4.0f));

        if (filterArea.contains (e.position))
        {
            draggingPad = true;
            cutoffHandle.beginGesture();
            spreadHandle.beginGesture();
            updateFromMouse (e.position, filterArea);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingPad)
        {
            const auto& t = ctx.theme;
            const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
            const auto filterArea = area.withTrimmedTop (area.getHeight() * 0.45f + t.px (4.0f));
            updateFromMouse (e.position, filterArea);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingPad)
        {
            cutoffHandle.endGesture();
            spreadHandle.endGesture();
            draggingPad = false;
        }
    }

private:
    void updateFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - area.getY()) / area.getHeight());

        cutoffHandle.setNormalised (normX);
        spreadHandle.setPlainComplete (normY * 100.0f);
        repaint();
    }

    ParamHandle timeHandle;
    ParamHandle time2Handle;
    ParamHandle feedHandle;
    ParamHandle cutoffHandle;
    ParamHandle spreadHandle;
    ParamHandle offsetHandle;

    bool draggingPad = false;
    bool showFftOverlay = false;
};

/** The row under the echo view: TAP (Tap->Delay, G74: the interval of the taps becomes the delay time) and the snap points of delay.offset. */
class FxDelayTools final : public juce::Component
{
public:
    explicit FxDelayTools (UiContext& context)
        : timeHandle (context, "delay.time"),
          time2Handle (context, "delay.time2"),
          syncHandle (context, "delay.sync"),
          offsetHandle (context, "delay.offset")
    {
        tapButton.setButtonText ("TAP");
        tapButton.setTooltip ("Tap the tempo: sets Free sync and both delay times to the interval of the last taps");
        tapButton.onClick = [this] { tap (juce::Time::getMillisecondCounterHiRes()); };
        addAndMakeVisible (tapButton);
        static const char* const names[3] = { "x1", "x4/3", "x3/2" };
        static const char* const tips[3] = { "Right delay time = left", "Right delay time = 4/3 of left (triplet)", "Right delay time = 3/2 of left (dotted)" };
        for (int i = 0; i < 3; ++i)
        {
            snapButtons[static_cast<size_t> (i)].setButtonText (names[i]);
            snapButtons[static_cast<size_t> (i)].setTooltip (tips[i]);
            snapButtons[static_cast<size_t> (i)].onClick = [this, i] { snapOffset (i); };
            addAndMakeVisible (snapButtons[static_cast<size_t> (i)]);
        }
    }

    /** One tap at `nowMs`; true once the interval was written to the parameters. */
    bool tap (double nowMs)
    {
        if (! tapModel.tap (nowMs))
        {
            tapButton.setButtonText ("TAP");
            return false;
        }
        const double ms = tapModel.intervalMs();
        syncHandle.setPlainComplete (0.0f); // Free
        timeHandle.setPlainComplete (juce::jlimit (timeHandle.info().min, timeHandle.info().max, static_cast<float> (ms)));
        time2Handle.setPlainComplete (juce::jlimit (time2Handle.info().min, time2Handle.info().max, static_cast<float> (ms)));
        tapButton.setButtonText ("TAP " + juce::String (juce::roundToInt (tapModel.bpm())));
        return true;
    }

    void snapOffset (int index)
    {
        if (index >= 0 && index < 3)
            offsetHandle.setPlainComplete (kDelayOffsetSnap[index]);
    }

    TapTempo& tapState() noexcept { return tapModel; }
    juce::TextButton& tapButtonRef() noexcept { return tapButton; }
    juce::TextButton& snapButton (int index) noexcept { return snapButtons[static_cast<size_t> (index)]; }

    void resized() override
    {
        auto area = getLocalBounds();
        tapButton.setBounds (area.removeFromLeft (area.getWidth() * 2 / 5).reduced (1, 0));
        const int w = area.getWidth() / 3;
        for (size_t i = 0; i < 3; ++i)
            snapButtons[i].setBounds (area.removeFromLeft (i == 2 ? area.getWidth() : w).reduced (1, 0));
    }

private:
    ParamHandle timeHandle, time2Handle, syncHandle, offsetHandle;
    TapTempo tapModel;
    juce::TextButton tapButton;
    std::array<juce::TextButton, 3> snapButtons;
};

class FxDelayPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Delay"; }

    std::vector<std::string> keys() const override
    {
        return {
            "delay.time", "delay.time2", "delay.feedback", "delay.mix",
            "delay.on", "delay.style", "delay.sync", "delay.division", "delay.division2",
            "delay.filter_cutoff", "delay.filter_spread", "delay.hq", "delay.offset", "delay.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxDelayView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("delay.time");
        addControl ("delay.time2");
        addControl ("delay.feedback");
        addControl ("delay.mix");

        // More controls (10 keys); under the view: TAP and the offset snaps
        addMoreControl ("delay.on");
        addMoreControl ("delay.style");
        addMoreControl ("delay.sync");
        addMoreControl ("delay.division");
        addMoreControl ("delay.division2");
        addMoreControl ("delay.filter_cutoff");
        addMoreControl ("delay.filter_spread");
        addMoreControl ("delay.hq");
        addMoreControl ("delay.offset");
        addMoreControl ("delay.rack");
        tools = std::make_unique<FxDelayTools> (ctx);
        addAndMakeVisible (*tools);
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (130.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap() + t.ipx (44.0f) + t.gap()
               + const_cast<FxDelayPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (130.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        if (tools != nullptr)
        {
            tools->setBounds (area.removeFromTop (ctx.theme.ipx (44.0f)));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

    FxDelayTools* toolsRow() noexcept { return tools.get(); }

private:
    std::unique_ptr<FxDelayView> visual;
    std::unique_ptr<FxDelayTools> tools;
};

SYNTHYBOY_REGISTER_PANEL (FxDelayPanel, Page::fx, 40)
} // namespace synthyboy::ui
