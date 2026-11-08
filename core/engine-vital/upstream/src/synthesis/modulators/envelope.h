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

#include "processor.h"
#include "utils.h"

namespace sbvital {

  /** SynthyBoy: poly legato (master.poly_legato, 13-new-keys.md 4.19 A102). One bus per envelope (env_1 .. env_6) of a voice handler,
   *  shared by all the clones of that envelope (one per voice pair) and by nothing else, so engines and envelopes never see each other.
   *  Every block each Envelope records the state of one of its voices that is held (attack, hold or decay stage: the key is down or
   *  the pedal holds it) in `writing`; beginBlock () publishes it as `heard`. A voice that is triggered by a note on while `heard` is
   *  valid and poly legato is on continues from that state instead of starting at 0. Audio thread only, no allocation. */
  struct PolyLegatoBus {
    struct Source {
      float state = 0.0f, position = 0.0f, value = 0.0f, start_value = 0.0f;
      bool valid = false;
    };

    Source heard, writing;

    void beginBlock() {
      heard = writing;
      writing = Source();
    }
  };

  class Envelope : public Processor {
    public:
      enum {
        kDelay,
        kAttack,
        kAttackPower,
        kHold,
        kDecay,
        kDecayPower,
        kSustain,
        kRelease,
        kReleasePower,
        kTrigger,
        kTriggerAlways,   // SynthyBoy: every note event, also the legato note ons that kTrigger leaves out
        kLegatoInverted,  // SynthyBoy: 1 = this envelope retriggers on legato note ons (reads kTriggerAlways)
        kPolyLegato,      // SynthyBoy: 1 = a note on continues from the state of a held voice (needs the bus)
        kNumInputs
      };

      enum ProcessorOutput {
        kValue,
        kPhase,
        kNumOutputs
      };

      Envelope();
      virtual ~Envelope() { }

      virtual Processor* clone() const override { return new Envelope(*this); }
      virtual void process(int num_samples) override;
      void setLegatoBus(PolyLegatoBus* bus) { legato_bus_ = bus; }

    private:
      force_inline const Output* triggerSource() const {
        return input(kLegatoInverted)->at(0)[0] > 0.5f ? input(kTriggerAlways)->source : input(kTrigger)->source;
      }

      force_inline bool polyLegatoOn() const {
        return legato_bus_ != nullptr && input(kPolyLegato)->at(0)[0] > 0.5f;
      }

      /** The voices of `mask` that start with a note on continue from the held voice of the bus (see PolyLegatoBus). */
      force_inline void continueHeldVoice(poly_mask mask) {
        const PolyLegatoBus::Source& src = legato_bus_->heard;
        poly_state_ = utils::maskLoad(poly_state_, poly_float(src.state), mask);
        position_ = utils::maskLoad(position_, poly_float(src.position), mask);
        value_ = utils::maskLoad(value_, poly_float(src.value), mask);
        start_value_ = utils::maskLoad(start_value_, poly_float(src.start_value), mask);
      }

      /** Publishes the first voice that is in its attack, hold or decay stage. */
      force_inline void recordHeldVoice(const poly_float& position, const poly_float& value) {
        for (int i = 0; i < poly_float::kSize; ++i) {
          const float state = poly_state_[i];
          if (state == kVoiceOn || state == kVoiceHold || state == kVoiceDecay) {
            PolyLegatoBus::Source& dst = legato_bus_->writing;
            dst.state = state;
            dst.position = position[i];
            dst.value = value[i];
            dst.start_value = start_value_[i];
            dst.valid = true;
            return;
          }
        }
      }

      void processControlRate(int num_samples);
      void processAudioRate(int num_samples);

      poly_float processSection(poly_float* audio_out, int from, int to,
                                poly_float power, poly_float delta_power,
                                poly_float position, poly_float delta_position,
                                poly_float start, poly_float end, poly_float delta_end);

      poly_float current_value_;

      poly_float position_;
      poly_float value_;
      poly_float poly_state_;
      poly_float start_value_;

      poly_float attack_power_;
      poly_float decay_power_;
      poly_float release_power_;
      poly_float sustain_;
      PolyLegatoBus* legato_bus_ = nullptr;

      JUCE_LEAK_DETECTOR(Envelope)
  };
} // namespace sbvital

