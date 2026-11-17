// SynthyBoy - MPC UI component library: the name of a piece of content (a sample, an instrument, a wavetable file, an impulse) as the screen writes it. A file name can be as long as the player made it, so a name is shown by a rule
// and not cut by chance: the TITLE of the pack's manifest when the content has one, else the file name without its extension with the underscores as spaces; when that is wider than its box at the smallest legible size, the MIDDLE is
// replaced by an ellipsis (the start and the end of a name are what tells two names apart: "drum loop 1…20 bpm"), never the end. A text with an ellipsis that is not at its end is a content name by this rule (the layout lint accepts
// it; an ellipsis at the end of a text is a cut and a finding).
// Pure functions, no state.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstring>
#include <string>

#include "../overlay/sdf_font.h"

namespace synthyboy_mpc { namespace ui { namespace contentname {

/** The name to show: `title` (the pack's title for the file) when it is not empty, else `file` (a path or a file name) without the folder and the extension, underscores as spaces. */
inline std::string display (const std::string& title, const std::string& file)
{
    if (! title.empty())
    {
        std::string t = title;
        std::replace (t.begin(), t.end(), '_', ' ');   // a title that is a file name written with underscores reads as words too
        return t;
    }
    const size_t slash = file.rfind ('/');
    std::string s = slash == std::string::npos ? file : file.substr (slash + 1);
    const size_t dot = s.rfind ('.');
    if (dot != std::string::npos && dot != 0)
        s.erase (dot);
    std::replace (s.begin(), s.end(), '_', ' ');
    return s;
}

/** Writes `s` into out[cap]: whole when it is not wider than maxW at `size` (pixels per em), else the longest start and end that fit together with the ellipsis between them (the start one character longer than the end when
    the number of kept characters is odd). Cuts only at the beginning of a UTF-8 character. */
inline void middleFit (const char* s, float size, float maxW, char* out, int cap)
{
    const int n = static_cast<int> (std::strlen (s));
    if (cap < 8 || overlay::sdf::textWidth (s, n, size) <= maxW)
    {
        const int c = std::min (n, std::max (0, cap - 1));
        std::memcpy (out, s, static_cast<size_t> (c));
        out[c] = '\0';
        return;
    }
    static const char kEll[] = "\xE2\x80\xA6";
    auto boundary = [&] (int i) { return i <= 0 || i >= n || (static_cast<unsigned char> (s[i]) & 0xC0) != 0x80; };
    std::string best = std::string (kEll);
    for (int keep = n - 1; keep >= 1; --keep)
    {
        int head = (keep + 1) / 2, tail = keep / 2;
        while (head > 0 && ! boundary (head)) --head;
        while (tail > 0 && ! boundary (n - tail)) --tail;
        const std::string cand = std::string (s, static_cast<size_t> (head)) + kEll + std::string (s + n - tail, static_cast<size_t> (tail));
        if (static_cast<int> (cand.size()) < cap && overlay::sdf::textWidth (cand.c_str(), static_cast<int> (cand.size()), size) <= maxW)
        {
            best = cand;
            break;
        }
    }
    std::memcpy (out, best.c_str(), best.size() + 1);
}

}}} // namespace synthyboy_mpc::ui::contentname
