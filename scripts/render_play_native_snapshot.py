#!/usr/bin/env python3
# SynthyBoy - generates an authentic offline native-layer snapshot of the Play tab of an MPC skin.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/render_play_native_snapshot.py --out /absolute/path.png
#
# Produces a single authentic 1280x628 PNG snapshot of the native UI layer:
#   - Proper visibility filtering for active/inactive/modal states (Sub active, Env 1 active, LFO 1 active)
#   - Accurate component definition resolution and handle remapping (Attack, Decay, Sustain, Release, etc.)
#   - Proper proportional bounds handling for sub-components (RATE, etc.)
#   - Real blue FilmStrip knob assets from components/native/level & components/native/pan
#     (play_sub_level_90.png, play_sub_level_60.png, play_sub_level_40.png, play_sub_pan_90.png);
#     strictly NO knob_uni.png fallback or default.
#   - Active state button artwork (Sub power ON, SYNC HZ ON)
#   - Strictly zero visible Q-Link numbers or frames (Rule 5b)
#
# Requires: MPC_SDK environment variable pointing to the MPC SDK directory.

from screen_images import require_external_output
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
GEN_MPC_UI = ROOT / "scripts" / "gen-mpc-ui.py"
RUN_GEN_UI = ROOT / "scripts" / "run-gen-ui.py"
APPLY_EXTRAS = ROOT / "scripts" / "apply-mpc-skin-extras.py"
RENDER_PREVIEW = ROOT / "scripts" / "render-mpc-skin-preview.py"
SDK = Path(os.environ.get("MPC_SDK") or Path("/Users/szabi/.local/share/local-worker/ext/mpc-sdk") if Path("/Users/szabi/.local/share/local-worker/ext/mpc-sdk").is_dir() else Path.home() / ".local/share/local-worker/ext/mpc-sdk")

# Load base Sketch from render-mpc-skin-preview.py
spec = importlib.util.spec_from_file_location("render_mpc_skin_preview", str(RENDER_PREVIEW))
preview_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview_mod)
BaseSketch = preview_mod.Sketch

# Documented fixture parameter state for Play tab:
# Sub active, Env 1 active, LFO 1 active, highlight on group 1 (Oscillator)
FIXTURE_STATE = {
    "Parameter 3529": 0,  # Native modulation editor closed
    "Parameter 2675": 0,  # Main page view (normal, not modal/drawer)
    "Parameter 3537": 0,  # Expanded Play editors closed
    "Parameter 2676": 0,  # Play Osc: Sub (index 0)
    "Parameter 2677": 0,  # Play Env: Env 1 (index 0)
    "Parameter 2678": 0,  # Play LFO: LFO 1 (index 0)
    "Parameter 3312": 1,  # Compact osc sub-state: Sub active (1)
    "Parameter 1558": 1,  # Sub power ON (1)
    "Parameter 2943": 1,  # LFO 1 is standard LFO (1)
    "Parameter 2699": 1,  # LFO Smooth switch ON (1) by default
    "Parameter 3302": 1,  # LFO 1 rate sync mode: Hz mode (1)
}

