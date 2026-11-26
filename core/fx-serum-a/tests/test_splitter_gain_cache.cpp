// SynthyBoy - the splitters' dB gain memo is bit identical to the uncached merge ().
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// OldLH / OldLMH / OldMS below are FROZEN copies of the reset () / merge () / process () bodies of the
// splitters as they were before the memo (every dbToGain () call evaluated afresh). They must not be
// edited to follow the production code: that is the reference the memo is compared with.

#include "test_runner.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "synthyboy/fx/dsp_utils.h"
#include "synthyboy/fx/splitter_gain_cache.h"
#include "synthyboy/fx/splitter_lh.h"
#include "synthyboy/fx/splitter_lmh.h"
#include "synthyboy/fx/splitter_ms.h"

using namespace synthyboy::fx;

namespace
{
std::uint32_t bitsOf (float f)
{
    std::uint32_t b;
    std::memcpy (&b, &f, sizeof b);
    return b;
}

float floatFromBits (std::uint32_t b)
{
    float f;
    std::memcpy (&f, &b, sizeof f);
    return f;
}

/** Same float: equal bits, or both NaN (the payload of a NaN that went through arithmetic is not part of the contract). */
bool sameFloat (float a, float b)
{
    return bitsOf (a) == bitsOf (b) || (std::isnan (a) && std::isnan (b));
}

std::vector<float> dbValues (std::mt19937& rng)
{
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> v = { 0.0f, -0.0f, 1.0e-45f, -1.0e-45f, 1.0e-39f, -1.0e-39f, 1.17549435e-38f, 1.0e-30f, -1.0e-30f,
                             -59.98f, -59.99f, -59.990005f, -59.989994f, -60.0f, -100.0f, -1000.0f, 12.0f, 24.0f, -24.0f, 0.001f, -0.001f,
                             6.0206f, -6.0206f, 40.0f, 1.0e10f, -1.0e10f, FLT_MAX, -FLT_MAX, FLT_MIN, inf, -inf,
                             std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN(),
                             floatFromBits (0x7fa00001u), floatFromBits (0xffc12345u) };
    std::uniform_real_distribution<float> range (-90.0f, 40.0f);
    for (int i = 0; i < 3000; ++i)
        v.push_back (range (rng));
    for (int i = 0; i < 3000; ++i)
        v.push_back (floatFromBits (static_cast<std::uint32_t> (rng())));   // random bit patterns: every exponent, NaNs, denormals
    return v;
}

// ---- frozen pre-memo copies ------------------------------------------------------------------------------------------------------------------------------------------

struct OldLH
{
    SplitterLH::Params params;
    float cLow = 1.0f, cHigh = 1.0f, cMix = 1.0f, cLevel = 1.0f;

    void reset()
    {
        cLow = params.lowBypass ? 0.0f : dbToGain (params.lowGainDb);
        cHigh = params.highBypass ? 0.0f : dbToGain (params.highGainDb);
        cMix = params.mix;
        cLevel = dbToGain (params.levelDb);
    }

    void merge (const float* lowL, const float* lowR, const float* highL, const float* highR, float* outL, float* outR, int n)
    {
        if (n <= 0)
            return;
        const float targetLowGain = params.lowBypass ? 0.0f : dbToGain (params.lowGainDb);
        const float targetHighGain = params.highBypass ? 0.0f : dbToGain (params.highGainDb);
        const float targetMix = std::clamp (params.mix, 0.0f, 1.0f);
        const float targetLevel = dbToGain (params.levelDb);
        const float invN = 1.0f / static_cast<float> (n);
        const float stepLowGain = (targetLowGain - cLow) * invN;
        const float stepHighGain = (targetHighGain - cHigh) * invN;
        const float stepMix = (targetMix - cMix) * invN;
        const float stepLevel = (targetLevel - cLevel) * invN;
        for (int i = 0; i < n; ++i)
        {
            cLow += stepLowGain;
            cHigh += stepHighGain;
            cMix += stepMix;
            cLevel += stepLevel;
            const float wetL = lowL[i] * cLow + highL[i] * cHigh;
            const float wetR = lowR[i] * cLow + highR[i] * cHigh;
            const float dryL = lowL[i] + highL[i];
            const float dryR = lowR[i] + highR[i];
            const float outSampleL = dryL + (wetL - dryL) * cMix;
            const float outSampleR = dryR + (wetR - dryR) * cMix;
            outL[i] = flushDenormal (outSampleL * cLevel);
            outR[i] = flushDenormal (outSampleR * cLevel);
        }
        cLow = targetLowGain;
        cHigh = targetHighGain;
        cMix = targetMix;
        cLevel = targetLevel;
    }

