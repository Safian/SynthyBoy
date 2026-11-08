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

#include "vital_compat.h"

#include "common.h"
#include "futils.h"
#include "utils.h"
#include "wave_frame.h"

namespace sbvital {

  /** SynthyBoy (docs/ui-design/13-new-keys.md 4.14): the frame position remap of one oscillator slot. ONE object per slot, owned by the Wavetable
      (so it is shared by all clones of the oscillator). Written by the adapter on the audio thread between two blocks, read by the oscillator
      on the same thread. `table` is the baked drawn curve over the normalised position 0..1 (kSize + 1 entries); max_frame is the frame number
      that position 1 stands for (the frames of the table - 1). */
  struct FrameRemap {
    static constexpr int kSize = 256;

    bool on = false;
    float max_frame = 63.0f;
    float rise_ms = 0.0f, fall_ms = 0.0f;
    float table[kSize + 1];

    FrameRemap() { setIdentity(); }
    void setIdentity() {
      for (int i = 0; i <= kSize; ++i)
        table[i] = static_cast<float>(i) / static_cast<float>(kSize);
    }
    /** The remapped normalised position (0..1) of a normalised position; linear between the baked entries. */
    force_inline float map(float u) const {
      u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
      float x = u * kSize;
      int i = static_cast<int>(x);
      if (i >= kSize)
        return table[kSize];
      float f = x - static_cast<float>(i);
      return table[i] + (table[i + 1] - table[i]) * f;
    }
  };

  class Wavetable {
    public:
      static constexpr int kFrequencyBins = WaveFrame::kWaveformBits;
      static constexpr int kWaveformSize = WaveFrame::kWaveformSize;
      static constexpr int kExtraValues = 3;
      static constexpr int kNumHarmonics = kWaveformSize / 2 + 1;
      static constexpr int kPolyFrequencySize = 2 * kNumHarmonics / poly_float::kSize + 2;

      struct WavetableData {
        WavetableData(int frames, int table_version) :
            num_frames(frames), frequency_ratio(1.0f), sample_rate(kDefaultSampleRate), version(table_version) { }

        int num_frames;
        mono_float frequency_ratio;
        mono_float sample_rate;
        int version;
        std::unique_ptr<mono_float[][kWaveformSize]> wave_data;
        std::unique_ptr<poly_float[][kPolyFrequencySize]> frequency_amplitudes;
        std::unique_ptr<poly_float[][kPolyFrequencySize]> normalized_frequencies;
        std::unique_ptr<poly_float[][kPolyFrequencySize]> phases;
      };

      static constexpr const mono_float* null_waveform() { return kZeroWaveform; }

      Wavetable(int max_frames);

      void loadDefaultWavetable();
      void setNumFrames(int num_frames);
      void setFrequencyRatio(float frequency_ratio);
      void setSampleRate(float rate);
      std::string getName() { return name_; }
      std::string getAuthor() { return author_; }
      void setName(const std::string& name) { name_ = name; }
      void setAuthor(const std::string& author) { author_ = author; }

      static force_inline mono_float getFrequencyFloatBin(mono_float phase_increment) {
        return futils::log2(1.0f / phase_increment);
      }

      static force_inline int getFrequencyBin(mono_float phase_increment) {
        int num_waves = 1.0f / phase_increment;
        return utils::iclamp(utils::ilog2(num_waves), 0, kFrequencyBins - 1);
      }

      force_inline int clampFrame(int frame) {
        return std::min(frame, current_data_->num_frames - 1);
      }

      force_inline const WavetableData* getAllData() {
        return current_data_;
      }

      force_inline mono_float* getBuffer(int frame_index) {
        return current_data_->wave_data[clampFrame(frame_index)];
      }

      force_inline poly_float* getFrequencyAmplitudes(int frame_index) {
        return current_data_->frequency_amplitudes[clampFrame(frame_index)];
      }

      force_inline poly_float* getNormalizedFrequencies(int frame_index) {
        return current_data_->normalized_frequencies[clampFrame(frame_index)];
      }

      force_inline int getVersion() {
        return current_data_->version;
      }

      force_inline int clampActiveFrame(int frame) {
        return std::min(frame, active_audio_data_.load()->num_frames - 1);
      }

      force_inline float getActiveFrequencyRatio() const {
        return active_audio_data_.load()->frequency_ratio;
      };

      force_inline float getActiveSampleRate() const {
        return active_audio_data_.load()->sample_rate;
      };

      force_inline const WavetableData* getAllActiveData() {
        return active_audio_data_.load();
      }

      force_inline poly_float* getActiveFrequencyAmplitudes(int frame_index) {
        return active_audio_data_.load()->frequency_amplitudes[clampActiveFrame(frame_index)];
      }

      force_inline poly_float* getActiveNormalizedFrequencies(int frame_index) {
        return active_audio_data_.load()->normalized_frequencies[clampActiveFrame(frame_index)];
      }

      force_inline int getActiveVersion() {
        return active_audio_data_.load()->version;
      }

      // Modified for SynthyBoy: points the table at data that someone else owns (a pre-built table), without waiting for the
      // audio thread or allocating. For the audio thread, between two blocks (it is the only reader).
      void useExternalData(WavetableData* data) { current_data_ = data; }
      void loadWaveFrame(const WaveFrame* wave_frame);
      void loadWaveFrame(const WaveFrame* wave_frame, int to_index);
      void postProcess(float max_span);

      force_inline int numFrames() const { return current_data_->num_frames; }
      force_inline int numActiveFrames() const { return active_audio_data_.load()->num_frames; }

      force_inline void markUsed() { active_audio_data_ = current_data_; }
      force_inline void markUnused() { active_audio_data_ = nullptr; }

      FrameRemap* frameRemap() { return &frame_remap_; }
      force_inline void setShepardTable(bool shepard) { shepard_table_ = shepard; }
      force_inline bool isShepardTable() { return shepard_table_; }

    protected:
      Wavetable() = default;
    
      void loadFrequencyAmplitudes(const std::complex<float>* frequencies, int to_index);
      void loadNormalizedFrequencies(const std::complex<float>* frequencies, int to_index);

      static const mono_float kZeroWaveform[kWaveformSize + kExtraValues];

      std::string name_;
      std::string author_;
      int max_frames_;
      WavetableData* current_data_;
      std::atomic<WavetableData*> active_audio_data_;
      std::unique_ptr<WavetableData> data_;
      bool shepard_table_;
      FrameRemap frame_remap_;

      mono_float fft_data_[2 * kWaveformSize];

      JUCE_LEAK_DETECTOR(Wavetable)
  };
} // namespace sbvital

