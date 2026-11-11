// SynthyBoy - param-audit: the rig (one engine, deterministic renders), the content fixtures, the note scripts and the fault injection of the self test.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

#include "audit.h"
#include "synthyboy/modulation.h"
#include "synthyboy/quality.h"

using namespace synthyboy;

namespace paudit
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<uint8_t> wav16 (const std::vector<float>& samples, int sampleRate = 48000)
{
    std::vector<uint8_t> b;
    const auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    const auto put16 = [&] (uint16_t v) { b.push_back (static_cast<uint8_t> (v)); b.push_back (static_cast<uint8_t> (v >> 8)); };
    const auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (t[i])); };
    tag ("RIFF"); put32 (static_cast<uint32_t> (36 + samples.size() * 2)); tag ("WAVE");
    tag ("fmt "); put32 (16); put16 (1); put16 (1); put32 (static_cast<uint32_t> (sampleRate)); put32 (static_cast<uint32_t> (sampleRate * 2)); put16 (2); put16 (16);
    tag ("data"); put32 (static_cast<uint32_t> (samples.size() * 2));
    for (float s : samples)
        put16 (static_cast<uint16_t> (static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f))));
    return b;
}

struct Lcg
{
    uint32_t s = 12345u;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float> ((s >> 8) & 0xffffu) / 32768.0f - 1.0f; }
};

/** A tone of `partials` harmonics (amplitude 1/h) with an exponential decay: the sample every sample oscillator gets. */
std::vector<float> toneSample (double f0, double seconds, int partials, double decayPerSecond)
{
    std::vector<float> v (static_cast<size_t> (seconds * 48000.0), 0.0f);
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = static_cast<double> (i) / 48000.0;
        double s = 0.0;
        for (int h = 1; h <= partials; ++h)
            s += std::sin (2.0 * kPi * f0 * h * t) / h;
        v[i] = static_cast<float> (0.35 * s * std::exp (-decayPerSecond * t));
    }
    return v;
}
} // namespace

