// SynthyBoy - param-audit: stage 1 of the parameter effect audit, the dead / wrong-wired scan over the whole parameter table.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_param_audit [--engine vital|own] [--quick | --full] [--keys REGEX] [--group PREFIX] [--shard I/N [--claims DIR]] [--part I/N] [--out DIR] [--keep-wavs DIR]
//                         [--seed N] [--list-keys] [--list-contexts KEY] [--verbose]
//
// For every key of the table that the engine supports: put the key's context in place (tools/param-audit/contexts.cpp: the first state of a ladder in which the key is audible),
// render the reference patch (a saw through the default filter, C3 + G3, velocity 100, deterministic) at EVERY value of the key (--full: every step of a stepped key, 65 points + default,
// both neighbours and the edges of a continuous one; --quick: nine) and measure RMS, peak, spectral centroid, 24 bands, balance, f0, envelope, zero crossings, click and level modulation.
// A key is ALIVE if the sound changes with it (renders are deterministic: bit identical audio for every value = DEAD). Keys flagged modulatable are swept again through a routed source
// (macro 1 through the modulation matrix). Per value: finite, peak bounded, no click, the value outside the range renders as the edge, the default renders the same twice.
// Output: one JSON line per key (keys.jsonl) in --out. scripts/param_audit_report.py merges the shards into report.md / CSV / JSON and compares them with the committed baseline.
// Exit code 0; the verdict of the run is made by the script.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "audit.h"
#include "synthyboy/engine_caps.h"
#include "synthyboy/modulation.h"

using namespace synthyboy;
using namespace paudit;

namespace
{
struct Options
{
    EngineKind engine = EngineKind::vital;
    Density density = Density::full;
    std::string keysRegex, group, out = ".", wavs, listContexts, claims, repeatKey;
    int repeatContext = 0, part = 0, parts = 1;
    int shard = 0, shards = 1;
    unsigned seed = 1;
    bool list = false, verbose = false, allInstances = false;
};

/** Instances 2 .. N of a family (osc2/3, lfo2..10, rlfo2..4, env3..6, mod02..64, fxi02..16) are the same code as instance 1: they run at the quick density unless --all-instances. */
bool followerInstance (const std::string& k)
{
    static const std::regex rx ("^(osc[23]\\.|lfo([2-9]|10)\\.|rlfo[2-4]\\.|env[3-6]\\.|mod(0[2-9]|[1-6][0-9])\\.|fxi(0[2-9]|1[0-6])\\.)");
    return std::regex_search (k, rx);
}

/** The last instance of each family: the one --quick keeps next to instance 1. */
bool lastInstance (const std::string& k)
{
    static const std::regex rx ("^(osc3\\.|lfo10\\.|rlfo4\\.|env6\\.|mod64\\.|fxi16\\.)");
    return std::regex_search (k, rx);
}

std::string jsonStr (const std::string& s)
{
    std::string o = "\"";
    for (char c : s)
    {
        if (c == '"' || c == '\\')
        {
            o += '\\';
            o += c;
        }
        else if (static_cast<unsigned char> (c) < 32)
            o += ' ';
        else
            o += c;
    }
    return o + "\"";
}

std::string num (double v)
{
    if (! std::isfinite (v))
        return "null";
    char b[48];
    std::snprintf (b, sizeof b, "%.6g", v);
    return b;
}

void writeWav (const std::string& path, const Audio& a)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t frames = static_cast<uint32_t> (a.l.size()), bytes = frames * 4;
    auto w32 = [&] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto w16 = [&] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    f.write ("RIFF", 4); w32 (36 + bytes); f.write ("WAVEfmt ", 8); w32 (16); w16 (1); w16 (2); w32 (48000); w32 (48000 * 4); w16 (4); w16 (16); f.write ("data", 4); w32 (bytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float s : { a.l[i], a.r[i] })
            w16 (static_cast<uint16_t> (static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f))));
}

struct Point
{
    float value = 0.0f;
    Metrics m;
    double diffDb = -180.0;     // peak of the difference to the render at the default, against its peak
    double diffPrevDb = -180.0; // ... to the render of the value before
    bool grid = false;
};

