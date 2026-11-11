// SynthyBoy - param-audit: the values of a sweep and the contexts (the state a key needs to be audible) of every key group.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "audit.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"

using namespace synthyboy;

namespace paudit
{
namespace
{
bool starts (const std::string& s, const char* p) { return s.rfind (p, 0) == 0; }
bool ends (const std::string& s, const char* p)
{
    const size_t n = std::strlen (p);
    return s.size() >= n && s.compare (s.size() - n, n, p) == 0;
}
bool contains (const std::string& s, const char* p) { return s.find (p) != std::string::npos; }

std::string two (int n)
{
    char b[8];
    std::snprintf (b, sizeof b, "%02d", n);
    return b;
}
} // namespace

std::vector<float> sweepValues (const ParamInfo& info, Density density, std::vector<float>* outside)
{
    std::vector<float> v;
    const bool quick = density == Density::quick;
    const double range = static_cast<double> (info.max) - info.min;
    const bool discrete = info.scale == Scale::stepped || info.interval > 0.0f;
    if (discrete)
    {
        const double step = info.interval > 0.0f ? info.interval : 1.0;
        const long count = static_cast<long> (std::floor (range / step + 0.5)) + 1;
        auto at = [&] (long i) { return static_cast<float> (info.min + step * static_cast<double> (i)); };
        const long limit = quick ? 9 : 129;
        if (count <= limit)
            for (long i = 0; i < count; ++i)
                v.push_back (at (i));
        else
        {
            const long points = quick ? 9 : 65;
            for (long p = 0; p < points; ++p)
                v.push_back (at (std::lround (static_cast<double> (p) * static_cast<double> (count - 1) / static_cast<double> (points - 1))));
            const double dn = (info.def - info.min) / step;
            for (double d : { dn - 1.0, dn, dn + 1.0 })
                if (d >= 0.0 && d <= static_cast<double> (count - 1))
                    v.push_back (at (std::lround (d)));
        }
        if (quick && count <= limit)
            ;
    }
    else
    {
        const int points = quick ? 9 : 65;
        const float defN = toNormalised (info, info.def);
        std::vector<float> norms;
        for (int i = 0; i < points; ++i)
            norms.push_back (static_cast<float> (i) / static_cast<float> (points - 1));
        norms.push_back (defN);
        if (! quick)
        {
            norms.push_back (std::max (0.0f, defN - 1.0f / 64.0f));
            norms.push_back (std::min (1.0f, defN + 1.0f / 64.0f));
            norms.push_back (0.001f);
            norms.push_back (0.999f);
        }
        for (float nrm : norms)
            v.push_back (fromNormalised (info, nrm));
    }
    std::sort (v.begin(), v.end());
    v.erase (std::unique (v.begin(), v.end()), v.end());
    if (outside != nullptr)
    {
        outside->clear();
        outside->push_back (info.min - static_cast<float> (std::max (1.0, 0.05 * range)));
        outside->push_back (info.max + static_cast<float> (std::max (1.0, 0.05 * range)));
    }
    return v;
}

int destinationChoice (const std::string& key)
{
    const int idx = findParamIndex (hashKey (key.c_str()));
    if (idx < 0)
        return 0;
    return static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (idx)));
}

static int sourceChoice (const char* key)
{
    const auto* t = modSourceTable();
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (std::strcmp (t[i].key, key) == 0)
            return static_cast<int> (i) + 1;
    return 0;
}

Ovs routeOverrides (int slot, int source, const std::string& destKey, float amountPercent, bool bipolar)
{
    const std::string p = "mod" + two (slot) + ".";
    return { { p + "source", static_cast<float> (source) }, { p + "destination", static_cast<float> (destinationChoice (destKey)) }, { p + "amount", amountPercent }, { p + "bipolar", bipolar ? 1.0f : 0.0f } };
}

namespace
{
Ovs cat (Ovs a, const Ovs& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}
} // namespace

