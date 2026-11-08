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

#include "random_lfo.h"

#include "synth_lfo.h"
#include "utils.h"
#include "futils.h"
#include "synth_constants.h"

namespace {
  constexpr float kLorenzInitial1 = 0.0f;
  constexpr float kLorenzInitial2 = 0.0f;
  constexpr float kLorenzInitial3 = 37.6f;
  constexpr float kLorenzA = 10.0f;
  constexpr float kLorenzB = 28.0f;
  constexpr float kLorenzC = 8.0f / 3.0f;
  constexpr float kLorenzTimeScale = 1.0f;
  constexpr float kLorenzSize = 40.0f;
  constexpr float kLorenzScale = 1.0f / kLorenzSize;
}

namespace sbvital {
  RandomLfo::RandomLfo() : Processor(kNumInputs, 1), random_generator_(-1.0f, 1.0f) {
    last_sync_ = std::make_shared<double>();
    sync_seconds_ = std::make_shared<double>();
    shared_state_ = std::make_shared<RandomState>();
    *sync_seconds_ = 0;
  }

  void RandomLfo::doReset(RandomState* state, bool mono, poly_float frequency) {
    poly_mask reset_mask = getResetMask(kReset);
    if (reset_mask.anyMask() == 0 || input(kSync)->at(0)[0])
      return;

    poly_float sample_offset = utils::toFloat(input(kReset)->source->trigger_offset);
    poly_float start_offset = frequency * (1.0f / getSampleRate()) * sample_offset;
    state->offset = utils::maskLoad(state->offset, -start_offset, reset_mask);
    poly_float from_random = 0.0f;
    poly_float to_random = 0.0f;
    if (mono) {
      from_random = random_generator_.polyVoiceNext();
      to_random = random_generator_.polyVoiceNext();
    }
    else {
      from_random = random_generator_.polyNext();
      to_random = random_generator_.polyNext();
    }

    state->last_random_value = utils::maskLoad(state->last_random_value, from_random, reset_mask);
    state->next_random_value = utils::maskLoad(state->next_random_value, to_random, reset_mask);
    last_value_ = utils::maskLoad(last_value_, state->last_random_value * 0.5f + 0.5f, reset_mask);
  }

  poly_int RandomLfo::updatePhase(RandomState* state, int num_samples) {
    poly_float frequency = input(kFrequency)->at(0);
    poly_float phase_delta = frequency * (1.0f / getSampleRate()) * num_samples;
    bool mono = input(kStereo)->at(0)[0] == 0.0f;
    poly_mask new_random_mask = 0;

    if (input(kSync)->at(0)[0]) {
      if (*last_sync_ != *sync_seconds_) {
        poly_float new_offset = utils::getCycleOffsetFromSeconds(*sync_seconds_, frequency);
        new_random_mask = poly_float::lessThan(new_offset, 0.5f) & poly_float::greaterThanOrEqual(state->offset, 0.5f);
        state->offset = new_offset;
      }
    }
    else {
      poly_float frequency = input(kFrequency)->at(0);
      doReset(state, mono, frequency);

      poly_float start_offset = state->offset;
      state->offset += phase_delta;
      new_random_mask = poly_float::greaterThanOrEqual(state->offset, 1.0f);
      state->offset = utils::mod(state->offset);
    }

    if (new_random_mask.anyMask()) {
      state->last_random_value = utils::maskLoad(state->last_random_value, state->next_random_value, new_random_mask);
      poly_float next_random = 0.0f; 
      if (mono)
        next_random = random_generator_.polyVoiceNext();
      else
        next_random = random_generator_.polyNext();

      state->next_random_value = utils::maskLoad(state->next_random_value, next_random, new_random_mask);

      poly_float delta = utils::maskLoad(phase_delta, 1.0f, poly_float::lessThanOrEqual(phase_delta, 0.0f));
      poly_float samples_to_wrap = state->offset / delta;
      return utils::roundToInt(samples_to_wrap);
    }

    return 0;
  }

  void RandomLfo::process(int num_samples) {
    if (input(kSync)->at(0)[0]) {
      if (*last_sync_ != *sync_seconds_) {
        process(shared_state_.get(), num_samples);

        poly_float* dest = output()->buffer;
        int update_samples = isControlRate() ? 1 : num_samples;
        for (int i = 0; i < update_samples; ++i) {
          poly_float value = dest[i] & constants::kFirstMask;
          dest[i] = value + utils::swapVoices(value);
        }

        poly_float trigger_value = output()->trigger_value & constants::kFirstMask;
        output()->trigger_value = trigger_value + utils::swapVoices(trigger_value);
        *last_sync_ = *sync_seconds_;
      }
    }
    else
      process(&state_, num_samples);
  }

