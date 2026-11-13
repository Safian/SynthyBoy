# SynthyBoy - MPC skin extra: editable envelope curves (SDK Envelope, envelopeType DAHDSR) on the tabs Env 1 .. Env 4.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Per tab: a full-width Envelope (rows 1-2 of the 4 x 4 grid) in a display component that opens a full-screen
# EnvelopeOverlay on double tap / Enter; the 8 knobs of the envelope sit in rows 3-4, and the tab's Q-Link map is rewritten
# so that those knobs are Q-Link 1-8 (Bank Direction "Column": Q-Link 4*(3-row)+col+1, row 0 = top, col 0 = left).
# The Envelope draws straight segments between the stage handles; the curve parameters stay knobs. Layout: docs/MPC.md.

import copy

COLS = (23, 343, 663, 983)
ROWS = (45, 196, 346, 497)
KNOB_W, KNOB_H = 278, 107
ENV_BOUNDS = (23, 45, 1238, 290)
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
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = 336, 288


def _style(big):
    return {
        "backgroundColour": "ff1e1d1e", "scaleColour": "0", "lineColour": "ffd31145", "fillColour": "40d31145",
        "highlightColour": "ffffffff", "lineWidth": 4 if not big else 6, "handleWidth": 44 if not big else 64,
        "handleHeight": 32 if not big else 46, "handleShape": "Circle",
        "handleLineWidth": 2, "handleLineColour": "ffffffff", "handleFillColour": "ff1e1d1e", "handleTextColour": "ffffffff",
        "handleLineWidthOnPressed": 2, "handleLineColourOnPressed": "ffffffff", "handleFillColourOnPressed": "ffd31145",
        "handleTextColourOnPressed": "ffffffff", "handleArrowLineColourOnPressed": "ffffffff",
        "handleArrowFillColourOnPressed": "ffd31145",
    }


def _envelope_data(n, overlay):
    data = {"version": 1}
    if overlay:
        data[("version" + DUP) + "1"] = 1
        data["actions"] = [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost",
                            "additionalData": ""}]
    data.update({"envelopeType": "DAHDSR", "envelopeStyle": "WithHeader", "envelopeName": "ENV %d" % n})
    data.update(_style(overlay))
    for handle, _ in STAGES:
        data[handle[0].lower() + handle[1:] + "Handle"] = handle
    if not overlay:
        data.update({"attackShapeHandle": "Invalid", "decayShapeHandle": "Invalid", "releaseShapeHandle": "Invalid",
                     "forceHideShapeHandles": True, "forceCurveShape": 0.5})
    data["curveType"] = "Quadratic"
    return data


def _bounds(rect, version=1, focus="No", show="Hide"):
    return {"version": version, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": "Always",
            "boundsType": "Absolute", "bounds": "%d %d %d %d" % rect}


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
                _bounds((0, 0, 1280, 800), 2, "Yes", "Show"))], [], True))
    return display


def _qlink(row, col):
    return "Q-Link %d" % (4 * (3 - row) + col + 1)


def apply(tui, qlinks, ctx):
    for n in (1, 2, 3, 4):
        tab = ctx.tab("Env %d" % n)
        items = tab["componentDefinition"]["componentsData"]
        by_name = {c["componentData"]["name"]: c for c in items}
        prefix = "Env %d " % n
        extra = set(by_name) - {prefix + s for s, _ in KNOBS}
        if extra:
            raise ValueError("unexpected components on tab 'Env %d': %s" % (n, sorted(extra)))
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
        # the Q-Link frames mark the lower two rows only
        tab["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H)
                                  for b in tab["qlinkBoundsData"]]
        display = _add_definitions(ctx, n)
        remap = [{"key": handle, "value": "Parameter %d" % ctx.param_index("env%d.%s" % (n, key))} for handle, key in STAGES]
        items.append(_entry("Env %d Curve" % n, display, {"version": 1, "handleName": "Attack"}, remap, _bounds(ENV_BOUNDS, 1)))
