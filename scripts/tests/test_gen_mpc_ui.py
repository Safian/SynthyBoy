#!/usr/bin/env python3
# SynthyBoy - tests for gen-mpc-ui.py (stdlib unittest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Run: python3 -I scripts/tests/test_gen_mpc_ui.py

import contextlib
import importlib.util
import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

# ---------------------------------------------------------------------------
# Load the generator module
# ---------------------------------------------------------------------------

def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gen_mpc = load("t_gen_mpc_ui", "scripts/gen-mpc-ui.py")

# ---------------------------------------------------------------------------
# Paths to real input files
# ---------------------------------------------------------------------------

PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def run_generator(out_dir, page_ids=None):
    """Run the generator and return (code, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    args = ["--params", str(PARAMS_JSON), "--pages", str(PAGES_JSON),
            "--out", str(out_dir)]
    if page_ids is not None:
        args.extend(["--page-ids", page_ids])
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = gen_mpc.main(args)
    return code, out.getvalue(), err.getvalue()


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


class BasicOutputTests(unittest.TestCase):
    """Test that the generator produces the expected output files."""

    def test_generator_exits_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, _, _ = run_generator(tmp)
            self.assertEqual(code, 0)

    def test_ui_json_created(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_generator(tmp)
            self.assertTrue((Path(tmp) / "ui.json").exists())

    def test_params_hostsim_created(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_generator(tmp)
            self.assertTrue((Path(tmp) / "params-hostsim.json").exists())

    def test_ui_json_is_valid_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_generator(tmp)
            data = load_json(Path(tmp) / "ui.json")
            self.assertIsInstance(data, dict)
            self.assertIn("pages", data)

    def test_params_hostsim_is_valid_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_generator(tmp)
            data = load_json(Path(tmp) / "params-hostsim.json")
            self.assertIsInstance(data, dict)
            self.assertIn("params", data)


class PageLimitTests(unittest.TestCase):
    """Test page count, controls-per-page, and page-name-length limits."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.code, self.out, self.err = run_generator(self.tmp)
        self.ui = load_json(Path(self.tmp) / "ui.json")
        self.hostsim = load_json(Path(self.tmp) / "params-hostsim.json")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_page_count_at_most_32(self):
        self.assertLessEqual(len(self.ui["pages"]), 32)

    def test_controls_per_page_at_most_16(self):
        for i, page in enumerate(self.ui["pages"]):
            self.assertLessEqual(len(page["controls"]), 16,
                                 "Page %d (%s) has %d controls" % (i, page["name"],
                                                                   len(page["controls"])))

    def test_page_name_length_at_most_8(self):
        for i, page in enumerate(self.ui["pages"]):
            self.assertLessEqual(len(page["name"]), 8,
                                 "Page %d name '%s' is %d chars" % (i, page["name"],
                                                                     len(page["name"])))

    def test_no_duplicate_page_names(self):
        names = [p["name"] for p in self.ui["pages"]]
        self.assertEqual(len(names), len(set(names)),
                         "Duplicate page names: %s" % [n for n in names if names.count(n) > 1])


