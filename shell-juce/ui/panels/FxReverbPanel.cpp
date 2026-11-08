// SynthyBoy - UI panel: Reverb (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxReverbView final : public FxVisualBase
{
public:
    explicit FxReverbView (UiContext& context)
        : FxVisualBase (context, "Damping EQ & Pre-Cut (Drag Shelves / Curtains)"),
          lowCutHandle (context, "reverb.pre_low_cut"),
          highCutHandle (context, "reverb.pre_high_cut"),
          lowShelfCutHandle (context, "reverb.low_shelf_cutoff"),
          lowShelfGainHandle (context, "reverb.low_shelf_gain"),
          highShelfCutHandle (context, "reverb.high_shelf_cutoff"),
          highShelfGainHandle (context, "reverb.high_shelf_gain"),
          sizeHandle (context, "reverb.size"),
          decayHandle (context, "reverb.decay"),
          forceEnableHandle (context, "reverb.force_enable")
    {
        lowCutHandle.onChange = [this] { repaint(); };
        highCutHandle.onChange = [this] { repaint(); };
        lowShelfCutHandle.onChange = [this] { repaint(); };
        lowShelfGainHandle.onChange = [this] { repaint(); };
        highShelfCutHandle.onChange = [this] { repaint(); };
        highShelfGainHandle.onChange = [this] { repaint(); };
        sizeHandle.onChange = [this] { repaint(); };
        decayHandle.onChange = [this] { repaint(); };
        forceEnableHandle.onChange = [this] { updateTierState(); repaint(); };

        updateTierState();
    }

    void updateTierState()
    {
        #if defined(SYNTHYBOY_DEFAULT_TIER) && SYNTHYBOY_DEFAULT_TIER == low
            const bool force = (forceEnableHandle.plain() >= 0.5f);
            setTierBypassed (! force);
        #else
            setTierBypassed (false);
        #endif
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 5, 3);

        // Pre-cut curtain regions
        const float normLoCut = fxFreqToNorm (lowCutHandle.plain());
        const float normHiCut = fxFreqToNorm (highCutHandle.plain());
        const float cutLeftX = area.getX() + area.getWidth() * normLoCut;
        const float cutRightX = area.getX() + area.getWidth() * normHiCut;

        // Dim outside pre-cut range
        g.setColour (t.colours.track.withAlpha (0.3f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), cutLeftX - area.getX(), area.getHeight()));
        g.fillRect (juce::Rectangle<float> (cutRightX, area.getY(), area.getRight() - cutRightX, area.getHeight()));

        // Pre-cut divider lines
        g.setColour (t.colours.warn);
        g.drawLine (cutLeftX, area.getY(), cutLeftX, area.getBottom(), t.px (1.5f));
        g.drawLine (cutRightX, area.getY(), cutRightX, area.getBottom(), t.px (1.5f));

        // Damping Shelf EQ curve (-6..0 dB)
        const float normLoShelf = fxFreqToNorm (lowShelfCutHandle.plain());
        const float normHiShelf = fxFreqToNorm (highShelfCutHandle.plain());

        // Range is -6..0 dB
        const float loGainNorm = juce::jlimit (0.0f, 1.0f, (lowShelfGainHandle.plain() + 6.0f) / 6.0f);
        const float hiGainNorm = juce::jlimit (0.0f, 1.0f, (highShelfGainHandle.plain() + 6.0f) / 6.0f);

        const float loX = area.getX() + area.getWidth() * normLoShelf;
        const float loY = area.getBottom() - area.getHeight() * (0.3f + 0.6f * loGainNorm);

        const float hiX = area.getX() + area.getWidth() * normHiShelf;
        const float hiY = area.getBottom() - area.getHeight() * (0.3f + 0.6f * hiGainNorm);

        juce::Path shelf;
        shelf.startNewSubPath (area.getX(), loY);
        shelf.lineTo (loX, loY);
        shelf.cubicTo (loX + t.px (20.0f), loY,
                       hiX - t.px (20.0f), hiY,
                       hiX, hiY);
        shelf.lineTo (area.getRight(), hiY);

        g.setColour (t.colours.accent);
        g.strokePath (shelf, juce::PathStrokeType (t.px (2.0f)));

        // Draggable shelf nodes
        drawNode (g, juce::Point<float> (loX, loY), draggingLoShelf, "Lo Shelf");
        drawNode (g, juce::Point<float> (hiX, hiY), draggingHiShelf, "Hi Shelf");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        const float normLoShelf = fxFreqToNorm (lowShelfCutHandle.plain());
        const float normHiShelf = fxFreqToNorm (highShelfCutHandle.plain());
        const float loGainNorm = (lowShelfGainHandle.plain() + 6.0f) / 6.0f;
        const float hiGainNorm = (highShelfGainHandle.plain() + 6.0f) / 6.0f;

        const juce::Point<float> loPos (area.getX() + area.getWidth() * normLoShelf,
                                        area.getBottom() - area.getHeight() * (0.3f + 0.6f * loGainNorm));
        const juce::Point<float> hiPos (area.getX() + area.getWidth() * normHiShelf,
                                        area.getBottom() - area.getHeight() * (0.3f + 0.6f * hiGainNorm));

        if (e.position.getDistanceSquaredFrom (loPos) < t.px (24.0f) * t.px (24.0f))
        {
            draggingLoShelf = true;
            lowShelfCutHandle.beginGesture();
            lowShelfGainHandle.beginGesture();
            updateLoShelf (e.position, area);
        }
        else if (e.position.getDistanceSquaredFrom (hiPos) < t.px (24.0f) * t.px (24.0f))
        {
            draggingHiShelf = true;
            highShelfCutHandle.beginGesture();
            highShelfGainHandle.beginGesture();
            updateHiShelf (e.position, area);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        if (draggingLoShelf)
            updateLoShelf (e.position, area);
        else if (draggingHiShelf)
            updateHiShelf (e.position, area);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingLoShelf)
        {
            lowShelfCutHandle.endGesture();
            lowShelfGainHandle.endGesture();
            draggingLoShelf = false;
        }
        if (draggingHiShelf)
        {
            highShelfCutHandle.endGesture();
            highShelfGainHandle.endGesture();
            draggingHiShelf = false;
        }
    }

