// SynthyBoy - own engine: the sample, granular, spectral and multisample oscillator types of one oscillator slot of a voice
// (the glue of docs/engine-spec/07-sample-granular-spectral.md around the libraries core/sample-osc, granular, spectral, multisample).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>

#include "synthyboy/granular/granular.h"
#include "synthyboy/multisample/multisample.h"
#include "synthyboy/sampleosc/sample_voice.h"
#include "synthyboy/spectral/spectral.h"

#include "../dsp/util.h"
#include "content_set.h"
#include "wt_osc.h"

namespace synthyboy
{
namespace own
{
/** The keys of the sample-type oscillators (suffixes after "oscN."), resolved once. */
#define SB_LIB_KEYS(X) \
    X (sStart, "sample.start") X (sEnd, "sample.end") X (sRoot, "sample.root") X (sKeytrack, "sample.keytrack") X (sLoopMode, "sample.loop_mode") \
    X (sLoopStart, "sample.loop_start") X (sLoopEnd, "sample.loop_end") X (sLoopXfade, "sample.loop_xfade") X (sLoopRelative, "sample.loop_relative") \
    X (sLoopExit, "sample.loop_exit") X (sFadeEdges, "sample.fade_edges") X (sNormalize, "sample.normalize") X (sReverse, "sample.reverse") \
    X (sSliceMode, "sample.slice_mode") X (sSliceThreshold, "sample.slice_threshold") X (sSlicePlay, "sample.slice_play") X (sSliceRoot, "sample.slice_root") \
    X (sSlice, "sample.slice") X (sScan, "sample.scan") X (sScanRange, "sample.scan_range") X (sScanReverse, "sample.scan_reverse") \
    X (sScanLockTempo, "sample.scan_lock_tempo") X (sScanToBpm, "sample.scan_to_bpm") X (sLengthBeats, "sample.length_beats") \
    X (sUniStart, "sample.uni_start") X (sUniSpan, "sample.uni_span") \
    X (gWindowAmount, "gran.window_amount") X (gWindowSkew, "gran.window_skew") X (gWindowShape, "gran.window_shape") X (gDensityMode, "gran.density_mode") \
    X (gDensity, "gran.density") X (gDensitySync, "gran.density_sync") X (gDensityGrains, "gran.density_grains") X (gLengthMode, "gran.length_mode") \
    X (gLength, "gran.length") X (gLengthSync, "gran.length_sync") X (gLengthPercent, "gran.length_percent") X (gJumpStart, "gran.jump_start") \
    X (gMaxGrains, "gran.max_grains") X (gLoopGrains, "gran.loop_grains") X (gManual, "gran.manual") X (gPosition, "gran.position") \
    X (gScanKeytrack, "gran.scan_keytrack") X (gRandOffset, "gran.rand_offset") X (gRandDir, "gran.rand_dir") X (gReverseGrains, "gran.reverse_grains") \
    X (gRandPitch, "gran.rand_pitch") X (gRandLength, "gran.rand_length") X (gRandPan, "gran.rand_pan") X (gRandLevel, "gran.rand_level") \
    X (gRandWindow, "gran.rand_window") X (gRandSkew, "gran.rand_skew") X (gRandWarp, "gran.rand_warp") X (gRandWarp2, "gran.rand_warp2") \
    X (gSpawnPattern, "gran.spawn_pattern") \
    X (pFreqLo, "spec.freq_lo") X (pFreqHi, "spec.freq_hi") X (pLohiSmooth, "spec.lohi_smooth") X (pLohiPost, "spec.lohi_post_warp") X (pCut, "spec.cut") \
    X (pMix, "spec.mix") X (pFreqShift, "spec.freq_shift") X (pManual, "spec.manual") X (pPosition, "spec.position") X (pScanKeytrack, "spec.scan_keytrack") \
    X (pPhaseLock, "spec.phase_lock") X (pTransients, "spec.transients") \
    X (mOverride, "multi.override") X (mDelay, "multi.delay") X (mAttack, "multi.attack") X (mHold, "multi.hold") X (mDecay, "multi.decay") \
    X (mSustain, "multi.sustain") X (mRelease, "multi.release") X (mVelTrack, "multi.vel_track") X (mVelOverride, "multi.vel_override") X (mTimbre, "multi.timbre") \
    X (morphType, "morph.type") X (morphAmount, "morph.amount") X (morphSpread, "morph.spread") X (smooth, "smooth_interp") X (fltUse, "spec.flt_use") X (fltTable, "spec.flt_table") X (fltFrame, "spec.flt_frame") X (gYAxis, "gran.y_axis") X (gXyY, "gran.xy_y") X (pYAxis, "spec.y_axis") X (pXyY, "spec.xy_y") X (remapOn, "remap.on") X (remapRise, "remap.rise") X (remapFall, "remap.fall") X (phase, "phase") X (randomPhase, "random_phase") X (remapSlot, "remap.slot") X (ratioSrc, "ratio_src") X (pitchMode, "pitch_mode")

enum LibKey : int
{
#define X(name, str) lk_##name,
    SB_LIB_KEYS (X)
#undef X
    kLibKeyCount
};

struct LibKeys
{
    int idx[kLibKeyCount];
};

/** Index of the keys of oscillator `osc` (0 .. 2). */
const LibKeys& libKeys (int osc) noexcept;

/** Values of the keys of one oscillator at the end of a control period, read by the voice. */
struct LibParams
{
    float v[kLibKeyCount] = {};
    int type = 0;
    float noteSemis = 60.0f;    // pitch of the voice incl. glide, bend and master tune (semitones)
    float noteRaw = 60.0f;      // the played key
    float transposeTune = 0.0f; // oscN.transpose + oscN.tune / 100
    bool pitchTrack = true, bendTrack = true;
    float velocity = 1.0f;
    float bpm = 120.0f;
    double hostRate = 48000.0;
    int quality = 2;            // 0 low, 1 medium, 2 high
    int key = 60;
    uint32_t serial = 0;
    int switchKey = -1;         // the last key switch of the instrument
    float fixedSemis = 0.0f;
    spectral::Scratch* scratch = nullptr;
    uint32_t* chokeLatest = nullptr;   // [256] of this oscillator slot: the latest note sequence that started a zone of each group (engine wide for the slot)
    uint32_t* chokeCounter = nullptr;  // notes started on this slot
};

class LibOsc
{
public:
    static constexpr int kMaxU = WtOsc::kMaxVoices;
    static constexpr int kSpecU = 4;
    static constexpr int kSlots = multisample::kMaxLayerSlots;