    /** The old process (): the bypass test, then the chunk loop with the (unchanged) split () of `splitter` and the old merge (). */
    void process (SplitterLH& splitter, int maxBlock, float* l, float* r, int n)
    {
        if (n <= 0 || ! params.enabled)
            return;
        const float targetMix = std::clamp (params.mix, 0.0f, 1.0f);
        const float targetLevel = dbToGain (params.levelDb);
        if (targetMix == 0.0f && targetLevel == 1.0f && cMix == 0.0f && cLevel == 1.0f)
            return;
        std::vector<float> a (static_cast<size_t> (maxBlock)), b (a), c (a), d (a);
        int processed = 0;
        while (processed < n)
        {
            const int block = std::min (n - processed, maxBlock);
            splitter.split (l + processed, r + processed, a.data(), b.data(), c.data(), d.data(), block);
            merge (a.data(), b.data(), c.data(), d.data(), l + processed, r + processed, block);
            processed += block;
        }
    }
};

struct OldLMH
{
    SplitterLMH::Params params;
    float cLow = 1.0f, cMid = 1.0f, cHigh = 1.0f, cMix = 1.0f, cLevel = 1.0f;

    void reset()
    {
        cLow = params.lowBypass ? 0.0f : dbToGain (params.lowGainDb);
        cMid = params.midBypass ? 0.0f : dbToGain (params.midGainDb);
        cHigh = params.highBypass ? 0.0f : dbToGain (params.highGainDb);
        cMix = params.mix;
        cLevel = dbToGain (params.levelDb);
    }

    void merge (const float* lowL, const float* lowR, const float* midL, const float* midR, const float* highL, const float* highR, float* outL, float* outR, int n)
    {
        if (n <= 0)
            return;
        const float targetLowGain = params.lowBypass ? 0.0f : dbToGain (params.lowGainDb);
        const float targetMidGain = params.midBypass ? 0.0f : dbToGain (params.midGainDb);
        const float targetHighGain = params.highBypass ? 0.0f : dbToGain (params.highGainDb);
        const float targetMix = std::clamp (params.mix, 0.0f, 1.0f);
        const float targetLevel = dbToGain (params.levelDb);
        const float invN = 1.0f / static_cast<float> (n);
        const float stepLowGain = (targetLowGain - cLow) * invN;
        const float stepMidGain = (targetMidGain - cMid) * invN;
        const float stepHighGain = (targetHighGain - cHigh) * invN;
        const float stepMix = (targetMix - cMix) * invN;
        const float stepLevel = (targetLevel - cLevel) * invN;
        for (int i = 0; i < n; ++i)
        {
            cLow += stepLowGain;
            cMid += stepMidGain;
            cHigh += stepHighGain;
            cMix += stepMix;
            cLevel += stepLevel;
            const float wetL = lowL[i] * cLow + midL[i] * cMid + highL[i] * cHigh;
            const float wetR = lowR[i] * cLow + midR[i] * cMid + highR[i] * cHigh;
            const float dryL = lowL[i] + midL[i] + highL[i];
            const float dryR = lowR[i] + midR[i] + highR[i];
            const float outSampleL = dryL + (wetL - dryL) * cMix;
            const float outSampleR = dryR + (wetR - dryR) * cMix;
            outL[i] = flushDenormal (outSampleL * cLevel);
            outR[i] = flushDenormal (outSampleR * cLevel);
        }
        cLow = targetLowGain;
        cMid = targetMidGain;
        cHigh = targetHighGain;
        cMix = targetMix;
        cLevel = targetLevel;
    }

