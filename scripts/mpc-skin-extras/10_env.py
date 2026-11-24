import sys as _token_sys
from pathlib import Path as _TokenPath
_token_root = str(_TokenPath(__file__).resolve().parents[2])
if _token_root not in _token_sys.path:
    _token_sys.path.insert(0, _token_root)
from components import style as _ui_style

# SynthyBoy - MPC skin extra: editable envelope curves (SDK Envelope, envelopeType DAHDSR) on the tab Env (the one Env tab of the layout since 2026-11-21: the selector ENV 1 .. 6 is the plugin page's; this module serves the tab's host widgets of ENV 1, which the overlay skin removes again in 97_no_fallback.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Per tab: a full-width Envelope (rows 1-2 of the 4 x 4 grid) in a display component that opens a full-screen
# EnvelopeOverlay on double tap / Enter; the 8 knobs of the envelope sit in rows 3-4, and the tab's Q-Link map is rewritten
# so that those knobs are Q-Link 1-8 (Bank Direction "Column": Q-Link 4*(3-row)+col+1, row 0 = top, col 0 = left).
# The stage handles are named (Delay .. Release) and resolved by the remap of the tab entry; the three shape handles are
# bound to the curve parameters. The Envelope and the EnvelopeOverlay carry the same complete key set (the factory
# Sub Factory DAHDSR set): on the Key 61 (MPC 3.10.1.12) an EnvelopeOverlay without the shape keys drops the whole TUI.json
# (docs/MPC.md).

import copy

COLS = tuple(_ui_style.size('screen.env.cols.rect'))
ROWS = tuple(_ui_style.size('screen.env.rows.rect'))
KNOB_W, KNOB_H = tuple(_ui_style.size('screen.env.metric.size'))
ENV_BOUNDS = tuple(_ui_style.size('screen.env.env_bounds.rect'))   # above the Q-Link badge zone's frame top (the frame of the lower two rows starts at y 301)
# order of the knobs in the grid cells (row 2, then row 3): (name suffix, params.json suffix, template knob)
KNOBS = (
    ("Delay", "delay"), ("Attack", "attack"), ("Hold", "hold"), ("Decay", "decay"),
    ("Sustain", "sustain"), ("Release", "release"), ("Attack Curve", "attack_curve"), ("Decay Curve", "decay_curve"),
)
STAGES = (  # handle name inside the envelope components, params.json suffix (also the order of the SDK handle keys)
    ("Delay", "delay"), ("Attack", "attack"), ("Hold", "hold"),
    ("Decay", "decay"), ("Sustain", "sustain"), ("Release", "release"),
)
DUP = "\x00"  # second "version" key of the overlay widgets (see mpc-skin-extras/README.md)
SHAPES = (("AttackShape", "attack_curve"), ("DecayShape", "decay_curve"), ("ReleaseShape", "release_curve"))
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = tuple(_ui_style.size('screen.env.metric.size.2'))   # x and width of the frames: 90_frames.py


def _style(big):
    return {
        "backgroundColour": _ui_style.argb('menu.background'), "scaleColour": "0", "lineColour": _ui_style.argb('focus.field'), "fillColour": _ui_style.argb('focus.field', 64),
        "highlightColour": _ui_style.argb('text.white'), "lineWidth": _ui_style.size("env.curve.line_width")[int(big)], "handleWidth": _ui_style.size("env.curve.handle_width")[int(big)],
        "handleHeight": _ui_style.size("env.curve.handle_height")[int(big)], "handleShape": "Circle",
        "handleLineWidth": _ui_style.size('screen.env.handlelinewidth'), "handleLineColour": _ui_style.argb('text.white'), "handleFillColour": _ui_style.argb('menu.background'), "handleTextColour": _ui_style.argb('text.white'),
        "handleLineWidthOnPressed": _ui_style.size('screen.env.handlelinewidthonpressed'), "handleLineColourOnPressed": _ui_style.argb('text.white'), "handleFillColourOnPressed": _ui_style.argb('focus.field'),
        "handleTextColourOnPressed": _ui_style.argb('text.white'), "handleArrowLineColourOnPressed": _ui_style.argb('text.white'),
        "handleArrowFillColourOnPressed": _ui_style.argb('focus.field'),
    }


def _envelope_data(n, overlay):
    data = {"version": 1}
    if overlay:
        data["version" + DUP + "1"] = 1
        data["actions"] = [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost",
                            "additionalData": ""}]
    data.update({"envelopeType": "DAHDSR", "envelopeStyle": "WithHeader", "envelopeName": "ENV %d" % n})
    data.update(_style(overlay))
    for handle, _ in STAGES:
        data[handle[0].lower() + handle[1:] + "Handle"] = handle
    for handle, _ in SHAPES:
        data[handle[0].lower() + handle[1:] + "Handle"] = handle
    data.update({"forceHideShapeHandles": False, "forceCurveShape": 0.5, "decayAndReleaseCurvesLinked": False,
                 "curveType": "Quadratic"})
    return data


