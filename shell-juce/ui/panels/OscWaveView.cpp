// SynthyBoy - UI: oscillator, noise, and mixer visualizers implementation (docs/ui-design/02-oscillators.md)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OscWaveView.h"

#include "synthyboy/granular/granular.h"

#include "WavetableEditorSheet.h"

#include <cmath>

#include "../ModuleSheet.h"

namespace synthyboy::ui
{
// Helper: evaluate sample point in [0, 1] for a given wavetable choice and frame; the warp is the picture of its mode (warpedValue)
static float evalWave (float phase, int wtChoice, float frame, float warpAmount, int warpType, const std::function<float (float)>& remapCurve = {})
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
    }, remapCurve);
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
      specFreqLoHandle (context, "osc" + std::to_string (oscIndex) + ".spec.freq_lo"),
      specFreqHiHandle (context, "osc" + std::to_string (oscIndex) + ".spec.freq_hi"),
      specManualHandle (context, "osc" + std::to_string (oscIndex) + ".spec.manual"),
      specPositionHandle (context, "osc" + std::to_string (oscIndex) + ".spec.position"),
      specXyYHandle (context, "osc" + std::to_string (oscIndex) + ".spec.xy_y"),
      granPositionHandle (context, "osc" + std::to_string (oscIndex) + ".gran.position"),
      granXyYHandle (context, "osc" + std::to_string (oscIndex) + ".gran.xy_y"),
      granManualHandle (context, "osc" + std::to_string (oscIndex) + ".gran.manual"),
      granWindowAmountHandle (context, "osc" + std::to_string (oscIndex) + ".gran.window_amount"),
      granWindowSkewHandle (context, "osc" + std::to_string (oscIndex) + ".gran.window_skew"),
      granWindowShapeHandle (context, "osc" + std::to_string (oscIndex) + ".gran.window_shape"),
      granDensityHandle (context, "osc" + std::to_string (oscIndex) + ".gran.density"),
      granLengthHandle (context, "osc" + std::to_string (oscIndex) + ".gran.length"),
      granRandOffsetHandle (context, "osc" + std::to_string (oscIndex) + ".gran.rand_offset"),
      multiOverrideHandle (context, "osc" + std::to_string (oscIndex) + ".multi.override"),
      sampleStartHandle (context, "osc" + std::to_string (oscIndex) + ".sample.start"),
      sampleEndHandle (context, "osc" + std::to_string (oscIndex) + ".sample.end"),
      sampleLoopStartHandle (context, "osc" + std::to_string (oscIndex) + ".sample.loop_start"),
      sampleLoopEndHandle (context, "osc" + std::to_string (oscIndex) + ".sample.loop_end"),
      sampleLengthBeatsHandle (context, "osc" + std::to_string (oscIndex) + ".sample.length_beats"),
      sampleUniStartHandle (context, "osc" + std::to_string (oscIndex) + ".sample.uni_start"),
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
    multiOverrideHandle.onChange = repaintCb;
    for (ParamHandle* handle : { &specFreqLoHandle, &specFreqHiHandle, &specManualHandle, &specPositionHandle, &specXyYHandle, &granPositionHandle, &granXyYHandle, &granManualHandle, &granWindowAmountHandle, &granWindowSkewHandle, &granWindowShapeHandle,
                                 &granDensityHandle, &granLengthHandle, &granRandOffsetHandle })
        handle->onChange = repaintCb;
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
    loadSampleBtn.onClick = [this] { requestSampleFile(); };
    loadSampleBtn.setTooltip ("Load a sample file (WAV, AIFF, FLAC, ...) into this oscillator");
    snapBtn.onClick = [this] { setSnapMode (static_cast<SnapMode> ((static_cast<int> (snap) + 1) % 4)); };
    snapBtn.setTooltip ("Snap the markers: Off / Zero crossing / Beats / Loop points");
    sampleMenuBtn.onClick = [this] { showSampleMenu(); };
    waveMenuBtn.onClick = [this] { showWaveMenu(); };
    waveMenuBtn.setTooltip ("Render OSC Warp, Resample, wavetable frame <-> LFO shape");
    editBtn.onClick = [this] { openEditor(); };
    editBtn.setTooltip ("Edit the wavetable of this oscillator");
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
    addAndMakeVisible (loadSampleBtn);
    addAndMakeVisible (snapBtn);
    addAndMakeVisible (sampleMenuBtn);
    addAndMakeVisible (editBtn);
    addAndMakeVisible (waveMenuBtn);
    refreshSliceCache();
    // WP-17: the live marker follows the engine while the marker animation shows and the unison start is modulated
    ctx.live->bind (*this,
                    [this] { return markerAnimation && sampleUniStartHandle.valid() && ! ctx.assign.snapshot().entriesFor (sampleUniStartHandle.index()).empty()
                                        ? std::vector<int> { sampleUniStartHandle.index() } : std::vector<int> {}; },
                    [this] { repaint(); });
    setViewMode (ctx.host.preferences().waveform3d ? ViewMode::wave3D : ViewMode::wave2D); // "Default waveform view" (S p.314)
}

OscWaveView::~OscWaveView()
{
    stopTimer();
}

int OscWaveView::unisonPillLevel() const
{
    const int voices = unisonVoicesHandle.valid() ? juce::roundToInt (unisonVoicesHandle.normalised() * static_cast<float> (unisonVoicesHandle.info().max - unisonVoicesHandle.info().min) + static_cast<float> (unisonVoicesHandle.info().min)) : 1;
    return voices > 1 ? cpuLevel (ctx.host.cpuLoad()) : -1;
}

bool OscWaveView::unisonCapped() const
{
    return unisonVoicesHandle.valid() && juce::roundToInt (unisonVoicesHandle.plain()) > 8;
}

int OscWaveView::oscType() const
{
    // (read from the normalised value: the listener runs before the atomic updates)
    return typeHandle.valid() ? juce::roundToInt (typeHandle.info().min + typeHandle.normalised() * (typeHandle.info().max - typeHandle.info().min)) : 0;
}

bool OscWaveView::isSampleType() const
{
    const int type = oscType();
    return type == kOscTypeSample || type == kOscTypeGranular || type == kOscTypeSpectral;
}

juce::Rectangle<float> OscWaveView::windowThumbArea() const
{
    const auto area = waveArea();
    const float w = static_cast<float> (ctx.theme.touch()) * 1.7f, h = static_cast<float> (ctx.theme.touch()) * 1.1f;
    return { area.getRight() - w - ctx.theme.px (4.0f), area.getY() + ctx.theme.px (4.0f), w, h };
}

juce::Point<float> OscWaveView::xyDotPosition() const
{
    const auto area = waveArea();
    const float start = sampleStartHandle.valid() ? sampleStartHandle.normalised() : 0.0f, end = sampleEndHandle.valid() ? sampleEndHandle.normalised() : 1.0f;
    const ParamHandle& positionHandle = isSpectral() ? specPositionHandle : granPositionHandle;
    const ParamHandle& yHandle = isSpectral() ? specXyYHandle : granXyYHandle;
    const float position = positionHandle.valid() ? positionHandle.normalised() : 0.0f;
    const float y = yHandle.valid() ? yHandle.normalised() : 0.5f;
    return { sampleNormToX (start + (end - start) * position), area.getBottom() - y * area.getHeight() };
}

bool OscWaveView::xyManualOn() const
{
    if (isGranular())
        return granManualHandle.valid() && granManualHandle.plain() > 0.5f;
    if (isSpectral())
        return specManualHandle.valid() && specManualHandle.plain() > 0.5f;
    return false;
}