/** Values of the nine point grid the baseline compares (a subset of the quick and of the full sweep). */
std::vector<float> gridValues (const ParamInfo& info)
{
    std::vector<float> v;
    const double range = static_cast<double> (info.max) - info.min;
    if (info.scale == Scale::stepped || info.interval > 0.0f)
    {
        const double step = info.interval > 0.0f ? info.interval : 1.0;
        const long count = static_cast<long> (std::floor (range / step + 0.5)) + 1;
        const long points = std::min<long> (9, count);
        for (long p = 0; p < points; ++p)
            v.push_back (static_cast<float> (info.min + step * static_cast<double> (std::lround (static_cast<double> (p) * static_cast<double> (count - 1) / static_cast<double> (std::max<long> (1, points - 1))))));
    }
    else
        for (int i = 0; i < 9; ++i)
            v.push_back (fromNormalised (info, static_cast<float> (i) / 8.0f));
    std::sort (v.begin(), v.end());
    v.erase (std::unique (v.begin(), v.end()), v.end());
    return v;
}

struct Sweep
{
    std::vector<Point> points;
    bool alive = false;
    std::vector<std::string> moved;
    double maxDiffDb = -180.0;
    double noiseDb = -180.0; // the run to run noise of the context: the default rendered again (random LFOs, effects with a free running state)
    double floorDb = kNoiseFloorDb;
    int distinct = 0;
    std::vector<std::string> findings;
};

double peakOf (const Audio& a)
{
    double p = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i)
        p = std::max ({ p, std::fabs (static_cast<double> (a.l[i])), std::fabs (static_cast<double> (a.r[i])) });
    return p;
}

double diffDbOf (const Audio& a, const Audio& ref)
{
    double d = 0.0;
    for (size_t i = 0; i < a.l.size() && i < ref.l.size(); ++i)
        d = std::max ({ d, std::fabs (static_cast<double> (a.l[i]) - ref.l[i]), std::fabs (static_cast<double> (a.r[i]) - ref.r[i]) });
    const double p = std::max (peakOf (ref), 1.0e-6);
    return d <= 0.0 ? -180.0 : 20.0 * std::log10 (std::max (d / p, 1.0e-9));
}

/** Renders every value of `values` in the context `ctx` (+ extra overrides); the default's audio is the reference of the difference. `tweak` sets the swept thing. */
template <typename SetFn>
Sweep sweepWith (Rig& rig, const Ovs& base, const Script& script, const std::vector<float>& values, float defValue, const std::set<float>& grid, SetFn setValue, const Options& o, const std::string& wavTag, bool careful = true)
{
    Sweep sw;
    rig.begin (base);
    // A pass over the values first, unrecorded: the first use of a value can build state lazily (tables, filter memory) so that the first sound differs from the later renders of
    // the same value by far more than the noise floor (seen: -12 dB on the first formant sweep of a fresh engine). All values for a short list, the ends and the middle otherwise.
    if (careful)
        for (size_t i = 0; i < values.size(); ++i)
            if (values.size() <= 12 || i == 0 || i + 1 == values.size() || i == values.size() / 2)
            {
                setValue (values[i]);
                rig.render (script);
            }
    setValue (defValue);
    if (careful)
        rig.render (script);
    const Audio ref = rig.render (script);
    if (careful)
    {
        const Audio rep1 = rig.render (script), rep2 = rig.render (script);
        sw.noiseDb = std::max (diffDbOf (rep1, ref), diffDbOf (rep2, rep1));
        sw.floorDb = std::max (kNoiseFloorDb, sw.noiseDb + 3.0);
    }
    Audio prev;
    int distinct = 1;
    for (float v : values)
    {
        setValue (v);
        Audio a = rig.render (script);
        Point p;
        p.value = v;
        p.m = analyse (a, script);
        p.grid = grid.count (v) > 0;
        p.diffDb = diffDbOf (a, ref);
        if (! sw.points.empty())
        {
            p.diffPrevDb = diffDbOf (a, prev);
            if (p.diffPrevDb > sw.floorDb)
                ++distinct;
        }
        sw.maxDiffDb = std::max (sw.maxDiffDb, p.diffDb);
        sw.points.push_back (p);
        if (! o.wavs.empty())
        {
            std::filesystem::create_directories (o.wavs);
            writeWav (o.wavs + "/" + wavTag + "_" + fmtG (v) + ".wav", a);
        }
        prev = std::move (a);
    }
    // the default again: deterministic (up to the noise floor)?
    setValue (defValue);
    if (sw.noiseDb > kNoiseFloorDb)
        sw.findings.push_back ("INFO NONDETERMINISTIC context: the default renders differently each time by " + fmtG (sw.noiseDb) + " dB (random LFO / free running effect state); verdicts need a change above that");
    sw.distinct = distinct;
    for (int i = 0; i < kNumMetrics; ++i)
    {
        double lo = 1e300, hi = -1e300;
        for (const auto& p : sw.points)
        {
            lo = std::min (lo, metricValue (p.m, i));
            hi = std::max (hi, metricValue (p.m, i));
        }
        if (hi - lo > metricThreshold (i))
            sw.moved.push_back (metricName (i));
    }
    sw.alive = sw.maxDiffDb > sw.floorDb;
    if (! sw.alive)
        sw.moved.clear();
    return sw;
}

