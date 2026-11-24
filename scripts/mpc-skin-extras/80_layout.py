# SynthyBoy - MPC skin extra: the layout of the controls inside the Q-Link column frames (symmetric about the axis of the column).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The generator puts every control in a 278 x 107 slot of a 4 x 4 grid (x 23 + 320 c, y 45 / 196 / 346 / 497) and draws the knob at the left of
# the slot with its name and value to the right. This module (it runs after the modules that move controls and pictures: 10_env,
# and before 90_frames, which owns the frames) puts every control on the vertical axis of its column frame (x = 160 + 320 c):
#   - knob (knobUni, knobBi): knob 90 x 90 in the middle (y 22), value centred below (26 px); slot 278 x 138;
#   - toggle: the switch in the middle, the state text below; slot 278 x 138;
#   - fader: the strip in the middle, value below; slot 278 x 100;
#   - selectors (sliderWithFocus, sliderWithFocusMenu) and radio3 / radio4: slot 278 x 100, their contents are symmetric already.
# The name of a control is not part of these definitions: 85_names.py (runs later) removes the host drawn "name" child and puts a short, upper
# case, bold text over the slot instead (one centred line over the inner width of the frame, 272 px).
# Slots: x = 21 + 320 c (the frame is x 16 .. 304, so 5 px on each side, centre 160 + 320 c).
# Rows (tab coordinates, the tab is 628 high, the frame 624): plain tabs 44 / 188 / 332 / 476 (pitch 144, rows 138 high, the frame top badge
# zone y < 40 stays empty); tabs with a picture above (frames from y 301, badge zone to y 341): rows 3 and 4 at 343 and 483 (end 621).
# Slots of 100 px sit in the middle of the 138 px row (19 px above and below).
# Touch and focus behaviour (actions, handles, Q-Link and overlay) are those of the generator: only geometry and justification change here.

import copy
from components import style as _ui_style

OLD_COLS = tuple(_ui_style.size('screen.layout.old_cols'))
OLD_ROWS = tuple(_ui_style.size('screen.layout.old_rows'))
SLOT_X = tuple(_ui_style.size('screen.layout.slot_x'))
SLOT_W = _ui_style.size('screen.layout.slot_w')
AXIS = _ui_style.size('screen.layout.axis') # axis of the column inside a slot
PLAIN_ROWS = tuple(_ui_style.size('screen.layout.plain_rows'))
GRAPHIC_ROWS = dict(zip((2, 3), _ui_style.size('screen.layout.graphic_rows')))  # tabs with a picture: only the lower two rows exist
ROW_H = _ui_style.size('screen.layout.row_h')
SLOT_TYPES = ("knobUni", "knobBi", "toggle", "faderUni", "faderBi", "radio3", "radio4", "sliderWithFocus", "sliderWithFocusMenu")
ALIGNED = ("knobUni", "knobBi", "toggle", "faderUni", "faderBi", "radio3", "radio4")  # definitions that get the centred layout
SHORT_H = _ui_style.size("screen.layout.short_height")
SLOT_H = {"knobUni": ROW_H, "knobBi": ROW_H, "toggle": ROW_H, "faderUni": SHORT_H, "faderBi": SHORT_H, "radio3": SHORT_H, "radio4": SHORT_H,
          "sliderWithFocus": SHORT_H, "sliderWithFocusMenu": SHORT_H}
VALUE_FONT = _ui_style.size('screen.layout.value_font')
CENTRED = "horizontallyCentred verticallyCentred"
SEG_DY = _ui_style.size('screen.layout.seg_dy') # the text over a radio button: y of the buttons inside the radio slot
RADIO4_DX = _ui_style.size('screen.layout.radio4_dx')


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


def _value(definition, value_rect):
    _label(definition, "value", value_rect, VALUE_FONT)


def _knob(definition):
    _value(definition, tuple(_ui_style.size('screen.layout.value_rect')))
    _place(definition, "knob", tuple(_ui_style.size('screen.layout.knob_rect')))


def _toggle(definition):
    _value(definition, tuple(_ui_style.size('screen.layout.value_rect')))
    _place(definition, "toggle", tuple(_ui_style.size('screen.layout.toggle_rect')))


def _fader(definition):
    _value(definition, tuple(_ui_style.size('screen.layout.fader_value_rect')))
    _place(definition, "fader", tuple(_ui_style.size('screen.layout.fader_rect')))


def _radio(definition):
    """The buttons of radio3 are symmetric already (x 15 .. 263); the four buttons of radio4 (x 15 .. 259) move 2 px to the right (axis 139).
    The name is 85_names.py's (a text on the tab)."""
    segs = [c for c in definition["componentsData"] if c["componentData"]["name"].startswith("seg ")]
    if len(segs) == 4:
        for c in segs:
            x, y, w, h = _rect(c)
            _set(c, (x + RADIO4_DX, y, w, h))


def _selector(definition):
    _place(definition, "slider", tuple(_ui_style.size('screen.layout.selector_rect')))


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
    return all(b.split()[1] == str(_ui_style.size('screen.layout.graphic_top')) for b in tab["qlinkBoundsData"])


def apply(tui, qlinks, ctx):
    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    _fit_definitions(defs)
    for tab in tui["pageData"]["tabs"]:
        items = tab["componentDefinition"]["componentsData"]
        graphic = _graphic(tab)
        moved = {}   # name of a slot -> (old y, new y)
        kinds = {}   # name of a slot -> its type
        for c in items:
            typ = c["componentData"]["type"]
            if typ not in SLOT_TYPES:
                continue
            # Standalone native apertures (Filter power, and the LFO Smooth
            # switch) are deliberately placed outside the Q-Link grid. They
            # are host-owned controls, not fallback slots to re-layout.
            custom_name = c["componentData"]["name"]
            if (custom_name.endswith(" Power") or custom_name.endswith(" Smooth Native")
                    or custom_name.startswith(("Master ", "Voice ", "Mod ", "Macro ", "Macros ", "FX ", "Mixer ", "Osc Native "))):
                continue
            x, y, w, h = _rect(c)
            if x not in OLD_COLS or y not in OLD_ROWS or (w, h) != tuple(_ui_style.size('screen.layout.generated_slot')):
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
            kinds[c["componentData"]["name"]] = typ
        for c in items:
            name = c["componentData"]["name"]
            if c["componentData"]["type"] == "Label" and " seg " in name:   # the texts over the buttons of a radio group
                owner = name.rsplit(" seg ", 1)[0]
                if owner not in moved:
                    # A native page may place a radio group outside the
                    # legacy Q-Link grid; its companion labels travel with
                    # that custom component and must not be re-laid out here.
                    continue
                old_y, new_y = moved[owner]
                x, y, w, h = _rect(c)
                dx = RADIO4_DX if kinds[owner] == "radio4" else 0
                _set(c, (x - (OLD_COLS[0] - SLOT_X[0]) + dx, y + new_y - old_y, w, h))
