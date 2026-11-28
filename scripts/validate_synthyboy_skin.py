#!/usr/bin/env python3
"""Check the native skin that the MPC looks up for the SynthyBoy instrument."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

try:
    from jsonschema import Draft7Validator
except ImportError:  # a missing validator must fail the install, never skip the check
    sys.exit("jsonschema is required (pip install jsonschema); the skin schema check cannot be skipped")

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "scripts/skin-baseline/TUI.keys.json"
TUI_SCHEMA = ROOT / "schemas/skin-tui.schema.json"
QLINKS_SCHEMA = ROOT / "schemas/skin-qlinks.schema.json"
SDK_MAX_TABS = 32  # SDK ui-reference.md 3.5; the proven skin (git 1fd0d592) runs 37 tabs on both devices
PROVEN_MAX_TABS = 37
# Largest values the proven skin uses; the SDK gives no upper limit, so anything above is a warning.
PROVEN_BUTTON_MAX = {"buttonId": 98, "numButtonsInGroup": 99}
PROVEN_LOCAL_IDS = {"synthyboyWarpPage"}
IMAGE_KEYS = {"image", "onImage", "offImage", "filmStrip", "thumbImage", "revealedImage", "inactiveImage", "peakImage", "rmsImage"}
# One identifying control per physical tab. The shared OSC surface serves five tabs.
SURFACES = {
    "Play": "playOscHeaderSelectorW360", "Macros": "macroCard", "Osc A": "oscNativeSurface",
    "Osc B": "oscNativeSurface", "Osc C": "oscNativeSurface", "Sub": "oscNativeSurface",
    "Noise": "oscNativeSurface", **dict.fromkeys(("Env 1", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6"), "envNativeFullFields"), **dict.fromkeys(("LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "LFO 9", "LFO 10", "RND 1", "RND 2", "RND 3", "RND 4"), "lfoNativeFullEditor"),
    "FX": "fxNativeEditorSurface", "Mix": "mixerStripSourceFrame", "Filter 1": "filterNativeEditorSurface", "Filter 2": "filterNativeEditorSurface", "Note": "filterNativeCurveSurface", "Velo": "filterNativeCurveSurface",
    "Mod": "modNativeRowNormal", "Master": "masterNativeOutputMeters", "Tuning": "masterNativeBlock_scale", "Voice": "voiceNativeStepLevel",
}
BUILTINS = {"Button", "Focus", "Image", "Knob", "Label", "MenuOverlay", "Meter", "NumericOverlay", "Slider"}
SDK_TEMPLATES = {"knobUni", "knobBi", "knob overlay", "menu overlay", "toggle", "faderUni", "faderBi",
                 "radio3", "radio4", "sliderWithFocus", "sliderWithFocusMenu"}


# Repeating screens: the items shared by every tab of a family live in '<family>Screen' definitions (skin/defs_<family>.json), one per run of shared
# items between the per-tab items (<family>Screen, <family>ScreenTop, ...). Every tab of the family holds those instances, in that order (z-order).
SCREEN_FAMILIES = {
    ("oscScreen", "oscScreenTop"): ["Osc A", "Osc B", "Osc C", "Sub", "Noise"],
    ("envScreen", "envScreenTop"): [f"Env {i}" for i in range(1, 7)],
    ("lfoScreen", "lfoScreenTop", "lfoScreenTop2", "lfoScreenTop3"): [f"LFO {i}" for i in range(1, 11)] + [f"RND {i}" for i in range(1, 5)],
    ("filterScreen", "filterScreenTop"): ["Filter 1", "Filter 2"],
}
# Generated header form containers: narrowed here because 104 legacy/leaf definitions outside header forms
# (leaf Button/Knob cells, macroCard, oscNativeTune, etc.) still use ignoreMousePresses: false with actions: [].
HEADER_FORMS = (
    "oscHeaderAbc", "oscHeaderSub", "oscHeaderNoise", "oscHeaderNoiseSample",
    "envHeader", "lfoHeader", "curveHeader", "modHeader",
    "tuningHeader", "voiceHeader", "masterHeader", "macroHeader",
)


def walk(value):
    yield value
    if isinstance(value, dict):
        for child in value.values():
            yield from walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


def read_imports(tui_path, loader=lambda text: json.loads(text), fail=None, sources=None):
    """TUI.json with its importFiles merged in: the host's own importFiles split (docs/MPC.md, skin section). Returns the document whose
    componentDefinitions.localComponentDefinitions is the union (TUI.json's own definitions first, then every import file in the listed
    order) and whose importFiles is empty, so every other check sees one merged view. `fail(path, message)` reports a problem; default raises."""
    tui_path = Path(tui_path)

    def raise_error(path, message):
        raise ValueError(f"{path}: {message}")

    fail = fail or raise_error
    doc = loader(tui_path.read_text(encoding="utf-8"))
    try:
        definitions = doc["pageData"]["componentDefinitions"]
        names = definitions["importFiles"]
        merged = list(definitions["localComponentDefinitions"])
    except (KeyError, TypeError):  # a shape error: the schema check reports it
        return doc
    if sources is not None:
        for item in merged:
            if isinstance(item, dict) and isinstance(item.get("key"), str):
                sources[item["key"]] = tui_path.name
    if not isinstance(names, list) or not all(isinstance(n, str) for n in names):
        return doc
    for name in names:
        path = tui_path.parent / name
        if "/" in name or "\\" in name or name in (tui_path.name, "Q-Links.json"):
            fail("$.pageData.componentDefinitions.importFiles", f"import file '{name}' must be a flat file name at the skin root")
            continue
        if not path.is_file():
            fail("$.pageData.componentDefinitions.importFiles", f"import file '{name}' is missing (the host would silently drop its definitions)")
            continue
        imported = loader(path.read_text(encoding="utf-8"))
        try:
            wrapped = imported["componentDefinitions"]
            ok = (list(imported) == ["componentDefinitions"] and wrapped["version"] == 2 and wrapped["importFiles"] == []
                  and isinstance(wrapped["localComponentDefinitions"], list))
        except (KeyError, TypeError):
            ok = False
        if not ok:
            fail(name, 'import file must have the wrapped shape {"componentDefinitions": {"version": 2, "importFiles": [], "localComponentDefinitions": [...]}}')
            continue
        if sources is not None:
            for item in wrapped["localComponentDefinitions"]:
                if isinstance(item, dict) and isinstance(item.get("key"), str):
                    sources[item["key"]] = name
        merged += wrapped["localComponentDefinitions"]
    for stray in sorted(tui_path.parent.glob("defs_*.json")):
        if stray.name not in names:
            fail(stray.name, "skin definitions file is not listed in TUI.json importFiles (the host would ignore it)")
    definitions["localComponentDefinitions"] = merged
    definitions["importFiles"] = []
    return doc


def current_hashes(tui_path):
    """sha256 of the canonical JSON of every localComponentDefinitions entry (TUI.json plus its import files), by key."""
    definitions = read_imports(tui_path)["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    return {d["key"]: hashlib.sha256(json.dumps(d, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()
            for d in definitions}


def check_handle_list_shape(page):
    """additionalInvalidatingHandles must be a flat list of strings: a nested list made the host drop the whole TUI (blank page, Key 61 2026-11-28)."""
    for node in walk(page):
        if isinstance(node, dict) and "additionalInvalidatingHandles" in node:
            handles = node["additionalInvalidatingHandles"]
            if not isinstance(handles, list) or not all(isinstance(h, str) for h in handles):
                raise ValueError("additionalInvalidatingHandles must be a flat list of strings: %r" % (handles,))


def plugin_param_count():
    """Engine parameters (docs/params.json) plus the MPC shell extras, computed from the committed headers."""
    engine = json.loads((ROOT / "docs/params.json").read_bytes())["count"]
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "extra"
        build = subprocess.run(["c++", "-std=c++17", "-w", f"-I{ROOT / 'shell-mpc'}", f"-I{ROOT / 'core/include'}",
                                str(ROOT / "scripts/skin-param-extra.cpp"), "-o", str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise ValueError(f"cannot compute the plugin parameter count (c++ failed): {build.stderr.strip()[:400]}")
        return engine + int(subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout)


def check_parameter_handles(page):
    count = plugin_param_count()
    used = {int(m[1]) for value in walk(page) if isinstance(value, str) for m in re.finditer(r"\bParameter (\d+)\b", value)}
    bad = sorted(n for n in used if n >= count)
    if bad:
        raise ValueError(f"'Parameter N' handles at or above the plugin parameter count {count}: {bad[:20]}")


def check_baseline(page):
    if os.environ.get("SYNTHYBOY_SKIN_ACCEPT_CHANGES") == "1":
        return
    base = json.loads(BASELINE.read_bytes())
    definitions = page["componentDefinitions"]["localComponentDefinitions"]
    now = {d["key"]: hashlib.sha256(json.dumps(d, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()
           for d in definitions}
    removed = sorted(k for k in base if k not in now)
    changed = sorted(k for k in base if k in now and now[k] != base[k])
    if removed or changed:
        raise ValueError(f"component definitions differ from scripts/skin-baseline/TUI.keys.json; changed: {changed}; removed: {removed}. "
                         "If intended, set SYNTHYBOY_SKIN_ACCEPT_CHANGES=1; after the owner has proven the skin on the Key 61 run scripts/skin-baseline-update.py")


def check_display_bindings(page, qlinks):
    first = json.loads((ROOT / "docs/params.json").read_bytes())["count"]
    live = range(first, first + 4 + 32)  # shell-mpc/lfo_live.h and shell-mpc/spectrum.h
    for value in walk([page["tabs"], page["componentDefinitions"]]):
        if isinstance(value, str):
            for match in re.finditer(r"\bParameter (\d+)\b", value):
                if int(match[1]) in live:
                    raise ValueError(f"unnotified display binding in native skin: {match[0]}")
    groups = [row["Q-Links"] for row in qlinks["Screen Mode Q-Links"]["map"]] + [qlinks["Program Mode Q-Links"]]
    for group in groups:
        for index in group.values():
            if index in live:
                raise ValueError(f"unnotified display binding in native Q-Links: {index}")


class _Pairs(list):
    """A JSON object as an ordered (key, value) list, so duplicate keys survive parsing."""


class Findings:
    """Errors and (aggregated) warnings, each with a JSON path."""

    def __init__(self):
        self.errors = []
        self.warnings = {}

    def error(self, path, message):
        self.errors.append((path, message))

    def warn(self, path, message):
        entry = self.warnings.setdefault(message, [0, path])
        entry[0] += 1

    def report(self, label, limit=60):
        for path, message in self.errors[:limit]:
            print(f"ERROR   {label} {path}: {message}", file=sys.stderr)
        if len(self.errors) > limit:
            print(f"ERROR   {label} ... and {len(self.errors) - limit} more errors", file=sys.stderr)
        for message, (count, first) in self.warnings.items():
            where = f"{first} (first of {count})" if count > 1 else first
            print(f"WARNING {label} {where}: {message}", file=sys.stderr)


def json_path(parts):
    out = "$"
    for part in parts:
        out += f"[{part}]" if isinstance(part, int) else (f".{part}" if re.fullmatch(r"\w+", part) else f'["{part}"]')
    return out


def dedupe(node, path, findings):
    """Plain dicts and lists from _Pairs. Only the proven duplicate 'version' key of an overlay widget's data is tolerated (first wins)."""
    if isinstance(node, _Pairs):
        out = {}
        for key, value in node:
            child = json_path([*path, key])
            converted = dedupe(value, [*path, key], findings)
            if key in out:
                if not (key == "version" and path[-2:] == ["componentData", "data"]):
                    findings.error(child, f"duplicate key '{key}' (only the 'version' key of a component's data is a proven duplicate)")
                continue
            out[key] = converted
        return out
    if isinstance(node, list):
        return [dedupe(item, [*path, i], findings) for i, item in enumerate(node)]
    return node


