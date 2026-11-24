import json
from pathlib import Path
import tempfile
import unittest

from coverage_report import build, markdown, verdict


class CoverageTest(unittest.TestCase):
    def test_requires_functional_evidence(self):
        self.assertEqual(verdict({"status":"PASS"}, "quick"), ("VISITED", []))
        self.assertEqual(verdict({"status":"PASS"}, "semantic")[0], "UNPROVEN")
        self.assertEqual(verdict({"status":"PROXY_CHANGED"}, "scalar")[0], "PROXY_CHANGED")
        self.assertEqual(verdict({"status":"PASS", "observations":[]}, "disabled")[0], "UNPROVEN")
        self.assertEqual(verdict({"status":"PASS", "expected_engine_key":"cutoff", "gesture":[1,2,3,4],
                                  "engine_changes":{"cutoff":[0,1]}, "bound_before":0, "bound_after":1}, "scalar")[0],
                         "VERIFIED")

    def test_reports_all_views_keeps_retries_and_limits_cross_revision_aggregation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            roots = {dev:root/dev for dev in ("key61","xl")}
            for r in roots.values():
                r.mkdir()
            for n, rev, outcome in [(1,"aaa","PASS"), (2,"bbb","FAIL")]:
                folder = roots["xl"]/str(n)/"full"
                folder.mkdir(parents=True)
                (folder.parent/"run.json").write_text(json.dumps({"device":"xl","tui_sha256":rev,"assigned_views":["fx"],
                                                                  "phases":{"full":{"start_unix_s":n}}}))
                (folder/"results.json").write_text(json.dumps([{"id":"fx.test","view":"fx","status":outcome,
                    "steps":[{"status":"PASS","spec":{"op":"tap"},"control":"fx/A"},
                             {"status":"PASS","spec":{"op":"expect","target":"rate","change":"only"}}]}]))
            report = build(roots, {"fx.test":"fx", "noise.unrun":"oscnoise"})
            self.assertEqual(len(report["views"]), 15)
            self.assertEqual([a["result"] for a in report["attempts"]], ["VERIFIED","FAIL"])
            fx = next(v for v in report["views"] if v["view"] == "fx")
            self.assertEqual(fx["counts"]["semantic"], {"FAIL":1})
            self.assertEqual(fx["missing_cases"], ["fx.test"])
            self.assertIn("oscnoise", markdown(report))
