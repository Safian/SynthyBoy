#!/usr/bin/env python3
# SynthyBoy - generates MPC ui.json and params-hostsim.json from docs/params.json
# and docs/kick-pages.json.
#
# Usage:
#   python3 scripts/gen-mpc-ui.py --params docs/params.json --pages docs/kick-pages.json --out <dir>
#   python3 scripts/gen-mpc-ui.py --params docs/params.json --pages docs/kick-pages.json --out <dir> --page-ids osc1_main,filter1_main
#   python3 scripts/gen-mpc-ui.py --params docs/params.json --pages docs/kick-pages.json --out <dir> --layout scripts/mpc-ui-pages.json
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


def _is_continuous(p):
    """A parameter that is not stepped (a knob or a fader), as opposed to a switch or a selector."""
    return p.get("scale", "linear") != "stepped"


def _is_level_page(kick_page, params_by_key):
    """True if the page of docs/kick-pages.json has a continuous own-engine control and every one of them is a level or a mix.

    The fader rule: only on such a page do the continuous controls become faders. A level among knobs (Osc 1 Level) or a volume among
    macros is a knob like its neighbours, so a page never has a lone fader between knobs."""
    continuous = []
    for knob in kick_page.get("knobs", []):
        p = params_by_key.get(knob.get("key", "")) if knob else None
        if p is not None and p.get("ownEngine") and _is_continuous(p):
            continuous.append(p)
    return bool(continuous) and all(_is_level_or_mix(p["name"]) for p in continuous)


def _widget_for_param(p, level_page=False):
    """Choose widget for a parameter based on the ui-guide table; level_page: the Kick page of the control is a level page (_is_level_page)."""
    choices = p.get("choices")
    n_choices = len(choices) if choices else 0
    scale = p.get("scale", "linear")

    # bool / stepped with 2-4 choices → switch
    # a stepped parameter without a choice list (e.g. a modulation source index) is a plain number for the SDK: "switch" needs choices
    if scale == "stepped" and 2 <= n_choices <= 4:
        return "switch"

    # 5+ choices → slider + overlay menu
    if scale == "stepped" and n_choices >= 5:
        return "slider"

    # continuous control on a page of levels and mixes only → fader
    if level_page and _is_continuous(p):
        return "fader"

    # zero-centred or any other continuous parameter → knob
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
# Names of the controls (MPC skin)
# ---------------------------------------------------------------------------
# The tab already says which module a control belongs to, so the name drawn over a control is the short name of the Kick page
# (docs/kick-pages.json "shortLabel", "Detune" instead of "Osc 1 Unison Detune"), unless scripts/mpc-short-names.json overrides the key
# (cut-off or ambiguous shortLabels). Two controls of one tab with the same name get the leading words of their full parameter names
# that are not shared ("Osc 1 Filt Blend", "Osc 2 Filt Blend"): the longest common word suffix of the full names is dropped, what is left in front is the prefix.
# The names are drawn upper case in a bold font (scripts/mpc-skin-extras/85_names.py); name_width() estimates the width.

NAME_AREA_W = 272          # inner width of a Q-Link column frame (frame 288, 8 px each side)
DEFAULT_SHORT_NAMES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mpc-short-names.json")
# Advance widths in 1/1000 em of Arial Bold for the upper case characters (measured on the build machine, Arial Bold.ttf). Titillium Web Bold, the font
# of the skin, is narrower for every one of these characters, so the sum is an upper bound of the width the host draws.
ADVANCE = {
    " ": 278, "A": 722, "B": 722, "C": 722, "D": 722, "E": 667, "F": 611, "G": 778, "H": 722, "I": 278, "J": 556, "K": 722, "L": 611,
    "M": 833, "N": 722, "O": 778, "P": 667, "Q": 778, "R": 722, "S": 667, "T": 611, "U": 722, "V": 667, "W": 944, "X": 667, "Y": 667,
    "Z": 611, "0": 556, "1": 556, "2": 556, "3": 556, "4": 556, "5": 556, "6": 556, "7": 556, "8": 556, "9": 556, "-": 333, "+": 584,
    "/": 278, ".": 278, ",": 278, "(": 333, ")": 333, "&": 722, "'": 238,
}