Fixtures::Fixtures()
{
    for (int osc = 1; osc <= 3; ++osc)
    {
        // a wavetable of 8 frames: frame f has 1 + 3 f harmonics
        std::vector<float> table;
        for (int f = 0; f < 8; ++f)
            for (int i = 0; i < 2048; ++i)
            {
                double s = 0.0;
                for (int h = 1; h <= 1 + 3 * f; ++h)
                    s += std::sin (2.0 * kPi * h * i / 2048.0) / h;
                table.push_back (static_cast<float> (0.5 * s));
            }
        const std::string tableFile = "table" + std::to_string (osc) + ".wav";
        store.add ("audit", tableFile, wav16 (table));
        ContentWavetable w;
        w.osc = osc;
        w.ref.pack = "audit";
        w.ref.file = tableFile;
        w.ref.name = "audit table";
        w.frames = 8;
        content.wavetables.push_back (w);

        const auto smp = toneSample (220.0 * osc, 1.2, 6, 1.5);
        const std::string smpFile = "sample" + std::to_string (osc) + ".wav";
        store.add ("audit", smpFile, wav16 (smp));
        ContentSample s;
        s.osc = osc;
        s.ref.pack = "audit";
        s.ref.file = smpFile;
        s.ref.name = "audit sample";
        s.sampleRate = 48000;
        s.frames = static_cast<int> (smp.size());
        s.channels = 1;
        s.slices = { 0.25f, 0.5f, 0.75f };
        content.samples.push_back (s);

        // the multisample instrument: two regions, two samples
        const std::string dir = "inst" + std::to_string (osc) + "/";
        const std::string sfz = "<global> ampeg_release=0.05\n<region> sample=low.wav lokey=0 hikey=59 pitch_keycenter=48 loop_mode=loop_continuous loop_start=0 loop_end=24000\n"
                                "<region> sample=high.wav lokey=60 hikey=127 pitch_keycenter=72 loop_mode=loop_continuous loop_start=0 loop_end=24000\n";
        store.add ("audit", dir + "instrument.sfz", std::vector<uint8_t> (sfz.begin(), sfz.end()));
        store.add ("audit", dir + "low.wav", wav16 (toneSample (130.81, 1.0, 5, 0.0)));
        store.add ("audit", dir + "high.wav", wav16 (toneSample (523.25, 1.0, 5, 0.0)));
        ContentInstrument inst;
        inst.osc = osc;
        inst.ref.pack = "audit";
        inst.ref.file = dir + "instrument.sfz";
        inst.ref.name = "audit instrument";
        inst.zones = 2;
        content.instruments.push_back (inst);

        for (int slot = 1; slot <= kNumRemapSlots; ++slot)
        {
            ContentRemap rm;
            rm.osc = osc;
            rm.slot = slot;
            rm.name = "audit remap";
            rm.points = { ContentPoint (0.0f, 0.0f), ContentPoint (0.5f, 0.15f + 0.2f * static_cast<float> (slot)), ContentPoint (1.0f, 1.0f) }; // every slot a different curve
            content.remaps.push_back (rm);
        }
        ContentRemap sf;
        sf.osc = osc;
        sf.slot = 1;
        sf.name = "audit spectral filter";
        sf.points = { ContentPoint (0.0f, 0.5f), ContentPoint (0.4f, 1.0f), ContentPoint (0.7f, 0.1f), ContentPoint (1.0f, 0.5f) };
        content.spectralFilters.push_back (sf);
    }
    // the noise source: a short burst of noise
    {
        Lcg rng;
        std::vector<float> n (24000);
        for (auto& x : n)
            x = 0.5f * rng.next();
        store.add ("audit", "noise.wav", wav16 (n));
        ContentSample s;
        s.osc = kNoiseSampleOsc;
        s.ref.pack = "audit";
        s.ref.file = "noise.wav";
        s.ref.name = "audit noise";
        s.sampleRate = 48000;
        s.frames = static_cast<int> (n.size());
        s.channels = 1;
        content.samples.push_back (s);
    }
    // the impulse response: 0.4 s of decaying noise, deterministic
    {
        Lcg rng;
        std::vector<float> ir (static_cast<size_t> (0.4 * 48000.0));
        for (size_t i = 0; i < ir.size(); ++i)
            ir[i] = 0.4f * rng.next() * static_cast<float> (std::exp (-9.0 * static_cast<double> (i) / 48000.0));
        store.add ("audit", "ir.wav", wav16 (ir));
        ContentFileRef ref;
        ref.pack = "audit";
        ref.file = "ir.wav";
        ref.name = "audit ir";
        content.impulses.push_back (ref);
    }
    for (int lfo = 1; lfo <= kMaxContentLfoShapes; ++lfo)
    {
        ContentLfoShape sh;
        sh.lfo = lfo;
        sh.name = "audit shape";
        sh.points = { ContentPoint (0.0f, 0.0f), ContentPoint (0.3f, 1.0f), ContentPoint (0.7f, 0.2f), ContentPoint (1.0f, 0.0f) };
        content.lfoShapes.push_back (sh);
    }
    for (const char* f : { "filter1", "filter2", "fxfilter" })
    {
        ContentFilterCurve c;
        c.filter = f;
        c.points = { { 0.0f, 0.0f }, { 0.3f, 12.0f }, { 0.6f, -12.0f }, { 1.0f, 0.0f } };
        content.filterCurves.push_back (c);
    }
    for (int slot = 61; slot <= 63; ++slot) // the curves sit on slots 61 .. 63 only: a curve on a slot bends its source (G2), the other slots stay linear
    {
        ContentModCurve c;
        c.slot = slot;
        c.points = { ContentPoint (0.0f, 0.0f), ContentPoint (0.5f, 0.2f), ContentPoint (1.0f, 1.0f) };
        content.modCurves.push_back (c);
    }
    content.shaperA = { ContentPoint (0.0f, 0.0f), ContentPoint (0.4f, 0.8f), ContentPoint (1.0f, 1.0f) };
    content.shaperB = { ContentPoint (0.0f, 0.0f), ContentPoint (0.6f, 0.3f), ContentPoint (1.0f, 1.0f) };
}

