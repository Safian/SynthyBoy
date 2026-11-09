// SynthyBoy - core/serum-import: the content a Serum 2 preset carries or references (tables, samples, multisamples) and the audio helpers.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstring>

#include "serum_importer.h"
#include "sha256.h"
#include "synthyboy/user_content.h"
#include "vital_content.h"
#include "vital_import_internal.h"

namespace synthyboy
{
namespace serumimport
{
namespace
{
constexpr int kCycle = 2048;
constexpr int kMaxFrames = 256;

std::string normalisePath (std::string p)
{
    for (char& c : p)
        if (c == '\\') c = '/';
    while (! p.empty() && p.front() == '/') p.erase (p.begin());
    return p;
}

std::string dirOf (const std::string& p)
{
    const size_t s = p.find_last_of ('/');
    return s == std::string::npos ? std::string() : p.substr (0, s);
}

std::string stemOf (const std::string& p)
{
    std::string f = p.substr (p.find_last_of ('/') == std::string::npos ? 0 : p.find_last_of ('/') + 1);
    const size_t dot = f.find_last_of ('.');
    return dot == std::string::npos ? f : f.substr (0, dot);
}

/** One cycle of 2048 samples from a buffer of any length (linear interpolation over the whole buffer). */
void resampleCycle (const std::vector<float>& in, std::vector<float>& out)
{
    out.assign (kCycle, 0.0f);
    if (in.empty()) return;
    for (int i = 0; i < kCycle; ++i)
    {
        const double pos = static_cast<double> (i) * static_cast<double> (in.size()) / kCycle;
        const size_t a = std::min (in.size() - 1, static_cast<size_t> (pos));
        const size_t b = (a + 1) % in.size();
        const double f = pos - std::floor (pos);
        out[static_cast<size_t> (i)] = static_cast<float> (in[a] * (1.0 - f) + in[b] * f);
    }
}
} // namespace

bool Importer::readContent (const char* area, const std::string& rel, std::vector<uint8_t>& bytes)
{
    if (! opt.resolver) return false;
    bytes.clear();
    return opt.resolver->read (area, normalisePath (rel), bytes) && ! bytes.empty();
}

bool Importer::loadTableFile (const std::string& relPath, Table& out)
{
    std::vector<uint8_t> bytes;
    if (! readContent ("Tables", relPath, bytes)) return false;
    DecodedAudio a;
    if (! decodeAudio (bytes.data(), bytes.size(), opt.limits.maxAudioFrames, a) || a.frames() == 0) return false;
    std::vector<float> mono (a.frames());
    for (size_t i = 0; i < mono.size(); ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < a.channels; ++c) s += a.data[i * static_cast<size_t> (a.channels) + static_cast<size_t> (c)];
        mono[i] = s / static_cast<float> (a.channels);
    }
    out.name = stemOf (normalisePath (relPath));
    if (mono.size() >= kCycle && mono.size() % kCycle == 0)
    {
        out.numFrames = static_cast<int> (std::min<size_t> (kMaxFrames, mono.size() / kCycle));
        out.frames.assign (mono.begin(), mono.begin() + static_cast<std::ptrdiff_t> (out.numFrames) * kCycle);
    }
    else
    {
        resampleCycle (mono, out.frames);
        out.numFrames = 1;
    }
    return true;
}

bool Importer::tableFromEmbedded (int node, int, Table& out)
{
    const uint32_t count = d.arraySize (static_cast<uint32_t> (node));
    if (count == 0) return false;
    std::vector<float> data (count);
    for (uint32_t k = 0; k < count; ++k)
    {
        const double v = d.arrayNumber (static_cast<uint32_t> (node), k);
        data[k] = std::isfinite (v) ? static_cast<float> (v) : 0.0f;
    }
    if (count >= kCycle && count % kCycle == 0)
    {
        out.numFrames = static_cast<int> (std::min<uint32_t> (kMaxFrames, count / kCycle));
        out.frames.assign (data.begin(), data.begin() + static_cast<std::ptrdiff_t> (out.numFrames) * kCycle);
    }
    else
    {
        resampleCycle (data, out.frames);
        out.numFrames = 1;
    }
    return true;
}

void Importer::attachTable (int osc, const Table& t, const std::string& area, const std::string& path, bool fromEmbedded)
{
    const std::string key = "osc" + std::to_string (osc) + ".wavetable";
    if (t.numFrames <= 0 || t.frames.empty()) return;
    // a single cycle that is one of the built-in shapes needs no stored table
    const int single = t.numFrames == 1 ? vitalimport::matchBuiltinSingleCycle (t.frames.data(), 1) : -1;
    if (single >= 0)
    {
        store (key, single);
        entry (ItemKind::imported, Reason::exact, "content", path, "", key, "built-in", "table_is_a_built_in_shape", 0);
        return;
    }
    if (! opt.customWavetables)
    {
        double dist = 0.0;
        const int idx = vitalimport::nearestBuiltinWavetable (t.frames.data(), t.numFrames, dist);
        store (key, idx);
        entry (ItemKind::approximated, Reason::nearestWavetable, "content", path, "", key, "built-in", "custom_tables_unsupported_nearest_built_in", 0);
        return;
    }
    ImportedWavetable w;
    w.osc = osc;
    w.name = t.name.empty() ? (fromEmbedded ? "Serum embedded table" : "Serum table") : t.name;
    w.numFrames = t.numFrames;
    w.frames.resize (t.frames.size());
    for (size_t i = 0; i < t.frames.size(); ++i)
        w.frames[i] = static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, t.frames[i])) * 32767.0f));
    w.wavBytes = vitalimport::createWav16 (w.frames.data(), nullptr, w.frames.size(), 44100, 1);
    w.sha256 = Sha256::hashBufferHex (w.wavBytes.data(), w.wavBytes.size());
    store (key, kWavetablePresetTable);
    R.wavetables.push_back (std::move (w));
    (void) area;
}