juce::Rectangle<float> OscWaveView::freqStripArea() const
{
    const auto area = waveArea();
    const float w = static_cast<float> (ctx.theme.touch());
    return { area.getRight() - w, area.getY(), w, area.getHeight() };
}

float OscWaveView::freqToY (float hz) const
{
    const auto strip = freqStripArea();
    const float norm = juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (20.0f, hz) / 20.0f) / std::log (1000.0f));
    return strip.getBottom() - norm * strip.getHeight();
}

float OscWaveView::yToFreq (float y) const
{
    const auto strip = freqStripArea();
    const float norm = juce::jlimit (0.0f, 1.0f, (strip.getBottom() - y) / juce::jmax (1.0f, strip.getHeight()));
    return 20.0f * std::pow (1000.0f, norm);
}

juce::Point<float> OscWaveView::freqMarkerPosition (bool high) const
{
    const ParamHandle& handle = high ? specFreqHiHandle : specFreqLoHandle;
    return { freqStripArea().getCentreX(), freqToY (handle.valid() ? handle.plain() : (high ? 20000.0f : 20.0f)) };
}

std::vector<OscWaveView::GrainMark> OscWaveView::grainMarks() const
{
    std::vector<GrainMark> marks;
    if (! granDensityHandle.valid() || ! granLengthHandle.valid())
        return marks;
    const float density = granDensityHandle.plain(), lengthSec = granLengthHandle.plain() * 0.001f;
    const int count = juce::jlimit (1, 24, juce::roundToInt (density * lengthSec));
    const float start = sampleStartHandle.valid() ? sampleStartHandle.normalised() : 0.0f, end = sampleEndHandle.valid() ? sampleEndHandle.normalised() : 1.0f;
    const bool manual = granManualHandle.valid() && granManualHandle.plain() > 0.5f;
    const float position = manual && granPositionHandle.valid() ? granPositionHandle.normalised() : 0.0f;
    const float spread = granRandOffsetHandle.valid() ? granRandOffsetHandle.normalised() : 0.0f;
    const auto prev = preview();
    const float seconds = prev != nullptr ? static_cast<float> (prev->seconds()) : 1.0f;
    uint32_t state = 0x9e3779b9u;
    const auto next = [&state] { state = state * 1664525u + 1013904223u; return static_cast<float> (state >> 8) * (1.0f / 16777216.0f); };
    for (int i = 0; i < count; ++i)
    {
        const float offset = (next() * 2.0f - 1.0f) * spread * 0.5f;
        const float here = juce::jlimit (0.0f, 1.0f, start + (end - start) * juce::jlimit (0.0f, 1.0f, position + offset + 0.02f * static_cast<float> (i) / static_cast<float> (count)));
        marks.push_back ({ here, juce::jlimit (0.002f, 0.5f, lengthSec / juce::jmax (0.01f, seconds)), 0.25f + 0.7f * next() });
    }
    return marks;
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
    prevWtButton.setVisible (! isSample && ! isMultisample());
    nextWtButton.setVisible (! isSample && ! isMultisample());
    viewModeButton.setVisible (! isSample && ! isMultisample());
    const bool multi = isMultisample();
    loadSampleBtn.setVisible (isSample || multi);
    loadSampleBtn.setButtonText (multi ? "Load SFZ" : "Load");
    snapBtn.setVisible (isSample);
    sampleMenuBtn.setVisible (isSample || multi);
    editBtn.setVisible (! isSample && ! multi);
    waveMenuBtn.setVisible (! isSample && ! multi);
    sampleMenuBtn.setEnabled (multi ? ! ctx.host.content().instruments.empty() : preview() != nullptr);
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

    // the file tools on the second row of the sample view: Load, Snap, the sample menu; the wavetable's Edit button sits behind the view mode button
    const int y2 = y + touchH + gap;
    int x2 = t.gap();
    loadSampleBtn.setBounds (x2, y2, juce::jmax (btnW, t.ipx (64.0f)), touchH);
    x2 += juce::jmax (btnW, t.ipx (64.0f)) + gap;
    snapBtn.setBounds (x2, y2, juce::jmax (btnW, t.ipx (96.0f)), touchH);
    x2 += juce::jmax (btnW, t.ipx (96.0f)) + gap;
    sampleMenuBtn.setBounds (x2, y2, btnW, touchH);
    waveMenuBtn.setBounds (t.gap(), y2, btnW, touchH);
    editBtn.setBounds (nextWtButton.getBounds().getX() + juce::jmax (btnW, t.ipx (50.0f)) + btnW + 2 * gap, y, juce::jmax (btnW, t.ipx (56.0f)), touchH);
}

// ---- content pipeline: sample file, snap, auto slices, marker animation -----------------------------------------------------------------

std::shared_ptr<const SamplePreview> OscWaveView::preview() const
{
    const auto content = ctx.host.content();
    const ContentSample* sample = sampleOf (content, index);
    return sample != nullptr ? contentCache (ctx).samplePreview (ctx.host, sample->ref) : nullptr;
}

std::shared_ptr<const wtedit::Wavetable> OscWaveView::userTable() const
{
    if (! wtHandle.valid() || juce::roundToInt (wtHandle.plain()) != choiceIndexOf (("osc" + std::to_string (index) + ".wavetable").c_str(), "Preset"))
        return nullptr;
    const auto content = ctx.host.content();
    const ContentWavetable* w = wavetableOf (content, index);
    return w != nullptr ? contentCache (ctx).table (ctx.host, w->ref) : nullptr;
}

FlowResult OscWaveView::loadSampleFile (const juce::File& file)
{
    const FlowResult result = synthyboy::ui::loadSampleFile (ctx, index, file);
    statusLine = result.message;
    sampleVp.fit();
    refreshSliceCache();
    updateButtons();
    repaint();
    return result;
}

void OscWaveView::requestSampleFile()
{
    if (isMultisample())
    {
        FileRequest sfzRequest;
        sfzRequest.title = "Load a multisample instrument (SFZ) into oscillator " + juce::String (index);
        sfzRequest.wildcards = "*.sfz";
        sfzRequest.purpose = "instrument";
        if (! ctx.host.requestFile (sfzRequest, [safe = juce::Component::SafePointer<OscWaveView> (this)] (const juce::File& file)
            {
                if (safe != nullptr)
                    safe->loadInstrument (file);
            }))
        {
            statusLine = "This host cannot ask for a file";
            repaint();
        }
        return;
    }
    FileRequest request;
    request.title = "Load a sample into oscillator " + juce::String (index);
    request.wildcards = audioFileWildcard();
    request.purpose = "sample";
    if (! ctx.host.requestFile (request, [safe = juce::Component::SafePointer<OscWaveView> (this)] (const juce::File& file)
        {
            if (safe != nullptr)
                safe->loadSampleFile (file);
        }))
    {
        statusLine = "This host cannot ask for a file";
        repaint();
    }
}

FlowResult OscWaveView::trimSample()
{
    const FlowResult result = synthyboy::ui::trimSample (ctx, index);
    statusLine = result.message;
    refreshSliceCache();
    repaint();
    return result;
}

FlowResult OscWaveView::removeSample()
{
    const FlowResult result = synthyboy::ui::removeSample (ctx, index);
    statusLine = result.message;
    refreshSliceCache();
    updateButtons();
    repaint();
    return result;
}

FlowResult OscWaveView::reloadSample()
{
    contentCache (ctx).clear();
    const bool ok = preview() != nullptr;
    statusLine = ok ? "Reloaded from the library" : "The sample file is missing";
    repaint();
    return { ok, statusLine };
}

