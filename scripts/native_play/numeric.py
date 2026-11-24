# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import copy


def build_numeric(name, rect, template, parameter):
    """Create a deep copy of a native numeric field instance.

    Rebinds only 'Data' to parameter while preserving original gestures and
    modal dismissal.
    """
    comp = copy.deepcopy(template)
    comp.setdefault("componentData", {})["name"] = name

    bounds_str = (
        "%d %d %d %d" % tuple(rect)
        if isinstance(rect, (list, tuple))
        else str(rect)
    )
    comp.setdefault("bounds", {})["bounds"] = bounds_str

    remap = comp.setdefault("handle remapping", {"version": 1, "map": []})
    mappings = remap.setdefault("map", [])

    for m in mappings:
        if m.get("key") == "Data":
            m["value"] = parameter
            break
    else:
        mappings.append({"key": "Data", "value": parameter})

    return comp
