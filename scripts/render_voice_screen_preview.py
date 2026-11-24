#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Actual generated Voice TUI + current C++ VoiceUi/scene_cpu offline fixtures.

No hand-painted dials/graph, old PNG crops, verification badges or device claims.
"""
from __future__ import annotations
from screen_images import screen_path, save_screen, require_external_output
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw
from render_osc_screen_preview import OscSkinSketch, ensure_generated_skin, _find_core_build_dir

ROOT = Path(__file__).resolve().parents[1]


class VoiceSkinSketch(OscSkinSketch):
    def __init__(self, *args):
        super().__init__(*args)
        self.unsupported = set()
        # The fixture is the unobscured Voice page: explicitly bind its marker
        # frame rather than accidentally relying on unknown-handle default0.
        tab=next(t for t in self._sketch.tui['pageData']['tabs'] if t['tabName']=='Voice')
        marker=next(c for c in tab['componentDefinition']['componentsData'] if c['componentData']['name'].startswith('Overlay Instance'))
        handle=next(b['value'] for b in marker['handle remapping']['map'] if b['key']=='Data')
        self._state['virtual'][handle]={'norm':0.,'display':'0'}

    def _resolve_handle(self, handle, remaps):
        caption, seen = None, set()
        while handle and handle not in seen:
            seen.add(handle)
            if handle.startswith('WithName[') and ']/' in handle:
                prefix, handle = handle.split(']/', 1)
                caption = prefix[len('WithName['):]
            if handle.startswith('FromGesture/'):
                handle = handle[len('FromGesture/'):]
                continue
            if handle in remaps:
                handle = remaps[handle]
                continue
            break
        return caption, handle

    def _draw_entry(self, canvas, entry, origin, parent_size, remaps):
        model = entry['componentData']
        merged = dict(remaps)
        merged.update({p['key']:p['value'] for p in entry.get('handle remapping', {}).get('map', [])})
        if not self._passes_gates(entry.get('bounds', {}).get('additionalInvalidatingHandles', []), merged):
            return
        data = model.get('data', {})
        if model['type'] == 'Button' and data.get('numButtonsInGroup',1)>1:
            _, handle = self._resolve_handle(data['handleName'], merged)
            selected = round(self._norm_for_handle(handle)*(data['numButtonsInGroup']-1)) == data['buttonId']
            image = self._open_image(data['onImage'] if selected else data['offImage'])
            x,y,w,h = map(int, entry['bounds']['bounds'].split())
            canvas.alpha_composite(image.resize((w,h),Image.Resampling.LANCZOS),(origin[0]+x,origin[1]+y))
            return
        if model['type'] == 'Knob' and data.get('knobType') != 'FilmStrip':
            # Generic host knobs have no portable artwork in this repo. Never fake them.
            self.unsupported.add(data.get('knobType', 'unknown'))
        return super()._draw_entry(canvas, entry, origin, parent_size, remaps)


def build_fixture(work, generation):
    build = Path(os.environ['VOICE_CORE_BUILD']) if os.environ.get('VOICE_CORE_BUILD') else _find_core_build_dir()
    binary = work/f'voice-fixture-{generation}'
    libs = [build/'core/libsynthyboy_core.a', *sorted((build/'core').glob('*/*.a'))]
    command = ['c++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror',f'-DSYNTHYBOY_MPC_UI_GEN={generation}',
               '-I',str(ROOT),'-I',str(ROOT/'core/include'),'-I',str(ROOT/'core/filters-serum/include'),'-I',str(ROOT/'core/shapes/include'),
               str(ROOT/'shell-mpc/overlay/tests/voice_render_fixture.cpp'),*map(str,libs),'-lpthread','-o',str(binary)]
    subprocess.run(command,check=True)
    return binary


def render_all(out, work, allow_unsupported=False):
    skins = ensure_generated_skin(work)
    require_external_output(out)
    out.mkdir(parents=True,exist_ok=True)
    manifest = {'synthetic_fixture':True,'device_capture':False,'tui_bytes':(skins/'TUI.json').stat().st_size,'png_bytes':sum(p.stat().st_size for p in skins.glob('*.png')),'states':{}}
    for gen in (1,2):
        binary = build_fixture(work,gen)
        for rate,active in ((0,1),(1,1),(0,0)):
            name = f'voice-{ "rate" if rate else "percent"}-{ "on" if active else "off"}-gen{gen}'
            raw,state = work/(name+'.rgba'),out/(name+'.json')
            subprocess.run([str(binary),str(rate),str(active),str(raw),str(state)],check=True)
            values = json.loads(state.read_text())
            sketch = VoiceSkinSketch(skins,values)
            host = sketch.render_tab('Voice').convert('RGBA')
            if sketch.unsupported and not allow_unsupported:
                raise ValueError('Native widgets lack portable artwork: '+repr(sketch.unsupported))
            overlay = Image.frombytes('RGBA',(1280,628),raw.read_bytes())
            composite = Image.alpha_composite(host,overlay).convert('RGB')
            composite.save(out/(name+'.png'),optimize=True)
            if gen == 2 and not rate and active:
                host.save(out/'native-host.png',optimize=True)
                overlay.save(out/'live-overlay.png',optimize=True)
                breakdown = Image.new('RGB',(1280,628*3))
                for i,layer in enumerate((host,overlay,composite)):
                    breakdown.paste(layer.convert('RGB'),(0,i*628))
                breakdown.save(out/'layers.png',optimize=True)
            manifest['states'][name] = {'unsupported_native':sorted(sketch.unsupported)}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return out/'voice-percent-on-gen2.png'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out-dir',type=Path,default=None)
    parser.add_argument('--work-dir',type=Path)
    parser.add_argument('--allow-unsupported',action='store_true',help='Diagnostic baseline only; manifest flags missing artwork')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='voice-render-') as folder:
        work = args.work_dir or Path(folder); work.mkdir(parents=True,exist_ok=True)
        out = args.out_dir if args.out_dir is not None else Path(folder)/'states'
        require_external_output(out)
        image = render_all(out,work,args.allow_unsupported)
        if args.out_dir is None and not args.allow_unsupported:
            with Image.open(image) as frame:
                save_screen(frame, screen_path('voice'))
    print('Voice screenshot:', args.out_dir if args.out_dir is not None else screen_path('voice'))

if __name__ == '__main__':
    main()