std::string curveJson (const Sweep& sw, bool gridOnly)
{
    std::ostringstream s;
    s << "[";
    bool first = true;
    for (const auto& p : sw.points)
    {
        if (gridOnly && ! p.grid)
            continue;
        if (! first)
            s << ",";
        first = false;
        s << "[" << num (p.value) << "," << num (p.m.rmsDb) << "," << num (p.m.peakDb) << "," << num (p.m.centroid) << "," << num (p.m.balanceDb) << "," << num (p.m.f0) << "," << num (p.m.attackMs) << "," << num (p.m.decayDb) << "," << num (p.m.releaseMs)
          << "," << num (p.m.zcRate) << "," << num (p.m.jump) << "," << num (p.m.envStdDb) << "," << num (p.m.envHz) << "," << num (p.diffDb) << "," << num (p.m.tailDb) << "]";
    }
    s << "]";
    return s.str();
}

struct KeyResult
{
    std::string verdict, context, caps, note;
    Sweep direct, mod;
    std::string modVerdict = "n/a", modContext;
    std::vector<std::string> findings;
    int contextsTried = 0;
    double seconds = 0.0;
    int renders = 0;
    float modAmount = 0.0f;
    std::vector<float> modMacro;
};

std::string capsText (size_t index)
{
    std::string s;
    const auto c1 = requiredCaps (index);
    const auto c2 = requiredCaps2 (index);
    for (int b = 0; b < 64; ++b)
        if (c1 & (1ull << b))
            s += std::string (s.empty() ? "" : "+") + capName (1ull << b);
    for (int b = 0; b < 64; ++b)
        if (c2 & (1ull << b))
            s += std::string (s.empty() ? "" : "+") + capName2 (1ull << b);
    return s;
}

