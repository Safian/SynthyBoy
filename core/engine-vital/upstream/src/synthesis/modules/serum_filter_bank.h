// SynthyBoy (AGPL-3.0-or-later): the filter models of state version 3 (docs/ui-design/13-new-keys.md 4.11), rendered by core/filters-serum.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// New file in the engine-vital fork (not part of the imported Vital sources, listed in docs/UPSTREAM.md). One processor instance serves two
// voices (the four lanes L, R, L, R of a poly_float): one scalar filters::Filter per lane.
#pragma once

#include <memory>

#include "processor.h"
#include "synthyboy/filters/filter.h"

namespace sbvital {
  /** The drawn magnitude curve of a PZ SVF (content.filter_curves, docs/ui-design/13-new-keys.md A28), fitted to a cascade of bell filters:
      one per point, centre frequency, gain and Q, calculated on the message thread. One object per filter slot, shared by the voice clones. */
  struct FilterCurveShared {
    static constexpr int kMaxBells = 16;
    int num_bells = 0;
    float frequency[kMaxBells] = {};    // Hz at a cutoff of 1 kHz
    float gain_db[kMaxBells] = {};
    float q[kMaxBells] = {};

    /** Fits the points (x = log frequency 0 .. 1 over 20 Hz .. 20 kHz, y = dB). Not real-time. */
    void fit(const float* x, const float* y, int count);
  };

  /** cutoff (MIDI note, audio rate) + pan x 12 semitones with opposite signs in the left and right lane (docs/ui-design/13-new-keys.md 4.11). */
  class StereoCutoffOffset : public Processor {
    public:
      enum { kCutoff, kPan, kNumInputs };

      StereoCutoffOffset() : Processor(kNumInputs, 1) { }
      void process(int num_samples) override;
      Processor* clone() const override { return new StereoCutoffOffset(*this); }
  };

  /** Clean Mode (docs/ui-design/13-new-keys.md 4.15, Serum 2 manual p.142): "the filter pre-gain stage the filter input -24 dB (with a +24 dB
      boost post-filter)". This processor is the pre-gain: it copies the audio, scaled by -24 dB while the clean switch is on and the model is
      not Comb / Formant (they have no drive stage); off it is the identity. The gain is ramped over the block. FilterModule applies the +24 dB. */
  class CleanInputGain : public Processor {
    public:
      enum { kAudio, kClean, kModel, kNumInputs };

      static constexpr mono_float kCleanDb = 24.0f;

      CleanInputGain() : Processor(kNumInputs, 1), gain_(1.0f) { }
      void process(int num_samples) override;
      Processor* clone() const override { return new CleanInputGain(*this); }

    private:
      mono_float gain_;
  };

  class SerumFilterBank : public Processor {
    public:
      enum {
        kAudio,
        kReset,
        kMidiCutoff,   // MIDI note of the cutoff, audio rate
        kResonance,
        kDrive,        // dB, 0 .. 20
        kBlend,        // 0 .. 2
        kVar,          // 0 .. 1
        kModel,        // filter model of the table: 8 .. 21
        kStyle,        // style inside the model
        kNumInputs
      };

      static constexpr int kFirstModel = 8;
      static constexpr int kLastModel = 21;

      SerumFilterBank();
      SerumFilterBank(const SerumFilterBank& other);
      ~SerumFilterBank() override;

      void process(int num_samples) override;
      void reset(poly_mask reset_mask) override;
      Processor* clone() const override { return new SerumFilterBank(*this); }
      void setSampleRate(int sample_rate) override;
      void setOversampleAmount(int oversample) override;

      /** The filters::FilterType of a model and style of the table (a style beyond the model clamps to its last). */
      static int filterTypeOf(int model, int style);

      FilterCurveShared* curve() { return curve_.get(); }

    private:
      void prepareFilters();
      void processCurve(int num_samples);

      std::unique_ptr<synthyboy::filters::Filter> filters_[poly_float::kSize];
      int type_ = -1;
      std::shared_ptr<FilterCurveShared> curve_;
      float bell_state_[poly_float::kSize][FilterCurveShared::kMaxBells][2] = {};
      int prepared_rate_ = 0;
      std::shared_ptr<float[]> scratch_;
  };
} // namespace sbvital
