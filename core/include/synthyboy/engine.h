// SynthyBoy - core: engine interface (the replacement seam of docs/ARCHITECTURE.md 3.1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>

#include "engine_caps.h"
#include "events.h"
#include "offline_render.h"
#include "params.h"
#include "quality.h"
#include "readout.h"

namespace synthyboy
{
struct StateContent;
class IContentStore;
namespace tuning
{
class ITuningSource;
}
} // namespace synthyboy

namespace synthyboy
{
/**
 * A synthesis engine. Shells and UIs only see this interface; engine-own and (optionally) engine-vital implement it.
 *
 * Threading: prepare / reset / destruction are called while no process() runs. setParam, getParam and activeVoices
 * may be called from any thread at any time (lock free); process() is called from one audio thread at a time.
 */
/** What an engine can tell about its voices and the keys it believes are held (diagnostics: the status file of the MPC shell, the note storm tests). All values as of the end of the last
    process(); the counters count since prepare() / reset (never wrap in practice). */
struct VoiceStats
{
    int active = 0;            // sounding voices, the released ones that still fade included
    int released = 0;          // of these in their release stage (or fading out)
    int sustaining = 0;        // of these not released (a key down, or the pedal)
    int heldKeys = 0;          // keys the engine believes are down (events only)
    int lowestHeld = -1, highestHeld = -1;   // MIDI notes, -1 none
    int orphans = 0;           // not released voices whose key is not held while the pedal is up: the bug state (a stuck note); always 0
    bool sustainPedal = false; // the pedal of any channel is down
    unsigned noteOns = 0, noteOffs = 0, unmatchedNoteOffs = 0, stolen = 0;
};

class IEngine
{
public:
    virtual ~IEngine() = default;

    /** The only call that may allocate (voice pool etc.). Parameter values are kept. maxBlockFrames is a hint; process()
        accepts any block size. The quality is capped to the limits of quality.h. */
    virtual void prepare (double sampleRate, int maxBlockFrames, const Quality& quality) = 0;

    /**
     * Changes the quality while playing: a lock-free hand-off (any thread, no allocation), the audio thread takes it over
     * at the start of its next block. maxVoices, maxUnison, interpolation and fxMask take effect then (a voice that is
     * above a lowered cap is released, not cut); the OVERSAMPLING of the quality takes effect with the next prepare() only
     * (it resizes the engine's buffers). Parameter values are never touched.
     */
    virtual void setQuality (const Quality& quality) noexcept = 0;

    /** Silences all voices and snaps smoothers to the current parameter values. */
    virtual void reset() = 0;

    /** Restarts the free running phases of the effects (the LFOs of chorus, flanger, phaser) at the beginning of their cycle, as at time 0 (message thread or between two process () calls). reset ()
        does not do it (the frozen goldens of the version 2 sound depend on the phase a reset leaves); a caller that wants a render that does not depend on the renders before it (the parameter
        audit, docs/ui-design/PARAM-AUDIT-REPORT.md 4.3) calls it after reset (). The default does nothing (engine-own has no such effects). */
    virtual void restartEffectPhases() noexcept {}

    /** Silences the sounding notes and flushes the tails of the effects at once (the host transport stopped, preferences.h silenceOnStop). Audio thread,
        between two process () calls: no allocation, the parameters, the controllers and the content stay as they are. The default does nothing (engine-own). */
    virtual void silence() noexcept {}

    /** Ultra quality for an offline render (preferences.h ultraRender): the oversampling of the quality option 4x, read by prepare () like
        quality.oversample_4x (needsPrepare () reports a change). Set it before prepare (); engines without the option ignore it. Any thread. */
    virtual void setUltraRender (bool) noexcept {}