// ---- scripts --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace
{
Event noteEv (int frame, Event::Type type, int key, float value, int channel = 0)
{
    Event e;
    e.frame = frame;
    e.type = type;
    e.channel = static_cast<int16_t> (channel);
    e.key = static_cast<int16_t> (key);
    e.value = value;
    return e;
}
int sec (double s) { return static_cast<int> (std::lround (s * kSr)); }
constexpr float kVel = 100.0f / 127.0f;

Script basic (const char* name, double hold, double total, const std::vector<int>& keys)
{
    Script s;
    s.name = name;
    for (int k : keys)
        s.events.push_back (noteEv (0, Event::NoteOn, k, kVel));
    for (int k : keys)
        s.events.push_back (noteEv (sec (hold), Event::NoteOff, k, 0.0f));
    s.frames = sec (total);
    s.offFrame = sec (hold);
    s.steadyFrom = sec (0.25);
    s.steadyTo = sec (hold);
    return s;
}
} // namespace

Script scriptByName (const std::string& name)
{
    if (name == "long")
    {
        auto s = basic ("long", 0.5, 2.0, { 48, 55 });
        return s;
    }
    if (name == "long87")
    {
        auto s = basic ("long87", 0.5, 2.0, { 48 });
        s.bpm = 87.0;
        return s;
    }
    if (name == "single")
        return basic ("single", 0.6, 1.0, { 48 });
    if (name == "chord8")
        return basic ("chord8", 0.6, 1.0, { 48, 52, 55, 59, 60, 64, 67, 71 });
    if (name == "scale")
        return basic ("scale", 0.6, 1.0, { 49, 54 });
    if (name == "legato")
    {
        Script s;
        s.name = "legato";
        s.events = { noteEv (0, Event::NoteOn, 48, kVel), noteEv (sec (0.25), Event::NoteOn, 55, kVel), noteEv (sec (0.45), Event::NoteOff, 48, 0.0f), noteEv (sec (0.6), Event::NoteOff, 55, 0.0f) };
        s.frames = sec (1.0);
        s.offFrame = sec (0.6);
        s.steadyFrom = sec (0.3);
        s.steadyTo = sec (0.6);
        return s;
    }
    if (name == "legatoVel")
    {
        Script s;
        s.name = "legatoVel";
        s.events = { noteEv (0, Event::NoteOn, 48, 0.95f), noteEv (sec (0.3), Event::NoteOn, 55, 0.3f), noteEv (sec (0.5), Event::NoteOff, 48, 0.0f), noteEv (sec (0.8), Event::NoteOff, 55, 0.0f) };
        s.frames = sec (1.2);
        s.offFrame = sec (0.8);
        s.steadyFrom = sec (0.35);
        s.steadyTo = sec (0.8);
        return s;
    }
    if (name == "seq")
    {
        Script s;
        s.name = "seq";
        s.events = { noteEv (0, Event::NoteOn, 48, kVel), noteEv (sec (0.2), Event::NoteOff, 48, 0.0f), noteEv (sec (0.25), Event::NoteOn, 60, kVel), noteEv (sec (0.6), Event::NoteOff, 60, 0.0f) };
        s.frames = sec (1.0);
        s.offFrame = sec (0.6);
        s.steadyFrom = sec (0.35);
        s.steadyTo = sec (0.6);
        return s;
    }
    if (name == "same")
    {
        Script s;
        s.name = "same";
        s.events = { noteEv (0, Event::NoteOn, 48, kVel), noteEv (sec (0.3), Event::NoteOn, 48, kVel), noteEv (sec (0.6), Event::NoteOff, 48, 0.0f) };
        s.frames = sec (1.0);
        s.offFrame = sec (0.6);
        s.steadyFrom = sec (0.35);
        s.steadyTo = sec (0.6);
        return s;
    }
    if (name == "fast")
    {
        Script s;
        s.name = "fast";
        s.events = { noteEv (0, Event::NoteOn, 48, kVel), noteEv (sec (0.1), Event::NoteOff, 48, 0.0f), noteEv (sec (0.15), Event::NoteOn, 55, kVel), noteEv (sec (0.3), Event::NoteOff, 55, 0.0f) };
        s.frames = sec (0.8);
        s.offFrame = sec (0.3);
        s.steadyFrom = sec (0.17);
        s.steadyTo = sec (0.3);
        return s;
    }
    if (name == "bend")
    {
        auto s = basic ("bend", 0.6, 1.0, { 48, 55 });
        s.events.insert (s.events.begin(), noteEv (0, Event::PitchBend, 0, 1.0f));
        return s;
    }
    if (name == "bendDown")
    {
        auto s = basic ("bendDown", 0.6, 1.0, { 48, 55 });
        s.events.insert (s.events.begin(), noteEv (0, Event::PitchBend, 0, -1.0f));
        return s;
    }
    if (name == "mpe")
    {
        Script s = basic ("mpe", 0.6, 1.0, {});
        s.events.push_back (noteEv (0, Event::PitchBend, 0, 0.5f, 1));
        s.events.push_back (noteEv (0, Event::NoteOn, 48, kVel, 1));
        s.events.push_back (noteEv (0, Event::NoteOn, 55, kVel, 2));
        for (int ch = 1; ch <= 2; ++ch)
        {
            auto e = noteEv (sec (0.1), Event::NoteExpression, -1, ch == 1 ? 0.8f : 0.3f, ch);
            e.expression = Event::Pressure;
            s.events.push_back (e);
            e = noteEv (sec (0.1), Event::NoteExpression, -1, ch == 1 ? 0.2f : 0.9f, ch);
            e.expression = Event::Brightness;
            s.events.push_back (e);
            e = noteEv (sec (0.1), Event::NoteExpression, -1, ch == 1 ? 0.15f : 0.85f, ch);
            e.expression = Event::Pan;
            s.events.push_back (e);
        }
        s.events.push_back (noteEv (sec (0.1), Event::Aftertouch, 0, 0.6f, 1));
        s.events.push_back (noteEv (sec (0.6), Event::NoteOff, 48, 0.0f, 1));
        s.events.push_back (noteEv (sec (0.6), Event::NoteOff, 55, 0.0f, 2));
        return s;
    }
    return basic ("default", 0.6, 1.0, { 48, 55 });
}

