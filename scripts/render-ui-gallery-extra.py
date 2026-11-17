#!/usr/bin/env python3
# SynthyBoy - MPC overlay: UI gallery extra renderer for offline visual reviews.
# Renders all extra pages, popups, and states not covered by the main gallery at 1280x800,
# producing PNG images and an index.html contact sheet.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import argparse
import html
import os
import subprocess
import sys
from pathlib import Path

DEFAULT_PRESETS = [
    "content/presets/Bass/scream_filter_bass.sbpreset",
]

BUILDSLOT = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh")


def find_repo_root():
    cur = Path(__file__).resolve().parent
    while cur != cur.parent:
        if (cur / "shell-mpc" / "overlay" / "overlay_layout.json").is_file():
            return cur
        cur = cur.parent
    return Path.cwd()


def find_or_build_host(repo_root, custom_bin=None, build_dir=None):
    if custom_bin and os.path.isfile(custom_bin) and os.access(custom_bin, os.X_OK):
        return Path(custom_bin)

    candidate_dirs = []
    if build_dir:
        candidate_dirs.append(Path(build_dir).resolve())
    candidate_dirs.extend([
        repo_root / "build",
        repo_root / "build-test",
        repo_root / "build-mpc",
        repo_root / "build-native",
    ])

    # Find configured build directory
    target_build = None
    for bdir in candidate_dirs:
        if (bdir / "CMakeCache.txt").is_file() or (bdir / "build.ninja").is_file():
            target_build = bdir
            break

    if not target_build:
        target_build = candidate_dirs[0] if candidate_dirs else repo_root / "build"
        target_build.mkdir(parents=True, exist_ok=True)
        config_cmd = [
            "cmake", "-S", str(repo_root), "-B", str(target_build),
            "-DCMAKE_BUILD_TYPE=Release", "-DSYNTHYBOY_BUILD_MPC_SHELL=ON"
        ]
        if BUILDSLOT.is_file() and os.access(BUILDSLOT, os.X_OK):
            config_cmd = ["bash", str(BUILDSLOT)] + config_cmd
        print(f"Configuring ui_gallery_extra_host in {target_build}...")
        subprocess.run(config_cmd, check=True)

    # Always build the target before running it
    build_cmd = ["cmake", "--build", str(target_build), "--target", "ui_gallery_extra_host", "-j4"]
    if BUILDSLOT.is_file() and os.access(BUILDSLOT, os.X_OK):
        build_cmd = ["bash", str(BUILDSLOT)] + build_cmd

    print(f"Building ui_gallery_extra_host in {target_build}...")
    res = subprocess.run(build_cmd)
    if res.returncode != 0:
        raise RuntimeError(f"Failed to build ui_gallery_extra_host (exit code {res.returncode})")

    for sub in ["shell-mpc", ""]:
        bin_path = target_build / sub / "ui_gallery_extra_host"
        if bin_path.is_file() and os.access(bin_path, os.X_OK):
            return bin_path

    raise RuntimeError("Could not find built ui_gallery_extra_host binary.")


def parse_page_and_state(filename, states):
    stem = Path(filename).stem
    # Filename format: <state>_<page_and_variant>.png
    state = "Init"
    rest = stem
    for s in sorted(states, key=len, reverse=True):
        if stem.startswith(s + "_"):
            state = s
            rest = stem[len(s) + 1:]
            break
    else:
        parts = stem.split("_", 1)
        if len(parts) == 2:
            state, rest = parts[0], parts[1]

    title = rest.replace("_", " ").title()
    title = title.replace("Osca", "Osc A").replace("Oscb", "Osc B").replace("Oscc", "Osc C")
    title = title.replace("Fx ", "FX ").replace("Lfo", "LFO ").replace("Env", "Env ")
    title = title.replace("Bpm", "(BPM)").replace("Key", "(KEY)").replace("Lib", "(Library)")
    title = title.replace("Dest Popup", "(Dest Popup)").replace("Style List", "(Style List)")
    title = title.replace("Route", "(Route)").replace("Unison", "(Unison)")

    return state, title, rest


