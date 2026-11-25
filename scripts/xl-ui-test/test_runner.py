"""Offline checks of the XL suite's evidence, layout and result accounting."""
import json
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

from device import XL, Unavailable, parse_dump
from inventory import absolute_rect, inventory, locate
from run import Runner, validate_cases


class DeviceFixture:
    def __init__(self):
        self.value = 0
        self.view = "play1"

    def params(self):
        return {"osc1.on": self.value, "unchanged": 2}

    def status(self):
        return {"view": self.view, "ui_depth": "0" if self.view == "play1" else "1", "state": "visible"}

    def gesture(self, *args):
        self.value ^= 1

    def image(self):
        return Image.new("RGB", (1280, 800))

    def remote(self, command):
        assert command == "pidof MPC"
        return b"7"


class RunnerTest(unittest.TestCase):
    def test_sub_proportional_dial_geometry_matches_native_pixel_coordinates(self):
        bounds = {"boundsType":"Proportional", "bounds":"0.16 0.03 0.68 0.6"}
        self.assertEqual(absolute_rect(bounds,(14,604,216,128)), (48,607,146,76))

    def test_dump_requires_complete_state_and_exact_parameter_manifest(self):
        payload = b"p 0 osc1.on 1\nv 0 0.8\nchunk 3\nabc\n"
        self.assertEqual(parse_dump(payload, ["osc1.on"]), {"engine": {"osc1.on": 1}, "vst": {0: .8}})
        for bad, keys in [(payload[:-2], ["osc1.on"]), (payload, ["osc1.on", "osc2.on"]),
                          (payload, ["osc2.on"]), (payload.replace(b"v 0", b"v 2"), ["osc1.on"])]:
            with self.assertRaises(Unavailable):
                parse_dump(bad, keys)

    def test_vst_expectation_requests_extended_dump(self):
        from unittest.mock import Mock
        self.dev.snapshot = Mock(return_value={"vst": {2944: .5}})
        result = {}
        self.runner.expect({"vst": {"2944": .5}}, {}, result)
        self.dev.snapshot.assert_called_once_with(vst=True)
        self.assertEqual(result["vst"], {"2944": .5})

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name)
        self.dev = DeviceFixture()
        self.control = {"view": "osca", "id": "osca/PANEL/Power", "type": "Button", "data": {},
                        "rect": (8, 128, 32, 24), "handles": {"handleName": "Parameter 2"}, "maps": {}, "actions": []}
        self.runner = Runner(types.SimpleNamespace(out=self.out, capture_overlay=False), {}, [self.control], self.dev)
        self.runner.pid = "7"
        def enter(view):
            self.dev.view = view
            return self.dev.status()
        self.runner.enter = enter

    def test_only_passes_a_witnessed_action_and_restore(self):
        case = {"id": "power", "view": "osca", "steps": [
            {"op": "snapshot", "name": "baseline"}, {"op": "tap", "control": "osca/PANEL/Power"},
            {"op": "expect", "target": "osc1.on", "change": "only"}], "restore": [
            {"op": "tap", "control": "osca/PANEL/Power", "if_changed": {"target": "osc1.on"}},
            {"op": "expect", "target": "*", "change": "restored"}]}
        self.runner.run_case(case)
        self.assertEqual(self.runner.results[-1]["status"], "PASS")
        self.assertEqual(self.runner.passed_controls, {"osca/PANEL/Power"})
        self.assertEqual(self.dev.params()["osc1.on"], 0)
        self.assertEqual(self.dev.status()["view"], "play1")
        self.assertTrue((self.out / "power.after.png").exists())

    def test_unwitnessed_input_does_not_count_in_coverage(self):
        self.runner.run_case({"id": "unverified", "view": "osca", "steps": [{"op": "tap", "control": "Power"}],
                              "restore": [{"op": "tap", "control": "Power"}]})
        self.assertEqual(self.runner.results[-1]["status"], "NOT_RUN")
        self.assertFalse(self.runner.passed_controls)

    def test_failure_does_not_invert_an_already_restored_value(self):
        self.runner.run_case({"id": "fail", "view": "osca", "steps": [
            {"op": "snapshot", "name": "baseline"}, {"op": "expect", "target": "osc1.on", "value": 1}],
            "restore": [{"op": "tap", "control": "Power", "if_changed": {"target": "osc1.on"}}]})
        self.assertEqual(self.runner.results[-1]["status"], "FAIL")
        self.assertEqual(self.dev.value, 0)
        self.assertTrue(self.runner.results[-1]["restore"][0]["already_restored"])

    def test_native_empty_body_is_a_failure_even_with_ocr_in_header(self):
        with patch("run.recognise", return_value=[{"text": "Osc", "conf": 99}]):
            with self.assertRaisesRegex(AssertionError, "empty oscillator editor body"):
                self.runner.quick_case("osca")

    def test_bounds_and_ambiguity(self):
        relative = {"boundsType": "Relative", "relativeBounds": "4, 24, parent.width - 4, parent.height - 24"}
        self.assertEqual(absolute_rect(relative, (100, 200, 100, 120)), (104, 224, 92, 72))
        with self.assertRaisesRegex(ValueError, "matches 2 controls"):
            locate([self.control, self.control], "osca", "Power")
        self.assertTrue(validate_cases([{"id": "bad", "view": "osca", "steps": [{"op": "tap", "control": "Missing"}]}],
                                       {"osca": {}}, [self.control]))

    def test_drm_premultiplied_composite(self):
        primary = Image.new("RGB", (2, 1), (20, 40, 80))
        overlay = Image.new("RGBA", (2, 1), (0, 0, 0, 0))
        overlay.putpixel((1, 0), (100, 0, 0, 128))
        self.assertEqual(list(XL.compose(primary, overlay).getdata()), [(20, 40, 80), (110, 20, 40)])


if __name__ == "__main__":
    unittest.main()
