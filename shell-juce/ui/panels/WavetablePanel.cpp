// SynthyBoy - UI panel: Wavetable view and editor (docs/ui-design/03-wavetable.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WavetablePanel.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::ui
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;

// Synthesizes a waveform for table index (0..12) at frame fraction t (0..1)
void synthesizeTableFrame (int tableIndex, float t, wtedit::Frame& outFrame)
{
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (wtedit::kWaveformSize);
        float sample = 0.0f;

        switch (tableIndex)
        {
            case 0: // Sine
                sample = std::sin (2.0f * kPi * x);
                break;
            case 1: // Saw
                sample = 1.0f - 2.0f * x;
                break;
            case 2: // Square
                sample = (x < 0.5f) ? 1.0f : -1.0f;
                break;
            case 3: // Triangle
                sample = std::abs (4.0f * x - 2.0f) - 1.0f;
                break;
            case 4: // Pulse 25%
                sample = (x < 0.25f) ? 1.0f : -1.0f;
                break;
            case 5: // Saw to Square morph
            {
                const float saw = 1.0f - 2.0f * x;
                const float sqr = (x < 0.5f) ? 1.0f : -1.0f;
                sample = saw * (1.0f - t) + sqr * t;
                break;
            }
            case 6: // Sine to Saw morph
            {
                const float sinVal = std::sin (2.0f * kPi * x);
                const float sawVal = 1.0f - 2.0f * x;
                sample = sinVal * (1.0f - t) + sawVal * t;
                break;
            }
            case 7: // Harmonic Series
            {
                for (int k = 1; k <= 8; ++k)
                    sample += (1.0f / static_cast<float> (k)) * std::sin (2.0f * kPi * static_cast<float> (k) * x);
                sample *= 0.5f;
                break;
            }
            case 8: // Odd Harmonics
            {
                for (int k = 1; k <= 7; k += 2)
                    sample += (1.0f / static_cast<float> (k)) * std::sin (2.0f * kPi * static_cast<float> (k) * x);
                sample *= 0.6f;
                break;
            }
            case 9: // Formant Sweep
            {
                const float f1 = 1.0f + t * 4.0f;
                sample = std::sin (2.0f * kPi * x) * std::sin (2.0f * kPi * f1 * x);
                break;
            }
            case 10: // FM Sweep
            {
                const float mod = std::sin (2.0f * kPi * x) * (t * 2.0f);
                sample = std::sin (2.0f * kPi * x + mod);
                break;
            }
            case 11: // PWM Sweep
            {
                const float duty = 0.1f + t * 0.8f;
                sample = (x < duty) ? 1.0f : -1.0f;
                break;
            }
            case 12: // Bell Partials
            {
                sample = 0.5f * std::sin (2.0f * kPi * x) +
                         0.3f * std::sin (2.0f * kPi * 2.76f * x) +
                         0.2f * std::sin (2.0f * kPi * 5.4f * x);
                break;
            }
            default: // Default Saw
                sample = 1.0f - 2.0f * x;
                break;
        }

        outFrame[i] = sample;
    }
}
} // namespace