  void RandomLfo::process(RandomState* state, int num_samples) {
    int random_type_int = std::round(utils::clamp(input(kStyle)->at(0)[0], 0.0f, kNumStyles - 1.0f));
    RandomType random_type = static_cast<RandomType>(random_type_int);

    if (random_type == kLorenzAttractor) {
      processLorenzAttractor(state, num_samples);
      return;
    }
    if (random_type == kRosslerAttractor) {
      processRosslerAttractor(state, num_samples);
      return;
    }
    if (random_type == kSampleAndHold) {
      processSampleAndHold(state, num_samples);
      return;
    }

    updatePhase(state, num_samples);

    poly_float result;
    switch (random_type) {
      case kPerlin:
        result = utils::perlinInterpolate(state->last_random_value, state->next_random_value, state->offset);
        break;
      case kSinInterpolate:
        result = futils::sinInterpolate(state->last_random_value, state->next_random_value, state->offset);
        break;
      default:
        result = 0.0f;
    }

    result = result * 0.5f + 0.5f;
    output()->trigger_value = result;

    poly_float* dest = output()->buffer;
    if (!isControlRate()) {
      poly_float current_value = last_value_;
      poly_float delta_value = (result - current_value) * (1.0f / num_samples);
      for (int i = 0; i < num_samples; ++i) {
        current_value += delta_value;
        dest[i] = current_value;
      }
    }
    else
      dest[0] = result;

    last_value_ = result;
  }

  void RandomLfo::processSampleAndHold(RandomState* state, int num_samples) {
    poly_float last_random_value = state->last_random_value * 0.5f + 0.5f;
    poly_int sample_change = updatePhase(state, num_samples);
    poly_float current_random_value = state->last_random_value * 0.5f + 0.5f;

    poly_float* dest = output()->buffer;
    if (!isControlRate()) {
      for (int i = 0; i < num_samples; ++i) {
        poly_mask over = poly_int::greaterThan(i, sample_change);
        dest[i] = utils::maskLoad(last_random_value, current_random_value, over);
      }
    }
    else
      dest[0] = current_random_value;

    output()->trigger_value = current_random_value;
  }

  void RandomLfo::processLorenzAttractor(RandomState* state, int num_samples) {
    static constexpr float kMaxFrequency = 0.01f;

    bool mono = input(kStereo)->at(0)[0] == 0.0f;
    poly_mask stereo_equal_mask = poly_float::equal(state->state1, utils::swapStereo(state->state1));
    poly_float stereo_bump_value = 0.0f;

    poly_float state1 = state->state1;
    poly_float state2 = state->state2;
    poly_float state3 = state->state3;

    poly_mask reset_mask = getResetMask(kReset);
    if (reset_mask.anyMask() && input(kSync)->at(0)[0] == 0.0f) {
      if (mono) {
        poly_float value1 = random_generator_.polyVoiceNext() + kLorenzInitial1;
        poly_float value2 = random_generator_.polyVoiceNext() + kLorenzInitial2;
        poly_float value3 = random_generator_.polyVoiceNext() + kLorenzInitial3;
        state1 = utils::maskLoad(state1, value1, reset_mask);
        state2 = utils::maskLoad(state2, value2, reset_mask);
        state3 = utils::maskLoad(state3, value3, reset_mask);
      }
      else {
        poly_float value1 = random_generator_.polyNext() + kLorenzInitial1;
        poly_float value2 = random_generator_.polyNext() + kLorenzInitial2;
        poly_float value3 = random_generator_.polyNext() + kLorenzInitial3;
        state1 = utils::maskLoad(state1, value1, reset_mask);
        state2 = utils::maskLoad(state2, value2, reset_mask);
        state3 = utils::maskLoad(state3, value3, reset_mask);
      }
    }

    if (mono) {
      state1 = state1 & constants::kLeftMask;
      state1 += utils::swapStereo(state1);
      state2 = state2 & constants::kLeftMask;
      state2 += utils::swapStereo(state2);
      state3 = state3 & constants::kLeftMask;
      state3 += utils::swapStereo(state3);
    }
    else
      state1 -= (state1 * 0.5f) & stereo_equal_mask & constants::kLeftMask;

    poly_float frequency = input(kFrequency)->at(0);
    poly_float t = utils::min(kMaxFrequency, frequency * (0.5f / getSampleRate()));

    poly_float* dest = output()->buffer;
    for (int i = 0; i < num_samples; ++i) {
      poly_float delta1 = (state2 - state1) * kLorenzA;
      poly_float delta2 = (-state3 + kLorenzB) * state1 - state2;
      poly_float delta3 = state1 * state2 - state3 * kLorenzC;
      state1 += delta1 * t;
      state2 += delta2 * t;
      state3 += delta3 * t;

      dest[i] = state1 * kLorenzScale + 0.5f;
    }

    state->state1 = state1;
    state->state2 = state2;
    state->state3 = state3;

    output()->trigger_value = state->state1 * kLorenzScale + 0.5f;
  }

