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

#include "producers_module.h"

namespace sbvital {
  ProducersModule::ProducersModule() :
      SynthModule(kNumInputs, kNumOutputs), sample_destination_(nullptr),
      sub_destination_(nullptr), filter1_on_(nullptr), filter2_on_(nullptr) {
    for (int i = 0; i < kNumOscillators; ++i) {
      std::string number = std::to_string(i + 1);
      oscillators_[i] = new OscillatorModule("osc_" + number);
      addSubmodule(oscillators_[i]);
      addProcessor(oscillators_[i]);
      oscillators_[i]->enable(false);
      oscillator_destinations_[i] = nullptr;
    }

    sampler_ = new SampleModule();
    addSubmodule(sampler_);
    addProcessor(sampler_);
    sampler_->enable(false);

    sub_module_ = new SubModule();
    addSubmodule(sub_module_);
    addProcessor(sub_module_);
    sub_module_->enable(false);
  }

  void ProducersModule::init() {
    for (int i = 0; i < kNumOscillators; ++i) {
      std::string number = std::to_string(i + 1);
      oscillator_destinations_[i] = createBaseControl("osc_" + number + "_destination");

      oscillators_[i]->useInput(input(kReset), OscillatorModule::kReset);
      oscillators_[i]->useInput(input(kRetrigger), OscillatorModule::kRetrigger);
      // SynthyBoy: the key fold offset of the oscillator mapping (0 unless it folds the note) and the "warp" switch
      map_warp_[i] = createBaseControl("osc_" + number + "_map_warp");
      ratio_src_[i] = createBaseControl("osc_" + number + "_ratio_src");
      oscillators_[i]->useInput(input(kMidi), OscillatorModule::kMidi);
      oscillators_[i]->useInput(input(kFold1 + i), OscillatorModule::kFold);
      oscillators_[i]->useInput(input(kNotePressed), OscillatorModule::kNotePressed);
      oscillators_[i]->useInput(input(kActiveVoices), OscillatorModule::kActiveVoices);
      oscillators_[i]->useInput(input(kRawMidi), OscillatorModule::kRawMidi);
      oscillators_[i]->useInput(input(kVoicePhaseLegato), OscillatorModule::kVoicePhaseLegato);
      oscillators_[i]->useInput(input(kNoteCount), OscillatorModule::kNoteCount);
      oscillators_[i]->useInput(input(kVoicePan1 + i), OscillatorModule::kVoicePan);
    }

    sample_destination_ = createBaseControl("sample_destination");
    sub_destination_ = createBaseControl("sub_destination");
    for (int i = 0; i < kNumSources; ++i) {
      std::string prefix;
      if (i < kNumOscillators)
        prefix = "osc_" + std::to_string(i + 1) + "_";
      else if (i == kNumOscillators)
        prefix = "sample_";
      else
        prefix = "sub_";
      filter_blend_[i] = createPolyModControl(prefix + "filter_blend");
      bus1_send_[i] = createPolyModControl(prefix + "bus1_send");
      bus2_send_[i] = createPolyModControl(prefix + "bus2_send");
      env1_[i] = createBaseControl(prefix + "env1");
      last_blend_[i] = 0.0f;
      last_send1_[i] = 0.0f;
      last_send2_[i] = 0.0f;
    }
    sampler_->useInput(input(kReset), SampleModule::kReset);
    sampler_->useInput(input(kNoteCount), SampleModule::kNoteCount);
    sampler_->useInput(input(kMidi), SampleModule::kMidi);
    sampler_->useInput(input(kVoicePanNoise), SampleModule::kVoicePan);
    sampler_->useInput(input(kVoiceDetuneNoise), SampleModule::kVoiceDetune);

    sub_module_->useInput(input(kReset), SubModule::kReset);
    sub_module_->useInput(input(kMidi), SubModule::kMidi);
    sub_module_->useInput(input(kActiveVoices), SubModule::kActiveVoices);
    sub_module_->useInput(input(kLowestNote), SubModule::kLowestNote);
    sub_module_->useInput(input(kVoicePanSub), SubModule::kVoicePan);
    sub_module_->useInput(input(kVoiceDetuneSub), SubModule::kVoiceDetune);

    SynthModule::init();
    for (int i = 0; i < kNumOscillators; ++i) {
      int index1 = getFirstModulationIndex(i);
      int index2 = getSecondModulationIndex(i);
      oscillators_[i]->oscillator()->setFirstOscillatorOutput(oscillators_[index1]->output(OscillatorModule::kRaw));
      oscillators_[i]->oscillator()->setSecondOscillatorOutput(oscillators_[index2]->output(OscillatorModule::kRaw));
      oscillators_[i]->oscillator()->setSampleOutput(sampler_->output(SampleModule::kRaw));
      oscillators_[i]->sampleOscillator()->setFirstOscillatorOutput(oscillators_[index1]->output(OscillatorModule::kRaw));
      oscillators_[i]->sampleOscillator()->setSecondOscillatorOutput(oscillators_[index2]->output(OscillatorModule::kRaw));
      oscillators_[i]->sampleOscillator()->setSampleOutput(sampler_->output(SampleModule::kRaw));
    }
  }