# Resolved fixture parameter metadata (name, display value, normalized 0..1 for knobs)
FIXTURE_PARAMS = {
    "Parameter 2676": {"name": "Play Osc", "value": "SUB", "norm": 0.0},
    "Parameter 2677": {"name": "Play Env", "value": "ENV 1", "norm": 0.0},
    "Parameter 2678": {"name": "Play LFO", "value": "LFO 1", "norm": 0.0},
    "Parameter 2685": {"name": "Sub Pan", "value": "C", "norm": 0.5},
    "Parameter 2686": {"name": "Sub Level", "value": "0.0 dB", "norm": 0.75},
    "Parameter 2687": {"name": "Attack", "value": "0.0 ms", "norm": 0.0},
    "Parameter 2688": {"name": "Hold", "value": "0.0 ms", "norm": 0.0},
    "Parameter 2689": {"name": "Decay", "value": "1.00 s", "norm": 0.501},
    "Parameter 2690": {"name": "Sustain", "value": "50.0 %", "norm": 0.5},
    "Parameter 2691": {"name": "Release", "value": "500 ms", "norm": 0.407},
    "Parameter 2695": {"name": "Rate", "value": "1.00 Hz", "norm": 0.265},
    "Parameter 2696": {"name": "Rise", "value": "0.0 ms", "norm": 0.0},
    "Parameter 2698": {"name": "Time", "value": "150 ms", "norm": 0.2},
    "Parameter 2699": {"name": "Smooth", "value": "ON", "norm": 1.0},
    "Parameter 3305": {"name": "SHAPE", "value": "SINE", "norm": 0.0},
    "Parameter 3306": {"name": "RATE", "value": "1.00 Hz", "norm": 0.265},
    "Parameter 3311": {"name": "Play LFO", "value": "LFO 1", "norm": 0.0},
    "Parameter 3316": {"name": "WAVE", "value": "SINE", "norm": 0.0},
    "Parameter 3317": {"name": "OCT", "value": "0", "norm": 0.5},
    "Parameter 3318": {"name": "PAN", "value": "C", "norm": 0.5},
    "Parameter 3319": {"name": "LEVEL", "value": "0.0 dB", "norm": 0.75},
    "sub.waveform": {"name": "WAVE", "value": "SINE", "norm": 0.0},
    "sub.octave": {"name": "OCT", "value": "0", "norm": 0.5},
    "sub.pan": {"name": "PAN", "value": "C", "norm": 0.5},
    "sub.level": {"name": "LEVEL", "value": "0.0 dB", "norm": 0.75},
    "lfo.rate": {"name": "RATE", "value": "1.00 Hz", "norm": 0.265},
    "lfo.shape": {"name": "SHAPE", "value": "SINE", "norm": 0.0},
}


def check_sdk():
    """Verify the MPC SDK is available."""
    gen_ui = SDK / "tools" / "gen_ui.py"
    if not gen_ui.is_file():
        print(f"render_play_native_snapshot.py: error: MPC SDK not found at {SDK} (set MPC_SDK)", file=sys.stderr)
        sys.exit(1)


def generate_ui(tmp_dir):
    """Generate ui.json and params-hostsim.json using gen-mpc-ui.py."""
    ui_dir = tmp_dir / "ui"
    ui_dir.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [sys.executable, str(GEN_MPC_UI),
         "--params", str(PARAMS_JSON),
         "--pages", str(PAGES_JSON),
         "--layout", str(LAYOUT_JSON),
         "--out", str(ui_dir)],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"render_play_native_snapshot.py: error: gen-mpc-ui.py failed:\n{result.stderr or result.stdout}", file=sys.stderr)
        sys.exit(1)
    return ui_dir


def generate_skins(tmp_dir, ui_dir):
    """Run the SDK gen_ui.py to produce the Plugin Skins folder."""
    skins_dir = tmp_dir / "pf" / "Plugin Skins"
    skins_dir.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [sys.executable, str(RUN_GEN_UI),
         "--sdk", str(SDK),
         "--params", str(ui_dir / "params-hostsim.json"),
         "--ui", str(ui_dir / "ui.json"),
         "--out", str(skins_dir),
         "--quiet"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"render_play_native_snapshot.py: error: run-gen-ui.py failed:\n{result.stderr or result.stdout}", file=sys.stderr)
        sys.exit(1)

    meta_src = ui_dir / "skin-meta.json"
    if meta_src.is_file():
        shutil.copy2(meta_src, skins_dir.parent / "skin-meta.json")
    return skins_dir


