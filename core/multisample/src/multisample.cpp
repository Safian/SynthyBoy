// SynthyBoy - core/multisample: the multisample oscillator.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/multisample/multisample.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace synthyboy
{
namespace multisample
{
namespace
{
std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace (static_cast<unsigned char> (s[a]))) ++a;
    while (b > a && std::isspace (static_cast<unsigned char> (s[b - 1]))) --b;
    return s.substr (a, b - a);
}

std::string lower (std::string s)
{
    for (char& c : s)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}

bool isOpcodeStart (const std::string& line, size_t at, size_t& nameEnd)
{
    // an opcode: [a-zA-Z_$][a-zA-Z0-9_$]*= preceded by the start of the line or whitespace
    if (at > 0 && ! std::isspace (static_cast<unsigned char> (line[at - 1])))
        return false;
    size_t i = at;
    if (i >= line.size() || ! (std::isalpha (static_cast<unsigned char> (line[i])) || line[i] == '_' || line[i] == '$'))
        return false;
    while (i < line.size() && (std::isalnum (static_cast<unsigned char> (line[i])) || line[i] == '_' || line[i] == '$'))
        ++i;
    if (i < line.size() && line[i] == '=')
    {
        nameEnd = i;
        return true;
    }
    return false;
}

using Opcodes = std::vector<std::pair<std::string, std::string>>;

void setOpcode (Opcodes& list, const std::string& name, const std::string& value)
{
    for (auto& entry : list)
        if (entry.first == name)
        {
            entry.second = value;
            return;
        }
    list.emplace_back (name, value);
}

double number (const std::string& text, double fallback) noexcept
{
    char* end = nullptr;
    const double v = std::strtod (text.c_str(), &end);
    return end != text.c_str() && std::isfinite (v) ? v : fallback;
}

int noteOrNumber (const std::string& text, int fallback) noexcept
{
    const int n = parseNoteName (text);
    return n >= 0 ? n : (std::isfinite (number (text, -1.0)) && number (text, -1.0) >= 0.0 ? static_cast<int> (number (text, -1.0)) : fallback);
}

std::string normalisePath (std::string p)
{
    for (char& c : p)
        if (c == '\\')
            c = '/';
    return p;
}

Region regionFrom (const std::vector<const Opcodes*>& layers, const std::string& defaultPath)
{
    std::map<std::string, std::string> v;
    for (const Opcodes* layer : layers)
        for (const auto& e : *layer)
            v[e.first] = e.second;
    Region r;
    const auto has = [&] (const char* key) { return v.count (key) != 0; };
    const auto get = [&] (const char* key) { return v.at (key); };
    if (has ("sample"))
    {
        std::string sample = normalisePath (get ("sample"));
        r.sample = defaultPath.empty() ? sample : defaultPath + (defaultPath.back() == '/' ? "" : "/") + sample;
    }
    if (has ("key"))
        r.lokey = r.hikey = r.center = noteOrNumber (get ("key"), 60);
    if (has ("lokey")) r.lokey = noteOrNumber (get ("lokey"), 0);
    if (has ("hikey")) r.hikey = noteOrNumber (get ("hikey"), 127);
    if (has ("pitch_keycenter")) r.center = noteOrNumber (get ("pitch_keycenter"), 60);
    if (has ("lovel")) r.lovel = static_cast<int> (number (get ("lovel"), 1.0));
    if (has ("hivel")) r.hivel = static_cast<int> (number (get ("hivel"), 127.0));
    if (has ("tune")) r.tuneCents = static_cast<float> (number (get ("tune"), 0.0));
    if (has ("transpose")) r.transpose = static_cast<int> (number (get ("transpose"), 0.0));
    if (has ("volume")) r.volumeDb = static_cast<float> (number (get ("volume"), 0.0));
    else if (has ("vol")) r.volumeDb = static_cast<float> (number (get ("vol"), 0.0));
    if (has ("pan")) r.pan = static_cast<float> (std::max (-100.0, std::min (100.0, number (get ("pan"), 0.0))) / 100.0);
    if (has ("offset")) r.offset = static_cast<int64_t> (std::max (0.0, number (get ("offset"), 0.0)));
    if (has ("end")) r.end = static_cast<int64_t> (number (get ("end"), -1.0));
    if (has ("loop_mode"))
    {
        const std::string m = lower (get ("loop_mode"));
        r.loopMode = m == "one_shot" ? 1 : (m == "loop_continuous" ? 2 : (m == "loop_sustain" ? 3 : 0));
    }
    if (has ("loopmode"))
    {
        const std::string m = lower (get ("loopmode"));
        r.loopMode = m == "one_shot" ? 1 : (m == "loop_continuous" ? 2 : (m == "loop_sustain" ? 3 : 0));
    }
    if (has ("loop_start")) r.loopStart = static_cast<int64_t> (number (get ("loop_start"), -1.0));
    if (has ("loop_end")) r.loopEnd = static_cast<int64_t> (number (get ("loop_end"), -1.0));
    if (has ("loop_crossfade")) r.loopCrossfadeSec = static_cast<float> (std::max (0.0, number (get ("loop_crossfade"), 0.0)));
    const struct { const char* name; float* target; } env[] = { { "ampeg_delay", &r.delay }, { "ampeg_attack", &r.attack }, { "ampeg_hold", &r.hold }, { "ampeg_decay", &r.decay },
                                                                  { "ampeg_sustain", &r.sustain }, { "ampeg_release", &r.release } };
    for (const auto& e : env)
        if (has (e.name))
        {
            *e.target = static_cast<float> (std::max (0.0, number (get (e.name), *e.target)));
            r.hasEnvelope = true;
        }
    r.sustain = std::min (100.0f, r.sustain);
    if (has ("amp_veltrack")) r.velTrack = static_cast<float> (std::max (-100.0, std::min (100.0, number (get ("amp_veltrack"), 100.0))));
    if (has ("amp_keytrack")) r.keyTrackDb = static_cast<float> (number (get ("amp_keytrack"), 0.0));
    for (const auto& e : v)
        if (e.first.rfind ("amp_velcurve_", 0) == 0)
        {
            const int vel = static_cast<int> (number (e.first.substr (13), -1.0));
            if (vel >= 0 && vel <= 127)
                r.velCurve.emplace_back (vel, static_cast<float> (std::max (0.0, std::min (1.0, number (e.second, 0.0)))));
        }
    std::sort (r.velCurve.begin(), r.velCurve.end());
    if (has ("group")) r.group = static_cast<int> (number (get ("group"), 0.0));
    if (has ("off_by")) r.offBy = static_cast<int> (number (get ("off_by"), 0.0));
    if (has ("off_mode")) r.offMode = lower (get ("off_mode")) == "normal" ? 1 : 0;
    if (has ("seq_length")) r.seqLength = std::max (1, static_cast<int> (number (get ("seq_length"), 1.0)));
    if (has ("seq_position")) r.seqPosition = std::max (1, static_cast<int> (number (get ("seq_position"), 1.0)));
    if (has ("lorand")) r.lorand = static_cast<float> (std::max (0.0, std::min (1.0, number (get ("lorand"), 0.0))));
    if (has ("hirand")) r.hirand = static_cast<float> (std::max (0.0, std::min (1.0, number (get ("hirand"), 1.0))));
    if (has ("sw_lokey")) r.swLokey = noteOrNumber (get ("sw_lokey"), -1);
    if (has ("sw_hikey")) r.swHikey = noteOrNumber (get ("sw_hikey"), -1);
    if (has ("sw_last")) r.swLast = noteOrNumber (get ("sw_last"), -1);
    r.lokey = std::max (0, std::min (127, r.lokey));
    r.hikey = std::max (r.lokey, std::min (127, r.hikey));
    r.lovel = std::max (0, std::min (127, r.lovel));
    r.hivel = std::max (r.lovel, std::min (127, r.hivel));
    r.center = std::max (0, std::min (127, r.center));
    return r;
}
} // namespace

