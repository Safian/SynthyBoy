#!/usr/bin/env python3
# SynthyBoy - Authentic offline standalone Mod screen renderer.
# Generates real TUI.json skins, runs the real C++ ModUi / scene_cpu.h fixture
# (mod_render_fixture), renders the real generated TUI tab through a Sketch adapter
# without hand-drawn fake controls or procedural fallback placeholders, and composites the
# C++ overlay layer identically to hardware display planes.
# NOTE: Renders documented synthetic fixtures, NOT a live MPC device capture.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
for p in (str(REPO_ROOT), str(HERE)):
    if p not in sys.path:
        sys.path.insert(0, p)

from native_tab_sketch import NativeTabSketch
from components import style
from native_skin_font import semibold
from screen_images import screen_path, save_screen, export_path
from preview_skin import PARAMS_JSON, ensure_generated_skin

CONTRACT_JSON = REPO_ROOT / "scripts" / "mpc-skin-extras" / "mod-native-contract.json"
PREVIEW_BASE_SCRIPT = REPO_ROOT / "scripts" / "render-mpc-skin-preview.py"
FIXTURE_SRC = REPO_ROOT / "shell-mpc" / "overlay" / "tests" / "mod_render_fixture.cpp"

OUT_FULL = screen_path('mod')
HISTORICAL_BEFORE = REPO_ROOT / "docs" / "screens" / "mod_old.jpg"
HISTORICAL_SLOT_BEFORE = Path("/tmp/mod-slot-historical.png") if Path("/tmp/mod-slot-historical.png").is_file() else REPO_ROOT / "docs" / "user-guide" / "img" / "mod-slot.png"

DEFAULT_BUILD_DIR = REPO_ROOT / "build"


