/* Copyright 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sub oscillator source for SynthyBoy Vital engine (G23 Sub oscillator).
 */

#include "sub_source.h"

#include <algorithm>

namespace sbvital {

  SubSource::SubSource() : Processor(kNumInputs, kNumOutputs), phase_(0.0f), gate_(0.0f) {
  }

  void SubSource::process(int num_samples) {
    poly_mask reset_mask = getResetMask(kReset);
    phase_ = utils::maskLoad(phase_, poly_float(0.0f), reset_mask);

    poly_float midi_note = input(kMidi)->at(0);
    poly_float octave = input(kOctave)->at(0);
    poly_float tune = input(kTune)->at(0);

    poly_float total_pitch = utils::clamp(midi_note + octave * 12.0f + tune * 0.01f, -24.0f, 136.0f);
    poly_float frequency = utils::midiNoteToFrequency(total_pitch);
    poly_float sample_rate_recip = 1.0f / getSampleRate();
    poly_float phase_inc = poly_float::min(frequency * sample_rate_recip, poly_float(0.499f));

    int waveform = static_cast<int>(input(kWaveform)->at(0)[0] + 0.5f);
    bool mono = input(kMono)->at(0)[0] > 0.5f;

    poly_mask voice_mask = poly_float::equal(input(kActiveVoices)->at(0), 1.0f);
    if (mono) {
      poly_float lowest = input(kLowestNote)->at(0);
      poly_mask lowest_mask = poly_float::lessThanOrEqual(midi_note, lowest + 0.5f);
      voice_mask = voice_mask & lowest_mask;
    }

    poly_float level = utils::clamp(input(kLevel)->at(0), 0.0f, 1.0f);
    poly_float pan = utils::clamp(input(kPan)->at(0), -1.0f, 1.0f);
    poly_float pan_amplitude = futils::panAmplitude(pan);
    poly_float gain = level * pan_amplitude;

    // The mono gate (sub.mono) is ramped over the block: a lower note arriving or leaving swaps the voice that carries the sub
    // without a click.
    poly_float gate_target = utils::maskLoad(poly_float(0.0f), poly_float(1.0f), voice_mask);
    poly_float gate_step = (gate_target - gate_) * (1.0f / std::max(1, num_samples));
    poly_float gate = gate_;

    poly_float* raw_buffer = output(kRaw)->buffer;
    poly_float* levelled_buffer = output(kLevelled)->buffer;

    for (int i = 0; i < num_samples; ++i) {
      phase_ += phase_inc;
      phase_ -= utils::floor(phase_);
      const poly_float p = phase_;

      poly_float val = 0.0f;
      switch (waveform) {
        case kSine:
          val = futils::sin1(p);
          break;

        case kRoundedRect:
          // a soft-clipped sine, normalised to a peak of 1
          val = futils::tanh(futils::sin1(p) * 3.0f) * (1.0f / 0.99505f);
          break;

        case kTriangle: {
          // starts at zero and rises, like the sine
          poly_float x = p + 0.25f;
          x -= utils::floor(x);
          val = poly_float(1.0f) - poly_float::abs(x * 4.0f - 2.0f);
          break;
        }

        case kSaw:
          val = (p * 2.0f - 1.0f) - polyBlep(p, phase_inc);
          break;

        case kSquare:
          val = squareEdges(p, phase_inc, 0.5f);
          break;

        case kPulse:
          // 25 % duty cycle, centred on zero and scaled to a peak of 1
          val = (squareEdges(p, phase_inc, kPulseWidth) + (1.0f - 2.0f * kPulseWidth)) * (1.0f / (2.0f - 2.0f * kPulseWidth));
          break;

        default:
          val = futils::sin1(p);
          break;
      }

      gate += gate_step;
      val = val * gate;
      raw_buffer[i] = val;
      levelled_buffer[i] = val * gain;
    }
    gate_ = gate_target;
  }

  // PolyBLEP residual at the wrap of the phase p (0..1) for the increment inc: 0 away from the edge.
  poly_float SubSource::polyBlep(poly_float p, poly_float inc) {
    poly_float result = 0.0f;
    poly_mask after = poly_float::lessThan(p, inc);
    poly_mask before = poly_float::greaterThan(p, poly_float(1.0f) - inc);
    if (after.anyMask()) {
      poly_float t = p / inc;
      result = utils::maskLoad(result, (t + t) - (t * t) - 1.0f, after);
    }
    if (before.anyMask()) {
      poly_float t = (p - 1.0f) / inc;
      result = utils::maskLoad(result, (t * t) + (t + t) + 1.0f, before);
    }
    return result;
  }

  // A band-limited +1 / -1 wave that is +1 for the phase 0 .. width: rises at 0, falls at width.
  poly_float SubSource::squareEdges(poly_float p, poly_float inc, float width) {
    poly_float naive = utils::maskLoad(poly_float(-1.0f), poly_float(1.0f), poly_float::lessThan(p, width));
    poly_float shifted = p - width;
    shifted -= utils::floor(shifted);
    return naive + polyBlep(p, inc) - polyBlep(shifted, inc);
  }

} // namespace sbvital
