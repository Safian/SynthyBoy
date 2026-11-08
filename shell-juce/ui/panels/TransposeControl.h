// SynthyBoy - UI: OCT segments + SEM wheel for `oscN.transpose` (docs/ui-design/02-oscillators.md 3, row OCT / SEM).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transpose = OCT * 12 + SEM (splitTranspose / joinTranspose of model/GestureModel.h). Tap an octave segment (-2 .. +2); the SEM wheel
// is a vertical drag (one semitone per 14 points) with `-` / `+` next to it; long-press opens the standard sheet = numeric entry of the
// total semitones (the alternative the design names). The control takes its own row in the panel (ParamControl::spansRow ()).
#pragma once

#include "../ParamControls.h"

namespace synthyboy::ui
{
class TransposeControl final : public ParamControl
{
public:
    TransposeControl (UiContext& context, const std::string& key, const juce::String& labelOverride = {});

    juce::Point<int> preferredSize() const override;
    bool spansRow() const noexcept override { return true; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int kNumOctSegments = 5; // -2 .. +2
    juce::Rectangle<int> octBounds (int segment) const;
    juce::Rectangle<int> semMinusBounds() const;
    juce::Rectangle<int> semPlusBounds() const;
    juce::Rectangle<int> semWheelBounds() const;

    /** The actions behind the gestures (the tests and the buttons call the same). */
    void setOctave (int oct);
    void stepSemitone (int delta);
    int octave() const;
    int semitone() const;

private:
    enum class Hit { none, oct, minus, plus, wheel };
    Hit hitAt (juce::Point<int> p, int& octSegment) const;

    Hit downHit = Hit::none;
    int downOct = 0, downTranspose = 0;
    juce::Point<float> downPos;
    bool dragging = false;
};
} // namespace synthyboy::ui
