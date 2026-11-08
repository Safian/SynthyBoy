// SynthyBoy - UI panel: Delay (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

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
          spreadHandle (context, "delay.filter_spread")
    {
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
        const float t2 = juce::jlimit (20.0f, 2000.0f, time2Handle.plain());

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

    bool draggingPad = false;
    bool showFftOverlay = false;
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
            "delay.filter_cutoff", "delay.filter_spread", "delay.rack"
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

        // More controls (8 keys)
        addMoreControl ("delay.on");
        addMoreControl ("delay.style");
        addMoreControl ("delay.sync");
        addMoreControl ("delay.division");
        addMoreControl ("delay.division2");
        addMoreControl ("delay.filter_cutoff");
        addMoreControl ("delay.filter_spread");
        addMoreControl ("delay.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (130.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
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
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxDelayView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxDelayPanel, Page::fx, 40)
} // namespace synthyboy::ui