    void process (SplitterLMH& splitter, int maxBlock, float* l, float* r, int n)
    {
        if (n <= 0 || ! params.enabled)
            return;
        const float targetMix = std::clamp (params.mix, 0.0f, 1.0f);
        const float targetLevel = dbToGain (params.levelDb);
        if (targetMix == 0.0f && targetLevel == 1.0f && cMix == 0.0f && cLevel == 1.0f)
            return;
        std::vector<float> a (static_cast<size_t> (maxBlock)), b (a), c (a), d (a), e (a), f (a);
        int processed = 0;
        while (processed < n)
        {
            const int block = std::min (n - processed, maxBlock);
            splitter.split (l + processed, r + processed, a.data(), b.data(), c.data(), d.data(), e.data(), f.data(), block);
            merge (a.data(), b.data(), c.data(), d.data(), e.data(), f.data(), l + processed, r + processed, block);
            processed += block;
        }
    }
};

struct OldMS
{
    SplitterMS::Params params;
    float cMid = 1.0f, cSide = 1.0f, cMix = 1.0f, cLevel = 1.0f;

    void reset()
    {
        cMid = params.midBypass ? 0.0f : dbToGain (params.midGainDb);
        cSide = params.sideBypass ? 0.0f : dbToGain (params.sideGainDb);
        cMix = params.mix;
        cLevel = dbToGain (params.levelDb);
    }

    void merge (const float* midL, const float* midR, const float* sideL, const float* sideR, float* outL, float* outR, int n)
    {
        if (n <= 0)
            return;
        const float targetMidGain = params.midBypass ? 0.0f : dbToGain (params.midGainDb);
        const float targetSideGain = params.sideBypass ? 0.0f : dbToGain (params.sideGainDb);
        const float targetMix = std::clamp (params.mix, 0.0f, 1.0f);
        const float targetLevel = dbToGain (params.levelDb);
        const float invN = 1.0f / static_cast<float> (n);
        const float stepMidGain = (targetMidGain - cMid) * invN;
        const float stepSideGain = (targetSideGain - cSide) * invN;
        const float stepMix = (targetMix - cMix) * invN;
        const float stepLevel = (targetLevel - cLevel) * invN;
        for (int i = 0; i < n; ++i)
        {
            cMid += stepMidGain;
            cSide += stepSideGain;
            cMix += stepMix;
            cLevel += stepLevel;
            const float wetL = midL[i] * cMid + sideL[i] * cSide;
            const float wetR = midR[i] * cMid + sideR[i] * cSide;
            const float dryL = midL[i] + sideL[i];
            const float dryR = midR[i] + sideR[i];
            const float outSampleL = dryL + (wetL - dryL) * cMix;
            const float outSampleR = dryR + (wetR - dryR) * cMix;
            outL[i] = flushDenormal (outSampleL * cLevel);
            outR[i] = flushDenormal (outSampleR * cLevel);
        }
        cMid = targetMidGain;
        cSide = targetSideGain;
        cMix = targetMix;
        cLevel = targetLevel;
    }

    void process (SplitterMS& splitter, int maxBlock, float* l, float* r, int n)
    {
        if (n <= 0 || ! params.enabled)
            return;
        const float targetMix = std::clamp (params.mix, 0.0f, 1.0f);
        const float targetLevel = dbToGain (params.levelDb);
        if (targetMix == 0.0f && targetLevel == 1.0f && cMix == 0.0f && cLevel == 1.0f)
            return;
        std::vector<float> a (static_cast<size_t> (maxBlock)), b (a), c (a), d (a);
        int processed = 0;
        while (processed < n)
        {
            const int block = std::min (n - processed, maxBlock);
            splitter.split (l + processed, r + processed, a.data(), b.data(), c.data(), d.data(), block);
            merge (a.data(), b.data(), c.data(), d.data(), l + processed, r + processed, block);
            processed += block;
        }
    }
};

/** A dB that is mostly one of a few steady values (the real use) and now and then a special or random one. */
struct DbPicker
{
    std::mt19937 rng { 12345u };
    std::vector<float> pool = dbValues (rng);
    float steady[3] = { 0.0f, -6.0f, 3.5f };

