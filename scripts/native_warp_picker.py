# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Host-native paged oscillator warp choices. Only a row writes the opener's Data."""
from PIL import Image, ImageDraw

from components import style
from components.native.button.button import button
from components.native.radio_button.radio_button import create_segmented_radio_button_group
from components.native.selector.selector import _choice_overlay, _image_component, _mark_native_menu, _value_label, save_png_filter0

DRAWER = "oscWarpChoices"
CONTROL = "oscNativeSettingWarpChoice"
ROWS = 8
COLUMNS = 2
PAGE_SIZE = ROWS * COLUMNS


def warp_picker(choices, skins):
    count = len(choices)
    pages = (count + PAGE_SIZE - 1) // PAGE_SIZE
    # Local is the SDK's stepped page-local UI handle (ui-reference §7.3).
    page_handle = f"Local/{pages}/synthyboyWarpPage"
    result = _choice_overlay(DRAWER)
    entries = result["componentsData"]
    entries.append(_image_component("Warp choices background", "osc_routing_overlay_bg.png", (0, 110, 1280, 690)))
    width, height = 612, 65
    selected_art = "osc_warp_row_selection.png"
    selected = Image.new("RGBA", (width, height))
    ImageDraw.Draw(selected).rounded_rectangle((2, 6, 7, height - 6), radius=2, fill=style.color("text.status.on"))
    save_png_filter0(selected, skins / selected_art)
    for index, choice in enumerate(choices):
        group, within = divmod(index, PAGE_SIZE)
        column, row = divmod(within, ROWS)
        x, y = 20 + column * 632, 121 + row * 72
        gate = f"IndexedEnabling/{group}/{pages}/{page_handle}"
        text = _value_label(choice, (x + 20, y, width - 30, height), 21)
        text["componentData"]["data"].update(type="Text", text=choice)
        text["componentData"]["data"]["textStyle"]["justification"] = "left verticallyCentred"
        text["bounds"].update(version=2, showWhenDataModelInvalid="Hide", additionalInvalidatingHandles=[gate])
        entries.extend([text, button("Choose " + choice, (x, y, width, height), selected_art,
                                     "osc_source_row_hit.png", index=index, count=count, gates=(gate,))])
    ranges = [f"{start + 1}–{min(start + PAGE_SIZE, count)}" for start in range(0, count, PAGE_SIZE)]
    entries.extend(create_segmented_radio_button_group("Warp choices", (20, 720, 1240, 64),
                                                       ranges, page_handle, skins))
    _mark_native_menu(entries)
    return result
