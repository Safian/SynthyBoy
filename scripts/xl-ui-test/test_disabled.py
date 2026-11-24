"""Offline disabled control oracles. Hardware reports only pass after both real gestures."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image, ImageDraw

from device import Unavailable
from disabled import observe, read_value, verify_art


class DisabledTest(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.out = Path(folder.name)
        self.screen = Image.new("RGB", (1280, 800))
        ImageDraw.Draw(self.screen).text((26, 120), "55", fill="white")
        self.row = {"rect": (20, 110, 70, 40), "values": [{"rect": (20, 110, 70, 40),
                    "handle": "Parameter 1", "gates": []}], "art": []}
        self.before = {"engine": {"gain": 55}, "vst": {1: .55}, "display": {1: "55"}}
        self.status = lambda: {"view": "osca", "state": "visible"}
        self.gestures = []
        self.gesture = lambda *coords: self.gestures.append(coords)
        self.image = lambda: self.screen.copy()
        self.snapshot = lambda: self.before

    def exercise(self):
        record = {}
        with patch("disabled.gate_state", return_value=True), patch("disabled.recognise", return_value=[{"text":"55"}]):
            observe(self, self.row, self.before, self.out, record, self.out/"frame.png")
        return record

    def test_both_gestures_and_readout(self):
        self.assertEqual([o["action"] for o in self.exercise()["observations"]], ["tap", "drag"])
        self.assertEqual(len(self.gestures), 2)

    def test_empty_native_body(self):
        self.screen = Image.new("RGB", self.screen.size)
        with self.assertRaisesRegex(AssertionError, "empty native"):
            self.exercise()
        self.assertFalse(self.gestures)

    def test_missing_value_is_not_a_pass(self):
        self.row["values"] = []
        with self.assertRaises(Unavailable):
            self.exercise()

    def test_changed_host_or_engine_fails(self):
        self.snapshot = lambda: {"engine": {"gain":55}, "vst":{1: .6}}
        with self.assertRaisesRegex(AssertionError, "changed plugin state"):
            self.exercise()

    def test_frame_uses_normalized_value_and_detects_wrong_art(self):
        strip = Image.new("RGB", (20, 40), "red")
        strip.paste("blue", (0,20,20,40))
        strip.save(self.out/"dial.png")
        art = {"asset":"dial.png", "rect":(20,120,20,20), "frames":2, "handle":"Parameter 1"}
        self.screen.paste("blue", (20,120,40,140))
        self.assertEqual(verify_art(self.screen, art, self.out, {1:1}), 1)
        with self.assertRaises(AssertionError):
            verify_art(self.screen, art, self.out, {1:0})

    def test_wrong_native_number_fails(self):
        with patch("disabled.recognise", return_value=[{"text":"54"}]), self.assertRaises(AssertionError):
            read_value(self.screen, self.row["rect"], "55")
