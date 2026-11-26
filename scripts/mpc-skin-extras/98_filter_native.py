# SynthyBoy - native controls of the Filter 60/40 Split + Expanded + Curve + Mixer PEEK views.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import json
from pathlib import Path
import sys

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from components import style
from components.native.active_text import active_text
from components.native.button import button as ui_button
from components.native.level import level as ui_level
from components.native.name_value import name_value as ui_name_value
from components.native.pan import pan as ui_pan
from components.native.radio_button import radio_button as ui_radio
from components.native.selector import selector as ui_selector
from native_skin_font import semibold
from native_ui_params import (FILTER_CHAIN_MENU, FILTER_STATE_OFFSET, FX_DOMAIN_OFFSET, LEGACY_EXTRAS, MIXER_PEEK_VIEW,
                              PZ_ACTIONS, PZ_AVAILABILITY, PZ_SOURCE)


CONTRACT = Path(__file__).with_name("filter-native-contract.json")
FX_CONTRACT = CONTRACT.with_name("fx-native-contract.json")
STATE_OFFSET = LEGACY_EXTRAS + FILTER_STATE_OFFSET
NATIVE = "Filter Native "
FIELDS = (
    "Power", "Cutoff", "Reso", "Drive", "Mix", "Keytrack", "Env 1", "Var", "Comb", "Blend", "Routing",
    "X", "Y", "Transpose", "F Reso", "Spread", "Clean", "Model", "Style",
    "Point", "X", "Y", "Curve", "Grid X", "Grid Y", "Snap", "Legato",
)
SELECT = ("F1", "F2", "NOTE", "VELO")
CURVE_ACTIONS = ("Init", "Undo", "Delete Point", "Targets")
CHAIN_CHOICES = ("F1", "F2", "Parallel", "F1 to F2", "F2 to F1")
POWER = 0
ROUTE = 10
MODEL, STYLE = 17, 18
POINT = 19
TOGGLES = (0, 6, 16, 25, 26)
BIPOLAR = (5, 9, 13, 15)
ACTION = len(FIELDS)
PZ_MODEL, PZ_ACTION = 21, 6
EXPAND_FILTER_ACTION, CURVE_ACTION_BASE, EXPAND_CURVE_ACTION = 7, 8, 12
FILTER_ACTION_COUNT = EXPAND_CURVE_ACTION + 1
STYLE_DOMAIN = 21  # Native FX domains share their declared MODEL and STYLE lists with Filter.
DIAL_SIZE = style.size("filter.layout.DIAL_SIZE.60")
FILTER_KEYS = ("on", "cutoff", "resonance", "drive", "mix", "keytrack", "env1", "var", "comb_offset", "blend", None,
               "formant.x", "formant.y", "formant.transpose", "formant.resonance", "formant.spread", "clean_mode", "model", "style")


