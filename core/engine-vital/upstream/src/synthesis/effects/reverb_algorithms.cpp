// SynthyBoy (AGPL-3.0-or-later combined work), 2026: see reverb_algorithms.h.

#include "reverb_algorithms.h"

#include <algorithm>
#include <cstring>

namespace sbvital {

  namespace {
    constexpr float kTwoPi = 6.28318530717958647692f;
    constexpr float kLnMilli = 6.907755279f;            // -ln (0.001): the T60 amplitude
    constexpr float kMaxLoopGain = 0.9996f;
    constexpr float kSlew = 0.002f;                     // the one-pole of delay changes (size), about 10 ms at 48 kHz
    constexpr float kMaxDelaySeconds = 0.5f;

    int nextPowerOfTwo(int value) {
      int power = 1;
      while (power < value)
        power <<= 1;
      return power;
    }

    float sign(int index, int pattern) {
      // the rows of an 8 x 8 Hadamard matrix
      static const int8_t kRows[4][8] = {
        { 1, 1, -1, -1, 1, 1, -1, -1 },
        { 1, -1, 1, -1, -1, 1, -1, 1 },
        { 1, -1, -1, 1, 1, -1, -1, 1 },
        { 1, 1, 1, 1, -1, -1, -1, -1 },
      };
      return static_cast<float>(kRows[pattern & 3][index & 7]);
    }

    // ---- the tunings -------------------------------------------------------------------------------------------------------------
    // Hall: eight mutually prime-ish lines (milliseconds), the all-pass in each line and four input all-passes
    constexpr float kHallLines[8] = { 41.7f, 53.9f, 61.3f, 71.1f, 79.7f, 89.9f, 101.3f, 113.9f };
    constexpr float kHallAllpass[8] = { 4.3f, 5.1f, 6.7f, 7.9f, 9.1f, 10.9f, 12.7f, 14.3f };
    constexpr float kHallInput[4] = { 5.3f, 7.9f, 11.3f, 13.7f };
    // Basin: short and dense
    constexpr float kBasinLines[8] = { 17.3f, 21.1f, 23.9f, 27.7f, 31.3f, 34.9f, 39.1f, 43.7f };
    constexpr float kBasinAllpass[8] = { 2.3f, 3.1f, 3.7f, 4.3f, 5.3f, 6.1f, 7.1f, 8.3f };
    constexpr float kBasinInput[4] = { 2.9f, 4.1f, 5.7f, 7.3f };
    // Nitrous: the size of the rooms in metres (MODE)
    constexpr float kRoomSize[5][3] = {
      { 24.0f, 32.0f, 18.0f },   // Space
      { 6.3f, 4.1f, 3.0f },      // Marble
      { 9.0f, 5.5f, 3.5f },      // Rectangle
      { 7.5f, 6.5f, 3.8f },      // Hexagon (corner to corner, flat to flat, height)
      { 5.0f, 5.0f, 5.0f },      // Box (a cube: the modes coincide)
    };
    constexpr float kRoomBrightness[5] = { 1.45f, 0.4f, 1.0f, 1.0f, 0.8f };   // factor on the loss of the high shelf
    constexpr float kRoomEarly[5] = { 0.15f, 0.6f, 0.4f, 0.4f, 0.45f };
    constexpr float kSpeedOfSound = 343.0f;
    // early reflections (milliseconds) and their gains
    constexpr float kEarlyLeft[6] = { 7.3f, 13.1f, 19.7f, 29.3f, 37.9f, 47.1f };
    constexpr float kEarlyRight[6] = { 9.1f, 15.7f, 23.3f, 31.9f, 41.3f, 53.3f };
    constexpr float kEarlyGain[6] = { 0.8f, -0.7f, 0.6f, -0.5f, 0.4f, -0.35f };
    // plate (samples at 29761 Hz, after Dattorro)
    constexpr float kPlateReferenceRate = 29761.0f;
    constexpr float kPlateInput[4] = { 142.0f, 107.0f, 379.0f, 277.0f };
    constexpr float kPlateMod[2] = { 672.0f, 908.0f };
    constexpr float kPlateDelay1[2] = { 4453.0f, 4217.0f };
    constexpr float kPlateSecond[2] = { 1800.0f, 2656.0f };
    constexpr float kPlateDelay2[2] = { 3720.0f, 3163.0f };
    // vintage (samples at 44100 Hz, after Freeverb)
    constexpr float kCombs[8] = { 1116.0f, 1188.0f, 1277.0f, 1356.0f, 1422.0f, 1491.0f, 1557.0f, 1617.0f };
    constexpr float kVintageAllpass[4] = { 556.0f, 441.0f, 341.0f, 225.0f };
    constexpr float kVintageInput[2] = { 142.0f, 107.0f };
    constexpr float kStereoSpread = 23.0f;
  } // namespace

