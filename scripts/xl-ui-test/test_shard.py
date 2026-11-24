import argparse
import unittest
from unittest.mock import Mock, patch

from PIL import Image

from device import Key61, Unavailable, XL, dismiss_fx_menu, navigate
from inventory import VIEW_IDS
from shard import MODELS, add_shard, selected


class ShardTest(unittest.TestCase):
    def shard(self, args):
        parser = argparse.ArgumentParser()
        add_shard(parser)
        return selected(parser.parse_args(args), parser)

    def test_disjoint_defaults_cover_all_views(self):
        xl = self.shard(["--device","xl"])
        key = self.shard(["--device","key61"])
        self.assertEqual(key, ["oscnoise"])
        self.assertFalse(set(xl) & set(key))
        self.assertEqual(set(xl + key), set(VIEW_IDS.values()))
        self.assertIs(MODELS["xl"].adapter, XL)
        self.assertIs(MODELS["key61"].adapter, Key61)
        self.assertNotEqual(MODELS["xl"].elf_machine, MODELS["key61"].elf_machine)

    def test_selection_and_exclusion(self):
        self.assertEqual(self.shard(["--view","fx","--view","mixer","--exclude-view","fx"]), ["mixer"])

    def test_key61_waits_for_transient_view_and_confirms_target(self):
        device = Mock(spec=Key61)
        device.footer_confirm, device.footer_settle = .35, 2
        views = iter(["osca", "osca", "osca", "oscnoise", "oscnoise", "oscnoise"])
        device.status.side_effect = lambda: {"state":"visible", "view":next(views)}
        with patch("device.footer_labels", return_value={1:320}), patch("device.time.sleep") as sleep:
            state = navigate(device, {"oscnoise":{"group":1}}, "oscnoise")
        self.assertEqual(state["view"], "oscnoise")
        device.gesture.assert_called_once_with(320, 770)
        sleep.assert_any_call(2)

    def test_modal_recognition_rejects_unknown_header(self):
        device = Mock()
        image = Image.new("RGB", (1280,800))
        with patch("device.footer_labels", return_value={}), patch("device.recognise", return_value=[{"text":"OTHER"}]):
            self.assertFalse(dismiss_fx_menu(device, image))
        device.gesture.assert_not_called()

    def test_modal_close_requires_editor_footer(self):
        device = Mock()
        image = Image.new("RGB", (1280,800))
        words = [{"text":s} for s in ("FX","ADD","EFFECT")]
        with patch("device.footer_labels", return_value={}), patch("device.recognise", return_value=words), patch("device.time.sleep"):
            with self.assertRaises(Unavailable):
                dismiss_fx_menu(device, image)
        device.gesture.assert_called_once_with(106, 49)
