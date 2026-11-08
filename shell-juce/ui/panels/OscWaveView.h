// SynthyBoy - UI: oscillator, noise, and mixer visualizers (docs/ui-design/02-oscillators.md)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include <string>

#include "../GestureKit.h"
#include "../ParamHandle.h"
#include "../Theme.h"
#include "../UiContext.h"
#include "../model/GestureModel.h"

namespace synthyboy::ui
{
/** Large interactive visual for an oscillator: wavetable 2D / 3D / spectral and sample waveform views (02-oscillators.md §2.1, §2.2, §3).
    Direct manipulation: horizontal drag on the wave changes oscN.frame; drag handles set sample start/end and loop markers.
    Tap on the view cycles 2D -> 3D -> SP mode. Visible alternative: prev/next and view mode buttons. */
class OscWaveView final : public juce::Component
{
public:
    OscWaveView (UiContext& context, int oscIndex);
    ~OscWaveView() override;

    enum class ViewMode { wave2D, wave3D, spectral };
    enum class SampleMarker { none, start, end, loopStart, loopEnd, loopPair, slice, threshold };

    ViewMode viewMode() const noexcept { return mode; }
    void setViewMode (ViewMode newMode);
    void cycleViewMode();

    int oscIndex() const noexcept { return index; }
    bool isSampleType() const;

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

    ParamHandle sampleStartHandle;
    ParamHandle sampleEndHandle;
    ParamHandle sampleLoopStartHandle;
    ParamHandle sampleLoopEndHandle;

    juce::TextButton prevWtButton { "<" };
    juce::TextButton nextWtButton { ">" };
    juce::TextButton viewModeButton { "2D" };
    juce::TextButton zoomInBtn { "+" }, zoomOutBtn { "-" }, fitBtn { "Fit" };
    juce::TextButton addSliceBtn { "+ Slice" }, removeSliceBtn { "- Slice" };

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

private:
    UiContext& ctx;
    ParamHandle levelHandle;
    ParamHandle panHandle;
    ParamHandle onHandle;
    ParamHandle destHandle;

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
        pan band above it (drag sideways = the strip's pan). */
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
