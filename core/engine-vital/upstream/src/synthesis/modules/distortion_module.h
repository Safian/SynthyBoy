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

#include "synth_constants.h"
#include "synth_module.h"

namespace sbvital {

  class Distortion;
  class DigitalSvf;

  class DistortionModule : public SynthModule {
    public:
      /** `keytrack` (the note of the last key relative to C4, may be null): SynthyBoy's distortion.filter_keytrack scales it onto the filter cutoff. */
      DistortionModule(const Output* keytrack = nullptr);
      virtual ~DistortionModule();

      /** The two drawn X-Shaper curves (Distortion::kShaperSize floats each; null = identity). Audio thread, between blocks. */
      void setShaperCurves(const float* curve_a, const float* curve_b);

      virtual void init() override;
      virtual void setSampleRate(int sample_rate) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      virtual Processor* clone() const override { return new DistortionModule(*this); }

    protected:
      Distortion* distortion_;
      Value* filter_order_;
      DigitalSvf* filter_;
      Output* distortion_mix_;
      poly_float mix_;
      const Output* keytrack_;

      JUCE_LEAK_DETECTOR(DistortionModule)
  };
} // namespace sbvital