FlowResult OscWaveView::loadInstrument (const juce::File& sfzFile)
{
    const FlowResult result = synthyboy::ui::loadInstrumentFile (ctx, index, sfzFile);
    statusLine = result.message;
    zoneCacheValid = false;
    updateButtons();
    repaint();
    return result;
}

FlowResult OscWaveView::switchToSample()
{
    const FlowResult result = synthyboy::ui::switchToSingleSample (ctx, index);
    statusLine = result.message;
    zoneCacheValid = false;
    updateButtons();
    repaint();
    return result;
}

int OscWaveView::lastPlayedZone() const
{
    LiveValues live;
    return index >= 1 && index <= 3 && ctx.host.copyReadout (live) ? live.multisampleZone[index - 1] : -1;
}

const std::vector<multisample::Region>& OscWaveView::instrumentZones()
{
    const auto content = ctx.host.content();
    const ContentInstrument* instrument = nullptr;
    for (const auto& i : content.instruments)
        if (i.osc == index)
            instrument = &i;
    if (instrument == nullptr)
    {
        zoneCache.clear();
        zoneCacheValid = false;
        return zoneCache;
    }
    if (zoneCacheValid && zoneCacheRef.tie() == instrument->ref.tie())
        return zoneCache;
    zoneCache.clear();
    std::vector<uint8_t> bytes;
    if (auto* library = ctx.host.contentLibrary())
        if (library->readFile (instrument->ref, bytes))
            zoneCache = multisample::parseSfz (std::string (bytes.begin(), bytes.end())).regions;
    zoneCacheRef = instrument->ref;
    zoneCacheValid = true;
    return zoneCache;
}

juce::Rectangle<float> OscWaveView::zoneMapArea() const
{
    return waveArea();
}

juce::Rectangle<float> OscWaveView::zoneRect (const multisample::Region& region) const
{
    const auto map = zoneMapArea();
    const float x0 = map.getX() + map.getWidth() * static_cast<float> (region.lokey) / 128.0f, x1 = map.getX() + map.getWidth() * static_cast<float> (region.hikey + 1) / 128.0f;
    const float y1 = map.getBottom() - map.getHeight() * static_cast<float> (region.lovel) / 128.0f, y0 = map.getBottom() - map.getHeight() * static_cast<float> (region.hivel + 1) / 128.0f;
    return { x0, y0, x1 - x0, y1 - y0 };
}

juce::Rectangle<float> OscWaveView::envThumbArea() const
{
    const auto area = waveArea();
    const float w = static_cast<float> (ctx.theme.touch()) * 1.9f, h = static_cast<float> (ctx.theme.touch()) * 1.1f;
    return { area.getRight() - w - ctx.theme.px (4.0f), area.getBottom() - h - ctx.theme.px (4.0f), w, h };
}

void OscWaveView::showSampleMenu()
{
    if (isMultisample())
    {
        juce::PopupMenu instrumentMenu;
        instrumentMenu.addItem (1, "Switch to Single Sample", ! ctx.host.content().instruments.empty());
        instrumentMenu.addItem (2, "Load SFZ...", true);
        instrumentMenu.showMenuAsync (popupOptions (&sampleMenuBtn), [safe = juce::Component::SafePointer<OscWaveView> (this)] (int result)
        {
            if (safe == nullptr)
                return;
            if (result == 1) safe->switchToSample();
            else if (result == 2) safe->requestSampleFile();
        });
        return;
    }
    juce::PopupMenu menu;
    const auto content = ctx.host.content();
    const ContentSample* sample = sampleOf (content, index);
    menu.addItem (1, "Reload", sample != nullptr);
    menu.addItem (2, "Show location", sample != nullptr);
    menu.addItem (3, "Trim to START / END", sample != nullptr);
    menu.addItem (4, "Remove the sample", sample != nullptr);
    menu.addSeparator();
    // Switch to Wavetable (manual p.68): the import options of the wavetable editor
    juce::PopupMenu toWavetable;
    const auto& importChoices = convert::importChoices();
    for (size_t i = 0; i < importChoices.size(); ++i)
        toWavetable.addItem (100 + static_cast<int> (i), importChoices[i].label, sample != nullptr);
    menu.addSubMenu ("Switch to Wavetable", toWavetable, sample != nullptr);
    menu.addSeparator();
    menu.addItem (5, "Show marker animation", true, markerAnimation);
    menu.showMenuAsync (popupOptions (&sampleMenuBtn), [safe = juce::Component::SafePointer<OscWaveView> (this)] (int result)
    {
        if (safe == nullptr)
            return;
        switch (result)
        {
            case 1: safe->reloadSample(); break;
            case 2:
            {
                const auto content2 = safe->ctx.host.content();
                if (const auto* s = sampleOf (content2, safe->index))
                    safe->statusLine = juce::String (s->ref.pack) + "/" + s->ref.file;
                safe->repaint();
                break;
            }
            case 3: safe->trimSample(); break;
            case 4: safe->removeSample(); break;
            case 5: safe->setMarkerAnimation (! safe->markerAnimation); break;
            default:
                if (result >= 100 && result < 100 + static_cast<int> (convert::importChoices().size()))
                {
                    wtedit::ImportOptions options;
                    options.mode = convert::importChoices()[static_cast<size_t> (result - 100)].mode;
                    safe->switchToWavetable (options);
                }
                break;
        }
    });
}

FlowResult OscWaveView::switchToWavetable (const wtedit::ImportOptions& options)
{
    const FlowResult result = synthyboy::ui::switchToWavetable (ctx, index, options);
    statusLine = result.message;
    refreshSliceCache();
    updateButtons();
    repaint();
    return result;
}

FlowResult OscWaveView::renderWarp()
{
    const FlowResult result = synthyboy::ui::renderOscWarp (ctx, index);
    statusLine = result.message;
    updateButtons();
    repaint();
    return result;
}

FlowResult OscWaveView::resample (bool withNext)
{
    const FlowResult result = synthyboy::ui::resampleToWavetable (ctx, withNext ? 0 : index);
    statusLine = result.message;
    updateButtons();
    repaint();
    return result;
}

FlowResult OscWaveView::copyFrameToLfo (int lfo)
{
    const FlowResult result = synthyboy::ui::copyWavetableFrameToLfo (ctx, index, lfo);
    statusLine = result.message;
    repaint();
    return result;
}

FlowResult OscWaveView::copyLfoShapeHere (int lfo)
{
    const FlowResult result = synthyboy::ui::copyLfoShapeToWavetable (ctx, lfo, index);
    statusLine = result.message;
    updateButtons();
    repaint();
    return result;
}

void OscWaveView::showWaveMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Render OSC Warp (256 frames)", ctx.host.contentLibrary() != nullptr && oscType() == kOscTypeWavetable);
    juce::PopupMenu resampleMenu;
    resampleMenu.addItem (2, "Osc " + juce::String (index), ctx.host.contentLibrary() != nullptr);
    if (index == 1)
        resampleMenu.addItem (3, "Osc A + Osc B", ctx.host.contentLibrary() != nullptr);
    menu.addSubMenu ("Resample (one bar of the last note) to", resampleMenu, ctx.host.contentLibrary() != nullptr);
    menu.addSeparator();
    juce::PopupMenu toLfo, fromLfo;
    for (int lfo = 1; lfo <= kMaxContentLfoShapes; ++lfo)
    {
        toLfo.addItem (100 + lfo, "LFO " + juce::String (lfo));
        fromLfo.addItem (200 + lfo, "LFO " + juce::String (lfo), ctx.host.contentLibrary() != nullptr);
    }
    menu.addSubMenu ("Copy this frame to LFO", toLfo);
    menu.addSubMenu ("Copy LFO shape to this wavetable", fromLfo, ctx.host.contentLibrary() != nullptr);
    menu.showMenuAsync (popupOptions (&waveMenuBtn), [safe = juce::Component::SafePointer<OscWaveView> (this)] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;
        if (result == 1) safe->renderWarp();
        else if (result == 2) safe->resample (false);
        else if (result == 3) safe->resample (true);
        else if (result > 100 && result <= 100 + kMaxContentLfoShapes) safe->copyFrameToLfo (result - 100);
        else if (result > 200 && result <= 200 + kMaxContentLfoShapes) safe->copyLfoShapeHere (result - 200);
    });
}

