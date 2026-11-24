# SynthyBoy - native controls of the Filter 60/40 Split + Expanded + Curve + Mixer PEEK views.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import json
from pathlib import Path
import sys
import tempfile

from PIL import Image, ImageDraw

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
from native_ui_params import (FILTER_STATE_OFFSET, FX_DOMAIN_OFFSET, LEGACY_EXTRAS, MIXER_PEEK_VIEW,
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
ROUTING = ("Parallel", "Series 1 to 2", "Series 2 to 1")
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
DIAL_SIZE = 60
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


def _label(name, text, rect, parameter, gates, *, height=16, colour=None, align="horizontallyCentred"):
    data = {
        "version": 1,
        "type": "Value" if text is None else "Text",
        "handleName": "Data" if text is None else "",
        "textStyle": {
            "version": 1,
            "font": {"version": 1, "name": style.size("font.family"), "style": style.size("font.style.semibold"), "height": height},
            "colour": colour or style.argb("text.icon"),
            "case": "Original",
            "justification": f"{align} verticallyCentred",
        },
    }
    if text is not None:
        data["text"] = text
    return _entry(name, "Label", data, rect, parameter if text is None else 0, gates)


def _image(name, file, rect, gates=()):
    data = {"version": 2, "imageType": "Regular", "image": file, "colour": style.native_color("transparent")}
    return _entry(name, "Image", data, rect, 0, gates)


def _button(name, rect, parameter, gates, index=1, count=1, art=("toggle_on.png", "toggle_off.png"), focus=True):
    entry = ui_button.button(NATIVE + name, rect, art[0], art[1], index=index, count=count,
                             remaps=[("Data", f"Parameter {parameter}")], gates=gates)
    entry["bounds"]["acceptsHWFocus"] = "Yes" if focus else "No"
    return entry


def _hidden_hit(name, rect, action, action_index, gates):
    return _button(name, rect, action, gates, action_index, FILTER_ACTION_COUNT, ("filter_native_hit.png",) * 2)


def _film(name, file, rect, parameter, gates, frames):
    data = {"version": 2, "imageType": "FilmStrip", "image": file, "colour": style.native_color("transparent"),
            "numFrames": frames, "handleName": "Data"}
    return _entry(name, "Image", data, rect, parameter, gates)


def _write_selector_strip(directory, key, rect, frames):
    """Render selector cell FilmStrip frames using components.native.radio_button."""
    file = "filter_native_" + key + ".png"
    w, h = rect[2:]
    art = Image.new("RGBA", (w, h * len(frames)))
    for i, (label, selected, used) in enumerate(frames):
        cell = ui_radio.render_segmented_button_art(label, selected, w, h)
        if used:
            draw = ImageDraw.Draw(cell)
            dot_col = style.color("radio.active.text") if selected else style.color("accent.cyan")
            draw.ellipse((w // 2 - 3, h - 11, w // 2 + 3, h - 5), fill=dot_col)
        art.paste(cell, (0, i * h))
    art.save(directory / file)
    return file


def _ensure_dial_assets(skins_dir):
    uni_on = ui_level.synthyboy_dial_data(skins_dir, frame_size=DIAL_SIZE, bipolar=False, active=True)
    bi_on = ui_pan.synthyboy_dial_data(skins_dir, frame_size=DIAL_SIZE, bipolar=True)

    # Render single static frame (frame 0) for passive OFF display without leaving unreferenced strips in skins_dir
    with tempfile.TemporaryDirectory(prefix="filter-dial-off-") as tmp_dir:
        tmp_path = Path(tmp_dir)
        uni_off_data = ui_level.synthyboy_dial_data(tmp_path, frame_size=DIAL_SIZE, bipolar=False, active=False)
        bi_off_strip = tmp_path / "filter_dial_60_bi_inactive.png"
        ui_level.write_strip(bi_off_strip, frame_size=DIAL_SIZE, bipolar=True, active=False)
        for src_name, out_name in ((uni_off_data["filmStrip"], "filter_dial_60_uni_off.png"),
                                     (bi_off_strip.name, "filter_dial_60_bi_off.png")):
            with Image.open(tmp_path / src_name) as src:
                src.crop((0, 0, DIAL_SIZE, DIAL_SIZE)).save(skins_dir / out_name)

    return {
        "uni_on": uni_on,
        "bi_on": bi_on,
        "uni_off_img": "filter_dial_60_uni_off.png",
        "bi_off_img": "filter_dial_60_bi_off.png",
    }


def _write_action_slot_art(skins_dir, key, size, *, icon=None, text=None, colour=None):
    w, h = size
    fill = colour or style.color("text.icon")
    label = text
    if label == "DELETE POINT" and w < 141:
        label = "DEL POINT"
    art = ui_button.in_slot(ui_button.action_art(icon=icon, text=label, width=None if icon else max(48, w - 4), colour=fill), (w, h))
    filename = f"filter_native_{key}_{w}x{h}.png"
    art.save(skins_dir / filename)
    return filename


def _ensure_power_art(skins_dir):
    files = {}
    for state_key, colour_key in (("off", "accent.red"), ("on", "accent.green")):
        art = ui_button.in_slot(ui_button.action_art(icon="power", colour=style.color(colour_key)), (56, 56))
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
        filename=f"filter_selector_{width}x{height}.png", label_height=18,
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
    label = _label(name, text, rect, 0, (), height=17, colour=colour or style.argb("text.icon"), align=align)
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
    font = semibold(17)
    for on in (False, True):
        title = "filterNativeOutOn" if on else "filterNativeOutOff"
        fg = style.argb("text.icon" if on else "text.status.off")
        dim = style.argb("text.caption" if on else "smooth.value.off")
        background = style.argb("button.surface" if on else "menu.background")
        filename = f"{title}.png"
        plate = Image.new("RGB", (w, h), "#" + background[2:])
        draw = ImageDraw.Draw(plate)
        labels = []
        for i, (caption, _, unit) in enumerate(rows):
            y, bottom = i * h // len(rows), (i + 1) * h // len(rows)
            unit_w = 24 if unit else 0
            mid = (y + bottom) // 2
            draw.text((4, mid), caption, font=font, fill="#" + dim[2:], anchor="lm")
            if unit:
                draw.text((w - 3 - unit_w // 2, mid), unit, font=font, fill="#" + dim[2:], anchor="mm")
            labels.append(_readout_label(caption + " value", None, (64, y, w - 68 - unit_w, bottom - y),
                                         f"Readout{i}", fg, "right"))
        plate.save(ctx.skins_dir / filename)
        ctx.add_component_definition(title, _readout_panel(labels, background, filename))
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
    glide_children = [_readout_label("GLIDE", "GLIDE", (0, 0, 100, h)),
                      _readout_label("GLIDE value", None, (100, 0, w - 145, h), "Data"),
                      _readout_label("GLIDE unit", "ms", (w - 45, 0, 45, h))]
    ctx.add_component_definition(kind, _readout_panel(glide_children, style.argb("button.surface")))
    for frame, source in ((3, "note"), (4, "velocity")):
        legato = ctx.param_index(f"curve.{source}.legato")
        gates = _state_gates(state, (frame, frame)) + [f"IndexedEnabling/1/2/Parameter {legato}"]
        yield _entry(f"Glide {source}", kind, {"version": 1, "handleName": "Data"}, glide,
                     ctx.param_index("master.portamento.time"), gates)


def _pz_toolbar(ctx, rects):
    base = ctx.param_count + LEGACY_EXTRAS
    mode = base + PZ_AVAILABILITY
    font = semibold(19)

    def picture(label, size, active):
        width, height = size
        art = Image.new("RGBA", size)
        draw = ImageDraw.Draw(art)
        draw.rounded_rectangle((1, 1, width - 2, height - 2), radius=6,
                               fill="#161d31" if active else "#202020",
                               outline="#525b73" if active else "#4c4c4c", width=2)
        draw.text((width / 2, height / 2), label.upper(), font=font,
                  fill="#e6e6e6" if active else "#949494", anchor="mm")
        return art

    x, y, _, height = rects[2]
    right = rects[3][0] + rects[3][2]
    film_name = "pz_draft_tools.png"
    film = Image.new("RGBA", (right - x, height * 4))
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
            name = "filter_native_hit.png"
        button = _button(f"PZ {label}", rect, 0, gates, index, 5, (name, name))
        button["handle remapping"]["map"][0]["value"] = f"WithName[{label}]/FromGesture/Data"
        children.append(button)
    film.save(ctx.skins_dir / film_name)

    kind = "filterNativePzToolbar"
    ctx.add_component_definition(kind, _readout_panel(children, "00000000"))
    for source in (1, 2):
        entry = _entry(f"PZ Toolbar F{source}", kind, {"version": 1, "handleName": "Data"},
                       (0, 0, 1280, 72), base + PZ_ACTIONS + source - 1,
                       (f"IndexedEnabling/{source}/3/Parameter {base + PZ_SOURCE}",))
        entry["bounds"]["acceptsHWFocus"] = "Children"
        yield entry


def _ensure_dial_definition(ctx, w, h, bipolar, active, dial_assets, side_layout):
    # The compact row has 182/185/187px cells from integer column division.
    # Reuse the narrowest control subtree; the at-most-five trailing pixels are
    # parent spacing, not a new dial/caption/value component variant.
    if side_layout and h == 76:
        w = 182
    layout_tag = "side" if (side_layout or w >= 170) else "top"
    kind = f"filterNativeDial_{layout_tag}_{'bi' if bipolar else 'uni'}_{'on' if active else 'off'}_{w}x{h}"
    defs = ctx._filter_dial_defs if hasattr(ctx, "_filter_dial_defs") else setattr(ctx, "_filter_dial_defs", set()) or ctx._filter_dial_defs
    if kind in defs:
        return kind
    defs.add(kind)

    if layout_tag == "side":
        dial_rect = (4, (h - DIAL_SIZE) // 2, DIAL_SIZE, DIAL_SIZE)
        tx = 4 + DIAL_SIZE + 6
        tw = max(48, w - tx - 2)
        cap_rect = (tx, max(4, h // 2 - 24), tw, 20)
        val_rect = (tx, max(24, h // 2 - 2), tw, 26)
        align = "left"
    else:
        dial_rect = ((w - DIAL_SIZE) // 2, 18, DIAL_SIZE, DIAL_SIZE)
        cap_rect = (2, 84, max(40, w - 4), 22)
        val_rect = (2, 108, max(40, w - 4), 24)
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
            if active:
                data["data"] = copy.deepcopy(dial_assets["bi_on" if bipolar else "uni_on"])
            else:
                off_img = dial_assets["bi_off_img"] if bipolar else dial_assets["uni_off_img"]
                replacement = _image("off dial", off_img, dial_rect)
                child.update(replacement)
        elif name in ("name", "value"):
            child["bounds"]["bounds"] = "%d %d %d %d" % (cap_rect if name == "name" else val_rect)
            ts = data["data"]["textStyle"]
            ts["font"]["height"] = 15 if name == "name" else 17
            ts["justification"] = align + " verticallyCentred"
            ts["colour"] = style.argb(("text.caption" if name == "name" else "text.icon") if active else "smooth.value.off")
    transparent = {"version": 1, "colour": style.native_color("transparent"), "image": ""}
    definition.update(actions=[], ignoreMousePresses=not active, disableCoarseDataWheel=not active,
                      backgroundData={"version": 1, "focussed": transparent, "unfocussed": transparent})
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


def _emit_toggle(container, name, rect, param, caption, active_gates, passive_gates, skins_dir, *, model_proxy=None, passive_models=()):
    x, y, w, h = rect
    tw, th = min(w, 120), min(h, 56)
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
        passive_art = active_text.render_active_text_art(caption, False, width=tw, height=th)
        r, g, b, a = passive_art.split()
        passive_dim = Image.merge("RGBA", (r.point(lambda p: p * 65 // 100),
                                           g.point(lambda p: p * 65 // 100),
                                           b.point(lambda p: p * 65 // 100),
                                           a.point(lambda p: p * 70 // 100)))
        off_file = f"filter_native_toggle_{key}_{tw}x{th}_passive.png"
        passive_dim.save(skins_dir / off_file)
        for suffix, selection_gate, raw_model in passive_models:
            gates = [*(gate.replace(f"Parameter {model_proxy}", f"Parameter {raw_model}") for gate in passive_gates), selection_gate]
            container.append(_image(name + suffix + " off", off_file, (tx, ty, tw, th), gates))


def _emit_routing(container, name, rect, param, active_gates, passive_gates, skins_dir, variant_key, *, active_alternatives=()):
    x, y, w, h = rect
    for index, caption in enumerate(ROUTING):
        top = index * h // 3
        part = (x, y + top, w, (index + 1) * h // 3 - top)
        pw, ph = part[2], part[3]
        on_file = f"filter_native_route_{variant_key}_{index}_{pw}x{ph}_on.png"
        off_file = f"filter_native_route_{variant_key}_{index}_{pw}x{ph}_off.png"
        ui_radio.render_radio_dot_button_art(caption, True, pw, ph).save(skins_dir / on_file)
        ui_radio.render_radio_dot_button_art(caption, False, pw, ph).save(skins_dir / off_file)
        for branch, gates in enumerate([active_gates, *active_alternatives]):
            suffix = " alternative" if branch else ""
            container.append(_button(f"{name} {index}{suffix}", part, param, gates, index, 3, (on_file, off_file)))
        if passive_gates:
            off_art = ui_radio.render_radio_dot_button_art(caption, False, pw, ph)
            r, g, b, a = off_art.split()
            dim_art = Image.merge("RGBA", (r.point(lambda p: p * 55 // 100),
                                           g.point(lambda p: p * 55 // 100),
                                           b.point(lambda p: p * 55 // 100),
                                           a.point(lambda p: p * 70 // 100)))
            passive_file = f"filter_native_route_{variant_key}_{index}_{pw}x{ph}_passive.png"
            dim_art.save(skins_dir / passive_file)
            container.append(_image(f"{name} {index} off", passive_file, part, passive_gates))


def _ensure_nv_definition(ctx, w, h):
    # Integer row division produces 119..121px cells. A common 119px subtree
    # preserves every >=56px value hitbox without serialising three copies.
    if h == 56:
        w = 119
    bw = max(44, min(60, w // 2))
    kind = f"filterNativeNv_{w}x{h}"
    defs = ctx._filter_nv_defs if hasattr(ctx, "_filter_nv_defs") else setattr(ctx, "_filter_nv_defs", set()) or ctx._filter_nv_defs
    if kind in defs:
        return kind
    defs.add(kind)
    defn = ui_name_value.build_name_value_definition(
        name=kind, rect=(0, 0, w, h), skins_dir=ctx.skins_dir, box_width=bw,
        handle="Data", caption_handle="Caption",
    )
    ctx.add_component_definition(kind, defn)
    return kind


def _emit_curve_controls(ctx, container, rects, base, action, state, displays, *, is_split, expand_rect=None):
    split_or_curve_gates = [] if is_split else _state_gates(state, (3, 4))
    for index, rect in enumerate(rects):
        slot = POINT + index
        x, y, w, h = rect
        if slot < ACTION:
            field_name = FIELDS[slot]
            host_value = base + slot
            if slot == ACTION - 1:  # Legato
                prefix = "Split " if is_split else ""
                _emit_toggle(container, f"{prefix}Curve {field_name}", rect, host_value, "LEGATO",
                             split_or_curve_gates, (), ctx.skins_dir)
            elif slot in TOGGLES:
                prefix = "Split " if is_split else ""
                _emit_toggle(container, f"{prefix}Curve {field_name}", rect, host_value, field_name.upper(),
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


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    if not any(tab.get("tabName") == "Filter" for tab in tui["pageData"]["tabs"]):
        return
    manifest = json.loads(CONTRACT.read_text(encoding="utf-8"))
    items = ctx.tab("Filter")["componentDefinition"]["componentsData"]
    items[:] = [item for item in items if item["componentData"]["name"] not in ("Filter 1 Power", "Filter 2 Power")]

    state = ctx.param_count + STATE_OFFSET
    base = state + 1
    action = base + ACTION
    displays = action + 1
    note_ind = displays + 2
    velo_ind = displays + 3
    power_frame = displays + 4
    domain_base = ctx.param_count + LEGACY_EXTRAS + FX_DOMAIN_OFFSET
    Image.new("RGBA", (1, 1)).save(ctx.skins_dir / "filter_native_hit.png")

    dial_assets = _ensure_dial_assets(ctx.skins_dir)
    power_art = _ensure_power_art(ctx.skins_dir)
    domains = json.loads(FX_CONTRACT.read_text(encoding="utf-8"))["domains"]

    items.extend(_pz_toolbar(ctx, manifest["pz_toolbar"]))

    # Gates distinguishing Split vs Expanded Filter vs Expanded Curve
    split_note_gates = _state_gates(state, (1, 2)) + [f"RangedEnabling/2/3/4/Parameter {note_ind}"]
    split_velo_gates = _state_gates(state, (1, 2)) + [f"RangedEnabling/2/3/4/Parameter {velo_ind}"]
    expanded_filter_gates = _state_gates(state, (1, 2)) + [
        f"RangedEnabling/0/1/4/Parameter {note_ind}",
        f"RangedEnabling/0/1/4/Parameter {velo_ind}",
    ]
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
        if slot in (MODEL, STYLE):
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
                    _emit_toggle(filter_items, name, rect, host_value, FIELDS[slot].upper(),
                                 active_gates, passive_gates, ctx.skins_dir, model_proxy=model,
                                 passive_models=[(f" F{f}", f"IndexedEnabling/{f}/5/Parameter {state}",
                                                  ctx.param_index(f"filter{f}.model")) for f in (1, 2)])
                elif slot == ROUTE:
                    # ROUTING belongs to both filters, including in the full view.
                    # The dual-view indicator is not an "other filter on" bit.
                    f1, f2 = ctx.param_index("filter1.on"), ctx.param_index("filter2.on")
                    _emit_routing(filter_items, name, rect, host_value,
                                  [f"IndexedEnabling/1/2/Parameter {f1}"],
                                  [f"IndexedEnabling/0/2/Parameter {f1}", f"IndexedEnabling/0/2/Parameter {f2}"],
                                  ctx.skins_dir, "exp", active_alternatives=[
                                      [f"IndexedEnabling/0/2/Parameter {f1}", f"IndexedEnabling/1/2/Parameter {f2}"]])
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
    surface = _entry("Editor Surface", kind, data, (0, 0, 1280, 628), state, expanded_filter_gates)
    surface["bounds"]["acceptsHWFocus"] = "Children"
    items.insert(0, surface)

    mixer = next((t for t in tui["pageData"]["tabs"] if t["tabName"] in ("Mix", "Mixer")), None)
    if mixer is not None:
        # Docked Mixer PEEK editor occupies 0..564 above the persistent bottom 6-tab bar (y=566..622).
        import copy as _copy
        docked_items = []
        docked_h = 564
        tab_shift = 628 - docked_h
        for comp in filter_items:
            c = _copy.deepcopy(comp)
            cx, cy, cw, ch = map(int, c["bounds"]["bounds"].split())
            if cy >= 400:
                cy -= tab_shift
            elif 110 <= cy < 400 and ch >= 90:
                # Formant 2x2 middle block shrinks vertically to match FilterDisplay::layout() at 564px
                mid_y = 76
                band_top = 384 - 8
                block_top = mid_y + 18 + 6
                cell_h = max(48, (band_top - mid_y - 30 - 6) // 2)
                row_idx = 0 if cy < 200 else 1
                cy = block_top + row_idx * (cell_h + 6)
                ch = min(ch, cell_h)
            c["bounds"]["bounds"] = f"{cx} {cy} {cw} {ch}"
            docked_items.append(c)
        dock_kind = "mixerPeekFilterEditorSurface"
        ctx.add_component_definition(dock_kind, {
            "version": 2, "actions": [], "ignoreMousePresses": True, "disableCoarseDataWheel": True,
            "backgroundData": {"version": 1, "focussed": transparent, "unfocussed": transparent},
            "componentsData": docked_items,
        })
        view = ctx.param_count + LEGACY_EXTRAS + MIXER_PEEK_VIEW
        peek = _entry("Peek Filter", dock_kind, data, (0, 0, 1280, docked_h),
                      view, (f"IndexedEnabling/1/3/Parameter {view}", *_state_gates(state, (1, 2))))
        peek["componentData"]["name"] = "Mixer Native Peek Filter"
        peek["bounds"]["acceptsHWFocus"] = "Children"
        mix_items = mixer["componentDefinition"]["componentsData"]
        first_marker = next((i for i, item in enumerate(mix_items)
                            if item["componentData"]["name"].startswith("Overlay ")), len(mix_items))
        mix_items.insert(first_marker, peek)

    # Expanded Filter top bar (F1 | F2 | NOTE | VELO, dual) + Graph corner Expand & PZ Edit
    vx, vy, vw, vh = manifest["view"]
    for i, label in enumerate(SELECT):
        rect = (vx + i * vw // 4, vy, vw // 4, vh)
        file = _write_selector_strip(ctx.skins_dir, label.lower(), rect, [(label, bool(k & 2), bool(k & 1)) for k in range(4)])
        # Expanded Curve keeps canonical "Filter Native F1..VELO" names on state 3..4;
        # Expanded Filter mounts them when NOT in Split (!note_ind[2..3] & !velo_ind[2..3])
        items.extend((_film(label + " art", file, rect, displays + i, expanded_curve_gates, 4),
                      _hidden_hit(label, rect, action, i + 1, expanded_curve_gates)))
        items.extend((_film("Exp " + label + " art", file, rect, displays + i, expanded_filter_gates, 4),
                      _hidden_hit("Exp " + label, rect, action, i + 1, expanded_filter_gates)))

    both = manifest["both"]
    dual_frames = [("F1+F2", bool(frame & 1), False) for frame in range(4)]
    art = _write_selector_strip(ctx.skins_dir, "dual", both, dual_frames)
    items.extend((_film("dual art", art, both, displays + 4, expanded_filter_gates, 4),
                  _hidden_hit("dual", both, action, 5, expanded_filter_gates)))

    exp_rect = manifest["expand_filter"]
    exp_art = _write_action_slot_art(ctx.skins_dir, "expand", tuple(exp_rect[2:]), icon="expand")
    items.append(_button("Expand Filter", exp_rect, action, expanded_filter_gates, EXPAND_FILTER_ACTION, FILTER_ACTION_COUNT, (exp_art, exp_art)))

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
                         is_split=False, expand_rect=manifest["expand_curve_top"])
    curve_kind = "filterNativeCurveSurface"
    ctx.add_component_definition(curve_kind, {
        "version": 2, "actions": [], "ignoreMousePresses": True, "disableCoarseDataWheel": True,
        "backgroundData": {"version": 1, "focussed": transparent, "unfocussed": transparent},
        "componentsData": curve_items,
    })
    curve_surface = _entry("Curve Surface", curve_kind, data, (0, 0, 1280, 628), state, expanded_curve_gates)
    curve_surface["bounds"]["acceptsHWFocus"] = "Children"
    items.append(curve_surface)

    # =========================================================================
    # 3. 60/40 Split Left Filter surface (filterNativeSplitLeftSurface)
    #    - 2883 == 0: Both OFF (Filter OFF + ROUTING passive)
    #    - 2883 == 1: Selected Filter OFF, Other Filter ON (Filter OFF + ROUTING ACTIVE)
    #    - 2883 == 2: Selected Filter ON (Filter ON + ROUTING ACTIVE)
    # =========================================================================
    split = manifest["split"]
    split_sel_on = f"IndexedEnabling/2/4/Parameter {power_frame}"
    split_sel_off = f"RangedEnabling/0/1/4/Parameter {power_frame}"
    split_routing_on = f"RangedEnabling/1/2/4/Parameter {power_frame}"
    split_routing_passive = f"IndexedEnabling/0/4/Parameter {power_frame}"

    split_left = []
    svx, svy, svw, svh = split["view"]
    for i, label in enumerate(("F1", "F2")):
        rect = (svx + i * svw // 2, svy, svw // 2, svh)
        file = _write_selector_strip(ctx.skins_dir, f"split_{label.lower()}", rect,
                                     [(label, bool(k & 2), bool(k & 1)) for k in range(4)])
        split_left.extend((_film(f"Split {label} art", file, rect, displays + i, (), 4),
                           _hidden_hit(f"Split {label}", rect, action, i + 1, ())))

    split_pwr_rect = split["filter"][0][POWER]
    split_left.append(_button("Split Power", split_pwr_rect, base + POWER, (), art=power_art))

    split_exp_rect = split["filter_expand"]
    split_left.append(_button("Split Expand Filter", split_exp_rect, action, (), EXPAND_FILTER_ACTION, FILTER_ACTION_COUNT, (exp_art, exp_art)))

    split_pz_rect = split["pz_expand"]
    split_pz = _button("Split Edit PZ Curve", split_pz_rect, action,
                       [split_sel_on, f"IndexedEnabling/{PZ_MODEL}/27/Parameter {model}"], PZ_ACTION, FILTER_ACTION_COUNT, (exp_art, exp_art))
    split_pz["handle remapping"]["map"][0]["value"] = "WithName[Edit PZ Curve]/FromGesture/" + split_pz["handle remapping"]["map"][0]["value"]
    split_left.append(split_pz)

    for slot, word in ((MODEL, "Model"), (STYLE, "Style")):
        rect = split["filter"][0][slot]
        definition = "filterNativeSplit" + word
        _choice_definition(tui, ctx, definition, *rect[2:], active=True)
        off_def = definition + "Off"
        _choice_definition(tui, ctx, off_def, *rect[2:], active=False)
        models = range(27) if slot == STYLE else (None,)
        for model_id in models:
            if model_id in manifest["one_style"]:
                continue
            gates = [split_sel_on]
            if model_id is not None:
                gates.append(f"IndexedEnabling/{model_id}/27/Parameter {model}")
            parameter = domain_base + (STYLE_DOMAIN + model_id if model_id is not None else 0)
            name = f"Split {word}" if model_id is None else f"Split {word} {model_id}"
            split_left.append(_entry(name, definition, {"version": 1, "handleName": "Data"}, rect, parameter, gates, True))
        for f in (1, 2):
            split_left.append(_entry(f"Split {word} F{f} off", off_def, {"version": 1, "handleName": "Data"}, rect,
                                     ctx.param_index(f"filter{f}.{FILTER_KEYS[slot]}"),
                                     [split_sel_off, f"IndexedEnabling/{f}/5/Parameter {state}"]))

    ssx, ssy, ssw, ssh = split["filter"][0][STYLE]
    for model_id in manifest["one_style"]:
        gates = [split_sel_on, f"IndexedEnabling/{model_id}/27/Parameter {model}"]
        split_left.append(_label(f"Split Single Style {model_id}", domains[STYLE_DOMAIN + model_id][0],
                                 (ssx, ssy, ssw, ssh), model, gates))

    for slot in range(1, POINT):
        if slot in (MODEL, STYLE):
            continue
        groups = {}
        for model_id in range(27):
            variant = 1 if model_id == 5 else 2 if model_id == 6 else 0
            rect = split["filter"][variant][slot]
            if rect is not None:
                groups.setdefault(tuple(rect), []).append(model_id)
        for rect, models in groups.items():
            for start, end in _runs(models):
                host_value = base + slot
                name = f"Split {FIELDS[slot]} {start}"
                model_gate = [] if (start, end) == (0, 26) else [f"RangedEnabling/{start}/{end}/27/Parameter {model}"]
                active_gates = [split_sel_on, *model_gate]
                passive_gates = [split_sel_off, *model_gate]
                if slot in TOGGLES:
                    _emit_toggle(split_left, name, rect, host_value, FIELDS[slot].upper(),
                                 active_gates, passive_gates, ctx.skins_dir, model_proxy=model,
                                 passive_models=[(f" F{f}", f"IndexedEnabling/{f}/5/Parameter {state}",
                                                  ctx.param_index(f"filter{f}.model")) for f in (1, 2)])
                elif slot == ROUTE:
                    _emit_routing(split_left, name, rect, host_value, [split_routing_on], [split_routing_passive], ctx.skins_dir, "split")
                else:
                    _emit_dial(ctx, split_left, name, rect, host_value, FIELDS[slot].upper(),
                               slot in BIPOLAR, active_gates, passive_gates, dial_assets, side_layout=True,
                               passive_bindings=[(f" F{f}", ctx.param_index(f"filter{f}.{FILTER_KEYS[slot]}"),
                                                  f"IndexedEnabling/{f}/5/Parameter {state}", ctx.param_index(f"filter{f}.model"))
                                                 for f in (1, 2)], model_proxy=model)

    split_left_kind = "filterNativeSplitLeftSurface"
    ctx.add_component_definition(split_left_kind, {
        "version": 2, "actions": [], "ignoreMousePresses": True, "disableCoarseDataWheel": True,
        "backgroundData": {"version": 1, "focussed": transparent, "unfocussed": transparent},
        "componentsData": split_left,
    })
    for tag, gates in (("Split Left Note", split_note_gates), ("Split Left Velo", split_velo_gates)):
        entry = _entry(tag, split_left_kind, data, (0, 0, 1280, 628), state, gates)
        entry["bounds"]["acceptsHWFocus"] = "Children"
        items.append(entry)

    # =========================================================================
    # 4. 60/40 Split Right Curve surface (filterNativeSplitRightSurface)
    # =========================================================================
    split_right = []
    srx, sry, srw, srh = split["scalar_view"]
    for i, label in enumerate(("NOTE", "VELO")):
        rect = (srx + i * srw // 2, sry, srw // 2, srh)
        file = _write_selector_strip(ctx.skins_dir, f"split_{label.lower()}", rect,
                                     [(label, bool(k & 2), bool(k & 1)) for k in range(4)])
        split_right.extend((_film(f"Split {label} art", file, rect, displays + 2 + i, (), 4),
                            _hidden_hit(f"Split {label}", rect, action, 3 + i, ())))

    _emit_curve_controls(ctx, split_right, split["curve"], base, action, state, displays,
                         is_split=True, expand_rect=split["curve_expand"])

    split_right_kind = "filterNativeSplitRightSurface"
    ctx.add_component_definition(split_right_kind, {
        "version": 2, "actions": [], "ignoreMousePresses": True, "disableCoarseDataWheel": True,
        "backgroundData": {"version": 1, "focussed": transparent, "unfocussed": transparent},
        "componentsData": split_right,
    })
    for tag, gates in (("Split Right Note", split_note_gates), ("Split Right Velo", split_velo_gates)):
        entry = _entry(tag, split_right_kind, data, (0, 0, 1280, 628), state, gates)
        entry["bounds"]["acceptsHWFocus"] = "Children"
        items.append(entry)

    tui["pageData"].setdefault("replacementStrings", []).append({"key": f"Parameter {base + ROUTE}", "value": list(ROUTING)})
    items.sort(key=lambda item: item["componentData"]["name"].startswith(("Overlay Marker ", "Overlay Instance ")))
