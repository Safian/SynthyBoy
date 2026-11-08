// SynthyBoy - UI panel: Chorus (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxChorusView final : public FxVisualBase
{
public:
    explicit FxChorusView (UiContext& context)
        : FxVisualBase (context, "Dual Tap & Post-Filter (Drag X: Cutoff, Y: Spread)"),
          cutoffHandle (context, "chorus.filter_cutoff"),
          spreadHandle (context, "chorus.filter_spread"),
          delay1Handle (context, "chorus.delay1"),
          delay2Handle (context, "chorus.delay2"),
          voicesHandle (context, "chorus.voices")
    {
        cutoffHandle.onChange = [this] { repaint(); };
        spreadHandle.onChange = [this] { repaint(); };
        delay1Handle.onChange = [this] { repaint(); };
        delay2Handle.onChange = [this] { repaint(); };
        voicesHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 4, 3);

        // Tap lines for Delay 1 and Delay 2 (1..20 ms)
        const float d1Norm = juce::jlimit (0.0f, 1.0f, (delay1Handle.plain() - 1.0f) / 19.0f);
        const float d2Norm = juce::jlimit (0.0f, 1.0f, (delay2Handle.plain() - 1.0f) / 19.0f);

        const float x1 = area.getX() + area.getWidth() * d1Norm;
        const float x2 = area.getX() + area.getWidth() * d2Norm;

        g.setColour (t.colours.track.withAlpha (0.6f));
        g.drawLine (x1, area.getBottom(), x1, area.getY() + area.getHeight() * 0.35f, t.px (2.0f));
        g.drawLine (x2, area.getBottom(), x2, area.getY() + area.getHeight() * 0.35f, t.px (2.0f));

        // Bandpass filter response curve
        const float normCutoff = fxFreqToNorm (cutoffHandle.plain());
        const float spread = juce::jlimit (0.0f, 1.0f, spreadHandle.plain() / 100.0f);
        const float centerX = area.getX() + area.getWidth() * normCutoff;
        const float bandWidth = juce::jmax (t.px (16.0f), area.getWidth() * (0.08f + 0.35f * spread));
        const float peakY = area.getY() + area.getHeight() * 0.2f;
        const float baseY = area.getBottom();

        juce::Path bp;
        bp.startNewSubPath (area.getX(), baseY);
        bp.cubicTo (centerX - bandWidth, baseY,
                    centerX - bandWidth * 0.4f, peakY,
                    centerX, peakY);
        bp.cubicTo (centerX + bandWidth * 0.4f, peakY,
                    centerX + bandWidth, baseY,
                    area.getRight(), baseY);

        // Fill under curve
        g.setColour (t.colours.accentDim.withAlpha (0.2f));
        g.fillPath (bp);

        // Stroke curve
        g.setColour (t.colours.accent);
        g.strokePath (bp, juce::PathStrokeType (t.px (2.0f)));

        // Draggable node at peak
        const juce::Point<float> nodePos (centerX, area.getY() + area.getHeight() * (1.0f - spread));
        drawNode (g, nodePos, draggingNode, juce::String (juce::roundToInt (cutoffHandle.plain())) + " Hz");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        if (area.contains (e.position))
        {
            draggingNode = true;
            cutoffHandle.beginGesture();
            spreadHandle.beginGesture();
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
            cutoffHandle.endGesture();
            spreadHandle.endGesture();
            draggingNode = false;
        }
    }

private:
    void updateFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - area.getY()) / area.getHeight());

        const float newFreq = fxNormToFreq (normX);
        const float newSpread = normY * 100.0f;

        cutoffHandle.setNormalised (normX);
        spreadHandle.setPlainComplete (newSpread);
        repaint();
    }

    ParamHandle cutoffHandle;
    ParamHandle spreadHandle;
    ParamHandle delay1Handle;
    ParamHandle delay2Handle;
    ParamHandle voicesHandle;
    bool draggingNode = false;
};

class FxChorusPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Chorus"; }

    std::vector<std::string> keys() const override
    {
        return {
            "chorus.rate", "chorus.depth", "chorus.feedback", "chorus.mix",
            "chorus.on", "chorus.sync", "chorus.division", "chorus.delay1", "chorus.delay2",
            "chorus.voices", "chorus.filter_cutoff", "chorus.filter_spread", "chorus.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxChorusView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("chorus.rate");
        addControl ("chorus.depth");
        addControl ("chorus.feedback");
        addControl ("chorus.mix");

        // More controls
        addMoreControl ("chorus.on");
        addMoreControl ("chorus.sync");
        addMoreControl ("chorus.division");
        addMoreControl ("chorus.delay1");
        addMoreControl ("chorus.delay2");
        addMoreControl ("chorus.voices");
        addMoreControl ("chorus.filter_cutoff");
        addMoreControl ("chorus.filter_spread");
        addMoreControl ("chorus.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (110.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxChorusPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (110.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxChorusView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxChorusPanel, Page::fx, 20)
} // namespace synthyboy::ui