private:
    void updateLoShelf (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight());

        lowShelfCutHandle.setNormalised (normX);
        const float gainDb = -6.0f + juce::jlimit (0.0f, 1.0f, (normY - 0.3f) / 0.6f) * 6.0f;
        lowShelfGainHandle.setPlainComplete (juce::jlimit (-6.0f, 0.0f, gainDb));
        repaint();
    }

    void updateHiShelf (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight());

        highShelfCutHandle.setNormalised (normX);
        const float gainDb = -6.0f + juce::jlimit (0.0f, 1.0f, (normY - 0.3f) / 0.6f) * 6.0f;
        highShelfGainHandle.setPlainComplete (juce::jlimit (-6.0f, 0.0f, gainDb));
        repaint();
    }

    ParamHandle lowCutHandle;
    ParamHandle highCutHandle;
    ParamHandle lowShelfCutHandle;
    ParamHandle lowShelfGainHandle;
    ParamHandle highShelfCutHandle;
    ParamHandle highShelfGainHandle;
    ParamHandle sizeHandle;
    ParamHandle decayHandle;
    ParamHandle forceEnableHandle;

    bool draggingLoShelf = false;
    bool draggingHiShelf = false;
};

class FxReverbPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Reverb"; }

    std::vector<std::string> keys() const override
    {
        return {
            "reverb.size", "reverb.decay", "reverb.predelay", "reverb.mix",
            "reverb.on", "reverb.pre_low_cut", "reverb.pre_high_cut",
            "reverb.low_shelf_cutoff", "reverb.low_shelf_gain", "reverb.high_shelf_cutoff", "reverb.high_shelf_gain",
            "reverb.chorus_amount", "reverb.chorus_rate", "reverb.force_enable", "reverb.level", "reverb.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxReverbView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("reverb.size");
        addControl ("reverb.decay");
        addControl ("reverb.predelay");
        addControl ("reverb.mix");

        // More controls (11 keys)
        addMoreControl ("reverb.on");
        addMoreControl ("reverb.pre_low_cut");
        addMoreControl ("reverb.pre_high_cut");
        addMoreControl ("reverb.low_shelf_cutoff");
        addMoreControl ("reverb.low_shelf_gain");
        addMoreControl ("reverb.high_shelf_cutoff");
        addMoreControl ("reverb.high_shelf_gain");
        addMoreControl ("reverb.chorus_amount");
        addMoreControl ("reverb.chorus_rate");
        addMoreControl ("reverb.force_enable");
        addMoreControl ("reverb.level"); // block B16 (G72): the output trim of the module
        addMoreControl ("reverb.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (130.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxReverbPanel*> (this)->layoutControls (area, true);
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
    std::unique_ptr<FxReverbView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxReverbPanel, Page::fx, 100)
} // namespace synthyboy::ui