  // ---- Line ---------------------------------------------------------------------------------------------------------------------
  void ReverbAlgorithms::Line::init(int minimum_size) {
    const int size = nextPowerOfTwo(std::max(minimum_size, 16));
    buffer.assign(static_cast<size_t>(size), 0.0f);
    mask = static_cast<unsigned>(size - 1);
    write = 0;
  }

  void ReverbAlgorithms::Line::clear() {
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    write = 0;
  }

  float ReverbAlgorithms::Line::read(float delay) const {
    delay = std::min(std::max(delay, 2.0f), static_cast<float>(mask) - 3.0f);
    const int whole = static_cast<int>(delay);
    const float t = delay - static_cast<float>(whole);
    const float p0 = buffer[(write - static_cast<unsigned>(whole - 1)) & mask];
    const float p1 = buffer[(write - static_cast<unsigned>(whole)) & mask];
    const float p2 = buffer[(write - static_cast<unsigned>(whole + 1)) & mask];
    const float p3 = buffer[(write - static_cast<unsigned>(whole + 2)) & mask];
    const float c1 = 0.5f * (p2 - p0);
    const float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
    return ((c3 * t + c2) * t + c1) * t + p1;
  }

  // ---- the reverb ---------------------------------------------------------------------------------------------------------------
  ReverbAlgorithms::ReverbAlgorithms() : sample_rate_(0), configured_(false) {
    std::memset(base_delay_, 0, sizeof(base_delay_));
    std::memset(delay_, 0, sizeof(delay_));
    std::memset(target_delay_, 0, sizeof(target_delay_));
    std::memset(gain_, 0, sizeof(gain_));
    std::memset(line_allpass_delay_, 0, sizeof(line_allpass_delay_));
    std::memset(input_gain_, 0, sizeof(input_gain_));
    std::memset(output_left_, 0, sizeof(output_left_));
    std::memset(output_right_, 0, sizeof(output_right_));
    std::memset(er_time_, 0, sizeof(er_time_));
    std::memset(er_gain_, 0, sizeof(er_gain_));
    std::memset(input_allpass_coefficient_, 0, sizeof(input_allpass_coefficient_));
    std::memset(input_ms_, 0, sizeof(input_ms_));
    std::memset(plate_feedback_, 0, sizeof(plate_feedback_));
    std::memset(comb_delay_, 0, sizeof(comb_delay_));
    std::memset(comb_gain_, 0, sizeof(comb_gain_));
    std::memset(vintage_lowpass_, 0, sizeof(vintage_lowpass_));
    er_scale_ = 1.0f;
    er_level_ = 0.0f;
    line_allpass_coefficient_ = 0.5f;
    depth_samples_ = 0.0f;
    phase_ = 0.0f;
    plate_decay_gain_ = 0.0f;
    plate_scale_ = 1.0f;
    vintage_scale_ = 1.0f;
    output_scale_ = 1.0f;
  }

