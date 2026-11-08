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
#include "synth_constants.h"
#include "line_generator.h"

namespace sbvital {
  class ModulationConnectionProcessor : public SynthModule {
    public:
      enum {
        kModulationInput,
        kModulationAmount,
        kModulationPower,
        kReset,
        kAuxInput,  // SynthyBoy: the aux source of the slot (docs/ui-design/13-new-keys.md 4.6), plugged by the adapter while it is set
        kNumInputs
      };

      enum {
        kModulationOutput,
        kModulationPreScale,
        kModulationSource,
        kNumOutputs
      };

      ModulationConnectionProcessor(int index);
      virtual ~ModulationConnectionProcessor() { }

      void init() override;
      void process(int num_samples) override;
      void processAudioRate(int num_samples, const Output* source);
      void processAudioRateLinear(int num_samples, const Output* source);
      void processAudioRateRemapped(int num_samples, const Output* source);
      void processAudioRateMorphed(int num_samples, const Output* source, poly_float power);
      void processAudioRateRemappedAndMorphed(int num_samples, const Output* source, poly_float power);
      void processControlRate(const Output* source);

      virtual Processor* clone() const override { return new ModulationConnectionProcessor(*this); }

      void initializeBaseValue(Value* base_value) { current_value_ = base_value; }
      void initializeMapping() { map_generator_->initLinear(); }

      mono_float currentBaseValue() const { return current_value_->value(); }
      void setBaseValue(mono_float value) { current_value_->set(value); }

      bool isPolyphonicModulation() const { return polyphonic_; }
      void setPolyphonicModulation(bool polyphonic) { polyphonic_ = polyphonic; }
      bool isBipolar() const { return bipolar_->value() != 0.0f; }
      void setBipolar(bool bipolar) { bipolar_->set(bipolar ? 1.0f : 0.0f); }
      bool isStereo() const { return stereo_->value() != 0.0f; }
      void setStereo(bool stereo) { stereo_->set(stereo ? 1.0f : 0.0f); }
      bool isBypassed() const { return bypass_->value() != 0.0f; }
      force_inline void setDestinationScale(mono_float scale) { *destination_scale_ = scale; }
      force_inline int index() const { return index_; }

      LineGenerator* lineMapGenerator() { return map_generator_.get(); }

      // SynthyBoy: out = curve (source) * amount * aux * (aux_output / 100), aux = curve_aux (invert ? 1 - a : a) with a the aux source
      // mapped to 0..1 (a bipolar source as (x + 1) / 2). `active` false: the factor is 1. The values are set between two blocks.
      void setAux(bool active, bool bipolar, bool invert, mono_float power, mono_float output) {
        extras_->aux_active = active;
        extras_->aux_bipolar = bipolar;
        extras_->aux_invert = invert;
        extras_->aux_power = power;
        extras_->aux_output = output;
      }
      // SynthyBoy: the velocity / note curves (Q17) glide on legato notes: the source moves to its new value over `seconds`
      void setGlide(bool active, mono_float seconds) {
        extras_->glide_active = active;
        extras_->glide_seconds = seconds;
      }

    protected:
      int index_;
      bool polyphonic_;
      Value* current_value_;
      Value* bipolar_;
      Value* stereo_;
      Value* bypass_;

      poly_float power_;
      poly_float modulation_amount_;
      // settings shared by the clones of the processor (every voice has its own copy of the processor, like destination_scale_)
      struct Extras {
        bool aux_active = false, aux_bipolar = false, aux_invert = false;
        mono_float aux_power = 0.0f, aux_output = 1.0f;
        bool glide_active = false;
        mono_float glide_seconds = 0.0f;
      };
      std::shared_ptr<Extras> extras_ = std::make_shared<Extras>();
      poly_float aux_factor_ = 1.0f;
      poly_float glide_value_ = 0.0f;
      mono_float block_seconds_ = 0.0f;
      poly_float computeAuxFactor() const;

      std::shared_ptr<mono_float> destination_scale_;
      mono_float last_destination_scale_;
      std::shared_ptr<LineGenerator> map_generator_;

      JUCE_LEAK_DETECTOR(ModulationConnectionProcessor)
  };
} // namespace sbvital

