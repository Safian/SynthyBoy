# SynthyBoy - compact Play oscillator controls in the MPC stock skin.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps

import sys

_ROOT = str(Path(__file__).resolve().parents[2])
_SCRIPTS = str(Path(__file__).resolve().parents[1])
for _p in (_ROOT, _SCRIPTS):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from components.native.level.level import compact_dial_definition, level_definition
from components.native.name_value.name_value import (
    DEFAULT_BOX_WIDTH,
    build_name_value_definition,
    create_name_value_entry,
    render_name_value_box_face,
)
from components.native.pan.pan import pan_definition
from components.native.selector.selector import (
    OSC_INSTANCE_NAMES,
    OSC_ROUTING_CHOICES,
    _value_label,
    build_choice_definition,
    passive_layer,
    selector_definition,
)
from components.style import argb as _style_argb, color as _style_color, size as _style_size
from native_skin_font import semibold
from components.native.button.button import action_button, button, entry
if __package__:
    from .sub_dial import sub_dial_definition
else:
    from native_play.sub_dial import sub_dial_definition

CONTRACT = Path(__file__).resolve().parent.parent / "mpc-skin-extras" / "play-osc-native-contract.json"
CONTRACT_HEADER = Path(__file__).resolve().parents[2] / "shell-mpc" / "play_osc_native_contract.h"
FIELDS = ("power", "expand", "routing", "first", "second", "pan", "level")
OSC_LAYOUT = ("sub", "abc", "abc", "abc", "noise")


def render_contract_header(contract_path=CONTRACT):
    """Render the C++ Play OSC native geometry contract header from JSON."""
    geometry = json.loads(Path(contract_path).read_text(encoding="utf-8"))
    lines = [
        "// Generated from scripts/mpc-skin-extras/play-osc-native-contract.json by scripts/native_play/osc.py.",
        "// Copyright (C) 2026 Safian Szabolcs",
        "// SPDX-License-Identifier: AGPL-3.0-or-later",
        "#pragma once",
        "",
        "#include <array>",
        "",
        "#include \"native_play_osc.h\"",
        "",
        "namespace synthyboy_mpc::nativeui::playoscui",
        "{",
        "struct ContractRect { int x, y, w, h; };",
        "",
    ]
    for group, symbol in (("sub", "kSubContractRects"), ("abc", "kAbcContractRects"), ("noise", "kNoiseContractRects")):
        entries = ", ".join(f"{{ {r[0]}, {r[1]}, {r[2]}, {r[3]} }}" for r in (geometry[group][f] for f in FIELDS))
        lines.append(f"inline constexpr std::array<ContractRect, kFields> {symbol} {{{{ {entries} }}}};")
    lines += [
        "",
        "constexpr ContractRect contractRect (int layout, int field) noexcept",
        "{",
        "    if (field < 0 || field >= kFields)",
        "        return {};",
        "    return layout == Sub ? kSubContractRects[field]",
        "         : layout == Noise ? kNoiseContractRects[field]",
        "         : kAbcContractRects[field];",
        "}",
        "} // namespace synthyboy_mpc::nativeui::playoscui",
        "",
    ]
    return "\n".join(lines)
ROUTE_MENU, CHOICE_MENU, OCTAVE_PAD = "play osc routing", "play osc choice", "play osc octave"
POPUP_MARKERS = ((52, "e5a44f"), (64, "de78c9"), (76, "58d4b1"))
MODAL_MARKERS = ((16, "fe356c"), (28, "24c7ff"), (40, "90ed43"))
ENGINE_FIRST = ("sub.waveform", "osc1.type", "osc2.type", "osc3.type", "noise.sample")
TYPE_ORDER = (0, 4, 1, 2, 3)


def component(name, kind, rect, remaps=(), gates=(), focus="Yes"):
    return entry(name, kind, {"version": 1, "handleName": "Data"}, rect, remaps, gates, focus)