  // GROUPB WP-06: Sub, Filter 1 and Filter 2 as warp sources. The cells of the oscillators are shared with the clones of the voice pairs (set at every block: the filter
  // taps are plugged after this module is initialised); the filter taps are the Feedback nodes of the voice router (the previous block of the filter, per voice pair), the sub is processed before the oscillators.
  void ProducersModule::connectWarpSources() {
    for (int i = 0; i < kNumOscillators; ++i) {
      SynthOscillator* osc = oscillators_[i]->oscillator();
      osc->setExtraWarpSource(1, sub_module_->output(SubModule::kRaw));
      osc->setExtraWarpSource(2, input(kFilter1Tap)->source);
      osc->setExtraWarpSource(3, input(kFilter2Tap)->source);
      SampleOscillator* so = oscillators_[i]->sampleOscillator();
      so->setExtraWarpSource(1, sub_module_->output(SubModule::kRaw));
      so->setExtraWarpSource(2, input(kFilter1Tap)->source);
      so->setExtraWarpSource(3, input(kFilter2Tap)->source);
    }
  }

  // SynthyBoy (GROUPB, WP-05): the Ratio SRC of oscillator `osc` as 0 (the note of the voice) or 1 .. 4 (oscillator 1 .. 3, sub). A source that
  // is the oscillator itself, that is switched off, or whose own chain of sources leads back to this oscillator (A and B tuned to each other)
  // is the note of the voice.
  int ProducersModule::effectiveRatioSource(int osc) const {
    auto source_of = [this](int o) {
      if (ratio_src_[o] == nullptr)
        return 0;
      int src = static_cast<int>(ratio_src_[o]->value() + 0.5f);
      return src < 0 || src > kNumOscillators + 1 ? 0 : src;
    };
    int src = source_of(osc);
    if (src == 0)
      return 0;
    if (src == kNumOscillators + 1)
      return sub_module_->isOn() ? src : 0;
    if (src - 1 == osc || !oscillators_[src - 1]->isOn())
      return 0;
    int walk = src - 1;
    for (int hops = 0; hops < kNumOscillators; ++hops) {
      int next = source_of(walk);
      if (next == 0 || next == kNumOscillators + 1)
        return src;
      walk = next - 1;
      if (walk == osc)
        return 0;
    }
    return 0;
  }

  // Hands the pitch tap of the source to the oscillator, once the source has run in this block (the taps are shared by the voice pairs,
  // so a tap that was not written by this pair's own pass would hold another pair's pitch).
  void ProducersModule::connectRatioSource(int osc, const bool* processed) {
    int src = effectiveRatioSource(osc);
    std::shared_ptr<Output> tap;
    if (src == kNumOscillators + 1)
      tap = sub_module_->pitchTap();
    else if (src > 0 && processed[src - 1])
      tap = oscillators_[src - 1]->pitchTap();
    oscillators_[osc]->oscillator()->setRatioSource(tap.get());
  }

  // SynthyBoy: silences the voices whose note or velocity is outside the oscillator's mapping. The levelled output always; the raw
  // output (what the other oscillators use as FM / RM / sync source) too, unless "warp" keeps the oscillator running silently.
  void ProducersModule::applyMute(int i, int num_samples) {
    poly_mask muted = poly_float::greaterThan(input(kMute1 + i)->at(0), 0.5f);
    if (!muted.anyMask())
      return;

    poly_float scale = utils::maskLoad(poly_float(1.0f), poly_float(0.0f), muted);
    poly_float* levelled = oscillators_[i]->output(OscillatorModule::kLevelled)->buffer;
    for (int s = 0; s < num_samples; ++s)
      levelled[s] *= scale;

    if (map_warp_[i] == nullptr || map_warp_[i]->value() < 0.5f) {
      poly_float* raw = oscillators_[i]->output(OscillatorModule::kRaw)->buffer;
      for (int s = 0; s < num_samples; ++s)
        raw[s] *= scale;
    }
  }