def load_skin_json(path, findings):
    return dedupe(json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=_Pairs), [], findings)


def load_skin_tui(path, findings, sources=None):
    """TUI.json plus its import files as one merged document (importFiles emptied); each file is parsed and duplicate-checked like TUI.json."""
    return read_imports(path, loader=lambda text: dedupe(json.loads(text, object_pairs_hook=_Pairs), [], findings),
                        fail=findings.error, sources=sources)


def schema_errors(schema_path, document, findings):
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    for err in sorted(Draft7Validator(schema).iter_errors(document), key=lambda e: [str(p) for p in e.absolute_path]):
        text = " ".join(err.message.split())
        findings.error(json_path(err.absolute_path), (text[:260] + "...") if len(text) > 260 else text)
    return schema


COVER_OVERLAP_PX = 4.0
# Confirmed false-positive passive covers (and deferred REAL covers needing bounds/Button->actions changes,
# tracked in docs/review/LABEL-OVERLAP-AUDIT-2026-11-28.md): (cover_source, cover_name) -> reason.
COVER_ALLOWLIST = {
    ("TUI.json:tab:Osc A", "Overlay Marker osca right"): "FALSE POSITIVE: 6x6 page-detector sentinel (shell-mpc/overlay/overlay_layout.h:27); oscDetailGrid slot 31 (col=7, row=3) is never populated (osc_spec_page.h:53,64).",
    ("TUI.json:tab:Osc B", "Overlay Marker oscb right"): "FALSE POSITIVE: 6x6 page-detector sentinel (shell-mpc/overlay/overlay_layout.h:28); oscDetailGrid slot 31 (col=7, row=3) is never populated (osc_spec_page.h:53,64).",
    ("TUI.json:tab:Osc C", "Overlay Marker oscc right"): "FALSE POSITIVE: 6x6 page-detector sentinel (shell-mpc/overlay/overlay_layout.h:29); oscDetailGrid slot 31 (col=7, row=3) is never populated (osc_spec_page.h:53,64).",
    ("TUI.json:tab:Sub", "Overlay Marker oscsub right"): "FALSE POSITIVE: 6x6 page-detector sentinel (shell-mpc/overlay/overlay_layout.h:30); oscDetailGrid slot 31 (col=7, row=3) is never populated (osc_spec_page.h:53,64).",
    ("TUI.json:tab:Noise", "Overlay Marker oscnoise right"): "FALSE POSITIVE: 6x6 page-detector sentinel (shell-mpc/overlay/overlay_layout.h:31); oscDetailGrid slot 31 (col=7, row=3) is never populated (osc_spec_page.h:53,64).",
    ("defs_fx.json:fxNativeGalleryCard", "name"): "DEFERRED REAL (LABEL-OVERLAP-AUDIT-2026-11-28.md): opaque card button fx_chain_gallery_card.png; needs Button->Image+actions refactor.",
    ("defs_lfo.json:lfoDetailRow", "name"): "DEFERRED REAL (LABEL-OVERLAP-AUDIT-2026-11-28.md): opaque button native_action_e4feb6764923e3caf30d.png; needs Button->Image+actions refactor.",
    ("defs_lfo.json:lfoEditAction", "name"): "DEFERRED REAL (LABEL-OVERLAP-AUDIT-2026-11-28.md): opaque button native_action_36f4ad4ed373ad966055.png; needs Button->Image+actions refactor.",
    ("defs_lfo.json:lfoScreenTop2", "Native LFO Point Mod Summary"): "DEFERRED REAL (LABEL-OVERLAP-AUDIT-2026-11-28.md): overlaps LFO 1 Native Detail SOURCE (636 24 220 56) while both are active (Parameter 3277==1); needs bounds shift (x>=864).",
}


