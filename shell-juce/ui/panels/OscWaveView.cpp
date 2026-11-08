// SynthyBoy - UI: oscillator, noise, and mixer visualizers implementation (docs/ui-design/02-oscillators.md)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OscWaveView.h"

#include <cmath>

#include "../ModuleSheet.h"

namespace synthyboy::ui
{
// Helper: evaluate sample point in [0, 1] for a given wavetable choice and frame; the warp is the picture of its mode (warpedValue)
static float evalWave (float phase, int wtChoice, float frame, float warpAmount, int warpType)
{
    return warpedValue (warpType, warpAmount, phase, [wtChoice, frame] (float ph)
    {
        const float twoPi = 6.28318530718f;
        const float theta = ph * twoPi;

        float y0 = 0.0f;
        switch (wtChoice % 4)
        {
            case 0: // Sine
                y0 = std::sin (theta);
                break;
            case 1: // Triangle
                y0 = (ph < 0.25f) ? (4.0f * ph) : ((ph < 0.75f) ? (2.0f - 4.0f * ph) : (-4.0f + 4.0f * ph));
                break;
            case 2: // Saw
                y0 = 1.0f - 2.0f * ph;
                break;
            case 3: // Square
                y0 = (ph < 0.5f) ? 0.9f : -0.9f;
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
    });
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
      sampleLoopEndHandle (context, "osc" + std::to_string (oscIndex) + ".sample.loop_end"),
      sliceModeHandle (context, "osc" + std::to_string (oscIndex) + ".sample.slice_mode"),
      sliceThresholdHandle (context, "osc" + std::to_string (oscIndex) + ".sample.slice_threshold")
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
    sliceModeHandle.onChange = [this] { refreshSliceCache(); updateButtons(); repaint(); };
    sliceThresholdHandle.onChange = repaintCb;

    prevWtButton.onClick = [this] { stepWavetable (-1); };
    nextWtButton.onClick = [this] { stepWavetable (+1); };
    viewModeButton.onClick = [this] { cycleViewMode(); };

    // the visible alternatives of the sample gestures (design 02: zoom buttons, slice + / trash tools)
    zoomInBtn.onClick = [this] { zoomSampleAt (waveArea().getCentreX(), 2.0f); };
    zoomOutBtn.onClick = [this] { zoomSampleAt (waveArea().getCentreX(), 0.5f); };
    fitBtn.onClick = [this] { fitSample(); };
    addSliceBtn.onClick = [this]
    {
        // a slice at the middle of what is on screen (or just right of the selected one)
        addSliceAt (static_cast<float> (sampleVp.toRange (0.5)));
    };
    removeSliceBtn.onClick = [this]
    {
        if (selectedSlice >= 0)
            removeSlice (selectedSlice);
        else if (! sliceCache.empty())
            removeSlice (nearestSlice (sliceCache, static_cast<float> (sampleVp.toRange (0.5)), 2.0f));
    };
    press.onLongPress = [this] { longPressedAt (dragStartPos); };
    touch.onGesture = [this] (const TouchTracker::Gesture& g) { handleTouchGesture (g); };

    addAndMakeVisible (prevWtButton);
    addAndMakeVisible (nextWtButton);
    addAndMakeVisible (viewModeButton);
    addAndMakeVisible (zoomInBtn);
    addAndMakeVisible (zoomOutBtn);
    addAndMakeVisible (fitBtn);
    addAndMakeVisible (addSliceBtn);
    addAndMakeVisible (removeSliceBtn);
    refreshSliceCache();
    updateButtons();
}

OscWaveView::~OscWaveView() = default;

int OscWaveView::unisonPillLevel() const
{
    const int voices = unisonVoicesHandle.valid() ? juce::roundToInt (unisonVoicesHandle.normalised() * static_cast<float> (unisonVoicesHandle.info().max - unisonVoicesHandle.info().min) + static_cast<float> (unisonVoicesHandle.info().min)) : 1;
    return voices > 1 ? cpuLevel (ctx.host.cpuLoad()) : -1;
}

bool OscWaveView::unisonCapped() const
{
    return unisonVoicesHandle.valid() && juce::roundToInt (unisonVoicesHandle.plain()) > 8;
}

bool OscWaveView::isSampleType() const
{
    return typeHandle.valid() && typeHandle.normalised() >= 0.5f; // normalised (): the listener runs before the atomic updates
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
    zoomInBtn.setVisible (isSample);
    zoomOutBtn.setVisible (isSample);
    fitBtn.setVisible (isSample);
    const bool manual = isSample && sliceModeIndex() == 2;
    addSliceBtn.setVisible (manual);
    removeSliceBtn.setVisible (manual);
    addSliceBtn.setEnabled (canEditSlices());
    removeSliceBtn.setEnabled (canEditSlices() && ! sliceCache.empty());
    addSliceBtn.setTooltip (canEditSlices() ? "Add a slice in the middle of the view" : "no sample is loaded in this oscillator");

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

    // Top control strip: prev, next, mode buttons (sample type: - + fit, and the slice tools)
    int x = t.gap();
    const int y = t.gap();

    prevWtButton.setBounds (x, y, btnW, touchH);
    zoomOutBtn.setBounds (x, y, btnW, touchH);
    x += btnW + gap;
    nextWtButton.setBounds (x, y, btnW, touchH);
    zoomInBtn.setBounds (x, y, btnW, touchH);
    x += btnW + gap;
    viewModeButton.setBounds (x, y, juce::jmax (btnW, t.ipx (50.0f)), touchH);
    fitBtn.setBounds (x, y, juce::jmax (btnW, t.ipx (50.0f)), touchH);
    x += juce::jmax (btnW, t.ipx (50.0f)) + gap;
    addSliceBtn.setBounds (x, y, juce::jmax (btnW, t.ipx (72.0f)), touchH);
    x += juce::jmax (btnW, t.ipx (72.0f)) + gap;
    removeSliceBtn.setBounds (x, y, juce::jmax (btnW, t.ipx (72.0f)), touchH);
}

// ---- sample view geometry, slices ----------------------------------------------------------------------------------------------------

juce::Rectangle<float> OscWaveView::waveArea() const
{
    const auto& t = ctx.theme;
    return getLocalBounds().toFloat().reduced (t.px (1.0f)).reduced (t.px (8.0f), t.px (4.0f));
}

float OscWaveView::sampleNormToX (float norm) const
{
    const auto a = waveArea();
    return a.getX() + static_cast<float> (sampleVp.toScreen (static_cast<double> (norm))) * a.getWidth();
}

float OscWaveView::xToSampleNorm (float x) const
{
    const auto a = waveArea();
    return static_cast<float> (sampleVp.toRange (static_cast<double> ((x - a.getX()) / juce::jmax (1.0f, a.getWidth()))));
}

void OscWaveView::zoomSampleAt (float x, float factor)
{
    const double anchor = static_cast<double> (juce::jlimit (0.0f, 1.0f, xToSampleNorm (x)));
    sampleVp.zoomAbout (anchor, static_cast<double> (factor));
    repaint();
}

void OscWaveView::panSample (float dxPixels)
{
    sampleVp.pan (-static_cast<double> (dxPixels / juce::jmax (1.0f, waveArea().getWidth())) * sampleVp.span);
    repaint();
}

void OscWaveView::fitSample()
{
    sampleVp.fit();
    repaint();
}

int OscWaveView::sliceModeIndex() const
{
    return sliceModeHandle.valid() ? juce::roundToInt (sliceModeHandle.normalised() * static_cast<float> (sliceModeHandle.info().max)) : 0;
}

void OscWaveView::refreshSliceCache()
{
    sliceCacheMs = juce::Time::getMillisecondCounter();
    if (draggedSlice >= 0)
        return; // the slice being dragged is the truth until the drag ends
    sliceCache.clear();
    for (const auto& s : ctx.host.content().samples)
        if (s.osc == index)
            sliceCache = s.slices;
    if (selectedSlice >= static_cast<int> (sliceCache.size()))
        selectedSlice = -1;
}

bool OscWaveView::canEditSlices() const
{
    for (const auto& s : ctx.host.content().samples)
        if (s.osc == index)
            return true;
    return false;
}

bool OscWaveView::commitSlices (const std::vector<float>& newSlices)
{
    auto c = ctx.host.content();
    for (auto& s : c.samples)
        if (s.osc == index)
        {
            s.slices = newSlices;
            if (! ctx.host.setContent (c))
                return false;
            refreshSliceCache();
            updateButtons();
            repaint();
            return true;
        }
    return false;
}

bool OscWaveView::addSliceAt (float norm)
{
    if (! canEditSlices())
        return false;
    auto list = sliceCache;
    if (! synthyboy::ui::addSlice (list, juce::jlimit (0.0f, 1.0f, norm)))
        return false;
    selectedSlice = nearestSlice (list, norm, 1.0f);
    return commitSlices (list);
}

bool OscWaveView::removeSlice (int idx)
{
    auto list = sliceCache;
    if (! synthyboy::ui::removeSlice (list, idx))
        return false;
    selectedSlice = -1;
    return commitSlices (list);
}

int OscWaveView::nearestSliceAtX (float x) const
{
    int best = -1;
    float bestD = static_cast<float> (ctx.theme.touch()) * 0.5f;
    for (size_t i = 0; i < sliceCache.size(); ++i)
    {
        const float d = std::abs (sampleNormToX (sliceCache[i]) - x);
        if (d <= bestD)
        {
            bestD = d;
            best = static_cast<int> (i);
        }
    }
    return best;
}

float OscWaveView::thresholdLineY() const
{
    const auto a = waveArea();
    return a.getBottom() - synthyboy::ui::thresholdToLine (sliceThresholdHandle.valid() ? sliceThresholdHandle.plain() : 50.0f) * a.getHeight();
}

void OscWaveView::stepWavetable (int direction)
{
    if (! wtHandle.valid())
        return;
    const float next = juce::jlimit (0.0f, static_cast<float> (wtHandle.info().max), wtHandle.plain() + static_cast<float> (direction));
    wtHandle.setPlainComplete (next);
}

void OscWaveView::handleTouchGesture (const TouchTracker::Gesture& g)
{
    using K = TouchTracker::Kind;
    if (isSampleType())
    {
        if (g.kind == K::pinch)
            zoomSampleAt (g.centre.x, g.scale);
        else if (g.kind == K::pan)
            panSample (g.dx);
    }
    else
    {
        if (g.kind == K::twoFingerSwipe && onSwitchOscillator)
            onSwitchOscillator (g.dx < 0.0f ? +1 : -1); // swipe left = the next oscillator
    }
}

void OscWaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! isSampleType())
        return;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        zoomSampleAt (e.position.x, std::exp (wheel.deltaY * 2.0f));
    else
        panSample ((wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * 200.0f);
}

void OscWaveView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    touch.magnify (e.position, scaleFactor);
}

