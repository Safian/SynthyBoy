# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Host controls for the standalone Env page. The graph and routing stay in EnvUi.

Uses public native component factories (Level, ActiveText, RadioButton, Button)
with no local image drawing. Idempotent: a second invocation adds nothing.
"""
import sys as _token_sys
from pathlib import Path as _TokenPath
_token_root = str(_TokenPath(__file__).resolve().parents[2])
if _token_root not in _token_sys.path:
    _token_sys.path.insert(0, _token_root)
from components import style as _ui_style


import copy
import re
from pathlib import Path

from components.native.button.button import TRANSPARENT_HIT, button as native_button, write_transparent_hit

from components.native.pan.pan import synthyboy_dial_data as pan_dial_data
from components.native.level.level import compact_dial_definition, synthyboy_dial_data, level_definition
from components.native.active_text.active_text import (
    active_text_definition,
    active_text_entry,
    render_active_text_art,
)
from components.native.radio_button.radio_button import (
    write_segmented_strip,
)
from components import style
import native_ui_params as handles
from native_ui_params import ENV_SELECTOR_CELLS, ENV_SELECTOR_LAST_FRAME, env_selector_base


# Tab coordinates matching EnvFullScreen::layout (screen y minus 110).
CELLS = (
    ("Attack", tuple(_ui_style.size('screen.env_native.cells.rect')), "envNativeDial"),
    ("Decay", tuple(_ui_style.size('screen.env_native.cells.rect.2')), "envNativeDial"),
    ("Sustain", tuple(_ui_style.size('screen.env_native.cells.rect.3')), "envNativeDial"),
    ("Release", tuple(_ui_style.size('screen.env_native.cells.rect.4')), "envNativeDial"),
    ("Attack Curve", tuple(_ui_style.size('screen.env_native.cells.rect.5')), "envNativeCurve"),
    ("Decay Curve", tuple(_ui_style.size('screen.env_native.cells.rect.6')), "envNativeCurve"),
    ("Release Curve", tuple(_ui_style.size('screen.env_native.cells.rect.7')), "envNativeCurve"),
    ("Delay", tuple(_ui_style.size('screen.env_native.cells.rect.8')), "envNativeDial"),
    ("Hold", tuple(_ui_style.size('screen.env_native.cells.rect.9')), "envNativeDial"),
    ("Beats", tuple(_ui_style.size('screen.env_native.cells.rect.10')), "envNativeBeatsSwitch"),
    ("Retrig", tuple(_ui_style.size('screen.env_native.cells.rect.11')), "envNativeRetrigSwitch"),
)
SELECTOR_X, SELECTOR_Y, SELECTOR_W, SELECTOR_H = tuple(_ui_style.size('screen.env_native.metric.rect'))
SELECTOR_VARIANTS = 5
SELECTOR_ART = "env_selector_cells.png"


def _bounds(rect, gate_list, focus="Yes"):
    result = {
        "version": 2 if gate_list else 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "%d %d %d %d" % rect,
    }
    if gate_list:
        result["additionalInvalidatingHandles"] = gate_list
    return result


def _entry(name, kind, parameter, caption, rect, gate_list):
    shown_caption = {"Attack Curve": "A CURVE", "Decay Curve": "D CURVE", "Release Curve": "R CURVE"}.get(caption, caption)
    remap = [
        {"key": "Data", "value": f"Parameter {parameter}"},
        {"key": "Caption", "value": f"WithName[{shown_caption}]/Parameter {parameter}"},
    ]
    bounds = _bounds(rect, gate_list)
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": kind,
            "data": {"version": 1, "handleName": "Data"},
        },
        "handle remapping": {"version": 1, "map": remap},
        "bounds": bounds,
    }


def _selector_assets(directory):
    """Build selector artwork using the approved segmented button factory.

    Six slots (ENV 1..6, +) with off/selected in shared gray/cyan.
    Used instances get a text suffix 'N ·'; '+' stays '+'.
    """
    art_path = Path(directory) / SELECTOR_ART
    write_transparent_hit(directory)
    if art_path.is_file():
        return

    frames = [None] * (ENV_SELECTOR_LAST_FRAME + 1)
    for number in range(1, ENV_SELECTOR_CELLS + 1):
        base = 1 + (number - 1) * SELECTOR_VARIANTS
        for variant in range(SELECTOR_VARIANTS):
            # variant 0: off, variant 1: used, variant 2: selected, variant 3: used+selected, variant 4: +
            selected = variant in (2, 3)
            used = variant in (1, 3)
            label = "+" if variant == 4 else str(number)
            if used and variant != 4:
                label = f"{number} ·"
            frames[base + variant] = (label, selected)
    write_segmented_strip(art_path, (SELECTOR_W, SELECTOR_H), frames)


def _selector_entry(name, kind, data, rect, remap, gates, focus="No"):
    bounds = _bounds(rect, gates, focus)
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": [{"key": key, "value": value} for key, value in remap]},
        "bounds": bounds,
    }


def _append_selector(ctx, items, selector_index, gate):
    _selector_assets(ctx.skins_dir)
    frame_count = ENV_SELECTOR_LAST_FRAME + 1
    rect = (0, 0, SELECTOR_W, SELECTOR_H)
    art = {
        "version": 2,
        "imageType": "FilmStrip",
        "colour": "0",
        "image": SELECTOR_ART,
        "numFrames": frame_count,
        "handleName": "Frame",
    }
    children = [_selector_entry("ENV cell artwork", "Image", art, rect, [], [])]
    for index in range(ENV_SELECTOR_CELLS):
        child = native_button(f"Choose ENV {index + 1}", rect, TRANSPARENT_HIT,
                              index=index, count=ENV_SELECTOR_CELLS, gates=[f"Choice {index + 1} visible"])
        child["bounds"]["acceptsHWFocus"] = "No"
        children.append(child)
    empty = {"version": 1, "colour": "0", "image": ""}
    ctx.add_component_definition("envNativeSelectorCell", {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": empty, "unfocussed": empty},
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": children,
    })

    base = env_selector_base(ctx.param_count)
    for slot in range(ENV_SELECTOR_CELLS):
        frame = f"Parameter {base + slot}"
        remap = [
            ("Data", f"Parameter {selector_index}"),
            ("Frame", frame),
        ]
        for index in range(ENV_SELECTOR_CELLS):
            low = 1 + index * SELECTOR_VARIANTS
            high = low + SELECTOR_VARIANTS - 1
            remap.append((
                f"Choice {index + 1} visible",
                f"RangedEnabling/{low}/{high}/{frame_count}/{frame}",
            ))
        cell = (SELECTOR_X + slot * SELECTOR_W, SELECTOR_Y, SELECTOR_W, SELECTOR_H)
        items.append(_selector_entry(
            f"Env Native Selector {slot + 1}", "envNativeSelectorCell",
            {"version": 1, "handleName": "Data"}, cell, remap,
            [gate, f"NonZero/{frame}"],
        ))


def _build_dial_definitions(ctx, definitions, bipolar_data):
    """Build envNativeDial and envNativeCurve definitions using Level factory."""
    rects = {
        "focus": tuple(_ui_style.size('screen.env_native.focus.rect')),
        "knob": tuple(_ui_style.size('screen.env_native.knob.rect')),
        "name": tuple(_ui_style.size('screen.env_native.name.rect')),
        "value": tuple(_ui_style.size('screen.env_native.value.rect')),
    }
    for kind, polarity in (("envNativeDial", "UniPolar"), ("envNativeCurve", "BiPolar")):
        if kind in definitions:
            continue
        dial = compact_dial_definition(
            level_definition(skins_dir=ctx.skins_dir), rects=rects, polarity=polarity
        )
        # Replace the Knob child's data with our FilmStrip factory result
        for item in dial["componentsData"]:
            if item["componentData"]["name"] == "knob":
                if polarity == "UniPolar":
                    film_data = synthyboy_dial_data(ctx.skins_dir, frame_size=_ui_style.size('screen.env_native.film_data.frame_size'), bipolar=False)
                else:
                    film_data = copy.deepcopy(bipolar_data)
                item["componentData"]["data"] = film_data
            elif item["componentData"]["name"] == "name":
                item["componentData"]["data"]["textStyle"]["font"]["height"] = 16
        for state in dial["backgroundData"].values():
            if isinstance(state, dict):
                state["colour"] = style.native_color("transparent")
        # Add popup actions
        dial["actions"].extend({
            "version": 1,
            "onAction": trigger,
            "handler": "Show Overlay",
            "handleName": "Data",
            "additionalData": "fx editor number menu",
        } for trigger in ("Double Click", "Enter Pressed"))
        ctx.add_component_definition(kind, dial)


def _write_active_text_assets(directory, text, width, height, *, on_name, off_name):
    for active, filename in ((True, on_name), (False, off_name)):
        render_active_text_art(text, active, width, height, font_size=style.size("font.caption")).save(Path(directory) / filename)
    return on_name, off_name


def _build_targets_definition(ctx, width, height):
    """Build envNativeTargets using the approved Button API."""
    on_name = "env_targets_on.png"
    render_active_text_art("TARGETS", True, width, height,
                           font_size=style.size("font.caption")).save(Path(ctx.skins_dir) / on_name)
    targets_entry = active_text_entry(
        "Env Native Targets", on_name, on_name,
        (0, 0, width, height),
        handle="Data",
        gates=[],
    )
    # Wrap in a definition with actions
    targets_def = {
        "version": 2,
        "actions": [],
        "backgroundData": {
            "version": 1,
            "focussed": {"version": 1, "colour": "0", "image": ""},
            "unfocussed": {"version": 1, "colour": "0", "image": ""},
        },
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": True,
        "componentsData": [targets_entry],
    }
    ctx.add_component_definition("envNativeTargets", targets_def)


def _install_tab(ctx, tab, bindings, first):
    """The native controls of one Env sub-tab (tab n shows envelope n; the cells follow the plugin's page, which publishes the envelope of its own sub-tab)."""
    gate, selector_index, targets_index, values = bindings
    items = tab["componentDefinition"]["componentsData"]
    # Idempotence: if native controls already present, skip
    if any(c["componentData"]["name"].startswith("Env Native ") for c in items):
        tab["qlinkBoundsData"] = []
        return
    # Remove the static widgets of the generator ("Env 1 Attack" ... of the tab's kick page)
    items[:] = [c for c in items if not re.match(r"Env \d ", c["componentData"]["name"])]

    # 1. The selector strip exists on the first tab only, as the source 98_play_expanded.py takes the strip of the Play page's expanded editor from
    #    (it moves the cells out of the tab: no Env tab keeps a selector, the hardware sub-tab names the envelope).
    if first:
        _append_selector(ctx, items, selector_index, gate)

    # 2. TARGETS button
    items.append(_entry(
        "Env Native Targets", "envNativeTargets",
        targets_index, "TARGETS",
        tuple(_ui_style.size('screen.env_native.apply.rect')), [gate],
    ))

    # 3. The field controls
    for field, (caption, rect, kind) in enumerate(CELLS):
        if caption in ("Beats", "Retrig"):
            kind = "envNativeBeatsSwitch" if caption == "Beats" else "envNativeRetrigSwitch"
        items.append(_entry(f"Env Native {caption}", kind, values + field, caption, rect, [gate]))
    tab["qlinkBoundsData"] = []


def apply(tui, qlinks, ctx):
    """Install native ENV controls on every Env sub-tab. Idempotent: a second invocation adds nothing."""
    if not ctx.meta or ctx.meta.get("variant") != "overlay":
        return
    present = {t["name"] for t in ctx.meta["tabs"]}
    names = [n for n in handles.ENV_TAB_NAMES if n in present]
    if not names:
        return

    base = handles.native_base_index(ctx.param_count)
    gate = f"NonZero/Parameter {base + handles.ENV_VIEW}"
    values = base + handles.ENV_FIELDS

    # Get existing definitions
    definitions = {
        d["key"]: d["value"]
        for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    }

    if "envNativeTargets" not in definitions:
        _build_targets_definition(ctx, 160, 56)

        # Dial definitions (ADSR+Delay+Hold unipolar, curves bipolar)
        bipolar_data = pan_dial_data(ctx.skins_dir, frame_size=_ui_style.size('screen.env_native.data.frame_size'))
        _build_dial_definitions(ctx, definitions, bipolar_data)

        # Switch definitions (BEATS/RETRIG via ActiveText)
        beats_on, beats_off = _write_active_text_assets(
            ctx.skins_dir, text="BEATS", width=_ui_style.size('screen.env_native.apply.width'), height=_ui_style.size('screen.env_native.apply.height'),
            on_name="env_beats_on.png", off_name="env_beats_off.png",
        )
        retrig_on, retrig_off = _write_active_text_assets(
            ctx.skins_dir, text="RETRIG", width=_ui_style.size('screen.env_native.apply.width.2'), height=_ui_style.size('screen.env_native.apply.height'),
            on_name="env_retrig_on.png", off_name="env_retrig_off.png",
        )
        beats_def = active_text_definition(
            "envNativeBeatsSwitch", beats_on, beats_off, tuple(_ui_style.size('screen.env_native.beats_def.size')),
            handle="Data",
        )
        beats_def["ignoreMousePresses"] = True
        ctx.add_component_definition("envNativeBeatsSwitch", beats_def)
        retrig_def = active_text_definition(
            "envNativeRetrigSwitch", retrig_on, retrig_off, tuple(_ui_style.size('screen.env_native.retrig_def.size')),
            handle="Data",
        )
        retrig_def["ignoreMousePresses"] = True
        ctx.add_component_definition("envNativeRetrigSwitch", retrig_def)

        # State strings
        strings = tui["pageData"].setdefault("replacementStrings", [])
        keys_present = {s.get("key") for s in strings}
        for field_offset, choices in ((9, ["MS", "BEATS"]), (10, ["OFF", "ON"])):
            key = f"Parameter {values + field_offset}"
            if key not in keys_present:
                strings.append({"key": key, "value": choices})

    bindings = (gate, base + handles.ENV_SELECTION, base + handles.ENV_TARGETS, values)
    for index, name in enumerate(names):
        _install_tab(ctx, ctx.tab(name), bindings, index == 0)
