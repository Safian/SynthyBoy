// SynthyBoy - UI: Filter big visual, formant XY pad, PZ curve, type picker and routing views.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FilterModels.h"
#include "../ParamHandle.h"
#include "../Theme.h"
#include "../UiContext.h"

namespace synthyboy::ui
{

// ---- PZ SVF Curve ------------------------------------------------------------------------------------------------------------------

class PzSvfCurve
{
public:
    PzSvfCurve() { reset(); }

    void reset()
    {
        points = {
            { 0.0f, 0.65f },
            { 0.35f, 0.65f },
            { 0.5f, 0.85f },
            { 0.7f, 0.35f },
            { 1.0f, 0.15f }
        };
    }

    const std::vector<juce::Point<float>>& getPoints() const noexcept { return points; }

    void setPoint (int index, juce::Point<float> p)
    {
        if (index >= 0 && index < static_cast<int> (points.size()))
        {
            points[static_cast<size_t> (index)].x = std::clamp (p.x, 0.0f, 1.0f);
            points[static_cast<size_t> (index)].y = std::clamp (p.y, 0.0f, 1.0f);
            sortPoints();
        }
    }

    int findNearestPoint (juce::Point<float> pos, float maxDist) const
    {
        int nearest = -1;
        float minDist = maxDist;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float d = pos.getDistanceFrom (points[i]);
            if (d < minDist)
            {
                minDist = d;
                nearest = static_cast<int> (i);
            }
        }
        return nearest;
    }

    void addPoint (juce::Point<float> p)
    {
        points.push_back ({ std::clamp (p.x, 0.0f, 1.0f), std::clamp (p.y, 0.0f, 1.0f) });
        sortPoints();
    }

    void removePoint (int index)
    {
        if (points.size() > 2 && index >= 0 && index < static_cast<int> (points.size()))
            points.erase (points.begin() + index);
    }

    float evaluate (float normX) const
    {
        if (points.empty())
            return 0.5f;
        if (normX <= points.front().x)
            return points.front().y;
        if (normX >= points.back().x)
            return points.back().y;

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            if (normX >= points[i].x && normX <= points[i + 1].x)
            {
                const float span = points[i + 1].x - points[i].x;
                if (span < 1e-5f)
                    return points[i].y;
                const float t = (normX - points[i].x) / span;
                // smoothstep interpolation
                const float smoothT = t * t * (3.0f - 2.0f * t);
                return (1.0f - smoothT) * points[i].y + smoothT * points[i + 1].y;
            }
        }
        return points.back().y;
    }

private:
    void sortPoints()
    {
        std::stable_sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.x < b.x; });
    }

    std::vector<juce::Point<float>> points;
};

// ---- FilterResponseView ------------------------------------------------------------------------------------------------------------

class FilterResponseView final : public juce::Component
{
public:
    enum class Mode { resp, plusFft, phase };

    FilterResponseView (UiContext& context,
                        ParamHandle& cutoffHandle,
                        ParamHandle& resHandle,
                        ParamHandle& onHandle,
                        ParamHandle& modelHandle,
                        ParamHandle& styleHandle,
                        ParamHandle& blendHandle,
                        ParamHandle& varHandle,
                        ParamHandle* formantX = nullptr,
                        ParamHandle* formantY = nullptr,
                        ParamHandle* formantTranspose = nullptr)
        : ctx (context),
          cutoff (cutoffHandle),
          resonance (resHandle),
          onParam (onHandle),
          modelParam (modelHandle),
          styleParam (styleHandle),
          blendParam (blendHandle),
          varParam (varHandle),
          fxParam (formantX),
          fyParam (formantY),
          ftParam (formantTranspose)
    {
        setOpaque (false);

        modeButton.setButtonText ("RESP");
        modeButton.onClick = [this] { cycleMode(); };
        addAndMakeVisible (modeButton);

        pzResetButton.setButtonText ("Reset");
        pzResetButton.onClick = [this] { pzCurve.reset(); repaint(); };
        addChildComponent (pzResetButton);

        const auto updateCb = [this] { repaint(); };
        cutoff.onChange = updateCb;
        resonance.onChange = updateCb;
        onParam.onChange = updateCb;
        modelParam.onChange = updateCb;
        styleParam.onChange = updateCb;
        blendParam.onChange = updateCb;
        varParam.onChange = updateCb;
        if (fxParam != nullptr) fxParam->onChange = updateCb;
        if (fyParam != nullptr) fyParam->onChange = updateCb;
        if (ftParam != nullptr) ftParam->onChange = updateCb;
    }