KeyResult auditKey (Rig& rig, size_t index, const Options& o)
{
    const auto& info = paramTable()[index];
    KeyResult r;
    const auto t0 = std::chrono::steady_clock::now();
    const long renders0 = Rig::renderCount();
    r.caps = capsText (index);
    if (! rig.engine().supportsParam (info.id))
    {
        r.verdict = "UNSUPPORTED";
        return r;
    }
    if (const char* why = exemptionReason (info))
    {
        r.verdict = "EXEMPT";
        r.note = why;
        return r;
    }
    const Density density = (o.density == Density::full && ! o.allInstances && followerInstance (info.key)) ? Density::quick : o.density;
    const bool full = density == Density::full;
    const Options quiet;
    const auto grid = gridValues (info);
    const std::set<float> gridSet (grid.begin(), grid.end());
    std::vector<float> outside;
    const auto probe = sweepValues (info, Density::quick, &outside);
    const auto values = sweepValues (info, density);
    const auto ctxs = contextsFor (info);
    auto makeSet = [&] (Rig& rg, const std::string& key) { return [&rg, key] (float v) { rg.set (key, v); }; };

    // 1. the ladder: the first context in which the probe values sound different
    int chosen = -1;
    for (size_t c = 0; c < ctxs.size() && chosen < 0; ++c)
    {
        ++r.contextsTried;
        const auto script = scriptByName (ctxs[c].script);
        auto sw = sweepWith (rig, ctxs[c].ov, script, probe, info.def, gridSet, makeSet (rig, info.key), quiet, "", false); // the fast probe: a context that shows nothing is left at once
        r.renders += static_cast<int> (probe.size()) + 1;
        if (sw.alive)
        {
            sw = sweepWith (rig, ctxs[c].ov, script, probe, info.def, gridSet, makeSet (rig, info.key), quiet, ""); // ... one that shows something is measured with the warm-up and the noise of the context
            r.renders += 2 * static_cast<int> (probe.size()) + 6;
        }
        if (o.verbose)
            std::fprintf (stderr, "    ladder %s: max diff %.1f dB, noise %.1f dB, floor %.1f -> %s\n", ctxs[c].name.c_str(), sw.maxDiffDb, sw.noiseDb, sw.floorDb, sw.alive ? "alive" : "no");
        if (sw.alive)
            chosen = static_cast<int>(c);
    }
    if (chosen < 0)
    {
        r.verdict = "DEAD";
        r.context = ctxs.empty() ? "none" : ctxs.front().name;
    }
    else
    {
        const auto& ctx = ctxs[static_cast<size_t> (chosen)];
        r.context = ctx.name;
        const auto script = scriptByName (ctx.script);
        r.direct = sweepWith (rig, ctx.ov, script, values, info.def, gridSet, makeSet (rig, info.key), o, info.key);
        r.renders += static_cast<int> (values.size()) + 2;
        r.verdict = "ALIVE";
        // per value assertions
        int clicks = 0, nonFinite = 0, loud = 0;
        double defaultJump = 0.0;
        for (const auto& p : r.direct.points)
            if (p.value == info.def)
                defaultJump = p.m.jump;
        for (const auto& p : r.direct.points)
        {
            if (! p.m.finite)
                ++nonFinite;
            else
            {
                if (p.m.peakDb > 12.0)
                    ++loud;
                // a click is a step of the order of the signal itself: larger than the peak and than 2.5 times the steepest step of the same key at its default
                // (a band limited saw already steps by half its peak at every cycle)
                if (p.m.jump > std::max (1.0, 2.5 * defaultJump) && ! abruptByDesign (info))
                    ++clicks;
            }
        }
        if (nonFinite > 0)
            r.findings.push_back ("NONFINITE at " + std::to_string (nonFinite) + " values");
        if (loud > 0)
            r.findings.push_back ("PEAK above +12 dBFS at " + std::to_string (loud) + " values");
        if (clicks > 0)
            r.findings.push_back ("CLICK (a step larger than the peak and 2.5 times the default's steepest step) at " + std::to_string (clicks) + " values");
        for (const auto& f : r.direct.findings)
            r.findings.push_back (f);
        // plateaus: neighbouring values that render the same although the key is alive. Reported for the full sweeps of continuous keys only (the quick sweep has nine points);
        // a plateau that touches an end of the range is saturation (INFO), one in the middle is a finding.
        if (full && info.scale != Scale::stepped && info.interval <= 0.0f)
        {
            std::string runs;
            int nMid = 0, nEnd = 0;
            size_t i = 0;
            const auto& P = r.direct.points;
            while (i < P.size())
            {
                size_t j = i;
                while (j + 1 < P.size() && P[j + 1].diffPrevDb <= r.direct.floorDb)
                    ++j;
                if (j > i && P.size() > 3)
                {
                    const bool atEnd = i == 0 || j + 1 == P.size();
                    (atEnd ? nEnd : nMid)++;
                    if (! atEnd && nMid <= 6)
                        runs += (runs.empty() ? "" : "; ") + fmtG (P[i].value) + ".." + fmtG (P[j].value);
                }
                i = j + 1;
            }
            // a context with run to run noise cannot decide it, and a time (ms, beats) below one sample / beyond the length of the render has no audible effect in it
            const bool undecidable = r.direct.noiseDb > kNoiseFloorDb || info.unit == Unit::ms || info.unit == Unit::beats;
            if (nMid > 0)
                r.findings.push_back (std::string (undecidable ? "INFO " : "") + "PLATEAU-MID " + std::to_string (nMid) + " run(s) of identical audio inside the range: " + runs
                                      + (undecidable ? (r.direct.noiseDb > kNoiseFloorDb ? " (random context)" : " (time below the resolution / beyond the render)") : ""));
            if (nEnd > 0)
                r.findings.push_back ("INFO PLATEAU-END " + std::to_string (nEnd) + " run(s) of identical audio at an end of the range (saturation)");
        }
        // outside the range renders as the edge
        {
            Rig& rg = rig;
            rg.begin (ctx.ov);
            auto at = [&] (float v) { rg.set (info.key, v); rg.render (script); return rg.render (script); };
            const Audio lo = at (info.min), hi = at (info.max), loOut = at (outside[0]), hiOut = at (outside[1]);
            if (diffDbOf (loOut, lo) > r.direct.floorDb || diffDbOf (hiOut, hi) > r.direct.floorDb)
            {
                // the engine clamps in setParam: when getParam reports the edge, the audio of the two renders (made several renders apart) differs for another reason (a random source, a voice
                // that was reused), which is not a clamping defect
                rg.set (info.key, outside[0]);
                const bool lowClamped = rg.get (info.key) == info.min;
                rg.set (info.key, outside[1]);
                const bool highClamped = rg.get (info.key) == info.max;
                if (lowClamped && highClamped)
                    r.findings.push_back ("INFO OUTSIDE audio of the clamped value differs from the edge's (getParam reports the edge: the clamp is right; a random source or a reused voice)");
                else
                    r.findings.push_back ("OUTSIDE value is not clamped to the range");
            }
        }
    }

    // 2. modulation: macro 1 (or 2) through slot 64 (or 63), swept 0..100 %
    if ((info.flags & kParamModulatable) != 0 && destinationChoice (info.key) > 0 && ! (info.scale == Scale::stepped && info.max - info.min > 100))
    {
        const std::string k = info.key;
        const bool macroKey = k == "macro.1";
        const int slot = (k.rfind ("mod64.", 0) == 0 || k.rfind ("mod63.", 0) == 0) ? 62 : 64;
        const char* srcName = macroKey ? "macro2" : "macro1";
        int src = 0;
        for (size_t i = 0; i < modSourceCount(); ++i)
            if (std::strcmp (modSourceTable()[i].key, srcName) == 0)
                src = static_cast<int> (i) + 1;
        const std::string macroKeyName = macroKey ? "macro.2" : "macro.1";
        const float amount = (info.max - info.def) >= (info.def - info.min) ? 100.0f : -100.0f;
        r.modAmount = amount;
        std::vector<float> macros;
        const int n = full ? 33 : 5;
        for (int i = 0; i < n; ++i)
            macros.push_back(100.0f * static_cast<float> (i) / static_cast<float> (n - 1));
        r.modMacro = macros;
        std::set<float> macroGrid;
        for (int i = 0; i < 5; ++i)
            macroGrid.insert (25.0f * static_cast<float> (i));
        std::vector<size_t> order;
        if (chosen >= 0)
            order.push_back (static_cast<size_t> (chosen));
        for (size_t c = 0; c < ctxs.size(); ++c)
            if (static_cast<int> (c) != chosen)
                order.push_back (c);
        for (size_t c : order)
        {
            const auto& ctx = ctxs[c];
            Ovs ov = ctx.ov;
            for (const auto& x : routeOverrides (slot, src, info.key, amount))
                ov.push_back (x);
            const auto script = scriptByName (ctx.script);
            auto setM = [&] (float v) { rig.set (macroKeyName, v); };
            auto sw = sweepWith (rig, ov, script, macros, 0.0f, macroGrid, setM, quiet, "", false);
            r.renders += n + 1;
            if (sw.alive)
            {
                sw = sweepWith (rig, ov, script, macros, 0.0f, macroGrid, setM, o, k + ".mod");
                r.renders += 2 * n + 6;
            }
            if (sw.alive)
            {
                r.mod = sw;
                r.modVerdict = "ALIVE";
                r.modContext = ctx.name;
                break;
            }
            if (c == order.front())
            {
                r.mod = sw;
                r.modContext = ctx.name;
            }
        }
        if (r.modVerdict == "n/a")
            r.modVerdict = "DEAD";
    }
    r.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    r.renders = static_cast<int> (Rig::renderCount() - renders0); // counted by the rig
    return r;
}

