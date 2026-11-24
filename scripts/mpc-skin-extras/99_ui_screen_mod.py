# SynthyBoy - verified native controls for the Mod matrix and route editor.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import sys
import json
from pathlib import Path

from PIL import Image, ImageDraw

SCRIPTS = Path(__file__).resolve().parent.parent
REPO_ROOT = SCRIPTS.parent
for p in (str(REPO_ROOT), str(SCRIPTS)):
    if p not in sys.path:
        sys.path.insert(0, p)

from components import style
from components.native.level.level import write_strip
from components.native.mod_row.mod_row import write_assets as write_mod_row_assets
from native_choice_group import render_segmented_button_art
from native_icon_button import action_art, in_slot
from native_skin_font import semibold
from components.native.button.button import button as native_button

MODE_BUTTONS = (
    ("mode1", "ALL", 80, 56, 1),
    ("mode2", "BY SOURCE", 140, 56, 2),
    ("mode3", "BY DEST", 120, 56, 3),
)


def _render_matrix_background(skins_dir):
    """Generate authentic 1280x628 Mod matrix static furniture background."""
    w = style.size("mod_matrix.width")
    h = style.size("mod_matrix.height")
    im = Image.new("RGBA", (w, h), style.color("mod_matrix.surface"))
    draw = ImageDraw.Draw(im)

    font_title = semibold(style.size("mod_row.font.title"))
    font_header = semibold(style.size("mod_row.font.header"))
    font_num = semibold(style.size("mod_row.font.label"))
    font_slots = semibold(style.size("mod_row.font.slots"))

    # Top furniture: Title and header line
    draw.text((style.size("mod_matrix.title_x"), style.size("mod_matrix.title_y")),
              "MOD MATRIX", fill=style.color("mod_matrix.title"), font=font_title, anchor="lm")
    draw.line([(8, 64), (w - 8, 64)], fill=style.color("mod_matrix.border"), width=2)
    draw.text((style.size("mod_matrix.slots_x"), style.size("mod_matrix.slots_y")),
              "5 / 64 SLOTS", fill=style.color("mod_matrix.slots"), font=font_slots, anchor="rm")

    # Column headers centered on both axes over canonical column bounds
    hy = style.size("mod_matrix.header_center_y")
    draw.text((style.size("mod_matrix.header_center_source"), hy), "SOURCE", fill=style.color("mod_matrix.header"), font=font_header, anchor="mm")
    draw.text((style.size("mod_matrix.header_center_target"), hy), "TARGET", fill=style.color("mod_matrix.header"), font=font_header, anchor="mm")
    draw.text((style.size("mod_matrix.header_center_amount"), hy), "AMOUNT", fill=style.color("mod_matrix.header"), font=font_header, anchor="mm")
    draw.text((style.size("mod_matrix.header_center_polarity"), hy), "POLARITY", fill=style.color("mod_matrix.header"), font=font_header, anchor="mm")
    draw.text((style.size("mod_matrix.header_center_state"), hy), "STATE", fill=style.color("mod_matrix.header"), font=font_header, anchor="mm")

    # Rows 1..7 (start y = 102, step = 72)
    for r in range(style.size("mod_matrix.rows")):
        row_y = style.size("mod_matrix.row_start_y") + style.size("mod_matrix.row_step") * r
        # Slot number in left lane
        draw.text((style.size("mod_matrix.slot_num_x"), row_y + 28),
                  f"{r + 1:02d}", fill=style.color("mod_matrix.header"), font=font_num, anchor="mm")
        # Arrow -> between source and target
        cx, cy = style.size("mod_matrix.arrow_cx"), row_y + 28
        draw.line([(cx - 7, cy), (cx + 7, cy)], fill=style.color("mod_matrix.arrow"), width=2)
        draw.line([(cx + 2, cy - 4), (cx + 7, cy), (cx + 2, cy + 4)], fill=style.color("mod_matrix.arrow"), width=2)
        # Subtle horizontal divider under row
        draw.line([(8, row_y + 71), (w - 8, row_y + 71)], fill=style.color("mod_matrix.divider"), width=1)

    im.save(skins_dir / "mod_matrix_background.png")


