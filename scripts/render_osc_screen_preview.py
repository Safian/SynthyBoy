#!/usr/bin/env python3
# SynthyBoy - Authentic offline standalone OSC A/B/C/Sub/Noise screen renderer.
# Generates real TUI.json skins, runs the real C++ OscPageUi/OscDisplay/scene_cpu.h
# fixture (osc_render_fixture), renders the real generated TUI tab through a
# Sketch adapter without altering render-mpc-skin-preview.py, and composites the
# C++ overlay layer identically to hardware display planes.
# NOTE: Renders documented synthetic fixtures, NOT a live MPC device capture.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import annotations
from screen_images import screen_path, save_screen, export_path

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = REPO_ROOT / "scripts"
for p in (str(REPO_ROOT), str(SCRIPTS_DIR)):
    if p not in sys.path:
        sys.path.insert(0, p)

from preview_skin import PARAMS_JSON, ensure_generated_skin
from native_tab_sketch import NativeTabSketch
from components import style
from native_ui_params import OSC_ABC_SETTINGS, OSC_ABC_SETTINGS_FIELDS, OSC_SETTINGS, OSC_SETTINGS_FIELDS

EXTRAS_SETTINGS_JSON = REPO_ROOT / "scripts" / "mpc-skin-extras" / "osc-native-settings.json"
PREVIEW_BASE_SCRIPT = REPO_ROOT / "scripts" / "render-mpc-skin-preview.py"
FIXTURE_SRC = REPO_ROOT / "shell-mpc" / "overlay" / "tests" / "osc_render_fixture.cpp"

DEFAULT_BUILD_DIR = Path(f"/tmp/synthyboy-{os.getuid()}/osc-preview")

STATES = {
    "osc_a_on": screen_path('osc_a'),
    "osc_a_off": screen_path('osc_a'),
    "osc_b_on": screen_path('osc_b'),
    "osc_c_on": screen_path('osc_c'),
    "sub_on": screen_path('sub'),
    "noise_gen_on": screen_path('noise'),
    "noise_sample_on": screen_path('noise'),
}

STATE_LABELS = {
    "osc_a_on": "SYNTHETIC FIXTURE: OSC A ACTIVE (WAVETABLE)",
    "osc_a_off": "SYNTHETIC FIXTURE: OSC A INACTIVE (OFF SHIELDING)",
    "osc_b_on": "SYNTHETIC FIXTURE: OSC B ACTIVE (OWN VALUES)",
    "osc_c_on": "SYNTHETIC FIXTURE: OSC C ACTIVE (OWN VALUES)",
    "sub_on": "SYNTHETIC FIXTURE: SUB ACTIVE (GENERATED)",
    "noise_gen_on": "SYNTHETIC FIXTURE: NOISE ACTIVE (GENERATED)",
    "noise_sample_on": "SYNTHETIC FIXTURE: NOISE ACTIVE (SAMPLE)",
}

TUNE_CAPTIONS = {
    "Caption_Oct": "OCT",
    "Caption_Sem": "SEM",
    "Caption_Fin": "FIN",
    "Caption_Crs": "CRS",
}


