// SynthyBoy - UI: oscillator, noise, and mixer visualizers implementation (docs/ui-design/02-oscillators.md)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OscWaveView.h"

#include <cmath>

namespace synthyboy::ui
{
// Helper: evaluate sample point in [0, 1] for a given wavetable choice and frame
static float evalWave (float phase, int wtChoice, float frame, float warpAmount, int warpType)
{
    // phase in [0, 1)
    phase = phase - std::floor (phase);

    // Apply warp
    if (warpAmount > 0.001f)
    {
        switch (warpType)
        {
            case 1: // Sync
            {
                const float factor = 1.0f + warpAmount * 3.0f;
                phase = phase * factor;
                phase = phase - std::floor (phase);
                break;
            }
            case 2: // Bend
            {
                const float p = phase - 0.5f;
                const float s = (warpAmount > 0.5f) ? (1.0f + (warpAmount - 0.5f) * 2.0f) : (1.0f - (0.5f - warpAmount) * 1.5f);
                phase = juce::jlimit (0.0f, 1.0f, 0.5f + p * s);
                break;
            }
            case 3: // Pulse / PWM
            {
                const float pw = 0.5f + (warpAmount - 0.5f) * 0.8f;
                phase = (phase < pw) ? (phase / (2.0f * pw)) : (0.5f + (phase - pw) / (2.0f * (1.0f - pw)));
                break;
            }
            default:
                break;
        }
    }

    const float twoPi = 6.28318530718f;
    const float theta = phase * twoPi;

    float y0 = 0.0f;
    switch (wtChoice % 4)
    {
        case 0: // Sine
            y0 = std::sin (theta);
            break;
        case 1: // Triangle
            y0 = (phase < 0.25f) ? (4.0f * phase) : ((phase < 0.75f) ? (2.0f - 4.0f * phase) : (-4.0f + 4.0f * phase));
            break;
        case 2: // Saw
            y0 = 1.0f - 2.0f * phase;
            break;
        case 3: // Square
            y0 = (phase < 0.5f) ? 0.9f : -0.9f;
            break;
        default:
            break;
    }

    // Add frame harmonics if wtChoice >= 4 or frame > 0
    if (wtChoice >= 4 || frame > 0.01f)
    {
        const float harm2 = std::sin (2.0f * theta) * 0.5f * frame;
        const float harm3 = std::sin (3.0f * theta) * 0.25f * frame;
        const float harm4 = std::sin (4.0f * theta) * 0.125f * (wtChoice >= 4 ? 0.6f : frame);
        y0 = (y0 + harm2 + harm3 + harm4) / (1.0f + 0.5f * frame + 0.25f * frame);
    }
    return juce::jlimit (-1.0f, 1.0f, y0);
}

// ---- OscWaveView ------------------------------------------------------------------------------------------------------------------

OscWaveView::OscWaveView (UiContext& context, int oscIndex)
    : ctx (context),
      index (oscIndex),
      frameHandle (context, "osc" + std::to_string (oscIndex) + ".frame"),
      typeHandle (context, "osc" + std::to_string (oscIndex) + ".type"),
      wtHandle (context, "osc" + std::to_string (oscIndex) + ".wavetable"),
      onHandle (context, "osc" + std::to_string (oscIndex) + ".on"),
      warpAmountHandle (context, "osc" + std::to_string (oscIndex) + ".warp.amount"),
      warpTypeHandle (context, "osc" + std::to_string (oscIndex) + ".warp.type"),
      unisonVoicesHandle (context, "osc" + std::to_string (oscIndex) + ".unison.voices"),
      sampleStartHandle (context, "osc" + std::to_string (oscIndex) + ".sample.start"),
      sampleEndHandle (context, "osc" + std::to_string (oscIndex) + ".sample.end"),
      sampleLoopStartHandle (context, "osc" + std::to_string (oscIndex) + ".sample.loop_start"),
      sampleLoopEndHandle (context, "osc" + std::to_string (oscIndex) + ".sample.loop_end")
{
    const auto repaintCb = [this] { repaint(); };
    frameHandle.onChange = repaintCb;
    typeHandle.onChange = [this] { updateButtons(); repaint(); };
    wtHandle.onChange = repaintCb;
    onHandle.onChange = repaintCb;
    warpAmountHandle.onChange = repaintCb;
    warpTypeHandle.onChange = repaintCb;
    unisonVoicesHandle.onChange = repaintCb;
    sampleStartHandle.onChange = repaintCb;
    sampleEndHandle.onChange = repaintCb;
    sampleLoopStartHandle.onChange = repaintCb;
    sampleLoopEndHandle.onChange = repaintCb;

    prevWtButton.onClick = [this]
    {
        if (wtHandle.valid())
        {
            const float cur = wtHandle.plain();
            const float next = juce::jmax (0.0f, cur - 1.0f);
            wtHandle.setPlainComplete (next);
        }
    };
    nextWtButton.onClick = [this]
    {
        if (wtHandle.valid())
        {
            const float cur = wtHandle.plain();
            const float maxVal = static_cast<float> (wtHandle.info().max);
            const float next = juce::jmin (maxVal, cur + 1.0f);
            wtHandle.setPlainComplete (next);
        }
    };
    viewModeButton.onClick = [this] { cycleViewMode(); };

    addAndMakeVisible (prevWtButton);
    addAndMakeVisible (nextWtButton);
    addAndMakeVisible (viewModeButton);
    updateButtons();
}

OscWaveView::~OscWaveView() = default;

bool OscWaveView::isSampleType() const
{
    return typeHandle.valid() && typeHandle.plain() >= 0.5f;
}

void OscWaveView::setViewMode (ViewMode newMode)
{
    mode = newMode;
    updateButtons();
    repaint();
}

void OscWaveView::cycleViewMode()
{
    switch (mode)
    {
        case ViewMode::wave2D:   setViewMode (ViewMode::wave3D); break;
        case ViewMode::wave3D:   setViewMode (ViewMode::spectral); break;
        case ViewMode::spectral: setViewMode (ViewMode::wave2D); break;
    }
}

void OscWaveView::updateButtons()
{
    const bool isSample = isSampleType();
    prevWtButton.setVisible (! isSample);
    nextWtButton.setVisible (! isSample);
    viewModeButton.setVisible (! isSample);

    switch (mode)
    {
        case ViewMode::wave2D:   viewModeButton.setButtonText ("2D"); break;
        case ViewMode::wave3D:   viewModeButton.setButtonText ("3D"); break;
        case ViewMode::spectral: viewModeButton.setButtonText ("SP"); break;
    }
}

void OscWaveView::resized()
{
    const auto& t = ctx.theme;
    const int touchH = t.touch();
    const int gap = t.gap();
    const int btnW = juce::jmax (touchH, t.ipx (44.0f));

    // Top control strip: prev, next, mode buttons
    int x = t.gap();
    const int y = t.gap();

    prevWtButton.setBounds (x, y, btnW, touchH);
    x += btnW + gap;
    nextWtButton.setBounds (x, y, btnW, touchH);
    x += btnW + gap;
    viewModeButton.setBounds (x, y, juce::jmax (btnW, t.ipx (50.0f)), touchH);
}

void OscWaveView::mouseDown (const juce::MouseEvent& e)
{
    dragStartPos = e.position;
    dragging = false;

    if (isSampleType())
    {
        // Check which marker is close to click x
        const float w = static_cast<float> (getWidth());
        const float sX = sampleStartHandle.normalised() * w;
        const float eX = sampleEndHandle.normalised() * w;
        const float lsX = sampleLoopStartHandle.normalised() * w;
        const float leX = sampleLoopEndHandle.normalised() * w;

        const float tol = static_cast<float> (ctx.theme.touch());
        if (std::abs (e.position.x - sX) < tol)
        {
            activeMarker = SampleMarker::start;
            sampleStartHandle.beginGesture();
        }
        else if (std::abs (e.position.x - eX) < tol)
        {
            activeMarker = SampleMarker::end;
            sampleEndHandle.beginGesture();
        }
        else if (std::abs (e.position.x - lsX) < tol)
        {
            activeMarker = SampleMarker::loopStart;
            sampleLoopStartHandle.beginGesture();
        }
        else if (std::abs (e.position.x - leX) < tol)
        {
            activeMarker = SampleMarker::loopEnd;
            sampleLoopEndHandle.beginGesture();
        }
        else
        {
            activeMarker = SampleMarker::none;
        }
    }
    else
    {
        if (frameHandle.valid())
        {
            frameHandle.beginGesture();
            dragStartNorm = frameHandle.normalised();
        }
    }
}

void OscWaveView::mouseDrag (const juce::MouseEvent& e)
{
    dragging = true;
    if (isSampleType())
    {
        const float w = static_cast<float> (juce::jmax (1, getWidth()));
        const float norm = juce::jlimit (0.0f, 1.0f, e.position.x / w);
        switch (activeMarker)
        {
            case SampleMarker::start:     sampleStartHandle.setNormalised (norm); break;
            case SampleMarker::end:       sampleEndHandle.setNormalised (norm); break;
            case SampleMarker::loopStart: sampleLoopStartHandle.setNormalised (norm); break;
            case SampleMarker::loopEnd:   sampleLoopEndHandle.setNormalised (norm); break;
            case SampleMarker::none:      break;
        }
    }
    else
    {
        if (frameHandle.valid())
        {
            const float w = static_cast<float> (juce::jmax (1, getWidth()));
            const float dx = e.position.x - dragStartPos.x;
            const float newNorm = juce::jlimit (0.0f, 1.0f, dragStartNorm + dx / w);
            frameHandle.setNormalised (newNorm);
        }
    }
}

void OscWaveView::mouseUp (const juce::MouseEvent& e)
{
    if (isSampleType())
    {
        switch (activeMarker)
        {
            case SampleMarker::start:     sampleStartHandle.endGesture(); break;
            case SampleMarker::end:       sampleEndHandle.endGesture(); break;
            case SampleMarker::loopStart: sampleLoopStartHandle.endGesture(); break;
            case SampleMarker::loopEnd:   sampleLoopEndHandle.endGesture(); break;
            case SampleMarker::none:      break;
        }
        activeMarker = SampleMarker::none;
    }
    else
    {
        if (frameHandle.valid())
            frameHandle.endGesture();

        // Direct tap on view cycles mode if not dragged
        if (! dragging && e.getDistanceFromDragStart() < 4)
            cycleViewMode();
    }
    dragging = false;
}

void OscWaveView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (isSampleType())
    {
        // Reset start and end to full range
        sampleStartHandle.setPlainComplete (0.0f);
        sampleEndHandle.setPlainComplete (100.0f);
    }
    else
    {
        // Reset frame to 0
        if (frameHandle.valid())
            frameHandle.resetToDefault();
    }
}