static std::vector<Context> contextsForBase (const ParamInfo& info)
{
    const std::string k = info.key;
    std::vector<Context> out;
    auto add = [&] (const std::string& name, Ovs ov, const char* script = "default") { out.push_back ({ name, std::move (ov), script }); };
    auto ladder = [&] (const std::string& label, const std::string& key, int lo, int hi, const Ovs& pre, const char* script = "default")
    {
        for (int v = lo; v <= hi; ++v)
            add (label + std::to_string (v), cat (pre, { { key, static_cast<float> (v) } }), script);
    };
    // routing sources: macro1 (constant, set to 60 %), lfo1 (2 Hz, slowed down to be inside the 1 s render), env2
    const int macro1 = sourceChoice ("macro1"), lfo1 = sourceChoice ("lfo1");
    const Ovs lowCut = { { "filter1.cutoff", 700.0f } };

    // ---- oscillators ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (k[0] == 'o' && starts (k, "osc") && k.size() > 5 && k[3] >= '1' && k[3] <= '3' && k[4] == '.')
    {
        const std::string P = k.substr (0, 4);
        const std::string sub = k.substr (5);
        Ovs pre;
        if (P != "osc1")
            pre.push_back ({ P + ".on", 1.0f });
        if (starts (sub, "sample."))
        {
            const Ovs t = cat (pre, { { P + ".type", 1.0f } });
            add ("sample", t);
            add ("sample+loop", cat (t, { { P + ".sample.loop_mode", 1.0f } }));
            for (int sm = 1; sm <= 2; ++sm)
                add ("sample+slices" + std::to_string (sm), cat (t, { { P + ".sample.slice_mode", static_cast<float> (sm) }, { P + ".sample.loop_mode", 1.0f } }));
            add ("sample+scan", cat (t, { { P + ".sample.keytrack", 0.0f }, { P + ".sample.loop_mode", 1.0f } }), "long");
            add ("sample+unison", cat (t, { { P + ".unison.voices", 5.0f }, { P + ".sample.loop_mode", 1.0f } }));
            add ("sample+fade", cat (t, { { P + ".sample.loop_mode", 4.0f }, { P + ".sample.fade_edges", 4.0f } }));
        }
        else if (starts (sub, "gran."))
        {
            const Ovs t = cat (pre, { { P + ".type", 2.0f }, { P + ".gran.manual", 0.0f } });
            add ("gran", t);
            add ("gran+manual", cat (t, { { P + ".gran.manual", 1.0f }, { P + ".gran.position", 40.0f } }));
            for (int m = 1; m <= 2; ++m)
                add ("gran+density" + std::to_string (m), cat (t, { { P + ".gran.density_mode", static_cast<float> (m) } }));
            for (int m = 1; m <= 2; ++m)
                add ("gran+length" + std::to_string (m), cat (t, { { P + ".gran.length_mode", static_cast<float> (m) } }));
            add ("gran+rand", cat (t, { { P + ".gran.rand_offset", 50.0f }, { P + ".gran.rand_pitch", 7.0f }, { P + ".gran.rand_dir", 50.0f }, { P + ".gran.rand_length", 50.0f }, { P + ".gran.rand_pan", 50.0f } }));
            add ("gran+warp", cat (t, { { P + ".warp.type", 1.0f }, { P + ".gran.rand_warp", 50.0f }, { P + ".gran.rand_warp2", 50.0f }, { P + ".warp2.type", 1.0f } }));
            add ("gran+yaxis", cat (t, { { P + ".gran.y_axis", 1.0f } }));
            add ("gran+loop", cat (t, { { P + ".gran.loop_grains", 1.0f }, { P + ".gran.reverse_grains", 1.0f }, { P + ".gran.scan_keytrack", 1.0f }, { P + ".gran.jump_start", 0.0f } }));
        }
        else if (starts (sub, "spec."))
        {
            const Ovs t = cat (pre, { { P + ".type", 3.0f } });
            add ("spec", t);
            add ("spec+manual", cat (t, { { P + ".spec.manual", 1.0f }, { P + ".spec.position", 30.0f } }));
            add ("spec+lohi", cat (t, { { P + ".spec.freq_lo", 300.0f }, { P + ".spec.freq_hi", 3000.0f } }));
            add ("spec+cut", cat (t, { { P + ".spec.cut", 60.0f } }));
            add ("spec+yaxis", cat (t, { { P + ".spec.y_axis", 1.0f } }));
            add ("spec+table", cat (t, { { P + ".spec.flt_use", 1.0f }, { P + ".spec.flt_frame", 30.0f } }));
            add ("spec+warp", cat (t, { { P + ".warp.type", 1.0f }, { P + ".warp2.type", 1.0f } }));
        }
        else if (starts (sub, "multi."))
        {
            const Ovs t = cat (pre, { { P + ".type", 4.0f } });
            add ("multi", t);
            add ("multi+override", cat (t, { { P + ".multi.override", 1.0f } }));
            add ("multi+velo", cat (t, { { P + ".multi.override", 1.0f }, { P + ".multi.vel_override", 1.0f } }));
            add ("multi+unison", cat (t, { { P + ".unison.voices", 5.0f } }));
        }
        else if (starts (sub, "warp.") || starts (sub, "warp2."))
        {
            const std::string typeKey = P + (starts (sub, "warp2.") ? ".warp2.type" : ".warp.type");
            if (ends (sub, ".type"))
                add ("base", pre);
            else
            {
                const std::set<int> first = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
                for (int t : first)
                    add ("warp" + std::to_string (t), cat (pre, { { typeKey, static_cast<float> (t) } }));
                add ("warp+unison", cat (pre, { { typeKey, 1.0f }, { P + ".unison.voices", 5.0f } }));
                for (int t : { 12, 14, 17, 20, 25, 27, 30, 34, 38, 41, 44, 47, 50, 52, 60, 76, 80 })
                    add ("warp" + std::to_string (t), cat (pre, { { typeKey, static_cast<float> (t) } }));
            }
        }
        else if (starts (sub, "morph."))
        {
            if (sub == "morph.type")
                add ("base", pre);
            else
            {
                ladder ("morph", P + ".morph.type", 1, 11, pre);
                add ("morph+unison", cat (pre, { { P + ".morph.type", 1.0f }, { P + ".unison.voices", 5.0f } }));
            }
        }
        else if (starts (sub, "unison."))
        {
            if (sub == "unison.voices")
                add ("base", pre);
            else
            {
                add ("unison", cat (pre, { { P + ".unison.voices", 5.0f } }));
                ladder ("unison+mode", P + ".unison.mode", 1, 4, cat (pre, { { P + ".unison.voices", 5.0f } }));
                add ("unison+stack", cat (pre, { { P + ".unison.voices", 8.0f }, { P + ".unison.stack", 4.0f } }));
                add ("unison+pitchmode", cat (pre, { { P + ".unison.voices", 5.0f }, { P + ".pitch_mode", 1.0f } }));
            }
        }
        else if (sub == "frame" || sub == "frame_spread" || sub == "smooth_interp")
        {
            add ("table", cat (pre, { { P + ".wavetable", 13.0f }, { P + ".unison.voices", sub == "frame_spread" ? 5.0f : 1.0f }, { P + ".frame", 40.0f } }));
            add ("builtin", cat (pre, { { P + ".unison.voices", 5.0f } }));
            add ("builtin-table", cat (pre, { { P + ".wavetable", 6.0f }, { P + ".unison.voices", 5.0f }, { P + ".frame", 40.0f } }));
        }
        else if (starts (sub, "remap."))
            add ("remap", cat (pre, { { P + ".wavetable", 13.0f }, { P + ".frame", 40.0f }, { P + ".remap.on", 1.0f }, { P + ".remap.slot", 1.0f } }));
        else if (sub == "random_phase" || sub == "phase" || starts (sub, "phase_"))
        {
            add ("base", pre);
            add ("unison", cat (pre, { { P + ".unison.voices", 5.0f } }));
            add ("legato", cat (pre, { { "master.legato", 1.0f } }), "legato");
            add ("same", cat (pre, { { P + ".random_phase", 0.0f } }), "same");
        }
        else if (starts (sub, "pitch_") || sub == "fixed_pitch" || sub == "transpose_snap" || sub == "ratio_src")
        {
            add ("transposed", cat (pre, { { P + ".transpose", 7.0f }, { P + ".tune", 20.0f }, { P + ".pitch_coarse", 7.0f }, { P + ".pitch_mode", 0.0f } }));
            add ("untracked", cat (pre, { { P + ".pitch_track", 0.0f } }));
            add ("base", pre);
            ladder ("pitchmode", P + ".pitch_mode", 1, 3, pre);
            add ("snap", cat (pre, { { P + ".transpose_snap", 100.0f } }));
            if (sub == "ratio_src")
                for (int src = 1; src <= 4; ++src)
                    add ("ratio" + std::to_string (src), cat (pre, { { P + ".pitch_mode", 2.0f }, { P + ".ratio_src", static_cast<float> (src) }, { "osc2.on", 1.0f }, { "osc3.on", 1.0f }, { "sub.on", 1.0f } }));
            add ("fixed", cat (pre, { { P + ".pitch_mode", 3.0f } }));
        }
        else if (sub == "bus1_send" || sub == "bus2_send" || sub == "filter_blend")
        {
            add ("base", pre);
            ladder ("dest", P + ".destination", 1, 5, pre);
            add ("blend", cat (pre, { { P + ".destination", 5.0f }, { "filter1.blend", 50.0f } }));
        }
        else if (sub == "destination")
            add ("base", cat (pre, { { "filter2.on", 1.0f } }));
        else if (starts (sub, "map."))
        {
            add ("base", pre);
            add ("range", cat (pre, { { P + ".map.key_low", 40.0f }, { P + ".map.key_high", 100.0f } }));
        }
        else if (sub == "env1")
            add ("slow-attack", cat (pre, { { "env1.attack", 400.0f }, { P + ".destination", 3.0f }, { "filter1.on", 0.0f } })); // straight to the effects, as the channel test of test_warpenv.cpp
        else
            add ("base", pre);
        if (out.empty() || out.back().name != "base")
            add ("fallback", pre);
        return out;
    }

    // ---- noise / sub -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (ends (k, ".env1") && ! starts (k, "env1.") && k.find ('.') == k.rfind ('.') && ! starts (k, "osc"))
    {
        const std::string F = k.substr (0, k.find ('.'));
        Ovs on = { { "env1.attack", 400.0f }, { "filter1.on", 0.0f } };
        if (F == "noise")
            on = cat (on, { { "osc1.on", 0.0f }, { "noise.on", 1.0f }, { "noise.level", 60.0f }, { "noise.destination", 3.0f } });
        else if (F == "sub")
            on = cat (on, { { "osc1.on", 0.0f }, { "sub.on", 1.0f }, { "sub.level", 80.0f }, { "sub.destination", 3.0f } });
        else if (F == "filter1")
            on = { { "env1.attack", 400.0f }, { "filter1.on", 1.0f }, { "filter1.destination", 0.0f }, { "osc1.destination", 0.0f } };
        else if (F == "filter2")
            on = { { "env1.attack", 400.0f }, { "filter1.on", 0.0f }, { "filter2.on", 1.0f }, { "filter2.destination", 0.0f }, { "osc1.destination", 1.0f } };
        add ("slow-attack", on);
        return out;
    }
    if (starts (k, "noise."))
    {
        const Ovs on = { { "noise.on", 1.0f }, { "noise.level", 60.0f } };
        if (k == "noise.sample" || k == "noise.type" || k == "noise.on" || k == "noise.level" || k == "noise.pan")
            add ("noise", on);
        add ("noise", on);
        add ("noise-sample", cat (on, { { "noise.type", 1.0f } }));
        add ("noise-colour", cat (on, { { "noise.sample", 3.0f } }));
        add ("noise-keytrack", cat (on, { { "noise.keytrack", 1.0f } }));
        add ("noise-bus", cat (on, { { "noise.destination", 5.0f }, { "filter1.blend", 50.0f } }));
        return out;
    }
    if (starts (k, "sub."))
    {
        const Ovs on = { { "sub.on", 1.0f }, { "sub.level", 80.0f } };
        add ("sub", on);
        add ("sub-filter", cat (on, { { "sub.destination", 0.0f } }));
        add ("sub-blend", cat (on, { { "sub.destination", 5.0f }, { "filter1.blend", 50.0f } }));
        add ("sub-legato", cat (on, { { "master.legato", 1.0f } }), "legato");
        return out;
    }

    if (k == "filter.routing")
    {
        add ("both-filters", { { "filter1.on", 1.0f }, { "filter2.on", 1.0f }, { "osc1.destination", 2.0f }, { "filter1.cutoff", 1500.0f }, { "filter2.cutoff", 3000.0f }, { "filter2.model", 2.0f } });
        return out;
    }

    // ---- filters ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (starts (k, "filter1.") || starts (k, "filter2.") || starts (k, "fxfilter."))
    {
        const std::string F = k.substr (0, k.find ('.'));
        Ovs on;
        if (F == "filter2")
            on = { { "filter2.on", 1.0f }, { "filter2.cutoff", 1200.0f }, { "filter1.on", 0.0f }, { "osc1.destination", 1.0f } };
        else if (F == "fxfilter")
            on = { { "fxfilter.on", 1.0f }, { "fxfilter.cutoff", 1200.0f } };
        else
            on = { { "filter1.cutoff", 1200.0f } };
        if (F == "filter2" && (k.find ("destination") != std::string::npos || k.find ("send") != std::string::npos))
            on = { { "filter2.on", 1.0f } };
        add ("filter", on);
        add ("filter+both", cat (on, { { "filter1.on", 1.0f }, { "filter2.on", 1.0f }, { "osc1.destination", 2.0f } }));
        add ("filter+series", cat (on, { { "filter1.on", 1.0f }, { "filter2.on", 1.0f }, { "osc1.destination", 2.0f }, { "filter.routing", 1.0f } }));
        for (int model = 0; model <= 26; ++model)
            add ("model" + std::to_string (model), cat (on, { { F + ".model", static_cast<float> (model) }, { F + ".resonance", 60.0f } }));
        for (int style = 1; style <= 11; ++style)
            add ("style" + std::to_string (style), cat (on, { { F + ".style", static_cast<float> (style) } }));
        add ("filter+keytrack-note", cat (on, { { F + ".keytrack", 100.0f } }), "chord8");
        add ("blend", cat (on, { { "osc1.destination", 5.0f }, { "osc1.filter_blend", 50.0f } }));
        return out;
    }

    // ---- envelopes -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (starts (k, "env") && k.size() > 5 && k[3] >= '1' && k[3] <= '6' && k[4] == '.')
    {
        const int n = k[3] - '0';
        const std::string sub = k.substr (5);
        Ovs pre;
        if (ends (sub, "_beats"))
            pre.push_back ({ k.substr (0, 4) + ".units", 1.0f });
        const Ovs shaped = cat (pre, { { k.substr (0, 4) + ".sustain", 30.0f }, { k.substr (0, 4) + ".decay", 300.0f }, { k.substr (0, 4) + ".decay_beats", 0.5f } }); // decay and hold act only below a full sustain
        if (n == 1)
        {
            add ("amp+sustain", shaped);
            add ("amp", pre);
            if (sub == "legato_inverted")
                add ("legato", cat (pre, { { "master.legato", 1.0f } }), "legato");
            add ("amp+retrig", cat (pre, { { "master.legato", 1.0f } }), "legato");
        }
        else
        {
            for (const char* dest : { "osc1.level", "filter1.cutoff", "osc1.tune" })
                add (std::string ("env->") + dest, cat (cat (shaped, lowCut), routeOverrides (1, n, dest, 80.0f)), "legato");
            for (const char* dest : { "osc1.level", "filter1.cutoff" })
                add (std::string ("env->") + dest + "-plain", cat (cat (pre, lowCut), routeOverrides (1, n, dest, 80.0f)));
        }
        return out;
    }
    // ---- LFOs ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if ((starts (k, "lfo") || starts (k, "rlfo")) && k.find ('.') != std::string::npos && k.find ('.') <= 6)
    {
        const bool random = starts (k, "rlfo");
        const int n = std::atoi (k.c_str() + (random ? 4 : 3));
        const std::string L = k.substr (0, k.find ('.'));
        const int src = sourceChoice (L.c_str());
        Ovs rate = { { L + ".rate", 6.0f } };
        for (const char* dest : { "osc1.level", "filter1.cutoff", "osc1.tune", "osc1.pan" })
        {
            Ovs o = cat (cat (rate, lowCut), routeOverrides (1, src, dest, dest[5] == 'u' ? 60.0f : 80.0f));
            add (std::string ("lfo->") + dest, o);
            add (std::string ("lfo->") + dest + "+stereo", cat (o, { { "mod01.stereo", 1.0f } }));
        }
        add ("lfo+nosmooth", cat (cat (rate, lowCut), cat (routeOverrides (1, src, "osc1.level", 80.0f), { { L + ".smooth", 0.0f } })));
        add ("lfo+smooth-slow", cat (cat (rate, lowCut), cat (routeOverrides (1, src, "osc1.level", 80.0f), { { L + ".smooth", 1.0f }, { L + ".smooth_time", 400.0f } })));
        for (int sync = 1; sync <= 4; ++sync)
            add ("lfo+sync" + std::to_string (sync), cat (cat (cat (rate, lowCut), routeOverrides (1, src, "osc1.level", 80.0f)), { { L + ".sync", static_cast<float> (sync) } }));
        if (! random)
            for (int t = 1; t <= 5; ++t)
                add ("lfo+type" + std::to_string (t), cat (cat (cat (rate, lowCut), routeOverrides (1, src, "osc1.level", 80.0f)), { { L + ".sync_type", static_cast<float> (t) } }), "legato");
        (void) n;
        return out;
    }
    // ---- macros ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (starts (k, "macro."))
    {
        const int n = std::atoi (k.c_str() + 6);
        const std::string name = "macro" + std::to_string (n);
        add ("macro->cutoff", cat (lowCut, routeOverrides (1, sourceChoice (name.c_str()), "filter1.cutoff", 80.0f)));
        add ("macro->level", routeOverrides (1, sourceChoice (name.c_str()), "osc1.level", -80.0f));
        return out;
    }

    // ---- the modulation matrix --------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (starts (k, "mod") && k.size() > 6 && k[5] == '.')
    {
        const int slot = std::atoi (k.c_str() + 3);
        const std::string P = "mod" + two (slot) + ".";
        const std::string sub = k.substr (6);
        auto routed = [&] (int source, const char* dest, float amount) { return cat (cat ({ { "macro.1", 60.0f }, { "lfo1.rate", 6.0f } }, lowCut), routeOverrides (slot, source, dest, amount)); };
        for (const char* dest : { "filter1.cutoff", "osc1.level", "osc1.tune", "osc1.pan" })
        {
            add (std::string ("macro->") + dest, routed (macro1, dest, 80.0f));
            add (std::string ("lfo->") + dest, routed (lfo1, dest, 80.0f));
            add (std::string ("lfo->") + dest + "+stereo", cat (routed (lfo1, dest, 80.0f), { { P + "stereo", 1.0f } }));
        }
        if (starts (sub, "aux_"))
        {
            Ovs o = routed (lfo1, "filter1.cutoff", 80.0f);
            for (int auxSrc : { macro1, lfo1, sourceChoice ("velocity") })
                add ("aux" + std::to_string (auxSrc), cat (cat (o, { { "macro.2", 50.0f } }), { { P + "aux_source", static_cast<float> (auxSrc) }, { P + "aux_output", 100.0f } }));
        }
        add ("env->cutoff", routed (sourceChoice ("env2"), "filter1.cutoff", 80.0f), "legato");
        add ("velocity->level", routed (sourceChoice ("velocity"), "osc1.level", 80.0f));
        return out;
    }

    // ---- effects ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        static const char* const fxGroups[] = { "chorus", "compressor", "delay", "distortion", "eq", "flanger", "phaser", "reverb", "hyperdim", "utility", "bode", "convolve", "splitter_lh", "splitter_lmh", "splitter_ms" };
        const std::string G = k.substr (0, k.find ('.'));
        for (const char* g : fxGroups)
            if (G == g)
            {
                const bool longTail = G == "delay" || G == "reverb" || G == "convolve" || G == "bode";
                const char* script = longTail ? "long" : "default";
                Ovs on = { { G + ".on", 1.0f } };
                if (G == "reverb")
                    on.push_back ({ "reverb.mix", 50.0f });
                if (G == "convolve")
                {
                    on.push_back ({ "convolve.ir", 1.0f });
                    on.push_back ({ "convolve.mix", 60.0f });
                }
                if (G == "distortion")
                    on.push_back ({ "distortion.drive", 12.0f });
                if (G == "eq")
                    on.push_back ({ "eq.low.gain", 9.0f });
                if (G == "chorus" || G == "flanger" || G == "phaser" || G == "delay" || G == "bode" || G == "convolve")
                    add (G + "+free", cat (on, { { G + ".sync", 0.0f } }), script); // the default of some effects is synced to the tempo: the time / rate keys act only when it is Free
                add (G, on, script);
                if (G == "compressor")
                    ladder ("bands", "compressor.bands", 1, 3, on);
                if (G == "distortion")
                {
                    ladder ("type", "distortion.type", 1, 13, on);
                    ladder ("filter", "distortion.filter_order", 1, 2, on);
                }
                if (G == "reverb")
                {
                    ladder ("type", "reverb.type", 1, 5, on, "long");
                    ladder ("room", "reverb.room", 0, 4, on, "long");
                }
                if (G == "delay")
                    ladder ("style", "delay.style", 0, 4, on, "long");
                if (G == "eq")
                    add ("eq+modes", cat (on, { { "eq.low.mode", 1.0f }, { "eq.mid.mode", 1.0f }, { "eq.high.mode", 1.0f }, { "eq.mid.gain", 9.0f }, { "eq.high.gain", 9.0f } }));
                if (G == "eq")
                    add ("eq+band2", cat (on, { { "eq.mid2.mode", 1.0f }, { "eq.mid2.gain", 12.0f } }));   // block B34: the second band acts when its mode is not Off
                if (G == "chorus" || G == "flanger" || G == "phaser" || G == "delay" || G == "bode" || G == "convolve")
                    for (int s = 1; s <= 3; ++s)
                        add ("sync" + std::to_string(s), cat (on, { { G + ".sync", static_cast<float> (s) } }), script);
                if (G == "hyperdim")
                    add ("hyperdim+retrig", cat (on, { { "hyperdim.retrig", 1.0f }, { "hyperdim.mix", 100.0f } }), script);
                if (G == "bode")
                {
                    add ("bode+shift", cat (on, { { "bode.shift", 40.0f }, { "bode.mono_input", 1.0f } }), script);
                    add ("bode+feed", cat (on, { { "bode.feed", 60.0f }, { "bode.delay", 250.0f }, { "bode.mix", 100.0f } }), script);
                    add ("bode+feed-synced", cat (on, { { "bode.feed", 60.0f }, { "bode.mix", 100.0f }, { "bode.sync", 1.0f } }), script);
                }
                if (G == "utility")
                    add ("utility+mono", cat (on, { { "utility.mono_bass", 1.0f } }));
                if (starts (G, "splitter"))
                    add ("splitter+bypass", cat (on, { { G + ".low_bypass", 1.0f } }));
                add (G + "+rack", cat (on, { { G + ".rack", 1.0f } }), script);
                add (G + "+mix", cat (on, { { G + ".mix", 100.0f } }), script);
                add (G + "+loud", cat (on, { { "master.volume", 0.0f }, { "osc1.level", 100.0f } }), script);
                return out;
            }
    }
    if (starts (k, "fx.order."))
    {
        add ("chain", { { "distortion.on", 1.0f }, { "distortion.drive", 14.0f }, { "chorus.on", 1.0f }, { "eq.on", 1.0f }, { "eq.low.gain", 12.0f }, { "phaser.on", 1.0f }, { "flanger.on", 1.0f }, { "delay.on", 1.0f }, { "reverb.on", 1.0f }, { "compressor.on", 1.0f }, { "fxfilter.on", 1.0f }, { "fxfilter.cutoff", 900.0f } }, "long");
        return out;
    }
    if (starts (k, "fxi") && k.size() > 6 && k[5] == '.')
    {
        const int n = std::atoi (k.c_str() + 3);
        const std::string P = "fxi" + two (n) + ".";
        const std::string sub = k.substr (6);
        if (sub == "type" || sub == "on" || sub == "rack")
        {
            add ("fxi", { { P + "on", 1.0f } }, "long");
            add ("fxi+rack", { { P + "on", 1.0f }, { P + "rack", 1.0f } }, "long");
            return out;
        }
        if (sub == "order")
        {
            for (int t : { 0, 2, 3, 5 })
                add ("fxi-order" + std::to_string (t), { { "fxi01.on", 1.0f }, { "fxi02.on", 1.0f }, { "fxi01.type", static_cast<float> (t) }, { "fxi02.type", 3.0f }, { "fxi01.p03", 0.9f }, { "fxi02.p03", 0.9f }, { "fxi02.p01", 0.9f } }, "long");
            return out;
        }
        const int number = std::atoi (sub.c_str() + 1) - 1; // p01 -> 0
        for (int t = 1; t < kNumFxInstanceTypes; ++t)
            if (fxTypeDescriptor (t).count > number)   // only the effect types that map this parameter
            {
                // a slot whose type is set through its key holds the zero values of the pool; the UI (and fxInstanceDefaults) sets the defaults of the type, with the module level at 0 dB (the level
                // minimum of a module mutes since REFCOMPARE round 3): the audit starts from the defaults
                Ovs ov { { P + "on", 1.0f }, { P + "type", static_cast<float> (t) } };
                float defaults[kFxInstanceParams];
                fxInstanceDefaults (t, defaults);
                for (int q = 0; q < fxTypeDescriptor (t).count; ++q)
                    ov.push_back ({ P + "p" + two (q + 1), defaults[q] });
                add ("fxi-type" + std::to_string (t), std::move (ov), "long");
            }
        return out;
    }

    // ---- mixer ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (starts (k, "bus1.") || starts (k, "bus2.") || starts (k, "direct.") || starts (k, "main.") || starts (k, "filter1.destination") || starts (k, "filter2.destination"))
    {
        const std::string b = k.substr (0, 4);
        for (int d = 0; d <= 6; ++d)
            add ("osc->dest" + std::to_string (d), { { "osc1.destination", static_cast<float> (d) }, { "osc1.bus1_send", 100.0f }, { "osc1.bus2_send", 100.0f }, { "chorus.on", 1.0f }, { "chorus.mix", 100.0f }, { "bus1.destination", 0.0f } });
        add ("bus1", { { "osc1.bus1_send", 100.0f }, { "osc1.destination", 3.0f }, { "chorus.on", 1.0f } });
        add ("bus2", { { "osc1.bus2_send", 100.0f }, { "osc1.destination", 3.0f }, { "chorus.on", 1.0f } });
        add ("bus-chain", { { "osc1.bus1_send", 100.0f }, { "osc1.destination", 6.0f }, { "bus1.destination", 2.0f }, { "chorus.on", 1.0f } });
        add ("bus2-chain", { { "osc1.bus2_send", 100.0f }, { "osc1.destination", 6.0f }, { "bus2.destination", 2.0f }, { "chorus.on", 1.0f } });
        add ("bus-direct", { { "osc1.bus1_send", 100.0f }, { "osc1.bus2_send", 100.0f }, { "osc1.destination", 6.0f }, { "bus1.destination", 1.0f }, { "bus2.destination", 1.0f } });
        add ("filter-dest", { { "filter1.destination", 1.0f }, { "filter2.on", 1.0f }, { "filter2.destination", 1.0f }, { "osc1.destination", 2.0f } });
        add ("filter-dest-fx", { { "filter1.bus1_send", 100.0f }, { "filter2.on", 1.0f }, { "filter2.bus2_send", 100.0f }, { "osc1.destination", 2.0f }, { "chorus.on", 1.0f }, { "filter1.destination", 2.0f }, { "filter2.destination", 2.0f } });
        (void) b;
        return out;
    }

    // ---- voice control ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (starts (k, "voicectl."))
    {
        add ("voicectl-osc-detuned", { { "osc2.on", 1.0f }, { "osc3.on", 1.0f }, { "noise.on", 1.0f }, { "sub.on", 1.0f }, { "osc2.transpose", 7.0f }, { "voicectl.s1.detune", 60.0f }, { "voicectl.s2.detune", -60.0f }, { "voicectl.s3.pan", 80.0f }, { "voicectl.s4.pan", -80.0f }, { "voicectl.s1.cutoff", 50.0f }, { "voicectl.s2.cutoff", -50.0f } }, "chord8");
        add ("chord8", {}, "chord8");
        add ("chord8+unison", { { "osc1.unison.voices", 3.0f } }, "chord8");
        add ("voicectl-mod", routeOverrides (1, sourceChoice ("voice_index"), "filter1.cutoff", 80.0f), "chord8");
        add ("voicectl-lfo", cat (lowCut, cat (routeOverrides (1, lfo1, "osc1.level", 80.0f), { { "lfo1.rate", 6.0f } })), "chord8");
        add ("voicectl-mod1", cat (lowCut, routeOverrides (1, sourceChoice ("voice_mod1"), "filter1.cutoff", 80.0f)), "chord8");
        add ("voicectl-mod2", cat (lowCut, routeOverrides (1, sourceChoice ("voice_mod2"), "filter1.cutoff", 80.0f)), "chord8");
        add ("voicectl-osc", { { "osc2.on", 1.0f }, { "osc3.on", 1.0f }, { "noise.on", 1.0f }, { "sub.on", 1.0f } }, "chord8");
        return out;
    }

    // ---- global ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    if (starts (k, "master.") || starts (k, "quality.") || starts (k, "curve.") || starts (k, "voice."))
    {
        add ("base", {});
        add ("chord8", {}, "chord8");
        add ("legato", { { "master.legato", 1.0f } }, "legato");
        add ("legato-porta", { { "master.legato", 1.0f }, { "master.portamento.time", 400.0f } }, "legato");
        add ("seq-porta", { { "master.portamento.time", 400.0f } }, "seq");
        add ("porta-always", { { "master.portamento.time", 400.0f }, { "master.portamento.always", 1.0f } }, "seq");
        add ("same", {}, "same");
        add ("same-stack", { { "master.voice.same_note", 1.0f } }, "same");
        add ("steal", { { "master.polyphony", 2.0f } }, "chord8");
        add ("bend", {}, "bend");
        add ("mpe", { { "master.mpe.on", 1.0f }, { "master.mpe.bend_range", 12.0f } }, "mpe");
        add ("mpe-macros", cat ({ { "master.mpe.on", 1.0f }, { "master.mpe.xyz_macro", 1.0f } }, cat (lowCut, routeOverrides (1, macro1, "filter1.cutoff", 80.0f))), "mpe");
        add ("mpe-slide", cat ({ { "master.mpe.on", 1.0f } }, cat (lowCut, routeOverrides (1, sourceChoice ("slide"), "filter1.cutoff", 80.0f))), "mpe");
        add ("scale", {}, "scale");
        add ("scale-major", { { "master.scale.type", 1.0f } }, "scale");
        add ("unison", { { "osc1.unison.voices", 5.0f } });
        add ("unison-override", cat ({ { "osc1.unison.voices", 5.0f }, { "osc1.unison.detune", 60.0f } }, routeOverrides (1, lfo1, "osc1.level", 80.0f)));
        add ("tuning", { { "master.tuning.scale", 1.0f } });
        add ("tuning-just", { { "master.tuning.scale", 2.0f } });
        add ("curve-velocity", {}, "legato");
        return out;
    }
    add ("base", {});
    return out;
}

