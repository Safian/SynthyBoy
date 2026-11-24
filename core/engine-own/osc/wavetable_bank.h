// SynthyBoy - own engine: the built-in band-limited wavetables (original definitions, additive spectra + inverse FFT).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace synthyboy
{
namespace own
{
/** One wavetable: `numFrames` single cycles, each stored as kNumLevels band-limited copies (mip levels). Level m keeps the harmonics
    1 .. maxHarmonics (m): octave steps from 1024 down to 256 harmonics, half octave steps below (that is where the pitches of music are: from about 94 Hz up). Memory: a level is
    stored with only as many samples as its band needs (eight times its highest harmonic, at most 2048, at least 256: the interpolation images of the 4 point read
    stay as low as they were with 2048 samples at every level - at 4 times the oversampling they alias into the audible band, measured at -43 dB against -58 dB)
    - the 18 levels cost 6.79 times one 2048 sample cycle instead of 18 (13,896 time floats with the guard samples + 2,048 spectrum floats = 63,776 B per frame; 32.65 MB / 31.14 MiB for
    the eight 64 frame tables, 33.10 MB / 31.57 MiB for all 15 built-in tables, measured by `own_wavetable_bank_memory`).
    A cycle is stored with one guard sample in front and two behind (the 4 point interpolation never wraps: indices -1 .. size (level) + 2 are valid).
    The spectrum of every frame (cosine / sine coefficients, harmonics 1 .. 1023) is kept too: the spectral morph rebuilds a cycle from it.
    Immutable after construction: shared by all voices and engines. */
class Wavetable
{
public:
    static constexpr int kCycle = 2048;
    static constexpr int kNumLevels = 18; // 1024, 512, 256 harmonics, then half octave steps (181, 128, 91 ... 1)
    static constexpr int kHarmonics = kCycle / 2; // spectrum arrays are indexed 0 .. kHarmonics - 1 (0 unused)
    /** The memory of the whole built-in bank (519 frames: eight 64 frame tables and seven single frame tables) in bytes, as documented above; builtinBankBytes () must give exactly this. */
    static constexpr size_t kBankDocumentedBytes = 519u * (13896u + 2048u) * sizeof (float);

    /** Samples per cycle of a level (power of two). */
    static int size (int level) noexcept;

    int numFrames() const noexcept { return frames; }

    /** The highest harmonic kept by level m. */
    static int maxHarmonics (int level) noexcept;
    /** The coarsest level that keeps every harmonic below `nyquistOverFundamental` (>= 1). */
    static int levelFor (float nyquistOverFundamental) noexcept;

    /** Pointer to sample 0 of the cycle (index -1 .. kCycle + 2 are valid). */
    const float* cycle (int frame, int level) const noexcept
    {
        return data.data() + static_cast<size_t> (frame) * frameStride + levelOffset[level] + 1;
    }

    /** Cosine coefficients (index h) of a frame; the sine coefficients follow at + kHarmonics. Peak normalised like the time signal. */
    const float* spectrum (int frame) const noexcept { return spec.data() + static_cast<size_t> (frame) * 2 * kHarmonics; }
    size_t memoryBytes() const noexcept { return (data.size() + spec.size()) * sizeof (float); }

    /** Builds a table from per frame harmonic spectra: sine[h] and cosine[h] coefficients (h = 1 .. 1024) of  sum a_h cos (h t) + b_h sin (h t),
        every frame normalised to a peak of 1 when `normalizePeak` (the built-in tables; user tables keep their level: the bake of Render OSC Warp relies on it). */
    void build (const std::vector<std::vector<float>>& cosCoeff, const std::vector<std::vector<float>>& sinCoeff, bool normalizePeak = true);
    /** The same, checking `cancel` between frames: false (and the table unusable: the caller drops it) when it became true. */
    bool buildCancellable (const std::vector<std::vector<float>>& cosCoeff, const std::vector<std::vector<float>>& sinCoeff, bool normalizePeak, const std::atomic<bool>* cancel);

private:
    int frames = 0;
    size_t frameStride = 0;
    size_t levelOffset[kNumLevels] = {};
    std::vector<float> data, spec;
};

/** Shared immutable bank of the built-in tables (index = oscN.wavetable choice; 13 "Preset table" has no entry here: nullptr; index kNumWavetableChoices is the
    rounded rectangle of the sub oscillator). Every table is built once, on first need, by whoever needs it first and is not the audio thread.
    builtinWavetable (): the table, built on the calling thread when it is not there yet (allocates, FFTs: NOT for the audio thread; tests, tools, the overlay's drawing). */
constexpr int kRoundedRectTable = 15;
const Wavetable* builtinWavetable (int choice) noexcept;
/** The audio thread's read: the table when it is built, nullptr when it is not (yet): no build, no allocation, no lock, one acquire load. The caller NEVER substitutes another table for it: the own engine
    keeps the table an oscillator had until the new one is ready (OwnEngine, TickGlobals::wtResolved). */
const Wavetable* builtinWavetableIfReady (int choice) noexcept;
/** true when `choice` needs no wait: it is built, or it has no built-in table at all ("Preset table"). Any thread. */
bool builtinWavetableSettled (int choice) noexcept;
/** Any thread, lock free (an atomic store): oscillator slot `slot` (0 .. 5: osc 1 .. 3 wavetable, osc 1 .. 3 spectral filter table) now wants table `choice`. The background builder takes the latest
    request of every slot; a table that was passed over (a knob swept past it) is not built. A table is built at most once per process and then kept, so the cost exists only for the first use of each table. */
void requestBuiltinWavetable (int slot, int choice) noexcept;
/** Builds, on the calling thread, the tables whose bit (1 << choice) is set in `mask` (waits for a table the background builder is making). Not for the audio thread. */
void ensureBuiltinWavetables (unsigned mask) noexcept;
/** The tables of one cycle (sine, saw, square, triangle, pulse, saw down, rounded rectangle): a few milliseconds all together; the eight 64 frame tables are the cost (docs/MPC.md "Start-up time"). */
void ensureBuiltinSingleCycleTables() noexcept;
/** All tables, on the calling thread and a few helper threads (the former behaviour of the first call of builtinWavetable (); offline rendering). */
void ensureAllBuiltinWavetables() noexcept;
/** An engine that runs lazily (Quality::lazyTables) takes a share of the background builder, a low priority thread that builds the requested tables (the first user starts it). Not for the audio thread. */
void startBuiltinBankBackground() noexcept;
/** Gives the share back; when it was the last, the thread is stopped and joined: it checks the stop flag between frames of a table (a frame is a few ms on the Key 61), so the bound is one frame plus
    the thread's wake up (about 10 ms). A table in the making is dropped (never published, so nothing half built is ever read). The thread holds no pointer to an engine. */
void stopBuiltinBankBackground() noexcept;
/** TESTS ONLY. Pauses / resumes the background builder (a table it would build waits); the host-thread builds (ensure...) are not affected. */
void setBuiltinBuilderPausedForTests (bool paused) noexcept;
/** TESTS ONLY. Forgets every built table (frees them). Only valid while no engine exists and no builder thread runs. */
void resetBuiltinBankForTests() noexcept;
/** Milliseconds spent building tables so far, summed over all threads (the status file: open_ms_detail_wavetables). */
double builtinBankBuildMs() noexcept;

/** A table from decoded audio: `samples` holds numFrames * 2048 samples (frames one after the other). Not real time (allocates, FFTs). Null for no frames. */
std::unique_ptr<Wavetable> makeWavetableFromSamples (const float* samples, int numFrames);
/** Total memory of the built-in bank (all tables), in bytes. */
size_t builtinBankBytes() noexcept;
} // namespace own
} // namespace synthyboy