    float pick()
    {
        const unsigned r = static_cast<unsigned> (rng() % 10u);
        if (r < 4)
            return steady[rng() % 3u];
        if (r < 6)
            return static_cast<float> (static_cast<int> (rng() % 25u) - 12);
        return pool[rng() % pool.size()];
    }
    float mix()
    {
        const unsigned r = static_cast<unsigned> (rng() % 8u);
        if (r == 0) return 0.0f;
        if (r == 1) return 1.0f;
        if (r == 2) return -0.5f;
        if (r == 3) return 2.0f;
        return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng);
    }
    bool flag() { return rng() % 4u == 0u; }
};

void fillNoise (std::mt19937& rng, std::vector<float>& v)
{
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    for (auto& x : v)
        x = d (rng);
}

bool sameBuffers (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (! sameFloat (a[i], b[i]))
            return false;
    return true;
}

/** `bands` noise buffers of n samples. */
std::vector<std::vector<float>> noiseBands (std::mt19937& rng, size_t bands, int n)
{
    std::vector<std::vector<float>> v (bands, std::vector<float> (static_cast<size_t> (n)));
    for (auto& b : v)
        fillNoise (rng, b);
    return v;
}
} // namespace

TEST (SplitterGainCache_MatchesDbToGainBitForBit)
{
    std::mt19937 rng (7u);
    const std::vector<float> values = dbValues (rng);
    DbGainCache cache;
    long bad = 0;
    for (int pass = 0; pass < 3; ++pass)   // later passes hit the memo for repeated values
        for (size_t i = 0; i < values.size(); ++i)
        {
            const float db = values[i];
            const float want = dbToGain (db);
            const float got = cache.get (db);
            if (! sameFloat (want, got) || (! std::isnan (want) && bitsOf (want) != bitsOf (got)))
                ++bad;
            const float again = cache.get (db);   // the same value twice in a row: the second call is the memo hit
            if (! sameFloat (again, got) || (! std::isnan (again) && bitsOf (again) != bitsOf (got)))
                ++bad;
        }
    CHECK_EQ (bad, 0L);

    // exactly 0 dB, either sign, is the exact unity gain, also right after a different value filled the slot
    DbGainCache c2;
    c2.get (-6.0f);
    CHECK_EQ (bitsOf (c2.get (0.0f)), bitsOf (1.0f));
    CHECK_EQ (bitsOf (c2.get (-0.0f)), bitsOf (1.0f));
    CHECK_EQ (bitsOf (dbToGain (0.0f)), bitsOf (1.0f));
    CHECK_EQ (bitsOf (dbToGain (-0.0f)), bitsOf (1.0f));
    CHECK_EQ (bitsOf (c2.get (-6.0f)), bitsOf (dbToGain (-6.0f)));

    // a denormal and its negative are different bit patterns and must not be confused
    DbGainCache c3;
    CHECK_EQ (bitsOf (c3.get (1.0e-45f)), bitsOf (dbToGain (1.0e-45f)));
    CHECK_EQ (bitsOf (c3.get (-1.0e-45f)), bitsOf (dbToGain (-1.0e-45f)));
    CHECK_EQ (bitsOf (c3.get (1.0e-45f)), bitsOf (dbToGain (1.0e-45f)));
}

