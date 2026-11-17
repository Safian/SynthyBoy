// SynthyBoy - sound-lab: scripted offline renders of one preset under a device profile.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_sound_lab JOBS.txt
//
// One job per line (blank lines and lines starting with # are skipped); words separated by blanks:
//   render OUT.wav PRESET.sbpreset PROFILE SECONDS [item]...
// PROFILE is the grammar of shell-mpc/device_profile.h with ';' for ',' ("low", "high;voices=16;control=economy"). Items, times in seconds:
//   on T KEY VEL           note on (velocity 0..1)        off T KEY              note off
//   press T KEY VALUE      poly pressure of a note (0..1; NoteExpression Pressure)
//   mw T VALUE             mod wheel                       bend T VALUE          pitch bend -1..1
//   set KEY VALUE          a parameter, before the note (after the preset is loaded; e.g. macro.1 0.5)
//   ramp KEY T0 T1 V0 V1   a parameter moved linearly between T0 and T1 (updated every block)
//   bpm VALUE              host tempo with a running transport
// The output is a 32 bit float stereo WAV at 44100 Hz, the raw engine output (the MPC shell adds its make-up gain after it). The engine runs as tier_bench runs it: 128 frame
// blocks, quality of the profile, offline (deterministic).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "device_profile.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

using namespace synthyboy;

namespace
{
constexpr int kBlock = 128;
constexpr double kRate = 44100.0;

struct Ramp
{
    uint32_t id = 0;
    double t0 = 0, t1 = 0, v0 = 0, v1 = 0;
};

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    const uint32_t frames = static_cast<uint32_t> (l.size());
    const uint32_t dataBytes = frames * 8;
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVEfmt ", 8); u32 (16); u16 (3); u16 (2); u32 (44100); u32 (44100 * 8); u16 (8); u16 (32);
    f.write ("data", 4); u32 (dataBytes);
    std::vector<float> inter (frames * 2);
    for (uint32_t i = 0; i < frames; ++i) { inter[2 * i] = l[i]; inter[2 * i + 1] = r[i]; }
    f.write (reinterpret_cast<const char*> (inter.data()), dataBytes);
}

