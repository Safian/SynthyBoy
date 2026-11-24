#!/usr/bin/env python3
# SynthyBoy - Authentic offline standalone LFO / RND screen renderer.
# Generates real TUI.json skins, runs the real C++ LfoUi / scene_cpu.h fixture
# (lfo_render_fixture), renders the real generated TUI tab through a Sketch adapter
# without hand-drawn fake controls or procedural fallback dials, and composites the
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
import shlex
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = REPO_ROOT / "scripts"
for p in (str(REPO_ROOT), str(SCRIPTS_DIR)):
    if p not in sys.path:
        sys.path.insert(0, p)

from components import style

PARAMS_JSON = REPO_ROOT / "docs" / "params.json"
PAGES_JSON = REPO_ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = REPO_ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
GEN_SCRIPT = REPO_ROOT / "scripts" / "gen-mpc-ui.py"
RUN_GEN_UI = REPO_ROOT / "scripts" / "run-gen-ui.py"
APPLY_SCRIPT = REPO_ROOT / "scripts" / "apply-mpc-skin-extras.py"
PREVIEW_BASE_SCRIPT = REPO_ROOT / "scripts" / "render-mpc-skin-preview.py"
FIXTURE_SRC = REPO_ROOT / "shell-mpc" / "overlay" / "tests" / "lfo_render_fixture.cpp"

OUT_DIR = REPO_ROOT / "docs" / "ui-components" / "komponensek"
UI_MAP_DIR = REPO_ROOT / "docs" / "ui-map"
CANONICAL_KOMP = screen_path('lfo')
CANONICAL_RND = screen_path('rnd')

DEFAULT_MPC_SDK = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk")
DEFAULT_BUILD_DIR = Path(f"/tmp/synthyboy-{os.getuid()}/lfo-preview")

STATE_LABELS = {
    "standard_hz": "SYNTHETIC FIXTURE: LFO 1 ACTIVE (HZ MODE · SINE WAVE · SMOOTH ON)",
    "standard_smooth_off": "SYNTHETIC FIXTURE: LFO 1 ACTIVE (SMOOTH OFF · INACTIVE DIAL)",
    "standard_bpm": "SYNTHETIC FIXTURE: LFO 1 ACTIVE (BPM SYNC · 1/4 DIVISION)",
    "standard_key": "SYNTHETIC FIXTURE: LFO 1 ACTIVE (KEY TRACKING)",
    "drawn_expanded": "SYNTHETIC FIXTURE: LFO 1 DRAWN (EXPANDED SHAPE EDITOR)",
    "rnd_hz": "SYNTHETIC FIXTURE: RND 1 ACTIVE (SAMPLE & HOLD · NO SMOOTH)",
    "rnd_bpm": "SYNTHETIC FIXTURE: RND 1 ACTIVE (BPM SYNC · NO SMOOTH)",
    "rnd_key": "SYNTHETIC FIXTURE: RND 1 ACTIVE (KEY FOLLOW · NO SMOOTH)",
}


