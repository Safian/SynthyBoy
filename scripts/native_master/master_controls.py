# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Bind shared native controls and meters to the two Master panels."""
import sys as _token_sys
from pathlib import Path as _TokenPath
_token_root = str(_TokenPath(__file__).resolve().parents[2])
if _token_root not in _token_sys.path:
    _token_sys.path.insert(0, _token_root)
from components import style as _ui_style


import copy


import native_ui_params
from components.native.active_text.active_text import build_active_text_toggle
from components.native.button.button import action_button, entry, mode_button
from components.native.level.level import stacked_dial_definition
from components.native.meter.meter import labelled_horizontal_meter, write_peak_scale as write_native_peak_scale
from components.native.meter_block.meter_block import build_meter_block_definition
from components.native.name_value.name_value import build_name_value_definition
from components.native.radio_button.radio_button import segmented_radio_entry
from components.native.selector.selector import bind_list_choices, selector_definition

# Explicit list of unconverted controls retained in overlay
UNCONVERTED_CONTROLS = [
    {
        "name": "Glide Slope Curve",
        "component": "PortamentoCurveDisplay",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.2')),
        "reason": "Mathematical exponential portamento curvature graph responding dynamically to master.portamento.slope.",
    },

]

# The two shell settings follow the display handles in the release build
# (PluginInstance::padPressureBase: four LFO cursors and 32 spectrum columns).
_DISPLAY_PARAMS = 4 + 32
SHELL_OFFSETS = {"shell.pad_pressure": _DISPLAY_PARAMS, "shell.pad_pressure_amount": _DISPLAY_PARAMS + 1}


def _param_index(ctx, key):
    if key in SHELL_OFFSETS:
        return ctx.param_count + SHELL_OFFSETS[key]
    return ctx.param_index(key)


