#!/usr/bin/env python3
# SynthyBoy - unit tests for the skin passive-cover validator check.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import validate_synthyboy_skin as validator  # noqa: E402


def _entry(name, kind, bounds, *, bounds_type="Absolute", focus="No", gates=None, remap=None, data=None):
    out = {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": kind,
            "data": data or {"version": 1, "handleName": "Data"},
        },
        "handle remapping": {"version": 1, "map": remap or []},
        "bounds": {
            "version": 2 if gates is not None else 1,
            "acceptsHWFocus": focus,
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": bounds_type,
            "bounds": bounds,
        },
    }
    if gates is not None:
        out["bounds"]["additionalInvalidatingHandles"] = gates
    return out


def _definition(entries, *, actions=None, repeats=None):
    out = {
        "version": 3 if repeats is not None else 2,
        "actions": actions or [],
        "backgroundData": {
            "version": 1,
            "focussed": {"version": 1, "colour": "0", "image": ""},
            "unfocussed": {"version": 1, "colour": "0", "image": ""},
        },
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": entries,
    }
    if repeats is not None:
        out["repeats"] = repeats
    return out


def _page(tabs, defs=None):
    return {
        "version": 1,
        "info": {"version": 1, "type": "CompleteDescription"},
        "replacementStrings": [],
        "componentDefinitions": {
            "version": 2,
            "importFiles": [],
            "localComponentDefinitions": [{"key": k, "value": v} for k, v in (defs or {}).items()],
        },
        "tabs": tabs,
    }


def _tab(name, entries):
    return {
        "tabName": name,
        "fnKeyIndex": 0,
        "fnKeySubIndex": 0,
        "qlinkBoundsData": [],
        "componentDefinition": _definition(entries),
    }


class TestSkinLabelOverlap(unittest.TestCase):
    def test_detects_ungated_later_label_over_button(self):
        page = _page([
            _tab("Tab1", [
                _entry("Btn", "Button", "10 10 100 40", focus="Yes"),
                _entry("Cover", "Label", "20 20 80 30"),
            ])
        ])
        covers = validator.find_label_covers(page)
        self.assertEqual(len(covers), 1)
        self.assertEqual(covers[0]["touchable_name"], "Btn")
        self.assertEqual(covers[0]["cover_name"], "Cover")
        self.assertEqual(covers[0]["cover_rect"], "20 20 80 30")

    def test_ignores_label_earlier_in_paint_order_or_small_overlap(self):
        page = _page([
            _tab("Tab1", [
                _entry("Earlier", "Label", "10 10 100 40"),
                _entry("Btn", "Button", "10 10 100 40", focus="Yes"),
                _entry("Edge4px", "Image", "106 10 20 40"),  # x overlap = 4 px (must be > 4)
            ])
        ])
        self.assertEqual(validator.find_label_covers(page), [])

    def test_ignores_gated_cover_and_descendant_of_action_def(self):
        action = [{"version": 1, "onAction": "Mouse Down", "handler": "Toggle Switch", "handleName": "Data", "additionalData": ""}]
        defs = {
            "ActionBtn": _definition([
                _entry("InnerText", "Label", "0 0 1 1", bounds_type="Proportional"),
            ], actions=action),
        }
        page = _page([
            _tab("Tab1", [
                _entry("Btn", "ActionBtn", "10 10 100 40"),
                _entry("GatedLabel", "Label", "10 10 100 40", gates=["IndexedEnabling/1/2/Parameter 10"]),
            ])
        ], defs=defs)
        self.assertEqual(validator.find_label_covers(page), [])

    def test_shared_ancestor_gate_still_reports_cover_and_resolves_proportional_repeats(self):
        defs = {
            "Tile": _definition([
                _entry("Knob1", "Knob", "0 0 0.5 1", bounds_type="Proportional", focus="Yes"),
                _entry("Cover", "Label", "0 0 0.5 1", bounds_type="Proportional"),
            ]),
            "Grid": _definition([
                _entry("Cell", "Tile", "0 0 100 50",
                       gates=["IndexedEnabling/1/2/Parameter {Increment:Base=10}"]),
            ], repeats=2),
        }
        page = _page([
            _tab("Tab1", [
                _entry("MyGrid", "Grid", "20 30 200 50",
                       gates=["IndexedEnabling/1/2/Parameter 5"]),
            ])
        ], defs=defs)
        covers = validator.find_label_covers(page, sources={"Tile": "defs_test.json", "Grid": "defs_test.json"})
        self.assertEqual(len(covers), 2)
        self.assertEqual(covers[0]["touchable_rect"], "20 30 50 50")
        self.assertEqual(covers[0]["cover_source"], "defs_test.json:Tile")
        self.assertEqual(covers[1]["touchable_rect"], "120 30 50 50")

    def test_unshared_ancestor_gate_on_cover_prevents_false_positive(self):
        defs = {
            "Overlay": _definition([
                _entry("Title", "Label", "0 0 1 1", bounds_type="Proportional"),
            ]),
        }
        page = _page([
            _tab("Tab1", [
                _entry("Btn", "Button", "10 10 100 40", focus="Yes", gates=["IndexedEnabling/0/2/Parameter 1"]),
                _entry("Modal", "Overlay", "0 0 200 100", gates=["IndexedEnabling/1/2/Parameter 1"]),
            ])
        ], defs=defs)
        self.assertEqual(validator.find_label_covers(page), [])

    def test_check_label_covers_errors_on_reintroduced_cover(self):
        page = _page([
            _tab("Tab1", [
                _entry("Btn", "Button", "10 10 100 40", focus="Yes"),
                _entry("Cover", "Label", "10 10 100 40"),
            ])
        ])
        findings = validator.Findings()
        validator.check_label_covers(page, findings, as_error=True)
        self.assertEqual(len(findings.errors), 1)

    def test_cli_list_covers_on_temp_skin(self):
        with tempfile.TemporaryDirectory() as tmp:
            skin_dir = Path(tmp)
            tui = {
                "pageData": _page([
                    _tab("Tab1", [
                        _entry("Btn", "Button", "10 10 100 40", focus="Yes"),
                        _entry("Cover", "Label", "10 10 100 40"),
                    ])
                ])
            }
            (skin_dir / "TUI.json").write_text(json.dumps(tui), encoding="utf-8")
            proc = subprocess.run(
                [sys.executable, str(ROOT / "scripts/validate_synthyboy_skin.py"), "--list-covers", str(skin_dir)],
                capture_output=True, text=True, check=True,
            )
            self.assertIn("Tab1\tBtn [10 10 100 40]\tCover [10 10 100 40]\tTUI.json:tab:Tab1", proc.stdout)


if __name__ == "__main__":
    unittest.main()
