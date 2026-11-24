// SynthyBoy - core/spectral: the start cache (docs/plans/spectral-start-cache.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A memo of the start frames of the Spectral voices: one entry = the contribution that frame j (1 .. 3) of the priming adds to the ring of one channel (the last (j + 1) * kHop samples of the frame, the
// very floats that overlapAdd adds) and the phases the voice holds after that frame, under a key of every input that decides those bits (SpectralVoice::startFrames makes the key). A hit adds the same
// floats at the same moment as the computation would, so the output is bit-identical to the cache off. One table for the whole process (all engine instances share it), allocated once by prepare() on a
// non real-time thread; fixed size, no allocation afterwards. Lookups and stores are lock-free (a sequence counter per entry; a reader that sees a change takes a miss, a writer that loses the race
// skips its store): nobody ever waits. Not prepared: every lookup misses and nothing is stored.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

#include "synthyboy/spectral/spectral.h"

namespace synthyboy
{
namespace spectral
{
class StartCache
{
public:
    static constexpr int kWords = 16;     // the key: scalar inputs as bits, see SpectralVoice::startFrames
    static constexpr int kSets = 64;
    static constexpr int kWays = 4;
    static constexpr int kEntries = kSets * kWays;   // 256 entries of about 12.5 KB = 3.2 MB

    struct Stats
    {
        uint64_t hits = 0, misses = 0;
    };

    static StartCache& instance() noexcept
    {
        static StartCache cache;
        return cache;
    }

    /** Allocates the table (once; later calls do nothing). Not for the audio thread. */
    void prepare()
    {
        std::lock_guard<std::mutex> lock (mutex_);
        if (table_ != nullptr)
            return;
        owner_.reset (new Entry[kEntries]);
        table_.store (owner_.get(), std::memory_order_release);
    }

    bool ready() const noexcept { return table_.load (std::memory_order_acquire) != nullptr; }

    /** The contribution of frame `j` (written to `contrib` from index (3 - j) * kHop on) and the phases after it (kBins values) under `key`. False: not there. */
    bool get (const uint64_t* key, int j, float* contrib, float* phase) noexcept
    {
        Entry* const table = table_.load (std::memory_order_acquire);
        if (table == nullptr)
            return false;
        Entry* const set = table + static_cast<size_t> (setOf (key)) * kWays;
        for (int w = 0; w < kWays; ++w)
        {
            Entry& e = set[w];
            const uint32_t s1 = e.seq.load (std::memory_order_acquire);
            if (s1 == 0u || (s1 & 1u) != 0u || ! equal (e, key))
                continue;
            std::memcpy (phase, e.phase, sizeof (float) * kBins);
            std::memcpy (contrib + (3 - j) * kHop, e.contrib, sizeof (float) * static_cast<size_t> ((j + 1) * kHop));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (e.seq.load (std::memory_order_relaxed) != s1)
                continue;   // a writer came in while the copy ran: the copy is dropped
            e.stamp.store (clock_.fetch_add (1u, std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
            hits_.fetch_add (1u, std::memory_order_relaxed);
            return true;
        }
        misses_.fetch_add (1u, std::memory_order_relaxed);
        return false;
    }

    /** Stores the frame just computed: `contrib` holds the values that were added to the ring at index (3 - j) * kHop ..; `phase` the kBins phases after the frame. */
    void put (const uint64_t* key, int j, const float* contrib, const float* phase) noexcept
    {
        Entry* const table = table_.load (std::memory_order_acquire);
        if (table == nullptr)
            return;
        Entry* const set = table + static_cast<size_t> (setOf (key)) * kWays;
        Entry* victim = nullptr;
        uint32_t victimSeq = 0u;
        uint32_t oldest = 0xffffffffu;
        for (int w = 0; w < kWays; ++w)
        {
            Entry& e = set[w];
            const uint32_t s = e.seq.load (std::memory_order_acquire);
            if ((s & 1u) != 0u)
                continue;   // being written
            if (s != 0u && equal (e, key))
                return;     // already there
            if (s == 0u)
            {
                if (victim == nullptr || victimSeq != 0u)
                {
                    victim = &e;
                    victimSeq = 0u;
                    oldest = 0u;
                }
                continue;
            }
            const uint32_t st = e.stamp.load (std::memory_order_relaxed);
            if (victim == nullptr || (victimSeq != 0u && st < oldest))
            {
                victim = &e;
                victimSeq = s;
                oldest = st;
            }
        }
        if (victim == nullptr)
            return;
        uint32_t expected = victim->seq.load (std::memory_order_relaxed);
        if ((expected & 1u) != 0u || ! victim->seq.compare_exchange_strong (expected, expected + 1u, std::memory_order_acquire, std::memory_order_relaxed))
            return;
        for (int i = 0; i < kWords; ++i)
            victim->key[i].store (key[i], std::memory_order_relaxed);
        std::memcpy (victim->phase, phase, sizeof (float) * kBins);
        std::memcpy (victim->contrib, contrib + (3 - j) * kHop, sizeof (float) * static_cast<size_t> ((j + 1) * kHop));
        victim->stamp.store (clock_.fetch_add (1u, std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
        victim->seq.store (expected + 2u, std::memory_order_release);
    }

    /** Tests only (one thread): empties the table and zeroes the counters. */
    void clearForTest() noexcept
    {
        Entry* const table = table_.load (std::memory_order_acquire);
        if (table != nullptr)
            for (int i = 0; i < kEntries; ++i)
            {
                table[i].seq.store (0u, std::memory_order_relaxed);
                table[i].stamp.store (0u, std::memory_order_relaxed);
            }
        hits_.store (0u, std::memory_order_relaxed);
        misses_.store (0u, std::memory_order_relaxed);
    }

    Stats statsForTest() const noexcept
    {
        Stats s;
        s.hits = hits_.load (std::memory_order_relaxed);
        s.misses = misses_.load (std::memory_order_relaxed);
        return s;
    }

private:
    StartCache() = default;

    struct Entry
    {
        std::atomic<uint32_t> seq { 0u };     // 0: never written, odd: being written, even: valid
        std::atomic<uint32_t> stamp { 0u };   // the use clock (least recently used goes first)
        std::atomic<uint64_t> key[kWords];
        float phase[kBins];
        float contrib[kFftSize];              // the last (j + 1) * kHop samples of frame j
    };

    static uint32_t setOf (const uint64_t* key) noexcept
    {
        uint64_t h = 0x9e3779b97f4a7c15ull;
        for (int i = 0; i < kWords; ++i)
        {
            h ^= key[i] + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h *= 0xff51afd7ed558ccdull;
            h ^= h >> 33;
        }
        return static_cast<uint32_t> (h >> 20) % static_cast<uint32_t> (kSets);
    }

    static bool equal (const Entry& e, const uint64_t* key) noexcept
    {
        for (int i = 0; i < kWords; ++i)
            if (e.key[i].load (std::memory_order_relaxed) != key[i])
                return false;
        return true;
    }

    std::mutex mutex_;
    std::unique_ptr<Entry[]> owner_;
    std::atomic<Entry*> table_ { nullptr };
    std::atomic<uint32_t> clock_ { 0u };
    std::atomic<uint64_t> hits_ { 0u }, misses_ { 0u };
};
} // namespace spectral
} // namespace synthyboy
