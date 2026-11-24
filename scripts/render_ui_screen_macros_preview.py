#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Renders authentic 1280x628 preview of the standalone Macros screen.

Composites the actual generated native host skin layer with the authentic C++ overlay:
1. Native Host Skin Layer (1280 x 628 px):
   - Genuine tab background (overlay_bg_macros.png) with static pass
   - 8 compact native rotary dials and readouts / MacroCard components in one bottom row
   - 8 standalone action buttons ("TARGETS")
   - Overlay sentinel / instance markers
2. Dynamic Overlay Layer (1280 x 628 px):
   - Rendered by the real C++ MacroView / SpectrumStrip scene renderer (scene_cpu.h)
   - Clean header bar with "MACROS" title in theme font (no header bank switcher)
   - Upper Studio Spectrum Strip: live curve stroke and shaded fill rendered by ui::SpectrumStrip
     from an explicit synthetic audio spectrum fixture across the full width
   - Hardware state: MPC XL wide hardware suppresses bank frame (wideDevice == true);
     Key 61 hardware renders active 4-card bank frame with single unnumbered 'Q' chip marker
   - Marker covers: opaque patches covering the native marker pixels
3. Composite:
   - Pixel-accurate alpha composite: Image.alpha_composite(skin_canvas, overlay_image)
   - CLI --out operates with zero side effects (mirror updates require explicit --update-mirrors)