def _parse_rect(text):
    return tuple(float(part) for part in text.split())


def _resolve_rect(parent_rect, bounds):
    px, py, pw, ph = parent_rect
    bx, by, bw, bh = _parse_rect(bounds["bounds"])
    if bounds.get("boundsType") == "Proportional":
        return (px + bx * pw, py + by * ph, bw * pw, bh * ph)
    return (px + bx, py + by, bw, bh)


def _fmt_rect(rect):
    return " ".join(str(int(round(v))) if abs(v - round(v)) < 1e-6 else f"{v:.2f}" for v in rect)


def _apply_increment(text, index):
    def replace(match):
        attrs = dict(part.split("=", 1) for part in match.group(1).split(","))
        base = int(attrs["Base"])
        step = int(attrs.get("Step", 1))
        offset = int(attrs.get("Offset", 0))
        return str(max(0, base + index * step + offset))
    return re.sub(r"\{Increment:([^}]+)\}", replace, text)


def _apply_remap(handle, remap_stack):
    resolved = handle
    for rmap in reversed(remap_stack):
        if not rmap:
            continue
        matched = False
        for key, value in rmap:
            if resolved == key:
                resolved = value
                matched = True
                break
        if matched:
            continue
        for key, value in rmap:
            if "*" in key:
                prefix, suffix = key.split("*", 1)
                if resolved.startswith(prefix) and resolved.endswith(suffix):
                    middle = resolved[len(prefix):len(resolved) - len(suffix) if suffix else None]
                    resolved = value.replace("*", middle)
                    matched = True
                    break
        if matched:
            continue
        if "/" in resolved:
            mod, base = resolved.rsplit("/", 1)
            for key, value in rmap:
                if base == key:
                    resolved = f"{mod}/{value}"
                    break
    return resolved


