# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The host choices of the LFO (98_lfo_pick.py, SOURCE of 53_lfo_smooth.py, shell-mpc/native_lfo_pick.h): ADD SOURCE, ADD TARGET (GROUP and a TARGET list per group), SOURCE of a point modulation."""

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
for path in (str(ROOT), str(SCRIPTS)):
    if path not in sys.path:
        sys.path.insert(0, path)

import native_ui_params as handles

_spec = importlib.util.spec_from_file_location("mpc_skin_extra_98_lfo_pick", SCRIPTS / "mpc-skin-extras" / "98_lfo_pick.py")
lfo_pick = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lfo_pick)


class Context:
    def __init__(self, doc, skins):
        self.doc = doc
        self.params = doc["params"]
        self.param_count = len(self.params)
        self.meta = {"variant": "overlay"}
        self.skins_dir = Path(skins)
        self.tui = {"pageData": {"tabs": [{"tabName": name, "componentDefinition": {"componentsData": []}}
                                          for name in (*handles.LFO_TAB_NAMES, "Play", "Env", "Mod")],
                                 "componentDefinitions": {"localComponentDefinitions": []}, "replacementStrings": []}}

    def add_component_definition(self, key, value):
        self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"].append({"key": key, "value": value})


class LfoPickTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = json.loads((ROOT / "docs" / "params.json").read_text(encoding="utf-8"))
        cls.tmp = tempfile.TemporaryDirectory()
        cls.ctx = Context(cls.doc, cls.tmp.name)
        lfo_pick.apply(cls.ctx.tui, {}, cls.ctx)
        cls.page = cls.ctx.tui["pageData"]
        cls.base = handles.native_base_index(cls.ctx.param_count)
        cls.strings = {item["key"]: item["value"] for item in cls.page["replacementStrings"]}
        cls.definitions = {d["key"]: d["value"] for d in cls.page["componentDefinitions"]["localComponentDefinitions"]}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_handle_block_is_contiguous_and_after_the_reserved_text_edit_handles(self):
        self.assertEqual(handles.TEXT_EDIT_RESERVED, handles.LFO_TARGETS_ACTION + 1)
        self.assertEqual(handles.LFO_PICK, handles.TEXT_EDIT_RESERVED + handles.TEXT_EDIT_RESERVED_COUNT)
        self.assertEqual(handles.NATIVE_PARAMS, handles.LFO_PICK + handles.LFO_PICK_HANDLES)
        header = (ROOT / "shell-mpc" / "native_ui_params.h").read_text(encoding="utf-8")
        self.assertIn("constexpr int kLfoPickHandles = %d;" % handles.LFO_PICK_HANDLES, header)
        self.assertIn("constexpr int kTextEditReservedCount = %d;" % handles.TEXT_EDIT_RESERVED_COUNT, header)

    def test_only_the_lfo_tabs_and_play_carry_the_choices(self):
        carrying = {tab["tabName"] for tab in self.page["tabs"] if tab["componentDefinition"]["componentsData"]}
        self.assertEqual(carrying, {*handles.LFO_TAB_NAMES, "Play"})
        gates = {tab["tabName"]: tab["componentDefinition"]["componentsData"][0]["bounds"]["additionalInvalidatingHandles"][0]
                 for tab in self.page["tabs"] if tab["componentDefinition"]["componentsData"]}
        view = f"Parameter {self.base + handles.MOD_TARGETS_VIEW}"
        self.assertEqual({gate for name, gate in gates.items() if name != "Play"}, {f"IndexedEnabling/3/8/{view}"})
        self.assertEqual(gates["Play"], f"IndexedEnabling/6/8/{view}")

    def test_add_source_lists_every_source_after_the_placeholder(self):
        names = [m["name"] for m in sorted(self.doc["modSources"], key=lambda m: int(m["choice"]))]
        self.assertEqual(self.strings[f"Parameter {self.base + handles.LFO_PICK_ADD_SOURCE}"], ["ADD SOURCE"] + names)
        self.assertEqual(len(names), 57)

    def test_target_lists_partition_all_destinations_like_the_dest_index(self):
        groups = lfo_pick.destination_groups(self.doc)
        self.assertLessEqual(len(groups), handles.LFO_PICK_MAX_GROUPS)
        self.assertEqual(sum(len(targets) for _, targets in groups), len(self.doc["modDestinations"]))
        self.assertEqual(self.strings[f"Parameter {self.base + handles.LFO_PICK_GROUP}"], ["ADD TARGET"] + [name for name, _ in groups])
        for index, (_, targets) in enumerate(groups):
            self.assertEqual(self.strings[f"Parameter {self.base + handles.LFO_PICK_TARGET + index}"], ["TARGET"] + targets)

    def test_each_target_list_is_shown_by_its_group_only(self):
        groups = lfo_pick.destination_groups(self.doc)
        container = self.definitions["lfoNativePicks"]["componentsData"]
        group_handle = f"Parameter {self.base + handles.LFO_PICK_GROUP}"
        targets = [c for c in container if c["componentData"]["name"].startswith("Add Target ")
                   and c["componentData"]["type"] == "lfoNativePickTarget"]
        self.assertEqual(len(targets), len(groups))
        for index, entry in enumerate(targets):
            gates = entry["bounds"]["additionalInvalidatingHandles"]
            self.assertIn(f"IndexedEnabling/{index + 1}/{len(groups) + 1}/{group_handle}", gates)
            self.assertEqual(entry["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {self.base + handles.LFO_PICK_TARGET + index}"}])

    def test_every_choice_opens_the_host_menu_and_not_a_plugin_list(self):
        for kind in ("lfoNativePickAddSource", "lfoNativePickGroup", "lfoNativePickTarget"):
            overlays = [a["additionalData"] for a in self.definitions[kind]["actions"] if a["handler"] == "Show Overlay"]
            self.assertTrue(overlays, kind)
            self.assertEqual(set(overlays), {lfo_pick.CHOICE_MENU})

    def test_the_old_plugin_list_cells_are_not_used_for_adding(self):
        # the shape editor's own lists (SHAPES, MORE) keep the plugin menu rows; the add lists never use them
        source = (ROOT / "shell-mpc" / "overlay" / "lfo_ui.h").read_text(encoding="utf-8")
        self.assertNotIn("ListKind::ModSource", source)
        self.assertNotIn("ListKind::PointSource", source)
        self.assertNotIn("ListKind::Dest", source)
        self.assertNotIn("openPointSourceList", source)

    def test_python_and_cpp_agree_on_the_block(self):
        pick = (ROOT / "shell-mpc" / "native_lfo_pick.h").read_text(encoding="utf-8")
        self.assertIn("constexpr int kMaxGroups = %d;" % handles.LFO_PICK_MAX_GROUPS, pick)
        self.assertEqual(handles.LFO_PICK_SOURCE - handles.LFO_PICK, 1)          # Field Source = 0, handle = 1 + field
        self.assertEqual(handles.LFO_PICK_ADD_SOURCE - handles.LFO_PICK, 2)      # AddSourceChoice = 1
        self.assertEqual(handles.LFO_PICK_GROUP - handles.LFO_PICK, 3)           # Group = 2
        self.assertEqual(handles.LFO_PICK_TARGET - handles.LFO_PICK, 4)          # FirstTarget = 3
        self.assertIn("enum Field { Source, AddSourceChoice, Group, FirstTarget };", pick)
        self.assertIn("enum View { Hidden, PointSource, AddSource, AddTarget, LastView = AddTarget };", pick)
        self.assertEqual((lfo_pick.VIEW_ADD_SOURCE, lfo_pick.VIEW_ADD_TARGET, lfo_pick.VIEWS), (2, 3, 4))


if __name__ == "__main__":
    unittest.main()