"""
from screen_images import screen_path, save_screen, export_path

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = ROOT / "scripts"
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
SITE_PACKAGES = Path("/Users/szabi/Library/Python/3.9/lib/python/site-packages")
if SITE_PACKAGES.is_dir() and str(SITE_PACKAGES) not in sys.path:
    sys.path.insert(0, str(SITE_PACKAGES))

from PIL import Image, ImageDraw

DEFAULT_OUTPUT = screen_path('macros')
CANONICAL_MIRRORS = []  # One canonical screenshot; no mirrored copies.

FIXTURES = {
    "default": {
        "names": ["MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"],
        "values": [0.35, 0.70, 0.12, 0.95, 0.50, 0.25, 0.80, 0.40],
        "value_texts": ["35.0%", "70.0%", "12.0%", "95.0%", "50.0%", "25.0%", "80.0%", "40.0%"],
    },
    "named": {
        "names": ["CUTOFF", "RESO", "DRIVE", "WARP", "SPACE", "ATTACK", "DECAY", "RATE"],
        "values": [0.50, 0.75, 0.45, 0.85, 0.60, 0.30, 0.15, 0.90],
        "value_texts": ["50.0%", "75.0%", "45.0%", "85.0%", "60.0%", "30.0%", "15.0%", "90.0%"],
    },
    "zero": {
        "names": ["MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"],
        "values": [0.0] * 8,
        "value_texts": ["0.0%"] * 8,
    },
}


def resolve_fixture(fixture_name="default", custom_names=None, custom_values=None):
    base = FIXTURES.get(fixture_name, FIXTURES["default"])
    names = list(base["names"])
    values = list(base["values"])
    value_texts = list(base["value_texts"])
    if custom_names:
        for i, n in enumerate(custom_names[:8]):
            names[i] = str(n)
    if custom_values:
        for i, v in enumerate(custom_values[:8]):
            fv = float(v)
            if fv > 1.0:
                fv /= 100.0
            values[i] = max(0.0, min(1.0, fv))
            value_texts[i] = f"{values[i] * 100.0:.1f}%"
    return {"names": names, "values": values, "value_texts": value_texts}


def get_sdk_path() -> Path:
    sdk_env = os.environ.get("MPC_SDK")
    if sdk_env and Path(sdk_env).is_dir():
        return Path(sdk_env)
    user_sdk = Path("/Users/szabi/.local/share/local-worker/ext/mpc-sdk")
    if user_sdk.is_dir():
        return user_sdk
    default_sdk = Path.home() / ".local/share/local-worker/ext/mpc-sdk"
    if default_sdk.is_dir():
        return default_sdk
    raise FileNotFoundError("MPC SDK directory not found; set MPC_SDK environment variable")


def ensure_env():
    if "MPC_SDK" not in os.environ:
        os.environ["MPC_SDK"] = str(get_sdk_path())


def compute_overlay_tool_hash() -> str:
    """Computes SHA256 content key from fixture source, dependent headers, and workspace path."""
    h = hashlib.sha256()
    source_file = ROOT / "tools" / "render_macros_overlay.cpp"
    if source_file.is_file():
        h.update(source_file.read_bytes())
    for rel in [
        "shell-mpc/overlay/native_marker_covers.h",
        "shell-mpc/overlay/overlay_layout.h",
        "shell-mpc/overlay/page_view_base.h",
        "shell-mpc/overlay/scene_cpu.h",
        "shell-mpc/overlay/macro_ui.h",
        "shell-mpc/ui/macro_card_contract.h",
        "shell-mpc/ui/mod_macros.h",
        "shell-mpc/ui/base.h",
        "shell-mpc/ui/core.h",
        "shell-mpc/ui/theme.h",
        "shell-mpc/ui/qgroup.h",
    ]:
        p = ROOT / rel
        if p.is_file():
            h.update(p.read_bytes())
    h.update(str(ROOT.resolve()).encode("utf-8"))
    return h.hexdigest()[:16]


def build_overlay_tool(cache_dir: Path = None) -> Path:
    """Compiles the narrow C++ Macros overlay render fixture using isolated content-key caching."""
    source_file = ROOT / "tools" / "render_macros_overlay.cpp"
    if not source_file.is_file():
        raise FileNotFoundError(f"Source fixture not found: {source_file}")

    content_key = compute_overlay_tool_hash()
    if cache_dir is None:
        cache_dir = ROOT / ".hw-local" / "tool-cache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    bin_path = cache_dir / f"render_macros_overlay_{content_key}"

    if not bin_path.is_file() or not os.access(bin_path, os.X_OK):
        cxx = os.environ.get("CXX", "c++")
        cmd = [
            cxx, "-std=c++17", "-O2",
            "-I", str(ROOT),
            "-I", str(ROOT / "core" / "include"),
            str(source_file),
            "-o", str(bin_path),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"Failed to compile {source_file}:\n{res.stderr}")

    return bin_path


def make_macros_sketch_class(sketch_mod, fixture_data=None):
    fd = fixture_data or FIXTURES["default"]

    class MacrosSketch(sketch_mod.Sketch):
        """Handle-aware MPC skin renderer implementing authentic production MacroCard contract.

        1. Propagates handle remappings across nested component definitions (e.g. macroCard -> playSubLevel).
        2. Resolves Data and Caption handles (WithName[...]/Parameter N).
        3. Respects production MacroCard/MacroUi aperture ownership:
           - Native host skin owns the rotary dial (knob), value readout (value), and standalone target button.
           - Overlay owns the interactive macro name button (nameRect), so the host skin leaves that aperture
             clean without drawing duplicate or unmapped fallback text beneath the overlay.
        4. Real Fixture-State Support:
           - Renders authentic non-zero parameter values (filmstrip rotation frame and value text)
             matching the active fixture, preventing state resolution defects from being masked by 0.0.
        """

        def __init__(self, skins, params):
            super().__init__(skins, params)
            self.fixture_names = list(fd["names"])
            self.fixture_values = list(fd["values"])
            self.fixture_value_texts = list(fd["value_texts"])

        def draw_entry(self, img, entry, origin, param_name=None, remap_context=None, macro_index=None):
            cd = entry["componentData"]
            b = entry["bounds"]
            if not sketch_mod.visible_by_default(b):
                return
            x0, y0, w, h = sketch_mod.rect(b["bounds"])
            x0, y0 = x0 + origin[0], y0 + origin[1]
            typ, data = cd["type"], cd["data"]

            entry_remap = {m["key"]: m["value"] for m in entry.get("handle remapping", {}).get("map", [])}
            active_remap = dict(remap_context or {})
            for k, v in entry_remap.items():
                if v in active_remap:
                    active_remap[k] = active_remap[v]
                else:
                    active_remap[k] = v

            cur_macro_idx = macro_index
            cname = cd.get("name", "")
            m_match = re.search(r"Macro\s*(\d+)", cname, re.IGNORECASE)
            if m_match:
                cur_macro_idx = int(m_match.group(1)) - 1

            if typ in self.defs:
                data_h = active_remap.get("Data")
                idx = sketch_mod._param_index(data_h) if data_h else None
                if idx is not None and idx < len(self.params):
                    pname = self.params[idx]["name"]
                    pkey = self.params[idx].get("key", "")
                    if pkey.startswith("macro."):
                        try:
                            cur_macro_idx = int(pkey.split(".")[1]) - 1
                        except (IndexError, ValueError):
                            pass
                else:
                    pname = param_name

                for child in self.defs[typ]["componentsData"]:
                    self.draw_entry(img, child, (x0, y0), pname, active_remap, cur_macro_idx)
            elif typ == "Label":
                kind = data["type"]
                ts = data["textStyle"]
                if kind == "Name":
                    # In production MacroCard / MacroUi, the macro name button is exclusively
                    # owned by the overlay layer (MacroCard::nameRect, MacroUi::covers),
                    # which draws the active name in theme font with an interactive button underline.
                    # To prevent double-ownership and blurred/doubled text, the host skin leaves
                    # the name aperture clear for the overlay pass.
                    return
                elif kind == "Value":
                    # Production value readout: format parameter value from fixture
                    if cur_macro_idx is not None and 0 <= cur_macro_idx < len(self.fixture_value_texts):
                        txt = self.fixture_value_texts[cur_macro_idx]
                    else:
                        txt = "0.0%"
                    colour = sketch_mod.argb(ts["colour"]) or (255, 255, 255, 255)
                    self.text(img, (x0, y0, w, h), txt, ts["font"]["style"], ts["font"]["height"], colour, ts["justification"], ts["case"])
                elif kind == "Text":
                    txt = data.get("text", "")
                    colour = sketch_mod.argb(ts["colour"]) or (255, 255, 255, 255)
                    self.text(img, (x0, y0, w, h), txt, ts["font"]["style"], ts["font"]["height"], colour, ts["justification"], ts["case"])
            elif typ in ("Knob", "Image"):
                key = "filmStrip" if typ == "Knob" else "image"
                name = data.get(key)
                frames = data.get("numFrames", 1) if data.get("knobType", data.get("imageType")) in ("FilmStrip",) else 1
                if typ == "Knob" and cur_macro_idx is not None and 0 <= cur_macro_idx < len(self.fixture_values) and frames > 1:
                    norm_val = self.fixture_values[cur_macro_idx]
                    frame_idx = max(0, min(frames - 1, round(norm_val * (frames - 1))))
                    strip_path = self.skins / name
                    if strip_path.is_file():
                        strip_im = Image.open(strip_path).convert("RGBA")
                        frame_h = strip_im.height // frames
                        frame_im = strip_im.crop((0, frame_idx * frame_h, strip_im.width, (frame_idx + 1) * frame_h))
                        img.alpha_composite(frame_im, (x0, y0))
                    else:
                        im = self.image(name, frames)
                        if im is not None:
                            img.alpha_composite(im, (x0, y0))
                else:
                    im = self.image(name, frames) if name else None
                    if im is not None:
                        img.alpha_composite(im, (x0, y0))
            elif typ == "Button":
                im = self.image(data.get("offImage"))
                if im is not None:
                    img.alpha_composite(im, (x0, y0))
            elif typ == "Slider":
                d = ImageDraw.Draw(img)
                d.rounded_rectangle([x0, y0, x0 + w, y0 + h], 5, outline=(106, 106, 108, 255), fill=(38, 37, 40, 255))
                name = (param_name or "").upper()
                d.text((x0 + 8, y0 + 4), name, font=sketch_mod.font("Bold", 22), fill=(180, 180, 184, 255))
            elif typ in ("Envelope",):
                d = ImageDraw.Draw(img)
                d.rectangle([x0, y0, x0 + w, y0 + h], outline=(211, 17, 69, 255))
                d.text((x0 + 8, y0 + 4), "ENVELOPE", font=sketch_mod.font("Bold", 20), fill=(211, 17, 69, 255))

    return MacrosSketch


def render_overlay_layer(
    out_overlay_path: Path,
    bank: int = 0,
    device: str = "xl",
    fixture: str = "default",
    custom_names: list = None,
    custom_values: list = None,
    cache_dir: Path = None,
) -> Image.Image:
    """Renders the authentic C++ overlay layer using the real MacroView and SpectrumStrip."""
    tool_bin = build_overlay_tool(cache_dir)
    out_overlay_path = Path(out_overlay_path)
    out_overlay_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(tool_bin),
        "--out", str(out_overlay_path),
        "--bank", str(bank),
        "--device", str(device),
        "--mode", "live",
        "--native-controls", "1",
        "--native-targets", "1",
        "--fixture", str(fixture),
    ]
    if custom_names:
        cmd.extend(["--macro-names", ",".join(str(n) for n in custom_names)])
    if custom_values:
        cmd.extend(["--macro-values", ",".join(str(v) for v in custom_values)])

    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"render_macros_overlay failed:\n{res.stderr}")

    if not out_overlay_path.is_file():
        raise FileNotFoundError(f"Overlay output file not found: {out_overlay_path}")

    return Image.open(out_overlay_path).convert("RGBA")


def render_macros_preview(
    out_path: Path = DEFAULT_OUTPUT,
    skins_dir: Path = None,
    bank: int = 0,
    device: str = "xl",
    fixture: str = "default",
    custom_names: list = None,
    custom_values: list = None,
    update_mirrors: bool = False,
) -> Path:
    """Renders the authentic full-screen Macros screen composite from generated skin + C++ overlay."""
    ensure_env()

    sys.path.insert(0, str(SCRIPTS_DIR))
    sys.path.insert(0, str(SCRIPTS_DIR / "tests"))

    import test_mpc_skin_extras as base
    import test_mpc_skin_no_fallback as nfb

    spec = importlib.util.spec_from_file_location("render_sketch", str(SCRIPTS_DIR / "render-mpc-skin-preview.py"))
    sketch_mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sketch_mod)

    fixture_data = resolve_fixture(fixture, custom_names, custom_values)

    def _render_skin_canvas(skins_path: Path) -> Image.Image:
        params_file = skins_path / "params.json"
        if not params_file.is_file():
            params_file = base.PARAMS_JSON
        params_data = json.loads(params_file.read_text(encoding="utf-8"))
        params_list = params_data["params"] if "params" in params_data else params_data

        MacrosSketch = make_macros_sketch_class(sketch_mod, fixture_data)
        sketch = MacrosSketch(skins_path, params_list)

        bg_path = skins_path / "overlay_bg_macros.png"
        if not bg_path.is_file():
            bg_path = SCRIPTS_DIR / "mpc-skin-extras" / "overlay-backgrounds" / "overlay_bg_macros.png"
        canvas = Image.open(bg_path).convert("RGBA")

        tab = next((t for t in sketch.tui["pageData"]["tabs"] if t["tabName"] == "Macros"), None)
        if tab is not None:
            for entry in tab["componentDefinition"]["componentsData"]:
                sketch.draw_entry(canvas, entry, (0, 0))

        return canvas

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        if skins_dir is not None and Path(skins_dir).is_dir():
            skin_canvas = _render_skin_canvas(Path(skins_dir))
        else:
            skins = base.generate_skins(tmp_dir / "gen_skins", nfb.OVERLAY_LAYOUT)
            code, err = base.run_apply(skins, base.REAL_EXTRAS)
            if code != 0:
                raise RuntimeError(f"apply-mpc-skin-extras failed (exit {code}):\n{err}")
            skin_canvas = _render_skin_canvas(skins)

        overlay_tmp_path = tmp_dir / "overlay.png"
        overlay_image = render_overlay_layer(
            out_overlay_path=overlay_tmp_path,
            bank=bank,
            device=device,
            fixture=fixture,
            custom_names=custom_names,
            custom_values=custom_values,
            cache_dir=tmp_dir / "tool",
        )

        final_canvas = Image.alpha_composite(skin_canvas, overlay_image)

    out_file = Path(out_path)
    out_file.parent.mkdir(parents=True, exist_ok=True)
    save_screen(final_canvas, out_file)
    print(f"Generated authentic Macros preview: {out_file} ({final_canvas.width}x{final_canvas.height})")

    return out_file


def main():
    parser = argparse.ArgumentParser(description="Render authentic Macros screen preview from generated skin + C++ overlay.")
    parser.add_argument("--out", type=Path, default=None, help="Output PNG path (no mirror updates if set)")
    parser.add_argument("--skins", type=Path, default=None, help="Pre-generated Plugin Skins folder")
    parser.add_argument("--bank", type=int, default=0, choices=[0, 1], help="Active Q-Link bank (0: Bank 1, 1: Bank 2)")
    parser.add_argument("--device", type=str, default="xl", choices=["xl", "key61"], help="Target device (xl: 16 Q-Links, no bank frame; key61: 4 Q-Links with bank frame)")
    parser.add_argument("--fixture", type=str, default="default", choices=["default", "named", "zero"], help="Macro fixture preset (default: non-zero numbered; named: sound design names; zero: baseline 0.0)")
    parser.add_argument("--macro-names", type=str, default=None, help="Comma-separated custom macro names")
    parser.add_argument("--macro-values", type=str, default=None, help="Comma-separated custom macro values (0.0..1.0 or 0..100)")
    parser.add_argument("--update-mirrors", action="store_true", help="Explicitly update canonical repository images")
    args = parser.parse_args()

    custom_names = [n.strip() for n in args.macro_names.split(",")] if args.macro_names else None
    custom_values = [float(v.strip()) for v in args.macro_values.split(",")] if args.macro_values else None

    alternate = args.bank != 0 or args.device != "xl" or args.fixture != "default" or custom_names or custom_values
    if alternate and (args.out is None or Path(args.out).resolve().is_relative_to(ROOT)):
        parser.error("Alternative macro states require an external --out path")
    out_path = args.out if args.out is not None else DEFAULT_OUTPUT
    render_macros_preview(
        out_path=out_path,
        skins_dir=args.skins,
        bank=args.bank,
        device=args.device,
        fixture=args.fixture,
        custom_names=custom_names,
        custom_values=custom_values,
        update_mirrors=args.update_mirrors,
    )


if __name__ == "__main__":
    main()
