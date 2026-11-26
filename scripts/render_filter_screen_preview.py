#!/usr/bin/env python3
# SynthyBoy - Authentic offline standalone Filter screen preview renderer.
# Generates the production TUI.json and component assets, runs FilterUi /
# scene_cpu.h, and composites those two offline layers. Host label/font/widget
# rasterisation is interpreted here; this is not pixel-identity proof for MPC.
# NOTE: Renders documented synthetic fixtures for offline verification, NOT a live MPC device capture.
# Output: docs/screens/filter.jpg (canonical) or external --out path.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import annotations

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

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
SCRIPTS_DIR = HERE
for p in (str(REPO_ROOT), str(SCRIPTS_DIR)):
    if p not in sys.path:
        sys.path.insert(0, p)

from preview_skin import PARAMS_JSON, ensure_generated_skin
from native_tab_sketch import NativeTabSketch
from components import style
from screen_images import export_path, require_external_output, save_screen, screen_path

PREVIEW_BASE_SCRIPT = REPO_ROOT / "scripts" / "render-mpc-skin-preview.py"
FIXTURE_SRC = REPO_ROOT / "shell-mpc" / "overlay" / "tests" / "filter_render_fixture.cpp"

DEFAULT_BUILD_DIR = REPO_ROOT / "build"
DEFAULT_WORK_DIR = Path(f"/tmp/synthyboy-{os.getuid()}/filter-preview")
DEFAULT_OUT = screen_path("filter")
DEFAULT_COMPARE = DEFAULT_WORK_DIR / "filter-current-before-after.png"
DEFAULT_STATE_JSON = DEFAULT_WORK_DIR / "filter-current-fixture-state.json"
HISTORICAL_BEFORE = screen_path("filter").with_name("filter_old.jpg")