def sub_choice_rows(ctx, definition, labels, field):
    """Use named native radio buttons because the XL drawer loses the proxy step metadata."""
    definition["ignoreMousePresses"] = False
    # Tapping the stock header closes the drawer and commits the selected row.
    font = semibold(27)
    hint = f"play_sub_{field}_apply.png"
    header = Image.new("RGBA", (550, 56))
    draw = ImageDraw.Draw(header)
    draw.line((24, 18, 14, 28, 24, 38), fill="white", width=3)
    title = "WAVEFORM" if field == "wave" else "OCTAVE"
    draw.text((41, 28), f"Tap PLAY SUB {title} to apply", anchor="lm", font=font, fill="white")
    header.save(ctx.skins_dir / hint)
    definition["componentsData"].append(entry("Apply hint", "Image", {
        "version": 2, "imageType": "Regular", "colour": "0", "image": hint
    }, (436, 27, 550, 56), focus="No"))
    for i, label in enumerate(labels):
        rect = (520, 105 + i * 65, 360, 58)
        images = []
        for state, fill in (("on", "#175a76"), ("off", "#303237")):
            filename = f"play_sub_{field}_{i}_{state}.png"
            im = Image.new("RGB", rect[2:], fill)
            draw = ImageDraw.Draw(im)
            draw.text((180, 29), label, anchor="mm", font=font, fill="white")
            im.save(ctx.skins_dir / filename)
            images.append(filename)
        definition["componentsData"].append(button(label, rect, *images, index=i, count=len(labels)))


def setup_popups(ctx, stock_menu):
    marker_paths = {}
    for x, rgb in (*MODAL_MARKERS, *POPUP_MARKERS):
        filename = f"play_osc_modal_{x}.png"
        Image.new("RGB", (6, 6), f"#{rgb}").save(ctx.skins_dir / filename)
        marker_paths[x] = filename
    for name, (x, _) in zip((ROUTE_MENU, CHOICE_MENU, OCTAVE_PAD), POPUP_MARKERS):
        definition = copy.deepcopy(stock_menu)
        if name == OCTAVE_PAD:
            sub_choice_rows(ctx, definition, [str(i) for i in range(-4, 5)], "octave")
        for offset in (16, 28, 40, x):
            mark = entry(f"Play OSC marker {offset}", "Image", {"version": 2, "imageType": "Regular",
                         "colour": "0", "image": marker_paths[offset]}, (offset, 120, 6, 6), focus="No")
            mark["bounds"]["showWhenDataModelInvalid"] = "Show"
            definition["componentsData"].append(mark)
        ctx.add_component_definition(name, definition)
        if name == CHOICE_MENU:
            sub = copy.deepcopy(definition)
            sub_choice_rows(ctx, sub, ctx.params[ctx.param_index("sub.waveform")]["choices"], "wave")
            ctx.add_component_definition("play sub waveform", sub)


