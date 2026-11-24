# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Voice sequence bindings over the existing Pan/Level native knob families."""
import json
from pathlib import Path

_LAYOUT=json.loads(Path(__file__).with_name('voice_layout.json').read_text())['steps']
VOICE_STEP_ROWS=('PAN','DETUNE','CUTOFF','ENV','MOD 1','MOD 2')
VOICE_STEP_ROW_KEYS=('pan','detune','cutoff','env','mod1','mod2')
VOICE_STEP_KNOB_PREFIX='Voice Native Step Knob '
VOICE_STEP_KNOB_NAMES=tuple(f'{VOICE_STEP_KNOB_PREFIX}{row} {step}' for row in VOICE_STEP_ROWS for step in range(1,9))


def voice_step_knob_rect(step):
    if not 1<=step<=8: raise ValueError('Voice step must be1..8')
    x,y,w,h=_LAYOUT['rect'];pad,gap=_LAYOUT['pad'],_LAYOUT['gap']
    col=(w-2*pad-7*gap)//8;diameter=_LAYOUT['diameter']
    return (x+pad+(step-1)*(col+gap)+(col-diameter)//2,y+pad+_LAYOUT['top'],diameter,diameter)


def build_voice_step_knobs(ctx,root_gate,row_handle):
    """48small references, only8visible; exactly2shared native knob definitions."""
    entries=[]
    for row,(label,key) in enumerate(zip(VOICE_STEP_ROWS,VOICE_STEP_ROW_KEYS)):
        for step in range(1,9):
            model=f'Parameter {ctx.param_index(f"voicectl.s{step}.{key}")}'
            entries.append({'version':2,'componentData':{'version':1,'name':f'{VOICE_STEP_KNOB_PREFIX}{label} {step}',
                            'type':'voiceNativeStepPan' if row==0 else 'voiceNativeStepLevel','data':{'version':1,'handleName':'Data'}},
                            'handle remapping':{'version':1,'map':[{'key':'Data','value':model}]},
                            'bounds':{'version':2,'acceptsHWFocus':'Yes','showWhenDataModelInvalid':'Hide','whenVisible':'Always',
                                      'boundsType':'Absolute','bounds':' '.join(map(str,voice_step_knob_rect(step))),
                                      'additionalInvalidatingHandles':[root_gate,f'IndexedEnabling/{row}/6/{row_handle}']}})
    return entries
