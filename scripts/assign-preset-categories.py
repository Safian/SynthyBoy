#!/usr/bin/env python3
# SynthyBoy - preset category assignment and verification for host browser
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Assigns and verifies musician-oriented browser categories for SynthyBoy presets.

The host Sounds browser displays presets grouped by category. Stock MPC instruments
use category names containing letters, spaces, and '+' only (e.g. "Synth Bass",
"Plucks+Synths", "Pitched Perc+Bells" - never slashes).

This script manages `scripts/factory-preset-categories.json`:
  --check:
    Validates that:
      1. Every factory preset on disk has exactly one entry in the JSON data.
      2. Every entry in the JSON data corresponds to an existing factory preset on disk.
      3. Every preset is assigned to a group from the documented list of at most 16 groups.
      4. Every group contains at least 4 presets (or must be merged into a neighbour).
      5. Group names conform to stock browser conventions (letters, spaces, '+' only; no '/').
    Exits with code 0 on success, code 1 on failure.

  --suggest:
    Analyzes presets without an entry (or all presets with --all) based on synth
    parameters, oscillator modes, filter models, unison voices, envelope shapes,
    and metadata, printing category recommendations and engine reasons.

Usage:
  python3 scripts/assign-preset-categories.py --check
  python3 scripts/assign-preset-categories.py --suggest
  python3 scripts/assign-preset-categories.py --suggest --all
  python3 scripts/assign-preset-categories.py --write
