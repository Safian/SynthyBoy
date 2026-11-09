// SynthyBoy (AGPL-3.0-or-later): the sample oscillator type of an oscillator slot (docs/ui-design/13-new-keys.md 4.9).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "sample_oscillator.h"

#include <algorithm>
#include <cmath>

#include "futils.h"
#include "synth_constants.h"
#include "synth_oscillator.h"
#include "utils.h"

namespace sbvital {
  namespace {
    using synthyboy::sampleosc::LoopMode;
    using synthyboy::sampleosc::QualityTier;
    using synthyboy::sampleosc::SampleOscParams;

    constexpr int kMaxScratch = kMaxBufferSize * kMaxOversample;

    LoopMode loopModeOf(int mode) {
      switch (mode) {
        case 1: return LoopMode::forward;
        case 2: return LoopMode::reverse;
        case 3: return LoopMode::pingPong;
        case 4: return LoopMode::tailed;
        default: return LoopMode::none;
      }
    }

    // Warp on a sample (docs/ui-design/13-new-keys.md 4.9, decided in WP4): a sample has no cycle, so the phase domain warps (Sync,
    // Formant, Bend, Squeeze, Pulse Width and the modes of block B21 that move the read phase) have nothing to bend and are inert.
    // Quantize reduces the resolution of the output, Frequency Modulation (also Thru-Zero, Exp and Linear) modulates the read speed
    // with the source, Ring and Amplitude Modulation multiply the output with it, the distortion group shapes the output (B21, 13-new-keys.md
    // 4.22). Filter, Odd/Even and the phase distortion are inert on a sample.
    enum WarpKind { kWarpNone, kWarpQuantize, kWarpFm, kWarpRm, kWarpAm, kWarpShape };
    enum FmLaw { kFmLinearLaw, kFmExpLaw, kFmClampedLaw };

    WarpKind warpKindOf(int type, const Output*& source, FmLaw& law, Output* a, Output* b, Output* sample) {
      using D = SynthOscillator;
      law = kFmLinearLaw;
      if (D::isExtendedWarp(type)) {
        if (type >= D::kShTube && type <= D::kShSoftSat)
          return kWarpShape;
        switch (type) {
          case D::kFmThruZeroA: source = a; return kWarpFm;
          case D::kFmThruZeroB: source = b; return kWarpFm;
          case D::kFmExpA: source = a; law = kFmExpLaw; return kWarpFm;
          case D::kFmExpB: source = b; law = kFmExpLaw; return kWarpFm;
          case D::kFmLinearA: source = a; law = kFmClampedLaw; return kWarpFm;
          case D::kFmLinearB: source = b; law = kFmClampedLaw; return kWarpFm;
          case D::kAmA: source = a; return kWarpAm;
          case D::kAmB: source = b; return kWarpAm;
          default: return kWarpNone;
        }
      }
      switch (type) {
        case D::kQuantize: return kWarpQuantize;
        case D::kFmOscillatorA: source = a; return kWarpFm;
        case D::kFmOscillatorB: source = b; return kWarpFm;
        case D::kFmSample: source = sample; return kWarpFm;
        case D::kRmOscillatorA: source = a; return kWarpRm;
        case D::kRmOscillatorB: source = b; return kWarpRm;
        case D::kRmSample: source = sample; return kWarpRm;
        default: return kWarpNone;
      }
    }
  } // namespace

  SampleOscillator::SampleOscillator(const SampleOscShared* shared) :
      Processor(kNumInputs, kNumOutputs), shared_(shared) {
    synthyboy::sampleosc::SincTables::instance(); // built now, not on the audio thread
    scratch_ = std::shared_ptr<float[]>(new float[3 * kMaxScratch]());
    scratch_size_ = kMaxScratch;
    for (int s = 0; s < kVoicesPerInstance; ++s)
      for (int u = 0; u < kMaxUnison; ++u)
        slots_[s].voices[u].prepare(44100.0);
  }

  void SampleOscillator::setOversampleAmount(int oversample) {
    Processor::setOversampleAmount(oversample);
    prepared_rate_ = 0; // the voices take the new rate at the next block
  }

  void SampleOscillator::reset(poly_mask) {
  }

  uint32_t SampleOscillator::nextRandom() noexcept {
    random_state_ ^= random_state_ << 13;
    random_state_ ^= random_state_ >> 17;
    random_state_ ^= random_state_ << 5;
    return random_state_;
  }

