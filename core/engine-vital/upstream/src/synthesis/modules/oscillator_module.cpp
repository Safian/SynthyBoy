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

#include "oscillator_module.h"

#include <algorithm>
#include <cmath>

#include "synth_oscillator.h"
#include "wavetable.h"

namespace sbvital {

  OscillatorModule::OscillatorModule(std::string prefix) :
      SynthModule(kNumInputs, kNumOutputs), prefix_(std::move(prefix)), on_(nullptr), oscillator_(nullptr),
      sample_oscillator_(nullptr), distortion_type_(nullptr), distortion2_type_(nullptr), type_(nullptr) {
    wavetable_ = std::make_shared<Wavetable>(kNumOscillatorWaveFrames);
    was_on_ = std::make_shared<bool>(true);
    sample_shared_ = std::make_shared<SampleOscShared>();
  }

  void OscillatorModule::init() {
    oscillator_ = new SynthOscillator(wavetable_.get());
    sample_oscillator_ = new SampleOscillator(sample_shared_.get());

    createBaseControl(prefix_ + "_view_2d");
    on_ = createBaseControl(prefix_ + "_on");
    Value* midi_track = createBaseControl(prefix_ + "_midi_track");
    Value* smooth_interpolation = createBaseControl(prefix_ + "_smooth_interpolation");
    Value* spectral_unison = createBaseControl(prefix_ + "_spectral_unison");
    Value* stack_style = createBaseControl(prefix_ + "_stack_style");
    Value* transpose_quantize = createBaseControl(prefix_ + "_transpose_quantize");

    Input* reset = input(kReset);

    Output* wave_frame = createPolyModControl(prefix_ + "_wave_frame");
    Output* transpose = createPolyModControl(prefix_ + "_transpose", true, false, nullptr, reset);
    Output* tune = createPolyModControl(prefix_ + "_tune", true, false, nullptr, reset);
    Output* unison_voices = createPolyModControl(prefix_ + "_unison_voices");
    Output* unison_detune = createPolyModControl(prefix_ + "_unison_detune");
    Output* detune_power = createPolyModControl(prefix_ + "_detune_power");
    Output* detune_range = createPolyModControl(prefix_ + "_detune_range");
    Output* amplitude = createPolyModControl(prefix_ + "_level", true, true, nullptr, reset);
    Output* pan = createPolyModControl(prefix_ + "_pan");
    Output* phase = createPolyModControl(prefix_ + "_phase", true, true, nullptr, reset);
    Output* distortion_phase = createPolyModControl(prefix_ + "_distortion_phase");
    Output* rand_phase = createPolyModControl(prefix_ + "_random_phase");
    Output* blend = createPolyModControl(prefix_ + "_unison_blend");
    Output* stereo_spread = createPolyModControl(prefix_ + "_stereo_spread");
    Output* frame_spread = createPolyModControl(prefix_ + "_frame_spread");
    Output* distortion_spread = createPolyModControl(prefix_ + "_distortion_spread");
    distortion_type_ = createBaseControl(prefix_ + "_distortion_type");
    Output* distortion_amount = createPolyModControl(prefix_ + "_distortion_amount");
    Output* spectral_morph_spread = createPolyModControl(prefix_ + "_spectral_morph_spread");
    Value* spectral_morph_type = createBaseControl(prefix_ + "_spectral_morph_type");
    Output* spectral_morph_amount = createPolyModControl(prefix_ + "_spectral_morph_amount");
    // SynthyBoy: the second warp slot, the oscillator type and the modulatable controls of the sample oscillator
    distortion2_type_ = createBaseControl(prefix_ + "_warp2_type");
    Output* warp2_amount = createPolyModControl(prefix_ + "_warp2_amount");
    Output* warp2_phase = createPolyModControl(prefix_ + "_warp2_phase");
    Output* warp2_spread = createPolyModControl(prefix_ + "_warp2_spread");
    type_ = createBaseControl(prefix_ + "_type");
    Output* sample_scan = createPolyModControl(prefix_ + "_sample_scan");
    Output* sample_uni_start = createPolyModControl(prefix_ + "_sample_uni_start");
    Output* sample_uni_span = createPolyModControl(prefix_ + "_sample_uni_span");

    oscillator_->useInput(input(kReset), SynthOscillator::kReset);
    oscillator_->useInput(input(kRetrigger), SynthOscillator::kRetrigger);
    oscillator_->useInput(input(kActiveVoices), SynthOscillator::kActiveVoices);
    oscillator_->useInput(input(kMidi), SynthOscillator::kMidiNote);
    oscillator_->useInput(input(kFold), SynthOscillator::kFoldNote);
    oscillator_->plug(wave_frame, SynthOscillator::kWaveFrame);
    oscillator_->plug(midi_track, SynthOscillator::kMidiTrack);
    oscillator_->plug(smooth_interpolation, SynthOscillator::kSmoothlyInterpolate);
    oscillator_->plug(spectral_unison, SynthOscillator::kSpectralUnison);
    oscillator_->plug(transpose_quantize, SynthOscillator::kTransposeQuantize);
    oscillator_->plug(transpose, SynthOscillator::kTranspose);
    oscillator_->plug(tune, SynthOscillator::kTune);
    oscillator_->plug(stack_style, SynthOscillator::kStackStyle);
    oscillator_->plug(unison_detune, SynthOscillator::kUnisonDetune);
    oscillator_->plug(unison_voices, SynthOscillator::kUnisonVoices);
    oscillator_->plug(phase, SynthOscillator::kPhase);
    oscillator_->plug(distortion_phase, SynthOscillator::kDistortionPhase);
    oscillator_->plug(rand_phase, SynthOscillator::kRandomPhase);
    oscillator_->plug(blend, SynthOscillator::kBlend);
    oscillator_->plug(amplitude, SynthOscillator::kAmplitude);
    oscillator_->plug(pan, SynthOscillator::kPan);
    oscillator_->plug(detune_power, SynthOscillator::kDetunePower);
    oscillator_->plug(detune_range, SynthOscillator::kDetuneRange);
    oscillator_->plug(stereo_spread, SynthOscillator::kStereoSpread);
    oscillator_->plug(frame_spread, SynthOscillator::kUnisonFrameSpread);
    oscillator_->plug(distortion_spread, SynthOscillator::kUnisonDistortionSpread);
    oscillator_->plug(distortion_type_, SynthOscillator::kDistortionType);
    oscillator_->plug(distortion_amount, SynthOscillator::kDistortionAmount);
    oscillator_->plug(spectral_morph_spread, SynthOscillator::kUnisonSpectralMorphSpread);
    oscillator_->plug(spectral_morph_type, SynthOscillator::kSpectralMorphType);
    oscillator_->plug(spectral_morph_amount, SynthOscillator::kSpectralMorphAmount);
    oscillator_->plug(distortion2_type_, SynthOscillator::kWarp2Type);
    oscillator_->plug(warp2_amount, SynthOscillator::kWarp2Amount);
    oscillator_->plug(warp2_phase, SynthOscillator::kWarp2Phase);
    oscillator_->plug(warp2_spread, SynthOscillator::kWarp2Spread);
    oscillator_->useOutput(output(kRaw), SynthOscillator::kRaw);
    oscillator_->useOutput(output(kLevelled), SynthOscillator::kLevelled);

    // the sample oscillator shares the controls and the outputs of the wavetable oscillator
    SampleOscillator* so = sample_oscillator_;
    so->useInput(input(kReset), SampleOscillator::kReset);
    so->useInput(input(kActiveVoices), SampleOscillator::kActiveVoices);
    so->useInput(input(kMidi), SampleOscillator::kMidiNote);
    so->useInput(input(kFold), SampleOscillator::kFoldNote);
    so->useInput(input(kNotePressed), SampleOscillator::kNotePressed);
    so->plug(transpose, SampleOscillator::kTranspose);
    so->plug(tune, SampleOscillator::kTune);
    so->plug(amplitude, SampleOscillator::kAmplitude);
    so->plug(pan, SampleOscillator::kPan);
    so->plug(unison_voices, SampleOscillator::kUnisonVoices);
    so->plug(unison_detune, SampleOscillator::kUnisonDetune);
    so->plug(detune_power, SampleOscillator::kDetunePower);
    so->plug(detune_range, SampleOscillator::kDetuneRange);
    so->plug(phase, SampleOscillator::kPhase);
    so->plug(rand_phase, SampleOscillator::kRandomPhase);
    so->plug(blend, SampleOscillator::kBlend);
    so->plug(stereo_spread, SampleOscillator::kStereoSpread);
    so->plug(sample_scan, SampleOscillator::kScan);
    so->plug(sample_uni_start, SampleOscillator::kUniStart);
    so->plug(sample_uni_span, SampleOscillator::kUniSpan);
    so->plug(distortion_type_, SampleOscillator::kWarpType);
    so->plug(distortion_amount, SampleOscillator::kWarpAmount);
    so->plug(distortion2_type_, SampleOscillator::kWarp2Type);
    so->plug(warp2_amount, SampleOscillator::kWarp2Amount);
    sample_raw_ = std::make_shared<Output>(output(kRaw)->buffer_size);
    sample_levelled_ = std::make_shared<Output>(output(kLevelled)->buffer_size);
    so->useOutput(sample_raw_.get(), SampleOscillator::kRaw);
    so->useOutput(sample_levelled_.get(), SampleOscillator::kLevelled);

    addProcessor(oscillator_);
    addProcessor(sample_oscillator_);
    sample_oscillator_->enable(false);
    SynthModule::init();
  }

