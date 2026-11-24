#!/usr/bin/env python3
"""Focused safety checks for the local skin probe (no hardware writes)."""
# Copyright (C) 2026 Safian. SPDX-License-Identifier: AGPL-3.0-or-later
import copy
import hashlib
import json
import unittest

from probe_factory_knob_blue import ASSET, TARGETS, patch_bytes, prepare


class FactoryBlueProbeTests(unittest.TestCase):
    def setUp(self):
        self.base = {'pageData': {'componentDefinitions': {'localComponentDefinitions': [
            {'key': key, 'value': {'actions': ['preserve'], 'componentsData': [
                {'componentData': {'name': 'knob', 'type': 'Knob', 'data': {
                    'knobType': 'FilmStrip', 'filmStrip': image, 'numFrames': 127,
                    'handleName': 'Data', 'invert': False}},
                 'bounds': {'bounds': '2 4 40 40'}, 'handle remapping': {'map': []}},
                {'componentData': {'type': 'Label', 'data': {'handleName': 'Caption'}}},
                {'componentData': {'type': 'Label', 'data': {'handleName': 'Data'}}}]}}
            for key, image in TARGETS.items()]}, 'tabs': [{'gates': ['unchanged']}]}}

    def run_probe(self, base=None, **kwargs):
        raw = json.dumps(self.base if base is None else base).encode()
        return prepare(raw, hashlib.sha256(raw).hexdigest(), **kwargs)

    def test_only_two_exact_fields_change(self):
        result, manifest = self.run_probe()
        self.assertEqual(len(manifest['changes']), 2)
        definitions = result['pageData']['componentDefinitions']['localComponentDefinitions']
        for definition in definitions:
            data = definition['value']['componentsData'][0]['componentData']['data']
            self.assertEqual(data['filmStrip'], ASSET)
            data['filmStrip'] = TARGETS[definition['key']]
        self.assertEqual(result, self.base)

    def test_raw_patch_preserves_duplicate_keys_and_unrelated_bytes(self):
        raw = json.dumps(self.base, indent=3).encode().replace(
            b'"pageData": {', b'"version": 1, "version": 2, "pageData": {', 1)
        patched, _ = patch_bytes(raw, hashlib.sha256(raw).hexdigest())
        restored = patched
        for previous in TARGETS.values():
            restored = restored.replace(json.dumps(ASSET).encode(),
                                        json.dumps(previous).encode(), 1)
        self.assertEqual(restored, raw)
        self.assertIn(b'"version": 1, "version": 2', patched)

    def test_other_definition_using_same_art_is_unchanged(self):
        definitions = self.base['pageData']['componentDefinitions']['localComponentDefinitions']
        other = copy.deepcopy(definitions[0])
        other['key'] = 'unrelatedDial'
        definitions.append(other)
        raw = json.dumps(self.base).encode()
        patched, _ = patch_bytes(raw, hashlib.sha256(raw).hexdigest())
        actual = json.loads(patched)['pageData']['componentDefinitions']['localComponentDefinitions']
        self.assertEqual(actual[-1], other)

    def test_missing_selection_fails_without_mutation(self):
        del self.base['pageData']['componentDefinitions']['localComponentDefinitions'][0]
        saved = copy.deepcopy(self.base)
        with self.assertRaises(ValueError):
            self.run_probe()
        self.assertEqual(self.base, saved)

    def test_unsafe_asset_fails(self):
        for asset in ('../../knob.png', '/tmp/knob.png', ASSET + '/../other.png'):
            with self.assertRaises(ValueError):
                self.run_probe(asset=asset)

    def test_changed_base_and_contract_fail(self):
        with self.assertRaises(ValueError):
            prepare(json.dumps(self.base).encode(), '0' * 64)
        self.base['pageData']['componentDefinitions']['localComponentDefinitions'][0][
            'value']['componentsData'][0]['componentData']['data']['invert'] = True
        with self.assertRaises(ValueError):
            self.run_probe()


if __name__ == '__main__':
    unittest.main()
