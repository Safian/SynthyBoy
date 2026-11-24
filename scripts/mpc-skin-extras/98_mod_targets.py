# SynthyBoy - shared native controls of the MPC modulation route editor.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
from pathlib import Path

from PIL import Image, ImageDraw

import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components.native.selector.selector import OSC_TAB_NAMES, passive_layer
from native_skin_font import semibold
from native_ui_params import (MOD_TARGETS, MOD_TARGETS_PAGE_GATE, MOD_TARGETS_ROW_FIELDS,
                              MOD_TARGETS_ROW_GATE, MOD_TARGETS_ROWS, MOD_TARGETS_VIEW,
                              mixer_peek_index)

CONTRACT = Path(__file__).with_name("mod-targets-native-contract.json")


def _entry(name, kind, rect, data, remap=(), gate=None, focus="No"):
    bounds = {"version": 2 if gate else 1, "acceptsHWFocus": focus,
              "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
              "boundsType": "Absolute", "bounds": " ".join(map(str, rect))}
    if gate:
        bounds["additionalInvalidatingHandles"] = [gate]
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]}, "bounds": bounds}


def _custom(name, kind, rect, remap, gate):
    return _entry(name, kind, rect, {"version": 1, "handleName": "Data"}, remap, gate, "Children")


def _label(name, rect, kind, handle, gate=None, align="horizontallyCentred"):
    data = {"version": 1, "type": kind, "handleName": handle, "textStyle": {
        "version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": 19},
        "colour": "ffe6e6e6", "justification": f"{align} verticallyCentred", "case": "Original"}}
    return _entry(name, "Label", rect, data, gate=gate)


def _picture(directory, key, caption, rect, active):
    w, h = rect[2:]
    im = Image.new("RGBA", (w, h))
    pen = ImageDraw.Draw(im)
    pen.rounded_rectangle((1, 1, w - 2, h - 2), radius=6, fill="#161d31",
                          outline="#72d1ec" if active else "#525b73", width=2)
    pen.text((w / 2, h / 2), caption, font=semibold(19), fill="#e6e6e6", anchor="mm")
    name = f"mod_targets_{key}.png"
    im.save(directory / name)
    return name


def _button(name, rect, images, handle, gate=None, index=1, count=1):
    on, off = images
    data = {"version": 2, "onImage": on, "offImage": off, "buttonId": index,
            "numButtonsInGroup": count, "handleName": "Data", "gestureBehaviour": "Instant"}
    return _entry(name, "Button", rect, data, (("Data", handle),), gate, "Yes")


def _rows(ctx, geometry):
    """One reusable row owns its dynamic route name and four independent controls."""
    y = geometry[0][1]
    name, amount, polarity, active, remove = [(x, top - y, w, h) for x, top, w, h in geometry]
    x, top, w, h = amount
    data = {"version": 2, "sliderType": "ColourBar", "direction": "Horizontal",
            "backgroundColour": "ff242b42", "outlineColour": "ff52607a", "valueColour": "ff72d1ec",
            "isBipolar": True, "handleName": "Amount"}
    items = [_label("Target", name, "Name", "Amount", "IndexedEnabling/2/3/Present", "leftJustified"),
             _entry("Amount", "Slider", amount, data, focus="Yes"),
             _label("Depth", (x + w - 104, top, 104, h), "Value", "Amount")]
    for key, rect, captions in (("Polarity", polarity, ("BI", "UNI")), ("Active", active, ("ON", "OFF")),
                                ("Remove", remove, ("×", "×"))):
        on = _picture(ctx.skins_dir, key.lower() + "_on", captions[0], rect, key != "Remove")
        off = on if key == "Remove" else _picture(ctx.skins_dir, key.lower() + "_off", captions[1], rect, False)
        items.append(_button(key, rect, (on, off), "FromGesture/" + key))
    return items


