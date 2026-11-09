// SynthyBoy - UI panel: Sub oscillator (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "../Panel.h"

namespace synthyboy::ui
{
class SubWaveView final : public juce::Component
{
public:
    explicit SubWaveView (UiContext& context);
    ~SubWaveView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    int currentWaveform() const;
    /** sub.phase as a fraction of a cycle: the picture starts there, as the voice does at note on (WP-04). */
    float startPhase() const;
    /** sub.phase_contiguous: a new note continues with the phase of the previous one (the picture says CONT). */
    bool phaseContiguous() const;

private:
    UiContext& ctx;
    ParamHandle levelHandle;
    ParamHandle panHandle;
    ParamHandle onHandle;
    ParamHandle waveHandle;
    ParamHandle phaseHandle;
    ParamHandle contiguousHandle;

    juce::Point<float> dragStartPos;
    float dragStartLevel = 0.0f;
    float dragStartPan = 0.0f;
    bool dragging = false;
};

class SubPanel final : public Panel
{
public:
    explicit SubPanel (UiContext& context);
    ~SubPanel() override;

    juce::String title() const override { return "Sub"; }
    std::vector<std::string> keys() const override;
    void build() override;
    int preferredHeight (int width) const override;
    void resized() override;
    void paint (juce::Graphics& g) override;

    void updateWaveformButtons();

    juce::Button& waveformButton (int index) noexcept
    {
        return *waveformButtons[static_cast<size_t> (juce::jlimit (0, 5, index))];
    }
    ParamHandle& waveformParamHandle() noexcept { return waveformHandle; }
    SubWaveView& view() noexcept { return subWaveView; }

private:
    SubWaveView subWaveView;
    ParamHandle onHandle;
    ParamHandle waveformHandle;
    std::array<std::unique_ptr<juce::TextButton>, 6> waveformButtons;
};
} // namespace synthyboy::ui