  void SampleOscillator::noteOn(Slot& slot, int lane, int unison, int num_samples) {
    (void)num_samples;
    const SampleOscShared& sh = *shared_;
    slot.running = true;
    slot.pressed = true;
    slot.released = false;
    slot.unison = unison;
    slot.view = sh.view;

    // the region: the whole start..end range, or one slice of it (slice mode: the key picks the slice)
    float lo = std::min(sh.start, sh.end), hi = std::max(sh.start, sh.end);
    slot.sliced = false;
    if (sh.slice_mode != 0 && sh.num_slices > 0) {
      int key = static_cast<int>(std::lround(input(kMidiNote)->at(0)[lane] - input(kTranspose)->at(0)[lane] -
                                           input(kTune)->at(0)[lane] - input(kFoldNote)->at(0)[lane]));
      int offset = 0;
      if (sh.slice_play == 1) // GROUPB WP-05: the slice destination moves a single slice (+-100 % = the whole slice list)
        offset = static_cast<int>(std::lround(input(kSlice)->at(0)[lane] * static_cast<float>(sh.num_slices - 1)));
      int index = std::max(0, std::min(sh.num_slices - 1, key - sh.slice_root + offset));
      lo = sh.slices[index];
      hi = sh.slice_play == 1 ? (index + 1 < sh.num_slices ? sh.slices[index + 1] : 1.0f) : 1.0f;
      slot.sliced = true;
    }
    slot.region_lo = lo;
    slot.region_hi = std::max(hi, lo + 1.0e-5f);

    SampleOscParams params;
    params.rootKey = sh.root;
    params.pitchTracking = tracksPitch(lane) && ! slot.sliced;
    params.loopMode = loopModeOf(sh.loop_mode);
    params.startOffset = slot.region_lo;
    params.endOffset = slot.region_hi;
    float ls = sh.loop_start, le = sh.loop_end;
    if (sh.loop_relative) {
      ls = slot.region_lo + ls * (slot.region_hi - slot.region_lo);
      le = slot.region_lo + le * (slot.region_hi - slot.region_lo);
    }
    params.loopStart = ls;
    params.loopEnd = le;
    const double loop_len = std::fabs(static_cast<double>(le) - ls) * static_cast<double>(sh.view->numFrames);
    params.loopCrossfade = loop_len > 1.0 ? static_cast<float>(std::min(0.49, sh.loop_xfade_ms * 0.001 * sh.view->sampleRate / loop_len)) : 0.0f;
    params.reverse = sh.reverse;
    params.edgeFadeMs = sh.fade_ms;
    params.quality = sh.quality <= 0 ? QualityTier::low : (sh.quality == 1 ? QualityTier::medium : QualityTier::high);
    params.level = 1.0f;
    params.pan = 0.0f;

    const float uni_start = utils::clamp(input(kUniStart)->at(0)[lane], 0.0f, 1.0f);
    const float uni_span = utils::clamp(input(kUniSpan)->at(0)[lane], 0.0f, 1.0f);
    const float phase = input(kPhase)->at(0)[lane];
    const float random_phase = utils::clamp(input(kRandomPhase)->at(0)[lane], 0.0f, 1.0f);
    const float rnd_phase = (nextRandom() >> 8) * (1.0f / 16777216.0f);

    for (int u = 0; u < unison; ++u) {
      synthyboy::sampleosc::SampleVoice& voice = slot.voices[u];
      voice.noteOn(60, 1.0f, sh.view, params);
      const float t = unison > 1 ? static_cast<float>(u) / static_cast<float>(unison - 1) : 0.0f;
      const float rnd = (nextRandom() >> 8) * (1.0f / 16777216.0f);
      slot.start_random[u] = rnd;
      float fraction = phase + random_phase * rnd_phase + uni_start * rnd + uni_span * t;
      fraction -= std::floor(fraction); // a start phase of 360 degrees is the start again
      voice.seekFraction(fraction);
    }
    for (int u = unison; u < kMaxUnison; ++u)
      slot.voices[u].reset();
  }

