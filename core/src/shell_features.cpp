// SynthyBoy - core: MIDI learn table, locks, the `shell` state section and the user default patch. See shell_features.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shell_features.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>


namespace synthyboy
{
// ---- MidiCcMap ------------------------------------------------------------------------------------------------------------------------

bool MidiCcMap::isLearnableCc (int cc) noexcept
{
    if (cc < 1 || cc > 119)
        return false;
    return cc != 32 && cc != 64 && cc != 6 && cc != 38 && (cc < 96 || cc > 101);
}

int MidiCcMap::paramForCc (int cc) const noexcept
{
    return cc >= 0 && cc < kNumCc ? static_cast<int> (table[cc].load (std::memory_order_relaxed)) : -1;
}

int MidiCcMap::ccForParam (int paramIndex) const noexcept
{
    if (paramIndex < 0)
        return -1;
    for (int cc = 0; cc < kNumCc; ++cc)
        if (table[cc].load (std::memory_order_relaxed) == paramIndex)
            return cc;
    return -1;
}

int MidiCcMap::numMappings() const noexcept
{
    int n = 0;
    for (int cc = 0; cc < kNumCc; ++cc)
        if (table[cc].load (std::memory_order_relaxed) >= 0)
            ++n;
    return n;
}

bool MidiCcMap::assign (int cc, int paramIndex) noexcept
{
    if (! isLearnableCc (cc) || paramIndex < 0 || static_cast<size_t> (paramIndex) >= paramCount())
        return false;
    for (int other = 0; other < kNumCc; ++other)
        if (table[other].load (std::memory_order_relaxed) == paramIndex)
            table[other].store (-1, std::memory_order_relaxed);
    table[cc].store (static_cast<int16_t> (paramIndex), std::memory_order_relaxed);
    rev.fetch_add (1, std::memory_order_relaxed);
    return true;
}

bool MidiCcMap::assignKey (int cc, const char* key) noexcept
{
    const ParamInfo* info = key != nullptr ? findParamByKey (key) : nullptr;
    return info != nullptr && assign (cc, findParamIndex (info->id));
}

void MidiCcMap::clearCc (int cc) noexcept
{
    if (cc >= 0 && cc < kNumCc)
    {
        table[cc].store (-1, std::memory_order_relaxed);
        rev.fetch_add (1, std::memory_order_relaxed);
    }
}

void MidiCcMap::clearParam (int paramIndex) noexcept
{
    for (int cc = 0; cc < kNumCc; ++cc)
        if (paramIndex >= 0 && table[cc].load (std::memory_order_relaxed) == paramIndex)
            table[cc].store (-1, std::memory_order_relaxed);
    rev.fetch_add (1, std::memory_order_relaxed);
}

void MidiCcMap::clearAll() noexcept
{
    for (auto& t : table)
        t.store (-1, std::memory_order_relaxed);
    armed.store (-1, std::memory_order_relaxed);
    rev.fetch_add (1, std::memory_order_relaxed);
}

void MidiCcMap::armLearn (int paramIndex) noexcept
{
    const bool ok = paramIndex >= 0 && static_cast<size_t> (paramIndex) < paramCount();
    armed.store (ok ? paramIndex : -1, std::memory_order_relaxed);
    rev.fetch_add (1, std::memory_order_relaxed);
}

MidiCcMap::Hit MidiCcMap::handleCc (int cc, int value) noexcept
{
    Hit hit;
    if (cc < 0 || cc >= kNumCc)
        return hit;

    const int learning = armed.load (std::memory_order_relaxed);
    if (learning >= 0 && isLearnableCc (cc))
    {
        if (assign (cc, learning))
        {
            armed.store (-1, std::memory_order_relaxed);
            hit.paramIndex = learning;
            hit.learned = true;
        }
        return hit;
    }

    const int index = paramForCc (cc);
    if (index < 0)
        return hit;
    const ParamInfo& info = paramTable()[index];
    const float norm = static_cast<float> (std::clamp (value, 0, 127)) / 127.0f;
    hit.paramIndex = index;
    hit.plain = constrainValue (info, fromNormalised (info, norm));
    hit.apply = true;
    return hit;
}

std::vector<std::pair<int, std::string>> MidiCcMap::bindings() const
{
    std::vector<std::pair<int, std::string>> out;
    for (int cc = 0; cc < kNumCc; ++cc)
    {
        const int index = paramForCc (cc);
        if (index >= 0 && static_cast<size_t> (index) < paramCount())
            out.emplace_back (cc, paramTable()[index].key);
    }
    return out;
}

int MidiCcMap::setBindings (const std::vector<std::pair<int, std::string>>& list)
{
    clearAll();
    int taken = 0;
    for (const auto& b : list)
        if (assignKey (b.first, b.second.c_str()))
            ++taken;
    return taken;
}

// ---- locks ----------------------------------------------------------------------------------------------------------------------------

bool isValidLockName (const std::string& name) noexcept
{
    if (name.empty() || name.size() > kMaxLockNameBytes)
        return false;
    for (const char c : name)
        if (! ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_'))
            return false;
    return name.front() != '.' && name.back() != '.';
}

bool lockCoversKey (const std::string& lock, const char* key) noexcept
{
    if (key == nullptr || lock.empty())
        return false;
    const size_t n = lock.size();
    if (std::strncmp (key, lock.c_str(), n) != 0)
        return false;
    return key[n] == '\0' || key[n] == '.';
}

bool isLocked (const std::vector<std::string>& locks, const std::string& name) noexcept
{
    return std::find (locks.begin(), locks.end(), name) != locks.end();
}

bool setLocked (std::vector<std::string>& locks, const std::string& name, bool locked)
{
    if (! isValidLockName (name))
        return false;
    const auto it = std::lower_bound (locks.begin(), locks.end(), name);
    const bool present = it != locks.end() && *it == name;
    if (locked && ! present)
    {
        if (locks.size() >= kMaxLocks)
            return false;
        locks.insert (it, name);
    }
    else if (! locked && present)
    {
        locks.erase (it);
    }
    return true;
}

int keepLockedValues (ParamValues& patch, const ParamValues& current, const std::vector<std::string>& locks) noexcept
{
    int restored = 0;
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        for (const auto& lock : locks)
            if (lockCoversKey (lock, table[i].key))
            {
                if (patch.plain[i] != current.plain[i])
                    ++restored;
                patch.plain[i] = current.plain[i];
                break;
            }
    return restored;
}

namespace
{
/** The module number when `locks` contains "<stem>N" for this N (e.g. stem "osc", N = 2), else false. */
bool lockedModule (const std::vector<std::string>& locks, const char* stem, int number)
{
    return isLocked (locks, std::string (stem) + std::to_string (number));
}

template <typename T, typename Pred>
void keepEntries (std::vector<T>& patch, const std::vector<T>& current, Pred locked)
{
    patch.erase (std::remove_if (patch.begin(), patch.end(), [&] (const T& e) { return locked (e); }), patch.end());
    for (const auto& e : current)
        if (locked (e))
            patch.push_back (e);
}
} // namespace

bool keepLockedContent (StateContent& patch, const StateContent& current, const std::vector<std::string>& locks)
{
    if (locks.empty())
        return false;
    const StateContent before = patch;
    keepEntries (patch.wavetables, current.wavetables, [&] (const ContentWavetable& e) { return lockedModule (locks, "osc", e.osc); });
    keepEntries (patch.samples, current.samples, [&] (const ContentSample& e) { return lockedModule (locks, "osc", e.osc); });
    keepEntries (patch.remaps, current.remaps, [&] (const ContentRemap& e) { return lockedModule (locks, "osc", e.osc); });
    keepEntries (patch.lfoShapes, current.lfoShapes, [&] (const ContentLfoShape& e) { return lockedModule (locks, "lfo", e.lfo); });
    keepEntries (patch.filterCurves, current.filterCurves, [&] (const ContentFilterCurve& e) { return isLocked (locks, e.filter); });
    // keep the order the writer expects (ascending oscillator / LFO / filter)
    std::sort (patch.wavetables.begin(), patch.wavetables.end(), [] (const auto& a, const auto& b) { return a.osc < b.osc; });
    std::sort (patch.samples.begin(), patch.samples.end(), [] (const auto& a, const auto& b) { return a.osc < b.osc; });
    std::sort (patch.remaps.begin(), patch.remaps.end(), [] (const auto& a, const auto& b) { return a.osc != b.osc ? a.osc < b.osc : a.slot < b.slot; });
    std::sort (patch.lfoShapes.begin(), patch.lfoShapes.end(), [] (const auto& a, const auto& b) { return a.lfo < b.lfo; });
    std::sort (patch.filterCurves.begin(), patch.filterCurves.end(), [] (const auto& a, const auto& b) { return a.filter < b.filter; });
    return patch != before;
}

// ---- the shell section ----------------------------------------------------------------------------------------------------------------

namespace
{
void putString (std::string& s, const std::string& text)
{
    s += '"';
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            s += '\\';
        s += c;
    }
    s += '"';
}

class Scan
{
public:
    Scan (const char* b, const char* e) noexcept : p (b), end (e) {}

    const char* p;
    const char* end;

    void ws() noexcept
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }
    bool eat (char c) noexcept
    {
        ws();
        if (p < end && *p == c)
        {
            ++p;
            return true;
        }
        return false;
    }
    bool peekIs (char c) noexcept
    {
        ws();
        return p < end && *p == c;
    }

    /** A string; keeps at most `cap` bytes (`tooLong` set when more). */
    bool str (std::string& out, size_t cap, bool& tooLong) noexcept
    {
        ws();
        if (p >= end || *p != '"')
            return false;
        ++p;
        out.clear();
        tooLong = false;
        while (p < end && *p != '"')
        {
            char c = *p++;
            if (c == '\\')
            {
                if (p >= end)
                    return false;
                const char e = *p++;
                switch (e)
                {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u':
                        if (end - p < 4)
                            return false;
                        p += 4;
                        c = '?';
                        break;
                    default: c = e; break;
                }
            }
            if (out.size() < cap)
                out += c;
            else
                tooLong = true;
        }
        if (p >= end)
            return false;
        ++p;
        return true;
    }

    bool number (double& v) noexcept
    {
        ws();
        char* stop = nullptr;
        const size_t avail = static_cast<size_t> (end - p);
        char buf[40];
        const size_t n = std::min (avail, sizeof buf - 1);
        std::memcpy (buf, p, n);
        buf[n] = '\0';
        v = std::strtod (buf, &stop);
        if (stop == buf)
            return false;
        p += stop - buf;
        return true;
    }

    /** Skips one value of any kind; nesting is counted (no recursion). */
    bool skip() noexcept
    {
        ws();
        if (p >= end)
            return false;
        if (*p == '"')
        {
            std::string dummy;
            bool tooLong = false;
            return str (dummy, 0, tooLong);
        }
        if (*p == '{' || *p == '[')
        {
            int depth = 0;
            while (p < end)
            {
                const char c = *p;
                if (c == '"')
                {
                    std::string dummy;
                    bool tooLong = false;
                    if (! str (dummy, 0, tooLong))
                        return false;
                    continue;
                }
                ++p;
                if (c == '{' || c == '[')
                    ++depth;
                else if (c == '}' || c == ']')
                {
                    if (--depth == 0)
                        return true;
                }
            }
            return false;
        }
        const char* start = p;
        while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t')
            ++p;
        return p > start;
    }
};

/** Parses the value of "shell". Returns false when the section is malformed or outside the caps. */
bool parseShell (Scan& sc, ShellState& out)
{
    if (! sc.eat ('{'))
        return false;
    if (sc.eat ('}'))
        return true;
    for (;;)
    {
        std::string key;
        bool tooLong = false;
        if (! sc.str (key, 32, tooLong) || ! sc.eat (':'))
            return false;
        if (! tooLong && key == "midi_cc")
        {
            if (! sc.eat ('['))
                return false;
            if (! sc.eat (']'))
                for (;;)
                {
                    if (! sc.eat ('{'))
                        return false;
                    int cc = -1;
                    std::string pkey;
                    bool haveKey = false;
                    if (! sc.eat ('}'))
                        for (;;)
                        {
                            std::string field;
                            bool fl = false;
                            if (! sc.str (field, 16, fl) || ! sc.eat (':'))
                                return false;
                            if (! fl && field == "cc")
                            {
                                double v = 0.0;
                                if (! sc.number (v) || v != std::floor (v) || v < 0.0 || v > 127.0)
                                    return false;
                                cc = static_cast<int> (v);
                            }
                            else if (! fl && field == "key")
                            {
                                bool kl = false;
                                if (! sc.str (pkey, 96, kl) || kl)
                                    return false;
                                haveKey = true;
                            }
                            else if (! sc.skip())
                                return false;
                            if (sc.eat (','))
                                continue;
                            if (sc.eat ('}'))
                                break;
                            return false;
                        }
                    if (out.midiCc.size() >= static_cast<size_t> (MidiCcMap::kNumCc))
                        return false;
                    if (cc >= 0 && haveKey)
                        out.midiCc.emplace_back (cc, pkey);
                    if (sc.eat (','))
                        continue;
                    if (sc.eat (']'))
                        break;
                    return false;
                }
        }
        else if (! tooLong && key == "locks")
        {
            if (! sc.eat ('['))
                return false;
            if (! sc.eat (']'))
                for (;;)
                {
                    std::string name;
                    bool nl = false;
                    if (! sc.str (name, kMaxLockNameBytes, nl))
                        return false;
                    if (out.locks.size() >= kMaxLocks)
                        return false;
                    if (! nl && isValidLockName (name))
                        setLocked (out.locks, name, true);
                    if (sc.eat (','))
                        continue;
                    if (sc.eat (']'))
                        break;
                    return false;
                }
        }
        else if (! sc.skip())
        {
            return false;
        }
        if (sc.eat (','))
            continue;
        return sc.eat ('}');
    }
}
} // namespace

