import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

import numpy as np
from PIL import Image

from control_arrival import check_discrete_art, measure, stable_arrival
from inventory import VIEW_IDS, inventory, resolve


class ArrivalTest(unittest.TestCase):
    def test_three_complete_matching_samples_at_end(self):
        times = [0.2, 0.4, 0.6, 0.8, 1.0, 1.2]
        self.assertIsNone(stable_arrival(times, [0, 0, 0, 0, 1, 1]))
        self.assertIsNone(stable_arrival(times, [1, 1, 1, 0, 1, 1]))
        self.assertEqual(stable_arrival(times, [1, 1, 0, .9, .95, 1]), .8)

    def test_each_sdk_repeat_resolves_its_gated_parameter(self):
        self.assertEqual(resolve('NonZero/Parameter {Increment:Base=50,Step=4,Offset=-2}', {}, 3),
                         'NonZero/Parameter 60')
        self.assertEqual(resolve('Parameter {Increment:Base=3,Step=-1}', {}, 5), 'Parameter 0')

    def test_repeated_leaves_have_distinct_rects_handles_and_gates(self):
        bounds = dict(boundsType='Absolute', bounds='5 2 12 8',
                      additionalInvalidatingHandles=['NonZero/Parameter {Increment:Base=60}'])
        button = dict(componentData=dict(name='key', type='Button',
                                        data=dict(handleName='Parameter {Increment:Base=4}', onImage='on.png')),
                      bounds=bounds)
        tabs = [dict(tabName=name, fnKeyIndex=i, fnKeySubIndex=0,
                     componentDefinition=dict(version=3, repeats=2, componentsData=[button] if i == 0 else []))
                for i, name in enumerate(VIEW_IDS)]
        tui = dict(pageData=dict(tabs=tabs, componentDefinitions=dict(localComponentDefinitions=[])))
        with TemporaryDirectory() as temp:
            path = Path(temp) / 'TUI.json'
            path.write_text(json.dumps(tui))
            _, rows = inventory(path, visual_leaves=True)
        self.assertEqual([r['rect'] for r in rows], [(5, 112, 12, 8), (17, 112, 12, 8)])
        self.assertEqual([r['handles']['handleName'] for r in rows], ['Parameter 4', 'Parameter 5'])
        self.assertEqual([(r['definition'], r['local_leaf']) for r in rows], [('tab:play1', (0, 'key'))] * 2)
        self.assertEqual([r['gates'][0] for r in rows], ['NonZero/Parameter 60', 'NonZero/Parameter 61'])

    def test_wrong_indicator_on_a_shared_background(self):
        sheet = Image.new('RGBA', (56, 112), (18, 20, 30, 255))
        sheet.paste((220, 100, 40, 255), (24, 80, 30, 86))
        # More than 98% of the button is the same in the two states.
        screen = Image.new('RGB', (1280, 800))
        cell = (10, 140, 56, 56)
        controls = [dict(id='mixer/FX', type='Image', filmstrip=('button.png', 2),
                         handles={'handleName': 'Parameter 1'})]
        with TemporaryDirectory() as folder:
            sheet.save(Path(folder) / 'button.png')
            for index in range(2):
                screen.paste(sheet.crop((0, index*56, 56, (index+1)*56)), cell[:2])
                row = dict(id='mixer/FX', rect=cell, result='matched', seconds=.4)
                check_discrete_art([row], controls, {1: 1}, Path(folder), screen)
                self.assertEqual(row['art']['closest'], [index])
                self.assertEqual(row['result'], 'matched' if index else 'wrong_settled_state')
                self.assertEqual(row['seconds'], .4 if index else None)

    def test_visibility_distinction_and_unchanged_readout(self):
        base = np.zeros((400, 640, 3), dtype=np.uint8)
        final = base.copy()
        final[60:64, 10:17] = 160
        final[70:74, 10:17] = base[70:74, 10:17] = 100
        frames = [base, final, final, final]
        def node(name, top, gates=()):
            return dict(id=name, rect=[12, top, 28, 12], type='Label', visual_leaf=True, gates=gates)
        controls = [node('visible', 118), node('unchanged', 138),
                    node('hidden', 118, ['NonZero/Parameter 1']), node('unknown', 118, ['NonZero/Parameter 2'])]
        result = {r['id']: r for r in measure(base, final, frames, [.1, .2, .3, .4], controls, {1: 0})}
        self.assertEqual(result['visible']['seconds'], .2)
        self.assertEqual(result['unchanged']['result'], 'continuous_match')
        self.assertNotIn('hidden', result)
        self.assertEqual(result['unknown']['result'], 'unknown_gate')


if __name__ == '__main__':
    unittest.main()
