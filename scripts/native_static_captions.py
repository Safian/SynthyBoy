# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native read-only captions; runtime background assets contain no UI."""
import json
from pathlib import Path
from components import style
from components.native.name_value.name_value import _label_component

CAPTIONS = Path(__file__).parent / "mpc-skin-extras/static-native-captions.json"


def host_font_height(row_size):
    """The TUI font height (a JUCE height, ascent + descent) for a caption size.

    A row size that is a token name ("font.page_title") is an em: the host draws an em of height / font.host.height.per.em,
    so the height is em x 1.521 (the previous round passed the em as the height: a 17 px em, 12 px caps). A plain number
    is a TUI height as before.
    """
    if isinstance(row_size, str):
        return round(style.size(row_size) * style.size("font.host.height.per.em"))
    return row_size


def caption_entries(view, *, prefix="Native Caption", gates=()):
    rows = json.loads(CAPTIONS.read_text()).get(view, [])
    for index, row in enumerate(rows):
        entry = _label_component(f"{prefix} {index}", "Text", "Data", tuple(row["rect"]),
                                 host_font_height(row["size"]), style.argb(row["colour"]), row.get("justify", "left") + " verticallyCentred")
        entry["componentData"]["data"]["text"] = row["text"]
        # Key 61 requires a valid model even for constant, read-only text.
        entry["handle remapping"]["map"] = [{"key": "Data", "value": "Parameter 0"}]
        if gates:
            entry["bounds"].update(version=2, showWhenDataModelInvalid="Hide", additionalInvalidatingHandles=list(gates))
        yield entry


def title_entry(name, text, rect, colour="text.value"):
    """One page title in the shared page-title em (components/sizes.json font.page_title, the overlay's kPageTitleSize), read-only, always shown."""
    entry = _label_component(name, "Text", "Data", tuple(rect), host_font_height("font.page_title"), style.argb(colour), "left verticallyCentred")
    entry["componentData"]["data"]["text"] = text
    entry["handle remapping"]["map"] = [{"key": "Data", "value": "Parameter 0"}]
    return entry
