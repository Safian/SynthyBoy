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

#include "delay_module.h"

#include "delay.h"
#include "memory.h"

namespace sbvital {

  DelayModule::DelayModule(const Output* beats_per_second) : SynthModule(0, 1), beats_per_second_(beats_per_second) {
    int size = kMaxDelayTime * getSampleRate();
    delay_ = new StereoDelay(size);
    addIdleProcessor(delay_);
  }

  DelayModule::~DelayModule() { }

  void DelayModule::init() {
    delay_->useOutput(output());

    Output* free_frequency = createMonoModControl("delay_frequency");
    Output* frequency = createTempoSyncSwitch("delay", free_frequency->owner, beats_per_second_, false);
    Output* free_frequency_aux = createMonoModControl("delay_aux_frequency");
    Output* frequency_aux = createTempoSyncSwitch("delay_aux", free_frequency_aux->owner, beats_per_second_, false);
    Output* feedback = createMonoModControl("delay_feedback");
    Output* wet = createMonoModControl("delay_dry_wet");

    Output* filter_cutoff = createMonoModControl("delay_filter_cutoff");
    Output* filter_spread = createMonoModControl("delay_filter_spread");

    Value* style = createBaseControl("delay_style");
    Value* high_quality = createBaseControl("delay_hq");
    Output* offset = createMonoModControl("delay_offset");
    Output* offset_left = createMonoModControl("delay_offset_left");

    delay_->plug(frequency, StereoDelay::kFrequency);
    delay_->plug(frequency_aux, StereoDelay::kFrequencyAux);
    delay_->plug(feedback, StereoDelay::kFeedback);
    delay_->plug(wet, StereoDelay::kWet);
    delay_->plug(style, StereoDelay::kStyle);
    delay_->plug(high_quality, StereoDelay::kHighQuality);
    delay_->plug(offset, StereoDelay::kOffset);
    delay_->plug(offset_left, StereoDelay::kOffsetLeft);
    delay_->plug(filter_cutoff, StereoDelay::kFilterCutoff);
    delay_->plug(filter_spread, StereoDelay::kFilterSpread);

    SynthModule::init();

    // Serum 2 manual p.166: one BPM / MS switch for both delay times. The right time had its own switch (delay_aux_sync, never written by the
    // adapter, always on tempo) so delay.time2 had no effect in Free mode; it now follows delay_sync.
    control_map controls = getControls();
    auto sync = controls.find("delay_sync");
    auto aux_sync = controls.find("delay_aux_sync");
    if (sync != controls.end() && aux_sync != controls.end()) {
      sync_ = sync->second;
      aux_sync_ = aux_sync->second;
    }
  }

  void DelayModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    delay_->setSampleRate(sample_rate);
    delay_->setMaxSamples(kMaxDelayTime * getSampleRate());
  }
  
  void DelayModule::setOversampleAmount(int oversample) {
    SynthModule::setOversampleAmount(oversample);
    delay_->setMaxSamples(kMaxDelayTime * getSampleRate());
  }
  
  void DelayModule::processWithInput(const poly_float* audio_in, int num_samples) {
    if (sync_ && aux_sync_)
      aux_sync_->set(sync_->value());
    SynthModule::process(num_samples);
    delay_->processWithInput(audio_in, num_samples);
  }
} // namespace sbvital
