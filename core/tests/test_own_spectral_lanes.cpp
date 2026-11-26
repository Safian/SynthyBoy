// SynthyBoy - own engine tests: the NEON forms of the spectral kernels (core/spectral/src/kernels.cpp) give the bits of their scalar forms for special values as well as for random data:
// zeros of both signs, subnormals, the neighbours of the constants of the compares (+-100, pi, 2 pi, the smallest normal), infinities, NaNs of both signs, and every special value in every
// lane position of the vector loops and of their tails. Compiled with kernels.cpp itself (no library) for 32 bit and 64 bit ARM (core/tests/CMakeLists.txt), run natively or under qemu-user.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Float modes: AArch64 runs the cases in the default (IEEE) mode and with FPCR.FZ set (the mode of the MPC shell); 32 bit ARM, where NEON always flushes subnormals, runs with FPSCR.FZ set (the
// mode of the shell) and, in the default mode, only data without subnormals. A NaN is compared as a NaN (32 bit ARM NEON produces the default NaN where the VFP code propagates a payload).
// Documented domains (the same as in kernels.h): the phase wrap of the vector form is only the scalar one for |x| < 1e5 (and for a NaN); gainClamp is only the scalar form for operands and products
// that are normal numbers (or zero) in the default mode of 32 bit ARM.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "synthyboy/spectral/kernels.h"
#include "fp_flush_mode.h"
#include "test_runner.h"

using namespace synthyboy::spectral;
using namespace synthyboy::spectral::kernels;

namespace
{
float fromBits (uint32_t u)
{
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}
uint32_t bitsOf (float f)
{
    uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 7u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return 2.0f * uni() - 1.0f; }
};

bool sameValue (float a, float b)
{
    if (a != a && b != b)
        return true; // both NaN
    return bitsOf (a) == bitsOf (b);
}
bool sameValues (const float* a, const float* b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (! sameValue (a[i], b[i]))
            return false;
    return true;
}

struct Mode
{
    bool fz;
    bool subnormals; // may the data hold subnormals in this mode
    const char* name;
};
/** The float modes of a case. `arithmetic`: the outputs are results of arithmetic that may be subnormal (a product of two small normal numbers): on 32 bit ARM the NEON forms flush those where the
    VFP code does not in the default mode, so only the mode of the shell (FZ) applies there. The compare kernels (clamp, peak search, placing) also run in the default mode without subnormal data. */
std::vector<Mode> modes (bool arithmetic)
{
#if defined(__aarch64__)
    (void) arithmetic;
    return { { false, true, "IEEE" }, { true, true, "FZ" } };
#else
    if (arithmetic)
        return { { true, true, "FZ" } };
    return { { false, false, "IEEE, no subnormals" }, { true, true, "FZ" } };
#endif
}

std::vector<float> specialValues()
{
    std::vector<float> v;
    const float inf = std::numeric_limits<float>::infinity();
    for (uint32_t u : { 0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x807fffffu, 0x00400000u, 0x00800000u, 0x80800000u, 0x00800001u, 0x7f7fffffu, 0xff7fffffu,
                        0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u, 0x7f800001u, 0xff800001u, 0x7fffffffu })
        v.push_back (fromBits (u));
    for (float c : { 100.0f, 3.14159265f, 6.28318530717958647692f, 1.0f, 0.5f, 1.0e-5f, 1.0e5f, 1.0e9f, 16.0f, 1.0e-18f, 0.78539816f, 1000.0f, 65536.0f })
        for (float sgn : { 1.0f, -1.0f })
        {
            const float x = sgn * c;
            v.push_back (x);
            v.push_back (std::nextafter (x, 0.0f));
            v.push_back (std::nextafter (x, sgn * inf));
            v.push_back (std::nextafter (std::nextafter (x, sgn * inf), sgn * inf));
        }
    for (int k = 1; k < 40; ++k)
    {
        v.push_back (6.28318530717958647692f * static_cast<float> (k));   // multiples of the float 2 pi and of the float pi (a phase can land on one)
        v.push_back (3.14159265f * static_cast<float> (k));
        v.push_back (-3.14159265f * static_cast<float> (k));
    }
    return v;
}

