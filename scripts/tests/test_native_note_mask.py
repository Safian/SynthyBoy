# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""NoteMask component: piano layout, the Button faces, the catalogue pictures equal the renderer, the skin definition (twelve Buttons bound to Note0 .. Note11)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.note_mask import note_mask

NATIVE = ROOT / "components/native"
SAMPLES = note_mask.mask_samples(json.loads((NATIVE / "note_mask/catalog.json").read_text(encoding="utf-8"))["note_mask"])
BY_ID = {s["id"]: s for s in SAMPLES}


def stored(file):
    with Image.open(NATIVE / "note_mask/img" / file) as image:
        return image.convert("RGBA")


def is_accent(pixel):
    return all(abs(a - b) <= 30 for a, b in zip(pixel[:3], style.rgb("accent.cyan")))


class NoteMaskTests(unittest.TestCase):
    def test_images_equal_renderer_output(self):
        for sample in SAMPLES:
            image = stored(sample["file"])
            self.assertEqual(image.tobytes(), note_mask.render_sample(sample).convert("RGBA").tobytes(), sample["id"])
            self.assertEqual(image.size, note_mask.sample_size(sample), sample["id"])

    def test_piano_layout_whites_below_blacks_above_on_the_borders(self):
        for width in (420, 474, 490):
            cells = note_mask.layout(width)
            row, gap = style.size("note_mask")["row_h"], style.size("note_mask")["gap"]
            self.assertEqual(note_mask.height(), 2 * row + gap)
            self.assertEqual(len(cells), 12)
            for note, (x, y, w, h) in enumerate(cells):
                self.assertEqual(h, row)
                self.assertGreaterEqual(w, 56)   # the touch target of a cell is at least 56 px
                self.assertEqual(y, 0 if note_mask.is_black(note) else row + gap, note)
            whites = [c for n, c in enumerate(cells) if not note_mask.is_black(n)]
            self.assertEqual(whites[0][0], 0)
            self.assertEqual(whites[-1][0] + whites[-1][2], width)
            for left, right in zip(whites, whites[1:]):
                self.assertEqual(left[0] + left[2], right[0])   # the whites tile the width without a gap
            for note in (1, 3, 6, 8, 10):   # a black key is centred on the border of the two whites around it
                border = cells[note - 1][0] + cells[note - 1][2]
                x, _, w, _ = cells[note]
                self.assertLessEqual(abs(x + w // 2 - border), 1, note)
            self.assertTrue(all(cells[a][0] + cells[a][2] <= cells[a + 2][0] for a in (1, 6, 8)))   # neighbouring blacks do not overlap

    def test_exactly_the_notes_of_the_mask_are_accent(self):
        for sample in (s for s in SAMPLES if s["state"] == "enabled"):
            image = stored(sample["file"])
            lit = [n for n, (x, y, w, h) in enumerate(note_mask.layout(sample["width"]))
                   if is_accent(image.getpixel((x + w // 2, y + 4)))]
            self.assertEqual(lit, [n for n in range(12) if sample["mask"] >> n & 1], sample["id"])

    def test_unlit_cell_is_the_button_legacy_face_and_lit_the_active_face(self):
        from components.native.button.button import action_art, in_slot
        sample, inset = BY_ID["c-major"], style.size("note_mask")["inset"]
        x, y, w, h = note_mask.layout(sample["width"])[1]   # C#, not in the set
        face = in_slot(action_art(text="C#", width=w - 2 * inset, height=h, look="legacy"), (w, h))
        self.assertEqual(stored(sample["file"]).crop((x, y, x + w, y + h)).tobytes(), face.tobytes())
        x, y, w, h = note_mask.layout(sample["width"])[0]   # C, in the set
        lit = in_slot(action_art(text="C", width=w - 2 * inset, height=h, look="active", colour=style.color("radio.active.text")), (w, h))
        self.assertEqual(stored(sample["file"]).crop((x, y, x + w, y + h)).tobytes(), lit.tobytes())

    def test_disabled_differs_and_has_no_accent(self):
        sample = BY_ID["disabled"]
        enabled = note_mask.render_sample(dict(sample, state="enabled"))
        disabled = stored(sample["file"])
        self.assertNotEqual(enabled.tobytes(), disabled.tobytes())
        x, y, w, h = note_mask.layout(sample["width"])[0]
        self.assertFalse(is_accent(disabled.getpixel((x + w // 2, y + 4))))

    def test_definition_has_twelve_buttons_bound_to_the_note_handles(self):
        with tempfile.TemporaryDirectory() as tmp:
            definition = note_mask.build_definition(tmp, 474)
            buttons = definition["componentsData"]
            self.assertEqual(len(buttons), 12)
            for note, (button, rect) in enumerate(zip(buttons, note_mask.layout(474))):
                self.assertEqual(button["componentData"]["type"], "Button")
                self.assertEqual(button["handle remapping"]["map"], [{"key": "Data", "value": f"Note{note}"}])
                self.assertEqual(button["bounds"]["bounds"], " ".join(map(str, rect)))
                data = button["componentData"]["data"]
                self.assertEqual((data["buttonId"], data["numButtonsInGroup"]), (1, 1))   # a single toggle button: lit when the handle is 1
                self.assertNotEqual(data["onImage"], data["offImage"])
                with Image.open(Path(tmp) / data["onImage"]) as picture:
                    self.assertEqual(picture.size, rect[2:])
                self.assertTrue((Path(tmp) / data["offImage"]).is_file())

    def test_invalid_samples_are_refused(self):
        base = BY_ID["c-major"]
        for bad in (dict(base, mask=4096), dict(base, mask=-1), dict(base, width=419), dict(base, state="hidden"), dict(base, extra=1)):
            with self.assertRaises(ValueError):
                note_mask.mask_samples({"samples": [bad]})
        with self.assertRaises(ValueError):
            note_mask.mask_samples({"samples": [base, base]})


if __name__ == "__main__":
    unittest.main()
