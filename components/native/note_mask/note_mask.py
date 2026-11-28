# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""NOTE MASK: the twelve notes of an octave as toggle buttons laid out like a piano (a set of notes, any number of them on).

The seven white notes (C D E F G A B) stand in the lower row under their place; the five black ones (C# D# F# G# A#) in the upper row, centred on the border between the two white notes around them. Every cell is the Button
recipe (`button.action_art(text=..., look=...)`): a note that is not in the set = the shaded gradient face (`control.gradient`, `button.gradient.*`), a note that is in the set = look "active" (accent fill, `radio.active.*`).
A cell is `note_mask.row_h` (56) px high; the mask is two rows and `note_mask.gap` (= `header.gap`, 8) px high: 2 x 56 + 8 = 120. A face is `note_mask.inset` px narrower than its hit rectangle on each side, so neighbours do not touch.
Bits: note 0 = C .. note 11 = B. Disabled: `style.dim_artwork`. In the skin the mask is a local definition of twelve Buttons bound to the handles `Note0` .. `Note11` (`build_definition`); the owner of the page writes the mask
parameter from the gestures (shell-mpc/overlay/osc_ui.h publishNativeNoteMask). Docs: docs/ui-components/note_mask.md.
"""
from components.native.button.button import action_art, button as native_button, in_slot
from components.native.selector.selector import passive_layer
from components.style import color as _style_color, dim_artwork, size as _style_size

try:
    from PIL import Image
except ImportError:
    Image = None

NOTES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
STATES = ("enabled", "disabled")
SAMPLE_KEYS = {"id", "file", "alt", "caption", "mask", "width", "state"}


def is_black(note):
    return note % 12 in (1, 3, 6, 8, 10)


def white_index(note):
    """The place of a white note among the seven (C D E F G A B = 0 .. 6), -1 for a black one."""
    return (0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6)[note % 12]


def height():
    tokens = _style_size("note_mask")
    return 2 * tokens["row_h"] + tokens["gap"]


def layout(width):
    """The hit rectangle (x, y, w, h) of every note in a mask `width` px wide, in note order; the whites share the width in sevenths (integer maths, the last one ends at `width`)."""
    tokens = _style_size("note_mask")
    row, white = tokens["row_h"], width // tokens["whites"]
    cells = []
    for note in range(12):
        index = white_index(note)
        if index >= 0:
            x = index * width // 7
            cells.append((x, height() - row, (index + 1) * width // 7 - x, row))
        else:
            border = (white_index(note - 1) + 1) * width // 7   # between the white note below and the one above
            cells.append((border - white // 2, 0, white, row))
    return cells


def cell_art(note, lit, rect, enabled=True):
    """The picture of one cell (the size of its hit rectangle): the face of the Button recipe, `note_mask.inset` px in from each side."""
    inset = _style_size("note_mask")["inset"]
    _, _, w, h = rect
    face_w = w - 2 * inset
    if lit:
        face = action_art(text=NOTES[note], width=face_w, height=h, look="active", colour=_style_color("radio.active.text"))
    else:
        face = action_art(text=NOTES[note], width=face_w, height=h, look="shaded")
    image = in_slot(face, (w, h))
    return image if enabled else dim_artwork(image)


def render_sample(sample):
    """The whole mask as a picture: `sample["mask"]` bit i set = note i lit."""
    width = sample["width"]
    image = Image.new("RGBA", (width, height()))
    for note, rect in enumerate(layout(width)):
        image.alpha_composite(cell_art(note, bool(sample["mask"] >> note & 1), rect, sample["state"] == "enabled"), rect[:2])
    return image


def sample_size(sample):
    return (sample["width"], height())


def build_definition(skins_dir, width):
    """The local definition of one mask `width` px wide: twelve Buttons, note n bound to the handle `Note<n>` (on / off = in the set / not); the instance remaps the handles. The pictures go to `skins_dir`."""
    from pathlib import Path
    directory = Path(skins_dir)
    cells = []
    for note, rect in enumerate(layout(width)):
        names = []
        for lit in (True, False):
            name = f"note_mask_{NOTES[note].replace('#', 's').lower()}_{'on' if lit else 'off'}_{rect[2]}x{rect[3]}.png"
            cell_art(note, lit, rect).save(directory / name)
            names.append(name)
        cells.append(native_button(f"Note {NOTES[note]}", rect, names[0], names[1], index=1, count=1, remaps=(("Data", f"Note{note}"),)))
    return passive_layer(cells)


def mask_samples(entry):
    """The samples of a catalogue "note_mask" entry, validated; a new example is one more entry."""
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("note_mask entry needs a nonempty samples list")
    for item in entry["samples"]:
        if (set(item) != SAMPLE_KEYS or item["state"] not in STATES or type(item["mask"]) is not int or not 0 <= item["mask"] < 4096
                or type(item["width"]) is not int or item["width"] < 7 * 60):
            raise ValueError(f"note mask sample needs exactly {sorted(SAMPLE_KEYS)}, state in {STATES}, a 12 bit mask and a width of at least 7 x 60 px (the label must fit a face)")
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("note mask sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    samples = mask_samples(entry)
    sections = [{"heading": "Hangkészlet", "images": [
        {"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if i["state"] == "enabled"]}]
    disabled = [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if i["state"] == "disabled"]
    if disabled:
        sections.append({"heading": "Disabled", "images": disabled})
    sections.append({"heading": "Helyigény", "table": {"head": ["Példa", "Méret (px)"], "rows": [
        [i["caption"], "{} × {}".format(*sample_size(i))] for i in samples]}})
    return sections


def write_catalog_images(entry, directory):
    """Write the catalogue pictures of the entry's samples into `directory` (the component's own img folder): the pictures are the renderer's output, nothing else."""
    from pathlib import Path
    for sample in mask_samples(entry):
        render_sample(sample).save(Path(directory) / sample["file"])
