# SynthyBoy - native texts of the Mod page: the column heads of the matrix, the block names of the slot page (static, gated by the view of the page) and the slot count, the title of the slot
# page and the two lines of an empty filter (dynamic: native Labels bound to the text lines of the plugin, shell-mpc/native_text.h).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The old C++ pages drew these texts (matrix_.drawPage, detail_.draw); the C++ layer has no UI elements any more. The page title "MOD MATRIX" is the static caption of 99_backgrounds.py, gated here by the title
# line (a line is 0 while it is empty). A new file beside 70_mod.py: no existing definition changes, the entries are appended to the Mod tab.

import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
REPO_ROOT = SCRIPTS.parent
for p in (str(REPO_ROOT), str(SCRIPTS)):
    if p not in sys.path:
        sys.path.insert(0, p)
import native_ui_params as handles
from components import style
from components.native.name_value.name_value import _label_component
from native_static_captions import caption_entries, host_font_height

# the dynamic lines: (handle, name, rect, size token, colour, justification), the rectangles of the old drawExtra / drawExtra of the detail page (tab coordinates)
LINES = (
    (handles.NATIVE_TEXT_MOD_COUNT, "Count", (930, 4, 180, 56), "font.caption", "text.caption", "right"),
    (handles.NATIVE_TEXT_MOD_DETAIL, "Detail Title", (180, 4, 930, 56), "font.value.hero", "text.value", "left"),
    (handles.NATIVE_TEXT_MOD_EMPTY1, "Empty 1", (0, 295, 1280, 40), "font.page_title", "text.caption", "horizontallyCentred"),
    (handles.NATIVE_TEXT_MOD_EMPTY2, "Empty 2", (0, 359, 1280, 28), "font.caption", "text.label", "horizontallyCentred"),
)


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay" or not any(tab["tabName"] == "Mod" for tab in tui["pageData"]["tabs"]):
        return
    items = ctx.tab("Mod")["componentDefinition"]["componentsData"]
    base = handles.native_base_index(ctx.param_count)
    view = f"Parameter {base + handles.MOD_VIEW}"
    # the matrix views are 1 (normal) and 2 (scroll); the slot page 3 and 4 (delay in ms / in beats); five views in all (70_mod.py)
    for frame, layout in ((1, "normal"), (2, "scroll")):
        items.extend(caption_entries(f"mod_header_{layout}", prefix=f"Mod Native Header {layout}", gates=(f"IndexedEnabling/{frame}/5/{view}",)))
    items.extend(caption_entries("mod_detail_blocks", prefix="Mod Native Block", gates=(f"RangedEnabling/3/4/5/{view}",)))
    for handle, name, rect, size, colour, justification in LINES:
        label = _label_component(f"Mod Native Text {name}", "Value", "Data", rect, host_font_height(size), style.argb(colour),
                                 justification + " verticallyCentred")
        label["handle remapping"]["map"] = [{"key": "Data", "value": f"Parameter {base + handle}"}]
        items.append(label)
