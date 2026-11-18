// SynthyBoy - own engine tests: the CPU optimisations of the voice control and the modulation matrix (docs/release/CPU-REVIEW-VOICE-MOD.md, CPU-REVIEW-PROFILE.md) change no output
// sample and no display value. The code they replaced stays selectable (setOwnSavingsOff (kOwnSavingVoiceControl), TickGlobals::referenceControl, EvalContext::reference); two engines /
// two voices run in step on random patches, one with the old code, one as shipped, and after every block / control period their outputs, their display readouts and the state of the
// voices are compared bit for bit. The patches: random values of every voice key (silence, denormal values, signed zeros, the ends of the ranges), random modulation slots
// (macros, wheels and pressure that are 0, envelopes, LFOs of every shape, random LFOs, aux sources, curves of every power, slew, delays, stereo, bipolar), parameter and tempo
// changes between blocks, note restarts, steals, legato notes and releases; and the factory presets.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "osc/wavetable_bank.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/readout.h"
#include "voice/param_index.h"
#include "voice/voice.h"
#include "test_runner.h"

namespace synthyboy
{
namespace own
{
/** Reads the state of a voice (friend of Voice): 0 when two voices are the same in everything their future output and their display depend on, else the number of the first field that differs. */
struct VoiceOptTest
{
    static bool same (const void* a, const void* b, size_t n) { return std::memcmp (a, b, n) == 0; }
    static int diff (const Voice& a, const Voice& b)
    {
        if (a.active != b.active || a.released != b.released) return 1;
        if (!a.active)
            return 0;
        if (!same (a.src, b.src, sizeof a.src)) return 2;
        if (!same (a.srcR, b.srcR, sizeof a.srcR)) return 3;
        if (!same (a.envOut, b.envOut, sizeof a.envOut)) return 4;
        for (int e = 0; e < 6; ++e)
        {
            const Envelope& x = a.env[e];
            const Envelope& y = b.env[e];
            const double px = x.stageSeconds(), py = y.stageSeconds();
            const float vx = x.current(), vy = y.current();
            if (x.fineStage() != y.fineStage() || x.held() != y.held() || !same (&px, &py, sizeof px) || !same (&vx, &vy, sizeof vx)) return 5;
            const float fx = x.fineFraction (1.0f), fy = y.fineFraction (1.0f);
            if (!same (&fx, &fy, sizeof fx)) return 6;
        }
        if (!same (a.envLen, b.envLen, sizeof a.envLen)) return 7;
        if (!same (a.legatoInv, b.legatoInv, sizeof a.legatoInv)) return 8;
        if (!same (a.lfoOut, b.lfoOut, sizeof a.lfoOut) || !same (a.lfoOutR, b.lfoOutR, sizeof a.lfoOutR)) return 9;
        if (!same (a.lfoSmoothL, b.lfoSmoothL, sizeof a.lfoSmoothL) || !same (a.lfoSmoothR, b.lfoSmoothR, sizeof a.lfoSmoothR)) return 10;
        if (!same (a.lfoAge, b.lfoAge, sizeof a.lfoAge) || !same (a.lfoReadPos, b.lfoReadPos, sizeof a.lfoReadPos)) return 11;
        for (int l = 0; l < 10; ++l)
            if (!same (&a.lfoCore[l].offset, &b.lfoCore[l].offset, 4) || !same (&a.lfoCore[l].age, &b.lfoCore[l].age, 4)) return 12;
        if (!same (a.rlfoOut, b.rlfoOut, sizeof a.rlfoOut) || !same (a.rlfoOutR, b.rlfoOutR, sizeof a.rlfoOutR)) return 13;
        if (a.mod.size() != b.mod.size() || !same (a.mod.data(), b.mod.data(), a.mod.size() * 4)) return 14;
        if (!same (a.modR.data(), b.modR.data(), a.modR.size() * 4)) return 15;
        if (a.padCutTouched != b.padCutTouched) return 16;
        for (int o = 0; o < 3; ++o)
            if (a.oscOn[o] != b.oscOn[o] || a.oscWarpOnly[o] != b.oscWarpOnly[o] || (a.oscOn[o] && !same (&a.oscTick[o], &b.oscTick[o], sizeof (OscTick)))) return 17;
        if (a.subCh.on != b.subCh.on || (a.subCh.on && !same (&a.subTick, &b.subTick, sizeof (OscTick)))) return 18;
        if (!same (a.pitchTap, b.pitchTap, sizeof a.pitchTap) || !same (a.pitchTapOn, b.pitchTapOn, sizeof a.pitchTapOn)) return 19;
        for (int f = 0; f < 2; ++f)
            if (a.filtOn[f] != b.filtOn[f] || (a.filtOn[f] && !same (&a.filtSet[f], &b.filtSet[f], sizeof (FilterSettings)))) return 20;
        if (!same (a.filtLevel, b.filtLevel, sizeof a.filtLevel) || !same (a.filtLevelPrev, b.filtLevelPrev, sizeof a.filtLevelPrev)) return 21;
        if (!same (&a.ampCur, &b.ampCur, 4) || !same (&a.ampPrev, &b.ampPrev, 4) || !same (&a.velGain, &b.velGain, 4)) return 22;
        if (!same (&a.noiseGain, &b.noiseGain, 4) || a.noiseCh.on != b.noiseCh.on) return 23;
        if (!same (&a.vc, &b.vc, sizeof a.vc)) return 24;
        if (!same (a.extMono, b.extMono, sizeof a.extMono) || !same (a.oscMono, b.oscMono, sizeof a.oscMono)) return 25;
        if (!same (a.srcLevel, b.srcLevel, sizeof a.srcLevel)) return 26;
        if (!same (a.lastBusL, b.lastBusL, sizeof a.lastBusL) || !same (a.lastBusR, b.lastBusR, sizeof a.lastBusR)) return 27;
        return 0;
    }
};
} // namespace own
} // namespace synthyboy

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
constexpr double kSr = 44100.0;

