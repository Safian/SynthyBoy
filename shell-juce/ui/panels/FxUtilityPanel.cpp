// SynthyBoy - UI panel: Utility (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxUtilityView final : public FxVisualBase
{
public:
    explicit FxUtilityView (UiContext& context)
        : FxVisualBase (context, "Stereo Field & Mono Bass (Drag Center: X Pan, Y Width)"),
          widthHandle (context, "utility.width"),
          panHandle (context, "utility.pan"),
          monoFreqHandle (context, "utility.mono_freq"),
          monoBassHandle (context, "utility.mono_bass")
    {
        widthHandle.onChange = [this] { repaint(); };
        panHandle.onChange = [this] { repaint(); };
        monoFreqHandle.onChange = [this] { repaint(); };
        monoBassHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 4, 4);

        const float midX = area.getCentreX();
        const float midY = area.getCentreY();

        // 1. Mono Bass vertical boundary (20..500 Hz)
        const bool monoOn = (monoBassHandle.plain() >= 0.5f);
        if (monoOn)
        {
            const float normMono = fxFreqToNorm (monoFreqHandle.plain(), 20.0f, 500.0f);
            const float monoX = area.getX() + area.getWidth() * normMono * 0.4f;

            g.setColour (t.colours.warn.withAlpha (0.2f));
            g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), monoX - area.getX(), area.getHeight()));
            g.setColour (t.colours.warn);
            g.drawLine (monoX, area.getY(), monoX, area.getBottom(), t.px (1.5f));

            g.setFont (t.font (t.px (9.0f)));
            g.drawText ("MONO BASS", juce::Rectangle<float> (area.getX() + t.px (2.0f), area.getBottom() - t.px (14.0f), t.px (60.0f), t.px (12.0f)),
                        juce::Justification::bottomLeft, false);
        }

        // 2. Stereo Correlation Goniometer ellipse
        const float widthVal = juce::jlimit (0.0f, 2.0f, widthHandle.plain() / 100.0f);
        const float panVal = juce::jlimit (-1.0f, 1.0f, panHandle.plain() / 100.0f);

        const float radiusY = area.getHeight() * 0.38f;
        const float radiusX = radiusY * widthVal;
        const float centerX = midX + panVal * (area.getWidth() * 0.25f);

        // Draw rotated / shaped stereo ellipse
        juce::Path ellipse;
        ellipse.addEllipse (centerX - radiusX, midY - radiusY, radiusX * 2.0f, radiusY * 2.0f);

        g.setColour (t.colours.accentDim.withAlpha (0.2f));
        g.fillPath (ellipse);

        g.setColour (t.colours.accent);
        g.strokePath (ellipse, juce::PathStrokeType (t.px (2.0f)));

        // Draggable center handle
        drawNode (g, juce::Point<float> (centerX, midY), draggingCenter,
                  "W:" + juce::String (juce::roundToInt (widthHandle.plain())) + "%");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        if (area.contains (e.position))
        {
            draggingCenter = true;
            widthHandle.beginGesture();
            panHandle.beginGesture();
            updateFromMouse (e.position, area);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingCenter)
        {
            const auto& t = ctx.theme;
            const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
            updateFromMouse (e.position, area);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingCenter)
        {
            widthHandle.endGesture();
            panHandle.endGesture();
            draggingCenter = false;
        }
    }

private:
    void updateFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight());

        const float pan = (normX - 0.5f) * 200.0f; // -100 .. +100 %
        const float width = normY * 200.0f;        // 0 .. 200 %

        panHandle.setPlainComplete (pan);
        widthHandle.setPlainComplete (width);
        repaint();
    }

    ParamHandle widthHandle;
    ParamHandle panHandle;
    ParamHandle monoFreqHandle;
    ParamHandle monoBassHandle;

    bool draggingCenter = false;
};

class FxUtilityPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Utility"; }

    std::vector<std::string> keys() const override
    {
        return {
            "utility.width", "utility.pan", "utility.mono_freq", "utility.level",
            "utility.on", "utility.polarity_l", "utility.polarity_r", "utility.lpf", "utility.hpf", "utility.mono_bass", "utility.mix", "utility.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxUtilityView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("utility.width");
        addControl ("utility.pan");
        addControl ("utility.mono_freq");
        addControl ("utility.level");

        // More controls (8 keys)
        addMoreControl ("utility.on");
        addMoreControl ("utility.polarity_l");
        addMoreControl ("utility.polarity_r");
        addMoreControl ("utility.lpf");
        addMoreControl ("utility.hpf");
        addMoreControl ("utility.mono_bass");
        addMoreControl ("utility.mix");
        addMoreControl ("utility.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (120.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxUtilityPanel*> (this)->layoutControls (area, true);
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
    std::unique_ptr<FxUtilityView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxUtilityPanel, Page::fx, 120)
} // namespace synthyboy::ui