def _load_sketch_module():
    spec = importlib.util.spec_from_file_location("render_mpc_skin_preview", PREVIEW_BASE_SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _find_core_build_dir() -> Path:
    candidates = [
        Path(f"/tmp/synthyboy-{os.getuid()}/1993681166/osc-check"),
        Path(f"/tmp/synthyboy-{os.getuid()}/play-build"),
        REPO_ROOT / "build",
    ]
    for c in candidates:
        if (c / "core" / "libsynthyboy_core.a").is_file():
            return c
    raise FileNotFoundError(
        "Prebuilt core libraries not found cleanly; configure/build synthyboy_core first."
    )


def ensure_fixture_binary(work_dir: Path) -> Path:
    build_dir = _find_core_build_dir()
    out_bin = work_dir / "osc_render_fixture"
    dependencies = [FIXTURE_SRC, * (REPO_ROOT / "shell-mpc").glob("*.h"),
                    * (REPO_ROOT / "shell-mpc" / "overlay").rglob("*.h"),
                    * (REPO_ROOT / "shell-mpc" / "ui").rglob("*.h")]
    libs = [
        build_dir / "core" / "libsynthyboy_core.a",
        build_dir / "core" / "wavetable-edit" / "libsynthyboy_wtedit.a",
        build_dir / "core" / "sample-osc" / "libsynthyboy_sampleosc.a",
        build_dir / "core" / "granular" / "libsynthyboy_granular.a",
        build_dir / "core" / "fx-serum-b" / "libsynthyboy_fx_serum_b.a",
        build_dir / "core" / "filters-serum" / "libsynthyboy_filters_serum.a",
        build_dir / "core" / "shapes" / "libsynthyboy_shapes.a",
        build_dir / "core" / "spectral" / "libsynthyboy_spectral.a",
        build_dir / "core" / "multisample" / "libsynthyboy_multisample.a",
        build_dir / "core" / "fx-serum-a" / "libsynthyboy_fx_serum_a.a",
    ]
    if out_bin.is_file() and out_bin.stat().st_mtime >= max(path.stat().st_mtime for path in [*dependencies, *libs]):
        return out_bin
    cmd = [
        "c++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-I", str(REPO_ROOT),
        "-I", str(REPO_ROOT / "core" / "include"),
        "-I", str(REPO_ROOT / "core" / "filters-serum" / "include"),
        "-I", str(REPO_ROOT / "core" / "shapes" / "include"),
        "-I", str(REPO_ROOT / "shell-mpc" / "overlay"),
        "-I", str(REPO_ROOT / "shell-mpc" / "ui"),
        "-o", str(out_bin),
        str(FIXTURE_SRC),
        *(str(lib) for lib in libs),
        "-lpthread",
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"osc_render_fixture compile failed:\n{res.stderr}")
    return out_bin


def _clean_caption(raw_name: str, key: str) -> str:
    if key.endswith(".pan"):
        return "PAN"
    if key.endswith(".level"):
        return "LEVEL"
    if key.endswith(".tune"):
        return "FIN" if key.startswith("osc") else "CRS"
    if key.endswith(".pitch_coarse"):
        return "CRS"
    if key.endswith(".transpose"):
        return "SEM"
    stripped = re.sub(r"^(Osc\s+[ABC]\s+|Sub\s+|Noise\s+|OSC\s+[ABC]\s+|Unison\s+)", "", raw_name)
    return stripped.upper()[:14]


class OscSkinSketch(NativeTabSketch):
    """Adapter around render-mpc-skin-preview.Sketch that evaluates gates,
    remapped handles, FilmStrip knob frames, Toggle Indicators, and C++ values."""

    def __init__(self, skins_dir: Path, state_data: dict):
        mod = _load_sketch_module()
        self._base_mod = mod
        self._params_list = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        self._sketch = mod.Sketch(skins_dir, self._params_list)
        self._skins = Path(skins_dir)
        self._state = state_data
        self._settings_labels = self._build_settings_label_map()
        self._replacements = {
            r["key"]: r["value"]
            for r in self._sketch.tui["pageData"].get("replacementStrings", [])
        }
        self._img_cache: dict[str, Image.Image] = {}

    def _build_settings_label_map(self) -> dict[tuple[int, int], str]:
        contract = json.loads(EXTRAS_SETTINGS_JSON.read_text(encoding="utf-8"))
        labels = {}
        for var in contract:
            layout = var["layout"]
            for f in var["fields"]:
                labels[(layout, f["slot"])] = f["label"]
        return labels

    def _norm_for_handle(self, resolved: str | None) -> float:
        if not resolved or not resolved.startswith("Parameter "):
            return 0.0
        if resolved in self._state["virtual"]:
            return float(self._state["virtual"][resolved]["norm"])
        idx = resolved[len("Parameter "):]
        if idx in self._state["params"]:
            return float(self._state["params"][idx]["norm"])
        return 0.0

    def _passes_gates(self, gates: list[str], remaps: dict[str, str]) -> bool:
        for gate in gates:
            _, resolved = self._resolve_handle(gate, remaps)
            parts = (resolved or gate).split("/")
            if parts[0] == "NonZero" and len(parts) == 2:
                _, h = self._resolve_handle(parts[1], remaps)
                if self._norm_for_handle(h) <= 1e-5:
                    return False
            elif parts[0] == "IndexedEnabling" and len(parts) == 4:
                idx, total = int(parts[1]), int(parts[2])
                _, h = self._resolve_handle(parts[3], remaps)
                v = self._norm_for_handle(h)
                step = int(round(v * max(1, total - 1)))
                if step != idx:
                    return False
            elif parts[0] == "RangedEnabling" and len(parts) == 5:
                lo, hi, total = int(parts[1]), int(parts[2]), int(parts[3])
                _, h = self._resolve_handle(parts[4], remaps)
                v = self._norm_for_handle(h)
                step = int(round(v * max(1, total - 1)))
                if not (lo <= step <= hi):
                    return False
        return True

    def _caption_for_handle(self, local_handle: str, resolved: str | None, with_name: str | None) -> str:
        if with_name:
            return with_name
        if local_handle in TUNE_CAPTIONS:
            return TUNE_CAPTIONS[local_handle]
        if not resolved or not resolved.startswith("Parameter "):
            return local_handle
        if resolved in self._state["virtual"]:
            v_disp = self._state["virtual"][resolved]["display"]
            base = int(self._state["base"])
            p_num = int(resolved[len("Parameter "):])
            settings_start = base + OSC_SETTINGS
            abc_start = base + OSC_ABC_SETTINGS
            if settings_start <= p_num < settings_start + OSC_SETTINGS_FIELDS:
                slot = p_num - settings_start
                view = int(self._state["settings_view"])
                if (view, slot) in self._settings_labels:
                    return self._settings_labels[(view, slot)]
            if abc_start <= p_num < abc_start + 3 * OSC_ABC_SETTINGS_FIELDS:
                slot = (p_num - abc_start) % OSC_ABC_SETTINGS_FIELDS
                view = int(self._state["settings_view"])
                if (view, slot) in self._settings_labels:
                    return self._settings_labels[(view, slot)]
            return v_disp
        idx = resolved[len("Parameter "):]
        if idx in self._state["params"]:
            info = self._params_list[int(idx)] if int(idx) < len(self._params_list) else {}
            return _clean_caption(info.get("name", ""), self._state["params"][idx]["key"])
        return ""

    def _value_for_handle(self, local_handle: str, resolved: str | None) -> str:
        if local_handle == "Data_Sem" and "tune_sem_display" in self._state:
            return self._state["tune_sem_display"]
        if not resolved or not resolved.startswith("Parameter "):
            return ""
        if resolved in self._state["virtual"]:
            v_disp = self._state["virtual"][resolved]["display"]
            if v_disp:
                return v_disp
            if resolved in self._replacements:
                choices = self._replacements[resolved]
                norm = self._norm_for_handle(resolved)
                if choices:
                    return choices[min(len(choices) - 1, max(0, int(round(norm * (len(choices) - 1)))))]
            return ""
        idx = resolved[len("Parameter "):]
        if idx in self._state["params"]:
            return self._state["params"][idx]["display"]
        return ""

    def _active_colour(self, entry: dict, remaps: dict[str, str], default_argb: str) -> tuple[int, int, int, int]:
        cdata = entry["componentData"]
        handle = cdata.get("data", {}).get("handleName", "")
        _, resolved = self._resolve_handle(handle, remaps)
        active = False
        if resolved and resolved.startswith("Parameter "):
            active = self._norm_for_handle(resolved) >= 0.5
        if active and entry["componentData"]["type"] == "ActiveText":
            # ActiveText style when ON
            return self._base_mod.argb(style.argb("accent.cyan"))
        return self._base_mod.argb(default_argb)

    def _draw_entry(self, canvas: Image.Image, entry: dict, origin: tuple[int, int],
                    parent_size: tuple[int, int], remaps: dict[str, str]):
        bounds = entry.get("bounds", {})
        gates = bounds.get("additionalInvalidatingHandles", [])
        if gates and not self._passes_gates(gates, remaps):
            return

        vals = [float(x) for x in bounds.get("bounds", "0 0 0 0").split()]
        if len(vals) != 4:
            return
        if bounds.get("boundsType") == "Proportional":
            pw, ph = parent_size
            x = origin[0] + int(round(vals[0] * pw))
            y = origin[1] + int(round(vals[1] * ph))
            w = int(round(vals[2] * pw))
            h = int(round(vals[3] * ph))
        else:
            x = origin[0] + int(round(vals[0]))
            y = origin[1] + int(round(vals[1]))
            w = int(round(vals[2]))
            h = int(round(vals[3]))
        if w <= 0 or h <= 0:
            return

        merged = dict(remaps)
        for item in entry.get("handle remapping", {}).get("map", []):
            merged[item["key"]] = item["value"]

        comp = entry["componentData"]
        ctype = comp["type"]
        data = comp.get("data", {})

        # Definition lookup (local or shared)
        defn = self._sketch.defs.get(ctype)
        if defn is not None:
            bg = defn.get("backgroundData", {}).get("unfocussed", {})
            bg_img = self._open_image(bg.get("image", ""))
            if bg_img is not None:
                canvas.alpha_composite(bg_img.resize((w, h), Image.Resampling.LANCZOS), (x, y))
            else:
                bg_col = self._base_mod.argb(bg.get("colour", "0"))
                if bg_col and bg_col[3] > 0:
                    patch = Image.new("RGBA", (w, h), bg_col)
                    canvas.alpha_composite(patch, (x, y))
            for child in defn.get("componentsData", []):
                self._draw_entry(canvas, child, (x, y), (w, h), merged)
            return

        if ctype == "Image":
            im = self._open_image(data.get("image", ""))
            if im is not None:
                if data.get("imageType") == "FilmStrip":
                    count = int(data["numFrames"])
                    _, handle = self._resolve_handle(data.get("handleName"), merged)
                    index = min(count - 1, max(0, round(self._norm_for_handle(handle) * (count - 1))))
                    height = im.height // count
                    im = im.crop((0, index * height, im.width, (index + 1) * height))
                canvas.alpha_composite(im.resize((w, h), Image.Resampling.LANCZOS), (x, y))
            return

        if ctype == "Button":
            _, resolved = self._resolve_handle(data.get("handleName"), merged)
            norm = self._norm_for_handle(resolved)
            n_grp = max(1, int(data.get("numButtonsInGroup", 1)))
            bid = int(data.get("buttonId", 1))
            if n_grp == 1:
                is_on = norm >= 0.5
            else:
                is_on = round(norm * (n_grp - 1)) == bid
            img_name = data.get("onImage") if is_on else data.get("offImage")
            if not img_name:
                img_name = data.get("offImage") or data.get("onImage", "")
            im = self._open_image(img_name)
            if im is not None:
                canvas.alpha_composite(im.resize((w, h), Image.Resampling.LANCZOS), (x, y))
            return

        if ctype == "Indicator":
            _, resolved = self._resolve_handle(data.get("handleName"), merged)
            is_on = self._norm_for_handle(resolved) >= 0.5
            img_name = data.get("onImage") if is_on else data.get("offImage")
            im = self._open_image(img_name or "")
            if im is not None:
                canvas.alpha_composite(im.resize((w, h), Image.Resampling.LANCZOS), (x, y))
            return

        if ctype == "Knob" and data.get("knobType") == "FilmStrip":
            strip = self._open_image(data.get("filmStrip", ""))
            if strip is not None:
                n_frames = max(1, int(data.get("numFrames", 127)))
                _, resolved = self._resolve_handle(data.get("handleName"), merged)
                norm = min(1.0, max(0.0, self._norm_for_handle(resolved)))
                frame_idx = min(n_frames - 1, max(0, int(round(norm * (n_frames - 1)))))
                fh = strip.height // n_frames
                fw = strip.width
                if fh > 0:
                    frame = strip.crop((0, frame_idx * fh, fw, (frame_idx + 1) * fh))
                    side = min(w, h)
                    dx = x + (w - side) // 2
                    dy = y + (h - side) // 2
                    canvas.alpha_composite(frame.resize((side, side), Image.Resampling.LANCZOS), (dx, dy))
            return

        if ctype in ("Label", "ActiveText"):
            ts = data.get("textStyle", {})
            font_info = ts.get("font", {})
            size = int(round(float(font_info.get("height", 15))))
            font = self._font(size, font_info.get("style", "SemiBold"))
            ltype = data.get("type", "Text")
            local_h = data.get("handleName", "")
            with_name, resolved = self._resolve_handle(local_h, merged)
            if ctype == "ActiveText" or ltype == "Text":
                txt = data.get("text", "")
            elif ltype == "Name":
                txt = self._caption_for_handle(local_h, resolved, with_name)
            elif ltype == "Value":
                txt = self._value_for_handle(local_h, resolved)
            else:
                txt = data.get("text", "")
            col = self._base_mod.argb(ts.get("colour", style.argb("text.icon")))
            if ctype == "ActiveText":
                # ActiveText switches colour when active handle is non-zero
                col = self._active_colour(entry, merged, ts.get("colour", style.argb("text.caption")))
            if col and txt:
                draw = ImageDraw.Draw(canvas)
                just = ts.get("justification", "horizontallyCentred verticallyCentred")
                anchor_x = "l" if "left" in just else ("r" if "right" in just else "m")
                tx = x + 4 if anchor_x == "l" else (x + w - 4 if anchor_x == "r" else x + w // 2)
                ty = y + h // 2
                draw.text((tx, ty), txt, fill=col, font=font, anchor=f"{anchor_x}m")
            return


def compose_full_mpc_screen(tab_img: Image.Image, overlay_tab_img: Image.Image, label: str) -> Image.Image:
    """Compose the real generated 1280x628 host tab + real C++ 1280x628 overlay
    onto the 1280x800 standalone MPC frame using project style tokens only."""
    frame = Image.new("RGBA", (1280, 800), style.color("field.surface"))
    draw = ImageDraw.Draw(frame)

    # Top MPC host chrome bar (0..110), matching MPC OS header + plugin tab band cleanly
    draw.rectangle((0, 0, 1280, 110), fill=style.color("surface.panel"))
    draw.line((0, 109, 1280, 109), fill=style.color("field.border"), width=1)
    try:
        font_title = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 14)
        font_sub = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 13)
    except Exception:
        font_title = ImageFont.load_default()
        font_sub = font_title
    draw.text((20, 26), "SynthyBoy  |  MPC Standalone Overlay", fill=style.color("text.icon"), font=font_title, anchor="lm")
    draw.text((1260, 26), label, fill=style.color("text.muted"), font=font_sub, anchor="rm")
    draw.text((20, 74), "OFFLINE COMPOSITE: GENERATED SKIN TUI + C++ OscPageUi / scene_cpu.h (NOT A DEVICE CAPTURE)",
              fill=style.color("text.caption"), font=font_sub, anchor="lm")

    # Exact 1280x628 composite: host tab underneath, C++ transparent-aperture overlay on top
    tab_comp = Image.alpha_composite(tab_img.convert("RGBA"), overlay_tab_img.convert("RGBA"))
    frame.alpha_composite(tab_comp, (0, 110))
    return frame


