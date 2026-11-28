// SynthyBoy - shell-mpc host test: the quality profile per device (shell-mpc/device_profile.h): the profile texts, the device table, the overrides and their order, the report.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   device_profile_host            every check; the process-wide choice is taken from a debug file this test writes
//   device_profile_host --print    prints the report of this machine with no override of its own (what the plugin would run here)
#include "../device_profile.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

using namespace synthyboy_mpc;

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++fails;
}

int main (int argc, char** argv)
{
    if (argc > 1 && std::string (argv[1]) == "--print")
    {
        const ProfileChoice& c = profileInForce();
        std::printf ("%s", profileReport (c, "(see the report file)", sizeof (void*) == 8).c_str());
        return 0;
    }

    const synthyboy::Quality low = synthyboy::qualityForTier (synthyboy::Tier::low);
    const synthyboy::Quality medium = synthyboy::qualityForTier (synthyboy::Tier::medium);
    const synthyboy::Quality high = synthyboy::qualityForTier (synthyboy::Tier::high);

    // ---- the bases ---------------------------------------------------------------------------------------------------------------------------------
    {
        Profile p;
        check (parseProfile ("low", p) && p == profileLow(), "'low' is the base profile");
        const synthyboy::Quality q = profileQuality (p);
        check (q.maxVoices == low.maxVoices && q.maxUnison == 3 && q.oversampling == low.oversampling && q.interpolation == low.interpolation && q.fxMask == low.fxMask,
               "low: the voices, the oversampling, the interpolation and the effect mask of tier low, and the engine's unison cap of 3 as maxUnison");
        check (q.economy == static_cast<int> (synthyboy::kEconomyAll) && synthyboy::economyOf (q) == synthyboy::economyOf (low), "low: every economy group, as tier low");
        check (! q.realtime, "the profile does not decide realtime (the shell sets it)");

        check (parseProfile ("medium", p) && p == profileMedium(), "'medium' parses");
        const synthyboy::Quality qm = profileQuality (p);
        check (qm.maxVoices == medium.maxVoices && qm.maxUnison == medium.maxUnison && qm.oversampling == medium.oversampling && qm.interpolation == medium.interpolation
               && qm.fxMask == medium.fxMask && synthyboy::economyOf (qm) == synthyboy::economyOf (medium) && synthyboy::economyOf (qm) == 0u, "medium: the quality of tier medium, no economy group");

        check (parseProfile ("high", p) && p == profileHigh(), "'high' parses");
        const synthyboy::Quality qh = profileQuality (p);
        check (qh.maxVoices == high.maxVoices && qh.maxUnison == high.maxUnison && qh.oversampling == high.oversampling && qh.interpolation == high.interpolation
               && qh.fxMask == high.fxMask && synthyboy::economyOf (qh) == 0u, "high: the quality of tier high, no economy group");
    }

    // ---- the single settings -----------------------------------------------------------------------------------------------------------------------
    {
        Profile p;
        check (parseProfile ("low,unison=7,warp=4x", p) && p.unison == 7 && p.economy == (synthyboy::kEconomyAll & ~synthyboy::kEconomyWarp) && p.maxVoices == 8 && p.fxMask == low.fxMask,
               "low,unison=7,warp=4x: seven unison voices and the 4x warps, everything else as low");
        check (parseProfile (" low , morph = full , math=exact,control=full ", p) && p.economy == synthyboy::kEconomyWarp, "spaces are ignored; morph, math and control switch their group off");
        check (parseProfile ("high,warp=2x,morph=half,math=fast,control=economy", p) && p.economy == synthyboy::kEconomyAll && p.maxVoices == 32, "the groups can be switched on over a higher base");
        check (parseProfile ("low,voices=16,fx=all,samples=high", p) && p.maxVoices == 16 && p.fxMask == synthyboy::kFxAll && p.samples == 2, "voices, fx and samples");
        const synthyboy::Quality q = profileQuality (p);
        check (q.oversampling == 2 && q.interpolation == synthyboy::Interpolation::spectral && q.economy == static_cast<int> (synthyboy::kEconomyAll),
               "samples=high asks for the high interpolation of the sample oscillators and leaves the economy groups alone");
        check (parseProfile ("medium,fx=low", p) && p.fxMask == low.fxMask, "fx=low over medium");
        check (parseProfile ("low,voices=1,unison=16", p) && p.maxVoices == 1 && p.unison == 16, "the ends of the ranges");

        // every profile reads back from its description
        for (const char* text : { "low", "medium", "high", "low,unison=7,warp=4x", "low,voices=16,fx=all,samples=high", "high,math=fast", "medium,control=economy,samples=low" })
        {
            Profile a, b;
            check (parseProfile (text, a) && parseProfile (describeProfile (a), b) && a == b, std::string ("the description of '") + text + "' reads back to the same profile: " + describeProfile (a));
        }
        check (describeProfile (profileLow()) == "low,voices=8,unison=3,warp=2x,morph=half,math=fast,control=economy,start=stagger,fx=low,samples=low", "the description of low names every setting");
    }

    // ---- the Spectral start cache: on at tier low only (owner decision 2026-11-21), bit-identical memo ---------------------------------------------------
    {
        Profile p;
        check (parseProfile ("low", p), "start cache: 'low' parses");
        check (synthyboy::spectralStartCacheOf (profileQuality (p)), "start cache: the low profile enables it");
        check (synthyboy::spectralStartCacheOf (low), "start cache: tier low enables it");
        check (parseProfile ("medium", p) && ! synthyboy::spectralStartCacheOf (profileQuality (p)), "start cache: the medium profile does not");
        check (parseProfile ("high", p) && ! synthyboy::spectralStartCacheOf (profileQuality (p)), "start cache: the high profile does not");
        check (! synthyboy::spectralStartCacheOf (medium) && ! synthyboy::spectralStartCacheOf (high), "start cache: tiers medium and high do not");
        synthyboy::Quality offLow = low;
        offLow.spectralStartCache = 0;
        synthyboy::Quality onHigh = high;
        onHigh.spectralStartCache = 1;
        check (! synthyboy::spectralStartCacheOf (offLow) && synthyboy::spectralStartCacheOf (onHigh), "start cache: an explicit 0 / 1 overrides the tier (bench)");
    }

    // ---- the Spectral start stagger (docs/plans/spectral-start.md S2) --------------------------------------------------------------------------------------
    {
        check (synthyboy::spectralStartOf (low), "spectralStartOf: tier low staggers the Spectral start");
        check (! synthyboy::spectralStartOf (medium) && ! synthyboy::spectralStartOf (high) && ! synthyboy::spectralStartOf (synthyboy::Quality {}), "spectralStartOf: tiers medium and high and the default quality do not");
        synthyboy::Quality noGrid = low;
        noGrid.economy = 0;
        check (! synthyboy::spectralStartOf (noGrid), "spectralStartOf: without the control economy there is no 32 sample grid, so no stagger, whatever the tier says");
        synthyboy::Quality off = low;
        off.spectralStart = 0;
        check (! synthyboy::spectralStartOf (off), "spectralStartOf: spectralStart = 0 switches it off at tier low");
        synthyboy::Quality forced = high;
        forced.economy = static_cast<int> (synthyboy::kEconomyControl);
        forced.spectralStart = 1;
        check (synthyboy::spectralStartOf (forced), "spectralStartOf: spectralStart = 1 with the control economy switches it on at tier high");

        Profile p;
        check (parseProfile ("low,start=off", p) && ! p.stagger && ! synthyboy::spectralStartOf (profileQuality (p)), "low,start=off: no stagger");
        Profile back;
        check (describeProfile (p).find (",start=off,") != std::string::npos && parseProfile (describeProfile (p), back) && back == p, "start=off is described and reads back");
        check (parseProfile ("low,start=stagger", p) && p.stagger && p == profileLow(), "start=stagger is the base of low");
        std::string why;
        check (! parseProfile ("low,start=maybe", p, &why) && why == "start must be stagger or off, not 'maybe'", "low,start=maybe is refused with the reason: " + why);
        check (parseProfile ("high,voices=16,control=economy", p) && ! p.stagger && ! synthyboy::spectralStartOf (profileQuality (p)), "the XL text: control economy, no stagger (it keeps today's instant start)");
        check (parseProfile ("low", p) && synthyboy::spectralStartOf (profileQuality (p)), "the Key 61 text 'low': stagger");
        check (! profileMedium().stagger && ! profileHigh().stagger, "medium and high do not stagger");
        check (parseProfile ("high,control=economy,start=stagger", p) && synthyboy::spectralStartOf (profileQuality (p)), "start=stagger can be switched on over a higher base");
    }

    // ---- what does not parse -----------------------------------------------------------------------------------------------------------------------
    {
        Profile p = profileHigh();
        std::string why;
        for (const char* bad : { "", " ", "ultra", "low,", "low,unison", "low,unison=0", "low,unison=17", "low,voices=33", "low,voices=0", "low,voices=8x", "low,warp=8x", "low,morph=fast",
                                 "low,math=on", "low,control=1", "low,fx=none", "low,samples=best", "low,tier=high", "unison=7", "low;unison=7", "low,unison=7777", "low,unison=-1" })
        {
            why.clear();
            check (! parseProfile (bad, p, &why) && ! why.empty() && p == profileHigh(), std::string ("'") + bad + "' is refused with a reason and leaves the profile alone: " + why);
        }
    }

    // ---- the table ---------------------------------------------------------------------------------------------------------------------------------
    {
        // every text of the table parses (a typo in a table edit fails here, not on the device)
        for (const DeviceProfileRow& row : kDeviceProfiles)
        {
            Profile p;
            std::string why;
            check (parseProfile (row.profile, p, &why), std::string ("table row '") + row.model + "' holds a profile that parses: " + row.profile + " " + why);
        }
        Profile p;
        check (parseProfile (kProfileUnknownDevice, p) && parseProfile (kProfileNoNameArm64, p) && parseProfile (kProfileNoNameArm32, p), "the three fallback rows parse");

        bool hasXl = false, hasKey = false;
        for (const DeviceProfileRow& row : kDeviceProfiles)
        {
            hasXl = hasXl || std::string (row.model) == "Akai Professional MPC XL";
            hasKey = hasKey || std::string (row.model) == "Akai Professional MPC Key 61";
        }
        check (hasXl && hasKey, "the table has the two measured names: 'Akai Professional MPC XL' and 'Akai Professional MPC Key 61'");

        ProfileChoice c = profileFromTable ("Akai Professional MPC XL", true);
        check (c.source == "table: Akai Professional MPC XL" && c.skipped.empty(), "the MPC XL is found by its exact name: " + c.source);
        Profile row;
        check (parseProfile (kDeviceProfiles[0].profile, row) && c.profile == row, "the MPC XL gets the profile of its row");
        c = profileFromTable ("Akai Professional MPC Key 61", false);
        check (c.source == "table: Akai Professional MPC Key 61", "the Key 61 is found by its exact name: " + c.source);
        c = profileFromTable ("Akai Professional MPC Live III", true);
        Profile unknown;
        check (parseProfile (kProfileUnknownDevice, unknown) && c.profile == unknown && c.source == "table: unknown device Akai Professional MPC Live III", "a name that is not in the table is an unknown device: " + c.source);
        c = profileFromTable ("Akai Professional MPC XL SE", true);
        check (c.source.rfind ("table: unknown device", 0) == 0, "a longer name is not the XL (exact match only): " + c.source);
        c = profileFromTable ("", true);
        check (c.source == "table: no device name, 64-bit build", "no name, 64-bit build: the architecture row: " + c.source);
        c = profileFromTable ("", false);
        check (c.source == "table: no device name, 32-bit build", "no name, 32-bit build: the architecture row: " + c.source);

        // WHAT THE PACKAGE RUNS, said on purpose (change it with the table): the MPC XL the profile its bench run of 2026-11-17 (round-3 engine) allows, every other device, known or not,
        // the profile the MPC build has always run.
        const Profile xl = profileFromTable ("Akai Professional MPC XL", true).profile;
        check (xl.maxVoices == 16 && xl.unison == profileHigh().unison && xl.fxMask == synthyboy::kFxAll && xl.samples == 2 && xl.economy == synthyboy::kEconomyControl,
               "the MPC XL runs 16 voices, the unison of high, all effects, the exact math, the 4x warps, the full morph rate and the high sample interpolation, with the control economy: " + describeProfile (xl));
        check (profileFromTable ("Akai Professional MPC XL", false).profile == xl, "the XL's row does not depend on the architecture of the build");
        bool restLow = true;
        for (const char* m : { "Akai Professional MPC Live III", "" })
            for (const bool b64 : { false, true })
                restLow = restLow && profileFromTable (m, b64).profile == profileLow();
        check (restLow, "an unknown device and a device without a name run 'low', 32-bit or 64-bit");
        Profile k61 = profileLow();
        k61.startBudgetUs = 900;
        check (profileFromTable ("Akai Professional MPC Key 61", false).profile == k61 && profileFromTable ("Akai Professional MPC Key 61", true).profile == k61,
               "the Key 61 runs 'low' with the Spectral start budget 900 us (measured in the real host, 2026-11-21)");
    }

    // ---- the overrides and their order ---------------------------------------------------------------------------------------------------------------
    {
        const std::string xl = "Akai Professional MPC XL";
        ProfileChoice c = chooseProfile (xl, true, "", "/tmp/f", "");
        check (c.source == "table: " + xl && c.skipped.empty(), "no override: the table");
        c = chooseProfile (xl, true, "", "/tmp/f", "medium");
        check (c.profile == profileMedium() && c.source == "environment SYNTHYBOY_MPC_PROFILE" && c.text == "medium", "the environment variable replaces the table");
        c = chooseProfile (xl, true, "low,unison=7", "/tmp/f", "high");
        check (c.profile.unison == 7 && c.source == "file /tmp/f", "the debug file goes before the environment variable");
        c = chooseProfile (xl, true, "nonsense", "/tmp/f", "high");
        check (c.profile == profileHigh() && c.source == "environment SYNTHYBOY_MPC_PROFILE" && c.skipped.find ("file /tmp/f") != std::string::npos,
               "a debug file that does not parse is skipped and named: " + c.skipped);
        c = chooseProfile (xl, true, "nonsense", "/tmp/f", "low,unison=99");
        check (c.source == "table: " + xl && c.skipped.find ("file /tmp/f") != std::string::npos && c.skipped.find ("environment") != std::string::npos,
               "two bad overrides: the table, both named: " + c.skipped);
        const std::string report = profileReport (c, xl, true);
        check (report.find ("profile: ") == 0 && report.find ("\nprofile_settings: " + describeProfile (c.profile) + "\n") != std::string::npos
               && report.find ("\nprofile_source: table: " + xl + "\n") != std::string::npos && report.find ("\nprofile_skipped: file /tmp/f") != std::string::npos
               && report.find ("\ndevice_model: " + xl + "\n") != std::string::npos && report.find ("\nbuild: 64-bit\n") != std::string::npos, "the report names the profile, its settings, its source, what was skipped, the device and the build");
        check (profileReport (profileFromTable ("", false), "", false).find ("\ndevice_model: (not readable)\n") != std::string::npos, "the report says when the device name is not readable");
    }

    // ---- startbudget=<us>: the Spectral start budget, and the tier word of the description ----------------------------------------------------------------
    {
        Profile p, back;
        std::string why;
        check (profileLow().startBudgetUs == 0 && profileQuality (profileLow()).spectralStartBudgetUs == 0, "no startbudget: 0, the engine's default budget");
        check (parseProfile ("low,startbudget=3000", p) && p.startBudgetUs == 3000 && profileQuality (p).spectralStartBudgetUs == 3000, "startbudget=3000 reaches Quality::spectralStartBudgetUs");
        check (parseProfile ("high,voices=16,startbudget=100", p) && p.startBudgetUs == 100 && parseProfile ("medium,startbudget=10000", p) && p.startBudgetUs == 10000, "the ends of the range, 100 and 10000");
        parseProfile ("low,startbudget=2500", p);
        check (describeProfile (p).find (",startbudget=2500") != std::string::npos && parseProfile (describeProfile (p), back) && back == p && back.startBudgetUs == 2500,
               "startbudget is described and reads back: " + describeProfile (p));
        check (describeProfile (profileLow()).find ("startbudget") == std::string::npos, "the default budget is not described");
        check (parseProfile ("low,startbudget=100,unison=7", p) && p != profileLow() && parseProfile ("low,unison=7", back) && p != back, "the budget takes part in the comparison");
        for (const char* bad : { "low,startbudget=99", "low,startbudget=10001", "low,startbudget=0", "low,startbudget=", "low,startbudget=abc", "low,startbudget=-5", "low,startbudget=100000" })
        {
            Profile keep = profileLow();
            check (! parseProfile (bad, keep, &why) && keep == profileLow() && ! why.empty(), std::string ("refused: ") + bad + ": " + why);
        }
        check (! parseProfile ("low,startbudget=50", p, &why) && why == "startbudget must be 100 to 10000 (microseconds), not '50'", "the reason of an out-of-range budget: " + why);

        const Profile xl = profileFromTable ("Akai Professional MPC XL", true).profile;
        check (describeProfile (xl).rfind ("high,", 0) == 0, "the XL (samples high) is described from the base high, not low: " + describeProfile (xl));
        check (describeProfile (profileHigh()).rfind ("high,", 0) == 0 && describeProfile (profileMedium()).rfind ("medium,", 0) == 0 && describeProfile (profileLow()).rfind ("low,", 0) == 0,
               "each base is described under its own name");
        check (parseProfile (describeProfile (xl), back) && back == xl, "the XL's description reads back to the XL's profile");
        for (const char* text : { "low,samples=high", "medium,samples=low", "high,samples=medium,startbudget=777", "high,math=fast,startbudget=9999" })
        {
            Profile a, b;
            check (parseProfile (text, a) && parseProfile (describeProfile (a), b) && a == b, std::string ("round trip of '") + text + "': " + describeProfile (a));
        }
    }

    // ---- the choice of this process: the debug file, resolved once, written to the report ---------------------------------------------------------------
    {
        const std::string base = "/tmp/sb-device-profile-host-" + std::to_string (static_cast<long> (::getpid()));
        const std::string file = base + ".profile", reportPath = base + ".txt";
        { std::ofstream (file) << "low,unison=7,warp=4x\nsecond line is ignored\n"; }
        ::setenv ("SYNTHYBOY_MPC_PROFILE_FILE", file.c_str(), 1);
        ::setenv ("SYNTHYBOY_MPC_PROFILE_REPORT", reportPath.c_str(), 1);
        ::setenv ("SYNTHYBOY_MPC_PROFILE", "high", 1);
        const ProfileChoice& c = profileInForce();
        check (c.source == "file " + file && c.profile.unison == 7 && (c.profile.economy & synthyboy::kEconomyWarp) == 0 && c.profile.maxVoices == 8,
               "the process takes the first line of the debug file before the environment variable: " + c.source);
        std::stringstream ss;
        ss << std::ifstream (reportPath).rdbuf();
        check (ss.str().find ("profile: low,unison=7,warp=4x\n") == 0 && ss.str().find ("profile_settings: low,voices=8,unison=7,warp=4x,morph=half,math=fast,control=economy,start=stagger,fx=low,samples=low\n") != std::string::npos,
               "the report file says what is in force: " + ss.str().substr (0, ss.str().find ('\n')));
        // fixed for the life of the process: a changed file and environment change nothing
        { std::ofstream (file) << "high\n"; }
        ::setenv ("SYNTHYBOY_MPC_PROFILE", "medium", 1);
        const ProfileChoice& again = profileInForce();
        check (&again == &c && again.profile.unison == 7 && again.profile.maxVoices == 8, "the choice is fixed for the life of the process (a later change of the file or the environment is not read)");
        std::remove (file.c_str());
        std::remove (reportPath.c_str());
    }

    std::printf ("%s: %d failure(s)\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
