# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""FILTER SELECTOR: the 56 x 48 chain chip of the Filter page (face plus two power lamps), catalogue pictures.

The chip is drawn by the existing recipe `selector.filter_chain_definition` (tokens `filter.chain.*`); this module only
composes that definition's images for a given lamp state. The face is the routing chip ("CHAIN"); the two lamp
segments above it show whether Filter 1 and Filter 2 are on (accent.cyan) or off (theme.neon.track, dim).
Docs: docs/ui-components/filter_selector.md.
"""
import tempfile
from pathlib import Path

from components.native.selector.selector import filter_chain_definition
from components.style import size as _style_size

try:
    from PIL import Image
except ImportError:
    Image = None

# id, Filter 1 on, Filter 2 on, picture, caption (shown in the HTML, never in the picture)
STATES = (
    ("f1-on-f2-off", True, False, "filter-selector-f1-on-f2-off.png", "F1 be, F2 ki"),
    ("both-on", True, True, "filter-selector-both-on.png", "F1 be, F2 be"),
    ("both-off", False, False, "filter-selector-both-off.png", "Mindkét szűrő ki"),
    ("f1-off-f2-on", False, True, "filter-selector-f1-off-f2-on.png", "F1 ki, F2 be"),
)


def render_state(f1_on, f2_on):
    """The chip for one lamp state, composed from the images of the real definition (first face frame, lamp frames)."""
    geometry = _style_size("filter.chain")
    with tempfile.TemporaryDirectory() as tmp:
        definition = filter_chain_definition(tmp, "catalog choice menu")
        chip = Image.new("RGBA", (geometry["width"], geometry["height"]))
        for child in definition["componentsData"]:
            item = child["componentData"]
            x, y, w, h = map(int, child["bounds"]["bounds"].split())
            on = {"F1 power": f1_on, "F2 power": f2_on}.get(item["name"], False)
            with Image.open(Path(tmp) / item["data"]["image"]) as strip:
                frame = h if on else 0
                chip.alpha_composite(strip.convert("RGBA").crop((0, frame, w, frame + h)), (x, y))
    return chip


def catalog_sections():
    """The catalogue section: one picture per state."""
    return [{"heading": "Állapotok: a gomb arca a láncot jelzi, a felső kétszegmenses lámpasáv a két szűrő be/ki állapotát (cián = be, halvány = ki)",
             "images": [{"presentation": "sample", "file": file, "alt": f"FilterSelector: {caption}",
                         "caption": f"{caption} · {_style_size('filter.chain')['width']} × {_style_size('filter.chain')['height']} px"}
                        for _, _, _, file, caption in STATES]}]
