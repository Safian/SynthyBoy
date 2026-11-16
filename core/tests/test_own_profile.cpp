// SynthyBoy - own engine tests: the economy groups of a quality (Quality::economy, EconomyFlags): the three tiers are unchanged by them, each group is its own switch,
// and a state sounds the same shape at every profile (the saved state holds nothing of the profile).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "own_heavy_patch.h"
#include "synthyboy/engine.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
using Patch = void (*) (IEngine&);

void heavy (IEngine& e) { bench::ownHeavyPatch (e); } // 3 oscillators x 7 unison voices, a morph (osc 1), a warp (osc 2), 2 filters, LFOs, the nine effects

/** One saw through the ladder filter with resonance and an envelope on the cutoff: no unison, warp, morph or effect, so only the Math and Control groups can act. */
void plain (IEngine& e)
{
    bench::ownSet (e, "master.polyphony", 8.0f);
    bench::ownSet (e, "filter1.model", 2.0f);
    bench::ownSet (e, "filter1.cutoff", 1200.0f);
    bench::ownSet (e, "filter1.resonance", 30.0f);
}

/** The plain patch with a sync warp on its oscillator: the Warp group acts. */
void warped (IEngine& e)
{
    plain (e);
    bench::ownSet (e, "osc1.warp.type", 1.0f);
    bench::ownSet (e, "osc1.warp.amount", 35.0f);
}

/** The plain patch with a spectral morph: the Morph group acts. */
void morphed (IEngine& e)
{
    plain (e);
    bench::ownSet (e, "osc1.wavetable", 5.0f);
    bench::ownSet (e, "osc1.morph.type", 1.0f);
    bench::ownSet (e, "osc1.morph.amount", 50.0f);
}

/** The plain patch with five unison voices: a profile with unison 3 plays three of them. */
void unison5 (IEngine& e)
{
    plain (e);
    bench::ownSet (e, "osc1.unison.voices", 5.0f);
    bench::ownSet (e, "osc1.unison.detune", 25.0f);
}

Quality quality (int voices, int unison, int oversampling, uint32_t fx, int economy)
{
    Quality q;
    q.maxVoices = voices;
    q.maxUnison = unison;
    q.oversampling = oversampling;
    q.interpolation = Interpolation::linear;
    q.fxMask = fx;
    q.economy = economy;
    return q;
}

const uint32_t kLowFx = qualityForTier (Tier::low).fxMask;

/** A four note chord struck twice with a release in between, 80 blocks of 128 frames at 44.1 kHz; left then right of every block. The notes of a chord are
    `strum` frames apart (0: all at the start of the block, where the event grid of the Control group changes nothing). */
std::vector<float> render (Patch patch, const Quality& q, int lowCap = -1, int strum = 3)
{
    auto e = createEngine (EngineKind::own);
    patch (*e);
    if (lowCap >= 0)
        setOwnLowTierUnisonCap (*e, lowCap);
    e->prepare (44100.0, 128, q);
    std::vector<float> out, l (128), r (128);
    for (int b = 0; b < 80; ++b)
    {
        std::vector<Event> ev;
        if (b == 0 || b == 40)
            for (int k = 0; k < 4; ++k)
            {
                Event n;
                n.type = Event::NoteOn;
                n.key = static_cast<int16_t> (48 + k * 5 + (b == 40 ? 2 : 0));
                n.value = 0.7f;
                n.frame = k * strum;
                ev.push_back (n);
            }
        if (b == 30 || b == 70)
        {
            Event o;
            o.type = Event::AllNotesOff;
            o.channel = -1;
            ev.push_back (o);
        }
        e->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 128);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), r.begin(), r.end());
    }
    return out;
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

bool finite (const std::vector<float>& a)
{
    for (const float x : a)
        if (! std::isfinite (x))
            return false;
    return true;
}

double rmsDb (const std::vector<float>& a)
{
    double s = 0.0;
    for (const float x : a)
        s += static_cast<double> (x) * x;
    return 10.0 * std::log10 (s / static_cast<double> (a.size()) + 1e-30);
}

/** The level of the difference of two renders relative to the level of the first, dB (identical: -300), after `b` is moved by the lag of -16 .. 16 frames that makes
    it smallest (the warp decimators of the two rates differ by 8 samples of delay). The renders hold blocks of 128 left then 128 right samples. */
