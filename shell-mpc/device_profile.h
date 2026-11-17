// SynthyBoy - MPC shell: the quality profile of the device the plugin runs on (docs/MPC.md "Quality profile per device").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A profile is the engine's quality spelled out setting by setting: the polyphony cap, the unison cap, the interpolation of the sample oscillators, the effects that may run
// and the four economy groups of the own engine (core/include/synthyboy/quality.h, EconomyFlags). It is written as a text, a base and changes to it:
//
//     low | medium | high [, voices=1..32] [, unison=1..16] [, warp=2x|4x] [, morph=half|full] [, math=fast|exact] [, control=economy|full] [, fx=low|all] [, samples=low|medium|high]
//
// "low" is what the MPC build has always run (8 voices, 3 unison voices per oscillator, every economy group, the heavy effects bypassed); "medium" and "high" are the tiers of
// quality.h. Example: "low,unison=7,warp=4x" keeps the cheap approximations and gives seven unison voices and the 4x warps.
//
// Which profile a device gets is the table kDeviceProfiles below, keyed by the name the kernel gives the device (/proc/device-tree/model). A name that is not in the table gets
// kProfileUnknownDevice; when the name cannot be read at all the architecture of the build decides (the rows arm64 / arm32). A row is raised only after the device was measured
// with bench/tier_bench.cpp (docs/MPC.md "Quality profile per device" has the measured tables); every other row is "low". Changing a device's quality is an edit of one string.
//
// Overrides, the first that holds a valid profile wins:
//   1. the first line of the file /tmp/synthyboy-mpc-profile (the environment variable SYNTHYBOY_MPC_PROFILE_FILE names another file): tests and trials on the device
//   2. the environment variable SYNTHYBOY_MPC_PROFILE
//   3. the build option SYNTHYBOY_MPC_PROFILE (CMake cache entry of the same name, empty by default)
//   4. the table
// An override that does not parse is skipped and named in the report. The profile is resolved ONCE per process (the first engine that is prepared) and stays for the life of the
// process: every instance of the plugin runs the same profile, and it never changes while notes sound (the warp decimator's history and delay, the morph cycles, the filter
// coefficient holds and the voice pool's cap all depend on it). What is in force is written to /tmp/synthyboy-mpc-profile.txt (SYNTHYBOY_MPC_PROFILE_REPORT names another file).
// Nothing here runs on the audio thread. The presets and the saved state never hold a profile dependent value, so a project moves between devices unchanged.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "synthyboy/quality.h"
#include "write_files.h"

namespace synthyboy_mpc {

struct Profile
{
    int maxVoices = 8;       // polyphony cap
    int unison = 3;          // unison voices per oscillator, at most
    int samples = 0;         // interpolation of the sample oscillators: 0 low, 1 medium, 2 high (the engine's tier number)
    uint32_t fxMask = synthyboy::qualityForTier (synthyboy::Tier::low).fxMask;
    uint32_t economy = synthyboy::kEconomyAll;

