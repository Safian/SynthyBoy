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

#include "synth_oscillator.h"

#include "distortion.h"
#include "fourier_transform.h"
#include "futils.h"
#include "matrix.h"
#include "wavetable.h"

#include <climits>
#include <cmath>

namespace sbvital {
  namespace {
    constexpr int kNumVoicesPerProcess = poly_float::kSize / 2;
    constexpr int kWaveformBits = WaveFrame::kWaveformBits;
    constexpr int kIntermediateBits = 8 * sizeof(uint32_t) - kWaveformBits;
    constexpr int kHalfPhase = INT_MIN;
    constexpr unsigned long long kFullPhase = 1ULL + (unsigned long long)(UINT_MAX);
    constexpr mono_float kPhaseMult = kFullPhase;
    constexpr mono_float kInvPhaseMult = 1.0f / kFullPhase;
    constexpr mono_float kIntermediateMult = 1 << kIntermediateBits;
    const poly_int kIntermediateMask = (1 << kIntermediateBits) - 1;

    constexpr mono_float kPhaseBits = (8 * sizeof(uint32_t));
    constexpr mono_float kDistortBits = kPhaseBits;
    constexpr mono_float kMaxQuantize = 0.85f;

    constexpr mono_float kMaxSqueezePercent = 0.95f;
    constexpr mono_float kMaxAmplitude = 2.0f;

    constexpr mono_float kCenterLowAmplitude = 0.4f;
    constexpr mono_float kDetunedHighAmplitude = 0.6f;
    constexpr mono_float kWavetableFadeTime = 0.007f;

    constexpr int kMaxSyncPower = 4;
    constexpr int kMaxSync = 1 << kMaxSyncPower;

    constexpr mono_float kFifthMult = 1.49830707688f;
    constexpr mono_float kMajorThirdMult = 1.25992104989f;
    constexpr mono_float kMinorThirdMult = 1.189207115f;
    constexpr mono_float kNoMidiTrackDefault = 48.0f;
    
    const poly_float kFmPhaseMult = kPhaseMult / 8.0f;
    const poly_int kMaxFmModulation = 48;

    force_inline poly_int passThroughPhase(poly_int phase, poly_float, poly_int, const poly_float*, int) {
      return phase;
    }

    force_inline poly_int quantizePhase(poly_int phase, poly_float distortion, poly_int distortion_phase,
                                        const poly_float*, int) {
      poly_float normal_phase = utils::toFloat(phase) * distortion * kInvPhaseMult;
      poly_float adjustment = utils::toFloat(distortion_phase) * kInvPhaseMult;
      poly_float floored_phase = utils::trunc(normal_phase + adjustment) - adjustment;
      return utils::toInt((floored_phase / distortion) * kPhaseMult) - distortion_phase;
    }

    force_inline poly_int bendPhase(poly_int phase, poly_float distortion, poly_int distortion_phase,
                                    const poly_float*, int) {
      poly_float float_phase = utils::toFloat(phase - distortion_phase) * (1.0f / kPhaseMult) + 0.5f;

      poly_float distortion_offset = (distortion - distortion * distortion) * 2.0f;
      poly_float float_phase2 = float_phase * float_phase;
      poly_float float_phase3 = float_phase * float_phase2;

      poly_float distortion_scale = distortion * 3.0f;
      poly_float middle_mult1 = distortion_scale + distortion_offset;
      poly_float middle_mult2 = distortion_scale - distortion_offset;
      poly_float middle1 = middle_mult1 * (float_phase2 - float_phase3);
      poly_float middle2 = middle_mult2 * (float_phase - float_phase2 * 2.0f + float_phase3);
      poly_float new_phase = float_phase3 + middle1 + middle2;
      return utils::toInt((new_phase - 0.5f) * kPhaseMult);
    }

    force_inline poly_int squeezePhase(poly_int phase, poly_float distortion, poly_int distortion_phase,
                                       const poly_float*, int) {
      static const poly_float kCenterPhase = kPhaseMult / 4.0f;
      static const poly_float kMaxPhase = kPhaseMult / 2.0f;
      poly_float float_phase = utils::toFloat(phase - distortion_phase);
      poly_mask positive_mask = poly_float::greaterThan(float_phase, 0.0f);
      float_phase = poly_float::abs(float_phase);

      poly_float pivot = distortion * kCenterPhase;
      poly_mask right_half_mask = poly_float::greaterThan(float_phase, pivot);

      poly_float left_phase = float_phase / distortion;
      poly_float right_phase = kMaxPhase - (kMaxPhase - float_phase) / (poly_float(2.0f) - distortion);
      poly_float new_phase = utils::maskLoad(left_phase, right_phase, right_half_mask);
      new_phase = utils::maskLoad(-new_phase, new_phase, positive_mask);
      return utils::toInt(new_phase);
    }

    force_inline poly_int syncPhase(poly_int phase, poly_float distortion, poly_int, const poly_float*, int) {
      poly_float float_val = utils::toFloat(phase + kHalfPhase) * distortion;
      return utils::toInt(float_val) * kMaxSync + kHalfPhase;
    }

    force_inline poly_int pulseWidthPhase(poly_int phase, poly_float distortion, poly_int distortion_phase,
                                          const poly_float*, int) {
      poly_float distorted_phase = utils::toFloat(phase) * distortion;
      poly_float clamped_phase = utils::clamp(distorted_phase, INT_MIN, INT_MAX);
      return utils::toInt(clamped_phase);
    }
    
    force_inline poly_int fmPhase(poly_int phase, poly_float distortion, poly_int,
                                  const poly_float* modulation, int i) {
      poly_float phase_offset = modulation[i] * distortion;
      return phase + utils::toInt(phase_offset * kFmPhaseMult) * kMaxFmModulation;
    }

    force_inline poly_int fmPhaseLeft(poly_int phase, poly_float distortion, poly_int,
                                      const poly_float* modulation, int i) {
      poly_float mod = modulation[i] & constants::kFirstMask;
      mod += utils::swapVoices(mod);
      poly_float phase_offset = mod * distortion;
      return phase + utils::toInt(phase_offset * kFmPhaseMult) * kMaxFmModulation;
    }
    
    force_inline poly_int fmPhaseRight(poly_int phase, poly_float distortion, poly_int,
                                       const poly_float* modulation, int i) {
      poly_float mod = modulation[i] & constants::kSecondMask;
      mod += utils::swapVoices(mod);
      poly_float phase_offset = mod * distortion;
      return phase + utils::toInt(phase_offset * kFmPhaseMult) * kMaxFmModulation;
    }

    force_inline poly_float passThroughWindow(poly_int, poly_int, poly_float, const poly_float*, int) {
      return 1.0f;
    }

    force_inline poly_float pulseWidthWindow(poly_int, poly_int distorted_phase, poly_float, const poly_float*, int) {
      return poly_float(1.0f) & ~poly_int::equal(distorted_phase, INT_MIN);
    }

    force_inline poly_float halfSinWindow(poly_int phase, poly_int, poly_float, const poly_float*, int) {
      poly_float normal_phase = utils::toFloat(phase + INT_MAX) * (kInvPhaseMult / 2.0f);
      return futils::sin(normal_phase + 0.25f);
    }

    force_inline poly_float rmWindow(poly_int, poly_int, poly_float distortion, const poly_float* modulation, int i) {
      return utils::interpolate(1.0f, modulation[i], distortion);
    }

    force_inline poly_float rmWindowLeft(poly_int, poly_int, poly_float distortion,
                                         const poly_float* modulation, int i) {
      poly_float mod = modulation[i] & constants::kFirstMask;
      mod += utils::swapVoices(mod);
      return utils::interpolate(1.0f, mod, distortion);
    }

    force_inline poly_float rmWindowRight(poly_int, poly_int, poly_float distortion, 
                                          const poly_float* modulation, int i) {
      poly_float mod = modulation[i] & constants::kSecondMask;
      mod += utils::swapVoices(mod);
      return utils::interpolate(1.0f, mod, distortion);
    }

    // SynthyBoy: the second warp slot. Runs on the phase the first slot produced (docs/ui-design/13-new-keys.md 4.9, A15); the phase
    // distortion of the slot is evaluated at run time (the first slot is a template for speed, this one is skipped when it is None).
    force_inline poly_int warp2Phase(int type, int side, poly_int phase, poly_float amount, poly_int dist_phase,
                                     const poly_float* mod, int i) {
      switch (type) {
        case SynthOscillator::kSync:
        case SynthOscillator::kFormant:
          return syncPhase(phase, amount, dist_phase, nullptr, 0);
        case SynthOscillator::kQuantize:
          return quantizePhase(phase, amount, dist_phase, nullptr, 0);
        case SynthOscillator::kBend:
          return bendPhase(phase, amount, dist_phase, nullptr, 0);
        case SynthOscillator::kSqueeze:
          return squeezePhase(phase, amount, dist_phase, nullptr, 0);
        case SynthOscillator::kPulseWidth:
          return pulseWidthPhase(phase, amount, dist_phase, nullptr, 0);
        case SynthOscillator::kFmOscillatorA:
        case SynthOscillator::kFmOscillatorB:
        case SynthOscillator::kFmSample:
          if (mod == nullptr)
            return phase;
          return side == 0 ? fmPhase(phase, amount, dist_phase, mod, i)
                           : (side == 1 ? fmPhaseLeft(phase, amount, dist_phase, mod, i) : fmPhaseRight(phase, amount, dist_phase, mod, i));
        default:
          return phase;
      }
    }

    force_inline poly_float warp2Window(int type, int side, poly_int phase, poly_int distorted, poly_float amount,
                                        const poly_float* mod, int i) {
      switch (type) {
        case SynthOscillator::kFormant:
          return halfSinWindow(phase, distorted, amount, nullptr, 0);
        case SynthOscillator::kPulseWidth:
          return pulseWidthWindow(phase, distorted, amount, nullptr, 0);
        case SynthOscillator::kRmOscillatorA:
        case SynthOscillator::kRmOscillatorB:
        case SynthOscillator::kRmSample:
          if (mod == nullptr)
            return 1.0f;
          return side == 0 ? rmWindow(phase, distorted, amount, mod, i)
                           : (side == 1 ? rmWindowLeft(phase, distorted, amount, mod, i) : rmWindowRight(phase, distorted, amount, mod, i));
        default:
          return 1.0f;
      }
    }

    // ---------------------------------------------------------------------------------------------------------------------------------
    // SynthyBoy block B21 (docs/ui-design/13-new-keys.md 4.22): the Serum 2 warp modes that are not in Vital. They run per lane in plain
    // arithmetic (they are optional, the Vital modes keep their templated fast path). Phase domain modes move the read phase, the value
    // domain modes (Odd/Even, Filter, Distortion) act on the sample after the wavetable read, FM / PD / AM use the other oscillator.
    // ---------------------------------------------------------------------------------------------------------------------------------
    constexpr double kCycle = 4294967296.0;
    constexpr double kXTwoPi = 6.283185307179586;

    force_inline double cycleOfPhase(uint32_t phase) { return static_cast<double>(phase) * (1.0 / kCycle); }

    force_inline uint32_t phaseOfCycle(double u) {
      u -= std::floor(u);
      const double p = u * kCycle;
      return p >= 4294967295.0 ? 0u : static_cast<uint32_t>(p);
    }

    // y = 0..1 of a curve with 257 values (the remap curve of the oscillator), x clamped to 0..1
    force_inline double curveAt(const mono_float* table, double x) {
      x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
      const double pos = x * 256.0;
      const int i = static_cast<int>(pos);
      if (i >= 256)
        return table[256];
      const double f = pos - i;
      return table[i] + (table[i + 1] - table[i]) * f;
    }

    // Bend: s > 0 pinches the waveform towards the middle of the cycle, s < 0 pulls it outwards, each half of the cycle on its own
    force_inline double asymmetricPower(double s) { return s >= 0.0 ? 1.0 + 3.0 * s : 1.0 / (1.0 + 3.0 * -s); }

    force_inline double bendCycle(double u, double s) {
      const double h = u < 0.5 ? 2.0 * u : 2.0 * (1.0 - u);
      const double r = 0.5 * std::pow(h, asymmetricPower(s));
      return u < 0.5 ? r : 1.0 - r;
    }

    // Asym: the same on the whole cycle, to the right (s > 0) or to the left (s < 0)
    force_inline double asymCycle(double u, double s) {
      return s >= 0.0 ? std::pow(u, 1.0 + 3.0 * s) : 1.0 - std::pow(1.0 - u, 1.0 + 3.0 * -s);
    }

    // The new cycle position of the phase domain modes; a = the warp amount 0..1
    double warpCycle(int type, double u, double a, const mono_float* table) {
      switch (type) {
        case SynthOscillator::kBendPlus: return bendCycle(u, a);
        case SynthOscillator::kBendMinus: return bendCycle(u, -a);
        case SynthOscillator::kBendPlusMinus: return bendCycle(u, 2.0 * a - 1.0);
        case SynthOscillator::kAsymPlus: return asymCycle(u, a);
        case SynthOscillator::kAsymMinus: return asymCycle(u, -a);
        case SynthOscillator::kAsymPlusMinus: return asymCycle(u, 2.0 * a - 1.0);
        case SynthOscillator::kMirror: {
          // the second half plays the first half backwards; the amount bends both halves like Asym +/-
          const double h = u < 0.5 ? 2.0 * u : 2.0 * (1.0 - u);
          return 0.5 * asymCycle(h, 2.0 * a - 1.0);
        }
        case SynthOscillator::kRemap1: return u + a * (curveAt(table, u) - u);
        case SynthOscillator::kRemap2: {
          const double half = u < 0.5 ? 0.0 : 1.0;
          const double h = 2.0 * u - half;
          return (h + a * (curveAt(table, h) - h) + half) * 0.5;
        }
        case SynthOscillator::kRemap3: return u - a * std::sin(kXTwoPi * u) / kXTwoPi;
        case SynthOscillator::kRemap4: {
          const double x = 4.0 * u;
          const double k = std::floor(x);
          const double h = x - k;
          return (k + h + a * (curveAt(table, h) - h)) * 0.25;
        }
        default: return u;
      }
    }

    force_inline bool isXPhaseDomain(int type) {
      return (type >= SynthOscillator::kBendPlus && type <= SynthOscillator::kAsymPlusMinus) || type == SynthOscillator::kMirror ||
             (type >= SynthOscillator::kRemap1 && type <= SynthOscillator::kRemap4);
    }

    force_inline bool isXDistortion(int type) { return type >= SynthOscillator::kShTube && type <= SynthOscillator::kShSoftSat; }

