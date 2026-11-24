// SynthyBoy - core tests: helpers (deterministic PRNG, rendering).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "synthyboy/engine.h"

namespace sbtest
{
/** xorshift64* - deterministic on every platform. */
struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed) : s (seed ? seed : 0x9e3779b97f4a7c15ull) {}
    uint64_t next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545f4914f6cdd1dull;
    }
    uint32_t u32() { return static_cast<uint32_t> (next() >> 32); }
    /** uniform in [0, 1) */
    double uniform() { return static_cast<double> (next() >> 11) * (1.0 / 9007199254740992.0); }
};

inline synthyboy::Event noteOn (int frame, int key, float velocity = 1.0f, int channel = 0, int noteId = -1)
{
    synthyboy::Event e;
    e.frame = frame;
    e.type = synthyboy::Event::NoteOn;
    e.channel = static_cast<int16_t> (channel);
    e.key = static_cast<int16_t> (key);
    e.value = velocity;
    e.noteId = noteId;
    return e;
}

inline synthyboy::Event noteOff (int frame, int key, int channel = 0)
{
    auto e = noteOn (frame, key, 0.0f, channel);
    e.type = synthyboy::Event::NoteOff;
    return e;
}

inline synthyboy::Event pitchBend (int frame, float bend, int channel = 0)
{
    auto e = noteOn (frame, 0, bend, channel);
    e.type = synthyboy::Event::PitchBend;
    return e;
}

inline synthyboy::Event allNotesOff (int frame, int channel = -1)
{
    auto e = noteOn (frame, 0, 0.0f, channel);
    e.type = synthyboy::Event::AllNotesOff;
    return e;
}

struct Rendered
{
    std::vector<float> left, right;
};

/** Renders totalFrames in blocks of blockSize; events carry absolute frames (sorted). */
inline Rendered render (synthyboy::IEngine& engine, const std::vector<synthyboy::Event>& events, int totalFrames, int blockSize)
{
    Rendered out;
    out.left.assign (static_cast<size_t> (totalFrames), 0.0f);
    out.right.assign (static_cast<size_t> (totalFrames), 0.0f);

    std::vector<synthyboy::Event> local;
    size_t idx = 0;
    for (int start = 0; start < totalFrames; start += blockSize)
    {
        const int n = std::min (blockSize, totalFrames - start);
        local.clear();
        while (idx < events.size() && events[idx].frame < start + n)
        {
            auto e = events[idx++];
            e.frame -= start;
            local.push_back (e);
        }
        engine.process (local.data(), static_cast<int> (local.size()), out.left.data() + start, out.right.data() + start, n);
    }
    return out;
}

inline bool allFinite (const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite (x))
            return false;
    return true;
}

inline float peak (const std::vector<float>& v, size_t from = 0, size_t to = static_cast<size_t> (-1))
{
    float p = 0.0f;
    for (size_t i = from; i < v.size() && i < to; ++i)
        p = std::max (p, std::fabs (v[i]));
    return p;
}
} // namespace sbtest