void OscWaveView::cancelOneFingerGesture()
{
    press.cancel();
    if (gestureOpen)
    {
        switch (activeMarker)
        {
            case SampleMarker::start:     sampleStartHandle.endGesture(); break;
            case SampleMarker::end:       sampleEndHandle.endGesture(); break;
            case SampleMarker::loopStart: sampleLoopStartHandle.endGesture(); break;
            case SampleMarker::loopEnd:   sampleLoopEndHandle.endGesture(); break;
            case SampleMarker::loopPair:  sampleLoopStartHandle.endGesture(); sampleLoopEndHandle.endGesture(); break;
            case SampleMarker::threshold: sliceThresholdHandle.endGesture(); break;
            case SampleMarker::none:
            case SampleMarker::slice:     break;
        }
        if (activeMarker == SampleMarker::none && frameHandle.valid() && frameHandle.inGesture())
            frameHandle.endGesture();
    }
    if (draggedSlice >= 0)
    {
        draggedSlice = -1;
        refreshSliceCache();
    }
    activeMarker = SampleMarker::none;
    gestureOpen = false;
    nameSwipe = false;
    if (loupe != nullptr)
        loupe->hide();
    repaint();
}

void OscWaveView::longPressedAt (juce::Point<float> where)
{
    if (! isSampleType() || sliceModeIndex() != 2 || ! canEditSlices())
        return;
    if (activeMarker == SampleMarker::loopPair) // held still inside the loop: this is no loop move
    {
        sampleLoopStartHandle.endGesture();
        sampleLoopEndHandle.endGesture();
        activeMarker = SampleMarker::none;
    }
    // long-press a handle removes it, long-press the waveform adds one (design 02, replaces Alt-click)
    const int hit = nearestSliceAtX (where.x);
    draggedSlice = -1;
    sliceMoved = true; // the release that follows is not a tap
    if (hit >= 0)
        removeSlice (hit);
    else if (waveArea().contains (where))
        addSliceAt (xToSampleNorm (where.x));
}