//==============================================================================
// OSC WAVE VIEW IMPLEMENTATION
//==============================================================================
WavetableView::WavetableView (UiContext& context, std::function<void()> onOpenEditorCallback)
    : ctx (context),
      onOpenEditor (std::move (onOpenEditorCallback)),
      wt ("Saw to Square"),
      handleWavetable (context, "osc1.wavetable"),
      handleFrame (context, "osc1.frame"),
      handleFrameSpread (context, "osc1.frame_spread"),
      handlePhase (context, "osc1.phase"),
      handleRandomPhase (context, "osc1.random_phase"),
      handleWarpType (context, "osc1.warp.type"),
      handleWarpAmount (context, "osc1.warp.amount"),
      handleMorphType (context, "osc1.morph.type"),
      handleMorphAmount (context, "osc1.morph.amount")
{
    handleWavetable.onChange = [this] { updateTableFromChoice(); repaint(); };
    handleFrame.onChange = [this] { repaint(); };
    handleFrameSpread.onChange = [this] { repaint(); };
    handlePhase.onChange = [this] { repaint(); };
    handleRandomPhase.onChange = [this] { repaint(); };
    handleWarpType.onChange = [this] { repaint(); };
    handleWarpAmount.onChange = [this] { repaint(); };
    handleMorphType.onChange = [this] { repaint(); };
    handleMorphAmount.onChange = [this] { repaint(); };

    // Build default 64-frame table for morphing
    wt.clear();
    for (size_t f = 0; f < 64; ++f)
    {
        wtedit::Frame frame;
        const float t = static_cast<float> (f) / 63.0f;
        synthesizeTableFrame (5, t, frame); // Saw to Square
        wt.insertFrame (f, frame);
    }

    prevTableBtn.onClick = [this] { stepWavetable (-1); };
    nextTableBtn.onClick = [this] { stepWavetable (1); };

    mode2DBtn.setClickingTogglesState (true);
    mode2DBtn.setRadioGroupId (501);
    mode2DBtn.onClick = [this] { setViewMode (ViewMode::mode2D); };

    mode3DBtn.setClickingTogglesState (true);
    mode3DBtn.setRadioGroupId (501);
    mode3DBtn.setToggleState (true, juce::dontSendNotification);
    mode3DBtn.onClick = [this] { setViewMode (ViewMode::mode3D); };

    modeSpBtn.setClickingTogglesState (true);
    modeSpBtn.setRadioGroupId (501);
    modeSpBtn.onClick = [this] { setViewMode (ViewMode::modeSP); };

    editBtn.onClick = [this]
    {
        if (onOpenEditor)
            onOpenEditor();
    };

    zoomInBtn.onClick = [this] { zoomIn(); };
    zoomOutBtn.onClick = [this] { zoomOut(); };
    fingers.onGesture = [this] (const TouchTracker::Gesture& g) { handleTouchGesture (g); };

    addAndMakeVisible (tableNameLabel);
    addAndMakeVisible (prevTableBtn);
    addAndMakeVisible (nextTableBtn);
    addAndMakeVisible (mode2DBtn);
    addAndMakeVisible (mode3DBtn);
    addAndMakeVisible (modeSpBtn);
    addAndMakeVisible (editBtn);
    addAndMakeVisible (zoomInBtn);
    addAndMakeVisible (zoomOutBtn);

    updateTableFromChoice();
}

int WavetableView::preferredHeight() const
{
    return ctx.theme.ipx (200.0f);
}

void WavetableView::stepWavetable (int direction)
{
    if (handleWavetable.valid())
    {
        const int cur = juce::roundToInt (handleWavetable.plain());
        const auto& inf = handleWavetable.info();
        const int next = juce::jlimit (static_cast<int> (inf.min), static_cast<int> (inf.max), cur + direction);
        handleWavetable.setPlainComplete (static_cast<float> (next));
        updateTableFromChoice();
        repaint();
    }
}

void WavetableView::updateTableFromChoice()
{
    if (handleWavetable.valid())
    {
        const int idx = juce::roundToInt (handleWavetable.plain());
        const auto& inf = handleWavetable.info();
        if (idx >= 0 && idx < static_cast<int> (inf.numChoices) && inf.choices != nullptr)
        {
            tableNameLabel.setText (inf.choices[idx], juce::dontSendNotification);
            tableNameLabel.setFont (ctx.theme.titleFont());
            tableNameLabel.setColour (juce::Label::textColourId, ctx.theme.colours.text);

            // Re-synthesize table preview if index is built-in
            if (idx < 13)
            {
                wt.clear();
                const size_t numFrames = (idx >= 5 && idx <= 11) ? 64 : 1;
                for (size_t f = 0; f < numFrames; ++f)
                {
                    wtedit::Frame frame;
                    const float t = (numFrames > 1) ? (static_cast<float> (f) / static_cast<float> (numFrames - 1)) : 0.0f;
                    synthesizeTableFrame (idx, t, frame);
                    wt.insertFrame (f, frame);
                }
            }
        }
    }
}

