// SynthyBoy - UI panel: Compressor (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxCompressorView final : public FxVisualBase
{
public:
    explicit FxCompressorView (UiContext& context)
        : FxVisualBase (context, "3-Band OTT Transfer (Drag Box Edges / Gain Bar)")
    {
        const char* const bands[3] = { "low", "mid", "high" };
        for (size_t i = 0; i < 3; ++i)
        {
            bandHandles[i].lowerThresh = std::make_unique<ParamHandle> (ctx, std::string ("compressor.") + bands[i] + ".lower_threshold");
            bandHandles[i].lowerThresh->onChange = [this] { repaint(); };

            bandHandles[i].upperThresh = std::make_unique<ParamHandle> (ctx, std::string ("compressor.") + bands[i] + ".upper_threshold");
            bandHandles[i].upperThresh->onChange = [this] { repaint(); };

            bandHandles[i].lowerRatio = std::make_unique<ParamHandle> (ctx, std::string ("compressor.") + bands[i] + ".lower_ratio");
            bandHandles[i].lowerRatio->onChange = [this] { repaint(); };

            bandHandles[i].upperRatio = std::make_unique<ParamHandle> (ctx, std::string ("compressor.") + bands[i] + ".upper_ratio");
            bandHandles[i].upperRatio->onChange = [this] { repaint(); };

            bandHandles[i].gain = std::make_unique<ParamHandle> (ctx, std::string ("compressor.") + bands[i] + ".gain");
            bandHandles[i].gain->onChange = [this] { repaint(); };
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (18.0f));

        const float bandW = area.getWidth() / 3.0f;
        const char* const bandNames[3] = { "LOW (<150 Hz)", "MID (150-3k)", "HIGH (>3 kHz)" };

        for (size_t i = 0; i < 3; ++i)
        {
            auto bRect = juce::Rectangle<float> (area.getX() + static_cast<float> (i) * bandW + t.px (2.0f),
                                                 area.getY(),
                                                 bandW - t.px (4.0f),
                                                 area.getHeight());

            // Band frame
            g.setColour (t.colours.panel);
            g.fillRoundedRectangle (bRect, t.px (4.0f));
            g.setColour (t.colours.outline);
            g.drawRoundedRectangle (bRect, t.px (4.0f), 1.0f);

            // Band label
            g.setColour (t.colours.textDim);
            g.setFont (t.font (t.px (9.0f), true));
            g.drawText (bandNames[i], bRect.removeFromTop (t.px (14.0f)), juce::Justification::centred, false);

            const auto inner = bRect.reduced (t.px (4.0f));

            // Threshold normalization (e.g. 0..1)
            const float upNorm = juce::jlimit (0.0f, 1.0f, bandHandles[i].upperThresh->normalised());
            const float lowNorm = juce::jlimit (0.0f, 1.0f, bandHandles[i].lowerThresh->normalised());
            const float gainNorm = juce::jlimit (0.0f, 1.0f, bandHandles[i].gain->normalised());

            const float upY = inner.getY() + inner.getHeight() * (1.0f - upNorm);
            const float lowY = inner.getY() + inner.getHeight() * (1.0f - lowNorm);

            // Upward / Downward transfer box
            const float boxTop = juce::jmin (upY, lowY);
            const float boxBottom = juce::jmax (upY, lowY);
            const auto compBox = juce::Rectangle<float> (inner.getX(), boxTop, inner.getWidth(), juce::jmax (t.px (4.0f), boxBottom - boxTop));

            g.setColour (t.colours.accentDim.withAlpha (0.3f));
            g.fillRoundedRectangle (compBox, t.px (2.0f));

            // Upper threshold line (amber / accent)
            g.setColour (t.colours.accent);
            g.drawLine (inner.getX(), upY, inner.getRight(), upY, t.px (2.0f));

            // Lower threshold line
            g.setColour (t.colours.track);
            g.drawLine (inner.getX(), lowY, inner.getRight(), lowY, t.px (1.5f));

            // Gain marker handle (post-compression trim)
            const float gainX = inner.getX() + inner.getWidth() * gainNorm;
            g.setColour (t.colours.warn);
            g.drawVerticalLine (juce::roundToInt (gainX), inner.getY(), inner.getBottom());

            // Gain text
            g.setColour (t.colours.text);
            g.setFont (t.font (t.px (9.0f)));
            g.drawText (juce::String (bandHandles[i].gain->plain(), 1) + " dB",
                        inner.withTrimmedTop (inner.getHeight() - t.px (12.0f)),
                        juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (18.0f));
        if (area.contains (e.position))
        {
            activeBand = juce::jlimit (0, 2, static_cast<int> ((e.position.x - area.getX()) / (area.getWidth() / 3.0f)));
            bandHandles[static_cast<size_t> (activeBand)].upperThresh->beginGesture();
            bandHandles[static_cast<size_t> (activeBand)].gain->beginGesture();
            isDragging = true;
            updateFromMouse (e.position, area);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isDragging && activeBand >= 0 && activeBand < 3)
        {
            const auto& t = ctx.theme;
            const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (18.0f));
            updateFromMouse (e.position, area);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (isDragging && activeBand >= 0 && activeBand < 3)
        {
            bandHandles[static_cast<size_t> (activeBand)].upperThresh->endGesture();
            bandHandles[static_cast<size_t> (activeBand)].gain->endGesture();
            isDragging = false;
        }
    }

private:
    void updateFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float bandW = area.getWidth() / 3.0f;
        const float localX = pos.x - (area.getX() + static_cast<float> (activeBand) * bandW);
        const float normX = juce::jlimit (0.0f, 1.0f, localX / bandW);
        const float normY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - area.getY()) / area.getHeight());

        bandHandles[static_cast<size_t> (activeBand)].gain->setNormalised (normX);
        bandHandles[static_cast<size_t> (activeBand)].upperThresh->setNormalised (normY);
        repaint();
    }

    struct BandHandles
    {
        std::unique_ptr<ParamHandle> lowerThresh;
        std::unique_ptr<ParamHandle> upperThresh;
        std::unique_ptr<ParamHandle> lowerRatio;
        std::unique_ptr<ParamHandle> upperRatio;
        std::unique_ptr<ParamHandle> gain;
    };
    std::array<BandHandles, 3> bandHandles;
    int activeBand = 0;
    bool isDragging = false;
};

class FxCompressorPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Compressor"; }

    std::vector<std::string> keys() const override
    {
        return {
            "compressor.attack", "compressor.release", "compressor.mid.gain", "compressor.mix",
            "compressor.on", "compressor.bands",
            "compressor.low.lower_threshold", "compressor.low.upper_threshold", "compressor.low.lower_ratio", "compressor.low.upper_ratio", "compressor.low.gain",
            "compressor.mid.lower_threshold", "compressor.mid.upper_threshold", "compressor.mid.lower_ratio", "compressor.mid.upper_ratio",
            "compressor.high.lower_threshold", "compressor.high.upper_threshold", "compressor.high.lower_ratio", "compressor.high.upper_ratio", "compressor.high.gain",
            "compressor.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxCompressorView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("compressor.attack");
        addControl ("compressor.release");
        addControl ("compressor.mid.gain");
        addControl ("compressor.mix");

        // More controls (17 keys)
        addMoreControl ("compressor.on");
        addMoreControl ("compressor.bands");
        addMoreControl ("compressor.low.lower_threshold");
        addMoreControl ("compressor.low.upper_threshold");
        addMoreControl ("compressor.low.lower_ratio");
        addMoreControl ("compressor.low.upper_ratio");
        addMoreControl ("compressor.low.gain");
        addMoreControl ("compressor.mid.lower_threshold");
        addMoreControl ("compressor.mid.upper_threshold");
        addMoreControl ("compressor.mid.lower_ratio");
        addMoreControl ("compressor.mid.upper_ratio");
        addMoreControl ("compressor.high.lower_threshold");
        addMoreControl ("compressor.high.upper_threshold");
        addMoreControl ("compressor.high.lower_ratio");
        addMoreControl ("compressor.high.upper_ratio");
        addMoreControl ("compressor.high.gain");
        addMoreControl ("compressor.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (130.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxCompressorPanel*> (this)->layoutControls (area, true);
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
    std::unique_ptr<FxCompressorView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxCompressorPanel, Page::fx, 30)
} // namespace synthyboy::ui
