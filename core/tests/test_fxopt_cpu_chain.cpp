// SynthyBoy - core tests: FxChain::update after the CPU review items 5 and 6 (2026-11-26) is bit identical to the code that derived everything on every period.
//  - the splitter level gains are cached on the exact dB bits;
//  - a pool slot without a type, not running and not fading takes no atomic exchange and no configuration;
//  - once the derivation (order, on, rack, plan, latency delays, echo scan, splitter settings) ran twice on the same inputs with nothing moving (no bypassed module, no slot with a type,
//    no splitter), it is not redone: only the modules' own update () runs.
// Two rigs, each with the three racks (Main, Bus 1, Bus 2) and its own pool, run in step on random control period walks (modules on and off, racks, order, levels, the mask, slots with
// types, parameters, global modulation that moves and returns to exactly 0, inputsSame as the engine computes it); one rig runs with makeup::setLegacyCachesForTest (true), the old path
// (every period derived, every cache bypassed). Every output sample, the latency of every rack and the active state are compared after every period.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "fx/fx_chain.h"
#include "fx/fx_makeup.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
constexpr double kSr = 44100.0;
constexpr int kP = kControlPeriod;

float uni (std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * static_cast<float> (rng() % 100001) / 100000.0f; }

/** The three racks of one engine and its pool. */
struct Rig
{
    FxPool pool;
    FxChain chain[3];
    std::vector<float> base, delta;
    explicit Rig (bool lowCost)
    {
        int types[kNumFxInstances] = {};
        pool.prepare (kSr, kP, types);
        for (int r = 0; r < 3; ++r)
        {
            chain[r].prepare (kSr, kP);
            chain[r].setRack (r);
            chain[r].setPool (&pool);
            chain[r].setLowCost (lowCost);
            chain[r].setBusDeferred (r != 0);
        }
        base.resize (paramCount());
        delta.assign (paramCount(), 0.0f);
        for (size_t i = 0; i < paramCount(); ++i)
            base[i] = paramTable()[i].def;
    }
};

int keyOf (const char* k) { return fxModuleKeyIndex (k); }

/** One scenario step: what changes in the rig (the same call on both rigs). */
struct Edit
{
    enum Kind { moduleOn, moduleRack, orderSwap, level, maskChange, param, globalMod, slotType, slotSetup, splitterFreq, none } kind = none;
    int a = 0, b = 0;
    float v = 0.0f;
    uint32_t mask = 0;
};

const char* const kParamKeys[] = { "chorus.mix", "delay.mix", "delay.feedback", "compressor.mix", "distortion.mix", "phaser.mix", "flanger.mix", "eq.low.gain", "fxfilter.mix", "utility.gain" };

