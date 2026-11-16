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
        check (describeProfile (profileLow()) == "low,voices=8,unison=3,warp=2x,morph=half,math=fast,control=economy,fx=low,samples=low", "the description of low names every setting");
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

        // THE DEFAULT IS UNCHANGED until the devices are measured: every row is the profile the MPC build has always run. (When a row is changed after the bench run, this check
        // is the one to change with it: it says on purpose what the package runs.)
        bool allLow = true;
        for (const char* m : { "Akai Professional MPC XL", "Akai Professional MPC Key 61", "Akai Professional MPC Live III", "" })
            for (const bool b64 : { false, true })
                allLow = allLow && profileFromTable (m, b64).profile == profileLow();
        check (allLow, "every device, known or not, 32-bit or 64-bit, runs 'low' (the default until the bench numbers of the devices exist)");
    }

    // ---- the overrides and their order ---------------------------------------------------------------------------------------------------------------
    {
        const std::string xl = "Akai Professional MPC XL";
        ProfileChoice c = chooseProfile (xl, true, "", "/tmp/f", "", "");
        check (c.source == "table: " + xl && c.skipped.empty(), "no override: the table");
        c = chooseProfile (xl, true, "", "/tmp/f", "", "medium");
        check (c.profile == profileMedium() && c.source == "build option SYNTHYBOY_MPC_PROFILE" && c.text == "medium", "the build option replaces the table");
        c = chooseProfile (xl, true, "", "/tmp/f", "high", "medium");
        check (c.profile == profileHigh() && c.source == "environment SYNTHYBOY_MPC_PROFILE", "the environment variable goes before the build option");
        c = chooseProfile (xl, true, "low,unison=7", "/tmp/f", "high", "medium");
        check (c.profile.unison == 7 && c.source == "file /tmp/f", "the debug file goes before the environment variable");
        c = chooseProfile (xl, true, "nonsense", "/tmp/f", "high", "medium");
        check (c.profile == profileHigh() && c.source == "environment SYNTHYBOY_MPC_PROFILE" && c.skipped.find ("file /tmp/f") != std::string::npos,
               "a debug file that does not parse is skipped and named: " + c.skipped);
        c = chooseProfile (xl, true, "nonsense", "/tmp/f", "low,unison=99", "also bad");
        check (c.source == "table: " + xl && c.skipped.find ("file /tmp/f") != std::string::npos && c.skipped.find ("environment") != std::string::npos
               && c.skipped.find ("build option") != std::string::npos, "three bad overrides: the table, all three named: " + c.skipped);
        const std::string report = profileReport (c, xl, true);
        check (report.find ("profile: ") == 0 && report.find ("\nprofile_settings: " + describeProfile (c.profile) + "\n") != std::string::npos
               && report.find ("\nprofile_source: table: " + xl + "\n") != std::string::npos && report.find ("\nprofile_skipped: file /tmp/f") != std::string::npos
               && report.find ("\ndevice_model: " + xl + "\n") != std::string::npos && report.find ("\nbuild: 64-bit\n") != std::string::npos, "the report names the profile, its settings, its source, what was skipped, the device and the build");
        check (profileReport (profileFromTable ("", false), "", false).find ("\ndevice_model: (not readable)\n") != std::string::npos, "the report says when the device name is not readable");
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
        check (ss.str().find ("profile: low,unison=7,warp=4x\n") == 0 && ss.str().find ("profile_settings: low,voices=8,unison=7,warp=4x,morph=half,math=fast,control=economy,fx=low,samples=low\n") != std::string::npos,
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