    /** Renders `request` offline (offline_render.h, GROUPD WP-15) on a private instance of the engine that starts from this engine's parameters
        and content: this engine is only read (no reset, no voice touched, the audio thread is not waited for), the result is deterministic and
        at the quality of the high tier without the caps of the tier it runs in; request.ultra = 4x oversampling. Message thread, may take long.
        False when the engine has no offline render (the default; renderOfflineAny () then builds the twin from outside). */
    virtual bool renderOffline (const OfflineRequest& /*request*/, OfflineAudio& /*out*/) { return false; }

    /** Sets a parameter (plain value, constrained to the parameter's range); unknown ids and NaN are ignored. */
    virtual void setParam (uint32_t id, float plain) noexcept = 0;
    virtual float getParam (uint32_t id) const noexcept = 0;

    /** The areas of functionality added after state version 2 that this engine implements (engine_caps.h). The default is none: the
        keys of version 3 are stored and read back but not live, and supportsParam () is false for them until the bits they need
        (requiredCaps ()) are all set. Constant for the lifetime of the engine; any thread. */
    virtual EngineCapsMask capabilities() const noexcept { return kCapNone; }
    /** The second capability word (GROUPC): the granular, spectral and multisample oscillators. A key needs the bits of both words (requiredCaps (), requiredCaps2 ()). */
    virtual EngineCaps2Mask capabilities2() const noexcept { return kCap2None; }

    /** The host transport at the start of the next process() call (audio thread, before process(); no allocation). Tempo sync,
        the beat units of the envelopes and the HOST mode of the LFOs read it. Without a call the engine runs at 120 BPM with a stopped
        transport. Engines without tempo sync ignore it. */
    virtual void setTransport (const TransportInfo&) noexcept {}

    /** The `content` section of the state (custom LFO shapes, velocity / note curves, the tuning file; content_state.h). NOT real-time:
        call it from the message thread (it may allocate), it is handed to the audio thread at the start of its next block without a lock.
        It is kept across prepare() and reset(); an engine ignores the parts it has no capability for (capabilities ()). */
    virtual void setContent (const StateContent&) {}

    /** The store that turns the references of `content` (wavetables, samples, impulse responses) into bytes (content_store.h). NOT real-time,
        message thread; the store must outlive the engine or be replaced by nullptr first. Setting it (or a new `content` by setContent ())
        decodes the referenced files and hands them to the audio thread without a lock; a reference the store cannot resolve falls back as
        documented in 13-new-keys.md 5 (Preset table: saw, Sample: silence, Preset IR: bypass). Engines without these capabilities ignore it. */
    virtual void setContentStore (const IContentStore*) {}

    /** The MTS-ESP seam (docs/ui-design/13-new-keys.md 4.7): an external dynamic tuning source that master.tuning.mts_esp uses while it
        reports itself connected. The pointer is not owned and must stay valid until it is replaced by nullptr; the shell links the
        MTS-ESP client library (the core never does) and wraps it in a tuning::ITuningSource. Any thread, lock free. */
    virtual void setTuningSource (const tuning::ITuningSource*) noexcept {}

    /** True if the engine acts on this parameter. The value of every parameter of the table is stored and read back by every
        engine, the others are simply ignored (the own engine implements a subset: core/engine-own/README.md). */
    virtual bool supportsParam (uint32_t id) const noexcept = 0;

    /**
     * Renders numFrames of audio, overwriting left and right (right may be null for a mono output; before prepare()
     * the output is silence). events must be sorted by frame; they are applied sample accurately. No allocation, no
     * locks, no exceptions, no I/O.
     */
    virtual void process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept = 0;

    /** The knobs whose modulated value the display wants (GROUPA2 WP-17): table indices, at most LiveValues::kMaxTargets (the rest is ignored). Any thread, lock free;
        the audio thread takes the list over at the end of its next block. */
    virtual void setReadoutTargets (const int* /*paramIndices*/, int /*count*/) noexcept {}
    /** The display shows the level meters of the SOURCE strips (LiveValues::stripPeak[0 .. 6]: Sub, A, B, C, Noise, Filter 1, Filter 2): call it at least every few frames while they are on screen. The own engine measures the
        sources (a peak per voice, source and control period) only for a while (about 0.7 s of blocks) after the last call, and the strips 0 .. 6 read 0 in between; the bus and master strips (7 .. 9) are always
        measured. Any thread, lock free; a no-op for an engine without these meters. */
    virtual void requestMeters() noexcept {}
    /** Copies the live values of the last blocks (readout.h). Any thread, lock free; false (nothing written) for an engine that has none. */
    virtual bool copyReadout (LiveValues& /*out*/) const noexcept { return false; }