def find_label_covers(page, sources=None):
    """Flatten every tab in paint order and return passive Label/Image entries overlapping earlier touchables (> 4 px in both axes) without a unshared gate."""
    sources = sources or {}
    definitions = {d["key"]: d["value"] for d in page.get("componentDefinitions", {}).get("localComponentDefinitions", [])}
    covers = []

    for tab in page.get("tabs", []):
        tab_name = tab.get("tabName", "")
        initial = tab.get("initialSize")
        root_rect = _parse_rect(initial) if isinstance(initial, str) else (0.0, 0.0, 1280.0, 628.0)
        flat = []
        counter = [0]

        def walk_entries(entries, parent_rect, gates, remap_stack, source_loc, ancestor_ids, def_stack, repeat_idx=None):
            for entry in entries:
                cd = entry.get("componentData", {})
                ctype = cd.get("type", "")
                cname = cd.get("name", "")
                bounds = entry.get("bounds", {})
                if "bounds" not in bounds:
                    continue
                rect = _resolve_rect(parent_rect, bounds)
                entry_rmap = [(m["key"], m["value"]) for m in entry.get("handle remapping", {}).get("map", [])]
                if repeat_idx is not None:
                    entry_rmap = [(k, _apply_increment(v, repeat_idx)) for k, v in entry_rmap]
                new_remap_stack = remap_stack + [entry_rmap]
                entry_gates = []
                for gate in bounds.get("additionalInvalidatingHandles", []):
                    if repeat_idx is not None:
                        gate = _apply_increment(gate, repeat_idx)
                    entry_gates.append(_apply_remap(gate, new_remap_stack))
                all_gates = gates + entry_gates

                nid = counter[0]
                counter[0] += 1

                is_def = ctype in definitions
                if ctype in ("Button", "Knob", "Slider") and bounds.get("acceptsHWFocus") == "Yes":
                    flat.append((nid, "touchable", cname, ctype, rect, all_gates, source_loc, ancestor_ids))
                elif is_def and definitions[ctype].get("actions"):
                    flat.append((nid, "touchable", cname, ctype, rect, all_gates, source_loc, ancestor_ids))

                if ctype in ("Label", "Image"):
                    flat.append((nid, "passive", cname, ctype, rect, all_gates, source_loc, ancestor_ids))

                if is_def and ctype not in def_stack:
                    dval = definitions[ctype]
                    dloc = f"{sources.get(ctype, 'TUI.json')}:{ctype}"
                    reps = dval.get("repeats")
                    next_stack = def_stack | {ctype}
                    if isinstance(reps, int) and reps > 0:
                        for r in range(reps):
                            for child in dval.get("componentsData", []):
                                cb = child.get("bounds", {})
                                if "bounds" not in cb:
                                    continue
                                _, _, cw, _ = _parse_rect(cb["bounds"])
                                step_x = cw * rect[2] if cb.get("boundsType") == "Proportional" else cw
                                offset_rect = (rect[0] + r * step_x, rect[1], rect[2], rect[3])
                                walk_entries([child], offset_rect, all_gates, new_remap_stack, dloc, ancestor_ids | {nid}, next_stack, repeat_idx=r)
                    else:
                        walk_entries(dval.get("componentsData", []), rect, all_gates, new_remap_stack, dloc, ancestor_ids | {nid}, next_stack, repeat_idx=None)

        walk_entries(tab.get("componentDefinition", {}).get("componentsData", []), root_rect, [], [], f"TUI.json:tab:{tab_name}", set(), set())

        for idx, touchable in enumerate(flat):
            if touchable[1] != "touchable":
                continue
            t_nid, _, t_name, t_type, t_rect, t_gates, t_loc, _ = touchable
            tx, ty, tw, th = t_rect
            t_gate_set = set(t_gates)
            for passive in flat[idx + 1:]:
                if passive[1] != "passive":
                    continue
                _, _, c_name, c_type, c_rect, c_gates, c_loc, c_anc = passive
                if t_nid in c_anc:
                    continue
                cx, cy, cw, ch = c_rect
                if min(tx + tw, cx + cw) - max(tx, cx) <= COVER_OVERLAP_PX:
                    continue
                if min(ty + th, cy + ch) - max(ty, cy) <= COVER_OVERLAP_PX:
                    continue
                if any(gate not in t_gate_set for gate in c_gates):
                    continue
                covers.append({
                    "tab": tab_name,
                    "touchable_name": t_name,
                    "touchable_type": t_type,
                    "touchable_rect": _fmt_rect(t_rect),
                    "touchable_source": t_loc,
                    "cover_name": c_name,
                    "cover_type": c_type,
                    "cover_rect": _fmt_rect(c_rect),
                    "cover_source": c_loc,
                })
    return covers


