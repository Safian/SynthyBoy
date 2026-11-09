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

#include <array>

#include "spectral_morph.h"
#include "synth_constants.h"
#include "utils.h"
#include "wave_frame.h"
#include "wavetable.h"

namespace sbvital {

  class FourierTransform;
  class Wavetable;

  struct PhaseBuffer {
    poly_int buffer[kMaxBufferSize * kMaxOversample];
  };

  class RandomValues {
    public:
      static constexpr int kSeed = 0x4;

      static RandomValues* instance() {
        int size = (kRandomAmplitudeStages + 1) * (Wavetable::kNumHarmonics + 1) / poly_float::kSize;
        static RandomValues instance(size);
        return &instance;
      }

      const poly_float* buffer() { return data_.get(); }

    private:
      RandomValues(int num_poly_floats) {
        data_ = std::make_unique<poly_float[]>(num_poly_floats);
        utils::RandomGenerator generator(-1.0f, 1.0f);
        generator.seed(kSeed);
        for (int i = 0; i < num_poly_floats; ++i)
          data_[i] = generator.polyNext();
      }

      std::unique_ptr<poly_float[]> data_;
  };

  class SynthOscillator : public Processor {
    public:
      enum {
        kWaveFrame,
        kMidiNote,
        kMidiTrack,
        kSmoothlyInterpolate,
        kTranspose,
        kTransposeQuantize,
        kTune,
        kAmplitude,
        kPan,
        kUnisonVoices,
        kUnisonDetune,
        kPhase,
        kDistortionPhase,
        kRandomPhase,
        kBlend,
        kStereoSpread,
        kStackStyle,
        kDetunePower,
        kDetuneRange,
        kUnisonFrameSpread,
        kUnisonDistortionSpread,
        kUnisonSpectralMorphSpread,
        kSpectralMorphType,
        kSpectralMorphAmount,
        kSpectralUnison,
        kDistortionType,
        kDistortionAmount,
        kActiveVoices,
        kReset,
        kRetrigger,
        kFoldNote,  // SynthyBoy: semitones added to the note this oscillator plays (key fold of the oscillator mapping), 0 unplugged
        kWarp2Type,    // SynthyBoy: the second warp slot (docs/ui-design/13-new-keys.md 4.9), applied after the first
        kWarp2Amount,
        kWarp2Phase,
        kWarp2Spread,
        // SynthyBoy Block B14: Pitch and unison modes, transpose snap, phase legato
        kPitchMode,
        kPitchCoarse,
        kFixedPitch,
        kPitchBendTrack,
        kRawMidiNote,
        kUnisonMode,
        kUnisonBlendShape,
        kUnisonStackRepeat,
        kPhaseLegato,
        kPhaseMemory,
        kVoicePhaseLegato,
        kNoteCount,
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      enum SpectralMorph {
        kNoSpectralMorph,
        kVocode,
        kFormScale,
        kHarmonicScale,
        kInharmonicScale,
        kSmear,
        kRandomAmplitudes,
        kLowPass,
        kHighPass,
        kPhaseDisperse,
        kShepardTone,
        kSkew,
        kNumSpectralMorphTypes
      };

