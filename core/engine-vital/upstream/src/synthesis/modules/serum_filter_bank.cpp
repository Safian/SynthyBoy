// SynthyBoy (AGPL-3.0-or-later): the filter models of state version 3, rendered by core/filters-serum.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "serum_filter_bank.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "synth_constants.h"
#include "utils.h"
#include "synthyboy/filters/filter_types.h"

namespace sbvital {
  namespace {
    using synthyboy::filters::FilterType;
    constexpr int kScratch = kMaxBufferSize * kMaxOversample;
  } // namespace

  namespace {
    constexpr double kTwoPi = 6.283185307179586;

    void bellCoefficients(float frequency, float gain_db, float q, double sample_rate, double* b, double* a) {
      const double A = std::pow(10.0, gain_db / 40.0);
      const double w0 = kTwoPi * std::min<double>(frequency, 0.45 * sample_rate) / sample_rate;
      const double alpha = std::sin(w0) / (2.0 * q);
      const double cs = std::cos(w0);
      const double a0 = 1.0 + alpha / A;
      b[0] = (1.0 + alpha * A) / a0;
      b[1] = -2.0 * cs / a0;
      b[2] = (1.0 - alpha * A) / a0;
      a[0] = -2.0 * cs / a0;
      a[1] = (1.0 - alpha / A) / a0;
    }

    double bellMagnitudeDb(float frequency, float gain_db, float q, double f, double sample_rate) {
      double b[3], a[2];
      bellCoefficients(frequency, gain_db, q, sample_rate, b, a);
      const double w = kTwoPi * f / sample_rate;
      const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
      const std::complex<double> num = b[0] + b[1] * z1 + b[2] * z2, den = 1.0 + a[0] * z1 + a[1] * z2;
      return 20.0 * std::log10(std::max(1.0e-9, std::abs(num / den)));
    }
  } // namespace

  void FilterCurveShared::fit(const float* x, const float* y, int count) {
    num_bells = 0;
    count = std::max(0, std::min(kMaxBells, count));
    if (count == 0)
      return;
    for (int i = 0; i < count; ++i) {
      frequency[i] = 20.0f * std::pow(1000.0f, std::max(0.0f, std::min(1.0f, x[i])));
      gain_db[i] = std::max(-36.0f, std::min(36.0f, y[i]));
      float octaves = 2.0f;
      if (count > 1) {
        const float left = i > 0 ? std::log2(frequency[i] / frequency[i - 1]) : std::log2(frequency[i + 1] / frequency[i]);
        const float right = i + 1 < count ? std::log2(frequency[i + 1] / frequency[i]) : left;
        octaves = std::max(0.5f, std::min(4.0f, 0.5f * (left + right)));
      }
      q[i] = std::max(0.3f, std::min(8.0f, static_cast<float>(1.0 / (2.0 * std::sinh(0.34657359 * octaves)))));
    }
    // the bells overlap: correct each gain by the error of the whole cascade at its centre (a few passes of a fixed point iteration)
    std::vector<double> target(count);
    for (int i = 0; i < count; ++i)
      target[i] = y[i];
    for (int pass = 0; pass < 12; ++pass)
      for (int j = 0; j < count; ++j) {
        double response = 0.0;
        for (int i = 0; i < count; ++i)
          response += bellMagnitudeDb(frequency[i], gain_db[i], q[i], frequency[j], 48000.0);
        gain_db[j] = std::max(-36.0f, std::min(36.0f, gain_db[j] + 0.7f * static_cast<float>(target[j] - response)));
      }
    num_bells = count;
  }

  void StereoCutoffOffset::process(int num_samples) {
    const poly_float* in = input(kCutoff)->source->buffer;
    poly_float* out = output()->buffer;
    // the pan control is -1 .. 1, +-100 % = +-12 semitones in opposite directions in the two channels
    const poly_float offset = input(kPan)->at(0) * 12.0f * poly_float(-1.0f, 1.0f, -1.0f, 1.0f);
    for (int i = 0; i < num_samples; ++i)
      out[i] = in[i] + offset;
  }

  void CleanInputGain::process(int num_samples) {
    const poly_float* in = input(kAudio)->source->buffer;
    poly_float* out = output()->buffer;
    const int model = static_cast<int>(std::lround(input(kModel)->at(0)[0]));
    const bool active = input(kClean)->at(0)[0] > 0.5f && model != constants::kComb && model != constants::kFormant;
    const mono_float target = active ? std::pow(10.0f, -kCleanDb * 0.05f) : 1.0f;
    if (gain_ == 1.0f && target == 1.0f) {
      for (int i = 0; i < num_samples; ++i)
        out[i] = in[i];
      return;
    }
    const mono_float delta = (target - gain_) / static_cast<mono_float>(num_samples);
    mono_float gain = gain_;
    for (int i = 0; i < num_samples; ++i) {
      gain += delta;
      out[i] = in[i] * gain;
    }
    gain_ = target;
  }

  int SerumFilterBank::filterTypeOf(int model, int style) {
    return synthyboy::filters::serumFilterTypeOf(model, style);   // docs/ui-design/13-new-keys.md 4.11 & 4.21: one table, shared with the UI
  }

  SerumFilterBank::SerumFilterBank() : Processor(kNumInputs, 1), curve_(std::make_shared<FilterCurveShared>()) {
    scratch_ = std::shared_ptr<float[]>(new float[3 * kScratch]());
    for (auto& f : filters_)
      f = std::make_unique<synthyboy::filters::Filter>();
    prepareFilters();
  }

