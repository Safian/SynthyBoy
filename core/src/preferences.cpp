// SynthyBoy - core: preferences and MIDI CC map files. See preferences.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/preferences.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "synthyboy/params.h"
#include "synthyboy/shell_features.h"

namespace synthyboy
{
namespace
{
constexpr size_t kMaxFileBytes = 1u << 20;

const PreferenceInfo kTable[] = {
    { "valueBubbles", "Parameter value bubbles", &Preferences::valueBubbles },
    { "doubleClickResets", "Double-click resets (off: opens the value box)", &Preferences::doubleClickResets },
    { "mouseWheel", "Mouse wheel changes controls", &Preferences::mouseWheel },
    { "keyboardShortcuts", "Keyboard shortcuts", &Preferences::keyboardShortcuts },
    { "waveform3d", "Oscillator view 3D by default", &Preferences::waveform3d },
    { "freqSemitones", "Frequencies in semitones", &Preferences::freqSemitones },
    { "mpeByDefault", "MPE enabled in a new instance", &Preferences::mpeByDefault },
    { "mpeBendToX", "MPE pitch bend also maps to Expr X", &Preferences::mpeBendToX },
    { "mpeYBipolar", "MPE Expr Y acts bi-directional", &Preferences::mpeYBipolar },
    { "limitModDepth", "Limit mod depth on drop", &Preferences::limitModDepth },
    { "modWheelToWtPos", "Mod wheel scans WT POS (editor open)", &Preferences::modWheelToWtPos },
    { "silenceOnStop", "Silence notes and FX tails when the host stops", &Preferences::silenceOnStop },
    { "loadMidiMapFromPresets", "Load MIDI map from presets", &Preferences::loadMidiMapFromPresets },
    { "ultraRender", "Ultra quality when rendering", &Preferences::ultraRender },
};

/** A tiny scanner over the flat documents of this file. */
struct Cursor
{
    const char* p;
    const char* end;
    void ws() noexcept { while (p < end && std::isspace (static_cast<unsigned char> (*p))) ++p; }
    bool eat (char c) noexcept { ws(); if (p < end && *p == c) { ++p; return true; } return false; }
    bool peek (char c) noexcept { ws(); return p < end && *p == c; }
    bool string (std::string& out) noexcept
    {
        ws();
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"')
        {
            if (*p == '\\' && p + 1 < end) ++p;
            out += *p++;
            if (out.size() > 256) return false;
        }
        if (p >= end) return false;
        ++p;
        return true;
    }
    /** Skips one value of any kind. */
    bool skip (int depth = 0) noexcept
    {
        ws();
        if (p >= end || depth > 8) return false;
        if (*p == '"') { std::string s; return string (s); }
        if (*p == '{' || *p == '[')
        {
            const char open = *p, close = open == '{' ? '}' : ']';
            ++p;
            if (eat (close)) return true;
            for (;;)
            {
                if (open == '{') { std::string k; if (! string (k) || ! eat (':')) return false; }
                if (! skip (depth + 1)) return false;
                if (eat (',')) continue;
                return eat (close);
            }
        }
        while (p < end && *p != ',' && *p != '}' && *p != ']' && ! std::isspace (static_cast<unsigned char> (*p))) ++p;
        return true;
    }
};

bool writeAtomic (const std::filesystem::path& target, const std::string& text)
{
    namespace fs = std::filesystem;
    try
    {
        std::error_code ec;
        fs::create_directories (target.parent_path(), ec);
        const fs::path temp = target.string() + ".tmp";
        {
            std::ofstream out (temp, std::ios::binary | std::ios::trunc);
            out.write (text.data(), static_cast<std::streamsize> (text.size()));
            out.flush();
            if (! out)
            {
                out.close();
                fs::remove (temp, ec);
                return false;
            }
        }
        fs::rename (temp, target, ec);
        if (ec)
        {
            fs::remove (temp, ec);
            return false;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool readFile (const std::filesystem::path& path, std::string& text)
{
    try
    {
        std::error_code ec;
        if (! std::filesystem::is_regular_file (path, ec))
            return false;
        std::ifstream in (path, std::ios::binary);
        text.assign ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        return ! text.empty() && text.size() <= kMaxFileBytes;
    }
    catch (...)
    {
        return false;
    }
}

void putString (std::string& s, const std::string& text)
{
    s += '"';
    for (const char c : text)
    {
        if (c == '"' || c == '\\') s += '\\';
        s += c;
    }
    s += '"';
}
} // namespace

bool Preferences::operator== (const Preferences& o) const noexcept
{
    for (const PreferenceInfo& info : kTable)
        if (this->*(info.field) != o.*(info.field))
            return false;
    return true;
}

const PreferenceInfo* preferenceTable() noexcept { return kTable; }
size_t preferenceCount() noexcept { return sizeof (kTable) / sizeof (kTable[0]); }

std::string serializePreferences (const Preferences& prefs)
{
    std::string s = "{\"format\":\"synthyboy-preferences\",\"version\":1";
    for (const PreferenceInfo& info : kTable)
        s += std::string (",\"") + info.key + "\":" + (prefs.*(info.field) ? "true" : "false");
    s += "}";
    return s;
}

bool parsePreferences (const char* data, size_t size, Preferences& prefs) noexcept
{
    if (data == nullptr || size == 0 || size > kMaxFileBytes)
        return false;
    try
    {
        Cursor c { data, data + size };
        if (! c.eat ('{'))
            return false;
        Preferences parsed = prefs;
        bool formatOk = false;
        if (! c.eat ('}'))
            for (;;)
            {
                std::string key;
                if (! c.string (key) || ! c.eat (':'))
                    return false;
                c.ws();
                if (key == "format")
                {
                    std::string v;
                    if (! c.string (v)) return false;
                    formatOk = v == "synthyboy-preferences";
                }
                else if (c.p + 4 <= c.end && std::strncmp (c.p, "true", 4) == 0)
                {
                    c.p += 4;
                    for (const PreferenceInfo& info : kTable) if (key == info.key) parsed.*(info.field) = true;
                }
                else if (c.p + 5 <= c.end && std::strncmp (c.p, "false", 5) == 0)
                {
                    c.p += 5;
                    for (const PreferenceInfo& info : kTable) if (key == info.key) parsed.*(info.field) = false;
                }
                else if (! c.skip())
                    return false;
                if (c.eat (',')) continue;
                if (! c.eat ('}')) return false;
                break;
            }
        if (! formatOk)
            return false;
        prefs = parsed;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string preferencesPath (const std::string& dir) { return (std::filesystem::path (dir) / "preferences.json").string(); }

Preferences loadPreferences (const std::string& dir)
{
    Preferences prefs;
    std::string text;
    if (! dir.empty() && readFile (preferencesPath (dir), text))
        parsePreferences (text.data(), text.size(), prefs);
    return prefs;
}

bool savePreferences (const std::string& dir, const Preferences& prefs)
{
    return ! dir.empty() && writeAtomic (preferencesPath (dir), serializePreferences (prefs));
}

// ---- MIDI maps --------------------------------------------------------------------------------------------------------------------------

std::string serializeMidiMap (const std::vector<std::pair<int, std::string>>& bindings)
{
    std::string s = "{\"format\":\"synthyboy-midi-map\",\"version\":1,\"midi_cc\":[";
    bool first = true;
    for (const auto& b : bindings)
    {
        if (! first) s += ',';
        first = false;
        s += "{\"cc\":" + std::to_string (b.first) + ",\"key\":";
        putString (s, b.second);
        s += '}';
    }
    return s + "]}";
}

int parseMidiMap (const char* data, size_t size, std::vector<std::pair<int, std::string>>& bindings) noexcept
{
    if (data == nullptr || size == 0 || size > kMaxFileBytes)
        return -1;
    try
    {
        Cursor c { data, data + size };
        if (! c.eat ('{'))
            return -1;
        bool formatOk = false;
        std::vector<std::pair<int, std::string>> list;
        int taken = 0;
        if (! c.eat ('}'))
            for (;;)
            {
                std::string key;
                if (! c.string (key) || ! c.eat (':'))
                    return -1;
                if (key == "format")
                {
                    std::string v;
                    if (! c.string (v)) return -1;
                    formatOk = v == "synthyboy-midi-map";
                }
                else if (key == "midi_cc")
                {
                    if (! c.eat ('[')) return -1;
                    if (! c.eat (']'))
                        for (;;)
                        {
                            if (! c.eat ('{')) return -1;
                            int cc = -1;
                            std::string k;
                            if (! c.eat ('}'))
                                for (;;)
                                {
                                    std::string member;
                                    if (! c.string (member) || ! c.eat (':')) return -1;
                                    c.ws();
                                    if (member == "cc") { char* e = nullptr; cc = static_cast<int> (std::strtol (c.p, &e, 10)); if (e == c.p) return -1; c.p = e; }
                                    else if (member == "key") { if (! c.string (k)) return -1; }
                                    else if (! c.skip()) return -1;
                                    if (c.eat (',')) continue;
                                    if (! c.eat ('}')) return -1;
                                    break;
                                }
                            if (cc >= 0 && findParamByKey (k.c_str()) != nullptr && MidiCcMap::isLearnableCc (cc))
                            {
                                list.emplace_back (cc, k);
                                ++taken;
                            }
                            if (c.eat (',')) continue;
                            if (! c.eat (']')) return -1;
                            break;
                        }
                }
                else if (! c.skip())
                    return -1;
                if (c.eat (',')) continue;
                if (! c.eat ('}')) return -1;
                break;
            }
        if (! formatOk)
            return -1;
        bindings = std::move (list);
        return taken;
    }
    catch (...)
    {
        return -1;
    }
}

std::string defaultMidiMapPath (const std::string& dir) { return (std::filesystem::path (dir) / "midimap" / "Default.sbmidimap").string(); }

bool hasDefaultMidiMap (const std::string& dir)
{
    std::error_code ec;
    return ! dir.empty() && std::filesystem::is_regular_file (defaultMidiMapPath (dir), ec);
}

bool saveDefaultMidiMap (const std::string& dir, const std::vector<std::pair<int, std::string>>& bindings)
{
    return ! dir.empty() && writeAtomic (defaultMidiMapPath (dir), serializeMidiMap (bindings));
}

bool loadDefaultMidiMap (const std::string& dir, std::vector<std::pair<int, std::string>>& bindings)
{
    std::string text;
    if (dir.empty() || ! readFile (defaultMidiMapPath (dir), text))
        return false;
    return parseMidiMap (text.data(), text.size(), bindings) >= 0;
}

bool clearDefaultMidiMap (const std::string& dir)
{
    std::error_code ec;
    if (! dir.empty())
        std::filesystem::remove (defaultMidiMapPath (dir), ec);
    return ! hasDefaultMidiMap (dir);
}

std::string serumMidiParamKey (long long paramId)
{
    // module * 1,000,000 + index * 1,000 + parameter; module 7 is the macro module ("Macros CC14-21": 7000000, 7001000 ... 7007000)
    if (paramId < 0)
        return {};
    const long long module = paramId / 1000000, index = (paramId / 1000) % 1000, parameter = paramId % 1000;
    if (module == 7 && index >= 0 && index < 8 && parameter == 0)
        return "macro." + std::to_string (index + 1);
    return {};
}
} // namespace synthyboy
