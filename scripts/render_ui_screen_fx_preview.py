#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Render current generated FX TUI + matching real C++ FxUi scene.

Outputs synthetic offline fixtures (1280x628), never device screenshots.
No historical screenshot is copied, cropped or labelled VERIFIED.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image

from render_osc_screen_preview import OscSkinSketch, ensure_generated_skin, _find_core_build_dir
from screen_images import screen_path, save_screen, export_path, require_external_output

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "shell-mpc/overlay/tests/fx_render_fixture.cpp"
STATES = {
    "chorus": (1, 0, 0), "chorus-gen1": (1, 0, 0),
    "reverb-main": (9, 0, 0), "reverb-tone": (9, 1, 0), "reverb-mod": (9, 2, 0),
    "compressor-main": (2, 0, 0), "compressor-bands": (2, 1, 0), "compressor-upward": (2, 2, 0),
    "compressor-limit": (2, 0, 1),
    "distortion": (4, 0, 0), "distortion-drive": (4, 0, 12),   # the third value is the drive in dB, with the filter on
    # every other effect and every one of its panels (the third value: a variant of the fixture's demo values, fx_render_fixture.cpp)
    "delay-main": (3, 0, 0), "delay-output": (3, 1, 0), "delay-pingpong": (3, 0, 2), "delay-tap": (3, 0, 4),
    "eq-main": (5, 0, 0), "eq-tone": (5, 1, 0), "eq-mid2": (5, 2, 1),
    "filter-main": (6, 0, 0), "filter-tone": (6, 1, 0), "filter-formant-tone": (6, 1, 1),
    "flanger": (7, 0, 0), "phaser": (8, 0, 0), "hyperdim": (10, 0, 0), "utility": (11, 0, 0),
    "bode": (12, 0, 0), "convolve": (13, 0, 0),
    "compressor-main-gen1": (2, 0, 0), "delay-main-gen1": (3, 0, 0),   # the Key 61 look: the Q frames of the side layout's 2 x 2 blocks
}


CANONICAL = "distortion-drive"   # the state of docs/screens/fx.jpg: the Distortion page, side layout, drive up, filter on


class FxSkinSketch(OscSkinSketch):
    """Use the existing TUI renderer, resolving FX repetition and actual names."""
    def _caption_for_handle(self, local_handle, resolved, with_name):
        if with_name:
            return with_name
        if resolved in self._state["virtual"]:
            return self._state["virtual"][resolved].get("name", "")
        return super()._caption_for_handle(local_handle, resolved, with_name)

    def _resolve_handle(self, handle, remaps):
        name, seen = None, set()
        while handle and handle not in seen:
            seen.add(handle)
            if handle.startswith("WithName[") and "]/" in handle:
                prefix, handle = handle.split("]/", 1)
                name = prefix[len("WithName["):]
            elif handle.startswith("FromGesture/"):
                handle = handle[len("FromGesture/"):]
            elif handle in remaps:
                handle = remaps[handle]
            else:
                break
        return name, handle

    def _draw_entry(self, canvas, entry, origin, parent_size, remaps):
        model = entry["componentData"]
        definition = self._sketch.defs.get(model["type"])
        repeats = (definition or {}).get("repeats", 1)
        if repeats > 1:
            expanded = copy.deepcopy(definition)
            children = []
            for index in range(repeats):
                def substitute(value):
                    if isinstance(value, str):
                        return re.sub(r"\{Increment:Base=(\d+)\}", lambda m: str(int(m[1])+index), value)
                    if isinstance(value, list):
                        return [substitute(v) for v in value]
                    if isinstance(value, dict):
                        return {k: substitute(v) for k, v in value.items()}
                    return value
                for child in definition["componentsData"]:
                    child = substitute(copy.deepcopy(child))
                    x, y, w, h = map(int, child["bounds"]["bounds"].split())
                    child["bounds"]["bounds"] = f"{x+index*w} {y} {w} {h}"
                    children.append(child)
            expanded["componentsData"] = children
            expanded.pop("repeats")
            self._sketch.defs[model["type"]] = expanded
            try:
                return super()._draw_entry(canvas, entry, origin, parent_size, remaps)
            finally:
                self._sketch.defs[model["type"]] = definition
        if model["type"] == "Button" and model["data"].get("numButtonsInGroup", 1) > 1:
            merged = dict(remaps)
            for binding in entry.get("handle remapping", {}).get("map", []):
                merged[binding["key"]] = binding["value"]
            if not self._passes_gates(entry["bounds"].get("additionalInvalidatingHandles", []), merged):
                return
            data = model["data"]
            _, handle = self._resolve_handle(data["handleName"], merged)
            selected = round(self._norm_for_handle(handle)*(data["numButtonsInGroup"]-1)) == data["buttonId"]
            image = self._open_image(data["onImage"] if selected else data["offImage"])
            x, y, w, h = map(int, entry["bounds"]["bounds"].split())
            canvas.alpha_composite(image.resize((w, h), Image.Resampling.LANCZOS), (origin[0]+x, origin[1]+y))
            return
        return super()._draw_entry(canvas, entry, origin, parent_size, remaps)


