#!/usr/bin/env python3
# SynthyBoy - MPC overlay: UI gallery renderer for reviews without a device.
# Renders every plugin-drawn page of the overlay build at 1280x800 in Init and factory presets,
# producing PNG images and an index.html contact sheet.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import argparse
import html
import json
import os
import subprocess
import sys
from pathlib import Path

DEFAULT_PRESETS = [
    "content/presets/Bass/scream_filter_bass.sbpreset",
    "content/presets/Pad/tri_band_split_texture.sbpreset",
    "content/presets/Pluck/aux_keyscale_pluck.sbpreset",
]


def find_preset_with_most_modulation_routes(repo_root):
    """Scan content/presets for the factory preset with the highest count of active unbypassed modulation routes."""
    presets_dir = Path(repo_root) / "content" / "presets"
    if not presets_dir.is_dir():
        return None
    best_path = None
    best_count = -1
    for p in sorted(presets_dir.glob("**/*.sbpreset")):
        try:
            with open(p, "r", encoding="utf-8") as f:
                d = json.load(f)
            params = d.get("state", {}).get("params", {})
            count = 0
            for slot in range(1, 65):
                pfx = f"mod{slot:02d}."
                src = params.get(pfx + "source", 0)
                dst = params.get(pfx + "destination", 0)
                amt = params.get(pfx + "amount", 0)
                bypass = params.get(pfx + "bypass", 0)
                if src > 0 and dst > 0 and not bypass and amt != 0:
                    count += 1
            if count > best_count:
                best_count = count
                best_path = p
        except Exception:
            continue
    return best_path


def find_repo_root():
    cur = Path(__file__).resolve().parent
    while cur != cur.parent:
        if (cur / "shell-mpc" / "overlay" / "overlay_layout.json").is_file():
            return cur
        cur = cur.parent
    return Path.cwd()


def load_layout_views(repo_root):
    layout_file = repo_root / "shell-mpc" / "overlay" / "overlay_layout.json"
    if not layout_file.is_file():
        raise FileNotFoundError(f"Layout file not found: {layout_file}")
    with open(layout_file, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("views", [])


def host_binary_in(bdir):
    for sub in ["shell-mpc", ""]:
        bin_path = Path(bdir) / sub / "ui_gallery_host"
        if bin_path.is_file() and os.access(bin_path, os.X_OK):
            return bin_path
    return None


def run_build(bdir):
    """The build step of a configured build directory: the ui_gallery_host target (an up to date binary costs one no-op ninja run). Returns the exit code."""
    print(f"Building ui_gallery_host in {bdir}...")
    return subprocess.run(["cmake", "--build", str(bdir), "--target", "ui_gallery_host", "-j4"]).returncode


def find_or_build_host(repo_root, custom_bin=None, build_dir=None, build=None):
    """The ui_gallery_host to run. A binary named with --host-bin is used as it is. Otherwise, when a build directory with a CMake cache is known (--build-dir, or one of the usual
    names under the repository), its target is ALWAYS built first, so the pictures are never those of a binary older than the sources (an existing binary was used without a rebuild
    before: stale pictures after a merge); a failing build is an error, never a silent fall back to the old binary. Without any configured directory an existing binary is used,
    else one is configured and built under build/. `build` is the build step (a function of the directory returning the exit code); tests pass a fake one."""
    build = build or run_build
    if custom_bin and os.path.isfile(custom_bin) and os.access(custom_bin, os.X_OK):
        return Path(custom_bin)

    candidate_dirs = []
    if build_dir:
        candidate_dirs.append(Path(build_dir))
    candidate_dirs.extend([
        repo_root / "build",
        repo_root / "build-test",
        repo_root / "build-mpc",
        repo_root / "build-native",
    ])

    configured = [d for d in candidate_dirs if (d / "CMakeCache.txt").is_file()]
    if build_dir and Path(build_dir) in configured:
        configured = [Path(build_dir)]   # the directory the caller named: only that one
    for bdir in configured:
        rc = build(bdir)
        if rc != 0:
            raise RuntimeError(f"The build of ui_gallery_host in {bdir} failed (exit code {rc}); not using a possibly stale binary.")
        found = host_binary_in(bdir)
        if found:
            return found
        raise RuntimeError(f"The build in {bdir} succeeded but there is no ui_gallery_host binary in it (is SYNTHYBOY_BUILD_MPC_SHELL on?).")

    for bdir in candidate_dirs:
        found = host_binary_in(bdir)
        if found:
            return found

    # If no configured build directory exists, configure and build under repo_root / "build"
    default_build = repo_root / "build"
    print(f"Configuring and building ui_gallery_host in {default_build}...")
    default_build.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        "cmake", "-S", str(repo_root), "-B", str(default_build),
        "-DCMAKE_BUILD_TYPE=Release", "-DSYNTHYBOY_BUILD_MPC_SHELL=ON"
    ], check=True)
    subprocess.run(["cmake", "--build", str(default_build), "--target", "ui_gallery_host", "-j4"], check=True)
    found = host_binary_in(default_build)
    if found:
        return found

    raise RuntimeError("Could not find or build ui_gallery_host binary.")