def _render_detail_background(skins_dir):
    """Generate authentic 1280x628 Mod detail static furniture background."""
    w = style.size("mod_matrix.width")
    h = style.size("mod_matrix.height")
    im = Image.new("RGBA", (w, h), style.color("mod_matrix.surface"))
    draw = ImageDraw.Draw(im)

    font_title = semibold(style.size("mod_row.font.title"))
    font_header = semibold(style.size("mod_row.font.header"))

    # Top furniture: Title and header line
    draw.text((180, 32),
              "SLOT 09   LFO 2  ->  Filter 1 Cutoff", fill=style.color("mod_matrix.title"), font=font_title, anchor="lm")
    draw.line([(8, 64), (w - 8, 64)], fill=style.color("mod_matrix.border"), width=2)
    # Summary row bottom divider
    draw.line([(8, 120), (w - 8, 120)], fill=style.color("mod_matrix.divider"), width=1)

    # Three Column Panels (y = 126, h = 494)
    # Column 1: AUXILIARY SOURCE (x = 8, w = 449)
    draw.rounded_rectangle([(8, 126), (8 + 449 - 1, 126 + 494 - 1)],
                           radius=6, fill=style.color("mod_matrix.surface"), outline=style.color("mod_matrix.border"), width=2)
    draw.text((8 + 16, 126 + 18), "AUXILIARY SOURCE", fill=style.color("mod_matrix.header"), font=font_header, anchor="lm")
    draw.line([(8 + 8, 126 + 32), (8 + 449 - 8, 126 + 32)], fill=style.color("mod_matrix.divider"), width=1)

    # Column 2: SHAPE (x = 465, w = 324)
    draw.rounded_rectangle([(465, 126), (465 + 324 - 1, 126 + 494 - 1)],
                           radius=6, fill=style.color("mod_matrix.surface"), outline=style.color("mod_matrix.border"), width=2)
    draw.text((465 + 16, 126 + 18), "SHAPE", fill=style.color("mod_matrix.header"), font=font_header, anchor="lm")
    draw.line([(465 + 8, 126 + 32), (465 + 324 - 8, 126 + 32)], fill=style.color("mod_matrix.divider"), width=1)

    # Column 3: TIME (x = 797, w = 475)
    draw.rounded_rectangle([(797, 126), (797 + 475 - 1, 126 + 494 - 1)],
                           radius=6, fill=style.color("mod_matrix.surface"), outline=style.color("mod_matrix.border"), width=2)
    draw.text((797 + 16, 126 + 18), "TIME", fill=style.color("mod_matrix.header"), font=font_header, anchor="lm")
    draw.line([(797 + 8, 126 + 32), (797 + 475 - 8, 126 + 32)], fill=style.color("mod_matrix.divider"), width=1)

    im.save(skins_dir / "mod_detail_background.png")


def _render_board_background(skins_dir):
    """Generate authentic 1280x628 Mod quick-add source board static background."""
    w = style.size("mod_matrix.width")
    h = style.size("mod_matrix.height")
    im = Image.new("RGBA", (w, h), style.color("mod_matrix.surface"))
    draw = ImageDraw.Draw(im)

    font_title = semibold(style.size("mod_row.font.title"))
    font_header = semibold(style.size("mod_row.font.header"))
    font_slots = semibold(style.size("mod_row.font.slots"))

    # Top furniture: Title and header line
    draw.text((style.size("mod_matrix.title_x"), style.size("mod_matrix.title_y")),
              "MOD MATRIX", fill=style.color("mod_matrix.title"), font=font_title, anchor="lm")
    draw.line([(8, 64), (w - 8, 64)], fill=style.color("mod_matrix.border"), width=2)
    draw.text((style.size("mod_matrix.slots_x"), style.size("mod_matrix.slots_y")),
              "0 / 64 SLOTS", fill=style.color("mod_matrix.slots"), font=font_slots, anchor="rm")

    # Headline
    draw.text((24, 88), "TAP A SOURCE TO START A MODULATION", fill=style.color("mod_matrix.slots"), font=font_header, anchor="lm")

    # Group headers & dividers
    groups = [
        ("ENVELOPES", 144, 186),
        ("LFOS", 228, 270),
        ("MACROS", 312, 354),
        ("PLAYER", 396, 438),
        ("SOUND", 480, 522),
    ]
    for name, gy, dy in groups:
        draw.text((24, gy), name, fill=style.color("mod_matrix.header"), font=font_header, anchor="lm")
        draw.line([(8, dy), (w - 8, dy)], fill=style.color("mod_matrix.divider"), width=1)

    im.save(skins_dir / "mod_board_background.png")