// ---- fault injection ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

const char* activeFault()
{
    const char* f = std::getenv ("SYNTHYBOY_AUDIT_FAULT");
    return (f != nullptr && f[0] != '\0') ? f : nullptr;
}

float faultFor (const std::string& key, float value)
{
    const char* f = activeFault();
    if (f == nullptr)
        return value;
    const std::string fault (f);
    if (fault == "cutoff_scaled" && key == "filter1.cutoff")
        return std::min (20000.0f, value * 1.5f);                 // cutoff scaled wrong (a 7 semitone error)
    if (fault == "pan_swapped" && key == "osc1.pan")
        return -value;                                            // left and right swapped
    if (fault == "delay_time_2x" && (key == "delay.time" || key == "delay.time2"))
        return std::min (4000.0f, value * 2.0f);                  // the delay line is twice as long as displayed
    if (fault == "tune_cents" && key == "osc1.tune")
        return value + 2.0f;                                      // two cents off
    if (fault == "chorus_unwired" && key.rfind ("chorus.", 0) == 0 && key != "chorus.on")
        return findParamByKey (key.c_str()) != nullptr ? findParamByKey (key.c_str())->def : value; // the chorus controls do not reach the engine
    if (fault == "volume_db_as_linear" && key == "master.volume")
        return value * 0.5f;                                      // master volume in half the dB
    if (fault == "env_attack_half" && key == "env1.attack")
        return value * 0.5f;                                      // an envelope time that is half the displayed value
    return value;
}

