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

#pragma once

#include "processor.h"
#include "linkwitz_riley_filter.h"

#include <memory>
#include <vector>

namespace sbvital {

  /** A look-ahead peak limiter (SynthyBoy, docs/ui-design/13-new-keys.md 4.16, G73): the signal is delayed by the look-ahead, the gain
      is the box-smoothed minimum of the gain each sample needs, so the gain is at its required value when the peak leaves the delay.
      Stereo linked (the two channels of a pair share the gain); the peak includes a mid point estimate between two samples. */
  class LookaheadLimiter {
    public:
      static constexpr mono_float kLookaheadMs = 1.5f;
      static constexpr int kMaxLevels = 11;          // windows up to 2^10 = 1024 samples
      static constexpr int kMaxLookahead = 1 << (kMaxLevels - 1);

      LookaheadLimiter();

      /** Look-ahead in samples for a sample rate (0 .. kMaxLookahead - 1); resets the state when it changes. */
      void setSampleRate(int sample_rate);
      int lookahead() const { return lookahead_; }
      void reset();

      /** Limits `num_samples` of audio_in to the amplitude `ceiling` (per sample window: a constant for the block). `release` is the
          fraction of the way back to unity per sample. `delayed` receives the input delayed by the look-ahead (the dry signal of a mix),
          `audio_out` the limited signal. */
      void process(const poly_float* audio_in, int num_samples, poly_float ceiling, poly_float release,
                   poly_float* audio_out, poly_float* delayed);

    private:
      int lookahead_;      // delay D in samples; the box smoothing has D + 1 taps
      int levels_;         // dyadic min levels: the min window is 2^levels_ >= D + 1
      int write_;
      std::vector<poly_float> input_ring_;     // kMaxLookahead + 1 rounded to a power of two
      std::vector<poly_float> need_ring_;      // the gain each sample needs, for the exact final clamp and the min levels
      std::vector<poly_float> level_ring_[kMaxLevels];
      std::vector<poly_float> smooth_ring_;    // the min values for the running sum
      poly_float sum_, previous_gain_;
      poly_float x1_, x2_, x3_;                // the last three samples (mid point estimate)
      int ring_mask_;
      int since_exact_sum_;
  };

  class Compressor : public Processor {
    public:
      enum {
        kAudio,
        kUpperThreshold,
        kLowerThreshold,
        kUpperRatio,
        kLowerRatio,
        kOutputGain,
        kAttack,
        kRelease,
        kMix,
        kNumInputs
      };

      enum {
        kAudioOut,
        kNumOutputs
      };

      Compressor(mono_float base_attack_ms_first, mono_float base_release_ms_first,
                 mono_float base_attack_ms_second, mono_float base_release_ms_second);
      virtual ~Compressor() { }

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }
      virtual void process(int num_samples) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      void processRms(const poly_float* audio_in, int num_samples);
      void processLimit(const poly_float* audio_in, int num_samples);
      void scaleOutput(const poly_float* audio_input, int num_samples);
      void reset(poly_mask reset_mask) override;
      void setSampleRate(int sample_rate) override;
      void setOversampleAmount(int oversample) override;

      /** SynthyBoy: limit mode. The upper stage becomes a look-ahead limiter at the upper threshold; the lower (upward) stage stays. */
      void setLimit(bool limit);
      bool limit() const { return limit_; }
      int limiterLatency() const { return limit_ ? limiter_.lookahead() : 0; }

      force_inline poly_float getInputMeanSquared() { return input_mean_squared_; }
      force_inline poly_float getOutputMeanSquared() { return output_mean_squared_; }

    protected:
      poly_float computeMeanSquared(const poly_float* audio_in, int num_samples, poly_float mean_squared);

      poly_float input_mean_squared_;
      poly_float output_mean_squared_;
      poly_float high_enveloped_mean_squared_;
      poly_float low_enveloped_mean_squared_;

      poly_float mix_;

      poly_float base_attack_ms_;
      poly_float base_release_ms_;

      poly_float output_mult_;

      bool limit_ = false;
      LookaheadLimiter limiter_;
      std::vector<poly_float> limited_;   // the limiter output and its delayed input, one block each
      std::vector<poly_float> delayed_;

      JUCE_LEAK_DETECTOR(Compressor)
  };

  class MultibandCompressor : public Processor {
    public:
      enum {
        kAudio,
        kLowUpperRatio,
        kBandUpperRatio,
        kHighUpperRatio,
        kLowLowerRatio,
        kBandLowerRatio,
        kHighLowerRatio,
        kLowUpperThreshold,
        kBandUpperThreshold,
        kHighUpperThreshold,
        kLowLowerThreshold,
        kBandLowerThreshold,
        kHighLowerThreshold,
        kLowOutputGain,
        kBandOutputGain,
        kHighOutputGain,
        kAttack,
        kRelease,
        kEnabledBands,
        kMix,
        kLimit,            // SynthyBoy: 1 = limit mode
        kLowCrossover,     // SynthyBoy: Hz (0 = unplugged: the default of the engine)
        kHighCrossover,
        kNumInputs
      };

      enum BandOptions {
        kMultiband,
        kLowBand,
        kHighBand,
        kSingleBand,
        kNumBandOptions
      };

      enum OutputType {
        kAudioOut,
        kLowInputMeanSquared,
        kBandInputMeanSquared,
        kHighInputMeanSquared,
        kLowOutputMeanSquared,
        kBandOutputMeanSquared,
        kHighOutputMeanSquared,
        kNumOutputs
      };

      MultibandCompressor();
      virtual ~MultibandCompressor() { }

      static constexpr mono_float kDefaultLowCrossover = 120.0f;
      static constexpr mono_float kDefaultHighCrossover = 2500.0f;

      /** The look-ahead of the limiter in samples of the chain, 0 while the limit mode is off. */
      int limiterLatency() const { return was_limit_ ? low_band_compressor_.limiterLatency() : 0; }

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }
      virtual void process(int num_samples) override;
      void setOversampleAmount(int oversample) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      void setSampleRate(int sample_rate) override;
      void reset(poly_mask reset_mask) override;

    protected:
      void packFilterOutput(LinkwitzRileyFilter* filter, int num_samples, poly_float* dest);
      void packLowBandCompressor(int num_samples, poly_float* dest);
      void writeAllCompressorOutputs(int num_samples, poly_float* dest);
      void writeCompressorOutputs(Compressor* compressor, int num_samples, poly_float* dest);

      bool was_low_enabled_;
      bool was_high_enabled_;
      bool was_limit_;

      cr::Output low_band_upper_ratio_;
      cr::Output band_high_upper_ratio_;
      cr::Output low_band_lower_ratio_;
      cr::Output band_high_lower_ratio_;
      cr::Output low_band_upper_threshold_;
      cr::Output band_high_upper_threshold_;
      cr::Output low_band_lower_threshold_;
      cr::Output band_high_lower_threshold_;

      cr::Output low_band_output_gain_;
      cr::Output band_high_output_gain_;

      LinkwitzRileyFilter low_band_filter_;
      LinkwitzRileyFilter band_high_filter_;

      Compressor low_band_compressor_;
      Compressor band_high_compressor_;

      JUCE_LEAK_DETECTOR(MultibandCompressor)
  };
} // namespace sbvital

