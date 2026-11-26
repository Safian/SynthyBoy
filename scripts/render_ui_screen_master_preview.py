#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Render the generated Master TUI + the matching real C++ MasterUi scene (both sides).

Synthetic offline fixtures (1280 x 628), never device screenshots: the host layer is the
generated skin drawn by the preview sketch, the overlay layer the C++ scene (glide curve,
Q frames). The canonical image is docs/screens/master.jpg (MAIN, XL generation); the
TUNING & MPE side and the Key 61 generation go to an external --out-dir only.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

from render_osc_screen_preview import OscSkinSketch, ensure_generated_skin, _find_core_build_dir
from screen_images import screen_path, save_screen, require_external_output
import native_ui_params as n

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "shell-mpc/overlay/tests/master_render_fixture.cpp"
STATES = {"main-gen2": (0, 2), "tuning-gen2": (1, 2), "main-gen1": (0, 1), "tuning-gen1": (1, 1)}
CANONICAL = "main-gen2"
PAD_PRESSURE_OFFSET = 4 + 32   # PluginInstance::padPressureBase (native_master.master_controls.SHELL_OFFSETS)


class MasterSkinSketch(OscSkinSketch):
    def _resolve_handle(self, handle, remaps):
        caption, seen = None, set()
        while handle and handle not in seen:
            seen.add(handle)
            if handle.startswith("WithName[") and "]/" in handle:
                prefix, handle = handle.split("]/", 1)
                caption = prefix[len("WithName["):]
            if handle.startswith("FromGesture/"):
                handle = handle[len("FromGesture/"):]
                continue
            if handle in remaps:
                handle = remaps[handle]
                continue
            break
        return caption, handle


def virtual_state(param_count, side):
    """The plugin's native handles after the engine parameters, as the page publishes them."""
    base = n.mixer_peek_index(param_count)
    rows = {
        base + n.MASTER_VIEW: ((side + 1) / 2, "MAIN" if side == 0 else "TUNING"),
        base + n.MASTER_PEAK_L: (0.78, ""), base + n.MASTER_PEAK_R: (0.70, ""),
        base + n.MASTER_HOLD_L: (0.86, ""), base + n.MASTER_HOLD_R: (0.80, ""),
        base + n.MASTER_VOICE_RATIO: (4 / 16, ""), base + n.MASTER_VOICE_COUNT: (4 / 32, "4 / 16 VOICES"),
        base + n.MASTER_LIMITER: (1.0, "On"), base + n.MASTER_CLIP: (0.0, ""),
        base + n.MASTER_SCALE_INPUT: (0.0, ""),
        param_count + PAD_PRESSURE_OFFSET: (1.0, "On"), param_count + PAD_PRESSURE_OFFSET + 1: (0.35, "35"),
    }
    return {f"Parameter {index}": {"norm": norm, "display": text} for index, (norm, text) in rows.items()}


def build_fixture(work, generation):
    build = Path(os.environ["MASTER_CORE_BUILD"]) if os.environ.get("MASTER_CORE_BUILD") else _find_core_build_dir()
    binary = work / f"master-fixture-{generation}"
    libraries = [build / "core/libsynthyboy_core.a", *sorted((build / "core").glob("*/*.a"))]
    command = ["c++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Wpedantic", "-Werror", f"-DSYNTHYBOY_MPC_UI_GEN={generation}",
               "-I", str(ROOT), "-I", str(ROOT / "core/include"), "-I", str(ROOT / "core/filters-serum/include"),
               "-I", str(ROOT / "core/shapes/include"), str(FIXTURE), *map(str, libraries),
               "-lpthread", "-o", str(binary)]
    subprocess.run(command, check=True)
    return binary


def render_all(out, work, states=STATES):
    skins = ensure_generated_skin(work)
    require_external_output(out)
    out.mkdir(parents=True, exist_ok=True)
    binaries = {generation: build_fixture(work, generation) for generation in {g for _, g in states.values()}}
    manifest = {"synthetic_fixture": True, "device_capture": False, "states": {}}
    for name, (side, generation) in states.items():
        raw, state_file = work / f"{name}.rgba", out / f"{name}.json"
        subprocess.run([str(binaries[generation]), str(side), str(raw), str(state_file)], check=True)
        state = json.loads(state_file.read_text())
        state.update(virtual=virtual_state(state["param_count"], side), base=n.mixer_peek_index(state["param_count"]),
                     settings_view=0)
        host = MasterSkinSketch(skins, state).render_tab("Master" if side == 0 else "Tuning").convert("RGBA")
        overlay = Image.frombytes("RGBA", (1280, 628), raw.read_bytes())
        Image.alpha_composite(host, overlay).convert("RGB").save(out / f"{name}.png", optimize=True)
        if name == CANONICAL:
            host.save(out / "native-host.png", optimize=True)
            overlay.save(out / "live-overlay.png", optimize=True)
        manifest["states"][name] = {"side": side, "generation": generation}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return out / f"{CANONICAL}.png"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, help="Export every state OUTSIDE the repository (no canonical image is written)")
    parser.add_argument("--work-dir", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="master-render-") as folder:
        work = args.work_dir or Path(folder)
        work.mkdir(parents=True, exist_ok=True)
        out = args.out_dir if args.out_dir is not None else Path(folder) / "states"
        require_external_output(out)
        image = render_all(out, work, STATES if args.out_dir is not None else {CANONICAL: STATES[CANONICAL]})
        if args.out_dir is None:
            with Image.open(image) as frame:
                save_screen(frame, screen_path("master"))
    print("Master screenshot:", args.out_dir if args.out_dir is not None else screen_path("master"))


if __name__ == "__main__":
    main()