      enum DistortionType {
        kNone,
        kSync,
        kFormant,
        kQuantize,
        kBend,
        kSqueeze,
        kPulseWidth,
        kFmOscillatorA,
        kFmOscillatorB,
        kFmSample,
        kRmOscillatorA,
        kRmOscillatorB,
        kRmSample,
        // SynthyBoy block B21 (docs/ui-design/13-new-keys.md 4.22): the Serum 2 warp modes, appended to the shared list of both slots
        kSyncVar,        // Sync with a soft edge at the reset (the VAR fader is the warp phase knob)
        kBendPlus, kBendMinus, kBendPlusMinus,
        kAsymPlus, kAsymMinus, kAsymPlusMinus,
        kFlip, kMirror,
        kRemap1, kRemap2, kRemap3, kRemap4,
        kOddEven,
        kFilterLow, kFilterHigh,
        kShTube, kShSoftClip, kShHardClip, kShDiode1, kShDiode2, kShLinearFold, kShSineFold, kShZeroSquare, kShAsym, kShRectify,
        kShSineShaper, kShStompBox, kShTapeSat, kShSoftSat,
        kFmThruZeroA, kFmThruZeroB, kFmExpA, kFmExpB, kFmLinearA, kFmLinearB,
        kPdA, kPdB, kPdSelf,
        kAmA, kAmB,
        // GROUPB WP-06 (docs/ui-design/13-new-keys.md 4.34): FM Thru-Zero / Exp / Linear, PD, AM and RM from the Noise, the Sub, Filter 1 and Filter 2 (six
        // families of four sources; the modes of the other oscillators and Vital's FM / RM from the sample are above)
        kSrcFmThruZeroNoise, kSrcFmThruZeroSub, kSrcFmThruZeroFilter1, kSrcFmThruZeroFilter2,
        kSrcFmExpNoise, kSrcFmExpSub, kSrcFmExpFilter1, kSrcFmExpFilter2,
        kSrcFmLinearNoise, kSrcFmLinearSub, kSrcFmLinearFilter1, kSrcFmLinearFilter2,
        kSrcPdNoise, kSrcPdSub, kSrcPdFilter1, kSrcPdFilter2,
        kSrcAmNoise, kSrcAmSub, kSrcAmFilter1, kSrcAmFilter2,
        kSrcRmNoise, kSrcRmSub, kSrcRmFilter1, kSrcRmFilter2,
        // GROUPC WP-12 (docs/ui-design/13-new-keys.md 4.38): the spectral warps, acting on the spectral oscillator only (the oscillators of the other types ignore them)
        kSpecComb, kSpecAddHarmonics, kSpecAddSubharmonics, kSpecDetune, kSpecShepardNarrow, kSpecShepardFilter, kSpecPeakOctaveDown, kSpecPeakOctaveUp,
        kSpecPeakHarmonicDown, kSpecPeakHarmonicUp, kSpecGate, kSpecPitchShift, kSpecPhaseTwist, kSpecSpread, kSpecShift, kSpecSmear, kSpecMirror,
        kSpecMaskFirst, kSpecMaskSecond, kSpecMaskNoise, kSpecVocodeFirst, kSpecVocodeSecond, kSpecVocodeNoise,
        kNumDistortionTypes
      };

      static bool isSourceWarp(int type) { return type >= kSrcFmThruZeroNoise && type < kSpecComb; }
      /** GROUPC WP-12: a spectral warp (index 0 = kSpecComb, the order of synthyboy::spectral::Warp). */
      static bool isSpectralWarp(int type) { return type >= kSpecComb && type < kNumDistortionTypes; }
      /** 0 FM Thru-Zero, 1 FM Exp, 2 FM Linear, 3 PD, 4 AM, 5 RM */
      static int sourceWarpFamily(int type) { return (type - kSrcFmThruZeroNoise) / 4; }
      /** 0 Noise, 1 Sub, 2 Filter 1, 3 Filter 2 */
      static int sourceWarpSource(int type) { return (type - kSrcFmThruZeroNoise) % 4; }

      static bool isExtendedWarp(int type) { return type >= kSyncVar; }
      /** One sample through a distortion mode of block B21 (kShTube .. kShSoftSat) at the warp amount 0..1; used by the sample oscillator. */
      static float warpShape(int type, float value, float amount);

      enum UnisonStackType {
        kNormal,
        kCenterDropOctave,
        kCenterDropOctave2,
        kOctave,
        kOctave2,
        kPowerChord,
        kPowerChord2,
        kMajorChord,
        kMinorChord,
        kHarmonicSeries,
        kOddHarmonicSeries,
        kNumUnisonStackTypes
      };

      static bool isFirstModulation(int type) {
        return type == kFmOscillatorA || type == kRmOscillatorA;
      }

      static bool isSecondModulation(int type) {
        return type == kFmOscillatorB || type == kRmOscillatorB;
      }

