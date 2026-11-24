// SynthyBoy - UI panel: Phaser (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxPhaserView final : public FxVisualBase
{
public:
    explicit FxPhaserView (UiContext& context)
        : FxVisualBase (context, "All-Pass Notch Curve (Drag Node: X Center, Y Feedback)"),
          centerHandle (context, "phaser.center"),
          feedHandle (context, "phaser.feedback"),
          depthHandle (context, "phaser.depth"),
          blendHandle (context, "phaser.blend"),
          depth2Handle (context, "phaser.depth2")
    {
        depth2Handle.onChange = [this] { repaint(); };
        centerHandle.onChange = [this] { repaint(); };
        feedHandle.onChange = [this] { repaint(); };
        depthHandle.onChange = [this] { repaint(); };
        blendHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 6, 3);

        const float normCenter = fxFreqToNorm (centerHandle.plain());
        const float feed = juce::jlimit (-0.95f, 0.95f, feedHandle.plain() / 100.0f);
        const float depth = juce::jlimit (0.0f, 1.0f, depthHandle.plain() / 48.0f);

        // Draw allpass notches (multi-stage); with depth 2 the right channel sweeps by depth + depth 2 and gets its own thinner curve
        const auto notchPath = [&] (float channelDepth)
        {
            juce::Path p;
            p.startNewSubPath (area.getX(), area.getY() + area.getHeight() * 0.35f);
            const int numPts = 80;
            for (int i = 0; i <= numPts; ++i)
            {
                const float normX = static_cast<float> (i) / static_cast<float> (numPts);
                const float diff = (normX - normCenter) * 6.0f;
                const float notch = (1.0f + feed * std::cos (diff * juce::MathConstants<float>::pi * 1.5f)) * 0.5f;
                const float y = area.getY() + area.getHeight() * (0.2f + 0.65f * (1.0f - notch * channelDepth));

                const float ptX = area.getX() + area.getWidth() * normX;
                p.lineTo (ptX, y);
            }
            return p;
        };
        const juce::Path p = notchPath (depth);
        if (depth2Handle.plain() != 0.0f)
        {
            g.setColour (t.colours.mod);
            g.strokePath (notchPath (juce::jlimit (0.0f, 1.0f, (depthHandle.plain() + depth2Handle.plain()) / 48.0f)), juce::PathStrokeType (t.px (1.5f)));
        }

        g.setColour (t.colours.accent);
        g.strokePath (p, juce::PathStrokeType (t.px (2.0f)));

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
    ParamHandle blendHandle;
    ParamHandle depth2Handle;

    bool draggingNode = false;
};

class FxPhaserPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Phaser"; }

    std::vector<std::string> keys() const override
    {
        return {
            "phaser.rate", "phaser.depth", "phaser.center", "phaser.mix",
            "phaser.on", "phaser.sync", "phaser.division", "phaser.feedback", "phaser.blend", "phaser.phase_offset", "phaser.depth2", "phaser.level", "phaser.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxPhaserView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("phaser.rate");
        addControl ("phaser.depth");
        addControl ("phaser.center");
        addControl ("phaser.mix");

        // More controls (8 keys)
        addMoreControl ("phaser.on");
        addMoreControl ("phaser.sync");
        addMoreControl ("phaser.division");
        addMoreControl ("phaser.feedback");
        addMoreControl ("phaser.blend");
        addMoreControl ("phaser.phase_offset");
        addMoreControl ("phaser.depth2");
        addMoreControl ("phaser.level"); // block B16 (G72): the output trim of the module
        addMoreControl ("phaser.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (110.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxPhaserPanel*> (this)->layoutControls (area, true);
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
    std::unique_ptr<FxPhaserView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxPhaserPanel, Page::fx, 90)
} // namespace synthyboy::ui
