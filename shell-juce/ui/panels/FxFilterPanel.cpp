// SynthyBoy - UI panel: FX Filter (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxFilterView final : public FxVisualBase
{
public:
    explicit FxFilterView (UiContext& context)
        : FxVisualBase (context, "Filter Response (Drag Node: X Cutoff, Y Resonance)"),
          cutoffHandle (context, "fxfilter.cutoff"),
          resHandle (context, "fxfilter.resonance"),
          driveHandle (context, "fxfilter.drive"),
          blendHandle (context, "fxfilter.blend"),
          modelHandle (context, "fxfilter.model")
    {
        cutoffHandle.onChange = [this] { repaint(); };
        resHandle.onChange = [this] { repaint(); };
        driveHandle.onChange = [this] { repaint(); };
        blendHandle.onChange = [this] { repaint(); };
        modelHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 5, 4);

        const float normCutoff = fxFreqToNorm (cutoffHandle.plain());
        const float normRes = juce::jlimit (0.0f, 1.0f, resHandle.plain() / 100.0f);
        const float cutoffX = area.getX() + area.getWidth() * normCutoff;
        const float peakY = area.getBottom() - area.getHeight() * (0.35f + 0.55f * normRes);
        const float passY = area.getBottom() - area.getHeight() * 0.35f;

        // Response curve (Lowpass / Bandpass / Highpass blend)
        const float blend = juce::jlimit (0.0f, 1.0f, blendHandle.plain() / 100.0f);

        juce::Path p;
        p.startNewSubPath (area.getX(), passY * (1.0f - blend) + area.getBottom() * blend);

        // Approach to cutoff
        p.cubicTo (cutoffX - area.getWidth() * 0.15f, passY * (1.0f - blend) + area.getBottom() * blend,
                   cutoffX - area.getWidth() * 0.05f, peakY,
                   cutoffX, peakY);

        // Roll-off after cutoff
        p.cubicTo (cutoffX + area.getWidth() * 0.05f, peakY,
                   cutoffX + area.getWidth() * 0.2f, area.getBottom() * (1.0f - blend) + passY * blend,
                   area.getRight(), area.getBottom() * (1.0f - blend) + passY * blend);

        // Fill under curve
        juce::Path filled = p;
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        g.setColour (t.colours.accentDim.withAlpha (0.15f));
        g.fillPath (filled);

        g.setColour (t.colours.accent);
        g.strokePath (p, juce::PathStrokeType (t.px (2.0f)));

        // Cutoff / Resonance draggable node
        drawNode (g, juce::Point<float> (cutoffX, peakY), draggingNode,
                  juce::String (juce::roundToInt (cutoffHandle.plain())) + " Hz");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        if (area.contains (e.position))
        {
            draggingNode = true;
            cutoffHandle.beginGesture();
            resHandle.beginGesture();
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
            resHandle.endGesture();
            draggingNode = false;
        }
    }

private:
    void updateFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight());

        cutoffHandle.setNormalised (normX);
        resHandle.setPlainComplete (normY * 100.0f);
        repaint();
    }

    ParamHandle cutoffHandle;
    ParamHandle resHandle;
    ParamHandle driveHandle;
    ParamHandle blendHandle;
    ParamHandle modelHandle;

    bool draggingNode = false;
};

class FxFilterPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "FX Filter"; }

    std::vector<std::string> keys() const override
    {
        return {
            "fxfilter.cutoff", "fxfilter.resonance", "fxfilter.drive", "fxfilter.mix",
            "fxfilter.on", "fxfilter.model", "fxfilter.style", "fxfilter.blend", "fxfilter.keytrack", "fxfilter.comb_offset",
            "fxfilter.formant.x", "fxfilter.formant.y", "fxfilter.formant.resonance", "fxfilter.formant.spread", "fxfilter.formant.transpose",
            "fxfilter.var", "fxfilter.pan", "fxfilter.level", "fxfilter.clean_mode", "fxfilter.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxFilterView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("fxfilter.cutoff");
        addControl ("fxfilter.resonance");
        addControl ("fxfilter.drive");
        addControl ("fxfilter.mix");

        // More controls (15 keys)
        addMoreControl ("fxfilter.on");
        addMoreControl ("fxfilter.model");
        addMoreControl ("fxfilter.style");
        addMoreControl ("fxfilter.blend");
        addMoreControl ("fxfilter.keytrack");
        addMoreControl ("fxfilter.comb_offset");
        addMoreControl ("fxfilter.formant.x");
        addMoreControl ("fxfilter.formant.y");
        addMoreControl ("fxfilter.formant.resonance");
        addMoreControl ("fxfilter.formant.spread");
        addMoreControl ("fxfilter.formant.transpose");
        addMoreControl ("fxfilter.var");
        addMoreControl ("fxfilter.pan");
        addMoreControl ("fxfilter.level");
        addMoreControl ("fxfilter.clean_mode"); // block B16 (G54)
        addMoreControl ("fxfilter.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (130.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxFilterPanel*> (this)->layoutControls (area, true);
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
    std::unique_ptr<FxFilterView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxFilterPanel, Page::fx, 70)
} // namespace synthyboy::ui
