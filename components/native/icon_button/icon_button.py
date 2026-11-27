# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""ICON BUTTON: the Button recipe with an icon where the text is, square (width = height), catalogue pictures.

The face is `button.action_art(icon=..., height=..., look=...)`: look "filled" is the Button's legacy face, "ghost"
has no fill and no keyline (icon only, transparent). Icons: the recipes POWER, EXPAND and CLOSE (an X). States: power on
(accent.green) / off (text.icon), disabled (the disabled look of style.dim_artwork). Docs: docs/ui-components/icon_button.md.
"""
from components.native.button.button import action_art
from components.style import color as _style_color, dim_artwork, size as _style_size

ICONS = ("power", "expand", "close")
STATES = ("on", "off", "disabled")
LOOKS = ("filled", "ghost")
SAMPLE_KEYS = {"id", "file", "alt", "caption", "icon", "height", "state", "look"}


def render_sample(sample):
    """One icon button: square at its height token; "on" lights POWER green, "disabled" dims the off face."""
    icon, state = sample["icon"], sample["state"]
    colour = _style_color("accent.green" if (icon == "power" and state == "on") else "text.icon")
    face = action_art(icon=icon, height=_style_size(sample["height"]), colour=colour,
                      look="legacy" if sample["look"] == "filled" else "ghost")
    return dim_artwork(face) if state == "disabled" else face


def icon_samples(entry):
    """The samples of a catalogue "icon_button" entry, validated; a new state or size is one more entry."""
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("icon_button entry needs a nonempty samples list")
    for item in entry["samples"]:
        if set(item) != SAMPLE_KEYS or item["icon"] not in ICONS or item["state"] not in STATES or item["look"] not in LOOKS:
            raise ValueError(f"icon button sample needs exactly {sorted(SAMPLE_KEYS)}, icon in {ICONS}, state in {STATES}, look in {LOOKS}")
        _style_size(f"button.size.{_style_size(item['height'])}")
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("icon button sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    samples = icon_samples(entry)
    groups = (("Teli (régi külső), méret szerint", lambda i: i["look"] == "filled" and i["state"] != "disabled"),
              ("Disabled", lambda i: i["state"] == "disabled"),
              ("Ghost: kitöltés és keret nélkül, átlátszó", lambda i: i["look"] == "ghost"))
    sections = []
    for heading, pick in groups:
        images = [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if pick(i)]
        if images:
            sections.append({"heading": heading, "images": images})
    return sections