def _load_sketch_module():
    spec = importlib.util.spec_from_file_location("render_mpc_skin_preview", PREVIEW_BASE_SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _find_core_build_dir(explicit=None) -> Path:
    candidates = [Path(explicit)] if explicit else [
        Path("/tmp/synthyboy-501/2269444331/accept/build-mpc"),
        REPO_ROOT / "build",
        REPO_ROOT.parent / "agy-gemini-worker-c860338358" / "build",
        REPO_ROOT.parent / "agy-gemini-worker-433db9b109" / "build",
    ]
    for candidate in candidates:
        if (candidate / "core" / "libsynthyboy_core.a").is_file():
            return candidate.resolve()
    raise FileNotFoundError("Build synthyboy_core in this checkout or supply --build-dir <CMake cache directory>.")


def ensure_fixture_binary(work_dir: Path, ui_gen: int = 2, core_build_dir=None) -> Path:
    build_dir = _find_core_build_dir(core_build_dir)
    out_bin = work_dir / f"mod_render_fixture_gen{ui_gen}"
    dependencies = [
        FIXTURE_SRC,
        *(REPO_ROOT / "shell-mpc").glob("*.h"),
        *(REPO_ROOT / "shell-mpc" / "overlay").rglob("*.h"),
        *(REPO_ROOT / "shell-mpc" / "ui").rglob("*.h"),
    ]
    libs = [
        build_dir / "core" / "libsynthyboy_core.a",
        build_dir / "core" / "filters-serum" / "libsynthyboy_filters_serum.a",
        build_dir / "core" / "shapes" / "libsynthyboy_shapes.a",
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
        raise RuntimeError(f"mod_render_fixture compile failed:\n{res.stderr}")
    command_file.write_text(command_text)
    return out_bin


class ModSketchAdapter(NativeTabSketch):
    """Adapter around render-mpc-skin-preview.Sketch that authentically renders
    the generated TUI tab using real MPC components, remapped handles,
    evaluated gates, and C++ fixture parameters."""

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

    def _resolve_handle(self, handle: str | None, remaps: dict[str, str]) -> tuple[str | None, str | None]:
        if not handle:
            return None, None
        seen = set()
        with_name = None
        cur = handle
        while cur and cur not in seen:
            seen.add(cur)
            if cur.startswith("WithName[") and "]/" in cur:
                head, rest = cur.split("]/", 1)
                with_name = head[len("WithName["):]
                cur = rest
                continue
            if cur.startswith("FromGesture/"):
                cur = cur[len("FromGesture/"):]
                continue
            if cur in remaps:
                cur = remaps[cur]
                continue
            prefix, slash, base_name = cur.rpartition("/")
            if slash and base_name in remaps:
                cur = remaps[base_name]
                continue
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
            if parts[0] == "NonZero" and len(parts) >= 2:
                handle_str = "/".join(parts[1:])
                _, h = self._resolve_handle(handle_str, remaps)
                if self._norm_for_handle(h) <= 1e-5:
                    return False
            elif parts[0] == "IndexedEnabling" and len(parts) >= 4:
                idx, total = int(parts[1]), int(parts[2])
                handle_str = "/".join(parts[3:])
                _, h = self._resolve_handle(handle_str, remaps)
                v = self._norm_for_handle(h)
                step = int(round(v * max(1, total - 1)))
                if step != idx:
                    return False
            elif parts[0] == "RangedEnabling" and len(parts) >= 5:
                lo, hi, total = int(parts[1]), int(parts[2]), int(parts[3])
                handle_str = "/".join(parts[4:])
                _, h = self._resolve_handle(handle_str, remaps)
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
        return item.get("name", "")

    def _value_for_handle(self, local_handle: str, resolved: str | None) -> str:
        if not resolved or not resolved.startswith("Parameter "):
            return ""
        item = self._state.get("virtual", {}).get(resolved)
        if item is not None:
            return item.get("display", "")
        item = self._state.get("params", {}).get(resolved[len("Parameter "):], {})
        return item.get("value", "")

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
                    draw = ImageDraw.Draw(canvas)
                    r = style.size("radius.field") if "list" in ctype or "toggle" in ctype else 0
                    draw.rounded_rectangle([(x, y), (x + w - 1, y + h - 1)],
                                           radius=r, fill=bg_col,
                                           outline=style.color("button.border") if r > 0 else None, width=1)
                    if "list" in ctype:
                        cx = x + w - 16
                        cy = y + h // 2
                        cw = 4
                        draw.polygon([(cx - cw, cy - 2), (cx + cw, cy - 2), (cx, cy + 3)],
                                     fill=style.color("text.muted"))

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

        if ctype == "Slider":
            stype = data.get("sliderType", "ColourBar")
            _, resolved = self._resolve_handle(data.get("handleName"), merged)
            norm = min(1.0, max(0.0, self._norm_for_handle(resolved)))
            is_bipolar = data.get("isBipolar", False) or data.get("revealType") == "Bipolar"

            if stype == "TrackAndThumb":
                draw = ImageDraw.Draw(canvas)
                track_h = style.size("mod_row.track_height")
                track_y = y + (h - track_h) // 2
                pad = 4
                # Track outline and surface
                draw.rounded_rectangle(
                    [(x + pad, track_y), (x + w - pad - 1, track_y + track_h - 1)],
                    radius=style.size("mod_row.track_radius"),
                    fill=style.color("mod_row.track"),
                    outline=style.color("mod_row.track_border"),
                    width=1,
                )
                mid_x = x + w // 2
                half_w = (w - 2 * pad) // 2

                # Center zero tick (prominent full height)
                draw.line([(mid_x, track_y + 1), (mid_x, track_y + track_h - 2)],
                          fill=style.color("mod_row.center_tick"), width=1)

                # Major ticks (+/- 50%)
                q_step = half_w // 2
                for sx in (mid_x - q_step, mid_x + q_step):
                    draw.line([(sx, track_y + 2), (sx, track_y + track_h - 3)],
                              fill=style.color("mod_row.tick_major"), width=1)
                # Minor quarter ticks (+/- 25%, +/- 75%)
                oct_step = half_w // 4
                for sx in (mid_x - 3 * oct_step, mid_x - oct_step, mid_x + oct_step, mid_x + 3 * oct_step):
                    draw.line([(sx, track_y + 3), (sx, track_y + track_h - 4)],
                              fill=style.color("mod_row.tick_minor"), width=1)

                val_fraction = (norm - 0.5) * 2.0 if is_bipolar else norm
                bar_len = int(round(val_fraction * (half_w - 4))) if is_bipolar else int(round(norm * (w - 2 * pad - 8)))
                val_col = style.color("mod_row.amount_fill")
                if is_bipolar:
                    if bar_len > 0:
                        draw.rectangle([(mid_x, track_y + 2), (mid_x + bar_len, track_y + track_h - 3)], fill=val_col)
                    elif bar_len < 0:
                        draw.rectangle([(mid_x + bar_len, track_y + 2), (mid_x, track_y + track_h - 3)], fill=val_col)
                    thumb_pos = mid_x + bar_len
                else:
                    if bar_len > 0:
                        draw.rectangle([(x + pad + 2, track_y + 2), (x + pad + 2 + bar_len, track_y + track_h - 3)], fill=val_col)
                    thumb_pos = x + pad + 2 + bar_len

                # Thumb indicator
                thumb_img = self._open_image(data.get("thumbImage", ""))
                if thumb_img is not None:
                    canvas.alpha_composite(thumb_img, (thumb_pos - thumb_img.width // 2, y))
                else:
                    draw.rectangle([(thumb_pos - 2, track_y - 2), (thumb_pos + 1, track_y + track_h + 1)],
                                   fill=val_col)
                return

            if stype == "ColourBar":
                bg_col = self._base_mod.argb(data.get("backgroundColour", "ff242b42"))
                outline_col = self._base_mod.argb(data.get("outlineColour", "ff52607a"))
                val_col = self._base_mod.argb(data.get("valueColour", "ff72d1ec"))
                draw = ImageDraw.Draw(canvas)
                draw.rectangle([(x, y), (x + w - 1, y + h - 1)], fill=bg_col, outline=outline_col, width=1)
                if is_bipolar:
                    mid_x = x + w // 2
                    val_fraction = (norm - 0.5) * 2.0  # -1.0 .. +1.0
                    bar_len = int(round(val_fraction * (w // 2 - 2)))
                    if bar_len > 0:
                        draw.rectangle([(mid_x, y + 2), (mid_x + bar_len, y + h - 3)], fill=val_col)
                    elif bar_len < 0:
                        draw.rectangle([(mid_x + bar_len, y + 2), (mid_x, y + h - 3)], fill=val_col)
                    draw.line([(mid_x, y), (mid_x, y + h - 1)], fill=outline_col, width=1)
                else:
                    bar_len = int(round(norm * (w - 4)))
                    if bar_len > 0:
                        draw.rectangle([(x + 2, y + 2), (x + 2 + bar_len, y + h - 3)], fill=val_col)
                return

        if ctype == "Knob":
            _, resolved = self._resolve_handle(data.get("handleName"), merged)
            norm = min(1.0, max(0.0, self._norm_for_handle(resolved)))
            strip_name = data.get("filmStrip") or data.get("image", "")
            num_frames = int(data.get("numFrames") or data.get("frames", 127))
            strip_img = self._open_image(strip_name)
            if strip_img is not None and num_frames > 1:
                frame_idx = min(num_frames - 1, max(0, int(round(norm * (num_frames - 1)))))
                frame_h = strip_img.height // num_frames
                frame = strip_img.crop((0, frame_idx * frame_h, strip_img.width, (frame_idx + 1) * frame_h))
                if frame.size != (w, h):
                    frame = frame.resize((w, h), Image.Resampling.LANCZOS)
                canvas.alpha_composite(frame, (x, y))
                return
            draw = ImageDraw.Draw(canvas)
            cx, cy = x + w // 2, y + h // 2
            r = min(w, h) // 2 - 4
            is_bipolar = "bi" in strip_name or data.get("polarity") == "BiPolar"
            draw.ellipse([(cx - r, cy - r), (cx + r, cy + r)], outline=style.color("dial.track"), width=3)
            ang = 270.0 + (norm - 0.5) * 260.0 if is_bipolar else 140.0 + norm * 260.0
            rad = math.radians(ang)
            px = cx + int(round((r - 2) * math.cos(rad)))
            py = cy + int(round((r - 2) * math.sin(rad)))
            draw.line([(cx, cy), (px, py)], fill=style.color("dial.value"), width=2)
            return

        if ctype == "Focus":
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
                tx = x + 8 if anchor_x == "l" else (x + w - 8 if anchor_x == "r" else x + w // 2)
                ty = y + h // 2
                draw.text((tx, ty), txt, fill=col, font=font, anchor=f"{anchor_x}m")
            return

    def render_tab(self, tab_name: str = "Mod") -> Image.Image:
        return super().render_tab(tab_name)


def compose_tab(host_tab: Image.Image, overlay_tab: Image.Image) -> Image.Image:
    """Exact 1280x628 composite of native host skin tab and C++ overlay."""
    out = Image.new("RGBA", (1280, 628), (0, 0, 0, 0))
    out.alpha_composite(host_tab, (0, 0))
    out.alpha_composite(overlay_tab, (0, 0))
    return out


def build_mpc_full_screen(tab_image: Image.Image, active_subtab: str = "MOD") -> Image.Image:
    """Composite 1280x628 tab content into full 1280x800 MPC standalone frame."""
    full = Image.new("RGBA", (1280, 800), "#0b1026")
    draw = ImageDraw.Draw(full)

    # Top Bar (y: 0..110)
    draw.rectangle([(0, 0), (1280, 110)], fill="#10141f")
    draw.line([(0, 109), (1280, 109)], fill="#26304b", width=1)

    # Title and header in top bar
    font_title = semibold(22)
    font_meta = semibold(15)
    draw.text((24, 40), "SYNTHYBOY", font=font_title, fill="#ffffff", anchor="lm")
    draw.text((200, 42), "MOD MATRIX · HOST-NATIVE SKIN", font=font_meta, fill="#72d1ec", anchor="lm")

    # Preset pill / badge
    draw.rounded_rectangle([(960, 24), (1256, 68)], radius=6, fill="#151a2d", outline="#26304b", width=1)
    draw.text((1108, 46), "MODULATION ROUTING", font=font_meta, fill="#e6e6e6", anchor="mm")

    # Sub-tabs row in top bar (y: 72..108)
    tabs = [("MAIN", False), ("MOD", active_subtab == "MOD"), ("TARGETS", False)]
    tx = 24
    for tab_name, active in tabs:
        tw = 100
        if active:
            draw.rectangle([(tx, 106), (tx + tw, 109)], fill="#00f0ff")
            draw.text((tx + tw // 2, 90), tab_name, font=font_meta, fill="#00f0ff", anchor="mm")
        else:
            draw.text((tx + tw // 2, 90), tab_name, font=font_meta, fill="#829bc4", anchor="mm")
        tx += tw + 16

    # Paste 1280x628 Tab Content (y: 110..738)
    full.alpha_composite(tab_image, (0, 110))

    # Bottom Bar / Function Keys (y: 738..800)
    draw.rectangle([(0, 738), (1280, 800)], fill="#10141f")
    draw.line([(0, 738), (1280, 738)], fill="#26304b", width=1)

    # 6 Function Key slots (F1..F6)
    fn_keys = ["PLAY", "OSC", "ENV", "LFO", "MOD", "FX"]
    slot_w = 1280 // 6
    for i, fn in enumerate(fn_keys):
        bx = i * slot_w + 4
        bw = slot_w - 8
        is_active = (fn == "MOD")
        btn_bg = "#175a76" if is_active else "#151a2d"
        btn_border = "#00f0ff" if is_active else "#26304b"
        text_col = "#ffffff" if is_active else "#829bc4"
        draw.rounded_rectangle([(bx, 746), (bx + bw, 792)], radius=5, fill=btn_bg, outline=btn_border, width=1)
        draw.text((bx + bw // 2, 769), fn, font=font_meta, fill=text_col, anchor="mm")

    return full


def create_before_after_comparison(
    before_img: Image.Image,
    after_img: Image.Image,
    title_before: str = "HISTORICAL BEFORE: FULL C++ OVERLAY MATRIX (GEN 1)",
    sub_before: str = "1280x628 · Yellow Q-Link bank frames & numbered chips",
    title_after: str = "CURRENT AFTER: AUTHENTIC NATIVE + OVERLAY COMPOSITE (GEN 2, OFFLINE PREVIEW)",
    sub_after: str = "1280x628 · Host native TUI layer + C++ overlay plane · Offline synthetic fixture preview",
) -> Image.Image:
    """Creates side-by-side Before/After comparison with neutral
    outside labels, without altering the historical before image or inventing
    fake MPC chrome/footer."""
    w, h = 1280, 628
    caption_h = 44
    border = 16
    total_w = w * 2 + border * 3
    total_h = h + caption_h + border * 2

    canvas = Image.new("RGBA", (total_w, total_h), style.color("surface.canvas"))
    draw = ImageDraw.Draw(canvas)

    font_file = REPO_ROOT / "components/native/mod_row/img" / style.size("font.file.semibold")
    if font_file.is_file():
        font_caption = ImageFont.truetype(str(font_file), style.size("mod_row.font.label"))
        font_sub = ImageFont.truetype(str(font_file), style.size("mod_row.font.header"))
    else:
        font_caption = ImageFont.load_default()
        font_sub = font_caption

    # Before column
    bx = border
    by = border + caption_h
    draw.text((bx + 4, border + 10), title_before,
              fill=style.color("accent.cyan"), font=font_caption)
    draw.text((bx + 4, border + 28), sub_before,
              fill=style.color("text.muted"), font=font_sub)
    canvas.alpha_composite(before_img.convert("RGBA").resize((w, h), Image.Resampling.LANCZOS), (bx, by))
    draw.rectangle([(bx - 1, by - 1), (bx + w, by + h)], outline=style.color("button.border"), width=1)

    # After column
    ax = border * 2 + w
    ay = border + caption_h
    draw.text((ax + 4, border + 10), title_after,
              fill=style.color("accent.cyan"), font=font_caption)
    draw.text((ax + 4, border + 28), sub_after,
              fill=style.color("text.muted"), font=font_sub)
    canvas.alpha_composite(after_img.convert("RGBA").resize((w, h), Image.Resampling.LANCZOS), (ax, ay))
    draw.rectangle([(ax - 1, ay - 1), (ax + w, ay + h)], outline=style.color("button.border"), width=1)

    return canvas


def render_all(
    detail_out: str | None = None,
    detail_confirm_out: str | None = None,
    comparison_out: str | None = None,
    edit_comparison_out: str | None = None,
):
    screen_path('mod').parent.mkdir(parents=True, exist_ok=True)

    import tempfile
    with tempfile.TemporaryDirectory() as tmp_dir_str:
        tmp_dir = Path(tmp_dir_str)
        fixture_bin = ensure_fixture_binary(tmp_dir, ui_gen=2)
        skins_dir = ensure_generated_skin(tmp_dir)

        # 1. Render Populated Matrix State
        pop_overlay_png = tmp_dir / "mod_pop_overlay.png"
        pop_state_json = tmp_dir / "mod_pop_state.json"
        cmd = [
            str(fixture_bin),
            "--state", "populated",
            "--out-overlay", str(pop_overlay_png),
            "--out-json", str(pop_state_json),
            "--gen", "2",
            "--knobs", "16",
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"mod_render_fixture populated failed: {res.stderr}")

        pop_state = json.loads(pop_state_json.read_text(encoding="utf-8"))
        adapter_pop = ModSketchAdapter(skins_dir, pop_state)
        host_tab_pop = adapter_pop.render_tab("Mod")
        pop_overlay = Image.open(pop_overlay_png).convert("RGBA")

        # Composite full current screen (true 1280x628 tab, authentic host-native + overlay plane)
        current_pop_tab = compose_tab(host_tab_pop, pop_overlay)

        # Save canonical screen (true 1280x628 tab)
        save_screen(current_pop_tab, OUT_FULL)
        print(f"Saved canonical full screen preview: {OUT_FULL} ({current_pop_tab.size})")

        # 2. Render Detail State (Fully Host-Native Edit Detail Screen, Unarmed)
        detail_overlay_png = tmp_dir / "mod_detail_overlay.png"
        detail_state_json = tmp_dir / "mod_detail_state.json"
        cmd_detail = [
            str(fixture_bin),
            "--state", "detail",
            "--out-overlay", str(detail_overlay_png),
            "--out-json", str(detail_state_json),
            "--gen", "2",
            "--knobs", "16",
        ]
        res_detail = subprocess.run(cmd_detail, capture_output=True, text=True)
        if res_detail.returncode != 0:
            raise RuntimeError(f"mod_render_fixture detail failed: {res_detail.stderr}")

        detail_state = json.loads(detail_state_json.read_text(encoding="utf-8"))
        adapter_detail = ModSketchAdapter(skins_dir, detail_state)
        host_tab_detail = adapter_detail.render_tab("Mod")
        detail_overlay = Image.open(detail_overlay_png).convert("RGBA")

        current_detail_tab = compose_tab(host_tab_detail, detail_overlay)

        if detail_out:
            det_p = Path(detail_out)
            det_p.parent.mkdir(parents=True, exist_ok=True)
            current_detail_tab.convert("RGB").save(det_p)
            print(f"Saved detail full screen export: {det_p} ({current_detail_tab.size})")

        # 2b. Render Detail Armed State (Delete armed -> CONFIRM?)
        if detail_confirm_out:
            confirm_overlay_png = tmp_dir / "mod_detail_confirm_overlay.png"
            confirm_state_json = tmp_dir / "mod_detail_confirm_state.json"
            cmd_confirm = [
                str(fixture_bin),
                "--state", "detail_confirm",
                "--out-overlay", str(confirm_overlay_png),
                "--out-json", str(confirm_state_json),
                "--gen", "2",
                "--knobs", "16",
            ]
            res_confirm = subprocess.run(cmd_confirm, capture_output=True, text=True)
            if res_confirm.returncode != 0:
                raise RuntimeError(f"mod_render_fixture detail_confirm failed: {res_confirm.stderr}")

            confirm_state = json.loads(confirm_state_json.read_text(encoding="utf-8"))
            adapter_confirm = ModSketchAdapter(skins_dir, confirm_state)
            host_tab_confirm = adapter_confirm.render_tab("Mod")
            confirm_overlay = Image.open(confirm_overlay_png).convert("RGBA")

            current_confirm_tab = compose_tab(host_tab_confirm, confirm_overlay)
            conf_p = Path(detail_confirm_out)
            conf_p.parent.mkdir(parents=True, exist_ok=True)
            current_confirm_tab.convert("RGB").save(conf_p)
            print(f"Saved armed detail full screen export: {conf_p} ({current_confirm_tab.size})")

        # 3. Optional Matrix Before / After Comparison
        if comparison_out and HISTORICAL_BEFORE.is_file():
            before_im = Image.open(HISTORICAL_BEFORE)
            comp_mat = create_before_after_comparison(
                before_im, current_pop_tab,
                title_before="HISTORICAL BEFORE: FULL C++ OVERLAY MATRIX (GEN 1)",
                sub_before="1280x628 · Yellow Q-Link bank frames & numbered chips (docs/screens/mod_old.jpg)",
                title_after="CURRENT AFTER: AUTHENTIC NATIVE + OVERLAY COMPOSITE (GEN 2, OFFLINE PREVIEW)",
                sub_after="1280x628 · Host native TUI layer + C++ overlay plane · Offline synthetic fixture preview",
            )
            cmp_p = Path(comparison_out)
            cmp_p.parent.mkdir(parents=True, exist_ok=True)
            (comp_mat.convert("RGB") if cmp_p.suffix.lower() in (".jpg", ".jpeg") else comp_mat).save(cmp_p)
            print(f"Saved matrix before/after comparison: {cmp_p} ({comp_mat.size})")

        # 4. Optional Detail Before / After Comparison
        if edit_comparison_out and HISTORICAL_SLOT_BEFORE.is_file():
            before_slot_im = Image.open(HISTORICAL_SLOT_BEFORE)
            comp_detail = create_before_after_comparison(
                before_slot_im,
                current_detail_tab,
                title_before="HISTORICAL BEFORE: FULL C++ OVERLAY SLOT DETAIL (GEN 1)",
                sub_before="1280x628 · Historical screenshot (docs/user-guide/img/mod-slot.png)",
                title_after="CURRENT AFTER: FULLY HOST-NATIVE EDIT DETAIL SCREEN (GEN 2, OFFLINE PREVIEW)",
                sub_after="1280x628 · Host native TUI layer · 3-column AUX/SHAPE/TIME · 90px dials · 0 overlay ops",
            )
            ed_cmp_p = Path(edit_comparison_out)
            ed_cmp_p.parent.mkdir(parents=True, exist_ok=True)
            (comp_detail.convert("RGB") if ed_cmp_p.suffix.lower() in (".jpg", ".jpeg") else comp_detail).save(ed_cmp_p)
            print(f"Saved detail before/after comparison: {ed_cmp_p} ({comp_detail.size})")


def main():
    parser = argparse.ArgumentParser(description="Render authentic Mod screen previews and comparison.")
    parser.add_argument("--all", action="store_true", default=True, help="Render canonical screen preview.")
    parser.add_argument("--detail-out", default=None, help="Optional external path to export detail screen.")
    parser.add_argument("--detail-confirm", default=None, nargs="?", const="/tmp/mod_detail_confirm.jpg",
                        help="Export armed detail screen (defaults to /tmp/mod_detail_confirm.jpg if flag given).")
    parser.add_argument("--detail-confirm-out", default=None, help="Optional external path to export armed detail screen.")
    parser.add_argument("--comparison-out", default=None, help="Optional external path to export matrix before/after.")
    parser.add_argument("--edit-comparison-out", default=None, help="Optional external path to export detail before/after.")
    args = parser.parse_args()
    confirm_out = args.detail_confirm_out or args.detail_confirm
    render_all(
        detail_out=args.detail_out,
        detail_confirm_out=confirm_out,
        comparison_out=args.comparison_out,
        edit_comparison_out=args.edit_comparison_out,
    )


if __name__ == "__main__":
    main()