bool Importer::loadSample (const std::string& area, const std::string& relPath, const std::string&, int osc, const std::string& path, int baseNote, int* framesOut, int* rateOut,
                           std::vector<float>*)
{
    const std::string okey = "osc" + std::to_string (osc);
    std::vector<uint8_t> bytes;
    const std::string rel = normalisePath (relPath);
    if (! opt.customSamples)
    {
        entry (ItemKind::dropped, Reason::customSample, "content", path, rel, okey + ".type", "Wavetable", "sample_oscillator_unsupported_by_the_engine", 0);
        store (okey + ".type", 0);
        return false;
    }
    DecodedAudio a;
    if (! readContent (area.c_str(), rel, bytes) || ! decodeAudio (bytes.data(), bytes.size(), opt.limits.maxAudioFrames, a) || a.frames() == 0)
    {
        ++R.missingFiles;
        store (okey + ".type", 0);
        entry (ItemKind::dropped, Reason::renderFallback, "content", path, rel, okey + ".type", "Wavetable", "sample_file_not_found_wavetable_oscillator_used", 0);
        return false;
    }
    ImportedSample s;
    s.osc = osc;
    s.name = stemOf (rel);
    const size_t n = a.frames();
    const int ch = a.channels >= 2 ? 2 : 1;
    std::vector<float> left (n), right (ch == 2 ? n : 0);
    for (size_t i = 0; i < n; ++i)
    {
        left[i] = a.data[i * static_cast<size_t> (a.channels)];
        if (ch == 2) right[i] = a.data[i * static_cast<size_t> (a.channels) + 1];
    }
    s.sampleRate = a.sampleRate;
    s.numFrames = static_cast<int> (n);
    s.channels = ch;
    s.wavBytes = encodeWav (left.data(), ch == 2 ? right.data() : nullptr, n, a.sampleRate, 24);
    s.sha256 = Sha256::hashBufferHex (s.wavBytes.data(), s.wavBytes.size());
    R.samples.erase (std::remove_if (R.samples.begin(), R.samples.end(), [osc] (const ImportedSample& x) { return x.osc == osc; }), R.samples.end());
    R.samples.push_back (std::move (s));
    store (okey + ".type", 1);
    store (okey + ".sample.root", std::max (0, std::min (127, baseNote)));
    if (framesOut) *framesOut = static_cast<int> (n);
    if (rateOut) *rateOut = a.sampleRate;
    entry (ItemKind::imported, Reason::exact, "content", path, rel, okey + ".type", "Sample", "sample_file_stored_in_the_library", 0);
    return true;
}

