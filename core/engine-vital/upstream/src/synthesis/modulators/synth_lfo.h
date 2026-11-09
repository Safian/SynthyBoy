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
#include "line_generator.h"

#include <cstdint>
#include <memory>

namespace sbvital {

  // SynthyBoy: point modulation of a custom shape, per voice. The shape and the baking are the adapter's (core/shapes); the LFO owns
  // one baked table per voice of its voice pair and bakes it again only when a source it depends on has moved.
  class LfoPointModulation {
    public:
      static constexpr int kMaxSources = 64;      // >= synthyboy::kNumModSources (checked by the adapter)
      static constexpr int kMaxResolution = 2048;

      virtual ~LfoPointModulation() { }

      // Bit s: the value of source s is read by bake () (a voice source through input kPointSource0 + s, a global one from LfoPointState).
      virtual uint64_t sources() const noexcept = 0;
      // Writes `resolution` values of the shape with the point modulation of values[s] applied. Real-time safe.
      virtual void bake(const float* values, int resolution, float* table) const noexcept = 0;
  };

  // The notes as the mono LFOs see them (shared by all LFOs): one group is "the keys held down together"; a note on while no key is held
  // (and no pedal is down) starts a new group. The adapter increments `chunk` before every engine chunk.
  struct LfoNoteGroup {
    uint32_t chunk = 0;
    uint32_t group = 0;
    bool held = false;    // a key is down or the sustain pedal is
  };

  // Shared by an LFO and all its voice clones. Written by the audio thread between blocks only.
  struct LfoPointState {
    bool mono = false;                                // lfoN.mono: in the envelope and loop modes ONE instance serves all voices (A45)
    std::shared_ptr<LfoNoteGroup> note_group;
    const LfoPointModulation* modulation = nullptr;   // null: the LFO reads its one shared table
    uint32_t generation = 0;                          // changes with the modulation / the resolution: baked tables are stale
    int resolution = LfoPointModulation::kMaxResolution;
    uint64_t global_mask = 0;                         // sources whose value is the same for every voice (global_values)
    float global_values[LfoPointModulation::kMaxSources] = {};
    std::shared_ptr<int> budget;                      // tables that may still be baked in this chunk (shared by every LFO)
    int force_after = 4;                              // a table that waited this many chunks is baked regardless of the budget
  };

  class SynthLfo : public Processor {
    public:
      enum {
        kFrequency,
        kPhase,
        kAmplitude,
        kNoteTrigger,
        kSyncType,
        kSmoothMode,
        kFade,
        kSmoothTime,
        kStereoPhase,
        kDelay,
        kNoteCount,
        kDirection,  // SynthyBoy: 0 forward, 1 reverse, 2 ping pong (the shape is read backwards / forth and back within one cycle)
        kHostLock,   // SynthyBoy: 1 = the phase is anchored to the sync clock (the host transport) on every block, not only at a note on
        kSwing,      // SynthyBoy: LFO swing / shuffle 0..1 (Serum LFO swing)
        kPointSource0,                                    // SynthyBoy: source s of a point modulation = input kPointSource0 + s
        kNumInputs = kPointSource0 + LfoPointModulation::kMaxSources
      };

      enum {
        kValue,
        kOscPhase,
        kOscFrequency,
        kNumOutputs
      };

      enum SyncType {
        kTrigger,
        kSync,
        kEnvelope,
        kSustainEnvelope,
        kLoopPoint,
        kLoopHold,
        kNumSyncTypes
      };

      enum {
        kTime,
        kTempo,
        kDottedTempo,
        kTripletTempo,
        kKeytrack,
        kNumSyncOptions
      };

      struct LfoState {
        poly_float delay_time_passed = 0.0f;
        poly_float fade_amplitude = 0.0f;
        poly_float smooth_value = 0.0f;
        poly_float fade_amount = 0.0f;
        poly_float offset = 0.0f;
        poly_float phase = 0.0f;
      };

      // SynthyBoy: the one instance of a mono LFO in the envelope and loop modes. The first clone processed in a chunk (the leader) advances it
      // and stores the result; the other voices start the chunk from it. A new note group restarts it.
      struct MonoMaster {
        LfoState control;
        LfoState audio;
        uint32_t chunk = ~0u;
        uint32_t group = ~0u;
      };

      static constexpr mono_float kMaxPower = 20.0f;
      static constexpr float kHalfLifeRatio = 0.2f;
      static constexpr float kMinHalfLife = 0.0002f;

      // SynthyBoy: LFO swing (lfoN.swing, 13-new-keys.md 4.19 A105). The cycle is a grid of kSwingPairs pairs of slots (8 slots: the
      // 8 steps of a step LFO, or eighths of any shape). Inside every pair the second slot is delayed: the content of the pair is
      // read with the boundary between its two halves moved from 1/2 to 1/2 + swing / 4 of the pair (swing 1 = 3/4, 2/3 is the
      // classic triplet swing at 2/3 of the way). At 0 the phase is untouched (bit identical), at every pair boundary the
      // warped phase meets the straight one, so the cycle length and the loop point do not change.
      static constexpr float kSwingPairs = 4.0f;

