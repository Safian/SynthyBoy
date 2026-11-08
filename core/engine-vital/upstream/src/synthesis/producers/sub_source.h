/* Copyright 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sub oscillator source for SynthyBoy Vital engine (G23 Sub oscillator).
 */

#pragma once

#include "processor.h"
#include "utils.h"
#include "futils.h"
#include "poly_utils.h"

namespace sbvital {

  class SubSource : public Processor {
    public:
      enum {
        kReset,
        kMidi,
        kActiveVoices,
        kLowestNote,
        kOctave,
        kTune,
        kLevel,
        kPan,
        kWaveform,
        kMono,
        kNumInputs
      };

      // the order of the sub.waveform choice
      enum Waveform {
        kSine,
        kRoundedRect,
        kTriangle,
        kSaw,
        kSquare,
        kPulse
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      SubSource();
      virtual ~SubSource() = default;

      void process(int num_samples) override;
      Processor* clone() const override { return new SubSource(*this); }

    private:
      static constexpr float kPulseWidth = 0.25f;
      static poly_float polyBlep(poly_float p, poly_float inc);
      static poly_float squareEdges(poly_float p, poly_float inc, float width);

      poly_float phase_;
      poly_float gate_;

      JUCE_LEAK_DETECTOR(SubSource)
  };

} // namespace sbvital
