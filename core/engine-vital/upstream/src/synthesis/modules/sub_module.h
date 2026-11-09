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
        kVoicePan,     // SynthyBoy (GROUPA2, WP-07): per voice pan (-1..1) and detune (semitones) of the voice control
        kVoiceDetune,
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

      /** GROUPB WP-05: the pitch tap of the sub source (Ratio SRC of the oscillators) and whether the sub runs. */
      std::shared_ptr<Output> pitchTap() const { return sub_source_->pitchTap(); }
      bool isOn() const { return on_ != nullptr && on_->value() != 0.0f; }

    protected:
      std::shared_ptr<bool> was_on_;
      SubSource* sub_source_;
      Value* on_;

      JUCE_LEAK_DETECTOR(SubModule)
  };

} // namespace sbvital
