/* Copyright 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GROUPB (WP-06, docs/ui-design/13-new-keys.md 4.34): the output of an oscillator, the noise, the sub or a filter as a modulation source.
 * The module that makes the signal writes the last sample of its block (after level and pan) into element 0 of a tap output; this
 * control rate processor turns the stereo pair into one value per voice (both channels carry the mean) and holds it for the block.
 */

#pragma once

#include "processor.h"
#include "utils.h"
#include "poly_utils.h"

namespace sbvital {

  class SourceTap : public Processor {
    public:
      enum { kInput, kNumInputs };
      enum { kValue, kNumOutputs };

      SourceTap() : Processor(kNumInputs, kNumOutputs, true) { }
      virtual ~SourceTap() = default;

      Processor* clone() const override { return new SourceTap(*this); }

      void process(int num_samples) override {
        (void)num_samples;
        const poly_float v = input(kInput)->at(0);
        output()->buffer[0] = (v + utils::swapStereo(v)) * 0.5f;
      }

    private:
      JUCE_LEAK_DETECTOR(SourceTap)
  };

} // namespace sbvital