bool Importer::noiseSampleFromFile (const std::string& relPath, const std::string& path)
{
    std::vector<uint8_t> bytes;
    const std::string rel = normalisePath (relPath);
    DecodedAudio a;
    if (! readContent ("Noises", rel, bytes) || ! decodeAudio (bytes.data(), bytes.size(), opt.limits.maxAudioFrames, a) || a.frames() == 0)
    {
        ++R.missingFiles;
        return false;
    }
    ImportedSample s;
    s.osc = kNoiseSampleOsc;
    s.name = stemOf (rel);
    const size_t n = a.frames();
    const int ch = a.channels >= 2 ? 2 : 1;
    std::vector<float> left (n), right (ch == 2 ? n : 0);
    for (size_t i = 0; i < n; ++i)
    {
        left[i] = a.data[i * static_cast<size_t> (a.channels)];
        if (ch == 2) right[i] = a.data[i * static_cast<size_t> (a.channels) + 1];
    }
    s.sampleRate = a.sampleRate;
    s.numFrames = static_cast<int> (n);
    s.channels = ch;
    s.wavBytes = encodeWav (left.data(), ch == 2 ? right.data() : nullptr, n, a.sampleRate, 24);
    s.sha256 = Sha256::hashBufferHex (s.wavBytes.data(), s.wavBytes.size());
    R.samples.erase (std::remove_if (R.samples.begin(), R.samples.end(), [] (const ImportedSample& x) { return x.osc == kNoiseSampleOsc; }), R.samples.end());
    R.samples.push_back (std::move (s));
    store ("noise.type", 1);
    entry (ItemKind::imported, Reason::exact, "content", path, rel, "noise.type", "Sample", "noise_sample_file_stored_in_the_library");
    return true;
}

// embeddedNoiseData: a list with one array of samples per channel (numChannels, numFrames, sampleRate beside it)
bool Importer::noiseSampleFromEmbedded (int embNode, int metaNode, const std::string& path)
{
    const uint32_t channelCount = d.arraySize (static_cast<uint32_t> (embNode));
    if (channelCount == 0) return false;
    const int ch = static_cast<int> (std::min<uint32_t> (channelCount, 2));
    std::vector<std::vector<float>> data (static_cast<size_t> (ch));
    size_t frames = 0;
    for (int c = 0; c < ch; ++c)
    {
        const int arr = static_cast<int> (d.kids[N (embNode).first + static_cast<uint32_t> (c)]);
        const uint32_t count = d.arraySize (static_cast<uint32_t> (arr));
        if (count == 0 || count > static_cast<uint32_t> (opt.limits.maxAudioFrames)) return false;
        data[static_cast<size_t> (c)].resize (count);
        for (uint32_t k = 0; k < count; ++k)
        {
            const double v = d.arrayNumber (static_cast<uint32_t> (arr), k);
            data[static_cast<size_t> (c)][k] = std::isfinite (v) ? static_cast<float> (std::max (-1.0, std::min (1.0, v))) : 0.0f;
        }
        frames = c == 0 ? count : std::min<size_t> (frames, count);
    }
    if (ch == 2) { data[0].resize (frames); data[1].resize (frames); }
    const int rateNode = get (metaNode, "sampleRate");
    const int rate = std::max (8000, std::min (384000, rateNode >= 0 ? static_cast<int> (num (rateNode, 44100.0)) : 44100));
    ImportedSample s;
    s.osc = kNoiseSampleOsc;
    s.name = "Embedded noise";
    s.sampleRate = rate;
    s.numFrames = static_cast<int> (frames);
    s.channels = ch;
    s.wavBytes = encodeWav (data[0].data(), ch == 2 ? data[1].data() : nullptr, frames, rate, 24);
    s.sha256 = Sha256::hashBufferHex (s.wavBytes.data(), s.wavBytes.size());
    R.samples.erase (std::remove_if (R.samples.begin(), R.samples.end(), [] (const ImportedSample& x) { return x.osc == kNoiseSampleOsc; }), R.samples.end());
    R.samples.push_back (std::move (s));
    store ("noise.type", 1);
    acctRest (embNode, ItemKind::imported, Reason::exact, "content", path + "/embeddedNoiseData", "noise.type", "Sample", "embedded_noise_sample_stored_in_the_library");
    return true;
}

