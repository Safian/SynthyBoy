// SynthyBoy - UI: master oscilloscope / spectrum analyser with the voice and CPU read-out (docs/ui-design/07 sections 2.2, 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Fed by UiHost::copyScope () (the processor's lock free ring of the master output). A tap on the display, or a pinch (two fingers, the
// trackpad), toggles Wave <-> FFT; the [Wave] [FFT] segment buttons are the visible alternative. The read-out shows the sounding voices
// against the voice cap (UiHost::activeVoiceCount ()) and the CPU load (UiHost::cpuLoad ()).
#pragma once

#include <array>

#include "../GestureKit.h"
#include "../Panel.h"

namespace synthyboy::ui
{
class ScopeView final : public juce::Component, private juce::Timer
{
public:
    enum class Mode { wave, fft };

    explicit ScopeView (UiContext& context);
    ~ScopeView() override;

    Mode mode() const noexcept { return currentMode; }
    void setMode (Mode newMode);
    void toggleMode();
    /** Pulls the latest samples from the host and redraws; the 30 Hz timer calls it. */
    void refresh();
    int sampleCount() const noexcept { return numSamples; }
    /** Peak of the pulled samples (the tests check the feed). */
    float peak() const noexcept { return lastPeak; }
    /** The spectrum bins (dB, 0 = full scale) of the last FFT frame; empty in Wave mode. */
    const std::vector<float>& spectrumDb() const noexcept { return spectrum; }
    /** Two-finger gestures: a pinch toggles. */
    void handleTouchGesture (const TouchTracker::Gesture& g);
    /** Called after every refresh (the panel repaints its read-out). */
    std::function<void()> onRefresh;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    void timerCallback() override { refresh(); }

    UiContext& ctx;
    Mode currentMode = Mode::wave;
    std::array<float, 2048> samples {};
    int numSamples = 0;
    float lastPeak = 0.0f;
    std::vector<float> spectrum;
    TwoFingerInput fingers;
    bool pairActive = false, moved = false;
};

class GlbScopePanel final : public Panel
{
public:
    explicit GlbScopePanel (UiContext& context);
    ~GlbScopePanel() override;

    juce::String title() const override { return "Scope"; }
    std::vector<std::string> keys() const override { return {}; }
    void build() override;
    int preferredHeight (int width) const override;
    void resized() override;
    void paint (juce::Graphics&) override;

    ScopeView& scope() noexcept { return *view; }
    juce::TextButton& waveButton() noexcept { return waveBtn; }
    juce::TextButton& fftButton() noexcept { return fftBtn; }
    /** "3 / 16 voices   CPU 12 %" */
    juce::String readout() const;

private:
    std::unique_ptr<ScopeView> view;
    juce::TextButton waveBtn { "Wave" }, fftBtn { "FFT" };
    ParamHandle polyHandle;
};
} // namespace synthyboy::ui
