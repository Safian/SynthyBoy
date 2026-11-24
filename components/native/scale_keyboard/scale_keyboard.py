# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""One octave of native radio keys with transposed scale-membership indicators."""
from pathlib import Path
from shutil import copyfile

from PIL import Image

from components import style
from components.native.button.button import entry
from components.native.name_value.name_value import _label_component
from components.native.radio_button.radio_button import segmented_radio_entry
from components.native.selector.selector import passive_layer

NOTES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
# Semitone intervals in the order of master.scale.type.
SCALES = (
    range(12), (0, 2, 4, 5, 7, 9, 11), (0, 2, 3, 5, 7, 8, 10), (0, 2, 3, 5, 7, 8, 11),
    (0, 2, 3, 5, 7, 9, 11), (0, 2, 3, 5, 7, 9, 10), (0, 1, 3, 5, 7, 8, 10),
    (0, 2, 4, 6, 7, 9, 11), (0, 2, 4, 5, 7, 9, 10), (0, 1, 3, 5, 6, 8, 10),
    (0, 2, 4, 7, 9), (0, 3, 5, 7, 10), (0, 3, 5, 6, 7, 10), (0, 2, 4, 6, 8, 10),
    (0, 1, 3, 4, 6, 7, 9, 10), (0, 2, 3, 5, 6, 8, 9, 11), (0, 1, 4, 5, 7, 8, 10),
    (0, 2, 3, 6, 7, 8, 11), (0, 1, 4, 5, 7, 8, 11), (0, 2, 3, 7, 8),
)


def scale_keyboard(directory, size, key_handle, scale_handle):
    def metric(name):
        return style.size("master.scale_keyboard." + name)

    width, height = size
    pad, caption = metric("padding"), metric("caption_height")
    available = width - 2 * pad
    top = pad + caption
    white_h = height - top - pad
    notes = []
    for i, pitch in enumerate((0, 2, 4, 5, 7, 9, 11)):
        left, right = round(i * available / 7), round((i + 1) * available / 7)
        notes.append((pitch, (pad + left, top, right - left, white_h)))
    black_w = round(available / 7 * metric("black_width"))
    for pitch, centre in zip((1, 3, 6, 8, 10), metric("black_centres")):
        left = pad + round(centre * available / 7) - black_w // 2
        notes.append((pitch, (left, top, black_w, round(white_h * metric("black_height")))))

    mark_w, mark_h = metric("mark_size")
    for semitone in range(12):
        filename = f"scale_interval_{semitone}.png"
        copyfile(Path(__file__).with_name("img") / filename, Path(directory) / filename)

    children = []
    captions = [("ALL 12 NOTES", [f"IndexedEnabling/0/{len(SCALES)}/{scale_handle}"])]
    for root, note in enumerate(NOTES):
        captions.append((f"SCALE ON {note}", [f"NonZero/{scale_handle}", f"IndexedEnabling/{root}/12/{key_handle}"]))
    for text, gates in captions:
        label = _label_component(text, "Text", "", (pad, 0, width - 2 * pad, caption),
                                 style.size("font.caption"), style.argb("text.caption"), "left verticallyCentred")
        label["componentData"]["data"]["text"] = text
        label["bounds"].update(version=2, additionalInvalidatingHandles=gates)
        children.append(label)
    for pitch, rect in notes:
        x, y, w, h = rect
        key = segmented_radio_entry(NOTES[pitch], NOTES[pitch], rect, pitch, 12, key_handle, directory)
        key["componentData"]["data"]["gestureBehaviour"] = "Hold"
        children.append(key)
        mark = (x + (w - mark_w) // 2, y + h - pad - mark_h, mark_w, mark_h)
        for root in range(12):
            degree = (pitch - root) % 12
            data = {"version": 2, "imageType": "FilmStrip", "colour": "0", "numFrames": len(SCALES),
                    "image": f"scale_interval_{degree}.png", "handleName": "Data"}
            children.append(entry(f"{NOTES[pitch]} interval {degree}", "Image", data, mark,
                                  (("Data", scale_handle),), (f"IndexedEnabling/{root}/12/{key_handle}",), focus="No"))
    return passive_layer(children)


if __name__ == "__main__":
    art = Path(__file__).with_name("img")
    art.mkdir(exist_ok=True)
    mark_w, mark_h = style.size("master.scale_keyboard.mark_size")
    for semitone in range(12):
        strip = Image.new("RGBA", (mark_w, mark_h * len(SCALES)))
        for frame, intervals in enumerate(SCALES):
            if semitone in intervals:
                strip.paste(style.color("accent.cyan"), (0, frame * mark_h, mark_w, (frame + 1) * mark_h))
        strip.save(art / f"scale_interval_{semitone}.png")