def _load_sketch_module():
    spec = importlib.util.spec_from_file_location("render_mpc_skin_preview", PREVIEW_BASE_SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


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


def ensure_fixture_binary(work_dir: Path, ui_gen: int = 2, qframe_enabled: int = 0, core_build_dir=None) -> Path:
    build_dir = _find_core_build_dir(core_build_dir)
    out_bin = work_dir / f"filter_render_fixture_gen{ui_gen}"
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
    for lib in libs:
        if not lib.is_file():
            raise FileNotFoundError(f"Missing required core static library: {lib}")

    cmd = [
        "c++", "-std=c++17", "-O1", "-Wall", "-Wextra",
        f"-DSYNTHYBOY_MPC_UI_GEN={ui_gen}",
        f"-DSYNTHYBOY_MPC_QFRAME_ENABLED={qframe_enabled}",
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
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"filter_render_fixture compile failed:\n{res.stderr}")
    command_file.write_text(command_text)
    return out_bin


class FilterSkinSketch(NativeTabSketch):
    """Authentic native host skin sketch interpreter for the Filter tab.
    Evaluates real gates, handle remappings, defs, images, filmstrips, buttons,
    shared FilmStrip dials, choice definitions, and labels."""

    def __init__(self, skins_dir: Path, state_data: dict):
        mod = _load_sketch_module()
        self._base_mod = mod
        self._params_list = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        self._sketch = mod.Sketch(skins_dir, self._params_list)
        self._skins = Path(skins_dir)
        self._state = state_data
        self._img_cache: dict[str, Image.Image] = {}

    def _open_image(self, filename: str) -> Image.Image | None:
        if not filename:
            return None
        if filename not in self._img_cache:
            path = self._skins / filename
            if not path.is_file():
                raise FileNotFoundError(f"Missing referenced skin asset: {path}")
            self._img_cache[filename] = Image.open(path).convert("RGBA")
        return self._img_cache[filename]

    @staticmethod
    def _strip_prefix(handle: str) -> tuple[str | None, str]:
        if handle.startswith("WithName[") and "]/ " in handle:
            head, rest = handle.split("]/ ", 1)
            return head[len("WithName["):], rest
        if handle.startswith("WithName[") and "]/" in handle:
            head, rest = handle.split("]/", 1)
            return head[len("WithName["):], rest
        if handle.startswith("FromGesture/"):
            return None, handle[len("FromGesture/"):]
        return None, handle

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
        if item is not None and "caption" in item:
            return item.get("caption", "")
        item = self._state.get("params", {}).get(resolved[len("Parameter "):], {})
        return item.get("key", "").split(".")[-1].upper()

    def _value_for_handle(self, local_handle: str, resolved: str | None) -> str:
        if not resolved or not resolved.startswith("Parameter "):
            return ""
        item = self._state.get("virtual", {}).get(resolved)
        if item is not None and "display" in item:
            return item.get("display", "")
        return self._state.get("params", {}).get(resolved[len("Parameter "):], {}).get("value", "")

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
            img_name = data.get("image", "")
            if data.get("imageType") == "FilmStrip":
                strip = self._open_image(img_name)
                if strip is not None:
                    n_frames = max(1, int(data.get("numFrames", 1)))
                    _, resolved = self._resolve_handle(data.get("handleName"), merged)
                    norm = min(1.0, max(0.0, self._norm_for_handle(resolved)))
                    frame_idx = min(n_frames - 1, max(0, int(round(norm * (n_frames - 1)))))
                    fh = strip.height // n_frames
                    fw = strip.width
                    if fh > 0:
                        frame = strip.crop((0, frame_idx * fh, fw, (frame_idx + 1) * fh))
                        canvas.alpha_composite(frame.resize((w, h), Image.Resampling.LANCZOS), (x, y))
                return
            im = self._open_image(img_name)
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

        if ctype == "Knob":
            knob_type = data.get("knobType", "")
            _, resolved = self._resolve_handle(data.get("handleName"), merged)
            norm = min(1.0, max(0.0, self._norm_for_handle(resolved)))
            if knob_type != "FilmStrip":
                raise RuntimeError(f"Unsupported non-FilmStrip Knob type '{knob_type}' for '{comp.get('name')}': fake PIL dial fallback is forbidden")
            film_name = data.get("filmStrip", "")
            strip = self._open_image(film_name)
            if strip is None:
                raise FileNotFoundError(f"Missing Knob FilmStrip asset '{film_name}' for '{comp.get('name')}'")
            n_frames = max(1, int(data.get("numFrames", 127)))
            frame_idx = min(n_frames - 1, max(0, int(round(norm * (n_frames - 1)))))
            fh = strip.height // n_frames
            fw = strip.width
            if fh <= 0:
                raise RuntimeError(f"Invalid FilmStrip dimensions {strip.size} for {n_frames} frames in '{film_name}'")
            frame = strip.crop((0, frame_idx * fh, fw, (frame_idx + 1) * fh))
            canvas.alpha_composite(frame.resize((w, h), Image.Resampling.LANCZOS), (x, y))
            return

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
            if col and txt:
                draw = ImageDraw.Draw(canvas)
                just = ts.get("justification", "horizontallyCentred verticallyCentred")
                anchor_x = "l" if "left" in just else ("r" if "right" in just else "m")
                tx = x + 4 if anchor_x == "l" else (x + w - 4 if anchor_x == "r" else x + w // 2)
                ty = y + h // 2
                draw.text((tx, ty), txt, fill=col, font=font, anchor=f"{anchor_x}m")
            return

        if ctype == "Slider":
            draw = ImageDraw.Draw(canvas)
            _, resolved = self._resolve_handle(data.get("handleName"), merged)
            norm = min(1.0, max(0.0, self._norm_for_handle(resolved)))
            bg_col = self._base_mod.argb(data.get("backgroundColour", "ff181b2c"))
            val_col = self._base_mod.argb(data.get("valueColour", "ff426f89"))
            out_col = self._base_mod.argb(data.get("outlineColour", "ff575a72"))
            draw.rounded_rectangle([(x, y), (x + w, y + h)], radius=4, fill=bg_col, outline=out_col, width=1)
            bar_w = int(round(w * norm))
            if bar_w > 0:
                draw.rounded_rectangle([(x, y), (x + bar_w, y + h)], radius=4, fill=val_col)
            return

    def render_tab(self, tab_name: str = "Filter 1") -> Image.Image:
        return super().render_tab(tab_name)


def get_commit_hash() -> str:
    try:
        res = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                             cwd=str(REPO_ROOT), capture_output=True, text=True)
        if res.returncode == 0:
            return res.stdout.strip()
    except Exception:
        pass
    return "unknown"


def compose_full_mpc_screen(tab_img: Image.Image, overlay_tab_img: Image.Image, label: str,
                            *, ui_gen=2, knobs=16, commit_hash=None) -> Image.Image:
    """Compose the real generated 1280x628 host tab + real C++ 1280x628 overlay
    onto the 1280x800 standalone MPC frame with clear outside provenance annotations."""
    frame = Image.new("RGBA", (1280, 800), style.color("field.surface"))
    draw = ImageDraw.Draw(frame)

    commit = commit_hash or get_commit_hash()

    # Top MPC host chrome bar (0..110)
    draw.rectangle((0, 0, 1280, 110), fill=style.color("surface.panel"))
    draw.line((0, 109, 1280, 109), fill=style.color("field.border"), width=1)
    try:
        font_title = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 14)
        font_sub = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 12)
        font_badge = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 11)
    except Exception:
        font_title = font_sub = font_badge = ImageFont.load_default()

    draw.text((20, 24), "SynthyBoy  |  Standalone Filter (F7) [TUI + C++ Overlay Kompozit]", fill=style.color("text.icon"), font=font_title, anchor="lm")
    draw.text((1260, 24), label, fill=style.color("text.muted"), font=font_sub, anchor="rm")
    draw.text((20, 62), "OFFLINE SYNTHETIC FIXTURE · GENERÁLT TUI.JSON SKIN + C++ FilterUi / scene_cpu.h (NEM HARDWARE CAPTURE)",
              fill=style.color("text.caption"), font=font_sub, anchor="lm")
    draw.text((20, 85), f"COMMIT: {commit} · GEN {ui_gen} · Q-FRAME: {0 if ui_gen==2 else 1} · {knobs} FIZIKAI Q-LINK ENKÓDER (XL SLOTS)",
              fill=style.color("accent.cyan"), font=font_sub, anchor="lm")

    # Bottom MPC tab navigation band (738..800)
    draw.rectangle((0, 738, 1280, 800), fill=style.color("surface.panel"))
    draw.line((0, 738, 1280, 738), fill=style.color("field.border"), width=1)
    tabs = ("Play", "Osc", "Filter", "Env", "LFO", "Mod", "Mixer", "FX")
    tab_w = 1280 // len(tabs)
    for idx, tname in enumerate(tabs):
        tx0 = idx * tab_w + 6
        tx1 = (idx + 1) * tab_w - 6
        active = tname == "Filter"
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