double differenceDb (const std::vector<float>& a, const std::vector<float>& b, int* lagOut = nullptr)
{
    const size_t blocks = a.size() / 256, frames = blocks * 128;
    auto at = [] (const std::vector<float>& v, size_t channel, size_t frame) { return v[(frame / 128) * 256 + channel * 128 + frame % 128]; };
    double best = 1e300, ref = 0.0;
    for (size_t c = 0; c < 2; ++c)
        for (size_t f = 16; f + 16 < frames; ++f)
            ref += static_cast<double> (at (a, c, f)) * at (a, c, f);
    int bestLag = 0;
    for (int lag = -16; lag <= 16; ++lag)
    {
        double s = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t f = 16; f + 16 < frames; ++f)
            {
                const double d = static_cast<double> (at (a, c, f)) - at (b, c, static_cast<size_t> (static_cast<long> (f) + lag));
                s += d * d;
            }
        if (s < best)
        {
            best = s;
            bestLag = lag;
        }
    }
    if (lagOut != nullptr)
        *lagOut = bestLag;
    return 10.0 * std::log10 (best / (ref + 1e-30) + 1e-30);
}
} // namespace

TEST (economy_of_a_quality_follows_the_tier_unless_it_is_given)
{
    CHECK_EQ (economyOf (qualityForTier (Tier::low)), static_cast<uint32_t> (kEconomyAll));
    CHECK_EQ (economyOf (qualityForTier (Tier::medium)), 0u);
    CHECK_EQ (economyOf (qualityForTier (Tier::high)), 0u);
    CHECK_EQ (economyOf (Quality {}), 0u); // the default quality (oversampling 2)
    Quality q = qualityForTier (Tier::high);
    q.economy = kEconomyWarp | kEconomyMath;
    CHECK_EQ (economyOf (q), static_cast<uint32_t> (kEconomyWarp | kEconomyMath));
    q = qualityForTier (Tier::low);
    q.economy = 0;
    CHECK_EQ (economyOf (q), 0u);
    q.economy = 0x7fffffff; // bits that are no group are dropped
    CHECK_EQ (economyOf (q), static_cast<uint32_t> (kEconomyAll));
}

TEST (all_groups_given_explicitly_are_tier_low_to_the_last_bit)
{
    // what the MPC build runs: tier low with the engine's unison cap of 3, against the same quality spelled out (the profile "low" of shell-mpc/device_profile.h)
    for (const Patch patch : { &heavy, &plain, &warped, &morphed, &unison5 })
    {
        const auto tier = render (patch, qualityForTier (Tier::low));
        const auto spelled = render (patch, quality (8, 3, 1, kLowFx, kEconomyAll));
        CHECK (finite (tier));
        CHECK (same (tier, spelled));
    }
}

TEST (no_group_given_explicitly_is_the_medium_and_high_dsp_to_the_last_bit)
{
    // tier medium, against a quality with oversampling 1 (the sample interpolation of low, which these patches do not use) and no group
    for (const Patch patch : { &heavy, &plain, &warped, &morphed, &unison5 })
    {
        const auto tier = render (patch, qualityForTier (Tier::medium));
        const Quality m = qualityForTier (Tier::medium);
        const auto spelled = render (patch, quality (m.maxVoices, m.maxUnison, 1, m.fxMask, 0));
        CHECK (same (tier, spelled));
        const auto spelled2 = render (patch, quality (m.maxVoices, m.maxUnison, 2, m.fxMask, 0));
        CHECK (same (tier, spelled2));
    }
}