    force_inline poly_float xModulator(const poly_float* mod, int side, int i) {
      if (side == 0)
        return mod[i];
      poly_float m = mod[i] & (side == 1 ? constants::kFirstMask : constants::kSecondMask);
      m += utils::swapVoices(m);
      return m;
    }

    // The distortion modes: the drive is 0 .. 30 dB over the warp amount
    force_inline poly_float xShape(int type, poly_float value, poly_float amount) {
      const poly_float drive = Distortion::driveDbScale(amount * 30.0f);
      // the product value x drive stays inside +-40: the exponentials of the diode curves leave the float range beyond (a hot drive on a
      // loud wave gave NaN in the random walk of test_rt_alloc)
      const poly_float limit = poly_float(40.0f) / drive;
      value = utils::clamp(value, -limit, limit);
      switch (type) {
        case SynthOscillator::kShTube: return Distortion::getDrivenValue(Distortion::kTube, value, drive);
        case SynthOscillator::kShSoftClip: return Distortion::getDrivenValue(Distortion::kSoftClip, value, drive);
        case SynthOscillator::kShHardClip: return Distortion::getDrivenValue(Distortion::kHardClip, value, drive);
        case SynthOscillator::kShDiode1: return Distortion::getDrivenValue(Distortion::kDiode1, value, drive);
        case SynthOscillator::kShDiode2: return Distortion::getDrivenValue(Distortion::kDiode2, value, drive);
        case SynthOscillator::kShLinearFold: return Distortion::getDrivenValue(Distortion::kLinearFold, value, drive);
        case SynthOscillator::kShSineFold: return Distortion::getDrivenValue(Distortion::kSinFold, value, drive);
        case SynthOscillator::kShZeroSquare: return Distortion::getDrivenValue(Distortion::kZeroSquare, value, drive);
        case SynthOscillator::kShAsym: return Distortion::getDrivenValue(Distortion::kAsym, value, drive);
        case SynthOscillator::kShRectify: return Distortion::getDrivenValue(Distortion::kRectify, value, drive);
        default: break;
      }
      poly_float out;
      for (size_t l = 0; l < poly_float::kSize; ++l) {
        const float v = value[l] * drive[l];
        float y = v;
        switch (type) {
          case SynthOscillator::kShSineShaper: y = std::sin(v * 1.5707963f); break;
          case SynthOscillator::kShStompBox: y = 0.7f * std::tanh(v * 2.0f) + 0.3f * std::max(-1.0f, std::min(1.0f, v)); break;
          case SynthOscillator::kShTapeSat: y = 0.63661977f * std::atan(v * 1.4137167f); break;
          case SynthOscillator::kShSoftSat: y = v / (1.0f + std::fabs(v)); break;
          default: break;
        }
        out.set(l, y);
      }
      return out;
    }

    // The phase stage of an extended warp slot. phase = the phase the slot receives, amount = the warp amount 0..1 per lane, inc = the phase
    // increment of this sample (int units, as float). mult is multiplied by the window of the mode. read(phase) reads the wavetable.
    template<class Read>
    force_inline poly_int xWarpPhase(const SynthOscillator::WarpStage& st, SynthOscillator::WarpState& ws, poly_int phase, poly_float amount,
                                     poly_float inc, int i, poly_float& mult, Read read) {
      const int type = st.type;
      if (isXPhaseDomain(type)) {
        poly_int out;
        for (size_t l = 0; l < poly_float::kSize; ++l) {
          const double a = std::max(0.0f, std::min(1.0f, amount[l]));
          out.set(l, phaseOfCycle(warpCycle(type, cycleOfPhase(phase[l]), a, st.remap)));
        }
        return out;
      }
      switch (type) {
        case SynthOscillator::kSyncVar: {
          // hard sync with a raised cosine fade of the slave cycle around its reset: VAR 0 is the hard edge, VAR 1 fades half a cycle each side
          poly_int out = syncPhase(phase, amount, 0, nullptr, 0);
          poly_float window;
          for (size_t l = 0; l < poly_float::kSize; ++l) {
            const double u = cycleOfPhase(out[l]);
            const double width = 0.5 * std::max(0.0f, std::min(1.0f, st.var[l]));
            const double edge = std::min(u, 1.0 - u);
            double w = 1.0;
            if (width > 1.0e-4 && edge < width) {
              const double x = edge / width;
              w = 0.5 - 0.5 * std::cos(3.141592653589793 * x);
            }
            window.set(l, static_cast<float>(w));
          }
          mult *= window;
          return out;
        }
        case SynthOscillator::kFlip: {
          poly_float sign;
          for (size_t l = 0; l < poly_float::kSize; ++l) {
            const double a = std::max(0.0f, std::min(1.0f, amount[l]));
            sign.set(l, cycleOfPhase(phase[l]) < 1.0 - a ? 1.0f : -1.0f);
          }
          mult *= sign;
          return phase;
        }
        case SynthOscillator::kFmThruZeroA: case SynthOscillator::kFmThruZeroB:
        case SynthOscillator::kFmExpA: case SynthOscillator::kFmExpB:
        case SynthOscillator::kFmLinearA: case SynthOscillator::kFmLinearB: {
          if (st.mod == nullptr)
            return phase;
          const poly_float m = xModulator(st.mod, st.side, i);
          const bool exponential = type == SynthOscillator::kFmExpA || type == SynthOscillator::kFmExpB;
          const bool clamped = type == SynthOscillator::kFmLinearA || type == SynthOscillator::kFmLinearB;
          for (size_t l = 0; l < poly_float::kSize; ++l) {
            const double a = std::max(0.0f, std::min(1.0f, amount[l]));
            const double mod = std::max(-4.0f, std::min(4.0f, m[l]));
            double deviation;
            if (exponential)
              deviation = std::exp2(4.0 * a * a * mod) - 1.0;
            else {
              deviation = 8.0 * a * a * mod;
              if (clamped)
                deviation = std::max(deviation, -1.0);
            }
            const int64_t step = static_cast<int64_t>(static_cast<double>(inc[l]) * deviation);
            ws.acc.set(l, static_cast<uint32_t>(static_cast<uint64_t>(ws.acc[l]) + static_cast<uint64_t>(step)));
          }
          return phase + ws.acc;
        }
        case SynthOscillator::kPdA: case SynthOscillator::kPdB: {
          if (st.mod == nullptr)
            return phase;
          const poly_float m = xModulator(st.mod, st.side, i);
          poly_int out;
          for (size_t l = 0; l < poly_float::kSize; ++l) {
            const double a = std::max(0.0f, std::min(1.0f, amount[l]));
            const double mod = std::max(-1.0f, std::min(1.0f, m[l]));
            out.set(l, phase[l] + phaseOfCycle(0.9 * a * mod));
          }
          return out;
        }
        case SynthOscillator::kPdSelf: {
          // the wavetable read at the unmodified phase is the modulator of the second read
          const poly_float self = read(phase);
          poly_int out;
          for (size_t l = 0; l < poly_float::kSize; ++l) {
            const double a = std::max(0.0f, std::min(1.0f, amount[l]));
            const double mod = std::max(-1.0f, std::min(1.0f, self[l]));
            out.set(l, phase[l] + phaseOfCycle(0.9 * a * mod));
          }
          return out;
        }
        case SynthOscillator::kAmA: case SynthOscillator::kAmB: {
          if (st.mod == nullptr)
            return phase;
          const poly_float m = xModulator(st.mod, st.side, i);
          poly_float gain;
          for (size_t l = 0; l < poly_float::kSize; ++l) {
            const float a = std::max(0.0f, std::min(1.0f, amount[l]));
            const float mod = std::max(-1.0f, std::min(1.0f, m[l]));
            gain.set(l, 1.0f - a + a * (0.5f + 0.5f * mod));
          }
          mult *= gain;
          return phase;
        }
        default:
          return phase;
      }
    }

    // The value stage of an extended warp slot: Odd/Even, the filters and the distortion group. read_phase is the phase of `result`.
    template<class Read>
    force_inline poly_float xWarpValue(const SynthOscillator::WarpStage& st, SynthOscillator::WarpState& ws, poly_int read_phase, poly_float amount,
                                       poly_float inc, poly_float result, Read read) {
      const int type = st.type;
      if (isXDistortion(type))
        return xShape(type, result, amount);
      switch (type) {
        case SynthOscillator::kOddEven: {
          // E = even harmonics, O = odd harmonics (the wave and itself half a cycle later); 50 % is the original, 0 % odd only, 100 % even only
          const poly_float shifted = read(read_phase + poly_int(static_cast<uint32_t>(0x80000000u)));
          poly_float out;
          for (size_t l = 0; l < poly_float::kSize; ++l) {
            const float a = std::max(0.0f, std::min(1.0f, amount[l]));
            const float even = a > 0.5f ? 1.0f : 2.0f * a;
            const float odd = a < 0.5f ? 1.0f : 2.0f * (1.0f - a);
            const float e = 0.5f * (result[l] + shifted[l]);
            const float o = 0.5f * (result[l] - shifted[l]);
            out.set(l, even * e + odd * o);
          }
          return out;
        }
        case SynthOscillator::kFilterLow:
        case SynthOscillator::kFilterHigh: {
          const bool high = type == SynthOscillator::kFilterHigh;
          poly_float out;
          for (size_t l = 0; l < poly_float::kSize; ++l) {
            const float a = std::max(0.0f, std::min(1.0f, amount[l]));
            // the cutoff in multiples of the fundamental: LPF 1 .. 128 (open at the top), HPF 0.5 .. 64 (open at the bottom)
            const float ratio = high ? 0.5f * std::exp2(7.0f * a) : std::exp2(7.0f * a);
            const float x = std::min(ratio * static_cast<float>(kXTwoPi) * std::fabs(inc[l]) * static_cast<float>(1.0 / kCycle), 8.0f);
            const float g = x / (1.0f + x);
            const float in = result[l];
            const float lp1 = ws.lp1[l] + g * (in - ws.lp1[l]);
            ws.lp1.set(l, lp1);
            if (high) {
              const float h1 = in - lp1;
              const float lp2 = ws.lp2[l] + g * (h1 - ws.lp2[l]);
              ws.lp2.set(l, lp2);
              out.set(l, h1 - lp2);
            }
            else {
              const float lp2 = ws.lp2[l] + g * (lp1 - ws.lp2[l]);
              ws.lp2.set(l, lp2);
              out.set(l, lp2);
            }
          }
          return out;
        }
        default:
          return result;
      }
    }

    force_inline poly_float noTransposeSnap(poly_float midi, poly_float transpose, float*) {
      return midi + transpose;
    }

    force_inline poly_float localTransposeSnap(poly_float midi, poly_float transpose, float* snap_buffer) {
      static constexpr float kScaleDown = 1.0f / kNotesPerOctave;
      static constexpr float kScaleUp = kNotesPerOctave;

      poly_float note_offset = utils::mod(transpose * kScaleDown) * kScaleUp;
      poly_float octave_snap = transpose - note_offset;
      poly_int int_snap = utils::roundToInt(note_offset);
      poly_float result;
      for (int i = 0; i < poly_float::kSize; ++i)
        result.set(i, snap_buffer[int_snap[i]]);

      return midi + octave_snap + result;
    }

    force_inline poly_float globalTransposeSnap(poly_float midi, poly_float transpose, float* snap_buffer) {
      static constexpr float kScaleDown = 1.0f / kNotesPerOctave;
      static constexpr float kScaleUp = kNotesPerOctave;

      poly_float total = midi + transpose;
      poly_float note_offset = utils::mod(total * kScaleDown) * kScaleUp;
      poly_float octave_snap = total - note_offset;
      poly_int int_snap = utils::roundToInt(note_offset);
      poly_float result;
      for (int i = 0; i < poly_float::kSize; ++i)
        result.set(i, snap_buffer[int_snap[i]]);

      return octave_snap + result;
    }

    force_inline poly_float getInterpolationValues(poly_int indices) {
      return utils::toFloat(indices & kIntermediateMask) * (1.0f / kIntermediateMult);
    }

    force_inline poly_float linearlyInterpolateBuffer(const mono_float* buffer, const poly_int indices) {
      poly_int start_indices = utils::shiftRight<kIntermediateBits>(indices);
      poly_float t = getInterpolationValues(indices);
      matrix interpolation_matrix = utils::getLinearInterpolationMatrix(t);
      matrix value_matrix = utils::getValueMatrix(buffer, start_indices);
      value_matrix.transpose();
      return interpolation_matrix.multiplyAndSumRows(value_matrix);
    }

    force_inline poly_float interpolateBuffers(const mono_float* const* buffers, const poly_int indices) {
      poly_int start_indices = utils::shiftRight<kIntermediateBits>(indices);
      poly_float t = getInterpolationValues(indices);
      matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
      matrix value_matrix = utils::getValueMatrix(buffers, start_indices);
      value_matrix.transpose();
      return interpolation_matrix.multiplyAndSumRows(value_matrix);
    }

    force_inline poly_float interpolateBuffers(const mono_float* const* buffers,
                                               const mono_float* const*,
                                               const poly_int indices, poly_float) {
      return interpolateBuffers(buffers, indices);
    }

    force_inline poly_float interpolateMultipleBuffers(const mono_float* const* buffers_from,
                                                       const mono_float* const* buffers_to,
                                                       const poly_int indices, poly_float buffer_t) {
      poly_int start_indices = utils::shiftRight<kIntermediateBits>(indices);
      poly_float t = getInterpolationValues(indices);
      matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
      matrix value_matrix = utils::getValueMatrix(buffers_from, start_indices);
      value_matrix.interpolateRows(utils::getValueMatrix(buffers_to, start_indices), buffer_t);
      value_matrix.transpose();
      return interpolation_matrix.multiplyAndSumRows(value_matrix);
    }

    force_inline poly_float interpolateShepardBuffers(const mono_float* const* buffers_from,
                                                      const mono_float* const* buffers_to,
                                                      const poly_int indices, poly_float buffer_t,
                                                      poly_mask double_mask, poly_mask half_mask) {
      poly_int adjusted_indices = utils::maskLoad(indices, indices * 2, double_mask);
      adjusted_indices = utils::maskLoad(adjusted_indices, utils::shiftRight<1>(adjusted_indices), half_mask);
      poly_float from = interpolateBuffers(buffers_from, adjusted_indices);
      poly_float to = interpolateBuffers(buffers_to, indices);
      return utils::interpolate(from, to, buffer_t);
    }

