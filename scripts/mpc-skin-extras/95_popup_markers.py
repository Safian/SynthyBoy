# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Pack the detector's three contiguous, unconditional modal marker entries."""

import copy

from PIL import Image


ASSET = "native_modal_markers.png"
PATCHES = ((16, (254, 53, 108)), (28, (36, 199, 255)), (40, (144, 237, 67)))


def _matches(entries, skins):
    for entry, (x, rgb) in zip(entries, PATCHES):
        data, bounds = entry["componentData"], entry["bounds"]
        if (data["type"] != "Image" or set(entry) != {"version", "componentData", "handle remapping", "bounds"}
                or set(data) != {"version", "name", "type", "data"}
                or entry["handle remapping"] != {"version": 1, "map": []}
                or bounds != {"version": 1, "bounds": f"{x} 120 6 6", "boundsType": "Absolute",
                              "whenVisible": "Always", "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show"}):
            return False
        image = data["data"]
        if (image.get("version") != 2 or image.get("imageType") != "Regular" or image.get("colour") != "0"
                or set(image) != {"version", "imageType", "colour", "image"}):
            return False
        with Image.open(skins / image["image"]) as png:
            if png.size != (6, 6) or png.convert("RGBA").getcolors() != [(36, (*rgb, 255))]:
                return False
    return True


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    definitions = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    used = False
    for definition in definitions:
        items = definition["value"].get("componentsData", [])
        index = 0
        while index + len(PATCHES) <= len(items):
            stop = index + len(PATCHES)
            if _matches(items[index:stop], ctx.skins_dir):
                packed = copy.deepcopy(items[index])
                packed["componentData"]["name"] = "Native Modal Markers"
                packed["componentData"]["data"]["image"] = ASSET
                packed["bounds"]["bounds"] = "16 120 30 6"
                items[index:stop] = [packed]
                used = True
            index += 1
    if used:
        image = Image.new("RGBA", (30, 6))
        for x, rgb in PATCHES:
            image.paste((*rgb, 255), (x - 16, 0, x - 16 + 6, 6))
        image.save(ctx.skins_dir / ASSET)