  void SampleOscillator::process(int num_samples) {
    const SampleOscShared& sh = *shared_;
    poly_float* raw = output(kRaw)->buffer;
    poly_float* levelled = output(kLevelled)->buffer;
    utils::zeroBuffer(raw, num_samples);
    utils::zeroBuffer(levelled, num_samples);
    if (pitch_tap_)
      pitch_tap_->buffer[0] = input(kMidiNote)->at(0) + input(kFoldNote)->at(0) + input(kTranspose)->at(0) + input(kTune)->at(0);

    if (num_samples > scratch_size_ || sh.view == nullptr || ! sh.view->isValid()) {
      for (Slot& slot : slots_)
        slot.running = false;
      return;
    }

    if (prepared_rate_ != getSampleRate()) {
      prepared_rate_ = getSampleRate();
      for (Slot& slot : slots_)
        for (synthyboy::sampleosc::SampleVoice& voice : slot.voices)
          voice.prepare(static_cast<double>(prepared_rate_));
    }

    const poly_float active = input(kActiveVoices)->at(0);
    const poly_mask reset_mask = getResetMask(kReset);
    const int unison = std::max(1, std::min(std::min(kMaxUnison, sh.max_unison), static_cast<int>(std::lround(input(kUnisonVoices)->at(0)[0]))));

    for (int v = 0; v < kVoicesPerInstance; ++v) {
      const int lane = 2 * v;
      Slot& slot = slots_[v];
      if (active[lane] != 1.0f) {
        slot.running = false;
        continue;
      }
      if (slot.running && slot.view != sh.view) { // the sample was replaced: the running voices must not read the old one
        slot.running = false;
        for (synthyboy::sampleosc::SampleVoice& voice : slot.voices)
          voice.reset();
      }
      if (reset_mask[lane] != 0)
        noteOn(slot, lane, unison, num_samples);
      if (! slot.running)
        continue;

      // key released: exit the loop when the settings ask for it (and Tailed always plays its tail)
      const bool pressed = input(kNotePressed)->at(0)[lane] > 0.5f;
      if (slot.pressed && ! pressed && ! slot.released) {
        slot.released = true;
        if (sh.loop_exit || sh.loop_mode == 4)
          for (int u = 0; u < slot.unison; ++u)
            slot.voices[u].releaseLoop();
      }
      slot.pressed = pressed;

      renderSlot(slot, v, unison, num_samples, raw);
    }

    // level (the control is a square root of the gain) and pan, like the wavetable oscillator
    const poly_float pan_amplitude = futils::panAmplitude(utils::clamp(input(kPan)->at(0), -1.0f, 1.0f));
    const poly_float* amplitude = input(kAmplitude)->source->buffer;
    const poly_float zero = 0.0f;
    for (int i = 0; i < num_samples; ++i) {
      poly_float amp = utils::max(amplitude[i], zero);
      levelled[i] = pan_amplitude * raw[i] * amp * amp;
    }
  }