void apply (Rig& rig, const Edit& e, uint32_t& mask)
{
    const FxIndex& f = fxIndex();
    switch (e.kind)
    {
        case Edit::moduleOn: rig.base[static_cast<size_t> (f.on[e.a])] = e.v; break;
        case Edit::moduleRack: if (f.rack[e.a] >= 0) rig.base[static_cast<size_t> (f.rack[e.a])] = static_cast<float> (e.b); break;
        case Edit::orderSwap:
        {
            float& x = rig.base[static_cast<size_t> (f.order[e.a])];
            float& y = rig.base[static_cast<size_t> (f.order[e.b])];
            std::swap (x, y);
            break;
        }
        case Edit::level: if (f.level[e.a] >= 0) rig.base[static_cast<size_t> (f.level[e.a])] = e.v; break;
        case Edit::maskChange: mask = e.mask; break;
        case Edit::param:
        {
            const int k = keyOf (kParamKeys[e.a]);
            if (k >= 0)
                rig.base[static_cast<size_t> (k)] = e.v;
            break;
        }
        case Edit::globalMod:
        {
            const int k = keyOf (kParamKeys[e.a]);
            if (k >= 0)
                rig.delta[static_cast<size_t> (k)] = e.v; // 0: the modulation returns to exactly zero
            break;
        }
        case Edit::slotType:
        {
            const FxInstanceKeys& keys = fxInstanceKeys (e.a);
            rig.base[static_cast<size_t> (keys.type)] = static_cast<float> (e.b);
            float norm[kFxInstanceParams];
            fxInstanceDefaults (e.b, norm);
            const FxTypeDescriptor& d = fxTypeDescriptor (e.b);
            for (int k = 0; k < d.count; ++k)
                rig.base[static_cast<size_t> (keys.p[k])] = norm[k];
            rig.pool.typeChanged (e.a, e.b); // (the engine does this on its builder thread; the module is handed over to the next update)
            break;
        }
        case Edit::slotSetup:
        {
            const FxInstanceKeys& keys = fxInstanceKeys (e.a);
            rig.base[static_cast<size_t> (keys.on)] = e.v;
            rig.base[static_cast<size_t> (keys.rack)] = static_cast<float> (e.b % 3);
            rig.base[static_cast<size_t> (keys.order)] = static_cast<float> ((e.b * 97) % 1000);
            break;
        }
        case Edit::splitterFreq:
        {
            const int k = e.a == 0 ? f.lhFreq : (e.a == 1 ? f.lmhFreqLow : f.lmhFreqHigh);
            if (k >= 0)
                rig.base[static_cast<size_t> (k)] = e.v;
            break;
        }
        case Edit::none: break;
    }
}

Edit randomEdit (std::mt19937& rng, bool allowSlots, bool allowSplitters)
{
    Edit e;
    switch (rng() % 11)
    {
        case 0: case 1: e.kind = Edit::moduleOn; { static const int modules[] = { 0, 1, 2, 3, 4, 5, 6, 7, 10, 8, 9, 11, 12, 13, 14, 15 }; e.a = modules[rng() % (allowSplitters ? 16 : 13)]; e.v = rng() % 2 == 0 ? 1.0f : 0.0f; } break;
        case 2: e.kind = Edit::moduleRack; e.a = static_cast<int> (rng() % 13); e.b = static_cast<int> (rng() % (allowSplitters ? 10 : 3)); break;
        case 3: e.kind = Edit::orderSwap; e.a = static_cast<int> (rng() % 16); e.b = static_cast<int> (rng() % 16); break;
        case 4: e.kind = Edit::level; e.a = 10 + static_cast<int> (rng() % 6); e.v = rng() % 3 == 0 ? 0.0f : uni (rng, -12.0f, 6.0f); break;
        case 5:
            e.kind = Edit::maskChange;
            e.mask = 0xffffu & ~((1u << 8) | (1u << 9) | (1u << 11) | (1u << 12)); // the heavy effects stay switched off by the mask (their makeup state is what runs for them)
            if (rng() % 2 == 0) e.mask &= ~(1u << (rng() % 8));
            if (rng() % 4 == 0) e.mask &= ~(1u << 10);
            break;
        case 6: e.kind = Edit::param; e.a = static_cast<int> (rng() % 10); e.v = uni (rng, 0.0f, 100.0f); break;
        case 7: e.kind = Edit::globalMod; e.a = static_cast<int> (rng() % 10); e.v = rng() % 3 == 0 ? 0.0f : uni (rng, -20.0f, 20.0f); break;
        case 8: if (allowSlots) { e.kind = Edit::slotType; e.a = static_cast<int> (rng() % 16); static const int types[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 11 }; e.b = types[rng() % 10]; } break;
        case 9: if (allowSlots) { e.kind = Edit::slotSetup; e.a = static_cast<int> (rng() % 16); e.b = static_cast<int> (rng() % 1000); e.v = rng() % 3 == 0 ? 0.0f : 1.0f; } break;
        default: if (allowSplitters) { e.kind = Edit::splitterFreq; e.a = static_cast<int> (rng() % 3); e.v = uni (rng, 100.0f, 6000.0f); } break;
    }
    return e;
}

