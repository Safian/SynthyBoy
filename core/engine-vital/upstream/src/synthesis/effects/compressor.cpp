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

#include "compressor.h"
#include "futils.h"

#include <algorithm>
#include <cmath>

namespace sbvital {

  namespace {
    constexpr int kLimiterRing = LookaheadLimiter::kMaxLookahead;   // a power of two
    constexpr mono_float kRmsTime = 0.025f;
    constexpr mono_float kMaxExpandMult = 32.0f;
    constexpr mono_float kLowAttackMs = 2.8f;
    constexpr mono_float kBandAttackMs = 1.4f;
    constexpr mono_float kHighAttackMs = 0.7f;
    constexpr mono_float kLowReleaseMs = 40.0f;
    constexpr mono_float kBandReleaseMs = 28.0f;
    constexpr mono_float kHighReleaseMs = 15.0f;

    constexpr mono_float kMinGain = -30.0f;
    constexpr mono_float kMaxGain = 30.0f;
    constexpr mono_float kMinThreshold = -100.0f;
    constexpr mono_float kPeakHoldMs = 1.0f;            // peak mode: decay of the peak detector
    constexpr mono_float kMinPeakThreshold = -120.0f;   // peak mode (Serum): the threshold knob of the plug-in reaches -106 dB
    constexpr mono_float kMaxThreshold = 12.0f;
    constexpr mono_float kMinSampleEnvelope = 5.0f;
  } // namespace

  LookaheadLimiter::LookaheadLimiter() : lookahead_(0), levels_(0), write_(0), sum_(1.0f), previous_gain_(1.0f), x1_(0.0f), x2_(0.0f),
                                          x3_(0.0f), ring_mask_(kLimiterRing - 1), since_exact_sum_(0) {
    input_ring_.assign(kLimiterRing, poly_float(0.0f));
    dry_ring_.assign(kLimiterRing, poly_float(0.0f));
    need_ring_.assign(kLimiterRing, poly_float(1.0f));
    smooth_ring_.assign(kLimiterRing, poly_float(1.0f));
    for (int j = 0; j < kMaxLevels; ++j)
      level_ring_[j].assign(static_cast<size_t>(1) << j, poly_float(1.0f));
  }

  void LookaheadLimiter::setSampleRate(int sample_rate) {
    int lookahead = static_cast<int>(std::lround(kLookaheadMs * 0.001f * sample_rate));
    lookahead = std::max(1, std::min(lookahead, kMaxLookahead - 2));
    if (lookahead == lookahead_)
      return;
    lookahead_ = lookahead;
    levels_ = 0;
    while ((1 << levels_) < lookahead_ + 1)
      ++levels_;
    reset();
  }

  void LookaheadLimiter::reset() {
    std::fill(input_ring_.begin(), input_ring_.end(), poly_float(0.0f));
    std::fill(dry_ring_.begin(), dry_ring_.end(), poly_float(0.0f));
    std::fill(need_ring_.begin(), need_ring_.end(), poly_float(1.0f));
    std::fill(smooth_ring_.begin(), smooth_ring_.end(), poly_float(1.0f));
    for (int j = 0; j < kMaxLevels; ++j)
      std::fill(level_ring_[j].begin(), level_ring_[j].end(), poly_float(1.0f));
    write_ = 0;
    sum_ = poly_float(static_cast<mono_float>(lookahead_ + 1));
    previous_gain_ = 1.0f;
    x1_ = x2_ = x3_ = 0.0f;
    since_exact_sum_ = 0;
  }