# Standard host-native MPC control specifications.
MASTER_SPECS = [
    # ---- OUTPUT BLOCK (SideMain)
    {
        "key": "master.volume",
        "name": "Master Volume",
        "caption": "VOLUME",
        "widget": "dial_bi",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.6')),
    },
    {
        "key": "master.stereo_spread",
        "name": "Master Stereo Spread",
        "caption": "SPREAD",
        "widget": "dial_uni",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.7')),
    },
    {
        "key": "master.stereo_routing",
        "name": "Master Stereo Routing",
        "caption": "STEREO",
        "widget": "dial_uni",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.8')),
    },
    {
        "key": "master.voice_override",
        "name": "Master Voice Override",
        "caption": "VOICE OVERRIDE",
        "widget": "selector",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.9')),
        "choices": ["Kill", "Steal"],
    },
    {
        "key": "quality.no_smoothing",
        "name": "Master No Smoothing",
        "caption": "NO SMOOTHING",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.10')),
    },
    {
        "key": "quality.oversample_4x",
        "name": "Master Oversample 4X",
        "caption": "OVERSAMPLE 4X",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.11')),
    },

    # ---- PITCH BLOCK (SideMain)
    {
        "key": "master.tune",
        "name": "Master Tune",
        "caption": "TUNE",
        "widget": "dial_bi",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.12')),
    },
    {
        "key": "master.transpose",
        "name": "Master Transpose",
        "caption": "TRANSPOSE",
        "widget": "dial_bi",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.13')),
    },
    {
        "key": "master.bend_range",
        "name": "Master Bend Range Up",
        "caption": "BEND UP",
        "widget": "dial_uni",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.14')),
        "condition": {"param": "master.bend_down_link", "choice": 0, "numChoices": 2},
    },
    {
        "key": "master.bend_down_range",
        "name": "Master Bend Range Down",
        "caption": "BEND DOWN",
        "widget": "dial_uni",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.15')),
        "condition": {"param": "master.bend_down_link", "choice": 0, "numChoices": 2},
    },
    {
        "key": "master.bend_down_link",
        "name": "Master Bend Link",
        "caption": "LINK",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.16')),
    },

    # ---- VOICES BLOCK (SideMain)
    {
        "key": "master.polyphony",
        "name": "Master Polyphony",
        "caption": "POLY",
        "widget": "dial_uni",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.17')),
    },
    {
        "key": "master.legato",
        "name": "Master Legato",
        "caption": "LEGATO",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.18')),
    },
    {
        "key": "master.poly_legato",
        "name": "Master Poly Legato",
        "caption": "POLY LEG",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.19')),
    },
    {
        "key": "voice.phase_legato",
        "name": "Voice Phase Legato",
        "caption": "PHASE LEG",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.20')),
    },
    {
        "key": "master.velocity_track",
        "name": "Master Velocity Track",
        "caption": "VEL TRACK",
        "widget": "dial_bi",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.21')),
    },
    {
        "key": "master.voice.same_note",
        "name": "Master Same Note",
        "caption": "SAME NOTE",
        "widget": "selector",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.22')),
        "choices": ["Steal", "Stack", "Ignore"],
    },
    {
        "key": "master.voice.steal_priority",
        "name": "Master Steal Priority",
        "caption": "STEAL",
        "widget": "selector",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.23')),
        "choices": ["Newest", "Oldest", "Highest", "Lowest", "Velocity"],
    },

    {
        "key": "shell.pad_pressure",
        "name": "Pad Pressure",
        "caption": "PAD PRESS",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.24')),
    },
    {
        "key": "shell.pad_pressure_amount",
        "name": "Master Pad Amount",
        "caption": "PAD %",
        "widget": "number",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.25')),
        "active_key": "shell.pad_pressure",
    },
    # ---- GLIDE BLOCK (SideMain)
    {
        "key": "master.portamento.time",
        "name": "Master Glide Time",
        "caption": "GLIDE",
        "widget": "dial_uni",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.26')),
    },
    {
        "key": "master.portamento.slope",
        "name": "Master Glide Slope",
        "caption": "SLOPE",
        "widget": "dial_bi",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.27')),
    },
    {
        "key": "master.portamento.always",
        "name": "Master Glide Always",
        "caption": "ALWAYS",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.28')),
    },
    {
        "key": "master.portamento.scaled",
        "name": "Master Glide Scaled",
        "caption": "SCALED",
        "widget": "toggle",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.29')),
    },

    # ---- SCALE BLOCK (SideTuning)
    {
        "key": "master.scale.type",
        "name": "Master Scale Type",
        "caption": "SCALE",
        "widget": "selector",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.30')),
        "choices": [
            "Chromatic (off)", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor",
            "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
            "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone",
            "Diminished (H-W)", "Diminished (W-H)", "Phrygian Dominant",
            "Hungarian Minor", "Double Harmonic", "Hirajoshi",
        ],
    },
    {
        "key": "master.scale.key",
        "name": "Master Scale Key",
        "caption": "KEY",
        "widget": "selector",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.31')),
        "choices": ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"],
    },
    {
        "key": "master.scale.mode",
        "name": "Master Scale Mode",
        "caption": "MODE",
        "widget": "selector",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.32')),
        "choices": ["Conform", "Mark only"],
    },

    # ---- TUNING BLOCK (SideTuning)
    {
        "key": "master.tuning.a4",
        "name": "Master Tuning A4",
        "caption": "A4 HZ",
        "widget": "dial_uni",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.33')),
    },
    {
        "key": "master.tuning.scale",
        "name": "Master Tuning Scale",
        "caption": "TUNING",
        "widget": "selector",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.34')),
        "choices": ["12-TET", "Pythagorean", "Just intonation", "User file"],
    },
    {
        "key": "master.tuning.mts_esp",
        "name": "Master MTS-ESP",
        "caption": "MTS-ESP",
        "widget": "toggle",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.35')),
    },
    {
        "key": "master.tuning.mts_note_on_only",
        "name": "Master MTS Note-On",
        "caption": "NOTE-ON",
        "widget": "toggle",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.36')),
    },

    # ---- MPE BLOCK (SideTuning)
    {
        "key": "master.mpe.on",
        "name": "Master MPE On",
        "caption": "MPE",
        "widget": "toggle",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.37')),
    },
    {
        "key": "master.mpe.bend_range",
        "name": "Master MPE Bend Range",
        "caption": "MPE BEND",
        "widget": "dial_uni",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.38')),
    },
    {
        "key": "master.mpe.xyz_macro",
        "name": "Master MPE XYZ to Macros",
        "caption": "XYZ TO MACROS",
        "widget": "selector",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.39')),
        "choices": ["Off", "XYZ -> Macro 1, 2, 3", "YZ -> Macro 1, 2"],
    },
    {
        "key": "master.mpe.y_modwheel",
        "name": "Master MPE Y Modwheel",
        "caption": "Y TO WHEEL",
        "widget": "toggle",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.40')),
    },
    {
        "key": "master.mpe.bend_to_x",
        "name": "Master MPE Bend to X",
        "caption": "BEND TO X",
        "widget": "toggle",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.41')),
    },
    {
        "key": "master.mpe.y_bipolar",
        "name": "Master MPE Y Bipolar",
        "caption": "Y BIPOLAR",
        "widget": "toggle",
        "side": "tuning",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.42')),
    },
]


