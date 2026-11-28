#!/usr/bin/env python3
# SynthyBoy - unit tests for the skin alias-rule validator checks (B1, B2).
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


def _entry(name, kind, *, show_invalid="Show", gates=None, remap=None, data=None):
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
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": show_invalid,
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "0 0 100 40",
        },
    }
    if gates is not None:
        out["bounds"]["additionalInvalidatingHandles"] = gates
    return out


def _definition(entries):
    return {
        "version": 2,
        "actions": [],
        "backgroundData": {
            "version": 1,
            "focussed": {"version": 1, "colour": "0", "image": ""},
            "unfocussed": {"version": 1, "colour": "0", "image": ""},
        },
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": entries,
    }


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


class TestSkinAliasRules(unittest.TestCase):
    def test_b1_detects_modifier_wrapping_alias_and_allows_parameter_and_increment(self):
        defs = {
            "BadChild": _definition([
                _entry("Btn", "Button", remap=[{"key": "Data", "value": "FromGesture/Back"}]),
                _entry("GateLbl", "Label", gates=["IndexedEnabling/1/2/Present"]),
                _entry("OkInc", "Button", remap=[{"key": "Data", "value": "FromGesture/Parameter {Increment:Base=10,Step=2}"}]),
                _entry("OkPlain", "Label", remap=[{"key": "Data", "value": "PlainAlias"}], gates=["PlainGate"]),
            ]),
        }
        page = _page([
            _tab("Tab1", [
                _entry("Inst", "BadChild", remap=[
                    {"key": "Data", "value": "Parameter 1"},
                    {"key": "Back", "value": "Parameter 2"},
                    {"key": "Present", "value": "Parameter 3"},
                    {"key": "PlainAlias", "value": "WithName[]/Parameter 4"},
                    {"key": "PlainGate", "value": "IndexedEnabling/1/2/Parameter 5"},
                ]),
            ])
        ], defs=defs)
        b1 = validator.find_alias_modifier_violations(page, sources={"BadChild": "defs_test.json"})
        self.assertEqual(len(b1), 2)
        self.assertEqual([(v["entry_name"], v["operand"]) for v in b1], [("Btn", "Back"), ("GateLbl", "Present")])
        findings = validator.Findings()
        validator.check_alias_rules(page, findings, as_error=True)
        self.assertEqual(len(findings.errors), 2)

    def test_b2_detects_nonempty_remap_without_data_and_hide(self):
        defs = {
            "Widget": _definition([_entry("Lbl", "Label")]),
        }
        page = _page([
            _tab("Tab1", [
                _entry("MissingDataHide", "Widget", show_invalid="Hide", remap=[{"key": "Caption", "value": "Parameter 10"}]),
                _entry("MissingDataShow", "Widget", show_invalid="Show", remap=[{"key": "Caption", "value": "Parameter 10"}]),
                _entry("EmptyRemapHide", "Widget", show_invalid="Hide", remap=[]),
                _entry("HasDataHide", "Widget", show_invalid="Hide", remap=[
                    {"key": "Data", "value": "Parameter 10"},
                    {"key": "Caption", "value": "Parameter 10"},
                ]),
            ])
        ], defs=defs)
        b2 = validator.find_missing_data_remaps(page, sources={"Widget": "defs_test.json"})
        self.assertEqual(len(b2), 1)
        self.assertEqual(b2[0]["entry_name"], "MissingDataHide")
        self.assertEqual(b2[0]["component_type"], "Widget")
        findings = validator.Findings()
        validator.check_alias_rules(page, findings, as_error=True)
        self.assertEqual(len(findings.errors), 1)

    def test_cli_list_alias_rules_on_temp_skin(self):
        with tempfile.TemporaryDirectory() as tmp:
            skin_dir = Path(tmp)
            defs = {"Widget": _definition([_entry("Btn", "Button", gates=["NonZero/GateAlias"])])}
            tui = {
                "pageData": _page([
                    _tab("Tab1", [
                        _entry("Inst", "Widget", show_invalid="Hide", remap=[{"key": "GateAlias", "value": "Parameter 5"}]),
                    ])
                ], defs=defs)
            }
            (skin_dir / "TUI.json").write_text(json.dumps(tui), encoding="utf-8")
            proc = subprocess.run(
                [sys.executable, str(ROOT / "scripts/validate_synthyboy_skin.py"), "--list-alias-rules", str(skin_dir)],
                capture_output=True, text=True, check=True,
            )
            self.assertIn("B1\tTUI.json:Widget\tBtn\tgate\tNonZero/GateAlias\tGateAlias", proc.stdout)
            self.assertIn("B2\tTUI.json:tab:Tab1\tInst\tWidget\tGateAlias", proc.stdout)


if __name__ == "__main__":
    unittest.main()
