# SynthyBoy - verified native controls for the Mod matrix and route editor.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import sys
import json
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parent.parent
REPO_ROOT = SCRIPTS.parent
for p in (str(REPO_ROOT), str(SCRIPTS)):
    if p not in sys.path:
        sys.path.insert(0, p)

from components import style
from components.native.radio_button.radio_button import render_segmented_button_art
from components.native.button.button import action_art, in_slot
from components.native.button.button import button as native_button

MODE_BUTTONS = (
    ("mode1", "ALL", 80, 56),
    ("mode2", "BY SOURCE", 140, 56),
    ("mode3", "BY DEST", 120, 56),
)


def _ensure_assets(skins_dir):
    """Generate verified native button and indicator PNGs into ctx.skins_dir."""
    skins_dir.mkdir(parents=True, exist_ok=True)

    # 2. Segmented Mode buttons (ALL, BY SOURCE, BY DEST)
    for tag, label, w, h in MODE_BUTTONS:
        on_art = render_segmented_button_art(label, True, w, h)
        off_art = render_segmented_button_art(label, False, w, h)
        on_art.save(skins_dir / f"mod_mode_{tag}_on.png")
        off_art.save(skins_dir / f"mod_mode_{tag}_off.png")

    # 3. Action buttons (+ ADD, EDIT, BACK, DELETE)
    edit_art = in_slot(action_art(text="EDIT", width=style.size('screen.ui_screen_mod.edit_art.width')), tuple(style.size('screen.ui_screen_mod.edit_art.size')))
    edit_art.save(skins_dir / "mod_action_edit.png")

    del_art = in_slot(action_art(text="DELETE", width=style.size('screen.ui_screen_mod.del_art.width')), tuple(style.size('screen.ui_screen_mod.del_art.size')))
    del_art.save(skins_dir / "mod_detail_delete.png")
    delete_rect = json.loads((SCRIPTS / "mpc-skin-extras" / "mod-native-contract.json").read_text())["detail_navigation"]["delete"]
    confirm_art = in_slot(action_art(text="CONFIRM?", width=delete_rect[2]), tuple(delete_rect[2:]))
    confirm_art.save(skins_dir / "mod_detail_confirm.png")


def apply(tui, qlinks, ctx):
    """Upgrade Mod screen definitions in place with verified native MPC components."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    if not any(tab.get("tabName") == "Mod" for tab in tui.get("pageData", {}).get("tabs", [])):
        return

    _ensure_assets(ctx.skins_dir)

    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    focus_col = style.argb("mod_row.focus")

    # Upgrade action button "EDIT" / "MORE"
    if "modNative_action_76x56" in defs:
        definition = defs["modNative_action_76x56"]
        definition["backgroundData"]["unfocussed"] = {"version": 1, "colour": style.argb('ink.black', 0), "image": "mod_action_edit.png"}
        definition["backgroundData"]["focussed"] = {"version": 1, "colour": focus_col, "image": "mod_action_edit.png"}
        definition["componentsData"] = []

    if "modNative_delete_120x56" in defs:
        definition = defs["modNative_delete_120x56"]
        clear = {"version": 1, "colour": style.argb("ink.black", 0), "image": ""}
        definition["backgroundData"] = {"version": 1, "unfocussed": clear.copy(), "focussed": clear.copy()}
        definition["actions"] = []
        definition["ignoreMousePresses"] = True
        delete_rect = json.loads((SCRIPTS / "mpc-skin-extras" / "mod-native-contract.json").read_text())["detail_navigation"]["delete"]
        rect = (0, 0, *delete_rect[2:])
        definition["componentsData"] = [
            native_button("Delete unarmed", rect, "mod_detail_delete.png", remaps=(("Data", "Data"),),
                          gates=("IndexedEnabling/0/2/Confirm",)),
            native_button("Delete confirm", rect, "mod_detail_confirm.png", remaps=(("Data", "Data"),),
                          gates=("IndexedEnabling/1/2/Confirm",)),
        ]

    # Tab background is neutral dark surface; Matrix and Detail backgrounds are gated components
    mod_tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") == "Mod"), None)
    if mod_tab is not None:
        mod_tab["qlinkBoundsData"] = []
        bg = mod_tab["componentDefinition"].setdefault("backgroundData", {})
        bg["focussed"] = {"version": 1, "colour": style.argb("surface.canvas"), "image": ""}
        bg["unfocussed"] = {"version": 1, "colour": style.argb("surface.canvas"), "image": ""}