      static constexpr int kMaxUnison = 16;
      static constexpr int kPolyPhasePerVoice = kMaxUnison / poly_float::kSize;
      static constexpr int kNumPolyPhase = kMaxUnison / 2;
      static constexpr int kNumBuffers = kNumPolyPhase * poly_float::kSize;
      static constexpr int kSpectralBufferSize = Wavetable::kWaveformSize * 2 / poly_float::kSize + poly_float::kSize;
      static const mono_float kStackMultipliers[kNumUnisonStackTypes][kNumPolyPhase];

      /** State a warp mode of block B21 keeps from sample to sample (per lane): the integrated modulator of FM Thru-Zero / Exp / Linear and the
          two one-pole stages of the filter warps. Reset at note on. */
      struct WarpState {
        poly_int acc = 0;
        poly_float lp1 = 0.0f;
        poly_float lp2 = 0.0f;
      };

      /** The per block data of one extended warp slot (slot 1 is stage 0, slot 2 is stage 1). type is 0 when the slot is None or one of the
          original Vital modes (those run through the original code path). */
      struct WarpStage {
        int type = 0;
        int side = 0;                  // 0 both voices active, 1 only the left voice, 2 only the right voice (modulator sources)
        poly_float var = 0.5f;         // Sync Var: the VAR fader 0..1
        const mono_float* remap = nullptr; // Remap 1-4: the 257 values of the curve of the oscillator
        const poly_float* mod = nullptr;
      };

      struct VoiceBlock {
        VoiceBlock();

        bool isStatic() const {
          return memcmp(from_buffers, to_buffers, poly_float::kSize * sizeof(mono_float*)) == 0;
        }

        int start_sample;
        int end_sample;
        int total_samples;

        poly_int phase;
        poly_float phase_inc_mult;
        poly_float from_phase_inc_mult;
        poly_mask shepard_double_mask;
        poly_mask shepard_half_mask;
        poly_int distortion_phase;
        poly_int last_distortion_phase;
        poly_float distortion;
        poly_float last_distortion;
        // SynthyBoy: second warp slot
        int warp2_type;
        int warp2_side;               // 0 both voices active, 1 only the left voice, 2 only the right voice (FM / RM sources)
        poly_int distortion_phase2;
        poly_int last_distortion_phase2;
        poly_float distortion2;
        poly_float last_distortion2;
        const poly_float* modulation_buffer2;
        WarpStage stage[2];            // SynthyBoy B21
        WarpState state[2];

        int num_buffer_samples;
        poly_int current_buffer_sample;

        bool smoothing_enabled;
        SpectralMorph spectral_morph;
        const poly_float* modulation_buffer;
        const poly_float* phase_inc_buffer;
        const poly_int* phase_buffer;

        const mono_float* from_buffers[poly_float::kSize];
        const mono_float* to_buffers[poly_float::kSize];
      };

      static void setDistortionValues(DistortionType distortion_type, poly_float* values, int num_values, bool spread);
      static void setSpectralMorphValues(SpectralMorph spectral_morph,
                                         poly_float* values, int num_values, bool spread);
      static void runSpectralMorph(SpectralMorph morph_type, float morph_amount,
                                   const Wavetable::WavetableData* wavetable_data,
                                   int wavetable_index, poly_float* dest, FourierTransform* transform);
      static sbvital::poly_int adjustPhase(DistortionType distortion_type, poly_int phase,
                                         poly_float distortion_amount, poly_int distortion_phase);
      static sbvital::poly_float getPhaseWindow(DistortionType distortion_type, poly_int phase,
                                              poly_int distorted_phase);
      static poly_float interpolate(const mono_float* buffers, const poly_int indices);
      static bool usesDistortionPhase(DistortionType distortion_type);

      SynthOscillator(Wavetable* wavetable);

      void reset(poly_mask reset_mask, poly_int sample);
      void reset(poly_mask reset_mask) override;
      void setSpectralMorphValues(SpectralMorph spectral_morph);
      void setDistortionValues(DistortionType distortion_type);
      void setDistortion2Values(DistortionType distortion_type);
      void process(int num_samples) override;
      Processor* clone() const override { return new SynthOscillator(*this); }

