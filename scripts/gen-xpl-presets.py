#!/usr/bin/env python3
# SynthyBoy - Akai MPC preset browser files for the factory presets
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Writes one `NNN-<Category>-<Preset Name>.xpl` per factory preset, in the form the stock instruments of the MPC
ship in their `<package>/Presets/` folder (observed on the MPC XL: `000-Keys-Studio Grand.xpl`), with SynthyBoy's
<PLUGIN> descriptor, the preset's name in <preset> and the state the host writes: an fxb bank chunk ("CcnK" /
"FBCh", plugin id "SfSB", 160-byte header) around the plugin's own chunk, in JUCE's base64.

The plugin chunks come from the plugin itself (effGetChunk of every program, through the dumper
tools/dump_mpc_chunks of shell-mpc), not from a second writer of the state. The file format code is
scripts/gen-mpc-xpl.py (this script imports it).

    python3 scripts/gen-xpl-presets.py --device xl --out DIR --plugin build/shell-mpc/synthyboy_mpc.so --dump-tool build/shell-mpc/dump_mpc_chunks
    python3 scripts/gen-xpl-presets.py --device k61 --out DIR --chunks DIR_OF_ALREADY_DUMPED_CHUNKS --by-category-folders
"""
import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

_spec = importlib.util.spec_from_file_location("gen_mpc_xpl", HERE / "gen-mpc-xpl.py")
gen_mpc_xpl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen_mpc_xpl)

# The device's path of the plugin binary (<PLUGIN file=...>).
DEVICE_PLUGIN_PATHS = {
    "xl": "/nvme/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm64.so",
    "k61": "/sdcard/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm32.so",
}

# Our content folders, in the order the files are numbered, and the category word in the file name.
# The names are the folder names for now; the owner may want finer groups (Reese, ...): change the
# right-hand side only, nothing else depends on it.
CATEGORIES = [
    ("Bass", "Bass"),
    ("FX", "FX"),
    ("Keys", "Keys"),
    ("Lead", "Lead"),
    ("Pad", "Pad"),
    ("Pluck", "Pluck"),
]
CATEGORIES_FILE = HERE / "factory-preset-categories.json"

# <PLUGIN> attributes as the host wrote them for SynthyBoy (device-observed/xpl/synthyboy-host-saved-*).
PLUGIN_ATTRS = {
    "name": "SynthyBoy",
    "format": "VST",
    "category": "Synth",
    "manufacturer": "Safian",
    "version": "1.0.0.0",
    "uid": "53665342",
    "isInstrument": "1",
    "fileTime": "0",
    "infoUpdateTime": "0",
    "numInputs": "0",
    "numOutputs": "2",
    "isShell": "0",
}
DEFAULT_DATE = "2026-01-01-00-00"


def preset_categories(presets_dir):
    """{preset display name: folder} from the .sbpreset files."""
    out = {}
    for folder, _word in CATEGORIES:
        for f in sorted((presets_dir / folder).glob("*.sbpreset")):
            name = json.loads(f.read_text(encoding="utf-8"))["meta"]["name"]
            out[name] = folder
    on_disk = {p.name for p in presets_dir.iterdir() if p.is_dir()}
    unknown = on_disk - {c for c, _ in CATEGORIES}
    if unknown:
        raise SystemExit(f"category folders missing from CATEGORIES: {sorted(unknown)}")
    return out


def file_stem(number, category_word, name):
    """'NNN-<Category>-<Name>' without the extension, FAT-safe."""
    safe = gen_mpc_xpl.sanitize_filename(f"{category_word}-{name}")[:-len(".xpl")]
    return f"{number:03d}-{safe}"


def load_categories_map(categories_path=CATEGORIES_FILE):
    """Loads (groups_list, {preset_name: group_name}). Returns ([], {}) if file is missing."""
    if not categories_path or not Path(categories_path).is_file():
        return [], {}
    try:
        data = json.loads(Path(categories_path).read_text(encoding="utf-8"))
        groups = data.get("groups", [])
        presets = {}
        for name, entry in data.get("presets", {}).items():
            if isinstance(entry, dict):
                presets[name] = entry.get("group", "")
            elif isinstance(entry, str):
                presets[name] = entry
        return groups, presets
    except Exception:
        return [], {}


def plan(chunk_entries, presets_dir, categories_file=CATEGORIES_FILE):
    """[(number, folder, category word, name, chunk bytes)] in numbering order:
    groups stay together in the group list's order, and names sort alphabetically inside a group.
    Falls back to folder mapping when categories_file is missing or a preset has no entry."""
    cats = preset_categories(presets_dir)
    folder_order = {c: i for i, (c, _) in enumerate(CATEGORIES)}
    groups, cat_map = load_categories_map(categories_file)
    group_order = {g: i for i, g in enumerate(groups)}

    rows = []
    for name, chunk in chunk_entries:
        if name not in cats:
            raise SystemExit(f"program '{name}' is not a factory preset of {presets_dir}")
        folder = cats[name]
        cword = cat_map.get(name, folder)
        if cword in group_order:
            rank = group_order[cword]
        else:
            rank = len(groups) + folder_order.get(folder, 0)
        rows.append((rank, name.lower(), folder, cword, name, chunk))
    rows.sort(key=lambda r: (r[0], r[1]))
    return [(i, r[2], r[3], r[4], r[5]) for i, r in enumerate(rows)]


def write_set(rows, out_dir, plugin_path, date=DEFAULT_DATE, file_time="0", by_folders=False, num_programs=None):
    attrs = dict(PLUGIN_ATTRS, file=plugin_path, fileTime=file_time)
    out_dir.mkdir(parents=True, exist_ok=True)
    seen = set()
    progs = num_programs if num_programs is not None else len(rows) + 1
    for number, folder, cword, name, chunk in rows:
        header = gen_mpc_xpl.build_bank_header(chunk_size=len(chunk), num_programs=progs)
        state = gen_mpc_xpl.encode_juce_base64(header + chunk)
        text = gen_mpc_xpl.render_xpl("1", date, attrs, name, state)
        target = out_dir / folder if by_folders else out_dir
        target.mkdir(parents=True, exist_ok=True)
        path = target / (file_stem(number, cword, name) + ".xpl")
        if str(path).lower() in seen:
            raise SystemExit(f"file name collision: {path}")
        seen.add(str(path).lower())
        path.write_bytes(text.encode("utf-8"))
    return len(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--device", choices=sorted(DEVICE_PLUGIN_PATHS), default="xl")
    ap.add_argument("--plugin-path", help="the <PLUGIN file=...> value (default: by --device)")
    ap.add_argument("--presets", type=Path, default=REPO / "content" / "presets")
    ap.add_argument("--chunks", type=Path, help="a dump_mpc_chunks output directory (else --plugin and --dump-tool run it)")
    ap.add_argument("--plugin", type=Path, help="a native synthyboy_mpc library")
    ap.add_argument("--dump-tool", type=Path, help="the native dump_mpc_chunks tool")
    ap.add_argument("--date", default=DEFAULT_DATE, help="<version date=...> (fixed for deterministic output)")
    ap.add_argument("--file-time", default="0", help="<PLUGIN fileTime=...>")
    ap.add_argument("--num-programs", type=int, default=None, help="number of programs in VST2 bank header (default: preset count + 1)")
    ap.add_argument("--by-category-folders", action="store_true", help="one sub folder per category")
    ap.add_argument("--categories", type=Path, default=CATEGORIES_FILE, help="path to factory-preset-categories.json")
    a = ap.parse_args()

    tmp = None
    chunks = a.chunks
    if chunks is None:
        if not (a.plugin and a.dump_tool):
            ap.error("give --chunks, or --plugin and --dump-tool")
        tmp = tempfile.TemporaryDirectory()
        chunks = Path(tmp.name)
        subprocess.run([str(a.dump_tool), str(a.plugin), str(a.presets), str(chunks)], check=True, stdout=subprocess.DEVNULL)
    entries = gen_mpc_xpl.load_chunk_entries(chunks)   # skips program 0 (Init)
    rows = plan(entries, a.presets, a.categories)
    n = write_set(
        rows,
        a.out,
        a.plugin_path or DEVICE_PLUGIN_PATHS[a.device],
        a.date,
        a.file_time,
        a.by_category_folders,
        num_programs=a.num_programs,
    )
    print(f"gen-xpl-presets: {n} files in {a.out}")


if __name__ == "__main__":
    main()
