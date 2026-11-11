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

#include "memory.h"
#include "synth_module.h"

namespace sbvital {
  class DigitalSvf;

  /** The equalizer of the Serum 2.1.5 law (13-new-keys.md 4.48): RBJ biquads, measured to 0.00 dB against the real plug-in. One stage of the cascade. */
  struct SerumEqStage {
    enum Kind { kPeak, kLowShelf, kHighShelf, kHighPass, kLowPass, kNotch, kOff };
    struct Coefficients {
      mono_float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    };

    /** The resonance percent (0 .. 1) as the quality factor: peak, high / low pass and notch 0.1 + 18.9053 x^4, shelf 0.4 + 1.7417 x^3 (measured against Serum 2.1.5). */
    static mono_float qualityOf(Kind kind, mono_float resonance);
    static Coefficients design(Kind kind, mono_float frequency, mono_float resonance, mono_float decibels, mono_float sample_rate);

    void reset() {
      z1 = 0.0f;
      z2 = 0.0f;
      initialised = false;
    }
    /** Filters in -> out over num_samples (may be the same buffer), the coefficients move linearly from the last block's to `target`. */
    void process(const poly_float* in, poly_float* out, int num_samples, const Coefficients& target);

    poly_float z1 = 0.0f, z2 = 0.0f;
    Coefficients current;
    bool initialised = false;
  };

  class EqualizerModule : public SynthModule {
    public:
      EqualizerModule();
      virtual ~EqualizerModule() { }

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new EqualizerModule(*this); }

      const StereoMemory* getAudioMemory() { return audio_memory_.get(); }

    protected:
      void processSerumLaw(const poly_float* audio_in, int num_samples);

      Value* low_mode_;
      Value* band_mode_;
      Value* high_mode_;
      Value* band2_mode_ = nullptr;      // 0 Off, 1 Peak, 2 Notch (block B34)
      Value* law_ = nullptr;             // 0 Vital (the SVF filters), 1 Serum (RBJ biquads of Serum 2.1.5) (block B34)
      Output* low_cutoff_ = nullptr;
      Output* band_cutoff_ = nullptr;
      Output* band2_cutoff_ = nullptr;
      Output* high_cutoff_ = nullptr;
      Output* low_resonance_ = nullptr;
      Output* band_resonance_ = nullptr;
      Output* band2_resonance_ = nullptr;
      Output* high_resonance_ = nullptr;
      Output* low_decibels_ = nullptr;
      Output* band_decibels_ = nullptr;
      Output* band2_decibels_ = nullptr;
      Output* high_decibels_ = nullptr;
      DigitalSvf* band2_shelf_ = nullptr;
      DigitalSvf* band2_notch_ = nullptr;
      SerumEqStage serum_low_, serum_band_, serum_band2_, serum_high_;
      bool was_serum_ = false;

      DigitalSvf* high_pass_;
      DigitalSvf* low_shelf_;
      DigitalSvf* notch_;
      DigitalSvf* band_shelf_;
      DigitalSvf* low_pass_;
      DigitalSvf* high_shelf_;

      std::shared_ptr<StereoMemory> audio_memory_;

      JUCE_LEAK_DETECTOR(EqualizerModule) 
  };
} // namespace sbvital

