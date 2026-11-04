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

#include "shepard_tone_source.h"
#include "wavetable_component_factory.h"

ShepardToneSource::ShepardToneSource() {
  loop_frame_ = std::make_unique<WaveSourceKeyframe>();
}

ShepardToneSource::~ShepardToneSource() { }

void ShepardToneSource::render(sbvital::WaveFrame* wave_frame, float position) {
  if (numFrames() == 0)
    return;

  WaveSourceKeyframe* keyframe = getKeyframe(0);
  sbvital::WaveFrame* key_wave_frame = keyframe->wave_frame();
  sbvital::WaveFrame* loop_wave_frame = loop_frame_->wave_frame();

  for (int i = 0; i < sbvital::WaveFrame::kWaveformSize / 2; ++i) {
    loop_wave_frame->frequency_domain[i * 2] = key_wave_frame->frequency_domain[i];
    loop_wave_frame->frequency_domain[i * 2 + 1] = 0.0f;
  }

  loop_wave_frame->toTimeDomain();

  compute_frame_->setInterpolationMode(interpolation_mode_);
  compute_frame_->interpolate(keyframe, loop_frame_.get(), position / (sbvital::kNumOscillatorWaveFrames - 1.0f));
  wave_frame->copy(compute_frame_->wave_frame());
}

WavetableComponentFactory::ComponentType ShepardToneSource::getType() {
  return WavetableComponentFactory::kShepardToneSource;
}