    /** Not real time (the host's thread, the display thread; never the audio thread): builds on the calling thread whatever the CURRENT parameter values need and prepare () left to a background thread
        (the own engine: the built-in wavetables the oscillators name). Call it after a state or program was applied. A no-op for an engine that builds everything in prepare (). */
    virtual void readyContent() noexcept {}
    /** The same for a state that is about to be applied: builds, on the calling thread, the tables the plain values `plain[0 .. count)` (indexed like the parameter table) name, BEFORE the caller sets them,
        so that the parameters and the tables they name take effect for the audio thread together. A no-op for an engine that builds everything in prepare (). */
    virtual void readyContentFor (const float* /*plain*/, size_t /*count*/) noexcept {}
    /** true while a table the current parameter values name is still being built (the plugin-drawn pages show a small "loading" mark); the sound is that of the table the oscillator had until then. Any thread. */
    virtual bool contentPending() const noexcept { return false; }
    /** Milliseconds this engine has spent building its built-in wavetables (summed over all threads that built them; 0: none or not known): the start-up record of the shell. Any thread. */
    virtual double wavetableBuildMs() const noexcept { return 0.0; }
    /** Milliseconds the last prepare () spent on part `part` of its work: 0 the voice pool, 1 the effects (pool and the three racks); 0.0 for another part or an engine that does not know. Any thread. */
    virtual double prepareDetailMs (int /*part*/) const noexcept { return 0.0; }

    /** Number of sounding voices (including released ones that are still fading out) after the last process(). */
    virtual int activeVoices() const noexcept = 0;
    /** The voice and held key diagnostics; false (nothing written) for an engine that has none. Any thread, lock free. */
    virtual bool voiceStats (VoiceStats& /*out*/) const noexcept { return false; }
    /** The content the sound needs and does not have (docs/ARCHITECTURE.md section 6, the `missing_content` flag), as of the last block; bits: 0 .. 2 the file (sample, granular, spectral,
        multisample) of oscillator 1 .. 3, 3 .. 5 the user wavetable of oscillator 1 .. 3 (the built-in saw plays), 6 the impulse of Convolve (bypassed), 7 the sample of the noise source. Any thread. */
    virtual unsigned missingContent() const noexcept { return 0; }

    /** Latency of the output in samples at the host rate, as of the last process() (any thread). engine-vital: Bode's Hilbert filter
        delays the signal path it sits in, the engine delays every other path to match (the Direct output, the other buses, the bands of
        a splitter, the voices that bypass the rack), so the whole output is late by this value and a host can compensate it. 0 without
        such an effect. It changes when Bode is switched on or off or moved to another rack. */
    virtual int latencySamples() const noexcept { return 0; }