def parse_page_and_state(filename, known_views, states):
    stem = Path(filename).stem
    # Filename format: <state>_<view_or_page>.png
    # Find the longest matching state name
    state = "Init"
    rest = stem
    for s in sorted(states, key=len, reverse=True):
        if stem.startswith(s + "_"):
            state = s
            rest = stem[len(s) + 1:]
            break
    else:
        # Generic prefix up to first underscore before known view
        parts = stem.split("_", 1)
        if len(parts) == 2:
            state, rest = parts[0], parts[1]

    # Human-readable page title
    title = rest.replace("_", " ").title()
    title = title.replace("Osca", "Osc A").replace("Oscb", "Osc B").replace("Oscc", "Osc C")
    title = title.replace("Oscsub", "Osc Sub").replace("Oscnoise", "Osc Noise")
    title = title.replace("Fx ", "FX ").replace("Lfo", "LFO ").replace("Env", "Env ")
    title = title.replace("Notargets", "(No Targets)").replace("Targets", "(With Targets)")
    title = title.replace("Hz", "(Hz)").replace("Bpm", "(BPM)").replace("Key", "(KEY)")
    title = title.replace("Main", "(Main)").replace("Unison", "(Unison)").replace("Mod", "(Mod)")

    # Find matching view if any
    matching_view = None
    for v in known_views:
        v_name = v.get("name", "")
        if v_name in rest:
            matching_view = v_name
            break

    return state, title, matching_view