TEST (SplitterGainCache_LH_MatchesFrozenOld)
{
    const int maxBlock = 64;
    DbPicker pick;
    SplitterLH fresh, ref;
    OldLH old;
    fresh.prepare (44100.0, maxBlock);
    ref.prepare (44100.0, maxBlock);
    std::mt19937 rng (99u);
    long processBad = 0, mergeBad = 0;
    for (int iter = 0; iter < 1500; ++iter)
    {
        SplitterLH::Params p;
        p.enabled = (iter % 50) != 7;
        p.splitFreq = 100.0f + static_cast<float> (iter % 7) * 300.0f;
        p.lowBypass = pick.flag();
        p.highBypass = pick.flag();
        p.lowGainDb = pick.pick();
        p.highGainDb = pick.pick();
        p.mix = pick.mix();
        p.levelDb = pick.pick();
        fresh.setParams (p);
        ref.setParams (p);
        old.params = p;
        if (iter % 97 == 0)   // reset () reads the parameters too
        {
            fresh.reset();
            ref.reset();
            old.reset();
        }
        const int n = (iter % 5 == 0) ? 150 : 1 + static_cast<int> (rng() % 64u);   // 150 > maxBlock: several chunks
        std::vector<float> l (static_cast<size_t> (n)), r (l);
        fillNoise (rng, l);
        fillNoise (rng, r);
        std::vector<float> l2 = l, r2 = r;
        fresh.process (l.data(), r.data(), n);
        old.process (ref, maxBlock, l2.data(), r2.data(), n);
        if (! sameBuffers (l, l2) || ! sameBuffers (r, r2))
            ++processBad;
    }
    CHECK_EQ (processBad, 0L);

    // merge () alone, on arbitrary band signals
    SplitterLH a;
    OldLH o;
    a.prepare (44100.0, maxBlock);
    for (int iter = 0; iter < 1500; ++iter)
    {
        SplitterLH::Params p;
        p.lowBypass = pick.flag();
        p.highBypass = pick.flag();
        p.lowGainDb = pick.pick();
        p.highGainDb = pick.pick();
        p.mix = pick.mix();
        p.levelDb = pick.pick();
        a.setParams (p);
        o.params = p;
        if (iter == 0)
        {
            a.reset();
            o.reset();
        }
        const int n = 1 + static_cast<int> (rng() % 64u);
        const auto b = noiseBands (rng, 4, n);
        std::vector<float> o1 (static_cast<size_t> (n)), o2 (o1), p1 (o1), p2 (o1);
        a.merge (b[0].data(), b[1].data(), b[2].data(), b[3].data(), o1.data(), o2.data(), n);
        o.merge (b[0].data(), b[1].data(), b[2].data(), b[3].data(), p1.data(), p2.data(), n);
        if (! sameBuffers (o1, p1) || ! sameBuffers (o2, p2))
            ++mergeBad;
    }
    CHECK_EQ (mergeBad, 0L);
}

TEST (SplitterGainCache_LMH_MatchesFrozenOld)
{
    const int maxBlock = 64;
    DbPicker pick;
    pick.rng.seed (4242u);
    SplitterLMH fresh, ref;
    OldLMH old;
    fresh.prepare (44100.0, maxBlock);
    ref.prepare (44100.0, maxBlock);
    std::mt19937 rng (98u);
    long processBad = 0, mergeBad = 0;
    for (int iter = 0; iter < 1200; ++iter)
    {
        SplitterLMH::Params p;
        p.enabled = (iter % 50) != 7;
        p.splitFreq1 = 150.0f + static_cast<float> (iter % 5) * 100.0f;
        p.splitFreq2 = 2000.0f + static_cast<float> (iter % 3) * 1000.0f;
        p.lowBypass = pick.flag();
        p.midBypass = pick.flag();
        p.highBypass = pick.flag();
        p.lowGainDb = pick.pick();
        p.midGainDb = pick.pick();
        p.highGainDb = pick.pick();
        p.mix = pick.mix();
        p.levelDb = pick.pick();
        fresh.setParams (p);
        ref.setParams (p);
        old.params = p;
        if (iter % 89 == 0)
        {
            fresh.reset();
            ref.reset();
            old.reset();
        }
        const int n = (iter % 5 == 0) ? 150 : 1 + static_cast<int> (rng() % 64u);
        std::vector<float> l (static_cast<size_t> (n)), r (l);
        fillNoise (rng, l);
        fillNoise (rng, r);
        std::vector<float> l2 = l, r2 = r;
        fresh.process (l.data(), r.data(), n);
        old.process (ref, maxBlock, l2.data(), r2.data(), n);
        if (! sameBuffers (l, l2) || ! sameBuffers (r, r2))
            ++processBad;
    }
    CHECK_EQ (processBad, 0L);

    SplitterLMH a;
    OldLMH o;
    a.prepare (44100.0, maxBlock);
    for (int iter = 0; iter < 1200; ++iter)
    {
        SplitterLMH::Params p;
        p.lowBypass = pick.flag();
        p.midBypass = pick.flag();
        p.highBypass = pick.flag();
        p.lowGainDb = pick.pick();
        p.midGainDb = pick.pick();
        p.highGainDb = pick.pick();
        p.mix = pick.mix();
        p.levelDb = pick.pick();
        a.setParams (p);
        o.params = p;
        if (iter == 0)
        {
            a.reset();
            o.reset();
        }
        const int n = 1 + static_cast<int> (rng() % 64u);
        const auto b = noiseBands (rng, 6, n);
        std::vector<float> o1 (static_cast<size_t> (n)), o2 (o1), p1 (o1), p2 (o1);
        a.merge (b[0].data(), b[1].data(), b[2].data(), b[3].data(), b[4].data(), b[5].data(), o1.data(), o2.data(), n);
        o.merge (b[0].data(), b[1].data(), b[2].data(), b[3].data(), b[4].data(), b[5].data(), p1.data(), p2.data(), n);
        if (! sameBuffers (o1, p1) || ! sameBuffers (o2, p2))
            ++mergeBad;
    }
    CHECK_EQ (mergeBad, 0L);
}

