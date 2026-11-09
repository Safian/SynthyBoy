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
        kPhase,              // GROUPA1: the start phase at note on, 0 .. 1
        kPhaseContiguous,    // GROUPA1: a new note continues with the phase of the previous one
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
      mono_float last_phase_ = 0.0f;   // the phase of the note that started last (as it was at the end of the last block), for PHASE CONTIGUOUS
      int last_lane_ = -1;             // its lane, followed while it sounds

      JUCE_LEAK_DETECTOR(SubSource)
  };

} // namespace sbvital