  void ReverbAlgorithms::prepare(int sample_rate) {
    sample_rate_ = std::max(sample_rate, 8000);
    const float rate = static_cast<float>(sample_rate_);
    const int line_size = static_cast<int>(kMaxDelaySeconds * rate) + 16;
    for (int i = 0; i < kLines; ++i) {
      lines_[i].init(line_size);
      line_allpass_[i].line.init(static_cast<int>(0.03f * rate));
    }
    for (auto& allpass : input_allpass_)
      allpass.line.init(static_cast<int>(0.03f * rate));
    er_line_[0].init(static_cast<int>(0.4f * rate));
    er_line_[1].init(16);

    for (auto& allpass : plate_input_)
      allpass.line.init(static_cast<int>(0.05f * rate));
    for (int b = 0; b < 2; ++b) {
      plate_mod_[b].line.init(static_cast<int>(0.15f * rate));
      plate_second_[b].line.init(static_cast<int>(0.4f * rate));
      plate_delay1_[b].init(line_size);
      plate_delay2_[b].init(line_size);
    }

    for (int c = 0; c < 2; ++c) {
      for (int i = 0; i < kLines; ++i)
        comb_[c][i].init(static_cast<int>(0.16f * rate));
      for (auto& allpass : vintage_allpass_[c])
        allpass.line.init(static_cast<int>(0.08f * rate));
      vintage_input_[c].line.init(static_cast<int>(0.04f * rate));
    }
    configured_ = false;
    reset();
  }

  void ReverbAlgorithms::reset() {
    for (int i = 0; i < kLines; ++i) {
      lines_[i].clear();
      line_allpass_[i].line.clear();
      damp_[i].clear();
    }
    for (auto& allpass : input_allpass_)
      allpass.line.clear();
    er_line_[0].clear();
    for (auto& allpass : plate_input_)
      allpass.line.clear();
    for (int b = 0; b < 2; ++b) {
      plate_mod_[b].line.clear();
      plate_second_[b].line.clear();
      plate_delay1_[b].clear();
      plate_delay2_[b].clear();
      plate_damp_[b].clear();
      plate_feedback_[b] = 0.0f;
      vintage_lowpass_[b] = 0.0f;
    }
    for (int c = 0; c < 2; ++c) {
      for (int i = 0; i < kLines; ++i) {
        comb_[c][i].clear();
        comb_damp_[c][i].clear();
      }
      for (auto& allpass : vintage_allpass_[c])
        allpass.line.clear();
      vintage_input_[c].line.clear();
    }
    for (int i = 0; i < kLines; ++i)
      delay_[i] = target_delay_[i];
    phase_ = 0.0f;
  }

  void ReverbAlgorithms::configure(const Settings& settings) {
    if (sample_rate_ == 0)
      return;
    const bool changed = ! configured_ || settings.type != settings_.type || (settings.type == kNitrous && settings.room != settings_.room);
    settings_ = settings;
    tune();
    if (changed) {
      configured_ = true;
      reset();
    }
  }