def generate_contact_sheet(out_dir, images_info):
    states = sorted(list(set(info["state"] for info in images_info)))

    html_lines = [
        "<!DOCTYPE html>",
        "<html lang='en'>",
        "<head>",
        "  <meta charset='UTF-8'>",
        "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>",
        "  <title>SynthyBoy MPC Overlay - UI Gallery Extra</title>",
        "  <style>",
        "    :root {",
        "      --bg0: #1e1d1e;",
        "      --bg1: #2a282a;",
        "      --card-bg: #262527;",
        "      --text: #eef1ff;",
        "      --text-dim: #aab2d8;",
        "      --accent: #ff2e63;",
        "      --border: #3d3b3e;",
        "    }",
        "    * { box-sizing: border-box; margin: 0; padding: 0; }",
        "    body {",
        "      background-color: var(--bg0);",
        "      color: var(--text);",
        "      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;",
        "      padding: 24px;",
        "    }",
        "    header {",
        "      margin-bottom: 24px;",
        "      padding-bottom: 16px;",
        "      border-bottom: 1px solid var(--border);",
        "    }",
        "    h1 { font-size: 24px; font-weight: 600; margin-bottom: 8px; }",
        "    .stats { color: var(--text-dim); font-size: 14px; margin-bottom: 16px; }",
        "    .filters { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 24px; }",
        "    .filter-btn {",
        "      background: var(--card-bg);",
        "      color: var(--text);",
        "      border: 1px solid var(--border);",
        "      padding: 6px 14px;",
        "      border-radius: 4px;",
        "      cursor: pointer;",
        "      font-size: 13px;",
        "      transition: background 0.15s, border-color 0.15s;",
        "    }",
        "    .filter-btn.active, .filter-btn:hover {",
        "      background: var(--accent);",
        "      border-color: var(--accent);",
        "      color: #fff;",
        "    }",
        "    .grid {",
        "      display: grid;",
        "      grid-template-columns: repeat(auto-fill, minmax(400px, 1fr));",
        "      gap: 20px;",
        "    }",
        "    .card {",
        "      background: var(--card-bg);",
        "      border: 1px solid var(--border);",
        "      border-radius: 6px;",
        "      overflow: hidden;",
        "      display: flex;",
        "      flex-direction: column;",
        "      box-shadow: 0 4px 12px rgba(0,0,0,0.3);",
        "    }",
        "    .card a { display: block; overflow: hidden; background: #000; }",
        "    .card img {",
        "      width: 100%;",
        "      height: auto;",
        "      display: block;",
        "      aspect-ratio: 1280 / 800;",
        "      object-fit: contain;",
        "      transition: transform 0.2s;",
        "    }",
        "    .card a:hover img { transform: scale(1.02); }",
        "    .card-info {",
        "      padding: 12px 14px;",
        "      display: flex;",
        "      justify-content: space-between;",
        "      align-items: center;",
        "      border-top: 1px solid var(--border);",
        "    }",
        "    .page-name { font-size: 14px; font-weight: 600; color: var(--text); }",
        "    .state-badge {",
        "      font-size: 11px;",
        "      font-weight: 500;",
        "      padding: 2px 8px;",
        "      border-radius: 10px;",
        "      background: rgba(255, 46, 99, 0.15);",
        "      color: var(--accent);",
        "      border: 1px solid var(--accent);",
        "    }",
        "  </style>",
        "</head>",
        "<body>",
        "  <header>",
        "    <h1>SynthyBoy MPC Overlay - UI Gallery Extra</h1>",
        f"    <div class='stats'>{len(images_info)} images across {len(states)} states (1280x800 reference CPU renderer)</div>",
        "    <div class='filters'>",
        "      <button class='filter-btn active' onclick='filterState(\"all\")'>All States</button>",
    ]

    for s in states:
        html_lines.append(f"      <button class='filter-btn' onclick='filterState(\"{html.escape(s)}\")'>{html.escape(s)}</button>")

    html_lines.extend([
        "    </div>",
        "  </header>",
        "  <div class='grid'>",
    ])

    for info in images_info:
        rel_path = info["rel_path"]
        page_title = html.escape(info["page_title"])
        state_name = html.escape(info["state"])
        html_lines.extend([
            f"    <div class='card' data-state='{state_name}'>",
            f"      <a href='{rel_path}' target='_blank'><img src='{rel_path}' alt='{page_title} - {state_name}' loading='lazy' /></a>",
            "      <div class='card-info'>",
            f"        <div class='page-name'>{page_title}</div>",
            f"        <div class='state-badge'>{state_name}</div>",
            "      </div>",
            "    </div>",
        ])

    html_lines.extend([
        "  </div>",
        "  <script>",
        "    function filterState(st) {",
        "      document.querySelectorAll('.filter-btn').forEach(btn => {",
        "        btn.classList.toggle('active', btn.textContent === st || (st === 'all' && btn.textContent === 'All States'));",
        "      });",
        "      document.querySelectorAll('.card').forEach(card => {",
        "        card.style.display = (st === 'all' || card.dataset.state === st) ? 'flex' : 'none';",
        "      });",
        "    }",
        "  </script>",
        "</body>",
        "</html>",
    ])

    index_file = Path(out_dir) / "index.html"
    index_file.write_text("\n".join(html_lines), encoding="utf-8")
    return index_file


