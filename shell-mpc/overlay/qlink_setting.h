// SynthyBoy - MPC overlay: the host's option "Show Q-Link Status When Touched" (Preferences > General). The pop-up that the key (panel_key.h) works around is an option of the host; it is stored as
//   <VALUE name="ShowQLinksStatusOverlay" val="1"/>
// in /data/Settings/MPC/MPC.settings (an XML file of JUCE properties, about 77 KB on the Key 61; val 0 / 1). With the option off the pop-up never comes up and the key does nothing at all. The file is read
// when the overlay starts and again whenever its modification time (or size) changes: one stat call per check, the file itself is read only then. A missing file or key says "unknown": the key stays on.
// Reading only: the file is the host's and is never written.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include <sys/stat.h>

namespace synthyboy_mpc { namespace overlay {

enum class QLinkSetting { Unknown, Off, On };

/** The value of the option in the text of the settings file: the `val` of the VALUE element named ShowQLinksStatusOverlay (the attributes in either order). A value that is not a number or true / false: Unknown. */
inline QLinkSetting parseQLinkStatusSetting (const std::string& text)
{
    static const char* const kName = "name=\"ShowQLinksStatusOverlay\"";
    const size_t at = text.find (kName);
    if (at == std::string::npos)
        return QLinkSetting::Unknown;
    const size_t open = text.rfind ('<', at);
    const size_t close = text.find ('>', at);
    if (open == std::string::npos || close == std::string::npos)
        return QLinkSetting::Unknown;
    const std::string tag = text.substr (open, close - open);
    const size_t v = tag.find ("val=\"");
    if (v == std::string::npos)
        return QLinkSetting::Unknown;
    const size_t b = v + 5, e = tag.find ('"', b);
    if (e == std::string::npos)
        return QLinkSetting::Unknown;
    const std::string val = tag.substr (b, e - b);
    if (val == "0" || val == "false")
        return QLinkSetting::Off;
    if (val == "1" || val == "true")
        return QLinkSetting::On;
    return QLinkSetting::Unknown;
}

inline const char* qlinkSettingText (QLinkSetting s) noexcept { return s == QLinkSetting::Off ? "0" : (s == QLinkSetting::On ? "1" : "unknown"); }

/** The option read from a file, with the cheap check: poll () looks at the file's modification time and size and reads it only when they changed (or on the first call). */
class QLinkSettingFile
{
public:
    explicit QLinkSettingFile (std::string path = std::string()) : path_ (std::move (path)) {}
    void setPath (std::string path) { path_ = std::move (path); known_ = false; value_ = QLinkSetting::Unknown; }
    /** Checks the file; true when the value changed. */
    bool poll()
    {
        ++polls_;
        struct stat st;
        if (path_.empty() || ::stat (path_.c_str(), &st) != 0)
        {
            const bool changed = value_ != QLinkSetting::Unknown;
            known_ = false;
            value_ = QLinkSetting::Unknown;
            return changed;
        }
        const int64_t sec = static_cast<int64_t> (st.st_mtime);
#if defined(__APPLE__)
        const int64_t nsec = st.st_mtimespec.tv_nsec;
#else
        const int64_t nsec = st.st_mtim.tv_nsec;
#endif
        const int64_t size = static_cast<int64_t> (st.st_size);
        if (known_ && sec == sec_ && nsec == nsec_ && size == size_)
            return false;
        known_ = true;
        sec_ = sec;
        nsec_ = nsec;
        size_ = size;
        ++reads_;
        std::string text;
        if (FILE* f = std::fopen (path_.c_str(), "rb"))
        {
            char buf[8192];
            size_t n;
            while ((n = std::fread (buf, 1, sizeof buf, f)) > 0 && text.size() < (4u << 20))
                text.append (buf, n);
            std::fclose (f);
        }
        const QLinkSetting v = parseQLinkStatusSetting (text);
        const bool changed = v != value_;
        value_ = v;
        return changed;
    }
    QLinkSetting value() const noexcept { return value_; }
    /** The key may work: unless the option is known to be off. */
    bool keyOn() const noexcept { return value_ != QLinkSetting::Off; }
    unsigned long polls() const noexcept { return polls_; }
    unsigned long reads() const noexcept { return reads_; }
    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    QLinkSetting value_ = QLinkSetting::Unknown;
    bool known_ = false;
    int64_t sec_ = 0, nsec_ = 0, size_ = 0;
    unsigned long polls_ = 0, reads_ = 0;
};

}} // namespace synthyboy_mpc::overlay
