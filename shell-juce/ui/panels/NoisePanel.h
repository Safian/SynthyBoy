// SynthyBoy - UI panel: Noise generator (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../Panel.h"
#include "OscWaveView.h"

namespace synthyboy::ui
{
class NoisePanel final : public Panel
{
public:
    explicit NoisePanel (UiContext& context);
    ~NoisePanel() override;

    juce::String title() const override { return "Noise"; }
    std::vector<std::string> keys() const override;
    void build() override;
    int preferredHeight (int width) const override;
    void resized() override;
    void paint (juce::Graphics& g) override;

    void stepSample (int direction);
    void showSampleMenu();
    void updateSampleButtonText();

    juce::Button& samplePrevButton() noexcept { return prevSampleBtn; }
    juce::Button& sampleNextButton() noexcept { return nextSampleBtn; }
    juce::Button& sampleNameButton() noexcept { return sampleNameBtn; }
    ParamHandle& sampleParamHandle() noexcept { return sampleHandle; }
    OscNoiseView& view() noexcept { return noiseView; }

private:
    OscNoiseView noiseView;
    ParamHandle onHandle;
    ParamHandle sampleHandle;
    juce::TextButton prevSampleBtn { "<" };
    juce::TextButton sampleNameBtn { "White Noise" };
    juce::TextButton nextSampleBtn { ">" };
};
} // namespace synthyboy::ui