int parseNoteName (const std::string& text) noexcept
{
    const std::string t = lower (trim (text));
    if (t.size() < 2 || t[0] < 'a' || t[0] > 'g')
        return -1;
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    int pc = base[t[0] - 'a'];
    size_t i = 1;
    if (i < t.size() && (t[i] == '#' || t[i] == 's')) { ++pc; ++i; }
    else if (i < t.size() && t[i] == 'b') { --pc; ++i; }
    if (i >= t.size())
        return -1;
    char* end = nullptr;
    const long octave = std::strtol (t.c_str() + i, &end, 10);
    if (end == t.c_str() + i || *end != '\0')
        return -1;
    const long midi = (octave + 1) * 12 + pc;
    return midi >= 0 && midi <= 127 ? static_cast<int> (midi) : -1;
}

SfzInstrument parseSfz (const std::string& rawText)
{
    SfzInstrument out;
    // comments
    std::string text;
    text.reserve (rawText.size());
    for (size_t i = 0; i < rawText.size(); ++i)
    {
        // a comment starts with // at the start of a line or after a blank; a double slash inside a path ("Samples//file.flac", written by libSFZ) is part of the path
        if (rawText[i] == '/' && i + 1 < rawText.size() && rawText[i + 1] == '/' && (i == 0 || std::isspace (static_cast<unsigned char> (rawText[i - 1]))))
        {
            while (i < rawText.size() && rawText[i] != '\n') ++i;
            if (i < rawText.size()) text += '\n';
        }
        else if (rawText[i] == '/' && i + 1 < rawText.size() && rawText[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < rawText.size() && ! (rawText[i] == '*' && rawText[i + 1] == '/')) ++i;
            ++i;
        }
        else
            text += rawText[i];
    }
    std::map<std::string, std::string> defines;
    Opcodes control, global, master, group, region;
    enum { none, inControl, inGlobal, inMaster, inGroup, inRegion } where = none;
    std::string defaultPath;
    const auto finishRegion = [&]
    {
        if (where == inRegion)
        {
            const Region r = regionFrom ({ &global, &master, &group, &region }, defaultPath);
            if (! r.sample.empty())
                out.regions.push_back (r);
        }
        region.clear();
    };
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find ('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr (pos, eol - pos);
        pos = eol + 1;
        if (! line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trim (line);
        if (trimmed.rfind ("#define", 0) == 0)
        {
            std::string rest = trim (trimmed.substr (7));
            const size_t sp = rest.find_first_of (" \t");
            if (sp != std::string::npos)
                defines[trim (rest.substr (0, sp))] = trim (rest.substr (sp));
            continue;
        }
        if (trimmed.empty() || trimmed[0] == '#')
            continue;
        size_t i = 0;
        while (i < line.size())
        {
            while (i < line.size() && std::isspace (static_cast<unsigned char> (line[i]))) ++i;
            if (i >= line.size()) break;
            if (line[i] == '<')
            {
                const size_t close = line.find ('>', i);
                if (close == std::string::npos) break;
                const std::string header = lower (line.substr (i + 1, close - i - 1));
                finishRegion();
                if (header == "control") { where = inControl; control.clear(); }
                else if (header == "global") { where = inGlobal; global.clear(); master.clear(); group.clear(); }
                else if (header == "master") { where = inMaster; master.clear(); group.clear(); }
                else if (header == "group") { where = inGroup; group.clear(); }
                else if (header == "region") where = inRegion;
                else where = none;
                i = close + 1;
                continue;
            }
            size_t nameEnd = 0;
            if (! isOpcodeStart (line, i, nameEnd))
            {
                ++i;
                continue;
            }
            std::string name = lower (line.substr (i, nameEnd - i));
            size_t valueEnd = nameEnd + 1;
            size_t scan = valueEnd;
            // the value runs to the next opcode or header of the line (a sample path may hold spaces)
            while (scan < line.size())
            {
                size_t dummy = 0;
                if ((std::isspace (static_cast<unsigned char> (line[scan])) && scan + 1 < line.size() && (isOpcodeStart (line, scan + 1, dummy) || line[scan + 1] == '<')) || line[scan] == '<')
                    break;
                ++scan;
            }
            valueEnd = scan;
            std::string value = trim (line.substr (nameEnd + 1, valueEnd - nameEnd - 1));
            for (const auto& d : defines)
                for (size_t at = value.find (d.first); at != std::string::npos; at = value.find (d.first, at + d.second.size()))
                    value.replace (at, d.first.size(), d.second);
            i = valueEnd;
            if (name == "default_path" && where == inControl) defaultPath = normalisePath (value);
            else if (name == "sw_default" && out.swDefault < 0) out.swDefault = noteOrNumber (value, -1);
            else if (name == "default_loop_mode") out.defaultLoopMode = value;
            switch (where)
            {
                case inControl: setOpcode (control, name, value); break;
                case inGlobal: setOpcode (global, name, value); break;
                case inMaster: setOpcode (master, name, value); break;
                case inGroup: setOpcode (group, name, value); break;
                case inRegion: setOpcode (region, name, value); break;
                default: break;
            }
        }
    }
    finishRegion();
    return out;
}

std::string storedSamplePath (const std::string& path, bool asWav)
{
    std::vector<std::string> parts;
    std::string segment;
    const auto flush = [&]
    {
        if (segment == "..")
        {
            if (! parts.empty())
                parts.pop_back();
        }
        else if (! segment.empty() && segment != ".")
        {
            for (char& ch : segment)
                if (ch == ':' || ch == '\\' || static_cast<unsigned char> (ch) < 0x20)
                    ch = '_';
            parts.push_back (segment);
        }
        segment.clear();
    };
    for (char ch : normalisePath (path))
    {
        if (ch == '/')
            flush();
        else
            segment += ch;
    }
    flush();
    if (parts.empty())
        return asWav ? "sample.wav" : "sample";
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i)
        out += (i > 0 ? "/" : "") + parts[i];
    if (asWav)
    {
        const size_t dot = out.rfind ('.');
        const size_t slash = out.rfind ('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            out.erase (dot);
        out += ".wav";
    }
    return out;
}

std::string writeSfz (const SfzInstrument& instrument)
{
    std::string out = "// SynthyBoy multisample instrument\n";
    if (instrument.swDefault >= 0)
        out += "<global> sw_default=" + std::to_string (instrument.swDefault) + "\n";
    const auto num = [] (double v) { char b[48]; std::snprintf (b, sizeof b, "%.9g", v); return std::string (b); };
    for (const Region& r : instrument.regions)
    {
        out += "<region>\nsample=" + r.sample + "\n";
        out += "lokey=" + std::to_string (r.lokey) + " hikey=" + std::to_string (r.hikey) + " pitch_keycenter=" + std::to_string (r.center)
             + " lovel=" + std::to_string (r.lovel) + " hivel=" + std::to_string (r.hivel) + "\n";
        out += "tune=" + num (r.tuneCents) + " transpose=" + std::to_string (r.transpose) + " volume=" + num (r.volumeDb) + " pan=" + num (r.pan * 100.0f) + "\n";
        out += "offset=" + std::to_string (r.offset) + " end=" + std::to_string (r.end) + "\n";
        static const char* const modes[] = { "no_loop", "one_shot", "loop_continuous", "loop_sustain" };
        if (r.loopMode >= 0)
            out += std::string ("loop_mode=") + modes[r.loopMode] + "\n";
        if (r.loopStart >= 0) out += "loop_start=" + std::to_string (r.loopStart) + "\n";
        if (r.loopEnd >= 0) out += "loop_end=" + std::to_string (r.loopEnd) + "\n";
        if (r.loopCrossfadeSec > 0.0f) out += "loop_crossfade=" + num (r.loopCrossfadeSec) + "\n";
        if (r.hasEnvelope)
            out += "ampeg_delay=" + num (r.delay) + " ampeg_attack=" + num (r.attack) + " ampeg_hold=" + num (r.hold) + " ampeg_decay=" + num (r.decay)
                 + " ampeg_sustain=" + num (r.sustain) + " ampeg_release=" + num (r.release) + "\n";
        out += "amp_veltrack=" + num (r.velTrack) + " amp_keytrack=" + num (r.keyTrackDb) + "\n";
        for (const auto& pt : r.velCurve)
            out += "amp_velcurve_" + std::to_string (pt.first) + "=" + num (pt.second) + "\n";
        if (r.group > 0 || r.offBy > 0)
            out += "group=" + std::to_string (r.group) + " off_by=" + std::to_string (r.offBy) + " off_mode=" + (r.offMode == 1 ? "normal" : "fast") + "\n";
        if (r.seqLength > 1 || r.seqPosition > 1)
            out += "seq_length=" + std::to_string (r.seqLength) + " seq_position=" + std::to_string (r.seqPosition) + "\n";
        if (r.lorand > 0.0f || r.hirand < 1.0f)
            out += "lorand=" + num (r.lorand) + " hirand=" + num (r.hirand) + "\n";
        if (r.swLast >= 0) out += "sw_last=" + std::to_string (r.swLast) + "\n";
        if (r.swLokey >= 0) out += "sw_lokey=" + std::to_string (r.swLokey) + "\n";
        if (r.swHikey >= 0) out += "sw_hikey=" + std::to_string (r.swHikey) + "\n";
    }
    return out;
}

// ---- instrument --------------------------------------------------------------------------------------------------------------------

int Instrument::build (const SfzInstrument& sfz, const SampleReader& reader)
{
    zones_.clear();
    samples_.clear();
    views_.clear();
    playable_ = missing_ = skipped_ = 0;
    switchDefault_ = sfz.swDefault;
    hasKeySwitches_ = false;
    switchLo_ = 127;
    switchHi_ = 0;
    std::map<std::string, int> loaded; // path -> sample index (-1: missing / skipped)
    int64_t total = 0;
    std::vector<uint8_t> bytes;
    for (const Region& r : sfz.regions)
    {
        Zone z;
        z.region = r;
        auto it = loaded.find (r.sample);
        if (it == loaded.end())
        {
            int index = -1;
            if (total >= kMaxInstrumentFrames)
                ++skipped_;
            else if (reader (r.sample, bytes))
            {
                auto data = std::make_unique<sampleosc::SampleData>();
                if (data->loadFromMemory (bytes.data(), bytes.size(), r.sample.c_str()) && data->isValid())
                {
                    total += data->numFrames();
                    index = static_cast<int> (samples_.size());
                    samples_.push_back (std::move (data));
                }
                else
                    ++missing_;
            }
            else
                ++missing_;
            it = loaded.emplace (r.sample, index).first;
        }
        z.sample = it->second;
        if (z.sample >= 0)
        {
            const sampleosc::SampleData& d = *samples_[static_cast<size_t> (z.sample)];
            const double frames = static_cast<double> (d.numFrames());
            z.startFraction = static_cast<float> (std::min (1.0, static_cast<double> (r.offset) / frames));
            z.endFraction = r.end > 0 ? static_cast<float> (std::min (1.0, static_cast<double> (r.end + 1) / frames)) : 1.0f;
            if (z.endFraction <= z.startFraction + 1.0e-6f)
                z.endFraction = 1.0f;
            int mode = r.loopMode;
            int64_t ls = r.loopStart, le = r.loopEnd;
            if (mode < 0)
                mode = d.hasSmplLoop() ? 2 : 0;
            if ((mode == 2 || mode == 3) && (ls < 0 || le < 0))
            {
                ls = d.hasSmplLoop() ? d.loopStart() : 0;
                le = d.hasSmplLoop() ? d.loopEnd() : d.numFrames();
            }
            z.loopType = mode;
            z.loopStartFraction = static_cast<float> (std::max (0.0, std::min (1.0, static_cast<double> (ls < 0 ? 0 : ls) / frames)));
            z.loopEndFraction = static_cast<float> (std::max (0.0, std::min (1.0, static_cast<double> (le < 0 ? d.numFrames() : le) / frames)));
            ++playable_;
        }
        if (r.swLast >= 0 || r.swLokey >= 0)
        {
            hasKeySwitches_ = true;
            if (r.swLokey >= 0) switchLo_ = std::min (switchLo_, r.swLokey);
            if (r.swHikey >= 0) switchHi_ = std::max (switchHi_, r.swHikey);
        }
        zones_.push_back (std::move (z));
    }
    for (const auto& s : samples_)
        views_.push_back (s->view());
    return playable_;
}

bool Instrument::isKeySwitchKey (int key) const noexcept
{
    return hasKeySwitches_ && key >= switchLo_ && key <= switchHi_;
}

int Instrument::timbreKey (int key, float timbre) noexcept
{
    timbre = std::max (-1.0f, std::min (1.0f, timbre == timbre ? timbre : 0.0f));
    const float k = static_cast<float> (key) + timbre * (127.0f - 2.0f * static_cast<float> (key));
    return std::max (0, std::min (127, static_cast<int> (std::lround (k))));
}

int Instrument::selectZones (int key, int velocity, float rand, uint32_t counter, int switchKey, int* out, int maxOut) const noexcept
{
    int n = 0;
    for (int i = 0; i < static_cast<int> (zones_.size()) && n < maxOut; ++i)
    {
        const Zone& z = zones_[static_cast<size_t> (i)];
        const Region& r = z.region;
        if (z.sample < 0 || key < r.lokey || key > r.hikey || velocity < r.lovel || velocity > r.hivel)
            continue;
        if (rand < r.lorand || (rand >= r.hirand && r.hirand < 1.0f))
            continue;
        if (r.seqLength > 1 && static_cast<int> (counter % static_cast<uint32_t> (r.seqLength)) + 1 != r.seqPosition)
            continue;
        if (r.swLast >= 0 && switchKey != r.swLast)
            continue;
        out[n++] = i;
    }
    return n;
}

// ---- layer voice -------------------------------------------------------------------------------------------------------------------

void LayerVoice::prepare (double hostSampleRate) noexcept
{
    rate_ = hostSampleRate > 1000.0 ? hostSampleRate : 48000.0;
    voice_.prepare (rate_);
}

void LayerVoice::reset() noexcept
{
    voice_.reset();
    active_ = false;
    stage_ = Stage::done;
}

void LayerVoice::enter (Stage stage, double seconds) noexcept
{
    stage_ = stage;
    stageSamples_ = 0.0;
    stageLength_ = std::max(0.0, seconds * rate_);
}

void LayerVoice::noteOn (const Instrument& instrument, int zoneIndex, int key, int velocity, const EnvelopeSettings* envelope, float velTrackPercent, uint32_t startSequence,
                         sampleosc::QualityTier quality) noexcept
{
    const Instrument::Zone& z = instrument.zone (zoneIndex);
    const sampleosc::SampleView* view = instrument.view (z.sample);
    if (view == nullptr)
    {
        active_ = false;
        return;
    }
    const Region& r = z.region;
    sampleosc::SampleOscParams p;
    p.rootKey = r.center;
    p.pitchTracking = true;
    p.startOffset = z.startFraction;
    p.endOffset = z.endFraction;
    p.loopMode = z.loopType == 2 || z.loopType == 3 ? sampleosc::LoopMode::forward : sampleosc::LoopMode::none;
    p.loopStart = z.loopStartFraction;
    p.loopEnd = z.loopEndFraction;
    const double loopFrames = std::fabs (static_cast<double> (z.loopEndFraction) - z.loopStartFraction) * static_cast<double> (view->numFrames);
    p.loopCrossfade = loopFrames > 1.0 ? static_cast<float> (std::min (0.49, static_cast<double> (r.loopCrossfadeSec) * view->sampleRate / loopFrames)) : 0.0f;
    p.quality = quality;
    p.level = 1.0f;
    p.pan = 0.0f;
    voice_.noteOn (r.center, 1.0f, view, p);
    oneShot_ = z.loopType == 1;
    sustainLoop_ = z.loopType == 3;
    released_ = false;
    group_ = r.group;
    offBy_ = r.offBy;
    offMode_ = r.offMode;
    zone_ = zoneIndex;
    startSequence_ = startSequence;
    tuneSemitones_ = r.tuneCents * 0.01f + static_cast<float>(r.transpose);

    // the gains: volume, key tracking, velocity (curve and tracking)
    const float vel01 = static_cast<float> (velocity) / 127.0f;
    float curve = vel01;
    if (! r.velCurve.empty())
    {
        // piece wise linear through the given points (0 gain at velocity 0, the last point continues flat)
        int prevVel = 0;
        float prevGain = 0.0f;
        curve = r.velCurve.back().second;
        for (const auto& pt : r.velCurve)
        {
            if (velocity <= pt.first)
            {
                const float t = pt.first > prevVel ? static_cast<float> (velocity - prevVel) / static_cast<float> (pt.first - prevVel) : 1.0f;
                curve = prevGain + (pt.second - prevGain) * t;
                break;
            }
            prevVel = pt.first;
            prevGain = pt.second;
        }
    }
    const float track = (velTrackPercent >= 0.0f ? velTrackPercent : r.velTrack) * 0.01f;
    const float velocityGain = 1.0f - track + track * curve;
    gain_ = std::pow (10.0f, (r.volumeDb + r.keyTrackDb * static_cast<float> (key - r.center)) / 20.0f) * std::max (0.0f, velocityGain);
    pan_ = r.pan;
    const float angle = (std::max (-1.0f, std::min (1.0f, pan_)) + 1.0f) * 0.78539816f;
    panL_ = std::cos (angle) * 1.41421356f;
    panR_ = std::sin (angle) * 1.41421356f;

    // the envelope: the override, or the SFZ's (a zone without ampeg_* plays at once and releases in 3 ms)
    if (envelope != nullptr)
        env_ = *envelope;
    else
    {
        env_.delay = r.delay;
        env_.attack = r.attack;
        env_.hold = r.hold;
        env_.decay = r.decay;
        env_.sustain = r.sustain * 0.01f;
        env_.release = r.hasEnvelope ? r.release : std::max (r.release, 0.003f);
    }
    env_.release = std::max (env_.release, 0.002f);
    level_ = 0.0f;
    active_ = true;
    if (env_.delay > 0.0f)
        enter (Stage::delay, env_.delay);
    else if (env_.attack > 0.0f)
        enter (Stage::attack, env_.attack);
    else
    {
        level_ = 1.0f;
        if (env_.hold > 0.0f) enter (Stage::hold, env_.hold);
        else if (env_.decay > 0.0f && env_.sustain < 1.0f) enter (Stage::decay, env_.decay);
        else { level_ = env_.sustain; stage_ = Stage::sustain; }
    }
}

void LayerVoice::noteOff (bool fast) noexcept
{
    if (! active_ || released_)
        return;
    released_ = true;
    if (sustainLoop_)
        voice_.releaseLoop();
    if (oneShot_ && ! fast)
        return; // a one shot plays to its end
    releaseFrom_ = level_;
    enter (Stage::release, fast ? std::min (static_cast<double> (env_.release), 0.01) : static_cast<double> (env_.release));
}

void LayerVoice::processAdd (float* left, float* right, float* tmpLeft, float* tmpRight, int numFrames, float semitones, const float* rateMod, float gainLeft, float gainRight) noexcept
{
    if (! active_)
        return;
    voice_.setSemitoneOffset (semitones + tuneSemitones_);
    voice_.setSpeedMultiplier (1.0);
    voice_.processModulated (tmpLeft, tmpRight, numFrames, rateMod);
    for (int i = 0; i < numFrames; ++i)
    {
        // advance the envelope one sample
        switch (stage_)
        {
            case Stage::delay:
                level_ = 0.0f;
                if (++stageSamples_ >= stageLength_)
                {
                    if (env_.attack > 0.0f) enter (Stage::attack, env_.attack);
                    else { level_ = 1.0f; enter (Stage::hold, env_.hold); }
                }
                break;
            case Stage::attack:
                level_ = static_cast<float> (std::min (1.0, ++stageSamples_ / std::max (1.0, stageLength_)));
                if (stageSamples_ >= stageLength_) enter (Stage::hold, env_.hold);
                break;
            case Stage::hold:
                level_ = 1.0f;
                if (stageLength_ <= 0.0 || ++stageSamples_ >= stageLength_)
                {
                    if (env_.decay > 0.0f && env_.sustain < 1.0f) enter (Stage::decay, env_.decay);
                    else { level_ = env_.sustain; stage_ = Stage::sustain; }
                }
                break;
            case Stage::decay:
                level_ = 1.0f + (env_.sustain - 1.0f) * static_cast<float> (std::min (1.0, ++stageSamples_ / std::max (1.0, stageLength_)));
                if (stageSamples_ >= stageLength_) { level_ = env_.sustain; stage_ = Stage::sustain; }
                break;
            case Stage::sustain:
                level_ = env_.sustain;
                break;
            case Stage::release:
                level_ = releaseFrom_ * static_cast<float> (std::max (0.0, 1.0 - ++stageSamples_ / std::max (1.0, stageLength_)));
                if (stageSamples_ >= stageLength_) { level_ = 0.0f; stage_ = Stage::done; }
                break;
            case Stage::done:
                level_ = 0.0f;
                break;
        }
        const float g = level_ * gain_;
        left[i] += tmpLeft[i] * g * panL_ * gainLeft;
        right[i] += tmpRight[i] * g * panR_ * gainRight;
    }
    if (stage_ == Stage::done || ! voice_.isActive())
        active_ = false;
}
} // namespace multisample
} // namespace synthyboy
