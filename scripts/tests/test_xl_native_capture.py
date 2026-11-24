"""Portable CLI fixtures for the standalone XL capture replay checker."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CHECKER = Path(__file__).with_name("check_xl_native_capture.py")
TARGETS = {
    "mixer": "main.fx_bypass",
    "voice": "voicectl.rand.pan",
    "master": "master.volume",
    "fx": "chorus.on",
}


class NativeCaptureTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.directory = Path(tmp.name)
        self.data = {}
        for page, target in TARGETS.items():
            filename = "input-results.json" if page == "fx" else "targeted-results.json"
            self.data.setdefault(filename, {})[page] = {
                "target": target,
                "changed": {target: ["0", "1"]},
                "geometry": {"rect": [8, 10, 4, 6], "opaque": 0, "transparent": 24, "composed_differs_primary": 0},
            }
            if page != "fx":
                self.data[filename][page]["view_after"] = page
        self.data["special-results.json"] = {
            edge: ["filter1", [16, 58, 44], [58, 16, 100]]
            for edge in ("filter_before", "filter_after")
        }

    def replay(self, *pages, passed, extended=False):
        for filename, records in self.data.items():
            (self.directory / filename).write_text(json.dumps(records))
        result = subprocess.run(
            [sys.executable, "-O", str(CHECKER), *(["--extended"] if extended else []), str(self.directory), *pages],
            text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0 if passed else 1, result.stdout + result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        for page in pages or (*TARGETS, "filter"):
            self.assertIn(("PASS " if passed else "FAIL ") + page, result.stdout)

    def test_complete_synthetic_capture(self):
        self.replay(passed=True)

    def test_wrong_target_off_page_numeric_noop_and_extra_write(self):
        for page, target in TARGETS.items():
            records = self.data["input-results.json" if page == "fx" else "targeted-results.json"]
            original = records[page]
            mutations = [
                {"target": "other"}, {"changed": {}}, {"changed": {target: ["1", "1.0"]}},
                {"changed": {target: ["0", "1"], "other": ["0", "1"]}},
                {"changed": {target: "01"}}, {"changed": {target: ["0", "NaN"]}},
            ]
            if page != "fx":
                mutations.append({"view_after": "none"})
            for mutation in mutations:
                with self.subTest(page=page, mutation=mutation):
                    records[page] = {**original, **mutation}
                    self.replay(page, passed=False)
            records[page] = original

    def test_both_filter_markers_on_each_edge(self):
        records = self.data["special-results.json"]
        for edge in records:
            sample = records[edge]
            for index, replacement in enumerate(("none", [39, 77, 64], [0, 0, 0])):
                with self.subTest(edge=edge, index=index):
                    records[edge] = copy.deepcopy(sample)
                    records[edge][index] = replacement
                    self.replay("filter", passed=False)
            records[edge] = sample

    def test_fx_exposure_requires_positive_area_and_all_pixels(self):
        fx = self.data["input-results.json"]["fx"]
        original = fx["geometry"]
        for change in [
            {"opaque": 1}, {"transparent": 23}, {"composed_differs_primary": 1},
            {"rect": [8, 10, 0, 6], "transparent": 0}, {"rect": [8, 10, -4, -6]},
        ]:
            with self.subTest(change=change):
                fx["geometry"] = {**original, **change}
                self.replay("fx", passed=False)

    def test_extended_require_independent_results(self):
        for page in ("mixer", "voice", "master"):
            record = self.data["targeted-results.json"][page]
            with self.subTest(page=page):
                self.replay(page, passed=False, extended=True)
                self.data["input-results.json"][page] = copy.deepcopy(record)
                self.replay(page, passed=True, extended=True)
                self.data["input-results.json"][page]["changed"] = {}
                self.replay(page, passed=False, extended=True)

    def test_filter_power_requires_a_single_write_in_extended_mode(self):
        record = self.data["special-results.json"]
        self.replay("filter", passed=False, extended=True)
        record["filter_changed"] = {"filter1.on": ["1", "0"]}
        self.replay("filter", passed=True, extended=True)
        record["filter_changed"]["filter2.on"] = ["1", "0"]
        self.replay("filter", passed=False, extended=True)

    def test_incomplete_capture(self):
        self.data.clear()
        self.replay("mixer", "voice", "master", "filter", "fx", passed=False)


if __name__ == "__main__":
    unittest.main()