bool sameSamples (const float* a, const float* b, int n)
{
    for (int i = 0; i < n; ++i)
        if (std::memcmp (&a[i], &b[i], 4) != 0 && !(std::isnan (a[i]) && std::isnan (b[i])))
            return false;
    return true;
}

struct Outcome
{
    bool same = true;
    int periods = 0, settledPeriods = 0;
};

/** One walk. `hold`: the probability (percent) that a period keeps the inputs of the one before (the usual case of a playing preset). */
Outcome walk (uint32_t seed, int periods, int hold, bool allowSlots, bool allowSplitters, bool lowCost, bool bypassOk)
{
    std::mt19937 rng (seed);
    Rig a (lowCost), b (lowCost);
    uint32_t maskA = 0xffffu & ~((1u << 8) | (1u << 9) | (1u << 11) | (1u << 12)), maskB = maskA;
    if (!bypassOk)
        maskA = maskB = 0xffffu & ~((1u << 8) | (1u << 9) | (1u << 11) | (1u << 12)); // (the same; the heavy effects are only ever bypassed by the mask)
    Outcome out;
    // a start that has modules on: the first edits
    for (int i = 0; i < 6; ++i)
    {
        const Edit e = randomEdit (rng, allowSlots, allowSplitters);
        apply (a, e, maskA);
        apply (b, e, maskB);
    }
    FxFrame frame;
    frame.lowCost = lowCost;
    frame.makeup = bypassOk;
    for (int per = 0; per < periods; ++per)
    {
        const bool change = static_cast<int> (rng() % 100) >= hold;
        if (change)
        {
            const int count = 1 + static_cast<int> (rng() % 3);
            for (int i = 0; i < count; ++i)
            {
                const Edit e = randomEdit (rng, allowSlots, allowSplitters);
                apply (a, e, maskA);
                apply (b, e, maskB);
            }
        }
        const bool inputsSame = !change && per > 0;
        frame.songBeats += 0.07; // moves every period: the modules read it in update ()
        frame.noteOns = static_cast<uint32_t> (per / 40);
        float in[3][2][kP];
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < kP; ++i)
            {
                in[r][0][i] = uni (rng, -0.5f, 0.5f);
                in[r][1][i] = uni (rng, -0.5f, 0.5f);
            }
        for (int r = 0; r < 3; ++r)
        {
            float la[kP], ra[kP], lb[kP], rb[kP];
            std::memcpy (la, in[r][0], sizeof la); std::memcpy (ra, in[r][1], sizeof ra);
            std::memcpy (lb, in[r][0], sizeof lb); std::memcpy (rb, in[r][1], sizeof rb);
            makeup::setLegacyCachesForTest (false);
            a.chain[r].update (a.base.data(), a.delta.data(), frame, maskA, inputsSame);
            a.chain[r].process (la, ra, kP, 0);
            makeup::setLegacyCachesForTest (true);
            b.chain[r].update (b.base.data(), b.delta.data(), frame, maskB, inputsSame);
            b.chain[r].process (lb, rb, kP, 0);
            makeup::setLegacyCachesForTest (false);
            if (!sameSamples (la, lb, kP) || !sameSamples (ra, rb, kP))
            {
                if (out.same)
                    std::printf ("chain differs: seed %u period %d rack %d\n", seed, per, r);
                out.same = false;
            }
            if (a.chain[r].anyActive() != b.chain[r].anyActive())
                out.same = false;
            for (int k = 0; k < 10; ++k)
                if (a.chain[r].rackLatency (k, false) != b.chain[r].rackLatency (k, false) || a.chain[r].rackLatency (k, true) != b.chain[r].rackLatency (k, true))
                    out.same = false;
            int bpA = 0, bpB = 0;
            float pa, ca, aa, pb, cb, ab;
            bpA = a.chain[r].busMakeup (pa, ca, aa) ? 1 : 0;
            bpB = b.chain[r].busMakeup (pb, cb, ab) ? 1 : 0;
            if (bpA != bpB || std::memcmp (&pa, &pb, 4) != 0 || std::memcmp (&ca, &cb, 4) != 0 || std::memcmp (&aa, &ab, 4) != 0)
                out.same = false;
        }
        ++out.periods;
    }
    return out;
}
} // namespace

