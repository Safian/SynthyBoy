// SynthyBoy (AGPL-3.0-or-later): the sample oscillator type of an oscillator slot (docs/ui-design/13-new-keys.md 4.9).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A new file in the engine-vital fork (not part of the imported Vital sources, listed in docs/UPSTREAM.md). It replaces the wavetable
// SynthOscillator inside an OscillatorModule while `osc_N_type` is 1, with the same inputs (note, transpose, tune, unison, level, pan,
// phase) and the same two outputs (raw, levelled), so the rest of the voice (filters, destinations, mixer) is untouched. The audio is
// rendered by the standalone package core/sample-osc (sampleosc::SampleVoice), one voice per unison voice of each of the two Vital voices
// a processor instance serves.
#pragma once

#include <array>
#include <cstdint>

#include "processor.h"
#include "synthyboy/granular/granular.h"
#include "synthyboy/sampleosc/sample_types.h"
#include "synthyboy/sampleosc/sample_voice.h"

namespace sbvital {
  /** The settings and the sample of one oscillator slot: ONE object per slot, shared by all clones of the oscillator (like the wavetable).
      Written by the adapter on the audio thread between two blocks, read by the processors on the same thread. */
  struct SampleOscShared {
    static constexpr int kMaxSlices = 130;

    const synthyboy::sampleosc::SampleView* view = nullptr; // null: no sample, the oscillator is silent
    float normalize_gain = 1.0f;                            // peak normalisation gain of the whole file
    int num_slices = 0;                                     // slice starts, ascending fractions, [0] = 0
    float slices[kMaxSlices] = {};

    float start = 0.0f, end = 1.0f;                         // playback region, fractions of the file
    int root = 60;
    bool keytrack = true;
    int loop_mode = 0;                                      // 0 one shot, 1 forward, 2 reverse, 3 forward/reverse, 4 tailed
    float loop_start = 0.0f, loop_end = 1.0f;
    float loop_xfade_ms = 0.0f;
    bool loop_relative = false;
    bool loop_exit = false;
    float fade_ms = 0.0f;
    bool normalize = false;
    bool reverse = false;
    int slice_mode = 0;                                     // 0 off, 1 auto, 2 manual
    int slice_play = 0;                                     // 0 slice to end, 1 single slice
    int slice_root = 36;
    int scan_range = 0;                                     // 0..3: x1, x2, x4, x8
    bool scan_reverse = false;
    bool scan_lock_tempo = false;
    bool scan_to_bpm = false;
    float length_beats = 4.0f;
    double bpm = 120.0;
    int quality = 2;                                        // 0 low, 1 medium, 2 high (interpolation of the sample voices)
    int max_unison = 16;

    // GROUPC WP-11 (13-new-keys.md 4.37): the static keys of the granular oscillator (the modulatable ones are inputs of the processor)
    struct Granular {
      int window_shape = 0;                                 // synthyboy::granular::WindowShape
      int density_mode = 0;                                 // 0 free (Hz), 1 BPM sync, 2 grains
      int density_sync = 12;                                // index of kGrainDivisions
      int density_grains = 8;
      int length_mode = 0;                                  // 0 free (ms), 1 BPM sync, 2 percent of the density period
      int length_sync = 12;
      bool jump_start = true;
      int max_grains = 64;
      bool loop_grains = false;
      bool manual = false;
      bool scan_keytrack = false;
      bool reverse_grains = false;
      int spawn_pattern = 0;                                // 0 together, 1 even, 2 exp, 3 random
      int y_axis = 0;                                       // 0 none, 1 level, 2 warp, 3 warp 2, 4 density, ... (kGrainYAxes)
    } gran;
  };

