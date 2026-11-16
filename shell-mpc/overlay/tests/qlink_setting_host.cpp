// SynthyBoy - MPC overlay tests: the host's option "Show Q-Link Status When Touched" (qlink_setting.h): the parse of the settings text and the cheap file check (stat, read only on a change).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <string>

#include <unistd.h>

#include "../qlink_setting.h"

using namespace synthyboy_mpc::overlay;

static int g_fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++g_fails;
}

static const char* kSample = R"(<?xml version="1.0" encoding="UTF-8"?>

<PROPERTIES>
  <VALUE name="SynthContentLocations">
    <SynthContentLocations>
      <Location>/sdcard/Expansions</Location>
    </SynthContentLocations>
  </VALUE>
  <VALUE name="ShowTipsOnStartup" val="0"/>
  <VALUE name="ShowQLinksStatusOverlay" val="%s"/>
  <VALUE name="AutoSaveInterval" val="1"/>
</PROPERTIES>
)";

static std::string sample (const char* val)
{
    char buf[2048];
    std::snprintf (buf, sizeof buf, kSample, val);
    return buf;
}

static bool writeFile (const std::string& path, const std::string& text)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return false;
    std::fwrite (text.data(), 1, text.size(), f);
    std::fclose (f);
    return true;
}

int main()
{
    check (parseQLinkStatusSetting (sample ("1")) == QLinkSetting::On, "the sample with val 1: on");
    check (parseQLinkStatusSetting (sample ("0")) == QLinkSetting::Off, "the sample with val 0: off");
    check (parseQLinkStatusSetting (sample ("true")) == QLinkSetting::On && parseQLinkStatusSetting (sample ("false")) == QLinkSetting::Off, "true / false are read too");
    check (parseQLinkStatusSetting (sample ("maybe")) == QLinkSetting::Unknown && parseQLinkStatusSetting (sample ("")) == QLinkSetting::Unknown, "another value: unknown");
    check (parseQLinkStatusSetting ("") == QLinkSetting::Unknown, "an empty file: unknown");
    check (parseQLinkStatusSetting ("<PROPERTIES>\n  <VALUE name=\"ShowTipsOnStartup\" val=\"0\"/>\n</PROPERTIES>") == QLinkSetting::Unknown, "no such key in the file: unknown");
    check (parseQLinkStatusSetting ("<VALUE val=\"0\" name=\"ShowQLinksStatusOverlay\"/>") == QLinkSetting::Off, "the attributes in the other order: off");
    check (parseQLinkStatusSetting ("<VALUE name=\"ShowQLinksStatusOverlay\"") == QLinkSetting::Unknown, "a cut off element: unknown");
    check (parseQLinkStatusSetting ("<VALUE name=\"OtherValue\" val=\"0\"/><VALUE name=\"ShowQLinksStatusOverlay\" val=\"1\"/>") == QLinkSetting::On, "the val of THIS element, not of the one before it");
    check (std::string (qlinkSettingText (QLinkSetting::Off)) == "0" && std::string (qlinkSettingText (QLinkSetting::On)) == "1" && std::string (qlinkSettingText (QLinkSetting::Unknown)) == "unknown", "the status text");

    char tmpl[] = "/tmp/sb-qlink-setting-XXXXXX";
    const std::string dir = mkdtemp (tmpl);
    const std::string path = dir + "/MPC.settings";
    QLinkSettingFile f (path);
    check (! f.poll() && f.value() == QLinkSetting::Unknown && f.keyOn(), "no file: unknown, the key stays on, nothing changed");
    writeFile (path, sample ("1"));
    check (f.poll() && f.value() == QLinkSetting::On && f.keyOn(), "the file appears with val 1: on (changed)");
    check (! f.poll() && ! f.poll() && f.reads() == 1, "unchanged file: no change and no second read (reads " + std::to_string (f.reads()) + " of " + std::to_string (f.polls()) + " checks)");
    writeFile (path, sample ("0"));     // same size: the modification time (nanoseconds) is what tells
    const bool ch = f.poll();
    check (ch && f.value() == QLinkSetting::Off && ! f.keyOn() && f.reads() == 2, "val 0 written (same size): found by the modification time: off, the key is off");
    writeFile (path, std::string (sample ("0")) + "\n<!-- more -->\n");
    check (! f.poll() && f.value() == QLinkSetting::Off && f.reads() == 3, "a rewrite that keeps the value: read again, no change reported");
    writeFile (path, sample ("1"));
    check (f.poll() && f.keyOn(), "val 1 again: the key is on again");
    std::remove (path.c_str());
    check (f.poll() && f.value() == QLinkSetting::Unknown && f.keyOn(), "the file is gone: unknown, the key stays on");
    writeFile (path, "garbage without the key");
    check (! f.poll() && f.value() == QLinkSetting::Unknown && f.keyOn(), "a file without the key: unknown, on");
    QLinkSettingFile none;
    check (! none.poll() && none.keyOn(), "no path: unknown, on");
    std::remove (path.c_str());
    rmdir (dir.c_str());
    std::printf (g_fails ? "qlink_setting_host: FAILED (%d)\n" : "qlink_setting_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
