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
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/readout.h"
#include "synthyboy/user_content.h"
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
    /** The sources and the output of the LFOs in `ignoreLfo` (bit l = LFO l + 1) are not compared: nothing reads them (no slot), only a display, and a voice that is not the newest does not make them. */
    static bool sameSources (const float* x, const float* y, uint32_t ignoreLfo)
    {
        for (int i = 0; i < kNumSourceIds; ++i)
        {
            bool skip = false;
            for (int l = 0; l < 10; ++l)
                skip = skip || (((ignoreLfo >> l) & 1u) != 0 && srcLfo (l) == i);
            if (!skip && std::memcmp (x + i, y + i, sizeof (float)) != 0)
                return false;
        }
        return true;
    }
    static int diff (const Voice& a, const Voice& b, uint32_t ignoreLfo = 0)
    {
        if (a.active != b.active || a.released != b.released) return 1;
        if (!a.active)
            return 0;
        if (!sameSources (a.src, b.src, ignoreLfo)) return 2;
        if (!sameSources (a.srcR, b.srcR, ignoreLfo)) return 3;
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
        for (int l = 0; l < 10; ++l)
        {
            if (((ignoreLfo >> l) & 1u) != 0)
                continue;
            if (!same (a.lfoOut + l, b.lfoOut + l, 4) || !same (a.lfoOutR + l, b.lfoOutR + l, 4)) return 9;
            if (!same (a.lfoSmoothL + l, b.lfoSmoothL + l, 4) || !same (a.lfoSmoothR + l, b.lfoSmoothR + l, 4)) return 10;
        }
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
        // field by field: VoiceCtl ends in bool mask[5] and 3 bytes of padding that nobody writes (a memcmp of the whole struct compared heap garbage and failed with 3 bytes 0 vs 53, 44, 34)
        if (!same (&a.vc.pan, &b.vc.pan, 4) || !same (&a.vc.detuneSemis, &b.vc.detuneSemis, 4) || !same (&a.vc.cutoffSemis, &b.vc.cutoffSemis, 4) || !same (&a.vc.envFactor, &b.vc.envFactor, 4)
            || !same (&a.vc.lfoFactor, &b.vc.lfoFactor, 4) || !same (&a.vc.mod1, &b.vc.mod1, 4) || !same (&a.vc.mod2, &b.vc.mod2, 4) || !same (&a.vc.index, &b.vc.index, 4)
            || !same (a.vc.mask, b.vc.mask, sizeof a.vc.mask))
            return 24;
        if (!same (a.extMono, b.extMono, sizeof a.extMono) || !same (a.oscMono, b.oscMono, sizeof a.oscMono)) return 25;
        if (!same (a.srcLevel, b.srcLevel, sizeof a.srcLevel)) return 26;
        if (!same (a.lastBusL, b.lastBusL, sizeof a.lastBusL) || !same (a.lastBusR, b.lastBusR, sizeof a.lastBusR)) return 27;
        return 0;
    }
    /** 0 when two parts are the same field for field (the library keys only where both hold them), else the number of the first field that differs. */
    static int partDiff (const Voice::OscPart& x, const Voice::OscPart& y)
    {
        int n = 0;
#define SB_SAME(f) do { ++n; if (std::memcmp (&x.f, &y.f, sizeof x.f) != 0) return n; } while (0)
        SB_SAME (dest); SB_SAME (wtChoice); SB_SAME (pmode); SB_SAME (ratioSrc); SB_SAME (transposeSnap); SB_SAME (voicesRaw); SB_SAME (stack); SB_SAME (stackRepeat); SB_SAME (mode);
        SB_SAME (law); SB_SAME (morphType); SB_SAME (remapSlot); SB_SAME (warp); SB_SAME (blend); SB_SAME (send); SB_SAME (fixedNote); SB_SAME (coarseSemis); SB_SAME (transpose);
        SB_SAME (tune001); SB_SAME (frame); SB_SAME (frameSpread); SB_SAME (level); SB_SAME (pan); SB_SAME (panRight); SB_SAME (detuneSemis); SB_SAME (curve); SB_SAME (blendShape);
        SB_SAME (spread); SB_SAME (uniBlend); SB_SAME (morphAmount); SB_SAME (morphSpread); SB_SAME (remapRise); SB_SAME (remapFall); SB_SAME (warpAmount); SB_SAME (warpPhase);
        SB_SAME (warpSpread); SB_SAME (env1); SB_SAME (track); SB_SAME (bendTrack); SB_SAME (smooth); SB_SAME (remapOn);
#undef SB_SAME
        if (x.libValid && y.libValid && std::memcmp (x.libV, y.libV, sizeof x.libV) != 0)
            return 100;
        return 0;
    }
    /** What `v` kept of oscillator o against the whole part read again from every key now (what the reference reads every period): 0 when the same. */
    static int oscPartVsWhole (Voice& v, int o)
    {
        Voice::OscPart whole;
        v.fillOscPart (o, whole, kOfAll);
        return partDiff (v.oscPart[o], whole);
    }
    static bool oscPartOk (const Voice& v, int o) { return v.oscPart[o].ok; }
    static int groupOfKey (const Voice& v, int idx) { return v.groupOf[idx]; }
    static uint32_t lfoSkippedMask (const Voice& v) { return v.lfoSkipped; }
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
                                                  "filter1.mix", "osc3.tune", "mod1.amount", "mod2.amount", "master.volume", "osc1.gran.position", "osc1.sample.start",
                                                  "osc1.spec.position", "osc1.gran.density", "osc1.pan", "sub.pan" };
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

    /** `refSavings`: the savings the reference engine has off (the old code of the item under test); `optSavings`: the savings both engines have off. Every test that edits the patch while voices sound
        keeps the display only evaluation of unused LFOs off in both (an edit that newly routes an LFO starts its smoothing from the value, not from the state of an earlier period: not bit identical by
        design, see the tests of kOwnSavingLfoDisplay). */
    EnginePair (const std::vector<float>& plain, Tier tier, Rand& r, uint32_t refSavings = kOwnSavingVoiceControl, uint32_t optSavings = kOwnSavingLfoDisplay)
    {
        opt = createOwnEngine();
        ref = createOwnEngine();
        CHECK (setOwnSavingsOff (*ref, refSavings | optSavings));
        CHECK (setOwnSavingsOff (*opt, optSavings));
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
bool playAndCompare (EnginePair& p, Rand& r, int blocks, const char* what, bool edits = true, uint64_t ignoreSources = 0)
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
        if (edits && r.chance (0.06f))
        {
            const size_t i = static_cast<size_t> (r.range (0, static_cast<int> (paramCount()) - 1));
            if (voiceKey (table[i].key) || startsWith (table[i].key, "mod"))
            {
                const float v = randomValue (r, table[i]);
                p.opt->setParam (table[i].id, v);
                p.ref->setParam (table[i].id, v);
            }
        }
        if (edits && r.chance (0.04f))
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
        for (int id = 1; id <= LiveValues::kMaxSources; ++id) // the sources the caller does not compare (the display value of an unused LFO of a voice that is not the newest)
            if (id < 64 && ((ignoreSources >> id) & 1u) != 0)
                a.source[id - 1] = c.source[id - 1] = 0.0f;
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
    const int patches = sbtest::instrumented() ? 12 : sbtest::quickScaled ("voice_opt random patches", 80, 10, 140);
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
    const size_t step = sbtest::instrumented() ? 8 : static_cast<size_t> (sbtest::quickScaled ("voice_opt factory presets (every Nth preset, both the stored and the edited kind)", 1, 7, 314));
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
    const int patches = sbtest::instrumented() ? 10 : sbtest::quickScaled ("voice_opt voice state patches", 120, 20, 0);
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

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// V7 by group: a period in which a slot touched a key of an oscillator reads only the field groups of that key (and of the keys touched in the period before); every key of every
// oscillator is touched alone, the part the voice keeps is compared field for field with the whole part read again, and the voice with the reference code (which reads the whole part
// every period) is compared in state and output.

namespace
{
/** The keys of oscillator o that its part (or its library keys) reads, with the group they are in. */
std::vector<int> oscKeys (int o)
{
    std::vector<int> keys;
    const OscIdx& oi = paramIndex().osc[o];
    const int* f = reinterpret_cast<const int*> (&oi);
    for (size_t i = 0; i < sizeof (OscIdx) / sizeof (int); ++i)
        if (f[i] >= 0)
            keys.push_back (f[i]);
    for (int i = 0; i < kLibKeyCount; ++i)
        if (libKeys (o).idx[i] >= 0)
            keys.push_back (libKeys (o).idx[i]);
    std::sort (keys.begin(), keys.end());
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
    return keys;
}
} // namespace

TEST (voice_opt_osc_part_refresh_by_group_touches_every_key_alone)
{
    ensureAllBuiltinWavetables();
    float bends[16] = {}, wheel[16] = {}, after[16] = {}, tuned[128];
    for (int k = 0; k < 128; ++k)
        tuned[k] = static_cast<float> (k);
    float lfoPhase[10] = {}, rlfoValue[4] = {};
    int tested = 0, notModulatable = 0, failed = 0;
    for (int o = 0; o < 3; ++o)
        for (const int key : oscKeys (o))
        {
            const size_t choice = modDestinationChoiceOf (static_cast<size_t> (key));
            if (choice == 0)
            {
                ++notModulatable;
                continue;
            }
            for (int variant = 0; variant < 3; ++variant)
            {
                // the defaults with the three oscillators on, and one slot: macro 1 -> the key (the depth moves it across a large part of its range, the base value is varied by the variant)
                std::vector<float> base (kMaxParams, 0.0f);
                for (size_t i = 0; i < paramCount(); ++i)
                    base[i] = paramTable()[i].def;
                for (int q = 0; q < 3; ++q)
                    base[static_cast<size_t> (paramIndex().osc[q].on)] = 1.0f;
                base[static_cast<size_t> (paramIndex().filter[0].on)] = 1.0f;
                const ParamInfo& info = paramTable()[key];
                if (variant == 1)
                    base[static_cast<size_t> (key)] = info.min + 0.3f * (info.max - info.min);
                if (variant == 2)
                    base[static_cast<size_t> (key)] = info.max;
                const ModSlotParams& sp = paramIndex().slot[0];
                base[static_cast<size_t> (sp.source)] = static_cast<float> (kSrcMacro1);
                base[static_cast<size_t> (sp.destination)] = static_cast<float> (choice);
                base[static_cast<size_t> (sp.amount)] = variant == 2 ? -80.0f : 70.0f;
                ModMatrix matrix;
                matrix.compile (base.data());
                VoiceRig a, b;
                a.v.prepare (kSr);
                b.v.prepare (kSr);
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
                    g.economy = 0u;
                    g.usedSources = matrix.sourceMask();
                    g.baseEpoch = 1;
                    g.activeVoices = 1;
                }
                b.g.referenceControl = true;
                NoteStart n;
                n.key = 57 + variant;
                n.tunedNote = static_cast<float> (n.key);
                n.velocity = 0.8f;
                n.serial = 1;
                n.sequence = 1;
                a.v.start (n, 0.0f, 0.0f);
                b.v.start (n, 0.0f, 0.0f);
                bool touchedOnce = false, untouchedAfterTouched = false, prevTouched = false;
                for (int t = 0; t < 40; ++t)
                {
                    // touched, untouched, touched twice ... : the key returns to its knob while the epoch stays the same
                    static const float kPattern[] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.6f, 0.6f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f };
                    const float m = kPattern[t % (sizeof kPattern / sizeof kPattern[0])];
                    a.g.macro[0] = b.g.macro[0] = m;
                    a.v.tick (a.g, 0);
                    b.v.tick (b.g, 0);
                    int d = VoiceOptTest::diff (a.v, b.v);
                    for (int q = 0; q < 3 && d == 0; ++q)
                        d = VoiceOptTest::oscPartVsWhole (a.v, q) != 0 ? 200 + q : 0;
                    if (d == 0)
                    {
                        a.render (kControlPeriod, 0);
                        b.render (kControlPeriod, 0);
                        if (std::memcmp (a.mixL, b.mixL, sizeof a.mixL) != 0 || std::memcmp (a.mixR, b.mixR, sizeof a.mixR) != 0)
                            d = 100;
                    }
                    touchedOnce = touchedOnce || m > 0.0f;
                    untouchedAfterTouched = untouchedAfterTouched || (prevTouched && m == 0.0f);
                    prevTouched = m > 0.0f;
                    if (d != 0)
                    {
                        std::printf ("osc%d key %s variant %d period %d: field %d differs\n", o + 1, paramTable()[key].key, variant, t, d);
                        ++failed;
                        break;
                    }
                }
                if (failed == 0)
                    CHECK (touchedOnce && untouchedAfterTouched);
                ++tested;
            }
        }
    std::printf ("%d (osc key, base) cases, %d keys without a modulation destination, %d differ\n", tested, notModulatable, failed);
    CHECK (tested > 150);
    CHECK_EQ (failed, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The exp2 calls of fillFilterSettings () (key track, voice control cutoff, pan shift) are kept per voice and filter under the bit patterns of their arguments (+-0: no call)

TEST (filter_setup_memo_gives_the_settings_the_calls_give_for_every_kind_of_argument)
{
    const FilterIdx& fi = paramIndex().filter[0];
    std::vector<float> v (kMaxParams, 0.0f);
    for (size_t i = 0; i < paramCount(); ++i)
        v[i] = paramTable()[i].def;
    static const float kSpecial[] = { 0.0f, -0.0f, 1.0e-40f, -1.0e-40f, 1.0e-30f, 0.5f, -0.5f, 1.0f, 100.0f, -100.0f, 50.0f, -12.5f, NAN, INFINITY, -INFINITY, 1.0e38f, 1.0e-45f };
    constexpr int kNumSpecial = static_cast<int> (sizeof kSpecial / sizeof kSpecial[0]);
    Rand r (2026);
    FilterSetupMemo memo;
    float vcSemis = 0.0f, trackKnob = 0.0f, panKnob = 0.0f, cutoff = 1000.0f;
    int key = 60;
    auto pick = [&] (float& x, float lo, float hi)
    {
        if (r.chance (0.7f))
            return; // the same as the period before: the memo answers
        x = r.chance (0.5f) ? kSpecial[r.range (0, kNumSpecial - 1)] : lo + (hi - lo) * r.uni();
    };
    auto sameBits = [] (float x, float y) { return (x != x && y != y) || std::memcmp (&x, &y, 4) == 0; };
    long long cases = 0, bad = 0, hits = 0;
    for (int n = 0; n < 300000; ++n)
    {
        pick (vcSemis, -36.0f, 36.0f);
        pick (trackKnob, -200.0f, 200.0f);
        pick (panKnob, -100.0f, 100.0f);
        pick (cutoff, 20.0f, 20000.0f);
        if (r.chance (0.3f))
            key = r.range (0, 127);
        v[static_cast<size_t> (fi.keytrack)] = trackKnob;
        if (fi.pan >= 0)
            v[static_cast<size_t> (fi.pan)] = panKnob;
        v[static_cast<size_t> (fi.cutoff)] = cutoff;
        auto P = [&] (int idx) { return v[static_cast<size_t> (idx)]; };
        FilterSettings a, b;
        const uint32_t k0 = memo.ktKey, k1 = memo.vcKey, k2 = memo.panKey;
        fillFilterSettings (a, fi, P, P, false, key, vcSemis, false, nullptr);
        fillFilterSettings (b, fi, P, P, false, key, vcSemis, false, &memo);
        hits += (k0 == memo.ktKey && k1 == memo.vcKey && k2 == memo.panKey) ? 1 : 0;
        ++cases;
        if (!sameBits (a.cutoffHz, b.cutoffHz) || !sameBits (a.cutoffHzR, b.cutoffHzR))
        {
            if (bad < 5)
                std::printf ("filter memo: key %d vc %g track %g pan %g cutoff %g: %g / %g against %g / %g\n", key, static_cast<double> (vcSemis), static_cast<double> (trackKnob),
                             static_cast<double> (panKnob), static_cast<double> (cutoff), static_cast<double> (b.cutoffHz), static_cast<double> (b.cutoffHzR), static_cast<double> (a.cutoffHz),
                             static_cast<double> (a.cutoffHzR));
            ++bad;
        }
    }
    std::printf ("%lld filter settings, %lld answered from the memo, %lld differ\n", cases, hits, bad);
    CHECK (hits > cases / 5);
    CHECK_EQ (bad, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The voice count gain keeps the last results of its three libm calls (kOwnSavingVcgCache): the same samples as the engine that calls them every control period

TEST (voice_opt_voice_count_gain_cache_is_bit_identical)
{
    ensureAllBuiltinWavetables();
    const std::vector<std::vector<float>> presets = factoryPresets();
    int failed = 0, runs = 0;
    const float alphas[] = { 0.5f, 0.25f, 1.0f, 0.0f, 0.73f };
    const size_t presetStep = sbtest::instrumented() ? 24 : static_cast<size_t> (sbtest::quickScaled ("voice_opt voice count gain cache, every Nth preset (the 164 checks are those of this and the next reduction together)", 6, 24, 164));
    const int extraPatches = sbtest::instrumented() ? 4 : sbtest::quickScaled ("voice_opt voice count gain cache, random patches", 24, 6, 0);
    for (size_t i = 0; i < presets.size(); i += presetStep)
    {
        Rand r (static_cast<uint32_t> (4100 + i));
        const std::vector<float> plain = randomPatch (r, &presets[i], 0.02f);
        EnginePair p (plain, i % 4 == 1 ? Tier::high : Tier::low, r, kOwnSavingVcgCache);
        const float alpha = alphas[i % 5];
        CHECK (setOwnVoiceCountAlpha (*p.opt, alpha));
        CHECK (setOwnVoiceCountAlpha (*p.ref, alpha));
        char what[64];
        std::snprintf (what, sizeof what, "voice count gain, preset %zu alpha %.2f", i, static_cast<double> (alpha));
        ++runs;
        failed += playAndCompare (p, r, 400, what) ? 0 : 1;
    }
    for (int s = 0; s < extraPatches; ++s)
    {
        Rand r (static_cast<uint32_t> (4900 + s));
        std::vector<float> plain = randomPatch (r, nullptr, 0.2f);
        plain[static_cast<size_t> (idxOf ("master.polyphony"))] = static_cast<float> (r.range (4, 16));
        EnginePair p (plain, s % 2 == 0 ? Tier::low : Tier::high, r, kOwnSavingVcgCache);
        const float alpha = 0.1f + 0.9f * r.uni();
        CHECK (setOwnVoiceCountAlpha (*p.opt, alpha));
        CHECK (setOwnVoiceCountAlpha (*p.ref, alpha));
        char what[64];
        std::snprintf (what, sizeof what, "voice count gain, random patch %d", s);
        ++runs;
        failed += playAndCompare (p, r, 500, what) ? 0 : 1;
    }
    std::printf ("%d voice count gain runs, %d differ\n", runs, failed);
    CHECK_EQ (failed, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// An LFO no slot reads is evaluated (shape, smoothing) only in the newest voice: the others advance its clock, its age and its display position. The sound, every clock and every source that
// something reads stay bit for bit those of the voice that evaluates everything; the display value of an unused LFO of a voice that is not the newest is not made.

TEST (voice_opt_unused_lfo_display_skip_keeps_the_sound_and_the_clocks)
{
    ensureAllBuiltinWavetables();
    const int patches = sbtest::instrumented() ? 10 : sbtest::quickScaled ("voice_opt unused LFO display skip patches", 100, 15, 0);
    int failed = 0;
    long long skippedPeriods = 0;
    float bends[16] = {}, wheel[16] = {}, after[16] = {}, tuned[128];
    for (int k = 0; k < 128; ++k)
        tuned[k] = static_cast<float> (k);
    float lfoPhase[10] = {}, rlfoValue[4] = {};
    for (int s = 0; s < patches && failed == 0; ++s)
    {
        Rand r (static_cast<uint32_t> (8000 + s));
        std::vector<float> base = randomPatch (r, nullptr, 0.3f);
        base.resize (kMaxParams, 0.0f);
        ModMatrix matrix;
        matrix.compile (base.data());
        uint32_t ignoreLfo = 0;
        for (int l = 0; l < 10; ++l)
            if (((matrix.sourceMask() >> srcLfo (l)) & 1u) == 0)
                ignoreLfo |= 1u << l;
        VoiceRig a, b;
        a.v.prepare (kSr);
        b.v.prepare (kSr);
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
            g.baseEpoch = 1;
            g.activeVoices = 2;
        }
        // both voices run the shipped control code (the V1 .. V13 caches); `a` also skips the display of the unused LFOs, `b` evaluates every LFO
        a.g.lfoDisplaySkip = true;
        uint32_t serial = 1, sequence = 1;
        bool started = false;
        for (int t = 0; t < 1200 && failed == 0; ++t)
        {
            if (!started || r.chance (0.01f))
            {
                NoteStart n;
                n.key = r.range (30, 100);
                n.tunedNote = static_cast<float> (n.key);
                n.velocity = r.chance (0.1f) ? 0.0f : r.uni();
                n.serial = serial++;
                n.sequence = sequence++;
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
                const int m = r.range (0, 7);
                a.g.macro[m] = b.g.macro[m] = r.chance (0.3f) ? 0.0f : r.uni();
                bends[0] = r.chance (0.5f) ? 0.0f : -2.0f + 4.0f * r.uni();
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
            // the newest voice changes now and then: this voice is it for a stretch, then another one (a higher serial) is
            if (r.chance (0.03f))
                a.g.newestSerial = r.chance (0.4f) ? serial - 1 : serial + 5;
            a.v.tick (a.g, 0);
            b.v.tick (b.g, 0);
            skippedPeriods += VoiceOptTest::lfoSkippedMask (a.v) != 0 ? 1 : 0;
            int d = VoiceOptTest::diff (a.v, b.v, ignoreLfo);
            if (d == 0 && a.v.isActive())
            {
                a.render (kControlPeriod, 0);
                b.render (kControlPeriod, 0);
                if (std::memcmp (a.mixL, b.mixL, sizeof a.mixL) != 0 || std::memcmp (a.mixR, b.mixR, sizeof a.mixR) != 0)
                    d = 100;
                else if (std::memcmp (a.srcSq, b.srcSq, sizeof a.srcSq) != 0)
                    d = 101;
                else
                    d = VoiceOptTest::diff (a.v, b.v, ignoreLfo);
            }
            if (d != 0)
            {
                std::printf ("unused LFO skip, patch %d period %d: field %d differs\n", s, t, d);
                ++failed;
            }
        }
    }
    std::printf ("%d patches, %lld periods with an LFO not evaluated, %d differ\n", patches, skippedPeriods, failed);
    CHECK (skippedPeriods > 1000);
    CHECK_EQ (failed, 0);
}

/** The engines with and without the display only evaluation (kOwnSavingLfoDisplay): random patches whose slots stay as they are (no parameter edits) while notes start, end and are stolen; the audio
    is bit for bit the same, and so is every value of the display readout except the display value of an unused LFO, which is not made for a voice that is not the newest (when the newest voice ends in
    the last period of a block, the voice that becomes the newest shows the value of its last evaluation until the next block). */
TEST (voice_opt_engine_unused_lfo_display_skip_sounds_the_same)
{
    ensureAllBuiltinWavetables();
    const std::vector<std::vector<float>> presets = factoryPresets();
    int failed = 0, runs = 0;
    const int numPatches = sbtest::instrumented() ? 6 : sbtest::quickScaled ("voice_opt engine unused LFO display skip, patches", 48, 8, 80);
    for (int s = 0; s < numPatches; ++s)
    {
        Rand r (static_cast<uint32_t> (6100 + s));
        std::vector<float> plain = s % 3 == 2 ? presets[static_cast<size_t> (s) % presets.size()] : randomPatch (r, nullptr, 0.2f);
        plain[static_cast<size_t> (idxOf ("master.polyphony"))] = static_cast<float> (r.range (2, 8));
        std::vector<float> padded = plain;
        padded.resize (kMaxParams, 0.0f);
        ModMatrix matrix;
        matrix.compile (padded.data());
        uint64_t ignore = 0;
        for (int l = 0; l < 10; ++l)
            if (((matrix.sourceMask() >> srcLfo (l)) & 1u) == 0)
                ignore |= uint64_t (1) << srcLfo (l);
        EnginePair p (plain, s % 2 == 0 ? Tier::low : Tier::high, r, kOwnSavingLfoDisplay, 0u);
        char what[64];
        std::snprintf (what, sizeof what, "unused LFO skip, patch %d", s);
        ++runs;
        failed += playAndCompare (p, r, 500, what, false, ignore) ? 0 : 1;
    }
    std::printf ("%d runs, %d differ\n", runs, failed);
    CHECK_EQ (failed, 0);
}

/** The display of the newest voice, block by block: a chord of voices with the ten LFOs at different rates, none of them read by a slot; the readout (every source, the LFO positions of all
    voices, the envelopes) is the whole LiveValues, compared bit for bit with the engine that evaluates the LFOs in every voice. No voice ends in the run. */
TEST (voice_opt_engine_unused_lfo_display_readout_of_the_newest_voice_is_the_same)
{
    ensureAllBuiltinWavetables();
    int failed = 0;
    for (int variant = 0; variant < 4; ++variant)
    {
        std::vector<float> plain (paramCount());
        for (size_t i = 0; i < plain.size(); ++i)
            plain[i] = paramTable()[i].def;
        auto set = [&] (const char* key, float v) { plain[static_cast<size_t> (idxOf (key))] = v; };
        set ("osc1.on", 1.0f);
        set ("filter1.on", 1.0f);
        set ("env1.release", 3000.0f);
        set ("master.polyphony", 8.0f);
        for (int l = 1; l <= 10; ++l)
        {
            char key[48];
            std::snprintf (key, sizeof key, "lfo%d.rate", l);
            set (key, 0.4f + 0.9f * static_cast<float> (l) * (variant % 2 == 0 ? 1.0f : 3.0f));
            std::snprintf (key, sizeof key, "lfo%d.shape", l);
            set (key, static_cast<float> ((l + variant) % 12));
            std::snprintf (key, sizeof key, "lfo%d.smooth", l);
            set (key, l % 2 == 0 ? 1.0f : 0.0f);
            std::snprintf (key, sizeof key, "lfo%d.sync", l);
            set (key, 0.0f);
        }
        if (variant >= 2)
        {
            // one LFO used by a slot (every voice evaluates it): the others are still display only
            const ModSlotParams& sp = paramIndex().slot[0];
            plain[static_cast<size_t> (sp.source)] = static_cast<float> (kSrcLfo1 + 2);
            plain[static_cast<size_t> (sp.destination)] = static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (idxOf ("osc1.tune"))));
            plain[static_cast<size_t> (sp.amount)] = 40.0f;
        }
        Rand r (static_cast<uint32_t> (31 + variant));
        EnginePair p (plain, variant % 2 == 0 ? Tier::low : Tier::high, r, kOwnSavingLfoDisplay, 0u);
        std::vector<float> lo (128), ro (128), lr (128), rr (128);
        const int onAt[] = { 0, 3, 4, 9, 16, 23 }, key[] = { 48, 55, 60, 64, 67, 72 };
        int ni = 0;
        for (int b = 0; b < 90 && failed == 0; ++b)
        {
            std::vector<Event> ev;
            if (ni < 6 && b == onAt[ni])
            {
                Event e;
                e.type = Event::NoteOn;
                e.frame = static_cast<int16_t> (5 + 7 * ni);
                e.key = static_cast<int16_t> (key[ni]);
                e.value = 0.5f + 0.08f * static_cast<float> (ni);
                e.channel = 0;
                ev.push_back (e);
                ++ni;
            }
            if (b == 50 || b == 60)
            {
                Event e;
                e.type = Event::NoteOff;
                e.frame = 11;
                e.key = static_cast<int16_t> (key[b == 50 ? 5 : 2]);
                e.value = 0.4f;
                ev.push_back (e);
            }
            p.opt->process (ev.data(), static_cast<int> (ev.size()), lo.data(), ro.data(), 128);
            p.ref->process (ev.data(), static_cast<int> (ev.size()), lr.data(), rr.data(), 128);
            LiveValues x, y;
            p.opt->copyReadout (x);
            p.ref->copyReadout (y);
            if (std::memcmp (lo.data(), lr.data(), 128 * 4) != 0 || std::memcmp (ro.data(), rr.data(), 128 * 4) != 0)
            {
                std::printf ("variant %d block %d: the audio differs\n", variant, b);
                ++failed;
            }
            else if (std::memcmp (&x, &y, sizeof x) != 0)
            {
                int first = -1;
                for (int i = 0; i < LiveValues::kMaxSources && first < 0; ++i)
                    if (std::memcmp (&x.source[i], &y.source[i], 4) != 0)
                        first = i;
                std::printf ("variant %d block %d: the readout differs (first differing source index %d)\n", variant, b, first);
                ++failed;
            }
        }
    }
    CHECK_EQ (failed, 0);
}

/** An edit that newly routes an LFO: the voice that skipped it starts the smoothing at the value of the first period that uses it (the reference smooths from the state of the earlier period).
    The two agree to the smoothing within a few smooth times; this is the one difference, and it is not bit identity. */
TEST (voice_opt_unused_lfo_newly_routed_starts_its_smoothing_at_the_value)
{
    ensureAllBuiltinWavetables();
    float bends[16] = {}, wheel[16] = {}, after[16] = {}, tuned[128];
    for (int k = 0; k < 128; ++k)
        tuned[k] = static_cast<float> (k);
    float lfoPhase[10] = {}, rlfoValue[4] = {};
    std::vector<float> base (kMaxParams, 0.0f);
    for (size_t i = 0; i < paramCount(); ++i)
        base[i] = paramTable()[i].def;
    base[static_cast<size_t> (idxOf ("osc1.on"))] = 1.0f;
    base[static_cast<size_t> (idxOf ("filter1.on"))] = 1.0f;
    base[static_cast<size_t> (idxOf ("lfo1.rate"))] = 3.0f;
    base[static_cast<size_t> (idxOf ("lfo1.smooth"))] = 1.0f;
    base[static_cast<size_t> (idxOf ("lfo1.smooth_time"))] = 80.0f;
    ModMatrix unused;
    unused.compile (base.data());
    std::vector<float> routed = base;
    const ModSlotParams& sp = paramIndex().slot[0];
    routed[static_cast<size_t> (sp.source)] = static_cast<float> (kSrcLfo1);
    routed[static_cast<size_t> (sp.destination)] = static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (idxOf ("osc1.level"))));
    routed[static_cast<size_t> (sp.amount)] = 50.0f;
    ModMatrix used;
    used.compile (routed.data());
    VoiceRig a, b;
    a.v.prepare (kSr);
    b.v.prepare (kSr);
    for (VoiceRig* x : { &a, &b })
    {
        TickGlobals& g = x->g;
        g.base = base.data();
        g.matrix = &unused;
        g.sampleRate = kSr;
        g.bpm = 120.0;
        g.channelBendSemis = bends;
        g.channelModWheel = wheel;
        g.channelAftertouch = after;
        g.tunedNotes = tuned;
        g.lfoPhase = lfoPhase;
        g.rlfoValue = rlfoValue;
        g.economy = kEconomyAll;
        g.usedSources = unused.sourceMask();
        g.baseEpoch = 1;
        g.activeVoices = 2;
    }
    a.g.lfoDisplaySkip = true;
    a.g.newestSerial = 99; // `a` is not the newest voice
    NoteStart n;
    n.key = 60;
    n.tunedNote = 60.0f;
    n.velocity = 0.8f;
    n.serial = 1;
    n.sequence = 1;
    a.v.start (n, 0.0f, 0.0f);
    b.v.start (n, 0.0f, 0.0f);
    for (int t = 0; t < 600; ++t)
    {
        a.v.tick (a.g, 0);
        b.v.tick (b.g, 0);
        CHECK_EQ (VoiceOptTest::diff (a.v, b.v, 0x3FFu), 0); // no LFO is read: the display values are the one thing that may differ
    }
    CHECK ((VoiceOptTest::lfoSkippedMask (a.v) & 1u) != 0);
    // the edit: LFO 1 now modulates osc1.level
    for (VoiceRig* x : { &a, &b })
    {
        x->g.base = routed.data();
        x->g.matrix = &used;
        x->g.usedSources = used.sourceMask();
        x->g.baseEpoch = 2;
    }
    float worst = 0.0f, firstGap = -1.0f;
    for (int t = 0; t < 900; ++t)
    {
        a.v.tick (a.g, 0);
        b.v.tick (b.g, 0);
        const float x = a.v.sourceValues()[srcLfo (0)], y = b.v.sourceValues()[srcLfo (0)];
        if (t == 0)
        {
            firstGap = std::fabs (x - y);
            CHECK ((VoiceOptTest::lfoSkippedMask (a.v) & 1u) == 0); // evaluated now
        }
        if (t >= 600)
            worst = std::max (worst, std::fabs (x - y));
    }
    std::printf ("newly routed LFO: first period differs by %.4g, after 600 periods by at most %.3g\n", static_cast<double> (firstGap), static_cast<double> (worst));
    CHECK (firstGap > 1.0e-4f); // the difference exists (the reference smooths from the state of the earlier period) ...
    CHECK (worst < 1.0e-6f);    // ... and is gone to the smoothing within 600 periods (19 time constants)
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The display readout and the voice statistics are written while somebody asks (copyReadout, voiceStats, setReadoutTargets; kOwnSavingReadoutCadence off = every block, as before), and the
// newest voices are found in one pass. The audio is the same in every case.

namespace
{
Event noteEvent (Event::Type type, int frame, int key, float value)
{
    Event e;
    e.type = type;
    e.frame = static_cast<int16_t> (frame);
    e.key = static_cast<int16_t> (key);
    e.value = value;
    e.channel = 0;
    return e;
}

bool sameBits (const float* a, const float* b, size_t n) { return std::memcmp (a, b, n * sizeof (float)) == 0; }
} // namespace

TEST (voice_opt_engine_readout_on_demand_is_the_same_while_it_is_polled_and_waits_when_it_is_not)
{
    ensureAllBuiltinWavetables();
    int failed = 0;
    for (int variant = 0; variant < 4; ++variant)
    {
        Rand r (static_cast<uint32_t> (900 + variant));
        std::vector<float> plain = randomPatch (r, nullptr, 0.15f);
        plain[static_cast<size_t> (idxOf ("master.polyphony"))] = variant < 2 ? 6.0f : 12.0f; // up to 6 voices: the newest voices of the readout are all of them; 9 and more: the newest eight
        plain[static_cast<size_t> (idxOf ("osc1.on"))] = 1.0f;
        plain[static_cast<size_t> (idxOf ("env1.sustain"))] = 100.0f; // the notes stay
        plain[static_cast<size_t> (idxOf ("env1.attack"))] = 1.0f;
        EnginePair p (plain, variant % 2 == 0 ? Tier::low : Tier::high, r, kOwnSavingReadoutCadence, 0u);
        int mostVoices = 0;
        std::vector<float> lo (128), ro (128), lr (128), rr (128);
        auto block = [&] (const std::vector<Event>& ev)
        {
            p.opt->process (ev.data(), static_cast<int> (ev.size()), lo.data(), ro.data(), 128);
            p.ref->process (ev.data(), static_cast<int> (ev.size()), lr.data(), rr.data(), 128);
            if (!sameBits (lo.data(), lr.data(), 128) || !sameBits (ro.data(), rr.data(), 128))
            {
                std::printf ("variant %d: the audio differs\n", variant);
                ++failed;
            }
        };
        auto poll = [&] (LiveValues& a, LiveValues& c, VoiceStats& sa, VoiceStats& sc)
        {
            CHECK (p.opt->copyReadout (a));
            CHECK (p.ref->copyReadout (c));
            CHECK (p.opt->voiceStats (sa));
            CHECK (p.ref->voiceStats (sc));
        };
        // 1. polled about 30 times a second (one block in 11): every poll reads what the engine that writes every block reads
        for (int b = 0; b < 330 && failed == 0; ++b)
        {
            std::vector<Event> ev;
            if (b % 37 == 0)
                ev.push_back (noteEvent (Event::NoteOn, 9, 40 + (b / 37) * 4, 0.6f));
            block (ev);
            if (b % 11 == 0)
            {
                LiveValues a, c;
                VoiceStats sa, sc;
                poll (a, c, sa, sc);
                mostVoices = std::max (mostVoices, a.numLfoVoices);
                if (std::memcmp (&a, &c, sizeof a) != 0 || std::memcmp (&sa, &sc, sizeof sa) != 0)
                {
                    std::printf ("variant %d block %d: a polled readout differs\n", variant, b);
                    ++failed;
                }
            }
        }
        CHECK (variant < 2 || mostVoices == LiveValues::kReadVoices); // more voices than the readout holds: the newest eight
        // 2. nobody asks for 400 blocks (the last poll keeps the readout going for 256 blocks), two notes start at the end: the readout is not written any more (the statistics are the old ones),
        //    the audio is the same
        LiveValues a, c;
        VoiceStats sa, sc;
        poll (a, c, sa, sc);
        const unsigned noteOnsBefore = sa.noteOns;
        for (int b = 0; b < 400 && failed == 0; ++b)
        {
            std::vector<Event> ev;
            if (b == 350 || b == 380)
                ev.push_back (noteEvent (Event::NoteOn, 20, 50 + (b - 350) / 10, 0.7f));
            if (b == 320)
            {
                // a state flag that anybody may read at any time (missingContent): oscillator 2 asks for the user table of the preset, which is not there
                for (IEngine* e : { p.opt.get(), p.ref.get() })
                {
                    e->setParam (findParamByKey ("osc2.on")->id, 1.0f);
                    e->setParam (findParamByKey ("osc2.type")->id, 0.0f);
                    e->setParam (findParamByKey ("osc2.wavetable")->id, static_cast<float> (kWavetablePresetTable));
                }
            }
            block (ev);
        }
        CHECK ((p.ref->missingContent() & 16u) != 0u);                       // the engine that writes every block saw it
        CHECK_EQ (p.opt->missingContent(), p.ref->missingContent());         // and so does the engine that waits for a poll
        p.opt->voiceStats (sa);
        p.ref->voiceStats (sc);
        CHECK_EQ (sc.noteOns, noteOnsBefore + 2u); // the engine that writes every block counted them
        CHECK_EQ (sa.noteOns, noteOnsBefore);      // the engine that waits for a poll did not publish them yet
        // 3. the poll woke it: the next block writes, and the readout is the one of the engine that writes every block (the meters start again from nothing: not compared)
        block ({});
        poll (a, c, sa, sc);
        CHECK_EQ (sa.noteOns, sc.noteOns);
        CHECK (std::memcmp (&sa, &sc, sizeof sa) == 0);
        const bool same = sameBits (a.source, c.source, LiveValues::kMaxSources) && sameBits (a.lfoPhase, c.lfoPhase, LiveValues::kNumLfos)
                          && sameBits (&a.lfoVoicePhase[0][0], &c.lfoVoicePhase[0][0], LiveValues::kNumLfos * LiveValues::kReadVoices)
                          && sameBits (&a.voicePos[0][0], &c.voicePos[0][0], 3 * LiveValues::kReadVoices) && sameBits (a.subPhase, c.subPhase, LiveValues::kReadVoices)
                          && sameBits (a.envStage, c.envStage, LiveValues::kNumEnvs) && sameBits (a.envPosition, c.envPosition, LiveValues::kNumEnvs)
                          && sameBits (&a.envVoiceStage[0][0], &c.envVoiceStage[0][0], LiveValues::kNumEnvs * LiveValues::kReadVoices) && a.activeVoices == c.activeVoices;
        if (!same)
            std::printf ("variant %d: the readout after the wake-up differs\n", variant);
        CHECK (same);
        // the meters of the woken engine start from nothing: never above those of the engine that kept them (it only decays from what it saw)
        for (int s = 0; s < LiveValues::kMaxStrips; ++s)
            CHECK (a.stripPeak[s] <= c.stripPeak[s] + 1.0e-6f);
    }
    CHECK_EQ (failed, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// V4: the zero skip of the matrix, every combination of the special values of what one () reads

namespace
{
struct SlotGridResult
{
    long long cases = 0, skipped = 0, failed = 0;
};

/** Bit for bit equal, except that any two NaNs are equal: the sign and payload of a NaN that the hardware makes (inf - inf, 0 * inf) are not defined by C++ and differ between the
    architectures (the x86 default NaN is negative, the ARM one positive), and the two evaluations of V4 reach the same NaN by different operations. */
bool sameFloatBits (const float* a, const float* b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (!(a[i] != a[i] && b[i] != b[i]) && std::memcmp (a + i, b + i, sizeof (float)) != 0)
            return false;
    return true;
}

/** One slot (slot 1: macro 1, aux macro 2, on `dest`), or with `viaAmount` that slot on the amount of slot 2 (slot 2: Fixed -> osc1.tune at 50 %); every source / aux value of `values`
    in both lanes; the reference evaluation against the one with the skip, delta, deltaRight, the touched list and the slot state compared bit for bit. */
void slotGrid (SlotGridResult& res, float power, bool bipolar, float rise, bool withAux, float auxPower, float auxOut, float amount, float delayMs, bool viaAmount)
{
    std::vector<float> base (kMaxParams, 0.0f);
    for (size_t i = 0; i < paramCount(); ++i)
        base[i] = paramTable()[i].def;
    const ModSlotParams& sp = paramIndex().slot[0];
    const int tune = idxOf ("osc1.tune");
    base[static_cast<size_t> (sp.source)] = static_cast<float> (kSrcMacro1);
    base[static_cast<size_t> (sp.destination)] = static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (viaAmount ? paramIndex().slot[1].amount : tune)));
    base[static_cast<size_t> (sp.amount)] = amount * 100.0f;
    base[static_cast<size_t> (sp.power)] = power;
    base[static_cast<size_t> (sp.bipolar)] = bipolar ? 1.0f : 0.0f;
    base[static_cast<size_t> (sp.auxSource)] = withAux ? static_cast<float> (kSrcMacro1 + 1) : 0.0f;
    base[static_cast<size_t> (sp.auxPower)] = auxPower;
    base[static_cast<size_t> (sp.auxOutput)] = auxOut * 100.0f;
    base[static_cast<size_t> (sp.rise)] = rise;
    base[static_cast<size_t> (sp.delay)] = delayMs;
    if (viaAmount)
    {
        const ModSlotParams& s2 = paramIndex().slot[1];
        base[static_cast<size_t> (s2.source)] = static_cast<float> (kSrcFixed);
        base[static_cast<size_t> (s2.destination)] = static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (tune)));
        base[static_cast<size_t> (s2.amount)] = 50.0f;
    }
    ModMatrix m;
    m.compile (base.data());
    static const float kValues[] = { 0.0f, -0.0f, -1.0e-40f, -0.5f, -INFINITY, NAN, 1.0e-40f, 0.25f, 1.0f, 2.0f, INFINITY };
    static const float kAux[] = { 0.0f, NAN, 0.5f, 1.0f, INFINITY, -1.0f };
    std::vector<float> dL[2], dR[2];
    for (int k = 0; k < 2; ++k)
    {
        dL[k].assign (paramCount(), 0.0f);
        dR[k].assign (paramCount(), 0.0f);
    }
    const std::vector<float> auxSet = withAux ? std::vector<float> (std::begin (kAux), std::end (kAux)) : std::vector<float> { 0.0f };
    for (float xl : kValues)
        for (float xr : kValues)
            for (float al : auxSet)
                for (float ar : auxSet)
                {
                    float srcL[kNumSourceIds] = {}, srcRv[kNumSourceIds] = {};
                    srcL[kSrcMacro1] = xl;
                    srcRv[kSrcMacro1] = xr;
                    srcL[kSrcMacro1 + 1] = al;
                    srcRv[kSrcMacro1 + 1] = ar;
                    srcL[kSrcFixed] = srcRv[kSrcFixed] = 1.0f;
                    int touched[2][kNumModSlots] = {}, num[2] = {};
                    SlotState state[2][kNumModSlots];
                    for (int k = 0; k < 2; ++k)
                    {
                        EvalContext ctx;
                        ctx.src = srcL;
                        ctx.srcR = srcRv;
                        ctx.delta = dL[k].data();
                        ctx.deltaRight = dR[k].data();
                        ctx.touched = touched[k];
                        ctx.numTouched = &num[k];
                        ctx.maxTouched = kNumModSlots;
                        ctx.state = state[k];
                        ctx.dt = 32.0f / 44100.0f;
                        ctx.voiceAge = 0.01f;
                        ctx.reference = k == 0;
                        m.evaluate (ctx);
                    }
                    ++res.cases;
                    const bool predicted = !bipolar && !(rise > 0.0f) && std::isfinite (power) && xl <= 0.0f && xr <= 0.0f && amount == amount
                                           && (!withAux || (std::isfinite (auxPower) && std::fabs (auxPower) <= 80.0f && std::isfinite (auxOut) && al == al && ar == ar));
                    res.skipped += predicted ? 1 : 0;
                    bool same = num[0] == num[1] && std::memcmp (touched[0], touched[1], sizeof touched[0]) == 0
                                && sameFloatBits (dL[0].data(), dL[1].data(), dL[0].size()) && sameFloatBits (dR[0].data(), dR[1].data(), dR[0].size());
                    for (int q = 0; q < kNumModSlots && same; ++q)
                        same = sameFloatBits (&state[0][q].heldL, &state[1][q].heldL, 1) && sameFloatBits (&state[0][q].heldR, &state[1][q].heldR, 1) && state[0][q].init == state[1][q].init;
                    if (!same)
                    {
                        if (res.failed < 5)
                            std::printf ("matrix slot: differs for power %g bipolar %d rise %g aux %d (power %g, out %g) amount %g delay %g via amount %d, source %g / %g, aux %g / %g\n",
                                         static_cast<double> (power), bipolar ? 1 : 0, static_cast<double> (rise), withAux ? 1 : 0, static_cast<double> (auxPower), static_cast<double> (auxOut),
                                         static_cast<double> (amount), static_cast<double> (delayMs), viaAmount ? 1 : 0, static_cast<double> (xl), static_cast<double> (xr),
                                         static_cast<double> (al), static_cast<double> (ar));
                        ++res.failed;
                    }
                    for (int k = 0; k < 2; ++k)
                        for (int q = 0; q < num[k]; ++q)
                            dL[k][static_cast<size_t> (touched[k][q])] = dR[k][static_cast<size_t> (touched[k][q])] = 0.0f;
                }
}
} // namespace

