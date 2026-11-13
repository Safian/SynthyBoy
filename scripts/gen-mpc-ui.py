#!/usr/bin/env python3
# SynthyBoy - generates MPC ui.json and params-hostsim.json from docs/params.json
# and docs/kick-pages.json.
#
# Usage:
#   python3 scripts/gen-mpc-ui.py --params docs/params.json --pages docs/kick-pages.json --out <dir>
#   python3 scripts/gen-mpc-ui.py --params docs/params.json --pages docs/kick-pages.json --out <dir> --page-ids osc1_main,filter1_main
#
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import argparse
import json
import math
import os
import re
import sys

# ---------------------------------------------------------------------------
# Limits
# ---------------------------------------------------------------------------
MAX_CONTROLS_PER_PAGE = 16
MAX_PAGES = 32
MAX_PAGE_NAME = 8

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _is_level_or_mix(name):
    """Return True if the parameter name suggests a level/mix control."""
    lower = name.lower()
    return any(w in lower for w in ("level", "mix", "volume", "dry", "wet", "send"))


def _is_zero_centred(p):
    """Return True if the parameter is zero-centred (min < 0 < max and default ≈ 0)."""
    mn = p.get("min", 0)
    mx = p.get("max", 1)
    d = p.get("default", 0)
    if mn >= 0 or mx <= 0:
        return False
    return abs(d) <= 0.001 * max(abs(mn), abs(mx), 1)


def _normalize(p):
    """Normalise a parameter value to 0..1 for the host_sim dump."""
    mn = p.get("min", 0)
    mx = p.get("max", 1)
    d = p.get("default", 0)
    if mx == mn:
        return 0.0
    return (d - mn) / (mx - mn)


def _widget_for_param(p):
    """Choose widget for a parameter based on the ui-guide table."""
    choices = p.get("choices")
    n_choices = len(choices) if choices else 0
    scale = p.get("scale", "linear")

    # bool / stepped with 2-4 choices → switch
    if scale == "stepped" and n_choices <= 4:
        return "switch"

    # 5+ choices → slider + overlay menu
    if scale == "stepped" and n_choices >= 5:
        return "slider"

    # level/mix → fader
    if _is_level_or_mix(p["name"]):
        return "fader"

    # zero-centred → bipolar knob
    if _is_zero_centred(p):
        return "knob"

    # default → knob
    return "knob"


def _polarity_for_param(p):
    """Return polarity string for a knob/fader."""
    if _is_zero_centred(p):
        return "bipolar"
    return "auto"


def _display_for_param(p):
    """Generate a display string for the default value."""
    name = p["name"]
    d = p.get("default", 0)
    unit = p.get("unit", "none")
    scale = p.get("scale", "linear")
    decimals = p.get("decimals", 1)

    if scale == "stepped" and "choices" in p:
        idx = int(d)
        if 0 <= idx < len(p["choices"]):
            return p["choices"][idx]
        return str(d)

    if unit == "none":
        if decimals == 0:
            return str(int(d))
        return ("%%.%df" % decimals) % d

    # Format with unit
    if unit in ("dB", "Hz", "ms", "percent", "semitone", "cents", "degrees", "voices", "beats"):
        val = ("%%.%df" % decimals) % d
        return val + " " + unit

    return str(d)


def _label_for_param(p):
    """Return the label (unit string) for the host_sim dump."""
    unit = p.get("unit", "none")
    if unit == "none":
        return ""
    return unit


def _kind_for_param(p):
    """Return the kind for the host_sim dump."""
    scale = p.get("scale", "linear")
    if scale == "stepped":
        if "choices" in p:
            n = len(p["choices"])
            if n == 2:
                return "bool"
            return "choice"
        return "float"
    return "float"


def _choices_for_param(p):
    """Return choices list for the host_sim dump."""
    scale = p.get("scale", "linear")
    if scale == "stepped" and "choices" in p:
        return p["choices"]
    return []


def _automatable_for_param(p):
    """Return whether the parameter is automatable."""
    flags = p.get("flags", [])
    return "automatable" in flags


def _build_hostsim_param(p, index):
    """Build a host_sim dump entry for a parameter."""
    return {
        "index": index,
        "id": p.get("id"),
        "name": p["name"],
        "label": _label_for_param(p),
        "display": _display_for_param(p),
        "default": round(_normalize(p), 10),
        "automatable": _automatable_for_param(p),
        "kind": _kind_for_param(p),
        "choices": _choices_for_param(p),
    }


def _build_page_name(title, continuation, used_names):
    """Create a unique page name <= 8 chars from the page title.
    
    Drops spaces and punctuation, abbreviates deterministically.
    Continuation pages get "Name 2", "Name 3" suffix within 8 chars.
    On clash, replaces the tail with a number.
    """
    # Build base name from title: drop spaces and punctuation, keep letters/digits
    base = re.sub(r'[^a-zA-Z0-9]', '', title)
    
    # Abbreviate to MAX_PAGE_NAME (8) chars
    base = base[:MAX_PAGE_NAME]
    
    if continuation == 0:
        return _unique_page_name(base, used_names)
    
    # For continuation pages, try "Base N" format
    suffix = " %d" % continuation
    available = MAX_PAGE_NAME - len(suffix)
    if available >= 1:
        candidate = base[:available] + suffix
        if candidate not in used_names:
            used_names.add(candidate)
            return candidate
    
    # Fallback: just use a number
    for i in range(2, 100):
        candidate = str(i)
        if candidate not in used_names:
            used_names.add(candidate)
            return candidate
    
    return str(len(used_names))