def build_fixture(work, generation=2):
    build = _find_core_build_dir()
    binary = work / f"fx_render_fixture_gen{generation}"
    libraries = [build / "core/libsynthyboy_core.a", *sorted((build / "core").glob("*/*.a"))]
    command = ["c++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
               f"-DSYNTHYBOY_MPC_UI_GEN={generation}", "-I", str(ROOT), "-I", str(ROOT / "core/include"),
               "-I", str(ROOT / "core/filters-serum/include"), "-I", str(ROOT / "core/shapes/include"),
               str(FIXTURE), *map(str, libraries), "-lpthread", "-o", str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    return binary


def render_all(out_dir, work, states=None):
    out_dir = require_external_output(out_dir)
    states = states or STATES
    generations = {1 if name.endswith("-gen1") else 2 for name in states}
    binaries = {generation: build_fixture(work, generation) for generation in generations}
    skins = ensure_generated_skin(work)
    out_dir.mkdir(parents=True, exist_ok=True)
    metadata = {"synthetic_fixture": True, "device_capture": False, "states": {}}
    for name, args in states.items():
        raw, state = work / f"{name}.rgba", work / f"{name}.json"
        generation = 1 if name.endswith("-gen1") else 2
        subprocess.run([str(binaries[generation]), *map(str, args), str(raw), str(state)], check=True)
        values = json.loads(state.read_text())
        host = FxSkinSketch(skins, values).render_tab("FX").convert("RGBA")
        overlay = Image.frombytes("RGBA", (1280, 628), raw.read_bytes())
        output = out_dir / f"{name}.png"
        save_screen(Image.alpha_composite(host, overlay), output)
        state.replace(out_dir / f"{name}.json")
        metadata["states"][name] = {"kind": args[0], "panel": args[1], "limit": args[2], "generation": generation, "image_bytes": output.stat().st_size}
    metadata["tui_bytes"] = (skins / "TUI.json").stat().st_size
    metadata["png_bytes"] = sum(p.stat().st_size for p in skins.glob("*.png"))
    (out_dir / "manifest.json").write_text(json.dumps(metadata, indent=2)+"\n")
    return out_dir / f"{next(iter(states))}.png"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=screen_path("fx"),
                        help="Canonical FX image, or an explicit external export")
    parser.add_argument("--out-dir", type=Path,
                        help="Export all nine fixture variants and metadata OUTSIDE the repository")
    args = parser.parse_args()
    output = export_path(args.out)
    if args.out_dir is not None:
        require_external_output(args.out_dir)
    with tempfile.TemporaryDirectory(prefix="synthyboy-fx-preview-") as folder:
        work = Path(folder)
        variants = args.out_dir if args.out_dir is not None else work / "fixtures"
        chorus = render_all(variants, work, STATES if args.out_dir is not None else {CANONICAL: STATES[CANONICAL]})
        with Image.open(chorus if args.out_dir is None else (args.out_dir / f"{CANONICAL}.png")) as current:
            save_screen(current, output)
    print(f"Rendered actual TUI + C++ FX composite to {output}")
    if args.out_dir is not None:
        print(f"Exported {len(STATES)} diagnostic fixtures to {args.out_dir}")


if __name__ == "__main__":
    main()
