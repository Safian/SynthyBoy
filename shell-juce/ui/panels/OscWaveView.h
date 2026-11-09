// SynthyBoy - UI: oscillator, noise, and mixer visualizers (docs/ui-design/02-oscillators.md)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "../GestureKit.h"
#include "../ParamHandle.h"
#include "../Theme.h"
#include "../ContentIO.h"
#include "../UiContext.h"
#include "../model/GestureModel.h"

#include "synthyboy/multisample/multisample.h"
#include "synthyboy/user_content.h"

namespace synthyboy::ui
{
class WavetableEditorSheet;

/** Large interactive visual for an oscillator: wavetable 2D / 3D / spectral and sample waveform views (02-oscillators.md §2.1, §2.2, §3).
    Direct manipulation: horizontal drag on the wave changes oscN.frame; drag handles set sample start/end and loop markers.
    Tap on the view cycles 2D -> 3D -> SP mode. Visible alternative: prev/next and view mode buttons. */
class OscWaveView final : public juce::Component, private juce::Timer
{
public:
    OscWaveView (UiContext& context, int oscIndex);
    ~OscWaveView() override;

    enum class ViewMode { wave2D, wave3D, spectral };
    enum class SampleMarker { none, start, end, loopStart, loopEnd, loopPair, slice, threshold, xy, window, freqLo, freqHi };

    ViewMode viewMode() const noexcept { return mode; }
    void setViewMode (ViewMode newMode);
    void cycleViewMode();

    int oscIndex() const noexcept { return index; }
    /** The value of oscN.type (0 Wavetable, 1 Sample, 2 Granular, 3 Spectral, 4 Multisample), read from the control itself. */
    int oscType() const;
    /** Sample, Granular and Spectral show the waveform of the content sample with the region and loop markers. */
    bool isSampleType() const;
    bool isGranular() const { return oscType() == kOscTypeGranular; }
    bool isSpectral() const { return oscType() == kOscTypeSpectral; }
    bool isMultisample() const { return oscType() == kOscTypeMultisample; }

    // ---- multisample view (GROUPC WP-13, 13-new-keys.md 4.39): the zone map of the instrument (keys across, velocity up), the last played zone, the envelope thumbnail ----
    /** The zones of the instrument of this oscillator (empty without one); read from the library when the content changed. */
    const std::vector<multisample::Region>& instrumentZones();
    /** The zone map and the cell of a zone / key / velocity inside it. */
    juce::Rectangle<float> zoneMapArea() const;
    juce::Rectangle<float> zoneRect (const multisample::Region& region) const;
    /** The envelope thumbnail (bottom right): a tap switches OVERRIDE (the SFZ envelope against the envelope of the keys). */
    juce::Rectangle<float> envThumbArea() const;
    /** Load SFZ... and Switch to Single Sample, also as calls (the buttons and the menu use them). */
    FlowResult loadInstrument (const juce::File& sfzFile);
    FlowResult switchToSample();
    /** The zone of the last note of this oscillator (-1: none yet), from the engine's read-out. */
    int lastPlayedZone() const;

    // ---- spectral view (GROUPC WP-12, 13-new-keys.md 4.38): the spectrogram of the sample, the low / high frequency markers on its right edge ----
    /** The strip at the right edge of the display where the Freq Lo / Hi markers live (log axis 20 Hz .. 20 kHz, bottom to top). */
    juce::Rectangle<float> freqStripArea() const;
    float freqToY (float hz) const;
    float yToFreq (float y) const;
    juce::Point<float> freqMarkerPosition (bool high) const;
    /** True once a spectrogram of the loaded sample is available (computed on the first paint after a sample is loaded). */
    bool hasSpectrogram() const noexcept { return spectrogramColumns > 0; }

    // ---- granular view (GROUPC WP-11, design 02 / 13-new-keys.md 4.37): the window thumbnail and the X|Y dot ----
    /** The window thumbnail (top right of the display): a tap steps the grain window shape, a drag sets the amount (up) and the skew (sideways). */
    juce::Rectangle<float> windowThumbArea() const;
    /** The X|Y dot (manual mode): x is the position in the region, y the Y position `oscN.gran.xy_y`. */
    juce::Point<float> xyDotPosition() const;
    /** The grains the display draws for the current settings: (position 0..1 in the file, length as share of the file, height 0..1). Deterministic. */
    struct GrainMark { float position, length, height; };
    std::vector<GrainMark> grainMarks() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