/** xorshift: the patches of a seed are the same on every run. */
struct Rand
{
    uint32_t s;
    explicit Rand (uint32_t seed) : s (seed * 2654435761u + 12345u) { if (s == 0) s = 1; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    int range (int lo, int hi) { return lo + static_cast<int> (next() % static_cast<uint32_t> (hi - lo + 1)); }
    bool chance (float p) { return uni() < p; }
};

bool startsWith (const char* s, const char* p) { return std::strncmp (s, p, std::strlen (p)) == 0; }

/** The keys of the voice (and of the master keys a voice reads) that the random patches move. */
bool voiceKey (const char* k)
{
    static const char* const prefixes[] = { "osc1.", "osc2.", "osc3.", "sub.", "noise.", "filter1.", "filter2.", "filter.routing", "env", "lfo", "rlfo", "voicectl.", "macro.",
                                            "master.portamento", "master.velocity", "master.transpose", "master.tune", "master.stereo_spread", "master.legato", "curve." };
    for (const char* p : prefixes)
        if (startsWith (k, p))
            return true;
    return false;
}

/** A random plain value of parameter i: often the default or an end of the range, sometimes 0, -0 or a denormal. */
float randomValue (Rand& r, const ParamInfo& p)
{
    const bool whole = p.numChoices > 0 || p.interval >= 1.0f;
    if (whole)
    {
        const int lo = static_cast<int> (std::ceil (p.min)), hi = static_cast<int> (std::floor (p.max));
        return r.chance (0.3f) ? p.def : static_cast<float> (r.range (lo, std::max (lo, hi)));
    }
    const float u = r.uni();
    if (u < 0.2f) return p.def;
    if (u < 0.3f) return p.min;
    if (u < 0.4f) return p.max;
    if (u < 0.45f && p.min <= 0.0f && p.max >= 0.0f) return 0.0f;
    if (u < 0.5f && p.min <= 0.0f && p.max >= 0.0f) return -0.0f;
    if (u < 0.55f && p.min <= 0.0f && p.max >= 1.0e-38f) return 1.0e-40f;   // a denormal
    if (u < 0.6f && p.min <= -1.0e-38f && p.max >= 0.0f) return -3.0e-39f;
    return p.min + (p.max - p.min) * r.uni();
}

int idxOf (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? findParamIndex (p->id) : -1;
}

/** Plain values of a random patch (indexed like paramTable ()). `fromPreset` != null: start from that preset instead of the defaults. */
std::vector<float> randomPatch (Rand& r, const std::vector<float>* fromPreset, float moveShare)
{
    const ParamInfo* table = paramTable();
    std::vector<float> v (paramCount());
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = fromPreset != nullptr ? (*fromPreset)[i] : table[i].def;
    for (size_t i = 0; i < v.size(); ++i)
        if (voiceKey (table[i].key) && !startsWith (table[i].key, "mod") && r.chance (moveShare))
            v[i] = randomValue (r, table[i]);
    // a few oscillators, filters on (else the patch is mostly silence)
    for (const char* k : { "osc1.on", "filter1.on" })
        if (r.chance (0.8f))
            v[static_cast<size_t> (idxOf (k))] = 1.0f;
    // the sample oscillator types need files; keep most oscillators wavetables (a missing file plays the wavetable, which the patches also test)
    for (const char* k : { "osc1.type", "osc2.type", "osc3.type" })
        if (r.chance (0.7f))
            v[static_cast<size_t> (idxOf (k))] = 0.0f;
    v[static_cast<size_t> (idxOf ("master.polyphony"))] = static_cast<float> (r.range (1, 8));
    // modulation slots
    const int slots = fromPreset != nullptr ? r.range (0, 6) : r.range (0, 16);
    const size_t dests = modDestinationCount();
    for (int n = 0; n < slots; ++n)
    {
        const int s = r.range (0, kNumModSlots - 1);
        const ModSlotParams& sp = paramIndex().slot[s];
        if (sp.source < 0 || sp.destination < 0 || sp.amount < 0)
            continue;
        static const int kSources[] = { kSrcEnv1, kSrcEnv1 + 1, kSrcEnv1 + 3, kSrcLfo1, kSrcLfo1 + 1, kSrcLfo1 + 4, kSrcLfo9, kSrcRlfo1, kSrcRlfo1 + 2, kSrcMacro1, kSrcMacro1 + 2,
                                        kSrcMacro5, kSrcVelocity, kSrcNote, kSrcAftertouch, kSrcPitchWheel, kSrcModWheel, kSrcRandom, kSrcLift, kSrcSlide, kSrcOctNote, kSrcPolyAftertouch,
                                        kSrcStereo, kSrcVoiceIndex, kSrcVoiceMod1, kSrcOscA, kSrcSub, kSrcFilter1, kSrcActiveVoices, kSrcNoteOnAlt1, kSrcNoteOnRand2, kSrcReleaseVelocity,
                                        kSrcExpressionX, kSrcFixed };
        v[static_cast<size_t> (sp.source)] = static_cast<float> (kSources[r.range (0, static_cast<int> (sizeof kSources / sizeof kSources[0]) - 1)]);
        // destinations: mostly the keys of the voice (envelopes and LFOs among them), some global ones and other slots' amounts
        size_t d = static_cast<size_t> (r.range (1, static_cast<int> (dests)));
        if (r.chance (0.6f))
        {
            static const char* const kDests[] = { "env1.attack", "env1.decay", "env1.sustain", "env1.release", "env2.attack", "env2.decay_curve", "env3.hold", "env1.attack_curve",
                                                  "lfo1.rate", "lfo1.smooth_time", "lfo2.phase", "lfo3.rate", "lfo1.fade_in", "rlfo1.rate", "osc1.level", "osc1.tune", "osc1.frame",
                                                  "osc2.pan", "osc1.unison.detune", "sub.level", "sub.tune", "filter1.cutoff", "filter1.resonance", "filter2.cutoff", "noise.level",
                                                  "filter1.mix", "osc3.tune", "mod1.amount", "mod2.amount", "master.volume" };
            const int di = idxOf (kDests[r.range (0, static_cast<int> (sizeof kDests / sizeof kDests[0]) - 1)]);
            if (di >= 0 && modDestinationChoiceOf (static_cast<size_t> (di)) > 0)
                d = modDestinationChoiceOf (static_cast<size_t> (di));
        }
        v[static_cast<size_t> (sp.destination)] = static_cast<float> (d);
        const float u = r.uni();
        v[static_cast<size_t> (sp.amount)] = u < 0.1f ? 0.0f : (u < 0.15f ? -0.0f : (u < 0.2f ? 1.0e-40f : -100.0f + 200.0f * r.uni()));
        if (sp.power >= 0) v[static_cast<size_t> (sp.power)] = r.chance (0.6f) ? 0.0f : (r.chance (0.1f) ? 5.0e-5f : -10.0f + 20.0f * r.uni());
        if (sp.bipolar >= 0) v[static_cast<size_t> (sp.bipolar)] = r.chance (0.3f) ? 1.0f : 0.0f;
        if (sp.stereo >= 0) v[static_cast<size_t> (sp.stereo)] = r.chance (0.2f) ? 1.0f : 0.0f;
        if (sp.bypass >= 0) v[static_cast<size_t> (sp.bypass)] = r.chance (0.05f) ? 1.0f : 0.0f;
        if (sp.auxSource >= 0) v[static_cast<size_t> (sp.auxSource)] = r.chance (0.3f) ? static_cast<float> (kSources[r.range (0, 20)]) : 0.0f;
        if (sp.auxInv >= 0) v[static_cast<size_t> (sp.auxInv)] = r.chance (0.3f) ? 1.0f : 0.0f;
        if (sp.auxPower >= 0) v[static_cast<size_t> (sp.auxPower)] = r.chance (0.6f) ? 0.0f : -10.0f + 20.0f * r.uni();
        if (sp.auxOutput >= 0) v[static_cast<size_t> (sp.auxOutput)] = r.chance (0.5f) ? 100.0f : 100.0f * r.uni();
        if (sp.rise >= 0) v[static_cast<size_t> (sp.rise)] = r.chance (0.8f) ? 0.0f : 200.0f * r.uni();
        if (sp.fall >= 0) v[static_cast<size_t> (sp.fall)] = r.chance (0.8f) ? 0.0f : 200.0f * r.uni();
        if (sp.delay >= 0) v[static_cast<size_t> (sp.delay)] = r.chance (0.85f) ? 0.0f : 100.0f * r.uni();
        if (sp.delaySync >= 0) v[static_cast<size_t> (sp.delaySync)] = r.chance (0.1f) ? 1.0f : 0.0f;
        if (sp.delayBeats >= 0) v[static_cast<size_t> (sp.delayBeats)] = r.chance (0.5f) ? 0.0f : 0.25f;
    }
    return v;
}

std::vector<std::vector<float>> factoryPresets()
{
    std::vector<std::vector<float>> out;
    const std::string dir = std::string (SYNTHYBOY_TEST_SOURCE_DIR) + "/content/presets";
    for (const PresetEntry& e : scanPresetDirectory (dir))
    {
        std::ifstream in (e.path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        PresetMeta meta;
        ParamValues values = ParamValues::defaults();
        StateContent content;
        const LoadResult r = loadPreset (text.data(), text.size(), meta, values, content);
        if (r.status != LoadStatus::ok && r.status != LoadStatus::newer_version)
            continue;
        out.emplace_back (values.plain, values.plain + paramCount());
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Engine level: the output and the whole display readout, block by block

struct EnginePair
{
    std::unique_ptr<IEngine> opt, ref;
    std::vector<int> targets;

    EnginePair (const std::vector<float>& plain, Tier tier, Rand& r)
    {
        opt = createOwnEngine();
        ref = createOwnEngine();
        CHECK (setOwnSavingsOff (*ref, kOwnSavingVoiceControl));
        Quality q = qualityForTier (tier);
        q.realtime = false;
        for (IEngine* e : { opt.get(), ref.get() })
        {
            e->prepare (kSr, 128, q);
            setAll (*e, plain);
        }
        // the display subscribes knobs: the modulated value of each (the newest voice's P (), or the idle evaluation)
        const ParamInfo* table = paramTable();
        for (size_t i = 0; i < paramCount() && static_cast<int> (targets.size()) < LiveValues::kMaxTargets; ++i)
            if (voiceKey (table[i].key) && r.chance (0.15f))
                targets.push_back (static_cast<int> (i));
        opt->setReadoutTargets (targets.data(), static_cast<int> (targets.size()));
        ref->setReadoutTargets (targets.data(), static_cast<int> (targets.size()));
    }
    static void setAll (IEngine& e, const std::vector<float>& plain)
    {
        const ParamInfo* table = paramTable();
        for (size_t i = 0; i < paramCount(); ++i)
            e.setParam (table[i].id, plain[i]);
    }
};

/** Plays `blocks` blocks of random events, parameter moves and tempo changes into both engines; false at the first difference (printed). */
bool playAndCompare (EnginePair& p, Rand& r, int blocks, const char* what)
{
    std::vector<float> lo (128), ro (128), lr (128), rr (128);
    std::vector<int> held;
    const ParamInfo* table = paramTable();
    TransportInfo tr;
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<Event> ev;
        const int n = r.chance (0.1f) ? r.range (1, 128) : 128;
        auto at = [&] () { return r.range (0, n - 1); };
        if (r.chance (0.12f) || (b == 0))
        {
            Event e;
            e.type = Event::NoteOn;
            e.frame = at();
            e.key = static_cast<int16_t> (r.range (30, 100));
            e.value = r.chance (0.1f) ? 1.0e-3f : r.uni();
            e.channel = static_cast<int16_t> (r.chance (0.8f) ? 0 : r.range (1, 3));
            ev.push_back (e);
            held.push_back (e.key);
        }
        if (!held.empty() && r.chance (0.1f))
        {
            const size_t k = static_cast<size_t> (r.range (0, static_cast<int> (held.size()) - 1));
            Event e;
            e.type = Event::NoteOff;
            e.frame = at();
            e.key = static_cast<int16_t> (held[k]);
            e.value = r.uni();
            ev.push_back (e);
            held.erase (held.begin() + static_cast<long> (k));
        }
        if (r.chance (0.05f))
        {
            Event e;
            e.type = r.chance (0.5f) ? Event::ModWheel : (r.chance (0.5f) ? Event::Aftertouch : Event::PitchBend);
            e.frame = at();
            e.value = e.type == Event::PitchBend ? -1.0f + 2.0f * r.uni() : (r.chance (0.3f) ? 0.0f : r.uni());
            ev.push_back (e);
        }
        if (r.chance (0.03f))
        {
            Event e;
            e.type = Event::NoteExpression;
            e.expression = r.chance (0.5f) ? Event::Pressure : Event::Brightness;
            e.frame = at();
            e.key = -1;
            e.value = r.uni();
            ev.push_back (e);
        }
        if (r.chance (0.01f))
        {
            Event e;
            e.type = Event::AllNotesOff;
            e.channel = -1;
            e.frame = at();
            ev.push_back (e);
            held.clear();
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const Event& x, const Event& y) { return x.frame < y.frame; });
        // parameters between blocks (the snapshot epoch moves): a voice key, a macro, a slot amount; the tempo
        if (r.chance (0.06f))
        {
            const size_t i = static_cast<size_t> (r.range (0, static_cast<int> (paramCount()) - 1));
            if (voiceKey (table[i].key) || startsWith (table[i].key, "mod"))
            {
                const float v = randomValue (r, table[i]);
                p.opt->setParam (table[i].id, v);
                p.ref->setParam (table[i].id, v);
            }
        }
        if (r.chance (0.04f))
        {
            const int m = r.range (1, 8);
            char key[16];
            std::snprintf (key, sizeof key, "macro.%d", m);
            if (const ParamInfo* mp = findParamByKey (key))
            {
                const float v = r.chance (0.3f) ? 0.0f : 100.0f * r.uni();
                p.opt->setParam (mp->id, v);
                p.ref->setParam (mp->id, v);
            }
        }
        if (r.chance (0.02f))
        {
            tr.bpm = 60.0 + 120.0 * r.uni();
            tr.playing = r.chance (0.5f);
        }
        tr.songPosBeats += tr.playing ? 128.0 / kSr * tr.bpm / 60.0 : 0.0;
        p.opt->setTransport (tr);
        p.ref->setTransport (tr);
        p.opt->process (ev.data(), static_cast<int> (ev.size()), lo.data(), ro.data(), n);
        p.ref->process (ev.data(), static_cast<int> (ev.size()), lr.data(), rr.data(), n);
        if (std::memcmp (lo.data(), lr.data(), static_cast<size_t> (n) * 4) != 0 || std::memcmp (ro.data(), rr.data(), static_cast<size_t> (n) * 4) != 0)
        {
            for (int k = 0; k < n; ++k)
                if (std::memcmp (&lo[static_cast<size_t> (k)], &lr[static_cast<size_t> (k)], 4) != 0 || std::memcmp (&ro[static_cast<size_t> (k)], &rr[static_cast<size_t> (k)], 4) != 0)
                {
                    std::printf ("%s: block %d sample %d differs: %.9g / %.9g against %.9g / %.9g\n", what, b, k, static_cast<double> (lo[static_cast<size_t> (k)]),
                                 static_cast<double> (ro[static_cast<size_t> (k)]), static_cast<double> (lr[static_cast<size_t> (k)]), static_cast<double> (rr[static_cast<size_t> (k)]));
                    break;
                }
            return false;
        }
        LiveValues a, c;
        p.opt->copyReadout (a);
        p.ref->copyReadout (c);
        if (std::memcmp (&a, &c, sizeof a) != 0)
        {
            const auto* x = reinterpret_cast<const uint8_t*> (&a);
            const auto* y = reinterpret_cast<const uint8_t*> (&c);
            size_t off = 0;
            while (off < sizeof a && x[off] == y[off])
                ++off;
            std::printf ("%s: block %d: the display readout differs at byte %zu of %zu\n", what, b, off, sizeof a);
            return false;
        }
    }
    return true;
}
} // namespace

TEST (voice_opt_engine_random_patches_are_bit_identical)
{
    ensureAllBuiltinWavetables();
    const int patches = sbtest::instrumented() ? 12 : 80;
    int failed = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int s = 0; s < patches; ++s)
    {
        Rand r (static_cast<uint32_t> (1000 + s));
        const std::vector<float> plain = randomPatch (r, nullptr, 0.25f);
        const Tier tier = s % 3 == 0 ? Tier::high : Tier::low;
        EnginePair p (plain, tier, r);
        char what[64];
        std::snprintf (what, sizeof what, "random patch %d (%s)", s, tier == Tier::low ? "low" : "high");
        if (!playAndCompare (p, r, 600, what))
            ++failed;
    }
    std::printf ("%d random patches, %d differ, %.1f s\n", patches, failed, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    CHECK_EQ (failed, 0);
}

TEST (voice_opt_engine_factory_presets_are_bit_identical)
{
    ensureAllBuiltinWavetables();
    const std::vector<std::vector<float>> presets = factoryPresets();
    CHECK (presets.size() >= 100);
    int failed = 0, runs = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t step = sbtest::instrumented() ? 8 : 1;
    for (size_t i = 0; i < presets.size(); i += step)
    {
        Rand r (static_cast<uint32_t> (77 + i));
        // as stored, and with a few keys moved and slots added (a user's edit of it)
        const std::vector<float> plain = (i % 2 == 0) ? presets[i] : randomPatch (r, &presets[i], 0.03f);
        EnginePair p (plain, i % 4 == 1 ? Tier::high : Tier::low, r);
        char what[64];
        std::snprintf (what, sizeof what, "factory preset %zu%s", i, i % 2 == 0 ? "" : " (edited)");
        ++runs;
        if (!playAndCompare (p, r, 300, what))
            ++failed;
    }
    std::printf ("%d factory presets, %d differ, %.1f s\n", runs, failed, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    CHECK_EQ (failed, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Voice level: the whole state of two voices after every period (the old code against the new one), with the voice driven directly

namespace
{
struct VoiceRig
{
    Voice v;
    TickGlobals g;
    float mixL[4][kControlPeriod] = {}, mixR[4][kControlPeriod] = {};
    float srcSq[7] = {};
    void render (int n, int t0)
    {
        MixOut mo;
        for (int t = 0; t < 4; ++t)
        {
            mo.l[t] = mixL[t];
            mo.r[t] = mixR[t];
            std::fill (mixL[t], mixL[t] + kControlPeriod, 0.0f);
            std::fill (mixR[t], mixR[t] + kControlPeriod, 0.0f);
        }
        std::fill (std::begin (srcSq), std::end (srcSq), 0.0f);
        mo.srcSq = srcSq;
        v.render (mo, n, t0);
    }
};
} // namespace

TEST (voice_opt_voice_state_is_bit_identical)
{
    ensureAllBuiltinWavetables();
    const int patches = sbtest::instrumented() ? 10 : 120;
    int failed = 0;
    float bends[16] = {}, wheel[16] = {}, after[16] = {}, tuned[128];
    for (int k = 0; k < 128; ++k)
        tuned[k] = static_cast<float> (k);
    float lfoPhase[10] = {}, rlfoValue[4] = {};
    const auto t0 = std::chrono::steady_clock::now();
    for (int s = 0; s < patches && failed == 0; ++s)
    {
        Rand r (static_cast<uint32_t> (5000 + s));
        std::vector<float> base = randomPatch (r, nullptr, 0.3f);
        base.resize (kMaxParams, 0.0f);
        ModMatrix matrix;
        matrix.compile (base.data());
        VoiceRig a, b;
        a.v.prepare (kSr);
        b.v.prepare (kSr);
        uint32_t epoch = 1;
        for (VoiceRig* x : { &a, &b })
        {
            TickGlobals& g = x->g;
            g.base = base.data();
            g.matrix = &matrix;
            g.sampleRate = kSr;
            g.bpm = 120.0;
            g.channelBendSemis = bends;
            g.channelModWheel = wheel;
            g.channelAftertouch = after;
            g.tunedNotes = tuned;
            g.lfoPhase = lfoPhase;
            g.rlfoValue = rlfoValue;
            g.economy = (s % 2 == 0) ? kEconomyAll : 0u;
            g.usedSources = matrix.sourceMask();
            g.baseEpoch = epoch;
            g.activeVoices = 1;
        }
        b.g.referenceControl = true;
        uint32_t serial = 1, sequence = 1;
        bool started = false;
        for (int t = 0; t < 1500 && failed == 0; ++t)
        {
            // events between periods
            if (!started || r.chance (0.01f))
            {
                NoteStart n;
                n.key = r.range (30, 100);
                n.tunedNote = static_cast<float> (n.key) + (r.chance (0.2f) ? r.uni() - 0.5f : 0.0f);
                n.velocity = r.chance (0.1f) ? 0.0f : r.uni();
                n.serial = serial++;
                n.sequence = sequence++;
                n.glide = r.chance (0.3f);
                n.glideFromSemis = static_cast<float> (r.range (30, 100));
                const float lastL = r.chance (0.5f) ? 0.0f : r.uni() - 0.5f;
                if (started && a.v.isActive() && r.chance (0.4f))
                {
                    a.v.legatoTo (n);
                    b.v.legatoTo (n);
                }
                else
                {
                    a.v.start (n, lastL, -lastL);
                    b.v.start (n, lastL, -lastL);
                }
                started = true;
            }
            if (r.chance (0.006f))
            {
                const float rv = r.uni();
                a.v.release (rv);
                b.v.release (rv);
            }
            if (r.chance (0.002f))
            {
                a.v.startKill();
                b.v.startKill();
            }
            if (r.chance (0.02f))
            {
                const float pr = r.chance (0.3f) ? 0.0f : r.uni();
                a.v.setPressure (pr);
                b.v.setPressure (pr);
                const float sl = r.uni();
                a.v.setSlide (sl);
                b.v.setSlide (sl);
            }
            if (r.chance (0.02f))
            {
                const int m = r.range (0, 7);
                a.g.macro[m] = b.g.macro[m] = r.chance (0.3f) ? 0.0f : r.uni();
                wheel[0] = r.chance (0.5f) ? 0.0f : r.uni();
                after[0] = r.chance (0.5f) ? 0.0f : r.uni();
                bends[0] = r.chance (0.5f) ? 0.0f : -2.0f + 4.0f * r.uni();
            }
            if (r.chance (0.01f))
            {
                // a parameter change: the snapshot is rebuilt (a new epoch) and the matrix compiled again
                const ParamInfo* table = paramTable();
                for (int k = 0; k < 3; ++k)
                {
                    const size_t i = static_cast<size_t> (r.range (0, static_cast<int> (paramCount()) - 1));
                    if (voiceKey (table[i].key) || startsWith (table[i].key, "mod"))
                        base[i] = randomValue (r, table[i]);
                }
                matrix.compile (base.data());
                ++epoch;
                a.g.baseEpoch = b.g.baseEpoch = epoch;
                a.g.usedSources = b.g.usedSources = matrix.sourceMask();
            }
            if (r.chance (0.005f))
                a.g.bpm = b.g.bpm = 60.0 + 120.0 * r.uni();
            for (int l = 0; l < 10; ++l)
                lfoPhase[l] = std::fmod (lfoPhase[l] + 0.01f * static_cast<float> (l + 1), 1.0f);
            for (int q = 0; q < 4; ++q)
                rlfoValue[q] = -1.0f + 2.0f * r.uni();
            if (!a.v.isActive())
            {
                started = false;
                continue;
            }
            a.v.tick (a.g, 0);
            b.v.tick (b.g, 0);
            int d = VoiceOptTest::diff (a.v, b.v);
            if (d == 0 && a.v.isActive())
            {
                a.render (kControlPeriod, 0);
                b.render (kControlPeriod, 0);
                if (std::memcmp (a.mixL, b.mixL, sizeof a.mixL) != 0 || std::memcmp (a.mixR, b.mixR, sizeof a.mixR) != 0)
                    d = 100;
                else if (std::memcmp (a.srcSq, b.srcSq, sizeof a.srcSq) != 0)
                    d = 101;
                else
                    d = VoiceOptTest::diff (a.v, b.v);
            }
            if (d != 0)
            {
                std::printf ("voice patch %d period %d: field %d differs\n", s, t, d);
                ++failed;
            }
        }
    }
    std::printf ("%d voice patches, %d differ, %.1f s\n", patches, failed, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    CHECK_EQ (failed, 0);
}
