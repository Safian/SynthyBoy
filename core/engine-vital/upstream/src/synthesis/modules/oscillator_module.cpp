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
#include "operators.h"
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
    {
      // SynthyBoy (GROUPA2, WP-07): the voice control moves the pan of every voice (0 unless a step or the random part sets it)
      cr::Add* voice_pan = new cr::Add();
      voice_pan->plug(pan, 0);
      voice_pan->useInput(input(kVoicePan), 1);
      addProcessor(voice_pan);
      pan = voice_pan->output();
    }
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
    Output* sample_slice = createPolyModControl(prefix_ + "_sample_slice"); // GROUPB WP-05
    // GROUPC WP-11 (13-new-keys.md 4.37): the modulatable keys of the granular oscillator, in the order of SampleOscillator::kGranWindowAmount ..
    static const char* const kGranControls[SampleOscillator::kGranXyY - SampleOscillator::kGranWindowAmount + 1] = {
      "_gran_window_amount", "_gran_window_skew", "_gran_density", "_gran_length", "_gran_length_percent", "_gran_position", "_gran_rand_offset",
      "_gran_rand_dir", "_gran_rand_pitch", "_gran_rand_length", "_gran_rand_pan", "_gran_rand_level", "_gran_rand_window", "_gran_rand_skew",
      "_gran_rand_warp", "_gran_rand_warp2", "_gran_xy_y" };
    Output* gran_controls[SampleOscillator::kGranXyY - SampleOscillator::kGranWindowAmount + 1];
    for (int i = 0; i <= SampleOscillator::kGranXyY - SampleOscillator::kGranWindowAmount; ++i)
      gran_controls[i] = createPolyModControl(prefix_ + kGranControls[i]);
    Output* multi_timbre = createPolyModControl(prefix_ + "_multi_timbre"); // GROUPC WP-13 (4.39): the timbre of the multisample oscillator
    // GROUPC WP-12 (4.38): the modulatable keys of the spectral oscillator, in the order of SampleOscillator::kSpecFreqLo ..
    static const char* const kSpecControls[SampleOscillator::kSpecXyY - SampleOscillator::kSpecFreqLo + 1] = {
      "_spec_freq_lo", "_spec_freq_hi", "_spec_cut", "_spec_mix", "_spec_freq_shift", "_spec_position", "_spec_xy_y" };
    Output* spec_controls[SampleOscillator::kSpecXyY - SampleOscillator::kSpecFreqLo + 1];
    for (int i = 0; i <= SampleOscillator::kSpecXyY - SampleOscillator::kSpecFreqLo; ++i)
      spec_controls[i] = createPolyModControl(prefix_ + kSpecControls[i]);
    // SynthyBoy Block B14: Pitch and unison modes, transpose snap, phase legato
    Value* pitch_mode = createBaseControl(prefix_ + "_pitch_mode");
    Output* pitch_coarse = createPolyModControl(prefix_ + "_pitch_coarse");
    Output* fixed_pitch = createPolyModControl(prefix_ + "_fixed_pitch");
    Value* pitch_bend_track = createBaseControl(prefix_ + "_pitch_bend_track");
    Value* unison_mode = createBaseControl(prefix_ + "_unison_mode");
    Output* unison_blend_shape = createPolyModControl(prefix_ + "_unison_blend_shape");
    Value* unison_stack_repeat = createBaseControl(prefix_ + "_unison_stack_repeat");
    Value* phase_legato = createBaseControl(prefix_ + "_phase_legato");
    Value* phase_memory = createBaseControl(prefix_ + "_phase_memory");

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
    // SynthyBoy Block B14: Pitch and unison modes, transpose snap, phase legato
    oscillator_->plug(pitch_mode, SynthOscillator::kPitchMode);
    oscillator_->plug(pitch_coarse, SynthOscillator::kPitchCoarse);
    oscillator_->plug(fixed_pitch, SynthOscillator::kFixedPitch);
    oscillator_->plug(pitch_bend_track, SynthOscillator::kPitchBendTrack);
    oscillator_->plug(unison_mode, SynthOscillator::kUnisonMode);
    oscillator_->plug(unison_blend_shape, SynthOscillator::kUnisonBlendShape);
    oscillator_->plug(unison_stack_repeat, SynthOscillator::kUnisonStackRepeat);
    oscillator_->plug(phase_legato, SynthOscillator::kPhaseLegato);
    oscillator_->plug(phase_memory, SynthOscillator::kPhaseMemory);
    oscillator_->useInput(input(kRawMidi), SynthOscillator::kRawMidiNote);
    oscillator_->useInput(input(kVoicePhaseLegato), SynthOscillator::kVoicePhaseLegato);
    oscillator_->useInput(input(kNoteCount), SynthOscillator::kNoteCount);
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
    so->useInput(input(kRawMidi), SampleOscillator::kRawMidiNote);
    so->plug(midi_track, SampleOscillator::kMidiTrack);
    so->plug(pitch_bend_track, SampleOscillator::kPitchBendTrack);
    so->plug(phase, SampleOscillator::kPhase);
    so->plug(rand_phase, SampleOscillator::kRandomPhase);
    so->plug(blend, SampleOscillator::kBlend);
    so->plug(stereo_spread, SampleOscillator::kStereoSpread);
    so->plug(sample_scan, SampleOscillator::kScan);
    so->plug(sample_uni_start, SampleOscillator::kUniStart);
    so->plug(sample_uni_span, SampleOscillator::kUniSpan);
    so->plug(sample_slice, SampleOscillator::kSlice);
    so->plug(type_, SampleOscillator::kType);
    so->useInput(input(kVelocity), SampleOscillator::kVelocity);
    so->plug(multi_timbre, SampleOscillator::kMultiTimbre);
    for (int i = 0; i <= SampleOscillator::kGranXyY - SampleOscillator::kGranWindowAmount; ++i)
      so->plug(gran_controls[i], SampleOscillator::kGranWindowAmount + i);
    for (int i = 0; i <= SampleOscillator::kSpecXyY - SampleOscillator::kSpecFreqLo; ++i)
      so->plug(spec_controls[i], SampleOscillator::kSpecFreqLo + i);
    so->plug(distortion_phase, SampleOscillator::kWarpVar);
    so->plug(warp2_phase, SampleOscillator::kWarp2Var);
    so->setPitchTap(oscillator_->pitchTap());
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