def apply_extras(skins_dir, ui_dir=None):
    """Apply MPC skin extras modules to the generated skin."""
    meta_path = (ui_dir / "skin-meta.json") if ui_dir and (ui_dir / "skin-meta.json").is_file() else (skins_dir.parent / "skin-meta.json")
    cmd = [sys.executable, str(APPLY_EXTRAS),
           "--skins", str(skins_dir),
           "--params", str(PARAMS_JSON)]
    if meta_path.is_file():
        cmd += ["--meta", str(meta_path)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"render_play_native_snapshot.py: error: apply-mpc-skin-extras.py failed:\n{result.stderr or result.stdout}", file=sys.stderr)
        sys.exit(1)


class PlaySketch(BaseSketch):
    """Dedicated Sketch adapter for the authentic Play tab snapshot."""

    draw_qlink_overlay = False  # Strictly zero Q-Link frames or numbers (Rule 5b)

    def __init__(self, skins, params):
        super().__init__(skins, params)
        self._font_path = ROOT / "shell-mpc" / "overlay" / "fonts" / "TitilliumWeb-SemiBold.ttf"

    def _font(self, style, px):
        size = max(8, int(round(px)))
        if self._font_path.is_file():
            try:
                return ImageFont.truetype(str(self._font_path), size)
            except OSError:
                pass
        return preview_mod.font(style, size)

    def text(self, img, box, txt, style, px, colour, justification, case):
        if case == "Upper Case":
            txt = txt.upper()
        x, y, w, h = box
        if w <= 0 or h <= 0 or not txt:
            return
        d = ImageDraw.Draw(img)
        size = max(8, int(round(px)))
        f = self._font(style, size)
        bb = d.textbbox((0, 0), txt, font=f)
        while (bb[2] - bb[0]) > w and size > 8:
            size -= 1
            f = self._font(style, size)
            bb = d.textbbox((0, 0), txt, font=f)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        tx = x + (w - tw) / 2 if "horizontallyCentred" in justification else (x + w - tw if "right" in justification else x)
        ty = y + (h - th) / 2 if "verticallyCentred" in justification else y
        d.text((tx - bb[0], ty - bb[1]), txt, font=f, fill=colour)

    def is_visible(self, bounds):
        """Accurately evaluate invalidating handles against the fixture state."""
        for h in bounds.get("additionalInvalidatingHandles", []):
            parts = h.split("/")
            rule = parts[0]
            if rule == "IndexedEnabling":
                target = int(parts[1])
                handle = parts[3]
                val = FIXTURE_STATE.get(handle, 0)
                if val != target:
                    return False
            elif rule == "RangedEnabling":
                rmin = int(parts[1])
                rmax = int(parts[2])
                handle = parts[4]
                val = FIXTURE_STATE.get(handle, 0)
                if not (rmin <= val <= rmax):
                    return False
        return True

    def parse_bounds(self, bounds_dict, parent_size):
        """Parse bounds supporting both Absolute and Proportional coordinate types."""
        raw = bounds_dict["bounds"].split()
        b_type = bounds_dict.get("boundsType", "Absolute")
        parent_w, parent_h = parent_size
        if b_type == "Proportional":
            x = int(round(float(raw[0]) * parent_w))
            y = int(round(float(raw[1]) * parent_h))
            w = int(round(float(raw[2]) * parent_w))
            h = int(round(float(raw[3]) * parent_h))
        else:
            x, y, w, h = (int(float(v)) for v in raw)
        return x, y, w, h

    def get_knob_frame(self, knob_name, norm_val, target_size, num_frames=127):
        """Load own component FilmStrip frame for normalized knob value, preserving 1:1 aspect ratio.

        Factory knob_uni.png is strictly rejected as both fallback and default.
        """
        target_w, target_h = target_size
        if target_w <= 0 or target_h <= 0:
            return None
        if not knob_name or Path(knob_name).name == "knob_uni.png":
            raise RuntimeError(f"Refusing forbidden factory knob asset: {knob_name!r}")
        film_path = self.skins / knob_name
        if not film_path.is_file():
            raise RuntimeError(f"Missing own component knob filmstrip asset: {film_path}")

        im = Image.open(film_path).convert("RGBA")
        num_frames = max(1, int(num_frames))
        frame_h = max(1, im.height // num_frames)
        frame_w = im.width
        frame_idx = max(0, min(num_frames - 1, int(round(norm_val * (num_frames - 1)))))
        crop = im.crop((0, frame_idx * frame_h, frame_w, (frame_idx + 1) * frame_h))
        scale = min(target_w / frame_w, target_h / frame_h)
        fit_w = max(1, int(round(frame_w * scale)))
        fit_h = max(1, int(round(frame_h * scale)))
        if crop.size != (fit_w, fit_h):
            crop = crop.resize((fit_w, fit_h), Image.Resampling.LANCZOS)
        return crop

    def fit_text(self, img, box, txt, style, px, colour, justification):
        """Draw text fitted within box width without overflowing."""
        self.text(img, box, txt, style, px, colour, justification, "Original")

    def draw_entry(self, img, entry, origin, parent_size=(1280, 628),
                   param_name=None, param_val=None, norm_val=0.0, remap_ctx=None):
        cd = entry["componentData"]
        b = entry["bounds"]
        if not self.is_visible(b):
            return

        x0, y0, w, h = self.parse_bounds(b, parent_size)
        x0, y0 = x0 + origin[0], y0 + origin[1]
        typ = cd.get("type", "")
        data = cd.get("data", {})

        # Handle remapping
        cur_remap = {m["key"]: m["value"] for m in entry.get("handle remapping", {}).get("map", [])}
        merged_remap = dict(remap_ctx or {})
        merged_remap.update(cur_remap)

        if typ in self.defs:
            # Component definition resolution
            def_data = self.defs[typ]
            if w <= 0 or h <= 0:
                return
            # Render custom component onto a local (w, h) canvas so children are clipped
            # to the component's actual bounds (matching JUCE/MPC Component clipping).
            local = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            bg_data = def_data.get("backgroundData", {}).get("unfocussed", {})
            bg_colour = preview_mod.argb(bg_data.get("colour"))
            if bg_colour and bg_colour[3] > 0:
                if bg_colour[3] == 255:
                    ImageDraw.Draw(local).rectangle([0, 0, w - 1, h - 1], fill=bg_colour)
                else:
                    local.alpha_composite(Image.new("RGBA", (w, h), bg_colour), (0, 0))
            bg_img_name = bg_data.get("image")
            if bg_img_name:
                bg_im = self.image(bg_img_name)
                if bg_im:
                    if bg_im.size != (w, h):
                        bg_im = bg_im.resize((w, h), Image.Resampling.LANCZOS)
                    local.alpha_composite(bg_im, (0, 0))

            # Determine resolved caption / param name
            child_name = param_name
            child_val = param_val
            child_norm = norm_val

            # Check Caption handle
            caption_handle = merged_remap.get("Caption", "")
            if caption_handle:
                m = re.search(r"WithName\[(.*?)\]", caption_handle)
                if m:
                    child_name = m.group(1)
                elif caption_handle in FIXTURE_PARAMS:
                    child_name = FIXTURE_PARAMS[caption_handle]["name"]
                elif not caption_handle.startswith("Parameter"):
                    child_name = caption_handle

            # Check Data handle
            data_handle = merged_remap.get("Data", "")
            if data_handle in FIXTURE_PARAMS:
                info = FIXTURE_PARAMS[data_handle]
                if not child_name:
                    child_name = info["name"]
                child_val = info["value"]
                child_norm = info.get("norm", child_norm)

            # Special case mappings for known custom types
            if typ == "playOscSelector":
                child_val = "SUB"
                child_name = "OSC"
            elif typ == "playModSelector":
                if "Env" in cd.get("name", ""):
                    child_val = "ENV 1"
                    child_name = "ENV"
                else:
                    child_val = "LFO 1"
                    child_name = "LFO"
            elif typ == "playEnvLfoChoice Select":
                child_val = "LFO 1"
                child_name = "LFO"
            elif typ == "playSubWaveField":
                child_name = "WAVE"
                child_val = "SINE"
            elif typ == "playOscNumberField":
                child_name = "OCT"
                child_val = "0"
            elif typ == "playSubPan":
                child_name = "PAN"
                child_val = "C"
                child_norm = 0.5
            elif typ == "playSubLevel":
                child_name = "LEVEL"
                child_val = "0.0 dB"
                child_norm = 0.75
            elif typ == "playEnvLfoChoice SHAPE":
                child_name = "SHAPE"
                child_val = "Sine"
            elif typ in ("compactNativeSmoothWithIndicator", "compactNativeSmoothTime", "compactNativeSmoothReadout"):
                child_name = "SMOOTH"
                child_val = FIXTURE_PARAMS.get("Parameter 2698", {}).get("value", "150 ms")
                child_norm = FIXTURE_PARAMS.get("Parameter 2698", {}).get("norm", 0.2)
            elif typ in ("playEnvLfoRate", "lfoNativeUni"):
                child_name = "RATE"
                child_val = "1.00 Hz"
                child_norm = FIXTURE_PARAMS["lfo.rate"]["norm"]

            for child in def_data.get("componentsData", []):
                self.draw_entry(local, child, (0, 0), parent_size=(w, h),
                                param_name=child_name, param_val=child_val,
                                norm_val=child_norm, remap_ctx=merged_remap)
            img.alpha_composite(local, (x0, y0))

        elif typ == "Label":
            kind = data.get("type", "Text")
            ts = data.get("textStyle", {})
            colour = preview_mod.argb(ts.get("colour")) or (255, 255, 255, 255)
            font_info = ts.get("font", {})
            style = font_info.get("style", "Regular")
            height = font_info.get("height", 16)
            just = ts.get("justification", "left")
            case = ts.get("case", "Original")

            if kind == "Text":
                txt = data.get("text", "")
            elif kind == "Name":
                txt = param_name or cd.get("name", "") or "NAME"
            elif kind == "Value":
                txt = param_val or "0.0"
            else:
                txt = param_name or ""

            if txt:
                self.text(img, (x0, y0, w, h), txt, style, height, colour, just, case)

        elif typ == "Knob":
            knob_type = data.get("knobType", "")
            if knob_type != "FilmStrip":
                raise RuntimeError(
                    f"Non-FilmStrip Knob ({knob_type!r}) in Play skin; refusing factory fallback"
                )
            film_strip = data.get("filmStrip")
            if not film_strip:
                raise RuntimeError("FilmStrip Knob is missing filmStrip asset name")
            num_frames = int(data.get("numFrames", 127))
            knob_im = self.get_knob_frame(film_strip, norm_val, (w, h), num_frames=num_frames)
            if knob_im:
                kx = x0 + (w - knob_im.width) // 2
                ky = y0 + (h - knob_im.height) // 2
                img.alpha_composite(knob_im, (kx, ky))

        elif typ == "Button":
            # Check active state
            btn_name = cd.get("name", "")
            handle_name = data.get("handleName", "")
            handle_ref = merged_remap.get(handle_name, "")
            on = False
            if handle_ref in FIXTURE_STATE:
                on = (FIXTURE_STATE[handle_ref] == 1)
            elif "power" in btn_name.lower():
                on = FIXTURE_STATE.get("Parameter 1558", 0) == 1
            elif "sync hz" in btn_name.lower():
                on = FIXTURE_STATE.get("Parameter 3302", 0) == 1
            elif "sync bpm" in btn_name.lower() or "sync key" in btn_name.lower():
                on = False
            elif handle_name in ("Active", "State") or "smooth" in btn_name.lower():
                on = (FIXTURE_STATE.get("Parameter 2699", 1) == 1)

            img_key = "onImage" if on else "offImage"
            btn_img_name = data.get(img_key) or data.get("offImage")
            im = self.image(btn_img_name) if btn_img_name else None
            if im:
                bx = x0 + (w - im.width) // 2
                by = y0 + (h - im.height) // 2
                img.alpha_composite(im, (bx, by))

        elif typ == "Image":
            img_name = data.get("image")
            im = self.image(img_name) if img_name else None
            if im:
                if data.get("imageType") == "FilmStrip":
                    num_frames = max(1, int(data.get("numFrames", 1)))
                    handle_name = data.get("handleName", "Data")
                    handle_ref = merged_remap.get(handle_name, "")
                    frame_idx = 0
                    if handle_ref in FIXTURE_STATE:
                        frame_idx = int(FIXTURE_STATE[handle_ref])
                    elif handle_ref in FIXTURE_PARAMS:
                        frame_idx = int(round(FIXTURE_PARAMS[handle_ref].get("norm", 0.0) * (num_frames - 1)))
                    frame_idx = max(0, min(num_frames - 1, frame_idx))
                    frame_h = max(1, im.height // num_frames)
                    im = im.crop((0, frame_idx * frame_h, im.width, (frame_idx + 1) * frame_h))
                if w > 0 and h > 0 and im.size != (w, h):
                    im = im.resize((w, h), Image.Resampling.LANCZOS)
                img.alpha_composite(im, (x0, y0))

    def render(self, tab_name="Play"):
        tab = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == tab_name)
        # Background: overlay_canvas.png if present, else dark solid
        bg_file = self.skins / "overlay_canvas.png"
        if bg_file.is_file():
            img = Image.open(bg_file).convert("RGBA")
        else:
            img = Image.new("RGBA", (preview_mod.TAB_W, preview_mod.TAB_H), (20, 26, 40, 255))

        for entry in tab["componentDefinition"]["componentsData"]:
            self.draw_entry(img, entry, (0, 0), parent_size=(preview_mod.TAB_W, preview_mod.TAB_H))

        # STRICTLY ZERO Q-Link frames or numbers (Rule 5b compliance)
        return img


def generate_snapshot(out_path, smooth_on=True):
    """Generate the authentic native snapshot image to out_path."""
    out_path = Path(out_path).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    check_sdk()
    if not smooth_on:
        FIXTURE_STATE["Parameter 2699"] = 0
        FIXTURE_PARAMS["Parameter 2699"] = {"name": "Smooth", "value": "OFF", "norm": 0.0}
    else:
        FIXTURE_STATE["Parameter 2699"] = 1
        FIXTURE_PARAMS["Parameter 2699"] = {"name": "Smooth", "value": "ON", "norm": 1.0}
        FIXTURE_PARAMS["Parameter 2698"] = {"name": "Time", "value": "150 ms", "norm": 0.2}
    tmp_dir = Path(tempfile.mkdtemp(prefix="play-snapshot-"))
    try:
        ui_dir = generate_ui(tmp_dir)
        skins_dir = generate_skins(tmp_dir, ui_dir)
        apply_extras(skins_dir, ui_dir)

        sketch = PlaySketch(skins_dir, json.loads(PARAMS_JSON.read_text())["params"])
        snapshot = sketch.render("Play")
        snapshot.convert("RGB").save(out_path, optimize=True)
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)
    return out_path


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Generate an authentic offline native-layer snapshot of the Play tab of an MPC skin. "
                    "Produces a single 1280x628 PNG snapshot of the native UI layer.")
    ap.add_argument("--out", required=True,
                    help="Output path for the PNG snapshot (must be an absolute path)")
    ap.add_argument("--smooth-off", action="store_true",
                    help="Render snapshot with Smooth turned OFF")
    args = ap.parse_args(argv)

    out_path = Path(args.out)
    if not out_path.is_absolute():
        print("render_play_native_snapshot.py: error: --out must be an absolute path", file=sys.stderr)
        sys.exit(1)

    generate_snapshot(out_path, smooth_on=not args.smooth_off)
    print(out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