    bool operator== (const Profile& o) const noexcept
    {
        return maxVoices == o.maxVoices && unison == o.unison && samples == o.samples && fxMask == o.fxMask && economy == o.economy;
    }
    bool operator!= (const Profile& o) const noexcept { return ! (*this == o); }
};

/** The three bases. "low" is the MPC build's quality since the first package: tier low with the engine's unison cap of 3. */
inline Profile profileLow() noexcept { return Profile {}; }
inline Profile profileMedium() noexcept
{
    const synthyboy::Quality q = synthyboy::qualityForTier (synthyboy::Tier::medium);
    return Profile { q.maxVoices, q.maxUnison, 1, q.fxMask, 0u };
}
inline Profile profileHigh() noexcept
{
    const synthyboy::Quality q = synthyboy::qualityForTier (synthyboy::Tier::high);
    return Profile { q.maxVoices, q.maxUnison, 2, q.fxMask, 0u };
}

/** Reads a profile text (the grammar at the top; spaces around the parts are ignored). False with a reason in `error` when it does not parse; `out` is then untouched. */
inline bool parseProfile (const std::string& text, Profile& out, std::string* error = nullptr)
{
    auto fail = [error] (const std::string& why) { if (error != nullptr) *error = why; return false; };
    auto trim = [] (std::string s)
    {
        while (! s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
        size_t a = 0;
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
        return s.substr (a);
    };
    auto number = [] (const std::string& s, int lo, int hi, int& v)
    {
        if (s.empty() || s.size() > 3)
            return false;
        int n = 0;
        for (const char c : s)
        {
            if (c < '0' || c > '9')
                return false;
            n = n * 10 + (c - '0');
        }
        if (n < lo || n > hi)
            return false;
        v = n;
        return true;
    };
    auto flag = [] (uint32_t& mask, uint32_t bit, bool on) { mask = on ? (mask | bit) : (mask & ~bit); };

    Profile p;
    bool first = true;
    size_t at = 0;
    while (at <= text.size())
    {
        const size_t comma = std::min (text.find (',', at), text.size());
        const std::string part = trim (text.substr (at, comma - at));
        at = comma + 1;
        if (first)
        {
            if (part == "low") p = profileLow();
            else if (part == "medium") p = profileMedium();
            else if (part == "high") p = profileHigh();
            else return fail ("the profile must start with low, medium or high, not '" + part + "'");
            first = false;
            continue;
        }
        const size_t eq = part.find ('=');
        if (eq == std::string::npos)
            return fail ("'" + part + "' is not key=value");
        const std::string key = trim (part.substr (0, eq)), value = trim (part.substr (eq + 1));
        if (key == "voices")       { if (! number (value, 1, synthyboy::kMaxVoices, p.maxVoices)) return fail ("voices must be 1 to 32, not '" + value + "'"); }
        else if (key == "unison")  { if (! number (value, 1, synthyboy::kMaxUnisonVoices, p.unison)) return fail ("unison must be 1 to 16, not '" + value + "'"); }
        else if (key == "warp")    { if (value != "2x" && value != "4x") return fail ("warp must be 2x or 4x, not '" + value + "'"); flag (p.economy, synthyboy::kEconomyWarp, value == "2x"); }
        else if (key == "morph")   { if (value != "half" && value != "full") return fail ("morph must be half or full, not '" + value + "'"); flag (p.economy, synthyboy::kEconomyMorph, value == "half"); }
        else if (key == "math")    { if (value != "fast" && value != "exact") return fail ("math must be fast or exact, not '" + value + "'"); flag (p.economy, synthyboy::kEconomyMath, value == "fast"); }
        else if (key == "control") { if (value != "economy" && value != "full") return fail ("control must be economy or full, not '" + value + "'"); flag (p.economy, synthyboy::kEconomyControl, value == "economy"); }
        else if (key == "fx")
        {
            if (value == "low") p.fxMask = profileLow().fxMask;
            else if (value == "all") p.fxMask = synthyboy::kFxAll;
            else return fail ("fx must be low or all, not '" + value + "'");
        }
        else if (key == "samples")
        {
            if (value == "low") p.samples = 0;
            else if (value == "medium") p.samples = 1;
            else if (value == "high") p.samples = 2;
            else return fail ("samples must be low, medium or high, not '" + value + "'");
        }
        else
            return fail ("unknown key '" + key + "'");
    }
    if (first)
        return fail ("empty profile");
    out = p;
    return true;
}

/** The profile as its full text, every setting named (what the report shows; parseProfile reads it back to the same profile). */
inline std::string describeProfile (const Profile& p)
{
    std::string t = "low,voices=" + std::to_string (p.maxVoices) + ",unison=" + std::to_string (p.unison);
    t += (p.economy & synthyboy::kEconomyWarp) != 0 ? ",warp=2x" : ",warp=4x";
    t += (p.economy & synthyboy::kEconomyMorph) != 0 ? ",morph=half" : ",morph=full";
    t += (p.economy & synthyboy::kEconomyMath) != 0 ? ",math=fast" : ",math=exact";
    t += (p.economy & synthyboy::kEconomyControl) != 0 ? ",control=economy" : ",control=full";
    t += p.fxMask == synthyboy::kFxAll ? ",fx=all" : ",fx=low";
    t += p.samples == 0 ? ",samples=low" : (p.samples == 1 ? ",samples=medium" : ",samples=high");
    return t;
}

/** The engine quality of a profile (realtime is the caller's). The economy groups are explicit, so the engine applies exactly these and takes the unison cap from maxUnison. */
inline synthyboy::Quality profileQuality (const Profile& p) noexcept
{
    synthyboy::Quality q;
    q.maxVoices = p.maxVoices;
    q.maxUnison = p.unison;
    q.oversampling = p.samples == 0 ? 1 : 2;
    q.interpolation = p.samples == 2 ? synthyboy::Interpolation::spectral : synthyboy::Interpolation::linear;
    q.fxMask = p.fxMask;
    q.economy = static_cast<int> (p.economy & synthyboy::kEconomyAll);
    return q;
}

// ---- the device table ------------------------------------------------------------------------------------------------------------------------------------------

struct DeviceProfileRow
{
    const char* model;   // the exact name of /proc/device-tree/model
    const char* profile; // a profile text
};

/** The measured names of the two devices; the line to edit after a measurement.
    MPC XL (aarch64, RK3588: 4 x Cortex-A55 + 4 x Cortex-A76, the host's audio workers on the A76 cores 4 - 7), measured 2026-11-17 on A76 core 6 at 2.4 GHz with the round-3 engine (all 112
    presets, 16 notes held): the highest step of the bench's ladder whose dearest factory preset stays within 50 % of the block at the 99th percentile: "high,voices=16,control=economy"
    (35.4 %, Lush Analog Pad, longest block 45.7 %, no block over the block's length; the step below, "low,unison=16,fx=all,voices=16,warp=4x,morph=full", is at 31.5 %; the next step,
    "high,voices=16" with the full control rate, is at 65.8 % and over the budget).
    MPC Key 61 (arm32, Cortex-A17 at 1.6 GHz): "low" already reaches the length of the block on its dearest presets with 8 notes held (99th percentile 100.7 % on core 3
    with the application running, 2026-11-17): no room for a step up until the engine is cheaper. */
constexpr DeviceProfileRow kDeviceProfiles[] = {
    { "Akai Professional MPC XL", "high,voices=16,control=economy" },
    { "Akai Professional MPC Key 61", "low" },
};
/** A device whose name was read and is not in the table. */
constexpr const char* kProfileUnknownDevice = "low";
/** The name could not be read (no device tree): the 64-bit build / the 32-bit build. */
constexpr const char* kProfileNoNameArm64 = "low";
constexpr const char* kProfileNoNameArm32 = "low";

struct ProfileChoice
{
    Profile profile;
    std::string text;    // the profile text that was taken (as written in its source)
    std::string source;  // "table: <model>", "table: unknown device <model>", "table: no device name, 64-bit build", "file <path>", "environment", "build option"
    std::string skipped; // overrides that did not parse, with the reason ("" when none)
};

/** The table alone, pure: `model` the device name ("" when it could not be read), `build64` the plugin is the 64-bit build. */
inline ProfileChoice profileFromTable (const std::string& model, bool build64)
{
    ProfileChoice c;
    c.text = model.empty() ? (build64 ? kProfileNoNameArm64 : kProfileNoNameArm32) : kProfileUnknownDevice;
    c.source = model.empty() ? std::string ("table: no device name, ") + (build64 ? "64-bit build" : "32-bit build") : "table: unknown device " + model;
    for (const DeviceProfileRow& row : kDeviceProfiles)
        if (model == row.model)
        {
            c.text = row.profile;
            c.source = "table: " + model;
        }
    if (! parseProfile (c.text, c.profile))
    {
        // a table entry that does not parse is a programming error (the host test parses every entry); the device still gets a sound: low
        c.skipped = "table entry '" + c.text + "' does not parse; ";
        c.text = "low";
        c.profile = profileLow();
    }
    return c;
}

/** The whole rule, pure: the overrides in their order (an empty text = not given), then the table. */
inline ProfileChoice chooseProfile (const std::string& model, bool build64, const std::string& fileLine, const std::string& filePath, const std::string& environment,
                                    const std::string& buildOption)
{
    struct Candidate { const std::string* text; std::string source; };
    const Candidate candidates[] = { { &fileLine, "file " + filePath }, { &environment, "environment SYNTHYBOY_MPC_PROFILE" }, { &buildOption, "build option SYNTHYBOY_MPC_PROFILE" } };
    std::string skipped;
    for (const Candidate& cand : candidates)
    {
        if (cand.text->empty())
            continue;
        Profile p;
        std::string why;
        if (parseProfile (*cand.text, p, &why))
        {
            ProfileChoice c;
            c.profile = p;
            c.text = *cand.text;
            c.source = cand.source;
            c.skipped = skipped;
            return c;
        }
        skipped += cand.source + ": " + why + "; ";
    }
    ProfileChoice c = profileFromTable (model, build64);
    c.skipped = skipped + c.skipped;
    return c;
}

/** The report of a choice: what one reads in /tmp/synthyboy-mpc-profile.txt. */
inline std::string profileReport (const ProfileChoice& c, const std::string& model, bool build64)
{
    return "profile: " + c.text + "\nprofile_settings: " + describeProfile (c.profile) + "\nprofile_source: " + c.source + "\nprofile_skipped: " + (c.skipped.empty() ? "none" : c.skipped)
         + "\ndevice_model: " + (model.empty() ? "(not readable)" : model) + "\nbuild: " + (build64 ? "64-bit" : "32-bit") + "\n";
}

#ifndef SYNTHYBOY_MPC_PROFILE
#define SYNTHYBOY_MPC_PROFILE ""
#endif

/** The profile of this process: resolved at the first call (never on the audio thread: it reads files), the same for every later call and every instance. */
inline const ProfileChoice& profileInForce()
{
    static const ProfileChoice choice = []
    {
        auto firstLine = [] (const std::string& path, size_t limit)
        {
            std::string text;
            if (FILE* f = std::fopen (path.c_str(), "rb"))
            {
                char buf[256];
                const size_t n = std::fread (buf, 1, std::min (limit, sizeof buf), f);
                std::fclose (f);
                for (size_t i = 0; i < n && buf[i] != '\0' && buf[i] != '\n' && buf[i] != '\r'; ++i)
                    text += buf[i];
                while (! text.empty() && text.back() == ' ')
                    text.pop_back();
            }
            return text;
        };
        auto env = [] (const char* name, const char* fallback) { const char* e = std::getenv (name); return std::string (e != nullptr && *e != '\0' ? e : fallback); };
#if defined(__aarch64__) || defined(__x86_64__) || defined(_M_X64)
        constexpr bool kBuild64 = true;
#else
        constexpr bool kBuild64 = false;
#endif
        const std::string model = firstLine ("/proc/device-tree/model", 255);
        const std::string filePath = env ("SYNTHYBOY_MPC_PROFILE_FILE", "/tmp/synthyboy-mpc-profile");
        ProfileChoice c = chooseProfile (model, kBuild64, firstLine (filePath, 255), filePath, env ("SYNTHYBOY_MPC_PROFILE", ""), SYNTHYBOY_MPC_PROFILE);
#if SYNTHYBOY_MPC_WRITE_FILES   // the release build (write_files.h) writes no report file
        if (FILE* f = std::fopen (env ("SYNTHYBOY_MPC_PROFILE_REPORT", "/tmp/synthyboy-mpc-profile.txt").c_str(), "w"))
        {
            const std::string report = profileReport (c, model, kBuild64);
            std::fwrite (report.data(), 1, report.size(), f);
            std::fclose (f);
        }
#endif
        return c;
    }();
    return choice;
}

} // namespace synthyboy_mpc