void OscWaveView::setSnapMode (SnapMode newMode)
{
    snap = newMode;
    static const char* const names[] = { "Snap: Off", "Snap: Zero", "Snap: Beats", "Snap: Loop" };
    snapBtn.setButtonText (names[static_cast<int> (newMode)]);
    repaint();
}

float OscWaveView::snappedMarker (float raw, SampleMarker marker) const
{
    if (snap == SnapMode::off || marker == SampleMarker::none || marker == SampleMarker::threshold)
        return raw;
    const float start = sampleStartHandle.valid() ? sampleStartHandle.normalised() : 0.0f;
    const float end = sampleEndHandle.valid() ? sampleEndHandle.normalised() : 1.0f;
    float beatGrid = 0.0f;
    if (sampleLengthBeatsHandle.valid() && sampleLengthBeatsHandle.plain() > 0.01f)
        beatGrid = (end - start) / sampleLengthBeatsHandle.plain();
    // the loop markers snap to the START / END markers, every other marker to the loop markers
    const bool loopMarker = marker == SampleMarker::loopStart || marker == SampleMarker::loopEnd;
    const float a = loopMarker ? start : (sampleLoopStartHandle.valid() ? sampleLoopStartHandle.normalised() : 0.0f);
    const float b = loopMarker ? end : (sampleLoopEndHandle.valid() ? sampleLoopEndHandle.normalised() : 1.0f);
    const auto prev = preview();
    return snapMarker (snap, raw, prev.get(), beatGrid, a, b, 0.02f);
}

std::vector<float> OscWaveView::autoSliceMarkers() const
{
    const auto prev = preview();
    if (prev == nullptr)
        return {};
    return prev->autoSlices((sliceThresholdHandle.valid() ? sliceThresholdHandle.plain() : 50.0f) * 0.01f);
}

void OscWaveView::setMarkerAnimation (bool on)
{
    markerAnimation = on;
    if (on)
        startTimerHz (20);
    else
        stopTimer();
    repaint();
}

std::vector<std::pair<float, float>> OscWaveView::modulationBands() const
{
    std::vector<std::pair<float, float>> bands;
    if (! sampleUniStartHandle.valid())
        return bands;
    const float base = sampleUniStartHandle.normalised();
    for (const auto& entry : ctx.assign.snapshot().entriesFor (sampleUniStartHandle.index()))
    {
        if (entry.bypass)
            continue;
        const float depth = std::abs (entry.amount) * 0.01f;
        bands.emplace_back (juce::jlimit (0.0f, 1.0f, entry.bipolar ? base - depth : base), juce::jlimit (0.0f, 1.0f, base + depth));
    }
    return bands;
}

float OscWaveView::liveUniStart() const
{
    if (! markerAnimation || ! sampleUniStartHandle.valid() || ! ctx.live->hasLive (sampleUniStartHandle.index()))
        return -1.0f;
    return toNormalised (sampleUniStartHandle.info(), ctx.live->liveOf (sampleUniStartHandle.index(), sampleUniStartHandle.plain()));
}

