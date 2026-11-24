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


def _disabled_button(data, skins_dir):
    from PIL import Image

    steps = max(2, data["numButtonsInGroup"])
    lit = data["buttonId"]
    on, off = data["onImage"], data["offImage"]
    base = Path(off or on)
    filename = base.with_name(f"{Path(off).stem}_{Path(on).stem}_{lit}of{steps}_disabled.png")
    if skins_dir is not None:
        root = Path(skins_dir)
        with Image.open(root / base) as source:
            frame_size = source.size
        strip = Image.new("RGBA", (frame_size[0], frame_size[1] * steps))
        for art_name, selected in ((off, False), (on, True)):
            if art_name:
                with Image.open(root / art_name) as art:
                    frame = art.convert("RGBA").resize(frame_size, Image.Resampling.LANCZOS)
                for index in range(steps):
                    if (index == lit) == selected:
                        strip.paste(frame, (0, frame.height * index))
        _dim_artwork(strip).save(root / filename)
    return {"version": 2, "imageType": "FilmStrip", "colour": "0",
            "image": str(filename), "numFrames": steps, "handleName": data["handleName"]}


def _dim_artwork(image):
    from PIL import ImageOps
    grey = ImageOps.colorize(image.convert("L"), color("inactive.preview.dark"), color("inactive.preview.light"))
    grey.putalpha(image.convert("RGBA").getchannel("A"))
    return grey


def apply_control_state(definition, skins_dir, *, active):
    """Keep the native face, geometry and saved position without exposing an input."""
    if active:
        return definition
    from PIL import Image

    result = copy.deepcopy(definition)
    result.update(actions=[], ignoreMousePresses=True, disableCoarseDataWheel=True)
    result["componentsData"] = [c for c in result["componentsData"] if c["componentData"]["type"] != "Focus"]
    for child in result["componentsData"]:
        child["bounds"]["acceptsHWFocus"] = "No"
        widget = child["componentData"]
        data = widget["data"]
        if widget["type"] == "Button":
            widget["type"] = "Image"
            if data["onImage"] != data["offImage"]:
                widget["data"] = _disabled_button(data, skins_dir)
                continue
            data = widget["data"] = {"version": 2, "imageType": "Regular", "colour": "0",
                                     "image": data["offImage"]}
        elif widget["type"] == "Knob":
            widget["type"] = "Image"
            data = widget["data"] = {"version": 2, "imageType": "FilmStrip", "colour": "0",
                                     "image": data["filmStrip"], "numFrames": data["numFrames"],
                                     "handleName": data["handleName"]}
        if widget["type"] == "Image" and data["image"]:
            asset = Path(data["image"])
            disabled = asset.with_name(asset.stem + "_disabled.png")
            data["image"] = str(disabled)
            if skins_dir is None:
                continue
            path = Path(skins_dir) / asset
            target = Path(skins_dir) / disabled
            with Image.open(path) as img:
                _dim_artwork(img).save(target)
        elif widget["type"] == "Label":
            data["textStyle"]["colour"] = argb("text.status.off")
    return result