/** A random value: mostly ordinary, sometimes a special value, sometimes any bit pattern. `ok`: may the value be a subnormal. */
float pick (Rng& r, const std::vector<float>& sp, bool subnormals, float range)
{
    float x;
    const uint32_t k = r.next() % 8u;
    if (k == 0)
        x = sp[r.next() % sp.size()];
    else if (k == 1)
        x = fromBits (r.next());
    else
        x = r.bi() * range;
    if (! subnormals && x != 0.0f && std::fabs (x) < 1.17549435e-38f)
        x = 0.0f;
    return x;
}

struct Tally
{
    long long checks = 0, bad = 0;
    void add (bool ok, const char* what, int mode)
    {
        ++checks;
        if (! ok && bad++ < 4)
            std::printf ("    differs: %s (mode %d)\n", what, mode);
    }
};
} // namespace

TEST (spectral_sine_cosine_and_phase_wrap_vector_forms_equal_the_scalar_forms_on_special_values)
{
    if (! haveNeon())
    {
        std::printf ("    no NEON in this build\n");
        CHECK (true);
        return;
    }
    const std::vector<float> sp = specialValues();
    Tally t;
    int modeNo = 0;
    for (const Mode& m : modes (true))
    {
        sbtest::FlushMode fm (m.fz);
        Rng r (11 + static_cast<uint32_t> (modeNo));
        // sincosBlock: any input (a NaN and an infinity convert the same way in both forms), every length around the 8 and 4 lane blocks, specials in every position
        for (int round = 0; round < 3000; ++round)
        {
            const int n = 1 + round % 41;
            std::vector<float> a (static_cast<size_t> (n)), s1 (a.size()), c1 (a.size()), s2 (a.size()), c2 (a.size());
            for (float& x : a)
                x = pick (r, sp, m.subnormals, round % 3 == 0 ? 8.0f : 1000.0f);
            sincosBlock (a.data(), s1.data(), c1.data(), n, false);
            sincosBlock (a.data(), s2.data(), c2.data(), n, true);
            t.add (sameValues (s1.data(), s2.data(), a.size()) && sameValues (c1.data(), c2.data(), a.size()), "sincosBlock", modeNo);
            // the spectrum of the angles: the same sine and cosine, then the products
            std::vector<float> mag (a.size()), re1 (a.size()), im1 (a.size()), re2 (a.size()), im2 (a.size());
            for (float& x : mag)
                x = pick (r, sp, m.subnormals, 4.0f);
            spectrumFromAngles (a.data(), mag.data(), re1.data(), im1.data(), n, false);
            spectrumFromAngles (a.data(), mag.data(), re2.data(), im2.data(), n, true);
            t.add (sameValues (re1.data(), re2.data(), a.size()) && sameValues (im1.data(), im2.data(), a.size()), "spectrumFromAngles", modeNo);
        }
        // advancePhases / advanceToSpectrum: the sum phase + step * kHop must stay below 1e5 in magnitude (or be a NaN): the documented domain of the vector wrap
        for (int round = 0; round < 3000; ++round)
        {
            const int n = 1 + round % 41;
            std::vector<float> ph1 (static_cast<size_t> (n)), step (ph1.size()), mag (ph1.size());
            for (size_t i = 0; i < ph1.size(); ++i)
            {
                float p = pick (r, sp, m.subnormals, round % 2 == 0 ? 120.0f : 3.2f);
                float st = pick (r, sp, m.subnormals, 0.3f);
                const float sum = p + st * static_cast<float> (kHop);
                if (! (std::fabs (sum) < 9.0e4f) && sum == sum)
                {
                    p = r.bi() * 50.0f;
                    st = r.bi() * 0.1f;
                }
                ph1[i] = p;
                step[i] = st;
                mag[i] = pick (r, sp, m.subnormals, 2.0f);
            }
            std::vector<float> ph2 = ph1, ph3 = ph1, ph4 = ph1;
            advancePhases (ph1.data(), step.data(), n, false);
            advancePhases (ph2.data(), step.data(), n, true);
            t.add (sameValues (ph1.data(), ph2.data(), ph1.size()), "advancePhases", modeNo);
            std::vector<float> re1 (ph1.size()), im1 (ph1.size()), re2 (ph1.size()), im2 (ph1.size());
            advanceToSpectrum (ph3.data(), step.data(), mag.data(), re1.data(), im1.data(), n, false);
            advanceToSpectrum (ph4.data(), step.data(), mag.data(), re2.data(), im2.data(), n, true);
            t.add (sameValues (ph3.data(), ph4.data(), ph1.size()) && sameValues (re1.data(), re2.data(), ph1.size()) && sameValues (im1.data(), im2.data(), ph1.size()), "advanceToSpectrum", modeNo);
        }
        // the wrap compares one value at a time: every special value (and its neighbours) alone in a lane, in the four lane positions, for the advance
        for (float x : sp)
            for (int pos = 0; pos < 8; ++pos)
            {
                if (! (std::fabs (x) < 9.0e4f) && x == x)
                    continue;
                if (! m.subnormals && x != 0.0f && std::fabs (x) < 1.17549435e-38f)
                    continue;
                std::vector<float> ph1 (8, 0.25f), step (8, 0.0f);
                ph1[static_cast<size_t> (pos)] = x;
                std::vector<float> ph2 = ph1;
                advancePhases (ph1.data(), step.data(), 8, false);
                advancePhases (ph2.data(), step.data(), 8, true);
                t.add (sameValues (ph1.data(), ph2.data(), 8), "advancePhases lone special", modeNo);
            }
        ++modeNo;
    }
    std::printf ("    %lld comparisons in %d float mode(s): %lld differ\n", t.checks, modeNo, t.bad);
    CHECK_EQ (t.bad, 0);
}