    /** True when a parameter changed that the engine can only apply in prepare() (engine-vital: quality.oversample_4x resizes the buffers
        of the effects and the voices, which must not happen on the audio thread). Any thread, lock free. The shell then prepares the engine
        again on a thread that may allocate (JUCE: the message thread with the processing suspended; CLAP: request_restart). */
    virtual bool needsPrepare() const noexcept { return false; }
};

/** The engine written for this project (always built; the only engine on iOS). */
std::unique_ptr<IEngine> createOwnEngine();
/** Own engine, benchmark only: the time of a block by section. Available only in a build with SYNTHYBOY_SECTION_TIMING (otherwise ownSectionTimingAvailable () is false and the
    totals stay zero). Totals are exclusive times in ms over all blocks since ownSectionReset (); read between blocks. ownSectionClockCostNs (): the measured cost of one scope. */
bool ownSectionTimingAvailable() noexcept;
int ownSectionCount() noexcept;
const char* ownSectionName (int index) noexcept;
void ownSectionReset() noexcept;
void ownSectionRead (int index, double& totalMs, unsigned long long& calls) noexcept;
double ownSectionClockCostNs() noexcept;
/** The scopes of other sections entered directly inside section `index` since ownSectionReset (). A scope reads the clock twice: about one read lands in its own time and
    about one in the time of the section around it, so a section with many small sections inside looks dearer than it is by childCalls x half the cost of a scope. */
unsigned long long ownSectionChildCalls (int index) noexcept;

/** Own engine only: the unison voices per oscillator at tier low (see OwnEngine::setLowTierUnisonCap; 0 = no cap, default 3). False if `engine` is not an own engine. */
bool setOwnLowTierUnisonCap (IEngine& engine, int voices) noexcept;
/** Own engine only: pad pressure to Filter 1 cutoff modulation depth (0 = off, 1 = +36 semitones at full pressure). False if `engine` is not an own engine. */
bool setOwnPadPressureToCutoff (IEngine& engine, float amount01) noexcept;
/** Own engine only: keep (default) or drop the spectral morph spread across the unison voices at tier low (see OwnEngine::setLowTierMorphSpread). */
bool setOwnLowTierMorphSpread (IEngine& engine, bool keep) noexcept;
/** Own engine only: the fade of a stolen voice at tier low in seconds (default 0.003; 0.05 = as at medium / high). */
bool setOwnLowTierKillFade (IEngine& engine, float seconds) noexcept;
/** Own engine only, for measurements: switches off ways the engine saves work without changing the sound, so that one binary can be measured with and without each
    (bench/tier_bench.cpp --no-fx-mute, --scalar-filters). Bits of `off`: kOwnSavingFxMute = an effect whose mix is exactly 0 is processed all the same (FxModule::process),
    kOwnSavingVectorFilters = the voice filters run their scalar loops also where a vector kernel exists, kOwnSavingVectorWarp = the oversampled samples of a
    warped oscillator are made one by one also where the four lane form exists (--scalar-warp), kOwnSavingContentKernels = the Sample / Granular / Spectral / Multisample oscillators run their
    per sample (or per frame) reference code instead of the block kernels (--scalar-content; the same samples, or the stated tolerance for the spectral kernels). kOwnSavingVectorFilters also covers the
    four lane LFO refresh of the chorus. kOwnSavingVoiceControl = the control code of the voices and the modulation matrix runs as it was before the caches and skips of
    docs/release/CPU-REVIEW-VOICE-MOD.md (--no-voice-savings; the same samples and the same display values). Default 0 (every saving on). False if `engine` is not an own engine. */
enum OwnSaving : uint32_t { kOwnSavingFxMute = 1u << 0, kOwnSavingVectorFilters = 1u << 1, kOwnSavingVectorWarp = 1u << 2, kOwnSavingContentKernels = 1u << 3,
                            kOwnSavingVoiceControl = 1u << 4 };
bool setOwnSavingsOff (IEngine& engine, uint32_t off) noexcept;

enum class EngineKind
{
    own,  // engine-own: always available
    vital // engine-vital: Vital-derived DSP, only when built with SYNTHYBOY_WITH_VITAL=ON (never on iOS)
};

/** True if the engine of this kind is part of this build. */
bool isEngineAvailable (EngineKind kind) noexcept;

/** Short stable name ("own", "vital"), for logs and command lines. */
const char* engineName (EngineKind kind) noexcept;

/**
 * Creates an engine of the given kind. Returns nullptr if it is not part of this build (EngineKind::vital without
 * Vital); if errorMessage is not null it then receives a static, human readable text (and nullptr otherwise).
 */
std::unique_ptr<IEngine> createEngine (EngineKind kind, const char** errorMessage = nullptr);
} // namespace synthyboy
