#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Render full-screen authentic composite preview for the Play screen.

Generates authentic composite screen preview on standalone Akai MPC hardware (1280 x 628 tab):
- Host-native Play skin layer generated from TUI.json via PlaySketch in render_play_native_snapshot.py:
  * 3 module selectors (Play Osc Select -> SUB, Play Env Select -> ENV 1, Play LFO Select -> LFO 1)
  * Sub controls: WAVE ("SINE"), OCT ("0"), 90 px Sub PAN ("C") & LEVEL ("0.0 dB") dials
  * Env controls: Attack, Decay, Sustain, Release compact dials
  * LFO controls: SHAPE ("SINE"), RATE ("1.00 Hz"), HZ/BPM/KEY mode buttons, Rise, Smooth Time & switch
  * Native action buttons (Sub power ON, Sub expand, Env expand, LFO expand)
- Real today C++ overlay layer from PlayView + scene_cpu.h (compiled and executed via play_render_fixture):
  * Internal Sub waveform cycle plot
  * Env 1 ADSR envelope curve plot
  * LFO 1 sine shape plot
  * Strictly identical documented fixture state: Sub active, Env 1 active, LFO 1 active
- Strictly zero visible Q-Link numbers or frames (Rule 5b)
- Strictly zero phantom badges or watermark badges
- Historical PNG is NEVER used as an operational input.
"""
from screen_images import screen_path, save_screen, export_path

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
OUT_DIR = REPO_ROOT / "docs" / "ui-components" / "komponensek"
UI_MAP_DIR = REPO_ROOT / "docs" / "ui-map"
CANONICAL_KOMP = screen_path('play')
CANONICAL_MAP = screen_path('play')

SNAPSHOT_SCRIPT = HERE / "render_play_native_snapshot.py"
FIXTURE_SRC = REPO_ROOT / "shell-mpc" / "overlay" / "tests" / "play_render_fixture.cpp"

# Display panel boxes from play_rows.h (tab coordinates, y=74..366)
DISPLAY_BOXES = [
    (14, 74, 422, 366),    # Oscillator display (408 x 292)
    (446, 74, 834, 366),   # Envelope display (388 x 292)
    (864, 74, 1260, 366),  # LFO display (396 x 292)
]

# Native host action buttons inside display header bands (cleared as apertures by C++ overlay)
NATIVE_BUTTON_APERTURES = [
    (18, 82, 82, 138),     # Sub power (64 x 56)
    (354, 82, 418, 138),   # Sub expand (64 x 56)
    (766, 82, 830, 138),   # Env expand (64 x 56)
    (1192, 82, 1256, 138), # LFO expand (64 x 56)
]

REQUIRED_CORE_LIBS = [
    "core/libsynthyboy_core.a",
    "core/shapes/libsynthyboy_shapes.a",
    "core/multisample/libsynthyboy_multisample.a",
    "core/sample-osc/libsynthyboy_sampleosc.a",
    "core/spectral/libsynthyboy_spectral.a",
    "core/granular/libsynthyboy_granular.a",
    "core/wavetable-edit/libsynthyboy_wtedit.a",
    "core/filters-serum/libsynthyboy_filters_serum.a",
    "core/fx-serum-a/libsynthyboy_fx_serum_a.a",
    "core/fx-serum-b/libsynthyboy_fx_serum_b.a",
]


def _posix_cksum(text: str) -> int:
    """Compute POSIX cksum matching scripts/build.sh and shell-mpc/accept.sh."""
    data = (text + "\n").encode("utf-8")
    crc = 0
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if (crc & 0x80000000) else ((crc << 1) & 0xFFFFFFFF)
    length = len(data)
    while length:
        crc ^= (length & 0xFF) << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if (crc & 0x80000000) else ((crc << 1) & 0xFFFFFFFF)
        length >>= 8
    return (~crc) & 0xFFFFFFFF


def default_fixture_build_dir() -> Path:
    """Deterministic per-checkout build directory for synthyboy_core and play_render_fixture."""
    uid = os.getuid() if hasattr(os, "getuid") else 0
    ck = _posix_cksum(str(REPO_ROOT.resolve()))
    return Path(f"/tmp/synthyboy-{uid}/{ck}/play-fixture-core")


def verify_build_dir_source(build_dir: Path) -> None:
    """Ensure CMakeCache.txt in build_dir (if present) belongs to this checkout."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return
    for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
            src = Path(line.split("=", 1)[1].strip()).resolve()
            if src != REPO_ROOT.resolve():
                raise RuntimeError(
                    f"Build directory {build_dir} was configured from {src}, not {REPO_ROOT.resolve()}"
                )
            return


