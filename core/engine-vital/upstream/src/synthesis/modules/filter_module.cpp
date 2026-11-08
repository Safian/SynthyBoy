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

#include "filter_module.h"

#include "comb_module.h"
#include "digital_svf.h"
#include "diode_filter.h"
#include "dirty_filter.h"
#include "formant_module.h"
#include "ladder_filter.h"
#include "phaser_filter.h"
#include "sallen_key_filter.h"
#include "serum_filter_bank.h"
#include "synth_constants.h"

namespace sbvital {

  FilterModule::FilterModule(std::string prefix) :
      SynthModule(kNumInputs, kNumOutputs), last_model_(-1), was_on_(false), 
      prefix_(std::move(prefix)), create_on_value_(true), mono_(false),
      on_(nullptr), filter_model_(nullptr), mix_(0.0f), filter_mix_(nullptr),
      comb_filter_(nullptr), digital_svf_(nullptr), dirty_filter_(nullptr),
      formant_filter_(nullptr), ladder_filter_(nullptr), phaser_filter_(nullptr),
      sallen_key_filter_(nullptr) {
    comb_filter_ = new CombModule();
    digital_svf_ = new DigitalSvf();
    diode_filter_ = new DiodeFilter();
    dirty_filter_ = new DirtyFilter();
    ladder_filter_ = new LadderFilter();
    phaser_filter_ = new PhaserFilter(false);
    sallen_key_filter_ = new SallenKeyFilter();
    formant_filter_ = new FormantModule(prefix_);
    serum_filter_ = new SerumFilterBank();
    cutoff_offset_ = new StereoCutoffOffset();

    addSubmodule(comb_filter_);
    addSubmodule(formant_filter_);

    addProcessor(comb_filter_);
    addProcessor(digital_svf_);
    addProcessor(diode_filter_);
    addProcessor(dirty_filter_);
    addProcessor(formant_filter_);
    addProcessor(ladder_filter_);
    addProcessor(phaser_filter_);
    addProcessor(sallen_key_filter_);
    addProcessor(serum_filter_);
    addProcessor(cutoff_offset_);
    serum_filter_->enable(false);
  }

