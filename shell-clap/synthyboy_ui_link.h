// SynthyBoy - native CLAP shell: the lock free link between the audio thread and a GUI on the main thread (JUCE free).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Everything the bitmap GUI (shell-clap/bitmap/) exchanges with the audio thread goes through this one object, which the shell owns whether or not a GUI exists.
// It allocates nothing after construction and takes no lock: fixed arrays of atomics and one single-producer / single-consumer ring.
//
//   audio / host thread -> GUI   publish ()           a parameter value changed (host automation, MIDI learn, a state load ...): mirror + "changed" bit
//                                takeChanges ()       main thread: visits every changed parameter once (the GUI store takes it as a change from the host)
//                                peak / voices / cpu / scope ring: the live displays, read exactly like the JUCE editor reads the processor's
//   GUI -> audio thread          postValue () / postGesture ()   edits of the GUI: one slot per parameter (a drag is coalesced to its newest value)
//                                drainEdits ()        process () and params.flush () turn the slots into the same handling as host automation + out events
//                                postMidi () / popMidi ()  the on-screen keyboard, pitch / mod wheel, sustain: raw 3 byte MIDI messages
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "synthyboy/params.h"

namespace synthyboy
{
class UiLink
{
public:
    static constexpr size_t kWords = (kMaxParams + 63) / 64;
    static constexpr size_t kScopeSamples = 4096; // UiHost::kScopeSamples
    static constexpr uint32_t kMidiCapacity = 1024;

    UiLink() noexcept
    {
        for (auto& v : mirror)
            v.store (0.0f, std::memory_order_relaxed);
        for (auto& w : changed)
            w.store (0, std::memory_order_relaxed);
        for (auto& w : valueBits)
            w.store (0, std::memory_order_relaxed);
        for (auto& w : beginBits)
            w.store (0, std::memory_order_relaxed);
        for (auto& w : endBits)
            w.store (0, std::memory_order_relaxed);
        for (auto& s : scopeRing)
            s.store (0.0f, std::memory_order_relaxed);
        for (auto& m : midiRing)
            m.store (0, std::memory_order_relaxed);
    }

    // ---- values: any thread writes, the main thread reads -----------------------------------------------------------------------------------------

    /** The value of table index `index` is now `plain`; the GUI is told (a "changed" bit). Any thread, wait free. */
    void publish (size_t index, float plain) noexcept
    {
        if (index >= kMaxParams)
            return;
        mirror[index].store (plain, std::memory_order_relaxed);
        changed[index >> 6].fetch_or (uint64_t { 1 } << (index & 63), std::memory_order_release);
        anyChange.store (true, std::memory_order_release);
    }
    /** The value changed because of the GUI itself (it already knows): no "changed" bit. */
    void publishQuiet (size_t index, float plain) noexcept
    {
        if (index < kMaxParams)
            mirror[index].store (plain, std::memory_order_relaxed);
    }
    float value (size_t index) const noexcept { return index < kMaxParams ? mirror[index].load (std::memory_order_relaxed) : 0.0f; }

    /** Main thread: calls f (index, plain) once for every parameter that changed since the last call. */
    template <typename F>
    void takeChanges (F&& f)
    {
        if (! anyChange.exchange (false, std::memory_order_acquire))
            return;
        for (size_t w = 0; w < kWords; ++w)
        {
            uint64_t bits = changed[w].exchange (0, std::memory_order_acquire);
            while (bits != 0)
            {
                const unsigned bit = static_cast<unsigned> (countTrailingZeros (bits));
                bits &= bits - 1;
                const size_t index = w * 64 + bit;
                f (index, mirror[index].load (std::memory_order_relaxed));
            }
        }
    }

    // ---- edits: the main thread posts, the audio thread drains --------------------------------------------------------------------------------------

    void postValue (size_t index, float plain) noexcept
    {
        if (index >= kMaxParams)
            return;
        uiValue[index].store (plain, std::memory_order_relaxed);
        valueBits[index >> 6].fetch_or (uint64_t { 1 } << (index & 63), std::memory_order_release);
        pending.store (true, std::memory_order_release);
    }
    void postGesture (size_t index, bool begin) noexcept
    {
        if (index >= kMaxParams)
            return;
        (begin ? beginBits : endBits)[index >> 6].fetch_or (uint64_t { 1 } << (index & 63), std::memory_order_release);
        pending.store (true, std::memory_order_release);
    }
    bool hasPendingEdits() const noexcept { return pending.load (std::memory_order_acquire); }