def check_label_covers(page, findings, sources=None, *, as_error=False):
    report = findings.error if as_error else findings.warn
    for item in find_label_covers(page, sources=sources):
        if (item["cover_source"], item["cover_name"]) in COVER_ALLOWLIST:
            continue
        report(
            f"$.pageData.tabs[{item['tab']!r}]",
            f"passive {item['cover_type']} '{item['cover_name']}' [{item['cover_rect']}] ({item['cover_source']}) "
            f"overlaps earlier {item['touchable_type']} '{item['touchable_name']}' [{item['touchable_rect']}] ({item['touchable_source']}) without an unshared gate",
        )


def tui_semantics(page, schema, skins, findings, sources=None):
    """What a schema cannot say: cross references, files, value relations, unproven forms (warnings)."""
    proven = re.compile(schema["x-proven-handle-pattern"])
    anyform = re.compile(schema["x-unproven-handle-pattern"])
    proven_types = set(schema["x-proven-component-types"])
    unproven_types = set(schema["x-unproven-component-types"])
    defs_path = ["pageData", "componentDefinitions", "localComponentDefinitions"]
    definitions = page["componentDefinitions"]["localComponentDefinitions"]
    keys = [d["key"] for d in definitions]
    for index, key in enumerate(keys):
        if keys.count(key) > 1:
            findings.error(json_path([*defs_path, index, "key"]), f"duplicate component definition key '{key}'")
        if key in proven_types | unproven_types:
            findings.error(json_path([*defs_path, index, "key"]), f"definition key '{key}' shadows a built-in component type")
    local = set(keys)

    def check_handle(handle, path, owner):
        if not isinstance(handle, str):
            return
        if not proven.match(handle):
            if anyform.match(handle):
                form = re.sub(r"\d+", "N", handle)
                findings.warn(path, f"handle form not proven on a device (SDK-documented only): '{form}'")
            return
        for match in re.finditer(r"Local/(\d+)/(\w+)", handle):
            if match[2] not in PROVEN_LOCAL_IDS:
                findings.warn(path, f"Local handle id '{match[2]}' is not the proven one ({', '.join(sorted(PROVEN_LOCAL_IDS))}); a Local state shared by several tabs is only proven for that id")
        rest = handle
        while True:
            step = re.match(r"IndexedEnabling/(\d+)/(\d+)/(.*)$", rest)
            ranged = re.match(r"RangedEnabling/(\d+)/(\d+)/(\d+)/(.*)$", rest)
            if step:
                index, count, rest = int(step[1]), int(step[2]), step[3]
                if index >= count:
                    findings.error(path, f"IndexedEnabling index {index} is not below the step count {count}: '{handle}'")
            elif ranged:
                low, high, count, rest = int(ranged[1]), int(ranged[2]), int(ranged[3]), ranged[4]
                if not low <= high < count:
                    findings.error(path, f"RangedEnabling needs min <= max < steps: '{handle}'")
            else:
                break
            inner = re.fullmatch(r"Local/(\d+)/\w+", rest)
            if inner and int(inner[1]) != count:
                findings.error(path, f"step count {count} differs from the range of {rest} (SDK 7.4: numChoices must equal the steps): '{handle}'")

    def check_entries(entries, base, owner):
        for j, entry in enumerate(entries):
            where = [*base, "componentsData", j]
            data_node = entry.get("componentData", {})
            kind, name, data = data_node.get("type"), data_node.get("name"), data_node.get("data", {})
            tag = f"{owner} / {name}"
            if isinstance(kind, str) and kind not in proven_types and kind not in local:
                if kind in unproven_types:
                    findings.warn(json_path([*where, "componentData", "type"]), f"component type '{kind}' is SDK-documented but not proven on a device")
                else:
                    findings.error(json_path([*where, "componentData", "type"]), f"{tag}: type '{kind}' is neither a built-in nor a local component definition")
            if not isinstance(data, dict):
                continue
            for key, value in data.items():
                if key.endswith("Handle") or key == "handleName":
                    check_handle(value, json_path([*where, "componentData", "data", key]), tag)
                if key in IMAGE_KEYS and isinstance(value, str) and value and not (skins / value).is_file():
                    findings.error(json_path([*where, "componentData", "data", key]), f"{tag}: image file '{value}' does not exist")
                if kind == "Button" and key in PROVEN_BUTTON_MAX and isinstance(value, int) and value > PROVEN_BUTTON_MAX[key]:
                    findings.warn(json_path([*where, "componentData", "data", key]), f"Button {key} above {PROVEN_BUTTON_MAX[key]} (largest proven on a device)")
                if key == "actions" and isinstance(value, list):
                    for k, action in enumerate(value):
                        check_handle(action.get("handleName"), json_path([*where, "componentData", "data", "actions", k, "handleName"]), tag)
            for k, item in enumerate(entry.get("handle remapping", {}).get("map", [])):
                check_handle(item.get("value"), json_path([*where, "handle remapping", "map", k, "value"]), tag)
            bounds = entry.get("bounds", {})
            for k, handle in enumerate(bounds.get("additionalInvalidatingHandles", []) if isinstance(bounds.get("additionalInvalidatingHandles"), list) else []):
                check_handle(handle, json_path([*where, "bounds", "additionalInvalidatingHandles", k]), tag)

    def check_definition(node, base, owner):
        for k, action in enumerate(node.get("actions", [])):
            check_handle(action.get("handleName"), json_path([*base, "actions", k, "handleName"]), owner)
            if action.get("handler") == "Show Overlay" and action.get("additionalData") not in local:
                findings.error(json_path([*base, "actions", k, "additionalData"]), f"{owner}: Show Overlay names '{action.get('additionalData')}', which is not a local component definition")
        check_entries(node.get("componentsData", []), base, owner)

    for i, definition in enumerate(definitions):
        check_definition(definition["value"], [*defs_path, i, "value"], f"definition '{definition['key']}'")
    seen = {}
    for i, tab in enumerate(page["tabs"]):
        position = (tab.get("fnKeyIndex"), tab.get("fnKeySubIndex"))
        if position in seen:
            findings.error(json_path(["pageData", "tabs", i]), f"tab '{tab.get('tabName')}' uses the same function key and sub-tab {position} as tab {seen[position]}")
        seen[position] = i
        check_definition(tab["componentDefinition"], ["pageData", "tabs", i, "componentDefinition"], f"tab '{tab.get('tabName')}'")
    tab_by_name = {tab.get("tabName"): tab for tab in page["tabs"]}
    for family_keys, tab_names in SCREEN_FAMILIES.items():
        for family_key in family_keys:
            if family_key not in local:
                findings.error(json_path(defs_path), f"screen definition '{family_key}' is missing")
            elif next(d["value"] for d in definitions if d["key"] == family_key).get("ignoreMousePresses") is not True:
                # Key 61 and XL bisect 2026-11-28: a screen container that takes mouse presses swallows the touch before its children get it (no control on the screen reacts)
                findings.error(json_path(defs_path), f"screen definition '{family_key}' must set ignoreMousePresses true: the container would take the touch from every control inside it")
        for tab_name in tab_names:
            entries = (tab_by_name.get(tab_name) or {}).get("componentDefinition", {}).get("componentsData", [])
            used = [e["componentData"]["type"] for e in entries if e.get("componentData", {}).get("type") in family_keys]
            if used != list(family_keys):
                findings.error(json_path(["pageData", "tabs", tab_name]), f"tab '{tab_name}' must hold the instances {list(family_keys)} in this order, found {used}: the repeating screens share their definitions")
    def_by_key = {d["key"]: d["value"] for d in definitions}

    def has_interactive_descendant(def_val, seen):
        for child in def_val.get("componentsData", []):
            cd = child.get("componentData", {})
            child_type = cd.get("type")
            if child_type in ("Button", "Knob") and child.get("bounds", {}).get("acceptsHWFocus") == "Yes":
                return True
            if child_type in def_by_key and child_type not in seen:
                target = def_by_key[child_type]
                if target.get("actions") or has_interactive_descendant(target, seen | {child_type}):
                    return True
        return False

    for index, definition in enumerate(definitions):
        key, value = definition["key"], definition["value"]
        if key not in HEADER_FORMS:
            continue
        if value.get("ignoreMousePresses") is False and not value.get("actions") and has_interactive_descendant(value, {key}):
            findings.error(json_path([*defs_path, index, "value", "ignoreMousePresses"]),
                           f"header form '{key}' has ignoreMousePresses false with empty actions and interactive children; container swallows touches")
    for k, item in enumerate(page["replacementStrings"]):
        check_handle(item.get("key"), json_path(["pageData", "replacementStrings", k, "key"]), "replacementStrings")
    if len(page["tabs"]) > PROVEN_MAX_TABS:
        findings.warn("$.pageData.tabs", f"{len(page['tabs'])} tabs: the SDK note allows {SDK_MAX_TABS}, the proven skin runs {PROVEN_MAX_TABS} on both devices")
    check_label_covers(page, findings, sources=sources, as_error=True)


