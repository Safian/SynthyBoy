# SynthyBoy - MPC skin extra: editable envelope curves (SDK Envelope, envelopeType DAHDSR) on the Env tabs.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# GATE VERSION (hardware probe): only env1 on "Env 1-2". The env1 knobs stay; the Envelope takes the area of the
# columns 2-4 / rows 1-2, so the five knobs that stood there move down into the rows 3-4 (where the env2 knobs stood;
# those are removed from this probe tab only, the Q-Links.json is untouched). Layout of the final version: see docs/MPC.md.

TAB = "Env 1-2"
ENV_BOUNDS = "343 45 918 295"
# knob name -> new "x y" (cells of the 4 x 4 grid of the tab, x = 23 343 663 983, y = 45 196 346 497)
MOVE = {
    "Env 1 Hold": "343 346",
    "Env 1 Decay": "663 346",
    "Env 1 Sustain": "983 346",
    "Env 1 Attack Curve": "343 497",
    "Env 1 Decay Curve": "663 497",
}

STAGES = (  # handle key of the Envelope, params.json key suffix
    ("delayHandle", "delay"), ("attackHandle", "attack"), ("holdHandle", "hold"),
    ("decayHandle", "decay"), ("sustainHandle", "sustain"), ("releaseHandle", "release"),
)


def envelope_entry(ctx, n):
    data = {
        "version": 1, "envelopeType": "DAHDSR", "envelopeStyle": "WithHeader", "envelopeName": "ENV %d" % n,
        "backgroundColour": "ff1e1d1e", "scaleColour": "ff3a3a3a", "lineColour": "ffd31145", "fillColour": "40d31145",
        "highlightColour": "ffffffff", "lineWidth": 4, "handleWidth": 44, "handleHeight": 32, "handleShape": "Circle",
        "handleLineWidth": 2, "handleLineColour": "ffffffff", "handleFillColour": "ff1e1d1e", "handleTextColour": "ffffffff",
        "handleLineWidthOnPressed": 2, "handleLineColourOnPressed": "ffffffff", "handleFillColourOnPressed": "ffd31145",
        "handleTextColourOnPressed": "ffffffff", "handleArrowLineColourOnPressed": "ffffffff",
        "handleArrowFillColourOnPressed": "ffd31145",
    }
    for handle, stage in STAGES:
        data[handle] = "Parameter %d" % ctx.param_index("env%d.%s" % (n, stage))
    data.update({"attackShapeHandle": "Invalid", "decayShapeHandle": "Invalid", "releaseShapeHandle": "Invalid",
                 "forceHideShapeHandles": True, "forceCurveShape": 0.5, "curveType": "Quadratic"})
    return {
        "version": 2,
        "componentData": {"version": 1, "name": "Env %d Curve" % n, "type": "Envelope", "data": data},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
                   "boundsType": "Absolute", "bounds": ENV_BOUNDS},
    }


def apply(tui, qlinks, ctx):
    items = ctx.tab(TAB)["componentDefinition"]["componentsData"]
    items[:] = [c for c in items if not c["componentData"]["name"].startswith("Env 2 ")]
    seen = set()
    for c in items:
        name = c["componentData"]["name"]
        if name in MOVE:
            x, y = MOVE[name].split()
            w, h = c["bounds"]["bounds"].split()[2:]
            c["bounds"]["bounds"] = "%s %s %s %s" % (x, y, w, h)
            seen.add(name)
    if seen != set(MOVE):
        raise ValueError("knobs not found on %r: %s" % (TAB, sorted(set(MOVE) - seen)))
    items.append(envelope_entry(ctx, 1))