  void OscillatorModule::process(int num_samples) {
    bool on = on_->value();

    // SynthyBoy: exactly one of the two oscillator types runs, except during the 10 ms crossfade that follows a change of oscN.type
    // while the oscillator is sounding (both run, the sample oscillator into its own outputs, and the module blends them).
    const bool sample = type_ != nullptr && type_->value() > 0.5f;
    if (sample != type_sample_) {
      if (on && *was_on_ && sounding_) {
        fading_ = true;
        fade_position_ = 0;
        fade_length_ = std::max(1, static_cast<int>(0.01 * getSampleRate()));
        getLocalProcessor(oscillator_)->enable(true);
        getLocalProcessor(sample_oscillator_)->enable(true);
      }
      else {
        fading_ = false;
        getLocalProcessor(oscillator_)->enable(!sample);
        getLocalProcessor(sample_oscillator_)->enable(sample);
      }
      type_sample_ = sample;
    }
    else if (!fading_ && getLocalProcessor(oscillator_)->enabled() == getLocalProcessor(sample_oscillator_)->enabled()) {
      // the block after a fade: only the new type keeps running
      getLocalProcessor(oscillator_)->enable(!type_sample_);
      getLocalProcessor(sample_oscillator_)->enable(type_sample_);
    }

    if (on) {
      SynthModule::process(num_samples);

      poly_float* raw = output(kRaw)->buffer;
      poly_float* levelled = output(kLevelled)->buffer;
      const poly_float* sample_raw = sample_raw_->buffer;
      const poly_float* sample_levelled = sample_levelled_->buffer;
      if (fading_) {
        for (int i = 0; i < num_samples; ++i) {
          const float g = std::min(1.0f, static_cast<float>(fade_position_ + i + 1) / static_cast<float>(fade_length_));
          const poly_float to_sample = type_sample_ ? g : 1.0f - g;   // share of the sample oscillator
          raw[i] = raw[i] + (sample_raw[i] - raw[i]) * to_sample;
          levelled[i] = levelled[i] + (sample_levelled[i] - levelled[i]) * to_sample;
        }
        fade_position_ += num_samples;
        if (fade_position_ >= fade_length_)
          fading_ = false;
      }
      else if (type_sample_) {
        for (int i = 0; i < num_samples; ++i) {
          raw[i] = sample_raw[i];
          levelled[i] = sample_levelled[i];
        }
      }

      float magnitude = 0.0f;
      for (int i = 0; i < num_samples; ++i) {
        const poly_float v = raw[i];
        magnitude = std::max(magnitude, std::max(std::max(std::fabs(v[0]), std::fabs(v[1])), std::max(std::fabs(v[2]), std::fabs(v[3]))));
      }
      sounding_ = magnitude > 1.0e-5f;   // a change of the type fades only what is being heard
    }
    else if (*was_on_) {
      output(kRaw)->clearBuffer();
      output(kLevelled)->clearBuffer();
    }

    *was_on_ = on;
  }
} // namespace sbvital
