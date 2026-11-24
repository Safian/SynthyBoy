#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Render actual generated native ENV TUI + current EnvUi/scene_cpu fixture.

Synthetic offline evidence, never a device capture. Alternate states stay external.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
from screen_images import screen_path, save_screen, require_external_output
from render_osc_screen_preview import OscSkinSketch, ensure_generated_skin, _find_core_build_dir
from render_voice_screen_preview import VoiceSkinSketch

ROOT=Path(__file__).resolve().parents[1]

class EnvSkinSketch(VoiceSkinSketch):
    def __init__(self,skins,values):
        OscSkinSketch.__init__(self,skins,values)
        self.unsupported=set()

    def _draw_entry(self,canvas,entry,origin,parent_size,remaps):
        model=entry['componentData'];data=model.get('data',{})
        merged=dict(remaps)
        merged.update({m['key']:m['value'] for m in entry.get('handle remapping',{}).get('map',[])})
        if not self._passes_gates(entry.get('bounds',{}).get('additionalInvalidatingHandles',[]),merged):return
        if model['type']=='Image' and data.get('imageType')=='FilmStrip':
            image=self._open_image(data['image'])
            _,handle=self._resolve_handle(data['handleName'],merged)
            count=data['numFrames'];frame=round(self._norm_for_handle(handle)*(count-1))
            height=image.height//count
            image=image.crop((0,height*frame,image.width,height*(frame+1)))
            x,y,w,h=map(int,entry['bounds']['bounds'].split())
            canvas.alpha_composite(image.resize((w,h),Image.Resampling.LANCZOS),(origin[0]+x,origin[1]+y))
            return
        return super()._draw_entry(canvas,entry,origin,parent_size,remaps)


def render_states(out,work):
    require_external_output(out);out.mkdir(parents=True,exist_ok=True)
    # A persistent diagnostic work directory must never silently reuse stale source output.
    (work/"skins/TUI.json").unlink(missing_ok=True)
    skins=ensure_generated_skin(work)
    build=Path(os.environ['ENV_CORE_BUILD']) if os.environ.get('ENV_CORE_BUILD') else _find_core_build_dir()
    libs=[build/'core/libsynthyboy_core.a',*sorted((build/'core').glob('*/*.a'))]
    for generation in (1,2):
        binary=work/f'env-fixture-{generation}'
        subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror',f'-DSYNTHYBOY_MPC_UI_GEN={generation}',
                        '-I',str(ROOT),'-I',str(ROOT/'core/include'),'-I',str(ROOT/'core/filters-serum/include'),'-I',str(ROOT/'core/shapes/include'),
                        str(ROOT/'shell-mpc/overlay/tests/env_render_fixture.cpp'),*map(str,libs),'-lpthread','-o',str(binary)],check=True)
        for beats in (0,1):
            name=f'env-{"beats" if beats else "ms"}-gen{generation}'
            raw,state=work/(name+'.rgba'),out/(name+'.json')
            subprocess.run([str(binary),str(beats),str(raw),str(state)],check=True)
            sketch=EnvSkinSketch(skins,json.loads(state.read_text()))
            host=sketch.render_tab('Env').convert('RGBA')
            if sketch.unsupported:raise ValueError('Unsupported native widgets: '+repr(sketch.unsupported))
            overlay=Image.frombytes('RGBA',(1280,628),raw.read_bytes())
            Image.alpha_composite(host,overlay).convert('RGB').save(out/(name+'.png'),optimize=True)
            if generation==2 and not beats:
                host.save(out/'native-host.png');overlay.save(out/'live-overlay.png')
    return out/'env-ms-gen2.png'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out-dir',type=Path)
    parser.add_argument('--work-dir',type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='env-render-') as folder:
        work=args.work_dir or Path(folder);work.mkdir(parents=True,exist_ok=True)
        out=args.out_dir or Path(folder)/'states'
        image=render_states(out,work)
        if args.out_dir is None:save_screen(Image.open(image),screen_path('env'))
    print('ENV screenshot:',args.out_dir or screen_path('env'))

if __name__=='__main__':main()