def render_state(state_name: str, out_path: Path, *, skins_dir: Path, fixture_bin: Path, work_dir: Path, tab_only: bool = False) -> Path:
    overlay_png = work_dir / f"{state_name}_overlay.png"
    state_json = work_dir / f"{state_name}_state.json"
    res = subprocess.run(
        [str(fixture_bin), "--state", state_name, "--tab-only", "--out-overlay", str(overlay_png), "--out-state", str(state_json)],
        capture_output=True, text=True,
    )
    if res.returncode != 0:
        raise RuntimeError(f"osc_render_fixture failed for {state_name}:\n{res.stderr}")

    state_data = json.loads(state_json.read_text(encoding="utf-8"))
    if not state_data.get("synthetic_fixture") or state_data.get("device_test"):
        raise ValueError("Expected synthetic_fixture=true, device_test=false")

    sketch = OscSkinSketch(skins_dir, state_data)
    host_tab = sketch.render_tab(state_data["tab_name"])
    with Image.open(overlay_png) as ov_in:
        overlay_tab = ov_in.convert("RGBA")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    if tab_only:
        final = Image.alpha_composite(host_tab, overlay_tab).convert("RGB")
    else:
        final = compose_full_mpc_screen(host_tab, overlay_tab, STATE_LABELS[state_name]).convert("RGB")
    save_screen(final, out_path)
    return out_path