void OscWaveView::openEditor()
{
    WavetableEditorSheet::open (ctx, index, *this, editorSheet, wtedit::Wavetable ("Default"),
                                [safe = juce::Component::SafePointer<OscWaveView> (this)] (const wtedit::Wavetable&) { if (safe != nullptr) safe->repaint(); },
                                [safe = juce::Component::SafePointer<OscWaveView> (this)] { if (safe != nullptr) safe->repaint(); });
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
            case SampleMarker::xy:        xyPositionHandle().endGesture(); xyYHandle().endGesture(); break;
            case SampleMarker::freqLo:    specFreqLoHandle.endGesture(); break;
            case SampleMarker::freqHi:    specFreqHiHandle.endGesture(); break;
            case SampleMarker::window:    granWindowAmountHandle.endGesture(); granWindowSkewHandle.endGesture(); break;
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
    if (isMultisample())
    {
        // the multisample display has one control: the envelope thumbnail switches OVERRIDE
        gestureOpen = false;
        if (envThumbArea().contains (e.position) && multiOverrideHandle.valid())
            multiOverrideHandle.setPlainComplete (multiOverrideHandle.plain() > 0.5f ? 0.0f : 1.0f);
        return;
    }

    if (isSampleType())
    {
        refreshSliceCache();
        const float tol = static_cast<float> (ctx.theme.touch());
        const auto closeTo = [&] (float norm) { return std::abs (e.position.x - sampleNormToX (norm)) < tol; };
        const bool manual = sliceModeIndex() == 2;

        const int sliceHit = manual ? nearestSliceAtX (e.position.x) : -1;
        const bool granularManual = xyManualOn();
        windowTap = false;
        const float distanceHigh = std::abs (e.position.y - freqMarkerPosition (true).y), distanceLow = std::abs (e.position.y - freqMarkerPosition (false).y);
        if (isSpectral() && freqStripArea().contains (e.position) && std::min (distanceHigh, distanceLow) < static_cast<float> (ctx.theme.touch()))
        {
            // the nearer of the two markers on the frequency strip
            const bool high = distanceHigh <= distanceLow;
            activeMarker = high ? SampleMarker::freqHi : SampleMarker::freqLo;
            (high ? specFreqHiHandle : specFreqLoHandle).beginGesture();
        }
        else if (isGranular() && windowThumbArea().contains (e.position))
        {
            // the window thumbnail: a tap steps the shape, a drag sets amount (up) and skew (sideways)
            activeMarker = SampleMarker::window;
            windowTap = true;
            granWindowAmountHandle.beginGesture();
            granWindowSkewHandle.beginGesture();
        }
        else if (sliceHit >= 0)
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
        else if (! granularManual && betweenLoopMarkers (xToSampleNorm (e.position.x), sampleLoopStartHandle.normalised(), sampleLoopEndHandle.normalised(), 0.0f))
        {
            // a drag between the two loop markers moves both (not in the manual granular mode: there the finger places the X|Y dot)
            activeMarker = SampleMarker::loopPair;
            loopPairStartNorm = sampleLoopStartHandle.normalised();
            loopPairEndNorm = sampleLoopEndHandle.normalised();
            loopPairDownNorm = xToSampleNorm (e.position.x);
            sampleLoopStartHandle.beginGesture();
            sampleLoopEndHandle.beginGesture();
        }
        if (activeMarker == SampleMarker::none && granularManual && waveArea().contains (e.position))
        {
            // manual mode: the dot follows the finger (position in the region, Y position)
            activeMarker = SampleMarker::xy;
            xyPositionHandle().beginGesture();
            xyYHandle().beginGesture();
            mouseDrag (e);
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
        const float norm = snappedMarker (juce::jlimit (0.0f, 1.0f, xToSampleNorm (e.position.x)), activeMarker);
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
            case SampleMarker::xy:
            {
                const auto a = waveArea();
                const float start = sampleStartHandle.normalised(), end = sampleEndHandle.normalised();
                const float p = end > start ? (xToSampleNorm (e.position.x) - start) / (end - start) : 0.0f;
                xyPositionHandle().setNormalised (juce::jlimit (0.0f, 1.0f, p));
                xyYHandle().setNormalised (juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / juce::jmax (1.0f, a.getHeight())));
                break;
            }
            case SampleMarker::freqLo:
            case SampleMarker::freqHi:
            {
                ParamHandle& handle = activeMarker == SampleMarker::freqHi ? specFreqHiHandle : specFreqLoHandle;
                const float other = activeMarker == SampleMarker::freqHi ? specFreqLoHandle.plain() : specFreqHiHandle.plain();
                const float hz = yToFreq (e.position.y);
                handle.setPlainComplete (activeMarker == SampleMarker::freqHi ? juce::jmax (hz, other) : juce::jmin (hz, other));
                break;
            }
            case SampleMarker::window:
            {
                const auto thumb = windowThumbArea();
                if ((e.position - dragStartPos).getDistanceFromOrigin() > kLongPressSlopPx)
                    windowTap = false;
                if (! windowTap)
                {
                    granWindowAmountHandle.setNormalised (juce::jlimit (0.0f, 1.0f, (thumb.getBottom() - e.position.y) / juce::jmax (1.0f, thumb.getHeight())));
                    granWindowSkewHandle.setNormalised (juce::jlimit (0.0f, 1.0f, (e.position.x - thumb.getX()) / juce::jmax (1.0f, thumb.getWidth())));
                }
                break;
            }
            case SampleMarker::none: break;
        }
        if (showLoupe && activeMarker != SampleMarker::xy && activeMarker != SampleMarker::window)
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
            case SampleMarker::xy:        xyPositionHandle().endGesture(); xyYHandle().endGesture(); break;
            case SampleMarker::freqLo:    specFreqLoHandle.endGesture(); break;
            case SampleMarker::freqHi:    specFreqHiHandle.endGesture(); break;
            case SampleMarker::window:
                granWindowAmountHandle.endGesture();
                granWindowSkewHandle.endGesture();
                if (windowTap && granWindowShapeHandle.valid())
                {
                    const int count = static_cast<int> (granWindowShapeHandle.info().max) + 1;
                    granWindowShapeHandle.setPlainComplete (static_cast<float> ((juce::roundToInt (granWindowShapeHandle.plain()) + 1) % count));
                }
                windowTap = false;
                break;
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
        const auto content = ctx.host.content();
        const ContentSample* sample = sampleOf (content, index);
        const char* const kKinds[] = { "", "", "Granular ", "Spectral ", "" };
        if (sample == nullptr)
            wtName = "Osc " + juce::String (index) + ": " + kKinds[oscType()] + "no sample (Load)";
        else
        {
            wtName = "Osc " + juce::String (index) + ": " + kKinds[oscType()] + juce::String (sample->ref.name);
            if (const auto prev = preview())
                wtName << "  " << juce::String (prev->seconds(), 2) << " s";
        }
    }
    else if (isMultisample())
    {
        const auto content = ctx.host.content();
        wtName = "Osc " + juce::String (index) + ": Multisample - ";
        const ContentInstrument* instrument = nullptr;
        for (const auto& i : content.instruments)
            if (i.osc == index)
                instrument = &i;
        wtName << (instrument == nullptr ? juce::String ("no instrument (Load SFZ)") : juce::String (instrument->ref.name) + ", " + juce::String (instrument->zones) + " zones");
    }
    else
    {
        const int wtIdx = wtHandle.valid() ? juce::roundToInt (wtHandle.plain()) : 0;
        const char* const kBuiltins[] = { "Basic Shapes", "Saw to Square", "Sine to Triangle", "Harmonics",
                                          "Organ", "Voice Formants", "Digital Bell", "Metal Resonance",
                                          "Bit Crush", "PWM Sweep", "Formant Scale", "Sub Harmonics", "Noise Pulse", "Preset Table" };
        juce::String name = (wtIdx >= 0 && wtIdx < 14) ? kBuiltins[wtIdx] : ("Table " + juce::String (wtIdx));
        if (const auto table = userTable())
            name = juce::String (table->name()) + " (" + juce::String (table->numFrames()) + " frames)";
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
        if (isGranular())
            paintGranular (g, area);
        else if (isSpectral())
            paintSpectral (g, area);
    }
    else if (isMultisample())
        paintMultisample (g, area);
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

    // the Preset table: the frames of the file in the library, not a drawing of the idea of a table
    if (const auto table = userTable())
    {
        paintUserTable (g, area, *table);
        return;
    }

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