  void ReverbAlgorithms::tune() {
    const float rate = static_cast<float>(sample_rate_);
    const Settings& s = settings_;
    const float size_scale = std::exp2((std::min(std::max(s.size, 0.0f), 1.0f) - 0.5f) * 3.0f);
    const float decay = std::max(s.decay_seconds, 0.05f);
    const float a = std::min(std::max(s.diff_a, 0.0f), 1.0f);
    const float b = std::min(std::max(s.diff_b, 0.0f), 1.0f);
    er_scale_ = std::exp2((std::min(std::max(s.er_size, 0.0f), 1.0f) - 0.5f) * 3.0f);

    // early reflections, for every type
    for (int k = 0; k < kErTaps; ++k) {
      er_time_[0][k] = kEarlyLeft[k] * 0.001f * rate * er_scale_;
      er_time_[1][k] = kEarlyRight[k] * 0.001f * rate * er_scale_;
      er_gain_[0][k] = kEarlyGain[k];
      er_gain_[1][k] = -kEarlyGain[k];
    }

    if (s.type == kPlate) {
      plate_scale_ = rate / kPlateReferenceRate * size_scale;
      float pass_seconds = 0.0f;
      for (int branch = 0; branch < 2; ++branch)
        pass_seconds += (kPlateMod[branch] + kPlateDelay1[branch] + kPlateSecond[branch] + kPlateDelay2[branch]) / kPlateReferenceRate * size_scale;
      pass_seconds *= 0.5f;
      plate_decay_gain_ = std::min(std::exp(-kLnMilli * pass_seconds / decay), kMaxLoopGain);
      depth_samples_ = s.mod_depth * 16.0f * rate / kPlateReferenceRate;
      input_allpass_coefficient_[0] = input_allpass_coefficient_[1] = 0.45f + 0.4f * a;
      input_allpass_coefficient_[2] = input_allpass_coefficient_[3] = 0.35f + 0.4f * a;
      line_allpass_coefficient_ = 0.4f + 0.45f * b;
      er_level_ = 0.3f;
      output_scale_ = 0.75f;
      return;
    }

    if (s.type == kVintage) {
      vintage_scale_ = rate / 44100.0f * size_scale;
      for (int c = 0; c < 2; ++c)
        for (int i = 0; i < kLines; ++i) {
          comb_delay_[c][i] = (kCombs[i] + kStereoSpread * static_cast<float>(c)) * vintage_scale_;
          comb_gain_[c][i] = std::min(std::exp(-kLnMilli * comb_delay_[c][i] / rate / decay), kMaxLoopGain);
        }
      depth_samples_ = s.mod_depth * 0.0008f * rate;
      input_allpass_coefficient_[0] = input_allpass_coefficient_[1] = 0.3f + 0.4f * a;
      line_allpass_coefficient_ = 0.3f + 0.4f * b;
      er_level_ = 0.25f;
      output_scale_ = 0.14f;
      return;
    }

    // the network: Hall, Basin (short, dense, bright), Nitrous (the modes of a room)
    for (int k = 0; k < 4; ++k)
      input_ms_[k] = kHallInput[k];
    float modulation_seconds = 0.0012f;
    if (s.type == kBasin) {
      for (int i = 0; i < kLines; ++i) {
        base_delay_[i] = kBasinLines[i] * 0.001f;
        line_allpass_delay_[i] = std::max(2, static_cast<int>(kBasinAllpass[i] * 0.001f * rate));
      }
      for (int k = 0; k < 4; ++k)
        input_ms_[k] = kBasinInput[k];
      modulation_seconds = 0.0024f;
      er_level_ = 0.25f;
      output_scale_ = 1.1f;
    } else if (s.type == kNitrous) {
      const float* d = kRoomSize[std::min(std::max(s.room, 0), kNumRooms - 1)];
      const float a2 = d[0] * d[0], b2 = d[1] * d[1], c2 = d[2] * d[2];
      const float paths[kLines] = { 2.0f * d[0], 2.0f * d[1], 2.0f * d[2], 2.0f * std::sqrt(a2 + b2), 2.0f * std::sqrt(a2 + c2),
                                    2.0f * std::sqrt(b2 + c2), 2.0f * std::sqrt(a2 + b2 + c2), d[0] + d[1] + d[2] };
      for (int i = 0; i < kLines; ++i) {
        base_delay_[i] = paths[i] / kSpeedOfSound * (1.0f + 0.0113f * static_cast<float>(i)); // the cube would give identical lines
        line_allpass_delay_[i] = std::max(2, static_cast<int>(kHallAllpass[i] * 0.5f * 0.001f * rate));
      }
      modulation_seconds = 0.0006f;
      er_level_ = kRoomEarly[std::min(std::max(s.room, 0), kNumRooms - 1)];
      output_scale_ = 1.5f;
    } else {
      for (int i = 0; i < kLines; ++i) {
        base_delay_[i] = kHallLines[i] * 0.001f;
        line_allpass_delay_[i] = std::max(2, static_cast<int>(kHallAllpass[i] * 0.001f * rate));
      }
      er_level_ = 0.45f;
      output_scale_ = 1.6f;
    }

    float input_scale = 1.0f / std::sqrt(static_cast<float>(kLines));
    for (int i = 0; i < kLines; ++i) {
      target_delay_[i] = std::min(std::max(base_delay_[i] * size_scale * rate, 4.0f), kMaxDelaySeconds * rate);
      if (! configured_)
        delay_[i] = target_delay_[i];
      const float loop_seconds = (target_delay_[i] + static_cast<float>(line_allpass_delay_[i])) / rate;
      gain_[i] = std::min(std::exp(-kLnMilli * loop_seconds / decay), kMaxLoopGain);
      input_gain_[i] = sign(i, 3) * input_scale * ((i & 1) != 0 ? -1.0f : 1.0f);
      output_left_[i] = 0.5f * sign(i, 0);
      output_right_[i] = 0.5f * sign(i, 1);
    }
    for (int k = 0; k < 4; ++k)
      input_allpass_coefficient_[k] = (s.type == kBasin ? 0.55f : 0.3f) + 0.4f * a;
    line_allpass_coefficient_ = 0.2f + 0.5f * b;
    depth_samples_ = s.mod_depth * modulation_seconds * rate;

    // the damping of the type: Basin and Marble stay bright, Space is darker
    if (s.type == kBasin)
      settings_.high_amplitude = 1.0f - (1.0f - s.high_amplitude) * 0.35f;
    else if (s.type == kNitrous)
      settings_.high_amplitude = 1.0f - (1.0f - s.high_amplitude) * kRoomBrightness[std::min(std::max(s.room, 0), kNumRooms - 1)];
  }

