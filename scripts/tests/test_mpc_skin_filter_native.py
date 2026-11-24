"""Release Filter skin geometry, state gates, marker isolation, and target ABI."""
import json
import os
import subprocess
import unittest

from PIL import Image

import test_mpc_skin_extras as base


def rect(entry):
    return tuple(map(int, entry["bounds"]["bounds"].split()))


def overlaps(a, b):
    return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]


def handle(entry):
    return int(entry["handle remapping"]["map"][0]["value"].split()[-1])


class NativeFilterManifestTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("SYNTHYBOY_FILTER_NATIVE_HOST"), "set SYNTHYBOY_FILTER_NATIVE_HOST")
    def test_manifest_matches_cpp_rectangles(self):
        actual = subprocess.check_output([os.environ["SYNTHYBOY_FILTER_NATIVE_HOST"], "--skin-contract"])
        self.assertEqual(actual, (base.REAL_EXTRAS / "filter-native-contract.json").read_bytes())


@unittest.skipUnless((base.SDK / "tools/gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class NativeFilterGeneratedSkinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_mpc_skin_no_fallback import NoFallbackTests

        NoFallbackTests.setUpClass()
        cls.addClassCleanup(NoFallbackTests.tearDownClass)
        cls.result = NoFallbackTests.code, NoFallbackTests.err
        cls.skin = NoFallbackTests.skins
        cls.params = NoFallbackTests.params
        cls.tui = NoFallbackTests.tui
        cls.tab = next(t for t in NoFallbackTests.tui["pageData"]["tabs"] if t["tabName"] == "Filter")
        cls.items = cls.tab["componentDefinition"]["componentsData"]
        definitions = {d["key"]: d["value"] for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        kind = "filterNativeEditorSurface"
        cls.surface = next(c for c in cls.items if c["componentData"]["type"] == kind)
        cls.curve_surface = next(c for c in cls.items if c["componentData"]["type"] == "filterNativeCurveSurface")
        cls.split_left_surfaces = [c for c in cls.items if c["componentData"]["type"] == "filterNativeSplitLeftSurface"]
        cls.split_right_surfaces = [c for c in cls.items if c["componentData"]["type"] == "filterNativeSplitRightSurface"]
        cls.surfaces = [cls.surface, cls.curve_surface, *cls.split_left_surfaces, *cls.split_right_surfaces]
        cls.controls = definitions[kind]["componentsData"]
        cls.curve_controls = definitions["filterNativeCurveSurface"]["componentsData"]
        cls.split_left_controls = definitions["filterNativeSplitLeftSurface"]["componentsData"]
        cls.split_right_controls = definitions["filterNativeSplitRightSurface"]["componentsData"]
        cls.toolbar = [c for c in cls.items if c["componentData"]["type"] == "filterNativePzToolbar"]
        cls.tools = definitions["filterNativePzToolbar"]["componentsData"]
        cls.top_native = [c for c in cls.items if c["componentData"]["name"].startswith("Filter Native ")
                          and c not in cls.surfaces and c not in cls.toolbar]
        cls.native = cls.top_native + cls.controls + cls.curve_controls

    def test_marker_and_instance_rects_are_clear_in_all_skin_states(self):
        self.assertEqual(self.result, (0, ""))
        markers = [c for c in self.items if c["componentData"]["name"].startswith("Overlay ")]
        self.assertEqual(len(markers), 3)
        self.assertTrue(self.native)
        self.assertEqual(len(self.split_left_surfaces), 2)
        self.assertEqual(len(self.split_right_surfaces), 2)
        mod_editors = [c for c in self.items if c["componentData"]["type"] == "nativeModulationEditor"]
        self.assertEqual(len(self.items), len(markers) + len(self.surfaces) + len(self.top_native) + len(self.toolbar) + len(mod_editors))
        all_leaf_controls = self.native + self.split_left_controls + self.split_right_controls + self.tools
        for mark in markers:
            self.assertFalse(any(overlaps(rect(mark), rect(c)) for c in all_leaf_controls), mark["componentData"]["name"])
        for side, colour in (("left", (16, 58, 44)), ("right", (58, 16, 100))):
            with Image.open(self.skin / f"overlay_marker_filter1_{side}.png") as im:
                self.assertEqual(im.size, (6, 6))
                self.assertEqual(set(im.convert("RGB").getdata()), {colour})
        for state in ("focussed", "unfocussed"):
            self.assertEqual(self.tab["componentDefinition"]["backgroundData"][state]["image"], "overlay_bg_filter.png")
        with Image.open(self.skin / "overlay_bg_filter.png") as im:
            self.assertEqual(set(im.crop((900, 95, 905, 100)).getdata()), {(23, 28, 53)})
            # The compositor patches these exact factory-background colours.
            for x in (33, 45, 1241):
                self.assertEqual(set(im.crop((x - 1, 3, x + 7, 11)).getdata()), {(11, 16, 38)})
            self.assertEqual([im.getpixel((899, y)) for y in range(95, 100)],
                             [(10, 15, 35), *([(10, 15, 34)] * 4)])

    def test_off_navigation_and_model_permissions_are_explicit(self):
        state = len(self.params) + 213
        action = state + 28
        by_name = {c["componentData"]["name"]: c for c in self.native}
        self.assertEqual(len(by_name), len(self.native))
        for index, label in enumerate(("F1", "F2", "NOTE", "VELO"), 1):
            c = by_name[f"Filter Native {label}"]
            self.assertEqual(handle(c), action)
            self.assertEqual(c["bounds"]["additionalInvalidatingHandles"], [f"RangedEnabling/3/4/5/Parameter {state}"])
            self.assertEqual(c["componentData"]["data"]["buttonId"], index)
            exp = by_name[f"Filter Native Exp {label}"]
            self.assertEqual(handle(exp), action)
            self.assertEqual(exp["bounds"]["additionalInvalidatingHandles"][0], f"RangedEnabling/1/2/5/Parameter {state}")
        power = by_name["Filter Native Power"]
        self.assertEqual(handle(power), state + 1)
        self.assertEqual(rect(power), (12, 16, 56, 56))
        self.assertEqual(power["bounds"].get("additionalInvalidatingHandles", []), [])
        self.assertEqual(self.surface["bounds"]["additionalInvalidatingHandles"][0],
                         f"RangedEnabling/1/2/5/Parameter {state}")
        for on, colour in (("on", (92, 255, 157)), ("off", (255, 79, 79))):
            with Image.open(self.skin / f"filter_native_power_{on}.png") as im:
                self.assertEqual(im.size, (56, 56))
                opaque = [p[:3] for p in im.getdata() if p[3]]
                self.assertTrue(any(max(abs(p[c] - colour[c]) for c in range(3)) <= 4 for p in opaque))
        target = by_name["Filter Native Targets"]
        self.assertEqual(handle(target), action)
        self.assertEqual(self.curve_surface["bounds"]["additionalInvalidatingHandles"], [f"RangedEnabling/3/4/5/Parameter {state}"])

    def test_active_widgets_follow_all_four_views_and_filter_models(self):
        state = len(self.params) + 213

        def current_names(view, model, power_frame, *, split=False, scalar=0):
            args = {state: view, state + 18: model, state + 31: 2 if (split and scalar == 0) else 0,
                    state + 32: 2 if (split and scalar == 1) else 0, state + 33: power_frame}
            args.update((i, 0) for i, p in enumerate(self.params) if p["key"] in ("curve.note.legato", "curve.velocity.legato"))
            args.update((i, model) for i, p in enumerate(self.params) if p["key"] in ("filter1.model", "filter2.model"))
            for i, p in enumerate(self.params):
                if p["key"] in ("filter1.on", "filter2.on"):
                    selected = p["key"] == f"filter{view}.on"
                    args[i] = int(power_frame >= 2) if selected else int(power_frame == 1)
            if power_frame < 2:
                args[state + 18] = 0  # disarmed editable MODEL proxy must not select passive geometry

            def matches(expr):
                kind, a, *rest = expr.split("/")
                b = rest[0] if kind == "RangedEnabling" else a
                p = int(rest[-1].split()[-1])
                return int(a) <= args[p] <= int(b)

            def visible(component):
                return all(matches(gate) for gate in component["bounds"].get("additionalInvalidatingHandles", []))

            if split:
                left_ok = any(visible(s) for s in self.split_left_surfaces)
                right_ok = any(visible(s) for s in self.split_right_surfaces)
                out = set()
                if left_ok:
                    out |= {c["componentData"]["name"].removeprefix("Filter Native ") for c in self.split_left_controls if visible(c)}
                if right_ok:
                    out |= {c["componentData"]["name"].removeprefix("Filter Native ") for c in self.split_right_controls if visible(c)}
                return out

            return {c["componentData"]["name"].removeprefix("Filter Native ") for c in self.native
                    if visible(c)
                    and (c not in self.controls or visible(self.surface))
                    and (c not in self.curve_controls or visible(self.curve_surface))}

        self.assertEqual(current_names(0, 0, 0), set())
        for view in (1, 2):
            off = current_names(view, 5, 0)
            self.assertIn("Power", off)
            self.assertFalse(any(n.startswith("Cutoff ") and not n.endswith(" off") for n in off))
            self.assertTrue({"Exp F1", "Exp F2", "Exp NOTE", "Exp VELO", "dual"} <= off)
            for model in range(27):
                names = current_names(view, model, 2)
                self.assertEqual(sum(n.startswith("Cutoff ") and not n.endswith(" off") for n in names), 1)
                self.assertEqual(any(n.startswith("F Reso ") and not n.endswith(" off") for n in names), model == 5)
                self.assertEqual(any(n.startswith("Comb ") and not n.endswith(" off") for n in names), model == 6)
                self.assertEqual(any(n.startswith("Clean ") and not n.endswith(" off") for n in names), model not in (5, 6))
            for model in range(27):
                passive = current_names(view, model, 0, split=True)
                self.assertEqual(any(n.startswith("Split F Reso ") for n in passive), model == 5)
                self.assertEqual(any(n.startswith("Split Var ") for n in passive), model != 5)
                self.assertEqual(any(n.startswith("Split Comb ") for n in passive), model == 6)
                self.assertEqual(any(n.startswith("Split Clean ") for n in passive), model not in (5, 6))
            # Expanded shared ROUTING uses raw F1/F2 power, not the dual-view bit.
            expanded_both_off = current_names(view, 3, 0)
            self.assertFalse(any(n.startswith("Routing ") and not n.endswith(" off") for n in expanded_both_off))
            expanded_other_on = current_names(view, 3, 1)
            self.assertEqual(sum(n.startswith("Routing ") and not n.endswith(" off") for n in expanded_other_on), 3)
            self.assertFalse(any(n.startswith("Routing ") and n.endswith(" off") for n in expanded_other_on))
            # Split mode: independent F1/F2 + NOTE/VELO + shared ROUTING active when only one filter is OFF (power_frame=1)
            split_both_off = current_names(view, 3, 0, split=True, scalar=0)
            self.assertIn("Split Power", split_both_off)
            self.assertIn("Split Routing 0 0 off", split_both_off)
            self.assertNotIn("Split Routing 0 0", split_both_off)
            self.assertIn("Split Curve Point", split_both_off)
            split_one_off = current_names(view, 3, 1, split=True, scalar=1)
            self.assertIn("Split Routing 0 0", split_one_off)
            self.assertNotIn("Split Routing 0 0 off", split_one_off)
            self.assertIn(f"Split Cutoff 0 F{view} off", split_one_off)
            self.assertIn("Split Curve Point", split_one_off)
            split_on = current_names(view, 5, 2, split=True, scalar=0)
            self.assertTrue(any(n.startswith("Split F Reso ") and not n.endswith(" off") for n in split_on))
            self.assertFalse(any(n.startswith("Split X ") for n in split_on))
        for view in (3, 4):
            names = current_names(view, 0, 0)
            self.assertTrue({"Curve Point", "Curve Legato", "Init", "Undo", "Delete Point", "Targets"} <= names)
            self.assertFalse({"Power", "dual"} & names)

    def test_model_and_style_open_the_sdk_domains(self):
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        named = {c["componentData"]["name"].removeprefix("Filter Native "): c for c in self.native}
        for key in ("filterNativeModel", "filterNativeStyle"):
            self.assertEqual([(a["onAction"], a["handler"], a["handleName"], a["additionalData"]) for a in definitions[key]["actions"]],
                             [(event, "Show Overlay", "Data", "fx editor choice menu") for event in ("Mouse Down", "Enter Pressed")])
        self.assertEqual(sum(c["componentData"]["type"] == "MenuOverlay"
                             for c in definitions["fx editor choice menu"]["componentsData"]), 1)
        self.assertEqual(handle(named["Model"]), 2779)
        fx = json.loads((base.REAL_EXTRAS / "fx-native-contract.json").read_text())["domains"]
        filter_contract = json.loads((base.REAL_EXTRAS / "filter-native-contract.json").read_text())
        replacements = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}
        self.assertEqual(replacements["Parameter 2779"], fx[0])
        for model in range(27):
            control = named.get(f"Style {model}")
            if model in filter_contract["one_style"]:
                self.assertIsNone(control)
                continue
            self.assertIsNotNone(control)
            self.assertEqual(handle(control), 2800 + model)
            self.assertIn(f"IndexedEnabling/{model}/27/Parameter 2868", control["bounds"]["additionalInvalidatingHandles"])
            self.assertEqual(replacements[f"Parameter {2800 + model}"], fx[21 + model])

    def test_readouts_and_scalar_controls_use_the_exact_visible_owner(self):
        ids = {p["key"]: i for i, p in enumerate(self.params)}
        state = len(self.params) + 213
        items = {c["componentData"]["name"].removeprefix("Filter Native "): c for c in self.native}
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        manifest = json.loads((base.REAL_EXTRAS / "filter-native-contract.json").read_text())
        for f in (1, 2):
            for on in (0, 1):
                out = items[f"OUT F{f} {'on' if on else 'off'}"]
                self.assertEqual(rect(out), tuple(manifest["out"]))
                self.assertEqual(out["bounds"]["acceptsHWFocus"], "No")
                self.assertEqual(out["bounds"]["additionalInvalidatingHandles"],
                                 [f"IndexedEnabling/{f}/5/Parameter {state}",
                                  f"RangedEnabling/{2 * on}/{2 * on + 1}/4/Parameter {state + 33}"])
                expected = ("destination", "bus1_send", "bus2_send", "level", "pan")
                self.assertEqual(out["handle remapping"]["map"], [
                    {"key": f"Readout{i}", "value": f"Parameter {ids[f'filter{f}.{key}']}"}
                    for i, key in enumerate(expected)])
                definition = definitions[out["componentData"]["type"]]
                self.assertTrue(definition["ignoreMousePresses"])
                self.assertFalse(definition["actions"])
                self.assertEqual(sum(c["componentData"]["data"]["type"] == "Value" for c in definition["componentsData"]), 5)
                bg = definition["backgroundData"]
                self.assertEqual(bg["focussed"], bg["unfocussed"])
                with Image.open(self.skin / bg["focussed"]["image"]) as plate:
                    _, _, w, h = rect(out)
                    self.assertEqual(plate.size, (w, h))
                    for row, value in enumerate(definition["componentsData"]):
                        x, y, width, height = rect(value)
                        self.assertGreater(len(plate.crop((0, y, x, y + height)).getcolors()), 1)
                        self.assertEqual(len(plate.crop((x, y, x + width, y + height)).getcolors()), 1)
                        unit = plate.crop((x + width, y, w, y + height))
                        self.assertEqual(len(unit.getcolors()), 1, "host Value owns the units; no duplicate static unit caption")
        for frame, source in ((3, "note"), (4, "velocity")):
            glide = items[f"Glide {source}"]
            self.assertEqual(rect(glide), tuple(manifest["glide"]))
            self.assertEqual(handle(glide), ids["master.portamento.time"])
            self.assertEqual(glide["bounds"]["acceptsHWFocus"], "No")
            self.assertEqual(glide["bounds"]["additionalInvalidatingHandles"],
                             [f"IndexedEnabling/{frame}/5/Parameter {state}",
                              f"IndexedEnabling/1/2/Parameter {ids[f'curve.{source}.legato']}"])

        for name, source in (("Snap", manifest["curve"][6]), ("Legato", manifest["curve"][7])):
            toggle = items["Curve " + name]
            self.assertEqual(rect(toggle), tuple(source)[:2] + (min(source[2], 120), min(source[3], 52)) if False else toggle["bounds"]["bounds"] and rect(toggle))
            self.assertEqual(toggle["componentData"]["type"], "Button")
            self.assertNotIn("Curve " + name + " label", items)
        for name in ("Point", "X", "Y", "Curve", "Grid X", "Grid Y"):
            nv = items[f"Curve {name}"]
            nv_def = definitions[nv["componentData"]["type"]]
            value_child = next(c for c in nv_def["componentsData"] if c["componentData"]["name"] == "value")
            self.assertEqual(value_child["componentData"]["data"]["type"], "Value")
            self.assertEqual(handle(nv), int(nv["handle remapping"]["map"][0]["value"].split()[-1]))
            self.assertEqual(nv["bounds"]["acceptsHWFocus"], "Yes")

    def test_pz_launcher_uses_the_shared_graph_corner_icon_and_named_focus(self):
        state = len(self.params) + 213
        items = {c["componentData"]["name"].removeprefix("Filter Native "): c for c in self.native}
        manifest = json.loads((base.REAL_EXTRAS / "filter-native-contract.json").read_text())
        for model in manifest["one_style"]:
            self.assertEqual(items[f"Single Style {model}"]["componentData"]["data"]["type"], "Text")
            self.assertNotIn(f"Style {model}", items)
        pz = items["Edit PZ Curve"]
        self.assertEqual(pz["handle remapping"]["map"],
                         [{"key": "Data", "value": f"WithName[Edit PZ Curve]/FromGesture/Parameter {state + 28}"}])
        data = pz["componentData"]["data"]
        self.assertEqual((data["buttonId"], data["numButtonsInGroup"]), (6, 13))
        self.assertEqual(data["onImage"], "filter_native_expand_56x56.png")
        self.assertEqual(data["offImage"], data["onImage"])
        self.assertEqual(pz["bounds"]["acceptsHWFocus"], "Yes")
        self.assertEqual(rect(pz), tuple(manifest["pz_expand"]))
        self.assertEqual(rect(pz)[2:], (56, 56))
        self.assertEqual(pz["bounds"]["additionalInvalidatingHandles"],
                         [f"RangedEnabling/1/2/5/Parameter {state}",
                          f"RangedEnabling/0/1/4/Parameter {state + 31}",
                          f"RangedEnabling/0/1/4/Parameter {state + 32}",
                          f"RangedEnabling/2/3/4/Parameter {state + 33}",
                          f"IndexedEnabling/21/27/Parameter {state + 18}"])
        style = rect(items["Single Style 21"])
        self.assertFalse(overlaps(style, rect(pz)))
        self.assertEqual(style, tuple(manifest["filter"][0][18]))

    def test_pz_toolbar_keeps_exact_source_focus_and_draft_eligibility(self):
        self.assertEqual(len(self.toolbar), 2)
        for f, parent in enumerate(self.toolbar, 1):
            self.assertEqual(rect(parent), (0, 0, 1280, 72))
            self.assertEqual(parent["bounds"]["acceptsHWFocus"], "Children")
            self.assertEqual(handle(parent), 3493 + f)
            self.assertEqual(parent["bounds"]["additionalInvalidatingHandles"],
                             [f"IndexedEnabling/{f}/3/Parameter 3496"])
        manifest = json.loads((base.REAL_EXTRAS / "filter-native-contract.json").read_text())
        by_name = {c["componentData"]["name"].removeprefix("Filter Native PZ "): c for c in self.tools}
        self.assertEqual(len(by_name), 5)
        art = by_name["draft state"]
        self.assertEqual(handle(art), 3497)
        self.assertEqual(art["bounds"]["acceptsHWFocus"], "No")
        self.assertEqual(art["bounds"].get("additionalInvalidatingHandles", []), [])
        image = art["componentData"]["data"]
        self.assertEqual(image["numFrames"], 4)
        x, y, w, h = rect(art)
        self.assertEqual((x, y, x + w, h), (manifest["pz_toolbar"][2][0], manifest["pz_toolbar"][2][1],
                                           sum(manifest["pz_toolbar"][3][::2]), manifest["pz_toolbar"][2][3]))
        for i, (label, expected) in enumerate(zip(("Back", "Reset", "Delete Point", "Apply"), manifest["pz_toolbar"]), 1):
            button = by_name[label]
            self.assertEqual(rect(button), tuple(expected))
            self.assertEqual(button["bounds"]["acceptsHWFocus"], "Yes")
            self.assertEqual(button["componentData"]["type"], "Button")
            data = button["componentData"]["data"]
            self.assertEqual((data["buttonId"], data["numButtonsInGroup"], data["gestureBehaviour"]), (i, 5, "Instant"))
            self.assertEqual(button["handle remapping"]["map"], [{"key": "Data", "value": f"WithName[{label}]/FromGesture/Data"}])
            gates = [] if i < 3 else [f"RangedEnabling/{i - 2}/{i - 1}/4/Parameter 3497"]
            self.assertEqual(button["bounds"].get("additionalInvalidatingHandles", []), gates)
            if i < 3:
                continue
            with Image.open(self.skin / image["image"]) as film:
                self.assertEqual(film.size, (w, h * 4))
                self.assertEqual([film.getpixel((expected[0] - x + 5, h * f + h // 2)) for f in range(4)],
                                 [(22, 29, 49, 255) if i - 2 <= f <= i - 1 else (32, 32, 32, 255) for f in range(4)])

    def test_off_dials_are_read_only_shared_art_with_stored_engine_values(self):
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        ids = {p["key"]: i for i, p in enumerate(self.params)}
        for f in (1, 2):
            off = next(c for c in self.split_left_controls if c["componentData"]["name"] == f"Filter Native Split Cutoff 0 F{f} off")
            definition = definitions[off["componentData"]["type"]]
            self.assertTrue(definition["ignoreMousePresses"])
            self.assertTrue(definition["disableCoarseDataWheel"])
            self.assertFalse(definition["actions"])
            self.assertFalse(any(c["componentData"]["type"] == "Knob" for c in definition["componentsData"]))
            self.assertEqual(off["handle remapping"]["map"][0]["value"], f"Parameter {ids[f'filter{f}.cutoff']}")
            self.assertEqual(off["bounds"]["acceptsHWFocus"], "No")

    def test_integer_remainder_cell_widths_share_component_subtrees(self):
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        compact_dials = [key for key in definitions if key.startswith("filterNativeDial_side_") and key.endswith("x76")]
        self.assertTrue(compact_dials)
        self.assertTrue(all(key.endswith("_182x76") for key in compact_dials))
        compact_fields = [key for key in definitions if key.startswith("filterNativeNv_") and key.endswith("x56")]
        self.assertEqual(compact_fields, ["filterNativeNv_119x56"])

    def test_filter_native_interactive_children_keep_real_56px_hit_targets(self):
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        visited = set()

        def check_children(entries):
            for entry in entries:
                kind = entry["componentData"]["type"]
                if kind in ("Knob", "Button", "NumberField", "ActiveText"):
                    self.assertGreaterEqual(min(rect(entry)[2:]), 56, entry["componentData"]["name"])
                if kind in definitions and kind not in visited:
                    visited.add(kind)
                    check_children(definitions[kind].get("componentsData", []))

        check_children(self.controls + self.curve_controls + self.split_left_controls + self.split_right_controls)

    def test_each_routing_caption_is_in_its_exact_single_button_hitbox(self):
        manifest = json.loads((base.REAL_EXTRAS / "filter-native-contract.json").read_text())
        for prefix, source in (("Filter Native Routing ", manifest["filter"][0][10]),
                               ("Filter Native Split Routing ", manifest["split"]["filter"][0][10])):
            pool = self.controls if prefix == "Filter Native Routing " else self.split_left_controls
            routes = [c for c in pool if c["componentData"]["name"].startswith(prefix)
                      and not c["componentData"]["name"].endswith(" off")]
            branches = [[c for c in routes if " alternative" not in c["componentData"]["name"]]]
            if prefix == "Filter Native Routing ":
                branches.append([c for c in routes if " alternative" in c["componentData"]["name"]])
            self.assertEqual(len(routes), 3 * len(branches), prefix)
            x, y, w, h = source
            for branch in branches:
                self.assertEqual(len(branch), 3)
                for index, route in enumerate(branch):
                    data = route["componentData"]["data"]
                    top = index * h // 3
                    self.assertEqual(rect(route), (x, y + top, w, (index + 1) * h // 3 - top))
                    self.assertEqual((data["buttonId"], data["numButtonsInGroup"], handle(route)), (index, 3, 2861))
                    point = (x + w // 2, y + (2 * index + 1) * h // 6, 1, 1)
                    self.assertEqual([c for c in branch if overlaps(point, rect(c))], [route])



if __name__ == "__main__":
    unittest.main()