      void setFirstOscillatorOutput(Output* oscillator) { first_mod_oscillator_ = oscillator; }
      void setSecondOscillatorOutput(Output* oscillator) { second_mod_oscillator_ = oscillator; }
      void setSampleOutput(Output* sample) { sample_ = sample; }
      // Shared with the clones (one per voice pair): the voice handler sets the table on the original once
      void setNoteTable(const mono_float* table) { *note_table_ = table; }
      // SynthyBoy (GROUPB, WP-05): Ratio SRC. pitchTap () holds, per voice, the pitch of this oscillator in MIDI notes (note + fold +
      // transposition + tune + the coarse term of the pitch mode) as of the last block; an oscillator in Ratio mode whose source is
      // set takes that pitch instead of the note of its own voice. The cell is shared with the clones (one per voice pair).
      std::shared_ptr<Output> pitchTap() const { return pitch_tap_; }
      /** GROUPB WP-06: the sources of the warps FM / PD / AM / RM from the Noise (0, the sample output), the Sub (1), Filter 1 (2) and Filter 2 (3). A cell shared with the clones. */
      void setExtraWarpSource(int index, const Output* source) { (*extra_sources_)[static_cast<size_t>(index)] = source; }
      void setRatioSource(const Output* tap) { *ratio_source_ = tap; }

      virtual void setOversampleAmount(int oversample) override {
        Processor::setOversampleAmount(oversample);
        phase_inc_buffer_->ensureBufferSize(oversample * kMaxBufferSize);
      }

    private:
      template<poly_int(*phaseDistort)(poly_int, poly_float, poly_int, const poly_float*, int),
               poly_float(*window)(poly_int, poly_int, poly_float, const poly_float*, int)>
      void processOscillators(int num_samples, DistortionType distortion_type);

      template<poly_int(*phaseDistort)(poly_int, poly_float, poly_int, const poly_float*, int),
               poly_float(*window)(poly_int, poly_int, poly_float, const poly_float*, int)>
      void processChunk(poly_float current_center_amplitude, poly_float current_detuned_amplitude);

      void processBlend(int num_samples, poly_mask reset_mask);

      void loadVoiceBlock(VoiceBlock& voice_block, int index, poly_mask active_mask);
      void storeWarpState(int index, bool single_voice, poly_mask active_mask);

      void resetWavetableBuffers();
      void setActiveOscillators(int new_active_oscillators);
      template<poly_float(*snapTranspose)(poly_float, poly_float, float*)>
      void setPhaseIncBufferSnap(int num_samples, poly_mask reset_mask,
                                 poly_int trigger_sample, poly_mask active_mask, float* snap_buffer);
      void setPhaseIncBuffer(int num_samples, poly_mask reset_mask, poly_int trigger_sample, poly_mask active_mask);
      void setPhaseIncMults();
      void setupShepardWrap();
      void clearShepardWrap();
      void doShepardWrap(poly_mask new_buffer_mask, int quantize);
      void setAmplitude();
      void setWaveBuffers(poly_float phase_inc, int index);
      poly_float remapFrame(poly_float frame, int index);
      template<void(*spectralMorph)(const Wavetable::WavetableData*, int, poly_float*,
                                    FourierTransform*, float, int, const poly_float*)>
      void computeSpectralWaveBufferPair(int phase_update, int index, bool formant_shift,
                                         float phase_adjustment, poly_int wave_index,
                                         poly_float voice_increment, poly_float morph_amount,
                                         poly_float frame_fraction);
      template<void(*spectralMorph)(const Wavetable::WavetableData*, int, poly_float*,
                                    FourierTransform*, float, int, const poly_float*)>
      void setFourierWaveBuffers(poly_float phase_inc, int index, bool formant_shift);
      void stereoBlend(poly_float* audio_out, int num_samples, poly_mask reset_mask);
      void levelOutput(poly_float* audio_out, const poly_float* raw_out, int buffer_size, poly_mask reset_mask);
      void convertVoiceChannels(int buffer_size, poly_float* audio_out, poly_mask active_mask);
      force_inline float getPhaseIncAdjustment() {
        static constexpr int kBaseSampleRate = 44100;
        
        float adjustment = 1.0f;
        int sample_rate_mult = getSampleRate() / kBaseSampleRate;
        while (sample_rate_mult > 1) {
          sample_rate_mult >>= 1;
          adjustment *= 2.0f;
        }
        return adjustment;
      }