def _entry(name, kind, data, rect, parameter, gates, focus=False):
    bounds = {
        "version": 2 if gates else 1,
        "acceptsHWFocus": "Yes" if focus else "No",
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "%d %d %d %d" % tuple(rect),
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    remaps = [{"key": "Data", "value": f"Parameter {parameter}"}] if parameter is not None and parameter > 0 else []
    return {
        "version": 2,
        "componentData": {"version": 1, "name": NATIVE + name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": remaps},
        "bounds": bounds,
    }


def _label(name, text, rect, parameter, gates, *, height=style.size("filter.label.font"), colour=None, align="horizontallyCentred"):
    child = ui_name_value._label_component(
        NATIVE + name, "Value" if text is None else "Text", "Data" if text is None else "",
        rect, height, colour or style.argb("text.icon"), f"{align} verticallyCentred")
    if text is not None:
        child["componentData"]["data"]["text"] = text
    instance = _entry(name, "Label", child["componentData"]["data"], rect,
                      parameter if text is None else 0, gates)
    return instance


def _image(name, file, rect, gates=()):
    data = {"version": 2, "imageType": "Regular", "image": file, "colour": style.native_color("transparent")}
    return _entry(name, "Image", data, rect, 0, gates)


def _button(name, rect, parameter, gates, index=1, count=1, art=("toggle_on.png", "toggle_off.png"), focus=True):
    entry = ui_button.button(NATIVE + name, rect, art[0], art[1], index=index, count=count,
                             remaps=[("Data", f"Parameter {parameter}")], gates=gates)
    entry["bounds"]["acceptsHWFocus"] = "Yes" if focus else "No"
    return entry


def _hidden_hit(name, rect, action, action_index, gates):
    return _button(name, rect, action, gates, action_index, FILTER_ACTION_COUNT, (ui_button.TRANSPARENT_HIT,) * 2)


def _film(name, file, rect, parameter, gates, frames):
    data = {"version": 2, "imageType": "FilmStrip", "image": file, "colour": style.native_color("transparent"),
            "numFrames": frames, "handleName": "Data"}
    return _entry(name, "Image", data, rect, parameter, gates)


def _ensure_dial_assets(skins_dir, frame_size=DIAL_SIZE):
    uni_on = ui_level.synthyboy_dial_data(skins_dir, frame_size=frame_size, bipolar=False, active=True)
    bi_on = ui_pan.synthyboy_dial_data(skins_dir, frame_size=frame_size, bipolar=True)

    return {"uni_on": uni_on, "bi_on": bi_on}


def _write_action_slot_art(skins_dir, key, size, *, icon=None, text=None, colour=None):
    w, h = size
    fill = colour or style.color("text.icon")
    label = text
    if label == "DELETE POINT" and w < 141:
        label = "DEL POINT"
    art = ui_button.in_slot(ui_button.action_art(icon=icon, text=label, width=None if icon else max(style.size("filter.write_action_slot_art.width.48"), w - style.size("filter.write_action_slot_art.width.4")), colour=fill), (w, h))
    filename = f"filter_native_{key}_{w}x{h}.png"
    art.save(skins_dir / filename)
    return filename


def _ensure_power_art(skins_dir):
    files = {}
    for state_key, colour_key in (("off", "accent.red"), ("on", "accent.green")):
        art = ui_button.in_slot(ui_button.action_art(icon="power", colour=style.color(colour_key)), (style.size("filter.ensure_power_art.in_slot.56"), style.size("filter.ensure_power_art.in_slot.56")))
        filename = f"filter_native_power_{state_key}.png"
        art.save(skins_dir / filename)
        files[state_key] = filename
    return files["on"], files["off"]


def _state_gates(state, frames):
    a, b = frames
    return [f"IndexedEnabling/{a}/5/Parameter {state}" if a == b else f"RangedEnabling/{a}/{b}/5/Parameter {state}"]


def _runs(values):
    values = sorted(values)
    start = end = values[0]
    for value in values[1:]:
        if value != end + 1:
            yield start, end
            start = value
        end = value
    yield start, end


def _choice_definition(tui, ctx, name, width, height, *, active=True):
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    source = next(d["value"] for d in defs if d["key"] == "fxNativeCellChoice")
    popup = next(action["additionalData"] for action in source["actions"] if action.get("handler") == "Show Overlay")
    definition = ui_selector.selector_definition(
        source, popup, (width, height), ctx.skins_dir,
        caption_text="MODEL" if "Model" in name else "STYLE",
        filename=f"filter_selector_{width}x{height}.png", label_height=style.size("filter.choice_definition.caption_height"),
    )
    if not active:
        definition["actions"] = []
        definition["ignoreMousePresses"] = True
        definition["disableCoarseDataWheel"] = True
        for child in definition["componentsData"]:
            data = child["componentData"]["data"]
            if "textStyle" in data:
                data["textStyle"]["colour"] = style.argb("smooth.value.off")
    ctx.add_component_definition(name, definition)


def _readout_label(name, text, rect, handle="", colour=None, align="horizontallyCentred"):
    label = _label(name, text, rect, 0, (), height=style.size("filter.readout.font"), colour=colour or style.argb("text.icon"), align=align)
    label["handle remapping"]["map"] = []
    label["componentData"]["data"]["handleName"] = handle
    return label


def _readout_panel(children, background, image="", *, ignore_mouse=True):
    colour = {"version": 1, "colour": background, "image": image}
    return {"version": 2, "componentsData": children, "actions": [], "ignoreMousePresses": ignore_mouse,
            "disableCoarseDataWheel": True,
            "backgroundData": {"version": 1, "focussed": colour, "unfocussed": colour}}


def _readouts(ctx, manifest, state, power_frame):
    # Native Value labels already carry host-formatted units (including PAN's
    # centre text). Do not append a second % or dB column.
    rows = (("DEST", "destination", ""), ("BUS 1", "bus1_send", ""), ("BUS 2", "bus2_send", ""),
            ("LEVEL", "level", ""), ("PAN", "pan", ""))
    out = manifest["out"]
    _, _, w, h = out
    for on in (False, True):
        title = "filterNativeOutOn" if on else "filterNativeOutOff"
        fg = style.argb("text.icon" if on else "text.status.off")
        dim = style.argb("text.caption" if on else "smooth.value.off")
        background = style.argb("button.surface" if on else "menu.background")
        labels = []
        for i, (caption, _, unit) in enumerate(rows):
            y, bottom = i * h // len(rows), (i + 1) * h // len(rows)
            unit_w = style.size("filter.readouts.unit_width") if unit else 0
            labels.append(_readout_label(caption + " caption", caption,
                                         (style.size("filter.readout.caption_x"), y,
                                          style.size("filter.readout.value_x") - style.size("filter.readout.caption_x"), bottom - y),
                                         colour=dim, align="left"))
            labels.append(_readout_label(caption + " value", None,
                                         (style.size("filter.readout.value_x"), y,
                                          w - style.size("filter.readout.value_x") - style.size("filter.readout.right_pad") - unit_w, bottom - y),
                                         f"Readout{i}", fg, "right"))
        ctx.add_component_definition(title, _readout_panel(labels, background))
        for f in (1, 2):
            gates = _state_gates(state, (f, f)) + [f"RangedEnabling/{2 * on}/{2 * on + 1}/4/Parameter {power_frame}"]
            item = _entry(f"OUT F{f} {'on' if on else 'off'}", title, {"version": 1, "handleName": "Data"}, out, 0, gates)
            item["handle remapping"]["map"] = [
                {"key": f"Readout{i}", "value": f"Parameter {ctx.param_index(f'filter{f}.{key}')}"}
                for i, (_, key, _) in enumerate(rows)
            ]
            yield item

    glide = manifest["glide"]
    _, _, w, h = glide
    kind = "filterNativeGlide"
    glide_children = [_readout_label("GLIDE", "GLIDE", (0, 0, style.size("filter.readouts.glide_caption_width"), h)),
                      _readout_label("GLIDE value", None, (style.size("filter.readouts.glide_caption_width"), 0, w - style.size("filter.readouts.glide_reserved_width"), h), "Data"),
                      _readout_label("GLIDE unit", "ms", (w - style.size("filter.readouts.glide_unit_width"), 0, style.size("filter.readouts.glide_unit_width"), h))]
    ctx.add_component_definition(kind, _readout_panel(glide_children, style.argb("button.surface")))
    for frame, source in ((3, "note"), (4, "velocity")):
        legato = ctx.param_index(f"curve.{source}.legato")
        gates = _state_gates(state, (frame, frame)) + [f"IndexedEnabling/1/2/Parameter {legato}"]
        yield _entry(f"Glide {source}", kind, {"version": 1, "handleName": "Data"}, glide,
                     ctx.param_index("master.portamento.time"), gates)


def _pz_toolbar(ctx, rects):
    base = ctx.param_count + LEGACY_EXTRAS
    mode = base + PZ_AVAILABILITY
    def picture(label, size, active):
        state = "active" if active else "inactive"
        return ui_button.text_face(label.upper(), size, geometry="filter.pz.button",
                                   surface=f"filter.pz.{state}.surface", border=f"filter.pz.{state}.border",
                                   text="text.icon" if active else "filter.pz.inactive.text")

    x, y, _, height = rects[2]
    right = rects[3][0] + rects[3][2]
    film_name = "pz_draft_tools.png"
    film = Image.new("RGBA", (right - x, height * style.size("filter.pz_toolbar.new.4")))
    children = [_film("PZ draft state", film_name, (x, y, right - x, height), mode, (), 4)]
    for index, (label, rect) in enumerate(zip(("Back", "Reset", "Delete Point", "Apply"), rects), 1):
        gates = ()
        if index < 3:
            name = f"pz_{index}.png"
            picture(label, tuple(rect[2:]), True).save(ctx.skins_dir / name)
        else:
            start = index - 2
            for frame in range(4):
                film.paste(picture(label, tuple(rect[2:]), start <= frame <= start + 1), (rect[0] - x, frame * height))
            gates = (f"RangedEnabling/{start}/{start + 1}/4/Parameter {mode}",)
            name = ui_button.TRANSPARENT_HIT
        button = _button(f"PZ {label}", rect, 0, gates, index, 5, (name, name))
        button["handle remapping"]["map"][0]["value"] = f"WithName[{label}]/FromGesture/Data"
        children.append(button)
    film.save(ctx.skins_dir / film_name)

    kind = "filterNativePzToolbar"
    ctx.add_component_definition(kind, _readout_panel(children, style.native_color("transparent")))
    for source in (1, 2):
        entry = _entry(f"PZ Toolbar F{source}", kind, {"version": 1, "handleName": "Data"},
                       (0, 0, style.size("filter.pz_toolbar.entry.1280"), style.size("filter.pz_toolbar.entry.72")), base + PZ_ACTIONS + source - 1,
                       (f"IndexedEnabling/{source}/3/Parameter {base + PZ_SOURCE}",))
        entry["bounds"]["acceptsHWFocus"] = "Children"
        yield entry


def _ensure_dial_definition(ctx, w, h, bipolar, active, dial_assets, side_layout):
    dial_size = style.size("height.medium") if side_layout else DIAL_SIZE
    layout_tag = "side" if (side_layout or w >= style.size("filter.ensure_dial_definition.side_threshold")) else "top"
    kind = f"filterNativeDial_{layout_tag}_{'bi' if bipolar else 'uni'}_{'on' if active else 'off'}_{w}x{h}"
    defs = ctx._filter_dial_defs if hasattr(ctx, "_filter_dial_defs") else setattr(ctx, "_filter_dial_defs", set()) or ctx._filter_dial_defs
    if kind in defs:
        return kind
    defs.add(kind)

    if layout_tag == "side":
        dial_rect = (style.size("filter.ensure_dial_definition.padding"), (h - dial_size) // 2, dial_size, dial_size)
        tx = style.size("filter.ensure_dial_definition.padding") + dial_size + style.size("filter.ensure_dial_definition.gap")
        tw = max(style.size("filter.ensure_dial_definition.min_label_width"), w - tx - style.size("filter.ensure_dial_definition.end_padding"))
        cap_rect = (tx, max(style.size("filter.ensure_dial_definition.padding"), h // 2 - style.size("filter.ensure_dial_definition.vertical_gap")), tw, style.size("filter.ensure_dial_definition.caption_height"))
        val_rect = (tx, max(style.size("filter.ensure_dial_definition.vertical_gap"), h // 2 - style.size("filter.ensure_dial_definition.end_padding")), tw, style.size("filter.ensure_dial_definition.value_height"))
        align = "left"
    else:
        dial_rect = ((w - DIAL_SIZE) // 2, style.size("filter.ensure_dial_definition.knob_y"), DIAL_SIZE, DIAL_SIZE)
        cap_rect = (style.size("filter.ensure_dial_definition.end_padding"), style.size("filter.ensure_dial_definition.caption_y"), max(style.size("filter.ensure_dial_definition.top_min_width"), w - style.size("filter.ensure_dial_definition.padding")), style.size("filter.ensure_dial_definition.top_caption_height"))
        val_rect = (style.size("filter.ensure_dial_definition.end_padding"), style.size("filter.ensure_dial_definition.value_y"), max(style.size("filter.ensure_dial_definition.top_min_width"), w - style.size("filter.ensure_dial_definition.padding")), style.size("filter.ensure_dial_definition.vertical_gap"))
        align = "horizontallyCentred"

    # Compose the shared Level/Pan control, changing only this screen's layout.
    factory = ui_pan.pan_definition if bipolar else ui_level.level_definition
    definition = factory(skins_dir=ctx.skins_dir)
    children = definition["componentsData"]
    for child in children:
        data = child["componentData"]
        name = data["name"]
        if name == "knob":
            child["bounds"]["bounds"] = "%d %d %d %d" % dial_rect
            data["data"] = copy.deepcopy(dial_assets["bi_on" if bipolar else "uni_on"])
        elif name in ("name", "value"):
            child["bounds"]["bounds"] = "%d %d %d %d" % (cap_rect if name == "name" else val_rect)
            ts = data["data"]["textStyle"]
            ts["font"]["height"] = style.size("filter.ensure_dial_definition.caption_font") if name == "name" else style.size("filter.ensure_dial_definition.value_font")
            ts["justification"] = align + " verticallyCentred"
            ts["colour"] = style.argb(("text.caption" if name == "name" else "text.icon") if active else "smooth.value.off")
    transparent = {"version": 1, "colour": style.native_color("transparent"), "image": ""}
    definition.update(actions=[], ignoreMousePresses=not active, disableCoarseDataWheel=not active,
                      backgroundData={"version": 1, "focussed": transparent, "unfocussed": transparent})
    definition = style.apply_control_state(definition, ctx.skins_dir, active=active)
    ctx.add_component_definition(kind, definition)
    return kind


def _emit_dial(ctx, container, name, rect, param, caption, bipolar, active_gates, passive_gates, dial_assets, side_layout, passive_bindings=(), model_proxy=None):
    _, _, w, h = rect
    remaps = [
        {"key": "Data", "value": f"Parameter {param}"},
        {"key": "Caption", "value": f"WithName[{caption}]/Parameter {param}"},
    ]
    on_kind = _ensure_dial_definition(ctx, w, h, bipolar, True, dial_assets, side_layout)
    on_entry = _entry(name, on_kind, {"version": 1, "handleName": "Data"}, rect, 0, active_gates, True)
    on_entry["handle remapping"]["map"] = copy.deepcopy(remaps)
    container.append(on_entry)

    if passive_gates:
        off_kind = _ensure_dial_definition(ctx, w, h, bipolar, False, dial_assets, side_layout)
        for suffix, raw_parameter, selection_gate, raw_model in passive_bindings:
            off_entry = _entry(name + suffix + " off", off_kind, {"version": 1, "handleName": "Data"}, rect, 0,
                               [*(gate.replace(f"Parameter {model_proxy}", f"Parameter {raw_model}") for gate in passive_gates), selection_gate], False)
            off_entry["handle remapping"]["map"] = [
                {"key": "Data", "value": f"Parameter {raw_parameter}"},
                {"key": "Caption", "value": f"WithName[{caption}]/Parameter {raw_parameter}"},
            ]
            container.append(off_entry)


def _emit_toggle(ctx, container, name, rect, param, caption, active_gates, passive_gates, skins_dir, *, model_proxy=None, passive_models=()):
    x, y, w, h = rect
    tw, th = min(w, style.size("filter.emit_toggle.max_width")), min(h, style.size("filter.emit_toggle.max_height"))
    tx, ty = x + (w - tw) // 2, y + (h - th) // 2
    key = caption.lower().replace(" ", "_")
    on_name = f"filter_native_toggle_{key}_{tw}x{th}_on.png"
    off_name = f"filter_native_toggle_{key}_{tw}x{th}_off.png"
    active_text.write_active_text_assets(skins_dir, text=caption, width=tw, height=th, on_name=on_name, off_name=off_name)
    btn = active_text.active_text_entry(NATIVE + name, on_name, off_name, (tx, ty, tw, th), handle="Data", gates=active_gates)
    btn["handle remapping"]["map"] = [{"key": "Data", "value": f"Parameter {param}"}]
    btn["bounds"]["acceptsHWFocus"] = "Yes"
    btn["bounds"]["showWhenDataModelInvalid"] = "Hide"
    container.append(btn)
    if passive_gates:
        for suffix, selection_gate, raw_model, raw_parameter in passive_models:
            gates = [*(gate.replace(f"Parameter {model_proxy}", f"Parameter {raw_model}") for gate in passive_gates), selection_gate]
            passive = active_text.active_text_entry(NATIVE + name + suffix + " off", on_name, off_name,
                                                     (tx, ty, tw, th), handle="Data", gates=gates)
            passive["handle remapping"]["map"] = [{"key": "Data", "value": f"Parameter {raw_parameter}"}]
            container.append(style.set_leaf_control_active(passive, ctx, active=False))


def _register_filter_chain(ctx, tui, f2_state):
    kind = "filterNativeChain"
    route = ctx.param_index("filter.routing")
    definitions = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    menu = copy.deepcopy(next(d["value"] for d in definitions if d["key"] == "fx editor choice menu"))
    popup = "filterNativeChainChoices"
    marks = [(f2_state, (True, True, False, False)), (f2_state, (False, False, True, True))]
    marks.extend((route, tuple(frame == index for frame in range(3))) for index in range(3))
    ui_selector.append_choice_rows(ctx, menu, CHAIN_CHOICES, "filter_chain", header_title="FILTER CHAIN", marks=marks)
    ctx.add_component_definition(popup, menu)
    ctx.add_component_definition(kind, ui_selector.filter_chain_definition(ctx.skins_dir, popup))
    shared = [("Edited", f"Parameter {f2_state}")]
    shared.extend((f"Power{f}", f"Parameter {ctx.param_index(f'filter{f}.on')}") for f in (1, 2))
    first = ctx.param_count + LEGACY_EXTRAS + FILTER_CHAIN_MENU
    for index in range(3):
        labels = [label + (" ✓" if row == index + 2 else "") for row, label in enumerate(CHAIN_CHOICES)]
        ui_selector.ensure_routing_replacement_strings(tui, [first + index], labels)

    def instances(name, x, y, height, gates=()):
        geometry = style.size("filter.chain")
        rect = (x, y + (height - geometry["height"]) // 2, geometry["width"], geometry["height"])
        for index in range(3):
            gate = f"IndexedEnabling/{index}/3/Parameter {route}"
            yield ui_button.entry(NATIVE + name + (f" {index}" if index else ""), kind, {"version": 1, "handleName": "Data"},
                                  rect, [("Data", f"Parameter {first + index}"), *shared], (*gates, gate), focus="Yes")
    return instances


def _ensure_nv_definition(ctx, w, h):
    # Integer row division produces 119..121px cells. A common 119px subtree
    # preserves the full-height touch row without serialising three copies.
    if h == style.size("filter.ensure_nv_definition.row_height"):
        w = style.size("filter.ensure_nv_definition.min_width")
    bw = style.size("filter.ensure_nv_definition.box_width")
    kind = f"filterNativeNv_{w}x{h}"
    defs = ctx._filter_nv_defs if hasattr(ctx, "_filter_nv_defs") else setattr(ctx, "_filter_nv_defs", set()) or ctx._filter_nv_defs
    if kind in defs:
        return kind
    defs.add(kind)
    defn = ui_name_value.build_name_value_definition(
        name=kind, rect=(0, 0, w, h), skins_dir=ctx.skins_dir, box_width=bw,
        box_height=style.size("height.small"),
        handle="Data", caption_handle="Caption",
    )
    ctx.add_component_definition(kind, defn)
    return kind


def _emit_curve_controls(ctx, container, rects, base, action, state, displays, *, is_split, expand_rect=None):
    split_or_curve_gates = [] if is_split else _state_gates(state, (3, 4))
    for index, rect in enumerate(rects):
        if rect is None:
            continue
        slot = POINT + index
        x, y, w, h = rect
        if slot < ACTION:
            field_name = FIELDS[slot]
            host_value = base + slot
            if slot == ACTION - 1:  # Legato
                prefix = "Split " if is_split else ""
                _emit_toggle(ctx, container, f"{prefix}Curve {field_name}", rect, host_value, "LEGATO",
                             split_or_curve_gates, (), ctx.skins_dir)
            elif slot in TOGGLES:
                prefix = "Split " if is_split else ""
                _emit_toggle(ctx, container, f"{prefix}Curve {field_name}", rect, host_value, field_name.upper(),
                             split_or_curve_gates, (), ctx.skins_dir)
            else:
                kind = _ensure_nv_definition(ctx, w, h)
                prefix = "Split " if is_split else ""
                container.append(ui_name_value.create_name_value_entry(
                    NATIVE + f"{prefix}Curve {field_name}", kind, rect,
                    data_handle=f"Parameter {host_value}",
                    caption_handle=f"WithName[{field_name.upper()}]/Parameter {host_value}",
                    gates=split_or_curve_gates,
                ))
        else:
            label = CURVE_ACTIONS[slot - ACTION]
            prefix = "Split " if is_split else ""
            art_file = _write_action_slot_art(ctx.skins_dir, f"{prefix.strip().lower()}_{label.lower().replace(' ', '_')}", (w, h), text=label.upper())
            container.append(_button(f"{prefix}{label}", rect, action, split_or_curve_gates,
                                     CURVE_ACTION_BASE + slot - ACTION, FILTER_ACTION_COUNT, (art_file, art_file)))

    if expand_rect is not None:
        ew, eh = expand_rect[2], expand_rect[3]
        exp_art = _write_action_slot_art(ctx.skins_dir, "expand", (ew, eh), icon="expand")
        key = "Split Expand Curve" if is_split else "Collapse Curve"
        container.append(_button(key, expand_rect, action, split_or_curve_gates, EXPAND_CURVE_ACTION, FILTER_ACTION_COUNT, (exp_art, exp_art)))


FILTER_TAB_NAMES = ("Filter", "Filter 2", "Note", "Velo")   # the hardware sub-tabs of the Filter key: the state of the page is the sub-tab's (1, 2: the filter; 3, 4: the NOTE / VELO curve)
CURVE_ENTRIES = ("Filter Native Curve ", "Filter Native Glide ")


def _is_curve_entry(item):
    return item["componentData"]["name"].startswith(CURVE_ENTRIES)


def _distribute(ctx, items, existing):
    """The first tab has made every entry of the page states (the filter ones and the curve ones). Filter and Filter 2 keep the filter entries, Note and Velo the curve entries: a tab carries only
    what its state can show. The markers and the instance picture of every tab stay its own."""
    added = [item for item in items if not any(item is old for old in existing)]
    filter_entries = [item for item in added if not _is_curve_entry(item)]
    curve_entries = [item for item in added if _is_curve_entry(item)]
    present = {tab["tabName"] for tab in ctx._tui["pageData"]["tabs"]}
    ordered = {"Filter": filter_entries, "Filter 2": filter_entries, "Note": curve_entries, "Velo": curve_entries}
    for name in FILTER_TAB_NAMES:
        if name not in present:
            continue
        tab_items = ctx.tab(name)["componentDefinition"]["componentsData"]
        wanted = [item for item in ordered[name] if not (name == "Note" and item["componentData"]["name"].endswith("Glide velocity"))
                  and not (name == "Velo" and item["componentData"]["name"].endswith("Glide note"))]
        if name != "Filter":
            tab_items[:] = [*(copy.deepcopy(item) for item in wanted), *tab_items]
            tab_items.sort(key=lambda item: item["componentData"]["name"].startswith(("Overlay Marker ", "Overlay Instance ")))
        else:
            tab_items[:] = [*(item for item in tab_items if item in existing), *wanted]
            tab_items.sort(key=lambda item: item["componentData"]["name"].startswith(("Overlay Marker ", "Overlay Instance ")))


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    if not any(tab.get("tabName") == "Filter" for tab in tui["pageData"]["tabs"]):
        return
    manifest = json.loads(CONTRACT.read_text(encoding="utf-8"))
    items = ctx.tab("Filter")["componentDefinition"]["componentsData"]
    items[:] = [item for item in items if item["componentData"]["name"] not in ("Filter 1 Power", "Filter 2 Power")]
    existing = list(items)   # the page markers and the instance picture of the first sub-tab

    state = ctx.param_count + STATE_OFFSET
    base = state + 1
    action = base + ACTION
    displays = action + 1
    chain = _register_filter_chain(ctx, tui, displays + 1)
    note_ind = displays + 2
    velo_ind = displays + 3
    power_frame = displays + 4
    domain_base = ctx.param_count + LEGACY_EXTRAS + FX_DOMAIN_OFFSET
    ui_button.write_transparent_hit(ctx.skins_dir)

    dial_assets = _ensure_dial_assets(ctx.skins_dir)
    power_art = _ensure_power_art(ctx.skins_dir)
    domains = json.loads(FX_CONTRACT.read_text(encoding="utf-8"))["domains"]

    items.extend(_pz_toolbar(ctx, manifest["pz_toolbar"]))

    # Gates distinguishing the Filter 1 / Filter 2 sub-tabs (state 1, 2) from the Note / Velo sub-tabs (state 3, 4)
    expanded_filter_gates = _state_gates(state, (1, 2))
    expanded_curve_gates = _state_gates(state, (3, 4))

    # =========================================================================
    # 1. Expanded Filter / Mixer PEEK shared surface (filterNativeEditorSurface)
    # =========================================================================
    filter_items = []
    for readout in _readouts(ctx, manifest, state, power_frame):
        if readout["componentData"]["type"] == "filterNativeGlide":
            items.append(readout)
        else:
            filter_items.append(readout)

    model = base + MODEL
    enabled = f"RangedEnabling/2/3/4/Parameter {power_frame}"
    passive = f"RangedEnabling/0/1/4/Parameter {power_frame}"
    filter_items.append(_button("Power", manifest["filter"][0][POWER], base + POWER, (), art=power_art))

    for slot, word in ((MODEL, "Model"), (STYLE, "Style")):
        rect = manifest["filter"][0][slot]
        definition = "filterNative" + word
        _choice_definition(tui, ctx, definition, *rect[2:], active=True)
        off_def = "filterNative" + word + "Off"
        _choice_definition(tui, ctx, off_def, *rect[2:], active=False)
        models = range(27) if slot == STYLE else (None,)
        for model_id in models:
            if model_id in manifest["one_style"]:
                continue
            gates = [enabled]
            if model_id is not None:
                gates.append(f"IndexedEnabling/{model_id}/27/Parameter {model}")
            parameter = domain_base + (STYLE_DOMAIN + model_id if model_id is not None else 0)
            name = word if model_id is None else f"{word} {model_id}"
            filter_items.append(_entry(name, definition, {"version": 1, "handleName": "Data"}, rect, parameter, gates, True))
        for f in (1, 2):
            filter_items.append(_entry(f"{word} F{f} off", off_def, {"version": 1, "handleName": "Data"}, rect,
                                       ctx.param_index(f"filter{f}.{FILTER_KEYS[slot]}"),
                                       [passive, f"IndexedEnabling/{f}/5/Parameter {state}"]))

    style_x, style_y, style_w, style_h = manifest["filter"][0][STYLE]
    for model_id in manifest["one_style"]:
        gates = [enabled, f"IndexedEnabling/{model_id}/27/Parameter {model}"]
        filter_items.append(_label(f"Single Style {model_id}", domains[STYLE_DOMAIN + model_id][0],
                                   (style_x, style_y, style_w, style_h), model, gates))

    for slot in range(1, POINT):
        if slot in (ROUTE, MODEL, STYLE):
            continue
        groups = {}
        for model_id in range(27):
            variant = 1 if model_id == 5 else 2 if model_id == 6 else 0
            rect = manifest["filter"][variant][slot]
            if rect is not None:
                groups.setdefault(tuple(rect), []).append(model_id)
        for rect, models in groups.items():
            for start, end in _runs(models):
                host_value = base + slot
                name = f"{FIELDS[slot]} {start}"
                model_gate = [] if (start, end) == (0, 26) else [f"RangedEnabling/{start}/{end}/27/Parameter {model}"]
                active_gates = [enabled, *model_gate]
                passive_gates = [passive, *model_gate]
                if slot in TOGGLES:
                    _emit_toggle(ctx, filter_items, name, rect, host_value, FIELDS[slot].upper(),
                                 active_gates, passive_gates, ctx.skins_dir, model_proxy=model,
                                 passive_models=[(f" F{f}", f"IndexedEnabling/{f}/5/Parameter {state}",
                                                  ctx.param_index(f"filter{f}.model"), ctx.param_index(f"filter{f}.{FILTER_KEYS[slot]}")) for f in (1, 2)])
                else:
                    _emit_dial(ctx, filter_items, name, rect, host_value, FIELDS[slot].upper(),
                               slot in BIPOLAR, active_gates, passive_gates, dial_assets, side_layout=False,
                               passive_bindings=[(f" F{f}", ctx.param_index(f"filter{f}.{FILTER_KEYS[slot]}"),
                                                  f"IndexedEnabling/{f}/5/Parameter {state}", ctx.param_index(f"filter{f}.model"))
                                                 for f in (1, 2)], model_proxy=model)

    kind = "filterNativeEditorSurface"
    transparent = {"version": 1, "colour": style.native_color("transparent"), "image": ""}
    ctx.add_component_definition(kind, {
        "version": 2, "actions": [], "ignoreMousePresses": True, "disableCoarseDataWheel": True,
        "backgroundData": {"version": 1, "focussed": transparent, "unfocussed": transparent},
        "componentsData": filter_items,
    })
    data = {"version": 1, "handleName": "Data"}
    surface = _entry("Editor Surface", kind, data, (0, 0, style.size("filter.apply.entry.1280"), style.size("filter.apply.entry.628")), state, expanded_filter_gates)
    surface["bounds"]["acceptsHWFocus"] = "Children"
    items.insert(0, surface)

    mixer = next((t for t in tui["pageData"]["tabs"] if t["tabName"] in ("Mix", "Mixer")), None)
    if mixer is not None:
        # Docked Mixer PEEK editor occupies 0..564 above the persistent bottom 6-tab bar (y=566..622).
        import copy as _copy
        docked_items = []
        docked_h = style.size("filter.apply.docked_h.564")
        middle_start, bottom_start, middle_height, second_row = style.size("filter.dock.thresholds")
        tab_shift = style.size("filter.apply.tab_shift.628") - docked_h
        for comp in filter_items:
            c = _copy.deepcopy(comp)
            cx, cy, cw, ch = map(int, c["bounds"]["bounds"].split())
            if cy >= bottom_start:
                cy -= tab_shift
            elif middle_start <= cy < bottom_start and ch >= middle_height:
                # Formant 2x2 middle block shrinks vertically to match FilterDisplay::layout() at 564px
                mid_y = style.size("filter.apply.mid_y.76")
                band_top = style.size("filter.apply.band_top.384") - style.size("filter.apply.band_top.8")
                block_top = mid_y + style.size("filter.apply.block_top.18") + style.size("filter.apply.block_top.6")
                cell_h = max(style.size("filter.apply.cell_h.48"), (band_top - mid_y - style.size("filter.apply.cell_h.30") - style.size("filter.apply.cell_h.6")) // 2)
                row_idx = 0 if cy < second_row else 1
                cy = block_top + row_idx * (cell_h + 6)
                ch = min(ch, cell_h)
            c["bounds"]["bounds"] = f"{cx} {cy} {cw} {ch}"
            docked_items.append(c)
        vx, vy, _, vh = manifest["view"]
        docked_items.extend(chain("Peek Chain", vx + style.size("filter.chain")["peek_offset"], vy, vh))
        dock_kind = "mixerPeekFilterEditorSurface"
        ctx.add_component_definition(dock_kind, {
            "version": 2, "actions": [], "ignoreMousePresses": True, "disableCoarseDataWheel": True,
            "backgroundData": {"version": 1, "focussed": transparent, "unfocussed": transparent},
            "componentsData": docked_items,
        })
        view = ctx.param_count + LEGACY_EXTRAS + MIXER_PEEK_VIEW
        peek = _entry("Peek Filter", dock_kind, data, (0, 0, style.size("filter.apply.entry.1280"), docked_h),
                      view, (f"IndexedEnabling/1/3/Parameter {view}", *_state_gates(state, (1, 2))))
        peek["componentData"]["name"] = "Mixer Native Peek Filter"
        peek["bounds"]["acceptsHWFocus"] = "Children"
        mix_items = mixer["componentDefinition"]["componentsData"]
        first_marker = next((i for i, item in enumerate(mix_items)
                            if item["componentData"]["name"].startswith("Overlay ")), len(mix_items))
        mix_items.insert(first_marker, peek)

    # Expanded top bar, graph corner Expand and PZ Edit
    vx, vy, vw, vh = manifest["view"]
    items.extend(chain("Chain", vx, vy, vh, expanded_filter_gates))
    both = manifest["both"]
    dual_frames = [("F1+F2", bool(frame & 1)) for frame in range(4)]
    art = ui_radio.write_segmented_strip(ctx.skins_dir / "filter_native_dual.png", both[2:], dual_frames)
    items.extend((_film("dual art", art, both, displays + 4, expanded_filter_gates, 4),
                  _hidden_hit("dual", both, action, 5, expanded_filter_gates)))

    exp_rect = manifest["expand_filter"]
    exp_art = _write_action_slot_art(ctx.skins_dir, "expand", tuple(exp_rect[2:]), icon="expand")

    pz_rect = manifest["pz_expand"]
    pz_gates = [*expanded_filter_gates, enabled, f"IndexedEnabling/{PZ_MODEL}/27/Parameter {model}"]
    launcher = _button("Edit PZ Curve", pz_rect, action, pz_gates, PZ_ACTION, FILTER_ACTION_COUNT, (exp_art, exp_art))
    launcher["handle remapping"]["map"][0]["value"] = "WithName[Edit PZ Curve]/FromGesture/" + launcher["handle remapping"]["map"][0]["value"]
    items.append(launcher)

    # =========================================================================
    # 2. Expanded Curve surface (filterNativeCurveSurface)
    # =========================================================================
    curve_items = []
    _emit_curve_controls(ctx, curve_items, manifest["curve"], base, action, state, displays,
                         is_split=False, expand_rect=None)
    curve_kind = "filterNativeCurveSurface"
    ctx.add_component_definition(curve_kind, {
        "version": 2, "actions": [], "ignoreMousePresses": True, "disableCoarseDataWheel": True,
        "backgroundData": {"version": 1, "focussed": transparent, "unfocussed": transparent},
        "componentsData": curve_items,
    })
    curve_surface = _entry("Curve Surface", curve_kind, data, (0, 0, style.size("filter.apply.entry.1280"), style.size("filter.apply.entry.628")), state, expanded_curve_gates)
    curve_surface["bounds"]["acceptsHWFocus"] = "Children"
    items.append(curve_surface)

    items.sort(key=lambda item: item["componentData"]["name"].startswith(("Overlay Marker ", "Overlay Instance ")))
    _distribute(ctx, items, existing)
