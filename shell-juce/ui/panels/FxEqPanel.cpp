// SynthyBoy - UI panel: Equalizer (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace synthyboy::ui
{
namespace
{
constexpr int kEqBands = 4; // low, mid, mid 2 (block B34), high

enum class EqKind { shelfLow, shelfHigh, peak, notch, highPass, lowPass, off };

/** The response in dB at `hz` of one band under the EQ law of Serum 2.1.5 (RBJ biquads, the Q laws measured on the plug-in: 13-new-keys.md 4.48), at 48 kHz. */
float serumBandDb (EqKind kind, float fc, float resonancePercent, float gainDb, float hz) noexcept
{
    if (kind == EqKind::off)
        return 0.0f;
    const double x = juce::jlimit (0.0, 1.0, static_cast<double> (resonancePercent) / 100.0);
    const bool shelf = kind == EqKind::shelfLow || kind == EqKind::shelfHigh;
    const double q = shelf ? 0.4 + 1.7417 * x * x * x : 0.1 + 18.9053 * x * x * x * x;
    const double fs = 48000.0, w0 = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, 0.499 * fs, static_cast<double> (fc)) / fs;
    const double a = std::pow (10.0, juce::jlimit (-24.0, 24.0, static_cast<double> (gainDb)) / 40.0), al = std::sin (w0) / (2.0 * q), c = std::cos (w0), s = 2.0 * std::sqrt (a) * al;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (kind)
    {
        case EqKind::peak: b0 = 1 + al * a; b1 = -2 * c; b2 = 1 - al * a; a0 = 1 + al / a; a1 = -2 * c; a2 = 1 - al / a; break;
        case EqKind::shelfLow:
            b0 = a * ((a + 1) - (a - 1) * c + s); b1 = 2 * a * ((a - 1) - (a + 1) * c); b2 = a * ((a + 1) - (a - 1) * c - s);
            a0 = (a + 1) + (a - 1) * c + s; a1 = -2 * ((a - 1) + (a + 1) * c); a2 = (a + 1) + (a - 1) * c - s; break;
        case EqKind::shelfHigh:
            b0 = a * ((a + 1) + (a - 1) * c + s); b1 = -2 * a * ((a - 1) + (a + 1) * c); b2 = a * ((a + 1) + (a - 1) * c - s);
            a0 = (a + 1) - (a - 1) * c + s; a1 = 2 * ((a - 1) - (a + 1) * c); a2 = (a + 1) - (a - 1) * c - s; break;
        case EqKind::highPass: b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
        case EqKind::lowPass: b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
        case EqKind::notch: b0 = 1; b1 = -2 * c; b2 = 1; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
        case EqKind::off: break;
    }
    const double w = 2.0 * juce::MathConstants<double>::pi * static_cast<double> (hz) / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    const double mag = std::abs ((b0 + b1 * z1 + b2 * z2) / (a0 + a1 * z1 + a2 * z2));
    return static_cast<float> (20.0 * std::log10 (std::max (mag, 1.0e-6)));
}
} // namespace

class FxEqView final : public FxVisualBase
{
public:
    explicit FxEqView (UiContext& context)
        : FxVisualBase (context, "4-Band Parametric EQ (Drag Nodes: X Cutoff, Y Gain)")
    {
        law = std::make_unique<ParamHandle> (ctx, "eq.law");
        law->onChange = [this] { repaint(); };
        const char* const bands[kEqBands] = { "low", "mid", "mid2", "high" };
        for (int i = 0; i < kEqBands; ++i)
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
        const auto area = plotArea();

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
            for (int b = 0; b < kEqBands; ++b)
                totalDb += bandResponseDb (b, freq);

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

        // 4 band handles (Low, Mid, Mid 2, High); band 2 is only there when it is not Off
        const char* const nodeLabels[kEqBands] = { "L", "M", "M2", "H" };
        for (int b = 0; b < kEqBands; ++b)
            if (bandActive (b))
                drawNode (g, nodePosition (b, area), (draggingBand == b), nodeLabels[b]);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = plotArea();

        int closestBand = -1;
        float minDistSq = 1e9f;

        for (int b = 0; b < kEqBands; ++b)
        {
            if (! bandActive (b))
                continue;
            const float distSq = e.position.getDistanceSquaredFrom (nodePosition (b, area));
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
        if (draggingBand >= 0 && draggingBand < kEqBands)
            updateBandFromMouse (e.position, plotArea());
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingBand >= 0 && draggingBand < kEqBands)
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
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().reduced (ctx.theme.px (8.0f)).withTrimmedTop (ctx.theme.px (16.0f));
    }

