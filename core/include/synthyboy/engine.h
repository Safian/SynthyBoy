// SynthyBoy - core: engine interface (the replacement seam of docs/ARCHITECTURE.md 3.1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>

#include "engine_caps.h"
#include "events.h"
#include "params.h"
#include "quality.h"

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

    /** Sets a parameter (plain value, constrained to the parameter's range); unknown ids and NaN are ignored. */
    virtual void setParam (uint32_t id, float plain) noexcept = 0;
    virtual float getParam (uint32_t id) const noexcept = 0;

    /** The areas of functionality added after state version 2 that this engine implements (engine_caps.h). The default is none: the
        keys of version 3 are stored and read back but not live, and supportsParam () is false for them until the bits they need
        (requiredCaps ()) are all set. Constant for the lifetime of the engine; any thread. */
    virtual EngineCapsMask capabilities() const noexcept { return kCapNone; }

    /** True if the engine acts on this parameter. The value of every parameter of the table is stored and read back by every
        engine, the others are simply ignored (the own engine implements a subset: core/engine-own/README.md). */
    virtual bool supportsParam (uint32_t id) const noexcept = 0;

    /**
     * Renders numFrames of audio, overwriting left and right (right may be null for a mono output; before prepare()
     * the output is silence). events must be sorted by frame; they are applied sample accurately. No allocation, no
     * locks, no exceptions, no I/O.
     */
    virtual void process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept = 0;

    /** Number of sounding voices (including released ones that are still fading out) after the last process(). */
    virtual int activeVoices() const noexcept = 0;
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