    LibOsc();
    ~LibOsc();
    LibOsc (LibOsc&&) noexcept;
    LibOsc& operator= (LibOsc&&) noexcept;

    /** Allocates the voices (prepare time). */
    void prepare (double sampleRate);
    /** A new note on this slot: all state back to start. */
    void resetForNote() noexcept;
    /** Sets the type of the period. A change while the slot sounds starts the new type and fades the old one out over 10 ms. */
    void setType (int newType, bool fresh) noexcept;
    int type() const noexcept { return cur; }
    bool fadingFromWavetable() const noexcept { return from == 0; }
    /** Weights of the wavetable oscillator (which the voice runs alongside during a type crossfade) at the start and end of the chunk. */
    void wavetableWeights (int t0, int n, float& w0, float& w1) const noexcept;
    bool libraryRunning() const noexcept { return cur != 0 || from > 0; }

    /** Per control period: computes pitch, unison and the parameters of the active type. `t` carries level, pan, unison and warp values of the slot. */
    void update (const OscTick& t, const LibParams& p, const ContentSet* content, int oscIndex) noexcept;
    void noteOff (bool loopExit) noexcept;
    /** The content under the voices changed: stop every player (they point into the old set). */
    void stopAll() noexcept;
    bool hasContentFor (const ContentSet* c) const noexcept { return c == boundSet; }

    /** Renders samples t0 .. t0 + n - 1 of the period into L / R (overwritten). `cross` = the signals of the other two oscillators (may be null). */
    void render (int t0, int n, float* L, float* R, const float* const* cross) noexcept;
    float pitchTap() const noexcept { return tapSemis; }
    /** The first zone and the key of the last note of the multisample type (-1 before one was played): the read-out of the display. */
    int lastZone() const noexcept { return lastZone_; }
    int lastKey() const noexcept { return lastKey_; }

private:
    struct Unison
    {
        float semis = 0.0f, det = 0.0f;
        float gPrevL = 0, gPrevR = 0, gL = 0, gR = 0;
    };
    void beginType (int type) noexcept;
    void renderType (int type, int t0, int n, float* L, float* R, const float* const* cross) noexcept;
    float modulator (int warpType, int slot, const float* const* cross, int k, float& m) const noexcept;

    double sr = 48000.0;
    int cur = 0, from = -1;
    int fadePos = 0, fadeTotal = 480;
    bool anyPlaying = false;
    const ContentSet* boundSet = nullptr;
    int oscIdx = 0;
    OscTick tick;
    LibParams lp;
    int U = 1;
    Unison uni[kMaxU];
    float tapSemis = 60.0f;
    Rng rng;
    float randomPerNote = 0.0f;
    bool noteStarted[5] = {};

    // Sample
    std::vector<sampleosc::SampleVoice> sampleVoices;
    sampleosc::SampleOscParams sparams;
    bool sliced = false;
    double rateSpeed = 1.0;
    // Granular
    std::vector<granular::GranularVoice> grainVoices;
    granular::GrainParams gparams;
    // Spectral
    std::vector<spectral::SpectralVoice> specVoices;
    spectral::SpectralParams specParams;
    std::vector<float> filterGain;
    float lastCut = 1.0e9f, lastMix = 1.0e9f, lastFltPos = -1.0f;
    int lastFltTable = -1;
    float fltX[257], fltY[257];
    int lastFilterOsc = -1;
    // Multisample
    struct Layer
    {
        multisample::LayerVoice voice;
        int unison = 0;
    };
    std::vector<Layer> layers;      // kSlots * kMaxU
    int layerCount[kSlots] = {};
    int heldZones[kSlots] = {};
    int numHeld = 0;
    int lastZone_ = -1, lastKey_ = -1;
    int noteVelocity127 = 100;
    std::vector<float> tmpL, tmpR, mixL, mixR, modBuf, rateBuf;
    // Mask / Vocode warps: the last 2048 samples of the other oscillators and the noise, and their magnitude spectra
    std::vector<float> ring, extMag;
    int ringPos = 0;
    bool ringDirty = false;
};
} // namespace own
} // namespace synthyboy