def _load_sketch_module():
    spec = importlib.util.spec_from_file_location("render_mpc_skin_preview", PREVIEW_BASE_SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _find_mpc_sdk() -> Path:
    env = os.environ.get("MPC_SDK")
    if env and Path(env).is_dir():
        return Path(env)
    if DEFAULT_MPC_SDK.is_dir():
        return DEFAULT_MPC_SDK
    raise FileNotFoundError("MPC_SDK not found; set MPC_SDK environment variable.")


def _find_core_build_dir(explicit=None) -> Path:
    candidates = [Path(explicit)] if explicit else [REPO_ROOT / "build"]
    for candidate in candidates:
        cache = candidate / "CMakeCache.txt"
        if not cache.is_file():
            continue
        source = next((line.split("=", 1)[1] for line in cache.read_text().splitlines()
                       if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")), None)
        if source and Path(source).resolve() != REPO_ROOT.resolve():
            raise ValueError(f"Core build cache belongs to {source}, not this checkout")
        if (candidate / "core" / "libsynthyboy_core.a").is_file():
            return candidate.resolve()
    raise FileNotFoundError("Build synthyboy_core in this checkout and supply --build-dir <CMake cache directory>.")


def ensure_fixture_binary(work_dir: Path, ui_gen: int = 2, core_build_dir=None) -> Path:
    build_dir = _find_core_build_dir(core_build_dir)
    out_bin = work_dir / f"lfo_render_fixture_gen{ui_gen}"
    dependencies = [
        FIXTURE_SRC,
        *(REPO_ROOT / "shell-mpc").glob("*.h"),
        *(REPO_ROOT / "shell-mpc" / "overlay").rglob("*.h"),
        *(REPO_ROOT / "shell-mpc" / "ui").rglob("*.h"),
    ]
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
    cmd = [
        "c++", "-std=c++17", "-O1", "-Wall", "-Wextra",
        f"-DSYNTHYBOY_MPC_UI_GEN={ui_gen}", f"-DSYNTHYBOY_MPC_QFRAME_ENABLED={1 if ui_gen == 1 else 0}",
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
    command_file = out_bin.with_suffix(".command.txt")
    command_text = shlex.join(cmd)
    if (out_bin.is_file() and command_file.is_file() and command_file.read_text() == command_text
            and out_bin.stat().st_mtime >= max(path.stat().st_mtime for path in [*dependencies, *libs])):
        return out_bin
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"lfo_render_fixture compile failed:\n{res.stderr}")
    command_file.write_text(command_text)
    return out_bin


def ensure_generated_skin(work_dir: Path, mpc_sdk: Path | None = None) -> Path:
    skins_dir = work_dir / "skins"
    tui_path = skins_dir / "TUI.json"
    inputs = [PARAMS_JSON, LAYOUT_JSON, * (SCRIPTS_DIR / "mpc-skin-extras").glob("*.py"),
              * (SCRIPTS_DIR / "mpc-skin-extras").glob("*.json"),
              * (REPO_ROOT / "components").rglob("*.py")]
    if tui_path.is_file() and tui_path.stat().st_mtime >= max(p.stat().st_mtime for p in inputs):
        return skins_dir
    sdk = mpc_sdk or _find_mpc_sdk()
    ui_dir = work_dir / "ui"
    ui_dir.mkdir(parents=True, exist_ok=True)
    skins_dir.mkdir(parents=True, exist_ok=True)

    steps = [
        [
            sys.executable, str(GEN_SCRIPT),
            "--params", str(PARAMS_JSON),
            "--pages", str(PAGES_JSON),
            "--layout", str(LAYOUT_JSON),
            "--out", str(ui_dir),
        ],
        [
            sys.executable, str(RUN_GEN_UI),
            "--sdk", str(sdk),
            "--params", str(ui_dir / "params-hostsim.json"),
            "--ui", str(ui_dir / "ui.json"),
            "--out", str(skins_dir),
            "--quiet",
        ],
        [
            sys.executable, str(APPLY_SCRIPT),
            "--skins", str(skins_dir),
            "--params", str(PARAMS_JSON),
            "--meta", str(ui_dir / "skin-meta.json"),
        ],
    ]
    for cmd in steps:
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"Skin generation command failed ({cmd[1]}):\n{res.stderr}")
    return skins_dir


class LfoSkinSketch:
    """Adapter around Sketch that evaluates gates, remapped handles,
    FilmStrip knob frames (enforcing 1:1 square ratio), ActiveText buttons, and C++ values."""

    def __init__(self, skins_dir: Path, state_data: dict):
        mod = _load_sketch_module()
        self._base_mod = mod
        self._params_list = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        self._sketch = mod.Sketch(skins_dir, self._params_list)
        self._skins = Path(skins_dir)
        self._state = state_data
        self._replacements = {
            r["key"]: r["value"]
            for r in self._sketch.tui["pageData"].get("replacementStrings", [])
        }
        self._img_cache: dict[str, Image.Image] = {}

    def _open_image(self, filename: str) -> Image.Image | None:
        if not filename:
            return None
        if filename not in self._img_cache:
            path = self._skins / filename
            if not path.is_file():
                raise FileNotFoundError(f"Generated native skin references missing packaged asset: {filename}")
            self._img_cache[filename] = Image.open(path).convert("RGBA")
        return self._img_cache[filename]

    def _font(self, size: int, style_name: str = "SemiBold") -> ImageFont.ImageFont:
        return self._base_mod.font(style_name, max(8, int(round(size))))

    @staticmethod
    def _strip_prefix(handle: str) -> tuple[str | None, str]:
        if handle.startswith("WithName[") and "]/" in handle:
            head, rest = handle.split("]/", 1)
            return head[len("WithName["):], rest
        if handle.startswith("FromGesture/"):
            return None, handle[len("FromGesture/"):]
        return None, handle

    def _resolve_handle(self, handle: str | None, remaps: dict[str, str]) -> tuple[str | None, str | None]:
        if not handle:
            return None, None
        seen = set()
        with_name = None
        cur = handle
        while cur and cur not in seen:
            seen.add(cur)
            wn, stripped = self._strip_prefix(cur)
            if wn:
                with_name = wn
            if stripped in remaps:
                cur = remaps[stripped]
                continue
            cur = stripped
            break
        return with_name, cur

    def _norm_for_handle(self, resolved: str | None) -> float:
        if not resolved or not resolved.startswith("Parameter "):
            return 0.0
        if resolved in self._state.get("virtual", {}):
            return float(self._state["virtual"][resolved]["norm"])
        idx = resolved[len("Parameter "):]
        if idx in self._state.get("params", {}):
            return float(self._state["params"][idx]["norm"])
        return 0.0

    def _passes_gates(self, gates: list[str], remaps: dict[str, str]) -> bool:
        for gate in gates:
            parts = gate.split("/")
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
        if not resolved or not resolved.startswith("Parameter "):
            return local_handle
        item = self._state.get("virtual", {}).get(resolved)
        if item is not None:
            return item.get("caption", "")
        item = self._state.get("params", {}).get(resolved[len("Parameter "):], {})
        return item.get("key", "").split(".")[-1].upper()

    def _value_for_handle(self, local_handle: str, resolved: str | None) -> str:
        if not resolved or not resolved.startswith("Parameter "):
            return ""
        # Production fixture exports the actual selected engine parameter's
        # formatted value alongside each native field. Never invent empty values.
        item = self._state.get("virtual", {}).get(resolved)
        if item is not None:
            return item.get("display", "")
        return self._state.get("params", {}).get(resolved[len("Parameter "):], {}).get("value", "")

    def _active_colour(self, entry: dict, remaps: dict[str, str], default_argb: str) -> tuple[int, int, int, int]:
        cdata = entry["componentData"]
        handle = cdata.get("data", {}).get("handleName", "")
        _, resolved = self._resolve_handle(handle, remaps)
        active = False
        if resolved and resolved.startswith("Parameter "):
            active = self._norm_for_handle(resolved) >= 0.5
        if active and entry["componentData"]["type"] == "ActiveText":
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
            caption, resolved = self._resolve_handle(item["value"], remaps)
            merged[item["key"]] = ((f"WithName[{caption}]/" if caption else "") + resolved
                                   if resolved else item["value"])

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
                is_on = int(round(norm * (n_grp - 1))) == bid
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
            film_name = data.get("filmStrip", "")
            if not film_name or film_name == "knob_uni.png":
                raise RuntimeError(f"Forbidden factory knob asset: {film_name!r}")
            strip = self._open_image(film_name)
            if strip is not None:
                n_frames = max(1, int(data.get("numFrames", 127)))
                _, resolved = self._resolve_handle(data.get("handleName"), merged)
                norm = min(1.0, max(0.0, self._norm_for_handle(resolved)))
                frame_idx = min(n_frames - 1, max(0, int(round(norm * (n_frames - 1)))))
                fh = strip.height // n_frames
                fw = strip.width
                if fh > 0:
                    frame = strip.crop((0, frame_idx * fh, fw, (frame_idx + 1) * fh))
                    if w != h:
                        raise ValueError(f"Production FilmStrip child is not square: {film_name} {w}x{h}")
                    side = w
                    dx = x + (w - side) // 2
                    dy = y + (h - side) // 2
                    canvas.alpha_composite(frame.resize((side, side), Image.Resampling.LANCZOS), (dx, dy))
            return

        if ctype == "Knob":
            raise ValueError(f"LFO native knob is not the approved FilmStrip: {data}")
        if ctype == "Focus":
            return  # The synthetic fixture has no native host keyboard focus.
        if ctype in ("Label", "ActiveText"):
            ts = data.get("textStyle", {})
            font_info = ts.get("font", {})
            size = int(round(float(font_info.get("height", 14))))
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
            col = self._base_mod.argb(ts.get("colour", style.argb("text.white")))
            if ctype == "ActiveText":
                col = self._active_colour(entry, merged, ts.get("colour", style.argb("text.caption")))
            if col and txt:
                draw = ImageDraw.Draw(canvas)
                just = ts.get("justification", "horizontallyCentred verticallyCentred")
                anchor_x = "l" if "left" in just else ("r" if "right" in just else "m")
                tx = x + 4 if anchor_x == "l" else (x + w - 4 if anchor_x == "r" else x + w // 2)
                ty = y + h // 2
                draw.text((tx, ty), txt, fill=col, font=font, anchor=f"{anchor_x}m")
            return
        raise ValueError(f"Unresolved native LFO primitive or definition: {ctype}")

    def render_tab(self, tab_name: str = "LFO") -> Image.Image:
        tabs_by_name = {t.get("tabName"): t for t in self._sketch.tui["pageData"]["tabs"]}
        if tab_name not in tabs_by_name:
            raise KeyError(f"Tab '{tab_name}' not found in TUI tabs: {list(tabs_by_name.keys())}")
        tab = tabs_by_name[tab_name]["componentDefinition"]
        bg = tab.get("backgroundData", {}).get("focussed", {})
        bg_name = bg.get("image", "")
        im = self._open_image(bg_name)
        if im is not None:
            canvas = im.resize((1280, 628), Image.Resampling.LANCZOS).copy()
        else:
            canvas = Image.new("RGBA", (1280, 628), style.color("surface.panel"))
        for child in tab.get("componentsData", []):
            self._draw_entry(canvas, child, (0, 0), (1280, 628), {})
        return canvas


def compose_full_mpc_screen(tab_img: Image.Image, overlay_tab_img: Image.Image, label: str, *, ui_gen=2, knobs=16) -> Image.Image:
    """Compose the real generated 1280x628 host tab + real C++ 1280x628 overlay
    onto the 1280x800 standalone MPC frame using project style tokens only."""
    frame = Image.new("RGBA", (1280, 800), style.color("field.surface"))
    draw = ImageDraw.Draw(frame)

    # Top MPC host chrome bar (0..110)
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
    draw.text((20, 74), "OFFLINE COMPOSITE: GENERATED SKIN TUI + C++ LfoUi / scene_cpu.h (NOT A DEVICE CAPTURE)",
              fill=style.color("text.caption"), font=font_sub, anchor="lm")

    draw.text((20, 95), f"GEN {ui_gen} · {knobs} PHYSICAL Q-LINK ENCODERS", fill=style.color("text.muted"), font=font_sub, anchor="lm")

    # Bottom MPC tab navigation band (738..800)
    draw.rectangle((0, 738, 1280, 800), fill=style.color("surface.panel"))
    draw.line((0, 738, 1280, 738), fill=style.color("field.border"), width=1)
    tabs = ("Play", "Osc", "Filter", "Env", "LFO", "Mod", "Mixer", "FX")
    tab_w = 1280 // len(tabs)
    for idx, tname in enumerate(tabs):
        tx0 = idx * tab_w + 6
        tx1 = (idx + 1) * tab_w - 6
        active = tname == "LFO"
        fill = style.color("field.focus") if active else style.color("button.surface")
        outline = style.color("accent.cyan") if active else style.color("field.border")
        draw.rounded_rectangle((tx0, 748, tx1, 792), radius=style.size("radius.field"), fill=fill, outline=outline, width=1)
        draw.text(((tx0 + tx1) // 2, 770), tname,
                  fill=style.color("text.white") if active else style.color("text.caption"),
                  font=font_title, anchor="mm")

    # Exact 1280x628 composite: host tab underneath, C++ transparent-aperture overlay on top
    tab_comp = Image.alpha_composite(tab_img.convert("RGBA"), overlay_tab_img.convert("RGBA"))
    frame.alpha_composite(tab_comp, (0, 110))
    return frame


def render_lfo_breakdown(native: Image.Image, overlay: Image.Image, composite: Image.Image) -> Image.Image:
    """Generate annotated 3-layer architecture comparison (1280 x 2080)."""
    w, h = 1280, 628
    header_h = 44
    gap = 16
    total_h = (h + header_h + gap) * 3 + gap
    breakdown = Image.new("RGBA", (w, total_h), (8, 11, 20, 255))
    draw = ImageDraw.Draw(breakdown)

    try:
        f_title = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 18)
        f_sub = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 14)
    except Exception:
        f_title = ImageFont.load_default()
        f_sub = f_title

    layers = [
        ("1. HOST NATIVE SKIN LAYER (Akai OS framework · TUI.json)",
         "overlay_bg_lfo.png background + redesigned header + modern blue FilmStrip dials & segmented radio buttons",
         native),
        ("2. DYNAMIC OVERLAY LAYER (SynthyBoy internal engine · C++ scene_cpu.h)",
         "Real-time waveform, voice cursors and live phase line · x:8..1272, y:72..322 aperture",
         overlay),
        ("3. FULL CERTIFIED COMPOSITE SCREEN (Akai MPC hardware display surface)",
         "Redesigned LFO screen · 100% native components · Disjoint ActiveText Smooth switch · Zero visible Q-Link frames",
         composite),
    ]

    curr_y = gap
    for title, subtitle, img in layers:
        draw.rounded_rectangle([gap, curr_y, w - gap, curr_y + header_h - 4], radius=6, fill="#121829", outline="#253556", width=1)
        draw.text((gap + 16, curr_y + 8), title, font=f_title, fill="#90edc0" if "3." in title else "#72d1ec")
        draw.text((gap + 520, curr_y + 12), subtitle, font=f_sub, fill="#a0a8be")
        curr_y += header_h

        draw.rectangle([0, curr_y, w, curr_y + h], outline="#253556", width=1)
        breakdown.paste(img, (0, curr_y), img if img.mode == "RGBA" else None)
        curr_y += h + gap

    return breakdown


def render_state(state_name: str, out_path: Path, *, skins_dir: Path, fixture_bin: Path, work_dir: Path, full_frame: bool = False, knobs: int = 16) -> tuple[Image.Image, Image.Image, Image.Image]:
    overlay_png = work_dir / f"{state_name}_overlay.png"
    state_json = work_dir / f"{state_name}_state.json"
    res = subprocess.run(
        [str(fixture_bin), "--state", state_name, "--dump-png", str(overlay_png), "--dump-json", str(state_json), "--knobs", str(knobs)],
        capture_output=True, text=True,
    )
    if res.returncode != 0:
        raise RuntimeError(f"lfo_render_fixture failed for {state_name}:\n{res.stderr}")

    state_data = json.loads(state_json.read_text(encoding="utf-8"))
    sketch = LfoSkinSketch(skins_dir, state_data)
    host_tab = sketch.render_tab("LFO")
    with Image.open(overlay_png) as ov_in:
        overlay_tab = ov_in.convert("RGBA")

    composite = Image.alpha_composite(host_tab.convert("RGBA"), overlay_tab)

    if out_path:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        if full_frame:
            frame = compose_full_mpc_screen(host_tab, overlay_tab, STATE_LABELS.get(state_name, ""),
                                            ui_gen=state_data["uiGeneration"], knobs=state_data["knobs"])
            save_screen(frame, out_path)
        else:
            save_screen(composite, out_path)

    return host_tab, overlay_tab, composite


def render_all_previews(skins_dir: Path, fixture_bin: Path, work_dir: Path):
    for mode, name in (("standard_hz", "lfo"), ("rnd_hz", "rnd")):
        render_state(mode, screen_path(name), skins_dir=skins_dir, fixture_bin=fixture_bin,
                     work_dir=work_dir, full_frame=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Render authentic offline SynthyBoy LFO previews.")
    ap.add_argument("--build-dir", help="This checkout's CMake cache containing built core libraries")
    ap.add_argument("--ui-gen", type=int, choices=(1, 2), default=2)
    ap.add_argument("--knobs", type=int, choices=(4, 16), default=16)
    ap.add_argument("--mode", choices=[*STATE_LABELS.keys(), "all"], default="all")
    ap.add_argument("--out", help="Custom output PNG path when rendering a single mode")
    ap.add_argument("--work-dir", default=str(DEFAULT_BUILD_DIR))
    ap.add_argument("--skins-dir", help="Optional pre-generated skins directory")
    ap.add_argument("--full-frame", action="store_true", help="Render full 1280x800 MPC frame with chrome")
    args = ap.parse_args(argv)

    work_dir = Path(args.work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)
    skins_dir = Path(args.skins_dir) if args.skins_dir else ensure_generated_skin(work_dir)
    fixture_bin = ensure_fixture_binary(work_dir, args.ui_gen, args.build_dir)

    if args.mode not in ("standard_hz", "rnd_hz", "all") and (not args.out or Path(args.out).resolve().is_relative_to(REPO_ROOT)):
        ap.error("Alternative LFO states require an external --out path")
    if args.out:
        mode = "standard_hz" if args.mode == "all" else args.mode
        render_state(mode, Path(args.out), skins_dir=skins_dir, fixture_bin=fixture_bin, work_dir=work_dir, full_frame=args.full_frame, knobs=args.knobs)
        print(f"Rendered {mode} -> {args.out}")
        return 0

    if args.mode == "all":
        render_all_previews(skins_dir=skins_dir, fixture_bin=fixture_bin, work_dir=work_dir)
    else:
        dest = CANONICAL_RND if args.mode == "rnd_hz" else CANONICAL_KOMP
        render_state(args.mode, dest, skins_dir=skins_dir, fixture_bin=fixture_bin, work_dir=work_dir, full_frame=args.full_frame, knobs=args.knobs)
        print(f"Rendered {args.mode} -> {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
