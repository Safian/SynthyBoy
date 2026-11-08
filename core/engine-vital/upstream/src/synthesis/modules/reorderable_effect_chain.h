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

#include <vector>

namespace sbvital {

  class StereoMemory;

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
      enum Rack { kMain, kBus1Rack, kBus2Rack, kLhLow, kLhHigh, kLmhLow, kLmhMid, kLmhHigh, kMsMid, kMsSide };
      enum Module { kHyperDimension = 9, kUtility, kBode, kConvolve, kSplitterLh, kSplitterLmh, kSplitterMs };

      ReorderableEffectChain(const Output* beats_per_second, const Output* keytrack);

      virtual void process(int num_samples) override;
      virtual void hardReset() override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      virtual Processor* clone() const override { return new ReorderableEffectChain(*this); }

      virtual void correctToTime(double seconds) override;
      void setOversampleAmount(int oversample) override;

      SynthModule* getEffect(constants::Effect effect) { return effects_[effect]; }
      SynthModule* getModule(int index) { return effects_[index]; }
      const StereoMemory* getEqualizerMemory() { return equalizer_memory_; }
      struct FilterCurveShared* filterFxCurve();

      /** The rack an effect is assigned to, as the chain uses it (a splitter never sits in a band rack). */
      int rackOf(int module) const;
      /** The module at position `position` of the order (a permutation of 0 .. 15; an invalid one is the identity). */
      int moduleAt(int position) const { return effect_order_[position]; }

    protected:
      SynthModule* createEffectModule(int index);
      void refreshConfiguration();
      const poly_float* runRack(int rack, const poly_float* in, int num_samples);
      bool rackHasEnabledEffects(int rack) const;
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
      float last_bus_gain_[2];
      float last_direct_gain_;

      std::vector<poly_float> main_mix_, bus_mix_[2], bus_out_[2], direct_mix_;

      JUCE_LEAK_DETECTOR(ReorderableEffectChain)
  };
} // namespace sbvital