TEST (each_group_is_its_own_switch)
{
    // on the heavy patch (a warp, a morph, filters, effects, modulators) every group changes the samples, and no two single groups give the same samples
    const uint32_t groups[4] = { kEconomyWarp, kEconomyMorph, kEconomyMath, kEconomyControl };
    const auto none = render (&heavy, quality (8, 7, 2, kFxAll, 0));
    std::vector<std::vector<float>> single;
    for (const uint32_t g : groups)
    {
        single.push_back (render (&heavy, quality (8, 7, 2, kFxAll, static_cast<int> (g))));
        CHECK (finite (single.back()));
        CHECK (! same (single.back(), none));
    }
    for (size_t i = 0; i < 4; ++i)
        for (size_t k = i + 1; k < 4; ++k)
            CHECK (! same (single[i], single[k]));

    // a group does nothing to a patch that has nothing of it: Warp without a warp, Morph without a morph (the Math and Control groups stay as they are)
    const auto plainNone = render (&plain, quality (8, 7, 2, kFxAll, 0));
    CHECK (same (render (&plain, quality (8, 7, 2, kFxAll, kEconomyWarp)), plainNone));
    CHECK (same (render (&plain, quality (8, 7, 2, kFxAll, kEconomyMorph)), plainNone));
    CHECK (same (render (&plain, quality (8, 7, 2, kFxAll, kEconomyWarp | kEconomyMorph)), plainNone));
    CHECK (! same (render (&warped, quality (8, 7, 2, kFxAll, kEconomyWarp)), render (&warped, quality (8, 7, 2, kFxAll, 0))));
    CHECK (same (render (&warped, quality (8, 7, 2, kFxAll, kEconomyMorph)), render (&warped, quality (8, 7, 2, kFxAll, 0))));
    CHECK (! same (render (&morphed, quality (8, 7, 2, kFxAll, kEconomyMorph)), render (&morphed, quality (8, 7, 2, kFxAll, 0))));
    CHECK (same (render (&morphed, quality (8, 7, 2, kFxAll, kEconomyWarp)), render (&morphed, quality (8, 7, 2, kFxAll, 0))));
}

TEST (a_quality_with_explicit_groups_takes_its_unison_cap_from_max_unison_only)
{
    // setLowTierUnisonCap is the knob of the plain tier low; a profile carries its cap in maxUnison (also with every group on and oversampling 1)
    const auto seven = render (&heavy, quality (8, 7, 1, kLowFx, kEconomyAll));
    CHECK (same (render (&heavy, quality (8, 7, 1, kLowFx, kEconomyAll), 2), seven));
    CHECK (same (render (&heavy, quality (8, 7, 1, kLowFx, kEconomyAll), 0), seven));
    const auto three = render (&heavy, quality (8, 3, 1, kLowFx, kEconomyAll));
    CHECK (! same (three, seven));
    // ... and the plain tier low still follows the knob
    CHECK (same (render (&heavy, qualityForTier (Tier::low), 3), three));
    CHECK (! same (render (&heavy, qualityForTier (Tier::low), 2), three));
}

