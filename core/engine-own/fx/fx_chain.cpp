// SynthyBoy - own engine: the effect racks and the instance pool.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <memory>
#include "fx_chain.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace synthyboy
{
namespace own
{
namespace
{
int K (const char* fmt, ...)
{
    char key[96];
    va_list ap;
    va_start (ap, fmt);
    std::vsnprintf (key, sizeof key, fmt, ap);
    va_end (ap);
    return findParamIndex (hashKey (key));
}

FxIndex build()
{
    FxIndex f {};
    for (int i = 0; i < 16; ++i)
        f.order[i] = K ("fx.order.%d", i + 1);
    static const char* const prefixes[16] = { "chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb", "hyperdim", "utility",
                                              "bode", "convolve", "splitter_lh", "splitter_lmh", "splitter_ms" };
    for (int i = 0; i < 16; ++i)
    {
        f.rack[i] = K ("%s.rack", prefixes[i]);
        f.on[i] = K ("%s.on", prefixes[i]);
        f.level[i] = K ("%s.level", prefixes[i]);
    }
    f.lhFreq = K ("splitter_lh.freq"); f.lhLow = K ("splitter_lh.low_bypass"); f.lhHigh = K ("splitter_lh.high_bypass");
    f.lmhFreqLow = K ("splitter_lmh.freq_low"); f.lmhFreqHigh = K ("splitter_lmh.freq_high"); f.lmhLow = K ("splitter_lmh.low_bypass");
    f.lmhMid = K ("splitter_lmh.mid_bypass"); f.lmhHigh = K ("splitter_lmh.high_bypass"); f.msMid = K ("splitter_ms.mid_bypass"); f.msSide = K ("splitter_ms.side_bypass");
    return f;
}

bool finiteBlock (const float* l, const float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        if (!std::isfinite (l[i]) || !std::isfinite (r[i]))
            return false;
    return true;
}
} // namespace

const FxIndex& fxIndex() noexcept
{
    static const FxIndex index = build();
    return index;
}

// ---------------------------------------------------------------------------------------------------------------------------------- pool
FxPool::~FxPool()
{
    for (auto& s : slots)
    {
        delete s.pending.exchange (nullptr);
        for (auto& g : s.grave)
            delete g.exchange (nullptr);
        if (s.from != nullptr && s.from != s.cur)
            delete s.from;
        delete s.cur;
    }
}

void FxPool::prepare (double sampleRate, int maxBlock, const int* types)
{
    sr = sampleRate;
    maxBlk = maxBlock;
    for (int i = 0; i < kSlots; ++i)
    {
        Slot& s = slots[static_cast<size_t> (i)];
        // the modules are rebuilt for the new rate: the old ones go
        delete s.pending.exchange (nullptr);
        for (auto& g : s.grave)
            delete g.exchange (nullptr);
        if (s.from != nullptr && s.from != s.cur)
            delete s.from;
        delete s.cur;
        s.cur = s.running = s.from = nullptr;
        s.fading = false;
        s.curType = 0;
        const int t = std::clamp (types[i], 0, kNumFxInstanceTypes - 1);
        s.typeSeen = 0;
        prepared.store (true);
        if (t > 0)
            typeChanged (i, t);
    }
    prepared.store (true);
}

void FxPool::collect (int slot) noexcept
{
    Slot& s = slots[static_cast<size_t> (slot)];
    for (auto& g : s.grave)
        delete g.exchange (nullptr);
}

void FxPool::retire (Slot& s, FxModule* m) noexcept
{
    if (m == nullptr)
        return;
    for (auto& g : s.grave)
    {
        FxModule* expected = nullptr;
        if (g.compare_exchange_strong (expected, m))
            return;
    }
    // the graveyard is full (the message thread has not collected for a long time): keep the module alive in the cell of the oldest entry's place is not possible; the
    // module is leaked rather than freed on the audio thread
}

void FxPool::typeChanged (int slot, int type)
{
    if (slot < 0 || slot >= kSlots)
        return;
    Slot& s = slots[static_cast<size_t> (slot)];
    collect (slot);
    type = std::clamp (type, 0, kNumFxInstanceTypes - 1);
    if (type == s.typeSeen && (type == 0 || s.pending.load() != nullptr || s.cur != nullptr))
        return;
    s.typeSeen = type;
    if (!prepared.load())
        return;
    std::unique_ptr<FxModule> m;
    if (type > 0)
    {
        m = makeFxModule (type - 1);
        if (m != nullptr)
            m->prepare (sr, maxBlk);
    }
    FxModule* old = s.pending.exchange (m.release(), std::memory_order_acq_rel);
    s.pendingType.store (type, std::memory_order_release);
    delete old;
}

void FxPool::resetAll() noexcept
{
    for (auto& s : slots)
    {
        if (s.cur != nullptr)
            s.cur->reset();
        if (s.from != nullptr)
            s.from->reset();
        s.fading = false;
        s.from = nullptr;
        s.running = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------- chain
FxChain::FxChain() = default;
FxChain::~FxChain() = default;

void FxChain::prepare (double sampleRate, int maxBlk)
{
    sr = sampleRate;
    maxBlock = maxBlk;
    for (int m = 0; m < kNumFxModules; ++m)
    {
        inst[m] = makeFxModule (m);
        inst[m]->prepare (sr, std::max (maxBlk, kControlPeriod));
    }
    splitLh.prepare (sr, std::max (maxBlk, kControlPeriod));
    splitLmh.prepare (sr, std::max (maxBlk, kControlPeriod));
    splitMs.prepare (sr, std::max (maxBlk, kControlPeriod));
    vbase.assign (paramCount(), 0.0f);
    reset();
}

void FxChain::reset() noexcept
{
    for (auto& m : inst)
        if (m != nullptr)
            m->reset();
    splitLh.reset();
    splitLmh.reset();
    splitMs.reset();
    idleSettled = 0;
    for (int i = 0; i < 16; ++i)
    {
        levelGain[i] = levelGainPrev[i] = 1.0f;
        prevOn[i] = false;
        fadeIn[i] = false;
    }
    for (auto& m : inst)
        if (m != nullptr)
            m->clearLevel();
    if (pool != nullptr && rackId == 0)
        pool->resetAll();
}

bool FxChain::slotRuns (int s) const noexcept
{
    const FxPool::Slot& sl = pool->slot (s);
    return sl.running != nullptr;
}

void FxChain::configureSlot (int s, const float* base, const float* delta, const FxFrame& frame, uint32_t fxMask) noexcept
{
    FxPool::Slot& sl = pool->slot (s);
    const FxInstanceKeys& keys = fxInstanceKeys (s);
    const int typeKey = std::clamp (static_cast<int> (base[keys.type] + 0.5f), 0, kNumFxInstanceTypes - 1);
    slotType[s] = typeKey;
    slotOn[s] = base[keys.on] >= 0.5f;
    slotRack[s] = std::clamp (static_cast<int> (base[keys.rack] + 0.5f), 0, kNumPoolRacks - 1);
    slotOrder[s] = std::clamp (static_cast<int> (base[keys.order] + 0.5f), 0, kFxOrderMax);
    slotMine[s] = slotRack[s] == rackId;
    if (!slotMine[s])
        return;
    // the module the audio thread works with: take over a freshly created one
    FxModule* prevRunning = sl.running;
    if (FxModule* fresh = sl.pending.exchange (nullptr, std::memory_order_acq_rel))
    {
        const int t = sl.pendingType.load (std::memory_order_acquire);
        if (sl.cur != nullptr && sl.cur != prevRunning && sl.cur != sl.from)
            pool->retire (sl, sl.cur); // the old module was not sounding: it can go
        sl.cur = fresh;
        sl.curType = t;
        sl.running = nullptr;
    }
    const bool maskOk = typeKey > 0 && (fxMask & (1u << (typeKey - 1))) != 0;
    const bool shouldRun = slotOn[s] && maskOk && sl.cur != nullptr && sl.curType == typeKey;
    FxModule* want = shouldRun ? sl.cur : nullptr;
    if (want != prevRunning)
    {
        // linear 10 ms crossfade from the module that sounded (nullptr: the dry input) to the new one; a change within the previous fade drops the earlier outgoing module at once
        if (sl.fading && sl.from != nullptr && sl.from != prevRunning && sl.from != sl.cur)
            pool->retire (sl, sl.from);
        sl.from = prevRunning;
        sl.fading = true;
        sl.fadePos = 0;
        sl.fadeLen = std::max (1, static_cast<int> (sr * 0.01));
        if (want != nullptr)
        {
            want->reset();
            want->clearLevel();
        }
        sl.running = want;
    }
    if (sl.running != nullptr)
    {
        // the parameters: the plain values of the source keys from the normalised p01 .. p24 (the modulation is added in the normalised domain)
        const FxTypeDescriptor& d = fxTypeDescriptor (typeKey);
        for (int k = 0; k < d.count; ++k)
        {
            const int pk = keys.p[k];
            const float norm = clamp01 (modulatedValue (paramTable()[static_cast<size_t> (pk)], destLawOf (static_cast<size_t> (pk)), base[pk], delta != nullptr ? delta[pk] : 0.0f));
            vbase[static_cast<size_t> (d.source[k])] = fxInstancePlain (typeKey, k, norm);
        }
        sl.running->update (frame, vbase.data(), nullptr);
    }
}

void FxChain::buildPlan (const float*) noexcept
{
    for (auto& p : plan)
        p.count = 0;
    auto add = [&] (int rack, bool isPool, int index, int key)
    {
        Plan& p = plan[static_cast<size_t> (rack)];
        if (p.count >= 32)
            return;
        Item it { isPool, index, key };
        int at = p.count;
        // stable insertion: ascending key; on a tie the module of instance 0 first, then the slots by number
        while (at > 0)
        {
            const Item& prev = p.item[static_cast<size_t> (at - 1)];
            const bool before = it.key < prev.key || (it.key == prev.key && !it.pool && prev.pool);
            if (!before)
                break;
            p.item[static_cast<size_t> (at)] = prev;
            --at;
        }
        p.item[static_cast<size_t> (at)] = it;
        ++p.count;
    };
    for (int pos = 0; pos < 16; ++pos)
    {
        const int m = order[pos];
        if (on[m])
            add (moduleRack[m], false, m, fxModuleSortKey (pos));
    }
    if (pool != nullptr)
        for (int s = 0; s < FxPool::kSlots; ++s)
            if (slotMine[s] && (pool->slot (s).running != nullptr || pool->slot (s).fading))
                add (slotRack[s], true, s, slotOrder[s]);
}

void FxChain::update (const float* base, const float* delta, const FxFrame& frame, uint32_t fxMask, bool inputsSame) noexcept
{
    if (inputsSame && idleSettled >= 2 && !active && (pool == nullptr || !pool->anyPending()))
    {
        configured = false; // nothing is on, nothing changed, and the bookkeeping below has already reached its fixed point
        return;
    }
    configured = true;
    const FxIndex& f = fxIndex();
    period = frame.period;
    active = false;
    // fx.order: a position holds one module; a repeated choice is repaired (the lowest free module takes the position)
    bool used[16] = {};
    for (int i = 0; i < 16; ++i)
    {
        int m = f.order[i] >= 0 ? std::clamp (static_cast<int> (base[f.order[i]] + 0.5f), 0, 15) : i;
        if (used[m])
        {
            m = 0;
            while (m < 16 && used[m])
                ++m;
        }
        used[std::min (m, 15)] = true;
        order[i] = std::min (m, 15);
    }
    for (int m = 0; m < 16; ++m)
        on[m] = f.on[m] >= 0 && base[f.on[m]] >= 0.5f && (fxMask & (1u << m)) != 0;
    // the rack of every module (the splitters only sit in a bus: a band value clamps to Main) and who owns it: this chain, if the module sits in this rack, or
    // in the band of a splitter that is on and sits in this rack
    for (int m = 0; m < 16; ++m)
    {
        int r = f.rack[m] >= 0 ? std::clamp (static_cast<int> (base[f.rack[m]] + 0.5f), 0, 9) : 0;
        if (m >= 13 && r >= 3)
            r = 0;
        moduleRack[m] = r;
    }
    bool splitterHere[3] = {};
    for (int s = 0; s < 3; ++s)
        splitterHere[s] = on[13 + s] && moduleRack[13 + s] == rackId;
    for (int m = 0; m < 16; ++m)
    {
        const int r = moduleRack[m];
        bool mine = r == rackId;
        if (r >= 3)
            mine = (r <= 4 && splitterHere[0]) || (r >= 5 && r <= 7 && splitterHere[1]) || (r >= 8 && splitterHere[2]);
        on[m] = on[m] && mine;
        if (m >= 13)
        {
            levelGainPrev[m] = levelGain[m];
            levelGain[m] = f.level[m] >= 0 ? dbToGain (fxValue (base, delta, f.level[m])) : 1.0f;
        }
        active = active || on[m];
    }
    // modules: configure the ones that are on (a module that was off starts clean and fades in over one period)
    for (int m = 0; m < kNumFxModules; ++m)
    {
        if (on[m])
        {
            if (!prevOn[m])
            {
                inst[m]->reset();
                inst[m]->clearLevel();
                fadeIn[m] = true;
            }
            inst[m]->update (frame, base, delta);
        }
        prevOn[m] = on[m];
    }
    for (int m = 13; m < 16; ++m)
        prevOn[m] = on[m];
    // the pool
    if (pool != nullptr)
        for (int s = 0; s < FxPool::kSlots; ++s)
        {
            configureSlot (s, base, delta, frame, fxMask);
            if (slotMine[s] && (pool->slot (s).running != nullptr || pool->slot (s).fading))
                active = true;
        }
    if (on[13])
    {
        fx::SplitterLH::Params p;
        p.enabled = true;
        p.splitFreq = fxValue (base, delta, f.lhFreq);
        p.lowBypass = base[f.lhLow] >= 0.5f;
        p.highBypass = base[f.lhHigh] >= 0.5f;
        splitLh.setParams (p);
    }
    if (on[14])
    {
        fx::SplitterLMH::Params p;
        p.enabled = true;
        p.splitFreq1 = fxValue (base, delta, f.lmhFreqLow);
        p.splitFreq2 = fxValue (base, delta, f.lmhFreqHigh);
        p.lowBypass = base[f.lmhLow] >= 0.5f;
        p.midBypass = base[f.lmhMid] >= 0.5f;
        p.highBypass = base[f.lmhHigh] >= 0.5f;
        splitLmh.setParams (p);
    }
    if (on[15])
    {
        fx::SplitterMS::Params p;
        p.enabled = true;
        p.midBypass = base[f.msMid] >= 0.5f;
        p.sideBypass = base[f.msSide] >= 0.5f;
        splitMs.setParams (p);
    }
    buildPlan (base);
    idleSettled = (inputsSame && !active) ? idleSettled + 1 : 0;
}

int FxChain::rackLatency (int rack, bool reported) const noexcept
{
    if (rack < 0 || rack >= 10)
        return 0;
    int sum = 0;
    const Plan& p = plan[static_cast<size_t> (rack)];
    for (int i = 0; i < p.count; ++i)
    {
        const Item& it = p.item[static_cast<size_t> (i)];
        if (it.pool)
        {
            const FxPool::Slot& sl = pool->slot (it.index);
            if (sl.running != nullptr && (!reported || sl.running->latencyReported()))
                sum += sl.running->latency();
        }
        else if (it.index < kNumFxModules)
        {
            const FxModule& m = *inst[it.index];
            if (!reported || m.latencyReported())
                sum += m.latency();
        }
        else if (it.index == 13)
            sum += std::max (rackLatency (3, reported), rackLatency (4, reported));
        else if (it.index == 14)
            sum += std::max ({ rackLatency (5, reported), rackLatency (6, reported), rackLatency (7, reported) });
        else if (it.index == 15)
            sum += std::max (rackLatency (8, reported), rackLatency (9, reported));
    }
    return sum;
}

void FxChain::runModule (int m, float* l, float* r, int n, int t0, int depth) noexcept
{
    if (m < kNumFxModules)
    {
        if (fadeIn[m])
        {
            float dl[kControlPeriod], dr[kControlPeriod];
            std::copy (l, l + n, dl);
            std::copy (r, r + n, dr);
            inst[m]->process (l, r, n, t0, period);
            const float invP = 1.0f / static_cast<float> (period);
            for (int i = 0; i < n; ++i)
            {
                const float t = clamp01 (static_cast<float> (t0 + i + 1) * invP); // the mix starts at 0 and reaches its value across the first period
                l[i] = dl[i] + (l[i] - dl[i]) * t;
                r[i] = dr[i] + (r[i] - dr[i]) * t;
            }
            if (t0 + n >= period)
                fadeIn[m] = false;
        }
        else
            inst[m]->process (l, r, n, t0, period);
        if (!finiteBlock (l, r, n))
        {
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);
            inst[m]->reset();
        }
        return;
    }
    switch (m)
    {
        case 13: // splitter L/H: the bands run through their racks (3 low, 4 high) and are summed again
        {
            float bl[2][kControlPeriod], br[2][kControlPeriod];
            splitLh.split (l, r, bl[0], br[0], bl[1], br[1], n);
            runRack (3, bl[0], br[0], n, t0, depth + 1);
            runRack (4, bl[1], br[1], n, t0, depth + 1);
            splitLh.merge (bl[0], br[0], bl[1], br[1], l, r, n);
            break;
        }
        case 14:
        {
            float bl[3][kControlPeriod], br[3][kControlPeriod];
            splitLmh.split (l, r, bl[0], br[0], bl[1], br[1], bl[2], br[2], n);
            runRack (5, bl[0], br[0], n, t0, depth + 1);
            runRack (6, bl[1], br[1], n, t0, depth + 1);
            runRack (7, bl[2], br[2], n, t0, depth + 1);
            splitLmh.merge (bl[0], br[0], bl[1], br[1], bl[2], br[2], l, r, n);
            break;
        }
        case 15:
        {
            float bl[2][kControlPeriod], br[2][kControlPeriod];
            splitMs.split (l, r, bl[0], br[0], bl[1], br[1], n);
            runRack (8, bl[0], br[0], n, t0, depth + 1);
            runRack (9, bl[1], br[1], n, t0, depth + 1);
            splitMs.merge (bl[0], br[0], bl[1], br[1], l, r, n);
            break;
        }
        default: break;
    }
    if (levelGain[m] != 1.0f || levelGainPrev[m] != 1.0f)
    {
        const float invP = 1.0f / static_cast<float> (period);
        for (int i = 0; i < n; ++i)
        {
            const float g = lerp (levelGainPrev[m], levelGain[m], static_cast<float> (t0 + i + 1) * invP);
            l[i] *= g;
            r[i] *= g;
        }
    }
}

/** A slot: a linear 10 ms crossfade between the outgoing and the incoming module (nullptr = the dry input), both fed with the same input. */
void FxChain::runSlot (int s, float* l, float* r, int n, int t0) noexcept
{
    FxPool::Slot& sl = pool->slot (s);
    auto runOne = [&] (FxModule* m, float* a, float* b)
    {
        m->process (a, b, n, t0, period);
        if (!finiteBlock (a, b, n))
        {
            std::fill (a, a + n, 0.0f);
            std::fill (b, b + n, 0.0f);
            m->reset();
        }
    };
    if (!sl.fading)
    {
        if (sl.running != nullptr)
            runOne (sl.running, l, r);
        return;
    }
    float* fa = scratchA[0];
    float* fb = scratchA[1];
    float* ta = scratchB[0];
    float* tb = scratchB[1];
    std::copy (l, l + n, fa);
    std::copy (r, r + n, fb);
    std::copy (l, l + n, ta);
    std::copy (r, r + n, tb);
    if (sl.from != nullptr)
        runOne (sl.from, fa, fb);
    if (sl.running != nullptr)
        runOne (sl.running, ta, tb);
    const float inv = 1.0f / static_cast<float> (sl.fadeLen);
    for (int i = 0; i < n; ++i)
    {
        const float t = clamp01 (static_cast<float> (sl.fadePos + i + 1) * inv);
        l[i] = fa[i] * (1.0f - t) + ta[i] * t;
        r[i] = fb[i] * (1.0f - t) + tb[i] * t;
    }
    sl.fadePos += n;
    if (sl.fadePos >= sl.fadeLen)
    {
        sl.fading = false;
        if (sl.from != nullptr && sl.from != sl.cur)
            pool->retire (sl, sl.from);
        sl.from = nullptr;
    }
}

/** The modules of one rack in the order of their sort keys (a module at position q of fx.order sorts as 64 q + 32, a slot by its order value). */
void FxChain::runRack (int rack, float* l, float* r, int n, int t0, int depth) noexcept
{
    if (depth > 2)
        return;
    const Plan& p = plan[static_cast<size_t> (rack)];
    for (int i = 0; i < p.count; ++i)
    {
        const Item& it = p.item[static_cast<size_t> (i)];
        if (it.pool)
            runSlot (it.index, l, r, n, t0);
        else
            runModule (it.index, l, r, n, t0, depth);
    }
}

void FxChain::process (float* l, float* r, int n, int t0) noexcept
{
    if (!active)
        return;
    runRack (rackId, l, r, n, t0, 0);
}
} // namespace own
} // namespace synthyboy
