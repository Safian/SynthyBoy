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
        entry = _label_component(f"{prefix} {index}", "Text", "", tuple(row["rect"]),
                                 row["size"], style.argb(row["colour"]), "left verticallyCentred")
        entry["componentData"]["data"]["text"] = row["text"]
        entry["bounds"]["showWhenDataModelInvalid"] = "Show"
        if gates:
            entry["bounds"].update(version=2, additionalInvalidatingHandles=list(gates))
        yield entry