def resolve_build_dir(build_dir=None) -> Path:
    """Resolve explicit build directory (--build-dir or SYNTHYBOY_BUILD_DIR) or per-checkout default."""
    explicit = build_dir or os.environ.get("SYNTHYBOY_BUILD_DIR")
    return Path(explicit).resolve() if explicit else default_fixture_build_dir()


def find_core_libraries(build_dir=None, allow_incremental_build=True):
    """Locate (and if using the per-checkout build dir, incrementally build) core static libraries."""
    bdir = resolve_build_dir(build_dir)
    explicit = bool(build_dir or os.environ.get("SYNTHYBOY_BUILD_DIR"))
    verify_build_dir_source(bdir)

    def collect(d: Path):
        paths = [d / r for r in REQUIRED_CORE_LIBS]
        return paths if all(p.is_file() for p in paths) else None

    libs = collect(bdir)
    if libs is not None:
        return libs, bdir

    if explicit or not allow_incremental_build:
        raise RuntimeError(
            f"Core static libraries not found in explicit build directory: {bdir}"
        )

    # Build ONLY synthyboy_core (JUCE-free) incrementally for this checkout.
    bdir.mkdir(parents=True, exist_ok=True)
    if not (bdir / "CMakeCache.txt").is_file():
        cfg_cmd = [
            "cmake", "-S", str(REPO_ROOT), "-B", str(bdir),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DSYNTHYBOY_BUILD_PLUGIN=OFF",
            "-DSYNTHYBOY_BUILD_TESTS=OFF",
            "-DSYNTHYBOY_BUILD_MPC_SHELL=OFF",
        ]
        if subprocess.run(["which", "ninja"], capture_output=True).returncode == 0:
            cfg_cmd.extend(["-G", "Ninja"])
        res = subprocess.run(cfg_cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"CMake configure failed for {bdir}:\n{res.stderr or res.stdout}")

    bld_cmd = ["cmake", "--build", str(bdir), "--target", "synthyboy_core", "--parallel", "4"]
    res = subprocess.run(bld_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Incremental build of synthyboy_core failed in {bdir}:\n{res.stderr or res.stdout}")

    libs = collect(bdir)
    if libs is None:
        raise RuntimeError(f"Core static libraries still missing after build in {bdir}")
    return libs, bdir


def _latest_source_mtime(libs) -> float:
    mtimes = [FIXTURE_SRC.stat().st_mtime, *(lib.stat().st_mtime for lib in libs)]
    contract_hdr = REPO_ROOT / "shell-mpc" / "play_osc_native_contract.h"
    if contract_hdr.is_file():
        mtimes.append(contract_hdr.stat().st_mtime)
    for folder in (REPO_ROOT / "shell-mpc" / "overlay", REPO_ROOT / "shell-mpc" / "ui"):
        if folder.is_dir():
            for hdr in folder.glob("*.h"):
                mtimes.append(hdr.stat().st_mtime)
    return max(mtimes)


def build_fixture_binary(bin_path: Path, libs):
    """Compile shell-mpc/overlay/tests/play_render_fixture.cpp into an executable."""
    if not FIXTURE_SRC.is_file():
        raise FileNotFoundError(f"Fixture source not found: {FIXTURE_SRC}")

    bin_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        "c++", "-std=c++17", "-O1",
        "-I", str(REPO_ROOT),
        "-I", str(REPO_ROOT / "core" / "include"),
        "-I", str(REPO_ROOT / "core" / "filters-serum" / "include"),
        "-I", str(REPO_ROOT / "core" / "shapes" / "include"),
        "-I", str(REPO_ROOT / "shell-mpc"),
        "-I", str(REPO_ROOT / "shell-mpc" / "overlay"),
        "-I", str(REPO_ROOT / "shell-mpc" / "ui"),
        str(FIXTURE_SRC),
        *[str(l) for l in libs],
        "-lpthread",
        "-o", str(bin_path),
    ]

    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Failed to compile play_render_fixture:\n{res.stderr or res.stdout}")

    return bin_path


