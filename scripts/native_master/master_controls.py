# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone Master tab migration to host-native MPC controls.

Builds host-native MPC controls for standard dials, buttons, toggles,
selectors and lists on the Master tab, while dynamic curves, meters,
and visualizers share the tab with the hardware overlay layer.
"""
import sys as _token_sys
from pathlib import Path as _TokenPath
_token_root = str(_TokenPath(__file__).resolve().parents[2])
if _token_root not in _token_sys.path:
    _token_sys.path.insert(0, _token_root)
from components import style as _ui_style


import copy


import native_ui_params
from components.native.button.button import write_panel_selector_assets
from components.native.level.level import master_control_definition as _definition
from components.native.meter.meter import write_peak_scale as write_native_peak_scale
from components.native.scale_keyboard.scale_keyboard import scale_keyboard
from native_meter import meter_data
from components.native.name_value.name_value import _label_component

# Explicit list of unconverted controls retained in overlay
UNCONVERTED_CONTROLS = [
    {
        "name": "Glide Slope Curve",
        "component": "PortamentoCurveDisplay",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.2')),
        "reason": "Mathematical exponential portamento curvature graph responding dynamically to master.portamento.slope.",
    },
    {
        "name": "List Popup",
        "component": "ListPopup",
        "side": "both",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.4')),
        "reason": "Dynamic modal list overlay for large selection lists (20 musical scales, 12 root keys).",
    },
    {
        "name": "Numeric Keyboard",
        "component": "TextEntry",
        "side": "both",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.5')),
        "reason": "Full-screen modal keypad popup for direct numeric entry on knob taps.",
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
        "name": "Pad Amount",
        "caption": "PAD %",
        "widget": "number",
        "side": "main",
        "rect": tuple(_ui_style.size('screen.master_controls.rect.rect.25')),
        "condition": {"param": "shell.pad_pressure", "choice": 1, "numChoices": 2},
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
                        | {"Master Scale Keyboard"} | MASTER_OUTPUT_WIDGETS.keys() | {MASTER_LINKED_BEND["name"]}
                        | {f"Master Panel {name}" for name, _ in MASTER_PANELS})
MASTER_INTEGER_FIELDS = {
    "master.transpose", "master.bend_range", "master.bend_down_range", "master.polyphony", "master.mpe.bend_range",
}


def _child(name, kind, data, rect):
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide",
                   "whenVisible": "Always", "boundsType": "Absolute", "bounds": _format_bounds(rect)},
    }


def _label(caption, rect):
    name, handle, kind = ("caption", "Caption", "Name") if caption else ("value", "Data", "Value")
    child = _label_component(
        name, kind, handle, rect,
        _ui_style.size("level.master.control")["caption_font" if caption else "value_font"],
        _ui_style.argb("master.meter.scale.text" if caption else "text.icon"),
        "horizontallyCentred verticallyCentred")
    child["bounds"]["showWhenDataModelInvalid"] = "Hide"
    return child


def get_master_control_specs():
    """Return a copy of the master control specifications."""
    return copy.deepcopy(MASTER_SPECS)


def get_unconverted_controls():
    """Return a copy of the unconverted controls list."""
    return copy.deepcopy(UNCONVERTED_CONTROLS)


def _format_bounds(rect):
    return "%d %d %d %d" % tuple(rect)


def _make_bounds(rect, handles=None):
    # Convert screen coordinates (y >= 110) to tab-relative coordinates (y in 0..628)
    tab_rect = (
        rect[0],
        rect[1] - 110 if rect[1] >= 110 else rect[1],
        rect[2],
        rect[3],
    )
    b = {
        "version": 2 if handles else 1,
        "acceptsHWFocus": "Yes",
        "showWhenDataModelInvalid": "Hide" if handles else "Show",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": _format_bounds(tab_rect),
    }
    if handles:
        b["additionalInvalidatingHandles"] = list(handles)
    return b


PEAK_SCALE_IMAGE = "master_peak_scale.png"


def write_peak_scale(directory):
    write_native_peak_scale(directory, MASTER_OUTPUT_WIDGETS["Master Peak Meters"][2:], PEAK_SCALE_IMAGE)


def write_master_panel_art(directory):
    write_panel_selector_assets(directory, MASTER_PANELS)


def _output_widgets(ctx, view, standard_widget):
    n = native_ui_params
    base = n.mixer_peek_index(ctx.param_count)
    main_gate = f"IndexedEnabling/1/3/Parameter {view}"

    def add(name, kind, definition, links, gates=(main_gate,), focus=False):
        rect = MASTER_OUTPUT_WIDGETS[name]
        if definition is not None:
            ctx.add_component_definition(kind, definition)
        item = _child(name, kind, {"version": 1, "handleName": "Data"}, rect)
        item["handle remapping"]["map"] = [
            {"key": handle, "value": f"Parameter {base + offset}"} for handle, offset in links
        ]
        item["bounds"] = _make_bounds(rect, gates)
        item["bounds"]["acceptsHWFocus"] = "Yes" if focus else "No"
        return item

    def text(value, rect, colour=_ui_style.argb('text.icon')):
        label = _label(False, rect)
        label["componentData"]["name"] = value
        data = label["componentData"]["data"]
        data.update({"type": "Text", "text": value, "handleName": ""})
        data["textStyle"].update(colour=colour)
        data["textStyle"]["font"]["height"] = 15.0
        return label

    def panel(children, colour=_ui_style.argb('button.surface'), actions=(), image=""):
        background = {"version": 1, "colour": colour, "image": image}
        return {"version": 2, "actions": list(actions), "backgroundData": {"version": 1,
                     "focussed": background, "unfocussed": background},
                "ignoreMousePresses": not actions, "disableCoarseDataWheel": True, "componentsData": children}

    peak_children = []
    for channel, x in (("L", 38), ("R", 68)):
        data = meter_data()
        data.update(peakHandle="Peak" + channel, rmsHandle="Peak" + channel, peakHoldHandle="Hold" + channel)
        peak_children.append(_child("Peak " + channel, "Meter", data, (x, 4, 26, 98)))
    yield add("Master Peak Meters", "masterNativeOutputMeters", panel(peak_children, image=PEAK_SCALE_IMAGE),
              (("PeakL", n.MASTER_PEAK_L), ("PeakR", n.MASTER_PEAK_R),
               ("HoldL", n.MASTER_HOLD_L), ("HoldR", n.MASTER_HOLD_R)))

    voice_bar = _child("voice bar", "Meter", meter_data("Right", hold=False), tuple(_ui_style.size('screen.master_controls.voice_bar.rect')))
    yield add("Master Voice Meter", "masterNativeVoiceMeter",
              panel([voice_bar, _label(False, tuple(_ui_style.size('screen.master_controls.output_widgets.rect')))]),
              (("Peak", n.MASTER_VOICE_RATIO), ("Data", n.MASTER_VOICE_COUNT)))

    limiter_type = standard_widget("toggle", *MASTER_OUTPUT_WIDGETS["Master Output Limiter"][2:])
    limiter = add("Master Output Limiter", limiter_type, None,
                  (("Data", n.MASTER_LIMITER),), focus=True)
    limiter["handle remapping"]["map"].append(
        {"key": "Caption", "value": f"WithName[LIMIT]/Parameter {base + n.MASTER_LIMITER}"})
    yield limiter

    for active, colour, text_colour in ((0, _ui_style.argb('button.surface'), _ui_style.argb('master.meter.scale.text')), (1, _ui_style.argb('screen.master_controls.output_widgets'), _ui_style.argb('text.white'))):
        suffix = "On" if active else "Off"
        actions = [{"version": 1, "onAction": trigger, "handler": "Toggle Switch",
                    "handleName": "Data", "additionalData": ""} for trigger in ("Mouse Down", "Enter Pressed")]
        yield add("Master Clip " + suffix, "masterNativeClip" + suffix,
                  panel([text("CLIP", tuple(_ui_style.size('screen.master_controls.output_widgets.rect.2')), text_colour)], colour, actions),
                  (("Data", n.MASTER_CLEAR_CLIP),),
                  (main_gate, f"IndexedEnabling/{active}/2/Parameter {base + n.MASTER_CLIP}"), focus=True)


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

    # sliderWithFocusMenu gets its choices from pageData.replacementStrings;
    # populate replacement choices for all 8 selector parameters on Master tab.
    replacement = tui.setdefault("pageData", {}).setdefault("replacementStrings", [])
    for spec in MASTER_SPECS:
        if spec.get("widget") == "selector" and "choices" in spec:
            try:
                param_idx = _param_index(ctx, spec["key"])
            except Exception:
                continue
            key_str = "Parameter %d" % param_idx
            entry = next((r for r in replacement if r.get("key") == key_str), None)
            if entry is None:
                replacement.append({"key": key_str, "value": list(spec["choices"])})
            else:
                entry["value"] = list(spec["choices"])

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
        definition = f"masterNative_{kind}" if kind.startswith("dial_") else f"masterNative_{kind}_{w}x{h}"
        if definition not in added_definitions:
            ctx.add_component_definition(definition, _definition(kind, w, h))
            added_definitions.add(definition)
        return definition

    created_count = 0
    view = native_ui_params.mixer_peek_index(ctx.param_count) + native_ui_params.MASTER_VIEW
    frames = {"main": 1, "tuning": 2}

    for spec in (*MASTER_SPECS, MASTER_LINKED_BEND):
        try:
            param_idx = _param_index(ctx, spec["key"])
        except Exception:
            continue

        handles = [f"IndexedEnabling/{frames[spec['side']]}/3/Parameter {view}"]
        if "condition" in spec:
            cond = spec["condition"]
            cond_idx = _param_index(ctx, cond["param"])
            handles.append("IndexedEnabling/%d/%d/Parameter %d" % (cond["choice"], cond["numChoices"], cond_idx))

        kind = "number" if spec["key"] in MASTER_INTEGER_FIELDS else spec["widget"]
        _, _, w, h = spec["rect"]
        definition = standard_widget(kind, w, h)
        component = _child(spec["name"], definition, {"version": 1, "handleName": "Data"}, spec["rect"])
        component["handle remapping"]["map"] = [
            {"key": "Data", "value": f"Parameter {param_idx}"},
            {"key": "Caption", "value": f"WithName[{spec['caption']}]/Parameter {param_idx}"},
        ]
        component["bounds"] = _make_bounds(spec["rect"], handles)
        if spec["key"].startswith("master.mpe.") and spec["key"] != "master.mpe.on":
            # Keep the original control and readout visible when MPE is off.
            # These native definitions use Knob/Label plus compound actions:
            # their inactive state removes actions, focus and knob dragging.
            _ui_style.bind_control_active(
                component, f"Parameter {_param_index(ctx, 'master.mpe.on')}", ctx,
                tui["pageData"]["componentDefinitions"]["localComponentDefinitions"],
                active_focus="Yes")
            component["bounds"]["acceptsHWFocus"] = "Children"
        components.append(component)
        created_count += 1

    keyboard_rect = _ui_style.size("master.scale_keyboard.rect")
    keyboard_key = f"Parameter {_param_index(ctx, 'master.scale.key')}"
    keyboard_scale = f"Parameter {_param_index(ctx, 'master.scale.type')}"
    ctx.add_component_definition("masterNativeScaleKeyboard",
        scale_keyboard(ctx.skins_dir, keyboard_rect[2:], keyboard_key, keyboard_scale))
    keyboard = _child("Master Scale Keyboard", "masterNativeScaleKeyboard",
                      {"version": 1, "handleName": "Data"}, keyboard_rect)
    keyboard["handle remapping"]["map"] = [{"key": "Data", "value": keyboard_key}]
    keyboard["bounds"] = _make_bounds(keyboard_rect, [f"IndexedEnabling/2/3/Parameter {view}"])
    keyboard["bounds"]["acceptsHWFocus"] = "Children"
    components.append(keyboard)
    created_count += 1

    output = list(_output_widgets(ctx, view, standard_widget))
    components.extend(output)
    panel = native_ui_params.mixer_peek_index(ctx.param_count) + native_ui_params.MASTER_PANEL
    for i, (name, rect) in enumerate(MASTER_PANELS, 1):
        data = {"version": 2, "onImage": f"master_panel_{i}_on.png", "offImage": f"master_panel_{i}_off.png",
                "buttonId": i, "numButtonsInGroup": 3, "handleName": "Data", "gestureBehaviour": "Instant"}
        component = _child(f"Master Panel {name}", "Button", data, rect)
        component["handle remapping"]["map"] = [{"key": "Data", "value": f"WithName[{name}]/FromGesture/Parameter {panel}"}]
        component["bounds"] = _make_bounds(rect, [f"NonZero/Parameter {view}"])
        components.append(component)
    return created_count + len(output) + len(MASTER_PANELS)
