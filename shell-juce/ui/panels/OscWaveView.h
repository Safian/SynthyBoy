// SynthyBoy - UI: oscillator, noise, and mixer visualizers (docs/ui-design/02-oscillators.md)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

#include "../ParamHandle.h"
#include "../Theme.h"
#include "../UiContext.h"

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
    enum class SampleMarker { none, start, end, loopStart, loopEnd };

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

private:
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

private:
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

    enum class FaderTarget { none, main, direct };
    FaderTarget activeFader = FaderTarget::none;
    float dragStartNorm = 0.0f;
    juce::Point<float> dragStartPos;
};
} // namespace synthyboy::ui