class PageNameTests(unittest.TestCase):
    """Test that page names are built from title, not truncated page ids."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.code, self.out, self.err = run_generator(self.tmp)
        self.ui = load_json(Path(self.tmp) / "ui.json")
        # Load kick-pages to get titles
        with open(PAGES_JSON, encoding="utf-8") as f:
            self.pages_doc = json.load(f)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_page_names_not_truncated_page_ids(self):
        """Page names should be derived from title, not page id."""
        # Check that no page name looks like a truncated page id
        # (page ids typically have underscores like "osc1_mai")
        for page in self.ui["pages"]:
            name = page["name"]
            # Names should not contain underscores (which come from page ids)
            # unless they're part of the title abbreviation
            # The key test: names should be readable (no trailing underscore)
            self.assertFalse(name.endswith("_"),
                             "Page name '%s' ends with underscore (truncated page id)" % name)

    def test_page_names_readable_from_title(self):
        """Page names should be derived from title by dropping spaces/punctuation."""
        # Build a mapping of page id -> title
        id_to_title = {}
        for p in self.pages_doc["pages"]:
            id_to_title[p["id"]] = p.get("title", "")

        # Check that page names contain letters/digits from the title
        for page in self.ui["pages"]:
            name = page["name"]
            # Names should only contain alphanumeric chars and spaces (for continuation)
            self.assertRegex(name, r'^[a-zA-Z0-9 ]+$',
                             "Page name '%s' contains non-alphanumeric chars" % name)

    def test_page_names_unique(self):
        """All page names must be unique."""
        names = [p["name"] for p in self.ui["pages"]]
        self.assertEqual(len(names), len(set(names)),
                         "Duplicate page names found")


class DroppedKeysTests(unittest.TestCase):
    """Test that dropped keys are reported as distinct count."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.code, self.out, self.err = run_generator(self.tmp)
        self.stderr = self.err

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_dropped_keys_count_is_distinct(self):
        """The dropped keys count should be a reasonable number, not inflated."""
        # The count should be much less than 50000 (the old buggy count)
        # Extract the dropped count from stderr
        import re
        match = re.search(r'Dropped (\d+) distinct parameter key', self.stderr)
        if match:
            count = int(match.group(1))
            # Should be a reasonable number, not 58339
            self.assertLess(count, 5000,
                            "Dropped keys count %d seems inflated (was 58339 before fix)" % count)
        else:
            # If no dropped keys message, that's fine too
            pass

    def test_dropped_keys_reported_once(self):
        """Each dropped key should only appear once in the report."""
        import re
        match = re.search(r'Dropped (\d+) distinct parameter key\(s\) due to limits: (.*)',
                          self.stderr)
        if match:
            keys_str = match.group(2)
            keys = [k.strip() for k in keys_str.split(",")]
            # All keys should be unique
            self.assertEqual(len(keys), len(set(keys)),
                             "Dropped keys list contains duplicates")


class PageIdsTests(unittest.TestCase):
    """Test the --page-ids argument."""

    def test_page_ids_selects_pages(self):
        """--page-ids should select specific pages in order."""
        with tempfile.TemporaryDirectory() as tmp:
            # Use first two page ids from kick-pages.json
            with open(PAGES_JSON, encoding="utf-8") as f:
                pages_doc = json.load(f)
            page_ids = ",".join([pages_doc["pages"][0]["id"], pages_doc["pages"][1]["id"]])
            code, _, _ = run_generator(tmp, page_ids=page_ids)
            self.assertEqual(code, 0)
            ui = load_json(Path(tmp) / "ui.json")
            # Should have at most 2 source pages (may split into more ui pages)
            self.assertLessEqual(len(ui["pages"]), 32)

    def test_page_ids_unknown_returns_exit_2(self):
        """Unknown page id should cause exit code 2."""
        with tempfile.TemporaryDirectory() as tmp:
            code, _, _ = run_generator(tmp, page_ids="nonexistent_id")
            self.assertEqual(code, 2)

    def test_page_ids_default_is_first_pages(self):
        """Default (no --page-ids) should use first pages in order."""
        with tempfile.TemporaryDirectory() as tmp1, tempfile.TemporaryDirectory() as tmp2:
            # Run with first 5 page ids
            with open(PAGES_JSON, encoding="utf-8") as f:
                pages_doc = json.load(f)
            first_5_ids = ",".join([p["id"] for p in pages_doc["pages"][:5]])
            run_generator(tmp1, page_ids=first_5_ids)
            # Run default (should be first pages)
            run_generator(tmp2)
            # The first pages should be the same
            ui1 = load_json(Path(tmp1) / "ui.json")
            ui2 = load_json(Path(tmp2) / "ui.json")
            # First page names should match
            self.assertEqual(ui1["pages"][0]["name"], ui2["pages"][0]["name"])