def name_width(name, font_px):
    """Upper bound of the width in pixels of `name` drawn upper case with a bold font of font_px pixels (ADVANCE; unknown character: error)."""
    total = 0
    for ch in name.upper():
        if ch not in ADVANCE:
            raise ValueError("character %r of the name %r has no entry in ADVANCE" % (ch, name))
        total += ADVANCE[ch]
    return total * font_px / 1000.0


def load_short_names(path):
    if not path:
        return {}
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    if doc.get("format") != "synthyboy-mpc-short-names":
        raise ValueError("%s: format is not 'synthyboy-mpc-short-names'" % path)
    return dict(doc.get("names", {}))


def resolve_tab_names(entries, placed, overrides):
    """Names of the controls of one tab.

    entries: [(key, full parameter name, shortLabel)] of every knob of the Kick pages of the tab (the first one wins for a repeated key);
    placed:  the keys that really are controls of the tab (only these have to differ); overrides: key -> name.
    Returns {key: name}. A clash between placed controls that the prefix rule cannot resolve raises ValueError."""
    first = {}
    for key, full, short in entries:
        first.setdefault(key, (full, short))
    out = {key: overrides.get(key, short) for key, (_, short) in first.items()}
    groups = {}
    for key in first:
        if key in placed:
            groups.setdefault(out[key], []).append(key)
    for name, keys in sorted(groups.items()):
        if len(keys) < 2:
            continue
        if any(k in overrides for k in keys):
            raise ValueError("the keys %s of one tab all get the name %r and one of them is an override: give each its own override"
                             % (", ".join(keys), name))
        words = {k: first[k][0].split() for k in keys}
        common = 0
        while all(len(w) > common and w[-1 - common] == words[keys[0]][-1 - common] for w in words.values()):
            common += 1
        heads = {k: " ".join(words[k][:len(words[k]) - common]) for k in keys}
        if common == 0 or any(not h for h in heads.values()) or len(set(heads.values())) != len(keys):
            raise ValueError("the keys %s of one tab all get the name %r and the full names share no word suffix to tell them apart: add overrides to %s"
                             % (", ".join(keys), name, os.path.basename(DEFAULT_SHORT_NAMES)))
        for k in keys:
            out[k] = heads[k] + " " + name
    return out


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


def _build_control_from_param(p, params_by_key, level_page=False):
    """Build a control dict from a parameter dict."""
    widget = _widget_for_param(p, level_page)
    ctrl = {"param": p["name"], "widget": widget}
    if widget in ("knob", "fader") and _polarity_for_param(p) == "bipolar":
        ctrl["polarity"] = "bipolar"
    if widget == "slider":
        ctrl["overlay"] = "menu"
    return ctrl


def _knob_to_control(knob, params_by_key, seen_params, level_page=False):
    """Convert a knob dict to a control dict, or 'empty'.
    
    Deduplicates by parameter name. Skips unknown keys and 'empty' keys.
    """
    key = knob.get("key", "")
    if key == "empty" or key not in params_by_key:
        return "empty"

    p = params_by_key[key]
    param_name = p["name"]

    # Skip if we've already included this param (deduplication)
    if param_name in seen_params:
        return "empty"
    seen_params.add(param_name)

    return _build_control_from_param(p, params_by_key, level_page)


def _write_hostsim(params_doc, out_dir):
    """Write params-hostsim.json (every parameter, in table order). Returns (path, number of parameters)."""
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

    return hostsim_path, len(hostsim_params)


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

    hostsim_path, hostsim_count = _write_hostsim(params_doc, out_dir)

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
                        ctrl = _knob_to_control(knob, params_by_key, dropped_keys, _is_level_page(kick_page, params_by_key))
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
    print("gen-mpc-ui: Wrote %d params to %s" % (hostsim_count, hostsim_path))

    return 0