def _ensure_assets(skins_dir):
    """Generate verified native button and indicator PNGs into ctx.skins_dir."""
    skins_dir.mkdir(parents=True, exist_ok=True)

    # 1. Compose public assets from components.native.mod_row (selectors, buttons, slider art, etc.)
    write_mod_row_assets(skins_dir)

    # 2. Segmented Mode buttons (ALL, BY SOURCE, BY DEST)
    for tag, label, w, h, _ in MODE_BUTTONS:
        on_art = render_segmented_button_art(label, True, w, h)
        off_art = render_segmented_button_art(label, False, w, h)
        on_art.save(skins_dir / f"mod_mode_{tag}_on.png")
        off_art.save(skins_dir / f"mod_mode_{tag}_off.png")

    # Safety compatibility: seg3_on / seg3_off if referenced anywhere
    if not (skins_dir / "seg3_on.png").is_file():
        seg_on = render_segmented_button_art("MODE", True, 80, 56)
        seg_off = render_segmented_button_art("MODE", False, 80, 56)
        seg_on.save(skins_dir / "seg3_on.png")
        seg_off.save(skins_dir / "seg3_off.png")

    # 3. Action buttons (+ ADD, EDIT, BACK, DELETE)
    add_art = in_slot(action_art(text="+ ADD", width=120), (120, 56))
    add_art.save(skins_dir / "mod_action_add.png")

    edit_art = in_slot(action_art(text="EDIT", width=76), (76, 56))
    edit_art.save(skins_dir / "mod_action_edit.png")

    back_art = in_slot(action_art(text="BACK", width=100), (100, 56))
    back_art.save(skins_dir / "mod_detail_back.png")

    del_art = in_slot(action_art(text="DELETE", width=120), (120, 56))
    del_art.save(skins_dir / "mod_detail_delete.png")
    delete_rect = json.loads((SCRIPTS / "mpc-skin-extras" / "mod-native-contract.json").read_text())["detail_navigation"]["delete"]
    confirm_art = in_slot(action_art(text="CONFIRM?", width=delete_rect[2]), tuple(delete_rect[2:]))
    confirm_art.save(skins_dir / "mod_detail_confirm.png")

    # 4. Group selector background compatibility
    if (skins_dir / "mod_selector_260x56.png").is_file():
        import shutil
        shutil.copy2(skins_dir / "mod_selector_260x56.png", skins_dir / "mod_selector_group.png")

    # 5. Dial filmstrips for Detail (90px)
    write_strip(skins_dir / "mod_dial_90_uni.png", frame_size=90, bipolar=False, active=True)
    write_strip(skins_dir / "mod_dial_90_bi.png", frame_size=90, bipolar=True, active=True)

    # 6. Static backgrounds for Matrix, Detail, and Quick-add Board
    _render_matrix_background(skins_dir)
    _render_detail_background(skins_dir)
    _render_board_background(skins_dir)

    # 7. Board source chip button art
    board_chips = (
        ("ENV 1", 1), ("ENV 2", 2), ("ENV 3", 3),
        ("LFO 1", 7), ("LFO 2", 8), ("LFO 3", 9), ("LFO 4", 10),
        ("MACRO 1", 19), ("MACRO 2", 20), ("MACRO 3", 21), ("MACRO 4", 22),
        ("WHEEL", 27), ("VELO", 23), ("AT", 25), ("KEY", 24),
        ("PITCH", 26), ("FIXED", 57),
    )
    for label, _ in board_chips:
        tag = label.lower().replace(" ", "")
        chip_art = in_slot(action_art(text=label, width=100), (100, 56))
        chip_art.save(skins_dir / f"mod_chip_{tag}.png")