/** Contexts for keys that need a particular state, and the two busy states ("kitchen sinks") that close every ladder. */
std::vector<Context> contextsFor (const ParamInfo& info)
{
    std::vector<Context> out = contextsForBase (info);
    const std::string k = info.key;
    auto add = [&] (const std::string& name, Ovs ov, const char* script = "default") { out.push_back ({ name, std::move (ov), script }); };
    const int macro1 = sourceChoice ("macro1"), lfo1 = sourceChoice ("lfo1");
    const Ovs lowCut = { { "filter1.cutoff", 700.0f } };
    auto both = [&] (Ovs a, const Ovs& b) { return cat (std::move (a), b); };
    const Ovs stereoSaw = { { "osc1.unison.voices", 5.0f }, { "osc1.unison.spread", 100.0f }, { "osc1.unison.detune", 40.0f } };

    if (starts (k, "bode."))
        for (const char* script : { "default", "fast", "long" })
            add (std::string ("bode+all-") + script, { { "bode.on", 1.0f }, { "bode.shift", 40.0f }, { "bode.mix", 100.0f }, { "bode.feed", 50.0f }, { "bode.delay", 80.0f }, { "bode.sync", 0.0f }, { "bode.dir", 100.0f }, { "bode.width", 100.0f }, { "bode.balance", 30.0f }, { "bode.blur", 20.0f } }, script);
    if (ends (k, ".fx_bypass") || k == "main.fx_bypass")
        for (int rack = 0; rack <= 2; ++rack)
            add ("bypass-rack" + std::to_string (rack), { { "chorus.on", 1.0f }, { "chorus.mix", 100.0f }, { "chorus.rack", static_cast<float> (rack) }, { "osc1.bus1_send", 100.0f }, { "osc1.bus2_send", 100.0f }, { "osc1.destination", 6.0f }, { "filter1.destination", 2.0f } });
    if (starts (k, "compressor."))
    {
        add ("compressor-quiet", { { "compressor.on", 1.0f }, { "master.volume", -40.0f }, { "compressor.low.lower_ratio", 60.0f }, { "compressor.mid.lower_ratio", 60.0f }, { "compressor.high.lower_ratio", 60.0f }, { "compressor.mix", 100.0f } });
        add ("compressor-limit", { { "compressor.on", 1.0f }, { "compressor.limit", 1.0f }, { "compressor.mix", 100.0f }, { "osc1.level", 100.0f }, { "master.volume", 0.0f } });
        add ("compressor-bands", { { "compressor.on", 1.0f }, { "compressor.bands", 3.0f }, { "compressor.limit", 1.0f }, { "master.volume", -40.0f }, { "compressor.low.lower_ratio", 80.0f }, { "compressor.mid.lower_ratio", 80.0f }, { "compressor.high.lower_ratio", 80.0f } });
    }
    if (starts (k, "env") && ends (k, "legato_inverted"))
        for (int n = 1; n <= 6; ++n)
            if (k.compare (3, 1, std::to_string (n)) == 0)
            {
                const std::string E = "env" + std::to_string (n);
                {
                    add ("legato-env", both (n == 1 ? Ovs {} : both (lowCut, routeOverrides (1, n, "osc1.level", 80.0f)), { { "master.legato", 1.0f }, { E + ".attack", 150.0f }, { E + ".release", 400.0f } }), "legato");
                    add ("legato-env-mono", both (n == 1 ? Ovs {} : both (lowCut, routeOverrides (1, n, "osc1.level", 80.0f)), { { "master.legato", 1.0f }, { "master.polyphony", 1.0f }, { E + ".attack", 150.0f }, { E + ".release", 400.0f }, { E + ".sustain", 60.0f } }), "legato");
                }
            }
    if (starts (k, "curve."))
        for (const char* dest : { "osc1.level", "filter1.cutoff" })
            add (std::string ("curve-legato->") + dest, both (both (lowCut, routeOverrides (1, sourceChoice (starts (k, "curve.velocity") ? "velocity" : "note"), dest, 80.0f)), { { "master.legato", 1.0f }, { "master.polyphony", 1.0f }, { "master.portamento.time", 600.0f }, { k, 1.0f } }), "legatoVel");
    if (starts (k, "reverb."))
        add ("reverb-shelves", { { "reverb.on", 1.0f }, { "reverb.mix", 60.0f }, { "reverb.low_shelf_gain", -6.0f }, { "reverb.high_shelf_gain", -6.0f }, { "reverb.pre_low_cut", 200.0f } }, "long");
    if (k == "delay.hq")
        add ("delay-modulated", both (routeOverrides (1, lfo1, "delay.time", 30.0f), { { "delay.on", 1.0f }, { "delay.sync", 0.0f }, { "delay.mix", 100.0f }, { "delay.feedback", 50.0f }, { "lfo1.rate", 5.0f } }), "long");
    if (k == "flanger.feedback")
    {
        add ("flanger-wet", { { "flanger.on", 1.0f }, { "flanger.sync", 0.0f }, { "flanger.mix", 100.0f }, { "flanger.depth", 20.0f }, { "flanger.center", 600.0f } }, "long");
        add ("flanger-sine", { { "flanger.on", 1.0f }, { "flanger.sync", 0.0f }, { "flanger.mix", 100.0f }, { "osc1.wavetable", 0.0f }, { "filter1.on", 0.0f }, { "flanger.depth", 0.0f } }, "single");
    }
    if (starts (k, "fxi") && k.size() > 6 && k[5] == '.' && (k.compare (6, 2, "on") == 0 || k.compare (6, 4, "rack") == 0 || k.compare (6, 5, "order") == 0))
    {
        const std::string P = k.substr (0, 6);
        for (int t : { 1, 2, 3, 5, 8 })
        {
            Ovs ov { { P + "type", static_cast<float> (t) }, { P + "on", 1.0f } };
            float defaults[kFxInstanceParams];
            fxInstanceDefaults (t, defaults);
            for (int q = 0; q < fxTypeDescriptor (t).count; ++q)
                ov.push_back ({ P + "p" + two (q + 1), defaults[q] });
            ov.push_back ({ P + "p03", 0.9f });
            ov.push_back ({ P + "p01", 0.8f });
            ov.push_back ({ P + "p02", 0.8f });
            add ("fxi-type" + std::to_string (t), std::move (ov), "long");
        }
    }
    if (starts (k, "fxi") && k.size() > 6 && k[5] == '.' && k[6] == 'p')
    {
        const std::string P = k.substr (0, 6);
        auto pv = [&] (int n, float v) { char b[8]; std::snprintf (b, sizeof b, "p%02d", n); return Ov { P + b, v }; };
        // the pool of a slot whose type is set through its key holds zeros: the module level would sit at its minimum, which mutes since REFCOMPARE round 3. Every context starts from the defaults of the type.
        auto addPool = [&] (const std::string& name, Ovs ov, const char* script)
        {
            int type = 0;
            for (const auto& o : ov)
                if (o.key == P + "type")
                    type = static_cast<int> (o.value);
            Ovs full;
            for (const auto& o : ov)
                if (o.key == P + "on" || o.key == P + "type")
                    full.push_back (o);
            float defaults[kFxInstanceParams];
            fxInstanceDefaults (type, defaults);
            for (int q = 0; q < fxTypeDescriptor (type).count; ++q)
                full.push_back ({ P + "p" + two (q + 1), defaults[q] });
            for (const auto& o : ov)
                if (! (o.key == P + "on" || o.key == P + "type"))
                    full.push_back (o);
            add (name, std::move (full), script);
        };
        for (int bands = 0; bands <= 3; ++bands)
            addPool ("fxi-compressor-bands" + std::to_string (bands), { { P + "on", 1.0f }, { P + "type", 2.0f }, pv (1, 1.0f), pv (4, static_cast<float> (bands) / 3.0f), pv (7, 0.8f), pv (12, 0.8f), pv (17, 0.8f), pv (5, 0.9f), pv (10, 0.9f), pv (15, 0.9f), pv (8, 0.9f), pv (13, 0.9f), pv (18, 0.9f), pv (9, 0.8f), pv (14, 0.8f), pv (19, 0.8f), pv (21, bands == 3 ? 1.0f : 0.0f), pv (22, 1.0f), { "master.volume", -30.0f } }, "long");
        addPool ("fxi-compressor", { { P + "on", 1.0f }, { P + "type", 2.0f }, pv (1, 1.0f), pv (4, 1.0f), pv (21, 1.0f), pv (7, 0.8f), pv (12, 0.8f), pv (17, 0.8f), pv (5, 0.9f), pv (10, 0.9f), pv (15, 0.9f), { "master.volume", -40.0f } }, "long");
        addPool ("fxi-compressor-loud", { { P + "on", 1.0f }, { P + "type", 2.0f }, pv (1, 1.0f), pv (4, 1.0f), pv (21, 1.0f), pv (2, 0.2f), pv (3, 0.2f), pv (9, 0.8f), pv (14, 0.8f), pv (19, 0.8f), pv (6, 0.5f), pv (11, 0.5f), pv (16, 0.5f), pv (8, 0.9f), pv (13, 0.9f), pv (18, 0.9f), { "master.volume", -6.0f }, { "osc1.level", 100.0f } }, "long");
        addPool ("fxi-compressor-latency", { { P + "on", 1.0f }, { P + "type", 2.0f }, pv (1, 0.5f), pv (21, 1.0f), pv (22, 1.0f) }, "long");
        for (int m = 0; m <= 26; ++m)
            addPool ("fxi-filter-model" + std::to_string (m), { { P + "on", 1.0f }, { P + "type", 6.0f }, pv (1, static_cast<float> (m) / 26.0f), pv (4, 0.6f), pv (3, 0.4f), pv (7, 1.0f) }, "long");
        for (int t = 0; t <= 5; ++t)
            for (int room : { 0, 2, 4 })
                addPool ("fxi-reverb-type" + std::to_string (t) + "-room" + std::to_string (room), { { P + "on", 1.0f }, { P + "type", 9.0f }, pv (1, 0.6f), pv (14, static_cast<float> (t) / 5.0f), pv (15, static_cast<float> (room) / 4.0f), pv (16, 1.0f), pv (3, 0.5f) }, "long");
    }
    if (starts (k, "fxi") && k.size() > 9 && k[5] == '.' && ends (k, ".order"))
    {
        const std::string A = k.substr (0, 5), B = std::string (A == "fxi01" ? "fxi02" : "fxi01");
        Ovs o = { { A + ".on", 1.0f }, { B + ".on", 1.0f }, { A + ".type", 4.0f }, { B + ".type", 6.0f }, { B + ".order", 500.0f } }; // the partner sits in the middle of the order range (both default to 1023: a tie goes by slot number)
        for (int n = 1; n <= 12; ++n)
        {
            char b[8];
            std::snprintf (b, sizeof b, ".p%02d", n);
            o.push_back ({ A + b, 0.8f });
            o.push_back ({ B + b, 0.35f });
        }
        add ("fxi-order-dist-filter", o, "long");
    }
    if (k == "bode.width" || k == "bode.retrig")
        add ("bode-stereo", both (stereoSaw, { { "bode.on", 1.0f }, { "bode.shift", 40.0f }, { "bode.mix", 100.0f }, { "bode.dir", 0.0f }, { "bode.retrig", 1.0f }, { "bode.mono_input", 0.0f } }), "fast");
    if (k == "compressor.limit_latency_comp")
        add ("compressor-limit-half", { { "compressor.on", 1.0f }, { "compressor.limit", 1.0f }, { "compressor.mix", 50.0f } });
    if (starts (k, "splitter_ms.") || starts (k, "splitter_lh.") || starts (k, "splitter_lmh.") || starts (k, "fx.order."))
        for (int rack = 3; rack <= 9; ++rack)
            add ("band-rack" + std::to_string (rack), both (stereoSaw, { { "splitter_ms.on", 1.0f }, { "splitter_lh.on", 1.0f }, { "splitter_lmh.on", 1.0f }, { "distortion.on", 1.0f }, { "distortion.drive", 20.0f }, { "distortion.rack", static_cast<float> (rack) }, { "chorus.on", 1.0f }, { "chorus.rack", static_cast<float> (rack) }, { "chorus.sync", 0.0f } }));
    if (starts (k, "hyperdim."))
        add ("hyperdim-all", { { "hyperdim.on", 1.0f }, { "hyperdim.mix", 100.0f }, { "hyperdim.dim_mix", 100.0f }, { "hyperdim.dim_size", 70.0f }, { "hyperdim.retrig", 1.0f }, { "hyperdim.unison", 6.0f } }, "fast");
    if (starts (k, "master.bend_down"))
    {
        add ("bend-down", { { "master.bend_range", 12.0f }, { "master.bend_down_link", 0.0f }, { "master.bend_down_range", 5.0f } }, "bendDown");
        add ("bend-down-linked", { { "master.bend_range", 12.0f }, { "master.bend_down_link", 1.0f }, { "master.bend_down_range", 5.0f } }, "bendDown");
    }
    if (starts (k, "master.mpe."))
    {
        add ("mpe-wheel", both (lowCut, both (routeOverrides (1, sourceChoice ("mod_wheel"), "filter1.cutoff", 80.0f), { { "master.mpe.on", 1.0f }, { "master.mpe.y_modwheel", 1.0f } })), "mpe");
        add ("mpe-wheel-bipolar", both (lowCut, both (routeOverrides (1, sourceChoice ("mod_wheel"), "filter1.cutoff", 80.0f), { { "master.mpe.on", 1.0f }, { "master.mpe.y_modwheel", 1.0f }, { "master.mpe.y_bipolar", 1.0f } })), "mpe");
        add ("mpe-xyz", both (lowCut, both (routeOverrides (1, macro1, "filter1.cutoff", 80.0f), { { "master.mpe.on", 1.0f }, { "master.mpe.xyz_macro", 1.0f } })), "mpe");
        add ("mpe-bend-to-x", { { "master.mpe.on", 1.0f }, { "master.mpe.bend_to_x", 1.0f }, { "master.mpe.xyz_macro", 1.0f }, { "master.mpe.bend_range", 2.0f } }, "mpe");
    }
    if (k == "master.voice_override")
        add ("override", { { "master.polyphony", 2.0f }, { "env1.release", 600.0f } }, "chord8");
    if (starts (k, "mod") && k.size() > 6 && k[5] == '.' && (ends (k, "delay_beats") || ends (k, "delay_sync")))
    {
        const std::string P = k.substr (0, 6);
        add ("delay-synced", both (both ({ { "macro.1", 60.0f } }, lowCut), both (routeOverrides (std::atoi (k.c_str() + 3), macro1, "filter1.cutoff", 80.0f), { { P + "delay_sync", 1.0f }, { P + "delay_beats", 1.0f } })), "long");
        add ("delay-free", both (both ({ { "macro.1", 60.0f } }, lowCut), both (routeOverrides (std::atoi (k.c_str() + 3), macro1, "filter1.cutoff", 80.0f), { { P + "delay_sync", 0.0f }, { P + "delay", 300.0f } })), "long");
    }
    if (k.size() > 5 && starts (k, "osc") && k[4] == '.' && k[3] >= '1' && k[3] <= '3')
    {
        const std::string P = k.substr (0, 4), sub = k.substr (5);
        Ovs pre;
        if (P != "osc1")
            pre.push_back ({ P + ".on", 1.0f });
        if (sub == "map.key_warp")
            for (int target = 1; target <= 3; ++target)
                if (std::string ("osc") + std::to_string (target) != P)
                    for (int wt : { 7, 8, 9, 10 })
                        add ("keywarp-source-" + P + "-target" + std::to_string (target) + "-warp" + std::to_string (wt), both (pre, { { P + ".map.key_low", 100.0f }, { "osc" + std::to_string (target) + ".on", 1.0f }, { "osc" + std::to_string (target) + ".warp.type", static_cast<float> (wt) }, { "osc" + std::to_string (target) + ".warp.amount", 100.0f } }));
        if (starts (sub, "map.key_"))
            add ("mapped-range", both (pre, { { P + ".map.key_low", 52.0f }, { P + ".map.key_high", 64.0f } }), "scale");
        if (sub == "gran.rand_warp" || sub == "gran.rand_warp2")
            for (int wt : { 3, 27, 33 }) // the pointwise warps (Quantize and the distortion group) are the ones a grain applies (granular.h)
                add ("gran-warp" + std::to_string (wt), both (pre, { { P + ".type", 2.0f }, { P + ".warp.type", static_cast<float> (wt) }, { P + ".warp.amount", 50.0f }, { P + ".warp2.type", static_cast<float> (wt) }, { P + ".warp2.amount", 50.0f }, { P + ".gran.rand_warp", 70.0f }, { P + ".gran.rand_warp2", 70.0f } }));
        if (sub == "multi.vel_override")
            add ("multi-velocity", both (pre, { { P + ".type", 4.0f }, { P + ".multi.override", 1.0f }, { P + ".multi.vel_track", 100.0f } }), "legatoVel");
        if (sub == "pitch_bend_track")
            add ("sample-untracked-bend", both (pre, { { P + ".type", 1.0f }, { P + ".sample.loop_mode", 1.0f }, { P + ".pitch_track", 0.0f }, { "master.bend_range", 12.0f } }), "bend");
        if (sub == "sample.scan_lock_tempo")
        {
            add ("sample-bpm87", both (pre, { { P + ".type", 1.0f }, { P + ".sample.keytrack", 0.0f }, { P + ".sample.loop_mode", 1.0f }, { P + ".sample.length_beats", 2.0f }, { P + ".sample.scan_to_bpm", 1.0f } }), "long87");
            add ("sample-bpm87-b", both (pre, { { P + ".type", 1.0f }, { P + ".sample.keytrack", 1.0f }, { P + ".sample.loop_mode", 1.0f }, { P + ".sample.length_beats", 1.0f } }), "long87");
        }
        if (sub == "sample.slice_play")
            add ("sample-slice-oneshot", both (pre, { { P + ".type", 1.0f }, { P + ".sample.loop_mode", 0.0f }, { P + ".sample.slice_mode", 2.0f }, { P + ".sample.slice", 40.0f }, { P + ".sample.slice_root", 48.0f } }), "long");
        if (starts (sub, "multi."))
            add ("multi-env", both (pre, { { P + ".type", 4.0f }, { P + ".multi.override", 1.0f }, { P + ".multi.sustain", 30.0f }, { P + ".multi.decay", 200.0f }, { P + ".multi.hold", 100.0f }, { P + ".multi.vel_override", 1.0f }, { P + ".multi.vel_track", 100.0f } }), "scale");
        if (sub == "phase_memory" || sub == "pitch_bend_track")
        {
            add ("phase-legato", both (pre, { { P + ".phase_legato", 1.0f }, { P + ".random_phase", 100.0f }, { P + ".unison.voices", 3.0f } }), "legato");
            add ("bend-tracking", both (pre, { { P + ".pitch_track", 1.0f }, { "master.bend_range", 12.0f } }), "bend");
        }
        if (starts (sub, "remap.r"))
            add ("remap-modulated", both (both (pre, { { P + ".wavetable", 13.0f }, { P + ".remap.on", 1.0f }, { P + ".remap.slot", 1.0f }, { "lfo1.rate", 5.0f } }), routeOverrides (1, lfo1, P + ".frame", 90.0f)));
        if (starts (sub, "sample."))
        {
            const Ovs t = both (pre, { { P + ".type", 1.0f }, { P + ".sample.loop_mode", 1.0f }, { P + ".sample.loop_start", 20.0f }, { P + ".sample.loop_end", 80.0f }, { P + ".sample.start", 10.0f }, { P + ".sample.end", 90.0f } });
            add ("sample-looped", t, "long");
            add ("sample-synced", both (t, { { P + ".sample.keytrack", 0.0f }, { P + ".sample.scan_lock_tempo", 1.0f }, { P + ".sample.length_beats", 4.0f }, { P + ".sample.scan", 100.0f }, { P + ".sample.scan_to_bpm", 1.0f } }), "long");
            add ("sample-sliced", both (t, { { P + ".sample.slice_mode", 2.0f }, { P + ".sample.slice_play", 0.0f }, { P + ".sample.slice", 60.0f } }), "long");
            add ("sample-sliced-single", both (t, { { P + ".sample.slice_mode", 1.0f }, { P + ".sample.slice_play", 1.0f }, { P + ".sample.slice", 60.0f } }), "long");
            add ("sample-loop-exit", both (t, { { P + ".sample.loop_exit", 1.0f }, { P + ".sample.loop_relative", 1.0f }, { P + ".sample.start", 40.0f } }), "scale");
        }
        if (starts (sub, "spec."))
        {
            add ("spec-table", both (pre, { { P + ".type", 3.0f }, { P + ".spec.flt_use", 1.0f }, { P + ".spec.flt_table", 1.0f }, { P + ".spec.flt_frame", 30.0f }, { P + ".spec.cut", 50.0f }, { P + ".spec.manual", 1.0f }, { P + ".spec.position", 40.0f } }));
            add ("spec-transients", both (pre, { { P + ".type", 3.0f }, { P + ".spec.transients", 1.0f } }), "fast");
        }
        if (sub == "unison.stack_repeat")
            add ("stack-repeat", both (pre, { { P + ".unison.voices", 8.0f }, { P + ".unison.stack", 5.0f }, { P + ".unison.detune", 60.0f } }));
    }
    if (starts (k, "rlfo"))
    {
        const std::string L = k.substr (0, k.find ('.'));
        const int src = sourceChoice (L.c_str());
        for (int sync = 0; sync <= 4; ++sync)
            add ("rlfo-sync" + std::to_string (sync), both (both ({ { L + ".rate", 6.0f }, { L + ".sync", static_cast<float> (sync) }, { L + ".stereo", 1.0f } }, lowCut), routeOverrides (1, src, "osc1.pan", 80.0f)));
    }
    if (starts (k, "splitter_ms."))
        add ("splitter-ms-stereo", both (stereoSaw, { { "splitter_ms.on", 1.0f }, { "splitter_ms.level", 0.0f } }));
    if (k == "utility.width" || k == "utility.pan")
        add ("utility-stereo", both (stereoSaw, { { "utility.on", 1.0f } }));
    if (starts (k, "voicectl."))
    {
        add ("voicectl-osc-all", { { "osc2.on", 1.0f }, { "osc3.on", 1.0f }, { "noise.on", 1.0f }, { "sub.on", 1.0f }, { "osc2.transpose", 7.0f }, { "osc3.transpose", 12.0f }, { "noise.level", 40.0f }, { "sub.level", 70.0f } }, "chord8");
        add ("voicectl-rand", { { "voicectl.rand.detune", 40.0f }, { "voicectl.rand.pan", 50.0f }, { "voicectl.rand.cutoff", 50.0f }, { "voicectl.rand.env", 50.0f } }, "chord8");
        add ("voicectl-lfo-scale", both (both (lowCut, routeOverrides (1, lfo1, "osc1.level", 80.0f)), { { "lfo1.rate", 5.0f }, { "voicectl.scale.lfo", 200.0f }, { "voicectl.s1.mod1", 50.0f } }), "chord8");
    }
    if (k == "osc1.unison.stack_repeat")
        add ("stack-repeat", both (stereoSaw, { { "osc1.unison.voices", 8.0f }, { "osc1.unison.stack", 5.0f } }));

    // the two busy states that close every ladder: nearly everything on, a chord / legato notes (no reverb: a random state would raise the noise floor of the context)
    const Ovs busy = { { "osc2.on", 1.0f }, { "osc3.on", 1.0f }, { "noise.on", 1.0f }, { "noise.level", 30.0f }, { "sub.on", 1.0f }, { "osc1.unison.voices", 3.0f }, { "osc1.destination", 2.0f }, { "filter2.on", 1.0f },
                       { "filter1.cutoff", 3000.0f }, { "filter2.cutoff", 2000.0f }, { "delay.on", 1.0f }, { "delay.sync", 0.0f }, { "delay.time", 120.0f }, { "delay.mix", 40.0f }, { "distortion.on", 1.0f }, { "distortion.drive", 6.0f },
                       { "eq.on", 1.0f }, { "eq.low.gain", 6.0f }, { "compressor.on", 1.0f }, { "hyperdim.on", 1.0f }, { "utility.on", 1.0f }, { "bode.on", 1.0f }, { "bode.shift", 20.0f }, { "bode.mix", 50.0f },
                       { "convolve.on", 1.0f }, { "convolve.ir", 1.0f }, { "convolve.mix", 20.0f }, { "flanger.on", 1.0f }, { "flanger.sync", 0.0f }, { "phaser.on", 1.0f }, { "phaser.sync", 0.0f }, { "chorus.on", 1.0f }, { "chorus.sync", 0.0f },
                       { "splitter_lh.on", 1.0f }, { "splitter_lmh.on", 1.0f }, { "splitter_ms.on", 1.0f } };
    add ("busy-chord", busy, "chord8");
    add ("busy-legato", both (busy, { { "master.legato", 1.0f }, { "master.portamento.time", 200.0f }, { "master.mpe.on", 1.0f } }), "legato");
    return out;
}

const char* exemptionReason (const ParamInfo& info)
{
    const std::string k = info.key;
    if (starts (k, "fxi") && k.size() > 6 && k[5] == '.' && k[6] == 'p')
    {
        const int number = std::atoi (k.c_str() + 7) - 1;
        bool used = false;
        for (int t = 1; t < kNumFxInstanceTypes; ++t)
            used = used || fxTypeDescriptor (t).count > number;
        if (! used)
            return "parameter slot beyond the parameter count of every effect type of the pool (fx_types.h descriptors): unused by design";
    }
    if (k == "master.tuning.mts_esp" || k == "master.tuning.mts_note_on_only")
        return "needs an MTS-ESP master connected (the shell links the client library, the core has only the seam); tested with a fake tuning source in test_param_semantics";
    return nullptr;
}

bool abruptByDesign (const ParamInfo& info)
{
    const std::string k = info.key;
    return contains (k, "distortion.type") || contains (k, "warp") || contains (k, ".wavetable") || contains (k, "bitcrush") || ends (k, ".model") || ends (k, ".style") || ends (k, ".type") || contains (k, "waveform");
}
} // namespace paudit