def _unique_page_name(name, used_names):
    """Ensure a page name is unique among used_names.
    
    If there's a conflict, appends a number.
    """
    if name not in used_names:
        used_names.add(name)
        return name
    
    # Try appending a number
    for i in range(2, 100):
        candidate = name[:MAX_PAGE_NAME - 2] + str(i)
        if candidate not in used_names:
            used_names.add(candidate)
            return candidate
    
    # Fallback: just use a number
    candidate = str(len(used_names))
    used_names.add(candidate)
    return candidate


# ---------------------------------------------------------------------------
# Main logic
# ---------------------------------------------------------------------------

def _get_param_group(knob, params_by_key):
    """Get the group for a knob's parameter, or empty string if not found."""
    key = knob.get("key", "")
    if key in params_by_key:
        return params_by_key[key].get("group", "")
    return ""


def _split_into_qlink_blocks(knobs, params_by_key):
    """Split knobs into blocks of 4, respecting Q-Link group boundaries.
    
    Each block of 4 consecutive controls must come from one parameter group.
    When the group changes inside a block, pad the block with "empty" and
    start the new group in the next block.
    
    Returns a list of blocks, where each block is a list of 4 items
    (knob dicts or "empty" strings).
    """
    if not knobs:
        return []

    blocks = []
    current_block = []  # List of (knob_or_empty, group) tuples
    current_group = None

    for knob in knobs:
        group = _get_param_group(knob, params_by_key)

        if current_group is None:
            # First control - start new block
            current_group = group
            current_block = [(knob, group)]
            continue

        if group == current_group:
            # Same group - add to current block
            current_block.append((knob, group))

            if len(current_block) == 4:
                # Block is full - emit it
                blocks.append([item[0] for item in current_block])
                current_block = []
                current_group = None
        else:
            # Group changed - pad current block to 4 with "empty"
            while len(current_block) < 4:
                current_block.append(("empty", current_group))
            blocks.append([item[0] for item in current_block])

            # Start new block with new group
            current_group = group
            current_block = [(knob, group)]

    # Handle remaining controls in the last block
    if current_block:
        while len(current_block) < 4:
            current_block.append(("empty", current_group))
        blocks.append([item[0] for item in current_block])

    return blocks


def _knob_to_control(knob, params_by_key, seen_params):
    """Convert a knob dict to a control dict, or 'empty'."""
    key = knob.get("key", "")
    if key == "empty" or key not in params_by_key:
        return "empty"

    p = params_by_key[key]
    param_name = p["name"]

    # Skip if we've already included this param (deduplication)
    if param_name in seen_params:
        return "empty"
    seen_params.add(param_name)

    widget = _widget_for_param(p)

    # Build control object
    ctrl = {"param": param_name, "widget": widget}

    # Add polarity for knob/fader if bipolar
    if widget in ("knob", "fader") and _polarity_for_param(p) == "bipolar":
        ctrl["polarity"] = "bipolar"

    # Add overlay menu for slider widgets
    if widget == "slider":
        ctrl["overlay"] = "menu"

    return ctrl