void WavetableView::setViewMode (ViewMode mode)
{
    currentViewMode = mode;
    mode2DBtn.setToggleState (mode == ViewMode::mode2D, juce::dontSendNotification);
    mode3DBtn.setToggleState (mode == ViewMode::mode3D, juce::dontSendNotification);
    modeSpBtn.setToggleState (mode == ViewMode::modeSP, juce::dontSendNotification);
    zoomInBtn.setVisible (mode == ViewMode::mode3D);
    zoomOutBtn.setVisible (mode == ViewMode::mode3D);
    repaint();
}

void WavetableView::cycleViewMode()
{
    switch (currentViewMode)
    {
        case ViewMode::mode2D: setViewMode (ViewMode::mode3D); break;
        case ViewMode::mode3D: setViewMode (ViewMode::modeSP); break;
        case ViewMode::modeSP: setViewMode (ViewMode::mode2D); break;
    }
}

void WavetableView::setWavetable (const wtedit::Wavetable& newWt)
{
    wt = newWt;
    tableNameLabel.setText (juce::String (wt.name()), juce::dontSendNotification);
    repaint();
}

void WavetableView::zoomIn()
{
    zoomLevel = juce::jmin (2.5f, zoomLevel * 1.25f);
    repaint();
}

void WavetableView::zoomOut()
{
    zoomLevel = juce::jmax (0.5f, zoomLevel * 0.8f);
    repaint();
}

void WavetableView::resetView()
{
    zoomLevel = 1.0f;
    tiltY = 1.0f;
    yawX = 0.0f;
    repaint();
}

void WavetableView::handleTouchGesture (const TouchTracker::Gesture& g)
{
    if (currentViewMode != ViewMode::mode3D)
        return;
    if (g.kind == TouchTracker::Kind::pinch)
    {
        zoomLevel = juce::jlimit (0.5f, 2.5f, zoomLevel * g.scale);
        repaint();
    }
    else if (g.kind == TouchTracker::Kind::pan)
    {
        tiltY = juce::jlimit (0.25f, 2.0f, tiltY + g.dy / static_cast<float> (juce::jmax (1, getHeight())) * 2.0f);
        yawX = juce::jlimit (-1.0f, 1.0f, yawX + g.dx / static_cast<float> (juce::jmax (1, getWidth())) * 2.0f);
        repaint();
    }
}

void WavetableView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (currentViewMode == ViewMode::mode3D)
        resetView();
}

void WavetableView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (currentViewMode != ViewMode::mode3D)
        return;
    TouchTracker::Gesture g;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        g.kind = TouchTracker::Kind::pinch;
        g.scale = std::exp (wheel.deltaY * 2.0f);
    }
    else
    {
        g.kind = TouchTracker::Kind::pan;
        g.dx = wheel.deltaX * 200.0f;
        g.dy = wheel.deltaY * 200.0f;
    }
    handleTouchGesture (g);
}

void WavetableView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    fingers.magnify (e.position, scaleFactor);
}

void WavetableView::mouseDown (const juce::MouseEvent& e)
{
    fingers.down (e);
    if (e.source.getIndex() > 0 || fingers.twoDown())
    {
        // the second finger: tilt / zoom take over, an open scrub is closed
        if (isDragging && handleFrame.valid())
            handleFrame.endGesture();
        isDragging = false;
        pairActive = true;
        return;
    }
    pairActive = false;
    dragStartPos = e.position;
    isDragging = false;
    if (handleFrame.valid())
        startFrameVal = handleFrame.normalised();

    // Check if clicked in SP mode to select harmonic bin
    if (currentViewMode == ViewMode::modeSP)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (static_cast<float> (ctx.theme.gap()));
        const float relX = (e.position.x - bounds.getX()) / bounds.getWidth();
        if (relX >= 0.0f && relX <= 1.0f)
        {
            selectedHarmonic = juce::jlimit (1, 64, static_cast<int> (relX * 64.0f) + 1);
            repaint();
        }
    }
}