namespace
{
struct SfzRegion
{
    std::string sample;
    int lokey = 0, hikey = 127, key = -1, center = 60, lovel = 0, hivel = 127;
    double tune = 0.0;
};

int noteNumber (const std::string& v, int def)
{
    if (v.empty()) return def;
    char* end = nullptr;
    const long n = std::strtol (v.c_str(), &end, 10);
    if (end != v.c_str() && *end == '\0') return static_cast<int> (n);
    static const char* const names[] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
    std::string lower;
    for (const char c : v) lower += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    for (int k = 11; k >= 0; --k)
    {
        const std::string nm = names[k];
        if (lower.rfind (nm, 0) == 0)
        {
            const long oct = std::strtol (lower.c_str() + nm.size(), nullptr, 10);
            return static_cast<int> ((oct + 1) * 12 + k);
        }
    }
    return def;
}

std::vector<SfzRegion> parseSfz (const std::string& text)
{
    std::vector<SfzRegion> out;
    SfzRegion groupDefaults, cur;
    bool inRegion = false;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find ('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr (pos, eol - pos);
        pos = eol + 1;
        if (line.rfind ("//", 0) == 0) continue;
        // split into <header> and opcode=value tokens (a value may contain spaces up to the next opcode)
        size_t at = 0;
        while (at < line.size())
        {
            while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) ++at;
            if (at >= line.size()) break;
            if (line[at] == '<')
            {
                const size_t close = line.find ('>', at);
                const std::string hdr = line.substr (at, close == std::string::npos ? std::string::npos : close - at + 1);
                if (inRegion) out.push_back (cur);
                inRegion = hdr == "<region>";
                if (hdr == "<group>") groupDefaults = SfzRegion {};
                cur = groupDefaults;
                at = close == std::string::npos ? line.size() : close + 1;
                continue;
            }
            const size_t eq = line.find ('=', at);
            if (eq == std::string::npos) break;
            const std::string name = line.substr (at, eq - at);
            // the value ends before the next " name=" or the next '<'
            size_t end = line.size();
            for (size_t q = eq + 1; q < line.size(); ++q)
            {
                if (line[q] == '<') { end = q; break; }
                if (line[q] == ' ')
                {
                    size_t r = q + 1;
                    while (r < line.size() && (std::isalnum (static_cast<unsigned char> (line[r])) || line[r] == '_')) ++r;
                    if (r < line.size() && line[r] == '=' && r > q + 1) { end = q; break; }
                }
            }
            std::string value = line.substr (eq + 1, end - eq - 1);
            while (! value.empty() && (value.back() == ' ' || value.back() == '\r' || value.back() == '\t')) value.pop_back();
            SfzRegion& tgt = inRegion ? cur : groupDefaults;
            if (name == "sample") tgt.sample = value;
            else if (name == "lokey") tgt.lokey = noteNumber (value, 0);
            else if (name == "hikey") tgt.hikey = noteNumber (value, 127);
            else if (name == "key") { tgt.key = noteNumber (value, -1); tgt.lokey = tgt.hikey = tgt.key; }
            else if (name == "pitch_keycenter") tgt.center = noteNumber (value, 60);
            else if (name == "lovel") tgt.lovel = static_cast<int> (std::strtol (value.c_str(), nullptr, 10));
            else if (name == "hivel") tgt.hivel = static_cast<int> (std::strtol (value.c_str(), nullptr, 10));
            else if (name == "tune") tgt.tune = std::strtod (value.c_str(), nullptr);
            at = end;
        }
    }
    if (inRegion) out.push_back (cur);
    return out;
}
} // namespace

