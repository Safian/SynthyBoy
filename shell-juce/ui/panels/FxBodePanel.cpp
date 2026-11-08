// SynthyBoy - UI panel: Bode Frequency Shifter (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxBodeView final : public FxVisualBase
{
public:
    explicit FxBodeView (UiContext& context)
        : FxVisualBase (context, "Dual Direction Frequency Shifter (Drag Node: X Shift, Y Range)"),
          shiftHandle (context, "bode.shift"),
          rangeHandle (context, "bode.range"),
          dirHandle (context, "bode.dir"),
          delayHandle (context, "bode.delay")
    {
        shiftHandle.onChange = [this] { repaint(); };
        rangeHandle.onChange = [this] { repaint(); };
        dirHandle.onChange = [this] { repaint(); };
        delayHandle.onChange = [this] { repaint(); };

        #if defined(SYNTHYBOY_DEFAULT_TIER) && SYNTHYBOY_DEFAULT_TIER == low
            setTierBypassed (true);
        #endif
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 5, 3);

        const float midX = area.getCentreX();

        // Center carrier line
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawVerticalLine (juce::roundToInt (midX), area.getY(), area.getBottom());

        const float shiftNorm = juce::jlimit (0.0f, 1.0f, shiftHandle.plain() / 100.0f);
        const float rangeHz = rangeHandle.plain();
        const float normRange = fxFreqToNorm (rangeHz, 1.0f, 5000.0f);
        const float dirVal = juce::jlimit (-1.0f, 1.0f, dirHandle.plain() / 100.0f);

        // Draw shifting frequency sidebands (up and down)
        const float shiftPx = shiftNorm * (area.getWidth() * 0.4f);

        // Up sideband
        const float upX = midX + shiftPx * (dirVal >= 0.0f ? 1.0f : -1.0f);
        g.setColour (t.colours.accent);
        g.drawLine (upX, area.getBottom(), upX, area.getY() + area.getHeight() * (1.0f - normRange * 0.8f), t.px (2.5f));

        // Down sideband
        const float downX = midX - shiftPx;
        g.setColour (t.colours.track);
        g.drawLine (downX, area.getBottom(), downX, area.getY() + area.getHeight() * 0.4f, t.px (2.0f));

        // Draggable node
        const juce::Point<float> nodePos (upX, area.getBottom() - area.getHeight() * normRange);
        drawNode (g, nodePos, draggingNode,
                  "+" + juce::String (juce::roundToInt (shiftNorm * rangeHz)) + " Hz");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        if (area.contains (e.position))
        {
            draggingNode = true;
            shiftHandle.beginGesture();
            rangeHandle.beginGesture();
            updateFromMouse (e.position, area);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingNode)
        {
            const auto& t = ctx.theme;
            const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
            updateFromMouse (e.position, area);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingNode)
        {
            shiftHandle.endGesture();
            rangeHandle.endGesture();
            draggingNode = false;
        }
    }

private:
    void updateFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight());

        // Shift is 0..100%
        const float shiftVal = std::abs (normX - 0.5f) * 200.0f;
        const float rangeVal = fxNormToFreq (normY, 1.0f, 5000.0f);

        shiftHandle.setPlainComplete (juce::jlimit (0.0f, 100.0f, shiftVal));
        rangeHandle.setPlainComplete (rangeVal);
        repaint();
    }

    ParamHandle shiftHandle;
    ParamHandle rangeHandle;
    ParamHandle dirHandle;
    ParamHandle delayHandle;

    bool draggingNode = false;
};

class FxBodePanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Bode"; }

    std::vector<std::string> keys() const override
    {
        return {
            "bode.shift", "bode.range", "bode.delay", "bode.mix",
            "bode.on", "bode.mono_input", "bode.dir", "bode.width", "bode.sync", "bode.division",
            "bode.feed", "bode.balance", "bode.blur", "bode.retrig", "bode.level", "bode.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxBodeView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("bode.shift");
        addControl ("bode.range");
        addControl ("bode.delay");
        addControl ("bode.mix");

        // More controls (12 keys)
        addMoreControl ("bode.on");
        addMoreControl ("bode.mono_input");
        addMoreControl ("bode.dir");
        addMoreControl ("bode.width");
        addMoreControl ("bode.sync");
        addMoreControl ("bode.division");
        addMoreControl ("bode.feed");
        addMoreControl ("bode.balance");
        addMoreControl ("bode.blur");
        addMoreControl ("bode.retrig");
        addMoreControl ("bode.level");
        addMoreControl ("bode.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (120.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxBodePanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (120.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxBodeView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxBodePanel, Page::fx, 130)
} // namespace synthyboy::ui