    /** Audio thread (process, params.flush): for every posted edit, in the order gesture begin, value, gesture end of each parameter:
        onBegin (index), onValue (index, plain), onEnd (index). A callback returns false when it could not deliver (the host's output queue is full): the edit stays pending for the
        next call. */
    template <typename OnBegin, typename OnValue, typename OnEnd>
    void drainEdits (OnBegin&& onBegin, OnValue&& onValue, OnEnd&& onEnd)
    {
        if (! pending.exchange (false, std::memory_order_acquire))
            return;
        bool undelivered = false;
        for (size_t w = 0; w < kWords; ++w)
        {
            drainWord (beginBits[w], w, undelivered, [&] (size_t i) { return onBegin (i); });
            drainWord (valueBits[w], w, undelivered, [&] (size_t i) { return onValue (i, uiValue[i].load (std::memory_order_relaxed)); });
            drainWord (endBits[w], w, undelivered, [&] (size_t i) { return onEnd (i); });
        }
        if (undelivered)
            pending.store (true, std::memory_order_release);
    }

    // ---- MIDI from the GUI -----------------------------------------------------------------------------------------------------------------------------

    /** Main thread. False when the ring is full (the message is dropped; a held key is never lost: its note off is posted even when the ring is nearly full by the caller's retry). */
    bool postMidi (uint8_t b0, uint8_t b1, uint8_t b2) noexcept
    {
        const uint32_t head = midiHead.load (std::memory_order_relaxed);
        const uint32_t tail = midiTail.load (std::memory_order_acquire);
        if (head - tail >= kMidiCapacity)
            return false;
        midiRing[head % kMidiCapacity].store (uint32_t { b0 } | (uint32_t { b1 } << 8) | (uint32_t { b2 } << 16), std::memory_order_relaxed);
        midiHead.store (head + 1, std::memory_order_release);
        return true;
    }
    /** Audio thread. */
    bool popMidi (uint8_t (&data)[3]) noexcept
    {
        const uint32_t tail = midiTail.load (std::memory_order_relaxed);
        if (tail == midiHead.load (std::memory_order_acquire))
            return false;
        const uint32_t packed = midiRing[tail % kMidiCapacity].load (std::memory_order_relaxed);
        data[0] = static_cast<uint8_t> (packed);
        data[1] = static_cast<uint8_t> (packed >> 8);
        data[2] = static_cast<uint8_t> (packed >> 16);
        midiTail.store (tail + 1, std::memory_order_release);
        return true;
    }

    // ---- live displays ---------------------------------------------------------------------------------------------------------------------------------

    /** Set by the GUI while it exists: the audio thread skips the meters and the scope ring otherwise (no cost without a GUI). */
    std::atomic<bool> guiActive { false };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<int> voiceCount { 0 };
    std::atomic<float> cpu { 0.0f };

    /** Audio thread: appends the mono mix of a block to the scope ring. */
    void writeScope (const float* left, const float* right, int numFrames) noexcept
    {
        uint32_t w = scopeWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < numFrames; ++i)
        {
            scopeRing[w % kScopeSamples].store (0.5f * (left[i] + (right != nullptr ? right[i] : left[i])), std::memory_order_relaxed);
            ++w;
        }
        scopeWrite.store (w, std::memory_order_release);
    }
    /** Main thread: the latest `maxSamples` (up to kScopeSamples), oldest first. */
    int copyScope (float* dest, int maxSamples) const noexcept
    {
        const uint32_t w = scopeWrite.load (std::memory_order_acquire);
        const int available = static_cast<int> (w < kScopeSamples ? w : static_cast<uint32_t> (kScopeSamples));
        const int n = maxSamples < available ? maxSamples : available;
        for (int i = 0; i < n; ++i)
            dest[i] = scopeRing[(w - static_cast<uint32_t> (n) + static_cast<uint32_t> (i)) % kScopeSamples].load (std::memory_order_relaxed);
        return n;
    }

private:
    static int countTrailingZeros (uint64_t v) noexcept
    {
        int n = 0;
        while ((v & 1u) == 0)
        {
            v >>= 1;
            ++n;
        }
        return n;
    }

    template <typename F>
    static void drainWord (std::atomic<uint64_t>& word, size_t w, bool& undelivered, F&& deliver)
    {
        uint64_t bits = word.exchange (0, std::memory_order_acquire);
        while (bits != 0)
        {
            const unsigned bit = static_cast<unsigned> (countTrailingZeros (bits));
            const uint64_t mask = uint64_t { 1 } << bit;
            bits &= bits - 1;
            if (! deliver (w * 64 + bit))
            {
                word.fetch_or (mask, std::memory_order_release); // stays pending
                undelivered = true;
            }
        }
    }

    std::array<std::atomic<float>, kMaxParams> mirror;
    std::array<std::atomic<float>, kMaxParams> uiValue {};
    std::array<std::atomic<uint64_t>, kWords> changed, valueBits, beginBits, endBits;
    std::atomic<bool> anyChange { false };
    std::atomic<bool> pending { false };
    std::array<std::atomic<uint32_t>, kMidiCapacity> midiRing;
    std::atomic<uint32_t> midiHead { 0 }, midiTail { 0 };
    std::array<std::atomic<float>, kScopeSamples> scopeRing;
    std::atomic<uint32_t> scopeWrite { 0 };
};
} // namespace synthyboy