      force_inline poly_float applySwing(poly_float phase) const {
        if (swing_ <= 0.0001f)
          return phase;
        poly_float scaled = phase * kSwingPairs;
        poly_float pair = utils::floor(scaled);
        poly_float within = scaled - pair;
        poly_float split = 0.5f + 0.25f * swing_;
        poly_mask first_half = poly_float::lessThan(within, split);
        poly_float warped_first = within * (poly_float(0.5f) / split);
        poly_float warped_second = poly_float(0.5f) + (within - split) * (poly_float(0.5f) / (poly_float(1.0f) - split));
        poly_float warped = utils::maskLoad(warped_second, warped_first, first_half);
        return (pair + warped) * (1.0f / kSwingPairs);
      }

      // SynthyBoy: the direction of the cycle. Applied to the position in the shape, not to the running phase, so a change
      // of direction never jumps the phase itself.
      force_inline poly_float applyDirection(poly_float phase) const {
        if (direction_ == 1)
          return poly_float(1.0f) - phase;
        if (direction_ == 2)
          return poly_float(1.0f) - poly_float::abs(phase * 2.0f - 1.0f);
        return phase;
      }

      force_inline poly_float getValueAtPhase(mono_float* buffer, poly_float resolution,
                                              poly_int max_index, poly_float phase) {
        phase = applySwing(phase);
        phase = applyDirection(phase);
        // SynthyBoy: with a per-voice point modulation every voice reads its own table (and its own resolution)
        if (voice_mode_) {
          resolution = lane_resolution_;
          max_index = lane_max_index_;
        }
        poly_float boost = utils::clamp(phase * resolution, 0.0f, resolution);
        poly_int indices = utils::clamp(utils::toInt(boost), 0, max_index);
        poly_float t = boost - utils::toFloat(indices);

        matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
        matrix value_matrix = voice_mode_ ? utils::getValueMatrix(lane_buffers_, indices) :
                                            utils::getValueMatrix(buffer, indices);
        value_matrix.transpose();

        return interpolation_matrix.multiplyAndSumRows(value_matrix);
      }

      force_inline poly_float getValueAtPhase(poly_float phase) {
        int resolution = source_->resolution();
        return getValueAtPhase(source_->getCubicInterpolationBuffer(), resolution, resolution - 1, phase);
      }

      SynthLfo(LineGenerator* source);

      force_inline poly_mask getReleaseMask() {
        poly_mask trigger_mask = input(kNoteTrigger)->source->trigger_mask;
        poly_float trigger_value = input(kNoteTrigger)->source->trigger_value;
        return trigger_mask & poly_float::equal(trigger_value, kVoiceOff);
      }

      virtual Processor* clone() const override { return new SynthLfo(*this); }

      void process(int num_samples) override;
      void correctToTime(double seconds);

      // SynthyBoy: the state shared with the voice clones (the adapter sets the modulation, the global values and the budget)
      LfoPointState* pointState() { return point_state_.get(); }
      static constexpr int kPointVoices = 2;   // voices of one voice pair (poly_float lanes / 2)

    protected:
      void updatePointTables();
      void beginMono();
      void endMono();
      void processTrigger();
      void processControlRate(int num_samples);

      poly_float processAudioRateEnvelope(int num_samples, poly_float current_phase,
                                          poly_float current_offset, poly_float delta_offset);
      poly_float processAudioRateSustainEnvelope(int num_samples, poly_float current_phase,
                                                 poly_float current_offset, poly_float delta_offset);
      poly_float processAudioRateLfo(int num_samples, poly_float current_phase,
                                     poly_float current_offset, poly_float delta_offset);
      poly_float processAudioRateLoopPoint(int num_samples, poly_float current_phase,
                                           poly_float current_offset, poly_float delta_offset);
      poly_float processAudioRateLoopHold(int num_samples, poly_float current_phase,
                                          poly_float current_offset, poly_float delta_offset);
      void processAudioRate(int num_samples);

      bool was_control_rate_;
      int direction_ = 0;
      mono_float swing_ = 0.0f;
      LfoState control_rate_state_;
      LfoState audio_rate_state_;

      poly_mask held_mask_;
      poly_int trigger_sample_;
      poly_float trigger_delay_;
      LineGenerator* source_;

      std::shared_ptr<double> sync_seconds_;

      // SynthyBoy: mono envelope / loop modes
      std::shared_ptr<MonoMaster> mono_master_;
      bool mono_env_ = false;
      bool mono_leader_ = false;
      bool mono_new_group_ = false;
      poly_mask reset_mask_;

      // SynthyBoy: per-voice point modulation
      std::shared_ptr<LfoPointState> point_state_;
      bool voice_mode_ = false;
      const mono_float* lane_buffers_[4] = {};
      poly_float lane_resolution_;
      poly_int lane_max_index_;
      uint32_t voice_generation_[kPointVoices] = {};
      bool voice_baked_[kPointVoices] = {};
      int voice_waited_[kPointVoices] = {};
      float voice_values_[kPointVoices][LfoPointModulation::kMaxSources] = {};
      mono_float voice_tables_[kPointVoices][LfoPointModulation::kMaxResolution + LineGenerator::kExtraValues] = {};

      JUCE_LEAK_DETECTOR(SynthLfo)
  };
} // namespace sbvital