def render_page(state_name, output_path=None, *, work_dir=DEFAULT_BUILD_DIR):
    """Render one standalone OSC-family screen from its skin and overlay fixture."""
    work_dir = Path(work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)
    skins_dir = ensure_generated_skin(work_dir)
    fixture = ensure_fixture_binary(work_dir)
    return render_state(state_name, Path(output_path) if output_path is not None else STATES[state_name],
                        skins_dir=skins_dir, fixture_bin=fixture, work_dir=work_dir)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Render authentic offline SynthyBoy OSC A/B/C/Sub/Noise previews.")
    ap.add_argument("--state", choices=[*STATES.keys(), "all"], default="all")
    ap.add_argument("--out", help="Output PNG path when rendering a single --state")
    ap.add_argument("--work-dir", default=str(DEFAULT_BUILD_DIR))
    ap.add_argument("--skins-dir", help="Optional pre-generated skins directory")
    ap.add_argument("--tab-only", action="store_true", help="Write 1280x628 tab surface instead of 1280x800 frame")
    args = ap.parse_args(argv)

    work_dir = Path(args.work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)
    skins_dir = Path(args.skins_dir) if args.skins_dir else ensure_generated_skin(work_dir)
    fixture_bin = ensure_fixture_binary(work_dir)

    baseline = ("osc_a_on", "osc_b_on", "osc_c_on", "sub_on", "noise_gen_on")
    if args.state not in (*baseline, "all") and (not args.out or Path(args.out).resolve().is_relative_to(REPO_ROOT)):
        ap.error("Alternative OSC states require an external --out path")
    if args.state == "all" and args.out:
        ap.error("--out requires one explicit --state")
    targets = [(name, STATES[name]) for name in baseline] if args.state == "all" else [(args.state, Path(args.out) if args.out else STATES[args.state])]
    for name, dest in targets:
        written = render_state(name, dest, skins_dir=skins_dir, fixture_bin=fixture_bin, work_dir=work_dir, tab_only=args.tab_only)
        print(f"Rendered {name} -> {written}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
