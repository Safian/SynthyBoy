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
        sentinel, = [c for c in markers if c["componentData"]["name"] == "Overlay Background Sentinel filter1"]
        self.assertEqual(len(markers), 4)
        self.assertIs(sentinel, self.items[-1])
        self.assertEqual(sentinel["bounds"]["acceptsHWFocus"], "No")
        self.assertTrue(self.native)
        self.assertEqual(len(self.split_left_surfaces), 2)
        self.assertEqual(len(self.split_right_surfaces), 2)
        mod_editors = [c for c in self.items if c["componentData"]["type"] == "nativeModulationEditor"]
        self.assertEqual(len(self.items), len(markers) + len(self.surfaces) + len(self.top_native) + len(self.toolbar) + len(mod_editors))
        all_leaf_controls = self.native + self.split_left_controls + self.split_right_controls + self.tools
        for mark in (c for c in markers if c is not sentinel):
            self.assertFalse(any(overlaps(rect(mark), rect(c)) for c in all_leaf_controls), mark["componentData"]["name"])
        for side, colour in (("left", (16, 58, 44)), ("right", (58, 16, 100))):
            with Image.open(self.skin / f"overlay_marker_filter1_{side}.png") as im:
                self.assertEqual(im.size, (6, 6))
                self.assertEqual(set(im.convert("RGB").getdata()), {colour})
        for state in ("focussed", "unfocussed"):
            self.assertEqual(self.tab["componentDefinition"]["backgroundData"][state]["image"], "overlay_canvas.png")
        self.assertEqual(rect(sentinel), (900, 95, 5, 5))
        with Image.open(self.skin / sentinel["componentData"]["data"]["image"]) as im:
            self.assertEqual(im.size, (5, 5))
            self.assertEqual(set(im.convert("RGB").getdata()), {(23, 28, 53)})
        with Image.open(self.skin / "overlay_canvas.png") as im:
            for mark in markers:
                x, y, w, h = rect(mark)
                self.assertEqual(set(im.crop((x, y, x + w, y + h)).getdata()), {(9, 13, 25)})

    def test_off_navigation_and_model_permissions_are_explicit(self):
        state = len(self.params) + 213
        action = state + 28
        by_name = {c["componentData"]["name"]: c for c in self.native}
        self.assertEqual(len(by_name), len(self.native))
        for index, label in enumerate(("NOTE", "VELO"), 3):
            c = by_name[f"Filter Native {label}"]
            self.assertEqual(handle(c), action)
            self.assertEqual(c["bounds"]["additionalInvalidatingHandles"], [f"RangedEnabling/3/4/5/Parameter {state}"])
            self.assertEqual(c["componentData"]["data"]["buttonId"], index)
            exp = by_name[f"Filter Native Exp {label}"]
            self.assertEqual(handle(exp), action)
            self.assertEqual(exp["bounds"]["additionalInvalidatingHandles"][0], f"RangedEnabling/1/2/5/Parameter {state}")
        power = by_name["Filter Native Power"]
        self.assertEqual(handle(power), state + 1)
        self.assertEqual(rect(power), (16, 80, 56, 56))
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
            args.update((i, 0) for i, p in enumerate(self.params) if p["key"] == "filter.routing")
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
            self.assertTrue({"Chain", "Exp NOTE", "Exp VELO", "dual"} <= off)
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
            for power in (0, 1, 2):
                self.assertIn("Chain", current_names(view, 3, power))
                self.assertIn("Split Chain", current_names(view, 3, power, split=True))
            split_one_off = current_names(view, 3, 1, split=True, scalar=1)
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
                self.assertFalse(bg["focussed"].get("image"))
                children = definition["componentsData"]
                self.assertEqual(len(children), 10)
                previous_bottom = 0
                for i, name in enumerate(("DEST", "BUS 1", "BUS 2", "LEVEL", "PAN")):
                    label, value = children[2 * i:2 * i + 2]
                    caption, readout = (c["componentData"]["data"] for c in (label, value))
                    self.assertEqual((caption["type"], caption["text"]), ("Text", name))
                    self.assertEqual((readout["type"], readout["handleName"]), ("Value", f"Readout{i}"))
                    x, y, w, h = rect(label)
                    vx, vy, vw, vh = rect(value)
                    self.assertEqual((y, h), (vy, vh))
                    self.assertGreaterEqual(y, previous_bottom)
                    self.assertLessEqual(x + w, vx)
                    self.assertLessEqual(vx + vw, rect(out)[2])
                    previous_bottom = y + h
                self.assertLessEqual(previous_bottom, rect(out)[3])
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
            x, y, w, h = source
            tw, th = rect(toggle)[2:]
            self.assertLessEqual(tw, w)
            self.assertLessEqual(th, h)
            self.assertEqual(rect(toggle)[:2], (x + (w - tw) // 2, y + (h - th) // 2))
            self.assertEqual(toggle["componentData"]["type"], "Button")
            self.assertNotIn("Curve " + name + " label", items)
        for index, name in enumerate(("Point", "X", "Y", "Curve", "Grid X", "Grid Y")):
            nv = items[f"Curve {name}"]
            nv_def = definitions[nv["componentData"]["type"]]
            value_child = next(c for c in nv_def["componentsData"] if c["componentData"]["name"] == "value")
            self.assertEqual(value_child["componentData"]["data"]["type"], "Value")
            self.assertEqual(handle(nv), state + 20 + index)
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
            knob, = [c["componentData"]["data"] for c in definition["componentsData"] if c["componentData"]["type"] == "Knob"]
            self.assertEqual(knob["dragOrientation"], "None")
            self.assertTrue(knob["filmStrip"].endswith("_disabled.png"))
            self.assertEqual(knob["handleName"], "Data")
            self.assertEqual(off["handle remapping"]["map"][0]["value"], f"Parameter {ids[f'filter{f}.cutoff']}")
            self.assertEqual(off["bounds"]["acceptsHWFocus"], "No")

    def test_integer_remainder_cell_widths_share_component_subtrees(self):
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        compact_dials = [key for key in definitions if key.startswith("filterNativeDial_side_") and key.endswith("x60")]
        self.assertEqual({key.rsplit("_", 1)[1] for key in compact_dials}, {"128x60", "131x60", "200x60"})
        compact_fields = [key for key in definitions if key.startswith("filterNativeNv_") and key.endswith("x56")]
        self.assertEqual(compact_fields, ["filterNativeNv_119x56"])

    def test_filter_native_interactive_children_fit_full_and_compact_controls(self):
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        visited = set()

        def check_children(entries, minimum, size):
            for entry in entries:
                kind = entry["componentData"]["type"]
                w, h = map(float, entry["bounds"]["bounds"].split()[2:])
                if entry["bounds"]["boundsType"] == "Proportional":
                    w, h = w * size[0], h * size[1]
                if kind in ("Knob", "Button", "NumberField", "ActiveText"):
                    self.assertGreaterEqual(min(w, h), minimum, entry["componentData"]["name"])
                key = (kind, w, h, minimum)
                if kind in definitions and key not in visited:
                    visited.add(key)
                    check_children(definitions[kind].get("componentsData", []), minimum, (w, h))

        for group, parent, minimum in ((self.controls, self.surface, 56), (self.curve_controls, self.curve_surface, 56),
                                       (self.split_left_controls, self.split_left_surfaces[0], 48),
                                       (self.split_right_controls, self.split_right_surfaces[0], 48)):
            check_children(group, minimum, rect(parent)[2:])

    def test_compact_filter_chain_shares_native_menu_and_actual_power(self):
        defs = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        instances = [item for item in (*self.top_native, *self.split_left_controls, *defs["mixerPeekFilterEditorSurface"]["componentsData"])
                     if item["componentData"]["type"] == "filterNativeChain"]
        self.assertEqual(len(instances), 12)
        params = {p["key"]: i for i, p in enumerate(self.params)}
        replacements = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}
        for instance in instances:
            bindings = {r["key"]: r["value"] for r in instance["handle remapping"]["map"]}
            for f in (1, 2):
                self.assertEqual(bindings[f"Power{f}"], "Parameter " + str(params[f"filter{f}.on"]))
            labels = replacements[bindings["Data"]]
            self.assertEqual([label.removesuffix(" ✓") for label in labels],
                             ["F1", "F2", "Parallel", "F1 to F2", "F2 to F1"])
            route, = [i - 2 for i, text in enumerate(labels) if text.endswith(" ✓")]
            self.assertIn(f"IndexedEnabling/{route}/3/Parameter {params['filter.routing']}",
                          instance["bounds"]["additionalInvalidatingHandles"])
            self.assertEqual(bindings["Edited"], "Parameter 2880")
        definition = defs["filterNativeChain"]
        face = definition["componentsData"][0]["componentData"]["data"]
        self.assertEqual((face["imageType"], face["numFrames"], face["handleName"]), ("FilmStrip", 4, "Edited"))
        actions = definition["actions"]
        self.assertEqual([a["additionalData"] for a in actions], ["filterNativeChainChoices"] * 2)
        self.assertEqual([a["onAction"] for a in actions], ["Mouse Down", "Enter Pressed"])
        self.assertEqual(sum(c["componentData"]["type"] == "MenuOverlay"
                             for c in defs["filterNativeChainChoices"]["componentsData"]), 1)
        rows = [c for c in defs["filterNativeChainChoices"]["componentsData"] if c["componentData"]["type"] == "Button"]
        self.assertEqual([r["componentData"]["data"]["buttonId"] for r in rows], list(range(5)))
        for pool in (self.controls, self.split_left_controls):
            self.assertFalse(any("Routing " in c["componentData"]["name"] for c in pool))


if __name__ == "__main__":
    unittest.main()
