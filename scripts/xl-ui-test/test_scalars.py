import unittest
from unittest.mock import Mock, patch

from scalars import exercise, expected_key, source_on, native


class ScalarsTest(unittest.TestCase):
    def test_named_source_and_beat_proxies(self):
        key = lambda path, engine, context={}: expected_key({"id": path}, engine, context)
        self.assertEqual(key("oscc/Osc Native Surface/Osc Native Settings/Osc C/osc1.frame [2]/knob",
                             {"osc3.frame":0}), "osc3.frame")
        self.assertEqual(key("oscnoise/Osc Native Settings/noise.filter [2]/knob", {"noise.filter":0}), "noise.filter")
        self.assertEqual(key("play1/Play Env Decay/knob", {"env3.units":1,"env3.decay_beats":0},
                             {"play_env":"3"}), "env3.decay_beats")

    def test_published_editors_select_their_actual_instance(self):
        engine = {"env5.units":1, "env5.hold_beats":0, "lfo8.swing":.5, "mod40.amount":.1, "filter2.resonance":1}
        base = native.native_base_index(len(engine))
        values = {base+native.ENV_SELECTION:.8, base+native.LFO_OFFSET:7/13,
                  base+native.MOD_ROWS+2:40/64, base+native.FILTER_STATE_OFFSET:.5}
        for path, key in (("env1/Env Native Hold/knob","env5.hold_beats"),
                          ("lfo1/LFO 1 Native 1-3.17.0/knob","lfo8.swing"),
                          ("mod/Mod Native Scroll Row 3/AMOUNT/amount","mod40.amount"),
                          ("filter1/Filter Native Split Reso 0/knob","filter2.resonance")):
            self.assertEqual(expected_key({"id":path}, engine, {}, values),key)

    def test_unexpected_destination_fails_known_proxy(self):
        dev = Mock()
        row = {"id":"oscnoise/Osc Native Settings/noise.stereo/knob", "type":"Knob",
               "handles":{"handleName":"Parameter 30"}, "rect":(0,210,90,90)}
        state = ({}, {"noise.stereo":0,"noise.phase":0}, {30:0})
        after = ({}, {"noise.stereo":0,"noise.phase":90}, {30:1})
        with patch("scalars.snapshot",return_value=after):
            self.assertEqual(exercise(dev,row,state,{})[0]["status"], "FAIL")

    def test_power_restored_even_after_scalar_error(self):
        dev = Mock()
        dev.status.return_value = {"view":"oscnoise"}
        widget = {"view":"oscnoise","id":"oscnoise/Osc Native Surface/Osc Native Power/Osc Native Noise Power",
                  "type":"Button","rect":(0,120,64,64),"gates":[]}
        off = ({}, {"noise.on":0,"noise.pan":0}, {})
        on = ({}, {"noise.on":1,"noise.pan":0}, {})
        changed = ({}, {"noise.on":1,"noise.pan":1}, {})
        restored = ({}, {"noise.on":0,"noise.pan":1}, {})
        with patch("scalars.snapshot",side_effect=[on,changed,restored]), self.assertRaisesRegex(ValueError,"test"):
            with source_on(dev,"oscnoise",[widget],off,lambda _: None):
                raise ValueError("test")
        self.assertEqual(dev.gesture.call_count,2)