void OscWaveView::paintUserTable (juce::Graphics& g, juce::Rectangle<float> area, const wtedit::Wavetable& table)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const size_t frames = table.numFrames();
    const float framePos = (frameHandle.valid() ? frameHandle.normalised() : 0.0f) * static_cast<float> (frames > 1 ? frames - 1 : 0);
    const size_t f0 = static_cast<size_t> (juce::jlimit (0.0f, static_cast<float> (frames - 1), std::floor (framePos)));
    const size_t f1 = juce::jmin (frames - 1, f0 + 1);
    const float mix = framePos - static_cast<float> (f0);
    const auto sampleAt = [&] (size_t frame, float phase)
    {
        const float pos = phase * static_cast<float> (wtedit::kWaveformSize);
        const size_t i0 = static_cast<size_t> (pos) % wtedit::kWaveformSize;
        return table.getFrame (frame)[i0];
    };
    const auto pathOf = [&] (size_t fa, size_t fb, float amount, juce::Rectangle<float> r)
    {
        juce::Path p;
        const int n = juce::jmax (2, juce::roundToInt (r.getWidth()));
        for (int i = 0; i < n; ++i)
        {
            const float ph = static_cast<float> (i) / static_cast<float> (n - 1) * 0.9999f;
            const float v = sampleAt (fa, ph) * (1.0f - amount) + sampleAt (fb, ph) * amount;
            const float x = r.getX() + r.getWidth() * static_cast<float> (i) / static_cast<float> (n - 1);
            const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * r.getHeight() * 0.42f;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        return p;
    };

    if (mode == ViewMode::wave3D)
    {
        // a stack of up to 24 frames, the playing one in the accent colour
        const size_t count = juce::jmin<size_t> (frames, 24);
        for (size_t k = count; k-- > 0;)
        {
            const size_t frame = count > 1 ? k * (frames - 1) / (count - 1) : 0;
            const float lift = static_cast<float> (k) * area.getHeight() * 0.025f;
            auto r = area.reduced (area.getWidth() * 0.08f, area.getHeight() * 0.12f).translated (static_cast<float> (k) * 1.5f, -lift + area.getHeight() * 0.1f);
            g.setColour (c.outline.withAlpha (0.35f + 0.5f * static_cast<float> (count - k) / static_cast<float> (count)));
            g.strokePath (pathOf (frame, frame, 0.0f, r), juce::PathStrokeType (t.px (1.0f)));
        }
        g.setColour (c.accent);
        g.strokePath (pathOf (f0, f1, mix, area.reduced (area.getWidth() * 0.08f, area.getHeight() * 0.12f)), juce::PathStrokeType (t.px (2.0f)));
    }
    else if (mode == ViewMode::spectral)
    {
        wtedit::Frame frame;
        for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
            frame[i] = table.getFrame (f0)[i] * (1.0f - mix) + table.getFrame (f1)[i] * mix;
        std::vector<float> magnitude (wtedit::kNumFftBins + 1), phase (wtedit::kNumFftBins + 1);
        wtedit::Radix2Fft::realToPolar (frame.sampleData(), magnitude.data(), phase.data());
        const int bars = 48;
        const float barW = area.getWidth() / static_cast<float> (bars);
        float peak = 1.0e-6f;
        for (int i = 1; i <= bars; ++i)
            peak = juce::jmax (peak, magnitude[static_cast<size_t> (i)]);
        for (int i = 1; i <= bars; ++i)
        {
            const float h = juce::jlimit (0.0f, 1.0f, magnitude[static_cast<size_t> (i)] / peak) * area.getHeight() * 0.85f;
            g.setColour (c.accent.withAlpha (0.5f + 0.5f * h / area.getHeight()));
            g.fillRect (area.getX() + static_cast<float> (i - 1) * barW, area.getBottom() - h, barW * 0.8f, h);
        }
    }
    else
    {
        g.setColour (c.track.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
        g.setColour (c.accent);
        g.strokePath (pathOf (f0, f1, mix, area), juce::PathStrokeType (t.px (2.0f)));
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

    // Remap 1, 2 and 4 (warp modes 20, 21, 23) follow the curve drawn for this oscillator (the slot of oscN.remap.slot, points joined by lines)
    std::function<float (float)> remapCurve;
    if (warpType == 20 || warpType == 21 || warpType == 23)
    {
        ParamHandle slotHandle (ctx, "osc" + std::to_string (oscIndex()) + ".remap.slot");
        const int slot = slotHandle.valid() ? juce::roundToInt (slotHandle.plain()) + 1 : 1;
        const auto content = ctx.host.content(); // (a copy: the range below must not point into a temporary)
        for (const auto& r : content.remaps)
            if (r.osc == oscIndex() && r.slot == slot && r.points.size() >= 2)
            {
                const auto points = r.points;
                remapCurve = [points] (float x)
                {
                    if (x <= points.front().x)
                        return points.front().y;
                    for (size_t k = 1; k < points.size(); ++k)
                        if (x <= points[k].x)
                        {
                            const float span = points[k].x - points[k - 1].x;
                            const float f = span > 1.0e-6f ? (x - points[k - 1].x) / span : 0.0f;
                            return points[k - 1].y + (points[k].y - points[k - 1].y) * f;
                        }
                    return points.back().y;
                };
            }
    }

    const float halfH = area.getHeight() * 0.42f;
    for (int i = 0; i < numPoints; ++i)
    {
        const float phase = static_cast<float> (i) / static_cast<float> (numPoints);
        const float yVal = evalWave (phase, wtIdx, frame, warpAmount, warpType, remapCurve);
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

    // the sample waveform: min / max of the decoded file over the visible window
    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.4f;
    if (const auto prev = preview())
    {
        const int columns = juce::jmax (2, juce::roundToInt (w));
        std::vector<float> lo, hi;
        prev->peaks (static_cast<float> (sampleVp.toRange (0.0)), static_cast<float> (sampleVp.toRange (1.0)), columns, lo, hi);
        juce::Path waveP;
        waveP.startNewSubPath (area.getX(), midY - hi[0] * halfH);
        for (int i = 1; i < columns; ++i)
            waveP.lineTo (area.getX() + static_cast<float> (i) * w / static_cast<float> (columns - 1), midY - hi[static_cast<size_t> (i)] * halfH);
        for (int i = columns - 1; i >= 0; --i)
            waveP.lineTo (area.getX() + static_cast<float> (i) * w / static_cast<float> (columns - 1), midY - lo[static_cast<size_t> (i)] * halfH);
        waveP.closeSubPath();
        g.setColour (c.accentDim.withAlpha (0.55f));
        g.fillPath (waveP);
        g.setColour (c.accentDim);
        g.strokePath (waveP, juce::PathStrokeType (t.px (1.0f)));
    }
    else
    {
        g.setColour (c.textDim);
        g.drawHorizontalLine (juce::roundToInt (midY), area.getX(), area.getRight());
        g.setFont (t.labelFont());
        g.drawText (statusLine.isNotEmpty() ? statusLine : juce::String ("No sample loaded - tap Load to choose a file"), area.toNearestInt(), juce::Justification::centred, true);
    }

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
        // Auto slices: the onsets of the file that pass the threshold (the slices the engine plays), then the threshold line
        for (const float position : autoSliceMarkers())
        {
            const float x = sampleNormToX (position);
            if (! inside (x))
                continue;
            g.setColour (c.warn.withAlpha (0.8f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
            juce::Path tri;
            tri.addTriangle (x - t.px (5.0f), area.getBottom(), x + t.px (5.0f), area.getBottom(), x, area.getBottom() - t.px (9.0f));
            g.fillPath (tri);
        }
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

    // Show Marker Animation: the range the modulation of UNI START sweeps over, and the marker itself where the engine plays it now (WP-17)
    if (const float live = liveUniStart(); live >= 0.0f)
    {
        const float lx = sampleNormToX (live);
        g.setColour (c.text);
        g.drawVerticalLine (juce::roundToInt (lx), area.getY(), area.getBottom());
        g.setColour (c.mod);
        g.fillRect (lx - t.px (4.0f), area.getY(), t.px (8.0f), t.px (6.0f));
    }
    if (markerAnimation)
        for (const auto& band : modulationBands())
        {
            const float x0 = sampleNormToX (band.first), x1 = sampleNormToX (band.second);
            const float pulse = 0.5f + 0.5f * std::sin (static_cast<float> (juce::Time::getMillisecondCounter() % 2000) * 0.0031416f);
            g.setColour (c.mod.withAlpha (0.10f + 0.12f * pulse));
            g.fillRect (juce::jmax (x0, area.getX()), area.getY(), juce::jmax (1.0f, juce::jmin (x1, area.getRight()) - juce::jmax (x0, area.getX())), area.getHeight());
            g.setColour (c.mod);
            g.drawVerticalLine (juce::roundToInt (x0), area.getY(), area.getBottom());
            g.drawVerticalLine (juce::roundToInt (x1), area.getY(), area.getBottom());
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

void OscWaveView::paintMultisample (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    const auto& zones = instrumentZones();
    // the keyboard under the map (octave lines) and the zones: keys across, velocity up
    g.setColour (c.outline.withAlpha (0.4f));
    for (int octave = 0; octave <= 10; ++octave)
        g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * static_cast<float> (octave * 12) / 128.0f), area.getY(), area.getBottom());
    const int played = lastPlayedZone();
    for (size_t i = 0; i < zones.size(); ++i)
    {
        const auto r = zoneRect (zones[i]).reduced (t.px (0.5f));
        const bool hot = static_cast<int> (i) == played;
        g.setColour ((hot ? c.accent : c.accentDim).withAlpha (hot ? 0.8f : 0.28f));
        g.fillRect (r);
        g.setColour (hot ? c.accent : c.accentDim);
        g.drawRect (r, 1.0f);
    }
    if (zones.empty())
    {
        g.setColour (c.textDim);
        g.setFont (t.labelFont());
        g.drawText ("Load an SFZ instrument: the zones appear here (keys across, velocity up)", area.toNearestInt(), juce::Justification::centred, true);
    }
    // the envelope thumbnail: the shape of the keys when OVERRIDE is on, the SFZ's otherwise
    const auto thumb = envThumbArea();
    g.setColour (c.panel.withAlpha (0.85f));
    g.fillRoundedRectangle (thumb, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (thumb, t.px (6.0f), 1.0f);
    const bool over = multiOverrideHandle.valid() && multiOverrideHandle.plain() > 0.5f;
    g.setFont (t.labelFont());
    g.setColour (over ? c.accent : c.textDim);
    g.drawText (over ? "ENV OVERRIDE" : "ENV: SFZ", thumb.toNearestInt().removeFromTop (t.ipx (14.0f)), juce::Justification::centred, false);
    if (over)
    {
        const auto plain = [&] (const char* tail, float fallback)
        {
            ParamHandle h (ctx, "osc" + std::to_string (index) + ".multi." + tail);
            return h.valid() ? h.plain() : fallback;
        };
        const float delay = plain ("delay", 0.0f), attack = plain ("attack", 0.0f), hold = plain ("hold", 0.0f), decay = plain ("decay", 500.0f), release = plain ("release", 100.0f);
        const float sustain = plain ("sustain", 100.0f) * 0.01f;
        const float total = juce::jmax (1.0f, delay + attack + hold + decay + release) * 1.3f;
        const auto inner = thumb.reduced (t.px (6.0f)).withTrimmedTop (t.px (14.0f));
        const auto at = [&] (float ms, float level) { return juce::Point<float> (inner.getX() + inner.getWidth() * ms / total, inner.getBottom() - inner.getHeight() * level); };
        juce::Path p;
        float x = 0.0f;
        p.startNewSubPath (at (0.0f, 0.0f));
        p.lineTo (at (x += delay, 0.0f));
        p.lineTo (at (x += attack, 1.0f));
        p.lineTo (at (x += hold, 1.0f));
        p.lineTo (at (x += decay, sustain));
        p.lineTo (at (x += release * 0.0f + total * 0.12f, sustain));
        p.lineTo (at (x += release, 0.0f));
        g.setColour (c.accent);
        g.strokePath (p, juce::PathStrokeType (t.px (1.5f)));
    }
    g.restoreState();
}

/** The spectral overlay: the spectrogram of the sample under the waveform, the frequency strip with the Lo / Hi markers and the band between them, the X|Y dot of the manual mode. */
void OscWaveView::rebuildSpectrogram()
{
    const auto prev = preview();
    const auto area = waveArea();
    const float start = static_cast<float> (sampleVp.toRange (0.0)), end = static_cast<float> (sampleVp.toRange (1.0));
    const int columns = juce::jlimit (8, 400, juce::roundToInt (area.getWidth() / 3.0f));
    if (prev == nullptr)
    {
        spectrogramFor = nullptr;
        spectrogramColumns = 0;
        return;
    }
    if (spectrogramFor == prev.get() && spectrogramColumns == columns && std::abs (spectrogramStart - start) < 1.0e-4f && std::abs (spectrogramEnd - end) < 1.0e-4f)
        return;
    std::vector<float> cells;
    const int rows = 48;
    prev->spectrogram (start, end, columns, rows, cells);
    spectrogramImage = juce::Image (juce::Image::ARGB, columns, rows, true);
    const Palette& c = ctx.theme.colours;
    for (int x = 0; x < columns; ++x)
        for (int y = 0; y < rows; ++y)
        {
            const float v = cells[static_cast<size_t> (x) * rows + static_cast<size_t> (y)];
            spectrogramImage.setPixelAt (x, rows - 1 - y, c.accent.withAlpha (juce::jlimit (0.0f, 1.0f, v * v * 0.8f)));
        }
    spectrogramFor = prev.get();
    spectrogramColumns = columns;
    spectrogramStart = start;
    spectrogramEnd = end;
}

void OscWaveView::paintSpectral (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    rebuildSpectrogram();
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    if (spectrogramColumns > 0)
    {
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (spectrogramImage, area);
    }
    // the band between Freq Lo and Freq Hi and the strip with the two markers
    const auto strip = freqStripArea();
    const auto lo = freqMarkerPosition (false), hi = freqMarkerPosition (true);
    g.setColour (c.panel.withAlpha (0.7f));
    g.fillRect (strip);
    g.setColour (c.accent.withAlpha (0.18f));
    g.fillRect (area.getX(), hi.y, area.getWidth() - strip.getWidth(), lo.y - hi.y);
    g.setColour (c.accent);
    for (const auto& marker : { lo, hi })
    {
        g.drawLine (area.getX(), marker.y, strip.getRight(), marker.y, t.px (1.0f));
        g.fillRoundedRectangle (marker.x - t.px (14.0f), marker.y - t.px (5.0f), t.px (28.0f), t.px (10.0f), t.px (4.0f));
    }
    g.setColour (c.textDim);
    g.setFont (t.labelFont());
    g.drawText (juce::String (juce::roundToInt (specFreqHiHandle.valid() ? specFreqHiHandle.plain() : 20000.0f)) + " Hz", strip.toNearestInt().removeFromTop (t.ipx (14.0f)), juce::Justification::centred, false);
    g.drawText (juce::String (juce::roundToInt (specFreqLoHandle.valid() ? specFreqLoHandle.plain() : 20.0f)) + " Hz", strip.toNearestInt().removeFromBottom (t.ipx (14.0f)), juce::Justification::centred, false);
    if (xyManualOn())
    {
        const auto dot = xyDotPosition();
        g.setColour (c.danger.withAlpha (0.9f));
        g.drawLine (dot.x, area.getY(), dot.x, area.getBottom(), t.px (1.0f));
        g.fillEllipse (dot.x - t.px (7.0f), dot.y - t.px (7.0f), t.px (14.0f), t.px (14.0f));
    }
    g.restoreState();
}

/** The granular overlay: the grains the settings spawn (bars across the region), the grain window thumbnail and, in manual mode, the X|Y dot. */
void OscWaveView::paintGranular (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());

    for (const GrainMark& mark : grainMarks())
    {
        const float x = sampleNormToX (mark.position);
        const float w = juce::jmax (t.px (2.0f), sampleNormToX (mark.position + mark.length) - x);
        const float h = area.getHeight() * 0.5f * mark.height;
        g.setColour (c.accent.withAlpha (0.35f));
        g.fillRoundedRectangle (x, area.getCentreY() - h * 0.5f, w, h, t.px (2.0f));
    }

    // the window thumbnail
    const auto thumb = windowThumbArea();
    g.setColour (c.panel.withAlpha (0.85f));
    g.fillRoundedRectangle (thumb, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (thumb, t.px (6.0f), 1.0f);
    {
        const auto shape = static_cast<synthyboy::granular::WindowShape> (juce::jlimit (0, synthyboy::granular::kNumWindowShapes - 1, granWindowShapeHandle.valid() ? juce::roundToInt (granWindowShapeHandle.plain()) : 0));
        const float amount = granWindowAmountHandle.valid() ? granWindowAmountHandle.normalised() : 0.5f;
        const float skew = granWindowSkewHandle.valid() ? granWindowSkewHandle.normalised() * 2.0f - 1.0f : 0.0f;
        const auto inner = thumb.reduced (t.px (6.0f));
        juce::Path p;
        const int steps = 40;
        for (int i = 0; i <= steps; ++i)
        {
            const float w = synthyboy::granular::windowValue (shape, static_cast<float> (i) / steps, amount, skew);
            const float x = inner.getX() + inner.getWidth() * static_cast<float> (i) / steps;
            const float y = inner.getBottom() - inner.getHeight() * juce::jlimit (0.0f, 1.0f, w);
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        g.setColour (c.accent);
        g.strokePath (p, juce::PathStrokeType (t.px (1.5f)));
    }

    // the X|Y dot of the manual mode
    if (granManualHandle.valid() && granManualHandle.plain() > 0.5f)
    {
        const auto dot = xyDotPosition();
        g.setColour (c.danger.withAlpha (0.9f));
        g.drawLine (dot.x, area.getY(), dot.x, area.getBottom(), t.px (1.0f));
        g.fillEllipse (dot.x - t.px (7.0f), dot.y - t.px (7.0f), t.px (14.0f), t.px (14.0f));
    }
    g.restoreState();
}

OscNoiseView::OscNoiseView (UiContext& context)
    : ctx (context),
      levelHandle (context, "noise.level"),
      panHandle (context, "noise.pan"),
      onHandle (context, "noise.on"),
      destHandle (context, "noise.destination"),
      sampleHandle (context, "noise.sample"),
      typeHandle (context, "noise.type")
{
    const auto repaintCb = [this] { repaint(); };
    typeHandle.onChange = repaintCb;
    levelHandle.onChange = repaintCb;
    panHandle.onChange = repaintCb;
    onHandle.onChange = repaintCb;
    destHandle.onChange = repaintCb;
    sampleHandle.onChange = repaintCb;
}

OscNoiseView::~OscNoiseView() = default;

bool OscNoiseView::showsContentSample() const
{
    return typeHandle.valid() && typeHandle.plain() > 0.5f && sampleOf (ctx.host.content(), kNoiseSampleOsc) != nullptr;
}

int OscNoiseView::currentSample() const
{
    return sampleHandle.valid() ? juce::jlimit (0, 8, static_cast<int> (sampleHandle.plain() + 0.5f)) : 0;
}

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

    // Header info with sample colour name
    static const char* const kSampleNames[] = {
        "White Noise", "Pink Noise", "Brown Noise", "Geiger",
        "Tape Hiss", "Vinyl Clicks", "Digital Hash", "Velvet Noise", "Atmosphere"
    };
    const int sampleIdx = currentSample();
    juce::String sampleName = (sampleIdx >= 0 && sampleIdx < 9) ? kSampleNames[sampleIdx] : "Noise";
    const auto content = ctx.host.content(); // a copy: the pointer below points into it
    const ContentSample* contentSample = showsContentSample() ? sampleOf (content, kNoiseSampleOsc) : nullptr;

    g.setFont (t.labelFont());
    g.setColour (c.textDim);
    g.drawText (contentSample != nullptr ? juce::String (contentSample->ref.name) + " (Sample)" : sampleName + " Generator",
                r.reduced (t.px (8.0f), t.px (4.0f)).removeFromTop (t.px (18.0f)).toNearestInt(), juce::Justification::topLeft, false);

    if (isOff)
    {
        g.setColour (c.warn);
        g.drawText ("OFF", r.reduced (t.px (8.0f), t.px (4.0f)).removeFromTop (t.px (18.0f)).toNearestInt(), juce::Justification::topRight, false);
    }

    const auto scopeArea = r.reduced (t.px (8.0f), t.px (8.0f)).withTrimmedTop (t.px (18.0f));

    // Draw animated pseudo-random noise waveform scope tailored to sample type
    juce::Path p;
    const int numPoints = juce::roundToInt (scopeArea.getWidth());
    const float midY = scopeArea.getCentreY();
    const float amp = scopeArea.getHeight() * 0.35f * (levelHandle.valid() ? levelHandle.normalised() : 0.5f);

    if (contentSample != nullptr)
    {
        // the file: min / max of the decoded sample over the width (the real waveform, WP-04)
        if (const auto prev = contentCache (ctx).samplePreview (ctx.host, contentSample->ref))
        {
            const int columns = juce::jmax (2, numPoints);
            std::vector<float> lo, hi;
            prev->peaks (0.0f, 1.0f, columns, lo, hi);
            juce::Path wave;
            const float w = scopeArea.getWidth();
            wave.startNewSubPath (scopeArea.getX(), midY - hi[0] * amp * 2.5f);
            for (int i = 1; i < columns; ++i)
                wave.lineTo (scopeArea.getX() + static_cast<float> (i) * w / static_cast<float> (columns - 1), midY - hi[static_cast<size_t> (i)] * amp * 2.5f);
            for (int i = columns - 1; i >= 0; --i)
                wave.lineTo (scopeArea.getX() + static_cast<float> (i) * w / static_cast<float> (columns - 1), midY - lo[static_cast<size_t> (i)] * amp * 2.5f);
            wave.closeSubPath();
            g.setColour (c.accentDim.withAlpha (0.55f));
            g.fillPath (wave);
            g.setColour (c.accentDim);
            g.strokePath (wave, juce::PathStrokeType (t.px (1.0f)));
        }
    }
    else
    {
    juce::Random rnd (42 + sampleIdx * 101);
    float filteredVal = 0.0f;
    for (int i = 0; i < numPoints; ++i)
    {
        const float x = scopeArea.getX() + static_cast<float> (i);
        float noiseVal = rnd.nextFloat() * 2.0f - 1.0f;

        switch (sampleIdx)
        {
            case 1: // Pink: smoothed low-mid emphasis
                filteredVal += 0.25f * (noiseVal - filteredVal);
                noiseVal = filteredVal * 1.8f;
                break;
            case 2: // Brown: integrated slope
                filteredVal = (filteredVal + 0.08f * noiseVal) * 0.96f;
                noiseVal = filteredVal * 3.0f;
                break;
            case 3: // Geiger: sparse spikes
                noiseVal = (rnd.nextFloat() < 0.04f) ? (rnd.nextBool() ? 1.0f : -1.0f) : (noiseVal * 0.05f);
                break;
            case 4: // Tape Hiss: gentle lowpass
                filteredVal += 0.4f * (noiseVal - filteredVal);
                noiseVal = filteredVal * 1.3f;
                break;
            case 5: // Vinyl Clicks: intermittent crackle spikes
                noiseVal = (rnd.nextFloat() < 0.02f) ? (rnd.nextBool() ? 0.9f : -0.9f) : (noiseVal * 0.15f);
                break;
            case 6: // Digital Hash: stepped 8-bit quantization
                noiseVal = std::round (noiseVal * 4.0f) / 4.0f;
                break;
            case 7: // Velvet Noise: sparse impulses
                noiseVal = ((i % 12) == (rnd.nextInt (12))) ? (rnd.nextBool() ? 1.0f : -1.0f) : 0.0f;
                break;
            case 8: // Atmosphere: resonant sinusoidal peaks
                noiseVal = std::sin (static_cast<float> (i) * 0.12f) * 0.4f + noiseVal * 0.5f;
                break;
            default:
                break;
        }

        const float y = midY + juce::jlimit (-1.0f, 1.0f, noiseVal) * amp;
        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }
    g.setColour (c.accent.withAlpha (0.7f));
    g.strokePath (p, juce::PathStrokeType (t.px (1.2f)));
    }

    if (status.isNotEmpty())
    {
        g.setFont (t.labelFont());
        g.setColour (c.textDim);
        g.drawText (status, r.reduced (t.px (8.0f), t.px (4.0f)).removeFromBottom (t.px (16.0f)).toNearestInt(), juce::Justification::bottomLeft, true);
    }

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
    // the top of the strip: the level bar grows from the foot, so the top stays free for a sideways drag
    const auto col = stripBounds (strip);
    return { col.getX(), col.getY(), col.getWidth(), ctx.theme.px (24.0f) };
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

        // Pan band: a thin track at the top of the strip, the dot is the pan
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