def add_compact_osc(tui, ctx, items, base, selector, selector_template, stock_menu, dial_template):
    geometry = json.loads(CONTRACT.read_text(encoding="utf-8"))
    layout_handle = f"Parameter {base}"
    setup_popups(ctx, stock_menu)

    artwork = {}
    for key in ("left", "right", "empty"):
        filename = f"play_osc_source_{key}.png"
        im = Image.new("RGBA", (56 if key != "empty" else 90, 72))
        if key != "empty":
            front, back = (24, 32) if key == "left" else (32, 24)
            ImageDraw.Draw(im).line((back, 28, front, 36, back, 44), fill="#e6e6e6", width=2)
        im.save(ctx.skins_dir / filename)
        artwork[key] = filename

    for name, popup, size, text_height in (
        ("playOscRouteField", ROUTE_MENU, (64, 64), 13.0),
        ("playOscChoiceField", CHOICE_MENU, (202, 72), 23.0),
    ):
        ctx.add_component_definition(name, build_choice_definition(
            selector_template, popup, (0, 0, *size), "Value", label_height=text_height))
    oct_w, oct_h = geometry["sub"]["second"][2:]
    oct_visual_h = _style_size("height.intermediate")
    oct_top = (oct_h - oct_visual_h) // 2

    def build_sub_oct_definition(name, *, active):
        definition = build_name_value_definition(
            name=name,
            rect=(0, 0, oct_w, oct_visual_h),
            skins_dir=ctx.skins_dir if active else None,
            popup=OCTAVE_PAD,
            label_colour=_style_argb("text.label") if active else _style_argb("text.status.off"),
            value_colour=_style_argb("field.value") if active else _style_argb("text.status.off"),
            with_face_art=active,
        )
        if not active:
            definition["actions"] = []
            definition["ignoreMousePresses"] = True
            definition["disableCoarseDataWheel"] = True
            definition["componentsData"] = [
                c for c in definition["componentsData"] if c["componentData"]["type"] != "Focus"
            ]
            box_w = min(DEFAULT_BOX_WIDTH, oct_w - _style_size("name_value.build_name_value_definition.bw"))
            box_x = oct_w - box_w
            face_name = f"play_sub_oct_off_{box_w}x{oct_visual_h}.png"
            base_face = render_name_value_box_face(box_w, oct_visual_h)
            grey_face = ImageOps.colorize(
                ImageOps.grayscale(base_face),
                _style_color("selector.neutral.surface"),
                _style_color("selector.neutral.border"),
            ).convert("RGBA")
            grey_face.putalpha(base_face.getchannel("A"))
            grey_face.save(ctx.skins_dir / face_name)
            definition["componentsData"].insert(0, {
                "version": 2,
                "componentData": {
                    "version": 1,
                    "name": "Face",
                    "type": "Image",
                    "data": {"version": 2, "imageType": "Regular", "colour": "0", "image": face_name},
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {
                    "version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
                    "whenVisible": "Always", "boundsType": "Absolute",
                    "bounds": f"{box_x} 0 {box_w} {oct_visual_h}",
                },
            })
        if oct_top:
            for child in definition["componentsData"]:
                bx, by, bw, bh = map(int, child["bounds"]["bounds"].split())
                child["bounds"]["bounds"] = f"{bx} {by + oct_top} {bw} {bh}"
        return definition

    ctx.add_component_definition("playOscNumberField", build_sub_oct_definition("playOscNumberField", active=True))
    ctx.add_component_definition("playSubOctInactive", build_sub_oct_definition("playSubOctInactive", active=False))
    wave_icon = "play_selector_wave.png"
    icon = Image.new("RGBA", (112, 96))
    points = [(x, 48 - 19 * math.sin((x - 4) * math.tau / 103)) for x in range(4, 108)]
    ImageDraw.Draw(icon).line(points, fill="#91aed5", width=7)
    icon.resize((28, 24), Image.Resampling.LANCZOS).save(ctx.skins_dir / wave_icon)
    ctx.add_component_definition("playSubWaveField", selector_definition(
        selector_template, "play sub waveform", tuple(geometry["sub"]["first"][2:]), ctx.skins_dir,
        icon=wave_icon, title="Data", name="Caption"))
    for name, with_steps in (("playOscTableField", True), ("playOscContentField", False)):
        source = copy.deepcopy(selector_template)
        source["actions"] = []
        centre = (56, 0, 90, 72) if with_steps else (0, 0, 202, 72)
        children = [button("browse", centre, artwork["empty"], index=2, count=4),
                    _value_label("source name", centre, 17.0)]
        if with_steps:
            children += [button("previous", (0, 0, 56, 72), artwork["left"], index=1, count=4),
                         button("next", (146, 0, 56, 72), artwork["right"], index=3, count=4)]
        source["componentsData"] = children
        source["disableCoarseDataWheel"] = True
        ctx.add_component_definition(name, source)

    ctx.add_component_definition("playOscPan", pan_definition(dial_template, ctx.skins_dir))
    ctx.add_component_definition("playSubPan", pan_definition(dial_template, ctx.skins_dir))
    ctx.add_component_definition("playSubLevel", level_definition(dial_template, ctx.skins_dir))
    inactive = copy.deepcopy(selector_template)
    inactive["actions"] = []
    inactive["ignoreMousePresses"] = True
    inactive["disableCoarseDataWheel"] = True
    for state in ("focussed", "unfocussed"):
        inactive["backgroundData"][state].update(colour="ff202020", image="")
    name = _value_label("caption", (10, 28, 182, 28), 17.0)
    name["componentData"]["data"].update(type="Name", handleName="Caption")
    value = _value_label("value", (10, 55, 182, 28), 17.0)
    for child in (name, value):
        child["componentData"]["data"]["textStyle"]["colour"] = "ff999999"
    inactive["componentsData"] = [name, value]
    sub_wave_off = copy.deepcopy(inactive)
    wave_text_w = geometry["sub"]["first"][2] - 20
    for child, y in zip(sub_wave_off["componentsData"], (5, 35)):
        child["bounds"]["bounds"] = f"10 {y} {wave_text_w} 28"
    ctx.add_component_definition("playSubWaveInactive", sub_wave_off)

    # The existing source power gates alone expose this passive readout.
    value_data = value["componentData"]["data"]
    value_data.update(type="Text", text="OFF")
    value_data["textStyle"]["colour"] = "ffff5a7a"
    ctx.add_component_definition("playOscInactive", inactive)

    expand_off = "play_sub_expand_off.png"
    with Image.open(ctx.skins_dir / "lfo_native_expand.png") as expand_art:
        disabled = ImageOps.colorize(expand_art.convert("L"), "#141414", "#999999")
        disabled.putalpha(expand_art.getchannel("A"))
        disabled.save(ctx.skins_dir / expand_off)

    power_gate = "IndexedEnabling/1/2/power"
    off_gate = "IndexedEnabling/0/2/power"
    rects = geometry["abc"]
    children = [
        button("power", rects["power"], "osc_native_power_on.png", "osc_native_power_off.png",
               remaps=(("Data", "power"),)),
        action_button(ctx, "expand", rects["expand"], "expand", icon="expand", gates=(power_gate,)),
        component("first", "playOscChoiceField", rects["first"], (("Data", "first"),), (power_gate,)),
        component("routing", "playOscRouteField", rects["routing"], (("Data", "routing"),),
                  (power_gate, "RoutingVisible")),
    ]
    for gate, kind in (("Wavetable", "playOscTableField"), ("Content", "playOscContentField"),
                       ("Filter", "compactNativeRiseDial")):
        remaps = [("Data", "second")]
        if gate == "Filter":
            remaps.append(("Caption", "WithName[FILTER]/second"))
        children.append(component("second", kind, rects["second"], remaps, (power_gate, gate),
                                  "Children" if gate in ("Wavetable", "Content") else "Yes"))
    for field, kind in (("pan", "playOscPan"), ("level", "compactNativeDial")):
        remaps = (("Data", field), ("Caption", f"WithName[{field.upper()}]/{field}"))
        children += [component(field, kind, rects[field], remaps, (power_gate,)),
                     component(field + " off", "playOscInactive", rects[field], remaps, (off_gate,), "No")]
    ctx.add_component_definition("playNativeOsc", passive_layer(children))

    replacements = tui["pageData"]["replacementStrings"]
    ctx.add_param_key(base, "shell.play_osc_native_view")
    for ordinal, (source, group, first_key) in enumerate(zip(OSC_INSTANCE_NAMES, OSC_LAYOUT, ENGINE_FIRST)):
        start = base + 1 + ordinal * len(FIELDS)
        handles = [f"Parameter {start + i}" for i in range(len(FIELDS))]
        for index, field in enumerate(FIELDS):
            ctx.add_param_key(start + index, f"shell.play_osc_native_{ordinal + 1}_{field}")
        remaps = [("Data", handles[0]), *zip(FIELDS, handles)]
        mode_gates = {name: "Invalid" for name in ("RoutingVisible", "Wavetable", "Content", "Filter")}
        if group == "abc":
            first = 2 * ordinal
            visible = f"RangedEnabling/{first}/{first + 1}/9/{layout_handle}"
            for i, mode in enumerate(("Wavetable", "Content")):
                mode_gates[mode] = f"IndexedEnabling/{first + i}/9/{layout_handle}"
            mode_gates["RoutingVisible"] = visible
        else:
            visible = f"IndexedEnabling/{1 if group == 'sub' else 8}/9/{layout_handle}"
            if group == "noise":
                mode_gates["Filter"] = visible
                mode_gates["RoutingVisible"] = visible
        remaps.extend(mode_gates.items())
        source_gates = (visible, f"IndexedEnabling/{ordinal}/5/Parameter {selector}")
        if group == "sub":
            # Place the cells directly on the tab, like the adjacent Play ENV dials.
            # The XL can show the buttons of a wrapper while nested widget bodies remain blank.
            rect = geometry["sub"]
            on = (*source_gates, f"IndexedEnabling/1/2/Parameter {ctx.param_index('sub.on')}")
            off = (*source_gates, f"IndexedEnabling/0/2/Parameter {ctx.param_index('sub.on')}")
            items.append(button("Play Native OSC Sub power", rect["power"], "osc_native_power_on.png",
                                "osc_native_power_off.png", remaps=(("Data", handles[0]),), gates=source_gates))
            items.append(action_button(ctx, "Play Native OSC Sub expand", rect["expand"], handles[1], icon="expand", gates=on))
            items.append(entry("Play Native OSC Sub expand off", "Image", {"version": 2, "imageType": "Regular",
                               "colour": "0", "image": expand_off}, rect["expand"], (("Data", handles[1]),), off, "No"))
            for field, widget, caption in (("first", "playSubWaveField", "WAVE"), ("second", "playOscNumberField", "OCT"),
                                           ("pan", "playSubPan", "PAN"), ("level", "playSubLevel", "LEVEL")):
                h = handles[FIELDS.index(field)]
                caption_h = f"WithName[{caption}]/{h}"
                bindings = (("Data", h), ("Caption", caption_h))
                name = f"Play Native OSC Sub {field}"
                if field == "second":
                    items.append(create_name_value_entry(
                        name, widget, rect[field], data_handle=h, caption_handle=caption_h, gates=on, focus="Yes"
                    ))
                    items.append(create_name_value_entry(
                        name + " off", "playSubOctInactive", rect[field],
                        data_handle=h, caption_handle=caption_h, gates=off, focus="No"
                    ))
                else:
                    items.append(component(name, widget, rect[field], bindings, on))
                    disabled = "playSubWaveInactive" if field == "first" else "playOscInactive"
                    items.append(component(name + " off", disabled, rect[field], bindings, off, "No"))
        else:
            items.append(component(f"Play Native OSC {source}", "playNativeOsc", (0, 0, 432, 628),
                                   remaps, source_gates, "Children"))
        choices = ctx.params[ctx.param_index(first_key)]["choices"]
        if group == "abc":
            choices = [choices[i] for i in TYPE_ORDER]
        replacements.append({"key": handles[3], "value": choices})
        if group != "sub":
            replacements.append({"key": handles[2], "value": [*OSC_ROUTING_CHOICES, "Routing controls"]})


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="Generate or check the Play OSC native C++ geometry contract header.")
    ap.add_argument("--check", action="store_true", help="Verify shell-mpc/play_osc_native_contract.h matches JSON.")
    ap.add_argument("--out", default=str(CONTRACT_HEADER), help="Output header path.")
    args = ap.parse_args()
    rendered = render_contract_header(CONTRACT)
    target = Path(args.out)
    if args.check:
        if not target.is_file() or target.read_text(encoding="utf-8") != rendered:
            raise SystemExit(f"{target} is out of date with {CONTRACT}")
    else:
        target.write_text(rendered, encoding="utf-8")
