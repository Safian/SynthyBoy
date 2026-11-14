#!/usr/bin/env python3
# SynthyBoy - checks docs/MPC.md: required sections and terms, links and repository paths that exist, measured numbers only among the device observations.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DOC = ROOT / "docs" / "MPC.md"
TEXT = DOC.read_text(encoding="utf-8") if DOC.exists() else ""

SECTIONS = ["Build", "Package layout", "Install on the MPC", "Known host behaviour", "Sound differences at tier low"]
TERMS = ["scripts/build-armv7-mpc.sh", "MPC_SDK", "ARMV7_IMAGE", "VST2 Plugins", "Safian - VST - SynthyBoy", "plugin.json", "version.xml",
         "VST/synthyboy-arm32.so", "Content/presets", "Plugin Skins", "Enable Enhancements", "External Plugin Support", "44.1 kHz", "128",
         "GCC", "Zig", "scripts/mpc-ui-pages.json", "shell-mpc/accept.sh", "file64", "kSpectrumFloorDb", "kSpectrumCeilingDb"]


def section(title):
    m = re.search(r"^## %s\s*$(.*?)(?=^## |\Z)" % re.escape(title), TEXT, re.M | re.S)
    return m.group(1) if m else None


class MpcDocTests(unittest.TestCase):
    def test_exists_with_one_title(self):
        self.assertTrue(DOC.exists(), "docs/MPC.md is missing")
        self.assertEqual(len(re.findall(r"^# \S", TEXT, re.M)), 1, "exactly one level-1 heading")

    def test_sections_in_order(self):
        found = re.findall(r"^## (.+?)\s*$", TEXT, re.M)
        self.assertEqual([s for s in found if s in SECTIONS], SECTIONS, "sections (## headings) found: %r" % found)

    def test_every_section_has_text(self):
        for s in SECTIONS:
            body = section(s)
            self.assertIsNotNone(body, s)
            self.assertGreaterEqual(len(body.split()), 40, "section '%s' is too thin" % s)

    def test_terms_present(self):
        missing = [t for t in TERMS if t not in TEXT]
        self.assertEqual(missing, [])

    def test_relative_links_resolve(self):
        for target in re.findall(r"\]\(([^)#\s]+)(?:#[^)]*)?\)", TEXT):
            if re.match(r"[a-z]+://", target):
                continue
            self.assertTrue((DOC.parent / target).exists(), "broken link: %s" % target)

    def test_repository_paths_in_backticks_exist(self):
        tops = ("scripts/", "shell-mpc/", "docs/", "core/", "content/", "cmake/", "bench/")
        for code in re.findall(r"`([^`\n]+)`", TEXT):
            for word in code.split():
                word = word.strip(",;:()\"'")
                if word.startswith(tops) and "<" not in word and "*" not in word and "$" not in word:
                    self.assertTrue((ROOT / word).exists(), "path in backticks does not exist: %s" % word)

    def test_host_behaviour_separates_observed_from_unverified(self):
        body = section("Known host behaviour") or ""
        subs = re.findall(r"^### (.+?)\s*$", body, re.M)
        self.assertEqual(subs, ["Observed on the device", "Per the SDK documentation / not yet verified on the device"])
        observed = body.split("### Per the SDK documentation")[0]
        for claim in ("selects program 0", "saves the program number"):
            self.assertNotIn(claim, observed, "a host-side program statement is not a device observation yet")

    def test_tier_low_section_lists_the_changes(self):
        body = section("Sound differences at tier low") or ""
        self.assertGreaterEqual(len(re.findall(r"^\s*[-*] ", body, re.M)), 5, "at least five list items")
        for word in ("oversampl", "tanh", "LFO", "silence", "reverb"):
            self.assertIn(word.lower(), body.lower(), "the tier low section does not mention '%s'" % word)

    def test_measured_numbers_only_in_the_device_observations(self):
        # CPU / timing results come from the measurement on the device and are listed under "Observed on the device" only
        body = section("Known host behaviour") or ""
        observed = body.split("### Per the SDK documentation")[0]
        rest = TEXT.replace(observed, "")
        hits = re.findall(r"\b\d+(?:[.,]\d+)?\s*(?:ms\b|µs\b|us\b|%|dBFS\b|x faster\b|times faster\b)", rest)
        self.assertEqual(hits, [], "measured numbers belong under 'Observed on the device'")

    def test_device_measurement_of_2026_11_13(self):
        body = section("Known host behaviour") or ""
        observed = body.split("### Per the SDK documentation")[0]
        unverified = body.split("### Per the SDK documentation")[-1]
        for value in ("2026-11-13", "2.902 ms", "0.535", "0.789", "1.405", "0.022 ms", "0.070 ms", "0.97", "1.34", "542 ms", "5.41", "8.09", "9.20",
                      "`--ftz`", "7 unison", "tier low", "own engine"):
            self.assertIn(value, observed, "the device measurement lacks %s" % value)
        self.assertNotIn(",", "".join(re.findall(r"\d,\d", observed)), "decimal point, not decimal comma")
        low = observed.lower()
        for phrase in ("stock mpc app", "30 pages", "by ear"):
            self.assertIn(phrase, low, "the device observations do not say: %s" % phrase)
        self.assertIn("during playback", unverified.lower(), "the CPU load during playback is still unverified and must be listed as such")

    def test_heavy_patch_ran_four_unison_voices(self):
        observed = (section("Known host behaviour") or "").split("### Per the SDK documentation")[0]
        self.assertIn("asks for 7 unison voices per oscillator, tier low ran 4", observed)
        self.assertNotIn("7 unison voices each, 2 filters", observed)

    def test_project_save_and_load_is_an_observation(self):
        body = section("Known host behaviour") or ""
        observed, unverified = body.split("### Per the SDK documentation")
        for value in ("Sbtest", "Acid Saw Reso *", "79.4", "450", "78", "Nobody listened", "4 to 7 %"):
            self.assertIn(value, observed)
        self.assertNotIn("has not been tried", unverified)
        self.assertNotIn("still not verified on the device: saving", unverified.lower())

    def test_unison_cap_measurement_and_conclusions(self):
        observed = (section("Known host behaviour") or "").split("### Per the SDK documentation")[0]
        for value in ("`c3a2065`", "`4cd77ce`", "3.73 / 5.46 / 6.04", "4.16 / 5.43 / 5.67", "5.36 / 8.27 / 8.98", "5.97 / 9.00 / 9.62",
                      "4.64 / 5.52 / 5.66", "3.64 / 4.85 / 5.40", "0.559 / 0.781 / 1.016", "does not fit into the 2.902 ms block",
                      "default of the unison cap is 3", "no cap on the number of notes is needed", "`878326f`", "4.42 ms",
                      "14 of the 112 factory presets", "2 oscillators with 7 unison voices each"):
            self.assertIn(value, observed)

    def test_presets_named_as_affected_match_the_report(self):
        # the names in the document are the presets whose largest unison on an active oscillator is above 3
        import json
        observed = (section("Known host behaviour") or "").split("### Per the SDK documentation")[0]
        line = next(l for l in observed.splitlines() if "14 of the 112 factory presets" in l)
        files = sorted((ROOT / "content" / "presets").rglob("*.sbpreset"))
        self.assertEqual(len(files), 112)
        affected = []
        for f in files:
            d = json.loads(f.read_text(encoding="utf-8"))
            p = d["state"]["params"]
            top = max([int(round(p.get("osc%d.unison.voices" % n, 1))) for n in (1, 2, 3) if p.get("osc%d.on" % n, 0)] or [0])
            (affected if top > 3 else []).append(d["meta"]["name"])
            if top <= 3:
                self.assertNotIn(d["meta"]["name"] + ",", line)
        self.assertEqual(len(affected), 14)
        for name in affected:
            self.assertIn(name, line)

    def test_final_measurement_and_tier_low_list(self):
        observed = (section("Known host behaviour") or "").split("### Per the SDK documentation")[0]
        unverified = (section("Known host behaviour") or "").split("### Per the SDK documentation")[-1]
        for value in ("2026-11-13", "`91f2d75`", "`17d6e11`", "0.87 ms", "0.666 ms", "0.715 ms", "1.018", "0.359 to 0.201", "33.3", "Diffusor Cloud Pad 1.75 / 1.96 / 2.74",
                      "`bc0f3ce`", "`e3abe08`", "`178316e`", "`e596f94`", "3.354 / 3.691 / 3.959 ms", "2.637 / 2.868 / 3.781 ms", "2.059 / 2.595 / 3.205 ms",
                      "0.474 / 0.703 / 0.965 ms", "does not fit safely with 8 or 6 notes"):
            self.assertIn(value, observed)
        self.assertIn("nobody has listened", unverified)
        body = section("Sound differences at tier low") or ""
        for word in ("Unison cap", "Warps", "Spectral morph", "tanh", "sin and cos", "Moving filters", "Serum filters", "Economy modulators", "Chorus and phaser", "Noise",
                     "Effect racks on silence", "Stolen voices", "Events", "Bypassed effects"):
            self.assertIn("**%s" % word, body, "the tier low list lacks %s" % word)
        self.assertIn("at most 3 unison voices", body)
        self.assertNotIn("unison at 4 voices", body)

    def test_benchmark_switches_documented(self):
        body = section("Benchmark switches") or ""
        for switch in ("--preset", "--preset-dir", "--csv", "--voices", "--sections", "--unison-cap", "--morph-spread", "--kill-fade"):
            self.assertIn("`%s" % switch, body, switch)

    def test_no_placeholders(self):
        for word in ("TODO", "TBD", "FIXME", "lorem"):
            self.assertNotIn(word, TEXT)


if __name__ == "__main__":
    unittest.main()