bool Importer::applyMultisample (int osc, int B, const std::string& path, int* rootOut)
{
    const std::string okey = "osc" + std::to_string (osc);
    const int sfzRel = get (B, "sfzPathRelative"), emb = get (B, "embedded_sfz");
    std::string text, dir;
    if (isText (sfzRel)) dir = dirOf (normalisePath (str (sfzRel)));
    if (isText (emb)) text = str (emb);
    else if (isText (sfzRel))
    {
        std::vector<uint8_t> bytes;
        if (readContent ("Multisamples", str (sfzRel), bytes)) text.assign (bytes.begin(), bytes.end());
    }
    if (isText (emb)) { entry (ItemKind::imported, Reason::exact, "content", path + "/embedded_sfz", "", okey + ".type", "Sample", "sfz_text_read"); consume (emb); }
    if (isText (sfzRel)) { entry (ItemKind::approximated, Reason::tierNote, "content", path + "/sfzPathRelative", str (sfzRel), okey + ".type", "Sample", "multisample_reduced_to_one_zone"); consume (sfzRel); }
    const std::vector<SfzRegion> regions = parseSfz (text);
    if (regions.empty())
    {
        entry (ItemKind::dropped, Reason::noEngineEquivalent, "content", path, "", okey + ".type", "Wavetable", "multisample_without_regions", 0);
        store (okey + ".type", 0);
        return false;
    }
    // the zone nearest to C3 (MIDI 60) at medium velocity
    size_t best = 0;
    double bestScore = 1e9;
    for (size_t r = 0; r < regions.size(); ++r)
    {
        const SfzRegion& g = regions[r];
        const double keyDist = 60 < g.lokey ? g.lokey - 60 : 60 > g.hikey ? 60 - g.hikey : 0;
        const double velDist = 64 < g.lovel ? g.lovel - 64 : 64 > g.hivel ? 64 - g.hivel : 0;
        const double score = keyDist + velDist * 0.1 + std::fabs (g.center - 60) * 0.001;
        if (score < bestScore) { bestScore = score; best = r; }
    }
    const SfzRegion& g = regions[best];
    const std::string rel = dir.empty() ? normalisePath (g.sample) : dir + "/" + normalisePath (g.sample);
    if (rootOut) *rootOut = g.center;
    const bool ok = loadSample ("Multisamples", rel, "", osc, path + "/files", g.center, nullptr, nullptr, nullptr);
    if (ok && std::fabs (g.tune) > 1e-9)
    {
        const double cur = R.values.plain[static_cast<size_t> (indexOf (okey + ".tune"))];
        store (okey + ".tune", cur + g.tune);
        entry (ItemKind::approximated, Reason::tierNote, "content", path + "/sfz tune", fmt (g.tune), okey + ".tune", "", "zone_tune_added_to_the_oscillator_tune", 0);
    }
    const int files = get (B, "files");
    if (files >= 0) acctRest (files, ItemKind::ignored, Reason::exact, "content", path + "/files", "", "", "multisample_zone_list_replaced_by_one_zone");
    for (const char* meta : { "defaultLoopMode" })
    {
        const int m = get (B, meta);
        if (m >= 0 && ! used[static_cast<size_t> (m)]) { entry (ItemKind::ignored, Reason::exact, "content", path + "/" + meta, valueText (m), "", "", "multisample_zone_option"); consume (m); }
    }
    return ok;
}

} // namespace serumimport

// ---------------------------------------------------------------------------------------------------------------------------------------
// stand-alone impulse
// ---------------------------------------------------------------------------------------------------------------------------------------
SerumImportResult importSerumImpulse (const void* data, size_t size, const std::string& name, const SerumImportOptions& options) noexcept
{
    SerumImportResult r;
    try
    {
        r.kind = SerumFileKind::impulse;
        r.sourceSha256 = Sha256::hashBufferHex (data, size);
        int rate = 0, ch = 0;
        size_t frames = 0;
        SerumImportedImpulse ir;
        ir.wavBytes = serumAudioToWav (data, size, 24, options.limits.maxAudioFrames, &rate, &ch, &frames);
        if (ir.wavBytes.empty())
        {
            r.error = "the impulse does not decode";
            r.status = ImportStatus::malformed;
            return r;
        }
        ir.name = name;
        ir.sampleRate = rate;
        ir.channels = ch;
        ir.numFrames = static_cast<int> (frames);
        ir.sha256 = Sha256::hashBufferHex (ir.wavBytes.data(), ir.wavBytes.size());
        r.impulses.push_back (std::move (ir));
        SerumReportEntry e;
        e.kind = ItemKind::imported;
        e.area = "content";
        e.serumPath = name;
        e.ourKey = "convolve.ir";
        e.ourValue = "Preset IR";
        e.detail = "impulse_response_converted_to_24_bit_wav";
        r.report.entries.push_back (e);
        r.report.numImported = 1;
        r.report.leaves = 1;
        r.status = ImportStatus::ok;
    }
    catch (...)
    {
        r.status = ImportStatus::budgetExceeded;
        r.error = "out of memory";
    }
    return r;
}

std::vector<uint8_t> serumAudioToWav (const void* data, size_t size, int bits, size_t maxFrames, int* sampleRate, int* channels, size_t* frames)
{
    serumimport::DecodedAudio a;
    if (! serumimport::decodeAudio (static_cast<const uint8_t*> (data), size, maxFrames, a) || a.frames() == 0) return {};
    const size_t n = a.frames();
    const int ch = a.channels >= 2 ? 2 : 1;
    std::vector<float> left (n), right (ch == 2 ? n : 0);
    for (size_t i = 0; i < n; ++i)
    {
        left[i] = a.data[i * static_cast<size_t> (a.channels)];
        if (ch == 2) right[i] = a.data[i * static_cast<size_t> (a.channels) + 1];
    }
    if (sampleRate) *sampleRate = a.sampleRate;
    if (channels) *channels = ch;
    if (frames) *frames = n;
    return encodeWav (left.data(), ch == 2 ? right.data() : nullptr, n, a.sampleRate, bits);
}

} // namespace synthyboy
