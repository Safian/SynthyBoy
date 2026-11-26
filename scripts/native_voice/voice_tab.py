# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone Voice tab migration to host-native MPC controls.

Defines exact geometry, parameter bindings, component generation, and explicit
inventory of converted vs retained dynamic overlay controls.
Geometry strictly matches shell-mpc/ui/voice_screen.h and docs/ui-map/voice.md.
"""
import json
import sys
import tempfile
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components import style
from components.native.active_text.active_text import build_active_text_toggle, build_titled_active_text_group_definition
from components.native.button.button import entry
from components.native.level.level import bare_dial_definition, stacked_dial_definition
from components.native.name_value.name_value import build_name_value_definition
from components.native.radio_button.radio_button import titled_radio_grid
from components.native.panel.panel import panel_definitions, panel_entry
from components.native.selector.selector import build_titled_selector, selector_definition
from scripts.native_ui_params import VOICE_SEQUENCE, mixer_peek_index
from .sequence_readouts import SEQUENCE_READOUT_NAMES, sequence_readout_definitions, sequence_readout_entries
from .step_controls import VOICE_STEP_KNOB_NAMES, VOICE_STEP_KNOB_PREFIX, VOICE_STEP_ROWS, build_voice_step_knobs

VOICE_TAB_NAME = "Voice"
KEEP = ("Overlay Marker", "Overlay Instance")

_LAYOUT = json.loads(Path(__file__).with_name("voice_layout.json").read_text())
VOICE_NATIVE_CONTROLS = [dict(c, rect=tuple(c["rect"])) for c in _LAYOUT["controls"]]
VOICE_STANDALONE_CONTROLS = [c for c in VOICE_NATIVE_CONTROLS if c["block"] != 2]
VOICE_TARGET_CONTROLS = [c for c in VOICE_NATIVE_CONTROLS if c["block"] == 2]
VOICE_ROWS = VOICE_STEP_ROWS
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
    | set(SEQUENCE_READOUT_NAMES)
    | {"Voice Target Scope"}
)
# The titled cards the controls lie on (listed first in the tab, under every control and marker).
VOICE_PANELS = tuple(dict(p, rect=tuple(p["rect"])) for p in _LAYOUT["panels"])
VOICE_PANEL_NAMES = frozenset(f"Voice Block {p['title']}" for p in VOICE_PANELS)
VOICE_NATIVE_CONTROL_NAMES = VOICE_NATIVE_CONTROL_NAMES | VOICE_PANEL_NAMES


def sequence_base(ctx):
    return mixer_peek_index(len(ctx.params)) + VOICE_SEQUENCE


# Live sounding voices come from the audio engine rather than a host parameter.
UNCONVERTED_CONTROLS = [{
    "name": "Voice Step Grid",
    "component": "VoiceGrid",
    "rect": tuple(_LAYOUT["steps"]["rect"]),
    "reason": "The overlay animates the currently sounding steps from LiveValues::voiceStep.",
}]


def voice_definitions(skins_dir=None):
    skins = Path(skins_dir) if skins_dir else Path(tempfile.gettempdir()) / "synthyboy-voice-assets"
    skins.mkdir(parents=True, exist_ok=True)
    row_size = style.size("voice.controls.row_size")
    x, y = style.size("voice.controls.row_origin")
    dx, dy = style.size("voice.controls.row_pitch")
    cells = [(x + (i % 2) * dx, y + (i // 2) * dy, *row_size) for i in range(len(VOICE_ROWS))]
    row_rect = next(rect for _, _, rect, _, key in VOICE_SEQUENCE_CONTROLS if key == "row")

    def stacked(size_key, alternate=None):
        size = style.size(size_key)
        diameter = min(style.size("voice.controls.dial_max"),
                       size[1] - style.size("voice.controls.dial_vertical_reserve"))
        return stacked_dial_definition(skins, size, diameter=diameter,
                                      caption_height=style.size("voice.controls.dial_caption_height"),
                                      alternate_caption=alternate)

    definitions = {
        "voiceNativeRowList": titled_radio_grid(
            skins, VOICE_ROWS, cells, size=row_rect[2:],
            caption_rect=style.size("screen.voice_tab.voicenativerowlist.rect"),
            caption_height=style.size("voice.controls.row_caption_font")),
        "voiceNativeStepSelect": selector_definition(
            None, "fx editor choice menu", style.size("screen.voice_tab.voicenativestepselect.size"), skins),
        "voiceNativeStepValue": build_name_value_definition(
            rect=style.size("screen.voice_tab.voicenativestepvalue.rect"), skins_dir=skins,
            box_width=style.size("voice.controls.step_value_box_width"), caption_kind="Value",
            label_size=style.size("font.caption"), value_size=style.size("font.value")),
        "voiceNativeNumber": build_name_value_definition(
            rect=style.size("screen.voice_tab.voicenativenumber.rect"), skins_dir=skins,
            box_width=style.size("voice.controls.number_box_width"),
            label_size=style.size("font.caption"), value_size=style.size("font.value")),
        "voiceNativeDial": stacked("voice.controls.dial_size"),
        "voiceNativeScaleDial": stacked("voice.controls.scale_dial_size", ("TimeCaption", "RateCaption", "LFO RATE")),
        "voiceNativeScaleEnv": stacked("voice.controls.scale_dial_size"),
        "voiceNativeStepLevel": bare_dial_definition(skins, _LAYOUT["steps"]["diameter"], polarity="BiPolar"),
        "voiceNativeChoice": build_titled_selector(
            None, "fx editor choice menu", style.size("screen.voice_tab.voicenativechoice.size"), skins,
            caption_handle="Caption", play_face=True),
        **sequence_readout_definitions(skins),
    }
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
    toggle = next(c for c in VOICE_STANDALONE_CONTROLS if c["key"] == "voicectl.rand.detune10x")
    definitions[toggle["type"]] = build_active_text_toggle(
        skins, toggle["type"], toggle["caption"], toggle["rect"][2:], font_size=style.size("font.selector"))
    definitions.update(panel_definitions([(f"voiceNativeBlock_{p['name']}", p["rect"][2:], p["title"], None)
                                          for p in VOICE_PANELS], keyline="voiceNativeBlockKeyline"))
    return definitions


def populate_voice_tab(tab, ctx, root_gate, include_step_knobs=True):
    """Replace fallback controls with the exact set of owned Voice apertures."""
    components = tab["componentDefinition"]["componentsData"]
    retained = [c for c in components if c["componentData"]["name"].startswith(KEEP)]

    native = []
    for ctrl in VOICE_STANDALONE_CONTROLS:
        model = "Parameter %d" % ctx.param_index(ctrl["key"])
        kind = ctrl["type"]
        remap = [("Data", model), ("Caption", f"WithName[{ctrl['caption']}]/{model}")]
        if ctrl["key"] == "voicectl.scale.lfo":
            mode = f"Parameter {ctx.param_index('voicectl.scale.lfo_mode')}"
            remap.extend((("TimeCaption", f"IndexedEnabling/0/2/{mode}"),
                          ("RateCaption", f"IndexedEnabling/1/2/{mode}")))
        native.append(entry(ctrl["name"], kind, {"version": 1, "handleName": "Data"},
                            ctrl["rect"], remap, gates=[root_gate]))

    base = sequence_base(ctx)
    row_handle = f"Parameter {base}"
    for field, (name, kind, rect, caption, key) in enumerate(VOICE_SEQUENCE_CONTROLS):
        model = f"Parameter {base + field}"
        handles = [("Data", model)]
        if key == "row":
            handles.append(("Caption", f"WithName[{caption}]/{model}"))
        elif key == "value":
            handles.append(("Caption", f"FromGesture/{row_handle}"))
            handles.append(("Row", row_handle))
        native.append(entry(name, kind, {"version": 1, "handleName": "Data"}, rect, handles, gates=[root_gate]))

    if include_step_knobs:
        native.extend(build_voice_step_knobs(ctx, root_gate, row_handle))

    native.extend(sequence_readout_entries(ctx, root_gate, row_handle, f"Parameter {base + 1}"))

    target_mappings = [
        (item["handle"], f"Parameter {ctx.param_index(item['key'])}")
        for item in VOICE_TARGET_ITEMS
    ]
    native.append(entry(
        "Voice Target Scope", "voiceNativeTargets", {"version": 1, "handleName": "Data"},
        VOICE_TARGET_GROUP_BOUNDS, [("Data", target_mappings[0][1]), *target_mappings],
        gates=[root_gate], focus="Children",
    ))
    panels = [panel_entry(f"Voice Block {p['title']}", f"voiceNativeBlock_{p['name']}", p["rect"], gates=[root_gate])
              for p in VOICE_PANELS]
    components[:] = panels + native + retained
    return components