    // ---- sample view: zoom / pan, slices, threshold line, loop pair, loupe (design 02 rows 19..23) ----
    /** Where the sample waveform is drawn (the markers and the gestures use the same rectangle). */
    juce::Rectangle<float> waveArea() const;
    Viewport& sampleViewport() noexcept { return sampleVp; }
    float sampleNormToX (float norm) const;
    float xToSampleNorm (float x) const;
    /** Zoom about the screen x (view coordinates) / pan by dx pixels. The + - Fit buttons call the same. */
    void zoomSampleAt (float x, float factor);
    void panSample (float dxPixels);
    void fitSample();
    /** Slice markers of the Manual slice mode: the `slices` of this oscillator's ContentSample (empty without a loaded sample). */
    const std::vector<float>& slices() const noexcept { return sliceCache; }
    bool canEditSlices() const;
    bool addSliceAt (float norm);
    bool removeSlice (int index);
    int selectedSliceIndex() const noexcept { return selectedSlice; }
    /** The threshold line (slice mode Auto): y in view coordinates. */
    float thresholdLineY() const;
    bool loupeShowing() const noexcept { return loupe != nullptr && loupe->isShowing(); }
    /** The gestures of two fingers / a trackpad, also callable directly (tests, hardware): pinch zoom, pan, swipe to the next oscillator. */
    void handleTouchGesture (const TouchTracker::Gesture& g);
    /** The row of the design: switch to the previous (-1) / next (+1) oscillator; set by the panel. */
    std::function<void (int)> onSwitchOscillator;
    /** The wavetable name swipe (design 02-04): previous (-1) / next (+1) table. */
    void stepWavetable (int direction);
    void longPressedAt (juce::Point<float> where);

    /** Test hooks. */
    bool isDraggingMarker() const noexcept { return activeMarker != SampleMarker::none; }
    juce::TextButton& zoomInButton() noexcept { return zoomInBtn; }
    juce::TextButton& zoomOutButton() noexcept { return zoomOutBtn; }
    juce::TextButton& fitButton() noexcept { return fitBtn; }
    juce::TextButton& addSliceButton() noexcept { return addSliceBtn; }
    juce::TextButton& removeSliceButton() noexcept { return removeSliceBtn; }
    LongPressDetector& longPress() noexcept { return press; }
    void refreshSliceCache();
    // ---- content pipeline (WP-01 c): load / reload / trim a sample, the real waveform, snap, auto slices, marker animation ----
    /** The decoded sample of this oscillator (null without one). */
    std::shared_ptr<const SamplePreview> preview() const;
    /** Load Sample: the file is decoded, stored in the user library and becomes `content.samples[osc]`; the root note is taken from the file name / the WAV
        chunk. Returns the result line shown in the header. */
    FlowResult loadSampleFile (const juce::File& file);
    /** Asks the host for the file (Load button); `loadSampleFile` runs when the user chose one. */
    void requestSampleFile();
    FlowResult trimSample();
    FlowResult removeSample();
    FlowResult reloadSample();
    /** Snap Off / Zero / Beats / Loop of the markers (design 02, S p.72-73). The Snap button cycles it. */
    SnapMode snapMode() const noexcept { return snap; }
    void setSnapMode (SnapMode mode);
    /** A marker position as the drag would set it for `raw` (the snap applied). */
    float snappedMarker (float raw, SampleMarker marker) const;
    /** The slice starts the Auto mode draws: the onsets stronger than the threshold (the same rule as the engine). */
    std::vector<float> autoSliceMarkers() const;
    /** Show Marker Animation: the modulation range of UNI START / SPAN / SCAN is drawn as bands. */
    bool markerAnimationOn() const noexcept { return markerAnimation; }
    void setMarkerAnimation (bool on);
    /** The ranges (start, end as fractions of the file) the modulation of oscN.sample.uni_start covers; empty when it is not modulated. */
    std::vector<std::pair<float, float>> modulationBands() const;
    /** WP-17: where the engine plays the unison start now (normalised position in the sample), -1 while the marker animation is off, nothing modulates it or the engine gives
        no read-out. */
    float liveUniStart() const;
    const juce::String& sampleStatus() const noexcept { return statusLine; }
    juce::TextButton& loadButton() noexcept { return loadSampleBtn; }
    juce::TextButton& snapButton() noexcept { return snapBtn; }
    juce::TextButton& sampleMenuButton() noexcept { return sampleMenuBtn; }
    juce::TextButton& editButton() noexcept { return editBtn; }
    juce::TextButton& waveMenuButton() noexcept { return waveMenuBtn; }