    poly_int processDetunedShepard(SynthOscillator::VoiceBlock& voice_block, poly_float* audio_out) {
      const mono_float* const* from_buffers = voice_block.from_buffers;
      const mono_float* const* to_buffers = voice_block.to_buffers;

      int start = voice_block.start_sample;
      poly_float t_inc = 1.0f / voice_block.num_buffer_samples;
      poly_float t = utils::toFloat(voice_block.current_buffer_sample + 1) * t_inc;
      mono_float sample_inc = (1.0f / voice_block.total_samples);

      poly_int phase = voice_block.phase;
      poly_float current_phase_inc_mult = voice_block.from_phase_inc_mult;
      poly_float end_phase_inc_mult = voice_block.phase_inc_mult;
      poly_float delta_phase_inc_mult = (end_phase_inc_mult - current_phase_inc_mult) * sample_inc;
      current_phase_inc_mult += delta_phase_inc_mult * start;

      poly_mask double_mask = voice_block.shepard_double_mask;
      poly_mask half_mask = voice_block.shepard_half_mask;

      const poly_float* phase_inc_buffer = voice_block.phase_inc_buffer + start;
      const poly_int* phase_buffer = voice_block.phase_buffer + start;
      int num_samples = voice_block.end_sample - start;
      for (int i = 0; i < num_samples; ++i) {
        current_phase_inc_mult += delta_phase_inc_mult;
        phase += utils::toInt(phase_inc_buffer[i] * current_phase_inc_mult);
        poly_int adjusted_phase = phase + phase_buffer[i];
        audio_out[i] += interpolateShepardBuffers(from_buffers, to_buffers, adjusted_phase, t, double_mask, half_mask);
        t += t_inc;
      }

      return phase;
    }

    template<poly_int(*phaseDistort)(poly_int, poly_float, poly_int, const poly_float*, int),
             poly_float(*window)(poly_int, poly_int, poly_float, const poly_float*, int),
             poly_float(*interpolate)(const mono_float* const*, const mono_float* const*,
                                      const poly_int, poly_float)>
    poly_int processDetuned(SynthOscillator::VoiceBlock& voice_block, poly_float* audio_out) {
      const mono_float* const* from_buffers = voice_block.from_buffers;
      const mono_float* const* to_buffers = voice_block.to_buffers;

      int start = voice_block.start_sample;
      poly_float t_inc = 1.0f / voice_block.num_buffer_samples;
      poly_float t = utils::toFloat(voice_block.current_buffer_sample + 1) * t_inc;
      mono_float sample_inc = (1.0f / voice_block.total_samples);

      poly_int phase = voice_block.phase;
      poly_float current_phase_inc_mult = voice_block.from_phase_inc_mult;
      poly_float end_phase_inc_mult = voice_block.phase_inc_mult;
      poly_float delta_phase_inc_mult = (end_phase_inc_mult - current_phase_inc_mult) * sample_inc;
      current_phase_inc_mult += delta_phase_inc_mult * start;

      poly_int current_dist_phase = voice_block.last_distortion_phase;
      poly_int end_dist_phase = voice_block.distortion_phase;
      poly_int delta_dist_phase = utils::toInt(utils::toFloat(end_dist_phase - current_dist_phase) * sample_inc);
      current_dist_phase += delta_dist_phase * start;

      poly_float current_distortion = voice_block.last_distortion;
      poly_float distortion_inc = (voice_block.distortion - current_distortion) * sample_inc;
      current_distortion += distortion_inc * start;

      const poly_float* modulation_buffer = voice_block.modulation_buffer;
      if (modulation_buffer)
        modulation_buffer += start; // the buffer is null when no distortion needs it (null + offset is undefined)
      // SynthyBoy: second warp slot
      const int warp2 = voice_block.warp2_type;
      const int warp2_side = voice_block.warp2_side;
      poly_int current_dist_phase2 = voice_block.last_distortion_phase2;
      poly_int delta_dist_phase2 = utils::toInt(utils::toFloat(voice_block.distortion_phase2 - current_dist_phase2) * sample_inc);
      current_dist_phase2 += delta_dist_phase2 * start;
      poly_float current_distortion2 = voice_block.last_distortion2;
      poly_float distortion2_inc = (voice_block.distortion2 - current_distortion2) * sample_inc;
      current_distortion2 += distortion2_inc * start;
      const poly_float* modulation_buffer2 = voice_block.modulation_buffer2;
      if (modulation_buffer2)
        modulation_buffer2 += start;
      const poly_float* phase_inc_buffer = voice_block.phase_inc_buffer + start;
      const poly_int* phase_buffer = voice_block.phase_buffer + start;
      int num_samples = voice_block.end_sample - start;
      // SynthyBoy B21: the extended warp modes of the two slots
      SynthOscillator::WarpStage stage1 = voice_block.stage[0];
      SynthOscillator::WarpStage stage2 = voice_block.stage[1];
      if (stage1.mod)
        stage1.mod += start;
      if (stage2.mod)
        stage2.mod += start;
      SynthOscillator::WarpState state1 = voice_block.state[0];
      SynthOscillator::WarpState state2 = voice_block.state[1];
      for (int i = 0; i < num_samples; ++i) {
        current_phase_inc_mult += delta_phase_inc_mult;
        phase += utils::toInt(phase_inc_buffer[i] * current_phase_inc_mult);
        poly_int adjusted_phase = phase + phase_buffer[i];
        current_distortion += distortion_inc;
        current_dist_phase += delta_dist_phase;
        poly_int distorted_phase = phaseDistort(adjusted_phase, current_distortion,
                                                current_dist_phase, modulation_buffer, i);
        poly_int read_phase = distorted_phase + current_dist_phase;
        poly_float mult = window(adjusted_phase, distorted_phase, current_distortion, modulation_buffer, i);
        const poly_float inc_now = phase_inc_buffer[i] * current_phase_inc_mult;
        const auto read = [&](poly_int p) { return interpolate(from_buffers, to_buffers, p, t); };
        if (stage1.type != 0) {
          const poly_float inc1 = inc_now;
          read_phase = xWarpPhase(stage1, state1, adjusted_phase, current_distortion, inc1, i, mult, read) + current_dist_phase;
        }
        if (warp2 != SynthOscillator::kNone) {
          current_distortion2 += distortion2_inc;
          current_dist_phase2 += delta_dist_phase2;
          poly_int phase2 = warp2Phase(warp2, warp2_side, read_phase, current_distortion2, current_dist_phase2,
                                       modulation_buffer2, i);
          mult *= warp2Window(warp2, warp2_side, read_phase, phase2, current_distortion2, modulation_buffer2, i);
          read_phase = phase2 + current_dist_phase2;
        }
        else if (stage2.type != 0) {
          current_distortion2 += distortion2_inc;
          current_dist_phase2 += delta_dist_phase2;
          read_phase = xWarpPhase(stage2, state2, read_phase, current_distortion2, inc_now, i, mult, read) + current_dist_phase2;
        }
        poly_float result = interpolate(from_buffers, to_buffers, read_phase, t);
        if (stage1.type != 0)
          result = xWarpValue(stage1, state1, read_phase, current_distortion, inc_now, result, read);
        if (stage2.type != 0)
          result = xWarpValue(stage2, state2, read_phase, current_distortion2, inc_now, result, read);
        audio_out[i] += mult * result;
        t += t_inc;
      }
      voice_block.state[0] = state1;
      voice_block.state[1] = state2;

      return phase;
    }

    template<poly_int(*phaseDistort)(poly_int, poly_float, poly_int, const poly_float*, int),
             poly_float(*window)(poly_int, poly_int, poly_float, const poly_float*, int)>
    force_inline poly_int processDetuned(SynthOscillator::VoiceBlock& voice_block, poly_float* audio_out) {
      if (voice_block.isStatic())
        return processDetuned<phaseDistort, window, interpolateBuffers>(voice_block, audio_out);
      if (voice_block.shepard_double_mask.anyMask() || voice_block.shepard_half_mask.anyMask())
        return processDetunedShepard(voice_block, audio_out);
      return processDetuned<phaseDistort, window, interpolateMultipleBuffers>(voice_block, audio_out);
    }

    poly_int processCenterShepard(SynthOscillator::VoiceBlock& voice_block, poly_float* audio_out,
                                  poly_float current_center_amplitude, poly_float delta_center_amplitude,
                                  poly_float current_detuned_amplitude, poly_float delta_detuned_amplitude) {
      const mono_float* const* from_buffers = voice_block.from_buffers;
      const mono_float* const* to_buffers = voice_block.to_buffers;

      int start = voice_block.start_sample;

      poly_float t_inc = 1.0f / voice_block.num_buffer_samples;
      poly_float t = utils::toFloat(voice_block.current_buffer_sample + 1) * t_inc;
      mono_float sample_inc = (1.0f / voice_block.total_samples);

      poly_int phase = voice_block.phase;
      poly_float current_phase_inc_mult = voice_block.from_phase_inc_mult;
      poly_float end_phase_inc_mult = voice_block.phase_inc_mult;
      poly_float delta_phase_inc_mult = (end_phase_inc_mult - current_phase_inc_mult) * sample_inc;
      current_phase_inc_mult += delta_phase_inc_mult * start;

      poly_mask double_mask = voice_block.shepard_double_mask;
      poly_mask half_mask = voice_block.shepard_half_mask;

      const poly_float* phase_inc_buffer = voice_block.phase_inc_buffer + start;
      const poly_int* phase_buffer = voice_block.phase_buffer + start;
      int num_samples = voice_block.end_sample - start;
      for (int i = 0; i < num_samples; ++i) {
        current_phase_inc_mult += delta_phase_inc_mult;
        phase += utils::toInt(phase_inc_buffer[i] * current_phase_inc_mult);
        poly_int adjusted_phase = phase + phase_buffer[i];

        current_center_amplitude += delta_center_amplitude;
        current_detuned_amplitude += delta_detuned_amplitude;
        poly_float read = interpolateShepardBuffers(from_buffers, to_buffers, adjusted_phase,
                                                    t, double_mask, half_mask);
        audio_out[i] = current_center_amplitude * read + current_detuned_amplitude * audio_out[i];
        t += t_inc;
      }

      return phase;
    }

    template<poly_int(*phaseDistort)(poly_int, poly_float, poly_int, const poly_float*, int),
             poly_float(*window)(poly_int, poly_int, poly_float, const poly_float*, int),
             poly_float(*interpolate)(const mono_float* const*, const mono_float* const*,
                                      const poly_int, poly_float)>
    poly_int processCenter(SynthOscillator::VoiceBlock& voice_block, poly_float* audio_out,
                           poly_float current_center_amplitude, poly_float delta_center_amplitude,
                           poly_float current_detuned_amplitude, poly_float delta_detuned_amplitude) {
      const mono_float* const* from_buffers = voice_block.from_buffers;
      const mono_float* const* to_buffers = voice_block.to_buffers;

      int start = voice_block.start_sample;

      poly_float t_inc = 1.0f / voice_block.num_buffer_samples;
      poly_float t = utils::toFloat(voice_block.current_buffer_sample + 1) * t_inc;
      mono_float sample_inc = (1.0f / voice_block.total_samples);

      poly_int phase = voice_block.phase;
      poly_float current_phase_inc_mult = voice_block.from_phase_inc_mult;
      poly_float end_phase_inc_mult = voice_block.phase_inc_mult;
      poly_float delta_phase_inc_mult = (end_phase_inc_mult - current_phase_inc_mult) * sample_inc;
      current_phase_inc_mult += delta_phase_inc_mult * start;

      poly_int current_dist_phase = voice_block.last_distortion_phase;
      poly_int end_dist_phase = voice_block.distortion_phase;
      poly_int delta_dist_phase = utils::toInt(utils::toFloat(end_dist_phase - current_dist_phase) * sample_inc);
      current_dist_phase += delta_dist_phase * start;

      poly_float current_distortion = voice_block.last_distortion;
      poly_float distortion_inc = (voice_block.distortion - current_distortion) * sample_inc;
      current_distortion += distortion_inc * start;

      const poly_float* modulation_buffer = voice_block.modulation_buffer;
      if (modulation_buffer)
        modulation_buffer += start; // the buffer is null when no distortion needs it (null + offset is undefined)
      // SynthyBoy: second warp slot
      const int warp2 = voice_block.warp2_type;
      const int warp2_side = voice_block.warp2_side;
      poly_int current_dist_phase2 = voice_block.last_distortion_phase2;
      poly_int delta_dist_phase2 = utils::toInt(utils::toFloat(voice_block.distortion_phase2 - current_dist_phase2) * sample_inc);
      current_dist_phase2 += delta_dist_phase2 * start;
      poly_float current_distortion2 = voice_block.last_distortion2;
      poly_float distortion2_inc = (voice_block.distortion2 - current_distortion2) * sample_inc;
      current_distortion2 += distortion2_inc * start;
      const poly_float* modulation_buffer2 = voice_block.modulation_buffer2;
      if (modulation_buffer2)
        modulation_buffer2 += start;
      const poly_float* phase_inc_buffer = voice_block.phase_inc_buffer + start;
      const poly_int* phase_buffer = voice_block.phase_buffer + start;
      int num_samples = voice_block.end_sample - start;
      // SynthyBoy B21: the extended warp modes of the two slots
      SynthOscillator::WarpStage stage1 = voice_block.stage[0];
      SynthOscillator::WarpStage stage2 = voice_block.stage[1];
      if (stage1.mod)
        stage1.mod += start;
      if (stage2.mod)
        stage2.mod += start;
      SynthOscillator::WarpState state1 = voice_block.state[0];
      SynthOscillator::WarpState state2 = voice_block.state[1];
      for (int i = 0; i < num_samples; ++i) {
        current_phase_inc_mult += delta_phase_inc_mult;
        phase += utils::toInt(phase_inc_buffer[i] * current_phase_inc_mult);
        poly_int adjusted_phase = phase + phase_buffer[i];
        current_distortion += distortion_inc;
        current_dist_phase += delta_dist_phase;
        current_center_amplitude += delta_center_amplitude;
        current_detuned_amplitude += delta_detuned_amplitude;
        poly_int distorted_phase = phaseDistort(adjusted_phase, current_distortion,
                                                current_dist_phase, modulation_buffer, i);
        poly_float mult = window(adjusted_phase, distorted_phase, current_distortion, modulation_buffer, i);
        poly_int read_phase = distorted_phase + current_dist_phase;
        const poly_float inc_now = phase_inc_buffer[i] * current_phase_inc_mult;
        const auto read_at = [&](poly_int p) { return interpolate(from_buffers, to_buffers, p, t); };
        if (stage1.type != 0)
          read_phase = xWarpPhase(stage1, state1, adjusted_phase, current_distortion, inc_now, i, mult, read_at) + current_dist_phase;
        if (warp2 != SynthOscillator::kNone) {
          current_distortion2 += distortion2_inc;
          current_dist_phase2 += delta_dist_phase2;
          poly_int phase2 = warp2Phase(warp2, warp2_side, read_phase, current_distortion2, current_dist_phase2,
                                       modulation_buffer2, i);
          mult *= warp2Window(warp2, warp2_side, read_phase, phase2, current_distortion2, modulation_buffer2, i);
          read_phase = phase2 + current_dist_phase2;
        }
        else if (stage2.type != 0) {
          current_distortion2 += distortion2_inc;
          current_dist_phase2 += delta_dist_phase2;
          read_phase = xWarpPhase(stage2, state2, read_phase, current_distortion2, inc_now, i, mult, read_at) + current_dist_phase2;
        }
        poly_float value = interpolate(from_buffers, to_buffers, read_phase, t);
        if (stage1.type != 0)
          value = xWarpValue(stage1, state1, read_phase, current_distortion, inc_now, value, read_at);
        if (stage2.type != 0)
          value = xWarpValue(stage2, state2, read_phase, current_distortion2, inc_now, value, read_at);
        poly_float read = mult * value;
        poly_float center_value = current_center_amplitude * read;
        audio_out[i] = center_value + current_detuned_amplitude * audio_out[i];
        VITAL_ASSERT(utils::isFinite(audio_out[i]));

        t += t_inc;
      }
      voice_block.state[0] = state1;
      voice_block.state[1] = state2;

      return phase;
    }

