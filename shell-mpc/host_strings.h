// SynthyBoy - MPC shell: the strings the plugin hands to the host (names, units, value texts, program names) and how long they may be.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The host owns the buffer of effGetParamName / effGetParamLabel / effGetParamDisplay and the plugin is not told its size, so the plugin may write only what is promised. What the SDK promises
// (mpc_vst2_abi.h, mpc_plugin.h of the MPC SDK):
//   effGetProgramName / effGetProgramNameIndexed   char[kVstMaxProgNameLen + 1]   = 25 bytes
//   effGetEffectName                               char[kVstMaxEffectNameLen + 1] = 33 bytes
//   effGetVendorString / effGetProductString       char[64 + 1]
//   effGetParamName / Label / Display              the ABI header says kVstMaxParamStrLen = 8 ("the conservative limit"); the SDK's own framework (mpc_plugin.h, `kStr`: "we never write
//                                                  more than this many chars (+NUL) into host string buffers") writes at most 32 bytes including the NUL, and every plugin made with it
//                                                  runs on both devices. 32 bytes is the bound of this plugin: kParamStrBytes. The host's real buffer was not measured (it cannot be
//                                                  measured without overrunning it); longer names are not attempted, and 8 characters are not attempted either (2637 names do not fit them).
// Every string goes through putBounded (): at most cap - 1 characters and the NUL, nothing else of the host's buffer is touched (the old code wrote a NUL at offset 63 every time).
// A parameter name that does not fit is shortened by shortName (): a fixed word list, then dropping the inner vowels, then a cut; the same text every time, no table to keep in step.
#pragma once

#include <cstddef>
#include <cstring>

namespace synthyboy_mpc {

/** Bytes (the NUL included) the plugin writes into the host's buffer of a parameter name, unit label or value text. */
constexpr size_t kParamStrBytes = 32;
/** The longest parameter name in characters (the NUL excluded). */
constexpr size_t kParamNameChars = kParamStrBytes - 1;

/** Copies `src` into `dst` (capacity `cap`): at most cap - 1 characters, then the NUL; nothing past it is written. A null `src` is the empty string. */
inline void putBounded (char* dst, size_t cap, const char* src) noexcept
{
    if (dst == nullptr || cap == 0)
        return;
    size_t i = 0;
    if (src != nullptr)
        for (; i + 1 < cap && src[i] != '\0'; ++i)
            dst[i] = src[i];
    dst[i] = '\0';
}

namespace names_detail {

struct Abbrev { const char* word; const char* shortWord; };
// The word list, in the order it is applied (the longest savings and the words that appear in the long names first). Each entry replaces whole words only, every occurrence.
constexpr Abbrev kWords[] = {
    { "Multisample", "Multi" }, { "Wavetable", "WT" },     { "Spectral", "Spec" },    { "Transpose", "Transp" }, { "Envelope", "Env" },
    { "Frequency", "Freq" },    { "Velocity", "Vel" },     { "Override", "Ovr" },     { "Tracking", "Trk" },     { "Release", "Rel" },
    { "Modulation", "Mod" },    { "Oscillator", "Osc" },   { "Control", "Ctl" },      { "Random", "Rnd" },       { "Filter", "Filt" },
    { "Follow", "Fol" },        { "Smooth", "Smth" },      { "Sample", "Smp" },       { "Unison", "Uni" },       { "Detune", "Det" },
    { "Feedback", "Fdbk" },     { "Resonance", "Res" },    { "Amount", "Amt" },       { "Position", "Pos" },     { "Level", "Lvl" },        { "Down", "Dn" },
};

inline size_t lengthOf (const char* s) noexcept { return std::strlen (s); }

/** Replaces every whole word `word` of `buf` (length `len`, capacity `cap`) with `shortWord`; returns the new length. */
inline size_t replaceWord (char* buf, size_t len, const char* word, const char* shortWord) noexcept
{
    const size_t wl = std::strlen (word), sl = std::strlen (shortWord);
    for (size_t at = 0; at + wl <= len;)
    {
        const bool startOk = at == 0 || buf[at - 1] == ' ';
        const bool endOk = at + wl == len || buf[at + wl] == ' ';
        if (startOk && endOk && std::memcmp (buf + at, word, wl) == 0)
        {
            std::memmove (buf + at + sl, buf + at + wl, len - at - wl + 1);
            std::memcpy (buf + at, shortWord, sl);
            len = len - wl + sl;
            at += sl;
        }
        else
            ++at;
    }
    return len;
}

inline bool isLowerVowel (char c) noexcept { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }

} // namespace names_detail

/** `full` shortened to at most `maxChars` characters (the NUL is extra; `cap` >= maxChars + 1 is the capacity of `out`): the text itself when it fits; else 1. the words of the list replaced one entry at
    a time until it fits, 2. the lower case vowels inside words of more than 4 letters dropped from the last word to the first until it fits, 3. a cut (trailing blanks removed). Deterministic, no allocation. */
inline void shortName (const char* full, char* out, size_t cap, size_t maxChars) noexcept
{
    using namespace names_detail;
    if (out == nullptr || cap == 0)
        return;
    if (maxChars + 1 > cap)
        maxChars = cap - 1;
    char buf[160];
    putBounded (buf, sizeof buf, full);
    size_t len = lengthOf (buf);
    for (size_t i = 0; len > maxChars && i < sizeof kWords / sizeof kWords[0]; ++i)
        len = replaceWord (buf, len, kWords[i].word, kWords[i].shortWord);
    // the vowels of the words that are still long, from the last word backwards
    for (size_t end = len; len > maxChars && end > 0;)
    {
        size_t start = end;
        while (start > 0 && buf[start - 1] != ' ')
            --start;
        if (end - start > 4)
            for (size_t k = end; len > maxChars && k > start + 1;)
            {
                --k;
                if (isLowerVowel (buf[k]))
                {
                    std::memmove (buf + k, buf + k + 1, len - k);
                    --len;
                    --end;
                }
            }
        end = start > 0 ? start - 1 : 0;
    }
    if (len > maxChars)
    {
        len = maxChars;
        buf[len] = '\0';
        while (len > 0 && buf[len - 1] == ' ')
            buf[--len] = '\0';
    }
    putBounded (out, cap, buf);
}

/** `full` shortened (shortName) so that name and `suffix` (" Oct", " Sem": the two knobs of a split transpose) fit the bound together: what the Q proxies report for their target. */
inline void nameWithSuffix (const char* full, const char* suffix, char* out, size_t cap) noexcept
{
    char shortened[kParamStrBytes];
    const size_t suffixLen = suffix != nullptr ? std::strlen (suffix) : 0;
    shortName (full, shortened, sizeof shortened, suffixLen < kParamNameChars ? kParamNameChars - suffixLen : 0);
    if (out == nullptr || cap == 0)
        return;
    putBounded (out, cap, shortened);
    const size_t at = std::strlen (out);
    if (suffix != nullptr)
        putBounded (out + at, cap - at, suffix);
}

} // namespace synthyboy_mpc
