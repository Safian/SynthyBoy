# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone Master tab migration to host-native MPC controls.

Builds host-native MPC controls for standard dials, buttons, toggles,
selectors and lists on the Master tab, while dynamic curves, meters,
and visualizers share the tab with the hardware overlay layer.
"""

import copy

from PIL import Image, ImageDraw

import native_ui_params
from native_skin_font import semibold
from native_meter import meter_data

# Explicit list of unconverted controls retained in overlay
UNCONVERTED_CONTROLS = [
    {
        "name": "Glide Slope Curve",
        "component": "PortamentoCurveDisplay",
        "side": "main",
        "rect": (1020, 456, 246, 268),
        "reason": "Mathematical exponential portamento curvature graph responding dynamically to master.portamento.slope.",
    },
    {
        "name": "Mini Keyboard",
        "component": "MiniKeyboardDisplay",
        "side": "tuning",
        "rect": (14, 294, 1252, 162),
        "reason": "Interactive 12-key piano keyboard visualizer with dynamic in-scale pitch highlights.",
    },
    {
        "name": "List Popup",
        "component": "ListPopup",
        "side": "both",
        "rect": (340, 130, 600, 460),
        "reason": "Dynamic modal list overlay for large selection lists (20 musical scales, 12 root keys).",
    },
    {
        "name": "Numeric Keyboard",
        "component": "TextEntry",
        "side": "both",
        "rect": (0, 110, 1280, 628),
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
        "rect": (122, 220, 119, 124),
    },
    {
        "key": "master.stereo_spread",
        "name": "Master Stereo Spread",
        "caption": "SPREAD",
        "widget": "dial_uni",
        "side": "main",
        "rect": (245, 220, 119, 124),
    },
    {
        "key": "master.stereo_routing",
        "name": "Master Stereo Routing",
        "caption": "STEREO",
        "widget": "dial_uni",
        "side": "main",
        "rect": (368, 220, 119, 124),
    },
    {
        "key": "master.voice_override",
        "name": "Master Voice Override",
        "caption": "VOICE OVERRIDE",
        "widget": "selector",
        "side": "main",
        "rect": (492, 220, 150, 124),
        "choices": ["Kill", "Steal"],
    },
    {
        "key": "quality.no_smoothing",
        "name": "Master No Smoothing",
        "caption": "NO SMOOTHING",
        "widget": "toggle",
        "side": "main",
        "rect": (300, 348, 160, 56),
    },
    {
        "key": "quality.oversample_4x",
        "name": "Master Oversample 4X",
        "caption": "OVERSAMPLE 4X",
        "widget": "toggle",
        "side": "main",
        "rect": (464, 348, 166, 56),
    },

    # ---- PITCH BLOCK (SideMain)
    {
        "key": "master.tune",
        "name": "Master Tune",
        "caption": "TUNE",
        "widget": "dial_bi",
        "side": "main",
        "rect": (662, 220, 190, 112),
    },
    {
        "key": "master.transpose",
        "name": "Master Transpose",
        "caption": "TRANSPOSE",
        "widget": "dial_bi",
        "side": "main",
        "rect": (856, 220, 134, 112),
    },
    {
        "key": "master.bend_range",
        "name": "Master Bend Range Up",
        "caption": "BEND UP",
        "widget": "dial_uni",
        "side": "main",
        "rect": (994, 220, 134, 112),
        "condition": {"param": "master.bend_down_link", "choice": 0, "numChoices": 2},
    },
    {
        "key": "master.bend_down_range",
        "name": "Master Bend Range Down",
        "caption": "BEND DOWN",
        "widget": "dial_uni",
        "side": "main",
        "rect": (1132, 220, 134, 112),
        "condition": {"param": "master.bend_down_link", "choice": 0, "numChoices": 2},
    },
    {
        "key": "master.bend_down_link",
        "name": "Master Bend Link",
        "caption": "LINK",
        "widget": "toggle",
        "side": "main",
        "rect": (994, 348, 96, 56),
    },

    # ---- VOICES BLOCK (SideMain)
    {
        "key": "master.polyphony",
        "name": "Master Polyphony",
        "caption": "POLY",
        "widget": "dial_uni",
        "side": "main",
        "rect": (14, 456, 189, 56),
    },
    {
        "key": "master.legato",
        "name": "Master Legato",
        "caption": "LEGATO",
        "widget": "toggle",
        "side": "main",
        "rect": (207, 456, 189, 56),
    },
    {
        "key": "master.poly_legato",
        "name": "Master Poly Legato",
        "caption": "POLY LEG",
        "widget": "toggle",
        "side": "main",
        "rect": (400, 456, 189, 56),
    },
    {
        "key": "voice.phase_legato",
        "name": "Voice Phase Legato",
        "caption": "PHASE LEG",
        "widget": "toggle",
        "side": "main",
        "rect": (593, 456, 189, 56),
    },
    {
        "key": "master.velocity_track",
        "name": "Master Velocity Track",
        "caption": "VEL TRACK",
        "widget": "dial_bi",
        "side": "main",
        "rect": (14, 528, 189, 112),
    },
    {
        "key": "master.voice.same_note",
        "name": "Master Same Note",
        "caption": "SAME NOTE",
        "widget": "selector",
        "side": "main",
        "rect": (207, 528, 226, 112),
        "choices": ["Steal", "Stack", "Ignore"],
    },
    {
        "key": "master.voice.steal_priority",
        "name": "Master Steal Priority",
        "caption": "STEAL",
        "widget": "selector",
        "side": "main",
        "rect": (437, 528, 345, 112),
        "choices": ["Newest", "Oldest", "Highest", "Lowest", "Velocity"],
    },

    {
        "key": "shell.pad_pressure",
        "name": "Pad Pressure",
        "caption": "PAD PRESS",
        "widget": "toggle",
        "side": "main",
        "rect": (14, 656, 189, 56),
    },
    {
        "key": "shell.pad_pressure_amount",
        "name": "Pad Amount",
        "caption": "PAD %",
        "widget": "number",
        "side": "main",
        "rect": (207, 656, 189, 56),
        "condition": {"param": "shell.pad_pressure", "choice": 1, "numChoices": 2},
    },
    # ---- GLIDE BLOCK (SideMain)
    {
        "key": "master.portamento.time",
        "name": "Master Glide Time",
        "caption": "GLIDE",
        "widget": "dial_uni",
        "side": "main",
        "rect": (802, 456, 105, 196),
    },
    {
        "key": "master.portamento.slope",
        "name": "Master Glide Slope",
        "caption": "SLOPE",
        "widget": "dial_bi",
        "side": "main",
        "rect": (911, 456, 105, 196),
    },
    {
        "key": "master.portamento.always",
        "name": "Master Glide Always",
        "caption": "ALWAYS",
        "widget": "toggle",
        "side": "main",
        "rect": (802, 668, 105, 56),
    },
    {
        "key": "master.portamento.scaled",
        "name": "Master Glide Scaled",
        "caption": "SCALED",
        "widget": "toggle",
        "side": "main",
        "rect": (911, 668, 105, 56),
    },

    # ---- SCALE BLOCK (SideTuning)
    {
        "key": "master.scale.type",
        "name": "Master Scale Type",
        "caption": "SCALE",
        "widget": "selector",
        "side": "tuning",
        "rect": (14, 220, 450, 64),
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
        "rect": (468, 220, 325, 64),
        "choices": ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"],
    },
    {
        "key": "master.scale.mode",
        "name": "Master Scale Mode",
        "caption": "MODE",
        "widget": "selector",
        "side": "tuning",
        "rect": (797, 220, 469, 64),
        "choices": ["Conform", "Mark only"],
    },

    # ---- TUNING BLOCK (SideTuning)
    {
        "key": "master.tuning.a4",
        "name": "Master Tuning A4",
        "caption": "A4 HZ",
        "widget": "dial_uni",
        "side": "tuning",
        "rect": (14, 508, 200, 144),
    },
    {
        "key": "master.tuning.scale",
        "name": "Master Tuning Scale",
        "caption": "TUNING",
        "widget": "selector",
        "side": "tuning",
        "rect": (218, 508, 412, 144),
        "choices": ["12-TET", "Pythagorean", "Just intonation", "User file"],
    },
    {
        "key": "master.tuning.mts_esp",
        "name": "Master MTS-ESP",
        "caption": "MTS-ESP",
        "widget": "toggle",
        "side": "tuning",
        "rect": (14, 668, 306, 56),
    },
    {
        "key": "master.tuning.mts_note_on_only",
        "name": "Master MTS Note-On",
        "caption": "NOTE-ON",
        "widget": "toggle",
        "side": "tuning",
        "rect": (324, 668, 306, 56),
    },

    # ---- MPE BLOCK (SideTuning)
    {
        "key": "master.mpe.on",
        "name": "Master MPE On",
        "caption": "MPE",
        "widget": "toggle",
        "side": "tuning",
        "rect": (650, 508, 202, 144),
    },
    {
        "key": "master.mpe.bend_range",
        "name": "Master MPE Bend Range",
        "caption": "MPE BEND",
        "widget": "dial_uni",
        "side": "tuning",
        "rect": (856, 508, 202, 144),
        "condition": {"param": "master.mpe.on", "choice": 1, "numChoices": 2},
    },
    {
        "key": "master.mpe.xyz_macro",
        "name": "Master MPE XYZ to Macros",
        "caption": "XYZ TO MACROS",
        "widget": "selector",
        "side": "tuning",
        "rect": (1062, 508, 204, 104),
        "choices": ["Off", "XYZ -> Macro 1, 2, 3", "YZ -> Macro 1, 2"],
    },
    {
        "key": "master.mpe.y_modwheel",
        "name": "Master MPE Y Modwheel",
        "caption": "Y TO WHEEL",
        "widget": "toggle",
        "side": "tuning",
        "rect": (650, 668, 202, 56),
    },
    {
        "key": "master.mpe.bend_to_x",
        "name": "Master MPE Bend to X",
        "caption": "BEND TO X",
        "widget": "toggle",
        "side": "tuning",
        "rect": (856, 668, 202, 56),
        "condition": {"param": "master.mpe.on", "choice": 1, "numChoices": 2},
    },
    {
        "key": "master.mpe.y_bipolar",
        "name": "Master MPE Y Bipolar",
        "caption": "Y BIPOLAR",
        "widget": "toggle",
        "side": "tuning",
        "rect": (1062, 668, 204, 56),
    },
]


MASTER_OUTPUT_WIDGETS = {
    "Master Peak Meters": (14, 276, 100, 128),
    "Master Voice Meter": (400, 662, 382, 44),
    "Master Clip Off": (14, 220, 100, 56),
    "Master Clip On": (14, 220, 100, 56),
    "Master Output Limiter": (122, 348, 96, 56),
}
MASTER_PANELS = (("MAIN", (300, 114, 160, 56)), ("TUNING & MPE", (468, 114, 220, 56)))
MASTER_LINKED_BEND = {
    "key": "master.bend_range",
    "name": "Master Bend Range Linked",
    "caption": "BEND",
    "widget": "dial_uni",
    "side": "main",
    "rect": (994, 220, 272, 112),
    "condition": {"param": "master.bend_down_link", "choice": 1, "numChoices": 2},
}
MASTER_CONTROL_NAMES = (frozenset(spec["name"] for spec in MASTER_SPECS)
                        | MASTER_OUTPUT_WIDGETS.keys() | {MASTER_LINKED_BEND["name"]}
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
    return _child(name, "Label", {
        "version": 1,
        "textStyle": {
            "version": 1,
            "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": 16.0 if caption else 20.0},
            "colour": "ffc3cbdf" if caption else "ffe6e6e6",
            "justification": "horizontallyCentred verticallyCentred", "case": "Original",
        },
        "type": kind, "handleName": handle,
    }, rect)


def _definition(kind, w, h):
    def action(trigger, handler, popup=""):
        return {"version": 1, "onAction": trigger, "handler": handler,
                "handleName": "Data", "additionalData": popup}

    if kind.startswith("dial_"):
        inset = 4
        children = [_label(True, (inset, 0, w - 2 * inset, 24)),
                    _child("dial", "Knob", {"version": 5, "knobType": "GenericNoLabel",
                           "polarity": "BiPolar" if kind == "dial_bi" else "UniPolar",
                           "dragOrientation": "Vertical", "handleName": "Data"},
                           (inset, 24, w - 2 * inset, h - 48)),
                    _label(False, (inset, h - 24, w - 2 * inset, 24))]
        for child, relative in zip(children, ("4, 0, parent.width - 4, 24",
                                               "4, 24, parent.width - 4, parent.height - 24",
                                               "4, parent.height - 24, parent.width - 4, parent.height")):
            bounds = child["bounds"]
            bounds.pop("bounds")
            bounds.update(boundsType="Relative", relativeBounds=relative)
        actions = [action(trigger, "Show Overlay", "fx editor number menu")
                   for trigger in ("Double Click", "Enter Pressed")]
    else:
        caption_width = w * 3 // 5
        children = [_label(True, (2, 0, caption_width - 4, h)),
                    _label(False, (caption_width, 0, w - caption_width - 2, h))]
        if kind == "selector":
            # Long choice names get the full width below their caption.
            children = [_label(True, (4, 0, w - 8, 24)), _label(False, (4, 24, w - 8, h - 24))]
        handler = "Toggle Switch" if kind == "toggle" else "Show Overlay"
        popup = {"selector": "fx editor choice menu", "number": "fx editor number menu"}.get(kind, "")
        actions = [action(trigger, handler, popup) for trigger in ("Mouse Down", "Enter Pressed")]
    return {
        "version": 2,
        "actions": actions,
        "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "ff202a44", "image": ""},
                           "unfocussed": {"version": 1, "colour": "ff151a2d", "image": ""}},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": children,
    }


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
    size = MASTER_OUTPUT_WIDGETS["Master Peak Meters"][2:]
    plate = Image.new("RGB", size, "#151a2d")
    draw = ImageDraw.Draw(plate)
    font = semibold(15)
    for caption, y in (("0", 10), ("-12", 26), ("-24", 45), ("-48", 84)):
        draw.text((19, y), caption, fill="#c3cbdf", font=font, anchor="mm")
    for channel, x in (("L", 51), ("R", 81)):
        draw.text((x, 113), channel, fill="#e6e6e6", font=font, anchor="mm")
    plate.save(directory / PEAK_SCALE_IMAGE)


def write_master_panel_art(directory):
    font = semibold(19)
    for i, (name, (_, _, width, height)) in enumerate(MASTER_PANELS, 1):
        for state, border in (("on", "#50c1ef"), ("off", "#3a456c")):
            image = Image.new("RGB", (width, height), "#151a2d")
            draw = ImageDraw.Draw(image)
            draw.rounded_rectangle((1, 1, width - 2, height - 2), radius=5, outline=border, width=2)
            draw.text((width / 2, height / 2), name, font=font, fill="#e6e6e6", anchor="mm")
            image.save(directory / f"master_panel_{i}_{state}.png")


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

    def text(value, rect, colour="ffe6e6e6"):
        label = _label(False, rect)
        label["componentData"]["name"] = value
        data = label["componentData"]["data"]
        data.update({"type": "Text", "text": value, "handleName": ""})
        data["textStyle"].update(colour=colour)
        data["textStyle"]["font"]["height"] = 15.0
        return label

    def panel(children, colour="ff151a2d", actions=(), image=""):
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

    voice_bar = _child("voice bar", "Meter", meter_data("Right", hold=False), (6, 6, 226, 32))
    yield add("Master Voice Meter", "masterNativeVoiceMeter",
              panel([voice_bar, _label(False, (238, 0, 138, 44))]),
              (("Peak", n.MASTER_VOICE_RATIO), ("Data", n.MASTER_VOICE_COUNT)))

    limiter_type = standard_widget("toggle", *MASTER_OUTPUT_WIDGETS["Master Output Limiter"][2:])
    limiter = add("Master Output Limiter", limiter_type, None,
                  (("Data", n.MASTER_LIMITER),), focus=True)
    limiter["handle remapping"]["map"].append(
        {"key": "Caption", "value": f"WithName[LIMIT]/Parameter {base + n.MASTER_LIMITER}"})
    yield limiter

    for active, colour, text_colour in ((0, "ff151a2d", "ffc3cbdf"), (1, "ff9d2236", "ffffffff")):
        suffix = "On" if active else "Off"
        actions = [{"version": 1, "onAction": trigger, "handler": "Toggle Switch",
                    "handleName": "Data", "additionalData": ""} for trigger in ("Mouse Down", "Enter Pressed")]
        yield add("Master Clip " + suffix, "masterNativeClip" + suffix,
                  panel([text("CLIP", (0, 0, 100, 56), text_colour)], colour, actions),
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
        components.append(component)
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
