# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone Voice tab migration to host-native MPC controls.

Defines exact geometry, parameter bindings, component generation, and explicit
inventory of converted vs retained dynamic overlay controls.
Geometry strictly matches shell-mpc/ui/voice_screen.h and docs/ui-map/voice.md.
"""
import sys as _token_sys
from pathlib import Path as _TokenPath
_token_root = str(_TokenPath(__file__).resolve().parents[2])
if _token_root not in _token_sys.path:
    _token_sys.path.insert(0, _token_root)
from components import style as _ui_style

from pathlib import Path
import sys

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from .step_controls import (VOICE_STEP_KNOB_NAMES,VOICE_STEP_KNOB_PREFIX,VOICE_STEP_ROW_KEYS,
                            build_voice_step_knobs,voice_step_knob_rect)
from scripts.native_ui_params import VOICE_SEQUENCE, mixer_peek_index

VOICE_TAB_NAME = "Voice"
KEEP = ("Overlay Marker", "Overlay Instance")

# Converted standard host-native controls:
# (Name, Parameter Key, Widget Kind, Type, Rect (x, y, w, h in tab coords), Caption, Block ID)
# Block ID: 0: RANDOM, 1: SCALING, 2: TARGETS, -1: HEADER
import json
import copy
import tempfile
from components import style
from components.native.level.level import compact_dial_definition, level_definition, synthyboy_dial_data
from components.native.pan.pan import synthyboy_dial_data as pan_dial_data
from components.native.active_text.active_text import (
    active_text_definition,
    build_titled_active_text_group_definition,
    render_active_text_art,
    titled_active_text_group_entry,
)
from components.native.name_value.name_value import build_name_value_definition
from components.native.radio_button.radio_button import segmented_radio_entry
from components.native.selector.selector import build_titled_selector, selector_definition

_LAYOUT = json.loads(Path(__file__).with_name("voice_layout.json").read_text())
VOICE_NATIVE_CONTROLS = [dict(c,rect=tuple(c["rect"])) for c in _LAYOUT["controls"]]
VOICE_STANDALONE_CONTROLS = [c for c in VOICE_NATIVE_CONTROLS if c["block"] != 2]
VOICE_TARGET_CONTROLS = [c for c in VOICE_NATIVE_CONTROLS if c["block"] == 2]
VOICE_ROWS = ("PAN", "DETUNE", "CUTOFF", "ENV", "MOD 1", "MOD 2")
VOICE_STEPS = tuple(f"STEP {i}" for i in range(1, 9))
VOICE_SEQUENCE_CONTROLS = tuple(tuple(c[:2]) + (tuple(c[2]),) + tuple(c[3:]) for c in _LAYOUT["sequence"])

_SCOPE = _LAYOUT["scope"]
VOICE_TARGET_SCOPE_TITLE = _SCOPE["title"]
_scope_rect = _SCOPE["rect"]
_target_gx = min([_scope_rect[0]] + [c["rect"][0] for c in VOICE_TARGET_CONTROLS])
_target_gy = min([_scope_rect[1]] + [c["rect"][1] for c in VOICE_TARGET_CONTROLS])
_target_gw = max([_scope_rect[0] + _scope_rect[2]] + [c["rect"][0] + c["rect"][2] for c in VOICE_TARGET_CONTROLS]) - _target_gx
_target_gh = max([_scope_rect[1] + _scope_rect[3]] + [c["rect"][1] + c["rect"][3] for c in VOICE_TARGET_CONTROLS]) - _target_gy
VOICE_TARGET_GROUP_BOUNDS = (_target_gx, _target_gy, _target_gw, _target_gh)
VOICE_TARGET_SCOPE_RECT = (_scope_rect[0] - _target_gx, _scope_rect[1] - _target_gy, _scope_rect[2], _scope_rect[3])

VOICE_TARGET_ITEMS = [
    {
        "name": c["name"].replace("Voice ", ""),
        "label": c["caption"],
        "handle": f"Active{c['key'].split('.')[-1].capitalize()}",
        "key": c["key"],
        "rect": (
            c["rect"][0] - _target_gx,
            c["rect"][1] - _target_gy,
            c["rect"][2],
            c["rect"][3],
        ),
    }
    for c in VOICE_TARGET_CONTROLS
]

VOICE_NATIVE_CONTROL_NAMES = (
    {c["name"] for c in VOICE_STANDALONE_CONTROLS}
    | {entry[0] for entry in VOICE_SEQUENCE_CONTROLS}
    | set(VOICE_STEP_KNOB_NAMES)
    | {"Voice Target Scope"}
)


def sequence_base(ctx):
    return mixer_peek_index(len(ctx.params)) + VOICE_SEQUENCE


# Explicit inventory of unconverted controls retained in overlay:
UNCONVERTED_CONTROLS = [
    {
        "name": "Voice Step Grid",
        "component": "VoiceGrid",
        "rect": tuple(_ui_style.size('screen.voice_tab.rect.rect')),
        "reason": (
            "The eight own native bipolar knobs represent6 modulation "
            "rows (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2) and Steps1-8 using "
            "the existing Pan/Level filmstrip families. The overlay layer retains "
            "the dynamic real-time sounding voice indicator (LiveValues::voiceStep -> voiceStepMask), "
            "selected step/value text and formatted per-step readouts."
        ),
    },
    {
        "name": "Voice Step Readout Strip",
        "component": "stripRect",
        "rect": tuple(_ui_style.size('screen.voice_tab.rect.rect.2')),
        "reason": (
            "Real-time multi-parameter readout strip displaying 'STEP N' and formatted values "
            "for all 6 modulation rows of the currently selected step simultaneously, with "
            "dynamic active-row highlighting."
        ),
    },
]


def _bounds(rect, focus="No", proportional=False):
    return {
        "version": 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Hide" if focus == "Yes" else "Show",
        "whenVisible": "Always",
        "boundsType": "Proportional" if proportional else "Absolute",
        "bounds": " ".join(str(n) for n in rect),
    }


def _component(name, kind, data, bounds, handles=()):
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": [{"key": key, "value": value} for key, value in handles]},
        "bounds": bounds,
    }


def _label(kind, rect, size):
    return _component(kind.lower(), "Label", {
        "version": 1,
        "textStyle": {
            "version": 1,
            "font": {"version": 1, "name": style.size("font.family"), "style": style.size("font.style.semibold"), "height": float(size)},
            "colour": style.argb("text.caption" if kind == "Name" else "text.value"),
            "justification": "horizontallyCentred verticallyCentred",
            "case": "Original",
        },
        "type": kind,
        "handleName": "Caption" if kind == "Name" else "Data",
    }, _bounds(rect))


def _definition(children, action, target="", triggers=("Mouse Down",)):
    background = {"version": 1, "colour": style.native_color("transparent"), "image": ""}
    focus = _component("focus", "Focus", {
        "version": 1, "backgroundColour": style.argb("surface.control"), "outlineColour": style.argb("accent.cyan"),
        "backgroundInset": _ui_style.size('screen.voice_tab.backgroundinset'), "outlineThickness": style.size('voice.controls.focus_outline'),
    }, dict(_bounds((0, 0, 1, 1), proportional=True), whenVisible="WhenFocussed"))
    return {
        "version": 2,
        "actions": [{"version": 1, "onAction": trigger, "handler": action,
                     "handleName": "Data", "additionalData": target} for trigger in triggers],
        "backgroundData": {"version": 1, "focussed": background, "unfocussed": background},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": [focus, *children],
    }


def _dial(skins, width, height, *, pan=False):
    diameter = min(style.size('voice.controls.dial_max'),height-style.size('voice.controls.dial_vertical_reserve'))
    caption_h = style.size('voice.controls.dial_caption_height')
    rects = {"focus": (0,0,width,height), "knob": ((width-diameter)//2,caption_h,diameter,diameter),
             "name": (0,0,width,caption_h), "value": (0,caption_h+diameter,width,height-caption_h-diameter)}
    result = compact_dial_definition(level_definition(skins_dir=skins), rects=rects)
    for child in result["componentsData"]:
        if child["componentData"]["name"] == "knob":
            factory = pan_dial_data if pan else synthyboy_dial_data
            if pan:
                with tempfile.TemporaryDirectory(prefix="voice-pan-") as folder:
                    data = factory(folder,frame_size=diameter,bipolar=False)
                    filename = f"voice_pan_depth_{diameter}.png"
                    (skins/filename).write_bytes((Path(folder)/data["filmStrip"]).read_bytes())
                    data["filmStrip"] = filename
                child["componentData"]["data"] = data
            else:
                child["componentData"]["data"] = factory(skins,frame_size=diameter,bipolar=False)
    result["actions"] = [{"version":1,"onAction":trigger,"handler":"Show Overlay","handleName":"Data","additionalData":"fx editor number menu"} for trigger in ("Double Click","Enter Pressed")]
    for key in ("focussed","unfocussed"):
        state = result["backgroundData"][key]
        state["colour"] = style.native_color("transparent")
    for child in result["componentsData"]:
        data = child["componentData"]["data"]
        if child["componentData"]["type"] == "Label":
            data["textStyle"]["justification"] = "horizontallyCentred verticallyCentred"
            data["textStyle"]["font"]["height"] = style.size("font.caption" if child["componentData"]["name"] == "name" else "font.value")
    return result


def voice_definitions(skins_dir=None):
    # The hooks provide the release asset directory; stand-alone callers may
    # inspect definitions using task-local temporary assets.
    skins = Path(skins_dir) if skins_dir else Path(tempfile.gettempdir()) / "synthyboy-voice-assets"
    skins.mkdir(parents=True,exist_ok=True)
    rows = []
    row_size = style.size('voice.controls.row_size')
    row_origin = style.size('voice.controls.row_origin')
    row_pitch = style.size('voice.controls.row_pitch')
    for i, label in enumerate(VOICE_ROWS):
        rect = (row_origin[0] + (i % 2) * row_pitch[0], row_origin[1] + (i // 2) * row_pitch[1], *row_size)
        rows.append(segmented_radio_entry(f"row {i}", label, rect, i, len(VOICE_ROWS), None, skins))
    definitions = {
        "voiceNativeRowList": _definition([_label("Name",tuple(_ui_style.size('screen.voice_tab.voicenativerowlist.rect')),style.size('voice.controls.row_caption_font')),*rows],"",triggers=()),
        "voiceNativeStepSelect": selector_definition(None,"fx editor choice menu",tuple(_ui_style.size('screen.voice_tab.voicenativestepselect.size')),skins),
        "voiceNativeStepValue": build_name_value_definition(rect=tuple(_ui_style.size('screen.voice_tab.voicenativestepvalue.rect')),skins_dir=skins,box_width=style.size('voice.controls.step_value_box_width')),
        "voiceNativeNumber": build_name_value_definition(rect=tuple(_ui_style.size('screen.voice_tab.voicenativenumber.rect')),skins_dir=skins,box_width=style.size('voice.controls.number_box_width')),
        "voiceNativeDial": _dial(skins,*style.size('voice.controls.dial_size')),
        "voiceNativePanDepth": _dial(skins,*style.size('voice.controls.dial_size'),pan=True),
        "voiceNativeScaleDial": _dial(skins,*style.size('voice.controls.scale_dial_size')),
        "voiceNativeChoice": build_titled_selector(None, "fx editor choice menu", tuple(_ui_style.size('screen.voice_tab.voicenativechoice.size')), skins, caption_handle="Caption", play_face=True),
    }
    caption = next(c for c in definitions["voiceNativeStepValue"]["componentsData"] if c["componentData"]["data"].get("handleName") == "Caption")
    caption["componentData"]["data"]["type"] = "Value"
    diameter=_LAYOUT["steps"]["diameter"]
    for name,factory in (("voiceNativeStepPan",pan_dial_data),("voiceNativeStepLevel",synthyboy_dial_data)):
        with tempfile.TemporaryDirectory(prefix="voice-step-") as folder:
            data=factory(folder,frame_size=diameter,bipolar=True)
            filename=f"{name}_{diameter}.png"
            (skins/filename).write_bytes((Path(folder)/data["filmStrip"]).read_bytes())
            data["filmStrip"]=filename
        definitions[name]=_definition([_component("knob","Knob",data,_bounds((0,0,diameter,diameter),focus="Yes"))],"",triggers=())
        definitions[name]["ignoreMousePresses"]=True
    # LFO TIME / RATE are state-dependent captions on the same real parameter.
    scale = definitions["voiceNativeScaleDial"]
    name = next(c for c in scale["componentsData"] if c["componentData"]["name"]=="name")
    name["bounds"]["additionalInvalidatingHandles"] = ["IndexedEnabling/0/2/Mode"]
    name["bounds"]["version"] = 2
    alternate = copy.deepcopy(name)
    alternate["componentData"]["name"] = "rate caption"
    alternate["bounds"]["additionalInvalidatingHandles"] = ["IndexedEnabling/1/2/Mode"]
    alternate["componentData"]["data"].update(type="Text",text="LFO RATE")
    scale["componentsData"].append(alternate)
    definitions["voiceNativeScaleEnv"] = _dial(skins,*style.size('voice.controls.scale_dial_size'))
    definitions["voiceNativeTargets"] = build_titled_active_text_group_definition(
        "voiceNativeTargets",
        VOICE_TARGET_SCOPE_TITLE,
        VOICE_TARGET_ITEMS,
        size=VOICE_TARGET_GROUP_BOUNDS[2:],
        skins_dir=skins,
        title_rect=VOICE_TARGET_SCOPE_RECT,
        title_font_size=style.size("font.label"),
        font_size=style.size("font.selector"),
    )
    # 10x DETUNE remains standalone ActiveText
    detune10x = next(c for c in VOICE_STANDALONE_CONTROLS if c["key"] == "voicectl.rand.detune10x")
    w, h = detune10x["rect"][2:]
    stem = detune10x["type"]
    on, off = stem + "_on.png", stem + "_off.png"
    for active, filename in ((True, on), (False, off)):
        render_active_text_art(detune10x["caption"], active, w, h, font_size=style.size("font.selector")).save(skins / filename)
    definitions[stem] = active_text_definition(stem, on, off, (w, h), handle="Data")
    definitions[stem]["disableCoarseDataWheel"] = False
    return definitions


def populate_voice_tab(tab, ctx, root_gate, include_step_knobs=True):
    """Replace fallback controls with the exact set of owned Voice apertures."""
    components = tab["componentDefinition"]["componentsData"]
    retained = [c for c in components if c["componentData"]["name"].startswith(KEEP)]

    native = []
    for ctrl in VOICE_STANDALONE_CONTROLS:
        model = "Parameter %d" % ctx.param_index(ctrl["key"])
        kind = ctrl["type"]
        bounds = _bounds(ctrl["rect"], focus="Yes")
        bounds["version"] = 2
        bounds["additionalInvalidatingHandles"] = [root_gate]
        remap = [("Data",model),("Caption",f"WithName[{ctrl['caption']}]/{model}")]
        if ctrl["key"] == "voicectl.scale.lfo":
            remap.append(("Mode",f"Parameter {ctx.param_index('voicectl.scale.lfo_mode')}"))
        native.append(_component(ctrl["name"],kind,{"version":1,"handleName":"Data"},bounds,remap))

    base = sequence_base(ctx)
    row_handle = f"Parameter {base}"
    for field, (name, kind, rect, caption, key) in enumerate(VOICE_SEQUENCE_CONTROLS):
        model = f"Parameter {base + field}"
        bounds = dict(_bounds(rect, focus="Yes"), version=2, additionalInvalidatingHandles=[root_gate])
        handles = [("Data", model)]
        if key == "row":
            handles.append(("Caption", f"WithName[{caption}]/{model}"))
        elif key == "value":
            handles.append(("Caption", f"FromGesture/{row_handle}"))
            handles.append(("Row", row_handle))
        native.append(_component(name, kind, {"version": 1, "handleName": "Data"}, bounds, handles))

    if include_step_knobs:
        native.extend(build_voice_step_knobs(ctx,root_gate,row_handle))

    target_mappings = [
        (item["handle"], f"Parameter {ctx.param_index(item['key'])}")
        for item in VOICE_TARGET_ITEMS
    ]
    targets = titled_active_text_group_entry(
        "Voice Target Scope",
        "voiceNativeTargets",
        VOICE_TARGET_GROUP_BOUNDS,
        target_mappings,
        gates=[root_gate],
        focus="Yes",
    )
    native.append(targets)
    components[:] = native + retained
    return components