  void ReverbAlgorithms::process(float in, float& left, float& right) {
    left = right = 0.0f;
    if (! configured_)
      return;

    // the early reflections of every type, from the input itself
    er_line_[0].push(in);
    float early_left = 0.0f, early_right = 0.0f;
    for (int k = 0; k < kErTaps; ++k) {
      early_left += er_gain_[0][k] * er_line_[0].read(std::max(er_time_[0][k], 2.0f));
      early_right += er_gain_[1][k] * er_line_[0].read(std::max(er_time_[1][k], 2.0f));
    }
    early_left *= er_level_ * 0.5f;
    early_right *= er_level_ * 0.5f;

    switch (settings_.type) {
      case kPlate:
        processPlate(in, left, right);
        break;
      case kVintage:
        processVintage(in, left, right);
        break;
      default:
        processNetwork(in, left, right);
        break;
    }
    left += early_left;
    right += early_right;
  }

  void ReverbAlgorithms::processNetwork(float in, float& left, float& right) {
    const Settings& s = settings_;
    const float rate = static_cast<float>(sample_rate_);

    float x = in;
    for (int k = 0; k < 4; ++k)
      x = input_allpass_[k].process(x, input_allpass_coefficient_[k], std::max(2, static_cast<int>(input_ms_[k] * 0.001f * rate)));

    phase_ += s.mod_hz / rate;
    phase_ -= std::floor(phase_);

    float y[kLines];
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < kLines; ++i) {
      delay_[i] += (target_delay_[i] - delay_[i]) * kSlew;
      const float lfo = std::sin(kTwoPi * (phase_ + static_cast<float>(i) * 0.125f));
      y[i] = lines_[i].read(delay_[i] + depth_samples_ * lfo);
      l += output_left_[i] * y[i];
      r += output_right_[i] * y[i];
    }

    // the feedback matrix: an orthonormal 8 x 8 Walsh-Hadamard transform
    float m[kLines];
    for (int i = 0; i < kLines; i += 2) {
      m[i] = y[i] + y[i + 1];
      m[i + 1] = y[i] - y[i + 1];
    }
    for (int i = 0; i < kLines; i += 4)
      for (int j = 0; j < 2; ++j) {
        const float u = m[i + j], v = m[i + j + 2];
        m[i + j] = u + v;
        m[i + j + 2] = u - v;
      }
    for (int j = 0; j < 4; ++j) {
      const float u = m[j], v = m[j + 4];
      m[j] = u + v;
      m[j + 4] = u - v;
    }
    const float matrix_scale = 0.35355339059f; // 1 / sqrt (8)

