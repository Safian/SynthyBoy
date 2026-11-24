#!/usr/bin/env python3
# SynthyBoy - packaged Mod matrix and route editor contract.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import itertools
import json
import tempfile
import unittest
from pathlib import Path

import test_mpc_skin_extras as skin


VIEW, ROW_BASE, FIELD_BASE = 2888, 2889, 2896
MODE, SOURCE_GROUP, TARGET_GROUP, ADD, ROW_ACTIONS = range(2944, 2949)


def intersects(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah


def enabled(widget, view, rows, mode=1):
    frames = {VIEW: view, MODE: mode, **{ROW_BASE + i: slot for i, slot in enumerate(rows)}}
    for gate in widget['bounds']['additionalInvalidatingHandles']:
        kind, lo, *rest = gate.split('/')
        hi, count, parameter = (lo, *rest) if kind == 'IndexedEnabling' else rest
        value = frames[int(parameter.split()[1])]
        if not 0 <= value < int(count):
            raise ValueError(gate)
        if not int(lo) <= value <= int(hi):
            return False
    return True


@unittest.skipUnless((skin.SDK / 'tools' / 'gen_ui.py').is_file(), 'MPC SDK unavailable')
class TestNativeMod(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory() as tmp:
            skins = skin.generate_skins(Path(tmp), skin.ROOT / 'scripts/overlay-skin/mpc-ui-pages-overlay.json')
            result = skin.run_apply(skins, skin.REAL_EXTRAS)
            if result != (0, ''):
                raise AssertionError(result)
            page = json.loads((skins / 'TUI.json').read_text(encoding='utf-8'))['pageData']
            cls.mod = next(tab for tab in page['tabs'] if tab['tabName'] == 'Mod')
            cls.definitions = {d['key']: d['value'] for d in page['componentDefinitions']['localComponentDefinitions']}
            cls.strings = {int(item['key'].split()[1]): item['value'] for item in page['replacementStrings']
                           if item['key'].startswith('Parameter ')}
        cls.contract = json.loads((skin.REAL_EXTRAS / 'mod-native-contract.json').read_text(encoding='utf-8'))
        cls.widgets = {}
        for widget in cls.mod['componentDefinition']['componentsData']:
            name = widget['componentData']['name']
            if not name.startswith('Mod Native '):
                continue
            name = name.removeprefix('Mod Native ')
            kind = widget['componentData']['type']
            if not kind.startswith('modNativeRow'):
                cls.widgets[name] = widget
                continue
            bindings = {h['key']: h['value'] for h in widget['handle remapping']['map']}
            px, py, _, _ = cls.rect(widget)
            for original in cls.definitions[kind]['componentsData']:
                child = copy.deepcopy(original)
                x, y, w, h = cls.rect(child)
                child['bounds']['bounds'] = f'{px + x} {py + y} {w} {h}'
                child['bounds']['additionalInvalidatingHandles'] = widget['bounds']['additionalInvalidatingHandles']
                for remap in child['handle remapping']['map']:
                    prefix, slash, handle = remap['value'].rpartition('/')
                    remap['value'] = prefix + slash + bindings[handle]
                cls.widgets[name + ' ' + child['componentData']['name']] = child

    @staticmethod
    def rect(widget):
        return tuple(int(v) for v in widget['bounds']['bounds'].split())

    @staticmethod
    def parameter(widget):
        return int(next(m['value'] for m in widget['handle remapping']['map'] if m['key'] == 'Data').split()[1])

    def test_both_widths_keep_all_six_row_cells_and_scrollbar_exclusive(self):
        scalar_names = ('AMOUNT', 'POLARITY', 'ROUTE')
        action_names = ('SOURCE', 'TARGET', 'MORE')
        for frame, name in enumerate(('normal', 'scroll'), 1):
            geom = self.contract[name]
            for row in range(7):
                cells = []
                y_offset = geom['rowStep'] * row
                for names, rects, base in ((scalar_names, geom['first'], FIELD_BASE),
                                            (action_names, geom['navigation'], ROW_ACTIONS)):
                    for field, (field_name, (x, y, w, h)) in enumerate(zip(names, rects)):
                        prefix = name.title() + ' '
                        widget = self.widgets[f'{prefix}Row {row + 1} {field_name}']
                        rect = self.rect(widget)
                        self.assertEqual(rect, (x, y + y_offset, w, h))
                        self.assertEqual(self.parameter(widget), base + row * 3 + field)
                        self.assertTrue(enabled(widget, frame, [7, 11, 19, 24, 30, 44, 64]))
                        self.assertFalse(enabled(widget, 3 - frame, [7] * 7))
                        self.assertFalse(enabled(widget, frame, [0] * 7))
                        if 'zone' in geom:
                            self.assertFalse(intersects(rect, geom['zone']))
                        cells.append(rect)
                for a, b in itertools.combinations(cells, 2):
                    self.assertFalse(intersects(a, b))

    def test_header_remains_native_when_no_routes_match(self):
        names = ('Mode ALL', 'Mode BY SOURCE', 'Mode BY DEST', 'ADD', 'Source Group', 'Target Group')
        for i, (name, expected) in enumerate(zip(names, (MODE, MODE, MODE, ADD, SOURCE_GROUP, TARGET_GROUP))):
            widget = self.widgets[name]
            self.assertEqual(list(self.rect(widget)), self.contract['header'][i])
            self.assertEqual(self.parameter(widget), expected)
            for frame in (1, 2, 3):
                self.assertEqual(enabled(widget, 0, [0] * 7, frame), i < 4 or frame == i - 2)
            self.assertFalse(enabled(widget, 0, [0] * 7, 0))
        for i, name in enumerate(names[:3], 1):
            definition = self.definitions[self.widgets[name]['componentData']['type']]
            button = definition['componentsData'][0]
            self.assertEqual(button['componentData']['type'], 'Button')
            self.assertEqual((button['componentData']['data']['buttonId'],
                              button['componentData']['data']['numButtonsInGroup']), (i, 4))
        self.assertEqual(self.strings[MODE], ['', 'ALL', 'BY SOURCE', 'BY DEST'])
        self.assertEqual(self.strings[SOURCE_GROUP], ['ALL SOURCES', 'ENVELOPES', 'LFOS', 'MACROS', 'PLAYER', 'SOUND'])
        self.assertEqual(self.strings[TARGET_GROUP], ['ALL MODULES', 'OSC A', 'OSC B', 'OSC C', 'SUB / NOISE',
                                                      'FILTER 1', 'FILTER 2', 'ENVELOPES', 'LFOS', 'FX', 'MASTER'])

    def test_detail_delete_and_confirm_strings(self):
        delete_param = self.parameter(self.widgets['Detail Delete'])
        confirm_param = delete_param + 4
        self.assertEqual(self.strings.get(delete_param), ['DELETE', 'CONFIRM?'])
        self.assertEqual(self.strings.get(confirm_param), ['', 'CONFIRM?'])

    def test_detail_delete_native_confirmation_wiring_and_contract(self):
        # 1. Geometry from contract
        delete_rect = tuple(self.contract["detail_navigation"]["delete"])
        self.assertEqual(delete_rect, (1120, 4, 120, 56))

        # 2. Generated TUI instance handle mapping
        items = self.mod["componentDefinition"]["componentsData"]
        delete_widget = next(w for w in items if w["componentData"]["name"] == "Mod Native Detail Delete")
        self.assertEqual(self.rect(delete_widget), delete_rect)
        remap = {m["key"]: m["value"] for m in delete_widget["handle remapping"]["map"]}
        delete_param = self.parameter(delete_widget)
        confirm_param = delete_param + 4
        self.assertEqual(remap["Data"], f"Parameter {delete_param}")
        self.assertEqual(remap["Confirm"], f"Parameter {confirm_param}")

        # 3. Transparent parent: no label touch steal, ignoreMousePresses, no actions
        definition = self.definitions["modNative_delete_120x56"]
        self.assertTrue(definition.get("ignoreMousePresses", False))
        self.assertEqual(definition.get("actions", []), [])
        unfocussed_bg = definition.get("backgroundData", {}).get("unfocussed", {})
        focussed_bg = definition.get("backgroundData", {}).get("focussed", {})
        self.assertEqual(unfocussed_bg.get("colour"), "00000000")
        self.assertEqual(unfocussed_bg.get("image"), "")
        self.assertEqual(focussed_bg.get("colour"), "00000000")
        self.assertEqual(focussed_bg.get("image"), "")

        # 4. Two disjoint native buttons with separate DELETE / CONFIRM? faces and gates
        children = definition["componentsData"]
        self.assertEqual(len(children), 2)
        child_unarmed, child_confirm = children[0], children[1]

        # Both are native Buttons
        self.assertEqual(child_unarmed["componentData"]["type"], "Button")
        self.assertEqual(child_confirm["componentData"]["type"], "Button")
        self.assertEqual(child_unarmed["componentData"]["name"], "Delete unarmed")
        self.assertEqual(child_confirm["componentData"]["name"], "Delete confirm")

        # Bounds fit slot exactly
        self.assertEqual(self.rect(child_unarmed), (0, 0, delete_rect[2], delete_rect[3]))
        self.assertEqual(self.rect(child_confirm), (0, 0, delete_rect[2], delete_rect[3]))

        # Art faces
        self.assertEqual(child_unarmed["componentData"]["data"]["onImage"], "mod_detail_delete.png")
        self.assertEqual(child_unarmed["componentData"]["data"]["offImage"], "mod_detail_delete.png")
        self.assertEqual(child_confirm["componentData"]["data"]["onImage"], "mod_detail_confirm.png")
        self.assertEqual(child_confirm["componentData"]["data"]["offImage"], "mod_detail_confirm.png")

        # Gating: disjoint IndexedEnabling 0/2/Confirm vs 1/2/Confirm
        gate_unarmed = child_unarmed["bounds"]["additionalInvalidatingHandles"]
        gate_confirm = child_confirm["bounds"]["additionalInvalidatingHandles"]
        self.assertEqual(gate_unarmed, ["IndexedEnabling/0/2/Confirm"])
        self.assertEqual(gate_confirm, ["IndexedEnabling/1/2/Confirm"])

        # 5. Same actionData writes original DeleteData; independent readonly Confirm
        self.assertEqual(child_unarmed["handle remapping"]["map"], [{"key": "Data", "value": "Data"}])
        self.assertEqual(child_confirm["handle remapping"]["map"], [{"key": "Data", "value": "Data"}])

    def test_hidden_and_detail_visibility(self):
        self.assertEqual(len(self.widgets), 108)
        for frame, mode, expected in ((0, 0, 0), (0, 1, 4), (0, 2, 5), (1, 1, 16), (2, 3, 17), (3, 0, 17), (4, 0, 17)):
            with self.subTest(frame=frame, mode=mode):
                rows = [7, 64, 0, 0, 0, 0, 0]
                visible = {name: w for name, w in self.widgets.items() if enabled(w, frame, rows, mode)}
                self.assertEqual(len(visible), expected)
                if frame >= 3:
                    self.assertTrue(all(name.startswith('Detail ') for name in visible))
                    self.assertIn('Detail ' + ('DELAY' if frame == 3 else 'BEATS'), visible)
                for a, b in itertools.combinations(visible.values(), 2):
                    self.assertFalse(intersects(self.rect(a), self.rect(b)))
        self.assertEqual(self.parameter(self.widgets['Detail DELAY']), self.parameter(self.widgets['Detail BEATS']))
        self.assertTrue(all(self.parameter(w) >= FIELD_BASE + 21 for n, w in self.widgets.items() if n.startswith('Detail ')))

    def test_native_definitions_fit_their_cells_and_toggle_one_value(self):
        for name, w in self.widgets.items():
            with self.subTest(widget=name):
                x, y, width, height = self.rect(w)
                self.assertTrue(x >= 0 and y >= 0 and x + width <= 1280 and y + height <= 628)
                definition = self.definitions[w['componentData']['type']]
                for child in definition['componentsData']:
                    cx, cy, cw, ch = self.rect(child)
                    self.assertTrue(cx >= 0 and cy >= 0 and cx + cw <= width and cy + ch <= height)
                if name.endswith(' Group'):
                    actions = {(a['onAction'], a['handler'], a['handleName'], a['additionalData']) for a in definition['actions']}
                    self.assertEqual(actions, {(trigger, 'Show Overlay', 'Data', 'fx editor choice menu')
                                               for trigger in ('Mouse Down', 'Enter Pressed')})
                elif definition['actions']:
                    self.assertEqual({(a['onAction'], a['handler'], a['handleName']) for a in definition['actions']},
                                     {('Mouse Down', 'Toggle Switch', 'Data'), ('Enter Pressed', 'Toggle Switch', 'Data')})
                if ' Row ' in name and name.endswith((' SOURCE', ' TARGET')):
                    self.assertEqual(skin.leaf_model(definition['componentsData'][0], self.definitions)['data']['type'], 'Value')
                for marker in self.mod['componentDefinition']['componentsData'][-3:]:
                    self.assertFalse(intersects(self.rect(w), self.rect(marker)), name)
        self.assertEqual(self.strings[self.parameter(self.widgets['Normal Row 1 POLARITY'])], ['UNI', 'BI'])
        self.assertEqual(self.strings[self.parameter(self.widgets['Normal Row 1 ROUTE'])], ['ON', 'OFF'])
        items = self.mod['componentDefinition']['componentsData']
        rows = [w for w in items if w['componentData']['type'].startswith('modNativeRow')]
        self.assertEqual(len(rows), 14)
        self.assertTrue(all(w['bounds']['acceptsHWFocus'] == 'Children' for w in rows))
        self.assertEqual(len(items) - len(rows), len(self.widgets) - 6 * len(rows) + 3 + 1 + 2)
        self.assertTrue(all(w['componentData']['name'].startswith('Overlay ') for w in items[-3:]))


if __name__ == '__main__':
    unittest.main()