  void FilterModule::init() {
    Output* keytrack_amount = createModControl(prefix_ + "_keytrack");
    cr::Multiply* current_keytrack = new cr::Multiply();
    current_keytrack->useInput(input(kKeytrack), 0);
    current_keytrack->plug(keytrack_amount, 1);

    Output* base_cutoff = createModControl(prefix_ + "_cutoff", true, true, current_keytrack->output());
    // SynthyBoy (docs/ui-design/13-new-keys.md 4.11): var is the fourth knob of the new models, pan offsets the cutoff of the two channels in
    // opposite directions, level trims the output of the filter
    Output* var = createModControl(prefix_ + "_var");
    Output* pan = createModControl(prefix_ + "_pan");
    level_ = createModControl(prefix_ + "_level");
    cutoff_offset_->plug(base_cutoff, StereoCutoffOffset::kCutoff);
    cutoff_offset_->plug(pan, StereoCutoffOffset::kPan);
    Output* midi_cutoff = cutoff_offset_->output();
    Output* resonance = createModControl(prefix_ + "_resonance");
    Output* drive = createModControl(prefix_ + "_drive");
    Output* blend = createModControl(prefix_ + "_blend");
    Output* blend_transpose = createModControl(prefix_ + "_blend_transpose");
    if (create_on_value_)
      on_ = createBaseControl(prefix_ + "_on");
    Value* filter_style = createBaseControl(prefix_ + "_style");
    filter_model_ = createBaseControl(prefix_ + "_model");

    filter_mix_ = createModControl(prefix_ + "_mix");
    if (prefix_ != "filter_fx") { // the two voice filters have a destination and sends to the buses
      destination_ = createBaseControl(prefix_ + "_destination");
      bus1_send_ = createModControl(prefix_ + "_bus1_send");
      bus2_send_ = createModControl(prefix_ + "_bus2_send");
    }

    comb_filter_->useInput(input(kAudio), CombModule::kAudio);
    comb_filter_->plug(filter_style, CombModule::kStyle);
    comb_filter_->useInput(input(kReset), CombModule::kReset);
    comb_filter_->useInput(input(kMidi), CombModule::kMidi);
    comb_filter_->plug(midi_cutoff, CombModule::kMidiCutoff);
    comb_filter_->plug(blend_transpose, CombModule::kMidiBlendTranspose);
    comb_filter_->plug(blend, CombModule::kFilterCutoffBlend);
    comb_filter_->plug(resonance, CombModule::kResonance);
    comb_filter_->useOutput(output());

    digital_svf_->useInput(input(kAudio), DigitalSvf::kAudio);
    digital_svf_->plug(filter_style, DigitalSvf::kStyle);
    digital_svf_->plug(blend, DigitalSvf::kPassBlend);
    digital_svf_->useInput(input(kReset), DigitalSvf::kReset);
    digital_svf_->plug(midi_cutoff, DigitalSvf::kMidiCutoff);
    digital_svf_->plug(resonance, DigitalSvf::kResonance);
    digital_svf_->plug(drive, DigitalSvf::kDriveGain);
    digital_svf_->useOutput(output());

    diode_filter_->useInput(input(kAudio), DiodeFilter::kAudio);
    diode_filter_->useInput(input(kReset), DiodeFilter::kReset);
    diode_filter_->plug(resonance, DiodeFilter::kResonance);
    diode_filter_->plug(filter_style, DiodeFilter::kStyle);
    diode_filter_->plug(blend, DiodeFilter::kPassBlend);
    diode_filter_->plug(midi_cutoff, DiodeFilter::kMidiCutoff);
    diode_filter_->plug(drive, DiodeFilter::kDriveGain);
    diode_filter_->useOutput(output());

    dirty_filter_->useInput(input(kAudio), DirtyFilter::kAudio);
    dirty_filter_->useInput(input(kReset), DirtyFilter::kReset);
    dirty_filter_->plug(resonance, DirtyFilter::kResonance);
    dirty_filter_->plug(filter_style, DirtyFilter::kStyle);
    dirty_filter_->plug(blend, DirtyFilter::kPassBlend);
    dirty_filter_->plug(midi_cutoff, DirtyFilter::kMidiCutoff);
    dirty_filter_->plug(drive, DirtyFilter::kDriveGain);
    dirty_filter_->useOutput(output());

    formant_filter_->useInput(input(kAudio), FormantModule::kAudio);
    formant_filter_->useInput(input(kReset), FormantModule::kReset);
    formant_filter_->plug(blend, FormantModule::kBlend);
    formant_filter_->plug(filter_style, FormantModule::kStyle);
    formant_filter_->useOutput(output());

    ladder_filter_->useInput(input(kAudio), LadderFilter::kAudio);
    ladder_filter_->useInput(input(kReset), LadderFilter::kReset);
    ladder_filter_->plug(resonance, LadderFilter::kResonance);
    ladder_filter_->plug(filter_style, LadderFilter::kStyle);
    ladder_filter_->plug(blend, LadderFilter::kPassBlend);
    ladder_filter_->plug(midi_cutoff, LadderFilter::kMidiCutoff);
    ladder_filter_->plug(drive, LadderFilter::kDriveGain);
    ladder_filter_->useOutput(output());

    phaser_filter_->useInput(input(kAudio), PhaserFilter::kAudio);
    phaser_filter_->useInput(input(kReset), PhaserFilter::kReset);
    phaser_filter_->plug(resonance, PhaserFilter::kResonance);
    phaser_filter_->plug(filter_style, PhaserFilter::kStyle);
    phaser_filter_->plug(blend_transpose, PhaserFilter::kTranspose);
    phaser_filter_->plug(blend, PhaserFilter::kPassBlend);
    phaser_filter_->plug(midi_cutoff, PhaserFilter::kMidiCutoff);
    phaser_filter_->plug(drive, PhaserFilter::kDriveGain);
    phaser_filter_->useOutput(output());

    sallen_key_filter_->plug(filter_style, SallenKeyFilter::kStyle);
    sallen_key_filter_->useInput(input(kAudio), SallenKeyFilter::kAudio);
    sallen_key_filter_->plug(blend, SallenKeyFilter::kPassBlend);
    sallen_key_filter_->useInput(input(kReset), SallenKeyFilter::kReset);
    sallen_key_filter_->plug(midi_cutoff, SallenKeyFilter::kMidiCutoff);
    sallen_key_filter_->plug(resonance, SallenKeyFilter::kResonance);
    sallen_key_filter_->plug(drive, SallenKeyFilter::kDriveGain);
    sallen_key_filter_->useOutput(output());

    serum_filter_->useInput(input(kAudio), SerumFilterBank::kAudio);
    serum_filter_->useInput(input(kReset), SerumFilterBank::kReset);
    serum_filter_->plug(midi_cutoff, SerumFilterBank::kMidiCutoff);
    serum_filter_->plug(resonance, SerumFilterBank::kResonance);
    serum_filter_->plug(drive, SerumFilterBank::kDrive);
    serum_filter_->plug(blend, SerumFilterBank::kBlend);
    serum_filter_->plug(var, SerumFilterBank::kVar);
    serum_filter_->plug(filter_model_, SerumFilterBank::kModel);
    serum_filter_->plug(filter_style, SerumFilterBank::kStyle);
    serum_filter_->useOutput(output());

    comb_filter_->enable(false);
    digital_svf_->enable(false);
    diode_filter_->enable(false);
    dirty_filter_->enable(false);
    formant_filter_->enable(false);
    ladder_filter_->enable(false);
    sallen_key_filter_->enable(false);

    addProcessor(current_keytrack);

    SynthModule::init();
  }