  void ProducersModule::process(int num_samples) {
    SynthModule::process(num_samples);
    connectWarpSources();

    getLocalProcessor(sampler_)->process(num_samples);
    getLocalProcessor(sub_module_)->process(num_samples);

    // SynthyBoy: an oscillator waits for the sources of both of its warp slots
    bool needs_first[kNumOscillators], needs_second[kNumOscillators];
    bool processed[kNumOscillators];
    int ratio_source[kNumOscillators];
    for (int i = 0; i < kNumOscillators; ++i) {
      ratio_source[i] = effectiveRatioSource(i);
      SynthOscillator::DistortionType type1 = oscillators_[i]->getDistortionType();
      SynthOscillator::DistortionType type2 = oscillators_[i]->getDistortionType2();
      needs_first[i] = SynthOscillator::isFirstModulation(type1) || SynthOscillator::isFirstModulation(type2);
      needs_second[i] = SynthOscillator::isSecondModulation(type1) || SynthOscillator::isSecondModulation(type2);
      processed[i] = false;
    }
   
    int num_processed = 0;
    int index = 0;
    for (int i = 0; i < kNumOscillators * kNumOscillators && num_processed < kNumOscillators; ++i) {
      OscillatorModule* module = oscillators_[index];
      int first_source = getFirstModulationIndex(index);
      int second_source = getSecondModulationIndex(index);
      const int ratio = ratio_source[index];
      if ((!needs_first[index] || processed[first_source]) &&
          (!needs_second[index] || processed[second_source]) &&
          (ratio < 1 || ratio > kNumOscillators || processed[ratio - 1]) &&
          !processed[index]) {
        num_processed++;
        processed[index] = true;
        connectRatioSource(index, processed);
        getLocalProcessor(module)->process(num_samples);
        applyMute(index, num_samples);
      }
      index = (index + 1) % kNumOscillators;
    }

    // Modified for SynthyBoy: oscillators that modulate each other (A is FM / RM of B and B of A) never become "ready" in the loop
    // above and were not processed at all: their output buffers kept whatever they held (uninitialised memory at the start, NaN in
    // the filters behind them). They are processed now, with the other oscillator's output of the previous block (docs/UPSTREAM.md).
    for (int i = 0; i < kNumOscillators; ++i)
      if (!processed[i]) {
        connectRatioSource(i, processed);
        getLocalProcessor(oscillators_[i])->process(num_samples);
        processed[i] = true;
        applyMute(i, num_samples);
      }

    // GROUPB WP-06: the taps of the sources (the last sample of the levelled output; SourceTap holds it for the block)
    for (int i = 0; i < kNumOscillators; ++i)
      output(kTapOsc1 + i)->buffer[0] = oscillators_[i]->output(OscillatorModule::kLevelled)->buffer[num_samples - 1];
    output(kTapNoise)->buffer[0] = sampler_->output(SampleModule::kLevelled)->buffer[num_samples - 1];
    output(kTapSub)->buffer[0] = sub_module_->output(SubModule::kLevelled)->buffer[num_samples - 1];

    poly_float* filter1_output = output(kToFilter1)->buffer;
    poly_float* filter2_output = output(kToFilter2)->buffer;
    poly_float* raw_output = output(kRawOut)->buffer;
    poly_float* direct_output = output(kDirectOut)->buffer;
    utils::zeroBuffer(filter1_output, num_samples);
    utils::zeroBuffer(filter2_output, num_samples);
    utils::zeroBuffer(raw_output, num_samples);
    utils::zeroBuffer(direct_output, num_samples);
    utils::zeroBuffer(output(kBus1Out)->buffer, num_samples);
    utils::zeroBuffer(output(kBus2Out)->buffer, num_samples);
    utils::zeroBuffer(output(kRawFree)->buffer, num_samples);
    utils::zeroBuffer(output(kDirectFree)->buffer, num_samples);

    bool filter1_on = isFilter1On();
    bool filter2_on = isFilter2On();

    for (int i = 0; i < kNumOscillators; ++i) {
      const poly_float* buffer = oscillators_[i]->output(OscillatorModule::kLevelled)->buffer;
      routeSource(i, buffer, static_cast<int>(oscillator_destinations_[i]->value()), num_samples, filter1_on, filter2_on,
                  filter1_output, filter2_output, raw_output, direct_output);
    }

    const poly_float* sample = sampler_->output(SampleModule::kLevelled)->buffer;
    routeSource(kNumOscillators, sample, static_cast<int>(sample_destination_->value()), num_samples, filter1_on, filter2_on,
                filter1_output, filter2_output, raw_output, direct_output);

    const poly_float* sub = sub_module_->output(SubModule::kLevelled)->buffer;
    routeSource(kSourceSub, sub, static_cast<int>(sub_destination_->value()), num_samples, filter1_on, filter2_on,
                filter1_output, filter2_output, raw_output, direct_output);
  }

