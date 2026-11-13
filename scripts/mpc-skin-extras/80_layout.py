# SynthyBoy - MPC skin extra: the layout of the controls inside the Q-Link column frames (symmetric about the axis of the column).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The generator puts every control in a 278 x 107 slot of a 4 x 4 grid (x 23 + 320 c, y 45 / 196 / 346 / 497) and draws the knob at the left of
# the slot with its name and value to the right. This module (it runs after the modules that move controls and pictures: 10_env, 20_osc, 30_lfo,
# and before 90_frames, which owns the frames) puts every control on the vertical axis of its column frame (x = 160 + 320 c):
#   - knob (knobUni, knobBi): name centred above (22 px), knob 90 x 90 in the middle, value centred below (26 px); slot 278 x 138;
#   - toggle: name above, the switch in the middle, the state text below; slot 278 x 138;
#   - fader: name above, the strip in the middle, value below; slot 278 x 100;
#   - selectors (sliderWithFocus, sliderWithFocusMenu) and radio3 / radio4: slot 278 x 100, their contents are symmetric already (radio name is centred).
# The names are one centred line over the inner width of the frame (272 px) in Titillium Web 18: the longest name of the skin
# ("Voice Control Envelope Time Scaling", 35 characters) fits that width; the host has no shrink-to-fit key for labels (ui-reference 6.3).
# Slots: x = 21 + 320 c (the frame is x 16 .. 304, so 5 px on each side, centre 160 + 320 c).
# Rows (tab coordinates, the tab is 628 high, the frame 624): plain tabs 44 / 188 / 332 / 476 (pitch 144, rows 138 high, the frame top badge
# zone y < 40 stays empty); tabs with a picture above (frames from y 301, badge zone to y 341): rows 3 and 4 at 343 and 483 (end 621).
# Slots of 100 px sit in the middle of the 138 px row (19 px above and below), so the LFO picture (208 x 38) fits between two selectors.
# Touch and focus behaviour (actions, handles, Q-Link and overlay) are those of the generator: only geometry and justification change here.

import copy

OLD_COLS = (23, 343, 663, 983)
OLD_ROWS = (45, 196, 346, 497)
SLOT_X = (21, 341, 661, 981)
SLOT_W = 278
AXIS = 139                       # axis of the column inside a slot
PLAIN_ROWS = (44, 188, 332, 476)
GRAPHIC_ROWS = {2: 343, 3: 483}  # tabs with a picture: only the lower two rows exist
ROW_H = 138
SLOT_TYPES = ("knobUni", "knobBi", "toggle", "faderUni", "faderBi", "radio3", "radio4", "sliderWithFocus", "sliderWithFocusMenu")
ALIGNED = ("knobUni", "knobBi", "toggle", "faderUni", "faderBi", "radio3", "radio4")  # definitions that get the centred layout
SLOT_H = {"knobUni": ROW_H, "knobBi": ROW_H, "toggle": ROW_H, "faderUni": 100, "faderBi": 100, "radio3": 100, "radio4": 100,
          "sliderWithFocus": 100, "sliderWithFocusMenu": 100}
NAME_FONT, VALUE_FONT = 18.0, 24.0
CENTRED = "horizontallyCentred verticallyCentred"
SEG_DY = 54                      # the text over a radio button: y of the buttons inside the radio slot
PICTURE_W = 208
PICTURE_H = 38
PICTURE_GAP = 2


def _child(definition, name):
    found = [c for c in definition["componentsData"] if c["componentData"]["name"] == name]
    if len(found) != 1:
        raise ValueError("definition has %d children named %r" % (len(found), name))
    return found[0]


def _place(definition, name, rect):
    _child(definition, name)["bounds"]["bounds"] = "%d %d %d %d" % rect


def _label(definition, name, rect, font):
    child = _child(definition, name)
    child["bounds"]["bounds"] = "%d %d %d %d" % rect
    style = child["componentData"]["data"]["textStyle"]
    style["justification"] = CENTRED
    style["font"]["height"] = font


def _name_value(definition, name_rect, value_rect):
    _label(definition, "name", name_rect, NAME_FONT)
    _label(definition, "value", value_rect, VALUE_FONT)


