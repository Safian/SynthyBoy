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
#include "oscillator_module.h"
#include "sample_module.h"
#include "sub_module.h"

namespace sbvital {
  class ProducersModule : public SynthModule {
    public:
      enum {
        kReset,
        kRetrigger,
        kMidi,
        kActiveVoices,
        kNoteCount,
        kMute1,   // SynthyBoy: per voice 1 = the oscillator is silenced for this note (key / velocity mapping), 0 unplugged
        kMute2,
        kMute3,
        kFold1,   // SynthyBoy: per voice semitones added to the note the oscillator plays (key fold)
        kFold2,
        kFold3,
        kNotePressed,  // SynthyBoy: 1 while the key of the voice is held
        // SynthyBoy Block B14:
        kRawMidi,
        kVoicePhaseLegato,
        kLowestNote,   // SynthyBoy: lowest active MIDI note (for mono sub)
        kNumInputs
      };

      enum {
        kToFilter1,
        kToFilter2,
        kRawOut,
        kDirectOut,
        kBus1Out,   // SynthyBoy: the bus sends of the oscillators and the noise (docs/ui-design/13-new-keys.md 4.10)
        kBus2Out,
        kRawFree,    // SynthyBoy B21: the Main / Direct channels whose ENV 1 button is off (not multiplied by the amplitude envelope)
        kDirectFree,
        kNumOutputs
      };

      // SynthyBoy: destinations appended to the five of the engine: a continuous blend of the two filters, and none (modulation source only)
      static constexpr int kDestinationFilterBlend = 5;
      static constexpr int kDestinationNone = 6;
      static constexpr int kNumSources = kNumOscillators + 2; // osc1..3, noise, sub
      static constexpr int kSourceSub = kNumOscillators + 1; // 4

      static force_inline int getFirstModulationIndex(int index) {
        return index == 0 ? 1 : 0;
      }

      static force_inline int getSecondModulationIndex(int index) {
        return index == 1 ? 2 : (getFirstModulationIndex(index) + 1);
      }

      ProducersModule();
      virtual ~ProducersModule() { }

      void process(int num_samples) override;
      void applyMute(int osc, int num_samples);
      void init() override;
      virtual Processor* clone() const override { return new ProducersModule(*this); }

      Wavetable* getWavetable(int index) {
        return oscillators_[index]->getWavetable();
      }

      SampleOscShared* getSampleShared(int index) { return oscillators_[index]->sampleShared(); }

      Sample* getSample() { return sampler_->getSample(); }
      Output* samplePhaseOutput() { return sampler_->getPhaseOutput(); }
      void setFilter1On(const Value* on) { filter1_on_ = on; }
      void setFilter2On(const Value* on) { filter2_on_ = on; }
      void setNoteTable(const mono_float* table) {
        for (int i = 0; i < kNumOscillators; ++i)
          oscillators_[i]->oscillator()->setNoteTable(table);
      }

    protected:
      void routeSource(int source, const poly_float* buffer, int destination, int num_samples, bool filter1_on, bool filter2_on,
                       poly_float* filter1_output, poly_float* filter2_output, poly_float* raw_output, poly_float* direct_output);

      bool isFilter1On() { return filter1_on_ == nullptr || filter1_on_->value() != 0.0f; }
      bool isFilter2On() { return filter2_on_ == nullptr || filter2_on_->value() != 0.0f; }
      OscillatorModule* oscillators_[kNumOscillators];
      Value* oscillator_destinations_[kNumOscillators];
      Value* map_warp_[kNumOscillators] = { nullptr, nullptr, nullptr };
      Value* sample_destination_;
      SampleModule* sampler_;
      Value* sub_destination_;
      SubModule* sub_module_;

      const Value* filter1_on_;
      const Value* filter2_on_;

      // SynthyBoy: per source (oscillators 1 to 3, noise, sub): the filter blend and the two bus sends (control-rate, ramped over the block)
      Output* filter_blend_[kNumSources] = {};
      Output* bus1_send_[kNumSources] = {};
      Output* bus2_send_[kNumSources] = {};
      Value* env1_[kNumSources] = {};   // SynthyBoy B21: the ENV 1 button of the channel (1 = on, the amplitude envelope applies)
      poly_float last_blend_[kNumSources];
      poly_float last_send1_[kNumSources];
      poly_float last_send2_[kNumSources];

      JUCE_LEAK_DETECTOR(ProducersModule)
  };
} // namespace sbvital

