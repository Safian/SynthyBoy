# SynthyBoy - MPC skin extra: the pad pressure controls on the Master tab (bottom row, left of x 1025).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The MPC pad sends polyphonic aftertouch; the plugin turns it into a Filter 1 cutoff offset per voice (shell-mpc/pad_pressure.h, docs/MPC.md "Pad pressure"). Two plugin
# parameters at the very end of the list switch it and set the depth: "Pad Pressure" (index = entries of docs/params.json + 4 LFO cursors + 32 spectrum columns) and "Pad
# Pressure Amount" (the next one). The plugin must be built with SYNTHYBOY_MPC_PAD_PRESSURE (the default of shell-mpc), otherwise the parameters do not exist.
#
# Where: the skin is at its limit of tabs (the validator's one accepted finding is "34 tabs; at most 32 are supported", a tab more would change it), so the controls go into
# a tab with a free row. The Master tab (global voicing and portamento) has its bottom row empty. Left to right in the first three columns (the fourth column, x >= 981, stays
# empty: a control there sits to the right of x 1025):
#   column 1  toggle "Pad Pressure"            (Q-Link 1)
#   column 2  knob "Pad Pressure Amount"       (Q-Link 2)
#   column 3  a two line text: the Full Level of the MPC pad must be OFF (with Full Level on, every hit is velocity 127 and the pad sends no pressure)
# The controls are put on the generator's grid (x 23 + 320 c, y 497, 278 x 107) and are laid out and named by 80_layout.py and 85_names.py like every other control: this module
# registers the keys of the two parameters (ctx.add_param_key) and their short names in the meta (ctx.meta), which those modules read. The hint is drawn after the layout: a
# text label at fixed places inside the column 3 slot (bound to the toggle's parameter, because the host draws a Text label only for a valid handle).

TAB = "Master"
LFO_CURSORS = 4          # display parameters before the spectrum (shell-mpc/lfo_live.h kLfoLiveCount)
SPECTRUM_COLUMNS = 32    # shell-mpc/spectrum.h kSpectrumCount
KEY_ON, KEY_AMOUNT = "shell.pad_pressure", "shell.pad_pressure_amount"
SHORT_ON, SHORT_AMOUNT = "Pad Pressure", "Pressure Amount"
HINT_LINES = ("Full Level must", "be off for pressure")   # the text shows in upper case
GRID_Y, GRID_W, GRID_H = 497, 278, 107
GRID_X = (23, 343)       # columns 1 and 2
HINT_X, HINT_Y, HINT_W, HINT_H, HINT_DY = 664, 508, 272, 28, 30   # inside the slot of column 3 (x 661 .. 939, y 476 .. 614 after 80_layout.py)
HINT_COLOUR = "ffe0e0e0"
QLINK_TOGGLE, QLINK_AMOUNT = "Q-Link 1", "Q-Link 2"   # bottom row, columns 1 and 2


def _control(name, kind, param, x):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": kind, "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % param}]},
            "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute",
                       "bounds": "%d %d %d %d" % (x, GRID_Y, GRID_W, GRID_H)}}


def _hint(name, text, param, y):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": "Label",
                              "data": {"version": 1,
                                       "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": 22.0},
                                                     "colour": HINT_COLOUR, "justification": "horizontallyCentred verticallyCentred", "case": "Upper Case"},
                                       "type": "Text", "handleName": "Data", "text": text}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % param}]},
            "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute",
                       "bounds": "%d %d %d %d" % (HINT_X, y, HINT_W, HINT_H)}}


def apply(tui, qlinks, ctx):
    first = ctx.param_count + LFO_CURSORS + SPECTRUM_COLUMNS
    p_on, p_amount = first, first + 1
    ctx.add_param_key(p_on, KEY_ON)
    ctx.add_param_key(p_amount, KEY_AMOUNT)
    tab = ctx.tab(TAB)
    items = tab["componentDefinition"]["componentsData"]
    for c in items:
        x, y, w, h = (int(v) for v in c["bounds"]["bounds"].split())
        if y >= GRID_Y - 10:
            raise ValueError("tab %r: %r at y %d is in the row of the pad pressure controls" % (TAB, c["componentData"]["name"], y))
    items.append(_control("Pad Pressure", "toggle", p_on, GRID_X[0]))
    items.append(_control("Pad Pressure Amount", "knobUni", p_amount, GRID_X[1]))
    for i, line in enumerate(HINT_LINES):
        items.append(_hint("Pad Pressure hint %d" % (i + 1), line, p_on, HINT_Y + i * HINT_DY))
    if ctx.meta is not None:
        names = {t["name"]: t["names"] for t in ctx.meta["tabs"]}
        names[TAB][KEY_ON] = SHORT_ON
        names[TAB][KEY_AMOUNT] = SHORT_AMOUNT
    # Q-Links of the bottom row (Q-Link 1 .. 4 = row 4, columns 1 .. 4; -1 = none): the generator left them free
    slot = (tab["fnKeyIndex"] + 1, tab["fnKeySubIndex"] + 1)
    entry = [e for e in qlinks["Screen Mode Q-Links"]["map"] if (e["Tab"], e["SubTab"]) == slot]
    if len(entry) != 1:
        raise ValueError("Q-Links.json has %d entries for the tab %r" % (len(entry), TAB))
    q = entry[0]["Q-Links"]
    if q[QLINK_TOGGLE] != -1 or q[QLINK_AMOUNT] != -1:
        raise ValueError("tab %r: %s / %s are not free" % (TAB, QLINK_TOGGLE, QLINK_AMOUNT))
    q[QLINK_TOGGLE], q[QLINK_AMOUNT] = p_on, p_amount
