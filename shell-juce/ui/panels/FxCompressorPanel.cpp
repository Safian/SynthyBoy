// SynthyBoy - UI panel: Compressor (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"
#include "../model/FxModel.h"

namespace synthyboy::ui
{
class FxCompressorView final : public FxVisualBase
{
public:
    explicit FxCompressorView (UiContext& context)
        : FxVisualBase (context, "3-Band OTT Transfer (Drag Box Edges / Gain Bar, Crossover Tags)"),
          limitHandle (context, "compressor.limit"),
          crossLowHandle (context, "compressor.crossover_low"),
          crossHighHandle (context, "compressor.crossover_high")
    {
        limitHandle.onChange = [this] { repaint(); };
        crossLowHandle.onChange = [this] { repaint(); };
        crossHighHandle.onChange = [this] { repaint(); };
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
        const juce::String lowText = crossoverText (crossLowHandle.plain()), highText = crossoverText (crossHighHandle.plain());
        const juce::String bandNames[3] = { "LOW", "MID", "HIGH" }; // the crossover tags between the bands carry the frequencies

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

        // limit mode (G73): the upper threshold lines are the ceilings of look-ahead limiters
        if (limitHandle.plain() >= 0.5f)
        {
            const auto badge = juce::Rectangle<float> (area.getRight() - t.px (46.0f), area.getBottom() - t.px (26.0f), t.px (42.0f), t.px (14.0f));
            g.setColour (t.colours.warn);
            g.fillRoundedRectangle (badge, t.px (3.0f));
            g.setColour (t.colours.textOnAccent);
            g.setFont (t.font (t.px (9.0f), true));
            g.drawText ("LIMIT", badge, juce::Justification::centred, false);
        }

        // the two crossover tags on the borders between the bands: drag sideways to scrub the frequency
        for (int k = 0; k < 2; ++k)
        {
            const auto tag = crossoverTag (k, area);
            g.setColour (draggingTag == k ? t.colours.accent : t.colours.panelRaised);
            g.fillRoundedRectangle (tag.reduced (0.0f, t.px (5.0f)), t.px (3.0f));
            g.setColour (t.colours.outline);
            g.drawRoundedRectangle (tag.reduced (0.0f, t.px (5.0f)), t.px (3.0f), 1.0f);
            g.setColour (draggingTag == k ? t.colours.textOnAccent : t.colours.text);
            g.setFont (t.font (t.px (9.0f), true));
            g.drawText (k == 0 ? lowText : highText, tag, juce::Justification::centred, false);
        }
    }

    /** The touch target of a crossover tag (k = 0 low, 1 high): 52 x 44 points centred on the border at the top of the view. */
    juce::Rectangle<float> crossoverTag (int k, juce::Rectangle<float> area) const
    {
        const auto& t = ctx.theme;
        const float x = area.getX() + area.getWidth() / 3.0f * static_cast<float> (k + 1);
        return juce::Rectangle<float> (x - t.px (26.0f), area.getY(), t.px (52.0f), t.px (22.0f));
    }

    static juce::String crossoverText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (18.0f));
        for (int k = 0; k < 2; ++k)
            if (crossoverTag (k, area).expanded (0.0f, t.px (11.0f)).contains (e.position))
            {
                draggingTag = k;
                tagDownX = e.position.x;
                tagStartHz = (k == 0 ? crossLowHandle : crossHighHandle).plain();
                (k == 0 ? crossLowHandle : crossHighHandle).beginGesture();
                repaint();
                return;
            }
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
        if (draggingTag >= 0)
        {
            auto& h = draggingTag == 0 ? crossLowHandle : crossHighHandle;
            const float hz = scrubCrossover (tagStartHz, e.position.x - tagDownX, ctx.theme.px (kScrubPointsPerOctave), h.info().min, h.info().max);
            h.setNormalised (toNormalised (h.info(), hz));
            return;
        }
        if (isDragging && activeBand >= 0 && activeBand < 3)
        {
            const auto& t = ctx.theme;
            const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (18.0f));
            updateFromMouse (e.position, area);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingTag >= 0)
        {
            (draggingTag == 0 ? crossLowHandle : crossHighHandle).endGesture();
            draggingTag = -1;
            repaint();
            return;
        }
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
    static constexpr float kScrubPointsPerOctave = 60.0f;
    std::array<BandHandles, 3> bandHandles;
    ParamHandle limitHandle, crossLowHandle, crossHighHandle;
    int activeBand = 0;
    int draggingTag = -1;
    float tagDownX = 0.0f, tagStartHz = 0.0f;
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
            "compressor.limit", "compressor.limit_latency_comp", "compressor.crossover_low", "compressor.crossover_high",
            "compressor.level", "compressor.rack"
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

        // More controls (21 keys)
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
        addMoreControl ("compressor.limit");
        addMoreControl ("compressor.limit_latency_comp");
        addMoreControl ("compressor.crossover_low");
        addMoreControl ("compressor.crossover_high");
        addMoreControl ("compressor.level"); // block B16 (G72): the output trim of the module
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