  void LookaheadLimiter::process(const poly_float* audio_in, int num_samples, poly_float ceiling, poly_float release,
                                 poly_float* audio_out, poly_float* delayed, const poly_float* pre_gain) {
    const int taps = lookahead_ + 1;
    const mono_float tap_scale = 1.0f / taps;
    for (int i = 0; i < num_samples; ++i) {
      const poly_float dry = audio_in[i];
      const poly_float x = pre_gain != nullptr ? dry * pre_gain[i] : dry;

      // the gain this sample needs: its own peak and the estimated peak between the two samples before it
      const poly_float mid = ((x2_ + x1_) * 9.0f - (x3_ + x)) * (1.0f / 16.0f);
      poly_float peak = utils::max(poly_float::abs(x), poly_float::abs(mid));
      peak = utils::max(peak, utils::swapStereo(peak));
      const poly_float need = utils::min(poly_float(1.0f), ceiling / utils::max(peak, 1.0e-9f));
      x3_ = x2_;
      x2_ = x1_;
      x1_ = x;

      // the minimum of the last 2^levels needs, with dyadic levels
      poly_float window_min = need;
      for (int j = 0; j < levels_; ++j) {
        poly_float& slot = level_ring_[j][static_cast<size_t>(write_) & ((static_cast<size_t>(1) << j) - 1)];
        const poly_float old = slot;
        slot = window_min;
        window_min = utils::min(window_min, old);
      }

      // box smoothing of the minimum over `taps` samples (running sum, recomputed exactly once per `taps` samples)
      const poly_float oldest = smooth_ring_[static_cast<size_t>(write_ - taps) & ring_mask_];
      sum_ = sum_ + window_min - oldest;
      smooth_ring_[static_cast<size_t>(write_) & ring_mask_] = window_min;
      input_ring_[static_cast<size_t>(write_) & ring_mask_] = x;
      dry_ring_[static_cast<size_t>(write_) & ring_mask_] = dry;
      need_ring_[static_cast<size_t>(write_) & ring_mask_] = need;
      if (++since_exact_sum_ >= taps) {
        since_exact_sum_ = 0;
        poly_float exact = 0.0f;
        for (int k = 0; k < taps; ++k)
          exact += smooth_ring_[static_cast<size_t>(write_ - k) & ring_mask_];
        sum_ = exact;
      }

      const size_t out_index = static_cast<size_t>(write_ - lookahead_) & ring_mask_;
      const poly_float delayed_sample = input_ring_[out_index];
      poly_float gain = utils::min(sum_ * tap_scale, need_ring_[out_index]);
      const poly_float climb = previous_gain_ + (poly_float(1.0f) - previous_gain_) * release;
      gain = utils::min(utils::min(gain, climb), 1.0f);
      previous_gain_ = gain;
      audio_out[i] = delayed_sample * gain;
      delayed[i] = pre_gain != nullptr ? dry_ring_[out_index] : delayed_sample;
      ++write_;
    }
  }

  Compressor::Compressor(mono_float base_attack_ms_first, mono_float base_release_ms_first,
                         mono_float base_attack_ms_second, mono_float base_release_ms_second) :
      Processor(kNumInputs, kNumOutputs), input_mean_squared_(0.0f) {
    limited_.assign(kMaxBufferSize * kMaxOversample, poly_float(0.0f));
    pre_gain_.assign(kMaxBufferSize * kMaxOversample, poly_float(1.0f));
    delayed_.assign(kMaxBufferSize * kMaxOversample, poly_float(0.0f));
    base_attack_ms_ = utils::maskLoad(poly_float(base_attack_ms_second), base_attack_ms_first, constants::kFirstMask);
    poly_float second_release = base_release_ms_second;
    base_release_ms_ = utils::maskLoad(second_release, base_release_ms_first, constants::kFirstMask);
    output_mult_ = 0.0f;
    mix_ = 0.0f;
  }

  void Compressor::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void Compressor::processWithInput(const poly_float* audio_in, int num_samples) {
    if (limit_)
      processLimit(audio_in, num_samples);
    else if (peak_)
      processPeak(audio_in, num_samples);
    else
      processRms(audio_in, num_samples);

    input_mean_squared_ = computeMeanSquared(audio_in, num_samples, input_mean_squared_);
    output_mean_squared_ = computeMeanSquared(output(kAudioOut)->buffer, num_samples, output_mean_squared_);
    scaleOutput(limit_ ? delayed_.data() : audio_in, num_samples);
  }

  void Compressor::setLimit(bool limit) {
    if (limit == limit_)
      return;
    limit_ = limit;
    limiter_.setSampleRate(getSampleRate());
    limiter_.reset();
    high_enveloped_mean_squared_ = 0.0f;
    low_enveloped_mean_squared_ = 0.0f;
  }

  void Compressor::setPeak(bool peak) {
    if (peak == peak_)
      return;
    peak_ = peak;
    peak_reduction_db_ = 0.0f;
    peak_held_ = 0.0f;
  }