    // ---- GROUPD (13-new-keys.md 4.40): Switch to wavetable, Render OSC Warp, Resample, wavetable frame <-> LFO shape; the menus call these ----
    /** "Switch to Wavetable" (Sample / Granular / Spectral): the START .. END region of the sample becomes this oscillator's table. */
    FlowResult switchToWavetable (const wtedit::ImportOptions& options);
    /** Render OSC Warp: 256 frames across WARP. */
    FlowResult renderWarp();
    /** Resample: one bar of the last note into this oscillator (`withNext`: Osc A and Osc B, only on oscillator 1). */
    FlowResult resample (bool withNext = false);
    /** Wavetable to LFO / LFO shape to this wavetable. */
    FlowResult copyFrameToLfo (int lfo);
    FlowResult copyLfoShapeHere (int lfo);
    /** The wavetable menu of the "..." button on a wavetable oscillator. */
    void showWaveMenu();
    /** The wavetable editor of this oscillator (Edit button); null until opened. */
    WavetableEditorSheet* editorSheetPtr() noexcept { return editorSheet.get(); }
    void openEditor();
    /** The table the oscillator plays from the library (the Preset table entry); null for a built-in table. */
    std::shared_ptr<const wtedit::Wavetable> userTable() const;

    /** Colour level of the unison voice pill by the CPU load (cpuLevel ()), -1 while the oscillator has one voice (no pill). */
    int unisonPillLevel() const;
    bool unisonCapped() const;

private:
    bool commitSlices (const std::vector<float>& newSlices);
    int sliceModeIndex() const;
    int nearestSliceAtX (float x) const;
    void cancelOneFingerGesture();
    void updateButtons();
    void paintWavetable (juce::Graphics& g, juce::Rectangle<float> area);
    void paintWave2D (juce::Graphics& g, juce::Rectangle<float> area, float frame, float warpAmount, int warpType, int wtIdx);
    void paintWave3D (juce::Graphics& g, juce::Rectangle<float> area, float frame, int wtIdx);
    void paintSpectral (juce::Graphics& g, juce::Rectangle<float> area, float frame, int wtIdx);
    void paintSample (juce::Graphics& g, juce::Rectangle<float> area);

    UiContext& ctx;
    int index = 1;
    ViewMode mode = ViewMode::wave2D;

    ParamHandle frameHandle;
    ParamHandle typeHandle;
    ParamHandle wtHandle;
    ParamHandle onHandle;
    ParamHandle warpAmountHandle;
    ParamHandle warpTypeHandle;
    ParamHandle unisonVoicesHandle;

    ParamHandle specFreqLoHandle, specFreqHiHandle, specManualHandle, specPositionHandle, specXyYHandle;
    juce::Image spectrogramImage;
    const SamplePreview* spectrogramFor = nullptr;
    int spectrogramColumns = 0;
    float spectrogramStart = -1.0f, spectrogramEnd = -1.0f;
    void paintSpectral (juce::Graphics& g, juce::Rectangle<float> area);
    void rebuildSpectrogram();
    ParamHandle& xyPositionHandle() noexcept { return isSpectral() ? specPositionHandle : granPositionHandle; }
    ParamHandle& xyYHandle() noexcept { return isSpectral() ? specXyYHandle : granXyYHandle; }
    bool xyManualOn() const;
    ParamHandle granPositionHandle, granXyYHandle, granManualHandle, granWindowAmountHandle, granWindowSkewHandle, granWindowShapeHandle, granDensityHandle,
        granLengthHandle, granRandOffsetHandle;
    bool windowTap = false;
    std::vector<multisample::Region> zoneCache;
    ContentFileRef zoneCacheRef;
    bool zoneCacheValid = false;
    ParamHandle multiOverrideHandle;
    void paintMultisample (juce::Graphics& g, juce::Rectangle<float> area);
    void paintGranular (juce::Graphics& g, juce::Rectangle<float> area);

    ParamHandle sampleStartHandle;
    ParamHandle sampleEndHandle;
    ParamHandle sampleLoopStartHandle;
    ParamHandle sampleLoopEndHandle;

    juce::TextButton prevWtButton { "<" };
    juce::TextButton nextWtButton { ">" };
    juce::TextButton viewModeButton { "2D" };
    juce::TextButton zoomInBtn { "+" }, zoomOutBtn { "-" }, fitBtn { "Fit" };
    juce::TextButton addSliceBtn { "+ Slice" }, removeSliceBtn { "- Slice" };
    juce::TextButton loadSampleBtn { "Load" }, snapBtn { "Snap: Off" }, sampleMenuBtn { "..." }, editBtn { "Edit" }, waveMenuBtn { "..." };
    SnapMode snap = SnapMode::off;
    bool markerAnimation = false;
    void timerCallback() override { repaint(); }
    juce::String statusLine;
    std::unique_ptr<WavetableEditorSheet> editorSheet;
    ParamHandle sampleLengthBeatsHandle, sampleUniStartHandle;
    void paintUserTable (juce::Graphics& g, juce::Rectangle<float> area, const wtedit::Wavetable& table);
    void showSampleMenu();