def render_before_after_comparison(current_full_img: Image.Image, out_path: Path, commit_hash: str,
                                 historical_after: Path | None = None) -> Path:
    """Create a fair, honest comparison image:
    1. Historical Before (develop 7147266b, fixed docs/screens/filter_old.jpg): legacy 24-control layout with separate F1/F2 tabs and OUTPUT popup.
    2. Optional explicit historical reference image if passed via --historical-after.
    3. Today's Real TUI + C++ Overlay Fixture (commit {commit_hash}): actual generated TUI skin with FilmStrip/ActiveText/Radio/NameValue/Button assets and C++ overlay."""

    if not HISTORICAL_BEFORE.is_file():
        raise FileNotFoundError(f"Missing historical before image: {HISTORICAL_BEFORE}")

    img_before = Image.open(HISTORICAL_BEFORE).convert("RGBA")
    img_current = current_full_img.convert("RGBA")

    panels = [
        (
            img_before,
            "1. TÖRTÉNETI BEFORE (docs/screens/filter_old.jpg · 2026-11-21 előtti kiinduló állapot)",
            "Legacy 2-tab Filter 1 / Filter 2 architektúra · 24 vezérlő az oldalon (külön OUTPUT felugró 5 paraméterrel)",
            (255, 170, 80, 255),
            "FIXED HISTORICAL BEFORE"
        ),
    ]
    if historical_after is not None:
        if not historical_after.is_file():
            raise FileNotFoundError(f"Missing explicit historical after image: {historical_after}")
        panels.append((
            Image.open(historical_after).convert("RGBA"),
            "2. EXPLICIT TÖRTÉNETI REFERENCIA",
            f"Külső megadott referencia: {historical_after.name}",
            (120, 200, 255, 255),
            "EXPLICIT HISTORICAL REF"
        ))
    panels.append((
        img_current,
        f"{len(panels) + 1}. MAI VALÓDI FEJLESZTÉSI ÁLLAPOT (2026-11-24, commit {commit_hash}) — GENERÁLT TUI + C++ OVERLAY",
        "60/40 Split + valódi FilmStrip/ActiveText/Radio/NameValue/Button komponens-assetek · Gen2 / Q-frame 0 · C++ FilterUi + ScalarCurveEditor overlay",
        (80, 240, 160, 255),
        "MAI TÉNYLEGES KOMPOZIT (OFFLINE FIXTURE)"
    ))

    panel_w = 1280
    panel_h = 800
    header_h = 75
    banner_h = 100

    total_w = panel_w
    total_h = banner_h + len(panels) * (panel_h + header_h) + 30

    comp = Image.new("RGBA", (total_w, total_h), (11, 14, 24, 255))
    draw = ImageDraw.Draw(comp)

    try:
        font_main = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 20)
        font_h1 = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
        font_sub = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 12)
        font_tag = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 12)
    except Exception:
        font_main = font_h1 = font_sub = font_tag = ImageFont.load_default()

    # Top main header banner
    draw.rectangle([(0, 0), (total_w, banner_h)], fill=(18, 22, 38, 255))
    draw.line([(0, banner_h), (total_w, banner_h)], fill=(45, 55, 85, 255), width=2)
    draw.text((20, 20), "SYNTHYBOY — FILTER KÉPERNYŐ: TÖRTÉNETI ÉS MAI ÁLLAPOT ÖSSZEVETÉSE",
              fill=(245, 248, 255, 255), font=font_main)
    draw.text((20, 50),
              "Init F1 paraméterek: Digital (model 3), Style A (12 dB), Cutoff: 12.0 kHz, Reso: 20%, Drive: 0.0 dB, Mix: 100%, Routing: Parallel",
              fill=(160, 185, 230, 255), font=font_sub)
    draw.text((20, 72),
              f"Aktuális checkout commit: {commit_hash} · Valós generált TUI.json + C++ overlay kompozit (offline szintetikus fixture)",
              fill=(76, 210, 255, 255), font=font_sub)

    cur_y = banner_h + 10
    for idx, (p_img, p_title, p_desc, p_col, p_badge) in enumerate(panels):
        # Header strip for panel
        draw.rectangle([(0, cur_y), (total_w, cur_y + header_h)], fill=(22, 26, 42, 255))
        draw.line([(0, cur_y + header_h), (total_w, cur_y + header_h)], fill=(50, 60, 90, 255), width=1)

        draw.text((20, cur_y + 14), p_title, fill=p_col, font=font_h1)
        draw.text((20, cur_y + 42), p_desc, fill=(180, 190, 215, 255), font=font_sub)

        bw = int(draw.textlength(p_badge, font=font_tag)) + 20
        draw.rounded_rectangle([(total_w - bw - 20, cur_y + 16), (total_w - 20, cur_y + 48)],
                               radius=4, fill=(15, 20, 32, 255), outline=p_col, width=1)
        draw.text((total_w - bw - 10, cur_y + 24), p_badge, fill=p_col, font=font_tag)

        # Image
        comp.alpha_composite(p_img, (0, cur_y + header_h))
        cur_y += header_h + panel_h + 15

    out_path.parent.mkdir(parents=True, exist_ok=True)
    comp.convert("RGB").save(out_path)
    print(f"Saved before/after comparison to: {out_path} ({comp.size[0]}x{comp.size[1]})")
    return out_path


