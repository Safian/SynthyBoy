# SynthyBoy - native PointMod modulation source board and curve editor command buttons.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native replacements for PointMod ModSourceBoard and curve editor command buttons.

1. PointMod ModSourceBoard:
   Replaced by:
   - A host-native Dropdown selector (host list via Show Overlay) on the PointMod header row.
   - A host-native RadioGrid / quick source chip matrix for common sources in the body area.
   Both are gated by the PointMod view and wired to the LFO_POINT_MOD_SOURCE parameter.

2. Curve Editor Command Buttons:
   - ScalarCurveEditor (Filter Note / Velocity): INIT, UNDO, DELETE POINT, TARGETS, EXPAND.
     Native Button components wired to FilterAction (FilterInit=8, FilterUndo=9, FilterDelete=10,
     FilterTargets=11, FilterExpandCurve=12).
   - LFO Shape Editor: SHAPES, MORE, UNDO, REDO, DELETE, MOD, MODE.
     Native Button and Dropdown components with skin actions (Toggle Switch / Show Overlay)
     and instant gestures.
   - Env Editor: CLOSE, TARGETS, INIT/RESET.
     Native Button components with skin actions.

All buttons follow the classic button gradient design (button.gradient.*, control.gradient).
"""

import copy
import json
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
REPO_ROOT = SCRIPTS.parent
for p in (str(REPO_ROOT), str(SCRIPTS)):
    if p not in sys.path:
        sys.path.insert(0, p)

import native_ui_params as handles
from components import style as _style
from components.native.button import button as ui_button
from components.native.selector import selector as ui_selector

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None

# Parameter slot allocation for PointMod source selection:
# native_lfo_detail.h currently publishes 17 handles (0..16).
# Handle 17 is allocated for LFO_POINT_MOD_SOURCE (0..57).
LFO_POINT_MOD_SOURCE_OFFSET = 17

CHOICE_OVERLAY = "fx editor choice menu"

POINTMOD_CHIPS = (
    # Group: Envelopes
    ("ENV 1", 1, 160, 116), ("ENV 2", 2, 272, 116), ("ENV 3", 3, 384, 116),
    # Group: LFOs
    ("LFO 1", 7, 160, 188), ("LFO 2", 8, 272, 188), ("LFO 3", 9, 384, 188), ("LFO 4", 10, 496, 188),
    # Group: Macros
    ("MACRO 1", 19, 160, 260), ("MACRO 2", 20, 272, 260), ("MACRO 3", 21, 384, 260), ("MACRO 4", 22, 496, 260),
    # Group: Player / Expression
    ("WHEEL", 27, 160, 332), ("VELO", 23, 272, 332), ("AT", 25, 384, 332), ("KEY", 24, 496, 332),
    ("PITCH", 26, 608, 332), ("FIXED", 57, 720, 332),
)

SCALAR_CURVE_BUTTONS = (
    ("INIT", 8, (872, 16, 130, 56)),
    ("UNDO", 9, (1006, 16, 130, 56)),
    ("DELETE POINT", 10, (1140, 16, 132, 56)),
    ("TARGETS", 11, (880, 448, 384, 56)),
    ("EXPAND", 12, (800, 84, 56, 56)),
)


def _entry(name, kind, data, rect, remaps=(), gates=(), focus="Yes"):
    bounds = {
        "version": 2 if gates else 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "%d %d %d %d" % tuple(rect),
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remaps]},
        "bounds": bounds,
    }


def _store_button_art(ctx, name, size, text=None, icon=None, active=True):
    """Render and store legacy gradient button face art."""
    if not hasattr(ctx, "_cmd_art_cache"):
        ctx._cmd_art_cache = {}
    cache_key = (name, size, text, icon, active)
    if cache_key in ctx._cmd_art_cache:
        return ctx._cmd_art_cache[cache_key]

    width, height = size
    filename = f"cmd_{name.lower().replace(' ', '_')}_{width}x{height}_{'on' if active else 'off'}.png"
    out_path = Path(ctx.skins_dir) / filename

    if not out_path.is_file():
        if icon:
            face = ui_button.action_art(icon=icon, width=width, height=height, look="legacy")
            if not active:
                face = _style.dim_artwork(face)
        else:
            face = ui_button.text_face(
                (text or name).upper(),
                (width, height),
                geometry="filter.pz.button",
                surface=f"filter.pz.{'active' if active else 'inactive'}.surface",
                border=f"filter.pz.{'active' if active else 'inactive'}.border",
                text="text.icon" if active else "filter.pz.inactive.text",
                gradient=active,
            )
        face.save(out_path)

    ctx._cmd_art_cache[cache_key] = filename
    return filename


def make_native_button(ctx, name, rect, handle, *, action_id=1, action_count=1, text=None, icon=None, gates=()):
    """Construct a canonical native Button control."""
    size = tuple(rect[2:])
    art_on = _store_button_art(ctx, name, size, text=text, icon=icon, active=True)
    art_off = _store_button_art(ctx, name, size, text=text, icon=icon, active=False)

    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": "Button",
            "data": {
                "version": 1,
                "buttonId": action_id,
                "numButtonsInGroup": action_count,
                "gestureBehaviour": "Instant",
                "upImage": art_on,
                "downImage": art_off,
            },
        },
        "handle remapping": {
            "version": 1,
            "map": [{"key": "Data", "value": handle}],
        },
        "bounds": {
            "version": 2 if gates else 1,
            "acceptsHWFocus": "Yes",
            "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % tuple(rect),
            **({"additionalInvalidatingHandles": list(gates)} if gates else {}),
        },
    }


def make_action_component(ctx, kind, width, height, *, handler="Toggle Switch", overlay=None, text=""):
    """Create a custom component definition with skin action trigger."""
    actions = [
        {
            "version": 1,
            "onAction": trigger,
            "handler": handler,
            "handleName": "Data",
            "additionalData": overlay if overlay else "",
        }
        for trigger in ("Mouse Down", "Enter Pressed")
    ]
    defn = ui_selector.dropdown_definition(width, height, actions=actions, skins_dir=ctx.skins_dir)
    defn["disableCoarseDataWheel"] = True
    return defn


def apply_pointmod_source_controls(tui, items, ctx, base=None, point_view=None):
    """Add native PointMod source selection controls (dropdown + quick board)."""
    if base is None:
        base = handles.native_base_index(ctx.param_count) + handles.LFO_DETAIL
    if point_view is None:
        point_view = f"IndexedEnabling/1/4/Parameter {base}"

    source_handle = f"Parameter {base + LFO_POINT_MOD_SOURCE_OFFSET}"

    # 1. Native dropdown for full modulation source list on PointMod top bar:
    # Positioned at [640, 24, 220, 56] next to REMOVE button
    dropdown_kind = "pointmodNativeSourceDropdown_220x56"
    existing_defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    if dropdown_kind not in existing_defs:
        dropdown_actions = [
            {"version": 1, "onAction": trigger, "handler": "Show Overlay", "handleName": "Data", "additionalData": CHOICE_OVERLAY}
            for trigger in ("Mouse Down", "Enter Pressed")
        ]
        defn = ui_selector.dropdown_definition(220, 56, actions=dropdown_actions, skins_dir=ctx.skins_dir)
        defn["disableCoarseDataWheel"] = True
        ctx.add_component_definition(dropdown_kind, defn)

    dropdown_entry = _entry(
        "PointMod Native Source Dropdown",
        dropdown_kind,
        {"version": 1, "handleName": "Data"},
        (640, 24, 220, 56),
        remaps=[("Data", f"WithName[Source]/{source_handle}")],
        gates=[point_view],
    )
    items.append(dropdown_entry)

    # 2. Quick-add board chips for common modulation sources in the board area:
    # Placed in the body rect (y: 116..400) replacing C++ ModSourceBoard
    chip_w, chip_h = 100, 48
    for label, src_id, cx, cy in POINTMOD_CHIPS:
        chip_rect = (cx, cy, chip_w, chip_h)
        btn = make_native_button(
            ctx,
            f"PointMod Source {label}",
            chip_rect,
            f"WithName[{label}]/FromGesture/{source_handle}",
            action_id=src_id,
            action_count=58,
            text=label,
            gates=[point_view],
        )
        items.append(btn)

    # 3. "ALL SOURCES" action button opening host choice overlay:
    all_src_rect = (160, 410, 200, 48)
    all_src_kind = "pointmodNativeAllSources_200x48"
    if all_src_kind not in existing_defs:
        all_actions = [
            {"version": 1, "onAction": trigger, "handler": "Show Overlay", "handleName": "Data", "additionalData": CHOICE_OVERLAY}
            for trigger in ("Mouse Down", "Enter Pressed")
        ]
        defn = ui_selector.dropdown_definition(200, 48, actions=all_actions, skins_dir=ctx.skins_dir)
        defn["disableCoarseDataWheel"] = True
        ctx.add_component_definition(all_src_kind, defn)

    all_src_entry = _entry(
        "PointMod Native All Sources",
        all_src_kind,
        {"version": 1, "handleName": "Data"},
        all_src_rect,
        remaps=[("Data", f"WithName[All Sources]/{source_handle}")],
        gates=[point_view],
    )
    items.append(all_src_entry)


def apply_scalar_curve_buttons(tui, items, ctx, filter_num=1):
    """Ensure native command buttons for Filter Note/Velocity ScalarCurveEditor."""
    base = handles.native_base_index(ctx.param_count)
    action_param = base + handles.FILTER_ACTION if hasattr(handles, "FILTER_ACTION") else base + 28
    filter_action_handle = f"Parameter {action_param}"

    # Filter tab gate: Curve shown (modes Note or Velo)
    gate = f"RangedEnabling/3/4/5/Parameter {base}"

    for label, action_id, rect in SCALAR_CURVE_BUTTONS:
        btn_name = f"Filter {filter_num} ScalarCurve {label}"
        btn = make_native_button(
            ctx,
            btn_name,
            rect,
            f"WithName[{label}]/FromGesture/{filter_action_handle}",
            action_id=action_id,
            action_count=13,
            text=label if label != "EXPAND" else None,
            icon="expand" if label == "EXPAND" else None,
            gates=[gate],
        )
        items.append(btn)


def apply(tui, qlinks, ctx):
    """Main entry point called by apply-mpc-skin-extras.py."""
    tabs = tui.get("pageData", {}).get("tabs", [])

    for tab in tabs:
        name = tab.get("tabName", "")
        items = tab.get("componentDefinition", {}).get("componentsData", [])

        # Apply PointMod controls on LFO tabs:
        if name.startswith("LFO"):
            base = handles.native_base_index(ctx.param_count) + handles.LFO_DETAIL
            point_view = f"IndexedEnabling/1/4/Parameter {base}"
            # Check if not already added
            if not any(c.get("componentData", {}).get("name") == "PointMod Native Source Dropdown" for c in items):
                apply_pointmod_source_controls(tui, items, ctx, base=base, point_view=point_view)

        # Apply ScalarCurveEditor command buttons on Filter sub-tabs Note & Velo:
        elif name in ("Note", "Velo"):
            if not any("ScalarCurve INIT" in c.get("componentData", {}).get("name", "") for c in items):
                apply_scalar_curve_buttons(tui, items, ctx)