  void SampleOscillator::renderSlot(Slot& slot, int slot_index, int unison, int num_samples, poly_float* raw) {
    const SampleOscShared& sh = *shared_;
    const int lane = 2 * slot_index;
    float* sl = scratch_.get();
    float* sr = scratch_.get() + kMaxScratch;
    float* rate_mod = scratch_.get() + 2 * kMaxScratch;

    // pitch: the note of the voice (bend, portamento) + fold + transposition + tune relative to the root key; a slice or a switched off
    // key tracking plays at the recorded pitch (+ transposition + tune)
    const float transpose = input(kTranspose)->at(0)[lane];
    const float tune = input(kTune)->at(0)[lane];
    float semitones = transpose + tune;
    if (tracksPitch(lane) && ! slot.sliced)
      semitones += input(kMidiNote)->at(0)[lane] + input(kFoldNote)->at(0)[lane] - static_cast<float>(sh.root);
    else if (input(kPitchBendTrack)->at(0)[0] > 0.5f && ! slot.sliced)
      semitones += input(kMidiNote)->at(0)[lane] - input(kRawMidiNote)->at(0)[lane]; // pitch tracking off: the bend still moves the sample

    // scan: the speed of the read head (A13): scan x range, sign flipped by Reverse, tempo lock and length-to-BPM
    static const float kRange[4] = { 1.0f, 2.0f, 4.0f, 8.0f };
    float speed = input(kScan)->at(0)[lane] * kRange[std::max(0, std::min(3, sh.scan_range))];
    if (sh.scan_reverse)
      speed = -speed;
    if (sh.scan_lock_tempo)
      speed *= static_cast<float>(sh.bpm / 120.0);
    if (sh.scan_to_bpm) {
      const double region_seconds = (slot.region_hi - slot.region_lo) * static_cast<double>(sh.view->numFrames) / sh.view->sampleRate;
      const double target_seconds = static_cast<double>(sh.length_beats) * 60.0 / std::max(1.0, sh.bpm);
      speed *= static_cast<float>(target_seconds > 1.0e-6 ? region_seconds / target_seconds : 1.0);
    }

    const float detune_cents = input(kDetuneRange)->at(0)[lane] * input(kUnisonDetune)->at(0)[lane];
    const float power = input(kDetunePower)->at(0)[lane];
    const float spread = utils::clamp(input(kStereoSpread)->at(0)[lane], 0.0f, 1.0f);
    const float blend = utils::clamp(input(kBlend)->at(0)[lane], 0.0f, 1.0f);
    const float gain_norm = sh.normalize ? sh.normalize_gain : 1.0f;

    // the two warp slots
    const Output* sources[2] = { nullptr, nullptr };
    FmLaw laws[2] = { kFmLinearLaw, kFmLinearLaw };
    const int warp_types[2] = { static_cast<int>(input(kWarpType)->at(0)[0]), static_cast<int>(input(kWarp2Type)->at(0)[0]) };
    const WarpKind kinds[2] = {
      warpKindOf(warp_types[0], sources[0], laws[0], first_mod_oscillator_, second_mod_oscillator_, sample_),
      warpKindOf(warp_types[1], sources[1], laws[1], first_mod_oscillator_, second_mod_oscillator_, sample_)
    };
    const float amounts[2] = { utils::clamp(input(kWarpAmount)->at(0)[lane], 0.0f, 1.0f), utils::clamp(input(kWarp2Amount)->at(0)[lane], 0.0f, 1.0f) };

    // unison weights (centre voice and detuned voices, normalised to equal power)
    const float centre_w = 1.0f - 0.5f * blend;
    const float detuned_w = std::max(0.25f, 1.0f - (1.0f - blend) * (1.0f - blend));
    float weights[kMaxUnison];
    float sum_squares = 0.0f;
    for (int u = 0; u < unison; ++u) {
      const float t = unison > 1 ? 2.0f * u / (unison - 1) - 1.0f : 0.0f;
      weights[u] = (unison % 2 == 1 && t == 0.0f) || unison == 1 ? centre_w : detuned_w;
      sum_squares += weights[u] * weights[u];
    }
    const float norm = sum_squares > 0.0f ? 1.0f / std::sqrt(sum_squares) : 1.0f;

    for (int u = 0; u < unison; ++u) {
      synthyboy::sampleosc::SampleVoice& voice = slot.voices[u];
      if (! voice.isActive())
        continue;
      const float t = unison > 1 ? 2.0f * u / (unison - 1) - 1.0f : 0.0f;
      const float shaped = std::copysign(futils::powerScale(std::fabs(t), power), t);
      voice.setSemitoneOffset(semitones + shaped * detune_cents * 0.01f);
      voice.setSpeedMultiplier(speed);

      // warp: Frequency Modulation of the read speed
      const float* mod = nullptr;
      for (int w = 0; w < 2; ++w)
        if (kinds[w] == kWarpFm && sources[w] != nullptr && amounts[w] > 0.0f) {
          const poly_float* src = sources[w]->buffer;
          const float depth = 4.0f * amounts[w] * amounts[w];
          for (int i = 0; i < num_samples; ++i) {
            const float s = src[i][lane];
            float deviation = depth * s;
            if (laws[w] == kFmExpLaw)
              deviation = std::exp2(deviation) - 1.0f;
            else if (laws[w] == kFmClampedLaw)
              deviation = std::max(deviation, -1.0f);
            rate_mod[i] = (mod == nullptr ? 1.0f : rate_mod[i]) + deviation;
          }
          mod = rate_mod;
        }
      voice.processModulated(sl, sr, num_samples, mod);

      for (int w = 0; w < 2; ++w) {
        if (amounts[w] <= 0.0f)
          continue;
        if (kinds[w] == kWarpRm && sources[w] != nullptr) {
          const poly_float* src = sources[w]->buffer;
          for (int i = 0; i < num_samples; ++i) {
            const float m = 1.0f + amounts[w] * (src[i][lane] - 1.0f);
            sl[i] *= m;
            sr[i] *= m;
          }
        }
        else if (kinds[w] == kWarpAm && sources[w] != nullptr) {
          const poly_float* src = sources[w]->buffer;
          for (int i = 0; i < num_samples; ++i) {
            const float m = 1.0f - amounts[w] + amounts[w] * (0.5f + 0.5f * std::max(-1.0f, std::min(1.0f, src[i][lane])));
            sl[i] *= m;
            sr[i] *= m;
          }
        }
        else if (kinds[w] == kWarpShape) {
          for (int i = 0; i < num_samples; ++i) {
            sl[i] = SynthOscillator::warpShape(warp_types[w], sl[i], amounts[w]);
            sr[i] = SynthOscillator::warpShape(warp_types[w], sr[i], amounts[w]);
          }
        }
        else if (kinds[w] == kWarpQuantize) {
          const float inv = 1.0f - amounts[w];
          const float levels = std::pow(2.0f, 1.0f + 14.0f * inv * inv * inv);
          for (int i = 0; i < num_samples; ++i) {
            sl[i] = std::round(sl[i] * levels) / levels;
            sr[i] = std::round(sr[i] * levels) / levels;
          }
        }
      }

      // stereo position of the unison voice, then into the raw output
      const poly_float position = futils::panAmplitude(poly_float(t * spread));
      const float gl = position[0] * norm * weights[u] * gain_norm, gr = position[1] * norm * weights[u] * gain_norm;
      for (int i = 0; i < num_samples; ++i) {
        raw[i].set(lane, raw[i][lane] + sl[i] * gl);
        raw[i].set(lane + 1, raw[i][lane + 1] + sr[i] * gr);
      }
    }
  }
} // namespace sbvital