      poly_int phases_[kNumPolyPhase];
      poly_float detunings_[kNumPolyPhase];
      poly_float phase_inc_mults_[kNumPolyPhase];
      poly_float from_phase_inc_mults_[kNumPolyPhase];
      poly_int shepard_double_masks_[kNumPolyPhase];
      poly_int shepard_half_masks_[kNumPolyPhase];
      poly_int waiting_shepard_double_masks_[kNumPolyPhase];
      poly_int waiting_shepard_half_masks_[kNumPolyPhase];
      poly_float pan_amplitude_;
      poly_float center_amplitude_;
      poly_float detuned_amplitude_;
      poly_float midi_note_;
      poly_float distortion_phase_;
      poly_float blend_stereo_multiply_;
      poly_float blend_center_multiply_;
      const mono_float* next_buffers_[kNumBuffers];
      const mono_float* wave_buffers_[kNumBuffers];
      const mono_float* last_buffers_[kNumBuffers];
      poly_float spectral_morph_values_[kNumPolyPhase];
      poly_float last_spectral_morph_values_[kNumPolyPhase];
      poly_float distortion_values_[kNumPolyPhase];
      poly_float last_distortion_values_[kNumPolyPhase];
      poly_float distortion2_values_[kNumPolyPhase];       // SynthyBoy: second warp slot
      poly_float last_distortion2_values_[kNumPolyPhase];
      poly_float distortion_phase2_;
      WarpState warp_state_[2][kNumPolyPhase];   // SynthyBoy B21: per unison pair, like phases_
      VoiceBlock voice_block_;
      utils::RandomGenerator random_generator_;

      int transpose_quantize_;
      poly_float last_quantized_transpose_;
      poly_float last_quantize_ratio_;
      int unison_;
      int active_oscillators_;
      Wavetable* wavetable_;
      int wavetable_version_;
      // SynthyBoy: the frame position remap (Wavetable::frameRemap ()): the remapped position per lane after RISE / FALL, and whether the
      // lane has to jump to the target (note on) instead of gliding
      poly_float remap_slew_ = 0.0f;
      bool remap_lane_on_[poly_float::kSize] = {};
      bool remap_snap_[poly_float::kSize] = {};
      Output* first_mod_oscillator_;
      Output* second_mod_oscillator_;
      Output* sample_;
    
      poly_float fourier_frames1_[kNumBuffers + 1][kSpectralBufferSize];
      poly_float fourier_frames2_[kNumBuffers + 1][kSpectralBufferSize];
      std::shared_ptr<FourierTransform> fourier_transform_;
      std::shared_ptr<Output> phase_inc_buffer_;
      std::shared_ptr<PhaseBuffer> phase_buffer_;
      std::shared_ptr<const mono_float*> note_table_ = std::make_shared<const mono_float*>(nullptr);
      std::shared_ptr<Output> pitch_tap_ = std::make_shared<Output>(1);
      std::shared_ptr<std::array<const Output*, 4>> extra_sources_ = std::make_shared<std::array<const Output*, 4>>();
      // SynthyBoy (GROUPB, WP-05): the second frame of a smooth interpolation (oscN.smooth_interp); a clone's own scratch
      std::shared_ptr<poly_float[]> blend_scratch_ = std::shared_ptr<poly_float[]>(new poly_float[kSpectralBufferSize]());
      std::shared_ptr<const Output*> ratio_source_ = std::make_shared<const Output*>(nullptr);
      // Phase legato (A86): the phases of the voice that rendered last, shared by all the voice pairs (clones) of this oscillator.
      // Every clone runs on the audio thread, so no synchronisation is needed.
      struct PhaseLegatoState {
        bool valid = false;
        uint32_t left[kNumPolyPhase] = {};
        uint32_t right[kNumPolyPhase] = {};
      };
      std::shared_ptr<PhaseLegatoState> legato_state_;

      JUCE_LEAK_DETECTOR(SynthOscillator)
  };
} // namespace sbvital

