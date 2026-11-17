// The strings the plugin hands to the host (shell-mpc/host_strings.h, review finding F1): the bounded copy writes nothing past the string, and the name of every parameter of the table fits the bound of
// 32 bytes (31 characters + the NUL) with its Q-proxy suffixes, readably and without two parameters getting the same name. Usage: host_strings_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "host_strings.h"
#include "synthyboy/params.h"

using namespace synthyboy_mpc;

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        std::printf ("FAIL %s\n", what.c_str());
        ++fails;
    }
}

int main()
{
    // putBounded: exactly what fits, the NUL included, nothing past it touched
    for (size_t cap : { size_t (1), size_t (2), size_t (9), size_t (24), size_t (32), size_t (64) })
        for (const char* src : { "", "a", "12345678", "123456789", "Osc 1 Spectral Filter Wavetable Frame", static_cast<const char*> (nullptr) })
        {
            std::vector<char> buf (cap + 8, 'Z');
            putBounded (buf.data(), cap, src);
            const size_t want = src ? std::min (std::strlen (src), cap - 1) : 0;
            check (std::strlen (buf.data()) == want, "putBounded length cap " + std::to_string (cap) + " '" + (src ? src : "(null)") + "'");
            bool tail = true;
            for (size_t i = want + 1; i < buf.size(); ++i)
                tail = tail && buf[i] == 'Z';
            check (tail, "putBounded writes nothing past the NUL, cap " + std::to_string (cap));
        }
    {
        char b[4] = { 'Z', 'Z', 'Z', 'Z' };
        putBounded (b, 0, "abc");
        putBounded (nullptr, 4, "abc");
        check (b[0] == 'Z', "putBounded with capacity 0 writes nothing");
    }

    // every parameter name of the table: fits, readable (printable ASCII, no leading / trailing blank), unique
    const synthyboy::ParamInfo* table = synthyboy::paramTable();
    const size_t n = synthyboy::paramCount();
    std::set<std::string> seen;
    size_t longest = 0, shortened = 0;
    for (size_t i = 0; i < n; ++i)
    {
        char out[kParamStrBytes];
        std::memset (out, 'Z', sizeof out);
        shortName (table[i].name, out, sizeof out, kParamNameChars);
        const size_t len = ::strnlen (out, sizeof out);
        check (len < sizeof out, std::string ("terminated: ") + table[i].name);
        check (len <= kParamNameChars && len > 0, std::string ("fits: ") + table[i].name);
        longest = std::max (longest, len);
        if (std::strcmp (out, table[i].name) != 0)
            ++shortened;
        bool printable = len > 0 && out[0] != ' ' && out[len - 1] != ' ';
        for (size_t k = 0; k < len; ++k)
            printable = printable && out[k] >= 32 && out[k] < 127;
        check (printable, std::string ("readable: ") + table[i].name + " -> " + out);
        check (seen.insert (out).second, std::string ("unique: ") + table[i].name + " -> " + out);
        if (std::strlen (table[i].name) <= kParamNameChars)
            check (std::strcmp (out, table[i].name) == 0, std::string ("a name that fits is left alone: ") + table[i].name);
        // the same name again gives the same text
        char again[kParamStrBytes];
        shortName (table[i].name, again, sizeof again, kParamNameChars);
        check (std::strcmp (out, again) == 0, "deterministic");
        // the Q proxy: name + " Oct" / " Sem" (a split transpose) must fit too
        for (const char* suffix : { " Oct", " Sem", "" })
        {
            char q[kParamStrBytes];
            std::memset (q, 'Z', sizeof q);
            nameWithSuffix (table[i].name, suffix, q, sizeof q);
            const size_t ql = ::strnlen (q, sizeof q);
            check (ql < sizeof q && ql <= kParamNameChars, std::string ("Q proxy name fits: ") + table[i].name + suffix + " -> " + q);
            check (ql >= std::strlen (suffix) && std::strcmp (q + ql - std::strlen (suffix), suffix) == 0, std::string ("Q proxy name keeps its suffix: ") + q);
        }
    }
    std::printf ("INFO  %zu parameters, %zu names shortened, the longest has %zu characters\n", n, shortened, longest);
    check (shortened > 0 && shortened < 100, "some, but few, names are shortened");

    // the words of the examples in the review
    char o[kParamStrBytes];
    shortName ("Osc 1 Spectral Filter Wavetable Frame", o, sizeof o, kParamNameChars);
    check (std::strcmp (o, "Osc 1 Spectral Filter WT Frame") == 0, std::string ("example: ") + o);
    nameWithSuffix ("Osc 1 Unison Detune", "", o, sizeof o);
    check (std::strcmp (o, "Osc 1 Unison Detune") == 0, std::string ("a name that fits stays: ") + o);
    // a hostile name of any length still fits
    std::string huge (5000, 'x');
    shortName (huge.c_str(), o, sizeof o, kParamNameChars);
    check (std::strlen (o) == kParamNameChars, "an endless word is cut");
    shortName ("", o, sizeof o, kParamNameChars);
    check (o[0] == '\0', "empty name");

    std::printf (fails == 0 ? "OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
