// SynthyBoy - UI panel: Distortion (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxDistortionView final : public FxVisualBase
{
public:
    explicit FxDistortionView (UiContext& context)
        : FxVisualBase (context, "Waveshaper Curve & Pre/Post Filter (Drag Y: Drive, Drag Node: Cut/Res)"),
          driveHandle (context, "distortion.drive"),
          typeHandle (context, "distortion.type"),
          cutoffHandle (context, "distortion.filter_cutoff"),
          resHandle (context, "distortion.filter_resonance"),
          orderHandle (context, "distortion.filter_order")
    {
        driveHandle.onChange = [this] { repaint(); };
        typeHandle.onChange = [this] { repaint(); };
        cutoffHandle.onChange = [this] { repaint(); };
        resHandle.onChange = [this] { repaint(); };
        orderHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 4, 4);

        // Axis center lines
        const float midX = area.getCentreX();
        const float midY = area.getCentreY();
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (midY), area.getX(), area.getRight());
        g.drawVerticalLine (juce::roundToInt (midX), area.getY(), area.getBottom());

        // Waveshaper In vs Out curve
        const int type = juce::roundToInt (typeHandle.plain());
        const float driveDb = driveHandle.plain();
        const float driveLinear = std::pow (10.0f, driveDb / 20.0f);

        juce::Path wave;
        const int numPts = 64;
        for (int i = 0; i <= numPts; ++i)
        {
            const float normIn = static_cast<float> (i) / static_cast<float> (numPts);
            const float in = (normIn - 0.5f) * 2.0f; // -1 .. +1
            const float driven = in * driveLinear;
            float out = 0.0f;

            switch (type)
            {
                case 0: // Soft clip
                    out = std::tanh (driven);
                    break;
                case 1: // Hard clip
                    out = juce::jlimit (-1.0f, 1.0f, driven);
                    break;
                case 2: // Linear fold
                {
                    float f = driven;
                    while (std::abs (f) > 1.0f)
                        f = (f > 1.0f) ? (2.0f - f) : (-2.0f - f);
                    out = f;
                    break;
                }
                case 3: // Sine fold
                    out = std::sin (driven * juce::MathConstants<float>::pi * 0.5f);
                    break;
                case 4: // Bit crush
                {
                    const float steps = 8.0f;
                    out = std::round (driven * steps) / steps;
                    out = juce::jlimit (-1.0f, 1.0f, out);
                    break;
                }
                case 5: // Down sample
                    out = std::floor (driven * 4.0f) / 4.0f;
                    out = juce::jlimit (-1.0f, 1.0f, out);
                    break;
                default:
                    out = juce::jlimit (-1.0f, 1.0f, driven);
                    break;
            }

            const float ptX = area.getX() + area.getWidth() * normIn;
            const float ptY = midY - (out * (area.getHeight() * 0.45f));
            if (i == 0)
                wave.startNewSubPath (ptX, ptY);
            else
                wave.lineTo (ptX, ptY);
        }

        g.setColour (t.colours.accent);
        g.strokePath (wave, juce::PathStrokeType (t.px (2.0f)));

        // Overlaid Filter Response if order != None (0)
        const int filterOrder = juce::roundToInt (orderHandle.plain());
        if (filterOrder > 0)
        {
            const float normCutoff = fxFreqToNorm (cutoffHandle.plain());
            const float normRes = juce::jlimit (0.0f, 1.0f, resHandle.plain() / 100.0f);
            const float nodeX = area.getX() + area.getWidth() * normCutoff;
            const float nodeY = area.getBottom() - area.getHeight() * (0.15f + 0.7f * normRes);

            // Filter indicator line
            g.setColour (t.colours.warn.withAlpha (0.7f));
            g.drawVerticalLine (juce::roundToInt (nodeX), area.getY(), area.getBottom());

            drawNode (g, juce::Point<float> (nodeX, nodeY), draggingNode,
                      juce::String (juce::roundToInt (cutoffHandle.plain())) + " Hz");
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        if (area.contains (e.position))
        {
            lastMouseY = e.position.y;
            const int filterOrder = juce::roundToInt (orderHandle.plain());
            const float normCutoff = fxFreqToNorm (cutoffHandle.plain());
            const float nodeX = area.getX() + area.getWidth() * normCutoff;

            if (filterOrder > 0 && std::abs (e.position.x - nodeX) < t.px (18.0f))
            {
                draggingNode = true;
                cutoffHandle.beginGesture();
                resHandle.beginGesture();
                updateFilterFromMouse (e.position, area);
            }
            else
            {
                draggingDrive = true;
                driveHandle.beginGesture();
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        if (draggingNode)
        {
            updateFilterFromMouse (e.position, area);
        }
        else if (draggingDrive)
        {
            const float dy = lastMouseY - e.position.y;
            lastMouseY = e.position.y;
            const float current = driveHandle.plain();
            const float nextVal = juce::jlimit (-30.0f, 30.0f, current + dy * 0.5f);
            driveHandle.setPlainComplete (nextVal);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingNode)
        {
            cutoffHandle.endGesture();
            resHandle.endGesture();
            draggingNode = false;
        }
        if (draggingDrive)
        {
            driveHandle.endGesture();
            draggingDrive = false;
        }
    }

private:
    void updateFilterFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight());

        cutoffHandle.setNormalised (normX);
        resHandle.setPlainComplete (normY * 100.0f);
        repaint();
    }

    ParamHandle driveHandle;
    ParamHandle typeHandle;
    ParamHandle cutoffHandle;
    ParamHandle resHandle;
    ParamHandle orderHandle;

    bool draggingDrive = false;
    bool draggingNode = false;
    float lastMouseY = 0.0f;
};

class FxDistortionPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Distortion"; }

    std::vector<std::string> keys() const override
    {
        return {
            "distortion.drive", "distortion.mix", "distortion.filter_cutoff", "distortion.filter_resonance",
            "distortion.on", "distortion.type", "distortion.filter_order", "distortion.filter_blend", "distortion.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxDistortionView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("distortion.drive");
        addControl ("distortion.mix");
        addControl ("distortion.filter_cutoff");
        addControl ("distortion.filter_resonance");

        // More controls (5 keys)
        addMoreControl ("distortion.on");
        addMoreControl ("distortion.type");
        addMoreControl ("distortion.filter_order");
        addMoreControl ("distortion.filter_blend");
        addMoreControl ("distortion.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (130.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxDistortionPanel*> (this)->layoutControls (area, true);
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
    std::unique_ptr<FxDistortionView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxDistortionPanel, Page::fx, 50)
} // namespace synthyboy::ui