    for (int i = 0; i < kLines; ++i) {
      float v = m[i] * matrix_scale + input_gain_[i] * x;
      v = line_allpass_[i].process(v, line_allpass_coefficient_, line_allpass_delay_[i]);
      v = damp_[i].process(v, s);
      lines_[i].push(v * gain_[i]);
    }
    left = l * output_scale_;
    right = r * output_scale_;
  }

  void ReverbAlgorithms::processPlate(float in, float& left, float& right) {
    const Settings& s = settings_;
    const float rate = static_cast<float>(sample_rate_);
    const float scale = plate_scale_;

    float x = in * 0.5f;
    x = plate_input_[0].process(x, input_allpass_coefficient_[0], std::max(2, static_cast<int>(kPlateInput[0] * scale)));
    x = plate_input_[1].process(x, input_allpass_coefficient_[1], std::max(2, static_cast<int>(kPlateInput[1] * scale)));
    x = plate_input_[2].process(x, input_allpass_coefficient_[2], std::max(2, static_cast<int>(kPlateInput[2] * scale)));
    x = plate_input_[3].process(x, input_allpass_coefficient_[3], std::max(2, static_cast<int>(kPlateInput[3] * scale)));

    phase_ += s.mod_hz / rate;
    phase_ -= std::floor(phase_);

    float branch_out[2];
    for (int b = 0; b < 2; ++b) {
      const float lfo = std::sin(kTwoPi * (phase_ + 0.5f * static_cast<float>(b)));
      float v = x + plate_feedback_[1 - b];
      v = plate_mod_[b].processModulated(v, -line_allpass_coefficient_, kPlateMod[b] * scale + depth_samples_ * lfo);
      plate_delay1_[b].push(v);
      v = plate_delay1_[b].read(std::max(2.0f, kPlateDelay1[b] * scale));
      v = plate_damp_[b].process(v, s) * plate_decay_gain_;
      v = plate_second_[b].process(v, line_allpass_coefficient_ * 0.75f, std::max(2, static_cast<int>(kPlateSecond[b] * scale)));
      plate_delay2_[b].push(v);
      branch_out[b] = plate_delay2_[b].read(std::max(2.0f, kPlateDelay2[b] * scale));
    }
    plate_feedback_[0] = branch_out[0];
    plate_feedback_[1] = branch_out[1];

    const auto tap = [&] (const Line& line, float position) { return line.tap(std::max(1, static_cast<int>(position * scale))); };
    left = tap(plate_delay1_[0], 266.0f) + tap(plate_delay1_[0], 2974.0f) - tap(plate_mod_[0].line, 1213.0f * 0.5f)
         + tap(plate_delay2_[0], 1996.0f) - tap(plate_delay1_[1], 1990.0f) - tap(plate_second_[1].line, 187.0f)
         - tap(plate_delay2_[1], 1066.0f);
    right = tap(plate_delay1_[1], 353.0f) + tap(plate_delay1_[1], 3627.0f) - tap(plate_second_[1].line, 1228.0f)
          + tap(plate_delay2_[1], 2673.0f) - tap(plate_delay1_[0], 2111.0f) - tap(plate_second_[0].line, 335.0f)
          - tap(plate_delay2_[0], 121.0f);
    left *= output_scale_;
    right *= output_scale_;
  }

  void ReverbAlgorithms::processVintage(float in, float& left, float& right) {
    const Settings& s = settings_;
    const float rate = static_cast<float>(sample_rate_);

    phase_ += s.mod_hz / rate;
    phase_ -= std::floor(phase_);

    const float lowpass = 1.0f - std::exp(-kTwoPi * 7000.0f / rate);
    float out[2];
    for (int c = 0; c < 2; ++c) {
      float x = in;
      x = vintage_input_[c].process(x, input_allpass_coefficient_[0], std::max(2, static_cast<int>((kVintageInput[0] + kStereoSpread * static_cast<float>(c)) * vintage_scale_)));
      float sum = 0.0f;
      for (int i = 0; i < kLines; ++i) {
        const float lfo = std::sin(kTwoPi * (phase_ + static_cast<float>(i) * 0.125f + 0.5f * static_cast<float>(c)));
        const float y = comb_[c][i].read(comb_delay_[c][i] + depth_samples_ * lfo);
        const float filtered = comb_damp_[c][i].process(y, s);
        comb_[c][i].push(x + filtered * comb_gain_[c][i]);
        sum += y;
      }
      float o = sum;
      for (int k = 0; k < 4; ++k)
        o = vintage_allpass_[c][k].process(o, line_allpass_coefficient_,
                                           std::max(2, static_cast<int>((kVintageAllpass[k] + kStereoSpread * static_cast<float>(c)) * vintage_scale_)));
      vintage_lowpass_[c] += lowpass * (o - vintage_lowpass_[c]);
      out[c] = vintage_lowpass_[c];
    }
    left = out[0] * output_scale_;
    right = out[1] * output_scale_;
  }
} // namespace sbvital
