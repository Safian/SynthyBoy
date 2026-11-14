# SynthyBoy - MPC skin extra: the Play page of the OVERLAY skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json: the pages "Play", "Play Env", "Play LFO", sub-tabs of the first function key).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Inactive unless skin-meta.json says "variant": "overlay" (the production skin stays byte for byte as it is). Made for the plugin built with SYNTHYBOY_MPC_OVERLAY: its parameters after the
# pad pressure ones are "Overlay Instance", the selectors "Play Osc", "Play Env", "Play LFO" and the 24 proxy parameters (shell-mpc/play_params.h, docs/MPC.md "Play page"):
#   selectors  index = entries of docs/params.json + 4 LFO cursors + 32 spectrum columns + 2 pad pressure + 1 Overlay Instance + 0 .. 2
#   proxies    after the selectors, group after group (oscillator, envelope, LFO), eight each
# Every one of the three tabs has the same TOP (tab coordinates, 1280 x 628): the region of the overlay view (shell-mpc/overlay/overlay_layout.json "play1" .. "play3": the three live panels), with
# the markers and the instance picture of the view; below it (y 197 ..) the three selectors, one under each panel; BOTTOM: the eight knobs of the module of the sub-tab in the lower two rows of the
# grid (Q-Link 1 .. 8: the bottom row left to right is Q-Link 1 .. 4, the row above Q-Link 5 .. 8 = slots 0 .. 7 of the table in play_params.h).
# The controls are COPIES of the controls of the real parameter of instance 1 on its own tab (the same widget, the same look), bound to the proxy parameter; a stepped real parameter with five or more
# choices is a selector, with two to four a toggle. Where the production skin has no control for a real parameter (Env release curve) the control of a similar one is copied.
# The fallback when the overlay is off or disabled: under the plane a plain dark picture with the three panel frames and the three titles ("Oscillator", "Envelope", "LFO" as fixed texts bound to the
# selectors); the selectors and the knobs work without the overlay.
# Runs after 80_layout.py (the copies already have the final geometry of their definitions), before 85_names.py (the names) and 90_frames.py.

import copy
import json
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAYOUT_JSON = HERE.parent.parent / "shell-mpc" / "overlay" / "overlay_layout.json"
TABS = ("Play", "Play Env", "Play LFO")
LFO_CURSORS, SPECTRUM_COLUMNS, PAD_PRESSURE_PARAMS, OVERLAY_INSTANCE = 4, 32, 2, 1
GROUPS = 3
SLOTS = 8
# (prefix, instances, [(key suffix of the real parameter, template suffix if the production skin has no control for it, short name)] x 8) ; the names are the ones of play_params.h
TABLE = (
    ("osc", 3, [("frame", None, "WT Position"), ("unison.voices", None, "Unison"), ("unison.detune", None, "Detune"), ("unison.blend", None, "Blend"),
                ("phase", None, "Phase"), ("random_phase", None, "Rand Phase"), ("pan", None, "Pan"), ("level", None, "Level")]),
    ("env", 6, [("attack", None, "Attack"), ("hold", None, "Hold"), ("decay", None, "Decay"), ("sustain", None, "Sustain"),
                ("release", None, "Release"), ("attack_curve", None, "Atk Curve"), ("decay_curve", None, "Dec Curve"), ("release_curve", "decay_curve", "Rel Curve")]),
    ("lfo", 10, [("rate", None, "Rate"), ("fade_in", None, "Rise"), ("delay", None, "Delay"), ("smooth_time", "fade_in", "Smooth"),
                 ("smooth", "@toggle", "Smooth On"), ("sync", None, "Sync"), ("sync_type", None, "Trigger"), ("division", None, "Division")]),
)
SELECTOR_KEYS = ("shell.play_osc", "shell.play_env", "shell.play_lfo")
SELECTOR_SHORT = ("Osc", "Env", "LFO")
PANEL_TITLES = ("Oscillator", "Envelope", "LFO")
SELECTOR_TEMPLATES = ("sliderWithFocusMenu",) * 3   # the widget of a selector: the selector slider of the skin (touch and drag; observed on the Key 61: the radio group did not answer a tap, the selector does)
PANEL_W, PANEL_GAP = 400, 17
SELECTOR_Y = 197           # the top of a selector slot (100 high: ends at 297, the Q-Link frames start at 301)
COLS = (21, 341, 661, 981) # slot x of the 4 columns (80_layout.py SLOT_X)
ROW_Y = {2: 343, 3: 483}   # graphic tabs: y of the grid rows 3 and 4 (80_layout.py GRAPHIC_ROWS)
ROW_H = 138
SLOT_W = 278
SLOT_H = {"knobUni": 138, "knobBi": 138, "toggle": 138, "faderUni": 100, "faderBi": 100, "radio3": 100, "radio4": 100, "sliderWithFocus": 100, "sliderWithFocusMenu": 100}
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = 301, 323
BACKGROUND_FILE, PANEL_FILE = "overlay_play_bg.png", "overlay_play_panel.png"
PANEL_RGB = (0x26, 0x25, 0x27)
TITLE_COLOUR = "ffe6e6e6"