def render_cpp_overlay(out_png, build_dir=None, smooth_on=True):
    """Execute the live C++ PlayView + scene_cpu.h renderer to produce the tab overlay."""
    libs, bdir = find_core_libraries(build_dir=build_dir)
    bin_path = bdir / "play_render_fixture"
    if not bin_path.is_file() or bin_path.stat().st_mtime < _latest_source_mtime(libs):
        build_fixture_binary(bin_path, libs)

    cmd = [str(bin_path), "--out", str(out_png), "--tab-only"]
    if not smooth_on:
        cmd.append("--smooth-off")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"play_render_fixture failed:\n{res.stderr or res.stdout}")

    if not Path(out_png).is_file():
        raise FileNotFoundError(f"Fixture failed to produce output at {out_png}")


def render_play_screen_preview(out_path=None, canonical=False, build_dir=None, smooth_on=True):
    """Render the full-screen composite preview for Play screen.

    If out_path is specified:
        Saves ONLY to out_path. Only that path is written; canonical does not create extra copies.
    If out_path is None:
        Updates only docs/screens/play.jpg.
    """
    with tempfile.TemporaryDirectory(prefix="play-preview-") as tmp_dir:
        tmp = Path(tmp_dir)
        native_png = tmp / "native_snap.png"
        overlay_png = tmp / "overlay_live.png"

        # 1. Generate current native skin snapshot from TUI.json (1280 x 628)
        snap_cmd = [sys.executable, str(SNAPSHOT_SCRIPT), "--out", str(native_png)]
        if not smooth_on:
            snap_cmd.append("--smooth-off")
        res = subprocess.run(snap_cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"render_play_screen_preview.py: error generating native snapshot:\n{res.stderr or res.stdout}", file=sys.stderr)
            sys.exit(1)
        native = Image.open(native_png).convert("RGBA")

        # 2. Run live C++ PlayView + scene_cpu.h overlay renderer (with native_marker_covers)
        render_cpp_overlay(overlay_png, build_dir=build_dir, smooth_on=smooth_on)
        overlay_tab = Image.open(overlay_png).convert("RGBA")

        # 3. Authentic alpha composite: native host layer underneath, real C++ overlay on top.
        # Transparent apertures in overlay_tab expose the native selectors, dials, and buttons.
        composite = Image.alpha_composite(native, overlay_tab)
        result_img = composite.convert("RGB")

        if not smooth_on and out_path is None:
            raise ValueError("Alternate Smooth state requires an external --out path")
        target = Path(out_path) if out_path is not None else screen_path("play")
        if not smooth_on and target.resolve().is_relative_to(REPO_ROOT):
            raise ValueError("Alternate Smooth state requires an external --out path")
        save_screen(result_img, target)
        print(f"Saved Play preview: {target} ({result_img.size})")
        return target


def main():
    ap = argparse.ArgumentParser(description="Render full-screen authentic composite preview for the Play screen.")
    ap.add_argument("--out", default=None, help="Output path (custom path does not modify canonical previews)")
    ap.add_argument("--canonical", action="store_true", help="Explicitly update canonical preview files")
    ap.add_argument("--build-dir", default=None, help="Explicit CMake build directory for core static libraries")
    ap.add_argument("--smooth-off", action="store_true", help="Render composite with Smooth turned OFF")
    args = ap.parse_args()
    render_play_screen_preview(args.out, canonical=args.canonical, build_dir=args.build_dir, smooth_on=not args.smooth_off)


if __name__ == "__main__":
    main()
