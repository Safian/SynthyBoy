// SynthyBoy - UI panel: Equalizer (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxEqView final : public FxVisualBase
{
public:
    explicit FxEqView (UiContext& context)
        : FxVisualBase (context, "3-Band Parametric EQ (Drag Nodes: X Cutoff, Y Gain)")
    {
        const char* const bands[3] = { "low", "mid", "high" };
        for (int i = 0; i < 3; ++i)
        {
            band (i).mode = std::make_unique<ParamHandle> (ctx, std::string ("eq.") + bands[i] + ".mode");
            band (i).mode->onChange = [this] { repaint(); };

            band (i).cutoff = std::make_unique<ParamHandle> (ctx, std::string ("eq.") + bands[i] + ".cutoff");
            band (i).cutoff->onChange = [this] { repaint(); };

            band (i).gain = std::make_unique<ParamHandle> (ctx, std::string ("eq.") + bands[i] + ".gain");
            band (i).gain->onChange = [this] { repaint(); };

            band (i).res = std::make_unique<ParamHandle> (ctx, std::string ("eq.") + bands[i] + ".resonance");
            band (i).res->onChange = [this] { repaint(); };
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 6, 4);

        // 0 dB center reference line
        const float midY = area.getCentreY();
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (midY), area.getX(), area.getRight());

        // Composite EQ response curve
        const int numPts = 80;
        juce::Path curve;

        for (int i = 0; i <= numPts; ++i)
        {
            const float normX = static_cast<float> (i) / static_cast<float> (numPts);
            const float freq = fxNormToFreq (normX);

            float totalDb = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const float bFreq = band (b).cutoff->plain();
                const float bGain = band (b).gain->plain();
                const int bMode = juce::roundToInt (band (b).mode->plain());
                const float bRes = juce::jlimit (0.1f, 10.0f, band (b).res->plain());

                if (b == 0) // Low band: 0 Shelf, 1 High pass
                {
                    if (bMode == 0)
                    {
                        const float ratio = freq / juce::jmax (20.0f, bFreq);
                        totalDb += bGain / (1.0f + std::pow (ratio, 2.0f * bRes));
                    }
                    else
                    {
                        if (freq < bFreq)
                            totalDb -= 24.0f * std::log2 (bFreq / freq);
                    }
                }
                else if (b == 1) // Mid band: 0 Peak, 1 Notch
                {
                    const float dOct = std::abs (std::log2 (freq / juce::jmax (20.0f, bFreq)));
                    if (bMode == 0)
                        totalDb += bGain * std::exp (- (dOct * dOct * (bRes + 1.0f)));
                    else
                        totalDb -= 36.0f * std::exp (- (dOct * dOct * (bRes * 4.0f + 1.0f)));
                }
                else // High band: 0 Shelf, 1 Low pass
                {
                    if (bMode == 0)
                    {
                        const float ratio = bFreq / juce::jmax (20.0f, freq);
                        totalDb += bGain / (1.0f + std::pow (ratio, 2.0f * bRes));
                    }
                    else
                    {
                        if (freq > bFreq)
                            totalDb -= 24.0f * std::log2 (freq / bFreq);
                    }
                }
            }

            const float ptX = area.getX() + area.getWidth() * normX;
            const float normY = fxDbToNorm (totalDb);
            const float ptY = area.getBottom() - area.getHeight() * normY;

            if (i == 0)
                curve.startNewSubPath (ptX, ptY);
            else
                curve.lineTo (ptX, ptY);
        }

        // Fill under curve
        juce::Path filled = curve;
        filled.lineTo (area.getRight(), midY);
        filled.lineTo (area.getX(), midY);
        filled.closeSubPath();

        g.setColour (t.colours.accentDim.withAlpha (0.15f));
        g.fillPath (filled);

        g.setColour (t.colours.accent);
        g.strokePath (curve, juce::PathStrokeType (t.px (2.0f)));

        // 3 Band handles (Low, Mid, High)
        const char* const nodeLabels[3] = { "L", "M", "H" };
        for (int b = 0; b < 3; ++b)
        {
            const float normX = fxFreqToNorm (band (b).cutoff->plain());
            const int bMode = juce::roundToInt (band (b).mode->plain());
            const bool isGainDimmed = (bMode == 1); // cut/notch mode dims gain (Section 4.4)

            const float normY = isGainDimmed ? 0.5f : fxDbToNorm (band (b).gain->plain());
            const juce::Point<float> nodePos (area.getX() + area.getWidth() * normX,
                                              area.getBottom() - area.getHeight() * normY);

            drawNode (g, nodePos, (draggingBand == b), nodeLabels[b]);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        int closestBand = -1;
        float minDistSq = 1e9f;

        for (int b = 0; b < 3; ++b)
        {
            const float normX = fxFreqToNorm (band (b).cutoff->plain());
            const int bMode = juce::roundToInt (band (b).mode->plain());
            const float normY = (bMode == 1) ? 0.5f : fxDbToNorm (band (b).gain->plain());
            const juce::Point<float> nodePos (area.getX() + area.getWidth() * normX,
                                              area.getBottom() - area.getHeight() * normY);

            const float distSq = e.position.getDistanceSquaredFrom (nodePos);
            if (distSq < minDistSq)
            {
                minDistSq = distSq;
                closestBand = b;
            }
        }

        if (closestBand >= 0 && minDistSq < t.px (30.0f) * t.px (30.0f))
        {
            draggingBand = closestBand;
            band (draggingBand).cutoff->beginGesture();
            band (draggingBand).gain->beginGesture();
            updateBandFromMouse (e.position, area);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingBand >= 0 && draggingBand < 3)
        {
            const auto& t = ctx.theme;
            const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
            updateBandFromMouse (e.position, area);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingBand >= 0 && draggingBand < 3)
        {
            band (draggingBand).cutoff->endGesture();
            band (draggingBand).gain->endGesture();
            draggingBand = -1;
        }
    }

    /** Pinch on a node = its resonance (design 08 section 3); a two-finger scroll / wheel does the same on a desktop. The band is the one
        being dragged, else the node nearest to the pointer. */
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override
    {
        scaleResonance (bandFor (e.position), scaleFactor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        scaleResonance (bandFor (e.position), std::pow (1.5f, wheel.deltaY * 2.0f));
    }

private:
    int bandFor (juce::Point<float> pos) const
    {
        if (draggingBand >= 0)
            return draggingBand;
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        int best = 0;
        float bestDist = 1e9f;
        for (int b = 0; b < 3; ++b)
        {
            const float x = area.getX() + area.getWidth() * fxFreqToNorm (bandData[static_cast<size_t> (b)].cutoff->plain());
            const float d = std::abs (pos.x - x);
            if (d < bestDist)
            {
                bestDist = d;
                best = b;
            }
        }
        return best;
    }

    void scaleResonance (int b, float factor)
    {
        auto& res = *band (b).res;
        if (! res.valid() || ! (factor > 0.0f))
            return;
        res.setPlainComplete (juce::jlimit (res.info().min, res.info().max, res.plain() * factor));
        repaint();
    }

    void updateBandFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float normY = juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight());

        band (draggingBand).cutoff->setNormalised (normX);

        const int bMode = juce::roundToInt (band (draggingBand).mode->plain());
        if (bMode == 0) // Gain active in shelf/peak modes
        {
            const float db = fxNormToDb (normY);
            band (draggingBand).gain->setPlainComplete (db);
        }
        repaint();
    }

    struct BandParams
    {
        std::unique_ptr<ParamHandle> mode;
        std::unique_ptr<ParamHandle> cutoff;
        std::unique_ptr<ParamHandle> gain;
        std::unique_ptr<ParamHandle> res;
    };
    BandParams& band (int i) { return bandData[static_cast<size_t> (i)]; }
    const BandParams& band (int i) const { return bandData[static_cast<size_t> (i)]; }

    std::array<BandParams, 3> bandData;
    int draggingBand = -1;
};

class FxEqPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Equalizer"; }

    std::vector<std::string> keys() const override
    {
        return {
            "eq.low.cutoff", "eq.low.gain", "eq.mid.cutoff", "eq.mid.gain",
            "eq.on", "eq.low.mode", "eq.low.resonance",
            "eq.mid.mode", "eq.mid.resonance",
            "eq.high.mode", "eq.high.cutoff", "eq.high.gain", "eq.high.resonance", "eq.level", "eq.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxEqView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("eq.low.cutoff");
        addControl ("eq.low.gain");
        addControl ("eq.mid.cutoff");
        addControl ("eq.mid.gain");

        // More controls (10 keys)
        addMoreControl ("eq.on");
        addMoreControl ("eq.low.mode");
        addMoreControl ("eq.low.resonance");
        addMoreControl ("eq.mid.mode");
        addMoreControl ("eq.mid.resonance");
        addMoreControl ("eq.high.mode");
        addMoreControl ("eq.high.cutoff");
        addMoreControl ("eq.high.gain");
        addMoreControl ("eq.high.resonance");
        addMoreControl ("eq.level"); // block B16 (G72): the output trim of the module
        addMoreControl ("eq.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (130.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxEqPanel*> (this)->layoutControls (area, true);
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
    std::unique_ptr<FxEqView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxEqPanel, Page::fx, 60)
} // namespace synthyboy::ui
