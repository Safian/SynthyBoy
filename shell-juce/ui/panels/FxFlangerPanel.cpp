// SynthyBoy - UI panel: Flanger (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxFlangerView final : public FxVisualBase
{
public:
    explicit FxFlangerView (UiContext& context)
        : FxVisualBase (context, "Comb Notch Response (Drag Node: X Center, Y Feedback)"),
          centerHandle (context, "flanger.center"),
          feedHandle (context, "flanger.feedback"),
          depthHandle (context, "flanger.depth")
    {
        centerHandle.onChange = [this] { repaint(); };
        feedHandle.onChange = [this] { repaint(); };
        depthHandle.onChange = [this] { repaint(); };

        // Low tier check (Tier::low masks Flanger)
        const auto tier = qualityForTier (Tier::low);
        if ((tier.fxMask & kFxFlanger) == 0 && (ctx.host.engineCaps() & kCapNone) == 0)
        {
            // If the host environment indicates Tier::low
            if (ctx.host.qualityTier() == Tier::low)
                setTierBypassed (true);
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 6, 3);

        const float normCenter = fxFreqToNorm (centerHandle.plain());
        const float feed = juce::jlimit (-0.95f, 0.95f, feedHandle.plain() / 100.0f);
        const float depth = juce::jlimit (0.0f, 1.0f, depthHandle.plain() / 100.0f);

        // Draw multiple comb notches
        juce::Path comb;
        comb.startNewSubPath (area.getX(), area.getY() + area.getHeight() * 0.3f);

        const int numPts = 80;
        for (int i = 0; i <= numPts; ++i)
        {
            const float normX = static_cast<float> (i) / static_cast<float> (numPts);
            const float diff = (normX - normCenter) * 8.0f;
            const float notch = (1.0f + feed * std::cos (diff * juce::MathConstants<float>::twoPi)) * 0.5f;
            const float y = area.getY() + area.getHeight() * (0.2f + 0.65f * (1.0f - notch * depth));

            const float ptX = area.getX() + area.getWidth() * normX;
            comb.lineTo (ptX, y);
        }

        g.setColour (t.colours.accent);
        g.strokePath (comb, juce::PathStrokeType (t.px (2.0f)));

        // Draggable node at center frequency
        const float normFeed = (feedHandle.plain() + 95.0f) / 190.0f;
        const juce::Point<float> nodePos (area.getX() + area.getWidth() * normCenter,
                                          area.getBottom() - area.getHeight() * normFeed);

        drawNode (g, nodePos, draggingNode,
                  juce::String (juce::roundToInt (centerHandle.plain())) + " Hz");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        if (area.contains (e.position))
        {
            draggingNode = true;
            centerHandle.beginGesture();
            feedHandle.beginGesture();
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
            centerHandle.endGesture();
            feedHandle.endGesture();
            draggingNode = false;
        }
    }

private:
    void updateFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight());

        centerHandle.setNormalised (normX);
        const float feedVal = -95.0f + normY * 190.0f;
        feedHandle.setPlainComplete (feedVal);
        repaint();
    }

    ParamHandle centerHandle;
    ParamHandle feedHandle;
    ParamHandle depthHandle;

    bool draggingNode = false;
};

class FxFlangerPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Flanger"; }

    std::vector<std::string> keys() const override
    {
        return {
            "flanger.rate", "flanger.depth", "flanger.feedback", "flanger.mix",
            "flanger.on", "flanger.sync", "flanger.division", "flanger.center", "flanger.phase_offset", "flanger.level", "flanger.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxFlangerView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("flanger.rate");
        addControl ("flanger.depth");
        addControl ("flanger.feedback");
        addControl ("flanger.mix");

        // More controls (6 keys)
        addMoreControl ("flanger.on");
        addMoreControl ("flanger.sync");
        addMoreControl ("flanger.division");
        addMoreControl ("flanger.center");
        addMoreControl ("flanger.phase_offset");
        addMoreControl ("flanger.level"); // block B16 (G72): the output trim of the module
        addMoreControl ("flanger.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (110.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxFlangerPanel*> (this)->layoutControls (area, true);
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
    std::unique_ptr<FxFlangerView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxFlangerPanel, Page::fx, 80)
} // namespace synthyboy::ui