  // SynthyBoy: the mixer graph of one source (docs/ui-design/13-new-keys.md 4.10). Destinations 0 to 4 are the engine's own and add
  // exactly as before; 5 splits the source between the two filters with an equal-power law (filter blend), 6 sends it nowhere. The bus
  // sends tap the source after its level and pan, whatever the destination is.
  void ProducersModule::routeSource(int source, const poly_float* buffer, int destination, int num_samples, bool filter1_on, bool filter2_on,
                                    poly_float* filter1_output, poly_float* filter2_output, poly_float* raw_output, poly_float* direct_output) {
    bool raw = destination == constants::kEffects;
    bool filter1 = destination == constants::kFilter1 || destination == constants::kDualFilters;
    bool filter2 = destination == constants::kFilter2 || destination == constants::kDualFilters;
    bool direct_out = destination == constants::kDirectOut;
    if (destination == kDestinationFilterBlend) {
      const poly_float blend = utils::clamp(filter_blend_[source]->buffer[0], 0.0f, 1.0f);
      poly_float current = last_blend_[source];
      const poly_float step = (blend - current) * (1.0f / num_samples);
      last_blend_[source] = blend;
      for (int i = 0; i < num_samples; ++i) {
        current += step;
        const poly_float g1 = futils::equalPowerFadeInverse(current); // cos (blend pi / 2)
        const poly_float g2 = futils::equalPowerFade(current);        // sin (blend pi / 2)
        const poly_float x = buffer[i];
        filter1_output[i] += x * g1;
        filter2_output[i] += x * g2;
        // a switched off filter lets its share through to the effects, as for the engine's own filter destinations
        if (!filter1_on)
          raw_output[i] += x * g1;
        if (!filter2_on)
          raw_output[i] += x * g2;
      }
    }
    else {
      // the ENV 1 button of a Main / Direct channel (manual p.146): off = the channel is not multiplied by the amplitude envelope
      const bool env1_off = env1_[source] != nullptr && env1_[source]->value() < 0.5f;
      if (raw && env1_off)
        utils::addBuffers(output(kRawFree)->buffer, output(kRawFree)->buffer, buffer, num_samples);
      else if (raw || (!filter2 && filter1 && !filter1_on) ||
                 (!filter1 && filter2 && !filter2_on) ||
                 (filter1 && filter2 && !filter1_on && !filter2_on)) {
        utils::addBuffers(raw_output, raw_output, buffer, num_samples);
      }
      if (filter1)
        utils::addBuffers(filter1_output, filter1_output, buffer, num_samples);
      if (filter2)
        utils::addBuffers(filter2_output, filter2_output, buffer, num_samples);
      if (direct_out && env1_off)
        utils::addBuffers(output(kDirectFree)->buffer, output(kDirectFree)->buffer, buffer, num_samples);
      else if (direct_out)
        utils::addBuffers(direct_output, direct_output, buffer, num_samples);
    }

    // bus sends (a linear gain, 100 % = unity), ramped over the block
    for (int bus = 0; bus < 2; ++bus) {
      poly_float* last = bus == 0 ? &last_send1_[source] : &last_send2_[source];
      const poly_float send = utils::clamp((bus == 0 ? bus1_send_[source] : bus2_send_[source])->buffer[0], 0.0f, 1.0f);
      if (!(send.sum() > 0.0f) && !(last->sum() > 0.0f))
        continue;
      poly_float* out = output(bus == 0 ? kBus1Out : kBus2Out)->buffer;
      poly_float current = *last;
      const poly_float step = (send - current) * (1.0f / num_samples);
      *last = send;
      for (int i = 0; i < num_samples; ++i) {
        current += step;
        out[i] += buffer[i] * current;
      }
    }
  }
} // namespace sbvital