  SerumFilterBank::SerumFilterBank(const SerumFilterBank& other) : Processor(other), type_(-1), curve_(other.curve_), prepared_rate_(0), scratch_(other.scratch_) {
    for (auto& f : filters_)
      f = std::make_unique<synthyboy::filters::Filter>();
    prepareFilters();
  }

  SerumFilterBank::~SerumFilterBank() = default;

  void SerumFilterBank::prepareFilters() {
    const int rate = std::max(22050, std::min(192000, getSampleRate()));
    for (auto& f : filters_)
      f->prepare(static_cast<double>(rate));
    prepared_rate_ = getSampleRate();
    type_ = -1;
  }

  void SerumFilterBank::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
    prepareFilters();
  }

  void SerumFilterBank::setOversampleAmount(int oversample) {
    Processor::setOversampleAmount(oversample);
    prepareFilters();
  }

  void SerumFilterBank::reset(poly_mask reset_mask) {
    for (int lane = 0; lane < static_cast<int>(poly_float::kSize); ++lane)
      if (reset_mask[lane] != 0)
        filters_[lane]->reset();
  }

  // PZ SVF with a drawn magnitude curve: the cascade of bells fitted to the curve, shifted along the frequency axis by the cutoff
  // (a cutoff of 1 kHz leaves the curve where it was drawn).
  void SerumFilterBank::processCurve(int num_samples) {
    const poly_float* audio = input(kAudio)->source->buffer;
    const poly_float* note = input(kMidiCutoff)->source->buffer;
    poly_float* out = output()->buffer;
    const poly_mask reset_mask = getResetMask(kReset);
    const double rate = static_cast<double>(getSampleRate());
    for (int lane = 0; lane < static_cast<int>(poly_float::kSize); ++lane) {
      if (reset_mask[lane] != 0)
        for (auto& stage : bell_state_[lane])
          stage[0] = stage[1] = 0.0f;
      const float shift = std::exp2((note[0][lane] - 69.0f) * (1.0f / 12.0f)) * (440.0f / 1000.0f);
      double b[FilterCurveShared::kMaxBells][3], a[FilterCurveShared::kMaxBells][2];
      for (int k = 0; k < curve_->num_bells; ++k)
        bellCoefficients(curve_->frequency[k] * shift, curve_->gain_db[k], curve_->q[k], rate, b[k], a[k]);
      for (int i = 0; i < num_samples; ++i) {
        double v = audio[i][lane];
        for (int k = 0; k < curve_->num_bells; ++k) {   // transposed direct form II
          const double y = b[k][0] * v + bell_state_[lane][k][0];
          bell_state_[lane][k][0] = static_cast<float>(b[k][1] * v - a[k][0] * y + bell_state_[lane][k][1]);
          bell_state_[lane][k][1] = static_cast<float>(b[k][2] * v - a[k][1] * y);
          v = y;
        }
        out[i].set(lane, std::isfinite(v) ? static_cast<float>(v) : 0.0f);
      }
    }
  }

  void SerumFilterBank::process(int num_samples) {
    if (num_samples > kScratch)
      return;
    const int model = static_cast<int>(input(kModel)->at(0)[0] + 0.5f);
    const int style = static_cast<int>(input(kStyle)->at(0)[0] + 0.5f);
    const int type = filterTypeOf(model, style);
    if (type == static_cast<int>(FilterType::PZSVF) && curve_->num_bells > 0) {
      processCurve(num_samples);   // PZ SVF with a drawn curve
      return;
    }
    const poly_float* audio = input(kAudio)->source->buffer;
    const poly_float* note = input(kMidiCutoff)->source->buffer;
    poly_float* out = output()->buffer;

    const poly_float resonance = input(kResonance)->at(0);
    const poly_float drive = input(kDrive)->at(0);
    const poly_float blend = input(kBlend)->at(0);
    const poly_float var = input(kVar)->at(0);
    const poly_mask reset_mask = getResetMask(kReset);
    float* in_lane = scratch_.get();
    float* out_lane = scratch_.get() + kScratch;
    float* cutoff_lane = scratch_.get() + 2 * kScratch;
    const float limit = 0.49f * static_cast<float>(getSampleRate());

    for (int lane = 0; lane < static_cast<int>(poly_float::kSize); ++lane) {
      synthyboy::filters::Filter& filter = *filters_[lane];
      for (int i = 0; i < num_samples; ++i) {
        in_lane[i] = audio[i][lane];
        // MIDI note -> Hz (A4 = 69 = 440 Hz)
        cutoff_lane[i] = std::min(limit, 440.0f * std::exp2((note[i][lane] - 69.0f) * (1.0f / 12.0f)));
      }

      synthyboy::filters::FilterParams params;
      params.cutoff = cutoff_lane[0];
      params.resonance = std::max(0.0f, std::min(1.0f, resonance[lane]));
      params.drive = std::max(0.0f, std::min(1.0f, drive[lane] * 0.05f));   // 0 .. 20 dB
      params.blend = std::max(0.0f, std::min(1.0f, blend[lane] * 0.5f));    // 0 .. 2
      params.var = std::max(0.0f, std::min(1.0f, var[lane]));
      params.mix = 1.0f;                                                     // the dry / wet mix is the module's own (filter_N_mix)
      if (type != type_ || reset_mask[lane] != 0) {
        filter.setType(type);
        filter.setParams(params);
        filter.reset();
      }
      else
        filter.setParams(params);

      filter.process(in_lane, out_lane, num_samples, cutoff_lane, nullptr);
      for (int i = 0; i < num_samples; ++i)
        out[i].set(lane, out_lane[i]);
    }
    type_ = type;
  }
} // namespace sbvital
