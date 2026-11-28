# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native on-screen touch keyboard for Macro rename and LFO shape naming.

Follows the design verified in docs/review/TEXT-ENTRY-NATIVE-KEYBOARD-2026-11-27.md:
- One modal component ("native keyboard overlay"), instanced full tab (1280 x 628) on the Macros, LFO, RND and Play tabs and shown by the instance bounds while TextEdit Context is in range.
- 4 key rows in standard QWERTY layout.
- Keys 40 px high with 8 px gaps, composed from shared Button recipe (components.native.button).
- 3 layers switched via Local/3/layer radio group:
    Layer 0: Lowercase letters
    Layer 1: Uppercase letters (Shift)
    Layer 2: Numbers & Symbols (?123)
- Key codes: 0 (idle), 1..95 (ASCII 32..126 minus 31), 96 (backspace), 97 (clear),
  98 (OK), 99 (cancel). Bound to ResetOnGestureEnd/Parameter <TextEdit Key>.
- Read-only Labels for context title (TextEdit Context) and text display (TextEdit Text).
"""
import hashlib
import math
from pathlib import Path

from components.native.button.button import action_art, entry, in_slot
from components.native.name_value.name_value import _bounds, _label_component
from components.style import (
    argb as _style_argb,
    color as _style_color,
    rgb as _style_rgb,
    size as _style_size,
)

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

OVERLAY_NAME = "native keyboard overlay"
NATIVE_KEYBOARD_OVERLAY_NAME = OVERLAY_NAME
OVERLAY_WIDTH = 1280
OVERLAY_HEIGHT = 800

PANEL_X = 164
PANEL_Y = 220
PANEL_WIDTH = 952
PANEL_HEIGHT = 340
PANEL_RECT = (PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT)

KEYS_X = 204
KEYS_WIDTH = 872
KEY_HEIGHT = 40
KEY_GAP = 8

NUM_KEY_BUTTONS = 100

KEY_IDLE = 0
KEY_SPACE = 1          # ASCII 32 - 31
KEY_BACKSPACE = 96
KEY_CLEAR = 97
KEY_OK = 98
KEY_CANCEL = 99

LAYER_HANDLE = "Local/3/layer"
NUM_LAYERS = 3

ROW_Y = [
    PANEL_Y + 100,  # Row 1: y = 320
    PANEL_Y + 148,  # Row 2: y = 368
    PANEL_Y + 196,  # Row 3: y = 416
    PANEL_Y + 244,  # Row 4: y = 464
]

FONT_FILE = Path(__file__).resolve().parent / "img" / _style_size('font.file.semibold')


def semibold(font_size):
    if ImageFont is None or not FONT_FILE.is_file():
        return None
    return ImageFont.truetype(str(FONT_FILE), font_size)


def ascii_to_key_code(char):
    """Map printable ASCII character to plugin key code (1..95)."""
    val = ord(char)
    if 32 <= val <= 126:
        return val - 31
    return None


def key_code_to_ascii(code):
    """Map plugin key code (1..95) back to ASCII character."""
    if 1 <= code <= 95:
        return chr(code + 31)
    return None


def layer_gate(layer_index):
    """SDK IndexedEnabling gate for Local/3/layer."""
    return [f"IndexedEnabling/{layer_index}/3/{LAYER_HANDLE}"]


def store_key_face(skins_dir, rect, *, icon=None, text=None, colour=None, look=None):
    """Store button artwork face in skins_dir (or return filename based on hash)."""
    width, height = rect[2], rect[3]
    ink = {} if colour is None else {"colour": colour}
    look_arg = {} if look is None else {"look": look}
    art = action_art(icon=icon, text=text, width=width, height=height, **ink, **look_arg)
    image = in_slot(art, (width, height))
    digest = hashlib.sha256(str(image.size).encode() + image.tobytes()).hexdigest()[:20]
    filename = f"native_kbd_{digest}.png"
    if skins_dir is not None:
        p = Path(skins_dir) / filename
        if not p.is_file():
            p.parent.mkdir(parents=True, exist_ok=True)
            image.save(p)
    return filename


def store_panel_art(skins_dir, rect, *, fill_color, border_color, radius=12, frame=1):
    """Generate and store panel background image."""
    width, height = rect[2], rect[3]
    scale = 2
    sw, sh = width * scale, height * scale
    image = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    pen = ImageDraw.Draw(image)
    box = (0, 0, sw - 1, sh - 1)
    pen.rounded_rectangle(box, radius * scale, fill=fill_color, outline=border_color, width=frame * scale)
    out = image.resize((width, height), Image.Resampling.LANCZOS)
    digest = hashlib.sha256(str(out.size).encode() + out.tobytes()).hexdigest()[:20]
    filename = f"native_kbd_bg_{digest}.png"
    if skins_dir is not None:
        p = Path(skins_dir) / filename
        if not p.is_file():
            p.parent.mkdir(parents=True, exist_ok=True)
            out.save(p)
    return filename


def _key_button(name, rect, on_img, off_img, key_code, num_buttons=NUM_KEY_BUTTONS, handle="Data", remaps=(), gates=()):
    """Helper creating a Button component bound to a momentary key code or layer."""
    return entry(
        name,
        "Button",
        {
            "version": 2,
            "onImage": on_img,
            "offImage": off_img or on_img,
            "buttonId": key_code,
            "numButtonsInGroup": num_buttons,
            "handleName": handle,
            "gestureBehaviour": "Instant",
        },
        rect,
        remaps,
        gates,
    )


def build_keyboard_overlay_definition(
    skins_dir=None,
    context_param="Parameter 3887",
    key_param="ResetOnGestureEnd/Parameter 3888",
    text_param="Parameter 3889",
):
    """Construct the native MPC keyboard component definition.

    Returns the canonical dictionary structure for localComponentDefinitions.
    """
    ctx_handle = f"Parameter {context_param}" if isinstance(context_param, int) else str(context_param)
    txt_handle = f"Parameter {text_param}" if isinstance(text_param, int) else str(text_param)
    if isinstance(key_param, int):
        key_handle = f"ResetOnGestureEnd/Parameter {key_param}"
    elif str(key_param).startswith("ResetOnGestureEnd/"):
        key_handle = str(key_param)
    else:
        key_handle = f"ResetOnGestureEnd/{key_param}"

    components_data = []

    # 1. Main Keyboard Panel Box
    panel_bg = store_panel_art(
        skins_dir,
        PANEL_RECT,
        fill_color=_style_color("surface.panel"),
        border_color=_style_color("control.border"),
        radius=12,
        frame=1,
    )
    components_data.append({
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "Keyboard Panel Box",
            "type": "Image",
            "data": {
                "version": 1,
                "image": panel_bg,
                "imageType": "Regular",
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": _bounds(PANEL_RECT, focus="No"),
    })

    # 2. Title Label (displays TextEdit Context string, e.g. "NAME MACRO 1")
    title_rect = (KEYS_X, PANEL_Y + 16, KEYS_WIDTH - 48, 24)
    components_data.append(_label_component(
        "Keyboard Title",
        "Value",
        ctx_handle,
        title_rect,
        font_height=14.0,
        colour=_style_argb("text.caption"),
        justification="left verticallyCentred",
    ))

    # 3. Top-right Cancel / Close IconButton (sends KEY_CANCEL)
    close_rect = (KEYS_X + KEYS_WIDTH - 32, PANEL_Y + 12, 32, 32)
    close_face = store_key_face(skins_dir, close_rect, icon="close", look="ghost", colour=_style_color("text.muted"))
    components_data.append(_key_button(
        "Keyboard Header Close",
        close_rect,
        close_face,
        close_face,
        KEY_CANCEL,
        NUM_KEY_BUTTONS,
        handle="Data",
        remaps=[("Data", key_handle)],
    ))

    # 4. Text Field Background Frame & Accumulated Text Label
    field_rect = (KEYS_X, PANEL_Y + 48, KEYS_WIDTH, 44)
    field_bg = store_panel_art(
        skins_dir,
        field_rect,
        fill_color=_style_color("field.surface"),
        border_color=_style_color("field.border"),
        radius=6,
        frame=1,
    )
    components_data.append({
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "Keyboard Text Field Frame",
            "type": "Image",
            "data": {
                "version": 1,
                "image": field_bg,
                "imageType": "Regular",
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": _bounds(field_rect, focus="No"),
    })

    text_label_rect = (KEYS_X + 16, PANEL_Y + 50, KEYS_WIDTH - 32, 40)
    components_data.append(_label_component(
        "Keyboard Text Display",
        "Value",
        txt_handle,
        text_label_rect,
        font_height=18.0,
        colour=_style_argb("text.value"),
        justification="left verticallyCentred",
    ))

    # -------------------------------------------------------------------------
    # Keys: Row 1 (10 keys, y = 320)
    # -------------------------------------------------------------------------
    r1_y = ROW_Y[0]
    r1_lower = ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"]
    r1_upper = ["Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"]
    r1_sym =   ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"]
    key_w = 80

    for i in range(10):
        kx = KEYS_X + i * (key_w + KEY_GAP)
        krect = (kx, r1_y, key_w, KEY_HEIGHT)

        # Layer 0 (lower)
        char0 = r1_lower[i]
        face0 = store_key_face(skins_dir, krect, text=char0, look="shaded")
        components_data.append(_key_button(
            f"Key L0 {char0}", krect, face0, face0, ascii_to_key_code(char0),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(0),
        ))

        # Layer 1 (upper)
        char1 = r1_upper[i]
        face1 = store_key_face(skins_dir, krect, text=char1, look="shaded")
        components_data.append(_key_button(
            f"Key L1 {char1}", krect, face1, face1, ascii_to_key_code(char1),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(1),
        ))

        # Layer 2 (sym)
        char2 = r1_sym[i]
        face2 = store_key_face(skins_dir, krect, text=char2, look="shaded")
        components_data.append(_key_button(
            f"Key L2 {char2}", krect, face2, face2, ascii_to_key_code(char2),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(2),
        ))

    # -------------------------------------------------------------------------
    # Keys: Row 2 (9 keys, y = 368, indented by 44 px)
    # -------------------------------------------------------------------------
    r2_y = ROW_Y[1]
    r2_lower = ["a", "s", "d", "f", "g", "h", "j", "k", "l"]
    r2_upper = ["A", "S", "D", "F", "G", "H", "J", "K", "L"]
    r2_sym =   ["!", "@", "#", "$", "%", "&", "*", "(", ")"]
    r2_offset_x = KEYS_X + 44

    for i in range(9):
        kx = r2_offset_x + i * (key_w + KEY_GAP)
        krect = (kx, r2_y, key_w, KEY_HEIGHT)

        char0 = r2_lower[i]
        face0 = store_key_face(skins_dir, krect, text=char0, look="shaded")
        components_data.append(_key_button(
            f"Key L0 {char0}", krect, face0, face0, ascii_to_key_code(char0),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(0),
        ))

        char1 = r2_upper[i]
        face1 = store_key_face(skins_dir, krect, text=char1, look="shaded")
        components_data.append(_key_button(
            f"Key L1 {char1}", krect, face1, face1, ascii_to_key_code(char1),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(1),
        ))

        char2 = r2_sym[i]
        face2 = store_key_face(skins_dir, krect, text=char2, look="shaded")
        components_data.append(_key_button(
            f"Key L2 {char2}", krect, face2, face2, ascii_to_key_code(char2),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(2),
        ))

    # -------------------------------------------------------------------------
    # Keys: Row 3 (Shift/Layer, 7 keys, Backspace, y = 416)
    # -------------------------------------------------------------------------
    r3_y = ROW_Y[2]
    side_w = 124

    # 3.1 Left key: Shift / Layer toggle
    shift_rect = (KEYS_X, r3_y, side_w, KEY_HEIGHT)

    # In Layer 0: SHIFT button switches to Layer 1 (buttonId=1)
    shift_f0 = store_key_face(skins_dir, shift_rect, text="SHIFT", look="shaded")
    components_data.append(_key_button(
        "Key Shift L0", shift_rect, shift_f0, shift_f0, 1, 3,
        handle="Data", remaps=[("Data", LAYER_HANDLE)], gates=layer_gate(0),
    ))

    # In Layer 1: Active SHIFT button switches back to Layer 0 (buttonId=0)
    shift_f1 = store_key_face(skins_dir, shift_rect, text="SHIFT", look="active", colour=_style_color("radio.active.text"))
    components_data.append(_key_button(
        "Key Shift L1", shift_rect, shift_f1, shift_f1, 0, 3,
        handle="Data", remaps=[("Data", LAYER_HANDLE)], gates=layer_gate(1),
    ))

    # In Layer 2: ABC button switches back to Layer 0 (buttonId=0)
    abc_f2 = store_key_face(skins_dir, shift_rect, text="ABC", look="shaded")
    components_data.append(_key_button(
        "Key ABC L2", shift_rect, abc_f2, abc_f2, 0, 3,
        handle="Data", remaps=[("Data", LAYER_HANDLE)], gates=layer_gate(2),
    ))

    # 3.2 Middle 7 keys
    r3_lower = ["z", "x", "c", "v", "b", "n", "m"]
    r3_upper = ["Z", "X", "C", "V", "B", "N", "M"]
    r3_sym =   ["-", "_", "/", ":", ";", ",", "."]
    r3_middle_x = KEYS_X + side_w + KEY_GAP

    for i in range(7):
        kx = r3_middle_x + i * (key_w + KEY_GAP)
        krect = (kx, r3_y, key_w, KEY_HEIGHT)

        char0 = r3_lower[i]
        face0 = store_key_face(skins_dir, krect, text=char0, look="shaded")
        components_data.append(_key_button(
            f"Key L0 {char0}", krect, face0, face0, ascii_to_key_code(char0),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(0),
        ))

        char1 = r3_upper[i]
        face1 = store_key_face(skins_dir, krect, text=char1, look="shaded")
        components_data.append(_key_button(
            f"Key L1 {char1}", krect, face1, face1, ascii_to_key_code(char1),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(1),
        ))

        char2 = r3_sym[i]
        face2 = store_key_face(skins_dir, krect, text=char2, look="shaded")
        components_data.append(_key_button(
            f"Key L2 {char2}", krect, face2, face2, ascii_to_key_code(char2),
            NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)], gates=layer_gate(2),
        ))

    # 3.3 Right key: Backspace (common to all layers, sends KEY_BACKSPACE)
    bksp_rect = (KEYS_X + KEYS_WIDTH - side_w, r3_y, side_w, KEY_HEIGHT)
    bksp_face = store_key_face(skins_dir, bksp_rect, text="BKSP", look="shaded")
    components_data.append(_key_button(
        "Key Backspace", bksp_rect, bksp_face, bksp_face, KEY_BACKSPACE,
        NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)],
    ))

    # -------------------------------------------------------------------------
    # Keys: Row 4 (Layer Switch, Clear, Space, Cancel, OK, y = 464)
    # Widths: 120 + 100 + 360 + 120 + 140 = 840 (+ 4*8 = 872)
    # -------------------------------------------------------------------------
    r4_y = ROW_Y[3]
    w_layer = 120
    w_clear = 100
    w_space = 360
    w_cancel = 120
    w_ok = 140

    x_layer = KEYS_X
    x_clear = x_layer + w_layer + KEY_GAP
    x_space = x_clear + w_clear + KEY_GAP
    x_cancel = x_space + w_space + KEY_GAP
    x_ok = x_cancel + w_cancel + KEY_GAP

    # 4.1 Layer toggle button (?123 in L0/L1 -> buttonId=2; ABC in L2 -> buttonId=0)
    layer_rect = (x_layer, r4_y, w_layer, KEY_HEIGHT)
    sym_face = store_key_face(skins_dir, layer_rect, text="?123", look="shaded")
    abc_face = store_key_face(skins_dir, layer_rect, text="ABC", look="shaded")

    components_data.append(_key_button(
        "Key Switch To Sym L0", layer_rect, sym_face, sym_face, 2, 3,
        handle="Data", remaps=[("Data", LAYER_HANDLE)], gates=layer_gate(0),
    ))
    components_data.append(_key_button(
        "Key Switch To Sym L1", layer_rect, sym_face, sym_face, 2, 3,
        handle="Data", remaps=[("Data", LAYER_HANDLE)], gates=layer_gate(1),
    ))
    components_data.append(_key_button(
        "Key Switch To ABC L2", layer_rect, abc_face, abc_face, 0, 3,
        handle="Data", remaps=[("Data", LAYER_HANDLE)], gates=layer_gate(2),
    ))

    # 4.2 CLEAR button (KEY_CLEAR = 97)
    clear_rect = (x_clear, r4_y, w_clear, KEY_HEIGHT)
    clear_face = store_key_face(skins_dir, clear_rect, text="CLEAR", look="shaded")
    components_data.append(_key_button(
        "Key Clear", clear_rect, clear_face, clear_face, KEY_CLEAR,
        NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)],
    ))

    # 4.3 SPACE bar (KEY_SPACE = 1)
    space_rect = (x_space, r4_y, w_space, KEY_HEIGHT)
    space_face = store_key_face(skins_dir, space_rect, text="SPACE", look="shaded")
    components_data.append(_key_button(
        "Key Space", space_rect, space_face, space_face, KEY_SPACE,
        NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)],
    ))

    # 4.4 CANCEL button (KEY_CANCEL = 99)
    cancel_rect = (x_cancel, r4_y, w_cancel, KEY_HEIGHT)
    cancel_face = store_key_face(skins_dir, cancel_rect, text="CANCEL", look="shaded")
    components_data.append(_key_button(
        "Key Cancel", cancel_rect, cancel_face, cancel_face, KEY_CANCEL,
        NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)],
    ))

    # 4.5 OK button (KEY_OK = 98) with cyan accent
    ok_rect = (x_ok, r4_y, w_ok, KEY_HEIGHT)
    ok_face = store_key_face(skins_dir, ok_rect, text="OK", look="active", colour=_style_color("radio.active.text"))
    components_data.append(_key_button(
        "Key OK", ok_rect, ok_face, ok_face, KEY_OK,
        NUM_KEY_BUTTONS, handle="Data", remaps=[("Data", key_handle)],
    ))

    # An inline modal component: the instance covers the whole tab and consumes the touches the keys do not take; the tab shows it while "TextEdit Context" is in range (the instance bounds).
    background = {"version": 1, "colour": "c0000000", "image": ""}
    return {
        "version": 2,
        "actions": [],
        "backgroundData": {
            "version": 1,
            "focussed": background.copy(),
            "unfocussed": background.copy(),
        },
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": components_data,
    }


def render_sample(sample=None):
    """Render a standalone preview of the keyboard card for documentation and catalog."""
    if Image is None:
        raise RuntimeError("Pillow is required to render keyboard preview")

    width, height = PANEL_WIDTH, PANEL_HEIGHT
    card = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pen = ImageDraw.Draw(card)

    # 1. Panel Box
    pen.rounded_rectangle(
        (0, 0, width - 1, height - 1),
        radius=12,
        fill=_style_color("surface.panel"),
        outline=_style_color("control.border"),
        width=1,
    )

    # 2. Title
    font_title = semibold(14)
    if font_title:
        pen.text((40, 24), "NAME MACRO 1", font=font_title, fill=_style_color("text.caption"), anchor="ls")

    # 3. Close (X) icon at top right
    close_art = action_art(icon="close", width=32, height=32, look="ghost", colour=_style_color("text.muted"))
    card.alpha_composite(close_art, (width - 40 - 32, 12))

    # 4. Text Field Frame & Text
    field_w = KEYS_WIDTH
    field_x = 40
    field_y = 48
    pen.rounded_rectangle(
        (field_x, field_y, field_x + field_w - 1, field_y + 44 - 1),
        radius=6,
        fill=_style_color("field.surface"),
        outline=_style_color("field.border"),
        width=1,
    )
    font_text = semibold(18)
    if font_text:
        pen.text((field_x + 16, field_y + 26), "Filter Cutoff |", font=font_text, fill=_style_color("text.value"), anchor="ls")

    # 5. Row 1 keys
    r1_keys = ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"]
    for i, ch in enumerate(r1_keys):
        kx = field_x + i * 88
        ky = 100
        art = action_art(text=ch, width=80, height=KEY_HEIGHT, look="shaded")
        card.alpha_composite(art, (kx, ky))

    # 6. Row 2 keys
    r2_keys = ["a", "s", "d", "f", "g", "h", "j", "k", "l"]
    for i, ch in enumerate(r2_keys):
        kx = field_x + 44 + i * 88
        ky = 148
        art = action_art(text=ch, width=80, height=KEY_HEIGHT, look="shaded")
        card.alpha_composite(art, (kx, ky))

    # 7. Row 3 keys
    ky = 196
    shift_art = action_art(text="SHIFT", width=124, height=KEY_HEIGHT, look="shaded")
    card.alpha_composite(shift_art, (field_x, ky))
    r3_keys = ["z", "x", "c", "v", "b", "n", "m"]
    for i, ch in enumerate(r3_keys):
        kx = field_x + 124 + KEY_GAP + i * 88
        art = action_art(text=ch, width=80, height=KEY_HEIGHT, look="shaded")
        card.alpha_composite(art, (kx, ky))
    bksp_art = action_art(text="BKSP", width=124, height=KEY_HEIGHT, look="shaded")
    card.alpha_composite(bksp_art, (field_x + field_w - 124, ky))

    # 8. Row 4 keys
    ky = 244
    sym_art = action_art(text="?123", width=120, height=KEY_HEIGHT, look="shaded")
    card.alpha_composite(sym_art, (field_x, ky))
    clear_art = action_art(text="CLEAR", width=100, height=KEY_HEIGHT, look="shaded")
    card.alpha_composite(clear_art, (field_x + 128, ky))
    space_art = action_art(text="SPACE", width=360, height=KEY_HEIGHT, look="shaded")
    card.alpha_composite(space_art, (field_x + 236, ky))
    cancel_art = action_art(text="CANCEL", width=120, height=KEY_HEIGHT, look="shaded")
    card.alpha_composite(cancel_art, (field_x + 604, ky))
    ok_art = action_art(text="OK", width=140, height=KEY_HEIGHT, look="active", colour=_style_color("radio.active.text"))
    card.alpha_composite(ok_art, (field_x + 732, ky))

    return card


def catalog_sections(entry=None):
    """Return catalogue sections for HTML catalog generator."""
    return [
        {
            "heading": "Native On-Screen Keyboard (40 px keys, 8 px gaps)",
            "images": [
                {
                    "presentation": "sample",
                    "file": "keyboard-preview.png",
                    "alt": "Native keyboard overlay, 40 px keys",
                    "caption": "Native on-screen touch keyboard · 40 px keys, 8 px gaps · QWERTY layout",
                }
            ],
        }
    ]
