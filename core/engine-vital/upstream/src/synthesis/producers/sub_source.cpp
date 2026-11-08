/* Copyright 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sub oscillator source for SynthyBoy Vital engine (G23 Sub oscillator).
 */

#include "sub_source.h"

namespace sbvital {

  SubSource::SubSource() : Processor(kNumInputs, kNumOutputs), phase_(0.0f) {
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

    poly_float* raw_buffer = output(kRaw)->buffer;
    poly_float* levelled_buffer = output(kLevelled)->buffer;

    for (int i = 0; i < num_samples; ++i) {
      phase_ += phase_inc;
      phase_ -= utils::floor(phase_);
      poly_float p = phase_;

      poly_float val = 0.0f;
      switch (waveform) {
        case 0: // Sine
          val = futils::sin1(p);
          break;

        case 1: // Rounded Rect
          val = futils::tanh(futils::sin1(p) * 3.0f) * 1.05f;
          break;

        case 2: // Triangle
          val = poly_float::abs(p * 4.0f - 2.0f) - 1.0f;
          break;

        case 3: { // Saw
          poly_float blep = 0.0f;
          poly_mask m1 = poly_float::lessThan(p, phase_inc);
          poly_mask m2 = poly_float::greaterThan(p, poly_float(1.0f) - phase_inc);
          if (m1.anyMask()) {
            poly_float t = p / phase_inc;
            blep = utils::maskLoad(blep, (t + t) - (t * t) - 1.0f, m1);
          }
          if (m2.anyMask()) {
            poly_float t = (p - 1.0f) / phase_inc;
            blep = utils::maskLoad(blep, (t * t) + (t + t) + 1.0f, m2);
          }
          val = (p * 2.0f - 1.0f) - blep;
          break;
        }

        case 4: { // Square
          poly_float sign = utils::maskLoad(poly_float(1.0f), poly_float(-1.0f), poly_float::lessThan(p, 0.5f));
          poly_float blep = 0.0f;
          poly_mask m1 = poly_float::lessThan(p, phase_inc);
          poly_mask m2 = poly_float::greaterThan(p, poly_float(1.0f) - phase_inc);
          if (m1.anyMask()) {
            poly_float t = p / phase_inc;
            blep = utils::maskLoad(blep, (t + t) - (t * t) - 1.0f, m1);
          }
          if (m2.anyMask()) {
            poly_float t = (p - 1.0f) / phase_inc;
            blep = utils::maskLoad(blep, (t * t) + (t + t) + 1.0f, m2);
          }
          poly_float p_mid = p - 0.5f;
          poly_mask mm1 = poly_float::greaterThanOrEqual(p_mid, 0.0f) & poly_float::lessThan(p_mid, phase_inc);
          poly_mask mm2 = poly_float::lessThan(p_mid, 0.0f) & poly_float::greaterThan(p_mid, -phase_inc);
          if (mm1.anyMask()) {
            poly_float t = p_mid / phase_inc;
            blep = utils::maskLoad(blep, blep - ((t + t) - (t * t) - 1.0f), mm1);
          }
          if (mm2.anyMask()) {
            poly_float t = p_mid / phase_inc;
            blep = utils::maskLoad(blep, blep - ((t * t) + (t + t) + 1.0f), mm2);
          }
          val = sign + blep;
          break;
        }

        case 5: { // Pulse (25% duty cycle)
          poly_float sign = utils::maskLoad(poly_float(0.75f), poly_float(-0.25f), poly_float::lessThan(p, 0.25f));
          val = sign;
          break;
        }

        default:
          val = futils::sin1(p);
          break;
      }

      val = val & voice_mask;
      raw_buffer[i] = val;
      levelled_buffer[i] = val * gain;
    }
  }

} // namespace sbvital