TEST (matrix_zero_skip_gives_the_same_deltas_for_every_special_value)
{
    SlotGridResult res;
    const auto t0 = std::chrono::steady_clock::now();
    // quick run: every 29th combination of the grid below (a stride that divides none of the loop sizes: the sample is spread over every axis); SB_TEST_FULL=1: the whole grid
    const int gridStride = sbtest::quickScaled ("voice_opt matrix zero skip grid (every Nth combination)", 1, 29);
    long long combination = 0;
    for (float power : { 0.0f, 5.0e-5f, 1.0f, -12.0f, 100.0f, -100.0f, INFINITY, NAN })
        for (bool bipolar : { false, true })
            for (float rise : { 0.0f, 5.0f, NAN })
                for (float amount : { 0.5f, -1.0f, -0.0f, NAN, 1.0e-40f })
                    for (float delayMs : { 0.0f, 50.0f })
                        for (bool viaAmount : { false, true })
                        {
                            if ((bipolar || rise != 0.0f) && power != 0.0f && power != 100.0f)
                                continue; // (a bipolar or slewed slot is never skipped: two powers of them are enough)
                            if (combination++ % gridStride == 0)
                                slotGrid (res, power, bipolar, rise, false, 0.0f, 1.0f, amount, delayMs, viaAmount);
                            for (float auxPower : { 0.0f, 2.0f, -85.0f, -95.0f, 95.0f, 80.0f, INFINITY, NAN })
                                for (float auxOut : { 1.0f, 0.0f, INFINITY })
                                    if (combination++ % gridStride == 0)
                                        slotGrid (res, power, bipolar, rise, true, auxPower, auxOut, amount, delayMs, viaAmount);
                        }
    std::printf ("%lld slot evaluations, %lld with the skip, %lld differ, %.1f s\n", res.cases, res.skipped, res.failed,
                 std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    CHECK (res.skipped > 100000 / gridStride);   // (the quick run has 1 / gridStride of the evaluations: the same share is required)
    CHECK_EQ (res.failed, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// V5: the zero test of a parameter delta on its bits is the float compare d == 0.0f, for all 2^32 bit patterns

namespace
{
/** Calls fn (pattern) for the bit patterns of the 2^32 sweeps: every pattern with stride 1 (SB_TEST_FULL=1); in the quick run every stride-th pattern and, as no stride may skip
    the places where an off by one decides, also the 128 patterns around every binade boundary (exponent field e, both signs: 0x00800000, 0x3f800000, 0x4a800000 = 2^22 among
    them). A pattern is visited once (the neighbourhood skips the ones the stride has). */
template <class Fn>
void forEachSweepPattern (uint64_t stride, Fn fn)
{
    for (uint64_t i = 0; i <= 0xffffffffull; i += stride)
        fn (static_cast<uint32_t> (i));
    if (stride > 1)
        for (const uint32_t sign : { 0u, 0x80000000u })
            for (uint32_t e = 0; e < 256u; ++e)
                for (uint32_t d = 0; d < 128u; ++d)
                {
                    const uint32_t u = ((e << 23) | sign) + d - 64u;
                    if (u % stride != 0)
                        fn (u);
                }
}
} // namespace

TEST (voice_param_read_zero_test_on_the_bits_is_the_float_compare_for_every_pattern)
{
    uint64_t mismatches = 0, zeros = 0;
    // quick run: every 257th pattern and the neighbourhood of every binade boundary (the negative zero is one of them); SB_TEST_FULL=1: all 2^32 patterns
    const uint64_t stride = static_cast<uint64_t> (sbtest::quickScaled ("voice_opt 2^32 pattern sweep of the zero test (stride)", 1, 257));
    forEachSweepPattern (stride, [&] (uint32_t u)
    {
        float f;
        std::memcpy (&f, &u, 4);
        volatile float vf = f;   // (a real float compare, not folded into the integer test)
        const bool byFloat = vf == 0.0f, byBits = (u << 1) == 0u;
        mismatches += byFloat != byBits ? 1 : 0;
        zeros += byBits ? 1 : 0;
    });
    CHECK_EQ (zeros, uint64_t (2));
    CHECK_EQ (mismatches, uint64_t (0));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// V12: ffloorBits and ffloor against std::floor and against the form they replaced, for all 2^32 bit patterns (signed zeros, denormals, infinities, every NaN)

namespace
{
float ffloorBefore (float x) noexcept
{
    if (!(std::fabs (x) < 4194304.0f) || x == 0.0f)
        return std::floor (x);
    const int i = static_cast<int> (x);
    return static_cast<float> (i - (x < static_cast<float> (i) ? 1 : 0));
}
} // namespace

namespace
{
/** Flush-to-zero on (the MPC audio thread and the bench run with it): denormal inputs of the float arithmetic and compares count as zero, denormal results become zero. */
struct FlushToZero
{
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t saved = 0;
    FlushToZero() { __asm__ __volatile__ ("mrs %0, fpcr" : "=r"(saved)); const uint64_t on = saved | (uint64_t (1) << 24); __asm__ __volatile__ ("msr fpcr, %0" : : "r"(on)); }
    ~FlushToZero() { __asm__ __volatile__ ("msr fpcr, %0" : : "r"(saved)); }
    static constexpr bool available = true;
#elif defined(__x86_64__) || defined(__i386__)
    unsigned saved = 0;
    FlushToZero() { saved = __builtin_ia32_stmxcsr(); __builtin_ia32_ldmxcsr (saved | 0x8040u); }
    ~FlushToZero() { __builtin_ia32_ldmxcsr (saved); }
    static constexpr bool available = true;
#else
    static constexpr bool available = false;
#endif
};
} // namespace

TEST (ffloor_on_the_bits_is_the_form_before_for_every_pattern_with_flush_to_zero)
{
    // with flush-to-zero the form before sent a denormal to std::floor (its compare with 0 was true): the new one does the same; every other pattern is unaffected by the mode
    if (!FlushToZero::available)
        return;
    FlushToZero ftz;
    uint64_t differBefore = 0;
    const uint64_t stride = static_cast<uint64_t> (sbtest::quickScaled ("voice_opt 2^32 pattern sweep of ffloor with flush-to-zero (stride)", 1, 257));
    forEachSweepPattern (stride, [&] (uint32_t u)
    {
        float x;
        std::memcpy (&x, &u, 4);
        volatile float vx = x;
        const float a = ffloorBits (vx), c = ffloorBefore (vx), d = ffloor (vx);
        differBefore += std::memcmp (&a, &c, 4) != 0 || std::memcmp (&d, &c, 4) != 0 ? 1 : 0;
    });
    CHECK_EQ (differBefore, uint64_t (0));
}

TEST (ffloor_on_the_bits_is_std_floor_for_every_pattern)
{
    uint64_t differFloor = 0, differBefore = 0, nans = 0;
    const uint64_t stride = static_cast<uint64_t> (sbtest::quickScaled ("voice_opt 2^32 pattern sweep of ffloor against std::floor (stride)", 1, 257));
    forEachSweepPattern (stride, [&] (uint32_t u)
    {
        float x;
        std::memcpy (&x, &u, 4);
        volatile float vx = x;
        const float a = ffloorBits (vx), b = std::floor (vx), c = ffloorBefore (vx), d = ffloor (vx);
        uint32_t ua, ub, uc;
        std::memcpy (&ua, &a, 4);
        std::memcpy (&ub, &b, 4);
        std::memcpy (&uc, &c, 4);
        nans += x != x ? 1 : 0;
        differFloor += ua != ub ? 1 : 0;
        differBefore += ua != uc || std::memcmp (&d, &c, 4) != 0 ? 1 : 0;
    });
    std::printf ("ffloor: 2^32 patterns (%llu NaN), %llu differ from std::floor, %llu from the form before\n", static_cast<unsigned long long> (nans),
                 static_cast<unsigned long long> (differFloor), static_cast<unsigned long long> (differBefore));
    CHECK_EQ (differFloor, uint64_t (0));
    CHECK_EQ (differBefore, uint64_t (0));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The engine comparisons again with flush-to-zero on, as the MPC audio thread and the bench run (the compares and the arithmetic treat denormals as zero there)

TEST (voice_opt_engine_with_flush_to_zero_is_bit_identical)
{
    if (!FlushToZero::available)
        return;
    ensureAllBuiltinWavetables();
    const std::vector<std::vector<float>> presets = factoryPresets();
    FlushToZero ftz;
    int failed = 0, runs = 0;
    const int numPatches = sbtest::instrumented() ? 6 : sbtest::quickScaled ("voice_opt flush-to-zero random patches (the 132 checks are those of this and the next reduction together)", 40, 8, 132);
    const size_t presetStep = sbtest::instrumented() ? 16 : static_cast<size_t> (sbtest::quickScaled ("voice_opt flush-to-zero factory presets, every Nth", 4, 16, 0));
    for (int s = 0; s < numPatches; ++s)
    {
        Rand r (static_cast<uint32_t> (9000 + s));
        const std::vector<float> plain = randomPatch (r, nullptr, 0.25f);
        EnginePair p (plain, s % 3 == 0 ? Tier::high : Tier::low, r);
        char what[64];
        std::snprintf (what, sizeof what, "flush-to-zero random patch %d", s);
        ++runs;
        failed += playAndCompare (p, r, 400, what) ? 0 : 1;
    }
    for (size_t i = 0; i < presets.size(); i += presetStep)
    {
        Rand r (static_cast<uint32_t> (300 + i));
        const std::vector<float> plain = randomPatch (r, &presets[i], 0.03f);
        EnginePair p (plain, Tier::low, r);
        char what[64];
        std::snprintf (what, sizeof what, "flush-to-zero factory preset %zu (edited)", i);
        ++runs;
        failed += playAndCompare (p, r, 300, what) ? 0 : 1;
    }
    std::printf ("%d runs with flush-to-zero, %d differ\n", runs, failed);
    CHECK_EQ (failed, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// V13: an envelope with the curve denominators given (and the decay to a sustain of 1 without its shape) against the same envelope without them

TEST (envelope_with_curve_denominators_is_bit_identical)
{
    Rand r (4242u);
    const float curves[] = { 0.0f, 5.0e-5f, -5.0e-5f, 1.0f, -3.0f, 3.0f, 10.0f, -10.0f, 79.0f, -79.0f, 80.0f, 85.0f, -85.0f, 95.0f, -95.0f, 200.0f, INFINITY, -INFINITY, NAN };
    const float sustains[] = { 1.0f, 1.0f, 0.0f, 0.5f, 0.999999f, -0.0f };
    long long steps = 0, failed = 0;
    for (int run = 0; run < 20000; ++run)
    {
        EnvSettings a;
        a.delay = r.chance (0.7f) ? 0.0f : 0.05f * r.uni();
        a.attack = r.chance (0.2f) ? 0.0f : 0.5f * r.uni();
        a.hold = r.chance (0.6f) ? 0.0f : 0.1f * r.uni();
        a.decay = r.chance (0.1f) ? 0.0f : (r.chance (0.05f) ? INFINITY : 2.0f * r.uni());
        a.sustain = r.chance (0.5f) ? sustains[r.range (0, 5)] : r.uni();
        a.release = r.chance (0.1f) ? 0.0f : 1.0f * r.uni();
        a.attackCurve = curves[r.range (0, static_cast<int> (sizeof curves / sizeof curves[0]) - 1)];
        a.decayCurve = curves[r.range (0, static_cast<int> (sizeof curves / sizeof curves[0]) - 1)];
        a.releaseCurve = curves[r.range (0, static_cast<int> (sizeof curves / sizeof curves[0]) - 1)];
        EnvSettings b = a;
        b.fillDens();
        Envelope x, y;
        const float start = r.chance (0.5f) ? 0.0f : r.uni();
        x.noteOn (start);
        y.noteOn (start);
        const int n = r.range (10, 400);
        const int off = r.range (0, n);
        for (int t = 0; t < n; ++t)
        {
            if (t == off)
            {
                x.noteOff();
                y.noteOff();
            }
            const double dt = r.chance (0.2f) ? 4.0 * 32.0 / 44100.0 : (r.chance (0.02f) ? 0.0 : 32.0 / 44100.0);
            const float vx = x.advance (a, dt), vy = y.advance (b, dt);
            const double px = x.stageSeconds(), py = y.stageSeconds();
            const float fx = x.fineFraction (0.37f), fy = y.fineFraction (0.37f);
            ++steps;
            if (std::memcmp (&vx, &vy, 4) != 0 || x.fineStage() != y.fineStage() || std::memcmp (&px, &py, 8) != 0 || std::memcmp (&fx, &fy, 4) != 0)
            {
                if (failed < 5)
                    std::printf ("envelope run %d step %d: %.9g against %.9g (curves %g %g %g, sustain %g)\n", run, t, static_cast<double> (vx), static_cast<double> (vy),
                                 static_cast<double> (a.attackCurve), static_cast<double> (a.decayCurve), static_cast<double> (a.releaseCurve), static_cast<double> (a.sustain));
                ++failed;
                break;
            }
        }
    }
    std::printf ("%lld envelope steps, %lld runs differ\n", steps, failed);
    CHECK_EQ (failed, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The library oscillators (Sample, Granular, Spectral) with a file: their keys are read through the kept oscillator part (V7) too

TEST (voice_opt_engine_library_oscillators_are_bit_identical)
{
    ensureAllBuiltinWavetables();
    MemoryContentStore store;
    {
        const size_t n = 44100;
        std::vector<float> x (n);
        for (size_t i = 0; i < n; ++i)
            x[i] = static_cast<float> (0.5 * std::sin (6.283185307179586 * 220.0 * static_cast<double> (i) / 44100.0) + 0.1 * std::sin (0.37 * static_cast<double> (i * i % 4001)));
        store.add ("test", "tone.wav", encodeWav (x.data(), nullptr, n, 44100, 16));
    }
    StateContent content;
    ContentFileRef ref;
    ref.pack = "test";
    ref.file = "tone.wav";
    ref.name = "tone";
    attachSample (content, 1, ref, 44100, 44100, 1);
    int failed = 0, runs = 0;
    const int perType = sbtest::instrumented() ? 2 : sbtest::quickScaled ("voice_opt library oscillators, patches per type", 8, 2, 36);
    for (int type = 1; type <= 3; ++type)
        for (int s = 0; s < perType; ++s)
        {
            Rand r (static_cast<uint32_t> (700 + 31 * type + s));
            std::vector<float> plain = randomPatch (r, nullptr, 0.15f);
            plain[static_cast<size_t> (idxOf ("osc1.on"))] = 1.0f;
            plain[static_cast<size_t> (idxOf ("osc1.type"))] = static_cast<float> (type);
            plain[static_cast<size_t> (idxOf ("osc1.level"))] = 80.0f;
            EnginePair p (plain, s % 2 == 0 ? Tier::low : Tier::high, r);
            for (IEngine* e : { p.opt.get(), p.ref.get() })
            {
                e->setContentStore (&store);
                e->setContent (content);
            }
            char what[64];
            std::snprintf (what, sizeof what, "library type %d patch %d", type, s);
            ++runs;
            failed += playAndCompare (p, r, 300, what) ? 0 : 1;
        }
    std::printf ("%d library oscillator runs, %d differ\n", runs, failed);
    CHECK_EQ (failed, 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Precomputed expm1 denominators and cached slew alphas (ModMatrix::compile / evaluate)
// Reusing helper types from matrix_zero_skip_gives_the_same_deltas_for_every_special_value and envelope_with_curve_denominators_is_bit_identical

TEST (matrix_precomputed_power_and_slew_are_bit_identical)
{
    long long cases = 0, failed = 0;
    const int tune = idxOf ("osc1.tune");
    const ModSlotParams& sp = paramIndex().slot[0];

    std::vector<float> dL[2], dR[2];
    for (int k = 0; k < 2; ++k)
    {
        dL[k].assign (paramCount(), 0.0f);
        dR[k].assign (paramCount(), 0.0f);
    }

    const float powers[] = { 0.0f, 5.0e-5f, -5.0e-5f, 1.5f, -6.0f, 80.0f, -80.0f, 100.0f, INFINITY, NAN };
    const float auxPowers[] = { 0.0f, 5.0e-5f, 3.0f, -8.0f, NAN };
    const bool bipolars[] = { false, true };
    const float rises[] = { 0.0f, 15.0f, 120.0f };
    const float falls[] = { 0.0f, 40.0f, 250.0f };

    for (float power : powers)
        for (float auxPower : auxPowers)
            for (bool bipolar : bipolars)
                for (float rise : rises)
                    for (float fall : falls)
                    {
                        std::vector<float> base (kMaxParams, 0.0f);
                        for (size_t i = 0; i < paramCount(); ++i)
                            base[i] = paramTable()[i].def;

                        base[static_cast<size_t> (sp.source)] = static_cast<float> (kSrcMacro1);
                        base[static_cast<size_t> (sp.destination)] = static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (tune)));
                        base[static_cast<size_t> (sp.amount)] = 100.0f;
                        base[static_cast<size_t> (sp.power)] = power;
                        base[static_cast<size_t> (sp.bipolar)] = bipolar ? 1.0f : 0.0f;
                        base[static_cast<size_t> (sp.auxSource)] = static_cast<float> (kSrcMacro1 + 1);
                        base[static_cast<size_t> (sp.auxPower)] = auxPower;
                        base[static_cast<size_t> (sp.auxOutput)] = 100.0f;
                        base[static_cast<size_t> (sp.rise)] = rise;
                        base[static_cast<size_t> (sp.fall)] = fall;

                        ModMatrix m;
                        m.compile (base.data());

                        int touched[2][kNumModSlots] = {}, num[2] = {};
                        SlotState state[2][kNumModSlots] = {};

                        for (const float dt : { 32.0f / 44100.0f, 32.0f / 48000.0f })
                        {
                            for (int tick = 0; tick < 16; ++tick)
                            {
                                const float u = static_cast<float> (tick < 8 ? tick : 15 - tick) / 7.0f;
                                const float xl = 0.05f + 0.88f * u;
                                const float xr = 0.93f - 0.88f * u;
                                const float al = 0.23f + 0.50f * u;
                                const float ar = 0.71f - 0.50f * u;

                                float srcL[kNumSourceIds] = {}, srcRv[kNumSourceIds] = {};
                                srcL[kSrcMacro1] = xl;
                                srcRv[kSrcMacro1] = xr;
                                srcL[kSrcMacro1 + 1] = al;
                                srcRv[kSrcMacro1 + 1] = ar;
                                srcL[kSrcFixed] = srcRv[kSrcFixed] = 1.0f;

                                for (int k = 0; k < 2; ++k)
                                {
                                    EvalContext ctx;
                                    ctx.src = srcL;
                                    ctx.srcR = srcRv;
                                    ctx.delta = dL[k].data();
                                    ctx.deltaRight = dR[k].data();
                                    ctx.touched = touched[k];
                                    ctx.numTouched = &num[k];
                                    ctx.maxTouched = kNumModSlots;
                                    ctx.state = state[k];
                                    ctx.dt = dt;
                                    ctx.voiceAge = 0.01f + 0.001f * static_cast<float> (tick);
                                    ctx.reference = (k == 0);
                                    m.evaluate (ctx);
                                }

                                ++cases;
                                bool same = num[0] == num[1]
                                            && std::memcmp (touched[0], touched[1], sizeof touched[0]) == 0
                                            && std::memcmp (dL[0].data(), dL[1].data(), dL[0].size() * sizeof (float)) == 0
                                            && std::memcmp (dR[0].data(), dR[1].data(), dR[0].size() * sizeof (float)) == 0;
                                for (int q = 0; q < kNumModSlots && same; ++q)
                                    same = std::memcmp (&state[0][q].heldL, &state[1][q].heldL, sizeof (float)) == 0
                                           && std::memcmp (&state[0][q].heldR, &state[1][q].heldR, sizeof (float)) == 0
                                           && state[0][q].init == state[1][q].init;

                                if (!same)
                                {
                                    if (failed < 5)
                                        std::printf ("matrix precompute differs: power %g auxPower %g bipolar %d rise %g fall %g dt %g tick %d\n",
                                                     static_cast<double> (power), static_cast<double> (auxPower), bipolar ? 1 : 0,
                                                     static_cast<double> (rise), static_cast<double> (fall), static_cast<double> (dt), tick);
                                    ++failed;
                                }

                                for (int k = 0; k < 2; ++k)
                                    for (int q = 0; q < num[k]; ++q)
                                        dL[k][static_cast<size_t> (touched[k][q])] = dR[k][static_cast<size_t> (touched[k][q])] = 0.0f;
                            }
                        }
                    }

    std::printf ("%lld matrix precompute checks, %lld differ\n", cases, failed);
    CHECK_EQ (failed, 0);

    // 2. Print MODMATRIX-CHECKSUM <case> sum=<%.9g> last8=<8x %.9g> for 4 fixed 200-tick runs
    struct FixedRun
    {
        const char* name;
        float power;
        float auxPower;
        bool withAux;
        bool bipolar;
        float riseMs;
        float fallMs;
    };

    const FixedRun runs4[4] = {
        { "unipolar_curve", 2.5f, 0.0f, false, false, 0.0f, 0.0f },
        { "bipolar_curve", -3.0f, 0.0f, false, true, 0.0f, 0.0f },
        { "aux_curve", 1.5f, -2.0f, true, false, 0.0f, 0.0f },
        { "rise_fall_slew", 0.0f, 0.0f, false, false, 15.0f, 40.0f }
    };

    const float dt48k = 32.0f / 48000.0f;

    for (const auto& run : runs4)
    {
        std::vector<float> base (kMaxParams, 0.0f);
        for (size_t i = 0; i < paramCount(); ++i)
            base[i] = paramTable()[i].def;

        base[static_cast<size_t> (sp.source)] = static_cast<float> (kSrcMacro1);
        base[static_cast<size_t> (sp.destination)] = static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (tune)));
        base[static_cast<size_t> (sp.amount)] = 100.0f;
        base[static_cast<size_t> (sp.power)] = run.power;
        base[static_cast<size_t> (sp.bipolar)] = run.bipolar ? 1.0f : 0.0f;
        base[static_cast<size_t> (sp.auxSource)] = run.withAux ? static_cast<float> (kSrcMacro1 + 1) : 0.0f;
        base[static_cast<size_t> (sp.auxPower)] = run.auxPower;
        base[static_cast<size_t> (sp.auxOutput)] = 100.0f;
        base[static_cast<size_t> (sp.rise)] = run.riseMs;
        base[static_cast<size_t> (sp.fall)] = run.fallMs;

        ModMatrix m;
        m.compile (base.data());

        SlotState state[2][kNumModSlots] = {};
        int touched[2][kNumModSlots] = {}, num[2] = {};
        double sumAbs = 0.0;
        float last8[8] = {};

        for (int t = 0; t < 200; ++t)
        {
            const float xl = 0.05f + 0.88f * (0.5f + 0.5f * std::sin (0.05f * static_cast<float> (t)));
            const float xr = 0.05f + 0.88f * (0.5f + 0.5f * std::cos (0.07f * static_cast<float> (t)));
            const float al = 0.10f + 0.80f * (0.5f + 0.5f * std::sin (0.11f * static_cast<float> (t) + 1.0f));
            const float ar = 0.10f + 0.80f * (0.5f + 0.5f * std::cos (0.13f * static_cast<float> (t) + 2.0f));

            float srcL[kNumSourceIds] = {}, srcRv[kNumSourceIds] = {};
            srcL[kSrcMacro1] = xl;
            srcRv[kSrcMacro1] = xr;
            srcL[kSrcMacro1 + 1] = al;
            srcRv[kSrcMacro1 + 1] = ar;
            srcL[kSrcFixed] = srcRv[kSrcFixed] = 1.0f;

            for (int k = 0; k < 2; ++k)
            {
                EvalContext ctx;
                ctx.src = srcL;
                ctx.srcR = srcRv;
                ctx.delta = dL[k].data();
                ctx.deltaRight = dR[k].data();
                ctx.touched = touched[k];
                ctx.numTouched = &num[k];
                ctx.maxTouched = kNumModSlots;
                ctx.state = state[k];
                ctx.dt = dt48k;
                ctx.voiceAge = 0.001f * static_cast<float> (t);
                ctx.reference = (k == 0);
                m.evaluate (ctx);
            }

            const float outL = dL[1][tune];
            const float outR = dR[1][tune];
            sumAbs += static_cast<double> (std::fabs (outL)) + static_cast<double> (std::fabs (outR));
            if (t >= 192)
                last8[t - 192] = outL;

            bool same = std::memcmp (dL[0].data(), dL[1].data(), dL[0].size() * sizeof (float)) == 0
                        && std::memcmp (dR[0].data(), dR[1].data(), dR[0].size() * sizeof (float)) == 0;
            if (!same)
                ++failed;

            for (int k = 0; k < 2; ++k)
                for (int q = 0; q < num[k]; ++q)
                    dL[k][static_cast<size_t> (touched[k][q])] = dR[k][static_cast<size_t> (touched[k][q])] = 0.0f;
        }

        CHECK (sumAbs > 0.0);
        std::printf ("MODMATRIX-CHECKSUM %s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     run.name, sumAbs,
                     static_cast<double> (last8[0]), static_cast<double> (last8[1]),
                     static_cast<double> (last8[2]), static_cast<double> (last8[3]),
                     static_cast<double> (last8[4]), static_cast<double> (last8[5]),
                     static_cast<double> (last8[6]), static_cast<double> (last8[7]));
    }
    CHECK_EQ (failed, 0);
}