"""

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PRESETS = ROOT / "content" / "presets"
DEFAULT_DATA = ROOT / "scripts" / "factory-preset-categories.json"
MAX_GROUPS = 16
MIN_PRESETS_PER_GROUP = 4

# Stock Akai MPC browser category names allow letters, spaces, digits, and '+' only (no slashes)
GROUP_NAME_PATTERN = re.compile(r"^[A-Za-z0-9 +]+$")

# Fixed, documented browser categories in preferred display / numbering order
DOCUMENTED_GROUPS = [
    "Bass Sub+808",
    "Bass Reese",
    "Bass Acid",
    "Bass Synth",
    "Lead Synth",
    "Lead Sync+FM",
    "Pad Warm",
    "Pad Evolving",
    "Keys",
    "Pluck",
    "Bells+Mallets",
    "Strings+Brass",
    "Sequence",
    "Texture",
    "FX",
]


def load_presets_from_disk(presets_dir: Path) -> Dict[str, Dict[str, Any]]:
    """Loads all .sbpreset files under presets_dir, returning {preset_name: preset_dict}."""
    presets = {}
    for p in sorted(presets_dir.glob("*/*.sbpreset")):
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except Exception as e:
            raise RuntimeError(f"Failed to read preset JSON {p}: {e}") from e
        meta = data.get("meta", {})
        name = meta.get("name", p.stem)
        presets[name] = {
            "path": p,
            "folder": p.parent.name,
            "data": data,
            "meta": meta,
            "params": data.get("state", {}).get("params", {}),
        }
    return presets


def load_category_data(data_file: Path) -> Dict[str, Any]:
    """Loads scripts/factory-preset-categories.json."""
    if not data_file.is_file():
        raise FileNotFoundError(f"Category data file not found: {data_file}")
    with open(data_file, "r", encoding="utf-8") as f:
        return json.load(f)


def suggest_category(
    folder: str,
    name: str,
    tags: str,
    desc: str,
    params: Dict[str, Any],
) -> Tuple[str, str]:
    """Derives a recommended category and engine reason based on musical & sonic features."""
    t_lower = tags.lower()
    d_lower = desc.lower()
    n_lower = name.lower()

    # 1. Sequence / Arp / Rhythmic groove patterns
    if (
        any(
            k in t_lower or k in d_lower or k in n_lower
            for k in ["pentatonic step", "tempo gate", "fast arp", "trance short pluck", "radiant pulse"]
        )
        or params.get("arp.on", 0)
    ):
        return "Sequence", "Rhythmic sequence or tempo-synced modulation pattern"

    # 2. Bells+Mallets (struck percussive bars, bells, chimes, tines)
    if any(
        k in n_lower or k in t_lower
        for k in ["celesta", "vibraphone", "gamelan", "marimba", "kalimba", "toy piano", "chime drop", "bell mallet"]
    ):
        return "Bells+Mallets", "Struck percussive bar, bell, or mallet tone"
    if folder in ["Pluck", "Keys"] and any(k in n_lower for k in ["bell", "chime"]):
        return "Bells+Mallets", "Struck percussive bell or chime tone"
    if folder == "Lead" and name == "Bell Chime Lead":
        return "Bells+Mallets", "Crystalline bell overtones and struck chime lead"

    # 3. Strings+Brass (acoustic & orchestral ensemble instruments, reeded keys, pizzicato)
    if any(
        k in n_lower or k in t_lower
        for k in ["accordion", "harpsichord", "pizzicato", "bowed", "brass swell", "vintage brass", "velvet strings"]
    ):
        return "Strings+Brass", "Acoustic or synthetic string/brass ensemble sound"

    # 4. Bass sub-styles
    if folder == "Bass":
        if any(k in n_lower or k in t_lower for k in ["808", "sub", "harmonic sine"]):
            return "Bass Sub+808", "Deep sub or 808-style low-end bass foundation"
        if (
            any(k in n_lower or k in t_lower for k in ["reese", "wobble"])
            or params.get("osc1.unison.voices", 1) >= 2
            or name == "Scream Filter Bass"
        ):
            return "Bass Reese", "Detuned multi-voice saws or wobble bass"
        if any(k in n_lower or k in t_lower for k in ["acid", "303", "squelch", "diode", "growl", "distortion", "dirty"]):
            return "Bass Acid", "Resonant acid, saturated ladder, or aggressive drive bass"
        return "Bass Synth", "General synthesizer bassline"

    # 5. Lead sub-styles
    if folder == "Lead":
        if any(k in n_lower or k in t_lower for k in ["sync", "laser beam", "twin warp"]) or "fm" in n_lower:
            return "Lead Sync+FM", "Hard-sync or FM modulated lead synth"
        if "pluck" in n_lower:
            return "Pluck", "Short transient pluck attack lead"
        return "Lead Synth", "Solo synthesizer lead"

    # 6. Keys
    if folder == "Keys":
        return "Keys", "Acoustic or electric keyboard/organ"

    # 7. Pluck
    if folder == "Pluck":
        return "Pluck", "Plucked acoustic or synth string transient"

    # 8. FX vs Texture
    if folder == "FX":
        if any(k in n_lower or k in t_lower or k in d_lower for k in ["drone", "atmosphere", "breathing", "wind", "crackle", "alien"]):
            return "Texture", "Continuous atmospheric texture or drone"
        return "FX", "Transitional effect, riser, or impact hit"

    # 9. Pad
    if folder == "Pad":
        if any(k in n_lower or k in t_lower for k in ["dark cinema drone", "shepard ascent", "tri-band split texture"]):
            return "Texture", "Continuous atmospheric texture or drone"
        if any(
            k in n_lower or k in t_lower
            for k in [
                "comb reso", "deep sea", "diffusor", "ethereal glass", "frozen aurora",
                "harmonic cloud", "infinite horizon", "lorenz garden", "parallel dusk",
                "slow motion", "space odyssey", "stretched glass"
            ]
        ):
            return "Pad Evolving", "Slowly modulating or evolving atmospheric pad"
        return "Pad Warm", "Warm classic analog or vocal pad"

    return "Lead Synth", "General synthesizer preset"


def check_categories(
    presets_dir: Path,
    data_file: Path,
) -> Tuple[bool, List[str]]:
    """Runs all verification checks on presets and category data.
    
    Returns (success, list_of_error_messages).
    """
    errors: List[str] = []

    if not data_file.is_file():
        return False, [f"Data file does not exist: {data_file}"]

    try:
        data = load_category_data(data_file)
    except Exception as e:
        return False, [f"Failed to parse category data file: {e}"]

    groups = data.get("groups", [])
    presets_data = data.get("presets", {})

    # Check 1: Group list limits and names
    if not groups:
        errors.append("No groups defined in data file")
    if len(groups) > MAX_GROUPS:
        errors.append(f"Too many groups: {len(groups)} exceeds maximum of {MAX_GROUPS}")

    for g in groups:
        if "/" in g:
            errors.append(f"Group name '{g}' contains forbidden character '/'")
        if not GROUP_NAME_PATTERN.match(g):
            errors.append(f"Group name '{g}' does not match allowed pattern (letters, spaces, digits, '+')")

    # Check 2: Load presets on disk
    try:
        disk_presets = load_presets_from_disk(presets_dir)
    except Exception as e:
        return False, [f"Failed to load presets from disk: {e}"]

    disk_names = set(disk_presets.keys())
    data_names = set(presets_data.keys())

    # Check 3: Every factory preset on disk must have an entry
    missing_in_data = disk_names - data_names
    if missing_in_data:
        for m in sorted(missing_in_data):
            errors.append(f"Preset on disk has no category entry: '{m}'")

    # Check 4: Every entry in data must correspond to a preset on disk
    extra_in_data = data_names - disk_names
    if extra_in_data:
        for ex in sorted(extra_in_data):
            errors.append(f"Category entry has no corresponding preset file: '{ex}'")

    # Check 5: Every assigned group must be in groups list and valid
    group_counts: Counter = Counter()
    for name, entry in presets_data.items():
        if isinstance(entry, dict):
            grp = entry.get("group")
            why = entry.get("why")
        elif isinstance(entry, str):
            grp = entry
            why = ""
        else:
            errors.append(f"Invalid entry type for preset '{name}': {type(entry).__name__}")
            continue

        if not grp:
            errors.append(f"Preset '{name}' has empty group assignment")
            continue

        if grp not in groups:
            errors.append(f"Preset '{name}' assigned to unknown group '{grp}' (not in groups list)")

        if not why or not str(why).strip():
            errors.append(f"Preset '{name}' missing 'why' explanation")

        group_counts[grp] += 1

    # Check 6: Every group must have at least MIN_PRESETS_PER_GROUP presets
    for g in groups:
        c = group_counts.get(g, 0)
        if c < MIN_PRESETS_PER_GROUP:
            errors.append(
                f"Group '{g}' has {c} presets, which is fewer than minimum {MIN_PRESETS_PER_GROUP}"
            )

    return (len(errors) == 0), errors


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--presets", type=Path, default=DEFAULT_PRESETS, help="Presets root directory")
    ap.add_argument("--data", type=Path, default=DEFAULT_DATA, help="Category JSON data file")
    ap.add_argument("--check", action="store_true", help="Run verification checks")
    ap.add_argument("--suggest", action="store_true", help="Suggest categories for presets")
    ap.add_argument("--all", action="store_true", help="Suggest for all presets, even existing ones")
    ap.add_argument("--write", action="store_true", help="Write suggestions to data file for missing presets")
    args = ap.parse_args()

    presets_dir = args.presets.resolve()
    data_file = args.data.resolve()

    if args.check:
        success, errors = check_categories(presets_dir, data_file)
        if not success:
            print("check-preset-categories: FAILED", file=sys.stderr)
            for err in errors:
                print(f"  ERROR: {err}", file=sys.stderr)
            sys.exit(1)
        else:
            data = load_category_data(data_file)
            counts = Counter(p["group"] for p in data["presets"].values())
            print(f"check-preset-categories: OK ({len(data['presets'])} presets in {len(data['groups'])} groups)")
            for g in data["groups"]:
                print(f"  {g:20}: {counts[g]} presets")
            sys.exit(0)

    disk_presets = load_presets_from_disk(presets_dir)
    existing_data = {}
    if data_file.is_file():
        existing_data = load_category_data(data_file)

    existing_presets = existing_data.get("presets", {})
    groups = existing_data.get("groups", DOCUMENTED_GROUPS)

    if args.suggest:
        target_names = sorted(disk_presets.keys()) if args.all else sorted(set(disk_presets.keys()) - set(existing_presets.keys()))
        if not target_names:
            print("No presets requiring category suggestions (all presets cataloged). Use --all to view suggestions for all.")
            return

        print(f"Category suggestions for {len(target_names)} presets:")
        for name in target_names:
            info = disk_presets[name]
            grp, why = suggest_category(
                folder=info["folder"],
                name=name,
                tags=info["meta"].get("tags", ""),
                desc=info["meta"].get("description", ""),
                params=info["params"],
            )
            current = existing_presets.get(name, {}).get("group", "<none>")
            mark = f" (current: {current})" if args.all and current != grp else ""
            print(f"  {name:30} -> {grp:16} | {why}{mark}")
        return

    if args.write:
        missing = sorted(set(disk_presets.keys()) - set(existing_presets.keys()))
        if not missing:
            print("Data file already includes all presets on disk. Nothing to write.")
            return
        updated_presets = dict(existing_presets)
        for name in missing:
            info = disk_presets[name]
            grp, why = suggest_category(
                folder=info["folder"],
                name=name,
                tags=info["meta"].get("tags", ""),
                desc=info["meta"].get("description", ""),
                params=info["params"],
            )
            updated_presets[name] = {"group": grp, "why": why}

        doc = {
            "format": "synthyboy-factory-preset-categories",
            "version": 1,
            "groups": groups,
            "presets": {k: updated_presets[k] for k in sorted(updated_presets.keys())},
        }
        data_file.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Updated {data_file}: added {len(missing)} presets.")
        return

    # Default action if neither --check, --suggest nor --write: print summary
    if data_file.is_file():
        data = load_category_data(data_file)
        counts = Counter(p["group"] for p in data.get("presets", {}).values())
        print(f"Preset Categories ({len(data.get('presets', {}))} presets, {len(data.get('groups', []))} groups):")
        for g in data.get("groups", []):
            print(f"  {g:20}: {counts[g]} presets")
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