def apply(tui, qlinks, ctx):
    """Upgrade Mod screen definitions in place with verified native MPC components."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    if not any(tab.get("tabName") == "Mod" for tab in tui.get("pageData", {}).get("tabs", [])):
        return

    _ensure_assets(ctx.skins_dir)

    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    focus_col = style.argb("mod_row.focus")

    # Upgrade Mode buttons to use verified segmented art without duplicate label overlay
    for tag, _, w, h, button_id in MODE_BUTTONS:
        def_key = f"modNative_{tag}_{w}x{h}"
        if def_key in defs:
            definition = defs[def_key]
            definition["backgroundData"]["unfocussed"] = {"version": 1, "colour": "00000000", "image": ""}
            definition["backgroundData"]["focussed"] = {"version": 1, "colour": focus_col, "image": ""}
            children = definition.get("componentsData", [])
            if children and children[0]["componentData"]["type"] == "Button":
                btn = children[0]["componentData"]
                btn["data"]["onImage"] = f"mod_mode_{tag}_on.png"
                btn["data"]["offImage"] = f"mod_mode_{tag}_off.png"
                btn["data"]["buttonId"] = button_id
                btn["data"]["numButtonsInGroup"] = 4
                btn["data"]["gestureBehaviour"] = "Instant"
            # Ensure no secondary duplicate text label
            if len(children) > 1:
                del children[1:]

    # Upgrade action button "+ ADD"
    if "modNative_action_120x56" in defs:
        definition = defs["modNative_action_120x56"]
        definition["backgroundData"]["unfocussed"] = {"version": 1, "colour": "00000000", "image": "mod_action_add.png"}
        definition["backgroundData"]["focussed"] = {"version": 1, "colour": focus_col, "image": "mod_action_add.png"}
        definition["componentsData"] = []

    # Upgrade action button "EDIT" / "MORE"
    if "modNative_action_76x56" in defs:
        definition = defs["modNative_action_76x56"]
        definition["backgroundData"]["unfocussed"] = {"version": 1, "colour": "00000000", "image": "mod_action_edit.png"}
        definition["backgroundData"]["focussed"] = {"version": 1, "colour": focus_col, "image": "mod_action_edit.png"}
        definition["componentsData"] = []

    # Upgrade detail action buttons: BACK and DELETE
    if "modNative_action_100x56" in defs:
        definition = defs["modNative_action_100x56"]
        definition["backgroundData"]["unfocussed"] = {"version": 1, "colour": "00000000", "image": "mod_detail_back.png"}
        definition["backgroundData"]["focussed"] = {"version": 1, "colour": focus_col, "image": "mod_detail_back.png"}
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

    # Upgrade dropdown group selector
    if "modNative_group_260x56" in defs:
        definition = defs["modNative_group_260x56"]
        definition["backgroundData"]["unfocussed"] = {"version": 1, "colour": "00000000", "image": "mod_selector_group.png"}
        definition["backgroundData"]["focussed"] = {"version": 1, "colour": focus_col, "image": "mod_selector_group.png"}

    # Upgrade row list selectors
    if "modNative_list_176x56" in defs:
        definition = defs["modNative_list_176x56"]
        definition["backgroundData"]["unfocussed"] = {"version": 1, "colour": "00000000", "image": "mod_selector_176x56.png"}
        definition["backgroundData"]["focussed"] = {"version": 1, "colour": focus_col, "image": "mod_selector_176x56.png"}

    if "modNative_list_244x56" in defs:
        definition = defs["modNative_list_244x56"]
        definition["backgroundData"]["unfocussed"] = {"version": 1, "colour": "00000000", "image": "mod_selector_244x56.png"}
        definition["backgroundData"]["focussed"] = {"version": 1, "colour": focus_col, "image": "mod_selector_244x56.png"}

    # Tab background is neutral dark surface; Matrix and Detail backgrounds are gated components
    mod_tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") == "Mod"), None)
    if mod_tab is not None:
        mod_tab["qlinkBoundsData"] = []
        bg = mod_tab["componentDefinition"].setdefault("backgroundData", {})
        bg["focussed"] = {"version": 1, "colour": style.argb("mod_matrix.surface"), "image": ""}
        bg["unfocussed"] = {"version": 1, "colour": style.argb("mod_matrix.surface"), "image": ""}

