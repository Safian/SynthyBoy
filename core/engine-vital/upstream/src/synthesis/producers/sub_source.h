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
      poly_float phase_;

      JUCE_LEAK_DETECTOR(SubSource)
  };

} // namespace sbvital