void OscWaveView::mouseDown (const juce::MouseEvent& e)
{
    touch.down (e);
    if (e.source.getIndex() > 0 || touch.twoDown())
    {
        cancelOneFingerGesture(); // the second finger: pinch / pan / swipe take over
        return;
    }
    dragStartPos = e.position;
    dragging = false;
    sliceMoved = false;
    nameSwipe = false;
    activeMarker = SampleMarker::none;
    gestureOpen = true;

    if (isSampleType())
    {
        refreshSliceCache();
        const float tol = static_cast<float> (ctx.theme.touch());
        const auto closeTo = [&] (float norm) { return std::abs (e.position.x - sampleNormToX (norm)) < tol; };
        const bool manual = sliceModeIndex() == 2;

        const int sliceHit = manual ? nearestSliceAtX (e.position.x) : -1;
        if (sliceHit >= 0)
        {
            activeMarker = SampleMarker::slice;
            draggedSlice = sliceHit;
            selectedSlice = sliceHit;
        }
        else if (sliceModeIndex() == 1 && std::abs (e.position.y - thresholdLineY()) < tol * 0.5f)
        {
            activeMarker = SampleMarker::threshold;
            sliceThresholdHandle.beginGesture();
        }
        else if (closeTo (sampleStartHandle.normalised()))
        {
            activeMarker = SampleMarker::start;
            sampleStartHandle.beginGesture();
        }
        else if (closeTo (sampleEndHandle.normalised()))
        {
            activeMarker = SampleMarker::end;
            sampleEndHandle.beginGesture();
        }
        else if (closeTo (sampleLoopStartHandle.normalised()))
        {
            activeMarker = SampleMarker::loopStart;
            sampleLoopStartHandle.beginGesture();
        }
        else if (closeTo (sampleLoopEndHandle.normalised()))
        {
            activeMarker = SampleMarker::loopEnd;
            sampleLoopEndHandle.beginGesture();
        }
        else if (betweenLoopMarkers (xToSampleNorm (e.position.x), sampleLoopStartHandle.normalised(), sampleLoopEndHandle.normalised(), 0.0f))
        {
            // a drag between the two loop markers moves both
            activeMarker = SampleMarker::loopPair;
            loopPairStartNorm = sampleLoopStartHandle.normalised();
            loopPairEndNorm = sampleLoopEndHandle.normalised();
            loopPairDownNorm = xToSampleNorm (e.position.x);
            sampleLoopStartHandle.beginGesture();
            sampleLoopEndHandle.beginGesture();
        }
        press.press (e.position);
        if (activeMarker != SampleMarker::none && activeMarker != SampleMarker::slice && activeMarker != SampleMarker::loopPair)
            press.cancel(); // only the waveform, the loop region and the slice handles have a long-press
    }
    else
    {
        // the name strip on top: a horizontal swipe there steps the wavetable (design 02-04), everywhere else the drag scrubs the frame
        if (e.position.y < static_cast<float> (ctx.theme.touch() + ctx.theme.gap()))
        {
            nameSwipe = true;
            return;
        }
        if (frameHandle.valid())
        {
            frameHandle.beginGesture();
            dragStartNorm = frameHandle.normalised();
        }
    }
}

