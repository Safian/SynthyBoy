// SynthyBoy - ref-compare: renders imported presets (.sbpreset) offline with the note script of the reference renderer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_ref_render --manifest FILE [--user-dir DIR] [--engine vital|own] [--tier low|medium|high] [--sr 48000]
//
// --user-dir: the directory that holds content/ (the importer's --content-dir is DIR/content).
// The manifest has one render per line, tab separated: ID  PRESET.sbpreset  NOTES  LENGTH  OUT.wav  [OVERRIDES]
//   OVERRIDES  optional, ';' separated edits of the loaded parameters (the deliberately broken variants of the calibration): key=plain, key+=delta, key*=factor
//   NOTES   "note:velocity:startSec:holdSec" separated by commas (the format of synthyboy_serum_reference --notes), velocity 0 .. 127
//   LENGTH  seconds of audio to render
// Per line: a fresh engine, the state loaded the way the shell loads it (every parameter, then the content), blocks of 512 frames, the note events at their exact sample
// (hold and start are rounded to samples like the reference renderer does), mod wheel 0, pitch bend centre, a 32-bit float stereo WAV. Offline = deterministic (Quality::realtime false).
// Prints "ok ID peak" or "FAIL ID reason" per line. Exit code 1 if one failed.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/quality.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"

using namespace synthyboy;

namespace
{
constexpr int kBlock = 512;

struct Note
{
    int note = 60, velocity = 100;
    double start = 0.0, hold = 2.5;
};

std::vector<std::string> split (const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in (s);
    while (std::getline (in, cur, sep)) out.push_back (cur);
    return out;
}

bool parseNotes (const std::string& spec, std::vector<Note>& out)
{
    for (const auto& item : split (spec, ','))
    {
        const auto f = split (item, ':');
        if (f.size() != 4) return false;
        out.push_back ({ std::atoi (f[0].c_str()), std::atoi (f[1].c_str()), std::atof (f[2].c_str()), std::atof (f[3].c_str()) });
    }
    return ! out.empty();
}

std::string readFile (const std::filesystem::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return std::string ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

void put32 (std::ofstream& f, uint32_t v) { for (int i = 0; i < 4; ++i) f.put (static_cast<char> ((v >> (8 * i)) & 0xff)); }
void put16 (std::ofstream& f, uint16_t v) { for (int i = 0; i < 2; ++i) f.put (static_cast<char> ((v >> (8 * i)) & 0xff)); }

bool writeWav (const std::filesystem::path& p, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    std::error_code ec;
    std::filesystem::create_directories (p.parent_path(), ec);
    std::ofstream f (p, std::ios::binary | std::ios::trunc);
    if (! f) return false;
    const uint32_t frames = static_cast<uint32_t> (l.size()), bytes = frames * 8;
    f.write ("RIFF", 4); put32 (f, 36 + bytes); f.write ("WAVEfmt ", 8); put32 (f, 16); put16 (f, 3); put16 (f, 2);
    put32 (f, static_cast<uint32_t> (sr)); put32 (f, static_cast<uint32_t> (sr) * 8); put16 (f, 8); put16 (f, 32);
    f.write ("data", 4); put32 (f, bytes);
    for (uint32_t i = 0; i < frames; ++i) { f.write (reinterpret_cast<const char*> (&l[i]), 4); f.write (reinterpret_cast<const char*> (&r[i]), 4); }
    return static_cast<bool> (f);
}
} // namespace

int main (int argc, char** argv)
{
    std::string manifest, contentDir, engineName = "vital", tierName = "high";
    int sr = 48000;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--manifest") manifest = next();
        else if (a == "--user-dir" || a == "--content-dir") contentDir = next();
        else if (a == "--engine") engineName = next();
        else if (a == "--tier") tierName = next();
        else if (a == "--sr") sr = std::atoi (next().c_str());
        else manifest.clear();
    }
    if (manifest.empty())
    {
        std::fprintf (stderr, "usage: synthyboy_ref_render --manifest FILE [--user-dir DIR] [--engine vital|own] [--tier low|medium|high] [--sr 48000]\n");
        return 2;
    }
    const EngineKind kind = engineName == "own" ? EngineKind::own : EngineKind::vital;
    const Tier tier = tierName == "low" ? Tier::low : tierName == "medium" ? Tier::medium : Tier::high;
    const Quality q = qualityForTier (tier);
    std::unique_ptr<UserContentStore> store;   // the tables, samples and instruments the importer stored below <DIR>/content
    if (! contentDir.empty() && std::filesystem::is_directory (contentDir)) store = std::make_unique<UserContentStore> (contentDir);

