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
#include "wavetable_component.h"
#include "wave_frame.h"

class WaveSourceKeyframe;

class WaveSource : public WavetableComponent {
  public:
    enum InterpolationMode {
      kTime,
      kFrequency
    };

    WaveSource();
    virtual ~WaveSource();

    virtual WavetableKeyframe* createKeyframe(int position) override;
    virtual void render(sbvital::WaveFrame* wave_frame, float position) override;
    virtual WavetableComponentFactory::ComponentType getType() override;
    virtual json stateToJson() override;
    virtual void jsonToState(json data) override;

    sbvital::WaveFrame* getWaveFrame(int index);
    WaveSourceKeyframe* getKeyframe(int index);

    void setInterpolationMode(InterpolationMode mode) { interpolation_mode_ = mode; }
    InterpolationMode getInterpolationMode() const { return interpolation_mode_; }

  protected:
    std::unique_ptr<WaveSourceKeyframe> compute_frame_;
    InterpolationMode interpolation_mode_;
 
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveSource)
};

class WaveSourceKeyframe : public WavetableKeyframe {
  public:
    WaveSourceKeyframe() : interpolation_mode_(WaveSource::kFrequency) {
      wave_frame_ = std::make_unique<sbvital::WaveFrame>();
    }
    virtual ~WaveSourceKeyframe() { }

    sbvital::WaveFrame* wave_frame() { return wave_frame_.get(); }

    void copy(const WavetableKeyframe* keyframe) override;
    void linearTimeInterpolate(const sbvital::WaveFrame* from, const sbvital::WaveFrame* to, float t);

    void cubicTimeInterpolate(const sbvital::WaveFrame* prev, const sbvital::WaveFrame* from,
                              const sbvital::WaveFrame* to, const sbvital::WaveFrame* next,
                              float range_prev, float range, float range_next, float t);
    void linearFrequencyInterpolate(const sbvital::WaveFrame* from, const sbvital::WaveFrame* to, float t);

    void cubicFrequencyInterpolate(const sbvital::WaveFrame* prev, const sbvital::WaveFrame* from,
                                   const sbvital::WaveFrame* to, const sbvital::WaveFrame* next,
                                   float range_prev, float range, float range_next, float t);
    
    void interpolate(const WavetableKeyframe* from_keyframe,
                     const WavetableKeyframe* to_keyframe, float t) override;
    void smoothInterpolate(const WavetableKeyframe* prev_keyframe,
                           const WavetableKeyframe* from_keyframe,
                           const WavetableKeyframe* to_keyframe,
                           const WavetableKeyframe* next_keyframe, float t) override;
     
    void render(sbvital::WaveFrame* wave_frame) override {
      wave_frame->copy(wave_frame_.get());
    }

    json stateToJson() override;
    void jsonToState(json data) override;

    void setInterpolationMode(WaveSource::InterpolationMode mode) { interpolation_mode_ = mode; }
    WaveSource::InterpolationMode getInterpolationMode() const { return interpolation_mode_; }

  protected:
    std::unique_ptr<sbvital::WaveFrame> wave_frame_;
    WaveSource::InterpolationMode interpolation_mode_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveSourceKeyframe)
};