def png_solid(width, height, rgb):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    row = b"\x00" + bytes(rgb) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height, 9)) + chunk(b"IEND", b""))


def _rgb(text):
    return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16))


def _rect(c):
    return tuple(int(v) for v in c["bounds"]["bounds"].split())


def _set(c, rect):
    c["bounds"]["bounds"] = "%d %d %d %d" % rect


def _param_of(c):
    m = c["handle remapping"]["map"]
    return int(m[0]["value"].split()[1]) if m else None


def _image(name, file, rect):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": "Image", "data": {"version": 2, "imageType": "Regular", "colour": "0", "image": file}},
            "handle remapping": {"version": 1, "map": []},
            "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % rect}}


def _title(name, text, param, rect):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": "Label",
                              "data": {"version": 1,
                                       "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": 22.0},
                                                     "colour": TITLE_COLOUR, "justification": "horizontallyCentred verticallyCentred", "case": "Upper Case"},
                                       "type": "Text", "handleName": "Data", "text": text}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % param}]},
            "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % rect}}


def _controls_by_param(tui):
    """{parameter index: (tab, control)} of the controls of the whole skin that are bound to one parameter (the first one found)."""
    found = {}
    for tab in tui["pageData"]["tabs"]:
        if tab["tabName"] in TABS:
            continue
        for c in tab["componentDefinition"]["componentsData"]:
            if c["componentData"]["type"] in SLOT_H and c["handle remapping"]["map"]:
                lst = found.setdefault(_param_of(c), [])
                if not lst or lst[0][0] is tab:   # the controls of the FIRST tab that has one; a control may have variants there (the LFO tab has three Division controls, each valid for one sync choice)
                    lst.append((tab, c))
    return found


def _seg_labels(tab, control):
    prefix = control["componentData"]["name"] + " seg "
    return [c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "Label" and c["componentData"]["name"].startswith(prefix)]


def _rebind_handles(bounds, rebind):
    """The handles a control is valid for ("IndexedEnabling/1/5/Parameter 189": valid when parameter 189 has the choice 1) follow the proxies: Parameter N of a real parameter becomes its proxy."""
    handles = bounds.get("additionalInvalidatingHandles")
    if handles:
        out = []
        for h in handles:
            head, _, tail = h.rpartition("Parameter ")
            if int(tail) not in rebind:
                raise ValueError("a control of the production skin depends on parameter %s, which has no Play proxy (%s)" % (tail, h))
            out.append("%sParameter %d" % (head, rebind[int(tail)]))
        bounds["additionalInvalidatingHandles"] = out