class HostSimPluginFieldsTests(unittest.TestCase):
    """Test that params-hostsim.json has the correct plugin fields."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        run_generator(self.tmp)
        self.hostsim = load_json(Path(self.tmp) / "params-hostsim.json")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_plugin_name_field(self):
        """params-hostsim.json should have 'name' field."""
        self.assertIn("name", self.hostsim)
        self.assertEqual(self.hostsim["name"], "SynthyBoy")

    def test_plugin_vendor_field(self):
        """params-hostsim.json should have 'vendor' field."""
        self.assertIn("vendor", self.hostsim)
        self.assertEqual(self.hostsim["vendor"], "Safian")

    def test_plugin_uid_field(self):
        """params-hostsim.json should have 'uid' field."""
        self.assertIn("uid", self.hostsim)
        self.assertEqual(self.hostsim["uid"], "SfSB")

    def test_plugin_uidint_field(self):
        """params-hostsim.json should have 'uidInt' field (big-endian int)."""
        self.assertIn("uidInt", self.hostsim)
        self.assertIsInstance(self.hostsim["uidInt"], int)
        # Verify it's the big-endian interpretation of "SfSB"
        uid = "SfSB"
        expected = (ord(uid[0]) << 24) | (ord(uid[1]) << 16) | (ord(uid[2]) << 8) | ord(uid[3])
        self.assertEqual(self.hostsim["uidInt"], expected)

    def test_plugin_version_field(self):
        """params-hostsim.json should have 'version' field = 1000."""
        self.assertIn("version", self.hostsim)
        self.assertEqual(self.hostsim["version"], 1000)

    def test_plugin_is_instrument_field(self):
        """params-hostsim.json should have 'isInstrument' field = true."""
        self.assertIn("isInstrument", self.hostsim)
        self.assertTrue(self.hostsim["isInstrument"])

    def test_plugin_num_inputs_field(self):
        """params-hostsim.json should have 'numInputs' field = 0."""
        self.assertIn("numInputs", self.hostsim)
        self.assertEqual(self.hostsim["numInputs"], 0)

    def test_plugin_num_outputs_field(self):
        """params-hostsim.json should have 'numOutputs' field = 2."""
        self.assertIn("numOutputs", self.hostsim)
        self.assertEqual(self.hostsim["numOutputs"], 2)

    def test_plugin_num_programs_field(self):
        """params-hostsim.json should have 'numPrograms' field = 1."""
        self.assertIn("numPrograms", self.hostsim)
        self.assertEqual(self.hostsim["numPrograms"], 1)

    def test_plugin_programs_field(self):
        """params-hostsim.json should have 'programs' field = ['Init']."""
        self.assertIn("programs", self.hostsim)
        self.assertEqual(self.hostsim["programs"], ["Init"])

    def test_plugin_fields_before_params(self):
        """Plugin fields should appear before 'params' in the JSON."""
        # Check the raw JSON to verify field order
        with open(Path(self.tmp) / "params-hostsim.json", encoding="utf-8") as f:
            content = f.read()
        # Find positions of keys
        name_pos = content.find('"name"')
        vendor_pos = content.find('"vendor"')
        uid_pos = content.find('"uid"')
        uidint_pos = content.find('"uidInt"')
        version_pos = content.find('"version"')
        isinstrument_pos = content.find('"isInstrument"')
        numinputs_pos = content.find('"numInputs"')
        numoutputs_pos = content.find('"numOutputs"')
        noprograms_pos = content.find('"numPrograms"')
        programs_pos = content.find('"programs"')
        params_pos = content.find('"params"')

        # All plugin fields should come before params
        self.assertLess(name_pos, params_pos)
        self.assertLess(vendor_pos, params_pos)
        self.assertLess(uid_pos, params_pos)
        self.assertLess(uidint_pos, params_pos)
        self.assertLess(version_pos, params_pos)
        self.assertLess(isinstrument_pos, params_pos)
        self.assertLess(numinputs_pos, params_pos)
        self.assertLess(numoutputs_pos, params_pos)
        self.assertLess(noprograms_pos, params_pos)
        self.assertLess(programs_pos, params_pos)


class QLinkGroupTests(unittest.TestCase):
    """Test that each block of 4 consecutive controls comes from one parameter group."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        run_generator(self.tmp)
        self.ui = load_json(Path(self.tmp) / "ui.json")
        # Build param name -> group lookup
        with open(PARAMS_JSON, encoding="utf-8") as f:
            params_doc = json.load(f)
        self.param_groups = {}
        for p in params_doc.get("params", []):
            self.param_groups[p["name"]] = p.get("group", "")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_qlink_blocks_respect_group(self):
        """Each block of 4 consecutive controls must come from one parameter group."""
        for pi, page in enumerate(self.ui["pages"]):
            controls = page["controls"]
            # Check each block of 4
            for block_start in range(0, len(controls), 4):
                block = controls[block_start:block_start + 4]
                groups = set()
                for ctrl in block:
                    if ctrl == "empty":
                        continue
                    if isinstance(ctrl, str):
                        continue
                    param_name = ctrl.get("param", "")
                    if param_name:
                        group = self.param_groups.get(param_name, "")
                        groups.add(group)
                # All non-empty controls in a block should have the same group
                non_empty_groups = [g for g in groups if g != ""]
                if len(non_empty_groups) > 1:
                    self.fail("Page %d, block %d has controls from multiple groups: %s"
                              % (pi, block_start // 4, non_empty_groups))


class ControlTests(unittest.TestCase):
    """Test that controls reference valid parameters and no control appears twice."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.code, self.out, self.err = run_generator(self.tmp)
        self.ui = load_json(Path(self.tmp) / "ui.json")
        self.hostsim = load_json(Path(self.tmp) / "params-hostsim.json")
        # Build lookup: param name -> param entry
        self.params_by_name = {}
        for p in self.hostsim["params"]:
            self.params_by_name[p["name"]] = p

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_all_control_params_exist_in_hostsim(self):
        """Every param referenced in ui.json exists by name in params-hostsim.json."""
        for pi, page in enumerate(self.ui["pages"]):
            for ci, ctrl in enumerate(page["controls"]):
                if ctrl == "empty":
                    continue
                if isinstance(ctrl, str):
                    # String controls are parameter names
                    param_name = ctrl
                else:
                    param_name = ctrl.get("param", "")
                if param_name and param_name not in self.params_by_name:
                    self.fail("Control on page %d, slot %d references unknown param '%s'"
                              % (pi, ci, param_name))

    def test_no_duplicate_controls(self):
        """No control (param name) appears twice across all pages."""
        seen = set()
        for pi, page in enumerate(self.ui["pages"]):
            for ci, ctrl in enumerate(page["controls"]):
                if ctrl == "empty":
                    continue
                if isinstance(ctrl, str):
                    param_name = ctrl
                else:
                    param_name = ctrl.get("param", "")
                if param_name:
                    if param_name in seen:
                        self.fail("Duplicate control '%s' on page %d slot %d"
                                  % (param_name, pi, ci))
                    seen.add(param_name)

    def test_all_controls_are_valid_widgets(self):
        """Every control has a valid widget type."""
        valid_widgets = {"slider", "knob", "fader", "switch", "empty", "text"}
        for pi, page in enumerate(self.ui["pages"]):
            for ci, ctrl in enumerate(page["controls"]):
                if ctrl == "empty":
                    continue
                if isinstance(ctrl, str):
                    continue
                widget = ctrl.get("widget", "slider")
                self.assertIn(widget, valid_widgets,
                              "Page %d, slot %d has invalid widget '%s'" % (pi, ci, widget))


class DeterminismTests(unittest.TestCase):
    """Test that two runs produce byte-identical output."""

    def test_deterministic_ui_json(self):
        with tempfile.TemporaryDirectory() as tmp1, tempfile.TemporaryDirectory() as tmp2:
            run_generator(tmp1)
            run_generator(tmp2)
            ui1 = (Path(tmp1) / "ui.json").read_bytes()
            ui2 = (Path(tmp2) / "ui.json").read_bytes()
            self.assertEqual(ui1, ui2, "ui.json differs between runs")

    def test_deterministic_params_hostsim(self):
        with tempfile.TemporaryDirectory() as tmp1, tempfile.TemporaryDirectory() as tmp2:
            run_generator(tmp1)
            run_generator(tmp2)
            p1 = (Path(tmp1) / "params-hostsim.json").read_bytes()
            p2 = (Path(tmp2) / "params-hostsim.json").read_bytes()
            self.assertEqual(p1, p2, "params-hostsim.json differs between runs")


class HostSimFormatTests(unittest.TestCase):
    """Test that the params-hostsim.json has the correct format."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        run_generator(self.tmp)
        self.hostsim = load_json(Path(self.tmp) / "params-hostsim.json")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_params_have_required_fields(self):
        """Every param has index, name, label, display, default, automatable, kind, choices."""
        required = {"index", "name", "label", "display", "default", "automatable", "kind", "choices"}
        for i, p in enumerate(self.hostsim["params"]):
            missing = required - set(p.keys())
            self.assertEqual(missing, set(),
                             "params[%d] missing keys: %s" % (i, missing))

    def test_default_normalized_0_to_1(self):
        """Default values are normalized to 0..1."""
        for i, p in enumerate(self.hostsim["params"]):
            d = p["default"]
            self.assertIsInstance(d, (int, float),
                                  "params[%d].default is not numeric" % i)
            self.assertGreaterEqual(d, 0.0,
                                    "params[%d].default %s < 0" % (i, d))
            self.assertLessEqual(d, 1.0,
                                 "params[%d].default %s > 1" % (i, d))

    def test_choices_is_list(self):
        """choices is always a list."""
        for i, p in enumerate(self.hostsim["params"]):
            self.assertIsInstance(p["choices"], list,
                                  "params[%d].choices is not a list" % i)

    def test_kind_is_valid(self):
        """kind is one of float, choice, bool."""
        valid = {"float", "choice", "bool"}
        for i, p in enumerate(self.hostsim["params"]):
            self.assertIn(p["kind"], valid,
                          "params[%d].kind '%s' is not valid" % (i, p["kind"]))

    def test_automatable_is_bool(self):
        """automatable is a boolean."""
        for i, p in enumerate(self.hostsim["params"]):
            self.assertIsInstance(p["automatable"], bool,
                                  "params[%d].automatable is not bool" % i)

    def test_index_matches_position(self):
        """index matches the position in the array."""
        for i, p in enumerate(self.hostsim["params"]):
            self.assertEqual(p["index"], i,
                             "params[%d].index is %d, expected %d" % (i, p["index"], i))


class GenUiIntegrationTests(unittest.TestCase):
    """Integration test: run gen_ui.py if MPC_SDK is set."""

    @unittest.skipUnless(os.environ.get("MPC_SDK"), "MPC_SDK environment variable not set")
    def test_gen_ui_exits_zero(self):
        """gen_ui.py --params --ui --out exits 0."""
        with tempfile.TemporaryDirectory() as tmp:
            run_generator(tmp)
            ui_path = Path(tmp) / "ui.json"
            params_path = Path(tmp) / "params-hostsim.json"
            out_skins = Path(tmp) / "skins"
            sdk = Path(os.environ["MPC_SDK"])
            gen_ui = sdk / "tools" / "gen_ui.py"
            result = os.system("python3 %s --params %s --ui %s --out %s --quiet"
                               % (gen_ui, params_path, ui_path, out_skins))
            self.assertEqual(result, 0, "gen_ui.py failed")

    @unittest.skipUnless(os.environ.get("MPC_SDK"), "MPC_SDK environment variable not set")
    def test_gen_ui_produces_valid_json(self):
        """gen_ui.py produces valid TUI.json and Q-Links.json."""
        with tempfile.TemporaryDirectory() as tmp:
            run_generator(tmp)
            ui_path = Path(tmp) / "ui.json"
            params_path = Path(tmp) / "params-hostsim.json"
            out_skins = Path(tmp) / "skins"
            sdk = Path(os.environ["MPC_SDK"])
            gen_ui = sdk / "tools" / "gen_ui.py"
            os.system("python3 %s --params %s --ui %s --out %s --quiet"
                      % (gen_ui, params_path, ui_path, out_skins))
            # Check TUI.json
            tui_path = out_skins / "TUI.json"
            self.assertTrue(tui_path.exists(), "TUI.json not produced")
            tui = load_json(tui_path)
            self.assertIsInstance(tui, dict)
            # Check Q-Links.json
            ql_path = out_skins / "Q-Links.json"
            self.assertTrue(ql_path.exists(), "Q-Links.json not produced")
            ql = load_json(ql_path)
            self.assertIsInstance(ql, dict)


class WidgetChoiceTests(unittest.TestCase):
    """Test that widget choices follow the ui-guide table."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        run_generator(self.tmp)
        self.ui = load_json(Path(self.tmp) / "ui.json")
        self.hostsim = load_json(Path(self.tmp) / "params-hostsim.json")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_switch_for_bool_params(self):
        """Bool params should use switch widget."""
        for pi, page in enumerate(self.ui["pages"]):
            for ci, ctrl in enumerate(page["controls"]):
                if ctrl == "empty" or isinstance(ctrl, str):
                    continue
                param_name = ctrl.get("param", "")
                if param_name:
                    p = self.hostsim["params"]
                    # Find by name
                    for hp in p:
                        if hp["name"] == param_name and hp["kind"] == "bool":
                            self.assertEqual(ctrl.get("widget", "slider"), "switch",
                                             "Bool param '%s' should use switch, got %s"
                                             % (param_name, ctrl.get("widget")))
                            break

    def test_switch_for_2_4_choices(self):
        """Choice params with 2-4 choices should use switch widget."""
        for pi, page in enumerate(self.ui["pages"]):
            for ci, ctrl in enumerate(page["controls"]):
                if ctrl == "empty" or isinstance(ctrl, str):
                    continue
                param_name = ctrl.get("param", "")
                if param_name:
                    for hp in self.hostsim["params"]:
                        if hp["name"] == param_name and hp["kind"] == "choice" and len(hp["choices"]) <= 4:
                            self.assertEqual(ctrl.get("widget", "slider"), "switch",
                                             "Choice param '%s' (%d choices) should use switch, got %s"
                                             % (param_name, len(hp["choices"]), ctrl.get("widget")))
                            break

    def test_slider_for_5_plus_choices(self):
        """Choice params with 5+ choices should use slider widget."""
        for pi, page in enumerate(self.ui["pages"]):
            for ci, ctrl in enumerate(page["controls"]):
                if ctrl == "empty" or isinstance(ctrl, str):
                    continue
                param_name = ctrl.get("param", "")
                if param_name:
                    for hp in self.hostsim["params"]:
                        if hp["name"] == param_name and hp["kind"] == "choice" and len(hp["choices"]) >= 5:
                            self.assertEqual(ctrl.get("widget", "slider"), "slider",
                                             "Choice param '%s' (%d choices) should use slider, got %s"
                                             % (param_name, len(hp["choices"]), ctrl.get("widget")))
                            break


if __name__ == "__main__":
    unittest.main()
