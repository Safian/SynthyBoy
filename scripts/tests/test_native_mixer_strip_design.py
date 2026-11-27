# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""MixerStrip design and HSlider catalogue entries: dimensions, row spacing, thumb position, centre tick, stereo meter, disabled look."""
import json
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.hslider import hslider
from components.native.mixer_strip import mixer_strip as strip_design

STRIP_DIR = ROOT / "components/native/mixer_strip"
SLIDER_DIR = ROOT / "components/native/hslider"
STRIPS = {e["id"]: e for e in strip_design.strip_examples(json.loads((STRIP_DIR / "catalog.json").read_text(encoding="utf-8"))["mixer_strip"])}
SLIDERS = {e["id"]: e for e in hslider.hslider_examples(json.loads((SLIDER_DIR / "catalog.json").read_text(encoding="utf-8"))["hslider"])}


def pixels(image, rgb, tolerance=0):
    """The (x, y) of every opaque pixel of this colour (within `tolerance` per channel)."""
    data = image.convert("RGBA")
    return [(x, y) for y in range(data.height) for x in range(data.width)
            if data.getpixel((x, y))[3] == 255 and all(abs(a - b) <= tolerance for a, b in zip(data.getpixel((x, y))[:3], rgb))]


class MixerStripDesignTests(unittest.TestCase):
    def test_pictures_equal_the_recipes_and_have_the_strip_size(self):
        m = strip_design.metrics()
        self.assertEqual((m["width"], m["height"]), (160, 606))
        for id_, example in STRIPS.items():
            with Image.open(STRIP_DIR / "img" / example["file"]) as image:
                self.assertEqual(image.size, (160, 606), id_)
                self.assertEqual(image.convert("RGBA").tobytes(), strip_design.render_strip(example).tobytes(), id_)

    def test_rows_are_one_gap_apart_with_the_padding_around(self):
        m = strip_design.metrics()
        for id_, example in STRIPS.items():
            r = strip_design.layout(example)
            self.assertEqual(r["power"][1], m["pad"], id_)                              # POWER row starts at the padding
            self.assertEqual(r["power"][3], 32, id_)
            name_h = r["name"][3]
            self.assertEqual(r["name"][1], m["pad"] + (m["row"] - name_h) // 2, id_)       # the name is centred on the POWER row
            self.assertLess(r["name"][0] + r["name"][2], r["power"][0], id_)               # the name ends before POWER
            below = r["solo"] if "solo" in r else r["destination"]
            self.assertEqual(below[1] - (r["power"][1] + r["power"][3]), m["gap"], id_)   # one gap between the name row and the next
            last = r["send2"] if "send2" in r else r["readout"]
            self.assertEqual(m["height"] - (last[1] + last[3]), m["pad"], id_)             # the padding under the last row
            self.assertEqual(r["send0"][1] - (r["readout"][1] + r["readout"][3]), m["gap"], id_) if "send0" in r else None

    def test_solo_mute_on_every_channel_strip_but_not_master(self):
        m = strip_design.metrics()
        for id_, example in STRIPS.items():
            r = strip_design.layout(example)
            if id_ == "master":
                self.assertNotIn("solo", r)
                self.assertNotIn("mute", r)
                self.assertEqual(r["destination"][1], m["pad"] + m["row"] + m["gap"])       # no empty row between name and dropdown
                self.assertFalse([k for k in r if k.startswith("send")])
            else:
                self.assertEqual(r["solo"][3], 32)
                self.assertEqual(r["solo"][2], (m["inner"] - m["gap"]) // 2)               # half the inner width minus the gap
                self.assertEqual(r["mute"][0] - (r["solo"][0] + r["solo"][2]), m["gap"] + (m["inner"] - m["gap"]) % 2)
                self.assertEqual(r["destination"][1] - (r["solo"][1] + 32), m["gap"])

    def test_stereo_meter_is_two_bars(self):
        meter = strip_design.layout(STRIPS["master"])["meter"]
        picture = Image.open(STRIP_DIR / "img" / STRIPS["master"]["file"]).convert("RGBA")
        lit = pixels(picture.crop((meter[0], meter[1], meter[0] + meter[2], meter[1] + meter[3])), style.rgb("meter.peak"))
        columns = sorted({x for x, _ in lit})
        runs = 1 + sum(1 for a, b in zip(columns, columns[1:]) if b != a + 1)
        self.assertEqual(runs, 2)                                                    # two bars: L and R
        self.assertEqual(len(columns), 2 * style.size("meter.stereo.channel_width"))

    def test_thumb_follows_the_level_and_is_the_contrasty_one(self):
        for id_ in ("noise-knobs", "master"):
            example = STRIPS[id_]
            r = strip_design.layout(example)
            picture = Image.open(STRIP_DIR / "img" / example["file"]).convert("RGBA")
            fill = pixels(picture, style.rgb("fader.thumb.fill"))
            ys = [y for _, y in fill]
            self.assertAlmostEqual((min(ys) + max(ys)) / 2, r["thumb"][1] + r["thumb"][3] / 2, delta=1.5, msg=id_)

    def test_disabled_strip_differs_and_keeps_name_and_power(self):
        on, off = strip_design.render_strip(STRIPS["noise-sliders"]), strip_design.render_strip(STRIPS["noise-off"])
        r = strip_design.layout(STRIPS["noise-off"])
        self.assertNotEqual(on.tobytes(), off.tobytes())
        x, y, w, h = r["name"]
        self.assertEqual(on.crop((x, y, x + w, y + h)).tobytes(), off.crop((x, y, x + w, y + h)).tobytes())   # the name is not dimmed
        self.assertFalse(pixels(off, style.rgb("fader.thumb.fill")))                                           # thumb has no live colour
        self.assertFalse(pixels(off, style.rgb("accent.green")))                                               # POWER is unlit

    def test_solo_and_mute_active_differ_from_the_off_buttons(self):
        r = strip_design.layout(STRIPS["noise-solo-mute"])
        base = strip_design.render_strip(STRIPS["noise-sliders"])
        active = strip_design.render_strip(STRIPS["noise-solo-mute"])
        for key in ("solo", "mute"):
            x, y, w, h = r[key]
            self.assertNotEqual(base.crop((x, y, x + w, y + h)).tobytes(), active.crop((x, y, x + w, y + h)).tobytes(), key)
        self.assertTrue(pixels(active, style.rgb("accent.cyan"), 40))
        self.assertTrue(pixels(active, style.rgb("accent.red"), 40))

    def test_both_bottom_rows_have_three_rows(self):
        knobs, sliders = strip_design.layout(STRIPS["noise-knobs"]), strip_design.layout(STRIPS["noise-sliders"])
        self.assertEqual(knobs["send0"][3], 56)
        self.assertEqual(sliders["send0"][3], style.size("hslider.height"))
        for r in (knobs, sliders):
            self.assertEqual({k for k in r if k.startswith("send")}, {"send0", "send1", "send2"})
        self.assertGreater(sliders["track"][3], knobs["track"][3])                  # the shorter bottom row gives the fader the room


class HSliderTests(unittest.TestCase):
    def test_pictures_equal_the_recipe(self):
        for id_, example in SLIDERS.items():
            with Image.open(SLIDER_DIR / "img" / example["file"]) as image:
                self.assertEqual(image.size, (style.size("hslider.width"), style.size("hslider.height")), id_)
                self.assertEqual(image.convert("RGBA").tobytes(), hslider.render_example(example).tobytes(), id_)

    def test_thumb_position_equals_the_value(self):
        width = style.size("hslider.width")
        for id_ in ("pan-centre", "pan-left-50", "send-60"):
            example = SLIDERS[id_]
            image = Image.open(SLIDER_DIR / "img" / example["file"]).convert("RGBA")
            xs = [x for x, _ in pixels(image, style.rgb("fader.thumb.fill"))]
            self.assertAlmostEqual((min(xs) + max(xs) + 1) / 2, hslider.thumb_centre_x(width, example["value"]), delta=1.0, msg=id_)
        left, right = hslider.geometry(width)["travel"]
        self.assertEqual((hslider.thumb_centre_x(width, 0), hslider.thumb_centre_x(width, 1)), (left, right))

    def test_bipolar_has_a_centre_tick_at_half_travel_and_unipolar_none(self):
        width = style.size("hslider.width")
        g = hslider.geometry(width)
        tick = style.rgb("text.muted")
        left50 = Image.open(SLIDER_DIR / "img" / SLIDERS["pan-left-50"]["file"]).convert("RGBA")   # the thumb is away: the tick is free
        xs = sorted({x for x, _ in pixels(left50, tick)})
        self.assertAlmostEqual(sum(xs) / len(xs) + 0.5, g["centre_x"], delta=1.0)
        self.assertEqual(g["centre_x"], width / 2)                                    # 50 % of the travel
        self.assertFalse(pixels(Image.open(SLIDER_DIR / "img" / SLIDERS["send-60"]["file"]).convert("RGBA"), tick))

    def test_disabled_differs(self):
        enabled = Image.open(SLIDER_DIR / "img" / SLIDERS["send-60"]["file"]).convert("RGBA")
        disabled = Image.open(SLIDER_DIR / "img" / SLIDERS["send-disabled"]["file"]).convert("RGBA")
        self.assertNotEqual(enabled.tobytes(), disabled.tobytes())
        self.assertFalse(pixels(disabled, style.rgb("fader.thumb.fill")))


if __name__ == "__main__":
    unittest.main()
