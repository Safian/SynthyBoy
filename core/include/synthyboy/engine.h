// SynthyBoyVST - core: engine interface (the replacement seam of docs/ARCHITECTURE.md 3.1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>

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
        accepts any block size. */
    virtual void prepare (double sampleRate, int maxBlockFrames, const Quality& quality) = 0;

    /** Silences all voices and snaps smoothers to the current parameter values. */
    virtual void reset() = 0;

    /** Sets a parameter (plain value, constrained to the parameter's range); unknown ids and NaN are ignored. */
    virtual void setParam (uint32_t id, float plain) noexcept = 0;
    virtual float getParam (uint32_t id) const noexcept = 0;

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
} // namespace synthyboy
