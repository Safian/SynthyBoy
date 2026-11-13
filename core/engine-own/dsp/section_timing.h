// SynthyBoy - own engine: where the time of a block goes, by section (a build switch for the benchmark, SB_SECTION_TIMING; off, the macros are empty and the engine is the
// same code, bit for bit). Scopes nest; a scope's time is the exclusive time (its own, without the scopes opened inside it), so the sections add up to the whole process () call.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace synthyboy
{
namespace own
{
namespace sections
{
enum Id : int
{
    engineRest = 0,       // process (): the block snapshot, the control tick of the engine, the global modulation, the bus mixing, the master stage
    tickOther,            // Voice::tick: pitch, parameters of the sources, filter settings
    tickEnvLfo,           // envelopes, LFOs, random LFOs
    tickMatrix,           // the modulation matrix of the voice
    tickOscUpdate,        // oscillator update (unison layout, frequencies, gains) and library oscillator update
    oscMorphBuild,        // the spectral morph cycles (buildMorphCycle)
    renderOther,          // Voice::render: routing, mixing, the output stage
    oscTable,             // wavetable oscillator, plain table read
    oscMorph,             // wavetable oscillator reading morph cycles
    oscWarp,              // wavetable oscillator with a warp (oversampled path)
    oscLib,               // sample / granular / spectral / multisample oscillators
    sub,
    noise,
    filter1,
    filter2,
    fxUpdate,             // FxChain::update
    fxModule0,            // 13 modules of the chain: Chorus, Compressor, Delay, Distortion, EQ, FX filter, Flanger, Phaser, Reverb, Hyper/Dim, Utility, Bode, Convolve
    fxSplitters = fxModule0 + 13, // the splitters (their bands are counted in the modules)
    fxPool,               // the instance pool slots
    tickOscParams,        // Voice::tick: the parameters of the three oscillators (the reads and the modulated values), without the oscillator update
    tickFilterParams,     // Voice::tick: the settings of the two filters
    tickSources,          // Voice::tick: the modulation sources of the period (the values of the envelopes, LFOs, the note, the controllers)
    tickSub,              // Voice::tick: the sub oscillator
    tickAmp,              // Voice::tick: amplitude, velocity, the end of the note
    renderFinal,          // Voice::render: amplitude, velocity, declick and the add to the buses
    kCount
};

inline const char* name (int id) noexcept
{
    static const char* const kNames[kCount] = {
        "engine rest (snapshot, control tick, buses, master)", "voice tick: other", "voice tick: envelopes + LFOs", "voice tick: modulation matrix",
        "voice tick: oscillator update", "osc: morph cycle build", "voice render: routing / mix / output", "osc: wavetable read", "osc: morph read", "osc: warp + decimator",
        "osc: sample / granular / spectral", "sub oscillator", "noise", "filter 1", "filter 2", "fx: update", "fx: chorus", "fx: compressor", "fx: delay", "fx: distortion",
        "fx: eq", "fx: fx filter", "fx: flanger", "fx: phaser", "fx: reverb", "fx: hyper / dimension", "fx: utility", "fx: bode", "fx: convolve", "fx: splitters", "fx: pool slots", "voice tick: oscillator parameters", "voice tick: filter settings", "voice tick: sources", "voice tick: sub", "voice tick: amplitude", "voice render: amplitude / velocity / output" };
    return id >= 0 && id < kCount ? kNames[id] : "?";
}

struct Totals
{
    uint64_t ns[kCount] = {};
    uint64_t calls[kCount] = {};
};
Totals& totals() noexcept; // section_timing.cpp (the engine library); the audio thread writes, the bench reads between blocks
uint64_t nowNs() noexcept;
} // namespace sections
} // namespace own
} // namespace synthyboy

#ifdef SB_SECTION_TIMING
namespace synthyboy
{
namespace own
{
namespace sections
{
struct Scope
{
    explicit Scope (Id i) noexcept : id (i), parent (current()), start (nowNs()) { current() = this; }
    ~Scope()
    {
        const uint64_t elapsed = nowNs() - start;
        Totals& t = totals();
        t.ns[id] += elapsed - child;
        t.calls[id] += 1;
        current() = parent;
        if (parent != nullptr)
            parent->child += elapsed;
    }
    static Scope*& current() noexcept { static Scope* p = nullptr; return p; }
    Id id;
    Scope* parent;
    uint64_t start;
    uint64_t child = 0;
};
} // namespace sections
} // namespace own
} // namespace synthyboy
 #define SB_SECTION_CAT2(a, b) a##b
 #define SB_SECTION_CAT(a, b) SB_SECTION_CAT2 (a, b)
 #define SB_SECTION(id) ::synthyboy::own::sections::Scope SB_SECTION_CAT (sbSection, __LINE__) (id)
#else
 #define SB_SECTION(id) ((void) 0)
#endif