def generate_layout(params_path, layout_path, pages_path, out_dir, names_path=DEFAULT_SHORT_NAMES):
    """Generate ui.json and params-hostsim.json from a layout file.
    
    The layout file specifies which Kick pages (from docs/kick-pages.json)
    go on each MPC page, with the slot rule for parameter placement.
    """
    with open(params_path, encoding="utf-8") as f:
        params_doc = json.load(f)

    with open(layout_path, encoding="utf-8") as f:
        layout_doc = json.load(f)

    with open(pages_path, encoding="utf-8") as f:
        pages_doc = json.load(f)

    # Build a lookup from param key to param dict
    params_by_key = {}
    # Build a lookup from param name to param dict (for O(1) lookup)
    params_by_name = {}
    for p in params_doc.get("params", []):
        params_by_key[p["key"]] = p
        params_by_name[p["name"]] = p

    # Build a lookup from kick page id to kick page dict
    kick_by_id = {}
    for kp in pages_doc.get("pages", []):
        kick_by_id[kp["id"]] = kp

    # ---- Validation (rules a-h) ----
    
    # a. format check
    if layout_doc.get("format") != "synthyboy-mpc-ui-pages":
        print("gen-mpc-ui: ERROR: format is not 'synthyboy-mpc-ui-pages'", file=sys.stderr)
        return 2

    layout_pages = layout_doc.get("pages", [])

    # b. more than 32 pages
    if len(layout_pages) > MAX_PAGES:
        print("gen-mpc-ui: ERROR: more than 32 pages in layout", file=sys.stderr)
        return 2

    # c & d. page name validation (empty/long + duplicate)
    seen_page_names = set()
    for page_entry in layout_pages:
        name = page_entry.get("name", "")
        if not name or len(name) > MAX_PAGE_NAME:
            print("gen-mpc-ui: ERROR: page name '%s' is invalid (must be 1-%d characters)"
                  % (name, MAX_PAGE_NAME), file=sys.stderr)
            return 2
        if name in seen_page_names:
            print("gen-mpc-ui: ERROR: duplicate page name '%s'" % name, file=sys.stderr)
            return 2
        seen_page_names.add(name)

    # e. kick list must have one or two ids
    for page_entry in layout_pages:
        kick_ids = page_entry.get("kick", [])
        if not isinstance(kick_ids, list) or len(kick_ids) < 1 or len(kick_ids) > 2:
            print("gen-mpc-ui: ERROR: page '%s' kick must contain one or two ids"
                  % page_entry.get("name", "?"), file=sys.stderr)
            return 2

    # f. each kick id must exist in kick-pages.json
    for page_entry in layout_pages:
        for kid in page_entry.get("kick", []):
            if kid not in kick_by_id:
                print("gen-mpc-ui: ERROR: kick page id '%s' not found in kick-pages.json" % kid,
                      file=sys.stderr)
                return 2

    # g. each kick page id used at most once in the whole layout
    all_kick_ids = []
    for page_entry in layout_pages:
        all_kick_ids.extend(page_entry.get("kick", []))
    seen_kick_ids = set()
    for kid in all_kick_ids:
        if kid in seen_kick_ids:
            print("gen-mpc-ui: ERROR: kick page id '%s' used more than once" % kid,
                  file=sys.stderr)
            return 2
        seen_kick_ids.add(kid)

    # h. two kick pages of one MPC page must have same section
    for page_entry in layout_pages:
        kick_ids = page_entry.get("kick", [])
        if len(kick_ids) >= 2:
            section0 = kick_by_id[kick_ids[0]].get("section", "")
            section1 = kick_by_id[kick_ids[1]].get("section", "")
            if section0 != section1:
                print("gen-mpc-ui: ERROR: kick pages '%s' and '%s' have different sections"
                      % (kick_ids[0], kick_ids[1]), file=sys.stderr)
                return 2

    # ---- Build pages (rule 4) ----
    
    ui_pages = []
    meta_tabs = []
    all_seen_names = set()  # Track all placed parameter names across pages
    overrides = load_short_names(names_path)
    for key in overrides:
        if key not in params_by_key:
            print("gen-mpc-ui: ERROR: %s: unknown parameter key '%s'" % (os.path.basename(names_path), key), file=sys.stderr)
            return 2

    for page_entry in layout_pages:
        page_name = page_entry.get("name", "")
        kick_ids = page_entry.get("kick", [])

        # Build 16 slots for this page
        slots = []
        name_entries, placed_keys = [], set()
        fader_names = set()  # parameters taken from a level page (_is_level_page): their continuous controls are faders
        for kid in kick_ids:
            kick_page = kick_by_id[kid]
            level_page = _is_level_page(kick_page, params_by_key)
            knob_params = []
            for knob in kick_page.get("knobs", []):
                if knob is None:
                    continue
                key = knob.get("key", "")
                if key not in params_by_key:
                    continue
                p = params_by_key[key]
                if not p.get("ownEngine"):
                    continue
                name_entries.append((key, p["name"], knob.get("shortLabel", "")))
                param_name = p["name"]
                if param_name in all_seen_names:
                    continue
                all_seen_names.add(param_name)
                placed_keys.add(key)
                if level_page:
                    fader_names.add(param_name)
                knob_params.append(p)
            # Pad to 8 slots
            slots += [p["name"] for p in knob_params] + [None] * (8 - len(knob_params))
        # Pad to 16 slots total
        slots += [None] * (16 - len(slots))

        # Build controls from slots
        controls = []
        for slot_name in slots:
            if slot_name is None:
                controls.append("empty")
            else:
                p = params_by_name[slot_name]
                controls.append(_build_control_from_param(p, params_by_key, slot_name in fader_names))

        ui_pages.append({"name": page_name, "controls": controls})
        try:
            names = resolve_tab_names(name_entries, placed_keys, overrides)
        except ValueError as e:
            print("gen-mpc-ui: ERROR: page '%s': %s" % (page_name, e), file=sys.stderr)
            return 2
        meta_tabs.append({"name": page_name, "placed": sorted(placed_keys), "names": names})

    # i. after applying rule 4, no page with no control at all
    for page_entry in layout_pages:
        page_name = page_entry.get("name", "")
        page = next(p for p in ui_pages if p["name"] == page_name)
        if all(c == "empty" for c in page["controls"]):
            print("gen-mpc-ui: ERROR: page '%s' has no control" % page_name, file=sys.stderr)
            return 2

    # ---- Write output files ----
    
    hostsim_path, hostsim_count = _write_hostsim(params_doc, out_dir)

    # Write ui.json
    ui_doc = {"pages": ui_pages}
    ui_path = os.path.join(out_dir, "ui.json")
    with open(ui_path, "w", encoding="utf-8") as f:
        json.dump(ui_doc, f, indent=2, ensure_ascii=False)
        f.write("\n")

    # skin-meta.json: what the MPC skin extras (scripts/mpc-skin-extras/) need to know that ui.json cannot carry
    meta_path = os.path.join(out_dir, "skin-meta.json")
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump({"format": "synthyboy-mpc-skin-meta", "version": 1, "tabs": meta_tabs}, f, indent=2, ensure_ascii=False)
        f.write("\n")

    # Report omitted own-engine parameters
    own_engine_params = [p for p in params_doc.get("params", []) if p.get("ownEngine")]
    omitted = sum(1 for p in own_engine_params if p["name"] not in all_seen_names)
    print("gen-mpc-ui: omitted own-engine parameters: %d" % omitted)

    print("gen-mpc-ui: Wrote %d pages (%d controls/page max) to %s"
          % (len(ui_pages), MAX_CONTROLS_PER_PAGE, ui_path))
    print("gen-mpc-ui: Wrote %d params to %s" % (hostsim_count, hostsim_path))

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
    ap.add_argument("--layout", default=None,
                    help="Layout file (synthyboy-mpc-ui-pages format) for own-engine MPC pages")
    ap.add_argument("--short-names", default=DEFAULT_SHORT_NAMES,
                    help="names of controls that override the Kick shortLabel (--layout mode; default scripts/mpc-short-names.json)")
    args = ap.parse_args(argv)

    # Validate: --layout and --page-ids are mutually exclusive
    if args.layout and args.page_ids:
        print("gen-mpc-ui: ERROR: --layout and --page-ids are mutually exclusive", file=sys.stderr)
        return 2

    os.makedirs(args.out, exist_ok=True)

    if args.layout:
        return generate_layout(args.params, args.layout, args.pages, args.out, args.short_names)
    else:
        # Parse page_ids if provided
        page_ids = None
        if args.page_ids:
            page_ids = [pid.strip() for pid in args.page_ids.split(",")]

        return generate(args.params, args.pages, args.out, page_ids)


if __name__ == "__main__":
    sys.exit(main())