MASTER_OUTPUT_WIDGETS = {
    "Master Peak Meters": tuple(_ui_style.size('screen.master_controls.master_peak_meters.rect')),
    "Master Voice Meter": tuple(_ui_style.size('screen.master_controls.master_voice_meter.rect')),
    "Master Clip Off": tuple(_ui_style.size('screen.master_controls.master_clip_off.rect')),
    "Master Clip On": tuple(_ui_style.size('screen.master_controls.master_clip_on.rect')),
    "Master Output Limiter": tuple(_ui_style.size('screen.master_controls.master_output_limiter.rect')),
}
MASTER_PANELS = (("MAIN", tuple(_ui_style.size('screen.master_controls.master_panels.rect'))), ("TUNING & MPE", tuple(_ui_style.size('screen.master_controls.master_panels.rect.2'))))
MASTER_LINKED_BEND = {
    "key": "master.bend_range",
    "name": "Master Bend Range Linked",
    "caption": "BEND",
    "widget": "dial_uni",
    "side": "main",
    "rect": tuple(_ui_style.size('screen.master_controls.rect.rect')),
    "condition": {"param": "master.bend_down_link", "choice": 1, "numChoices": 2},
}
MASTER_CONTROL_NAMES = (frozenset(spec["name"] for spec in MASTER_SPECS)
                        | MASTER_OUTPUT_WIDGETS.keys() | {MASTER_LINKED_BEND["name"], "Master Scale Input"}
                        | {f"Master Panel {name}" for name, _ in MASTER_PANELS})
MASTER_INTEGER_FIELDS = {
    "master.transpose", "master.bend_range", "master.bend_down_range", "master.polyphony", "master.mpe.bend_range",
}


def get_master_control_specs():
    """Return a copy of the master control specifications."""
    return copy.deepcopy(MASTER_SPECS)


def get_unconverted_controls():
    """Return a copy of the unconverted controls list."""
    return copy.deepcopy(UNCONVERTED_CONTROLS)


def _tab_rect(rect):
    x, y, w, h = rect
    return x, y - 110, w, h


PEAK_SCALE_IMAGE = "master_peak_scale.png"


def write_peak_scale(directory):
    write_native_peak_scale(directory, MASTER_OUTPUT_WIDGETS["Master Peak Meters"][2:], PEAK_SCALE_IMAGE)


def _toggle(ctx, name, caption, rect, parameter, gates):
    w, h = rect[2:]
    kind = f"masterNativeActiveText_{caption.replace(' ', '_')}_{w}x{h}"
    ctx.add_component_definition(kind, build_active_text_toggle(ctx.skins_dir, kind, caption, (w, h)))
    return entry(name, kind, {"version": 1, "handleName": "Data"}, _tab_rect(rect),
                 (("Data", parameter),), gates=gates)


def _output_widgets(ctx, view):
    n = native_ui_params
    base = n.mixer_peek_index(ctx.param_count)
    main = (f"IndexedEnabling/1/3/Parameter {view}",)

    def meter(name, kind, definition, links):
        ctx.add_component_definition(kind, definition)
        return entry(name, kind, {"version": 1, "handleName": "Data"}, _tab_rect(MASTER_OUTPUT_WIDGETS[name]),
                     [(key, f"Parameter {base + offset}") for key, offset in links],
                     gates=main, focus="No")

    peaks = build_meter_block_definition("masterNativeOutputMeters", with_clip=False,
                                         stereo_rects=_ui_style.size("master.peak_channels"),
                                         scale_image=PEAK_SCALE_IMAGE)
    yield meter("Master Peak Meters", "masterNativeOutputMeters", peaks,
                (("Data", n.MASTER_PEAK_L), ("PeakL", n.MASTER_PEAK_L), ("PeakR", n.MASTER_PEAK_R),
                 ("HoldL", n.MASTER_HOLD_L), ("HoldR", n.MASTER_HOLD_R)))
    voices = labelled_horizontal_meter(_ui_style.size("screen.master_controls.voice_bar.rect"),
                                       _ui_style.size("screen.master_controls.output_widgets.rect"))
    yield meter("Master Voice Meter", "masterNativeVoiceMeter", voices,
                (("Peak", n.MASTER_VOICE_RATIO), ("Data", n.MASTER_VOICE_COUNT)))
    yield _toggle(ctx, "Master Output Limiter", "LIMIT", MASTER_OUTPUT_WIDGETS["Master Output Limiter"],
                  f"Parameter {base + n.MASTER_LIMITER}", main)
    for state, label in enumerate(("Off", "On")):
        name = "Master Clip " + label
        yield action_button(ctx, name, _tab_rect(MASTER_OUTPUT_WIDGETS[name]),
                            f"Parameter {base + n.MASTER_CLEAR_CLIP}", text="CLIP",
                            colour=_ui_style.rgb("accent.red" if state else "text.muted"),
                            gates=(*main, f"IndexedEnabling/{state}/2/Parameter {base + n.MASTER_CLIP}"))


