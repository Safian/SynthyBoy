/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */
// Modified for SynthyBoy (AGPL-3.0-or-later combined work), 2026

#pragma once

#include "synth_module.h"
#include "synth_oscillator.h"
#include "sample_oscillator.h"

namespace sbvital {
  class Wavetable;

  class OscillatorModule : public SynthModule {
    public:
      enum {
        kReset,
        kRetrigger,
        kMidi,
        kActiveVoices,
        kFold,
        kNotePressed,  // SynthyBoy: 1 while the key of the voice is held (sample loop exit on release)
        // SynthyBoy Block B14: Pitch and unison modes, phase legato
        kRawMidi,
        kVoicePhaseLegato,
        kNoteCount,
        kVoicePan,     // SynthyBoy (GROUPA2, WP-07): per voice pan offset of the voice control, -1..1
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      OscillatorModule(std::string prefix = "");
      virtual ~OscillatorModule() { }

      void process(int num_samples) override;
      void init() override;
      virtual Processor* clone() const override { return new OscillatorModule(*this); }

      Wavetable* getWavetable() { return wavetable_.get(); }
      force_inline SynthOscillator* oscillator() { return oscillator_; }
      force_inline SampleOscillator* sampleOscillator() { return sample_oscillator_; }
      SampleOscShared* sampleShared() { return sample_shared_.get(); }
      SynthOscillator::DistortionType getDistortionType() {
        int val = distortion_type_->value();
        return static_cast<SynthOscillator::DistortionType>(val);
      }
      SynthOscillator::DistortionType getDistortionType2() {
        int val = distortion2_type_->value();
        return static_cast<SynthOscillator::DistortionType>(val);
      }

    protected:
      std::string prefix_;
      std::shared_ptr<Wavetable> wavetable_;
      std::shared_ptr<bool> was_on_;
      std::shared_ptr<SampleOscShared> sample_shared_;

      // SynthyBoy: oscN.type crossfade. The sample oscillator writes into its own outputs; the module copies them into its outputs (or
      // blends them with the wavetable oscillator during the 10 ms fade that follows a change of the type under a sounding note).
      // The voices are processed by clones of this module one after the other: the processor flags (enabled) and the Output buffers are
      // shared between the clones, the counters below are the clone's own. The processors are switched at the START of the block after the
      // fade ends, so every clone of the block that ends it still blends.
      std::shared_ptr<Output> sample_raw_, sample_levelled_;
      bool type_sample_ = false;     // the type that is (or is being faded to)
      bool fading_ = false;
      bool sounding_ = false;        // the output of the last block was not silent
      int fade_position_ = 0;
      int fade_length_ = 1;

      Value* on_;
      SynthOscillator* oscillator_;
      SampleOscillator* sample_oscillator_;
      Value* distortion_type_;
      Value* distortion2_type_;
      Value* type_;

      JUCE_LEAK_DETECTOR(OscillatorModule)
  };
} // namespace sbvital