TEST (spectral_output_clamp_peak_search_and_placing_vector_forms_equal_the_scalar_forms_on_special_values)
{
    if (! haveNeon())
    {
        std::printf ("    no NEON in this build\n");
        CHECK (true);
        return;
    }
    const std::vector<float> sp = specialValues();
    Tally t;
    int modeNo = 0;
    for (const Mode& m : modes (false))
    {
        sbtest::FlushMode fm (m.fz);
        Rng r (31 + static_cast<uint32_t> (modeNo));
        // gainClamp: any input and gain (a non finite product is masked to 0; the products of normal numbers of the default mode of 32 bit ARM are the documented domain, kept by the generator there)
        for (int round = 0; round < 4000; ++round)
        {
            const int n = 1 + round % 37;
            std::vector<float> in (static_cast<size_t> (n)), o1 (in.size()), o2 (in.size());
            for (float& x : in)
                x = pick (r, sp, m.subnormals, round % 2 == 0 ? 30.0f : 1.0f);
            float gain = round % 5 == 0 ? sp[r.next() % sp.size()] : (round % 5 == 1 ? 1.0f : r.bi() * 4.0f);
            if (! m.subnormals && gain != 0.0f && std::fabs (gain) < 1.17549435e-38f)
                gain = 1.0f;
            if (! m.subnormals) // normal operands and products only
                for (float& x : in)
                {
                    const float p = x * gain;
                    if (p != 0.0f && std::fabs (p) < 1.17549435e-38f)
                        x = 0.0f;
                }
            gainClamp (o1.data(), in.data(), gain, n, false);
            gainClamp (o2.data(), in.data(), gain, n, true);
            t.add (sameValues (o1.data(), o2.data(), in.size()), "gainClamp", modeNo);
        }
        // maxBins: a NaN does not change the maximum (as std::max), the other special values are ordered as the float compare orders them
        for (int round = 0; round < 3000; ++round)
        {
            const int n = round % 41;
            std::vector<float> a (static_cast<size_t> (std::max (n, 1)));
            for (float& x : a)
                x = pick (r, sp, m.subnormals, 3.0f);
            t.add (sameValue (maxBins (a.data(), n, false), maxBins (a.data(), n, true)), "maxBins", modeNo);
        }
        // findPeaks: arrays of magnitudes with plateaus, ties and tiny values, and now and then a special value (negative, -0, NaN, subnormal, infinity) at a random place: the vector form sends them to the scalar loop
        for (int round = 0; round < 1500; ++round)
        {
            const int n = round % 11 == 0 ? kBins : (round % 11 == 1 ? 1056 : 2 + static_cast<int> (r.next() % 120u));
            std::vector<float> a (static_cast<size_t> (n));
            for (int i = 0; i < n; ++i)
            {
                const uint32_t kind = r.next() % 4u;
                a[static_cast<size_t> (i)] = kind == 0 ? std::floor (r.uni() * 6.0f) * 0.25f : (kind == 1 ? r.uni() : (kind == 2 ? r.uni() * 1.0e-6f : (i % 3 == 0 ? 0.0f : r.uni() * 100.0f)));
            }
            if (round % 3 != 0)
                for (int i = 0, count = 1 + static_cast<int> (r.next() % 3u); i < count; ++i)
                {
                    float x = sp[r.next() % sp.size()];
                    if (! m.subnormals && x != 0.0f && std::fabs (x) < 1.17549435e-38f)
                        x = 0.0f;
                    a[r.next() % a.size()] = x;
                }
            const float top = round % 4 == 0 ? maxBins (a.data(), n, false) : (round % 4 == 1 ? 1.0f : (round % 4 == 2 ? 0.0f : pick (r, sp, m.subnormals, 2.0f)));
            for (int cap : { 512, 5 })
            {
                std::vector<int> p1 (512), p2 (512);
                const int n1 = findPeaks (a.data(), n, top, p1.data(), cap, false), n2 = findPeaks (a.data(), n, top, p2.data(), cap, true);
                bool same = n1 == n2;
                for (int i = 0; same && i < n1; ++i)
                    same = p1[static_cast<size_t> (i)] == p2[static_cast<size_t> (i)];
                t.add (same, "findPeaks", modeNo);
            }
        }
        // placeRegion / placeAll: magnitudes and output magnitudes with special values in them
        for (int round = 0; round < 1200; ++round)
        {
            std::vector<float> mag (kBins), freq (kBins), theta (kBins), om1 (kBins), of1 (kBins), oth1 (kBins);
            for (int k = 0; k < kBins; ++k)
            {
                const size_t i = static_cast<size_t> (k);
                mag[i] = r.next() % 7u == 0 ? 0.0f : r.uni();
                freq[i] = r.bi() * 3.0f;
                theta[i] = r.bi() * 3.14f;
                om1[i] = r.next() % 5u == 0 ? 0.0f : r.uni() * 0.8f;
                of1[i] = r.bi();
                oth1[i] = r.bi();
            }
            for (int i = 0, count = round % 3 == 0 ? 0 : 1 + static_cast<int> (r.next() % 6u); i < count; ++i)
            {
                float x = sp[r.next() % sp.size()];
                if (! m.subnormals && x != 0.0f && std::fabs (x) < 1.17549435e-38f)
                    x = 0.0f;
                (r.next() % 2u == 0 ? mag : om1)[r.next() % static_cast<uint32_t> (kBins)] = x;
            }
            std::vector<float> om2 = om1, of2 = of1, oth2 = oth1;
            const int from = 1 + static_cast<int> (r.next() % 900u), len = static_cast<int> (r.next() % 90u), offset = static_cast<int> (r.bi() * 40.0f);
            placeRegion (mag.data(), freq.data(), theta.data(), om1.data(), of1.data(), oth1.data(), from, std::min (from + len, kBins - 1), offset, 1.37f, 0.02f, kBins, false);
            placeRegion (mag.data(), freq.data(), theta.data(), om2.data(), of2.data(), oth2.data(), from, std::min (from + len, kBins - 1), offset, 1.37f, 0.02f, kBins, true);
            t.add (sameValues (om1.data(), om2.data(), om1.size()) && sameValues (of1.data(), of2.data(), of1.size()) && sameValues (oth1.data(), oth2.data(), oth1.size()), "placeRegion", modeNo);
        }
        ++modeNo;
    }
    std::printf ("    %lld comparisons in %d float mode(s): %lld differ\n", t.checks, modeNo, t.bad);
    CHECK_EQ (t.bad, 0);
}