bool doJob (const std::vector<std::string>& w)
{
    if (w.size() < 5 || w[0] != "render")
        return false;
    const std::string out = w[1], presetPath = w[2];
    std::string profileText = w[3];
    std::replace (profileText.begin(), profileText.end(), ';', ',');
    const double seconds = std::atof (w[4].c_str());

    synthyboy_mpc::Profile profile;
    std::string why;
    if (! synthyboy_mpc::parseProfile (profileText, profile, &why))
    {
        std::fprintf (stderr, "sound-lab: profile '%s': %s\n", profileText.c_str(), why.c_str());
        return false;
    }
    std::ifstream in (presetPath, std::ios::binary);
    const std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    PresetMeta meta;
    ParamValues values = ParamValues::defaults();
    StateContent content;
    if (loadPreset (text.data(), text.size(), meta, values, content).status != LoadStatus::ok)
    {
        std::fprintf (stderr, "sound-lab: %s does not load\n", presetPath.c_str());
        return false;
    }

    struct Timed { long frame; Event e; };
    std::vector<Timed> events;
    std::vector<Ramp> ramps;
    double bpm = 0;
    for (size_t i = 5; i < w.size();)
    {
        const std::string& k = w[i];
        auto num = [&] (size_t n) { return std::atof (w[i + n].c_str()); };
        auto frameOf = [&] (double t) { return static_cast<long> (std::llround (t * kRate)); };
        Event e;
        if (k == "on" && i + 3 < w.size())
        {
            e.type = Event::NoteOn; e.key = static_cast<int16_t> (num (2)); e.value = static_cast<float> (num (3)); e.noteId = static_cast<int32_t> (1000 + events.size());
            events.push_back ({ frameOf (num (1)), e }); i += 4;
        }
        else if (k == "off" && i + 2 < w.size())
        {
            e.type = Event::NoteOff; e.key = static_cast<int16_t> (num (2)); e.value = 0.0f;
            events.push_back ({ frameOf (num (1)), e }); i += 3;
        }
        else if (k == "press" && i + 3 < w.size())
        {
            e.type = Event::NoteExpression; e.expression = Event::Pressure; e.key = static_cast<int16_t> (num (2)); e.value = static_cast<float> (num (3));
            events.push_back ({ frameOf (num (1)), e }); i += 4;
        }
        else if (k == "mw" && i + 2 < w.size())
        {
            e.type = Event::ModWheel; e.value = static_cast<float> (num (2));
            events.push_back ({ frameOf (num (1)), e }); i += 3;
        }
        else if (k == "bend" && i + 2 < w.size())
        {
            e.type = Event::PitchBend; e.value = static_cast<float> (num (2));
            events.push_back ({ frameOf (num (1)), e }); i += 3;
        }
        else if (k == "set" && i + 2 < w.size())
        {
            const ParamInfo* p = findParamByKey (w[i + 1].c_str());
            if (p == nullptr) { std::fprintf (stderr, "sound-lab: unknown key %s\n", w[i + 1].c_str()); return false; }
            values.plain[static_cast<size_t> (findParamIndex (p->id))] = static_cast<float> (num (2));
            i += 3;
        }
        else if (k == "ramp" && i + 5 < w.size())
        {
            const ParamInfo* p = findParamByKey (w[i + 1].c_str());
            if (p == nullptr) { std::fprintf (stderr, "sound-lab: unknown key %s\n", w[i + 1].c_str()); return false; }
            ramps.push_back ({ p->id, num (2), num (3), num (4), num (5) }); i += 6;
        }
        else if (k == "bpm" && i + 1 < w.size())
        {
            bpm = num (1); i += 2;
        }
        else
        {
            std::fprintf (stderr, "sound-lab: bad item '%s'\n", k.c_str());
            return false;
        }
    }
    std::stable_sort (events.begin(), events.end(), [] (const Timed& a, const Timed& b) { return a.frame < b.frame; });

    // The files a preset names (content.wavetables / samples / impulses / instruments: pack id + file + SHA-256) are read from $SYNTHYBOY_CONTENT_DIR/<pack id>/<file>, the layout of
    // DirectoryContentStore; without the variable there is no store and a sample oscillator is silent, Convolve absent, a Preset table the saw (scripts/sound-lab.py builds such a folder
    // of links to content/factory-* itself). The store is declared before the engine: the engine keeps a pointer to it.
    std::unique_ptr<DirectoryContentStore> contentStore;
    if (const char* dir = std::getenv ("SYNTHYBOY_CONTENT_DIR"); dir != nullptr && dir[0] != '\0')
        contentStore = std::make_unique<DirectoryContentStore> (dir);

    auto engine = createEngine (EngineKind::own);
    if (engine == nullptr)
        return false;
    Quality q = synthyboy_mpc::profileQuality (profile);
    q.realtime = false;
    engine->prepare (kRate, kBlock, q);
    for (size_t i = 0; i < paramCount(); ++i)
        engine->setParam (paramTable()[i].id, values.plain[i]);
    if (contentStore != nullptr)
        engine->setContentStore (contentStore.get());
    engine->setContent (content);
    if (bpm > 0)
    {
        TransportInfo t;
        t.bpm = bpm; t.playing = true; t.songPosBeats = 0;
        engine->setTransport (t);
    }

    const long total = static_cast<long> (std::ceil (seconds * kRate / kBlock)) * kBlock;
    std::vector<float> L (static_cast<size_t> (total)), R (static_cast<size_t> (total));
    size_t next = 0;
    std::vector<Event> block;
    for (long pos = 0; pos < total; pos += kBlock)
    {
        block.clear();
        while (next < events.size() && events[next].frame < pos + kBlock)
        {
            Event e = events[next].e;
            e.frame = static_cast<int32_t> (std::max<long> (0, events[next].frame - pos));
            block.push_back (e);
            ++next;
        }
        const double t = static_cast<double> (pos) / kRate;
        for (const Ramp& r : ramps)
        {
            const double a = t <= r.t0 ? 0.0 : (t >= r.t1 ? 1.0 : (t - r.t0) / (r.t1 - r.t0));
            engine->setParam (r.id, static_cast<float> (r.v0 + a * (r.v1 - r.v0)));
        }
        if (bpm > 0)
        {
            TransportInfo ti;
            ti.bpm = bpm; ti.playing = true; ti.songPosBeats = t * bpm / 60.0;
            engine->setTransport (ti);
        }
        engine->process (block.empty() ? nullptr : block.data(), static_cast<int> (block.size()), L.data() + pos, R.data() + pos, kBlock);
    }
    writeWav (out, L, R);
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: synthyboy_sound_lab JOBS.txt\n");
        return 2;
    }
    std::ifstream jobs (argv[1]);
    std::string line;
    int failed = 0;
    while (std::getline (jobs, line))
    {
        std::istringstream ss (line);
        std::vector<std::string> w;
        for (std::string x; ss >> x;)
            w.push_back (x);
        if (w.empty() || w[0][0] == '#')
            continue;
        if (! doJob (w))
        {
            std::fprintf (stderr, "sound-lab: job failed: %s\n", line.c_str());
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}
