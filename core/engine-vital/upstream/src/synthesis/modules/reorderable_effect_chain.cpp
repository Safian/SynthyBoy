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

#include "reorderable_effect_chain.h"

#include "chorus_module.h"
#include "compressor_module.h"
#include "delay_module.h"
#include "distortion_module.h"
#include "equalizer_module.h"
#include "flanger_module.h"
#include "filter_module.h"
#include "phaser_module.h"
#include "reverb_module.h"
#include "serum_fx_modules.h"
#include "synth_strings.h"

namespace sbvital {

  class FilterFxModule : public SynthModule {
    public:
      enum {
        kAudio,
        kKeytrack,
        kNumInputs
      };

      FilterFxModule(const Output* keytrack) : SynthModule(kNumInputs, 1) {
        filter_ = new FilterModule("filter_fx");
        filter_->setCreateOnValue(false);
        filter_->setMono(true);
        addSubmodule(filter_);
        addProcessor(filter_);
        filter_->useOutput(output());
        filter_->plug(&input_, FilterModule::kAudio);
        filter_->plug(keytrack, FilterModule::kKeytrack);
      }

      void processWithInput(const poly_float* audio_in, int num_samples) override {
        utils::copyBuffer(input_.buffer, audio_in, num_samples);
        filter_->process(num_samples);
      }

      void setOversampleAmount(int oversampling) override {
        input_.ensureBufferSize(kMaxBufferSize * oversampling);
        SynthModule::setOversampleAmount(oversampling);
      }

      FilterModule* filter() { return filter_; }

    private:
      FilterModule* filter_;
      Output input_;
  };

  namespace {
    /** A module that does nothing yet: the placeholder of a Serum module whose DSP is not wired (its signal passes). */
    class PassThroughModule : public SynthModule {
      public:
        PassThroughModule() : SynthModule(0, 1) { }
        void processWithInput(const poly_float* audio_in, int num_samples) override {
          utils::copyBuffer(output()->buffer, audio_in, num_samples);
        }
        void process(int) override { }
        Processor* clone() const override { return new PassThroughModule(*this); }
    };

    const char* const kModuleNames[ReorderableEffectChain::kNumModules] = {
      "chorus", "compressor", "delay", "distortion", "eq", "filter_fx", "flanger", "phaser", "reverb",
      "hyperdim", "utility", "bode", "convolve", "splitter_lh", "splitter_lmh", "splitter_ms"
    };

    float dbToGain(float db) {
      return db <= -59.9f ? 0.0f : std::pow(10.0f, db * 0.05f);
    }

    bool isSilent(const poly_float* buffer, int num_samples) {
      poly_float peak = 0.0f;
      for (int i = 0; i < num_samples; ++i)
        peak = utils::max(peak, poly_float::abs(buffer[i]));
      return !(peak.sum() > 0.0f);
    }
  } // namespace

  ReorderableEffectChain::ReorderableEffectChain(const Output* beats_per_second, const Output* keytrack) :
      sbvital::SynthModule(kNumInputs, 1), equalizer_memory_(nullptr),
      beats_per_second_(beats_per_second), keytrack_(keytrack), main_fx_bypass_(nullptr), direct_level_(nullptr),
      last_direct_gain_(1.0f) {
    for (int i = 0; i < kNumModules; ++i) {
      SynthModule* effect_module = createEffectModule(i);
      VITAL_ASSERT(effect_module);

      splitters_[i] = dynamic_cast<BandSplitterModule*>(effect_module);
      addSubmodule(effect_module);
      addProcessor(effect_module);
      effects_on_[i] = createBaseControl(std::string(kModuleNames[i]) + "_on");
      order_controls_[i] = createBaseControl("fx_order_" + std::to_string(i + 1));
      rack_controls_[i] = createBaseControl(std::string(kModuleNames[i]) + "_rack");
      effects_[i] = effect_module;
      effect_order_[i] = i;
      effect_rack_[i] = kMain;
    }

    for (int b = 0; b < 2; ++b) {
      const std::string name = "bus" + std::to_string(b + 1) + "_";
      bus_destination_[b] = createBaseControl(name + "destination");
      bus_fx_bypass_[b] = createBaseControl(name + "fx_bypass");
      bus_level_[b] = createMonoModControl(name + "level");
      last_bus_gain_[b] = 1.0f;
    }
    main_fx_bypass_ = createBaseControl("main_fx_bypass");
    direct_level_ = createMonoModControl("direct_level");

    const size_t size = kMaxBufferSize * kMaxOversample;
    main_mix_.assign(size, poly_float(0.0f));
    direct_mix_.assign(size, poly_float(0.0f));
    for (int b = 0; b < 2; ++b) {
      bus_mix_[b].assign(size, poly_float(0.0f));
      bus_out_[b].assign(size, poly_float(0.0f));
    }
  }