// ---- rig ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

Rig::Rig (EngineKind kind, double sampleRate, Tier tier) : sr_ (sampleRate), tier_ (tier)
{
    engine_ = createEngine (kind);
    if (engine_ == nullptr)
        return;
    Quality q = qualityForTier (tier_);
    q.realtime = false;
    engine_->prepare (sr_, kBlock, q);
    engine_->setContentStore (&fixtures_.store);
    engine_->setContent (fixtures_.content);
    // warm-up: the first renders of a new engine differ from the later ones (caches and pools are filled lazily); the audit starts from the settled state
    begin ({});
    for (int i = 0; i < 3; ++i)
        render (scriptByName ("default"));
    begin ({});
    for (int i = 0; i < 2; ++i)
        render (scriptByName ("chord8"));
}

void Rig::prepareIfNeeded()
{
    if (engine_->needsPrepare())
    {
        Quality q = qualityForTier (tier_);
        q.realtime = false;
        engine_->prepare (sr_, kBlock, q);
    }
}

void Rig::set (const std::string& key, float value)
{

    const auto* info = findParamByKey (key.c_str());
    if (info == nullptr)
        return;
    engine_->setParam (info->id, faultFor (key, value));
}

float Rig::get (const std::string& key) const
{
    const auto* info = findParamByKey (key.c_str());
    return info != nullptr ? engine_->getParam (info->id) : 0.0f;
}

bool Rig::supports (const std::string& key) const
{
    const auto* info = findParamByKey (key.c_str());
    return info != nullptr && engine_->supportsParam (info->id);
}

void Rig::begin (const Ovs& ov)
{
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        engine_->setParam (table[i].id, table[i].def);
    // quality.no_smoothing: the audit measures the settled sound of a value, so the parameter smoothing is off unless the key itself is swept. (A render that follows
    // another value of a key can still differ from the same render of a fresh engine by up to about -70 dB re peak for 40 ms, a leftover of the voice that is reused:
    // PARAM-AUDIT-REPORT.md 4. That residual is the noise floor of the audit: a change below kNoiseFloorDb is no change.)
    set ("quality.no_smoothing", 1.0f);
    for (const auto& o : ov)
        set (o.key, o.value);
    prepareIfNeeded();
}

Audio Rig::render (const Script& script)
{
    return renderOnce (script);
}

Audio Rig::renderOnce (const Script& script)
{
    prepareIfNeeded();
    engine_->reset();
    engine_->restartEffectPhases(); // the phases of the effect LFOs start at 0: a render does not depend on the renders before it (IEngine::restartEffectPhases)
    TransportInfo tr;
    tr.bpm = script.bpm;
    engine_->setTransport (tr);
    Audio out;
    out.l.assign (static_cast<size_t> (script.frames), 0.0f);
    out.r.assign (static_cast<size_t> (script.frames), 0.0f);
    std::vector<Event> block;
    size_t next = 0;
    for (int start = 0; start < script.frames; start += kBlock)
    {
        const int n = std::min (kBlock, script.frames - start);
        block.clear();
        while (next < script.events.size() && script.events[next].frame < start + n)
        {
            Event e = script.events[next++];
            e.frame = std::max (0, e.frame - start);
            block.push_back (e);
        }
        engine_->process (block.empty() ? nullptr : block.data(), static_cast<int> (block.size()), out.l.data() + start, out.r.data() + start, n);
    }
    return out;
}

Audio Rig::renderWith (const Ovs& ov, const Script& script)
{
    begin (ov);
    return render (script);
}

std::string fmtG (double v)
{
    char b[48];
    std::snprintf (b, sizeof b, "%.6g", v);
    return b;
}

uint64_t fnv (const float* p, size_t n, uint64_t h)
{
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t u;
        std::memcpy (&u, p + i, 4);
        h = (h ^ u) * 1099511628211ull;
    }
    return h;
}
} // namespace paudit