  // SynthyBoy: the Rossler attractor (a = b = 0.2, c = 5.7), 4th order Runge-Kutta, the same system, constants, time scale (one turn of
  // the orbit about 6 time units) and output normalisation as synthyboy::shapes::RosslerAttractor (core/shapes), per voice and stereo
  // lane. The output is x / 12 mapped to 0..1.
  void RandomLfo::processRosslerAttractor(RandomState* state, int num_samples) {
    static constexpr float kA = 0.2f, kB = 0.2f, kC = 5.7f;
    static constexpr float kRosslerInitialX = -6.0f;

    bool mono = input(kStereo)->at(0)[0] == 0.0f;
    poly_float x = state->state1, y = state->state2, z = state->state3;

    poly_mask reset_mask = getResetMask(kReset);
    if (reset_mask.anyMask() && input(kSync)->at(0)[0] == 0.0f) {
      poly_float jitter = mono ? random_generator_.polyVoiceNext() : random_generator_.polyNext();
      x = utils::maskLoad(x, jitter * 0.5f + kRosslerInitialX, reset_mask);
      y = utils::maskLoad(y, 0.0f, reset_mask);
      z = utils::maskLoad(z, 0.0f, reset_mask);
    }
    // a state that was never started (the constructor's 0.1, 0, 0 sits on the unstable fixed point side): begin on the orbit
    poly_mask unstarted = poly_float::equal(z, 0.0f) & poly_float::equal(y, 0.0f) & poly_float::equal(x, 0.1f);
    x = utils::maskLoad(x, kRosslerInitialX, unstarted);

    poly_float frequency = input(kFrequency)->at(0);
    poly_float h = utils::clamp(frequency * (6.0f / getSampleRate()), 0.00001f, 0.05f);
    poly_float* dest = output()->buffer;
    int count = isControlRate() ? 1 : num_samples;
    int steps = isControlRate() ? num_samples : 1;

    for (int i = 0; i < count; ++i) {
      for (int s = 0; s < steps; ++s) {
        poly_float k1x = -y - z, k1y = x + y * kA, k1z = (x - kC) * z + kB;
        poly_float x2 = x + k1x * h * 0.5f, y2 = y + k1y * h * 0.5f, z2 = z + k1z * h * 0.5f;
        poly_float k2x = -y2 - z2, k2y = x2 + y2 * kA, k2z = (x2 - kC) * z2 + kB;
        poly_float x3 = x + k2x * h * 0.5f, y3 = y + k2y * h * 0.5f, z3 = z + k2z * h * 0.5f;
        poly_float k3x = -y3 - z3, k3y = x3 + y3 * kA, k3z = (x3 - kC) * z3 + kB;
        poly_float x4 = x + k3x * h, y4 = y + k3y * h, z4 = z + k3z * h;
        poly_float k4x = -y4 - z4, k4y = x4 + y4 * kA, k4z = (x4 - kC) * z4 + kB;
        x += (k1x + k2x * 2.0f + k3x * 2.0f + k4x) * (h * (1.0f / 6.0f));
        y += (k1y + k2y * 2.0f + k3y * 2.0f + k4y) * (h * (1.0f / 6.0f));
        z += (k1z + k2z * 2.0f + k3z * 2.0f + k4z) * (h * (1.0f / 6.0f));

        // numerical safety (as the package): a diverged state restarts on the orbit
        poly_mask bad = poly_float::greaterThan(poly_float::abs(x), 50.0f) | poly_float::greaterThan(poly_float::abs(y), 50.0f) |
                        poly_float::lessThan(z, -10.0f) | poly_float::greaterThan(z, 100.0f);
        x = utils::maskLoad(x, kRosslerInitialX, bad);
        y = utils::maskLoad(y, 0.0f, bad);
        z = utils::maskLoad(z, 0.0f, bad);
      }
      dest[i] = utils::clamp(x * (1.0f / 24.0f) + 0.5f, 0.0f, 1.0f);
    }

    if (mono) {
      // both stereo lanes of a voice follow the left lane
      x = x & constants::kLeftMask;
      x += utils::swapStereo(x);
      y = y & constants::kLeftMask;
      y += utils::swapStereo(y);
      z = z & constants::kLeftMask;
      z += utils::swapStereo(z);
    }

    state->state1 = x;
    state->state2 = y;
    state->state3 = z;
    output()->trigger_value = utils::clamp(x * (1.0f / 24.0f) + 0.5f, 0.0f, 1.0f);
  }

  void RandomLfo::correctToTime(double seconds) {
    *sync_seconds_ = seconds;
  }
} // namespace sbvital