  FilterCurveShared* ReorderableEffectChain::filterFxCurve() {
    return static_cast<FilterFxModule*>(effects_[constants::kFilterFx])->filter()->curveShared();
  }

  SynthModule* ReorderableEffectChain::createEffectModule(int index) {
    switch(index) {
      case constants::kChorus:
        return new ChorusModule(beats_per_second_);
      case constants::kCompressor:
        return new CompressorModule();
      case constants::kDelay:
        return new DelayModule(beats_per_second_);
      case constants::kDistortion:
        return new DistortionModule();
      case constants::kEq: {
        EqualizerModule* eq = new EqualizerModule();
        equalizer_memory_ = eq->getAudioMemory();
        return eq;
      }
      case constants::kFlanger:
        return new FlangerModule(beats_per_second_);
      case constants::kFilterFx:
        return new FilterFxModule(keytrack_);
      case constants::kPhaser:
        return new PhaserModule(beats_per_second_);
      case constants::kReverb:
        return new ReverbModule();
      default: {
        SynthModule* serum = createSerumModule(index, beats_per_second_, &note_counter_);
        return serum != nullptr ? serum : new PassThroughModule();
      }
    }
  }

  void ReorderableEffectChain::setOversampleAmount(int oversample) {
    SynthModule::setOversampleAmount(oversample);
  }

  int ReorderableEffectChain::rackOf(int module) const {
    int rack = static_cast<int>(rack_controls_[module]->value() + 0.5f);
    rack = std::max(0, std::min(kNumRacks - 1, rack));
    if (splitters_[module] != nullptr && rack >= kLhLow)
      rack = kMain; // a splitter cannot sit in the band of another splitter
    return rack;
  }

  // The order is the permutation fx_order_1 .. 16 (the adapter repairs duplicates); anything invalid here is the identity.
  void ReorderableEffectChain::refreshConfiguration() {
    unsigned used = 0;
    bool valid = true;
    int order[kNumModules];
    for (int i = 0; i < kNumModules; ++i) {
      order[i] = static_cast<int>(order_controls_[i]->value() + 0.5f);
      if (order[i] < 0 || order[i] >= kNumModules || (used & (1u << order[i])))
        valid = false;
      else
        used |= 1u << order[i];
    }
    for (int i = 0; i < kNumModules; ++i) {
      effect_order_[i] = valid ? order[i] : i;
      effect_rack_[i] = rackOf(i);
      bool on = isModuleOn(i);
      if (on != effects_[i]->enabled())
        effects_[i]->enable(on);
    }
  }

  bool ReorderableEffectChain::rackHasEnabledEffects(int rack) const {
    for (int m = 0; m < kNumModules; ++m)
      if (effect_rack_[m] == rack && effects_on_[m]->value() > 0.5f)
        return true;
    return false;
  }

  // The signal after the effects of one rack, in the order of the chain. A splitter in the rack splits the signal, runs the effects of
  // its band racks on the bands and sums them again at its place in the order. Returns `in` itself when nothing processes.
  const poly_float* ReorderableEffectChain::runRack(int rack, const poly_float* in, int num_samples) {
    const poly_float* current = in;
    for (int position = 0; position < kNumModules; ++position) {
      const int index = effect_order_[position];
      if (effect_rack_[index] != rack || !isModuleOn(index))
        continue;

      VITAL_ASSERT(utils::isFinite(current, num_samples));
      if (splitters_[index] != nullptr) {
        BandSplitterModule* splitter = splitters_[index];
        splitter->split(current, num_samples);
        const poly_float* processed[3] = { nullptr, nullptr, nullptr };
        for (int band = 0; band < splitter->numBands() && band < 3; ++band)
          processed[band] = runRack(splitter->bandRack(band), splitter->bandBuffer(band), num_samples);
        splitter->merge(processed, num_samples);
        current = splitter->output(0)->buffer;
      }
      else {
        effects_[index]->processWithInput(current, num_samples);
        current = effects_[index]->output(0)->buffer;
      }
    }
    return current;
  }