def generate(params_path, pages_path, out_dir, page_ids=None):
    """Generate ui.json and params-hostsim.json."""
    with open(params_path, encoding="utf-8") as f:
        params_doc = json.load(f)

    with open(pages_path, encoding="utf-8") as f:
        pages_doc = json.load(f)

    # Build a lookup from param key to param dict
    params_by_key = {}
    for p in params_doc.get("params", []):
        params_by_key[p["key"]] = p

    # Build host_sim params list (all params in order)
    hostsim_params = []
    for idx, p in enumerate(params_doc.get("params", [])):
        hostsim_params.append(_build_hostsim_param(p, idx))

    # Build plugin info from params_doc if available, otherwise use defaults
    plugin_info = params_doc.get("plugin", {})
    hostsim_doc = {
        "name": plugin_info.get("name", "SynthyBoy"),
        "vendor": plugin_info.get("vendor", "Safian"),
        "uid": plugin_info.get("uid", "SfSB"),
        "uidInt": plugin_info.get("uidInt", None),
        "version": plugin_info.get("version", 1000),
        "isInstrument": plugin_info.get("isInstrument", True),
        "numInputs": plugin_info.get("numInputs", 0),
        "numOutputs": plugin_info.get("numOutputs", 2),
        "numPrograms": plugin_info.get("numPrograms", 1),
        "programs": plugin_info.get("programs", ["Init"]),
        "params": hostsim_params,
    }

    # Compute uidInt if not provided (big-endian four-character code as integer)
    if hostsim_doc["uidInt"] is None and len(hostsim_doc["uid"]) == 4:
        uid = hostsim_doc["uid"]
        hostsim_doc["uidInt"] = (ord(uid[0]) << 24) | (ord(uid[1]) << 16) | (ord(uid[2]) << 8) | ord(uid[3])

    # Write params-hostsim.json
    hostsim_path = os.path.join(out_dir, "params-hostsim.json")
    with open(hostsim_path, "w", encoding="utf-8") as f:
        json.dump(hostsim_doc, f, indent=2, ensure_ascii=False)
        f.write("\n")

    # Process kick-pages into ui.json pages
    kick_pages = pages_doc.get("pages", [])

    # Determine which pages to emit
    if page_ids is not None:
        # Select pages by id in the specified order
        id_to_page = {p["id"]: p for p in kick_pages}
        missing_ids = [pid for pid in page_ids if pid not in id_to_page]
        if missing_ids:
            print("gen-mpc-ui: ERROR: unknown page id(s): %s" % ", ".join(missing_ids),
                  file=sys.stderr)
            return 2
        ordered_pages = [id_to_page[pid] for pid in page_ids]
    else:
        ordered_pages = kick_pages

    ui_pages = []
    dropped_keys = set()  # Track distinct dropped keys
    used_page_names = set()  # Track used page names for uniqueness

    for page_idx, kick_page in enumerate(ordered_pages):
        if page_idx >= MAX_PAGES:
            print("gen-mpc-ui: WARNING: page %d (%s) dropped (max %d pages exceeded)"
                  % (page_idx, kick_page.get("title", "?"), MAX_PAGES), file=sys.stderr)
            # Collect all keys from remaining pages
            for kp in ordered_pages[page_idx:]:
                for knob in kp.get("knobs", []):
                    if knob is not None and "key" in knob:
                        dropped_keys.add(knob["key"])
            continue

        knobs = kick_page.get("knobs", [])
        # Filter out null knobs
        valid_knobs = [k for k in knobs if k is not None]

        # Build page name from title
        page_title = kick_page.get("title", "Page")

        # Split into Q-Link blocks of 4
        blocks = _split_into_qlink_blocks(valid_knobs, params_by_key)

        # Group blocks into pages of 4 blocks (16 controls)
        for page_block_start in range(0, len(blocks), 4):
            page_blocks = blocks[page_block_start:page_block_start + 4]
            continuation = page_block_start // 4
            page_name = _build_page_name(page_title, continuation, used_page_names)

            controls = []
            for block in page_blocks:
                for knob in block:
                    if knob == "empty":
                        controls.append("empty")
                    else:
                        ctrl = _knob_to_control(knob, params_by_key, dropped_keys)
                        controls.append(ctrl)

            # Pad to 16 if needed
            while len(controls) < MAX_CONTROLS_PER_PAGE:
                controls.append("empty")

            ui_page = {"name": page_name, "controls": controls}
            ui_pages.append(ui_page)

    # Check page limit after splitting
    if len(ui_pages) > MAX_PAGES:
        # Report what was dropped
        excess = len(ui_pages) - MAX_PAGES
        print("gen-mpc-ui: WARNING: %d page(s) dropped due to %d-page limit (max %d controls per page may need reduction)"
              % (excess, MAX_PAGES, MAX_PAGES), file=sys.stderr)
        ui_pages = ui_pages[:MAX_PAGES]

    # Build ui.json
    ui_doc = {"pages": ui_pages}

    ui_path = os.path.join(out_dir, "ui.json")
    with open(ui_path, "w", encoding="utf-8") as f:
        json.dump(ui_doc, f, indent=2, ensure_ascii=False)
        f.write("\n")

    # Report dropped keys (distinct)
    if dropped_keys:
        print("gen-mpc-ui: Dropped %d distinct parameter key(s) due to limits: %s"
              % (len(dropped_keys), ", ".join(sorted(dropped_keys)[:20])), file=sys.stderr)

    print("gen-mpc-ui: Wrote %d pages (%d controls/page max) to %s"
          % (len(ui_pages), MAX_CONTROLS_PER_PAGE, ui_path))
    print("gen-mpc-ui: Wrote %d params to %s" % (len(hostsim_params), hostsim_path))

    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Generate MPC ui.json and params-hostsim.json from SynthyBoy docs/ files.")
    ap.add_argument("--params", required=True,
                    help="docs/params.json (parameter table)")
    ap.add_argument("--pages", required=True,
                    help="docs/kick-pages.json (remote-control page definitions)")
    ap.add_argument("--out", required=True,
                    help="output directory")
    ap.add_argument("--page-ids", default=None,
                    help="Comma-separated list of page ids to emit in order (default: first pages)")
    args = ap.parse_args(argv)

    # Parse page_ids if provided
    page_ids = None
    if args.page_ids:
        page_ids = [pid.strip() for pid in args.page_ids.split(",")]

    os.makedirs(args.out, exist_ok=True)
    result = generate(args.params, args.pages, args.out, page_ids)
    return result


if __name__ == "__main__":
    sys.exit(main())
