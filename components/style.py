# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared native-family design data; bundle this module and its two JSON files together."""
import copy
from functools import lru_cache
import json
from pathlib import Path
import re

DIRECTORY = Path(__file__).resolve().parent


@lru_cache(maxsize=2)
def _data(filename):
    path = DIRECTORY / filename
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError(f"Cannot load native style data {path}: {error}") from error
    if not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1:
        raise ValueError(f"Expected native style data version 1 in {path}")
    return data


def _token(filename, group, name):
    try:
        return _data(filename)[group][name]
    except KeyError as error:
        raise ValueError(f"Missing native style token {name!r} in {DIRECTORY / filename} ({group})") from error


def color(name):
    value = _token("colors.json", "colors", name)
    if not isinstance(value, str) or not re.fullmatch(r"#[0-9a-fA-F]{6}", value):
        raise ValueError(f"Native color {name!r} must be #RRGGBB in colors.json")
    return value.lower()


def rgb(name):
    value = color(name)
    return tuple(int(value[index:index + 2], 16) for index in (1, 3, 5))


def rgba(name, alpha=None):
    return rgb(name) + (opacity("opaque") if alpha is None else alpha,)


def argb(name, alpha=None):
    alpha = opacity("opaque") if alpha is None else alpha
    return f"{alpha:02x}" + color(name)[1:]


def size(name):
    def resolve(value, seen):
        if isinstance(value, dict) and set(value) == {"ref"}:
            target = value["ref"]
            if target in seen:
                raise ValueError(f"Cyclic native size token reference: {target}")
            return resolve(_token("sizes.json", "sizes", target), seen | {target})
        if isinstance(value, list):
            return [resolve(item, seen) for item in value]
        if isinstance(value, dict):
            return {key: resolve(item, seen) for key, item in value.items()}
        return value
    return resolve(_token("sizes.json", "sizes", name), {name})


def palette():
    return copy.deepcopy(_data("colors.json")["palette"])


def named_sizes():
    return copy.deepcopy(_data("sizes.json")["named_sizes"])


def opacity(name):
    return _token("colors.json", "opacity", name)


def rect(name):
    return " ".join(map(str, size(name)))


def native_color(name):
    return _token("colors.json", "native", name)


def _dim_artwork(image):
    from PIL import ImageOps
    grey = ImageOps.colorize(image.convert("L"), color("inactive.preview.dark"), color("inactive.preview.light"))
    grey.putalpha(image.convert("RGBA").getchannel("A"))
    return grey


def apply_control_state(definition, skins_dir, *, active):
    """Style the original native widgets; never replace controls with Images.

    Input authority remains with the bound UI bank. The MPC schema has no
    general enabled flag: remove actions/focus and knob drags in this state.
    """
    if active:
        return definition
    from PIL import Image

    result = copy.deepcopy(definition)
    result.update(actions=[], ignoreMousePresses=True, disableCoarseDataWheel=True)
    result["componentsData"] = [c for c in result["componentsData"] if c["componentData"]["type"] != "Focus"]
    for child in result["componentsData"]:
        child["bounds"]["acceptsHWFocus"] = "No"
        widget = child["componentData"]
        data = widget.get("data", {})
        if widget["type"] == "Knob":
            data["dragOrientation"] = "None"
        for key in ("image", "filmStrip", "onImage", "offImage", "foregroundImage", "backgroundImage"):
            if not data.get(key):
                continue
            asset = Path(data[key])
            disabled = asset.with_name(asset.stem + "_disabled.png")
            data[key] = str(disabled)
            if skins_dir is not None:
                target = Path(skins_dir) / disabled
                if not target.exists():
                    with Image.open(Path(skins_dir) / asset) as image:
                        _dim_artwork(image).save(target)
        if widget["type"] == "Label":
            data["textStyle"]["colour"] = argb("text.status.off")
    return result


def bind_control_active(entry, active_handle, ctx, definitions):
    """Bind a native component's two presentation states to a boolean handle.

    Both states use the same native control subtree and value bindings. The
    shared family owns this SDK IndexedEnabling implementation; callers only
    supply Active. The bound UI bank must reject writes while inactive.
    """
    import hashlib
    from components.native.button.button import entry as native_entry
    from components.native.selector.selector import passive_layer

    lookup = {d["key"]: d["value"] for d in definitions}
    widget = entry["componentData"]
    kind = widget["type"]
    if kind in lookup:
        base = lookup[kind]
        identity = kind
    else:
        identity = kind + hashlib.sha256(json.dumps(widget.get("data", {}), sort_keys=True).encode()).hexdigest()[:12]
        leaf = native_entry("Control", kind, copy.deepcopy(widget["data"]), (0, 0, 1, 1))
        leaf["bounds"]["boundsType"] = "Proportional"
        base = passive_layer([leaf])
    state_name = "NativeActive " + identity

    def inactive(name, source):
        target = "NativeInactive " + name
        if target not in lookup:
            # Register before descending to handle shared subcomponents once.
            result = apply_control_state(source, ctx.skins_dir, active=False)
            lookup[target] = result
            for child in result["componentsData"]:
                data = child["componentData"]
                if data["type"] in lookup:
                    original = data["type"]
                    data["type"] = inactive(original, lookup[original])
            ctx.add_component_definition(target, result)
        return target

    if state_name not in lookup:
        on_name = kind if kind in lookup else "NativeControl " + identity
        if on_name not in lookup:
            ctx.add_component_definition(on_name, base)
        off_name = inactive(identity, base)
        states = []
        for value, type_name in ((0, off_name), (1, on_name)):
            child = native_entry("State", type_name, {"version": 1, "handleName": "Data"},
                                 (0, 0, 1, 1), gates=(f"NativeActiveGate{value}",),
                                 focus="No" if value == 0 else "Children")
            child["bounds"]["boundsType"] = "Proportional"
            states.append(child)
        ctx.add_component_definition(state_name, passive_layer(states))
    entry["componentData"] = {"version": 1, "name": widget["name"], "type": state_name,
                              "data": {"version": 1, "handleName": "Data"}}
    for value in (0, 1):
        entry["handle remapping"]["map"].append({
            "key": f"NativeActiveGate{value}",
            "value": f"IndexedEnabling/{value}/2/{active_handle}",
        })
    return entry
