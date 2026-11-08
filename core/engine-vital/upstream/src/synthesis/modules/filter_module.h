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
#include "formant_module.h"

namespace sbvital {
  class CombModule;
  class DigitalSvf;
  class DiodeFilter;
  class DirtyFilter;
  class Interpolate;
  class LadderFilter;
  class PhaserFilter;
  class SallenKeyFilter;
  class SerumFilterBank;      // SynthyBoy: the filter models of state version 3
  class StereoCutoffOffset;

  class FilterModule : public SynthModule {
    public:
      enum {
        kAudio,
        kReset,
        kKeytrack,
        kMidi,
        kNumInputs
      };

      enum {
        kOutput,
        kBus1Send,   // SynthyBoy: the output of the filter times its bus 1 send (docs/ui-design/13-new-keys.md 4.10)
        kBus2Send,
        kNumOutputs
      };

      FilterModule(std::string prefix = "");
      virtual ~FilterModule() { }

      void process(int num_samples) override;
      void setCreateOnValue(bool create_on_value) { create_on_value_ = create_on_value; }
      void setMono(bool mono);
      Output* createModControl(std::string name, bool audio_rate = false, bool smooth_value = false,
                               Output* internal_modulation = nullptr);
      void init() override;
      void hardReset() override;
      virtual Processor* clone() const override {
        FilterModule* newModule = new FilterModule(*this);
        newModule->last_model_ = -1;
        return newModule;
      }

      const Value* getOnValue() { return on_; }
      const Value* getDestinationValue() { return destination_; }
      struct FilterCurveShared* curveShared();   // SynthyBoy: the drawn curve of the PZ SVF

    protected:
      void setModel(int new_model);

      int last_model_;
      bool was_on_;
      std::string prefix_;
      bool create_on_value_;
      bool mono_;

      Value* on_;
      Value* destination_ = nullptr;     // SynthyBoy: 0 effects only, 1 direct out, 2 none (the two voice filters only)
      Output* bus1_send_ = nullptr;
      Output* bus2_send_ = nullptr;
      poly_float bus1_gain_ = 0.0f;
      poly_float bus2_gain_ = 0.0f;
      Value* filter_model_;
      poly_float mix_;

      Output* filter_mix_;

      CombModule* comb_filter_;
      DigitalSvf* digital_svf_;
      DiodeFilter* diode_filter_;
      DirtyFilter* dirty_filter_;
      FormantModule* formant_filter_;
      LadderFilter* ladder_filter_;
      PhaserFilter* phaser_filter_;
      SallenKeyFilter* sallen_key_filter_;
      SerumFilterBank* serum_filter_ = nullptr;
      StereoCutoffOffset* cutoff_offset_ = nullptr;
      Output* level_ = nullptr;
      poly_float level_gain_ = 1.0f;

      JUCE_LEAK_DETECTOR(FilterModule)
  };
} // namespace sbvital