TEST (SplitterGainCache_MS_MatchesFrozenOld)
{
    const int maxBlock = 64;
    DbPicker pick;
    pick.rng.seed (777u);
    SplitterMS fresh, ref;
    OldMS old;
    fresh.prepare (44100.0, maxBlock);
    ref.prepare (44100.0, maxBlock);
    std::mt19937 rng (97u);
    long processBad = 0, mergeBad = 0;
    for (int iter = 0; iter < 1500; ++iter)
    {
        SplitterMS::Params p;
        p.enabled = (iter % 50) != 7;
        p.midBypass = pick.flag();
        p.sideBypass = pick.flag();
        p.midGainDb = pick.pick();
        p.sideGainDb = pick.pick();
        p.mix = pick.mix();
        p.levelDb = pick.pick();
        fresh.setParams (p);
        ref.setParams (p);
        old.params = p;
        if (iter % 97 == 0)
        {
            fresh.reset();
            ref.reset();
            old.reset();
        }
        const int n = (iter % 5 == 0) ? 150 : 1 + static_cast<int> (rng() % 64u);
        std::vector<float> l (static_cast<size_t> (n)), r (l);
        fillNoise (rng, l);
        fillNoise (rng, r);
        std::vector<float> l2 = l, r2 = r;
        fresh.process (l.data(), r.data(), n);
        old.process (ref, maxBlock, l2.data(), r2.data(), n);
        if (! sameBuffers (l, l2) || ! sameBuffers (r, r2))
            ++processBad;
    }
    CHECK_EQ (processBad, 0L);

    SplitterMS a;
    OldMS o;
    a.prepare (44100.0, maxBlock);
    for (int iter = 0; iter < 1500; ++iter)
    {
        SplitterMS::Params p;
        p.midBypass = pick.flag();
        p.sideBypass = pick.flag();
        p.midGainDb = pick.pick();
        p.sideGainDb = pick.pick();
        p.mix = pick.mix();
        p.levelDb = pick.pick();
        a.setParams (p);
        o.params = p;
        if (iter == 0)
        {
            a.reset();
            o.reset();
        }
        const int n = 1 + static_cast<int> (rng() % 64u);
        const auto b = noiseBands (rng, 4, n);
        std::vector<float> o1 (static_cast<size_t> (n)), o2 (o1), p1 (o1), p2 (o1);
        a.merge (b[0].data(), b[1].data(), b[2].data(), b[3].data(), o1.data(), o2.data(), n);
        o.merge (b[0].data(), b[1].data(), b[2].data(), b[3].data(), p1.data(), p2.data(), n);
        if (! sameBuffers (o1, p1) || ! sameBuffers (o2, p2))
            ++mergeBad;
    }
    CHECK_EQ (mergeBad, 0L);
}
