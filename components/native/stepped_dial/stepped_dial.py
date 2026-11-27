# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""STEPPED DIAL: our dial face without the gradient value arc, with N stops (2..8), catalogue pictures.

The face is `level.render_dial_frame(diameter, position / (N - 1), value_arc=False, stops=N)`: the ui::Knob recipe with only the
dark track ring, the white thumb and the pointer line, plus a dot on the track at each of the N stop angles of the 270 degree
sweep (`knob.start.degrees`, `knob.sweep.degrees`); the selected stop is in the pointer colour, the others dim. The pointer jumps
to the stop angle: N positions are N frames. Diameters: `stepped_dial.diameters` (36 / 40 / 48 / 56). Disabled: the inactive dial
(`active=False`) and `style.dim_artwork`. Docs: docs/ui-components/stepped_dial.md.
"""
import math

from components.native.level.level import render_dial_frame
from components.style import dim_artwork, size as _style_size

STATES = ("enabled", "disabled")
SAMPLE_KEYS = {"id", "file", "alt", "caption", "stops", "position", "diameter", "state"}


def stop_angle(index, count):
    """The pointer angle (degrees, screen, clockwise from 3 o'clock) of stop `index` of `count`."""
    return _style_size("knob.start.degrees") + index * _style_size("knob.sweep.degrees") / (count - 1)


def render_frame(diameter, position, count, *, active=True):
    """The dial picture with the pointer on stop `position` of `count`."""
    if diameter not in _style_size("stepped_dial")["diameters"]:
        raise ValueError(f"diameter must be one of {_style_size('stepped_dial')['diameters']}: {diameter!r}")
    if not 2 <= count <= _style_size("stepped_dial")["max_stops"] or not 0 <= position < count:
        raise ValueError("a stepped dial has 2 to 8 stops and a position among them")
    return render_dial_frame(diameter, position / (count - 1), value_arc=False, stops=count, active=active)


def render_sample(sample):
    image = render_frame(sample["diameter"], sample["position"], sample["stops"], active=sample["state"] == "enabled")
    return image if sample["state"] == "enabled" else dim_artwork(image)


def stepped_samples(entry):
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("stepped_dial entry needs a nonempty samples list")
    for item in entry["samples"]:
        if set(item) != SAMPLE_KEYS or item["state"] not in STATES:
            raise ValueError(f"stepped dial sample needs exactly {sorted(SAMPLE_KEYS)} and state in {STATES}")
        render_frame(item["diameter"], item["position"], item["stops"])      # validates diameter, stops and position
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("stepped dial sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    samples = stepped_samples(entry)
    groups = (("Állások és átmérők", lambda i: i["state"] == "enabled"), ("Disabled", lambda i: i["state"] == "disabled"))
    sections = []
    for heading, pick in groups:
        images = [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if pick(i)]
        if images:
            sections.append({"heading": heading, "images": images})
    sections.append({"heading": "Helyigény", "table": {"head": ["Példa", "Méret (px)"], "rows": [
        [i["caption"], "{0} × {0}".format(i["diameter"])] for i in samples]}})
    return sections
