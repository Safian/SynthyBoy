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
// Modified for SynthyBoy (AGPL-3.0-or-later combined work), 2026: the 16 module chain with racks and the mixer buses
// (docs/ui-design/13-new-keys.md 4.10 and 4.12, docs/UPSTREAM.md).

#pragma once

#include "synth_module.h"

#include "synth_constants.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace sbvital {

  class StereoMemory;
  class DistortionModule;
  struct FilterCurveShared;

  /** A module that splits its input into bands that other effects process (the three splitters; the band racks are 3 to 9). */
  class BandSplitterModule : public SynthModule {
    public:
      BandSplitterModule(int num_inputs, int num_outputs) : SynthModule(num_inputs, num_outputs) { }

      virtual int numBands() const = 0;
      virtual int bandRack(int band) const = 0;
      /** Splits `in` into the band buffers (valid until the next call). */
      virtual void split(const poly_float* in, int num_samples) = 0;
      virtual const poly_float* bandBuffer(int band) const = 0;
      /** Sums the processed bands into output (0) (bypassed bands pass their unprocessed signal or nothing, the level is applied). */
      virtual void merge(const poly_float* const* processed, int num_samples) = 0;
  };

  /** A fixed delay (at most kMaxDelay samples) that aligns a signal path with a slower one (latency compensation of the chain). */
  class AlignDelay {
    public:
      static constexpr int kRing = 256;
      static constexpr int kMaxDelay = kRing - 1;

      AlignDelay() : ring_(kRing, poly_float(0.0f)), out_(kMaxBufferSize * kMaxOversample, poly_float(0.0f)) { }

      /** `in` delayed by `delay` samples (`in` itself for 0). A change of the delay clears the history. */
      const poly_float* run(const poly_float* in, int num_samples, int delay) {
        delay = std::max(0, std::min(kMaxDelay, delay));
        if (delay != delay_) {
          delay_ = delay;
          reset();
        }
        if (delay == 0)
          return in;
        for (int i = 0; i < num_samples; ++i) {
          ring_[pos_] = in[i];
          out_[i] = ring_[(pos_ - delay) & (kRing - 1)];
          pos_ = (pos_ + 1) & (kRing - 1);
        }
        return out_.data();
      }

      void reset() { std::fill(ring_.begin(), ring_.end(), poly_float(0.0f)); }

    private:
      std::vector<poly_float> ring_, out_;
      int pos_ = 0, delay_ = 0;
  };

  /** SynthyBoy (docs/ui-design/18-FX-INSTANCES.md): one MORE instance of an effect module, living outside the chain's own 16 modules.
      It owns its effect module and the output level trim of the Vital effects (which the chain keeps in its own controls for the
      modules of instance 0) and has a control table of its own: the controls carry the usual names ("chorus_dry_wet" ...), so the same
      bindings that drive instance 0 drive an instance, through `getControls()` of THIS module. Built (constructed, init, sample rate
      set) on any thread, then handed to the chain with `setPoolModule()` on the audio thread. */
  class FxInstanceModule : public SynthModule {
    public:
      FxInstanceModule(int module_index, const Output* beats_per_second, const Output* keytrack, const int* note_counter);

      int moduleIndex() const { return module_index_; }
      SynthModule* effect() { return effect_; }
      /** The module reads the tempo, the key track and the note counter of the engine from copies of its own: the builder thread runs it (the
          settle blocks) while the audio thread writes the engine's own. `prime ()` sets the copies (any thread, before the hand over), `run ()`
          refreshes them from the engine's (audio thread). */
      void prime(poly_float beats_per_second, poly_float keytrack);
      /** Runs the effect on `audio_in` and applies the level trim; returns the output buffer (valid until the next call). `follow_engine`
          false (the builder thread's settle blocks): the tempo, key track and note counter are NOT read from the engine (the audio thread
          writes them: a data race otherwise, found by ThreadSanitizer, WP-20); the copies set by `prime ()` are used and no note counts. */
      const poly_float* run(const poly_float* audio_in, int num_samples, bool follow_engine = true);
      /** The latency in samples of the module (Bode's Hilbert filter, the limiter of the compressor). */
      int latency() const;
      /** The drawn curves of the PZ SVF (FX filter type), null for the other types. */
      FilterCurveShared* filterCurve();
      /** The distortion (X-Shaper tables), null for the other types. */
      DistortionModule* distortion();

      void process(int) override { }
      void hardReset() override;
      void correctToTime(double seconds) override;
      Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

    private:
      int module_index_;
      SynthModule* effect_;
      Output* level_ = nullptr;
      float last_level_gain_ = 1.0f;
      const Output* live_beats_per_second_;
      const Output* live_keytrack_;
      const int* live_note_counter_;
      Output beats_per_second_ { 1 }, keytrack_ { 1 };   // control rate copies the effect reads
      int note_counter_ = 0, note_counter_offset_ = 0;   // note ons since the module's first block
      bool counted_ = false;
  };

  class ReorderableEffectChain : public SynthModule {
    public:
      enum {
        kAudio,    // the main bus: the sum of the voices that go to the effects
        kOrder,    // (unused since the 16 module chain: the order comes from fx_order_1 .. 16)
        kBus1,     // SynthyBoy: the bus sends of bus 1 and bus 2, and the direct output
        kBus2,
        kDirect,
        kNumInputs
      };

      static constexpr int kNumModules = 16;          // the 9 effects of Vital and the 7 Serum modules
      static constexpr int kFirstSerumModule = constants::kNumEffects;
      static constexpr int kNumRacks = 10;            // Main, Bus 1, Bus 2, then the bands of the splitters
      static constexpr int kNumPoolSlots = 16;        // SynthyBoy: the effect instance pool (FxInstanceModule), racks Main / Bus 1 / Bus 2 only
      static constexpr int kMaxRackItems = kNumModules + kNumPoolSlots;
      static constexpr int kPoolOrderStep = 64;       // a module of instance 0 at position q of fx_order sorts as q * kPoolOrderStep + kPoolOrderBase,
      static constexpr int kPoolOrderBase = 32;       // a slot by its own order (core/include/synthyboy/fx_types.h kFxOrderStep / kFxOrderBase: the same numbers)
      static constexpr int kPoolFadeMs = 10;          // crossfade when the module of a slot changes or is switched on / off
      enum Rack { kMain, kBus1Rack, kBus2Rack, kLhLow, kLhHigh, kLmhLow, kLmhMid, kLmhHigh, kMsMid, kMsSide };
      enum Module { kHyperDimension = 9, kUtility, kBode, kConvolve, kSplitterLh, kSplitterLmh, kSplitterMs };

      ReorderableEffectChain(const Output* beats_per_second, const Output* keytrack);

      virtual void process(int num_samples) override;
      virtual void hardReset() override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }   // never cloned (a mono module); the pool's ring is not copyable

      virtual void correctToTime(double seconds) override;
      void noteTriggered() { ++note_counter_; }   // SynthyBoy: a note on (the retrigger of Hyper and Bode)
      void setOversampleAmount(int oversample) override;

      SynthModule* getEffect(constants::Effect effect) { return effects_[effect]; }
      SynthModule* getModule(int index) { return effects_[index]; }
      const StereoMemory* getEqualizerMemory() { return equalizer_memory_; }
      struct FilterCurveShared* filterFxCurve();

      /** The rack an effect is assigned to, as the chain uses it (a splitter never sits in a band rack). */
      int rackOf(int module) const;
      /** The module at position `position` of the order (a permutation of 0 .. 15; an invalid one is the identity). */
      int moduleAt(int position) const { return effect_order_[position]; }
      // ---- the effect instance pool (audio thread; between two process () calls) ---------------------------------------------------------
      /** Hands the module of a pool slot over (nullptr: the slot is empty). The chain does not own it: the replaced module is faded out
          (10 ms) if it was running and then offered through popRetired (); the creator deletes it there, off the audio thread. */
      void setPoolModule(int slot, FxInstanceModule* module);
      /** on / rack (0 Main, 1 Bus 1, 2 Bus 2) / order (0 .. 1023) of a slot. A module that is switched on or off is faded in or out. */
      void setPoolConfig(int slot, bool on, int rack, int order);
      /** A module the chain has finished with (lock free, one consumer thread); nullptr when there is none. */
      FxInstanceModule* popRetired();
      /** The module the slot has been handed (even while it is off). */
      FxInstanceModule* poolModule(int slot) const { return pool_[slot].module; }
      /** What the creator of an instance needs to build the same module the chain builds (the tempo, the key track, the note counter). */
      const Output* beatsPerSecondOutput() const { return beats_per_second_; }
      const Output* keytrackOutput() const { return keytrack_; }
      const int* noteCounter() const { return &note_counter_; }
      poly_float liveBeatsPerSecond() const { return beats_per_second_->buffer[0]; }
      poly_float liveKeytrack() const { return keytrack_->buffer[0]; }
      /** True while a slot crossfades (its module is changing or switching on / off). */
      bool poolFading(int slot) const { return pool_[slot].fade_left > 0; }

      /** The latency of the chain output in samples of the chain's (oversampled) rate: the longest signal path (Bode's Hilbert filter); every
          other path (the Direct output, the buses, the bands of a splitter, the voices that bypass a rack) is delayed to match it. */
      int latencyFrames() const { return latency_->load(std::memory_order_relaxed); }

    protected:
      struct RackItem {
        int key;   // sort value: q * kPoolOrderStep for the module at position q, the order of a pool slot
        int id;    // 0 .. kNumModules - 1: a module of instance 0; kNumModules + s: pool slot s
      };

      struct PoolSlot {
        FxInstanceModule* module = nullptr;    // handed over by the creator, not owned
        FxInstanceModule* target = nullptr;    // what processes now: `module` while the slot is on, else nullptr (dry)
        FxInstanceModule* leaving = nullptr;   // fading out
        bool leaving_retire = false;           // `leaving` has been replaced by the creator: hand it back when the fade is over
        bool on = false;
        int rack = 0, order = 1023;
        int fade_left = 0, fade_total = 1;
        std::vector<poly_float> mix;           // output of a crossfade
      };

      void updatePoolTarget(int slot);
      void startPoolFade(PoolSlot& s, FxInstanceModule* next);
      void finishPoolFade(PoolSlot& s);
      void retirePoolModule(FxInstanceModule* module);
      const poly_float* runPool(int slot, const poly_float* in, int num_samples);
      void buildRackItems();

      int note_counter_ = 0;
      SynthModule* createEffectModule(int index);
      void refreshConfiguration();
      const poly_float* runRack(int rack, const poly_float* in, int num_samples);
      bool rackHasEnabledEffects(int rack) const;
      int rackLatency(int rack) const;   // the latency a signal gets passing through the rack (enabled modules, the slowest band of a splitter)
      bool isModuleOn(int module) const { return effects_on_[module]->value() > 0.5f; }

      const StereoMemory* equalizer_memory_;
      const Output* beats_per_second_;
      const Output* keytrack_;
      SynthModule* effects_[kNumModules];
      BandSplitterModule* splitters_[kNumModules];
      Value* effects_on_[kNumModules];
      Value* order_controls_[kNumModules];
      Value* rack_controls_[kNumModules];
      int effect_order_[kNumModules];
      int effect_rack_[kNumModules];

      Value* bus_destination_[2];
      Value* bus_fx_bypass_[2];
      Value* main_fx_bypass_;
      Output* bus_level_[2];
      Output* direct_level_;
      // SynthyBoy (docs/ui-design/13-new-keys.md 4.17): the output LEVEL trim of the nine Vital effects (the Serum modules have their own,
      // the FX filter has filter_fx_level); null where the module has none
      Output* level_controls_[kNumModules] = {};
      float last_level_gain_[kNumModules] = {};
      float last_bus_gain_[2];
      float last_direct_gain_;

      PoolSlot pool_[kNumPoolSlots];
      RackItem rack_items_[kNumRacks][kMaxRackItems];
      int rack_item_count_[kNumRacks] = {};
      static constexpr int kRetireRing = 256;
      std::atomic<FxInstanceModule*> retired_[kRetireRing];
      std::atomic<unsigned> retired_write_ { 0 }, retired_read_ { 0 };
      FxInstanceModule* retire_backlog_[kNumPoolSlots * 2] = {};

      std::shared_ptr<std::atomic<int>> latency_;
      AlignDelay main_in_delay_, main_out_delay_, direct_in_delay_, bus_in_delay_[2], bus_out_delay_[2];
      std::shared_ptr<AlignDelay> band_delay_[kNumModules][3];
      std::vector<poly_float> main_mix_, bus_mix_[2], bus_out_[2], direct_mix_;

      JUCE_LEAK_DETECTOR(ReorderableEffectChain)
  };
} // namespace sbvital
