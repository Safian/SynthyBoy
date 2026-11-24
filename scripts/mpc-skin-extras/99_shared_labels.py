# SynthyBoy - share passive stock Label styles in the packaged native skin.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import json
from collections import defaultdict

from pathlib import Path
import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components.native.selector.selector import passive_layer


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    page = tui["pageData"]
    definitions = page["componentDefinitions"]["localComponentDefinitions"]
    surfaces = [definition["value"] for definition in definitions]
    surfaces.extend(tab["componentDefinition"] for tab in page["tabs"])
    styles = defaultdict(list)
    for surface in surfaces:
        for entry in surface.get("componentsData", []):
            model, bounds = entry["componentData"], entry["bounds"]
            data = model["data"]
            if (model["type"] == "Label" and data.get("type") in ("Name", "Value")
                    and bounds["acceptsHWFocus"] == "No" and bounds["whenVisible"] == "Always"
                    and bounds["showWhenDataModelInvalid"] == "Hide"):
                styles[json.dumps(data)].append(entry)

    for entries in styles.values():
        # Sharing six copies already saves more than a full compound definition.
        if len(entries) < 6:
            continue
        name = f"sharedNativeLabel{len(definitions)}"
        label = copy.deepcopy(entries[0]["componentData"])
        label["name"] = "Text"
        leaf = {"version": 2, "componentData": label, "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "No", "whenVisible": "Always",
                           "showWhenDataModelInvalid": "Hide", "boundsType": "Proportional", "bounds": "0 0 1 1"}}
        ctx.add_component_definition(name, passive_layer([leaf]))
        for entry in entries:
            entry["componentData"]["type"] = name
            entry["componentData"]["data"] = {"version": 1, "handleName": "Data"}
            # The actual Label tests its own handle after the original remap.
            # The wrapper may have a different primary handle (e.g. Caption).
            entry["bounds"]["showWhenDataModelInvalid"] = "Show"
