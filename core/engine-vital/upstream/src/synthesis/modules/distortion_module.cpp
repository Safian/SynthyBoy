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

#include "distortion_module.h"

#include "distortion.h"
#include "digital_svf.h"
#include "operators.h"

namespace sbvital {

  DistortionModule::DistortionModule(const Output* keytrack) :
      SynthModule(0, 1), distortion_(nullptr), filter_(nullptr), mix_(0.0f), keytrack_(keytrack) { }

  void DistortionModule::setShaperCurves(const float* curve_a, const float* curve_b) {
    distortion_->setShaperCurves(curve_a, curve_b);
  }

  DistortionModule::~DistortionModule() {
  }

  void DistortionModule::init() {
    distortion_ = new Distortion();
    distortion_->useOutput(output());
    addIdleProcessor(distortion_);

    Value* distortion_type = createBaseControl("distortion_type");
    Output* distortion_drive = createMonoModControl("distortion_drive", true, true);
    distortion_mix_ = createMonoModControl("distortion_mix");

    distortion_->plug(distortion_type, Distortion::kType);
    distortion_->plug(distortion_drive, Distortion::kDrive);

    filter_order_ = createBaseControl("distortion_filter_order");
    // SynthyBoy (13-new-keys.md 4.14, G75): the key track of the filter, like the one of the FX filter
    Output* keytrack_amount = createMonoModControl("distortion_filter_keytrack");
    cr::Multiply* current_keytrack = nullptr;
    if (keytrack_ != nullptr) {
      current_keytrack = new cr::Multiply();
      current_keytrack->plug(keytrack_, 0);
      current_keytrack->plug(keytrack_amount, 1);
      addProcessor(current_keytrack);
    }
    Output* midi_cutoff = createMonoModControl("distortion_filter_cutoff", true, true,
                                               current_keytrack != nullptr ? current_keytrack->output() : nullptr);
    Output* resonance = createMonoModControl("distortion_filter_resonance");
    Output* blend = createMonoModControl("distortion_filter_blend");

    filter_ = new DigitalSvf();
    filter_->useOutput(output());
    filter_->plug(midi_cutoff, DigitalSvf::kMidiCutoff);
    filter_->plug(resonance, DigitalSvf::kResonance);
    filter_->plug(blend, DigitalSvf::kPassBlend);
    filter_->setDriveCompensation(false);
    filter_->setBasic(true);
    addIdleProcessor(filter_);

    SynthModule::init();
  }

  void DistortionModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    distortion_->setSampleRate(sample_rate);
    filter_->setSampleRate(sample_rate);
  }

  void DistortionModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);

    if (filter_order_->output()->buffer[0][0] < 1.0f)
      distortion_->processWithInput(audio_in, num_samples);
    else if (filter_order_->output()->buffer[0][0] > 1.0f) {
      distortion_->processWithInput(audio_in, num_samples);
      filter_->processWithInput(output()->buffer, num_samples);
    }
    else {
      filter_->processWithInput(audio_in, num_samples);
      distortion_->processWithInput(output()->buffer, num_samples);
    }
    
    poly_float current_mix = mix_;
    mix_ = utils::clamp(distortion_mix_->buffer[0], 0.0f, 1.0f);
    poly_float delta_mix = (mix_ - current_mix) * (1.0f / num_samples);
    poly_float* audio_out = output()->buffer;
    
    for (int i = 0; i < num_samples; ++i) {
      current_mix += delta_mix;
      audio_out[i] = utils::interpolate(audio_in[i], audio_out[i], current_mix);
    }
  }
} // namespace sbvital