  class SampleOscillator : public Processor {
    public:
      enum {
        kMidiNote,
        kTranspose,
        kTune,
        kAmplitude,
        kPan,
        kUnisonVoices,
        kUnisonDetune,
        kDetunePower,
        kDetuneRange,
        kPhase,
        kRandomPhase,
        kBlend,
        kStereoSpread,
        kActiveVoices,
        kReset,
        kFoldNote,
        kNotePressed,
        kScan,
        kUniStart,
        kUniSpan,
        kWarpType,
        kWarpAmount,
        kWarp2Type,
        kWarp2Amount,
        kRawMidiNote,      // the note without the bend (pitch bend tracking, Serum 2 manual p.36)
        kMidiTrack,        // oscN.pitch_track: 0 = the recorded pitch, whatever the key
        kPitchBendTrack,   // oscN.pitch_bend_track: 1 = the bend still moves an oscillator whose pitch tracking is off
        kSlice,            // GROUPB WP-05: oscN.sample.slice, -1 .. 1: slices added to the slice of the key in Play Single Slice mode
        kType,             // GROUPC: oscN.type, 1 Sample, 2 Granular, 3 Spectral, 4 Multisample (the module runs this processor for 1 .. 4)
        // GROUPC WP-11 (4.37): the modulatable keys of the granular oscillator
        kGranWindowAmount, kGranWindowSkew, kGranDensity, kGranLength, kGranLengthPercent, kGranPosition, kGranRandOffset, kGranRandDir, kGranRandPitch,
        kGranRandLength, kGranRandPan, kGranRandLevel, kGranRandWindow, kGranRandSkew, kGranRandWarp, kGranRandWarp2, kGranXyY,
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      static constexpr int kMaxUnison = 16;
      static constexpr int kVoicesPerInstance = 2;  // two Vital voices per poly_float

      explicit SampleOscillator(const SampleOscShared* shared);

      void process(int num_samples) override;
      Processor* clone() const override { return new SampleOscillator(*this); }

      /** The sample follows the key: the sample's own keytrack switch and the pitch tracking of the oscillator (Serum 2 manual p.35) are on. */
      bool tracksPitch(int lane) const { return shared_->keytrack && input(kMidiTrack)->at(0)[lane] > 0.5f; }

      void setFirstOscillatorOutput(Output* oscillator) { first_mod_oscillator_ = oscillator; }
      void setSecondOscillatorOutput(Output* oscillator) { second_mod_oscillator_ = oscillator; }
      void setSampleOutput(Output* sample) { sample_ = sample; }
      /** GROUPB WP-05: the tap that carries this oscillator's pitch (MIDI notes per voice) to the Ratio SRC of the others; shared with the wavetable oscillator of the module. */
      void setPitchTap(std::shared_ptr<Output> tap) { pitch_tap_ = std::move(tap); }
      /** GROUPB WP-06: the extra warp sources, the Sub (1), Filter 1 (2) and Filter 2 (3) (0 is the noise: setSampleOutput); a cell shared with the clones. */
      void setExtraWarpSource(int index, const Output* source) { (*extra_sources_)[static_cast<size_t>(index)] = source; }

      void setOversampleAmount(int oversample) override;
      void reset(poly_mask reset_mask) override;

    private:
      struct Slot {
        synthyboy::sampleosc::SampleVoice voices[kMaxUnison];
        synthyboy::granular::GranularVoice gvoices[kMaxUnison];      // GROUPC WP-11: one grain cloud per unison voice
        int kind = 1;                                                // the oscillator type this slot's voices were started for
        int old_kind = 0;                                            // GROUPC: the kind that fades out after a change of the type under a running note (0: none)
        int fade_pos = 0, fade_len = 1;
        float start_random[kMaxUnison] = {};
        bool running = false;
        bool pressed = false;
        bool released = false;
        int unison = 1;
        float region_lo = 0.0f, region_hi = 1.0f;
        bool sliced = false;
        const synthyboy::sampleosc::SampleView* view = nullptr; // the sample the running voices read
      };

      void noteOn(Slot& slot, int lane, int unison, int num_samples);
      void startKind(Slot& slot, int kind, int lane, int unison);
      void stopKind(Slot& slot, int kind);
      bool kindActive(const Slot& slot, int kind) const;
      void renderSlot(Slot& slot, int kind, int slot_index, int unison, int num_samples, poly_float* raw, float ramp0, float ramp1);
      void buildGrainParams(const Slot& slot, int lane, int unison, synthyboy::granular::GrainParams& gp, float& level, float* warp_amounts, bool tracks) const;
      uint32_t nextRandom() noexcept;

      const SampleOscShared* shared_;
      Slot slots_[kVoicesPerInstance];
      uint32_t random_state_ = 0x9e3779b9u;
      int prepared_rate_ = 0;
      Output* first_mod_oscillator_ = nullptr;
      Output* second_mod_oscillator_ = nullptr;
      Output* sample_ = nullptr;
      int scratch_size_ = 0;
      std::shared_ptr<float[]> scratch_;
      std::shared_ptr<Output> pitch_tap_;
      std::shared_ptr<std::array<const Output*, 4>> extra_sources_ = std::make_shared<std::array<const Output*, 4>>();
  };
} // namespace sbvital