std::string saveShellSection (const ShellState& shell)
{
    std::string s = "{\"midi_cc\":[";
    bool first = true;
    for (const auto& b : shell.midiCc)
    {
        if (! first)
            s += ',';
        first = false;
        s += "{\"cc\":" + std::to_string (b.first) + ",\"key\":";
        putString (s, b.second);
        s += '}';
    }
    s += "],\"locks\":[";
    first = true;
    for (const auto& l : shell.locks)
    {
        if (! first)
            s += ',';
        first = false;
        putString (s, l);
    }
    s += "]}";
    return s;
}

std::string withShellSection (const std::string& stateDocument, const ShellState& shell)
{
    const size_t close = stateDocument.find_last_not_of (" \t\r\n");
    if (close == std::string::npos || stateDocument[close] != '}')
        return stateDocument;
    std::string s = stateDocument.substr (0, close);
    s += ",\"shell\":";
    s += saveShellSection (shell);
    s += '}';
    return s;
}

ShellStatus loadShellSection (const void* data, size_t size, ShellState& out) noexcept
{
    if (data == nullptr || size == 0 || size > kMaxStateBytes)
        return ShellStatus::invalid;
    try
    {
        const char* b = static_cast<const char*> (data);
        Scan sc (b, b + size);
        if (size >= 3 && static_cast<unsigned char> (b[0]) == 0xef && static_cast<unsigned char> (b[1]) == 0xbb && static_cast<unsigned char> (b[2]) == 0xbf)
            sc.p += 3;
        if (! sc.eat ('{'))
            return ShellStatus::invalid;
        if (sc.eat ('}'))
            return ShellStatus::absent;
        for (;;)
        {
            std::string key;
            bool tooLong = false;
            if (! sc.str (key, 32, tooLong) || ! sc.eat (':'))
                return ShellStatus::invalid;
            if (! tooLong && key == "shell")
            {
                ShellState parsed;
                if (! parseShell (sc, parsed))
                {
                    out = ShellState {};
                    return ShellStatus::rejected;
                }
                out = std::move (parsed);
                return ShellStatus::ok;
            }
            if (! sc.skip())
                return ShellStatus::invalid;
            if (sc.eat (','))
                continue;
            return sc.eat ('}') ? ShellStatus::absent : ShellStatus::invalid;
        }
    }
    catch (...)
    {
        return ShellStatus::invalid;
    }
}