    template<poly_int(*phaseDistort)(poly_int, poly_float, poly_int, const poly_float*, int),
             poly_float(*window)(poly_int, poly_int, poly_float, const poly_float*, int)>
    force_inline poly_int processCenter(SynthOscillator::VoiceBlock& voice_block, poly_float* audio_out,
                                        poly_float current_center_amplitude, poly_float delta_center_amplitude,
                                        poly_float current_detuned_amplitude, poly_float delta_detuned_amplitude) {
      if (voice_block.isStatic()) {
        return processCenter<phaseDistort, window, interpolateBuffers>(
            voice_block, audio_out,
            current_center_amplitude, delta_center_amplitude,
            current_detuned_amplitude, delta_detuned_amplitude);
      }
      if (voice_block.shepard_double_mask.anyMask() || voice_block.shepard_half_mask.anyMask()) {
        return processCenterShepard(
            voice_block, audio_out,
            current_center_amplitude, delta_center_amplitude,
            current_detuned_amplitude, delta_detuned_amplitude);
      }
      return processCenter<phaseDistort, window, interpolateMultipleBuffers>(
          voice_block, audio_out,
          current_center_amplitude, delta_center_amplitude,
          current_detuned_amplitude, delta_detuned_amplitude);
    }

    template<class T>
    force_inline T compactAndLoadVoice(T* values, poly_mask active_mask) {
      T one = values[0];
      T two = utils::swapVoices(values[1]);
      return utils::maskLoad(two, one, active_mask);
    }

    template<class T>
    force_inline void expandAndWriteVoice(T* values, T value, poly_mask active_mask) {
      T two = utils::swapVoices(value);
      values[0] = utils::maskLoad(values[0], value, active_mask);
      values[1] = utils::maskLoad(values[1], two, active_mask);
    }

    force_inline void compactAndLoadVoice(const mono_float** dest, const mono_float* const* values,
                                          poly_mask active_mask) {
      const mono_float* const* position1 = values;
      const mono_float* const* position2 = values + poly_float::kSize;
      int index = active_mask[0] ? 0 : 2;
      dest[index] = position1[index];
      dest[index + 1] = position1[index + 1];
      dest[(index + 2) % poly_float::kSize] = position2[index];
      dest[(index + 3) % poly_float::kSize] = position2[index + 1];
    }

    void setPowerDistortionValues(poly_float* values, int num_values, float exponent, bool spread) {
      if (spread) {
        for (int i = 0; i < num_values; ++i)
          values[i] = futils::pow(2.0f, (values[i] - 0.5f) * 2.0f * exponent);
      }
      else {
        poly_float value = futils::pow(2.0f, (values[0] - 0.5f) * 2.0f * exponent);
        for (int i = 0; i < num_values; ++i)
          values[i] = value;
      }
    }
  }