    void cycleMode()
    {
        switch (currentMode)
        {
            case Mode::resp:    setMode (Mode::plusFft); break;
            case Mode::plusFft: setMode (Mode::phase); break;
            case Mode::phase:   setMode (Mode::resp); break;
        }
    }

    void setMode (Mode newMode)
    {
        currentMode = newMode;
        switch (currentMode)
        {
            case Mode::resp:    modeButton.setButtonText ("RESP"); break;
            case Mode::plusFft: modeButton.setButtonText ("+FFT"); break;
            case Mode::phase:   modeButton.setButtonText ("PHASE"); break;
        }
        repaint();
    }

    Mode mode() const noexcept { return currentMode; }

    void setHasInputs (bool has)
    {
        if (inputsRouted != has)
        {
            inputsRouted = has;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto& col = t.colours;
        const auto bounds = getLocalBounds().toFloat();
        const float radius = t.px (8.0f);

        // Background
        g.setColour (col.panelRaised);
        g.fillRoundedRectangle (bounds, radius);

        // Border
        g.setColour (col.outline);
        g.drawRoundedRectangle (bounds, radius, 1.0f);

        const auto plotArea = bounds.reduced (t.px (6.0f), t.px (8.0f));
        if (plotArea.getWidth() <= 10.0f || plotArea.getHeight() <= 10.0f)
            return;

        const bool isOn = (onParam.valid() ? onParam.plain() >= 0.5f : true);
        const int model = (modelParam.valid() ? static_cast<int> (modelParam.plain()) : 3);
        const int style = (styleParam.valid() ? static_cast<int> (styleParam.plain()) : 0);
        const float fc = (cutoff.valid() ? cutoff.plain() : 1000.0f);
        const float resVal = (resonance.valid() ? resonance.plain() : 20.0f);
        const float blendVal = (blendParam.valid() ? blendParam.plain() : 0.0f);
        const float varVal = (varParam.valid() ? varParam.plain() : 50.0f);
        const float fX = (fxParam != nullptr && fxParam->valid() ? fxParam->plain() : 50.0f);
        const float fY = (fyParam != nullptr && fyParam->valid() ? fyParam->plain() : 50.0f);
        const float fTrans = (ftParam != nullptr && ftParam->valid() ? ftParam->plain() : 0.0f);

        // Grid lines: 100 Hz, 1 kHz, 10 kHz; 0 dB
        g.setColour (col.outline.withAlpha (0.4f));
        const float fGrid[] = { 100.0f, 1000.0f, 10000.0f };
        for (const float fg : fGrid)
        {
            const float x = plotArea.getX() + frequencyToNorm (fg) * plotArea.getWidth();
            g.drawVerticalLine (juce::roundToInt (x), plotArea.getY(), plotArea.getBottom());
        }
        const float y0dB = plotArea.getY() + plotArea.getHeight() * (18.0f / 54.0f);
        g.drawHorizontalLine (juce::roundToInt (y0dB), plotArea.getX(), plotArea.getRight());

        // Subdued labels
        g.setFont (t.font (t.px (10.0f)));
        g.setColour (col.textDim.withAlpha (0.6f));
        g.drawText ("100Hz", juce::roundToInt (plotArea.getX() + frequencyToNorm (100.0f) * plotArea.getWidth()) + 2,
                    juce::roundToInt (plotArea.getBottom() - t.px (12.0f)), 40, 12, juce::Justification::left);
        g.drawText ("1kHz", juce::roundToInt (plotArea.getX() + frequencyToNorm (1000.0f) * plotArea.getWidth()) + 2,
                    juce::roundToInt (plotArea.getBottom() - t.px (12.0f)), 40, 12, juce::Justification::left);
        g.drawText ("10kHz", juce::roundToInt (plotArea.getX() + frequencyToNorm (10000.0f) * plotArea.getWidth()) + 2,
                    juce::roundToInt (plotArea.getBottom() - t.px (12.0f)), 40, 12, juce::Justification::left);
        g.drawText ("0dB", juce::roundToInt (plotArea.getX() + 2), juce::roundToInt (y0dB - t.px (12.0f)), 30, 12, juce::Justification::left);

        // Dimmed look if filter is OFF or unrouted
        const float alpha = (! isOn ? 0.35f : (inputsRouted ? 1.0f : 0.45f));

        // +FFT Mode: stylized spectral activity under the curve
        if (isOn && currentMode == Mode::plusFft)
        {
            const int numBins = 32;
            const float binW = plotArea.getWidth() / static_cast<float> (numBins);
            for (int b = 0; b < numBins; ++b)
            {
                const float normB = (static_cast<float> (b) + 0.5f) / static_cast<float> (numBins);
                const float freqB = normToFrequency (normB);
                const auto r = evaluateFilterResponse (freqB, model, style, fc, resVal, blendVal, varVal, fX, fY, fTrans);
                const float binH = std::clamp ((r.magnitudeDb + 36.0f) / 54.0f, 0.0f, 1.0f) * plotArea.getHeight() * 0.75f;
                const auto binRect = juce::Rectangle<float> (plotArea.getX() + static_cast<float> (b) * binW + 1.0f,
                                                            plotArea.getBottom() - binH, binW - 2.0f, binH);
                g.setColour (col.accent.withAlpha (0.15f * alpha));
                g.fillRect (binRect);
            }
        }

        // Draw response curve
        juce::Path curvePath;
        const int steps = juce::jmax (20, juce::roundToInt (plotArea.getWidth()));
        const float dx = plotArea.getWidth() / static_cast<float> (steps);

        for (int i = 0; i <= steps; ++i)
        {
            const float px = plotArea.getX() + static_cast<float> (i) * dx;
            const float normX = static_cast<float> (i) / static_cast<float> (steps);
            float py = y0dB;

            if (! isOn)
            {
                py = y0dB; // Flat line when filter is OFF
            }
            else if (model == 21) // PZ SVF: evaluates user-sculpted curve
            {
                const float curveVal = pzCurve.evaluate (normX);
                py = plotArea.getBottom() - curveVal * plotArea.getHeight();
            }
            else if (currentMode == Mode::phase)
            {
                const float freq = normToFrequency (normX);
                const auto r = evaluateFilterResponse (freq, model, style, fc, resVal, blendVal, varVal, fX, fY, fTrans);
                // Phase range [-pi, +pi] -> [bottom, top]
                const float normPhase = (r.phaseRad + 3.14159265f) / (2.0f * 3.14159265f);
                py = plotArea.getBottom() - std::clamp (normPhase, 0.0f, 1.0f) * plotArea.getHeight();
            }
            else // Frequency response |H(f)| dB
            {
                const float freq = normToFrequency (normX);
                const auto r = evaluateFilterResponse (freq, model, style, fc, resVal, blendVal, varVal, fX, fY, fTrans);
                const float normDb = std::clamp ((r.magnitudeDb + 36.0f) / 54.0f, 0.0f, 1.0f);
                py = plotArea.getBottom() - normDb * plotArea.getHeight();
            }

            if (i == 0)
                curvePath.startNewSubPath (px, py);
            else
                curvePath.lineTo (px, py);
        }

        // Filled area under the curve in response mode
        if (isOn && currentMode != Mode::phase)
        {
            juce::Path fillPath (curvePath);
            fillPath.lineTo (plotArea.getRight(), plotArea.getBottom());
            fillPath.lineTo (plotArea.getX(), plotArea.getBottom());
            fillPath.closeSubPath();
            g.setColour (col.accent.withAlpha (0.18f * alpha));
            g.fillPath (fillPath);
        }

        // Stroke line
        const bool isPeakWarn = (isOn && resVal > 90.0f);
        const juce::Colour strokeCol = (isPeakWarn ? col.warn : col.accent).withAlpha (alpha);
        g.setColour (strokeCol);
        g.strokePath (curvePath, juce::PathStrokeType (t.px (2.0f)));

        // Cutoff / Resonance marker dot (when on and not in PZ SVF)
        if (isOn && model != 21 && model != 5)
        {
            const float dotNormX = frequencyToNorm (fc);
            const float dotX = plotArea.getX() + dotNormX * plotArea.getWidth();
            const auto atCutoff = evaluateFilterResponse (fc, model, style, fc, resVal, blendVal, varVal);
            const float dotNormY = std::clamp ((atCutoff.magnitudeDb + 36.0f) / 54.0f, 0.0f, 1.0f);
            const float dotY = plotArea.getBottom() - dotNormY * plotArea.getHeight();
            const float dotRadius = t.px (isPeakWarn ? 6.0f : 5.0f);

            // Halo for self-oscillation
            if (isPeakWarn)
            {
                g.setColour (col.warn.withAlpha (0.35f));
                g.fillEllipse (dotX - dotRadius * 2.0f, dotY - dotRadius * 2.0f, dotRadius * 4.0f, dotRadius * 4.0f);
            }
            g.setColour (isPeakWarn ? col.warn : col.accent);
            g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
            g.setColour (col.panel);
            g.drawEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f, 1.5f);
        }

        // Formant Model: draw vowel landmarks & moving dot
        if (isOn && model == 5)
        {
            const struct Vowel { const char* text; float x, y; } vowels[] = {
                { "A", 0.6f, 0.7f }, { "E", 0.8f, 0.4f }, { "I", 0.9f, 0.2f },
                { "O", 0.3f, 0.6f }, { "U", 0.2f, 0.3f }
            };
            g.setFont (t.font (t.px (12.0f), true));
            for (const auto& v : vowels)
            {
                const float vx = plotArea.getX() + v.x * plotArea.getWidth();
                const float vy = plotArea.getBottom() - v.y * plotArea.getHeight();
                g.setColour (col.textDim.withAlpha (0.4f));
                g.drawText (v.text, juce::roundToInt (vx - 10), juce::roundToInt (vy - 10), 20, 20, juce::Justification::centred);
            }
            // Formant dot
            const float fDotX = plotArea.getX() + (fX / 100.0f) * plotArea.getWidth();
            const float fDotY = plotArea.getBottom() - (fY / 100.0f) * plotArea.getHeight();
            g.setColour (col.accent);
            g.fillEllipse (fDotX - t.px (6.0f), fDotY - t.px (6.0f), t.px (12.0f), t.px (12.0f));
            g.setColour (col.panel);
            g.drawEllipse (fDotX - t.px (6.0f), fDotY - t.px (6.0f), t.px (12.0f), t.px (12.0f), 1.5f);
        }

        // PZ SVF Model: draw control point handles
        if (isOn && model == 21)
        {
            for (size_t i = 0; i < pzCurve.getPoints().size(); ++i)
            {
                const auto pt = pzCurve.getPoints()[i];
                const float hx = plotArea.getX() + pt.x * plotArea.getWidth();
                const float hy = plotArea.getBottom() - pt.y * plotArea.getHeight();
                const bool isSel = (static_cast<int> (i) == activePzPoint);
                g.setColour (isSel ? col.warn : col.accent);
                g.fillEllipse (hx - t.px (5.0f), hy - t.px (5.0f), t.px (10.0f), t.px (10.0f));
                g.setColour (col.panel);
                g.drawEllipse (hx - t.px (5.0f), hy - t.px (5.0f), t.px (10.0f), t.px (10.0f), 1.0f);
            }
        }

        // "No input routed" dimmed banner (Section 4)
        if (! inputsRouted)
        {
            const auto banner = bounds.withHeight (t.px (24.0f)).withY (bounds.getCentreY() - t.px (12.0f));
            g.setColour (col.panel.withAlpha (0.85f));
            g.fillRoundedRectangle (banner.reduced (t.px (20.0f), 0.0f), t.px (4.0f));
            g.setColour (col.textDim);
            g.setFont (t.labelFont());
            g.drawFittedText ("no input routed", banner.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int touchH = t.touch();
        const int btnW = juce::jmax (touchH, t.ipx (64.0f));
        modeButton.setBounds (getWidth() - btnW - t.gap(), t.gap(), btnW, touchH);

        const int model = (modelParam.valid() ? static_cast<int> (modelParam.plain()) : 0);
        pzResetButton.setVisible (model == 21);
        if (model == 21)
            pzResetButton.setBounds (getWidth() - btnW * 2 - t.gap() * 2, t.gap(), btnW, touchH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        const auto plotArea = getLocalBounds().toFloat().reduced (ctx.theme.px (6.0f), ctx.theme.px (8.0f));
        if (plotArea.getWidth() <= 1.0f || plotArea.getHeight() <= 1.0f)
            return;

        const int model = (modelParam.valid() ? static_cast<int> (modelParam.plain()) : 3);

        if (model == 21) // PZ SVF drawing
        {
            const float normX = std::clamp ((e.position.x - plotArea.getX()) / plotArea.getWidth(), 0.0f, 1.0f);
            const float normY = std::clamp ((plotArea.getBottom() - e.position.y) / plotArea.getHeight(), 0.0f, 1.0f);
            activePzPoint = pzCurve.findNearestPoint ({ normX, normY }, 0.15f);
            if (activePzPoint < 0 && e.getNumberOfClicks() >= 2)
            {
                pzCurve.addPoint ({ normX, normY });
                repaint();
            }
            return;
        }

        if (model == 5 && fxParam != nullptr && fyParam != nullptr) // Formant vowel pad
        {
            draggingFormant = true;
            fxParam->beginGesture();
            fyParam->beginGesture();
            updateFormantFromMouse (e.position, plotArea);
            return;
        }

        // Standard cutoff & resonance drag
        if (cutoff.valid() && resonance.valid())
        {
            draggingCutoffRes = true;
            cutoff.beginGesture();
            resonance.beginGesture();
            updateCutoffResFromMouse (e.position, plotArea);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto plotArea = getLocalBounds().toFloat().reduced (ctx.theme.px (6.0f), ctx.theme.px (8.0f));
        if (plotArea.getWidth() <= 1.0f || plotArea.getHeight() <= 1.0f)
            return;

        const int model = (modelParam.valid() ? static_cast<int> (modelParam.plain()) : 3);

        if (model == 21 && activePzPoint >= 0)
        {
            const float normX = std::clamp ((e.position.x - plotArea.getX()) / plotArea.getWidth(), 0.0f, 1.0f);
            const float normY = std::clamp ((plotArea.getBottom() - e.position.y) / plotArea.getHeight(), 0.0f, 1.0f);
            pzCurve.setPoint (activePzPoint, { normX, normY });
            repaint();
            return;
        }

        if (draggingFormant)
        {
            updateFormantFromMouse (e.position, plotArea);
            return;
        }

        if (draggingCutoffRes)
        {
            updateCutoffResFromMouse (e.position, plotArea);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (e);
        if (draggingFormant)
        {
            draggingFormant = false;
            if (fxParam != nullptr) fxParam->endGesture();
            if (fyParam != nullptr) fyParam->endGesture();
        }
        if (draggingCutoffRes)
        {
            draggingCutoffRes = false;
            cutoff.endGesture();
            resonance.endGesture();
        }
        activePzPoint = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (modeButton.getBounds().contains (e.getPosition()))
            return;
        cycleMode();
    }

private:
    void updateCutoffResFromMouse (juce::Point<float> pos, juce::Rectangle<float> plotArea)
    {
        const float normX = std::clamp ((pos.x - plotArea.getX()) / plotArea.getWidth(), 0.0f, 1.0f);
        const float hz = normToFrequency (normX);
        const float normY = std::clamp ((plotArea.getBottom() - pos.y) / plotArea.getHeight(), 0.0f, 1.0f);
        const float resVal = normY * 100.0f;

        if (cutoff.valid())
            cutoff.setNormalised (frequencyToNorm (hz));
        if (resonance.valid())
            resonance.setNormalised (resVal / 100.0f);
        repaint();
    }

    void updateFormantFromMouse (juce::Point<float> pos, juce::Rectangle<float> plotArea)
    {
        const float normX = std::clamp ((pos.x - plotArea.getX()) / plotArea.getWidth(), 0.0f, 1.0f);
        const float normY = std::clamp ((plotArea.getBottom() - pos.y) / plotArea.getHeight(), 0.0f, 1.0f);
        if (fxParam != nullptr && fxParam->valid())
            fxParam->setNormalised (normX);
        if (fyParam != nullptr && fyParam->valid())
            fyParam->setNormalised (normY);
        repaint();
    }

    UiContext& ctx;
    ParamHandle& cutoff;
    ParamHandle& resonance;
    ParamHandle& onParam;
    ParamHandle& modelParam;
    ParamHandle& styleParam;
    ParamHandle& blendParam;
    ParamHandle& varParam;
    ParamHandle* fxParam = nullptr;
    ParamHandle* fyParam = nullptr;
    ParamHandle* ftParam = nullptr;

    Mode currentMode = Mode::resp;
    bool inputsRouted = true;
    bool draggingCutoffRes = false;
    bool draggingFormant = false;

    juce::TextButton modeButton;
    juce::TextButton pzResetButton;
    PzSvfCurve pzCurve;
    int activePzPoint = -1;
};

// ---- FilterInputsBar (Input Switches S A B C N) ------------------------------------------------------------------------------------

class FilterInputsBar final : public juce::Component
{
public:
    FilterInputsBar (UiContext& context, int filterIndex)
        : ctx (context), filterIdx (filterIndex),
          osc1Dest (context, "osc1.destination"),
          osc2Dest (context, "osc2.destination"),
          osc3Dest (context, "osc3.destination"),
          noiseDest (context, "noise.destination")
    {
        const char* const labels[] = { "S", "A", "B", "C", "N" };
        for (int i = 0; i < 5; ++i)
        {
            auto b = std::make_unique<juce::TextButton> (labels[i]);
            b->setClickingTogglesState (false);
            if (i == 0) // S = Sub oscillator (GAP)
            {
                b->setEnabled (false);
                b->setAlpha (0.4f);
                b->setTooltip ("Sub oscillator (GAP)");
            }
            else
            {
                b->onClick = [this, i] { toggleSource (i); };
            }
            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
        }

        const auto updateState = [this] { refreshButtonStates(); };
        osc1Dest.onChange = updateState;
        osc2Dest.onChange = updateState;
        osc3Dest.onChange = updateState;
        noiseDest.onChange = updateState;
        refreshButtonStates();
    }

    void refreshButtonStates()
    {
        const int d1 = (osc1Dest.valid() ? static_cast<int> (osc1Dest.plain()) : 3);
        const int d2 = (osc2Dest.valid() ? static_cast<int> (osc2Dest.plain()) : 3);
        const int d3 = (osc3Dest.valid() ? static_cast<int> (osc3Dest.plain()) : 3);
        const int dn = (noiseDest.valid() ? static_cast<int> (noiseDest.plain()) : 3);

        const bool aActive = (filterIdx == 1 ? isFilter1Active (d1) : isFilter2Active (d1));
        const bool bActive = (filterIdx == 1 ? isFilter1Active (d2) : isFilter2Active (d2));
        const bool cActive = (filterIdx == 1 ? isFilter1Active (d3) : isFilter2Active (d3));
        const bool nActive = (filterIdx == 1 ? isFilter1Active (dn) : isFilter2Active (dn));

        buttons[1]->setToggleState (aActive, juce::dontSendNotification);
        buttons[2]->setToggleState (bActive, juce::dontSendNotification);
        buttons[3]->setToggleState (cActive, juce::dontSendNotification);
        buttons[4]->setToggleState (nActive, juce::dontSendNotification);

        if (onInputsChanged)
            onInputsChanged();
    }

    bool hasAnyInputs (int routingChoice) const
    {
        const int d1 = (osc1Dest.valid() ? static_cast<int> (osc1Dest.plain()) : 3);
        const int d2 = (osc2Dest.valid() ? static_cast<int> (osc2Dest.plain()) : 3);
        const int d3 = (osc3Dest.valid() ? static_cast<int> (osc3Dest.plain()) : 3);
        const int dn = (noiseDest.valid() ? static_cast<int> (noiseDest.plain()) : 3);

        if (filterIdx == 1)
            return hasAnyInputForFilter1 (d1, d2, d3, dn);
        return hasAnyInputForFilter2 (d1, d2, d3, dn, routingChoice);
    }

    std::function<void()> onInputsChanged;

    void resized() override
    {
        const auto& t = ctx.theme;
        const int touchH = t.touch();
        const int count = static_cast<int> (buttons.size());
        const int gap = t.gap();
        const int labelW = t.ipx (48.0f);
        const int availW = getWidth() - labelW - gap;
        const int btnW = juce::jmax (touchH, (availW - (count - 1) * gap) / count);

        int x = labelW + gap;
        for (auto& b : buttons)
        {
            b->setBounds (x, (getHeight() - touchH) / 2, btnW, touchH);
            x += btnW + gap;
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        g.setColour (t.colours.textDim);
        g.setFont (t.labelFont());
        g.drawFittedText ("IN", 0, 0, t.ipx (48.0f), getHeight(), juce::Justification::centredRight, 1);
    }

private:
    void toggleSource (int sourceIdx)
    {
        ParamHandle* target = nullptr;
        if (sourceIdx == 1) target = &osc1Dest;
        else if (sourceIdx == 2) target = &osc2Dest;
        else if (sourceIdx == 3) target = &osc3Dest;
        else if (sourceIdx == 4) target = &noiseDest;
        if (target == nullptr || ! target->valid())
            return;

        const int cur = static_cast<int> (target->plain());
        const int next = (filterIdx == 1 ? toggleFilter1Input (cur) : toggleFilter2Input (cur));
        target->setPlainComplete (static_cast<float> (next));
        refreshButtonStates();
    }

    UiContext& ctx;
    int filterIdx;
    ParamHandle osc1Dest, osc2Dest, osc3Dest, noiseDest;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
};

// ---- FilterRoutingChip (PAR | 1>2 | 2>1) -------------------------------------------------------------------------------------------

class FilterRoutingChip final : public juce::Component
{
public:
    FilterRoutingChip (UiContext& context, ParamHandle& routingHandle)
        : ctx (context), routing (routingHandle)
    {
        const char* const labels[] = { "PAR", "1>2", "2>1" };
        for (int i = 0; i < 3; ++i)
        {
            auto b = std::make_unique<juce::TextButton> (labels[i]);
            b->setClickingTogglesState (false);
            b->onClick = [this, i] {
                if (routing.valid())
                    routing.setPlainComplete (static_cast<float> (i));
                refreshStates();
            };
            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
        }

        routing.onChange = [this] { refreshStates(); };
        refreshStates();
    }

    void refreshStates()
    {
        const int active = (routing.valid() ? static_cast<int> (routing.plain()) : 0);
        for (int i = 0; i < 3; ++i)
            buttons[static_cast<size_t> (i)]->setToggleState (i == active, juce::dontSendNotification);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int touchH = t.touch();
        const int btnW = juce::jmax (touchH, getWidth() / 3);
        for (int i = 0; i < 3; ++i)
            buttons[static_cast<size_t> (i)]->setBounds (i * btnW, (getHeight() - touchH) / 2, btnW, touchH);
    }

private:
    UiContext& ctx;
    ParamHandle& routing;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
};

// ---- FilterTypePickerSheet (Section 2.3) -------------------------------------------------------------------------------------------

class FilterTypePickerSheet final : public juce::Component
{
public:
    FilterTypePickerSheet (UiContext& context, ParamHandle& modelHandle, ParamHandle& styleHandle, ParamHandle& blendHandle)
        : ctx (context), model (modelHandle), style (styleHandle), blend (blendHandle)
    {
        title.setText ("FILTER TYPES", juce::dontSendNotification);
        title.setFont (ctx.theme.titleFont());
        title.setColour (juce::Label::textColourId, ctx.theme.colours.text);
        addAndMakeVisible (title);

        const auto& catalog = getSerumFilterCatalog();
        const char* const families[] = { "Normal", "Multi", "Flanges", "Misc", "New" };

        for (const char* fam : families)
        {
            auto groupLabel = std::make_unique<juce::Label> ("", fam);
            groupLabel->setFont (ctx.theme.labelFont());
            groupLabel->setColour (juce::Label::textColourId, ctx.theme.colours.accent);
            addAndMakeVisible (*groupLabel);
            categoryLabels.push_back (std::move (groupLabel));

            std::vector<std::unique_ptr<juce::TextButton>> groupButtons;
            for (const auto& item : catalog)
            {
                if (std::string (item.family) == fam)
                {
                    auto b = std::make_unique<juce::TextButton> (item.name);
                    b->onClick = [this, item] {
                        if (model.valid()) model.setPlainComplete (static_cast<float> (item.model));
                        if (style.valid()) style.setPlainComplete (static_cast<float> (item.style));
                        if (blend.valid()) blend.setPlainComplete (item.defaultBlend);
                        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
                            callout->dismiss();
                    };
                    addAndMakeVisible (*b);
                    groupButtons.push_back (std::move (b));
                }
            }
            typeButtons.push_back (std::move (groupButtons));
        }
    }

    int preferredHeight() const
    {
        const auto& t = ctx.theme;
        int h = t.touch() + t.gap(); // title
        for (const auto& group : typeButtons)
        {
            h += t.ipx (20.0f) + t.gap(); // category label
            const int perRow = 4;
            const int rows = (static_cast<int> (group.size()) + perRow - 1) / perRow;
            h += rows * (t.touch() + t.gap());
        }
        return h + t.gap() * 2;
    }

    int preferredWidth() const
    {
        return juce::jmax (ctx.theme.ipx (340.0f), ctx.theme.touch() * 6);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int touchH = t.touch();
        const int gap = t.gap();

        int y = gap;
        title.setBounds (gap, y, getWidth() - 2 * gap, touchH);
        y += touchH + gap;

        for (size_t g = 0; g < categoryLabels.size(); ++g)
        {
            categoryLabels[g]->setBounds (gap, y, getWidth() - 2 * gap, t.ipx (20.0f));
            y += t.ipx (20.0f) + gap / 2;

            const auto& buttons = typeButtons[g];
            const int cols = juce::jmax (1, (getWidth() - 2 * gap) / (touchH + gap));
            const int btnW = (getWidth() - 2 * gap - (cols - 1) * gap) / cols;

            int col = 0;
            for (const auto& b : buttons)
            {
                b->setBounds (gap + col * (btnW + gap), y, btnW, touchH);
                if (++col == cols)
                {
                    col = 0;
                    y += touchH + gap / 2;
                }
            }
            if (col != 0)
                y += touchH + gap / 2;
            y += gap / 2;
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ctx.theme.colours.panel);
    }

private:
    UiContext& ctx;
    ParamHandle &model, &style, &blend;
    juce::Label title;
    std::vector<std::unique_ptr<juce::Label>> categoryLabels;
    std::vector<std::vector<std::unique_ptr<juce::TextButton>>> typeButtons;
};

inline void showFilterTypePicker (UiContext& ctx, ParamHandle& model, ParamHandle& style, ParamHandle& blend, juce::Component& anchor)
{
    auto sheet = std::make_unique<FilterTypePickerSheet> (ctx, model, style, blend);
    sheet->setSize (sheet->preferredWidth(), sheet->preferredHeight());
    juce::CallOutBox::launchAsynchronously (std::move (sheet), anchor.getScreenBounds(), anchor.getTopLevelComponent());
}

} // namespace synthyboy::ui