def check_skin_schema(skins, label="skin"):
    """Schema + semantic checks of TUI.json and Q-Links.json in `skins`. Returns Findings; prints every finding with its JSON path."""
    findings = Findings()
    sources = {}
    tui = load_skin_tui(skins / "TUI.json", findings, sources=sources)
    qlinks = load_skin_json(skins / "Q-Links.json", findings)
    tui_schema = schema_errors(TUI_SCHEMA, tui, findings)
    schema_errors(QLINKS_SCHEMA, qlinks, findings)
    try:
        tui_semantics(tui["pageData"], tui_schema, skins, findings, sources=sources)
    except (KeyError, TypeError, AttributeError):  # a shape error above broke an assumption; it is already listed
        if not findings.errors:
            raise
        findings.warn("$", "semantic checks stopped early: fix the schema errors first")
    findings.report(label)
    return findings


def validate_package(package, *, check_live=False):
    manifest = json.loads((package / "plugin.json").read_bytes())
    if (package.name, manifest["uid"], manifest["manufacturer"], manifest["name"]) != (
            "Safian - VST - SynthyBoy", "SfSB", "Safian", "SynthyBoy"):
        raise ValueError("package folder, vendor, instrument name or UID does not match SynthyBoy")
    if ET.parse(package / "version.xml").findtext("uiversion") != "0.1.0.0":
        raise ValueError("missing or unsupported MPC scripted UI version")
    skins = package / "Plugin Skins"
    for variant in skins.iterdir():
        if variant.name.casefold().startswith(("tui - ", "q-links - ")) and variant.suffix.casefold() == ".json":
            raise ValueError(f"hardware-specific file shadows the shared native skin: {variant.name}")

    findings = check_skin_schema(skins)
    if findings.errors:
        raise ValueError(f"{len(findings.errors)} schema/semantic errors in TUI.json / Q-Links.json (listed above, with JSON paths)")
    page = read_imports(skins / "TUI.json")["pageData"]
    qlinks = json.loads((skins / "Q-Links.json").read_bytes())
    check_parameter_handles(page)
    check_handle_list_shape(page)
    check_baseline(page)
    if check_live:
        check_display_bindings(page, qlinks)
    if (page["version"], page["componentDefinitions"]["version"], qlinks["version"],
            qlinks["Screen Mode Q-Links"]["version"]) != (1, 2, 4, 4):
        raise ValueError("unsupported native skin or Q-Link version")
    if page["info"] != {"version": 1, "type": "CompleteDescription"} or qlinks["info"] != page["info"]:
        raise ValueError("expected a complete native page description and Q-Link map")
    definitions = page["componentDefinitions"]
    keys = [item["key"] for item in definitions["localComponentDefinitions"]]
    local = set(keys)
    if len(local) != len(keys) or local & (SDK_TEMPLATES | BUILTINS):
        raise ValueError("duplicate, stock or shadowed component definition")
    tabs = page["tabs"]
    if [tab["tabName"] for tab in tabs] != list(SURFACES):
        raise ValueError("incorrect SynthyBoy page set or order")
    views = json.loads((ROOT / "shell-mpc/overlay/overlay_layout.json").read_bytes())["views"]
    view_names = {v["name"] for v in views}
    detected = set()
    for tab in tabs:
        name = tab["tabName"]
        components = [c["componentData"] for c in tab["componentDefinition"]["componentsData"]]
        types = {c["type"] for c in components}
        # a screen definition (lfoScreen, oscScreen ...) holds the shared items of the page: its types count for the tab
        stack = list(types)
        by_key = {d["key"]: d["value"] for d in definitions["localComponentDefinitions"]}
        while stack:
            definition = by_key.get(stack.pop())
            for child in (definition or {}).get("componentsData", []):
                child_type = child["componentData"]["type"]
                if child_type not in types:
                    types.add(child_type)
                    stack.append(child_type)
        images = {c["name"] for c in components if c["type"] == "Image" and c["data"].get("image")}
        found = {c["name"][len("Overlay Marker "):-len(" left")] for c in components
                 if c["name"].startswith("Overlay Marker ") and c["name"].endswith(" left")}
        if len(found) != 1 or not found <= view_names:
            raise ValueError(f"{name}: expected exactly one known detector view, found {sorted(found)}")
        view = next(iter(found))
        detected.add(view)
        markers = {f"Overlay Marker {view} left", f"Overlay Marker {view} right", f"Overlay Instance {view}"}
        if SURFACES[name] not in types or not markers <= images or tab["qlinkBoundsData"] != []:
            raise ValueError(f"{name}: missing custom surface, detector images or unexpected stock highlight")

    if detected != view_names:
        raise ValueError(f"detector views without a tab: {sorted(view_names - detected)}")

    positions = {(t["fnKeyIndex"] + 1, t["fnKeySubIndex"] + 1) for t in tabs}
    mappings = qlinks["Screen Mode Q-Links"]["map"]
    if len(mappings) != len(tabs) or {(m["Tab"], m["SubTab"]) for m in mappings} != positions:
        raise ValueError("screen Q-Link mapping does not match the native tabs")
    if any(m["Bank Direction"] != "Row" for m in mappings):
        raise ValueError("unexpected SynthyBoy Q-Link bank layout")
    knob_names = {f"Q-Link {i}" for i in range(1, 17)}
    for knobs in [m["Q-Links"] for m in mappings] + [qlinks["Program Mode Q-Links"]]:
        if set(knobs) != knob_names or not all(type(v) == int and v >= -1 for v in knobs.values()):
            raise ValueError("invalid native Q-Link assignments")
    assets = set()
    for value in walk(page):
        if isinstance(value, str) and value.lower().endswith(".png"):
            assets.add(value)
        if not isinstance(value, dict):
            continue
        if value.get("handler") == "Show Overlay" and value.get("additionalData") not in local:
            raise ValueError(f"unresolved popup: {value.get('additionalData')}")
        if "componentData" not in value:
            continue
        item = value["componentData"]
        kind, data = item["type"], item["data"]
        if kind not in BUILTINS and kind not in local:
            raise ValueError(f"{item['name']}: unresolved or stock control {kind}")
        if kind == "Knob" and (data.get("knobType") != "FilmStrip" or not data.get("filmStrip")
                               or data["filmStrip"] in {"knob_uni.png", "knob_bi.png"}):
            raise ValueError(f"{item['name']}: custom dial artwork required")
        if kind == "Slider":
            custom_track = data.get("sliderType") == "TrackAndThumb" and data.get("thumbImage")
            custom_bar = data.get("sliderType") == "ColourBar" and all(data.get(key) for key in
                                                                       ("backgroundColour", "outlineColour", "valueColour"))
            if not (custom_track or custom_bar):
                raise ValueError(f"{item['name']}: custom slider appearance required")
        if kind == "Focus" and data.get("outlineColour") == "ffd31145":
            raise ValueError(f"{item['name']}: SDK default focus artwork")
        if kind == "Button" and not (data.get("onImage") and data.get("offImage")):
            raise ValueError(f"{item['name']}: custom button artwork required")
    for name in sorted(assets):
        if Path(name).name != name:
            raise ValueError(f"expected local skin artwork: {name}")
        with Image.open(skins / name) as image:
            if image.format != "PNG":
                raise ValueError(f"invalid skin PNG: {name}")
            image.verify()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_dir", type=Path)
    parser.add_argument("--check-display-bindings", action="store_true")
    parser.add_argument("--schema-only", action="store_true",
                        help="only the JSON Schema and semantic checks of TUI.json / Q-Links.json; package_dir is a package or a folder holding both files")
    parser.add_argument("--list-covers", action="store_true",
                        help="list every passive Label/Image overlapping an earlier touchable control without an unshared gate")
    args = parser.parse_args()
    if args.list_covers:
        folder = args.package_dir / "Plugin Skins"
        skins = folder if folder.is_dir() else args.package_dir
        findings = Findings()
        sources = {}
        tui = load_skin_tui(skins / "TUI.json", findings, sources=sources)
        if findings.errors:
            findings.report("skin")
            return 1
        for item in find_label_covers(tui["pageData"], sources=sources):
            print(f"{item['tab']}\t{item['touchable_name']} [{item['touchable_rect']}]\t{item['cover_name']} [{item['cover_rect']}]\t{item['cover_source']}")
        return 0
    if args.schema_only:
        folder = args.package_dir / "Plugin Skins"
        findings = check_skin_schema(folder if folder.is_dir() else args.package_dir)
        print(f"skin schema: {len(findings.errors)} errors, {len(findings.warnings)} kinds of warnings")
        return 1 if findings.errors else 0
    try:
        validate_package(args.package_dir, check_live=args.check_display_bindings)
    except (OSError, json.JSONDecodeError, ValueError, KeyError, TypeError, AttributeError, ET.ParseError, SyntaxError) as exc:
        print(f"Invalid SynthyBoy skin: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