std::string toJson (const ParamInfo& info, const KeyResult& r, const Options& o)
{
    std::ostringstream s;
    s << "{\"key\":" << jsonStr (info.key) << ",\"group\":" << jsonStr (info.group) << ",\"verdict\":" << jsonStr (r.verdict) << ",\"context\":" << jsonStr (r.context) << ",\"caps\":" << jsonStr (r.caps)
      << ",\"note\":" << jsonStr (r.note) << ",\"min\":" << num (info.min) << ",\"max\":" << num (info.max) << ",\"def\":" << num (info.def) << ",\"stepped\":" << (info.scale == Scale::stepped ? 1 : 0)
      << ",\"unit\":" << jsonStr (unitLabel (info.unit)) << ",\"values\":" << r.direct.points.size() << ",\"distinct\":" << r.direct.distinct << ",\"maxDiffDb\":" << num (r.direct.maxDiffDb) << ",\"noiseDb\":" << num (r.direct.noiseDb) << ",\"contexts\":" << r.contextsTried
      << ",\"moved\":[";
    for (size_t i = 0; i < r.direct.moved.size(); ++i)
        s << (i ? "," : "") << jsonStr (r.direct.moved[i]);
    s << "],\"findings\":[";
    for (size_t i = 0; i < r.findings.size(); ++i)
        s << (i ? "," : "") << jsonStr (r.findings[i]);
    s << "],\"grid\":" << curveJson (r.direct, true);
    if (o.density == Density::full && r.direct.points.size() > 12)
        s << ",\"curve\":" << curveJson (r.direct, false);
    s << ",\"mod\":{\"verdict\":" << jsonStr (r.modVerdict) << ",\"context\":" << jsonStr (r.modContext) << ",\"amount\":" << num (r.modAmount) << ",\"values\":" << r.mod.points.size() << ",\"distinct\":" << r.mod.distinct << ",\"moved\":[";
    for (size_t i = 0; i < r.mod.moved.size(); ++i)
        s << (i ? "," : "") << jsonStr (r.mod.moved[i]);
    s << "],\"grid\":" << curveJson (r.mod, true) << "}";
    s << ",\"renders\":" << r.renders << ",\"seconds\":" << num (r.seconds) << "}";
    return s.str();
}
} // namespace