TEST (a_state_is_the_same_state_at_every_profile_and_sounds_within_the_bounds)
{
    // The same parameter values at the profile the MPC ships ("low") and at the full quality: the engine never writes a value back, so the saved state is the same
    // bytes, whatever profile played it. (The shell saves the values the host set and the content: shell-mpc buildStateChunk.)
    const Quality low = quality (8, 3, 1, kLowFx, kEconomyAll);
    const Quality high = qualityForTier (Tier::high);
    for (const Patch patch : { &heavy, &plain, &warped, &morphed, &unison5 })
    {
        std::string saved[2];
        int k = 0;
        for (const Quality& q : { low, high })
        {
            auto e = createEngine (EngineKind::own);
            patch (*e);
            e->prepare (44100.0, 128, q);
            std::vector<float> l (128), r (128);
            Event n;
            n.type = Event::NoteOn;
            n.key = 60;
            n.value = 0.8f;
            for (int b = 0; b < 20; ++b)
                e->process (b == 0 ? &n : nullptr, b == 0 ? 1 : 0, l.data(), r.data(), 128);
            ParamValues v = ParamValues::defaults();
            for (size_t i = 0; i < paramCount(); ++i)
                v.plain[i] = e->getParam (paramTable()[i].id);
            saved[k++] = saveState (v);
        }
        CHECK_EQ (saved[0], saved[1]);
    }

    // How far the sound of one state moves between the profiles. The bounds are measured on these patches (printed below) with a margin; they are not derived from
    // the per function error bounds of docs/MPC.md, which do not add up to a bound of a whole voice (a resonant filter and a free running phase amplify them).
    struct Case { const char* name; Patch patch; Quality a, b; double maxLevelDb, maxDifferenceDb; int lag; };
    const uint32_t all = kEconomyAll;
    const Case cases[] = {
        // the approximations alone on a saw through the resonant ladder: the same waveform far below audibility (measured -138 dB)
        { "plain: math fast against exact", &plain, low, quality (8, 3, 1, kLowFx, all & ~kEconomyMath), 0.05, -100.0, 0 },
        // the control economy alone, notes on the control grid, no voice stolen: the same samples
        { "plain: control economy against full", &plain, low, quality (8, 3, 1, kLowFx, all & ~kEconomyControl), 0.001, -250.0, 0 },
        { "heavy: control economy against full", &heavy, low, quality (8, 3, 1, kLowFx, all & ~kEconomyControl), 0.001, -250.0, 0 },
        // the approximations on the patch with everything (distortion, compressor, phaser, modulated filters): the same level, the waveform drifts (measured -26 dB)
        { "heavy: math fast against exact", &heavy, low, quality (8, 3, 1, kLowFx, all & ~kEconomyMath), 0.2, -15.0, 0 },
        // the warp at 2x against 4x: the same level; the 4x decimator is 8 samples later (docs/MPC.md: about 8 instead of 16 samples), the rest is the aliasing (measured -25 dB)
        { "warped: warp 2x against 4x", &warped, quality (8, 3, 1, kLowFx, kEconomyWarp), quality (8, 3, 1, kLowFx, 0), 0.2, -15.0, 8 },
        // the morph at half rate from the half size transform: a static morph is the same cycle (measured -137 dB), a moving one lags (heavy: measured -88 dB)
        { "morphed: morph half against full", &morphed, quality (8, 3, 1, kLowFx, kEconomyMorph), quality (8, 3, 1, kLowFx, 0), 0.05, -100.0, 0 },
        { "heavy: morph half against full", &heavy, low, quality (8, 3, 1, kLowFx, all & ~kEconomyMorph), 0.05, -60.0, 0 },
        // the unison cap: the level law holds (measured +0.45 dB with 5 instead of 3 voices), the waveform is another one (more voices beating)
        { "unison5: unison 3 against 5", &unison5, quality (8, 3, 1, kLowFx, 0), quality (8, 5, 1, kLowFx, 0), 1.5, 6.0, 0 },
        { "heavy: unison 3 against 7", &heavy, low, quality (8, 7, 1, kLowFx, all), 1.0, 0.0, 0 },
        // the effects that low bypasses (here reverb and flanger): the sound gets them, the level moves with their mix (measured -2.0 dB)
        { "heavy: effects low against all", &heavy, low, quality (8, 3, 1, kFxAll, all), 3.5, 0.0, 0 },
        // the whole step, low against high, on the patch with everything (measured -2.0 dB, the reverb and the flanger again)
        { "heavy: low against high", &heavy, low, high, 3.5, 0.0, -1 },
    };
    for (const Case& c : cases)
    {
        const auto a = render (c.patch, c.a, -1, 0), b = render (c.patch, c.b, -1, 0);
        int lag = 0;
        const double level = rmsDb (b) - rmsDb (a), difference = differenceDb (a, b, &lag);
        std::printf ("    %-58s level %+6.2f dB, difference %7.1f dB at a lag of %d samples\n", c.name, level, difference, lag);
        CHECK (finite (a) && finite (b));
        CHECK (std::fabs (level) <= c.maxLevelDb);
        CHECK (difference <= c.maxDifferenceDb);
        if (c.lag >= 0)
            CHECK_EQ (lag, c.lag);
    }
}

TEST (the_groups_follow_a_quality_change_while_playing)
{
    // setQuality hands the groups over at a block boundary without a crash or a non-finite sample (the MPC shell never does this: its profile is fixed for the life of
    // the process, because the switch is audible: the warp decimator's history is read at the other rate for one filter length and the warped oscillator moves by 8 samples)
    auto e = createEngine (EngineKind::own);
    heavy (*e);
    e->prepare (44100.0, 128, quality (8, 7, 1, kFxAll, kEconomyAll));
    std::vector<float> l (128), r (128), out;
    for (int b = 0; b < 64; ++b)
    {
        std::vector<Event> ev;
        if (b == 0)
            for (int k = 0; k < 4; ++k)
            {
                Event n;
                n.type = Event::NoteOn;
                n.key = static_cast<int16_t> (48 + k * 5);
                n.value = 0.7f;
                ev.push_back (n);
            }
        if (b % 4 == 0)
            e->setQuality (quality (8, 7, 1, kFxAll, (b / 4) % (static_cast<int> (kEconomyAll) + 1)));
        e->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 128);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), r.begin(), r.end());
    }
    CHECK (finite (out));
    float peak = 0.0f;
    for (const float x : out)
        peak = std::max (peak, std::fabs (x));
    CHECK (peak > 0.001f && peak < 8.0f);
}