// ---- Save as Default ------------------------------------------------------------------------------------------------------------------

std::string userDataDirectory()
{
    namespace fs = std::filesystem;
    if (const char* env = std::getenv ("SYNTHYBOY_USER_DIR"); env != nullptr && env[0] != '\0')
        return env;
    const char* home = std::getenv ("HOME");
   #if defined(_WIN32)
    if (const char* appData = std::getenv ("APPDATA"); appData != nullptr && appData[0] != '\0')
        return (fs::path (appData) / "SynthyBoy").string();
    home = std::getenv ("USERPROFILE");
    return home != nullptr && home[0] != '\0' ? (fs::path (home) / "AppData" / "Roaming" / "SynthyBoy").string() : std::string();
   #elif defined(__APPLE__)
    return home != nullptr && home[0] != '\0' ? (fs::path (home) / "Library" / "Application Support" / "SynthyBoy").string() : std::string();
   #else
    if (const char* xdg = std::getenv ("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0')
        return (fs::path (xdg) / "SynthyBoy").string();
    return home != nullptr && home[0] != '\0' ? (fs::path (home) / ".local" / "share" / "SynthyBoy").string() : std::string();
   #endif
}

std::string userDefaultPath (const std::string& dir)
{
    return (std::filesystem::path (dir) / "default.sbstate").string();
}

bool saveUserDefault (const std::string& dir, const std::string& stateDocument)
{
    namespace fs = std::filesystem;
    if (dir.empty() || stateDocument.empty() || stateDocument.size() > kMaxStateBytes)
        return false;
    try
    {
        std::error_code ec;
        fs::create_directories (dir, ec);
        const fs::path target = userDefaultPath (dir);
        const fs::path temp = target.string() + ".tmp";
        {
            std::ofstream out (temp, std::ios::binary | std::ios::trunc);
            out.write (stateDocument.data(), static_cast<std::streamsize> (stateDocument.size()));
            out.flush();
            if (! out)
            {
                out.close();
                fs::remove (temp, ec);
                return false;
            }
        }
        fs::rename (temp, target, ec); // replaces an existing file on POSIX; Windows' rename of std::filesystem replaces too
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

bool hasUserDefault (const std::string& dir)
{
    std::error_code ec;
    return ! dir.empty() && std::filesystem::is_regular_file (userDefaultPath (dir), ec);
}

bool loadUserDefault (const std::string& dir, ParamValues& values, StateContent& content)
{
    if (! hasUserDefault (dir))
        return false;
    try
    {
        std::ifstream in (userDefaultPath (dir), std::ios::binary);
        std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        if (text.empty() || text.size() > kMaxStateBytes)
            return false;
        ParamValues v = ParamValues::defaults();
        StateContent c;
        const auto result = loadStateWithContent (text.data(), text.size(), v, c);
        if (result.status != LoadStatus::ok && result.status != LoadStatus::newer_version)
            return false;
        values = v;
        content = std::move (c);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool clearUserDefault (const std::string& dir)
{
    std::error_code ec;
    if (! dir.empty())
        std::filesystem::remove (userDefaultPath (dir), ec);
    return ! hasUserDefault (dir);
}
} // namespace synthyboy