  void Compressor::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
    limiter_.setSampleRate(getSampleRate());
  }

  void Compressor::setOversampleAmount(int oversample) {
    Processor::setOversampleAmount(oversample);
    limiter_.setSampleRate(getSampleRate());
  }

  // Limit mode (G73): the upper stage is a look-ahead limiter at the upper threshold, the lower (upward) stage is the one of processRms.
  void Compressor::processLimit(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;

    mono_float samples_per_ms = (1.0f * getSampleRate()) / kMsPerSec;
    poly_float attack_mult = base_attack_ms_ * samples_per_ms;
    poly_float release_mult = base_release_ms_ * samples_per_ms;
    poly_float attack_exponent = utils::clamp(input(kAttack)->at(0), 0.0f, 1.0f) * 8.0f - 4.0f;
    poly_float release_exponent = utils::clamp(input(kRelease)->at(0), 0.0f, 1.0f) * 8.0f - 4.0f;
    poly_float envelope_attack_samples = utils::max(futils::exp(attack_exponent) * attack_mult, kMinSampleEnvelope);
    poly_float envelope_release_samples = utils::max(futils::exp(release_exponent) * release_mult, kMinSampleEnvelope);
    poly_float attack_scale = poly_float(1.0f) / (envelope_attack_samples + 1.0f);
    poly_float release_scale = poly_float(1.0f) / (envelope_release_samples + 1.0f);

    poly_float ceiling = futils::dbToMagnitude(utils::clamp(input(kUpperThreshold)->at(0), kMinThreshold, kMaxThreshold));
    poly_float lower_threshold = utils::clamp(input(kLowerThreshold)->at(0), kMinThreshold, kMaxThreshold);
    lower_threshold = futils::dbToMagnitude(lower_threshold);
    lower_threshold *= lower_threshold;
    poly_float lower_ratio = utils::clamp(input(kLowerRatio)->at(0), -1.0f, 1.0f) * 0.5f;

    // GROUPA1 (A347): in limit mode the makeup gain drives the signal INTO the limiter (Serum 2 manual p.163: the limiter has a makeup range of
    // 0-36 dB), so the output never exceeds the ceiling; scaleOutput () only follows the ramp and does not multiply again
    {
      const poly_float from = output_mult_;
      const poly_float target = futils::dbToMagnitude(utils::clamp(input(kOutputGain)->at(0), kMinGain, kMaxGain));
      const poly_float step = (target - from) * (1.0f / num_samples);
      poly_float g = from;
      for (int i = 0; i < num_samples; ++i) {
        g += step;
        pre_gain_[i] = g;
      }
    }
    limiter_.process(audio_in, num_samples, ceiling, release_scale, limited_.data(), delayed_.data(), pre_gain_.data());

    poly_float low_enveloped_mean_squared = low_enveloped_mean_squared_;
    for (int i = 0; i < num_samples; ++i) {
      poly_float sample_squared = audio_in[i] * audio_in[i];
      poly_mask low_attack_mask = poly_float::greaterThan(sample_squared, low_enveloped_mean_squared);
      poly_float low_samples = utils::maskLoad(envelope_release_samples, envelope_attack_samples, low_attack_mask);
      poly_float low_scale = utils::maskLoad(release_scale, attack_scale, low_attack_mask);
      low_enveloped_mean_squared = (sample_squared + low_enveloped_mean_squared * low_samples) * low_scale;
      low_enveloped_mean_squared = utils::min(low_enveloped_mean_squared, lower_threshold);
      poly_float lower_mult = futils::pow(lower_threshold / low_enveloped_mean_squared, lower_ratio);
      audio_out[i] = limited_[i] * utils::clamp(lower_mult, 0.0f, kMaxExpandMult);
      VITAL_ASSERT(utils::isContained(audio_out[i]));
    }
    low_enveloped_mean_squared_ = low_enveloped_mean_squared;
  }

  void Compressor::processPeak(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;

    const mono_float sample_rate = static_cast<mono_float>(getSampleRate());
    const mono_float attack_x = utils::clamp(input(kAttack)->at(0)[0], 0.0f, 1.0f);
    const mono_float release_x = utils::clamp(input(kRelease)->at(0)[0], 0.0f, 1.0f);
    const mono_float attack_ms = 0.1f * powf(10000.0f, attack_x);
    const mono_float release_ms = 0.1f * powf(10000.0f, release_x);
    // the time constants of the gain reduction fitted to the real plug-in (tools/ref-compare/compfit2.py, 0.2 - 0.5 dB rms over the gain reduction curves): attack 0.37 att^2 / (att + 4) ms, release = the
    // release time; the detector holds a peak with the decay of 1 ms
    const mono_float attack_time_ms = 0.37f * attack_ms * attack_ms / (attack_ms + 4.0f);
    const mono_float attack_samples = std::max(attack_time_ms * 0.001f * sample_rate, 1.0f);
    const mono_float release_samples = std::max(release_ms * 0.001f * sample_rate, 1.0f);
    const poly_float attack_coefficient = 1.0f - expf(-1.0f / attack_samples);
    const poly_float release_coefficient = 1.0f - expf(-1.0f / release_samples);

    const poly_float threshold = futils::dbToMagnitude(utils::clamp(input(kUpperThreshold)->at(0), kMinPeakThreshold, kMaxThreshold));
    const poly_float inverse_threshold = poly_float(1.0f) / threshold;
    const poly_float ratio = utils::clamp(input(kUpperRatio)->at(0), 0.0f, 1.0f);
    const poly_float hold_decay = expf(-1.0f / std::max(kPeakHoldMs * 0.001f * sample_rate, 1.0f));
    constexpr mono_float kDbPerOctave = 6.0205999f;
    constexpr mono_float kOctavesPerDb = 1.0f / 6.0205999f;

    poly_float reduction = peak_reduction_db_;
    poly_float held = peak_held_;
    for (int i = 0; i < num_samples; ++i) {
      const poly_float sample = audio_in[i];
      poly_float level = poly_float::abs(sample);
      level = utils::max(level, utils::swapStereo(level));
      held = utils::max(level, held * hold_decay);   // the detector keeps a peak for a moment: the gain follows the peaks of a cycle, not its rectified waveform
      level = held;
      const poly_float over_db = utils::max(futils::log2(utils::max(level * inverse_threshold, 1.0e-9f)) * kDbPerOctave, 0.0f);
      const poly_float needed = over_db * ratio;
      const poly_mask rising = poly_float::greaterThan(needed, reduction);
      reduction += (needed - reduction) * utils::maskLoad(release_coefficient, attack_coefficient, rising);
      audio_out[i] = sample * futils::exp2(reduction * (-kOctavesPerDb));
      VITAL_ASSERT(utils::isContained(audio_out[i]));
    }
    peak_reduction_db_ = reduction;
    peak_held_ = held;
  }

  void Compressor::processRms(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;

    mono_float samples_per_ms = (1.0f * getSampleRate()) / kMsPerSec;
    poly_float attack_mult = base_attack_ms_ * samples_per_ms;
    poly_float release_mult = base_release_ms_ * samples_per_ms;
    poly_float attack_exponent = utils::clamp(input(kAttack)->at(0), 0.0f, 1.0f) * 8.0f - 4.0f;
    poly_float release_exponent = utils::clamp(input(kRelease)->at(0), 0.0f, 1.0f) * 8.0f - 4.0f;
    poly_float envelope_attack_samples = futils::exp(attack_exponent) * attack_mult;
    poly_float envelope_release_samples = futils::exp(release_exponent) * release_mult;
    envelope_attack_samples = utils::max(envelope_attack_samples, kMinSampleEnvelope);
    envelope_release_samples = utils::max(envelope_release_samples, kMinSampleEnvelope);

    poly_float attack_scale = poly_float(1.0f) / (envelope_attack_samples + 1.0f);
    poly_float release_scale = poly_float(1.0f) / (envelope_release_samples + 1.0f);

    poly_float upper_threshold = utils::clamp(input(kUpperThreshold)->at(0), kMinThreshold, kMaxThreshold);
    upper_threshold = futils::dbToMagnitude(upper_threshold);
    upper_threshold *= upper_threshold;
    poly_float lower_threshold = utils::clamp(input(kLowerThreshold)->at(0), kMinThreshold, kMaxThreshold);
    lower_threshold = futils::dbToMagnitude(lower_threshold);
    lower_threshold *= lower_threshold;

    poly_float upper_ratio = utils::clamp(input(kUpperRatio)->at(0), 0.0f, 1.0f) * 0.5f;
    poly_float lower_ratio = utils::clamp(input(kLowerRatio)->at(0), -1.0f, 1.0f) * 0.5f;

    poly_float low_enveloped_mean_squared = low_enveloped_mean_squared_;
    poly_float high_enveloped_mean_squared = high_enveloped_mean_squared_;

    for (int i = 0; i < num_samples; ++i) {
      poly_float sample = audio_in[i];
      poly_float sample_squared = sample * sample;

      poly_mask high_attack_mask = poly_float::greaterThan(sample_squared, high_enveloped_mean_squared);
      poly_float high_samples = utils::maskLoad(envelope_release_samples, envelope_attack_samples, high_attack_mask);
      poly_float high_scale = utils::maskLoad(release_scale, attack_scale, high_attack_mask);

      high_enveloped_mean_squared = (sample_squared + high_enveloped_mean_squared * high_samples) * high_scale;
      high_enveloped_mean_squared = utils::max(high_enveloped_mean_squared, upper_threshold);

      poly_float upper_mag_delta = upper_threshold / high_enveloped_mean_squared;
      poly_float upper_mult = futils::pow(upper_mag_delta, upper_ratio);

      poly_mask low_attack_mask = poly_float::greaterThan(sample_squared, low_enveloped_mean_squared);
      poly_float low_samples = utils::maskLoad(envelope_release_samples, envelope_attack_samples, low_attack_mask);
      poly_float low_scale = utils::maskLoad(release_scale, attack_scale, low_attack_mask);

      low_enveloped_mean_squared = (sample_squared + low_enveloped_mean_squared * low_samples) * low_scale;
      low_enveloped_mean_squared = utils::min(low_enveloped_mean_squared, lower_threshold);
      
      poly_float lower_mag_delta = lower_threshold / low_enveloped_mean_squared;
      poly_float lower_mult = futils::pow(lower_mag_delta, lower_ratio);

      poly_float gain_compression = utils::clamp(upper_mult * lower_mult, 0.0f, kMaxExpandMult);
      audio_out[i] = gain_compression * sample;
      VITAL_ASSERT(utils::isContained(audio_out[i]));
    }

    low_enveloped_mean_squared_ = low_enveloped_mean_squared;
    high_enveloped_mean_squared_ = high_enveloped_mean_squared;
  }

  void Compressor::scaleOutput(const poly_float* audio_input, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;

    poly_float current_output_mult = output_mult_;
    poly_float gain = utils::clamp(input(kOutputGain)->at(0), kMinGain, kMaxGain);
    output_mult_ = futils::dbToMagnitude(gain);
    poly_float delta_output_mult = (output_mult_ - current_output_mult) * (1.0f / num_samples);

    poly_float current_mix = mix_;
    mix_ = utils::clamp(input(kMix)->at(0), 0.0f, 1.0f);
    poly_float delta_mix = (mix_ - current_mix) * (1.0f / num_samples);

    for (int i = 0; i < num_samples; ++i) {
      current_output_mult += delta_output_mult;
      current_mix += delta_mix;
      // limit mode: the gain was applied before the limiter (processLimit)
      audio_out[i] = utils::interpolate(audio_input[i], limit_ ? audio_out[i] : audio_out[i] * current_output_mult, current_mix);
      VITAL_ASSERT(utils::isContained(audio_out[i]));
    }
  }

  void Compressor::reset(poly_mask reset_mask) {
    input_mean_squared_ = 0.0f;
    output_mean_squared_ = 0.0f;
    output_mult_ = 0.0f;
    mix_ = 0.0f;

    high_enveloped_mean_squared_ = 0.0f;
    low_enveloped_mean_squared_ = 0.0f;
    peak_reduction_db_ = 0.0f;
    peak_held_ = 0.0f;
    limiter_.reset();
  }

  poly_float Compressor::computeMeanSquared(const poly_float* audio_in, int num_samples, poly_float mean_squared) {
    int rms_samples = kRmsTime * getSampleRate();
    float rms_adjusted = rms_samples - 1.0f;
    mono_float input_scale = 1.0f / rms_samples;

    for (int i = 0; i < num_samples; ++i) {
      poly_float sample = audio_in[i];
      poly_float sample_squared = sample * sample;
      mean_squared = (mean_squared * rms_adjusted + sample_squared) * input_scale;
    }
    return mean_squared;
  }

  MultibandCompressor::MultibandCompressor() :
      Processor(kNumInputs, kNumOutputs), low_band_filter_(kDefaultLowCrossover), band_high_filter_(kDefaultHighCrossover),
      low_band_compressor_(kLowAttackMs, kLowReleaseMs, kBandAttackMs, kBandReleaseMs),
      band_high_compressor_(kBandAttackMs, kBandReleaseMs, kHighAttackMs, kHighReleaseMs) {
    was_low_enabled_ = false;
    was_high_enabled_ = false;
    was_limit_ = false;

    low_band_compressor_.plug(&low_band_upper_threshold_, Compressor::kUpperThreshold);
    low_band_compressor_.plug(&low_band_lower_threshold_, Compressor::kLowerThreshold);
    low_band_compressor_.plug(&low_band_upper_ratio_, Compressor::kUpperRatio);
    low_band_compressor_.plug(&low_band_lower_ratio_, Compressor::kLowerRatio);
    low_band_compressor_.plug(&low_band_output_gain_, Compressor::kOutputGain);
    low_band_compressor_.useInput(input(kAttack), Compressor::kAttack);
    low_band_compressor_.useInput(input(kRelease), Compressor::kRelease);
    low_band_compressor_.useInput(input(kMix), Compressor::kMix);

    band_high_compressor_.plug(&band_high_upper_threshold_, Compressor::kUpperThreshold);
    band_high_compressor_.plug(&band_high_lower_threshold_, Compressor::kLowerThreshold);
    band_high_compressor_.plug(&band_high_upper_ratio_, Compressor::kUpperRatio);
    band_high_compressor_.plug(&band_high_lower_ratio_, Compressor::kLowerRatio);
    band_high_compressor_.plug(&band_high_output_gain_, Compressor::kOutputGain);
    band_high_compressor_.useInput(input(kAttack), Compressor::kAttack);
    band_high_compressor_.useInput(input(kRelease), Compressor::kRelease);
    band_high_compressor_.useInput(input(kMix), Compressor::kMix);
  }

  void MultibandCompressor::setOversampleAmount(int oversample) {
    Processor::setOversampleAmount(oversample);
    low_band_filter_.setOversampleAmount(oversample);
    band_high_filter_.setOversampleAmount(oversample);
    low_band_compressor_.setOversampleAmount(oversample);
    band_high_compressor_.setOversampleAmount(oversample);
  }

  void MultibandCompressor::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
    low_band_filter_.setSampleRate(sample_rate);
    band_high_filter_.setSampleRate(sample_rate);
    low_band_compressor_.setSampleRate(sample_rate);
    band_high_compressor_.setSampleRate(sample_rate);
  }

  void MultibandCompressor::reset(poly_mask reset_mask) {
    low_band_filter_.reset(reset_mask);
    band_high_filter_.reset(reset_mask);
    low_band_compressor_.reset(reset_mask);
    band_high_compressor_.reset(reset_mask);

    output(kLowInputMeanSquared)->buffer[0] = 0.0f;
    output(kLowOutputMeanSquared)->buffer[0] = 0.0f;
    output(kBandInputMeanSquared)->buffer[0] = 0.0f;
    output(kBandOutputMeanSquared)->buffer[0] = 0.0f;
    output(kHighInputMeanSquared)->buffer[0] = 0.0f;
    output(kHighOutputMeanSquared)->buffer[0] = 0.0f;
  }

  void MultibandCompressor::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void MultibandCompressor::packFilterOutput(LinkwitzRileyFilter* filter, int num_samples, poly_float* dest) {
    const poly_float* low_output = filter->output(LinkwitzRileyFilter::kAudioLow)->buffer;
    const poly_float* high_output = filter->output(LinkwitzRileyFilter::kAudioHigh)->buffer;

    for (int i = 0; i < num_samples; ++i) {
      poly_float low_sample = low_output[i];
      poly_float high_sample = utils::swapVoices(high_output[i]);
      dest[i] = utils::maskLoad(high_sample, low_sample, constants::kFirstMask);
    }
  }

  void MultibandCompressor::packLowBandCompressor(int num_samples, poly_float* dest) {
    const poly_float* low_output = band_high_filter_.output(LinkwitzRileyFilter::kAudioLow)->buffer;
    const poly_float* high_output = band_high_filter_.output(LinkwitzRileyFilter::kAudioHigh)->buffer;

    for (int i = 0; i < num_samples; ++i) {
      poly_float low_band_sample = low_output[i];
      poly_float low_high_sample = high_output[i] & constants::kFirstMask;
      dest[i] = low_band_sample + low_high_sample;
    }
  }

  void MultibandCompressor::writeAllCompressorOutputs(int num_samples, poly_float* dest) {
    const poly_float* low_band_output = low_band_compressor_.output(Compressor::kAudioOut)->buffer;
    const poly_float* high_output = band_high_compressor_.output(Compressor::kAudioOut)->buffer;

    for (int i = 0; i < num_samples; ++i) {
      poly_float low_band_sample = low_band_output[i];
      low_band_sample += utils::swapVoices(low_band_sample);
      poly_float high_sample = utils::swapVoices(high_output[i]);
      dest[i] = low_band_sample + high_sample;
    }
  }

  void MultibandCompressor::writeCompressorOutputs(Compressor* compressor, int num_samples, poly_float* dest) {
    const poly_float* compressor_output = compressor->output(Compressor::kAudioOut)->buffer;

    for (int i = 0; i < num_samples; ++i) {
      poly_float sample = compressor_output[i];
      dest[i] = sample + utils::swapVoices(sample);
    }
  }

  void MultibandCompressor::processWithInput(const poly_float* audio_in, int num_samples) {
    int enabled_bands = input(kEnabledBands)->at(0)[0];
    bool low_enabled = enabled_bands == kMultiband || enabled_bands == kLowBand;
    bool high_enabled = enabled_bands == kMultiband || enabled_bands == kHighBand;

    low_band_upper_threshold_.buffer[0] = utils::maskLoad(input(kBandUpperThreshold)->at(0),
                                                          input(kLowUpperThreshold)->at(0), constants::kFirstMask);
    band_high_upper_threshold_.buffer[0] = utils::maskLoad(input(kHighUpperThreshold)->at(0),
                                                           input(kBandUpperThreshold)->at(0), constants::kFirstMask);
    low_band_lower_threshold_.buffer[0] = utils::maskLoad(input(kBandLowerThreshold)->at(0),
                                                          input(kLowLowerThreshold)->at(0), constants::kFirstMask);
    band_high_lower_threshold_.buffer[0] = utils::maskLoad(input(kHighLowerThreshold)->at(0),
                                                           input(kBandLowerThreshold)->at(0), constants::kFirstMask);
    low_band_upper_ratio_.buffer[0] = utils::maskLoad(input(kBandUpperRatio)->at(0),
                                                      input(kLowUpperRatio)->at(0), constants::kFirstMask);
    band_high_upper_ratio_.buffer[0] = utils::maskLoad(input(kHighUpperRatio)->at(0),
                                                       input(kBandUpperRatio)->at(0), constants::kFirstMask);
    low_band_lower_ratio_.buffer[0] = utils::maskLoad(input(kBandLowerRatio)->at(0),
                                                      input(kLowLowerRatio)->at(0), constants::kFirstMask);
    band_high_lower_ratio_.buffer[0] = utils::maskLoad(input(kHighLowerRatio)->at(0),
                                                       input(kBandLowerRatio)->at(0), constants::kFirstMask);

    low_band_output_gain_.buffer[0] = utils::maskLoad(input(kBandOutputGain)->at(0),
                                                      input(kLowOutputGain)->at(0), constants::kFirstMask);
    band_high_output_gain_.buffer[0] = utils::maskLoad(input(kHighOutputGain)->at(0),
                                                       input(kBandOutputGain)->at(0), constants::kFirstMask);

    // SynthyBoy (13-new-keys.md 4.17): adjustable crossovers (0 = control not plugged: the fixed ones of the original engine) and limit mode
    const mono_float low_crossover_in = input(kLowCrossover)->at(0)[0];
    const mono_float high_crossover_in = input(kHighCrossover)->at(0)[0];
    mono_float low_crossover = low_crossover_in > 0.0f ? utils::clamp(low_crossover_in, 20.0f, 1000.0f) : kDefaultLowCrossover;
    mono_float high_crossover = high_crossover_in > 0.0f ? utils::clamp(high_crossover_in, 200.0f, 16000.0f) : kDefaultHighCrossover;
    high_crossover = std::max(high_crossover, low_crossover * 1.25f);
    high_crossover = std::min(high_crossover, 0.45f * getSampleRate());
    low_band_filter_.setCutoff(low_crossover);
    band_high_filter_.setCutoff(high_crossover);

    // compressor.limit: 0 compress (the RMS compressor of Vital), 1 limit (the look-ahead peak limiter), 2 the peak compressor of Serum 2.1.5 (13-new-keys.md 4.48)
    const int mode = static_cast<int>(input(kLimit)->at(0)[0] + 0.5f);
    const bool limit = mode == 1;
    const bool peak = mode >= 2;
    if (limit != was_limit_ || peak != was_peak_) {
      was_limit_ = limit;
      was_peak_ = peak;
      low_band_filter_.reset(constants::kFullMask);
      band_high_filter_.reset(constants::kFullMask);
    }
    low_band_compressor_.setLimit(limit);
    band_high_compressor_.setLimit(limit);
    low_band_compressor_.setPeak(peak);
    band_high_compressor_.setPeak(peak);

    if (low_enabled != was_low_enabled_ || high_enabled != was_high_enabled_) {
      low_band_filter_.reset(constants::kFullMask);
      band_high_filter_.reset(constants::kFullMask);
      low_band_compressor_.reset(constants::kFullMask);
      band_high_compressor_.reset(constants::kFullMask);
      was_low_enabled_ = low_enabled;
      was_high_enabled_ = high_enabled;
    }

    poly_float* audio_out = output(kAudioOut)->buffer;

    if (low_enabled && high_enabled) {
      low_band_filter_.processWithInput(audio_in, num_samples);
      packFilterOutput(&low_band_filter_, num_samples, audio_out);
      band_high_filter_.processWithInput(audio_out, num_samples);
      packLowBandCompressor(num_samples, audio_out);

      low_band_compressor_.processWithInput(audio_out, num_samples);
      const poly_float* band_high_buffer = band_high_filter_.output(LinkwitzRileyFilter::kAudioHigh)->buffer;
      band_high_compressor_.processWithInput(band_high_buffer, num_samples);
      writeAllCompressorOutputs(num_samples, audio_out);
    }
    else if (low_enabled) {
      low_band_filter_.processWithInput(audio_in, num_samples);
      packFilterOutput(&low_band_filter_, num_samples, audio_out);
      low_band_compressor_.processWithInput(audio_out, num_samples);
      writeCompressorOutputs(&low_band_compressor_, num_samples, audio_out);
    }
    else if (high_enabled) {
      band_high_filter_.processWithInput(audio_in, num_samples);
      packFilterOutput(&band_high_filter_, num_samples, audio_out);
      band_high_compressor_.processWithInput(audio_out, num_samples);
      writeCompressorOutputs(&band_high_compressor_, num_samples, audio_out);
    }
    else {
      band_high_compressor_.processWithInput(audio_in, num_samples);
      utils::copyBuffer(audio_out, band_high_compressor_.output(Compressor::kAudioOut)->buffer, num_samples);
    }

    poly_float low_band_input_ms = low_band_compressor_.getInputMeanSquared();
    poly_float band_high_input_ms = band_high_compressor_.getInputMeanSquared();
    poly_float low_band_output_ms = low_band_compressor_.getOutputMeanSquared();
    poly_float band_high_output_ms = band_high_compressor_.getOutputMeanSquared();

    output(kLowInputMeanSquared)->buffer[0] = low_band_input_ms;
    output(kLowOutputMeanSquared)->buffer[0] = low_band_output_ms;

    if (low_enabled) {
      output(kBandInputMeanSquared)->buffer[0] = utils::swapVoices(low_band_input_ms);
      output(kBandOutputMeanSquared)->buffer[0] = utils::swapVoices(low_band_output_ms);
    }
    else {
      output(kBandInputMeanSquared)->buffer[0] = band_high_input_ms;
      output(kBandOutputMeanSquared)->buffer[0] = band_high_output_ms;
    }

    output(kHighInputMeanSquared)->buffer[0] = utils::swapVoices(band_high_input_ms);
    output(kHighOutputMeanSquared)->buffer[0] = utils::swapVoices(band_high_output_ms);
  }

} // namespace sbvital
