// SynthyBoy (AGPL-3.0-or-later combined work), 2026: the reverb algorithms of docs/ui-design/13-new-keys.md 4.17 (G77).
//
// Five reverbs next to the feedback delay network of the Vital reverb (type 0, which stays in reverb.cpp):
//   Plate    after Dattorro's plate (J. Dattorro, "Effect Design, Part 1", JAES 1997): four input all-passes into a figure-of-eight tank of two
//            branches with a modulated all-pass, a delay, a damping filter and a second all-pass each, fourteen output taps
//   Hall     eight-line feedback delay network (Hadamard feedback matrix), long lines, an all-pass in every line, early reflections
//   Vintage  eight parallel feedback combs and four series all-passes per channel (Schroeder / Moorer), darker output
//   Nitrous  the same network with short, dense lines, a bright damping and strong modulation
//   Basin    the network with the line lengths of the modes of a room: Space, Marble, Rectangle, Hexagon or Box
// All of them are mono in, stereo out, sample-rate independent, have no allocation in process() and use the same damping shelves, size,
// decay (T60), diffusion, early reflection size and modulation controls of the Vital reverb.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace sbvital {

  class ReverbAlgorithms {
    public:
      enum Type { kVital, kPlate, kHall, kVintage, kNitrous, kBasin, kNumTypes };
      enum Room { kSpace, kMarble, kRectangle, kHexagon, kBox, kNumRooms };

      static constexpr int kLines = 8;
      static constexpr int kErTaps = 6;

      /** The controls of one block. */
      struct Settings {
        int type = kPlate;
        int room = kRectangle;
        float size = 0.5f;            // 0 .. 1
        float decay_seconds = 1.5f;   // T60
        float diff_a = 0.7f;          // 0 .. 1: input diffusion
        float diff_b = 0.6f;          // 0 .. 1: diffusion inside the loops
        float er_size = 0.5f;         // 0 .. 1
        float mod_depth = 0.2f;       // 0 .. 1
        float mod_hz = 0.25f;
        float high_coefficient = 0.1f, high_amplitude = 0.9f;   // the damping shelves of the Vital reverb
        float low_coefficient = 0.01f, low_amplitude = 0.0f;
      };

      ReverbAlgorithms();

      /** Allocates every buffer (not real time). */
      void prepare(int sample_rate);
      /** Clears the state. */
      void reset();

      /** Takes the settings of a block; a change of the type or of the room clears the state first. */
      void configure(const Settings& settings);

      /** One sample. `in` is the mono input, `left` and `right` receive the wet output. */
      void process(float in, float& left, float& right);

    private:
      // ---- building blocks -----------------------------------------------------------------------------------------------------------
      struct Line {
        std::vector<float> buffer;
        unsigned mask = 0, write = 0;

        void init(int minimum_size);
        void clear();
        void push(float x) { buffer[write] = x; write = (write + 1) & mask; }
        /** The value pushed `delay` writes ago (1 = the last one). */
        float tap(int delay) const { return buffer[(write - static_cast<unsigned>(delay)) & mask]; }
        /** Four-point interpolated read, delay >= 2. */
        float read(float delay) const;
      };

      struct Allpass {
        Line line;
        float process(float x, float gain, int delay) {
          const float z = line.tap(delay);
          const float v = x - gain * z;
          line.push(v);
          return z + gain * v;
        }
        float processModulated(float x, float gain, float delay) {
          const float z = line.read(delay);
          const float v = x - gain * z;
          line.push(v);
          return z + gain * v;
        }
      };

      struct Damp {
        float high = 0.0f, low = 0.0f;
        float process(float x, const Settings& s) {
          float delta = s.high_coefficient * (x - high);
          high += delta;
          const float high_filtered = high;
          high += delta;
          x = high_filtered + s.high_amplitude * (x - high_filtered);
          delta = s.low_coefficient * (x - low);
          low += delta;
          const float low_filtered = low;
          low += delta;
          return x - low_filtered * s.low_amplitude;
        }
        void clear() { high = low = 0.0f; }
      };

      void tune();
      void processNetwork(float in, float& left, float& right);
      void processPlate(float in, float& left, float& right);
      void processVintage(float in, float& left, float& right);

      int sample_rate_;
      Settings settings_;
      bool configured_;

      // ---- the network (Hall, Nitrous, Basin) ----------------------------------------------------------------------------------------
      Line lines_[kLines];
      Allpass line_allpass_[kLines];
      Damp damp_[kLines];
      Allpass input_allpass_[4];
      Line er_line_[2];
      float base_delay_[kLines];           // seconds at size 1, from the tuning of the type
      float delay_[kLines];                // samples, slewed towards the target
      float target_delay_[kLines];
      float gain_[kLines];
      int line_allpass_delay_[kLines];
      float input_gain_[kLines];
      float output_left_[kLines], output_right_[kLines];
      float er_time_[2][kErTaps], er_gain_[2][kErTaps];
      float er_scale_, er_level_;
      float input_allpass_coefficient_[4];
      float input_ms_[4];
      float line_allpass_coefficient_;
      float depth_samples_;
      float phase_;

      // ---- the plate ----------------------------------------------------------------------------------------------------------------
      Allpass plate_input_[4];
      Allpass plate_mod_[2], plate_second_[2];
      Line plate_delay1_[2], plate_delay2_[2];
      Damp plate_damp_[2];
      float plate_feedback_[2];
      float plate_decay_gain_;
      float plate_scale_;

      // ---- the combs ---------------------------------------------------------------------------------------------------------------
      Line comb_[2][kLines];
      Damp comb_damp_[2][kLines];
      Allpass vintage_allpass_[2][4];
      Allpass vintage_input_[2];
      float comb_delay_[2][kLines];
      float comb_gain_[2][kLines];
      float vintage_scale_;
      float vintage_lowpass_[2];

      float output_scale_;
  };
} // namespace sbvital
