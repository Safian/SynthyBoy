/* Copyright 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sub oscillator module for SynthyBoy Vital engine (G23 Sub oscillator).
 */

#include "sub_module.h"

namespace sbvital {

  SubModule::SubModule() : SynthModule(kNumInputs, kNumOutputs), on_(nullptr) {
    sub_source_ = new SubSource();
    was_on_ = std::make_shared<bool>(true);
  }

  void SubModule::init() {
    on_ = createBaseControl("sub_on");
    Value* waveform = createBaseControl("sub_waveform");
    Value* mono = createBaseControl("sub_mono");
    Output* octave = createPolyModControl("sub_octave");
    Output* tune = createPolyModControl("sub_tune");
    Output* level = createPolyModControl("sub_level");
    Output* pan = createPolyModControl("sub_pan");
    Output* phase = createPolyModControl("sub_phase");                  // GROUPA1: sub.phase
    Value* phase_contiguous = createBaseControl("sub_phase_contiguous"); // GROUPA1: sub.phase_contiguous

    sub_source_->useInput(input(kReset), SubSource::kReset);
    sub_source_->useInput(input(kMidi), SubSource::kMidi);
    sub_source_->useInput(input(kActiveVoices), SubSource::kActiveVoices);
    sub_source_->useInput(input(kLowestNote), SubSource::kLowestNote);

    sub_source_->plug(waveform, SubSource::kWaveform);
    sub_source_->plug(mono, SubSource::kMono);
    sub_source_->plug(octave, SubSource::kOctave);
    sub_source_->plug(tune, SubSource::kTune);
    sub_source_->plug(level, SubSource::kLevel);
    sub_source_->plug(pan, SubSource::kPan);
    sub_source_->plug(phase, SubSource::kPhase);
    sub_source_->plug(phase_contiguous, SubSource::kPhaseContiguous);

    sub_source_->useOutput(output(kRaw), SubSource::kRaw);
    sub_source_->useOutput(output(kLevelled), SubSource::kLevelled);

    addProcessor(sub_source_);
    SynthModule::init();
  }

  void SubModule::process(int num_samples) {
    bool on = on_ && on_->value() != 0.0f;

    if (on)
      SynthModule::process(num_samples);
    else if (*was_on_) {
      output(kRaw)->clearBuffer();
      output(kLevelled)->clearBuffer();
    }

    *was_on_ = on;
  }

} // namespace sbvital