def main():
    parser = argparse.ArgumentParser(description="SynthyBoy UI gallery extra renderer")
    parser.add_argument("--out", default="", help="Output directory for PNGs and index.html")
    parser.add_argument("--presets", nargs="*", default=[], help="Presets to render (.sbpreset files)")
    parser.add_argument("--host-bin", default="", help="Path to ui_gallery_extra_host binary")
    parser.add_argument("--build-dir", default="", help="Build directory")
    parser.add_argument("--list", action="store_true", help="Print STATE\\tPAGE\\tFILE lines (sorted by state, then page)")
    parser.add_argument("--only", default="", help="Comma-separated list of page-name beginnings to include in --list and contact sheet")
    args = parser.parse_args()

    repo_root = find_repo_root()

    if args.out:
        out_dir = Path(args.out).resolve()
    else:
        out_dir = (repo_root / "build" / "ui-gallery-extra").resolve()

    out_dir.mkdir(parents=True, exist_ok=True)

    # Locate or build host
    host_bin = find_or_build_host(repo_root, custom_bin=args.host_bin, build_dir=args.build_dir)
    print(f"Using host binary: {host_bin}")

    preset_paths = []
    if args.presets:
        for p in args.presets:
            p_path = Path(p)
            if not p_path.is_absolute():
                p_path = (repo_root / p).resolve()
            if p_path.is_file():
                preset_paths.append(str(p_path))
            else:
                print(f"Warning: preset not found: {p_path}", file=sys.stderr)
    else:
        for p in DEFAULT_PRESETS:
            p_path = (repo_root / p).resolve()
            if p_path.is_file():
                preset_paths.append(str(p_path))

    states = ["Init"] + [Path(p).stem for p in preset_paths]

    cmd = [str(host_bin), "--out", str(out_dir)]
    if preset_paths:
        cmd.append("--presets")
        cmd.extend(preset_paths)

    print(f"Running extra host: {' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=str(repo_root))
    if res.returncode != 0:
        print(f"Error: host binary exited with code {res.returncode}", file=sys.stderr)
        return res.returncode

    png_files = sorted(out_dir.glob("*.png"))
    print(f"Found {len(png_files)} rendered PNG images in {out_dir}")

    images_info = []
    for img in png_files:
        st, title, page_name = parse_page_and_state(img.name, states)
        images_info.append({
            "path": img,
            "rel_path": img.name,
            "state": st,
            "page_title": title,
            "page_name": page_name,
        })

    filtered_info = images_info
    if args.only:
        prefixes = [p for p in args.only.split(",") if p]
        if prefixes:
            filtered_info = [info for info in images_info
                             if any(info["page_name"].startswith(pfx) for pfx in prefixes)]

    if args.list:
        sorted_rows = sorted(filtered_info, key=lambda i: (i["state"], i["page_name"]))
        for info in sorted_rows:
            print(f"{info['state']}\t{info['page_name']}\t{info['rel_path']}")

    index_file = generate_contact_sheet(out_dir, filtered_info)
    print(f"Generated contact sheet: {index_file}")

    print(f"UI gallery extra completed successfully: {len(png_files)} images rendered.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