// The usual case: the parameters stay (80 to 95 percent of the periods keep the inputs), modules on and off in the three racks, the mask, global modulation that moves and returns to zero.
TEST (chain_update_is_bit_identical_to_deriving_everything_every_period)
{
    int walks = 0;
    long long periods = 0;
    for (uint32_t seed = 1; seed <= 24; ++seed)
    {
        const Outcome o = walk (seed * 2654435761u, 500, 80 + static_cast<int> (seed % 4) * 5, false, false, (seed & 1) != 0, (seed % 3) != 0);
        CHECK (o.same);
        periods += o.periods;
        ++walks;
    }
    std::printf ("chain walks (no slots, no splitters): %d walks, %lld periods, bit identical to the old derivation\n", walks, periods);
}

// With splitters (their band racks, the latency delays) and with pool slots (types, on, racks, orders, the crossfades).
TEST (chain_update_is_bit_identical_with_splitters_and_pool_slots)
{
    int walks = 0;
    for (uint32_t seed = 1; seed <= 16; ++seed)
    {
        const Outcome o = walk (seed * 40503u + 7u, 400, 85, (seed & 1) == 0, (seed & 2) != 0, (seed & 4) != 0, true);
        CHECK (o.same);
        ++walks;
    }
    std::printf ("chain walks (slots / splitters): %d walks\n", walks);
}

// A scripted case: a module is on, its mix is modulated globally (the inputs change), the modulation returns to exactly 0 and stays; the settled path must pick up the module's value of
// the base again (the sound after the return is the sound without the modulation).
TEST (chain_global_modulation_that_returns_to_zero_is_followed)
{
    for (int round = 0; round < 2; ++round)
    {
        Rig a (false), b (false);
        const FxIndex& f = fxIndex();
        const int chorusMix = keyOf ("chorus.mix");
        CHECK (chorusMix >= 0);
        for (Rig* r : { &a, &b })
        {
            r->base[static_cast<size_t> (f.on[0])] = 1.0f; // chorus on
            r->base[static_cast<size_t> (chorusMix)] = 40.0f;
        }
        uint32_t mask = 0xffffu & ~((1u << 8) | (1u << 9) | (1u << 11) | (1u << 12));
        std::mt19937 rng (42u + static_cast<unsigned> (round));
        FxFrame frame;
        bool same = true;
        for (int per = 0; per < 200; ++per)
        {
            // periods 0 .. 59 still, 60 .. 99 modulated (changing every period), 100 .. 199 back at 0: still
            const bool modulated = per >= 60 && per < 100;
            const float d = modulated ? 30.0f * std::sin (0.3f * static_cast<float> (per)) : 0.0f;
            const bool inputsSame = per > 0 && per != 60 && !modulated && per != 100;
            a.delta[static_cast<size_t> (chorusMix)] = b.delta[static_cast<size_t> (chorusMix)] = d;
            float la[kP], ra[kP], lb[kP], rb[kP];
            for (int i = 0; i < kP; ++i)
                la[i] = lb[i] = ra[i] = rb[i] = uni (rng, -0.5f, 0.5f);
            makeup::setLegacyCachesForTest (false);
            a.chain[0].update (a.base.data(), a.delta.data(), frame, mask, inputsSame);
            a.chain[0].process (la, ra, kP, 0);
            makeup::setLegacyCachesForTest (true);
            b.chain[0].update (b.base.data(), b.delta.data(), frame, mask, inputsSame);
            b.chain[0].process (lb, rb, kP, 0);
            makeup::setLegacyCachesForTest (false);
            same = same && sameSamples (la, lb, kP) && sameSamples (ra, rb, kP);
        }
        CHECK (same);
    }
}