def render_preview(state_name: str = "init_f1",
                   out_path: Path | None = None,
                   *,
                   work_dir: Path | None = None,
                   build_dir: Path | None = None,
                   ui_gen: int = 2,
                   knobs: int = 16,
                   compare_out: Path | None = None,
                   state_out: Path | None = None,
                   historical_after: Path | None = None) -> tuple[Path, Path]:
    output_path = out_path or DEFAULT_OUT
    comparison_path = compare_out or DEFAULT_COMPARE
    state_json_path = state_out or DEFAULT_STATE_JSON

    working_dir = work_dir or DEFAULT_WORK_DIR
    working_dir.mkdir(parents=True, exist_ok=True)

    print(f"Ensuring core build directory and fixture binary (UI Gen {ui_gen})...")
    fixture_bin = ensure_fixture_binary(working_dir, ui_gen=ui_gen, qframe_enabled=0 if ui_gen == 2 else 1, core_build_dir=build_dir)

    print("Ensuring generated MPC skin TUI.json...")
    skins_dir = ensure_generated_skin(working_dir)

    overlay_png = working_dir / f"{state_name}_overlay_gen{ui_gen}.png"
    state_json = working_dir / f"{state_name}_state_gen{ui_gen}.json"

    print(f"Running C++ filter overlay fixture for state '{state_name}'...")
    cmd = [
        str(fixture_bin),
        "--state", state_name,
        "--dump-png", str(overlay_png),
        "--dump-json", str(state_json),
        "--knobs", str(knobs),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"filter_render_fixture failed:\n{res.stderr}\n{res.stdout}")

    if not overlay_png.is_file() or not state_json.is_file():
        raise FileNotFoundError(f"Fixture did not output expected files: {overlay_png}, {state_json}")

    # Copy state JSON to canonical output if requested
    state_json_path.parent.mkdir(parents=True, exist_ok=True)
    state_json_path.write_text(state_json.read_text(encoding="utf-8"), encoding="utf-8")
    print(f"Saved fixture state JSON to: {state_json_path}")

    state_data = json.loads(state_json.read_text(encoding="utf-8"))

    print("Rendering native host skin tab via FilterSkinSketch...")
    fsk = FilterSkinSketch(skins_dir, state_data)
    tab_native = fsk.render_tab({"subtab_f2": "Filter 2", "subtab_note": "Note", "subtab_velo": "Velo"}.get(state_name, "Filter 1"))

    print("Compositing C++ overlay onto native host tab...")
    tab_overlay = Image.open(overlay_png).convert("RGBA")

    commit = get_commit_hash()
    state_labels = {
        "subtab_f1": "SUB-TAB FILTER 1 (VIEW INDEX 1)",
        "subtab_f2": "SUB-TAB FILTER 2 (VIEW INDEX 2)",
        "subtab_note": "SUB-TAB NOTE (VIEW INDEX 3): CURVE EDITOR USES THE WHOLE PAGE",
        "subtab_velo": "SUB-TAB VELO (VIEW INDEX 4)",
        "init_f1": "SPLIT F1 + NOTE · DIGITAL 12dB · PARALLEL",
        "init_f2": "SPLIT F2 + NOTE · DIGITAL 12dB · PARALLEL",
        "f2_velo": "SPLIT F2 + VELO · DIGITAL 12dB · PARALLEL",
        "f1_off_f2_on": "SPLIT F1 OFF / F2 ON · FILTER CHAIN",
        "both_off": "SPLIT F1 OFF / F2 OFF · FILTER CHAIN · NOTE/VELO ACTIVE",
        "formant": "SPLIT F1 + NOTE · FORMANT XY + F RESO",
        "formant_off": "SPLIT FORMANT OFF · READ-ONLY MODEL-SPECIFIC STYLE",
        "comb_off": "SPLIT COMB OFF · READ-ONLY MODEL-SPECIFIC STYLE",
        "comb": "SPLIT F1 + NOTE · COMB + OFFSET",
        "expanded_filter": "EXPANDED FILTER F1 · OUT READOUT + FULL CONTROLS",
        "expanded_curve": "EXPANDED NOTE CURVE · FULL SCALAR EDITOR",
        "dual_parallel": "EXPANDED FILTER F1+F2 DUAL · PARALLEL",
    }
    label = f"FIXTURE: {state_name.upper()} · {state_labels.get(state_name, state_name.upper())}"

    full_screen = compose_full_mpc_screen(tab_native, tab_overlay, label, ui_gen=ui_gen, knobs=knobs, commit_hash=commit)

    output_path = export_path(output_path)
    save_screen(full_screen.convert("RGB"), output_path)
    print(f"Saved canonical Filter preview to: {output_path}")

    if compare_out is not None:
        comparison_path = require_external_output(comparison_path)
        print("Generating comparison image in external path...")
        render_before_after_comparison(full_screen, comparison_path, commit, historical_after=historical_after)

    return output_path, comparison_path