def generate_contact_sheet(out_dir, images_info):
    # Group images by state
    states = sorted(list(set(info["state"] for info in images_info)))

    html_lines = [
        "<!DOCTYPE html>",
        "<html lang='en'>",
        "<head>",
        "  <meta charset='UTF-8'>",
        "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>",
        "  <title>SynthyBoy MPC Overlay - UI Gallery</title>",
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
        "    <h1>SynthyBoy MPC Overlay - UI Gallery</h1>",
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


def create_contact_sheets(out_dir, images_info, pictures_per_sheet=8):
    """Write graphical contact sheets of 8 pictures each using PIL if available."""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        print("PIL (Pillow) is not installed; skipping graphical contact sheet step.")
        return []

    if not images_info:
        return []

    out_dir = Path(out_dir)
    sheet_files = []

    # 8 pictures per sheet arranged in a 4x2 grid
    cols = 4
    rows = 2
    thumb_w = 640
    thumb_h = 400
    pad = 20
    header_h = 32
    card_h = thumb_h + header_h

    sheet_w = pad + cols * (thumb_w + pad)
    sheet_h = pad + rows * (card_h + pad)

    bg_color = (30, 29, 30)
    card_bg = (38, 37, 39)
    text_color = (238, 241, 255)
    border_color = (61, 59, 62)

    total_images = len(images_info)
    sheet_count = (total_images + pictures_per_sheet - 1) // pictures_per_sheet

    for sheet_idx in range(sheet_count):
        batch = images_info[sheet_idx * pictures_per_sheet : (sheet_idx + 1) * pictures_per_sheet]
        sheet_img = Image.new("RGB", (sheet_w, sheet_h), bg_color)
        draw = ImageDraw.Draw(sheet_img)

        for i, info in enumerate(batch):
            col = i % cols
            row = i // cols
            x0 = pad + col * (thumb_w + pad)
            y0 = pad + row * (card_h + pad)

            draw.rectangle([x0, y0, x0 + thumb_w, y0 + card_h], fill=card_bg, outline=border_color)

            img_path = info.get("path")
            if img_path and Path(img_path).is_file():
                try:
                    with Image.open(img_path) as im:
                        im_thumb = im.convert("RGB").resize((thumb_w, thumb_h), Image.Resampling.LANCZOS)
                        sheet_img.paste(im_thumb, (x0, y0))
                except Exception:
                    pass

            label = f"{info.get('state', '')} : {info.get('page_name', '')}"
            draw.text((x0 + 10, y0 + thumb_h + 8), label, fill=text_color)

        sheet_filename = f"contact_sheet_{sheet_idx + 1:02d}.png"
        sheet_path = out_dir / sheet_filename
        sheet_img.save(sheet_path, "PNG")
        sheet_files.append(sheet_path)

    print(f"Generated {len(sheet_files)} image contact sheets ({pictures_per_sheet} pictures/sheet) in {out_dir}")
    return sheet_files


def main():
    parser = argparse.ArgumentParser(description="SynthyBoy UI gallery renderer")
    parser.add_argument("--out", default="", help="Output directory for PNGs and index.html (default: build/ui-gallery)")
    parser.add_argument("--presets", nargs="*", default=[], help="Presets to render (.sbpreset files)")
    parser.add_argument("--host-bin", default="", help="Path to ui_gallery_host binary")
    parser.add_argument("--build-dir", default="", help="Build directory")
    parser.add_argument("--list", action="store_true", help="Print STATE\\tPAGE\\tFILE lines (sorted by state, then page)")
    parser.add_argument("--knobs", type=int, default=16, choices=[4, 16], help="Q-Link knobs of the device: 16 (MPC XL, the default: idle chips 5..16) or 4 (Key 61)")
    parser.add_argument("--only", default="", help="Comma-separated list of page-name beginnings to include in --list and contact sheet")
    parser.add_argument("--only-prefix", default="", help="Render only states whose name starts with this prefix")
    args = parser.parse_args()

    repo_root = find_repo_root()
    views = load_layout_views(repo_root)
    print(f"Loaded {len(views)} views from overlay_layout.json")

    # Determine output directory (under build tree by default)
    if args.out:
        out_dir = Path(args.out).resolve()
    else:
        out_dir = (repo_root / "build" / "ui-gallery").resolve()

    out_dir.mkdir(parents=True, exist_ok=True)

    # Locate ui_gallery_host
    host_bin = find_or_build_host(repo_root, custom_bin=args.host_bin, build_dir=args.build_dir)
    print(f"Using host binary: {host_bin}")

    # Presets
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

        # Dynamically scan factory presets for the one with the most modulation routes
        long_preset = find_preset_with_most_modulation_routes(repo_root)
        if long_preset:
            long_path_str = str(long_preset.resolve())
            if long_path_str not in preset_paths:
                print(f"Adding preset with most modulation routes: {long_preset.name} ({long_preset})")
                preset_paths.append(long_path_str)

    # Build states list from preset stems
    states = ["Init"] + [Path(p).stem for p in preset_paths]

    # Run ui_gallery_host
    cmd = [str(host_bin), "--out", str(out_dir), "--knobs", str(args.knobs)]
    if args.only_prefix:
        cmd.extend(["--only-prefix", args.only_prefix])
    if preset_paths:
        cmd.append("--presets")
        cmd.extend(preset_paths)

    print(f"Running host: {' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=str(repo_root))
    if res.returncode != 0:
        print(f"Error: host binary exited with code {res.returncode}", file=sys.stderr)
        return res.returncode

    # Discover generated PNG images (excluding any generated contact sheets)
    png_files = sorted([f for f in out_dir.glob("*.png") if not f.name.startswith("contact_sheet_")])
    print(f"Found {len(png_files)} rendered PNG images in {out_dir}")

    images_info = []
    for img in png_files:
        st, title, match_v = parse_page_and_state(img.name, views, states)
        # Extract raw page name from filename for --list and --only
        stem = Path(img.name).stem
        page_name = stem[len(st) + 1:] if stem.startswith(st + "_") else stem
        images_info.append({
            "path": img,
            "rel_path": img.name,
            "state": st,
            "page_title": title,
            "page_name": page_name,
            "view": match_v,
        })

    # Apply --only filter
    filtered_info = images_info
    if args.only:
        prefixes = [p for p in args.only.split(",") if p]
        if prefixes:
            filtered_info = [info for info in filtered_info
                             if any(info["page_name"].startswith(pfx) for pfx in prefixes)]

    # Apply --only-prefix filter
    if args.only_prefix:
        filtered_info = [info for info in filtered_info
                         if Path(info["rel_path"]).stem.startswith(args.only_prefix)
                         or info["state"].startswith(args.only_prefix)
                         or info["page_name"].startswith(args.only_prefix)]

    # --list output
    if args.list:
        sorted_rows = sorted(filtered_info, key=lambda i: (i["state"], i["page_name"]))
        for info in sorted_rows:
            print(f"{info['state']}\t{info['page_name']}\t{info['rel_path']}")

    # Generate index.html contact sheet
    index_file = generate_contact_sheet(out_dir, filtered_info)
    print(f"Generated HTML contact sheet: {index_file}")

    # Generate graphical contact sheets (8 pictures per sheet) via PIL
    create_contact_sheets(out_dir, filtered_info, pictures_per_sheet=8)

    # Check views coverage (against all files, not filtered; skipped if filtering by prefix/only)
    missing_views = []
    for v in views:
        v_name = v["name"]
        if not any(v_name in img.name for img in png_files):
            missing_views.append(v_name)

    if not (args.only or args.only_prefix) and missing_views:
        print(f"Warning: The following {len(missing_views)} views had no pictures: {missing_views}", file=sys.stderr)
        return 1

    print(f"UI gallery completed successfully: all {len(views)} views have rendered pictures.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