    ParamHandle sliceModeHandle, sliceThresholdHandle;
    Viewport sampleVp;
    std::vector<float> sliceCache;
    juce::uint32 sliceCacheMs = 0;
    int selectedSlice = -1;
    int draggedSlice = -1;
    bool sliceMoved = false;
    float loopPairStartNorm = 0.0f, loopPairEndNorm = 0.0f, loopPairDownNorm = 0.0f;
    bool nameSwipe = false;
    bool gestureOpen = false; // a one-finger gesture of this view is running (its mouseUp must close it)
    TwoFingerInput touch;
    LongPressDetector press;
    std::unique_ptr<Loupe> loupe;

    juce::Point<float> dragStartPos;
    float dragStartNorm = 0.0f;
    SampleMarker activeMarker = SampleMarker::none;
    bool dragging = false;
};

/** Scope and spectrum visualizer for the noise generator (02-oscillators.md §2.3).
    Direct manipulation: drag horizontally adjusts pan, drag vertically adjusts level. */
class OscNoiseView final : public juce::Component
{
public:
    explicit OscNoiseView (UiContext& context);
    ~OscNoiseView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    int currentSample() const;
    /** noise.type = Sample and the content sample (osc 4) exists: the view shows the file's waveform and name. */
    bool showsContentSample() const;
    void setStatus (const juce::String& text) { status = text; repaint(); }

private:
    UiContext& ctx;
    ParamHandle levelHandle;
    ParamHandle panHandle;
    ParamHandle onHandle;
    ParamHandle destHandle;
    ParamHandle sampleHandle;
    ParamHandle typeHandle;
    juce::String status;

    juce::Point<float> dragStartPos;
    float dragStartLevel = 0.0f;
    float dragStartPan = 0.0f;
    bool dragging = false;
};

/** Mixer console strips overview for all 8 channels + Main & Direct outputs (02-oscillators.md §2.4).
    Direct manipulation: dragging Main fader adjusts master.volume, Direct fader adjusts direct.level. */
class OscMixConsoleView final : public juce::Component
{
public:
    explicit OscMixConsoleView (UiContext& context);
    ~OscMixConsoleView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    /** The channel strips (O1 O2 O3 NOISE F1 F2 B1 B2) in view coordinates; used by the gestures and the tests. */
    juce::Rectangle<float> stripBounds (int strip) const;
    static constexpr int kNumStrips = 8;
    /** The header of a strip (its name at the foot: tap = power, long-press = module sheet, drag onto another strip = swap / copy) and the
        pan band at the top of the strip (drag sideways = the strip's pan). */
    juce::Rectangle<float> nameBounds (int strip) const;
    juce::Rectangle<float> panBounds (int strip) const;
    /** The module the strip stands for ("osc1" .. "osc3", "filter1", "filter2"); empty for NOISE and the buses. */
    static const char* stripModulePrefix (int strip) noexcept;
    bool hasPan (int strip) const noexcept { return strip >= 0 && strip < 6 && panHandles[static_cast<size_t> (strip)] != nullptr; }
    /** End of a header drag (the mouse path calls it; the tests call it): swap the two modules, copy with `copy`. */
    bool dropOnStrip (int from, int to, bool copy, bool withMods);
    void stripHeaderLongPressed (int strip);
    LongPressDetector& longPress() noexcept { return press; }
    int dropTargetStrip() const noexcept { return dropStrip; }

private:
    ParamHandle& stripLevel (int strip);
    ParamHandle* stripPower (int strip); // O1 O2 O3 NOISE only
    int stripAt (juce::Point<float> p) const;
    void stepFilterRouting();
    void toggleFxBypass();

    UiContext& ctx;
    ParamHandle masterVolumeHandle;
    ParamHandle directLevelHandle;
    ParamHandle mainFxBypassHandle;
    ParamHandle filterRoutingHandle;

    // channel levels for meters
    ParamHandle o1Level, o2Level, o3Level, noiseLevel;
    ParamHandle f1Level, f2Level, b1Level, b2Level;
    ParamHandle o1On, o2On, o3On, noiseOn;

    juce::TextButton fxBypassButton { "FX On" };
    juce::TextButton routingButton { "Parallel" };

    enum class FaderTarget { none, main, direct, strip };
    FaderTarget activeFader = FaderTarget::none;
    int activeStrip = -1;
    bool stripNameTap = false; // the press started on the name of a strip: a tap there toggles the channel
    float dragStartNorm = 0.0f;
    juce::Point<float> dragStartPos;

    std::array<std::unique_ptr<ParamHandle>, 6> panHandles; // O1 O2 O3 NOISE F1 F2
    LongPressDetector press;
    bool panDrag = false, nameDragging = false, nameConsumed = false;
    int dropStrip = -1;
};
} // namespace synthyboy::ui