TEST (spectral_arithmetic_kernels_vector_forms_equal_the_scalar_forms_on_special_values)
{
    if (! haveNeon())
    {
        std::printf ("    no NEON in this build\n");
        CHECK (true);
        return;
    }
    const std::vector<float> sp = specialValues();
    Tally t;
    int modeNo = 0;
    for (const Mode& m : modes (true))
    {
        sbtest::FlushMode fm (m.fz);
        Rng r (51 + static_cast<uint32_t> (modeNo));
        for (int round = 0; round < 1500; ++round)
        {
            const int n = 1 + round % 45;
            std::vector<float> a (static_cast<size_t> (n)), b (a.size()), o1 (a.size()), o2 (a.size());
            for (size_t i = 0; i < a.size(); ++i)
            {
                a[i] = pick (r, sp, m.subnormals, 2.0f);
                b[i] = pick (r, sp, m.subnormals, 2.0f);
            }
            const float tt = pick (r, sp, m.subnormals, 1.0f);
            lerpBins (o1.data(), a.data(), b.data(), tt, n, false);
            lerpBins (o2.data(), a.data(), b.data(), tt, n, true);
            t.add (sameValues (o1.data(), o2.data(), a.size()), "lerpBins", modeNo);
            o1 = a;
            o2 = a;
            mulBins (o1.data(), b.data(), n, false);
            mulBins (o2.data(), b.data(), n, true);
            t.add (sameValues (o1.data(), o2.data(), a.size()), "mulBins", modeNo);
            std::vector<float> p1 = a, p2 = a;
            const float p0 = pick (r, sp, m.subnormals, 3.0f), q0 = pick (r, sp, m.subnormals, 3.0f);
            const int from = static_cast<int> (r.next() % 3u), to = n - 1 - static_cast<int> (r.next() % 3u);
            lockRun (p1.data(), b.data(), from, to, p0, q0, false);
            lockRun (p2.data(), b.data(), from, to, p0, q0, true);
            t.add (sameValues (p1.data(), p2.data(), a.size()), "lockRun", modeNo);
        }
        // the overlap add (`at` and the starting sample `first` multiples of 8) and the real inverse transform of a spectrum with special values
        std::vector<float> zr (kFftSize / 2), zi (kFftSize / 2);
        for (int round = 0; round < 40; ++round)
        {
            for (float& v : zr) v = round % 4 == 0 ? pick (r, sp, m.subnormals, 2.0f) : r.bi();
            for (float& v : zi) v = round % 4 == 0 ? pick (r, sp, m.subnormals, 2.0f) : r.bi();
            for (int at : { 0, 8, 504, 1536 })
            {
                std::vector<float> a (kFftSize), b (kFftSize);
                for (size_t i = 0; i < a.size(); ++i)
                    a[i] = b[i] = r.bi();
                const float scale = round % 3 == 0 ? sp[r.next() % sp.size()] : 0.7f;
                const int first = 8 * static_cast<int> (r.next() % 3u);
                overlapAdd (a.data(), at, zr.data(), zi.data(), m.subnormals || ! (scale != 0.0f && std::fabs (scale) < 1.17549435e-38f) ? scale : 1.0f, false, first);
                overlapAdd (b.data(), at, zr.data(), zi.data(), m.subnormals || ! (scale != 0.0f && std::fabs (scale) < 1.17549435e-38f) ? scale : 1.0f, true, first);
                t.add (sameValues (a.data(), b.data(), a.size()), "overlapAdd", modeNo);
            }
            std::vector<float> xr (kBins), xi (kBins), z1r (kFftSize / 2), z1i (kFftSize / 2), z2r (kFftSize / 2), z2i (kFftSize / 2);
            for (int k = 0; k < kBins; ++k)
            {
                xr[static_cast<size_t> (k)] = round % 5 == 0 ? pick (r, sp, m.subnormals, 2.0f) : r.bi();
                xi[static_cast<size_t> (k)] = k == 0 || k == kBins - 1 ? 0.0f : (round % 5 == 0 ? pick (r, sp, m.subnormals, 2.0f) : r.bi());
            }
            RealIfft::instance().inverse (xr.data(), xi.data(), z1r.data(), z1i.data(), false);
            RealIfft::instance().inverse (xr.data(), xi.data(), z2r.data(), z2i.data(), true);
            t.add (sameValues (z1r.data(), z2r.data(), z1r.size()) && sameValues (z1i.data(), z2i.data(), z1i.size()), "RealIfft::inverse", modeNo);
        }
        ++modeNo;
    }
    std::printf ("    %lld comparisons in %d float mode(s): %lld differ\n", t.checks, modeNo, t.bad);
    CHECK_EQ (t.bad, 0);
}
