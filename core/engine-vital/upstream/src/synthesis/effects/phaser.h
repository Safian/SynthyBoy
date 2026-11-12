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

#include "processor_router.h"
#include "phaser_filter.h"
#include "operators.h"

namespace sbvital {

  class PhaserFilter;

  class Phaser : public ProcessorRouter {
    public:
      enum {
        kAudio,
        kMix,
        kRate,
        kFeedbackGain,
        kCenter,
        kModDepth,
        kPhaseOffset,
        kBlend,
        kModDepth2,   // SynthyBoy (13-new-keys.md 4.17, G76): semitones added to the depth of the right channel
        kNumInputs
      };

      enum {
        kAudioOutput,
        kCutoffOutput,
        kNumOutputs
      };

      Phaser();
      virtual ~Phaser() { }

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }
      void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void init() override;
      void hardReset() override;
      void correctToTime(double seconds);
      // the idle phaser filter is not reached by ProcessorRouter::setSampleRate (only by setOversampleAmount): without this the all-pass stages kept the
      // default 44.1 kHz and every notch / cutoff frequency was 8 % high at 48 kHz (phaser defect P2, 06-effects.md 8.1)
      void setSampleRate(int sample_rate) override;
      void setOversampleAmount(int oversample) override {
        ProcessorRouter::setOversampleAmount(oversample);
        cutoff_.ensureBufferSize(oversample * kMaxBufferSize);
      }

    private:
      poly_float totalDepth() const;

      Output cutoff_;
      PhaserFilter* phaser_filter_;
      poly_float mix_;
      poly_float mod_depth_;
      poly_float phase_offset_;
      poly_int phase_;

      JUCE_LEAK_DETECTOR(Phaser)
  };
} // namespace sbvital

