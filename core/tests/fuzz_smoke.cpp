// SynthyBoy - core tests: fuzz smoke (ctest target synthyboy_fuzz_smoke).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Garbage and mutated documents into every parser that takes bytes from outside: loadState / loadStateWithContent, loadPreset,
// readPresetMeta, the content pack manifest, importVitalPreset / .vitaltable / .vitallfo, the SCL / KBM / TUN parsers, the formula
// compiler and renderer, and the three WAV readers (fx-serum-b, sample-osc, wavetable import). Fixed seeds, bounded iteration counts
// (about 3 s native, under 30 s with ASan + UBSan): the point is that the build running with the sanitizers (CI core-sanitizers job)
// finds memory errors and undefined behaviour in the parsers on every push. The invariants checked on every document: no crash, the
// verdict of the plain and the content loader agree, a failed load leaves the target untouched, an accepted one is inside the
// parameter ranges. Longer runs: SYNTHYBOY_FUZZ_SCALE=10 (multiplies every iteration count).
//
// Seeds: the factory presets (content/presets), a state of defaults, a synthesised Vital preset, small valid documents of every format.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "fixtures.h"
#include "synthyboy/content_pack.h"
#include "synthyboy/engine.h"
#include "synthyboy/formula.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"
#include "synthyboy/tuning.h"
#include "synthyboy/vital_import.h"
#include "synthyboy/wavetable_edit.h"
#include "synthyboy/sampleosc/wav_reader.h"
#include "test_runner.h"
#include "test_util.h"
#include "wav_reader.h" // fx-serum-b

using namespace synthyboy;