  void ReorderableEffectChain::process(int num_samples) {
    refreshConfiguration();

    const poly_float* main_in = input(kAudio)->source->buffer;
    const poly_float* bus_in[2] = { input(kBus1)->source->buffer, input(kBus2)->source->buffer };
    const poly_float* direct_in = input(kDirect)->source->buffer;

    int destination[2] = { static_cast<int>(bus_destination_[0]->value() + 0.5f), static_cast<int>(bus_destination_[1]->value() + 0.5f) };
    if (destination[0] == 2 && destination[1] == 2)
      destination[1] = 0;   // both want the other bus: bus 2 goes to the main bus, no feedback
    const bool bypass[2] = { bus_fx_bypass_[0]->value() > 0.5f, bus_fx_bypass_[1]->value() > 0.5f };

    // the buses: sends -> (the other bus) -> rack -> level
    const poly_float* bus_signal[2] = { nullptr, nullptr };
    poly_float* bus_result[2] = { bus_out_[0].data(), bus_out_[1].data() };
    bool bus_live[2] = { false, false };
    float bus_gain[2];
    for (int b = 0; b < 2; ++b)
      bus_gain[b] = dbToGain(bus_level_[b]->buffer[0][0]);
    const int first = destination[1] == 2 ? 1 : 0;   // bus 2 feeds bus 1: bus 2 runs first
    for (int step = 0; step < 2; ++step) {
      const int b = step == 0 ? first : 1 - first;
      const int other = 1 - b;
      const poly_float* source = bus_in[b];
      bool live = !isSilent(bus_in[b], num_samples);
      if (step == 1 && destination[other] == 2 && bus_live[other]) {   // the other bus ran first and sends into this one
        for (int i = 0; i < num_samples; ++i)
          bus_mix_[b][i] = bus_in[b][i] + bus_result[other][i];
        source = bus_mix_[b].data();
        live = true;
      }
      const bool has_effects = !bypass[b] && rackHasEnabledEffects(kBus1Rack + b);
      if (!live && !has_effects) {
        bus_live[b] = false;
        continue;
      }
      const poly_float* processed = bypass[b] ? source : runRack(kBus1Rack + b, source, num_samples);
      const float from = last_bus_gain_[b], step_gain = (bus_gain[b] - from) / static_cast<float>(num_samples);
      float gain = from;
      for (int i = 0; i < num_samples; ++i) {
        gain += step_gain;
        bus_result[b][i] = processed[i] * gain;
      }
      last_bus_gain_[b] = bus_gain[b];
      bus_signal[b] = bus_result[b];
      bus_live[b] = true;
    }

    // the main bus: voices + the buses that go to Main, then the Main rack
    const poly_float* main_signal = main_in;
    bool main_mixed = false;
    for (int b = 0; b < 2; ++b)
      if (bus_live[b] && destination[b] == 0) {
        if (!main_mixed) {
          utils::copyBuffer(main_mix_.data(), main_in, num_samples);
          main_mixed = true;
        }
        for (int i = 0; i < num_samples; ++i)
          main_mix_[i] += bus_signal[b][i];
      }
    if (main_mixed)
      main_signal = main_mix_.data();
    if (main_fx_bypass_->value() <= 0.5f)
      main_signal = runRack(kMain, main_signal, num_samples);

    // the direct output: no rack, scaled by its level, added at the end
    bool direct_live = !isSilent(direct_in, num_samples);
    for (int b = 0; b < 2; ++b)
      direct_live = direct_live || (bus_live[b] && destination[b] == 1);
    poly_float* out = output()->buffer;
    if (!direct_live) {
      utils::copyBuffer(out, main_signal, num_samples);
      last_direct_gain_ = dbToGain(direct_level_->buffer[0][0]);
    }
    else {
      const float target = dbToGain(direct_level_->buffer[0][0]);
      const float step_gain = (target - last_direct_gain_) / static_cast<float>(num_samples);
      float gain = last_direct_gain_;
      for (int i = 0; i < num_samples; ++i) {
        poly_float direct = direct_in[i];
        for (int b = 0; b < 2; ++b)
          if (bus_live[b] && destination[b] == 1)
            direct += bus_signal[b][i];
        gain += step_gain;
        out[i] = main_signal[i] + (gain == 1.0f ? direct : direct * gain);
      }
      last_direct_gain_ = target;
    }

    VITAL_ASSERT(utils::isFinite(out, num_samples));
  }

  void ReorderableEffectChain::processWithInput(const poly_float* audio_in, int num_samples) {
    utils::copyBuffer(output()->buffer, audio_in, num_samples);
  }

  void ReorderableEffectChain::hardReset() {
    for (int i = 0; i < kNumModules; ++i)
      effects_[i]->hardReset();
  }

  void ReorderableEffectChain::correctToTime(double seconds) {
    for (int i = 0; i < kNumModules; ++i)
      effects_[i]->correctToTime(seconds);
  }
} // namespace sbvital