void OscWaveView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const bool isOff = onHandle.valid() && onHandle.plain() < 0.5f;

    // Dimming: 40 % opacity when off (02-oscillators.md §4)
    const float alpha = isOff ? 0.4f : 1.0f;
    g.beginTransparencyLayer (alpha);

    const auto r = getLocalBounds().toFloat().reduced (t.px (1.0f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (r, t.px (8.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (r, t.px (8.0f), 1.0f);

    // Header info (wavetable name, tier warning, off badge)
    const int topH = t.touch();
    auto headerArea = juce::Rectangle<float> (r).removeFromTop (static_cast<float> (topH + t.gap())).reduced (t.px (8.0f), 0.0f);

    g.setFont (t.labelFont());
    g.setColour (c.textDim);

    juce::String wtName;
    if (isSampleType())
    {
        wtName = "Sample Osc " + juce::String (index);
    }
    else
    {
        const int wtIdx = wtHandle.valid() ? juce::roundToInt (wtHandle.plain()) : 0;
        const char* const kBuiltins[] = { "Basic Shapes", "Saw to Square", "Sine to Triangle", "Harmonics",
                                          "Organ", "Voice Formants", "Digital Bell", "Metal Resonance",
                                          "Bit Crush", "PWM Sweep", "Formant Scale", "Sub Harmonics", "Noise Pulse", "Preset Table" };
        const juce::String name = (wtIdx >= 0 && wtIdx < 14) ? kBuiltins[wtIdx] : ("Table " + juce::String (wtIdx));
        wtName = "Osc " + juce::String (index) + ": " + name;
    }
    g.drawText (wtName, headerArea.toNearestInt(), juce::Justification::centred, true);

    // Tier-capped badge (voices > tierCap)
    const int voices = unisonVoicesHandle.valid() ? juce::roundToInt (unisonVoicesHandle.plain()) : 1;
    if (voices > 8) // medium / low tier cap hint
    {
        g.setColour (c.warn);
        g.drawText ("TIER CAPPED", headerArea.toNearestInt(), juce::Justification::centredRight, true);
    }
    if (isOff)
    {
        g.setColour (c.warn);
        g.drawText ("OFF", headerArea.removeFromRight (t.px (40.0f)).toNearestInt(), juce::Justification::centredRight, true);
    }

    // Main waveform area
    const auto waveArea = r.reduced (t.px (8.0f), t.px (4.0f));
    if (isSampleType())
        paintSample (g, waveArea);
    else
        paintWavetable (g, waveArea);

    g.endTransparencyLayer();
}

void OscWaveView::paintWavetable (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float frame = frameHandle.valid() ? frameHandle.normalised() : 0.0f;
    const float warpAmt = warpAmountHandle.valid() ? warpAmountHandle.normalised() : 0.0f;
    const int warpType = warpTypeHandle.valid() ? juce::roundToInt (warpTypeHandle.plain()) : 0;
    const int wtIdx = wtHandle.valid() ? juce::roundToInt (wtHandle.plain()) : 0;

    switch (mode)
    {
        case ViewMode::wave2D:
            paintWave2D (g, area, frame, warpAmt, warpType, wtIdx);
            break;
        case ViewMode::wave3D:
            paintWave3D (g, area, frame, wtIdx);
            break;
        case ViewMode::spectral:
            paintSpectral (g, area, frame, wtIdx);
            break;
    }
}

void OscWaveView::paintWave2D (juce::Graphics& g, juce::Rectangle<float> area, float frame, float warpAmount, int warpType, int wtIdx)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;

    // Background centerline
    const float midY = area.getCentreY();
    g.setColour (c.track.withAlpha (0.4f));
    g.drawHorizontalLine (juce::roundToInt (midY), area.getX(), area.getRight());

    // Generate wave path
    const int numPoints = juce::roundToInt (area.getWidth());
    if (numPoints <= 1)
        return;

    juce::Path p;
    juce::Path fillP;

    const float halfH = area.getHeight() * 0.42f;
    for (int i = 0; i < numPoints; ++i)
    {
        const float phase = static_cast<float> (i) / static_cast<float> (numPoints);
        const float yVal = evalWave (phase, wtIdx, frame, warpAmount, warpType);
        const float x = area.getX() + static_cast<float> (i);
        const float y = midY - yVal * halfH;

        if (i == 0)
        {
            p.startNewSubPath (x, y);
            fillP.startNewSubPath (x, midY);
            fillP.lineTo (x, y);
        }
        else
        {
            p.lineTo (x, y);
            fillP.lineTo (x, y);
        }
    }
    fillP.lineTo (area.getRight(), midY);
    fillP.closeSubPath();

    // Fill under curve
    g.setColour (c.accent.withAlpha (0.15f));
    g.fillPath (fillP);

    // Stroke curve
    g.setColour (c.accent);
    g.strokePath (p, juce::PathStrokeType (t.px (2.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Frame position cursor
    const float cursorX = area.getX() + frame * area.getWidth();
    g.setColour (c.mod);
    g.drawVerticalLine (juce::roundToInt (cursorX), area.getY(), area.getBottom());

    // Frame readout text
    g.setFont (t.labelFont());
    g.setColour (c.textDim);
    g.drawText ("Pos: " + juce::String (juce::roundToInt (frame * 100.0f)) + "%",
                area.removeFromBottom (t.px (16.0f)).toNearestInt(), juce::Justification::bottomLeft, false);
}

void OscWaveView::paintWave3D (juce::Graphics& g, juce::Rectangle<float> area, float frame, int wtIdx)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;

    const int numSlices = 6;
    const float sliceH = area.getHeight() * 0.32f;
    const float xShift = area.getWidth() * 0.05f;
    const float yShift = area.getHeight() * 0.12f;

    const int activeSlice = juce::jlimit (0, numSlices - 1, juce::roundToInt (frame * static_cast<float> (numSlices - 1)));

    for (int s = numSlices - 1; s >= 0; --s)
    {
        const float sliceRatio = static_cast<float> (s) / static_cast<float> (numSlices - 1);
        const float originX = area.getX() + (1.0f - sliceRatio) * xShift * static_cast<float> (numSlices);
        const float originY = area.getY() + static_cast<float> (s) * yShift + sliceH;
        const float w = area.getWidth() - xShift * static_cast<float> (numSlices);

        juce::Path p;
        const int steps = 64;
        for (int i = 0; i <= steps; ++i)
        {
            const float ph = static_cast<float> (i) / static_cast<float> (steps);
            const float y = evalWave (ph, wtIdx, sliceRatio, 0.0f, 0);
            const float px = originX + ph * w;
            const float py = originY - y * (sliceH * 0.45f);

            if (i == 0)
                p.startNewSubPath (px, py);
            else
                p.lineTo (px, py);
        }

        const bool isActive = (s == activeSlice);
        g.setColour (isActive ? c.accent : c.outline.withAlpha (0.4f + sliceRatio * 0.4f));
        g.strokePath (p, juce::PathStrokeType (t.px (isActive ? 2.0f : 1.0f)));
    }
}

void OscWaveView::paintSpectral (juce::Graphics& g, juce::Rectangle<float> area, float frame, int wtIdx)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;

    const int numBars = 24;
    const float barW = (area.getWidth() - static_cast<float> (numBars - 1) * t.px (2.0f)) / static_cast<float> (numBars);
    const float maxH = area.getHeight() * 0.85f;

    for (int i = 0; i < numBars; ++i)
    {
        const float harm = static_cast<float> (i + 1);
        // Approximate harmonic amplitude for wave shape
        float amp = 1.0f / harm;
        if (wtIdx == 3) // square: odd harmonics only
            amp = (i % 2 == 0) ? (1.0f / harm) : 0.05f;
        else if (wtIdx == 1) // triangle: 1/n^2
            amp = 1.0f / (harm * harm);

        // Frame modulation: boost higher harmonics
        amp = juce::jlimit (0.02f, 1.0f, amp * (1.0f + frame * (harm * 0.3f)));
        const float h = amp * maxH;
        const float x = area.getX() + static_cast<float> (i) * (barW + t.px (2.0f));
        const float y = area.getBottom() - h;

        g.setColour (c.accent.withAlpha (0.6f + 0.4f * amp));
        g.fillRect (x, y, barW, h);
    }
}

void OscWaveView::paintSample (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;

    const float startNorm = sampleStartHandle.valid() ? sampleStartHandle.normalised() : 0.0f;
    const float endNorm = sampleEndHandle.valid() ? sampleEndHandle.normalised() : 1.0f;
    const float loopStartNorm = sampleLoopStartHandle.valid() ? sampleLoopStartHandle.normalised() : 0.0f;
    const float loopEndNorm = sampleLoopEndHandle.valid() ? sampleLoopEndHandle.normalised() : 1.0f;

    const float w = area.getWidth();
    const float sX = area.getX() + startNorm * w;
    const float eX = area.getX() + endNorm * w;
    const float lsX = area.getX() + loopStartNorm * w;
    const float leX = area.getX() + loopEndNorm * w;

    // Loop region shaded background
    if (leX > lsX)
    {
        g.setColour (c.accent.withAlpha (0.12f));
        g.fillRect (lsX, area.getY(), leX - lsX, area.getHeight());
    }

    // Sample waveform (procedural audio wave display)
    juce::Path waveP;
    const int points = juce::roundToInt (w);
    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.4f;

    for (int i = 0; i < points; ++i)
    {
        const float normX = static_cast<float> (i) / static_cast<float> (points);
        const float x = area.getX() + static_cast<float> (i);

        // Procedural decay envelope sample
        const float env = std::exp (-normX * 3.0f);
        const float y = std::sin (normX * 45.0f) * env * std::sin (normX * 12.0f);
        const float py = midY - y * halfH;

        if (i == 0)
            waveP.startNewSubPath (x, py);
        else
            waveP.lineTo (x, py);
    }
    g.setColour (c.accentDim);
    g.strokePath (waveP, juce::PathStrokeType (t.px (1.5f)));

    // Markers: Start (green/text), End (warn/amber), Loop (mod/purple)
    // Loop markers
    g.setColour (c.mod);
    g.drawVerticalLine (juce::roundToInt (lsX), area.getY(), area.getBottom());
    g.drawVerticalLine (juce::roundToInt (leX), area.getY(), area.getBottom());

    // Start / End markers with tabs
    g.setColour (c.accent);
    g.drawVerticalLine (juce::roundToInt (sX), area.getY(), area.getBottom());
    g.fillRect (sX - t.px (6.0f), area.getY(), t.px (12.0f), t.px (14.0f));
    g.setColour (c.textOnAccent);
    g.setFont (t.labelFont());
    g.drawText ("S", juce::Rectangle<float> (sX - t.px (6.0f), area.getY(), t.px (12.0f), t.px (14.0f)).toNearestInt(), juce::Justification::centred, false);

    g.setColour (c.warn);
    g.drawVerticalLine (juce::roundToInt (eX), area.getY(), area.getBottom());
    g.fillRect (eX - t.px (6.0f), area.getY(), t.px (12.0f), t.px (14.0f));
    g.setColour (c.textOnAccent);
    g.drawText ("E", juce::Rectangle<float> (eX - t.px (6.0f), area.getY(), t.px (12.0f), t.px (14.0f)).toNearestInt(), juce::Justification::centred, false);
}

// ---- OscNoiseView -----------------------------------------------------------------------------------------------------------------

OscNoiseView::OscNoiseView (UiContext& context)
    : ctx (context),
      levelHandle (context, "noise.level"),
      panHandle (context, "noise.pan"),
      onHandle (context, "noise.on"),
      destHandle (context, "noise.destination")
{
    const auto repaintCb = [this] { repaint(); };
    levelHandle.onChange = repaintCb;
    panHandle.onChange = repaintCb;
    onHandle.onChange = repaintCb;
    destHandle.onChange = repaintCb;
}

OscNoiseView::~OscNoiseView() = default;

void OscNoiseView::resized()
{
}

void OscNoiseView::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    dragStartPos = e.position;
    if (levelHandle.valid())
    {
        levelHandle.beginGesture();
        dragStartLevel = levelHandle.normalised();
    }
    if (panHandle.valid())
    {
        panHandle.beginGesture();
        dragStartPan = panHandle.normalised();
    }
}

void OscNoiseView::mouseDrag (const juce::MouseEvent& e)
{
    dragging = true;
    const float w = static_cast<float> (juce::jmax (1, getWidth()));
    const float h = static_cast<float> (juce::jmax (1, getHeight()));
    const float dx = e.position.x - dragStartPos.x;
    const float dy = e.position.y - dragStartPos.y;

    if (panHandle.valid())
        panHandle.setNormalised (juce::jlimit (0.0f, 1.0f, dragStartPan + dx / w));
    if (levelHandle.valid())
        levelHandle.setNormalised (juce::jlimit (0.0f, 1.0f, dragStartLevel - dy / h));
}

void OscNoiseView::mouseUp (const juce::MouseEvent&)
{
    if (levelHandle.valid())
        levelHandle.endGesture();
    if (panHandle.valid())
        panHandle.endGesture();
    dragging = false;
}

void OscNoiseView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const bool isOff = onHandle.valid() && onHandle.plain() < 0.5f;

    const float alpha = isOff ? 0.4f : 1.0f;
    g.beginTransparencyLayer (alpha);

    const auto r = getLocalBounds().toFloat().reduced (t.px (1.0f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (r, t.px (8.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (r, t.px (8.0f), 1.0f);

    // Header info
    g.setFont (t.labelFont());
    g.setColour (c.textDim);
    g.drawText ("White Noise Generator", r.reduced (t.px (8.0f), t.px (4.0f)).removeFromTop (t.px (18.0f)).toNearestInt(), juce::Justification::topLeft, false);

    if (isOff)
    {
        g.setColour (c.warn);
        g.drawText ("OFF", r.reduced (t.px (8.0f), t.px (4.0f)).removeFromTop (t.px (18.0f)).toNearestInt(), juce::Justification::topRight, false);
    }

    const auto scopeArea = r.reduced (t.px (8.0f), t.px (8.0f)).withTrimmedTop (t.px (18.0f));

    // Draw animated pseudo-random noise waveform scope
    juce::Path p;
    const int numPoints = juce::roundToInt (scopeArea.getWidth());
    const float midY = scopeArea.getCentreY();
    const float amp = scopeArea.getHeight() * 0.35f * (levelHandle.valid() ? levelHandle.normalised() : 0.5f);

    juce::Random rnd (42);
    for (int i = 0; i < numPoints; ++i)
    {
        const float x = scopeArea.getX() + static_cast<float> (i);
        const float noiseVal = (rnd.nextFloat() * 2.0f - 1.0f);
        const float y = midY + noiseVal * amp;
        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }
    g.setColour (c.accent.withAlpha (0.7f));
    g.strokePath (p, juce::PathStrokeType (t.px (1.2f)));

    // Crosshair for Pan / Level
    const float panNorm = panHandle.valid() ? panHandle.normalised() : 0.5f;
    const float lvlNorm = levelHandle.valid() ? levelHandle.normalised() : 0.5f;
    const float crossX = scopeArea.getX() + panNorm * scopeArea.getWidth();
    const float crossY = scopeArea.getBottom() - lvlNorm * scopeArea.getHeight();

    g.setColour (c.mod.withAlpha (0.5f));
    g.drawVerticalLine (juce::roundToInt (crossX), scopeArea.getY(), scopeArea.getBottom());
    g.drawHorizontalLine (juce::roundToInt (crossY), scopeArea.getX(), scopeArea.getRight());
    g.setColour (c.mod);
    g.fillEllipse (crossX - t.px (4.0f), crossY - t.px (4.0f), t.px (8.0f), t.px (8.0f));

    g.endTransparencyLayer();
}

// ---- OscMixConsoleView ------------------------------------------------------------------------------------------------------------

OscMixConsoleView::OscMixConsoleView (UiContext& context)
    : ctx (context),
      masterVolumeHandle (context, "master.volume"),
      directLevelHandle (context, "direct.level"),
      mainFxBypassHandle (context, "main.fx_bypass"),
      filterRoutingHandle (context, "filter.routing"),
      o1Level (context, "osc1.level"),
      o2Level (context, "osc2.level"),
      o3Level (context, "osc3.level"),
      noiseLevel (context, "noise.level"),
      f1Level (context, "filter1.level"),
      f2Level (context, "filter2.level"),
      b1Level (context, "bus1.level"),
      b2Level (context, "bus2.level"),
      o1On (context, "osc1.on"),
      o2On (context, "osc2.on"),
      o3On (context, "osc3.on"),
      noiseOn (context, "noise.on")
{
    const auto repaintCb = [this] { repaint(); };
    masterVolumeHandle.onChange = repaintCb;
    directLevelHandle.onChange = repaintCb;
    mainFxBypassHandle.onChange = [this]
    {
        const bool bypassed = mainFxBypassHandle.valid() && mainFxBypassHandle.plain() >= 0.5f;
        fxBypassButton.setButtonText (bypassed ? "FX Byp" : "FX On");
        repaint();
    };
    filterRoutingHandle.onChange = [this]
    {
        const int route = filterRoutingHandle.valid() ? juce::roundToInt (filterRoutingHandle.plain()) : 0;
        const char* const kRoutes[] = { "Parallel", "Series 1-2", "Series 2-1" };
        routingButton.setButtonText (route >= 0 && route < 3 ? kRoutes[route] : "Route");
        repaint();
    };

    fxBypassButton.onClick = [this] { toggleFxBypass(); };
    routingButton.onClick = [this] { stepFilterRouting(); };

    addAndMakeVisible (fxBypassButton);
    addAndMakeVisible (routingButton);
}

OscMixConsoleView::~OscMixConsoleView() = default;

void OscMixConsoleView::toggleFxBypass()
{
    if (mainFxBypassHandle.valid())
    {
        const float cur = mainFxBypassHandle.plain();
        mainFxBypassHandle.setPlainComplete (cur >= 0.5f ? 0.0f : 1.0f);
    }
}

void OscMixConsoleView::stepFilterRouting()
{
    if (filterRoutingHandle.valid())
    {
        const int cur = juce::roundToInt (filterRoutingHandle.plain());
        const int next = (cur + 1) % 3;
        filterRoutingHandle.setPlainComplete (static_cast<float> (next));
    }
}

void OscMixConsoleView::resized()
{
    const auto& t = ctx.theme;
    const int touchH = t.touch();
    const int gap = t.gap();
    const int btnW = juce::jmax (touchH, t.ipx (64.0f));

    int x = t.gap();
    const int y = t.gap();
    fxBypassButton.setBounds (x, y, btnW, touchH);
    x += btnW + gap;
    routingButton.setBounds (x, y, juce::jmax (touchH, t.ipx (76.0f)), touchH);
}

void OscMixConsoleView::mouseDown (const juce::MouseEvent& e)
{
    dragStartPos = e.position;
    const float w = static_cast<float> (getWidth());
    // Direct manipulation on the rightmost two master faders: Main and Direct
    const float mainFaderLeft = w - static_cast<float> (ctx.theme.touch() * 2);
    const float directFaderLeft = w - static_cast<float> (ctx.theme.touch());

    if (e.position.x >= directFaderLeft)
    {
        activeFader = FaderTarget::direct;
        if (directLevelHandle.valid())
        {
            directLevelHandle.beginGesture();
            dragStartNorm = directLevelHandle.normalised();
        }
    }
    else if (e.position.x >= mainFaderLeft)
    {
        activeFader = FaderTarget::main;
        if (masterVolumeHandle.valid())
        {
            masterVolumeHandle.beginGesture();
            dragStartNorm = masterVolumeHandle.normalised();
        }
    }
    else
    {
        activeFader = FaderTarget::none;
    }
}

void OscMixConsoleView::mouseDrag (const juce::MouseEvent& e)
{
    if (activeFader == FaderTarget::none)
        return;

    const float h = static_cast<float> (juce::jmax (1, getHeight()));
    const float dy = e.position.y - dragStartPos.y;
    const float newNorm = juce::jlimit (0.0f, 1.0f, dragStartNorm - dy / h);

    if (activeFader == FaderTarget::main && masterVolumeHandle.valid())
        masterVolumeHandle.setNormalised (newNorm);
    else if (activeFader == FaderTarget::direct && directLevelHandle.valid())
        directLevelHandle.setNormalised (newNorm);
}

void OscMixConsoleView::mouseUp (const juce::MouseEvent&)
{
    if (activeFader == FaderTarget::main && masterVolumeHandle.valid())
        masterVolumeHandle.endGesture();
    else if (activeFader == FaderTarget::direct && directLevelHandle.valid())
        directLevelHandle.endGesture();
    activeFader = FaderTarget::none;
}

void OscMixConsoleView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;

    const auto r = getLocalBounds().toFloat().reduced (t.px (1.0f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (r, t.px (8.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (r, t.px (8.0f), 1.0f);

    // Channel strips layout
    const float headerH = static_cast<float> (t.touch() + t.gap());
    auto body = r.withTrimmedTop (headerH).reduced (t.px (6.0f), t.px (4.0f));

    struct ChannelInfo
    {
        const char* name;
        float level;
        bool on;
    };
    const ChannelInfo channels[] = {
        { "O1", o1Level.valid() ? o1Level.normalised() : 0.5f, o1On.valid() && o1On.plain() >= 0.5f },
        { "O2", o2Level.valid() ? o2Level.normalised() : 0.5f, o2On.valid() && o2On.plain() >= 0.5f },
        { "O3", o3Level.valid() ? o3Level.normalised() : 0.5f, o3On.valid() && o3On.plain() >= 0.5f },
        { "NOISE", noiseLevel.valid() ? noiseLevel.normalised() : 0.5f, noiseOn.valid() && noiseOn.plain() >= 0.5f },
        { "F1", f1Level.valid() ? f1Level.normalised() : 0.5f, true },
        { "F2", f2Level.valid() ? f2Level.normalised() : 0.5f, true },
        { "B1", b1Level.valid() ? b1Level.normalised() : 0.5f, true },
        { "B2", b2Level.valid() ? b2Level.normalised() : 0.5f, true },
    };

    const float masterWidth = static_cast<float> (t.touch() * 2 + t.gap());
    auto stripsArea = body.withTrimmedRight (masterWidth);
    auto masterArea = body.removeFromRight (masterWidth);

    const int numStrips = 8;
    const float stripW = (stripsArea.getWidth() - static_cast<float> (numStrips - 1) * t.px (3.0f)) / static_cast<float> (numStrips);

    g.setFont (t.labelFont());

    for (int i = 0; i < numStrips; ++i)
    {
        const auto& ch = channels[i];
        const float x = stripsArea.getX() + static_cast<float> (i) * (stripW + t.px (3.0f));
        auto col = juce::Rectangle<float> (x, stripsArea.getY(), stripW, stripsArea.getHeight());

        // Channel meter background
        g.setColour (c.track.withAlpha (0.3f));
        g.fillRect (col.reduced (t.px (2.0f), 0.0f));

        // Level bar
        const float barH = ch.level * col.getHeight() * 0.7f;
        const float barY = col.getBottom() - barH - t.px (16.0f);
        g.setColour (ch.on ? c.accent.withAlpha (0.75f) : c.textDim.withAlpha (0.3f));
        g.fillRect (col.getX() + t.px (3.0f), barY, col.getWidth() - t.px (6.0f), barH);

        // Name
        g.setColour (ch.on ? c.text : c.textDim);
        g.drawText (ch.name, col.removeFromBottom (t.px (14.0f)).toNearestInt(), juce::Justification::centred, false);
    }

    // Master / Direct Faders
    const float faderColW = (masterArea.getWidth() - t.px (4.0f)) * 0.5f;

    // Main Fader
    auto mainCol = juce::Rectangle<float> (masterArea.getX(), masterArea.getY(), faderColW, masterArea.getHeight());
    g.setColour (c.track.withAlpha (0.4f));
    g.fillRect (mainCol.reduced (t.px (2.0f), 0.0f));

    const float mainNorm = masterVolumeHandle.valid() ? masterVolumeHandle.normalised() : 0.7f;
    const float mainH = mainNorm * mainCol.getHeight() * 0.75f;
    g.setColour (c.accent);
    g.fillRect (mainCol.getX() + t.px (3.0f), mainCol.getBottom() - mainH - t.px (16.0f), mainCol.getWidth() - t.px (6.0f), mainH);

    g.setFont (t.labelFont());
    g.setColour (c.text);
    g.drawText ("MAIN", mainCol.removeFromBottom (t.px (14.0f)).toNearestInt(), juce::Justification::centred, false);

    // Direct Fader
    auto dirCol = juce::Rectangle<float> (masterArea.getX() + faderColW + t.px (4.0f), masterArea.getY(), faderColW, masterArea.getHeight());
    g.setColour (c.track.withAlpha (0.4f));
    g.fillRect (dirCol.reduced (t.px (2.0f), 0.0f));

    const float dirNorm = directLevelHandle.valid() ? directLevelHandle.normalised() : 0.5f;
    const float dirH = dirNorm * dirCol.getHeight() * 0.75f;
    g.setColour (c.mod);
    g.fillRect (dirCol.getX() + t.px (3.0f), dirCol.getBottom() - dirH - t.px (16.0f), dirCol.getWidth() - t.px (6.0f), dirH);

    g.setColour (c.text);
    g.drawText ("DIR", dirCol.removeFromBottom (t.px (14.0f)).toNearestInt(), juce::Justification::centred, false);
}
} // namespace synthyboy::ui