namespace
{
int scaled (int n)
{
    static const int scale = [] {
        const char* s = std::getenv ("SYNTHYBOY_FUZZ_SCALE");
        const int v = s != nullptr ? std::atoi (s) : 1;
        return v >= 1 && v <= 1000 ? v : 1;
    }();
    return n * scale;
}

std::string mutate (const std::string& seed, sbtest::Rng& rng)
{
    std::string s = seed;
    // half of the documents get one or two edits (they stay near-valid and reach deep into the parsers), the rest up to six
    const int ops = 1 + static_cast<int> (rng.u32() % (rng.u32() % 2 ? 2 : 6));
    for (int k = 0; k < ops; ++k)
    {
        if (s.empty())
        {
            s.push_back (static_cast<char> (rng.u32()));
            continue;
        }
        const size_t pos = rng.u32() % s.size();
        switch (rng.u32() % 9)
        {
            case 0: s[pos] = static_cast<char> (rng.u32()); break;
            case 1: s[pos] = static_cast<char> (s[pos] ^ (1 << (rng.u32() % 8))); break;
            case 2: s.erase (pos, 1 + rng.u32() % 24); break;
            case 3: { static const std::string kStruct = "{}[]\",:\\0-9.eE+ \n\t"; s.insert (pos, 1, kStruct[rng.u32() % kStruct.size()]); break; }
            case 4: s.resize (pos); break;
            case 5: { const size_t n = std::min<size_t> (s.size() - pos, 1 + rng.u32() % 200); s.insert (pos, s.substr (pos, n)); break; }
            case 6: s.insert (pos, rng.u32() % 2 ? "1e999" : "\"\\ud800\""); break;
            case 7: s.insert (pos, std::string (1 + rng.u32() % 30, "[{\"("[rng.u32() % 4])); break;
            default: s.insert (pos, rng.u32() % 2 ? "-1" : "99999999999999999999"); break;
        }
    }
    return s;
}

std::string garbage (sbtest::Rng& rng, bool jsonish)
{
    std::string s (rng.u32() % 300, '\0');
    for (auto& c : s)
        c = static_cast<char> (rng.u32());
    return jsonish ? "{" + s : s;
}

std::string readFile (const std::filesystem::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return std::string ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

/** Every 6th factory preset (a spread over the categories), as text. */
const std::vector<std::string>& presetSeeds()
{
    static const std::vector<std::string> seeds = [] {
        std::vector<std::string> v;
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        for (const auto& e : std::filesystem::recursive_directory_iterator (std::filesystem::path (SYNTHYBOY_FUZZ_SOURCE_DIR) / "content" / "presets", ec))
            if (e.is_regular_file() && e.path().extension() == ".sbpreset")
                files.push_back (e.path());
        std::sort (files.begin(), files.end());
        for (size_t i = 0; i < files.size(); i += 6)
            v.push_back (readFile (files[i]));
        PresetMeta meta = PresetMeta::initDefault();
        v.push_back (savePreset (meta, ParamValues::defaults()));
        return v;
    }();
    return seeds;
}

bool inRange (const ParamValues& v)
{
    for (size_t i = 0; i < paramCount(); ++i)
        if (! (v.plain[i] >= paramTable()[i].min && v.plain[i] <= paramTable()[i].max))
            return false;
    return true;
}

bool sameValues (const ParamValues& a, const ParamValues& b)
{
    for (size_t i = 0; i < paramCount(); ++i)
        if (a.plain[i] != b.plain[i])
            return false;
    return true;
}

std::vector<uint8_t> bytes (const std::string& s)
{
    return std::vector<uint8_t> (s.begin(), s.end());
}
} // namespace

TEST (fuzz_state_and_preset_loaders)
{
    sbtest::Rng rng (0xf0220001);
    const auto& presets = presetSeeds();
    CHECK (presets.size() >= 10);
    // state documents: the states inside the preset seeds, plus the defaults
    std::vector<std::string> stateSeeds { saveState (ParamValues::defaults()) };
    for (const auto& p : presets)
    {
        PresetMeta m;
        auto v = ParamValues::defaults();
        StateContent c;
        if (loadPreset (p.data(), p.size(), m, v, c).status == LoadStatus::ok)
            stateSeeds.push_back (saveState (v, c));
    }
    int ok = 0, bad = 0;

    for (int n = 0; n < scaled (900); ++n)
    {
        std::string doc;
        switch (n % 6)
        {
            case 0: doc = garbage (rng, true); break;
            case 1: doc = garbage (rng, false); break;
            case 2: case 3: doc = mutate (stateSeeds[rng.u32() % stateSeeds.size()], rng); break;
            default: doc = mutate (presets[rng.u32() % presets.size()], rng); break;
        }

        auto before = ParamValues::defaults();
        before.plain[static_cast<size_t> (findParamIndex (findParamByKey ("master.volume")->id))] = -33.0f;

        // loadState and loadStateWithContent agree about the same bytes
        auto v1 = before, v2 = before;
        StateContent content;
        const auto r1 = loadState (doc.data(), doc.size(), v1);
        const auto r2 = loadStateWithContent (doc.data(), doc.size(), v2, content);
        CHECK (r1.status == r2.status);
        CHECK (sameValues (v1, v2));
        if (r1.status == LoadStatus::ok)
        {
            CHECK (inRange (v1));
            CHECK (validateContent (content));
            ++ok;
        }
        else
        {
            CHECK (sameValues (v1, before));
            ++bad;
        }

        // the same bytes as a preset
        auto v3 = before;
        PresetMeta meta;
        StateContent content2;
        const auto r3 = loadPreset (doc.data(), doc.size(), meta, v3, content2);
        if (r3.status == LoadStatus::ok)
            CHECK (inRange (v3) && validateContent (content2));
        else
            CHECK (sameValues (v3, before));
        PresetMeta meta2;
        auto v4 = before;
        (void) loadPreset (doc.data(), doc.size(), meta2, v4);
        (void) readPresetMeta (doc.data(), doc.size(), meta2);
    }
    std::printf ("    state / preset: %d accepted, %d rejected\n", ok, bad);
    CHECK (ok > 20 && bad > 50); // the mutations reach both outcomes
}

TEST (fuzz_content_pack_manifest)
{
    sbtest::Rng rng (0xf0220002);
    std::vector<std::string> seeds { readFile (std::filesystem::path (SYNTHYBOY_FUZZ_SOURCE_DIR) / "content" / "presets" / "contentpack.json") };
    CHECK (! seeds[0].empty());
    int ok = 0;
    for (int n = 0; n < scaled (600); ++n)
    {
        const std::string doc = n % 5 == 0 ? garbage (rng, true) : mutate (seeds[0], rng);
        ContentPackInfo info;
        if (parseContentPackManifest (doc.data(), doc.size(), info))
        {
            ++ok;
            (void) serializeContentPackManifest (info); // a parsed manifest serialises
        }
    }
    CHECK (ok > 0);
}

TEST (fuzz_vital_import)
{
    sbtest::Rng rng (0xf0220003);
    using namespace sbfx;
    std::vector<std::string> seeds;
    seeds.push_back (defaultPatch().json());
    {
        Fixture f = defaultPatch();
        f.mods.emplace_back ("lfo_1", "filter_1_cutoff");
        f.wavetables = "[" + wavetable ("T", { component ("Wave Source", "\"interpolation\":1", { waveKeyframe (0, sineWave (1)), waveKeyframe (3, sineWave (2)) }) }) + "]";
        f.lfos = "[" + lineObject ({ { 0, 1 }, { 0.5, 0.2 }, { 1, 0 } }, { 1, -1, 0 }, true) + "]";
        seeds.push_back (f.json());
    }
    const std::string table = wavetable ("T", { component ("Wave Source", "", { waveKeyframe (0, sineWave (1)), waveKeyframe (4, sineWave (3)) }) });
    const std::string lfo = lineObject ({ { 0, 1 }, { 1, 0 } }, { 0, 0 }, false);

    ImportOptions o;
    o.limits.deadlineMs = 3000;
    int ok = 0;
    for (int n = 0; n < scaled (150); ++n)
    {
        const std::string pick = n % 3 == 0 ? table : (n % 3 == 1 ? lfo : seeds[rng.u32() % seeds.size()]);
        const std::string doc = n % 10 == 9 ? garbage (rng, true) : mutate (pick, rng);
        (void) looksLikeVitalPreset (doc.data(), doc.size());
        ImportResult r;
        switch (n % 3)
        {
            case 0: r = importVitalWavetableFile (doc.data(), doc.size(), o); break;
            case 1: r = importVitalLfoFile (doc.data(), doc.size(), o); break;
            default: r = importVitalPreset (doc.data(), doc.size(), o); break;
        }
        if (r.status == ImportStatus::ok)
        {
            ++ok;
            CHECK (inRange (r.values));
        }
    }
    CHECK (ok > 5);
}

TEST (fuzz_tuning_parsers)
{
    sbtest::Rng rng (0xf0220004);
    const std::vector<std::string> seeds {
        "! 12tet.scl\n!\n12 EDO\n 12\n!\n 100.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n 2/1\n",
        "! ji.scl\nJust\n 5\n 9/8\n 5/4\n 4/3\n 3/2\n 2/1\n",
        "! map.kbm\n12\n0\n127\n60\n69\n440.0\n12\n0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n",
        "; sample.tun\n[Tuning]\nnote0=0\nnote1=100\nnote60=6000\nnote127=12700\n[Exact Tuning]\nnote0=1200.0\n",
    };
    for (int n = 0; n < scaled (1500); ++n)
    {
        const std::string doc = n % 8 == 0 ? garbage (rng, false) : mutate (seeds[rng.u32() % seeds.size()], rng);
        tuning::Tuning t;
        (void) t.loadScl (doc.data(), doc.size());
        (void) t.loadKbm (doc.data(), doc.size());
        (void) t.loadTun (doc.data(), doc.size());
        const auto& table = t.table();
        for (size_t k = 0; k < table.size(); ++k)
            CHECK (std::isfinite (table[k]) && table[k] >= 0.0); // 0: an unmapped key
        (void) t.toScl();
        (void) t.toKbm();
        (void) t.toTun();
        tuning::Tuning scl, kbm, tun;
        (void) tuning::parseScl (doc, scl);
        (void) tuning::parseKbm (doc, kbm);
        (void) tuning::parseTun (doc, tun);
    }
}

TEST (fuzz_formula_compile_and_render)
{
    sbtest::Rng rng (0xf0220005);
    const std::vector<std::string> seeds {
        "sin(2*pi*x)", "sin(pi*x*2) * (1 - y) + rint(x*3) * y", "(x < 0.5) ? 1 : -1", "q < 9 ? 1/q : 0", "((q%2)==1)?in:0",
        "sin(pi*x*rint(1 + z*7)) / (1 + abs(sin(w))) + in", "x ^ 3 * sign(sin(x * 50)) + rand * 0.1",
        "min(max(x * 4, -1), 1) + tanh(x*5) + sel", "x ^ 2 ^ 3 % 0.5 + 1/0 - 0/0 + sum(x,y,z) + avg(1,2) + log(0) + acosh(0) + atanh(x*9)",
    };
    for (const auto& seed : seeds)
    {
        formula::Formula f;
        const auto e = formula::Formula::compile (seed, f);
        if (! CHECK (e.ok))
            std::printf ("    seed does not compile: %s (at %zu: %s)\n", seed.c_str(), e.position, e.message);
    }
    std::vector<float> out (static_cast<size_t> (2) * 2048 + 8);
    std::vector<float> in (out.size(), 0.25f);
    int compiled = 0;
    for (int n = 0; n < scaled (1500); ++n)
    {
        const std::string doc = n % 10 == 0 ? garbage (rng, false) : mutate (seeds[rng.u32() % seeds.size()], rng);
        formula::Formula f;
        if (formula::Formula::compile (doc, f))
        {
            ++compiled;
            const auto e = formula::renderFrames (f, 2, out.data(), in.data(), static_cast<int> (rng.u32() % 2));
            if (e.ok)
                for (size_t i = 0; i < 2 * 2048; ++i)
                    CHECK (std::isfinite (out[i]));
        }
    }
    std::printf ("    formula: %d of %d compiled\n", compiled, scaled (1500));
    CHECK (compiled > 100);
}

TEST (fuzz_wav_readers)
{
    sbtest::Rng rng (0xf0220006);
    // valid WAVs of every format the readers take, as seeds
    std::vector<std::vector<uint8_t>> seeds;
    std::vector<float> pcm (2 * 3000);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = 0.5f * std::sin (static_cast<float> (i) * 0.05f);
    for (int depth : { 16, 24, 32 })
        for (int ch : { 1, 2 })
        {
            std::vector<uint8_t> w;
            CHECK (fx::writeWav (w, pcm.data(), 3000, ch, 44100, depth, depth == 32 && ch == 2));
            seeds.push_back (w);
        }

    wtedit::ImportOptions modes[4];
    modes[0].mode = wtedit::ImportMode::ByFrameSize;
    modes[1].mode = wtedit::ImportMode::PitchAverage;
    modes[2].mode = wtedit::ImportMode::Fft256;
    modes[3].mode = wtedit::ImportMode::PitchDetectFollow;
    modes[3].normalize = true;

    int decoded = 0;
    for (int n = 0; n < scaled (900); ++n)
    {
        std::vector<uint8_t> w;
        if (n % 9 == 0)
        {
            const auto g = garbage (rng, false);
            w = bytes ("RIFF" + g.substr (0, 4) + "WAVEfmt " + g);
        }
        else
        {
            // the byte-level mutations on the bytes of a valid file; the header (first 64 bytes) takes most of the hits
            const auto& seed = seeds[rng.u32() % seeds.size()];
            std::string s (seed.begin(), seed.end());
            const int edits = 1 + static_cast<int> (rng.u32() % 4);
            for (int e = 0; e < edits; ++e)
            {
                const size_t pos = rng.u32() % 3 == 0 ? rng.u32() % s.size() : rng.u32() % std::min<size_t> (s.size(), 64);
                switch (rng.u32() % 4)
                {
                    case 0: s[pos] = static_cast<char> (rng.u32()); break;
                    case 1: s[pos] = static_cast<char> (s[pos] ^ (1 << (rng.u32() % 8))); break;
                    case 2: s.resize (pos); break;
                    default: s.insert (pos, std::string (1 + rng.u32() % 8, static_cast<char> (rng.u32()))); break;
                }
                if (s.empty())
                    s.push_back (0);
            }
            w = bytes (s);
        }

        fx::WavAudio a;
        if (fx::readWav (w.data(), w.size(), a))
        {
            ++decoded;
            CHECK (a.samples.size() == static_cast<size_t> (a.totalFrames) * static_cast<size_t> (a.channels));
        }

        sampleosc::WavInfo info;
        (void) sampleosc::readWavInfo (w.data(), w.size(), info);
        std::vector<float> l, r;
        sampleosc::WavInfo info2;
        if (sampleosc::readWav (w.data(), w.size(), l, r, info2, "Fuzz C#3.wav"))
            CHECK (l.size() == r.size() || r.empty());

        wtedit::Wavetable wt;
        std::string err;
        (void) wtedit::importAudioWav (w.data(), w.size(), modes[n % 4], wt, &err);
    }
    CHECK (decoded > 20);
}

TEST (fuzz_garbled_state_through_the_engines)
{
    // a loaded document is clamped into the table's ranges; the engines must render it to finite samples
    sbtest::Rng rng (0xf0220007);
    const auto& presets = presetSeeds();
    for (EngineKind kind : { EngineKind::own, EngineKind::vital })
    {
        if (! isEngineAvailable (kind))
            continue;
        auto engine = createEngine (kind);
        CHECK (engine != nullptr);
        if (engine == nullptr)
            continue;
        Quality q = qualityForTier (Tier::low);
        engine->prepare (48000.0, 256, q);
        int rendered = 0;
        for (int n = 0; n < scaled (24); ++n)
        {
            // the numeric values of a preset are replaced by random numbers of the same text shape: ranges get clamped
            std::string doc = mutate (presets[rng.u32() % presets.size()], rng);
            auto v = ParamValues::defaults();
            if (loadState (doc.data(), doc.size(), v).status != LoadStatus::ok)
            {
                for (size_t i = 0; i < paramCount(); ++i)
                {
                    const auto& p = paramTable()[i];
                    v.plain[i] = p.min + static_cast<float> (rng.uniform()) * (p.max - p.min);
                }
            }
            engine->reset();
            for (size_t i = 0; i < paramCount(); ++i)
                engine->setParam (paramTable()[i].id, v.plain[i]);
            engine->setParam (kParamMasterPolyphony, 4.0f);
            std::vector<Event> ev { sbtest::noteOn (0, 48 + static_cast<int> (rng.u32() % 36), 0.8f),
                                    sbtest::noteOn (0, 60 + static_cast<int> (rng.u32() % 36), 1.0f) };
            std::vector<float> l (256), r (256);
            for (int b = 0; b < 3; ++b)
            {
                engine->process (b == 0 ? ev.data() : nullptr, b == 0 ? 2 : 0, l.data(), r.data(), 256);
                for (int i = 0; i < 256; ++i)
                    CHECK (std::isfinite (l[static_cast<size_t> (i)]) && std::isfinite (r[static_cast<size_t> (i)]));
            }
            ++rendered;
        }
        CHECK (rendered > 0);
    }
}