void OscWaveView::mouseDrag (const juce::MouseEvent& e)
{
    touch.drag (e);
    if (e.source.getIndex() > 0 || touch.twoDown() || ! gestureOpen)
        return;
    dragging = true;
    if (isSampleType())
    {
        press.moved (e.position);
        const float norm = juce::jlimit (0.0f, 1.0f, xToSampleNorm (e.position.x));
        const bool showLoupe = activeMarker != SampleMarker::none;
        switch (activeMarker)
        {
            case SampleMarker::start:     sampleStartHandle.setNormalised (norm); break;
            case SampleMarker::end:       sampleEndHandle.setNormalised (norm); break;
            case SampleMarker::loopStart: sampleLoopStartHandle.setNormalised (norm); break;
            case SampleMarker::loopEnd:   sampleLoopEndHandle.setNormalised (norm); break;
            case SampleMarker::loopPair:
            {
                float s = loopPairStartNorm, en = loopPairEndNorm;
                moveLoopPair (s, en, xToSampleNorm (e.position.x) - loopPairDownNorm);
                sampleLoopStartHandle.setNormalised (s);
                sampleLoopEndHandle.setNormalised (en);
                break;
            }
            case SampleMarker::slice:
                if ((e.position - dragStartPos).getDistanceFromOrigin() > kLongPressSlopPx)
                {
                    sliceMoved = true;
                    moveSlice (sliceCache, draggedSlice, norm);
                    repaint();
                }
                break;
            case SampleMarker::threshold:
            {
                const auto a = waveArea();
                sliceThresholdHandle.setNormalised (lineToThreshold ((a.getBottom() - e.position.y) / juce::jmax (1.0f, a.getHeight())) / 100.0f);
                break;
            }
            case SampleMarker::none: break;
        }
        if (showLoupe)
        {
            if (loupe == nullptr)
                loupe = std::make_unique<Loupe> (ctx.theme);
            loupe->show (*this, e.getPosition());
        }
    }
    else if (! nameSwipe)
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
    touch.up (e);
    if (e.source.getIndex() > 0 || ! gestureOpen)
        return;
    press.cancel();
    gestureOpen = false;
    if (loupe != nullptr)
        loupe->hide();
    if (isSampleType())
    {
        switch (activeMarker)
        {
            case SampleMarker::start:     sampleStartHandle.endGesture(); break;
            case SampleMarker::end:       sampleEndHandle.endGesture(); break;
            case SampleMarker::loopStart: sampleLoopStartHandle.endGesture(); break;
            case SampleMarker::loopEnd:   sampleLoopEndHandle.endGesture(); break;
            case SampleMarker::loopPair:  sampleLoopStartHandle.endGesture(); sampleLoopEndHandle.endGesture(); break;
            case SampleMarker::threshold: sliceThresholdHandle.endGesture(); break;
            case SampleMarker::slice:
            {
                const int moved = draggedSlice;
                draggedSlice = -1;
                if (sliceMoved && moved >= 0)
                    commitSlices (sliceCache);
                else
                    refreshSliceCache();
                break;
            }
            case SampleMarker::none: break;
        }
        activeMarker = SampleMarker::none;
        repaint();
    }
    else
    {
        if (nameSwipe)
        {
            const float dx = e.position.x - dragStartPos.x;
            if (std::abs (dx) >= 48.0f)
                stepWavetable (dx < 0.0f ? +1 : -1); // swipe left = the next table
            nameSwipe = false;
            dragging = false;
            return;
        }
        if (frameHandle.valid())
            frameHandle.endGesture();

        // Direct tap on view cycles mode if not dragged
        if (! dragging && e.getDistanceFromDragStart() < 4)
            cycleViewMode();
    }
    dragging = false;
}

void OscWaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isSampleType())
    {
        // double-tap between the loop markers resets the loop to the sample start / end; elsewhere start and end go back to the full range
        if (betweenLoopMarkers (xToSampleNorm (e.position.x), sampleLoopStartHandle.normalised(), sampleLoopEndHandle.normalised(),
                                kMinSliceGap))
        {
            sampleLoopStartHandle.setPlainComplete (sampleStartHandle.normalised() * 100.0f);
            sampleLoopEndHandle.setPlainComplete (sampleEndHandle.normalised() * 100.0f);
            return;
        }
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
    if (unisonCapped()) // medium / low tier cap hint
    {
        g.setColour (c.warn);
        g.drawText ("TIER CAPPED", headerArea.toNearestInt(), juce::Justification::centredRight, true);
    }
    // the unison count as a pill coloured by the CPU load (green / amber / red), amber outline when the tier caps it
    if (const int level = unisonPillLevel(); level >= 0)
    {
        const auto pill = juce::Rectangle<float> (r.getRight() - t.px (54.0f), r.getBottom() - t.px (24.0f), t.px (48.0f), t.px (18.0f));
        const juce::Colour colours[] = { c.ok, c.warn, c.danger };
        g.setColour (colours[level].withAlpha (0.85f));
        g.fillRoundedRectangle (pill, t.px (9.0f));
        if (unisonCapped())
        {
            g.setColour (c.warn);
            g.drawRoundedRectangle (pill, t.px (9.0f), t.px (2.0f));
        }
        g.setColour (c.textOnAccent);
        g.setFont (t.labelFont());
        g.drawText (juce::String (voices) + "v", pill.toNearestInt(), juce::Justification::centred, false);
    }
    if (isOff)
    {
        g.setColour (c.warn);
        g.drawText ("OFF", headerArea.removeFromRight (t.px (40.0f)).toNearestInt(), juce::Justification::centredRight, true);
    }

    // Main waveform area
    const auto area = waveArea();
    if (isSampleType())
    {
        if (juce::Time::getMillisecondCounter() - sliceCacheMs > 500)
            refreshSliceCache();
        paintSample (g, area);
    }
    else
        paintWavetable (g, area);

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
    const float sX = sampleNormToX (startNorm);
    const float eX = sampleNormToX (endNorm);
    const float lsX = sampleNormToX (loopStartNorm);
    const float leX = sampleNormToX (loopEndNorm);
    const auto inside = [&area] (float x) { return x >= area.getX() - 0.5f && x <= area.getRight() + 0.5f; };

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());

    // Loop region shaded background
    if (leX > lsX)
    {
        g.setColour (c.accent.withAlpha (0.12f));
        g.fillRect (juce::jmax (lsX, area.getX()), area.getY(), juce::jmin (leX, area.getRight()) - juce::jmax (lsX, area.getX()), area.getHeight());
    }

    // Sample waveform (procedural audio wave display) over the visible window
    juce::Path waveP;
    const int points = juce::jmax (2, juce::roundToInt (w));
    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.4f;

    for (int i = 0; i < points; ++i)
    {
        const float normX = static_cast<float> (sampleVp.toRange (static_cast<double> (i) / static_cast<double> (points)));
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

    // Loop markers
    g.setColour (c.mod);
    if (inside (lsX))
        g.drawVerticalLine (juce::roundToInt (lsX), area.getY(), area.getBottom());
    if (inside (leX))
        g.drawVerticalLine (juce::roundToInt (leX), area.getY(), area.getBottom());

    // Slice markers (Manual mode): a line with a handle at the foot, the selected one in the accent colour
    if (sliceModeIndex() == 2)
    {
        for (size_t i = 0; i < sliceCache.size(); ++i)
        {
            const float x = sampleNormToX (sliceCache[i]);
            if (! inside (x))
                continue;
            const bool sel = static_cast<int> (i) == selectedSlice;
            g.setColour (sel ? c.accent : c.warn.withAlpha (0.85f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
            juce::Path tri;
            tri.addTriangle (x - t.px (6.0f), area.getBottom(), x + t.px (6.0f), area.getBottom(), x, area.getBottom() - t.px (11.0f));
            g.fillPath (tri);
        }
    }
    else if (sliceModeIndex() == 1)
    {
        // Auto slices: the threshold line, drag it up for fewer slices
        const float y = thresholdLineY();
        g.setColour (c.warn);
        const float dashes[] = { t.px (6.0f), t.px (4.0f) };
        g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dashes, 2, t.px (1.5f));
        g.setFont (t.labelFont());
        g.drawText ("TH " + juce::String (juce::roundToInt (sliceThresholdHandle.valid() ? sliceThresholdHandle.plain() : 50.0f)) + "%",
                    juce::Rectangle<float> (area.getRight() - t.px (64.0f), y - t.px (16.0f), t.px (62.0f), t.px (14.0f)).toNearestInt(),
                    juce::Justification::centredRight, false);
    }

    // Start / End markers with tabs
    const juce::Rectangle<float> sTab (sX - t.px (6.0f), area.getY(), t.px (12.0f), t.px (14.0f));
    const juce::Rectangle<float> eTab (eX - t.px (6.0f), area.getY(), t.px (12.0f), t.px (14.0f));
    if (inside (sX))
    {
        g.setColour (c.accent);
        g.drawVerticalLine (juce::roundToInt (sX), area.getY(), area.getBottom());
        g.fillRect (sTab);
        g.setColour (c.textOnAccent);
        g.setFont (t.labelFont());
        g.drawText ("S", sTab.toNearestInt(), juce::Justification::centred, false);
    }
    if (inside (eX))
    {
        g.setColour (c.warn);
        g.drawVerticalLine (juce::roundToInt (eX), area.getY(), area.getBottom());
        g.fillRect (eTab);
        g.setColour (c.textOnAccent);
        g.setFont (t.labelFont());
        g.drawText ("E", eTab.toNearestInt(), juce::Justification::centred, false);
    }
    g.restoreState();

    // zoom read-out and the window bar (where the visible part sits in the whole sample)
    if (! sampleVp.isFit())
    {
        g.setColour (c.textDim);
        g.setFont (t.labelFont());
        g.drawText ("x" + juce::String (sampleVp.zoom(), 1), area.toNearestInt().removeFromBottom (juce::roundToInt (t.px (16.0f))),
                    juce::Justification::bottomRight, false);
        g.setColour (c.accent.withAlpha (0.6f));
        g.fillRect (area.getX() + static_cast<float> (sampleVp.start) * w, area.getBottom() - t.px (3.0f),
                    static_cast<float> (sampleVp.span) * w, t.px (3.0f));
    }
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
    for (size_t i = 0; i < panHandles.size(); ++i)
    {
        static const char* const keys[] = { "osc1.pan", "osc2.pan", "osc3.pan", "noise.pan", "filter1.pan", "filter2.pan" };
        panHandles[i] = std::make_unique<ParamHandle> (context, keys[i]);
        panHandles[i]->onChange = repaintCb;
    }
    press.onLongPress = [this]
    {
        nameConsumed = true;
        nameDragging = false;
        stripHeaderLongPressed (activeStrip);
    };
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

juce::Rectangle<float> OscMixConsoleView::stripBounds (int strip) const
{
    // the same geometry as paint (): header, body, master faders on the right
    const auto& t = ctx.theme;
    const auto r = getLocalBounds().toFloat().reduced (t.px (1.0f));
    const float headerH = static_cast<float> (t.touch() + t.gap());
    const auto body = r.withTrimmedTop (headerH).reduced (t.px (6.0f), t.px (4.0f));
    const float masterWidth = static_cast<float> (t.touch() * 2 + t.gap());
    const auto stripsArea = body.withTrimmedRight (masterWidth);
    const float stripW = (stripsArea.getWidth() - static_cast<float> (kNumStrips - 1) * t.px (3.0f)) / static_cast<float> (kNumStrips);
    const float x = stripsArea.getX() + static_cast<float> (strip) * (stripW + t.px (3.0f));
    return { x, stripsArea.getY(), stripW, stripsArea.getHeight() };
}

juce::Rectangle<float> OscMixConsoleView::nameBounds (int strip) const
{
    auto col = stripBounds (strip);
    return col.removeFromBottom (static_cast<float> (ctx.theme.touch()));
}

juce::Rectangle<float> OscMixConsoleView::panBounds (int strip) const
{
    const auto name = nameBounds (strip);
    return { name.getX(), name.getY() - ctx.theme.px (24.0f), name.getWidth(), ctx.theme.px (24.0f) };
}

const char* OscMixConsoleView::stripModulePrefix (int strip) noexcept
{
    static const char* const prefixes[] = { "osc1", "osc2", "osc3", "", "filter1", "filter2", "", "" };
    return strip >= 0 && strip < kNumStrips ? prefixes[strip] : "";
}

bool OscMixConsoleView::dropOnStrip (int from, int to, bool copy, bool withMods)
{
    dropStrip = -1;
    const std::string a = stripModulePrefix (from), b = stripModulePrefix (to);
    if (a.empty() || b.empty() || a == b)
        return false;
    return copy ? module_ops::copyOnto (ctx, a, b, withMods) : module_ops::swap (ctx, a, b, true);
}

void OscMixConsoleView::stripHeaderLongPressed (int strip)
{
    const std::string prefix = stripModulePrefix (strip);
    if (! prefix.empty())
        showModuleSheet (ctx, prefix, *this);
}

int OscMixConsoleView::stripAt (juce::Point<float> p) const
{
    for (int i = 0; i < kNumStrips; ++i)
        if (stripBounds (i).contains (p))
            return i;
    return -1;
}

ParamHandle& OscMixConsoleView::stripLevel (int strip)
{
    ParamHandle* levels[] = { &o1Level, &o2Level, &o3Level, &noiseLevel, &f1Level, &f2Level, &b1Level, &b2Level };
    return *levels[juce::jlimit (0, kNumStrips - 1, strip)];
}

ParamHandle* OscMixConsoleView::stripPower (int strip)
{
    ParamHandle* power[] = { &o1On, &o2On, &o3On, &noiseOn };
    return strip >= 0 && strip < 4 ? power[strip] : nullptr;
}

void OscMixConsoleView::mouseDown (const juce::MouseEvent& e)
{
    dragStartPos = e.position;
    stripNameTap = false;
    activeStrip = stripAt (e.position);
    if (activeStrip >= 0)
    {
        // design 02 section 3: drag a strip = its level, tap its header (here: the name at the foot) = its power
        auto& level = stripLevel (activeStrip);
        activeFader = FaderTarget::strip;
        panDrag = false;
        nameDragging = false;
        nameConsumed = false;
        dropStrip = -1;
        if (nameBounds (activeStrip).contains (e.position))
        {
            // the header: tap = power (O1 O2 O3 NOISE), long-press = module sheet, drag onto another strip = swap / copy
            stripNameTap = true;
            if (stripModulePrefix (activeStrip)[0] != '\0')
                press.press (e.position);
        }
        else if (hasPan (activeStrip) && panBounds (activeStrip).contains (e.position))
        {
            panDrag = true;
            panHandles[static_cast<size_t> (activeStrip)]->beginGesture();
        }
        else if (level.valid())
        {
            level.beginGesture();
            dragStartNorm = level.normalised();
        }
        return;
    }
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

    if (activeFader == FaderTarget::strip)
    {
        if (stripNameTap)
        {
            press.moved (e.position);
            if (! nameConsumed && (e.position - dragStartPos).getDistanceFromOrigin() > ctx.theme.px (10.0f))
            {
                nameDragging = true;
                dropStrip = stripAt (e.position);
                repaint();
            }
            return;
        }
        if (panDrag)
        {
            const auto col = stripBounds (activeStrip);
            panHandles[static_cast<size_t> (activeStrip)]->setNormalised (juce::jlimit (0.0f, 1.0f, (e.position.x - col.getX()) / juce::jmax (1.0f, col.getWidth())));
            return;
        }
        if (stripLevel (activeStrip).valid())
        {
            const float colH = juce::jmax (1.0f, stripBounds (activeStrip).getHeight() * 0.7f);
            stripLevel (activeStrip).setNormalised (juce::jlimit (0.0f, 1.0f, dragStartNorm - (e.position.y - dragStartPos.y) / colH));
        }
        return;
    }

    const float h = static_cast<float> (juce::jmax (1, getHeight()));
    const float dy = e.position.y - dragStartPos.y;
    const float newNorm = juce::jlimit (0.0f, 1.0f, dragStartNorm - dy / h);

    if (activeFader == FaderTarget::main && masterVolumeHandle.valid())
        masterVolumeHandle.setNormalised (newNorm);
    else if (activeFader == FaderTarget::direct && directLevelHandle.valid())
        directLevelHandle.setNormalised (newNorm);
}

void OscMixConsoleView::mouseUp (const juce::MouseEvent& e)
{
    press.cancel();
    if (activeFader == FaderTarget::strip)
    {
        if (stripNameTap)
        {
            if (nameDragging)
                dropOnStrip (activeStrip, stripAt (e.position), e.mods.isAltDown(), e.mods.isShiftDown());
            else if (! nameConsumed)
                if (auto* power = stripPower (activeStrip); power != nullptr && power->valid() && e.getDistanceFromDragStart() < 6)
                    power->setPlainComplete (power->plain() >= 0.5f ? 0.0f : 1.0f);
        }
        else if (panDrag)
        {
            panHandles[static_cast<size_t> (activeStrip)]->endGesture();
        }
        else if (stripLevel (activeStrip).valid())
        {
            stripLevel (activeStrip).endGesture();
        }
        nameDragging = false;
        nameConsumed = false;
        panDrag = false;
        dropStrip = -1;
        repaint();
    }
    else if (activeFader == FaderTarget::main && masterVolumeHandle.valid())
        masterVolumeHandle.endGesture();
    else if (activeFader == FaderTarget::direct && directLevelHandle.valid())
        directLevelHandle.endGesture();
    activeFader = FaderTarget::none;
    activeStrip = -1;
    stripNameTap = false;
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

        // Pan band: a thin track over the foot of the level bar, the dot is the pan
        if (hasPan (i))
        {
            const auto pb = panBounds (i).reduced (t.px (6.0f), 0.0f);
            const float panNorm = panHandles[static_cast<size_t> (i)]->valid() ? panHandles[static_cast<size_t> (i)]->normalised() : 0.5f;
            g.setColour (c.outline);
            g.drawHorizontalLine (juce::roundToInt (pb.getCentreY()), pb.getX(), pb.getRight());
            g.setColour (c.mod);
            const float dx = pb.getX() + panNorm * pb.getWidth();
            g.fillEllipse (dx - t.px (4.0f), pb.getCentreY() - t.px (4.0f), t.px (8.0f), t.px (8.0f));
        }

        // Name (the header of the strip); the drop target of a header drag is outlined
        if (dropStrip == i && activeStrip != i)
        {
            g.setColour (c.mod);
            g.drawRoundedRectangle (stripBounds (i).reduced (t.px (1.0f)), t.px (4.0f), t.px (3.0f));
        }
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