void WavetableView::mouseDrag (const juce::MouseEvent& e)
{
    fingers.drag (e);
    if (e.source.getIndex() > 0 || fingers.twoDown() || pairActive)
        return;
    const float dx = e.position.x - dragStartPos.x;
    if (! isDragging && std::abs (dx) > 3.0f)
    {
        isDragging = true;
        if (handleFrame.valid())
            handleFrame.beginGesture();
    }

    if (isDragging && handleFrame.valid())
    {
        // Drag horizontally scrubs osc1.frame
        const float deltaNorm = dx / (static_cast<float> (getWidth()) * 0.8f);
        const float newNorm = juce::jlimit (0.0f, 1.0f, startFrameVal + deltaNorm);
        handleFrame.setNormalised (newNorm);
        repaint();
    }
}

void WavetableView::mouseUp (const juce::MouseEvent& e)
{
    fingers.up (e);
    if (e.source.getIndex() > 0)
        return;
    if (pairActive)
    {
        pairActive = fingers.twoDown();
        return;
    }
    if (isDragging)
    {
        if (handleFrame.valid())
            handleFrame.endGesture();
        isDragging = false;
    }
    else
    {
        // Single tap cycles view mode (2D -> 3D -> SP)
        cycleViewMode();
    }
}

void WavetableView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    auto r = getLocalBounds().toFloat().reduced (t.px (2.0f));

    // Background panel
    g.setColour (t.colours.panelRaised);
    g.fillRoundedRectangle (r, t.px (6.0f));
    g.setColour (t.colours.outline);
    g.drawRoundedRectangle (r, t.px (6.0f), 1.0f);

    auto canvasArea = r.withTrimmedTop (static_cast<float> (headerHeight())).reduced (t.px (6.0f));
    if (canvasArea.getWidth() <= 10.0f || canvasArea.getHeight() <= 10.0f)
        return;

    // Current frame fraction [0..1]
    const float frameNorm = handleFrame.valid() ? handleFrame.normalised() : 0.0f;
    const size_t numFrames = wt.numFrames();
    const size_t curFrameIdx = juce::jlimit (size_t (0), numFrames - 1,
        static_cast<size_t> (frameNorm * static_cast<float> (numFrames - 1)));

    if (currentViewMode == ViewMode::mode2D)
    {
        // 2D VIEW: Center axis line
        const float midY = canvasArea.getCentreY();
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (midY), canvasArea.getX(), canvasArea.getRight());

        // Unison frame spread ghosts if spread != 0
        const float spread = handleFrameSpread.valid() ? handleFrameSpread.plain() : 0.0f;
        if (std::abs (spread) > 1.0f && numFrames > 1)
        {
            g.setColour (t.colours.accent.withAlpha (0.25f));
            for (int sign : { -1, 1 })
            {
                const float ghostT = juce::jlimit (0.0f, 1.0f, frameNorm + static_cast<float> (sign) * (spread / 100.0f) * 0.25f);
                const size_t gIdx = static_cast<size_t> (ghostT * static_cast<float> (numFrames - 1));
                const auto& gFrame = wt.getFrame (gIdx);
                juce::Path gp;
                for (size_t s = 0; s < wtedit::kWaveformSize; s += 16)
                {
                    const float gx = canvasArea.getX() + (static_cast<float> (s) / static_cast<float> (wtedit::kWaveformSize - 1)) * canvasArea.getWidth();
                    const float gy = midY - gFrame[s] * (canvasArea.getHeight() * 0.4f);
                    if (s == 0) gp.startNewSubPath (gx, gy); else gp.lineTo (gx, gy);
                }
                g.strokePath (gp, juce::PathStrokeType (1.0f));
            }
        }

        // Draw current frame waveform
        const auto& curFrame = wt.getFrame (curFrameIdx);
        juce::Path p;
        for (size_t s = 0; s < wtedit::kWaveformSize; s += 8)
        {
            const float px = canvasArea.getX() + (static_cast<float> (s) / static_cast<float> (wtedit::kWaveformSize - 1)) * canvasArea.getWidth();
            const float py = midY - curFrame[s] * (canvasArea.getHeight() * 0.42f);
            if (s == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (t.colours.accent);
        g.strokePath (p, juce::PathStrokeType (t.px (2.0f)));

        // Warp preview (dotted curve if warp amount > 0)
        const float warpAmt = handleWarpAmount.valid() ? handleWarpAmount.plain() : 0.0f;
        if (warpAmt > 5.0f)
        {
            juce::Path wp;
            const float factor = 1.0f + (warpAmt / 100.0f) * 2.0f;
            for (size_t s = 0; s < wtedit::kWaveformSize; s += 8)
            {
                const float frac = static_cast<float> (s) / static_cast<float> (wtedit::kWaveformSize - 1);
                const float warpedFrac = std::fmod (frac * factor, 1.0f);
                const size_t ws = static_cast<size_t> (warpedFrac * static_cast<float> (wtedit::kWaveformSize - 1));
                const float wpx = canvasArea.getX() + frac * canvasArea.getWidth();
                const float wpy = midY - curFrame[ws] * (canvasArea.getHeight() * 0.42f);
                if (s == 0) wp.startNewSubPath (wpx, wpy); else wp.lineTo (wpx, wpy);
            }
            g.setColour (t.colours.mod);
            const float dashLengths[] = { 4.0f, 4.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt)
                .createDashedStroke (dashed, wp, dashLengths, 2);
            g.fillPath (dashed);
        }

        // Phase marker line
        const float phaseDeg = handlePhase.valid() ? handlePhase.plain() : 0.0f;
        const float phaseX = canvasArea.getX() + (phaseDeg / 360.0f) * canvasArea.getWidth();
        g.setColour (t.colours.warn.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (phaseX), canvasArea.getY(), canvasArea.getBottom());

        // Random phase shaded band
        const float randPhase = handleRandomPhase.valid() ? handleRandomPhase.plain() : 0.0f;
        if (randPhase > 1.0f)
        {
            const float bandW = (randPhase / 100.0f) * canvasArea.getWidth() * 0.5f;
            g.setColour (t.colours.warn.withAlpha (0.15f));
            g.fillRect (phaseX - bandW * 0.5f, canvasArea.getY(), bandW, canvasArea.getHeight());
        }
    }
    else if (currentViewMode == ViewMode::mode3D)
    {
        // 3D STACKED-FRAME VIEW
        const size_t drawCount = juce::jmin (size_t (64), numFrames);
        const float stepY = (canvasArea.getHeight() * 0.45f * zoomLevel * tiltY) / static_cast<float> (drawCount);
        const float stepX = (canvasArea.getWidth() * 0.15f * zoomLevel * (1.0f + yawX)) / static_cast<float> (drawCount);
        const float waveW = canvasArea.getWidth() * 0.75f;
        const float waveH = canvasArea.getHeight() * 0.28f * zoomLevel;

        // Render from back (index 0) to front (index drawCount-1)
        for (size_t i = 0; i < drawCount; ++i)
        {
            const size_t fIdx = (drawCount == numFrames) ? i : (i * (numFrames - 1) / (drawCount - 1));
            const auto& frame = wt.getFrame (fIdx);
            const bool isCur = (fIdx == curFrameIdx);
            const bool isKeyframe = (fIdx == 0 || fIdx == numFrames - 1 || (fIdx % 8 == 0));

            const float ox = canvasArea.getX() + static_cast<float> (drawCount - 1 - i) * stepX;
            const float oy = canvasArea.getY() + static_cast<float> (i) * stepY + canvasArea.getHeight() * 0.25f;

            juce::Path p;
            for (size_t s = 0; s < wtedit::kWaveformSize; s += 16)
            {
                const float px = ox + (static_cast<float> (s) / static_cast<float> (wtedit::kWaveformSize - 1)) * waveW;
                const float py = oy - frame[s] * waveH;
                if (s == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }

            if (isCur)
            {
                // Current active frame: yellow/accent, prominent line
                g.setColour (t.colours.accent);
                g.strokePath (p, juce::PathStrokeType (t.px (2.5f)));
            }
            else if (isKeyframe)
            {
                // Real / keyframe: bright green
                g.setColour (t.colours.ok.withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (1.2f));
            }
            else
            {
                // Interpolated frame: subtle grey outline
                g.setColour (t.colours.outline.withAlpha (0.4f));
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
        }
    }
    else if (currentViewMode == ViewMode::modeSP)
    {
        // SPECTRAL VIEW: FFT harmonic magnitudes of current frame
        std::array<float, wtedit::kWaveformSize> mags {};
        std::array<float, wtedit::kWaveformSize> phases {};
        const auto& curFrame = wt.getFrame (curFrameIdx);
        wtedit::Radix2Fft::realToPolar (curFrame.sampleData(), mags.data(), phases.data(), wtedit::kWaveformSize);

        const int numBins = 64;
        const float barW = canvasArea.getWidth() / static_cast<float> (numBins);

        for (int k = 1; k <= numBins; ++k)
        {
            const float mag = juce::jlimit (0.0f, 1.0f, mags[static_cast<size_t> (k)]);
            const float barH = mag * (canvasArea.getHeight() - t.px (24.0f));
            const float bx = canvasArea.getX() + static_cast<float> (k - 1) * barW;
            const float by = canvasArea.getBottom() - barH;

            const bool isSel = (k == selectedHarmonic);
            g.setColour (isSel ? t.colours.accent : t.colours.mod);
            g.fillRect (bx + t.px (1.0f), by, barW - t.px (2.0f), barH);
        }

        // Tap harmonic indicator
        g.setColour (t.colours.text);
        g.setFont (t.labelFont());
        g.drawText ("Harmonic " + juce::String (selectedHarmonic) + "  " +
                    juce::String (mags[static_cast<size_t> (selectedHarmonic)], 3),
                    canvasArea.withHeight (t.px (20.0f)), juce::Justification::topRight, false);
    }

    // Frame position lower-left label (e.g. "frame 27 / 64")
    g.setColour (t.colours.textDim);
    g.setFont (t.labelFont());
    g.drawText ("frame " + juce::String (curFrameIdx + 1) + " / " + juce::String (numFrames),
                canvasArea.removeFromBottom (t.px (18.0f)), juce::Justification::bottomLeft, false);
}

int WavetableView::headerHeight() const
{
    const auto& t = ctx.theme;
    // one row: [<][>] name [2D|3D|SP] [EDIT]; narrower than that the view switch drops to a second row
    const int segW = juce::jmax (t.touch(), t.ipx (46.0f));
    const int oneRowMin = 2 * t.touch() + t.ipx (2.0f) + 3 * t.gap() + 3 * segW + juce::jmax (t.touch(), t.ipx (60.0f)) + t.ipx (60.0f);
    return getWidth() - 2 * t.gap() >= oneRowMin ? t.touch() : 2 * t.touch() + t.gap();
}

void WavetableView::resized()
{
    const auto& t = ctx.theme;
    const int touch = t.touch();
    const int gap = t.gap();
    const bool twoRows = headerHeight() > touch;
    auto bounds = getLocalBounds().reduced (gap);
    auto header = bounds.removeFromTop (touch);
    juce::Rectangle<int> second;
    if (twoRows)
    {
        bounds.removeFromTop (gap);
        second = bounds.removeFromTop (touch);
    }

    prevTableBtn.setBounds (header.removeFromLeft (touch));
    header.removeFromLeft (t.ipx (2));
    nextTableBtn.setBounds (header.removeFromLeft (touch));
    header.removeFromLeft (gap);

    editBtn.setBounds (header.removeFromRight (juce::jmax (touch, t.ipx (60))));
    header.removeFromRight (gap);

    // Segmented 2D | 3D | SP buttons
    const int segW = juce::jmax (touch, t.ipx (46));
    auto& segRow = twoRows ? second : header;
    modeSpBtn.setBounds (segRow.removeFromRight (segW));
    mode3DBtn.setBounds (segRow.removeFromRight (segW));
    mode2DBtn.setBounds (segRow.removeFromRight (segW));
    header.removeFromRight (twoRows ? 0 : gap);

    tableNameLabel.setBounds (header);

    // Zoom buttons (3D tilt / zoom, the pinch alternative) float in the bottom-right corner of the canvas
    auto corner = getLocalBounds().reduced (gap);
    zoomInBtn.setBounds (corner.removeFromRight (touch).removeFromBottom (touch));
    corner.removeFromRight (t.ipx (2));
    zoomOutBtn.setBounds (corner.removeFromRight (touch).removeFromBottom (touch));
}

//==============================================================================
// WAVETABLE PANEL MAIN IMPLEMENTATION
//==============================================================================
WavetablePanel::WavetablePanel (UiContext& context)
    : Panel (context)
{
}

std::vector<std::string> WavetablePanel::keys() const
{
    return {
        // 4 Primary controls next to the visual
        "osc1.frame",
        "osc1.frame_spread",
        "osc1.warp.amount",
        "osc1.morph.amount",
        // Behind More toggle
        "osc1.wavetable",
        "osc1.phase",
        "osc1.random_phase",
        "osc1.warp.type",
        "osc1.warp.phase",
        "osc1.warp.spread",
        "osc1.morph.type",
        "osc1.morph.spread"
    };
}

void WavetablePanel::build()
{
    // 1. Add big visual
    waveView = std::make_unique<WavetableView> (ctx, [this] { openEditorSheet(); });
    addAndMakeVisible (*waveView);

    // 2. Add primary controls (4)
    const auto all = keys();
    for (size_t i = 0; i < 4 && i < all.size(); ++i)
        addControl (all[i]);

    // 3. Add more controls (8)
    for (size_t i = 4; i < all.size(); ++i)
        addMoreControl (all[i]);
}

int WavetablePanel::preferredHeight (int width) const
{
    const auto& t = ctx.theme;
    const int visualH = waveView ? waveView->preferredHeight() : t.ipx (200.0f);
    const juce::Rectangle<int> controlsArea (0, 0, width - 2 * t.gap(), 100000);
    return headerHeight() + 2 * t.gap() + visualH + t.gap() +
           const_cast<WavetablePanel*> (this)->layoutControls (controlsArea, true);
}

void WavetablePanel::resized()
{
    const auto& t = ctx.theme;
    auto area = contentArea();
    const int visualH = waveView ? waveView->preferredHeight() : t.ipx (200.0f);

    if (waveView)
    {
        waveView->setBounds (area.removeFromTop (visualH));
        area.removeFromTop (t.gap());
    }

    layoutControls (area, false);

    if (editorSheet && editorSheet->isVisible())
    {
        auto* topComp = getTopLevelComponent();
        if (topComp != nullptr)
            editorSheet->setBounds (topComp->getLocalBounds());
        else
            editorSheet->setBounds (getLocalBounds());
    }
}

void WavetablePanel::openEditorSheet()
{
    if (! editorSheet)
    {
        wtedit::Wavetable initialWt = waveView ? waveView->wavetable() : wtedit::Wavetable ("Default");
        editorSheet = std::make_unique<WavetableEditorSheet> (ctx, std::move (initialWt));
        editorSheet->onClosed = [this] (const wtedit::Wavetable& modifiedWt)
        {
            if (waveView)
                waveView->setWavetable (modifiedWt);
        };
    }

    auto* topComp = getTopLevelComponent();
    if (topComp != nullptr)
    {
        topComp->addAndMakeVisible (*editorSheet);
        editorSheet->setBounds (topComp->getLocalBounds());
    }
    else
    {
        addAndMakeVisible (*editorSheet);
        editorSheet->setBounds (getLocalBounds());
    }
    editorSheet->setVisible (true);
    editorSheet->toFront (true);
}

SYNTHYBOY_REGISTER_PANEL (WavetablePanel, Page::osc, 40)

} // namespace synthyboy::ui
