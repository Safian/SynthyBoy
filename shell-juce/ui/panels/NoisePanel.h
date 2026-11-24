// SynthyBoy - UI panel: Noise generator (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../Panel.h"
#include "OscWaveView.h"
#include "../ContentIO.h"

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

    /** Noise sample file (GROUPA1, WP-04): the file chooser, then the WP-01 flow `loadSampleFile` for the noise source (osc 4). */
    void requestSampleFile();
    FlowResult loadSampleFile (const juce::File& file);
    FlowResult removeSample();
    /** noise.type = Sample (the content sample plays) / Noise (the generator of noise.sample plays). */
    bool usesSample() const;
    /** The status line of the last file action. */
    const juce::String& status() const noexcept { return statusLine; }

    juce::Button& samplePrevButton() noexcept { return prevSampleBtn; }
    juce::Button& sampleNextButton() noexcept { return nextSampleBtn; }
    juce::Button& sampleNameButton() noexcept { return sampleNameBtn; }
    juce::Button& loadButton() noexcept { return loadBtn; }
    juce::Button& removeButton() noexcept { return removeBtn; }
    ParamHandle& sampleParamHandle() noexcept { return sampleHandle; }
    OscNoiseView& view() noexcept { return noiseView; }

private:
    OscNoiseView noiseView;
    ParamHandle onHandle;
    ParamHandle sampleHandle;
    ParamHandle typeHandle;
    juce::String statusLine;
    juce::TextButton loadBtn { "Load..." };
    juce::TextButton removeBtn { "Remove" };
    juce::TextButton prevSampleBtn { "<" };
    juce::TextButton sampleNameBtn { "White Noise" };
    juce::TextButton nextSampleBtn { ">" };
};
} // namespace synthyboy::ui
