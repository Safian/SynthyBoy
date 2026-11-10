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
#include "synthyboy/granular/granular.h"

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

    WarpKind warpKindOf(int type, const Output*& source, FmLaw& law, Output* a, Output* b, Output* sample, const std::array<const Output*, 4>& extra) {
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
          default:
            if (D::isSourceWarp(type)) {
              // GROUPB WP-06: the same modes from the Noise (0), Sub (1), Filter 1 (2), Filter 2 (3); PD is a phase mode and inert on a sample
              const int src = D::sourceWarpSource(type);
              source = src == 0 ? sample : extra[static_cast<size_t>(src)];
              switch (D::sourceWarpFamily(type)) {
                case 0: return kWarpFm;
                case 1: law = kFmExpLaw; return kWarpFm;
                case 2: law = kFmClampedLaw; return kWarpFm;
                case 3: return kWarpNone;
                case 4: return kWarpAm;
                default: return kWarpRm;
              }
            }
            return kWarpNone;
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
    scratch_ = std::shared_ptr<float[]>(new float[5 * kMaxScratch]());
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

  namespace {
    // GROUPC WP-11: the beats of an entry of kGrainDivisions (3 per division: straight, triplet, dotted; 1/1 .. 1/128)
    float divisionBeats(int index) {
      index = std::max(0, std::min(23, index));
      const float beats = 4.0f / static_cast<float>(1 << (index / 3));
      const int style = index % 3;
      return style == 0 ? beats : (style == 1 ? beats * 2.0f / 3.0f : beats * 1.5f);
    }

    // The pointwise warps of a grain: Quantize and the distortion group (SynthOscillator::warpShape)
    float grainShape(int type, float x, float amount) {
      if (type == SynthOscillator::kQuantize) {
        const float inv = 1.0f - amount;
        const float levels = std::pow(2.0f, 1.0f + 14.0f * inv * inv * inv);
        return std::round(x * levels) / levels;
      }
      return SynthOscillator::warpShape(type, x, amount);
    }

    bool isGrainWarp(WarpKind kind) { return kind == kWarpShape || kind == kWarpQuantize; }
  } // namespace

  bool SampleOscillator::kindActive(const Slot& slot, int kind) const {
    switch (kind) {
      case 1: for (int u = 0; u < slot.unison; ++u) if (slot.voices[u].isActive()) return true; return false;
      case 2: for (int u = 0; u < slot.unison; ++u) if (slot.gvoices[u].isActive()) return true; return false;
      case 3: for (int u = 0; u < std::min(slot.unison, kMaxSpectralUnison); ++u) if (slot.svoices[u].isActive()) return true; return false;
      case 4: for (int u = 0; u < slot.unison; ++u) for (auto& layer : slot.layers[u]) if (layer.isActive()) return true; return false;
      default: return false;
    }
  }

  void SampleOscillator::stopKind(Slot& slot, int kind) {
    for (int u = 0; u < kMaxUnison; ++u) {
      if (kind == 1)
        slot.voices[u].reset();
      else if (kind == 2)
        slot.gvoices[u].reset();
      else if (kind == 3 && u < kMaxSpectralUnison)
        slot.svoices[u].reset();
      else if (kind == 4)
        for (auto& layer : slot.layers[u])
          layer.reset();
    }
  }

  void SampleOscillator::noteOn(Slot& slot, int lane, int unison, int num_samples) {
    (void)num_samples;
    const SampleOscShared& sh = *shared_;
    if (slot.old_kind != 0) {
      stopKind(slot, slot.old_kind);
      slot.old_kind = 0;
    }
    slot.running = true;
    slot.pressed = true;
    slot.released = false;
    slot.unison = unison;
    slot.view = sh.view;
    slot.instrument = sh.multi.instrument;

    // the region: the whole start..end range, or one slice of it (slice mode: the key picks the slice)
    float lo = std::min(sh.start, sh.end), hi = std::max(sh.start, sh.end);
    slot.sliced = false;
    if (sh.slice_mode != 0 && sh.num_slices > 0 && !(slot.kind == 2 && sh.gran.manual)) {
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
    startKind(slot, slot.kind, lane, unison);
  }

  void SampleOscillator::startKind(Slot& slot, int kind, int lane, int unison) {
    const SampleOscShared& sh = *shared_;
    // the pitch of the note (as in renderSlot): the grains / frames that are made at the note on already play at it
    float note_semitones = input(kTranspose)->at(0)[lane] + input(kTune)->at(0)[lane];
    if (tracksPitch(lane) && ! slot.sliced)
      note_semitones += input(kMidiNote)->at(0)[lane] + input(kFoldNote)->at(0)[lane] - static_cast<float>(sh.root);
    else if (input(kPitchBendTrack)->at(0)[0] > 0.5f && ! slot.sliced)
      note_semitones += input(kMidiNote)->at(0)[lane] - input(kRawMidiNote)->at(0)[lane];
    const float note_detune_cents = input(kDetuneRange)->at(0)[lane] * input(kUnisonDetune)->at(0)[lane];
    const float note_power = input(kDetunePower)->at(0)[lane];
    const auto unison_ratio = [&](int u) {
      const float t = unison > 1 ? 2.0f * u / (unison - 1) - 1.0f : 0.0f;
      const float shaped = std::copysign(futils::powerScale(std::fabs(t), note_power), t);
      return std::exp2((note_semitones + shaped * note_detune_cents * 0.01f) / 12.0f);
    };
    const float uni_start = utils::clamp(input(kUniStart)->at(0)[lane], 0.0f, 1.0f);
    const float uni_span = utils::clamp(input(kUniSpan)->at(0)[lane], 0.0f, 1.0f);
    const float phase = input(kPhase)->at(0)[lane];
    const float random_phase = utils::clamp(input(kRandomPhase)->at(0)[lane], 0.0f, 1.0f);
    const float rnd_phase = (nextRandom() >> 8) * (1.0f / 16777216.0f);

    if (kind == 2) {
      synthyboy::granular::GrainParams gp;
      float level = 1.0f;
      float warp_amounts[2];
      buildGrainParams(slot, lane, unison, gp, level, warp_amounts, tracksPitch(lane) && !slot.sliced);
      for (int u = 0; u < unison; ++u) {
        const float t = unison > 1 ? static_cast<float>(u) / static_cast<float>(unison - 1) : 0.0f;
        const float rnd = (nextRandom() >> 8) * (1.0f / 16777216.0f);
        float spawn = 0.0f;
        switch (sh.gran.spawn_pattern) {
          case 1: spawn = static_cast<float>(u) / static_cast<float>(unison); break;
          case 2: spawn = std::exp2(static_cast<float>(u) / static_cast<float>(unison)) - 1.0f; break;
          case 3: spawn = (nextRandom() >> 8) * (1.0f / 16777216.0f); break;
          default: break;
        }
        float fraction = phase + random_phase * rnd_phase + uni_start * rnd + uni_span * t;
        fraction -= std::floor(fraction);
        slot.start_random[u] = rnd;
        gp.pitchRatio = unison_ratio(u);
        slot.gvoices[u].noteOn(sh.view, gp, nextRandom() | 1u, spawn, sh.gran.jump_start, fraction);
      }
      for (int u = unison; u < kMaxUnison; ++u)
        slot.gvoices[u].reset();
      return;
    }

    if (kind == 4) {
      // the zones of the note: the key looked up through the timbre, the velocity, a random number, the round robin counter, the key switch
      const synthyboy::multisample::Instrument* instrument = sh.multi.instrument;
      for (int u = 0; u < kMaxUnison; ++u)
        for (auto& layer : slot.layers[u])
          layer.reset();
      if (instrument == nullptr)
        return;
      const int key = std::max(0, std::min(127, static_cast<int>(std::lround(input(kMidiNote)->at(0)[lane] + input(kFoldNote)->at(0)[lane]))));
      if (instrument->isKeySwitchKey(key))
        sh.multi.switch_last = key;
      const int velocity = std::max(1, std::min(127, static_cast<int>(std::lround(input(kVelocity)->at(0)[lane] * 127.0f))));
      const float timbre = utils::clamp(input(kMultiTimbre)->at(0)[lane], -1.0f, 1.0f);
      const int lookup = synthyboy::multisample::Instrument::timbreKey(key, timbre);
      const float rnd = (nextRandom() >> 8) * (1.0f / 16777216.0f);
      int zones[synthyboy::multisample::kMaxLayers];
      const uint32_t counter = sh.multi.round_robin++;
      const int count = instrument->selectZones(lookup, velocity, rnd, counter, sh.multi.switch_last >= 0 ? sh.multi.switch_last : instrument->switchDefault(), zones, synthyboy::multisample::kMaxLayers);
      sh.multi.last_zone = count > 0 ? zones[0] : -1;
      sh.multi.last_key = key;
      const synthyboy::sampleosc::QualityTier tier = sh.quality <= 0 ? QualityTier::low : (sh.quality == 1 ? QualityTier::medium : QualityTier::high);
      slot.layer_count = count;
      for (int l = 0; l < count; ++l) {
        const auto& region = instrument->zone(zones[l]).region;
        if (region.group > 0 && region.group < synthyboy::multisample::kMaxGroups)
          sh.multi.group_trigger[region.group] = sh.multi.sequence + 1;
      }
      const uint32_t sequence = ++sh.multi.sequence;
      for (int u = 0; u < unison; ++u) {
        const float t = unison > 1 ? static_cast<float>(u) / static_cast<float>(unison - 1) : 0.0f;
        const float rnd_u = (nextRandom() >> 8) * (1.0f / 16777216.0f);
        slot.start_random[u] = rnd_u;
        float fraction = phase + random_phase * rnd_phase + uni_start * rnd_u + uni_span * t;
        fraction -= std::floor(fraction);
        for (int l = 0; l < count; ++l) {
          slot.layers[u][l].noteOn(*instrument, zones[l], key, velocity, sh.multi.envelope_override ? &sh.multi.envelope : nullptr,
                                   sh.multi.vel_override ? sh.multi.vel_track : -1.0f, sequence, tier);
          if (fraction > 0.0f && uni_start + uni_span + random_phase > 0.0f)
            slot.layers[u][l].seekFraction(fraction);
        }
      }
      return;
    }

    if (kind == 3) {
      synthyboy::spectral::SpectralParams sp;
      float level = 1.0f;
      float warp_amounts[2];
      buildSpectralParams(slot, lane, unison, sp, level, warp_amounts);
      const int voices = std::min(unison, kMaxSpectralUnison);
      for (int u = 0; u < voices; ++u) {
        const float t = unison > 1 ? static_cast<float>(u) / static_cast<float>(unison - 1) : 0.0f;
        const float rnd = (nextRandom() >> 8) * (1.0f / 16777216.0f);
        slot.start_random[u] = rnd;
        float fraction = phase + random_phase * rnd_phase + uni_start * rnd + uni_span * t;
        fraction -= std::floor(fraction);
        sp.pitchRatio = unison_ratio(u);
        slot.svoices[u].noteOn(sh.spec.analysis, sp, fraction);
      }
      for (int u = voices; u < kMaxSpectralUnison; ++u)
        slot.svoices[u].reset();
      return;
    }

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

    const int type_now = std::max(1, std::min(4, static_cast<int>(std::lround(input(kType)->at(0)[0]))));
    // a Multisample oscillator plays its instrument: it needs no sample; the other types need the sample of the content
    const bool playable = type_now == 4 ? sh.multi.instrument != nullptr : (sh.view != nullptr && sh.view->isValid());
    if (num_samples > scratch_size_ || ! playable) {
      for (Slot& slot : slots_)
        slot.running = false;
      return;
    }

    if (prepared_rate_ != getSampleRate()) {
      prepared_rate_ = getSampleRate();
      for (Slot& slot : slots_) {
        for (synthyboy::sampleosc::SampleVoice& voice : slot.voices)
          voice.prepare(static_cast<double>(prepared_rate_));
        for (synthyboy::granular::GranularVoice& voice : slot.gvoices)
          voice.prepare(static_cast<double>(prepared_rate_));
        for (synthyboy::spectral::SpectralVoice& voice : slot.svoices)
          voice.prepare(static_cast<double>(prepared_rate_));
        for (auto& unison_layers : slot.layers)
          for (auto& layer : unison_layers)
            layer.prepare(static_cast<double>(prepared_rate_));
      }
    }

    const poly_float active = input(kActiveVoices)->at(0);
    const poly_mask reset_mask = getResetMask(kReset);
    const int unison = std::max(1, std::min(std::min(kMaxUnison, sh.max_unison), static_cast<int>(std::lround(input(kUnisonVoices)->at(0)[0]))));
    const int kind = std::max(1, std::min(4, static_cast<int>(std::lround(input(kType)->at(0)[0]))));
    const int spectral_unison = std::min(unison, kMaxSpectralUnison);
    (void)spectral_unison;

    for (int v = 0; v < kVoicesPerInstance; ++v) {
      const int lane = 2 * v;
      Slot& slot = slots_[v];
      if (active[lane] != 1.0f) {
        slot.running = false;
        continue;
      }
      if (slot.running && (slot.view != sh.view || (slot.kind == 4 && slot.instrument != sh.multi.instrument))) { // the sample / the instrument was replaced: the running voices must not read the old one
        slot.running = false;
        stopKind(slot, slot.kind);
        if (slot.old_kind != 0)
          stopKind(slot, slot.old_kind);
        slot.old_kind = 0;
      }
      if (reset_mask[lane] != 0) {
        slot.kind = kind;
        noteOn(slot, lane, unison, num_samples);
      }
      else if (slot.running && slot.kind != kind) {
        // the type changed under a running note: the new kind starts now, the old one fades out over 10 ms
        if (slot.old_kind != 0)
          stopKind(slot, slot.old_kind);
        slot.old_kind = slot.kind;
        slot.kind = kind;
        slot.fade_pos = 0;
        slot.fade_len = std::max(1, static_cast<int>(0.01 * getSampleRate()));
        slot.unison = unison;
        startKind(slot, kind, lane, unison);
      }
      if (! slot.running)
        continue;

      // key released: exit the loop when the settings ask for it (and Tailed always plays its tail)
      const bool pressed = input(kNotePressed)->at(0)[lane] > 0.5f;
      if (slot.pressed && ! pressed && ! slot.released) {
        slot.released = true;
        if (sh.loop_exit || sh.loop_mode == 4)
          for (int u = 0; u < slot.unison; ++u) {
            slot.voices[u].releaseLoop();
            slot.gvoices[u].releaseLoop();
            if (u < kMaxSpectralUnison)
              slot.svoices[u].releaseLoop();
          }
        if (slot.kind == 4 || slot.old_kind == 4)
          for (int u = 0; u < slot.unison; ++u)
            for (auto& layer : slot.layers[u])
              layer.noteOff(false);
      }
      slot.pressed = pressed;

      if (slot.old_kind != 0) {
        const float r0 = static_cast<float>(slot.fade_pos) / static_cast<float>(slot.fade_len);
        const float r1 = std::min(1.0f, static_cast<float>(slot.fade_pos + num_samples) / static_cast<float>(slot.fade_len));
        renderSlot(slot, slot.kind, v, unison, num_samples, raw, r0, r1);
        renderSlot(slot, slot.old_kind, v, unison, num_samples, raw, 1.0f - r0, 1.0f - r1);
        slot.fade_pos += num_samples;
        if (slot.fade_pos >= slot.fade_len) {
          stopKind(slot, slot.old_kind);
          slot.old_kind = 0;
        }
      }
      else
        renderSlot(slot, slot.kind, v, unison, num_samples, raw, 1.0f, 1.0f);
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

  // The settings of a grain cloud for this block from the keys (SampleOscShared::gran, the modulatable inputs), the Y axis of the X|Y control and the region of the slot.
  void SampleOscillator::buildGrainParams(const Slot& slot, int lane, int unison, synthyboy::granular::GrainParams& gp, float& level, float* warp_amounts, bool tracks) const {
    using namespace synthyboy::granular;
    const SampleOscShared& sh = *shared_;
    const SampleOscShared::Granular& g = sh.gran;
    const auto in = [&](int what) { return input(what)->at(0)[lane]; };
    const double bpm = std::max(1.0, sh.bpm);
    const double sr = static_cast<double>(getSampleRate());

    // the X|Y control: while an axis is assigned, the dot's Y (0 .. 1) sets the normalised value of that parameter
    const float y = utils::clamp(in(kGranXyY), 0.0f, 1.0f);
    const int axis = g.y_axis;
    const auto yOr = [&](int target, float value) { return axis == target ? y : value; };

    float density = in(kGranDensity);
    if (axis == 4)
      density = 0.1f * std::pow(10.0f, 4.0f * y);
    float length_ms = in(kGranLength);
    if (axis == 5)
      length_ms = std::pow(4000.0f, y);
    float length_pct = in(kGranLengthPercent);
    float density_hz = density;
    if (g.density_mode == 1)
      density_hz = static_cast<float>(bpm / 60.0 / divisionBeats(g.density_sync));
    float length_sec = length_ms * 0.001f;
    if (g.length_mode == 1)
      length_sec = static_cast<float>(divisionBeats(g.length_sync) * 60.0 / bpm);
    if (g.density_mode == 2) {
      // Grains: a constant number of grains plays at a time -> the rate follows the grain length (the percent mode is not offered with it)
      density_hz = static_cast<float>(g.density_grains) / std::max(0.001f, length_sec);
    }
    else if (g.length_mode == 2)
      length_sec = length_pct * 0.01f / std::max(0.05f, density_hz);
    gp.densityHz = utils::clamp(density_hz, 0.05f, 2000.0f);
    gp.lengthSec = utils::clamp(length_sec, 0.001f, 4.0f);
    gp.maxGrains = std::max(1, std::min(kMaxGrains, g.max_grains / std::max(1, unison)));
    gp.shape = static_cast<WindowShape>(std::max(0, std::min(kNumWindowShapes - 1, g.window_shape)));
    gp.windowAmount = yOr(6, utils::clamp(in(kGranWindowAmount), 0.0f, 1.0f));
    gp.windowSkew = axis == 7 ? 2.0f * y - 1.0f : utils::clamp(in(kGranWindowSkew), -1.0f, 1.0f);
    gp.randOffset = yOr(8, utils::clamp(in(kGranRandOffset), 0.0f, 1.0f));
    gp.randDir = yOr(9, utils::clamp(in(kGranRandDir), 0.0f, 1.0f));
    gp.randPitch = axis == 10 ? 24.0f * y : utils::clamp(in(kGranRandPitch), 0.0f, 24.0f);
    gp.randLength = yOr(11, utils::clamp(in(kGranRandLength), 0.0f, 1.0f));
    gp.randPan = yOr(12, utils::clamp(in(kGranRandPan), 0.0f, 1.0f));
    gp.randGain = yOr(13, utils::clamp(in(kGranRandLevel), 0.0f, 1.0f));
    gp.randWindow = yOr(14, utils::clamp(in(kGranRandWindow), 0.0f, 1.0f));
    gp.randSkew = yOr(15, utils::clamp(in(kGranRandSkew), 0.0f, 1.0f));
    gp.randWarp[0] = yOr(16, utils::clamp(in(kGranRandWarp), 0.0f, 1.0f));
    gp.randWarp[1] = yOr(17, utils::clamp(in(kGranRandWarp2), 0.0f, 1.0f));
    gp.reverseGrains = g.reverse_grains;
    level = axis == 1 ? y : 1.0f;

    // region, loop, play head
    gp.regionLo = slot.region_lo;
    gp.regionHi = slot.region_hi;
    gp.loopMode = loopModeOf(sh.loop_mode);
    float ls = sh.loop_start, le = sh.loop_end;
    if (sh.loop_relative) {
      ls = slot.region_lo + ls * (slot.region_hi - slot.region_lo);
      le = slot.region_lo + le * (slot.region_hi - slot.region_lo);
    }
    gp.loopLo = ls;
    gp.loopHi = le;
    gp.loopGrains = g.loop_grains;
    const double loop_len = std::fabs(static_cast<double>(le) - ls) * static_cast<double>(sh.view->numFrames);
    gp.loopCrossfade = loop_len > 1.0 ? static_cast<float>(std::min(0.49, sh.loop_xfade_ms * 0.001 * sh.view->sampleRate / loop_len)) : 0.0f;
    gp.manual = g.manual;
    gp.position = utils::clamp(in(kGranPosition), 0.0f, 1.0f);

    // scan speed (the same as the sample oscillator: scan x range, Reverse, tempo lock, length to BPM) and the key tracking of the scan rate
    static const float kRange[4] = { 1.0f, 2.0f, 4.0f, 8.0f };
    float speed = in(kScan) * kRange[std::max(0, std::min(3, sh.scan_range))];
    if (sh.scan_reverse)
      speed = -speed;
    if (sh.scan_lock_tempo)
      speed *= static_cast<float>(sh.bpm / 120.0);
    if (sh.scan_to_bpm) {
      const double region_seconds = (slot.region_hi - slot.region_lo) * static_cast<double>(sh.view->numFrames) / sh.view->sampleRate;
      const double target_seconds = static_cast<double>(sh.length_beats) * 60.0 / std::max(1.0, sh.bpm);
      speed *= static_cast<float>(target_seconds > 1.0e-6 ? region_seconds / target_seconds : 1.0);
    }
    if (g.scan_keytrack)
      speed *= std::exp2((in(kMidiNote) + in(kFoldNote) - 60.0f) / 12.0f);
    gp.scanSpeed = speed;
    (void)sr;
    (void)tracks;

    // the pointwise warps are applied to every grain (their amount is randomised per grain); the others act on the mix
    const Output* sources[2] = { nullptr, nullptr };
    FmLaw laws[2] = { kFmLinearLaw, kFmLinearLaw };
    const int types[2] = { static_cast<int>(input(kWarpType)->at(0)[0]), static_cast<int>(input(kWarp2Type)->at(0)[0]) };
    const float amounts[2] = { axis == 2 ? y : utils::clamp(in(kWarpAmount), 0.0f, 1.0f), axis == 3 ? y : utils::clamp(in(kWarp2Amount), 0.0f, 1.0f) };
    gp.shapeFn = grainShape;
    for (int w = 0; w < 2; ++w) {
      const WarpKind k = warpKindOf(types[w], sources[w], laws[w], first_mod_oscillator_, second_mod_oscillator_, sample_, *extra_sources_);
      warp_amounts[w] = amounts[w];
      gp.shapeType[w] = isGrainWarp(k) ? types[w] : -1;
      gp.shapeAmount[w] = amounts[w];
    }
  }

  // The block's external signals for the mask and vocode warps: the lane of the first / second other oscillator and of the noise is appended to a ring, the magnitude spectrum of
  // the last 2048 samples is computed once per block (only when a warp needs it).
  void SampleOscillator::updateExternalSpectra(Slot& slot, int lane, int num_samples, const int* warp_types) {
    using namespace synthyboy::spectral;
    bool needed[3] = { false, false, false };
    for (int w = 0; w < 2; ++w)
      if (SynthOscillator::isSpectralWarp(warp_types[w])) {
        const int index = warp_types[w] - SynthOscillator::kSpecComb;
        if (isExternalWarp(index))
          needed[externalSource(index)] = true;
      }
    const Output* outputs[3] = { first_mod_oscillator_, second_mod_oscillator_, sample_ };
    for (int s = 0; s < 3; ++s) {
      if (!needed[s] || outputs[s] == nullptr)
        continue;
      const poly_float* src = outputs[s]->buffer;
      float* ring = slot.ext_ring[s];
      for (int i = 0; i < num_samples; ++i)
        ring[(slot.ext_pos + i) & (kFftSize - 1)] = src[i][lane];
    }
    slot.ext_pos = (slot.ext_pos + num_samples) & (kFftSize - 1);
    Scratch& scratch = *spectral_scratch_;
    for (int s = 0; s < 3; ++s) {
      if (!needed[s] || outputs[s] == nullptr)
        continue;
      for (int n = 0; n < kFftSize; ++n) {
        const float w = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(n) / kFftSize);
        scratch.re[n] = slot.ext_ring[s][(slot.ext_pos + n) & (kFftSize - 1)] * w;
        scratch.im[n] = 0.0f;
      }
      Fft::instance().forward(scratch.re, scratch.im);
      for (int k = 0; k < kBins; ++k)
        slot.ext_mag[s][k] = std::sqrt(scratch.re[k] * scratch.re[k] + scratch.im[k] * scratch.im[k]);
    }
  }

  // The settings of the spectral voices for this block (the keys, the X|Y control, the region of the slot, the drawn filter).
  void SampleOscillator::buildSpectralParams(Slot& slot, int lane, int unison, synthyboy::spectral::SpectralParams& sp, float& level, float* warp_amounts) {
    using namespace synthyboy::spectral;
    const SampleOscShared& sh = *shared_;
    const SampleOscShared::Spectral& g = sh.spec;
    const auto in = [&](int what) { return input(what)->at(0)[lane]; };
    const float y = utils::clamp(in(kSpecXyY), 0.0f, 1.0f);
    const int axis = g.y_axis;
    sp.scratch = spectral_scratch_.get();
    sp.hostRate = static_cast<double>(getSampleRate());
    sp.regionLo = slot.region_lo;
    sp.regionHi = slot.region_hi;
    sp.loopMode = loopModeOf(sh.loop_mode);
    float ls = sh.loop_start, le = sh.loop_end;
    if (sh.loop_relative) {
      ls = slot.region_lo + ls * (slot.region_hi - slot.region_lo);
      le = slot.region_lo + le * (slot.region_hi - slot.region_lo);
    }
    sp.loopLo = ls;
    sp.loopHi = le;
    sp.manual = g.manual;
    sp.position = utils::clamp(in(kSpecPosition), 0.0f, 1.0f);
    static const float kRange[4] = { 1.0f, 2.0f, 4.0f, 8.0f };
    float speed = in(kScan) * kRange[std::max(0, std::min(3, sh.scan_range))];
    if (sh.scan_reverse)
      speed = -speed;
    if (sh.scan_lock_tempo)
      speed *= static_cast<float>(sh.bpm / 120.0);
    if (sh.scan_to_bpm) {
      const double region_seconds = (slot.region_hi - slot.region_lo) * static_cast<double>(sh.view->numFrames) / sh.view->sampleRate;
      const double target_seconds = static_cast<double>(sh.length_beats) * 60.0 / std::max(1.0, sh.bpm);
      speed *= static_cast<float>(target_seconds > 1.0e-6 ? region_seconds / target_seconds : 1.0);
    }
    if (g.scan_keytrack)
      speed *= std::exp2((in(kMidiNote) + in(kFoldNote) - 60.0f) / 12.0f);
    sp.scanSpeed = speed;

    float lo = axis == 6 ? 20.0f * std::pow(1000.0f, y) : in(kSpecFreqLo);
    float hi = axis == 7 ? 20.0f * std::pow(1000.0f, y) : in(kSpecFreqHi);
    sp.freqLoHz = utils::clamp(lo, 20.0f, 20000.0f);
    sp.freqHiHz = utils::clamp(hi, 20.0f, 20000.0f);
    sp.bandSmooth = g.lohi_smooth;
    sp.bandPostWarp = g.lohi_post_warp;
    sp.phaseLock = g.phase_lock;
    sp.transients = g.transients;
    sp.freqShiftHz = utils::clamp(in(kSpecFreqShift), -2000.0f, 2000.0f);
    const float cut = axis == 4 ? 2.0f * y - 1.0f : utils::clamp(in(kSpecCut), -1.0f, 1.0f);
    const float mix = axis == 5 ? y : utils::clamp(in(kSpecMix), 0.0f, 1.0f);
    level = axis == 1 ? y : 1.0f;

    // the spectral filter: the drawn curve (flat without one) shifted by CUT, mixed wet / dry
    // (REMAINDER item 2: a wavetable can act as the filter instead of the drawn curve: the frame at the position, two neighbours blended)
    const bool bank = g.flt_bank != nullptr && g.flt_frames > 0;
    if (g.has_filter || bank) {
      float frame_position = 0.0f;
      if (bank)
        frame_position = utils::clamp(in(kSpecFltFrame), 0.0f, 1.0f) * static_cast<float>(g.flt_frames - 1);
      const uint32_t base = bank ? g.flt_bank_id : g.filter_id;
      const uint32_t key = base * 2654435761u ^ static_cast<uint32_t>(std::lround((cut + 1.0f) * 4000.0f)) * 40503u ^ static_cast<uint32_t>(std::lround(mix * 1000.0f)) * 9973u ^ static_cast<uint32_t>(sh.view->sampleRate)
                           ^ static_cast<uint32_t>(std::lround(frame_position * 256.0f)) * 69061u;
      if (key != slot.filter_key) {
        slot.filter_key = key;
        static const struct Axis { float x[257]; Axis() { for (int i = 0; i < 257; ++i) x[i] = static_cast<float>(i) / 256.0f; } } axis_table;
        const float* table = g.filter_table;
        if (bank) {
          const int f0 = std::min(static_cast<int>(frame_position), g.flt_frames - 1);
          const int f1 = std::min(f0 + 1, g.flt_frames - 1);
          const float t = frame_position - static_cast<float>(f0);
          const float* a = g.flt_bank + static_cast<size_t>(f0) * 257;
          const float* b = g.flt_bank + static_cast<size_t>(f1) * 257;
          for (int i = 0; i < 257; ++i)
            slot.filter_blend[i] = a[i] + (b[i] - a[i]) * t;
          table = slot.filter_blend;
        }
        buildFilterGains(slot.filter_gains, axis_table.x, table, 257, cut, mix, sh.view->sampleRate / kFftSize); // the filter acts on the bins of the sample (its own sample rate)
      }
      sp.filterGain = slot.filter_gains;
    }
    else
      sp.filterGain = nullptr;

    const int types[2] = { static_cast<int>(input(kWarpType)->at(0)[0]), static_cast<int>(input(kWarp2Type)->at(0)[0]) };
    const float amounts[2] = { axis == 2 ? y : utils::clamp(in(kWarpAmount), 0.0f, 1.0f), axis == 3 ? y : utils::clamp(in(kWarp2Amount), 0.0f, 1.0f) };
    const float vars[2] = { utils::clamp(in(kWarpVar), 0.0f, 1.0f), utils::clamp(in(kWarp2Var), 0.0f, 1.0f) };
    for (int w = 0; w < 2; ++w) {
      warp_amounts[w] = amounts[w];
      sp.warpType[w] = SynthOscillator::isSpectralWarp(types[w]) ? types[w] - SynthOscillator::kSpecComb : -1;
      sp.warpAmount[w] = amounts[w];
      sp.warpVar[w] = vars[w];
    }
    for (int s = 0; s < 3; ++s)
      sp.external[s] = slot.ext_mag[s];
    (void)unison;
  }

  void SampleOscillator::renderSlot(Slot& slot, int kind, int slot_index, int unison, int num_samples, poly_float* raw, float ramp0, float ramp1) {
    const SampleOscShared& sh = *shared_;
    const int lane = 2 * slot_index;
    float* sl = scratch_.get();
    float* sr = scratch_.get() + kMaxScratch;
    float* rate_mod = scratch_.get() + 2 * kMaxScratch;
    const bool granular = kind == 2;
    const bool spectral = kind == 3;
    const bool multi = kind == 4;

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
    if (sh.scan_to_bpm && sh.view != nullptr) {
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
      warpKindOf(warp_types[0], sources[0], laws[0], first_mod_oscillator_, second_mod_oscillator_, sample_, *extra_sources_),
      warpKindOf(warp_types[1], sources[1], laws[1], first_mod_oscillator_, second_mod_oscillator_, sample_, *extra_sources_)
    };
    float amounts[2] = { utils::clamp(input(kWarpAmount)->at(0)[lane], 0.0f, 1.0f), utils::clamp(input(kWarp2Amount)->at(0)[lane], 0.0f, 1.0f) };

    synthyboy::granular::GrainParams gp;
    float gran_level = 1.0f;
    if (granular) {
      float grain_amounts[2];
      buildGrainParams(slot, lane, unison, gp, gran_level, grain_amounts, true);
      amounts[0] = grain_amounts[0];
      amounts[1] = grain_amounts[1];
    }

    synthyboy::spectral::SpectralParams sp;
    float spec_level = 1.0f;
    if (spectral) {
      float spec_amounts[2];
      buildSpectralParams(slot, lane, unison, sp, spec_level, spec_amounts);
      updateExternalSpectra(slot, lane, num_samples, warp_types);
    }
    const int voices = spectral ? std::min(unison, kMaxSpectralUnison) : unison;
    if (multi) {
      // choke groups: a layer whose off_by group was triggered after it started is released (off_mode: fast 10 ms / normal)
      for (int u = 0; u < unison; ++u)
        for (auto& layer : slot.layers[u])
          if (layer.isActive() && layer.offBy() > 0 && layer.offBy() < synthyboy::multisample::kMaxGroups && sh.multi.group_trigger[layer.offBy()] > layer.startSequence())
            layer.noteOff(layer.offFast());
    }

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

    for (int u = 0; u < voices; ++u) {
      bool voice_active = spectral ? slot.svoices[u].isActive() : (granular ? slot.gvoices[u].isActive() : slot.voices[u].isActive());
      if (multi) {
        voice_active = false;
        for (auto& layer : slot.layers[u])
          voice_active = voice_active || layer.isActive();
      }
      if (! voice_active)
        continue;
      const float t = unison > 1 ? 2.0f * u / (unison - 1) - 1.0f : 0.0f;
      const float shaped = std::copysign(futils::powerScale(std::fabs(t), power), t);

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
      if (multi) {
        // every zone of the note at its own recorded pitch: the note relative to the key centre of the zone (the root key of the sample oscillator does not apply)
        const bool tracks = tracksPitch(lane) && ! slot.sliced;
        const float detune = shaped * detune_cents * 0.01f;
        std::fill(sl, sl + num_samples, 0.0f);
        std::fill(sr, sr + num_samples, 0.0f);
        float* tmp_left = scratch_.get() + 3 * kMaxScratch;
        float* tmp_right = scratch_.get() + 4 * kMaxScratch;
        for (int l = 0; l < slot.layer_count; ++l) {
          const auto& zone_region = sh.multi.instrument->zone(slot.layers[u][l].zoneIndex()).region;
          const float relative = semitones + detune + (tracks ? static_cast<float>(sh.root - zone_region.center) : 0.0f);
          slot.layers[u][l].processAdd(sl, sr, tmp_left, tmp_right, num_samples, relative, mod, 1.0f, 1.0f);
        }
      }
      else if (spectral) {
        // Frequency Modulation of the read speed has no per sample meaning here: the block mean of the deviation scales the pitch
        float mean_rate = 1.0f;
        if (mod != nullptr) {
          float sum = 0.0f;
          for (int i = 0; i < num_samples; ++i)
            sum += mod[i];
          mean_rate = std::max(0.1f, sum / static_cast<float>(std::max(1, num_samples)));
        }
        sp.pitchRatio = std::exp2((semitones + shaped * detune_cents * 0.01f) / 12.0f) * mean_rate;
        sp.gain = spec_level;
        slot.svoices[u].process(sp, sl, sr, num_samples);
      }
      else if (granular) {
        gp.pitchRatio = std::exp2((semitones + shaped * detune_cents * 0.01f) / 12.0f);
        gp.gain = gran_level;
        slot.gvoices[u].process(gp, sl, sr, num_samples, mod);
      }
      else {
        synthyboy::sampleosc::SampleVoice& voice = slot.voices[u];
        voice.setSemitoneOffset(semitones + shaped * detune_cents * 0.01f);
        voice.setSpeedMultiplier(speed);
        voice.processModulated(sl, sr, num_samples, mod);
      }

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
        else if (granular && isGrainWarp(kinds[w])) {
          // applied to every grain already
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

      // stereo position of the unison voice, then into the raw output (ramp: the fade of a change of the type)
      const poly_float position = futils::panAmplitude(poly_float(t * spread));
      const float gl = position[0] * norm * weights[u] * gain_norm, gr = position[1] * norm * weights[u] * gain_norm;
      const float ramp_step = num_samples > 0 ? (ramp1 - ramp0) / static_cast<float>(num_samples) : 0.0f;
      for (int i = 0; i < num_samples; ++i) {
        const float ramp = ramp0 + ramp_step * static_cast<float>(i);
        raw[i].set(lane, raw[i][lane] + sl[i] * gl * ramp);
        raw[i].set(lane + 1, raw[i][lane + 1] + sr[i] * gr * ramp);
      }
    }
  }
} // namespace sbvital