    int failures = 0;
    std::ifstream in (manifest);
    std::string line;
    while (std::getline (in, line))
    {
        if (line.empty() || line[0] == '#') continue;
        const auto f = split (line, '\t');
        if (f.size() != 5 && f.size() != 6) { std::printf ("FAIL ? bad manifest line\n"); ++failures; continue; }
        const std::string& id = f[0];
        PresetMeta meta;
        ParamValues values = ParamValues::defaults();
        StateContent content;
        const std::string text = readFile (f[1]);
        if (text.empty() || loadPreset (text.data(), text.size(), meta, values, content).status != LoadStatus::ok) { std::printf ("FAIL %s preset does not load\n", id.c_str()); ++failures; continue; }
        if (f.size() == 6)
        {
            bool bad = false;
            for (const auto& ov : split (f[5], ';'))
            {
                const size_t eq = ov.find ('=');
                if (eq == std::string::npos || eq == 0) { bad = true; break; }
                char mode = '=';
                std::string key = ov.substr (0, eq);
                if (key.size() > 1 && (key.back() == '+' || key.back() == '*')) { mode = key.back(); key.pop_back(); }
                const ParamInfo* info = findParamByKey (key.c_str());
                const int idx = info != nullptr ? findParamIndex (info->id) : -1;
                if (idx < 0) { bad = true; break; }
                const float v = static_cast<float> (std::atof (ov.c_str() + eq + 1));
                float& cur = values.plain[static_cast<size_t> (idx)];
                cur = mode == '+' ? cur + v : mode == '*' ? cur * v : v;
                cur = std::min (std::max (cur, info->min), info->max);
            }
            if (bad) { std::printf ("FAIL %s bad override\n", id.c_str()); ++failures; continue; }
        }
        std::vector<Note> notes;
        if (! parseNotes (f[2], notes)) { std::printf ("FAIL %s bad note spec\n", id.c_str()); ++failures; continue; }
        auto engine = createEngine (kind);
        if (engine == nullptr) { std::printf ("FAIL %s engine not built in\n", id.c_str()); ++failures; continue; }
        engine->prepare (static_cast<double> (sr), kBlock, q);
        engine->setContentStore (store.get());
        for (size_t i = 0; i < paramCount(); ++i) engine->setParam (paramTable()[i].id, values.plain[i]);
        engine->setContent (content);
        engine->reset();   // the loaded values are the targets: the smoothers start there (the render of the first note must not hear a parameter ramp from the defaults)

        const int total = static_cast<int> (std::llround (std::atof (f[3].c_str()) * sr));
        std::vector<float> L (static_cast<size_t> (total)), R (static_cast<size_t> (total));
        std::vector<float> l (kBlock), r (kBlock);
        auto sampleOf = [sr] (double sec) { return static_cast<int> (std::llround (sec * sr)); };
        double peak = 0.0;
        bool finite = true;
        int noteId = 1;
        std::vector<int> ids (notes.size());
        for (auto& id2 : ids) id2 = noteId++;
        for (int base = 0; base < total; base += kBlock)
        {
            std::vector<Event> ev;
            for (size_t k = 0; k < notes.size(); ++k)
            {
                const int on = sampleOf (notes[k].start), off = sampleOf (notes[k].start + notes[k].hold);
                if (on >= base && on < base + kBlock)
                {
                    Event e;
                    e.frame = on - base; e.type = Event::NoteOn; e.channel = 0; e.key = static_cast<int16_t> (notes[k].note);
                    e.value = static_cast<float> (notes[k].velocity) / 127.0f; e.noteId = ids[k];
                    ev.push_back (e);
                }
                if (off >= base && off < base + kBlock)
                {
                    Event e;
                    e.frame = off - base; e.type = Event::NoteOff; e.channel = 0; e.key = static_cast<int16_t> (notes[k].note); e.value = 0.0f; e.noteId = ids[k];
                    ev.push_back (e);
                }
            }
            std::stable_sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) { return a.frame < b.frame; });
            engine->process (ev.empty() ? nullptr : ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), kBlock);
            for (int i = 0; i < kBlock && base + i < total; ++i)
            {
                float a = l[static_cast<size_t> (i)], c = r[static_cast<size_t> (i)];
                if (! std::isfinite (a) || ! std::isfinite (c)) { finite = false; a = c = 0.0f; }
                L[static_cast<size_t> (base + i)] = a;
                R[static_cast<size_t> (base + i)] = c;
                peak = std::max ({ peak, static_cast<double> (std::fabs (a)), static_cast<double> (std::fabs (c)) });
            }
        }
        if (! writeWav (f[4], L, R, sr)) { std::printf ("FAIL %s cannot write %s\n", id.c_str(), f[4].c_str()); ++failures; continue; }
        if (! finite) { std::printf ("FAIL %s non-finite samples\n", id.c_str()); ++failures; continue; }
        std::printf ("ok %s %.4f\n", id.c_str(), peak);
        std::fflush (stdout);
    }
    return failures ? 1 : 0;
}
