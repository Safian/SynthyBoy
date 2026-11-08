/* Copyright 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sub oscillator module for SynthyBoy Vital engine (G23 Sub oscillator).
 */

#pragma once

#include "synth_module.h"
#include "sub_source.h"

namespace sbvital {

  class SubModule : public SynthModule {
    public:
      enum {
        kReset,
        kMidi,
        kActiveVoices,
        kLowestNote,
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      SubModule();
      virtual ~SubModule() = default;

      void process(int num_samples) override;
      void init() override;
      Processor* clone() const override { return new SubModule(*this); }

    protected:
      std::shared_ptr<bool> was_on_;
      SubSource* sub_source_;
      Value* on_;

      JUCE_LEAK_DETECTOR(SubModule)
  };

} // namespace sbvital