    bool serumLaw() const { return law != nullptr && law->plain() >= 0.5f; }

    /** The mode of a band as the number of its choice (low / high: 0 Shelf, 1 High / Low pass; mid: 0 Peak, 1 Notch; mid 2: 0 Off, 1 Peak, 2 Notch). */
    int modeOf (int b) const { return juce::roundToInt (band (b).mode->plain()); }

    bool bandActive (int b) const { return b != 2 || modeOf (2) != 0; }

    EqKind kindOf (int b) const
    {
        const int m = modeOf (b);
        switch (b)
        {
            case 0: return m == 0 ? EqKind::shelfLow : EqKind::highPass;
            case 1: return m == 0 ? EqKind::peak : EqKind::notch;
            case 2: return m == 0 ? EqKind::off : m == 1 ? EqKind::peak : EqKind::notch;
            default: return m == 0 ? EqKind::shelfHigh : EqKind::lowPass;
        }
    }

    /** The gain knob acts on a shelf and on a peak; a pass filter and a notch dim it (design 08 section 4.4). */
    bool gainUsed (int b) const
    {
        const EqKind k = kindOf (b);
        return k == EqKind::shelfLow || k == EqKind::shelfHigh || k == EqKind::peak;
    }

    juce::Point<float> nodePosition (int b, juce::Rectangle<float> area) const
    {
        const float normX = fxFreqToNorm (band (b).cutoff->plain());
        const float normY = gainUsed (b) ? fxDbToNorm (band (b).gain->plain()) : 0.5f;
        return { area.getX() + area.getWidth() * normX, area.getBottom() - area.getHeight() * normY };
    }

    /** The response of one band in dB at `freq`: the exact RBJ response under the Serum law, a drawing aid under the Vital law (the SVF shapes of the original engine). */
    float bandResponseDb (int b, float freq) const
    {
        const float bFreq = band (b).cutoff->plain();
        const float bGain = band (b).gain->plain();
        const EqKind kind = kindOf (b);
        if (serumLaw())
            return serumBandDb (kind, bFreq, band (b).res->plain(), bGain, freq);
        const float bRes = juce::jlimit (0.1f, 10.0f, band (b).res->plain());
        switch (kind)
        {
            case EqKind::shelfLow:
                return bGain / (1.0f + std::pow (freq / juce::jmax (20.0f, bFreq), 2.0f * bRes));
            case EqKind::highPass:
                return freq < bFreq ? -24.0f * std::log2 (bFreq / freq) : 0.0f;
            case EqKind::peak:
            {
                const float dOct = std::abs (std::log2 (freq / juce::jmax (20.0f, bFreq)));
                return bGain * std::exp (- (dOct * dOct * (bRes + 1.0f)));
            }
            case EqKind::notch:
            {
                const float dOct = std::abs (std::log2 (freq / juce::jmax (20.0f, bFreq)));
                return -36.0f * std::exp (- (dOct * dOct * (bRes * 4.0f + 1.0f)));
            }
            case EqKind::shelfHigh:
                return bGain / (1.0f + std::pow (bFreq / juce::jmax (20.0f, freq), 2.0f * bRes));
            case EqKind::lowPass:
                return freq > bFreq ? -24.0f * std::log2 (freq / bFreq) : 0.0f;
            case EqKind::off:
                break;
        }
        return 0.0f;
    }

    int bandFor (juce::Point<float> pos) const
    {
        if (draggingBand >= 0)
            return draggingBand;
        const auto area = plotArea();
        int best = 0;
        float bestDist = 1e9f;
        for (int b = 0; b < kEqBands; ++b)
        {
            if (! bandActive (b))
                continue;
            const float x = area.getX() + area.getWidth() * fxFreqToNorm (band (b).cutoff->plain());
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

        if (gainUsed (draggingBand))
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

    std::array<BandParams, kEqBands> bandData;
    std::unique_ptr<ParamHandle> law;
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
            "eq.high.mode", "eq.high.cutoff", "eq.high.gain", "eq.high.resonance", "eq.level", "eq.rack",
            "eq.mid2.mode", "eq.mid2.cutoff", "eq.mid2.gain", "eq.mid2.resonance", "eq.law" // block B34
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

        // More controls (16 keys)
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
        // block B34 (REFCOMPARE round 3): the second peak / notch band and the law of the EQ (Vital filters / the RBJ biquads of Serum 2.1.5)
        addMoreControl ("eq.mid2.mode");
        addMoreControl ("eq.mid2.cutoff");
        addMoreControl ("eq.mid2.gain");
        addMoreControl ("eq.mid2.resonance");
        addMoreControl ("eq.law");
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