def _bounds(rect, version=1, focus="No", show="Hide"):
    out = {"version": version, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": "Always",
           "boundsType": "Absolute", "bounds": "%d %d %d %d" % rect}
    if version == 2:  # version 2 of the bounds object requires this key (ui-reference 3.x, "Bounds")
        out["additionalInvalidatingHandles"] = []
    return out


def _entry(name, typ, data, remap, bounds):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": remap}, "bounds": bounds}


def _definition(children, actions, ignore_mouse):
    nocolour = {"version": 1, "colour": "0", "image": ""}
    return {"version": 4, "actions": actions,
            "backgroundData": {"version": 1, "focussed": dict(nocolour), "unfocussed": dict(nocolour)},
            "ignoreMousePresses": ignore_mouse, "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": False,
            "componentsData": children}


def _overlay_actions(overlay_name):
    return [{"version": 2, "onAction": action, "handler": "Show Overlay", "handleName": "", "additionalData": overlay_name,
             "handle remapping": {"version": 1, "map": []}} for action in ("Double Click", "Enter Pressed")]


def _add_definitions(ctx, n):
    display, overlay = "envDisplay%d" % n, "envOverlay%d" % n
    w, h = ENV_BOUNDS[2:]
    ctx.add_component_definition(display, _definition(
        [_entry("Envelope", "Envelope", _envelope_data(n, False), [], _bounds((0, 0, w, h), 2))],
        _overlay_actions(overlay), False))
    ctx.add_component_definition(overlay, _definition(
        [_entry("Envelope Overlay", "EnvelopeOverlay", _envelope_data(n, True), [],
                _bounds(tuple(_ui_style.size('screen.env.add_definitions.rect')), 2, "Yes", "Show"))], [], True))
    return display


def _qlink(row, col):
    return "Q-Link %d" % (4 * (3 - row) + col + 1)


def apply(tui, qlinks, ctx):
    for n, tab_name in ((1, "Env"),):
        tab = ctx.tab(tab_name)
        items = tab["componentDefinition"]["componentsData"]
        by_name = {c["componentData"]["name"]: c for c in items}
        prefix = "Env %d " % n
        extra = set(by_name) - {prefix + s for s, _ in KNOBS}
        if extra:
            raise ValueError("unexpected components on tab %r: %s" % (tab_name, sorted(extra)))
        # the generator puts a parameter on one tab only (env1.release is on Macros): give this tab its own knob
        for suffix, key in KNOBS:
            if prefix + suffix not in by_name:
                knob = copy.deepcopy(by_name[prefix + "Sustain"])
                knob["componentData"]["name"] = prefix + suffix
                knob["handle remapping"]["map"][0]["value"] = "Parameter %d" % ctx.param_index("env%d.%s" % (n, key))
                by_name[prefix + suffix] = knob
        # re-place the knobs in rows 3-4 and rebuild the Q-Link map of the tab
        row_map = next(m for m in qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1)
        links = {"Q-Link %d" % i: -1 for i in range(1, 17)}
        items[:] = []
        for i, (suffix, key) in enumerate(KNOBS):
            row, col = 2 + i // 4, i % 4
            knob = by_name[prefix + suffix]
            knob["bounds"]["bounds"] = "%d %d %d %d" % (COLS[col], ROWS[row], KNOB_W, KNOB_H)
            param = knob["handle remapping"]["map"][0]["value"]
            if param != "Parameter %d" % ctx.param_index("env%d.%s" % (n, key)):
                raise ValueError("%s is bound to %s, not env%d.%s" % (knob["componentData"]["name"], param, n, key))
            links[_qlink(row, col)] = ctx.param_index("env%d.%s" % (n, key))
            items.append(knob)
        row_map["Q-Links"] = links
        # the Q-Link frames mark the lower two rows only (below the orange badge, which sits at the top left corner of a frame)
        tab["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H)
                                  for b in tab["qlinkBoundsData"]]
        display = _add_definitions(ctx, n)
        remap = [{"key": handle, "value": "Parameter %d" % ctx.param_index("env%d.%s" % (n, key))} for handle, key in STAGES + SHAPES]
        items.append(_entry("Env %d Curve" % n, display, {"version": 1, "handleName": "Attack"}, remap, _bounds(ENV_BOUNDS, 1)))
