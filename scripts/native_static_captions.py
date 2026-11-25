# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native read-only captions; runtime background assets contain no UI."""
import json
from pathlib import Path
from components import style
from components.native.name_value.name_value import _label_component

CAPTIONS = Path(__file__).parent / "mpc-skin-extras/static-native-captions.json"


def caption_entries(view, *, prefix="Native Caption", gates=()):
    rows = json.loads(CAPTIONS.read_text()).get(view, [])
    for index, row in enumerate(rows):
        entry = _label_component(f"{prefix} {index}", "Text", "Data", tuple(row["rect"]),
                                 row["size"], style.argb(row["colour"]), "left verticallyCentred")
        entry["componentData"]["data"]["text"] = row["text"]
        # Key 61 requires a valid model even for constant, read-only text.
        entry["handle remapping"]["map"] = [{"key": "Data", "value": "Parameter 0"}]
        if gates:
            entry["bounds"].update(version=2, showWhenDataModelInvalid="Hide", additionalInvalidatingHandles=list(gates))
        yield entry