def main():
    parser = argparse.ArgumentParser(description="Render authentic standalone Filter screen preview.")
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="Output preview image path")
    parser.add_argument("--compare", default=None, help="Optional external output before/after comparison path")
    parser.add_argument("--historical-after", type=Path, default=None, help="Optional explicit historical reference image for 3-panel compare")
    parser.add_argument("--state-json", default=str(DEFAULT_STATE_JSON), help="Output fixture state JSON path")
    parser.add_argument("--state", default="init_f1", help="Fixture state (init_f1, init_f2, subtab_f1, subtab_f2, subtab_note, subtab_velo, f2_velo, f1_off_f2_on, both_off, formant, comb, expanded_filter, expanded_curve, dual_parallel)")
    parser.add_argument("--ui-gen", type=int, default=2, choices=[1, 2], help="UI Generation (1: Gen1 with Q frames, 2: Gen2 without Q frames)")
    parser.add_argument("--knobs", type=int, default=16, choices=[4, 16], help="Number of physical Q-Link knobs")
    parser.add_argument("--work-dir", type=Path, default=None, help="Working directory for skin generation and fixture binary")
    parser.add_argument("--build-dir", type=Path, default=None, help="CMake core build directory")

    args = parser.parse_args()

    render_preview(
        state_name=args.state,
        out_path=Path(args.out),
        work_dir=args.work_dir,
        build_dir=args.build_dir,
        ui_gen=args.ui_gen,
        knobs=args.knobs,
        compare_out=Path(args.compare) if args.compare else None,
        state_out=Path(args.state_json),
        historical_after=args.historical_after,
    )


if __name__ == "__main__":
    main()