  FilterCurveShared* FilterModule::curveShared() {
    return serum_filter_->curve();
  }

  void FilterModule::hardReset() {
    comb_filter_->hardReset();
    digital_svf_->hardReset();
    diode_filter_->hardReset();
    dirty_filter_->hardReset();
    formant_filter_->hardReset();
    ladder_filter_->hardReset();
    phaser_filter_->hardReset();
    sallen_key_filter_->hardReset();
    serum_filter_->hardReset();
  }

  Output* FilterModule::createModControl(std::string name, bool audio_rate, bool smooth_value,
                                         Output* internal_modulation) {
    if (mono_)
      return createMonoModControl(name, audio_rate, smooth_value, internal_modulation);
    return createPolyModControl(name, audio_rate, smooth_value, internal_modulation, input(kReset));
  }

  force_inline void FilterModule::setModel(int new_model) {
    comb_filter_->enable(new_model == constants::kComb);
    digital_svf_->enable(new_model == constants::kDigital);
    diode_filter_->enable(new_model == constants::kDiode);
    dirty_filter_->enable(new_model == constants::kDirty);
    formant_filter_->enable(new_model == constants::kFormant);
    ladder_filter_->enable(new_model == constants::kLadder);
    phaser_filter_->enable(new_model == constants::kPhase);
    sallen_key_filter_->enable(new_model == constants::kAnalog);
    serum_filter_->enable(new_model >= SerumFilterBank::kFirstModel);

    if (new_model == last_model_)
      return;

    Processor* to_reset = nullptr;
    if (new_model == constants::kAnalog)
      to_reset = sallen_key_filter_;
    else if (new_model == constants::kComb)
      to_reset = comb_filter_;
    else if (new_model == constants::kDigital)
      to_reset = digital_svf_;
    else if (new_model == constants::kDiode)
      to_reset = diode_filter_;
    else if (new_model == constants::kDirty)
      to_reset = dirty_filter_;
    else if (new_model == constants::kFormant)
      to_reset = formant_filter_;
    else if (new_model == constants::kLadder)
      to_reset = ladder_filter_;
    else if (new_model == constants::kPhase)
      to_reset = phaser_filter_;
    else if (new_model >= SerumFilterBank::kFirstModel)
      to_reset = serum_filter_;

    if (to_reset)
      getLocalProcessor(to_reset)->hardReset();

    last_model_ = new_model;
  }