def _knob(definition):
    _name_value(definition, (3, 0, 272, 22), (3, 112, 272, 26))
    _place(definition, "knob", (AXIS - 45, 22, 90, 90))


def _toggle(definition):
    _name_value(definition, (3, 0, 272, 22), (3, 112, 272, 26))
    _place(definition, "toggle", (AXIS - 42, 33, 84, 68))


def _fader(definition):
    _name_value(definition, (3, 4, 272, 22), (3, 70, 272, 26))
    _place(definition, "fader", (15, 32, 248, 36))


def _radio(definition):
    _label(definition, "name", (3, 6, 272, 24), NAME_FONT)


def _selector(definition):
    _place(definition, "slider", (15, 11, 248, 77))


ALIGN = {"knobUni": _knob, "knobBi": _knob, "toggle": _toggle, "faderUni": _fader, "faderBi": _fader, "radio3": _radio, "radio4": _radio}
FIT = {"sliderWithFocus": _selector, "sliderWithFocusMenu": _selector}   # geometry only (the slot got shorter)


def slot_height(typ):
    return SLOT_H[typ]


def _fit_definitions(ctx_defs):
    for key, value in ctx_defs.items():
        if key not in SLOT_TYPES:
            continue
        if key in ALIGNED:
            ALIGN[key](value)
        elif key in FIT:
            FIT[key](value)
        _child(value, "focus")["bounds"]["bounds"] = "0 0 %d %d" % (SLOT_W, slot_height(key))


def _rect(component):
    return tuple(int(v) for v in component["bounds"]["bounds"].split())


def _set(component, rect):
    component["bounds"]["bounds"] = "%d %d %d %d" % rect


def _graphic(tab):
    return all(b.split()[1] == "301" for b in tab["qlinkBoundsData"])


def apply(tui, qlinks, ctx):
    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    _fit_definitions(defs)
    for tab in tui["pageData"]["tabs"]:
        items = tab["componentDefinition"]["componentsData"]
        graphic = _graphic(tab)
        moved = {}   # name of a slot -> (old y, new y)
        for c in items:
            typ = c["componentData"]["type"]
            if typ not in SLOT_TYPES:
                continue
            x, y, w, h = _rect(c)
            if x not in OLD_COLS or y not in OLD_ROWS or (w, h) != (278, 107):
                raise ValueError("tab %r: %r is not on the generator's grid: %d %d %d %d" % (tab["tabName"], c["componentData"]["name"], x, y, w, h))
            row = OLD_ROWS.index(y)
            if graphic:
                if row not in GRAPHIC_ROWS:
                    raise ValueError("tab %r: %r is in row %d, above the frames" % (tab["tabName"], c["componentData"]["name"], row + 1))
                row_y = GRAPHIC_ROWS[row]
            else:
                row_y = PLAIN_ROWS[row]
            slot_h = slot_height(typ)
            new_y = row_y + (ROW_H - slot_h) // 2
            _set(c, (SLOT_X[OLD_COLS.index(x)], new_y, SLOT_W, slot_h))
            moved[c["componentData"]["name"]] = (y, new_y)
        for c in items:
            name = c["componentData"]["name"]
            if c["componentData"]["type"] == "Label" and " seg " in name:   # the texts over the buttons of a radio group
                owner = name.rsplit(" seg ", 1)[0]
                old_y, new_y = moved[owner]
                x, y, w, h = _rect(c)
                _set(c, (x - (OLD_COLS[0] - SLOT_X[0]), y + new_y - old_y, w, h))
        for c in items:   # the LFO pictures: centred over the column of their selector, directly above it
            name = c["componentData"]["name"]
            if c["componentData"]["type"] == "Image" and name.startswith("LFO ") and " Picture" in name:
                lfo = name.split(" Picture")[0]
                selector = next(s for s in items if s["componentData"]["name"] == lfo + " Shape")
                sx, sy, _, _ = _rect(selector)
                _set(c, (sx + AXIS - PICTURE_W // 2, sy - PICTURE_H - PICTURE_GAP, PICTURE_W, PICTURE_H))
