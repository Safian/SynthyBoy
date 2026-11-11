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
    /** Copies the live values of the last blocks (readout.h). Any thread, lock free; false (nothing written) for an engine that has none. */
    virtual bool copyReadout (LiveValues& /*out*/) const noexcept { return false; }

    /** Number of sounding voices (including released ones that are still fading out) after the last process(). */
    virtual int activeVoices() const noexcept = 0;

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
