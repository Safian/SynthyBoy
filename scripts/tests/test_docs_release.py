#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests of docs/release/*: the form (sections, lengths) and the FACTS (every cited commit exists and its subject has the claimed topic,
every cited docs anchor exists, the numbers equal what the files say, the generated parts of OPEN-QUESTIONS.md are current)."""

import json
import os
import re
import subprocess
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REL = os.path.join(ROOT, "docs", "release")
sys.path.insert(0, os.path.join(ROOT, "scripts"))


def read(*p):
    with open(os.path.join(ROOT, *p), encoding="utf-8") as f:
        return f.read()


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)


# every commit the release texts cite, with the topic its subject must contain (what the citing line claims it is about)
TOPICS = {
    "01ac74a3": "stepped", "038b6538": "state v3", "0a46744f": "factory presets", "0a618f27": "oscillator display",
    "0e1f2100": "limiter", "178316e": "chorus", "1eafdf37": "mod-ui", "23e25c2d": "panel", "27d6b9bb": "mixer",
    "2ee94841": "output stage", "370c3895": "mouse", "3b8fc073": "macros page", "3c6abfbb": "output_level",
    "42682c52": "sample", "4cd77ce": "tier low", "5a8cf0c7": "mixfx", "69460e77": "filter", "6b191edb": "modulation amount",
    "72436f75": "ui library", "7da8db33": "sixteen q", "80107be9": "play page", "82917444": "lfo", "82b78e5c": "envelope",
    "906cb667": "lfo", "91f2d75": "chorus", "98805c9d": "q proxies", "99373e7e": "envelope", "a1166d2c": "filter-ui",
    "a5126580": "master page", "b0b8cc69": "flicker", "b56cc510": "output level", "b77d4fc7": "output stage",
    "b78d9de8": "oscillator display", "b8ac66ba": "quick-add", "b9e331f0": "pointer", "bc0f3ce": "tier", "c3a2065": "unison cap",
    "c72b9f37": "master and voice", "c8544168": "env and lfo", "cdc14c4c": "lfo", "d557a512": "osc tab", "d700b6f1": "mastervoice",
    "d90e1a0a": "sub oscillator", "da5eec7d": "panel", "e3abe08": "tier low", "e596f94": "tier low", "ecb776ad": "device rule",
    "f2e133c7": "key 61", "397c0654": "pointer plane", "94f5e3dd": "quality profile", "77f74d2e": "led", "76ffa60f": "led",
    "331af461": "macro names", "5c470640": "standby", "389944b1": "q router", "2b3555b7": "stepped", "f9b1310f": "string",
    "f5b13a93": "in-host drawing", "a7a0034d": "q-link",
}


def heading_and_bold_slugs(path):
    def slug(h):
        h = re.sub(r"[^\w\s-]", "", h.strip().lower().replace("`", ""))
        return re.sub(r"\s", "-", h).strip("-")
    text = read(path)
    out = set()
    for line in text.split("\n"):
        m = re.match(r"#+\s+(.*)", line)
        if m:
            out.add(slug(m.group(1)))
    for m in re.finditer(r"\*\*([^*\n]{3,250}?)\*\*", text):
        out.add(slug(m.group(1)))
    return out


def preset_state():
    with open(os.path.join(ROOT, "content", "presets", "Bass", "scream_filter_bass.sbpreset"), encoding="utf-8") as f:
        j = json.load(f)
    return j


class DocsReleaseTests(unittest.TestCase):
    def setUp(self):
        self.changelog = read("docs", "release", "CHANGELOG-draft.md")
        self.history = read("docs", "release", "HISTORY-by-week.md")
        self.params = json.load(open(os.path.join(ROOT, "docs", "params.json"), encoding="utf-8"))

    # ---- form
    def test_changelog_form(self):
        intro = self.changelog.split("---", 1)[0]
        lines = [l for l in intro.strip().split("\n") if l.strip() and not l.startswith("#")]
        self.assertLessEqual(len(lines), 5, "the intro has at most five lines")
        for h in ("### Sound Engine", "### Presets", "### MPC Plugin Shell", "### Drawn UI Page by Page (Overlay Build)",
                  "### Hardware Control", "### Known Problems and Limits", "### In Progress"):
            self.assertIn(h, self.changelog)
        bullets = [l.strip() for l in self.changelog.split("\n") if l.strip().startswith("- ")]
        self.assertGreater(len(bullets), 20)
        for b in bullets:
            self.assertTrue(b.endswith(")") or b.endswith(")."), "bullet without a source: " + b)

    def test_history_lengths(self):
        sections = re.split(r"### Week ", self.history)[1:]
        self.assertGreater(len(sections), 1)
        for sec in sections:
            head, *body = sec.strip().split("\n")
            n = len([l for l in body if l.strip().startswith("-")])
            self.assertTrue(3 <= n <= 8, f"week {head}: {n} lines")

    # ---- facts: commits
    def test_cited_commits_exist_and_have_the_topic(self):
        text = self.changelog + "\n" + self.history
        hashes = set(re.findall(r"\b([0-9a-f]{7,8})\b", text))
        hashes = {h for h in hashes if not h.isdigit()}
        for h in hashes:
            self.assertIn(h, TOPICS, f"commit {h} is cited without a claimed topic in TOPICS")
            r = git("log", "-1", "--format=%s", h)
            self.assertEqual(r.returncode, 0, f"commit {h} does not exist")
            self.assertIn(TOPICS[h], r.stdout.lower(), f"commit {h}: subject {r.stdout.strip()!r} lacks {TOPICS[h]!r}")

    # ---- facts: anchors
    def test_cited_docs_anchors_exist(self):
        text = self.changelog + "\n" + self.history
        for m in re.finditer(r"((?:README|docs/[A-Za-z0-9_-]+)\.md)#([\w.-]+)", text):
            slugs = heading_and_bold_slugs(m.group(1))
            self.assertTrue(any(s.startswith(m.group(2)) for s in slugs), f"anchor {m.group(0)} not found")

    # ---- facts: numbers
    def test_numbers_equal_the_files(self):
        st = preset_state()
        n_params = len(st["state"]["params"])
        self.assertEqual(n_params, self.params["count"])
        self.assertEqual(st["state"]["version"], self.params["stateVersion"])
        self.assertIn(f"`synthyboy-state` version {st['state']['version']} with all {n_params} parameter keys", self.changelog)
        self.assertIn(f"{n_params} engine parameters", self.changelog)
        n_files = len(subprocess.check_output(["git", "ls-files", "content/presets"], cwd=ROOT, text=True).split("\n")) - 1
        n_pre = len([f for f in os.listdir(os.path.join(ROOT, "content", "presets")) if os.path.isdir(os.path.join(ROOT, "content", "presets", f))])
        count = sum(len([x for x in os.listdir(os.path.join(ROOT, "content", "presets", d)) if x.endswith(".sbpreset")])
                    for d in os.listdir(os.path.join(ROOT, "content", "presets")) if os.path.isdir(os.path.join(ROOT, "content", "presets", d)))
        self.assertEqual(count, 112)
        self.assertIn("112 factory presets", self.changelog)
        self.assertGreater(n_files, 0)
        self.assertGreater(n_pre, 0)
        # parameter sequence of the overlay build: engine + 4 cursors + 32 spectrum + 2 pad pressure + 1 instance + 3 selectors + 24 proxies + 16 Q
        total = n_params + 4 + 32 + 2 + 1 + 3 + 24 + 16
        self.assertEqual(total, 2719)
        self.assertIn(f"{total} parameters", self.changelog)
        models = next(p for p in self.params["params"] if p["key"] == "filter1.model")["choices"]
        styles = next(p for p in self.params["params"] if p["key"] == "filter1.style")["choices"]
        self.assertIn(f"{len(models)} models in {len(styles)} styles", self.changelog)
        self.assertIn(f"{len(models)} filter models in {len(styles)} styles", self.changelog)
        keys = {p["key"].split(".")[0] for p in self.params["params"]}
        self.assertEqual(len([k for k in keys if re.fullmatch(r"env\d+", k)]), 6)
        self.assertEqual(len([k for k in keys if re.fullmatch(r"lfo\d+", k)]), 10)
        self.assertEqual(len([k for k in keys if re.fullmatch(r"rlfo\d+", k)]), 4)
        self.assertEqual(len([k for k in keys if re.fullmatch(r"mod\d+", k)]), 64)
        self.assertEqual(len(self.params["modSources"]), 57)
        for t in ("6 envelopes", "10 LFOs", "4 random LFOs", "64-slot", "57 internal modulation sources", "13 effect types"):
            self.assertIn(t, self.changelog)

    def test_no_stale_readme_numbers_in_the_texts(self):
        for stale in ("1484", "37 filter", "37 models", "8-voice polyphonic synthesizer plugin built as VST3"):
            self.assertNotIn(stale, self.changelog.replace("README.md", ""))
        self.assertNotIn("Resolved Key 61", self.history)
        self.assertNotIn("3/128", self.changelog + self.history)

    # ---- OPEN-QUESTIONS
    def test_open_questions_generated_parts_are_current(self):
        r = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "gen-open-questions.py"), "--check"], cwd=ROOT)
        self.assertEqual(r.returncode, 0, "run scripts/gen-open-questions.py to refresh docs/release/OPEN-QUESTIONS.md")

    def test_open_questions_counts_equal_the_grep(self):
        sys.path.insert(0, os.path.join(ROOT, "scripts"))
        import importlib.util
        spec = importlib.util.spec_from_file_location("genoq", os.path.join(ROOT, "scripts", "gen-open-questions.py"))
        g = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(g)
        rows = g.collect()
        text = read("docs", "release", "OPEN-QUESTIONS.md")
        self.assertIn(f"Lines found: {len(rows)} in {len({r[0] for r in rows})} files.", text)
        # the same search with git grep, line count (git grep -c would count per file)
        files = g.scope_files()
        out = subprocess.run(["git", "grep", "-n", "-i", "-E", g.PATTERN, "--", *files], cwd=ROOT, capture_output=True, text=True).stdout
        self.assertEqual(len([l for l in out.split("\n") if l]), len(rows))
        self.assertIn(g.PATTERN, text)

    def test_open_questions_part_a_cites_real_lines(self):
        text = read("docs", "release", "OPEN-QUESTIONS.md")
        part_a = text.split("## B.", 1)[0].split("## A.", 1)[1]
        pat = re.compile(r"not (yet )?(measured|verified|tested|seen|checked|tried|known|built)|was not built|unverified|TODO|known bug|GUESS|"
                         r"pending|(still|stays|remains?|is|are|left) open|open (bug|question|item|issue)|Still open|Open question|open", re.I)
        cited = 0
        for row in part_a.split("\n"):
            if not row.startswith("| A"):
                continue
            src = row.rstrip().rstrip("|").rsplit("|", 1)[1]
            for m in re.finditer(r"(README\.md|THIRD_PARTY\.md|docs/[A-Za-z0-9_-]+\.md):(\d+)((?:, \d+\b)*)", src):
                nums = [int(m.group(2))] + [int(x) for x in m.group(3).replace(" ", "").split(",") if x]
                lines = read(m.group(1)).split("\n")
                for n in nums:
                    cited += 1
                    self.assertLessEqual(n, len(lines), f"{m.group(1)}:{n} does not exist")
                    self.assertTrue(pat.search(lines[n - 1]), f"{m.group(1)}:{n} has no marker: {lines[n-1][:80]}")
            for h in re.findall(r"\b[0-9a-f]{8}\b", src):
                self.assertEqual(git("cat-file", "-e", h).returncode, 0, f"commit {h} does not exist")
        self.assertGreaterEqual(cited, 18)

    def test_open_questions_commit_headings_have_hashes(self):
        text = read("docs", "release", "OPEN-QUESTIONS.md")
        block = text.split("<!-- BEGIN AUTO-COMMITS -->")[1].split("<!-- END AUTO-COMMITS -->")[0]
        heads = re.findall(r"^### (\S*)", block, re.M)
        self.assertGreater(len(heads), 5)
        for h in heads:
            self.assertRegex(h, r"^[0-9a-f]{7,8}$")
            self.assertEqual(git("cat-file", "-e", h).returncode, 0)
        self.assertNotIn("Co-Authored-By", block)


if __name__ == "__main__":
    unittest.main()