  void FilterModule::process(int num_samples) {
    bool on = on_ == nullptr || on_->value() > 0.5f;
    setModel(static_cast<int>(roundf(filter_model_->value())));

    if (on) {
      SynthModule::process(num_samples);

      poly_float current_mix = mix_;
      mix_ = utils::clamp(filter_mix_->buffer[0], 0.0f, 1.0f);
      current_mix = utils::maskLoad(current_mix, mix_, getResetMask(kReset));
      poly_float delta_mix = (mix_ - current_mix) * (1.0f / num_samples);

      poly_float* audio_out = output()->buffer;
      const poly_float* audio_in = input(kAudio)->source->buffer;
      for (int i = 0; i < num_samples; ++i) {
        current_mix += delta_mix;
        audio_out[i] = utils::interpolate(audio_in[i], audio_out[i], current_mix);
      }

      // SynthyBoy: the output level of the filter (-60 dB = silence), ramped over the block; 0 dB leaves the signal untouched
      {
        poly_float db = level_->buffer[0];
        poly_float target;
        for (int lane = 0; lane < static_cast<int>(poly_float::kSize); ++lane)
          target.set(lane, db[lane] <= -59.9f ? 0.0f : std::pow(10.0f, db[lane] * 0.05f));
        poly_float current_gain = utils::maskLoad(level_gain_, target, getResetMask(kReset));
        level_gain_ = target;
        if (poly_float::notEqual(current_gain, 1.0f).anyMask() || poly_float::notEqual(target, 1.0f).anyMask()) {
          poly_float delta_gain = (target - current_gain) * (1.0f / num_samples);
          for (int i = 0; i < num_samples; ++i) {
            current_gain += delta_gain;
            audio_out[i] *= current_gain;
          }
        }
      }

      // SynthyBoy: the bus sends tap the output of the filter (after its mix, pan and level)
      if (bus1_send_ != nullptr) {
        poly_mask reset_mask = getResetMask(kReset);
        poly_float send1 = utils::clamp(bus1_send_->buffer[0], 0.0f, 1.0f);
        poly_float send2 = utils::clamp(bus2_send_->buffer[0], 0.0f, 1.0f);
        poly_float current1 = utils::maskLoad(bus1_gain_, send1, reset_mask);
        poly_float current2 = utils::maskLoad(bus2_gain_, send2, reset_mask);
        bus1_gain_ = send1;
        bus2_gain_ = send2;
        poly_float delta1 = (send1 - current1) * (1.0f / num_samples);
        poly_float delta2 = (send2 - current2) * (1.0f / num_samples);
        poly_float* bus1 = output(kBus1Send)->buffer;
        poly_float* bus2 = output(kBus2Send)->buffer;
        for (int i = 0; i < num_samples; ++i) {
          current1 += delta1;
          current2 += delta2;
          bus1[i] = audio_out[i] * current1;
          bus2[i] = audio_out[i] * current2;
        }
      }
    }
    else {
      utils::zeroBuffer(output()->buffer, num_samples);
      if (bus1_send_ != nullptr) {
        utils::zeroBuffer(output(kBus1Send)->buffer, num_samples);
        utils::zeroBuffer(output(kBus2Send)->buffer, num_samples);
      }
    }
  }

  void FilterModule::setMono(bool mono) {
    mono_ = mono;
    formant_filter_->setMono(mono);
  }
} // namespace sbvital
