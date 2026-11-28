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

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "scripts/skin-baseline/TUI.keys.json"
# One identifying control per physical tab. The shared OSC surface serves five tabs.
SURFACES = {
    "Play": "playOscHeaderSelector", "Macros": "macroCard", "Osc A": "oscNativeSurface",
    "Osc B": "oscNativeSurface", "Osc C": "oscNativeSurface", "Sub": "oscNativeSurface",
    "Noise": "oscNativeSurface", **dict.fromkeys(("Env 1", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6"), "envNativeFullFields"), **dict.fromkeys(("LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "LFO 9", "LFO 10", "RND 1", "RND 2", "RND 3", "RND 4"), "lfoNativeFullEditor"),
    "FX": "fxNativeEditorSurface", "Mix": "mixerStripSourceFrame", "Filter 1": "filterNativeEditorSurface", "Filter 2": "filterNativeEditorSurface", "Note": "filterNativeCurveSurface", "Velo": "filterNativeCurveSurface",
    "Mod": "modNativeRowNormal", "Master": "masterNativeOutputMeters", "Tuning": "masterNativeBlock_scale", "Voice": "voiceNativeStepLevel",
}
BUILTINS = {"Button", "Focus", "Image", "Knob", "Label", "MenuOverlay", "Meter", "NumericOverlay", "Slider"}
SDK_TEMPLATES = {"knobUni", "knobBi", "knob overlay", "menu overlay", "toggle", "faderUni", "faderBi",
                 "radio3", "radio4", "sliderWithFocus", "sliderWithFocusMenu"}


def walk(value):
    yield value
    if isinstance(value, dict):
        for child in value.values():
            yield from walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


def current_hashes(tui_path):
    """sha256 of the canonical JSON of every localComponentDefinitions entry, by key."""
    definitions = json.loads(Path(tui_path).read_bytes())["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    return {d["key"]: hashlib.sha256(json.dumps(d, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()
            for d in definitions}


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

    page = json.loads((skins / "TUI.json").read_bytes())["pageData"]
    qlinks = json.loads((skins / "Q-Links.json").read_bytes())
    check_parameter_handles(page)
    check_baseline(page)
    if check_live:
        check_display_bindings(page, qlinks)
    if (page["version"], page["componentDefinitions"]["version"], qlinks["version"],
            qlinks["Screen Mode Q-Links"]["version"]) != (1, 2, 4, 4):
        raise ValueError("unsupported native skin or Q-Link version")
    if page["info"] != {"version": 1, "type": "CompleteDescription"} or qlinks["info"] != page["info"]:
        raise ValueError("expected a complete native page description and Q-Link map")
    definitions = page["componentDefinitions"]
    if definitions["importFiles"]:
        raise ValueError("native components must be self-contained")
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
    args = parser.parse_args()
    try:
        validate_package(args.package_dir, check_live=args.check_display_bindings)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, ET.ParseError, SyntaxError) as exc:
        print(f"Invalid SynthyBoy skin: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