int main (int argc, char** argv)
{
    Options o;
    bool determinism = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--engine")
            o.engine = next() == "own" ? EngineKind::own : EngineKind::vital;
        else if (a == "--quick")
            o.density = Density::quick;
        else if (a == "--full")
            o.density = Density::full;
        else if (a == "--keys")
            o.keysRegex = next();
        else if (a == "--group")
            o.group = next();
        else if (a == "--out")
            o.out = next();
        else if (a == "--keep-wavs")
            o.wavs = next();
        else if (a == "--seed")
            o.seed = static_cast<unsigned> (std::atoi (next().c_str()));
        else if (a == "--list-keys")
            o.list = true;
        else if (a == "--verbose")
            o.verbose = true;
        else if (a == "--shard")
        {
            const auto t = next();
            std::sscanf (t.c_str(), "%d/%d", &o.shard, &o.shards);
        }
        else if (a == "--claims") // a directory shared by the shards: a key is run by the shard that creates its claim directory first (the result of a key does not depend on who runs it)
            o.claims = next();
        else if (a == "--part") // I/N: only every N-th key (from the I-th): the keys of one of N machines (CI matrix); the shards of a machine divide these
        {
            const auto t = next();
            std::sscanf (t.c_str(), "%d/%d", &o.part, &o.parts);
        }
        else if (a == "--all-instances")
            o.allInstances = true;
        else if (a == "--determinism")
            determinism = true;
        else if (a == "--repeat-context") // KEY [INDEX]: see the diagnostic below
        {
            o.repeatKey = next();
            if (i + 1 < argc && argv[i + 1][0] != '-')
                o.repeatContext = std::atoi (argv[++i]);
        }
        else if (a == "--list-contexts")
            o.listContexts = next();
        else
        {
            std::fprintf (stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (o.parts < 1 || o.part < 0 || o.part >= o.parts)
    {
        std::fprintf (stderr, "bad --part\n");
        return 2;
    }
    if (o.shards < 1 || o.shard < 0 || o.shard >= o.shards)
    {
        std::fprintf (stderr, "bad --shard\n");
        return 2;
    }
    if (! isEngineAvailable (o.engine))
    {
        std::fprintf (stderr, "the %s engine is not part of this build\n", engineName (o.engine));
        return 3;
    }
    if (determinism)
    {
        {
            const auto t = std::chrono::steady_clock::now();
            Rig timing (o.engine);
            const auto t1 = std::chrono::steady_clock::now();
            Quality q = qualityForTier (Tier::high);
            q.realtime = false;
            for (int i = 0; i < 5; ++i)
                timing.engine().prepare (48000.0, 256, q);
            const auto t2 = std::chrono::steady_clock::now();
            std::printf ("create + prepare %.1f ms, prepare again %.1f ms\n", std::chrono::duration<double, std::milli> (t1 - t).count(), std::chrono::duration<double, std::milli> (t2 - t1).count() / 5);
        }
        Rig rig (o.engine);
        const auto script = scriptByName ("default");
        uint64_t first = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto a = rig.renderWith ({}, script);
            const auto m = analyse (a, script);
            std::printf ("render %d: hash %016llx peak %.3f dB%s\n", i, static_cast<unsigned long long> (m.hash), m.peakDb, i > 0 && m.hash != first ? "   DIFFERENT" : "");
            if (i == 0)
                first = m.hash;
        }
        // history: the same render after other values of a key
        for (const char* key : { "filter1.cutoff", "master.volume", "osc1.pan", "osc1.transpose", "env1.attack", "osc1.level" })
        {
            const auto* info = findParamByKey (key);
            rig.begin ({});
            const uint64_t ref = analyse (rig.render (script), script).hash;
            for (float v : { info->min, info->max, info->def })
            {
                rig.set (key, v);
                const uint64_t h = analyse (rig.render (script), script).hash;
                std::printf ("%s = %g: %s\n", key, static_cast<double> (v), h == ref ? "same as the first render" : "DIFFERENT from the first render");
            }
        }
        {
            rig.begin ({});
            const auto a0 = rig.render (script);
            const char* keys[] = { "osc1.transpose", "osc1.tune", "master.transpose" };
            for (const char* key : keys)
            {
                const auto* info = findParamByKey (key);
                rig.set (key, info->max);
                rig.render (script);
                rig.set (key, info->def);
                const auto a1 = rig.render (script);
                size_t first = a0.l.size(), count = 0;
                double worst = 0.0;
                for (size_t i = 0; i < a0.l.size(); ++i)
                    if (a0.l[i] != a1.l[i])
                    {
                        first = std::min (first, i);
                        ++count;
                        worst = std::max (worst, std::fabs (static_cast<double> (a0.l[i]) - a1.l[i]));
                    }
                std::printf ("%s: after max then default: %zu differing samples, first at %zu, worst %g\n", key, count, first, worst);
                for (int again = 0; again < 4; ++again)
                {
                    const auto an = rig.render (script);
                    size_t cn = 0;
                    for (size_t i = 0; i < a0.l.size(); ++i)
                        cn += a0.l[i] != an.l[i];
                    std::printf ("    render %d after returning to the default: %zu differing samples\n", again, cn);
                }
                rig.set (key, info->max);
                rig.render (script);
                rig.set (key, info->def);
                rig.engine().reset();
                const auto a2 = rig.render (script);
                size_t c2 = 0;
                for (size_t i = 0; i < a0.l.size(); ++i)
                    c2 += a0.l[i] != a2.l[i];
                std::printf ("    with a second reset(): %zu differing samples\n", c2);
            }
        }
        return 0;
    }
    std::vector<size_t> selected;
    std::regex rx (o.keysRegex.empty() ? ".*" : o.keysRegex);
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const std::string k = paramTable()[i].key;
        if (! o.group.empty() && k.rfind (o.group, 0) != 0)
            continue;
        if (! std::regex_search (k, rx))
            continue;
        if (o.density == Density::quick && ! o.allInstances && followerInstance (k) && ! lastInstance (k))
            continue; // --quick: instance 1 and the last instance of every family (the others are the same code; --full covers them at the quick density)
        selected.push_back (i);
    }
    if (o.parts > 1)
    {
        // the part of a key comes from a hash of its name without the instance number, so that the instances of a family (osc1.x, osc3.x ...) are in the same part and the report's check
        // that they agree still sees them together; the hash (FNV-1a) is the same on every platform
        static const std::regex instanceRx ("^(osc|lfo|rlfo|env|mod|fxi)[0-9]+\\.");
        std::vector<size_t> mine;
        for (size_t i : selected)
        {
            const std::string name = std::regex_replace (std::string (paramTable()[i].key), instanceRx, "$1N.");
            uint64_t h = 1469598103934665603ull;
            for (char c : name)
                h = (h ^ static_cast<unsigned char> (c)) * 1099511628211ull;
            if (static_cast<int> (h % static_cast<uint64_t> (o.parts)) == o.part)
                mine.push_back (i);
        }
        selected = std::move (mine);
    }
    if (o.list)
    {
        Rig rig (o.engine);
        for (size_t i : selected)
        {
            const auto& p = paramTable()[i];
            std::printf ("%s\t%s\t%s\n", p.key, rig.engine().supportsParam (p.id) ? "supported" : "unsupported", capsText (i).c_str());
        }
        return 0;
    }
    if (! o.repeatKey.empty())
    {
        // diagnostic: the first context of a key rendered at the default several times in a row, then after every value of the quick sweep once, then at the default again (peak of the
        // difference to the first render). The engine's output depends on the number of renders since the last parameter change (the first renders after a change differ from the later ones),
        // which is why the sweeps of this tool warm up and why a shorter procedure gives other numbers (docs/QUALITY.md).
        const auto* info = findParamByKey (o.repeatKey.c_str());
        if (info == nullptr)
            return 2;
        Rig rig (o.engine);
        const auto ctxs = contextsFor (*info);
        if (ctxs.empty())
            return 2;
        const auto& ctx = ctxs[static_cast<size_t> (std::min<int> (std::max (0, o.repeatContext), static_cast<int> (ctxs.size()) - 1))];
        const auto script = scriptByName (ctx.script);
        rig.begin (ctx.ov);
        rig.set (info->key, info->def);
        const Audio first = rig.render (script);
        std::printf ("context %s [%s]\n", ctx.name.c_str(), ctx.script.c_str());
        for (int i = 0; i < 8; ++i)
            std::printf ("  default again %d: %.1f dB\n", i, diffDbOf (rig.render (script), first));
        for (float v : sweepValues (*info, Density::quick))
        {
            rig.set (info->key, v);
            rig.render (script);
            rig.set (info->key, info->def);
            std::printf ("  after value %g: default differs by %.1f dB\n", static_cast<double> (v), diffDbOf (rig.render (script), first));
        }
        return 0;
    }
    if (! o.listContexts.empty())
    {
        const auto* info = findParamByKey (o.listContexts.c_str());
        if (info == nullptr)
            return 2;
        for (const auto& c : contextsFor (*info))
        {
            std::printf ("%s [%s]:", c.name.c_str(), c.script.c_str());
            for (const auto& ov : c.ov)
                std::printf (" %s=%g", ov.key.c_str(), static_cast<double> (ov.value));
            std::printf ("\n");
        }
        return 0;
    }
    std::filesystem::create_directories (o.out);
    if (! o.claims.empty())
        std::filesystem::create_directories (o.claims);
    const std::string file = o.out + "/keys" + (o.shards > 1 ? "-" + std::to_string (o.shard) : std::string()) + ".jsonl";
    std::ofstream out (file);
    const auto t0 = std::chrono::steady_clock::now();
    size_t done = 0;
    for (size_t n = 0; n < selected.size(); ++n)
    {
        if (o.claims.empty())
        {
            if (static_cast<int> (n % static_cast<size_t> (o.shards)) != o.shard)
                continue;
        }
        else
        {
            std::error_code ec;
            if (! std::filesystem::create_directory (o.claims + "/" + std::to_string (n), ec) || ec)
                continue;
        }
        const size_t index = selected[n];
        Rig rig (o.engine); // a fresh engine per key: the result of a key never depends on the keys before it (or on the shard it ran in)
        if (! rig.ok())
            return 3;
        const KeyResult r = auditKey (rig, index, o);
        out << toJson (paramTable()[index], r, o) << "\n";
        out.flush();
        ++done;
        if (o.verbose || done % 50 == 0)
            std::fprintf (stderr, "[%d/%d] %zu/%zu %s %s (%s) %.1fs\n", o.shard, o.shards, done, selected.size() / static_cast<size_t> (o.shards), paramTable()[index].key, r.verdict.c_str(), r.context.c_str(), r.seconds);
    }
    std::fprintf (stderr, "shard %d/%d: %zu keys in %.1f s -> %s\n", o.shard, o.shards, done, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), file.c_str());
    return 0;
}