def apply_master_native_controls(tui, qlinks, ctx):
    """Applies host-native MPC controls migration to the Master tab in TUI.json.

    Builds the standard dials, toggles, and selectors on the Master tab,
    ensuring correct parameter indices, coordinate geometry, replacementStrings,
    and conditional visibility / touch focus.
    """
    try:
        tab = ctx.tab("Master")
    except Exception:
        return 0

    components = tab["componentDefinition"]["componentsData"]

    # Check if native controls are already applied (idempotency check)
    if any(c.get("componentData", {}).get("name") == "Master Volume" for c in components):
        return 0

    # Purge legacy fallback controls so no ghost fallbacks remain
    components[:] = [
        c for c in components
        if c.get("componentData", {}).get("name", "").startswith(("Overlay Marker", "Overlay Instance"))
    ]

    added_definitions = set()

    def standard_widget(kind, w, h):
        definition = f"masterNative_{kind}_{w}x{h}"
        if definition in added_definitions:
            return definition
        if kind == "number":
            widget = build_name_value_definition(rect=(0, 0, w, h), skins_dir=ctx.skins_dir,
                                                 box_height=_ui_style.size("height.small"),
                                                 box_width=min(w // 3, _ui_style.size("name_value.box.width")))
        elif kind == "selector":
            widget = selector_definition(None, "fx editor choice menu", (w, h), ctx.skins_dir,
                                         name="Caption", play_face=True)
        else:
            caption_h = _ui_style.size("active_text.default.height")
            diameter = min(_ui_style.size("voice.controls.dial_max"), h - 2 * caption_h, w)
            widget = stacked_dial_definition(ctx.skins_dir, (w, h), diameter=diameter, caption_height=caption_h,
                                             polarity="BiPolar" if kind == "dial_bi" else "UniPolar")
        ctx.add_component_definition(definition, widget)
        added_definitions.add(definition)
        return definition

    created_count = 0
    view = native_ui_params.mixer_peek_index(ctx.param_count) + native_ui_params.MASTER_VIEW
    frames = {"main": 1, "tuning": 2}

    for spec in (*MASTER_SPECS, MASTER_LINKED_BEND):
        param_idx = _param_index(ctx, spec["key"])
        if "choices" in spec:
            bind_list_choices(tui, param_idx, spec["caption"], spec["choices"])

        handles = [f"IndexedEnabling/{frames[spec['side']]}/3/Parameter {view}"]
        if "condition" in spec:
            cond = spec["condition"]
            cond_idx = _param_index(ctx, cond["param"])
            handles.append("IndexedEnabling/%d/%d/Parameter %d" % (cond["choice"], cond["numChoices"], cond_idx))

        kind = "number" if spec["key"] in MASTER_INTEGER_FIELDS else spec["widget"]
        _, _, w, h = spec["rect"]
        parameter = f"Parameter {param_idx}"
        if kind == "toggle":
            component = _toggle(ctx, spec["name"], spec["caption"], spec["rect"], parameter, handles)
        else:
            definition = standard_widget(kind, w, h)
            component = entry(spec["name"], definition, {"version": 1, "handleName": "Data"},
                              _tab_rect(spec["rect"]), (("Data", parameter),
                              ("Caption", f"WithName[{spec['caption']}]/{parameter}")), gates=handles)
        active_key = spec.get("active_key")
        if spec["key"].startswith("master.mpe.") and spec["key"] != "master.mpe.on":
            active_key = "master.mpe.on"
        if active_key:
            # Keep the original control and readout visible when MPE is off.
            # These native definitions use Knob/Label plus compound actions:
            # their inactive state removes actions, focus and knob dragging.
            _ui_style.bind_control_active(
                component, f"Parameter {_param_index(ctx, active_key)}", ctx,
                tui["pageData"]["componentDefinitions"]["localComponentDefinitions"],
                active_focus="Yes")
            component["bounds"]["acceptsHWFocus"] = "Children"
        components.append(component)
        created_count += 1

    base = native_ui_params.mixer_peek_index(ctx.param_count)
    control = base + native_ui_params.MASTER_SCALE_INPUT
    switch = mode_button(ctx, "Master Scale Input", _tab_rect(_ui_style.size("master.scale_input.rect")),
                         f"Parameter {control}", on_text="KEY MODE  •  SELECT ROOT  •  TAP TO PLAY",
                         off_text="ACTIVATE SCALE INPUT", gates=(f"IndexedEnabling/2/3/Parameter {view}",))
    components.append(switch)
    output = list(_output_widgets(ctx, view))
    components.extend(output)
    panel = base + native_ui_params.MASTER_PANEL
    for i, (name, rect) in enumerate(MASTER_PANELS):
        components.append(segmented_radio_entry(f"Master Panel {name}", name, _tab_rect(rect), i, len(MASTER_PANELS),
                          f"Parameter {panel}", ctx.skins_dir,
                          gates=(f"NonZero/Parameter {view}",)))
    return created_count + 1 + len(output) + len(MASTER_PANELS)
