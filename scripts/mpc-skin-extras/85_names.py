# SynthyBoy - MPC skin extra: the short names of the controls.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The tab says which module a control belongs to, so the name over a control is short ("Detune Curve", not "Osc 1 Unison Detune Curve"). The host draws the
# name of a knob, toggle, fader or radio group from the parameter name (a Label of type Name in the shared component definition); this module removes that
# child from the definitions and puts a fixed text (Label type Text) over every such control instead, at the place where the host drew the name. The names come
# from skin-meta.json (scripts/gen-mpc-ui.py: Kick page shortLabel, scripts/mpc-short-names.json, prefix on a clash inside a tab); run with --meta.
# Style: the weight of the headers the host draws over the selectors ("OSC 1 MORPH TYPE": upper case, bold, grey): Titillium Web Bold NAME_FONT px,
# upper case, the grey of the skin (the SDK's "muted" colour ff6a6a6c); the value below keeps the white 24 px of 80_layout.py. A label of a control that
# is only visible for some value of another parameter (bounds version 2, IndexedEnabling) carries the same additionalInvalidatingHandles.
# The selectors (sliderWithFocus, sliderWithFocusMenu) have no name child: the SDK Slider draws the parameter name itself and has no key for another text.
# The full parameter name stays where the host shows it by itself (the header of the knob and menu overlays).
# Runs after 80_layout.py (which places the slots) and before 90_frames.py.

NAME_FONT = 22.0
NAME_STYLE = "Bold"
NAME_COLOUR = "ff6a6a6c"
NAME_H = 26
NAME_X, NAME_W = 3, 272          # inside the slot of 278 px: the inner width of the Q-Link frame
NAME_DY = {"knobUni": 0, "knobBi": 0, "toggle": 0, "faderUni": 4, "faderBi": 4, "radio3": 6, "radio4": 6}   # y of the name inside the slot
CENTRED = "horizontallyCentred verticallyCentred"


def _label_data(text):
    return {"version": 1,
            "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": NAME_STYLE, "height": NAME_FONT},
                          "colour": NAME_COLOUR, "justification": CENTRED, "case": "Upper Case"},
            "type": "Text", "handleName": "Data", "text": text}


def _rect(component):
    return tuple(int(v) for v in component["bounds"]["bounds"].split())


def _label_entry(control, text):
    x, y, w, h = _rect(control)
    bounds = control["bounds"]
    out = {"version": bounds["version"], "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
           "boundsType": "Absolute",
           "bounds": "%d %d %d %d" % (x + NAME_X, y + NAME_DY[control["componentData"]["type"]], NAME_W, NAME_H)}
    if "additionalInvalidatingHandles" in bounds:
        out["additionalInvalidatingHandles"] = list(bounds["additionalInvalidatingHandles"])
    return {"version": 2,
            "componentData": {"version": 1, "name": control["componentData"]["name"] + " name", "type": "Label", "data": _label_data(text)},
            "handle remapping": {"version": 1, "map": []}, "bounds": out}


def apply(tui, qlinks, ctx):
    if ctx.meta is None:
        raise ValueError("needs skin-meta.json (apply-mpc-skin-extras.py --meta)")
    names = {t["name"]: t["names"] for t in ctx.meta["tabs"]}
    for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]:
        if d["key"] in NAME_DY:
            d["value"]["componentsData"] = [c for c in d["value"]["componentsData"] if c["componentData"]["name"] != "name"]
    for tab in tui["pageData"]["tabs"]:
        items = tab["componentDefinition"]["componentsData"]
        tab_names = names.get(tab["tabName"])
        if tab_names is None:
            raise ValueError("skin-meta.json has no tab %r" % tab["tabName"])
        out = []
        for c in items:
            out.append(c)
            if c["componentData"]["type"] not in NAME_DY:
                continue
            key = ctx.param_key(int(c["handle remapping"]["map"][0]["value"].split()[1]))
            if key not in tab_names:
                raise ValueError("tab %r: no short name for %r (skin-meta.json)" % (tab["tabName"], key))
            out.append(_label_entry(c, tab_names[key]))
        items[:] = out