  const mono_float SynthOscillator::kStackMultipliers[kNumUnisonStackTypes][kNumPolyPhase] = {
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 1.0f, 2.0f, 1.0f, 2.0f, 1.0f, 2.0f, 1.0f, 2.0f },
    { 1.0f, 2.0f, 3.0f, 1.0f, 2.0f, 3.0f, 1.0f, 2.0f },
    { 1.0f, kFifthMult, 2.0f, 1.0f, kFifthMult, 2.0f, 1.0f, kFifthMult },
    { 1.0f, kFifthMult, 2.0f, 2.0f * kFifthMult, 4.0f, 1.0f, kFifthMult, 2.0f },
    { 1.0f, kMajorThirdMult, kFifthMult, 2.0f, 1.0f, kMajorThirdMult, kFifthMult, 2.0f },
    { 1.0f, kMinorThirdMult, kFifthMult, 2.0f, 1.0f, kMinorThirdMult, kFifthMult, 2.0f },
    { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f },
    { 1.0f, 3.0f, 5.0f, 7.0f, 9.0f, 11.0f, 13.0f, 15.0f }
  };

  SynthOscillator::VoiceBlock::VoiceBlock() : start_sample(0), end_sample(0), total_samples(0),
                                              phase(0), phase_inc_mult(0.0f), from_phase_inc_mult(0.0f),
                                              shepard_double_mask(0), shepard_half_mask(0),
                                              distortion_phase(0), last_distortion_phase(0),
                                              distortion(0.0f), last_distortion(0.0f),
                                              warp2_type(0), warp2_side(0), distortion_phase2(0), last_distortion_phase2(0),
                                              distortion2(0.0f), last_distortion2(0.0f), modulation_buffer2(nullptr),
                                              num_buffer_samples(0), current_buffer_sample(0),
                                              smoothing_enabled(false), spectral_morph(kNoSpectralMorph),
                                              modulation_buffer(nullptr) {
    const mono_float* default_buffer = Wavetable::null_waveform();
    for (int i = 0; i < poly_int::kSize; ++i) {
      from_buffers[i] = default_buffer;
      to_buffers[i] = default_buffer;
    }
  }

  SynthOscillator::SynthOscillator(Wavetable* wavetable) :
      Processor(kNumInputs, kNumOutputs), random_generator_(-1.0f, 1.0f),
      transpose_quantize_(0), last_quantized_transpose_(0.0f), last_quantize_ratio_(1.0f),
      unison_(1), active_oscillators_(2), wavetable_(wavetable), wavetable_version_(wavetable->getVersion()),
      first_mod_oscillator_(nullptr), second_mod_oscillator_(nullptr), sample_(nullptr),
      fourier_frames1_(), fourier_frames2_() {
    pan_amplitude_ = 0.0f;
    center_amplitude_ = 0.0f;
    detuned_amplitude_ = 0.0f;
    distortion_phase_ = 0.0f;
    blend_stereo_multiply_ = 0.0f;
    blend_center_multiply_ = 0.0f;

    for (int i = 0; i < kNumPolyPhase; ++i) {
      phases_[i] = 0;
      phase_inc_mults_[i] = 1.0f;
      from_phase_inc_mults_[i] = 1.0f;
      shepard_double_masks_[i] = 0;
      shepard_half_masks_[i] = 0;
      waiting_shepard_double_masks_[i] = 0;
      waiting_shepard_half_masks_[i] = 0;
      detunings_[i] = 1.0f;
      spectral_morph_values_[i] = 0.0f;
      last_spectral_morph_values_[i] = 1.0f;
      distortion_values_[i] = 0.0f;
      last_distortion_values_[i] = 0.0f;
      distortion2_values_[i] = 0.0f;
      last_distortion2_values_[i] = 0.0f;
    }
    distortion_phase2_ = 0.0f;

    resetWavetableBuffers();

    fourier_transform_ = std::make_shared<FourierTransform>(kWaveformBits);
    phase_inc_buffer_ = std::make_shared<Output>();
    phase_buffer_ = std::make_shared<PhaseBuffer>();
    legato_state_ = std::make_shared<PhaseLegatoState>();
    voice_block_.phase_inc_buffer = phase_inc_buffer_->buffer;
    voice_block_.phase_buffer = phase_buffer_->buffer;
    RandomValues::instance();
  }

  void SynthOscillator::reset(poly_mask reset_mask, poly_int sample) {
    reset(reset_mask);
    voice_block_.current_buffer_sample = utils::maskLoad(voice_block_.current_buffer_sample, -sample, reset_mask);
  }

  void SynthOscillator::reset(poly_mask reset_mask) {
    poly_float random_amount = input(kRandomPhase)->at(0);
    last_quantize_ratio_ = utils::maskLoad(last_quantize_ratio_, 1.0f, reset_mask);

    bool phase_legato_active = false;
    if (input(kPhaseLegato) && poly_float::notEqual(input(kPhaseLegato)->at(0), 0.0f).anyMask())
      phase_legato_active = true;
    if (input(kVoicePhaseLegato) && poly_float::notEqual(input(kVoicePhaseLegato)->at(0), 0.0f).anyMask())
      phase_legato_active = true;

    bool is_legato = false;
    if (input(kNoteCount) && poly_float::greaterThan(input(kNoteCount)->at(0), 1.0f).anyMask())
      is_legato = true;

    int mem_mode = input(kPhaseMemory) ? static_cast<int>(std::round(input(kPhaseMemory)->at(0)[0])) : 0;

    for (int v = 0; v < kNumVoicesPerProcess; ++v) {
      if (reset_mask[v * 2]) {
        const bool follow = phase_legato_active && is_legato && legato_state_->valid;
        if (follow && mem_mode == 2) {
          // Per Voice: the voice resumes its own free running phases (no reset)
        }
        else {
          for (int i = 0; i < kNumPolyPhase; ++i) {
            // float -> uint32_t of a negative value is undefined: convert through int32_t (clamped), then wrap
            float phase_left = random_generator_.next() * random_amount[2 * v] * INT_MAX;
            float phase_right = random_generator_.next() * random_amount[2 * v + 1] * INT_MAX;
            uint32_t random_phase_left = static_cast<uint32_t>(static_cast<int32_t>(std::max(-2147483520.0f, std::min(2147483520.0f, phase_left))));
            uint32_t random_phase_right = static_cast<uint32_t>(static_cast<int32_t>(std::max(-2147483520.0f, std::min(2147483520.0f, phase_right))));
            phases_[i].set(2 * v, random_phase_left);
            phases_[i].set(2 * v + 1, random_phase_right);

            int buffer_index = i * poly_float::kSize + 2 * v;
            last_buffers_[buffer_index] = wave_buffers_[buffer_index];
            last_buffers_[buffer_index + 1] = wave_buffers_[buffer_index + 1];
          }

          if (unison_ < active_oscillators_)
            phases_[0].set(v * 2, phases_[0][v * 2 + 1]);

          if (follow) {
            // All Voices (0): every unison phase follows the voice that rendered last; Contiguous (1): the centre phase only
            const int count = mem_mode == 0 ? kNumPolyPhase : 1;
            for (int i = 0; i < count; ++i) {
              phases_[i].set(2 * v, legato_state_->left[i]);
              phases_[i].set(2 * v + 1, legato_state_->right[i]);
            }
          }
        }
      }
    }

    for (int i = 0; i < kNumPolyPhase; ++i) {
      last_distortion_values_[i] = utils::maskLoad(last_distortion_values_[i], distortion_values_[i], reset_mask);
      // SynthyBoy: the second warp slot starts a note at its amount too (it used to ramp up from zero over the first block)
      last_distortion2_values_[i] = utils::maskLoad(last_distortion2_values_[i], distortion2_values_[i], reset_mask);
      last_spectral_morph_values_[i] = utils::maskLoad(last_spectral_morph_values_[i],
                                                       spectral_morph_values_[i], reset_mask);
      from_phase_inc_mults_[i] = utils::maskLoad(from_phase_inc_mults_[i], phase_inc_mults_[i], reset_mask);
      for (int w = 0; w < 2; ++w) {   // SynthyBoy B21: a new note starts the warp states from zero
        warp_state_[w][i].acc = utils::maskLoad(warp_state_[w][i].acc, poly_int(0), reset_mask);
        warp_state_[w][i].lp1 = utils::maskLoad(warp_state_[w][i].lp1, poly_float(0.0f), reset_mask);
        warp_state_[w][i].lp2 = utils::maskLoad(warp_state_[w][i].lp2, poly_float(0.0f), reset_mask);
      }
      shepard_double_masks_[i] = shepard_double_masks_[i] & ~reset_mask;
      shepard_half_masks_[i] = shepard_half_masks_[i] & ~reset_mask;
      waiting_shepard_double_masks_[i] = waiting_shepard_double_masks_[i] & ~reset_mask;
      waiting_shepard_half_masks_[i] = waiting_shepard_half_masks_[i] & ~reset_mask;
    }
  }

  void SynthOscillator::setPhaseIncMults() {
    poly_float range = input(kDetuneRange)->at(0);
    poly_float cents = range * input(kUnisonDetune)->at(0);
    poly_float power = input(kDetunePower)->at(0);
    int stack_style = std::roundf(input(kStackStyle)->at(0)[0]);
    const mono_float* stack_settings = kStackMultipliers[stack_style];

    int unison_mode = input(kUnisonMode) ? static_cast<int>(std::round(input(kUnisonMode)->at(0)[0])) : 0;
    int stack_repeat = input(kUnisonStackRepeat) ? static_cast<int>(std::round(input(kUnisonStackRepeat)->at(0)[0])) : 0;

    mono_float divisor = utils::max(1.0f, unison_ - 1.0f);
    int bump = (unison_ % 2 == 0) ? 1 : 0;

    poly_mask sharp_flat_mask = constants::kLeftMask;
    int num_updates = active_oscillators_ / 2;
    for (int i = 0; i < num_updates; ++i) {
      mono_float t = (2 * i + bump) / divisor;

      // Detune law (A84): Linear (0), Super (1), Exp (2), Inv (3), Random (4)
      if (unison_mode == 1) { // Super: cubic distribution clustered near center
        t = t * t * t;
      }
      else if (unison_mode == 2) { // Exp: exponential distribution
        t = (std::exp(2.0f * t) - 1.0f) / (std::exp(2.0f) - 1.0f);
      }
      else if (unison_mode == 3) { // Inv: inverse/concave distribution
        float inv_t = 1.0f - t;
        t = 1.0f - inv_t * inv_t * inv_t;
      }
      else if (unison_mode == 4) { // Random: deterministic per-voice pseudo-random detune
        uint32_t h = static_cast<uint32_t>(i * 1664525u + 1013904223u);
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        t = static_cast<float>(h & 0xFFFF) / 65535.0f;
      }

      poly_float adjusted_t = futils::powerScale(t, power);
      poly_float oscillator_cents = adjusted_t * cents;

      poly_float up_ratio = utils::centsToRatio(oscillator_cents);
      poly_float down_ratio = poly_float(1.0f) / up_ratio;

      mono_float stack_mult = stack_settings[i];
      // Serum 2 manual p.47: STACK "12 (1-3x)" distributes the voices between the original pitch and octave transpositions up to 1-3 octaves
      // higher, "12+7 (1-3x)" between the original pitch, the fifth and the octaves. Here: Octaves (style 3) and Power Chord (style 5), the
      // repeat 1x / 2x / 3x is the number of octaves. 1x is the table above; the other stack styles ignore the repeat.
      if (stack_repeat > 0 && (stack_style == 3 || stack_style == 5)) {
        const int octaves = stack_repeat + 1;
        if (stack_style == 3) {
          stack_mult = std::ldexp(1.0f, i % (octaves + 1));
        }
        else {
          const int step = i % (2 * octaves + 1);
          stack_mult = std::ldexp((step & 1) ? kFifthMult : 1.0f, step / 2);
        }
      }

      detunings_[i] = utils::maskLoad(up_ratio, down_ratio, sharp_flat_mask) * stack_mult;
      from_phase_inc_mults_[i] = phase_inc_mults_[i];
      phase_inc_mults_[i] = detunings_[i];

      sharp_flat_mask = ~sharp_flat_mask;
    }
  }

  force_inline void SynthOscillator::setupShepardWrap() {
    int num_phase_updates = active_oscillators_ / 2;

    poly_float ratio_div = poly_float(1.0f) / last_quantize_ratio_;
    for (int i = 0; i < num_phase_updates; ++i) {
      poly_float spectral_diff = last_spectral_morph_values_[i] - spectral_morph_values_[i];
      poly_float mult = futils::exp2(-spectral_morph_values_[i]);
      phase_inc_mults_[i] *= mult;
      detunings_[i] *= mult;

      poly_mask double_mask = waiting_shepard_double_masks_[i] | poly_float::lessThan(spectral_diff, -0.6f);
      poly_mask half_mask = waiting_shepard_half_masks_[i] | poly_float::greaterThan(spectral_diff, 0.6f);

      phase_inc_mults_[i] = utils::maskLoad(phase_inc_mults_[i], phase_inc_mults_[i] * 2.0f, double_mask);
      phase_inc_mults_[i] = utils::maskLoad(phase_inc_mults_[i], phase_inc_mults_[i] * 0.5f, half_mask);
      poly_float reset_phase_inc_mult = from_phase_inc_mults_[i] * ratio_div;
      from_phase_inc_mults_[i] = utils::maskLoad(from_phase_inc_mults_[i], reset_phase_inc_mult,
                                                 half_mask | double_mask);

      waiting_shepard_double_masks_[i] = double_mask;
      waiting_shepard_half_masks_[i] = half_mask;
    }
  }

  force_inline void SynthOscillator::clearShepardWrap() {
    int num_phase_updates = active_oscillators_ / 2;

    for (int i = 0; i < num_phase_updates; ++i) {
      shepard_double_masks_[i] = 0;
      shepard_half_masks_[i] = 0;
      waiting_shepard_double_masks_[i] = 0;
      waiting_shepard_half_masks_[i] = 0;
    }
  }

  force_inline void SynthOscillator::doShepardWrap(poly_mask new_buffer_mask, int quantize) {
    int num_phase_updates = active_oscillators_ / 2;

    if (quantize) {
      for (int i = 0; i < num_phase_updates; ++i) {
        poly_mask double_mask = waiting_shepard_double_masks_[i] & new_buffer_mask;
        poly_mask half_mask = waiting_shepard_half_masks_[i] & new_buffer_mask;
        waiting_shepard_double_masks_[i] = waiting_shepard_double_masks_[i] & ~new_buffer_mask;
        waiting_shepard_half_masks_[i] = waiting_shepard_half_masks_[i] & ~new_buffer_mask;

        phase_inc_mults_[i] = utils::maskLoad(phase_inc_mults_[i], phase_inc_mults_[i] * 0.5f, double_mask);
        phases_[i] = utils::maskLoad(phases_[i], utils::shiftRight<1>(phases_[i]), double_mask);

        phase_inc_mults_[i] = utils::maskLoad(phase_inc_mults_[i], phase_inc_mults_[i] * 2.0f, half_mask);
        phases_[i] = utils::maskLoad(phases_[i], phases_[i] * 2, half_mask);
        from_phase_inc_mults_[i] = utils::maskLoad(from_phase_inc_mults_[i], phase_inc_mults_[i],
                                                   double_mask | half_mask);

        shepard_double_masks_[i] = utils::maskLoad(shepard_double_masks_[i], double_mask, new_buffer_mask);
        shepard_half_masks_[i] = utils::maskLoad(shepard_half_masks_[i], half_mask, new_buffer_mask);
      }
    }
    else {
      for (int i = 0; i < num_phase_updates; ++i) {
        poly_mask double_mask = waiting_shepard_double_masks_[i] & new_buffer_mask;
        poly_mask half_mask = waiting_shepard_half_masks_[i] & new_buffer_mask;
        waiting_shepard_double_masks_[i] = waiting_shepard_double_masks_[i] & ~new_buffer_mask;
        waiting_shepard_half_masks_[i] = waiting_shepard_half_masks_[i] & ~new_buffer_mask;

        phase_inc_mults_[i] = utils::maskLoad(phase_inc_mults_[i], phase_inc_mults_[i] * 0.5f, double_mask);
        from_phase_inc_mults_[i] = utils::maskLoad(from_phase_inc_mults_[i],
                                                   from_phase_inc_mults_[i] * 0.5f, double_mask);
        phases_[i] = utils::maskLoad(phases_[i], utils::shiftRight<1>(phases_[i]), double_mask);

        phase_inc_mults_[i] = utils::maskLoad(phase_inc_mults_[i], phase_inc_mults_[i] * 2.0f, half_mask);
        from_phase_inc_mults_[i] = utils::maskLoad(from_phase_inc_mults_[i], from_phase_inc_mults_[i] * 2, half_mask);
        phases_[i] = utils::maskLoad(phases_[i], phases_[i] * 2.0f, half_mask);

        shepard_double_masks_[i] = utils::maskLoad(shepard_double_masks_[i], double_mask, new_buffer_mask);
        shepard_half_masks_[i] = utils::maskLoad(shepard_half_masks_[i], half_mask, new_buffer_mask);
      }
    }
  }

  force_inline void SynthOscillator::setAmplitude() {
    if (unison_ <= 2) {
      center_amplitude_ = 1.0f;
      detuned_amplitude_ = 0.0f;
      return;
    }

    poly_float blend = input(kBlend)->at(0);
    poly_float center = utils::interpolate(1.0f, kCenterLowAmplitude, blend);
    poly_float detuned_blend = -blend + 1.0f;
    detuned_blend *= detuned_blend;
    poly_float detuned = utils::interpolate(kDetunedHighAmplitude, 0.0f, detuned_blend);

    // Unison Blend Shape (A85): -1.0 .. 1.0 (from -100% .. 100%)
    if (input(kUnisonBlendShape)) {
      poly_float shape = input(kUnisonBlendShape)->at(0);
      center = center * (poly_float(1.0f) - shape * 0.5f);
      detuned = detuned * (poly_float(1.0f) + shape * 0.5f);
    }

    int half_oscillators = active_oscillators_ / 2;
    poly_float square_sums = center * center + detuned * detuned * (half_oscillators - 1);
    poly_float adjustment = poly_float(1.0f) / utils::sqrt(square_sums);
    center_amplitude_ = adjustment * center;
    detuned_amplitude_ = adjustment * detuned;
  }

  void SynthOscillator::setWaveBuffers(poly_float phase_inc, int index) {
    if (voice_block_.spectral_morph == kShepardTone)
      setFourierWaveBuffers<shepardMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kVocode)
      setFourierWaveBuffers<evenOddVocodeMorph>(phase_inc, index, true);
    else if (voice_block_.spectral_morph == kFormScale)
      setFourierWaveBuffers<evenOddVocodeMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kHarmonicScale)
      setFourierWaveBuffers<harmonicScaleMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kInharmonicScale)
      setFourierWaveBuffers<inharmonicScaleMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kSmear)
      setFourierWaveBuffers<smearMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kRandomAmplitudes)
      setFourierWaveBuffers<randomAmplitudeMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kLowPass)
      setFourierWaveBuffers<lowPassMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kHighPass)
      setFourierWaveBuffers<highPassMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kPhaseDisperse)
      setFourierWaveBuffers<phaseMorph>(phase_inc, index, false);
    else if (voice_block_.spectral_morph == kSkew)
      setFourierWaveBuffers<wavetableSkewMorph>(phase_inc, index, false);
    else
      setFourierWaveBuffers<passthroughMorph>(phase_inc, index, false);

    voice_block_.current_buffer_sample.set(index, 0);
    voice_block_.current_buffer_sample.set(index + 1, 0);
  }

  template<void(*spectralMorph)(const Wavetable::WavetableData*, int, poly_float*,
                                FourierTransform*, float, int, const poly_float*)>
  void SynthOscillator::computeSpectralWaveBufferPair(int phase_update, int index, bool formant_shift,
                                                      float phase_adjustment, poly_int wave_index,
                                                      poly_float voice_increment, poly_float morph_amount) {
    for (int i = index; i < index + 2; ++i) {
      mono_float adjust_phase_inc = voice_increment[i] * phase_adjustment;
      mono_float formant_adjustment = voice_increment[i] * Wavetable::kWaveformSize;
      float bin = Wavetable::getFrequencyFloatBin(adjust_phase_inc);
      int buffer_index = phase_update * poly_float::kSize + i;
      last_buffers_[buffer_index] = wave_buffers_[buffer_index];

      poly_float* fourier_buffer = fourier_frames1_[buffer_index];
      mono_float* destination = ((mono_float*)fourier_buffer) + poly_float::kSize - 1;
      if (destination == wave_buffers_[buffer_index])
        fourier_buffer = fourier_frames2_[buffer_index];

      float shift = morph_amount[i];
      if (formant_shift)
        shift *= formant_adjustment;
      const Wavetable::WavetableData* wavetable_data = wavetable_->getAllActiveData();
      int table_index = std::min<int>(wave_index[i], wavetable_data->num_frames - 1);

      float bin_shift = Wavetable::kFrequencyBins + 1.0f - bin;
      int last_harmonic = std::max<int>(0, WaveFrame::kWaveformSize * futils::exp2(-bin_shift));
      last_harmonic = std::min(last_harmonic, WaveFrame::kWaveformSize / 2);

      spectralMorph(wavetable_data, table_index, fourier_buffer,
                    fourier_transform_.get(), shift, last_harmonic, RandomValues::instance()->buffer());
      wave_buffers_[buffer_index] = ((mono_float*)fourier_buffer) + poly_float::kSize - 1;

      if (i == index && morph_amount[i] == morph_amount[i + 1] && wave_index[i] == wave_index[i + 1]) {
        last_buffers_[buffer_index + 1] = wave_buffers_[buffer_index + 1];
        wave_buffers_[buffer_index + 1] = wave_buffers_[buffer_index];
        return;
      }
    }
  }
  
  // SynthyBoy (docs/ui-design/13-new-keys.md 4.14): the drawn remap curve of the oscillator slot applied to the frame position of the two lanes
  // of one voice (index, index + 1), then RISE / FALL: a one pole glide of the remapped position (the position is read once per wave buffer,
  // every kWavetableFadeTime seconds), jumping to the target at a note on. Off: the position is returned untouched.
  poly_float SynthOscillator::remapFrame(poly_float frame, int index) {
    const FrameRemap* remap = wavetable_->frameRemap();
    if (!remap->on) {
      for (bool& lane_on : remap_lane_on_)
        lane_on = false;
      return frame;
    }

    const float inverse_max = remap->max_frame > 0.0f ? 1.0f / remap->max_frame : 0.0f;
    for (int lane = index; lane < index + 2; ++lane) {
      float target = remap->map(frame[lane] * inverse_max) * remap->max_frame;
      float current = remap_slew_[lane];
      float time_ms = target > current ? remap->rise_ms : remap->fall_ms;
      if (!remap_lane_on_[lane] || remap_snap_[lane] || time_ms <= 0.0f)
        current = target;
      else
        current += (target - current) * (1.0f - std::exp(-1000.0f * kWavetableFadeTime / time_ms));
      remap_slew_.set(lane, current);
      remap_lane_on_[lane] = true;
      frame.set(lane, current);
    }
    return frame;
  }

  template<void(*spectralMorph)(const Wavetable::WavetableData*, int, poly_float*,
                                FourierTransform*, float, int, const poly_float*)>
  void SynthOscillator::setFourierWaveBuffers(poly_float phase_inc, int index, bool formant_shift) {
    poly_float wave_frame = remapFrame(input(kWaveFrame)->at(0), index);
    poly_float frame_spread = input(kUnisonFrameSpread)->at(0);
    phase_inc = utils::max(0.0f, phase_inc);
    float phase_inc_adjustment = getPhaseIncAdjustment();
    
    DistortionType distortion_type = static_cast<DistortionType>((int)input(kDistortionType)->at(0)[0]);
    poly_mask distortion_frequency_mask = 0;
    mono_float distortion_mult = 1.0f;
    if (distortion_type == kFormant || distortion_type == kSync || distortion_type == kSyncVar) {
      distortion_frequency_mask = constants::kFullMask;
      distortion_mult = kMaxSync;
    }

    SpectralMorph spectral_morph = static_cast<SpectralMorph>((int)input(kSpectralMorphType)->at(0)[0]);
    poly_mask spectral_unison_mask = poly_float::notEqual(input(kSpectralUnison)->at(0), 0.0f);
    poly_mask spectral_morph_mask = poly_float::notEqual(spectral_morph_values_[0], spectral_morph_values_[1]);
    poly_mask frame_spread_mask = poly_float::notEqual(frame_spread, 0.0f);
    bool spectral_unison = spectral_unison_mask.anyMask() &&
                           (spectral_morph_mask.anyMask() || frame_spread_mask.anyMask() || spectral_morph == kVocode);

    int num_phase_updates = active_oscillators_ / 2;
    if (spectral_unison) {
      float t_inc = 1.0f / (utils::imax(2, num_phase_updates) - 1.0f);
      for (int v = 0; v < num_phase_updates; ++v) {
        poly_float frequency_mult = distortion_values_[v] * distortion_mult;
        frequency_mult = utils::maskLoad(1.0f, frequency_mult, distortion_frequency_mask);

        poly_float morph_amount = spectral_morph_values_[v];
        poly_float voice_increment = phase_inc * detunings_[v] * frequency_mult;
        poly_float t = v * t_inc;
        poly_float frame = wave_frame + t * frame_spread;
        poly_int wave_index = utils::toInt(utils::clamp(frame, 0.0f, kNumOscillatorWaveFrames - 1));

        computeSpectralWaveBufferPair<spectralMorph>(v, index, formant_shift, phase_inc_adjustment,
                                                     wave_index, voice_increment, morph_amount);
      }
    }
    else {
      poly_float frequency_mult = distortion_values_[0] * distortion_mult;
      frequency_mult = utils::maskLoad(1.0f, frequency_mult, distortion_frequency_mask);

      poly_float morph_amount = spectral_morph_values_[0];
      poly_float voice_increment = phase_inc * detunings_[0] * frequency_mult;
      poly_int wave_index = utils::toInt(utils::clamp(wave_frame, 0.0f, kNumOscillatorWaveFrames - 1));

      computeSpectralWaveBufferPair<spectralMorph>(0, index, formant_shift, phase_inc_adjustment,
                                                   wave_index, voice_increment, morph_amount);

      for (int v = 1; v < num_phase_updates; ++v) {
        for (int i = index; i < index + 2; ++i) {
          int buffer_index = v * poly_float::kSize + i;
          last_buffers_[buffer_index] = wave_buffers_[buffer_index];
          wave_buffers_[buffer_index] = wave_buffers_[i];
        }
      }
    }
  }

  void SynthOscillator::stereoBlend(poly_float* audio_out, int num_samples, poly_mask reset_mask) {
    poly_float stereo_spread = utils::clamp(input(kStereoSpread)->at(0), 0.0f, 1.0f);

    poly_float current_stereo_mult = blend_stereo_multiply_;
    poly_float current_center_mult = blend_center_multiply_;
    blend_stereo_multiply_ = futils::equalPowerFade(stereo_spread * 0.5f + 0.5f);
    blend_center_multiply_ = futils::equalPowerFadeInverse(stereo_spread * 0.5f + 0.5f);

    current_stereo_mult = utils::maskLoad(current_stereo_mult, blend_stereo_multiply_, reset_mask);
    current_center_mult = utils::maskLoad(current_center_mult, blend_center_multiply_, reset_mask);
    poly_float delta_stereo_mult = (blend_stereo_multiply_ - current_stereo_mult) * (1.0f / num_samples);
    poly_float delta_center_mult = (blend_center_multiply_ - current_center_mult) * (1.0f / num_samples);

    if (delta_stereo_mult.sum() + delta_center_mult.sum() == 0.0f && utils::equal(stereo_spread, 1.0f))
      return;

    for (int i = 0; i < num_samples; ++i) {
      current_stereo_mult += delta_stereo_mult;
      current_center_mult += delta_center_mult;
      poly_float val = audio_out[i];
      poly_float swap = utils::swapStereo(val);
      audio_out[i] = val * current_stereo_mult + swap * current_center_mult;
    }
  }

  void SynthOscillator::levelOutput(poly_float* audio_out, const poly_float* raw_out,
                                    int num_samples, poly_mask reset_mask) {
    VITAL_ASSERT(inputMatchesBufferSize(kAmplitude));

    poly_float current_pan_amplitude = pan_amplitude_;
    pan_amplitude_ = futils::panAmplitude(utils::clamp(input(kPan)->at(0), -1.0f, 1.0f));

    current_pan_amplitude = utils::maskLoad(current_pan_amplitude, pan_amplitude_, reset_mask);
    poly_float delta_pan_amplitude = (pan_amplitude_ - current_pan_amplitude) * (1.0f / num_samples);

    const poly_float* amplitude = input(kAmplitude)->source->buffer;
    poly_float zero = 0.0f;
    for (int i = 0; i < num_samples; ++i) {
      poly_float amp = utils::max(amplitude[i], zero);
      current_pan_amplitude += delta_pan_amplitude;
      audio_out[i] = current_pan_amplitude * raw_out[i] * amp * amp;

      VITAL_ASSERT(utils::isFinite(audio_out[i]));
    }
  }

  void SynthOscillator::convertVoiceChannels(int num_samples, poly_float* audio_out, poly_mask active_mask) {
    for (int i = 0; i < num_samples; ++i)
      audio_out[i] += utils::swapVoices(audio_out[i]);
  }

  force_inline void SynthOscillator::resetWavetableBuffers() {
    const mono_float* default_buffer = Wavetable::null_waveform();
    for (int i = 0; i < kNumBuffers; ++i) {
      last_buffers_[i] = default_buffer;
      wave_buffers_[i] = default_buffer;
    }
  }

  // SynthyBoy B21: the warp state of the voice block back into the arrays (the counterpart of the load in loadVoiceBlock)
  void SynthOscillator::storeWarpState(int index, bool single_voice, poly_mask active_mask) {
    for (int s = 0; s < 2; ++s) {
      const WarpState& state = voice_block_.state[s];
      if (single_voice) {
        WarpState& one = warp_state_[s][2 * index];
        WarpState& two = warp_state_[s][2 * index + 1];
        one.acc = utils::maskLoad(one.acc, state.acc, active_mask);
        two.acc = utils::maskLoad(two.acc, utils::swapVoices(state.acc), active_mask);
        one.lp1 = utils::maskLoad(one.lp1, state.lp1, active_mask);
        two.lp1 = utils::maskLoad(two.lp1, utils::swapVoices(state.lp1), active_mask);
        one.lp2 = utils::maskLoad(one.lp2, state.lp2, active_mask);
        two.lp2 = utils::maskLoad(two.lp2, utils::swapVoices(state.lp2), active_mask);
      }
      else
        warp_state_[s][index] = state;
    }
  }

  force_inline void SynthOscillator::loadVoiceBlock(VoiceBlock& voice_block, int index, poly_mask active_mask) {
    bool single_voice = (~active_mask).anyMask();
    if (single_voice) {
      voice_block.phase = compactAndLoadVoice(phases_ + 2 * index, active_mask);
      voice_block.phase_inc_mult = compactAndLoadVoice(phase_inc_mults_ + 2 * index, active_mask);
      voice_block.from_phase_inc_mult = compactAndLoadVoice(from_phase_inc_mults_ + 2 * index, active_mask);
      voice_block.shepard_double_mask = compactAndLoadVoice(shepard_double_masks_ + 2 * index, active_mask);
      voice_block.shepard_half_mask = compactAndLoadVoice(shepard_half_masks_ + 2 * index, active_mask);
      voice_block.distortion = compactAndLoadVoice(distortion_values_ + 2 * index, active_mask);
      voice_block.last_distortion = compactAndLoadVoice(last_distortion_values_ + 2 * index, active_mask);
      voice_block.distortion2 = compactAndLoadVoice(distortion2_values_ + 2 * index, active_mask);
      voice_block.last_distortion2 = compactAndLoadVoice(last_distortion2_values_ + 2 * index, active_mask);
      for (int s = 0; s < 2; ++s) {
        const WarpState& one = warp_state_[s][2 * index];
        const WarpState& two = warp_state_[s][2 * index + 1];
        voice_block.state[s].acc = utils::maskLoad(utils::swapVoices(two.acc), one.acc, active_mask);
        voice_block.state[s].lp1 = utils::maskLoad(utils::swapVoices(two.lp1), one.lp1, active_mask);
        voice_block.state[s].lp2 = utils::maskLoad(utils::swapVoices(two.lp2), one.lp2, active_mask);
        for (int w = 0; w < 2; ++w) {
          poly_float& var = voice_block.stage[w].var;
          var = utils::maskLoad(utils::swapVoices(var), var, active_mask);
        }
      }
      poly_int distortion_phase_swap = utils::swapVoices(voice_block.distortion_phase);
      voice_block.distortion_phase = utils::maskLoad(distortion_phase_swap, voice_block.distortion_phase, active_mask);
      poly_int last_distortion_phase_swap = utils::swapVoices(voice_block.last_distortion_phase);
      voice_block.last_distortion_phase = utils::maskLoad(last_distortion_phase_swap, voice_block.last_distortion_phase,
                                                          active_mask);
      poly_int distortion_phase2_swap = utils::swapVoices(voice_block.distortion_phase2);
      voice_block.distortion_phase2 = utils::maskLoad(distortion_phase2_swap, voice_block.distortion_phase2, active_mask);
      poly_int last_distortion_phase2_swap = utils::swapVoices(voice_block.last_distortion_phase2);
      voice_block.last_distortion_phase2 = utils::maskLoad(last_distortion_phase2_swap, voice_block.last_distortion_phase2,
                                                           active_mask);

      int buffer_index = 2 * index * poly_float::kSize;
      compactAndLoadVoice(voice_block.from_buffers, last_buffers_ + buffer_index, active_mask);
      compactAndLoadVoice(voice_block.to_buffers, wave_buffers_ + buffer_index, active_mask);

      if ((index + 1) * poly_float::kSize > active_oscillators_) {
        int zero_index = active_mask[0] ? 2 : 0;
        voice_block.from_buffers[zero_index] = Wavetable::null_waveform();
        voice_block.from_buffers[zero_index + 1] = Wavetable::null_waveform();
        voice_block.to_buffers[zero_index] = Wavetable::null_waveform();
        voice_block.to_buffers[zero_index + 1] = Wavetable::null_waveform();
      }
    }
    else {
      voice_block.phase = phases_[index];
      voice_block.phase_inc_mult = phase_inc_mults_[index];
      voice_block.from_phase_inc_mult = from_phase_inc_mults_[index];
      voice_block.shepard_double_mask = shepard_double_masks_[index];
      voice_block.shepard_half_mask = shepard_half_masks_[index];
      voice_block.distortion = distortion_values_[index];
      voice_block.last_distortion = last_distortion_values_[index];
      voice_block.distortion2 = distortion2_values_[index];
      voice_block.last_distortion2 = last_distortion2_values_[index];
      for (int s = 0; s < 2; ++s)
        voice_block.state[s] = warp_state_[s][index];

      int buffer_index = index * poly_float::kSize;
      memcpy(voice_block.from_buffers, last_buffers_ + buffer_index, poly_float::kSize * sizeof(mono_float*));
      memcpy(voice_block.to_buffers, wave_buffers_ + buffer_index, poly_float::kSize * sizeof(mono_float*));
    }
  }

  void SynthOscillator::setSpectralMorphValues(SpectralMorph spectral_morph) {
    static constexpr float kModMult = 0.99f;
    poly_float spectral_morph_amount = input(kSpectralMorphAmount)->at(0);
    poly_float morph_spread = input(kUnisonSpectralMorphSpread)->at(0);
    int num_phase_updates = active_oscillators_ / 2;

    for (int v = 0; v < kNumPolyPhase; ++v) {
      poly_float t = (v / (utils::imax(2, num_phase_updates) - 1.0f)) * 2.0f;
      last_spectral_morph_values_[v] = spectral_morph_values_[v];
      spectral_morph_values_[v] = spectral_morph_amount + t * morph_spread;
    }
    
    if (spectral_morph == kShepardTone) {
      for (int v = 0; v < kNumPolyPhase; ++v)
        spectral_morph_values_[v] = utils::mod(spectral_morph_values_[v] * kModMult) * (1.0f / kModMult);
    }
    else {
      for (int v = 0; v < kNumPolyPhase; ++v)
        spectral_morph_values_[v] = utils::clamp(spectral_morph_values_[v], 0.0f, 1.0f);
    }

    bool is_spread = poly_float::notEqual(morph_spread, 0.0f).anyMask() != 0;
    setSpectralMorphValues(spectral_morph, spectral_morph_values_, kNumPolyPhase, is_spread);
    if (spectral_morph == kVocode) {
      static constexpr float kDefaultSampleRate = 88200;
      float sample_rate = wavetable_->getActiveSampleRate();
      if (sample_rate <= 0.0f)
        sample_rate = kDefaultSampleRate;
      float sample_rate_ratio = getSampleRate() / sample_rate;
      float frequency_ratio = sample_rate_ratio * wavetable_->getActiveFrequencyRatio();

      for (int i = 0; i < kNumPolyPhase; ++i)
        spectral_morph_values_[i] *= frequency_ratio;
    }
  }

  void SynthOscillator::setDistortionValues(DistortionType distortion_type) {
    poly_float distortion_amount = input(kDistortionAmount)->at(0);
    poly_float distortion_spread = input(kUnisonDistortionSpread)->at(0);
    int num_phase_updates = active_oscillators_ / 2;
    for (int v = 0; v < kNumPolyPhase; ++v) {
      poly_float t = (v / (utils::imax(2, num_phase_updates) - 1.0f) * 2.0f);
      last_distortion_values_[v] = distortion_values_[v];
      distortion_values_[v] = utils::clamp(distortion_amount + t * distortion_spread, 0.0f, 1.0f);
    }

    if (distortion_type == kQuantize) {
      for (int i = 0; i < kNumPolyPhase; ++i)
        last_distortion_values_[i] = utils::max(1.5f, last_distortion_values_[i]);
    }

    bool is_spread = poly_float::notEqual(distortion_spread, 0.0f).anyMask() != 0;
    setDistortionValues(distortion_type, distortion_values_, kNumPolyPhase, is_spread);
  }

  void SynthOscillator::setDistortion2Values(DistortionType distortion_type) {
    poly_float distortion_amount = input(kWarp2Amount)->at(0);
    poly_float distortion_spread = input(kWarp2Spread)->at(0);
    int num_phase_updates = active_oscillators_ / 2;
    for (int v = 0; v < kNumPolyPhase; ++v) {
      poly_float t = (v / (utils::imax(2, num_phase_updates) - 1.0f) * 2.0f);
      last_distortion2_values_[v] = distortion2_values_[v];
      distortion2_values_[v] = utils::clamp(distortion_amount + t * distortion_spread, 0.0f, 1.0f);
    }

    if (distortion_type == kQuantize) {
      for (int i = 0; i < kNumPolyPhase; ++i)
        last_distortion2_values_[i] = utils::max(1.5f, last_distortion2_values_[i]);
    }

    bool is_spread = poly_float::notEqual(distortion_spread, 0.0f).anyMask() != 0;
    setDistortionValues(distortion_type, distortion2_values_, kNumPolyPhase, is_spread);
  }

  void SynthOscillator::setDistortionValues(DistortionType distortion_type,
                                            poly_float* values, int num_values, bool spread) {
    switch (distortion_type) {
      case kFmOscillatorA:
      case kFmOscillatorB:
      case kFmSample:
        for (int i = 0; i < num_values; ++i)
          values[i] *= values[i];
        break;
      case kSync:
      case kSyncVar:
      case kFormant:
        setPowerDistortionValues(values, num_values, kMaxSyncPower, spread);
        for (int i = 0; i < num_values; ++i)
          values[i] *= (1.0f / kMaxSync);
        break;
      case kQuantize:
        if (spread) {
          for (int i = 0; i < num_values; ++i) {
            poly_float distortion = poly_float(1.0f) - values[i];
            distortion *= distortion * distortion;
            distortion = distortion * kMaxQuantize;
            values[i] = futils::pow(2.0f, distortion * kDistortBits + 1.0f);
          }
        }
        else {
          poly_float distortion = poly_float(1.0f) - values[0];
          distortion *= distortion * distortion;
          distortion = distortion * kMaxQuantize;
          distortion = futils::pow(2.0f, distortion * kDistortBits + 1.0f);
          for (int i = 0; i < num_values; ++i)
            values[i] = distortion;
        }
        break;
      case kSqueeze:
        for (int i = 0; i < num_values; ++i)
          values[i] = values[i] * 2.0f * kMaxSqueezePercent + (1.0f - kMaxSqueezePercent);
        break;
      case kPulseWidth:
        if (spread) {
          for (int i = 0; i < num_values; ++i) {
            poly_float distortion = utils::max(poly_float(1.0f) - values[i], 1.0f / UINT_MAX);
            values[i] = poly_float(1.0f) / distortion;
          }
        }
        else {
          poly_float distortion = utils::max(poly_float(1.0f) - values[0], 1.0f / UINT_MAX);
          distortion = poly_float(1.0f) / distortion;
          for (int i = 0; i < num_values; ++i)
            values[i] = distortion;
        }
        break;
      default:
        break;
    }
  }

  void SynthOscillator::setSpectralMorphValues(SpectralMorph spectral_morph,
                                               poly_float* values, int num_values, bool spread) {
    switch (spectral_morph) {
      case kVocode:
        setPowerDistortionValues(values, num_values, -kMaxFormantShift, spread);
        break;
      case kFormScale:
        setPowerDistortionValues(values, num_values, -kMaxEvenOddFormantShift, spread);
        break;
      case kHarmonicScale:
        setPowerDistortionValues(values, num_values, kMaxHarmonicScale, spread);
        break;
      case kInharmonicScale:
        setPowerDistortionValues(values, num_values, kMaxInharmonicScale, spread);
        break;
      case kSmear:
        for (int i = 0; i < num_values; ++i) {
          poly_float invert = poly_float(1.0f) - values[i];
          values[i] = poly_float(1.0f) - invert * invert * invert;
        }
        break;
      case kRandomAmplitudes:
        for (int i = 0; i < num_values; ++i)
          values[i] = values[i] * (kRandomAmplitudeStages - 1);
        break;
      case kPhaseDisperse:
        for (int i = 0; i < num_values; ++i)
          values[i] = -(values[i] * 2.0f - 1.0f) * kPhaseDisperseScale;
        break;
      case kSkew:
        for (int i = 0; i < num_values; ++i)
          values[i] = values[i] * values[i] * kSkewScale;
        break;
      case kShepardTone:
        for (int i = 0; i < num_values; ++i)
          values[i] = poly_float(1.0f) - values[i];
        break;
      default:
        break;
    }
  }
  
  void SynthOscillator::runSpectralMorph(SpectralMorph morph_type, float morph_amount,
                                         const Wavetable::WavetableData* wavetable_data,
                                         int wavetable_index, poly_float* dest, FourierTransform* transform) {
    int h = Wavetable::kNumHarmonics;
    switch (morph_type) {
      case kVocode:
      case kFormScale:
        evenOddVocodeMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      case kHarmonicScale:
        harmonicScaleMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      case kInharmonicScale:
        inharmonicScaleMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      case kSmear:
        smearMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      case kRandomAmplitudes:
        randomAmplitudeMorph(wavetable_data, wavetable_index,
                             dest, transform, morph_amount, h, RandomValues::instance()->buffer());
        break;
      case kShepardTone:
        shepardMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      case kLowPass:
        lowPassMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      case kHighPass:
        highPassMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      case kPhaseDisperse:
        phaseMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      case kSkew:
        wavetableSkewMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
        break;
      default:
        passthroughMorph(wavetable_data, wavetable_index, dest, transform, morph_amount, h, nullptr);
    }
  }

  poly_int SynthOscillator::adjustPhase(DistortionType distortion_type, poly_int phase,
                                        poly_float distortion_amount, poly_int distortion_phase) {
    switch (distortion_type) {
      case kSync:
      case kFormant:
        return syncPhase(phase, distortion_amount, distortion_phase, nullptr, 0);
      case kQuantize:
        return quantizePhase(phase, distortion_amount, distortion_phase, nullptr, 0);
      case kBend:
        return bendPhase(phase, distortion_amount, distortion_phase, nullptr, 0);
      case kSqueeze:
        return squeezePhase(phase, distortion_amount, distortion_phase, nullptr, 0);
      case kPulseWidth:
        return pulseWidthPhase(phase, distortion_amount, distortion_phase, nullptr, 0);
      default:
        return phase;
    }
  }

  poly_float SynthOscillator::getPhaseWindow(DistortionType distortion_type, poly_int phase,
                                             poly_int distorted_phase) {
    switch (distortion_type) {
      case kFormant:
        return halfSinWindow(phase, distorted_phase, 0.0f, nullptr, 0);
      case kPulseWidth:
        return pulseWidthWindow(phase, distorted_phase, 0.0f, nullptr, 0);
      default:
        return 1.0f;
    }
  }

  float SynthOscillator::warpShape(int type, float value, float amount) {
    return xShape(type, poly_float(value), poly_float(amount))[0];
  }

  poly_float SynthOscillator::interpolate(const mono_float* buffer, const poly_int indices) {
    return linearlyInterpolateBuffer(buffer, indices);
  }

  bool SynthOscillator::usesDistortionPhase(DistortionType distortion_type) {
    return distortion_type == kSync || distortion_type == kFormant || distortion_type == kQuantize ||
           distortion_type == kBend || distortion_type == kSqueeze || distortion_type == kPulseWidth ||
           isXPhaseDomain(distortion_type) || distortion_type == kFlip;
  }

  void SynthOscillator::process(int num_samples) {
    wavetable_->markUsed();
    int wavetable_version = wavetable_->getActiveVersion();
    if (wavetable_version != wavetable_version_) {
      wavetable_version_ = wavetable_version;
      resetWavetableBuffers();
    }

    poly_float active_voice = input(kActiveVoices)->at(0);
    bool left_active = active_voice[0] == 1.0f;
    bool right_active = active_voice[2] == 1.0f;

    unison_ = utils::clamp(roundf(input(kUnisonVoices)->at(0)[0]), 1.0f, kMaxUnison);
    setActiveOscillators(unison_ + (unison_ % 2));

    SpectralMorph spectral_morph = static_cast<SpectralMorph>((int)input(kSpectralMorphType)->at(0)[0]);
    DistortionType distortion_type = static_cast<DistortionType>((int)input(kDistortionType)->at(0)[0]);
    setSpectralMorphValues(spectral_morph);
    setDistortionValues(distortion_type);
    DistortionType warp2_type = static_cast<DistortionType>(utils::clamp((int)input(kWarp2Type)->at(0)[0], 0, kNumDistortionTypes - 1));
    setDistortion2Values(warp2_type);
    // SynthyBoy B21: the extended modes run through the stages, the original code path of the slot sees None
    voice_block_.warp2_type = isExtendedWarp(warp2_type) ? kNone : warp2_type;
    voice_block_.warp2_side = left_active && right_active ? 0 : (left_active ? 1 : 2);
    {
      const auto mod_source = [this](int type) -> const poly_float* {
        switch (type) {
          case kFmThruZeroA: case kFmExpA: case kFmLinearA: case kPdA: case kAmA:
            return first_mod_oscillator_ != nullptr ? first_mod_oscillator_->buffer : nullptr;
          case kFmThruZeroB: case kFmExpB: case kFmLinearB: case kPdB: case kAmB:
            return second_mod_oscillator_ != nullptr ? second_mod_oscillator_->buffer : nullptr;
          default:
            return nullptr;
        }
      };
      const mono_float* remap_table = wavetable_->frameRemap()->table;
      const int types[2] = { isExtendedWarp(distortion_type) ? static_cast<int>(distortion_type) : 0,
                             isExtendedWarp(warp2_type) ? static_cast<int>(warp2_type) : 0 };
      for (int w = 0; w < 2; ++w) {
        WarpStage& stage = voice_block_.stage[w];
        stage.type = types[w];
        stage.side = voice_block_.warp2_side;
        stage.remap = remap_table;
        stage.mod = mod_source(types[w]);
        stage.var = utils::clamp(input(w == 0 ? kDistortionPhase : kWarp2Phase)->at(0), 0.0f, 1.0f);
      }
    }
    voice_block_.modulation_buffer2 = nullptr;
    if (warp2_type == kFmOscillatorB || warp2_type == kRmOscillatorB)
      voice_block_.modulation_buffer2 = second_mod_oscillator_ != nullptr ? second_mod_oscillator_->buffer : nullptr;
    else if (warp2_type == kFmSample || warp2_type == kRmSample)
      voice_block_.modulation_buffer2 = sample_ != nullptr ? sample_->buffer : nullptr;
    else if (warp2_type == kFmOscillatorA || warp2_type == kRmOscillatorA)
      voice_block_.modulation_buffer2 = first_mod_oscillator_ != nullptr ? first_mod_oscillator_->buffer : nullptr;
    voice_block_.phase_inc_buffer = phase_inc_buffer_->buffer;
    voice_block_.spectral_morph = spectral_morph;

    switch (distortion_type) {
      case kSync:
        processOscillators<syncPhase, passThroughWindow>(num_samples, distortion_type);
        break;
      case kFormant:
        processOscillators<syncPhase, halfSinWindow>(num_samples, distortion_type);
        break;
      case kQuantize:
        processOscillators<quantizePhase, passThroughWindow>(num_samples, distortion_type);
        break;
      case kBend:
        processOscillators<bendPhase, passThroughWindow>(num_samples, distortion_type);
        break;
      case kSqueeze:
        processOscillators<squeezePhase, passThroughWindow>(num_samples, distortion_type);
        break;
      case kPulseWidth:
        processOscillators<pulseWidthPhase, pulseWidthWindow>(num_samples, distortion_type);
        break;
      case kFmOscillatorA:
      case kFmOscillatorB:
      case kFmSample:
        if (distortion_type == kFmOscillatorB)
          voice_block_.modulation_buffer = second_mod_oscillator_->buffer;
        else if (distortion_type == kFmSample)
          voice_block_.modulation_buffer = sample_->buffer;
        else
          voice_block_.modulation_buffer = first_mod_oscillator_->buffer;
        
        if (left_active && right_active)
          processOscillators<fmPhase, passThroughWindow>(num_samples, distortion_type);
        else if (left_active)
          processOscillators<fmPhaseLeft, passThroughWindow>(num_samples, distortion_type);
        else
          processOscillators<fmPhaseRight, passThroughWindow>(num_samples, distortion_type);
        break;
      case kRmOscillatorA:
      case kRmOscillatorB:
      case kRmSample:
        if (distortion_type == kRmOscillatorB)
          voice_block_.modulation_buffer = second_mod_oscillator_->buffer;
        else if (distortion_type == kRmSample)
          voice_block_.modulation_buffer = sample_->buffer;
        else
          voice_block_.modulation_buffer = first_mod_oscillator_->buffer;

        if (left_active && right_active)
          processOscillators<passThroughPhase, rmWindow>(num_samples, distortion_type);
        else if (left_active)
          processOscillators<passThroughPhase, rmWindowLeft>(num_samples, distortion_type);
        else
          processOscillators<passThroughPhase, rmWindowRight>(num_samples, distortion_type);
        break;
      default:
        processOscillators<passThroughPhase, passThroughWindow>(num_samples, distortion_type);
        break;
    }

    wavetable_->markUnused();
  }

  force_inline void SynthOscillator::setActiveOscillators(int new_active_oscillators) {
    for (int i = active_oscillators_; i < new_active_oscillators; ++i) {
      wave_buffers_[2 * i] = Wavetable::null_waveform();
      wave_buffers_[2 * i + 1] = Wavetable::null_waveform();
    }

    active_oscillators_ = new_active_oscillators;
  }

  template<poly_float(*snapTranspose)(poly_float, poly_float, float*)>
  void SynthOscillator::setPhaseIncBufferSnap(int num_samples, poly_mask reset_mask,
                                              poly_int trigger_sample, poly_mask active_mask, float* snap_buffer) {
    bool midi_track = poly_float::notEqual(input(kMidiTrack)->at(0), 0.0f).anyMask();
    poly_float current_midi = midi_note_;
    midi_note_ = kNoMidiTrackDefault;

    // Serum 2 manual p.36: wavetable oscillators always track the pitch bend, also with pitch tracking off (the switch is for the sample types)
    const bool bend_track = true;
    poly_float raw_note = input(kRawMidiNote) ? input(kRawMidiNote)->at(0) : input(kMidiNote)->at(0);
    poly_float bend_offset = input(kMidiNote)->at(0) - raw_note;

    if (midi_track) {
      poly_float note_in = bend_track ? input(kMidiNote)->at(0) : raw_note;
      midi_note_ = note_in + input(kFoldNote)->at(0);
    } else {
      poly_float fixed_p = input(kFixedPitch) ? input(kFixedPitch)->at(0) : poly_float(kNoMidiTrackDefault);
      midi_note_ = bend_track ? (fixed_p + bend_offset) : fixed_p;
    }

    int pitch_mode = input(kPitchMode) ? static_cast<int>(std::round(input(kPitchMode)->at(0)[0])) : 0;
    poly_float coarse = input(kPitchCoarse) ? input(kPitchCoarse)->at(0) : poly_float(0.0f);
    poly_float coarse_semitones = 0.0f;
    if (pitch_mode == 0) {
      coarse_semitones = coarse;
    }
    else if (pitch_mode == 1) { // Harmonics: multiplier = 1 + floor(coarse)
      for (int v = 0; v < poly_float::kSize; ++v) {
        float c = coarse[v];
        float h = std::max(1.0f, 1.0f + std::floor(c));
        coarse_semitones.set(v, 12.0f * std::log2(h));
      }
    }
    else if (pitch_mode == 2) { // Ratio: multiplier = 1 + coarse if >= 0, else 1 / (1 - coarse)
      for (int v = 0; v < poly_float::kSize; ++v) {
        float c = coarse[v];
        float r = c >= 0.0f ? (1.0f + c) : (1.0f / (1.0f - c));
        coarse_semitones.set(v, 12.0f * std::log2(std::max(1.0e-6f, r)));
      }
    }
    else if (pitch_mode == 3) { // Step (MTS-ESP / tuning seam)
      for (int v = 0; v < poly_float::kSize; ++v) {
        int step = static_cast<int>(std::round(coarse[v]));
        const mono_float* note_table = *note_table_;
        if (note_table != nullptr) {
          int k = static_cast<int>(std::round(current_midi[v]));
          int target_k = std::clamp(k + step, 0, 127);
          int k_clamped = std::clamp(k, 0, 127);
          float target_n = note_table[target_k];
          float base_n = note_table[k_clamped];
          if (!std::isnan(target_n) && !std::isnan(base_n))
            coarse_semitones.set(v, target_n - base_n);
          else
            coarse_semitones.set(v, static_cast<float>(step));
        } else {
          coarse_semitones.set(v, static_cast<float>(step));
        }
      }
    }

    float sample_inc = 1.0f / num_samples;
    current_midi = utils::maskLoad(current_midi, midi_note_, reset_mask);
    poly_float delta_midi = (midi_note_ - current_midi) * sample_inc;
    current_midi = utils::maskLoad(utils::swapVoices(current_midi), current_midi, active_mask);
    delta_midi = utils::maskLoad(utils::swapVoices(delta_midi), delta_midi, active_mask);

    const poly_float* transpose_buffer = input(kTranspose)->source->buffer;
    const poly_float* tune_buffer = input(kTune)->source->buffer;
    const poly_float* phase_buffer = input(kPhase)->source->buffer;

    poly_float base_midi = current_midi + transpose_buffer[0] + tune_buffer[0] + coarse_semitones;
    poly_float base_frequency = utils::midiNoteToFrequency(base_midi);

    poly_float sample_rate_scale = kPhaseMult / getSampleRate();
    poly_float phase_scale = kPhaseMult;

    poly_float* inc_dest = phase_inc_buffer_->buffer;
    poly_int* phase_dest = phase_buffer_->buffer;

    for (int i = 0; i < num_samples; ++i) {
      poly_float shift_phase = utils::mod(phase_buffer[i]) - 0.5f;
      poly_int phase = utils::toInt(shift_phase * phase_scale);
      phase_dest[i] = utils::maskLoad(utils::swapVoices(phase), phase, active_mask);

      current_midi += delta_midi;

      poly_float midi = snapTranspose(current_midi, transpose_buffer[i], snap_buffer) + tune_buffer[i] + coarse_semitones;
      poly_float frequency = base_frequency * futils::midiOffsetToRatio(midi - base_midi);
      poly_mask zero_mask = poly_int::lessThan(i, trigger_sample) & reset_mask;
      poly_float result = (frequency * sample_rate_scale) & ~zero_mask;
      inc_dest[i] = utils::maskLoad(utils::swapVoices(result), result, active_mask);
    }
  }

  void SynthOscillator::setPhaseIncBuffer(int num_samples, poly_mask reset_mask, 
                                          poly_int trigger_sample, poly_mask active_mask) {
    int transpose_quantize = static_cast<int>(input(kTransposeQuantize)->at(0)[0]);
    if (!utils::isTransposeSnapping(transpose_quantize)) {
      setPhaseIncBufferSnap<noTransposeSnap>(num_samples, reset_mask, trigger_sample, active_mask, nullptr);
      return;
    }

    float snap_buffer[kNotesPerOctave + 1];
    utils::fillSnapBuffer(transpose_quantize, snap_buffer);
    if (utils::isTransposeQuantizeGlobal(transpose_quantize))
      setPhaseIncBufferSnap<globalTransposeSnap>(num_samples, reset_mask, trigger_sample, active_mask, snap_buffer);
    else
      setPhaseIncBufferSnap<localTransposeSnap>(num_samples, reset_mask, trigger_sample, active_mask, snap_buffer);
  }

  template<poly_int(*phaseDistort)(poly_int, poly_float, poly_int, const poly_float*, int),
           poly_float(*window)(poly_int, poly_int, poly_float, const poly_float*, int)>
  void SynthOscillator::processOscillators(int num_samples, DistortionType distortion_type) {
    poly_mask active_voice_mask = poly_float::equal(input(kActiveVoices)->at(0), 1.0f);
    poly_float current_center_amplitude = center_amplitude_;
    poly_float current_detuned_amplitude = detuned_amplitude_;
    setAmplitude();

    poly_mask reset_mask = getResetMask(kReset);
    poly_int trigger_offset = input(kReset)->source->trigger_offset;
    poly_mask retrigger_mask = getResetMask(kRetrigger) & ~reset_mask;

    current_center_amplitude = utils::maskLoad(current_center_amplitude, center_amplitude_, reset_mask);
    current_detuned_amplitude = utils::maskLoad(current_detuned_amplitude, detuned_amplitude_, reset_mask);

    setPhaseIncMults();
    setPhaseIncBuffer(num_samples, reset_mask, trigger_offset, active_voice_mask);

    poly_float current_distortion_phase = distortion_phase_;
    distortion_phase_ = 0.0f;
    if (usesDistortionPhase(distortion_type))
      distortion_phase_ = input(kDistortionPhase)->at(0) - 0.5f;
    current_distortion_phase = utils::maskLoad(current_distortion_phase, distortion_phase_, reset_mask);

    voice_block_.last_distortion_phase = utils::toInt(current_distortion_phase * kPhaseMult);
    voice_block_.distortion_phase = utils::toInt(distortion_phase_ * kPhaseMult);

    // SynthyBoy: the distortion phase of the second warp slot
    poly_float current_distortion_phase2 = distortion_phase2_;
    distortion_phase2_ = 0.0f;
    if (usesDistortionPhase(static_cast<DistortionType>(voice_block_.stage[1].type != 0 ? voice_block_.stage[1].type : voice_block_.warp2_type)))
      distortion_phase2_ = input(kWarp2Phase)->at(0) - 0.5f;
    current_distortion_phase2 = utils::maskLoad(current_distortion_phase2, distortion_phase2_, reset_mask);
    voice_block_.last_distortion_phase2 = utils::toInt(current_distortion_phase2 * kPhaseMult);
    voice_block_.distortion_phase2 = utils::toInt(distortion_phase2_ * kPhaseMult);

    poly_mask wave_buffer_mask = reset_mask | retrigger_mask;
    poly_float buffer_phase_inc = phase_inc_buffer_->buffer[num_samples - 1] * (1.0f / kPhaseMult);
    for (int lane = 0; lane < poly_float::kSize; ++lane)
      remap_snap_[lane] = wave_buffer_mask[lane] != 0; // SynthyBoy: a new note jumps to its remapped frame, it does not glide from the last note
    if (wave_buffer_mask[0])
      setWaveBuffers(buffer_phase_inc, 0);
    if (wave_buffer_mask[2])
      setWaveBuffers(buffer_phase_inc, 2);
    for (int lane = 0; lane < poly_float::kSize; ++lane)
      remap_snap_[lane] = false;

    if (reset_mask.anyMask())
      reset(reset_mask, trigger_offset);

    if (retrigger_mask.anyMask()) {
      for (int i = 0; i < kNumPolyPhase; ++i)
        from_phase_inc_mults_[i] = utils::maskLoad(from_phase_inc_mults_[i], phase_inc_mults_[i], retrigger_mask);
    }

    voice_block_.start_sample = 0;
    voice_block_.total_samples = num_samples;
    int num_buffer_samples = kWavetableFadeTime * getSampleRate();
    if (voice_block_.num_buffer_samples != num_buffer_samples) {
      voice_block_.num_buffer_samples = num_buffer_samples;
      voice_block_.current_buffer_sample = 0;
    }

    bool shepard = voice_block_.spectral_morph == kShepardTone;
    if (shepard)
      setupShepardWrap();
    else
      clearShepardWrap();

    voice_block_.current_buffer_sample &= active_voice_mask;
    while (voice_block_.start_sample < num_samples) {
      poly_int remaining_fade_samples = poly_int(voice_block_.num_buffer_samples) - voice_block_.current_buffer_sample;
      int min_remaining_fade_samples = std::min(remaining_fade_samples[0], remaining_fade_samples[2]);
      int samples = std::min(min_remaining_fade_samples, num_samples - voice_block_.start_sample);
      voice_block_.end_sample = voice_block_.start_sample + samples;
      processChunk<phaseDistort, window>(current_center_amplitude, current_detuned_amplitude);

      voice_block_.current_buffer_sample += samples;
      voice_block_.start_sample = voice_block_.end_sample;

      poly_mask new_buffer_mask = poly_int::equal(voice_block_.current_buffer_sample, voice_block_.num_buffer_samples);
      if (shepard && new_buffer_mask.anyMask())
        doShepardWrap(new_buffer_mask, transpose_quantize_);

      if (new_buffer_mask[0])
        setWaveBuffers(buffer_phase_inc, 0);
      if (new_buffer_mask[2])
        setWaveBuffers(buffer_phase_inc, 2);

      VITAL_ASSERT((int)voice_block_.current_buffer_sample[0] < voice_block_.num_buffer_samples);
      VITAL_ASSERT((int)voice_block_.current_buffer_sample[2] < voice_block_.num_buffer_samples);
    }

    if (reset_mask.anyMask())
      clearOutputBufferForReset(reset_mask, kReset, kRaw);

    processBlend(num_samples, reset_mask);
  }

  template<poly_int(*phaseDistort)(poly_int, poly_float, poly_int, const poly_float*, int),
           poly_float(*window)(poly_int, poly_int, poly_float, const poly_float*, int)>
  void SynthOscillator::processChunk(poly_float current_center_amplitude, poly_float current_detuned_amplitude) {
    int active_channels = input(kActiveVoices)->at(0).sum();
    if (active_channels < 2)
      return;

    VITAL_ASSERT(active_channels == 2 || active_channels == 4);
    int num_active_voices = active_channels / 2;
    poly_mask active_voice_mask = poly_float::equal(input(kActiveVoices)->at(0), 1.0f);
    int num_samples = voice_block_.end_sample - voice_block_.start_sample;

    poly_float* audio_out = output(kRaw)->buffer + voice_block_.start_sample;
    utils::zeroBuffer(audio_out, num_samples);

    poly_float center_amplitude = center_amplitude_;
    poly_float detuned_amplitude = detuned_amplitude_;

    if (num_active_voices < 2) {
      poly_float current_detuned_swap = utils::swapVoices(current_detuned_amplitude);
      current_detuned_amplitude = utils::maskLoad(current_detuned_swap, current_detuned_amplitude, active_voice_mask);
      current_center_amplitude = utils::maskLoad(current_detuned_amplitude,
                                                 current_center_amplitude, active_voice_mask);

      poly_float detuned_swap = utils::swapVoices(detuned_amplitude);
      detuned_amplitude = utils::maskLoad(detuned_swap, detuned_amplitude, active_voice_mask);
      center_amplitude = utils::maskLoad(detuned_amplitude, center_amplitude, active_voice_mask);

      poly_float distortion_swap = utils::swapVoices(voice_block_.distortion);
      voice_block_.distortion = utils::maskLoad(distortion_swap, voice_block_.distortion, active_voice_mask);

      poly_float last_distortion_swap = utils::swapVoices(voice_block_.last_distortion);
      voice_block_.last_distortion = utils::maskLoad(last_distortion_swap, voice_block_.last_distortion,
                                                     active_voice_mask);
      // (the second warp slot is loaded per index by loadVoiceBlock, which does the same for its values)
      poly_int swap_buffer_sample = utils::swapVoices(voice_block_.current_buffer_sample);
      voice_block_.current_buffer_sample = utils::maskLoad(swap_buffer_sample, voice_block_.current_buffer_sample,
                                                           active_voice_mask);
    }

    int num_phase_updates = (poly_float::kSize - 1 + num_active_voices * active_oscillators_) / poly_float::kSize;
    for (int p = 1; p < num_phase_updates; ++p) {
      loadVoiceBlock(voice_block_, p, active_voice_mask);

      poly_int phase = processDetuned<phaseDistort, window>(voice_block_, audio_out);
      if (num_active_voices < 2)
        expandAndWriteVoice(phases_ + 2 * p, phase, active_voice_mask);
      else
        phases_[p] = phase;
      storeWarpState(p, num_active_voices < 2, active_voice_mask);
    }

    loadVoiceBlock(voice_block_, 0, active_voice_mask);

    mono_float sample_inc = 1.0f / voice_block_.total_samples;
    poly_float delta_center_amplitude = (center_amplitude - current_center_amplitude) * sample_inc;
    poly_float delta_detuned_amplitude = (detuned_amplitude - current_detuned_amplitude) * sample_inc;
    current_center_amplitude += delta_center_amplitude * voice_block_.start_sample;
    current_detuned_amplitude += delta_detuned_amplitude * voice_block_.start_sample;
    poly_int center_phase = processCenter<phaseDistort, window>(voice_block_, audio_out,
                                                                current_center_amplitude, delta_center_amplitude,
                                                                current_detuned_amplitude, delta_detuned_amplitude);

    if (num_active_voices < 2) {
      expandAndWriteVoice(phases_, center_phase, active_voice_mask);
      convertVoiceChannels(num_samples, audio_out, active_voice_mask);
    }
    else
      phases_[0] = center_phase;
    storeWarpState(0, num_active_voices < 2, active_voice_mask);

    // Phase legato: publish the phases of the voice that rendered last (voice 1 of the pair when both play)
    const int writer = active_voice_mask[2] ? 1 : (active_voice_mask[0] ? 0 : -1);
    if (writer >= 0) {
      for (int i = 0; i < kNumPolyPhase; ++i) {
        legato_state_->left[i] = phases_[i][2 * writer];
        legato_state_->right[i] = phases_[i][2 * writer + 1];
      }
      legato_state_->valid = true;
    }
  }

  void SynthOscillator::processBlend(int num_samples, poly_mask reset_mask) {
    poly_float* audio_out = output(kRaw)->buffer;
    stereoBlend(audio_out, num_samples, reset_mask);
    levelOutput(output(kLevelled)->buffer, audio_out, num_samples, reset_mask);
  }
} // namespace sbvital