def _pagination(ctx, rect, parameter, availability):
    x, y, w, h = rect
    half = w // 2
    film = Image.new("RGBA", (w, h * 4))
    for mode in range(1, 4):
        im = Image.new("RGBA", (w, h))
        pen = ImageDraw.Draw(im)
        pen.rounded_rectangle((1, 1, w - 2, h - 2), radius=6, fill="#161d31", outline="#525b73", width=2)
        for arrow, dx, active in (("‹", 22, mode <= 2), ("›", w - 22, mode >= 2)):
            pen.text((dx, h / 2), arrow, font=semibold(27), fill="#e6e6e6" if active else "#727a90", anchor="mm")
        film.paste(im, (0, h * mode))
    image = "mod_targets_pages.png"
    film.save(ctx.skins_dir / image)
    gate = f"NonZero/{availability}"
    art = {"version": 2, "imageType": "FilmStrip", "image": image, "colour": "0", "numFrames": 4,
           "handleName": availability}
    hit = ("mod_targets_hit.png",) * 2
    return [_entry("Page arrows", "Image", rect, art, gate=gate),
            _label("Page", (x + 40, y, w - 80, h), "Value", parameter, gate),
            _button("Previous", (x, y, half, h), hit, f"WithName[Previous Page]/FromGesture/{parameter}",
                    f"RangedEnabling/1/2/4/{availability}", 1, 4),
            _button("Next", (x + half, y, w - half, h), hit, f"WithName[Next Page]/FromGesture/{parameter}",
                    f"RangedEnabling/2/3/4/{availability}", 2, 4)]


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    base = mixer_peek_index(ctx.param_count)
    view = f"Parameter {base + MOD_TARGETS_VIEW}"
    row_type = "nativeModulationRoute"
    ctx.add_component_definition(row_type, passive_layer(_rows(ctx, contract["row"])))
    Image.new("RGBA", (1, 1)).save(ctx.skins_dir / "mod_targets_hit.png")
    back = contract["back"]
    art = _picture(ctx.skins_dir, "back", "BACK", back, False)
    add = f"Parameter {base + MOD_TARGETS + 1}"
    items = [_button("BACK", back, (art, art), f"FromGesture/Parameter {base + MOD_TARGETS}"),
             _entry("Add", "oscNativeSetting_action", contract["add"], {"version": 1, "handleName": "Data"},
                    (("Data", "FromGesture/" + add), ("Caption", add)), focus="Yes")]
    items.extend(_pagination(ctx, contract["page"], f"Parameter {base + MOD_TARGETS + 2}",
                             f"Parameter {base + MOD_TARGETS_PAGE_GATE}"))
    _, row_top, _, row_height = contract["row"][0]
    for row in range(MOD_TARGETS_ROWS):
        start = base + MOD_TARGETS + 3 + row * MOD_TARGETS_ROW_FIELDS
        present = f"Parameter {base + MOD_TARGETS_ROW_GATE + row}"
        remap = [("Data", present), ("Present", present)]
        remap.extend((name, f"Parameter {start + j}") for j, name in enumerate(("Amount", "Polarity", "Active", "Remove")))
        rect = (0, row_top + row * contract["stride"], 1280, row_height)
        items.append(_custom(f"Route {row + 1}", row_type, rect, remap, f"NonZero/{present}"))

    kind = "nativeModulationEditor"
    ctx.add_component_definition(kind, passive_layer(items))
    pages = {"Macros": 1, "Env": 2, "LFO": 3, "Filter": 4, "Mod": 5, "Play": 6,
             **dict.fromkeys((*OSC_TAB_NAMES, "Osc"), 7), **dict.fromkeys(("Mix", "Mixer"), 8)}
    for tab in tui["pageData"]["tabs"]:
        if (frame := pages.get(tab["tabName"])) is not None:
            entries = tab["componentDefinition"]["componentsData"]
            entries.insert(0, _custom("Native Modulation Editor", kind, (0, 0, 1280, 628),
                                      (("Data", view),), f"IndexedEnabling/{frame}/9/{view}"))