def _copy_control(template, name, param, rect, rebind=None):
    tab, c = template
    out = copy.deepcopy(c)
    out["componentData"]["name"] = name
    out["handle remapping"]["map"][0]["value"] = "Parameter %d" % param
    _rebind_handles(out["bounds"], rebind or {})
    _set(out, rect)
    segs = []
    for lab in _seg_labels(tab, c):
        s = copy.deepcopy(lab)
        s["componentData"]["name"] = lab["componentData"]["name"].replace(c["componentData"]["name"], name)
        s["handle remapping"]["map"] = [{"key": "Data", "value": "Parameter %d" % param}]
        dx, dy = _rect(lab)[0] - _rect(c)[0], _rect(lab)[1] - _rect(c)[1]
        _set(s, (rect[0] + dx, rect[1] + dy) + _rect(lab)[2:])
        segs.append(s)
    return out, segs


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    layout = json.loads(LAYOUT_JSON.read_text(encoding="utf-8"))
    play_views = [v for v in layout["views"] if v["kind"] == "play"]
    if [v["tab"] for v in play_views] != list(TABS):
        raise ValueError("overlay_layout.json has the play views %s, this module builds %s" % ([v["tab"] for v in play_views], list(TABS)))
    first = ctx.param_count + LFO_CURSORS + SPECTRUM_COLUMNS + PAD_PRESSURE_PARAMS + OVERLAY_INSTANCE
    sel_idx = [first + g for g in range(GROUPS)]
    proxy_idx = [[first + GROUPS + g * SLOTS + s for s in range(SLOTS)] for g in range(GROUPS)]
    proxy_keys = [[], [], []]
    for g, (prefix, instances, slots) in enumerate(TABLE):
        ctx.add_param_key(sel_idx[g], SELECTOR_KEYS[g])
        for s, (suffix, _, _) in enumerate(slots):
            key = "shell.play_%s_%s" % (prefix, suffix.replace(".", "_"))
            proxy_keys[g].append(key)
            ctx.add_param_key(proxy_idx[g][s], key)
    by_param = _controls_by_param(tui)
    skins = ctx.skins_dir
    region = play_views[0]["region"]
    (skins / BACKGROUND_FILE).write_bytes(png_solid(region["w"], region["h"], _rgb(layout["background"])))
    (skins / PANEL_FILE).write_bytes(png_solid(PANEL_W, region["h"], PANEL_RGB))
    # the templates of the selectors: a control of the production skin of the same widget
    selector_template = {}
    for kind in set(SELECTOR_TEMPLATES):
        found = [v[0] for v in by_param.values() if v[0][1]["componentData"]["type"] == kind]
        if not found:
            raise ValueError("the skin has no %s control to copy for a Play selector" % kind)
        selector_template[kind] = found[0]
    names = {t["name"]: t for t in ctx.meta["tabs"]}
    for view, tab_name in zip(play_views, TABS):
        tab = ctx.tab(tab_name)
        items = tab["componentDefinition"]["componentsData"]
        highlight = view["index"] - 1
        extra = []
        # fallback picture (under everything): dark region, three panels, three titles
        under = [_image("Play Background", BACKGROUND_FILE, (region["x"], region["y"], region["w"], region["h"]))]
        for p in range(GROUPS):
            x = region["x"] + p * (PANEL_W + PANEL_GAP)
            under.append(_image("Play Panel %d" % (p + 1), PANEL_FILE, (x, region["y"], PANEL_W, region["h"])))
        items[:0] = under
        for p in range(GROUPS):
            x = region["x"] + p * (PANEL_W + PANEL_GAP)
            extra.append(_title("Play Title %d" % (p + 1), PANEL_TITLES[p], sel_idx[p], (x + 8, region["y"] + 8, PANEL_W - 16, 28)))
        # the selectors, one under each panel
        for p in range(GROUPS):
            kind = SELECTOR_TEMPLATES[p]
            x = region["x"] + p * (PANEL_W + PANEL_GAP) + (PANEL_W - SLOT_W) // 2
            c, segs = _copy_control(selector_template[kind], "Play %s Select" % SELECTOR_SHORT[p], sel_idx[p], (x, SELECTOR_Y, SLOT_W, SLOT_H[kind]))
            if kind == "radio3":
                if len(segs) != 3:
                    raise ValueError("the radio3 template has %d captions" % len(segs))
                for k, lab in enumerate(sorted(segs, key=lambda s: _rect(s)[0])):
                    lab["componentData"]["data"]["text"] = str(k + 1)
            extra.append(c)
            extra.extend(segs)
        # the eight knobs of the module of this sub-tab
        prefix, _, slots = TABLE[highlight]
        links = {"Q-Link %d" % i: -1 for i in range(1, 17)}
        rebind = {ctx.param_index("%s1.%s" % (prefix, suffix)): proxy_idx[highlight][s] for s, (suffix, _, _) in enumerate(slots)}   # real parameter of instance 1 -> its proxy
        for s, (suffix, fallback, short) in enumerate(slots):
            real = ctx.param_index("%s1.%s" % (prefix, suffix))
            templates = by_param.get(real)
            if templates is None and fallback is not None:
                if fallback.startswith("@"):   # any control of that widget (a switch the production skin has no control for)
                    templates = next(([v[0]] for v in by_param.values() if v[0][1]["componentData"]["type"] == fallback[1:]), None)
                else:
                    templates = by_param.get(ctx.param_index("%s1.%s" % (prefix, fallback)))
            if templates is None:
                raise ValueError("no control of the production skin to copy for %s1.%s" % (prefix, suffix))
            kind = templates[0][1]["componentData"]["type"]
            row, col = (3, s) if s < 4 else (2, s - 4)
            y = ROW_Y[row] + (ROW_H - SLOT_H[kind]) // 2
            for v, template in enumerate(templates):
                c, segs = _copy_control(template, "Play %s %s%s" % (prefix.capitalize(), short, "" if v == 0 else " %d" % (v + 1)), proxy_idx[highlight][s], (COLS[col], y, SLOT_W, SLOT_H[kind]), rebind)
                extra.append(c)
                extra.extend(segs)
            links["Q-Link %d" % (s + 1)] = proxy_idx[highlight][s]
        items.extend(extra)
        # names over the controls (85_names.py): the meta of this tab
        tab_names = names[tab_name]["names"]
        for s, (_, _, short) in enumerate(slots):
            tab_names[proxy_keys[highlight][s]] = short
        entry = [m for m in qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1]
        if len(entry) != 1:
            raise ValueError("Q-Links.json has %d entries for the tab %r" % (len(entry), tab_name))
        entry[0]["Q-Links"] = links
        tab["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H) for b in tab["qlinkBoundsData"]]
