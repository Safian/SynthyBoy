# SynthyBoy - compact Play oscillator controls in the MPC stock skin.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import json
from pathlib import Path

from PIL import Image

import sys

_ROOT = str(Path(__file__).resolve().parents[2])
_SCRIPTS = str(Path(__file__).resolve().parents[1])
for _p in (_ROOT, _SCRIPTS):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from components.native.level.level import compact_dial_definition, level_definition
from components.native.name_value.name_value import (
    build_name_value_definition,
    create_name_value_entry,
)
from components.native.pan.pan import pan_definition
from components.native.selector.selector import (
    append_choice_rows,
    write_source_navigation_assets,
    write_wave_selector_icon,
    OSC_INSTANCE_NAMES,
    OSC_ROUTING_CHOICES,
    _value_label,
    build_choice_definition,
    selector_definition,
)
from components.style import size as _style_size, bind_control_active
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


def setup_popups(ctx, stock_menu):
    marker_paths = {}
    for x, rgb in (*MODAL_MARKERS, *POPUP_MARKERS):
        filename = f"play_osc_modal_{x}.png"
        Image.new("RGB", (6, 6), f"#{rgb}").save(ctx.skins_dir / filename)
        marker_paths[x] = filename
    for name, (x, _) in zip((ROUTE_MENU, CHOICE_MENU, OCTAVE_PAD), POPUP_MARKERS):
        definition = copy.deepcopy(stock_menu)
        if name == OCTAVE_PAD:
            append_choice_rows(ctx, definition, [str(i) for i in range(-4, 5)], "octave")
        for offset in (16, 28, 40, x):
            mark = entry(f"Play OSC marker {offset}", "Image", {"version": 2, "imageType": "Regular",
                         "colour": "0", "image": marker_paths[offset]}, (offset, 120, 6, 6), focus="No")
            mark["bounds"]["showWhenDataModelInvalid"] = "Show"
            definition["componentsData"].append(mark)
        ctx.add_component_definition(name, definition)
        if name == CHOICE_MENU:
            sub = copy.deepcopy(definition)
            append_choice_rows(ctx, sub, ctx.params[ctx.param_index("sub.waveform")]["choices"], "wave")
            ctx.add_component_definition("play sub waveform", sub)


def add_compact_osc(tui, ctx, items, base, selector, selector_template, stock_menu, dial_template):
    geometry = json.loads(CONTRACT.read_text(encoding="utf-8"))
    layout_handle = f"Parameter {base}"
    setup_popups(ctx, stock_menu)

    artwork = write_source_navigation_assets(ctx.skins_dir)

    for name, popup, size, text_height in (
        ("playOscRouteField", ROUTE_MENU, _style_size('play.osc.routing.size'), _style_size("play.font.13")),
        ("playOscChoiceField", CHOICE_MENU, _style_size('play.osc.choice.size'), _style_size("play.font.23")),
    ):
        ctx.add_component_definition(name, build_choice_definition(
            selector_template, popup, (0, 0, *size), "Value", label_height=text_height))
    oct_w, oct_h = geometry["sub"]["second"][2:]
    oct_visual_h = _style_size("height.intermediate")
    oct_top = (oct_h - oct_visual_h) // 2

    def build_sub_oct_definition(name):
        definition = build_name_value_definition(
            name=name,
            rect=(0, 0, oct_w, oct_visual_h),
            skins_dir=ctx.skins_dir,
            popup=OCTAVE_PAD,
        )
        if oct_top:
            for child in definition["componentsData"]:
                bx, by, bw, bh = map(int, child["bounds"]["bounds"].split())
                child["bounds"]["bounds"] = f"{bx} {by + oct_top} {bw} {bh}"
        return definition

    ctx.add_component_definition("playOscNumberField", build_sub_oct_definition("playOscNumberField"))
    wave_icon = write_wave_selector_icon(ctx.skins_dir)
    ctx.add_component_definition("playSubWaveField", selector_definition(
        selector_template, "play sub waveform", tuple(geometry["sub"]["first"][2:]), ctx.skins_dir,
        icon=wave_icon, title="Data", name="Caption"))
    for name, with_steps in (("playOscTableField", True), ("playOscContentField", False)):
        source = copy.deepcopy(selector_template)
        source["actions"] = []
        centre = _style_size('play.osc.source.stepped_rect') if with_steps else _style_size('play.osc.source.rect')
        children = [button("browse", centre, artwork["empty"], index=2, count=4),
                    _value_label("source name", centre, _style_size("play.font.17"))]
        if with_steps:
            children += [button("previous", _style_size('play.osc.source.previous_rect'), artwork["left"], index=1, count=4),
                         button("next", _style_size('play.osc.source.next_rect'), artwork["right"], index=3, count=4)]
        source["componentsData"] = children
        source["disableCoarseDataWheel"] = True
        ctx.add_component_definition(name, source)

    ctx.add_component_definition("playOscPan", pan_definition(dial_template, ctx.skins_dir))
    ctx.add_component_definition("playSubPan", pan_definition(dial_template, ctx.skins_dir))
    ctx.add_component_definition("playSubLevel", level_definition(dial_template, ctx.skins_dir))
    rects = geometry["abc"]
    children = [
        button("power", rects["power"], "osc_native_power_on.png", "osc_native_power_off.png",
               remaps=(("Data", "power"),)),
        action_button(ctx, "expand", rects["expand"], "expand", icon="expand"),
        component("first", "playOscChoiceField", rects["first"], (("Data", "first"),)),
        component("routing", "playOscRouteField", rects["routing"], (("Data", "routing"),),
                  ("RoutingVisible",)),
    ]
    for gate, kind in (("Wavetable", "playOscTableField"), ("Content", "playOscContentField"),
                       ("Filter", "compactNativeRiseDial")):
        remaps = [("Data", "second")]
        if gate == "Filter":
            remaps.append(("Caption", "WithName[FILTER]/second"))
        children.append(component("second", kind, rects["second"], remaps, (gate,),
                                  "Children" if gate in ("Wavetable", "Content") else "Yes"))
    for field, kind in (("pan", "playOscPan"), ("level", "compactNativeDial")):
        remaps = (("Data", field), ("Caption", f"WithName[{field.upper()}]/{field}"))
        children.append(component(field, kind, rects[field], remaps))
    definitions = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    replacements = tui["pageData"]["replacementStrings"]
    ctx.add_param_key(base, "shell.play_osc_native_view")
    for ordinal, (source, group, first_key) in enumerate(zip(OSC_INSTANCE_NAMES, OSC_LAYOUT, ENGINE_FIRST)):
        start = base + 1 + ordinal * len(FIELDS)
        handles = [f"Parameter {start + i}" for i in range(len(FIELDS))]
        for index, field in enumerate(FIELDS):
            ctx.add_param_key(start + index, f"shell.play_osc_native_{ordinal + 1}_{field}")
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
        source_gates = (visible, f"IndexedEnabling/{ordinal}/5/Parameter {selector}")
        if group == "sub":
            # Place the cells directly on the tab, like the adjacent Play ENV dials.
            # The XL can show the buttons of a wrapper while nested widget bodies remain blank.
            rect = geometry["sub"]
            first_item = len(items)
            items.append(button("Play Native OSC Sub power", rect["power"], "osc_native_power_on.png",
                                "osc_native_power_off.png", remaps=(("Data", handles[0]),), gates=source_gates))
            items.append(action_button(ctx, "Play Native OSC Sub expand", rect["expand"], handles[1], icon="expand", gates=source_gates))
            for field, widget, caption in (("first", "playSubWaveField", "WAVE"), ("second", "playOscNumberField", "OCT"),
                                           ("pan", "playSubPan", "PAN"), ("level", "playSubLevel", "LEVEL")):
                h = handles[FIELDS.index(field)]
                caption_h = f"WithName[{caption}]/{h}"
                bindings = (("Data", h), ("Caption", caption_h))
                name = f"Play Native OSC Sub {field}"
                if field == "second":
                    items.append(create_name_value_entry(
                        name, widget, rect[field], data_handle=h, caption_handle=caption_h, gates=source_gates, focus="Yes"
                    ))
                else:
                    items.append(component(name, widget, rect[field], bindings, source_gates))
            for control in items[first_item + 1:]:
                bind_control_active(control, handles[0], ctx, definitions)
        else:
            # Bind each shared control on the tab, as for Sub. The XL does
            # not resolve all nested handles through a full-panel wrapper.
            fields = dict(zip(FIELDS, handles))
            for template in children:
                control = copy.deepcopy(template)
                model = control["componentData"]
                field = model["name"]
                model["name"] = f"Play Native OSC {source} {field}"
                for mapping in control["handle remapping"]["map"]:
                    prefix, slash, key = mapping["value"].rpartition("/")
                    mapping["value"] = prefix + slash + fields.get(key, key)
                bounds = control["bounds"]
                gates = [mode_gates[key] for key in bounds.get("additionalInvalidatingHandles", ())]
                if "Invalid" in gates:
                    continue
                bounds.update(version=2, additionalInvalidatingHandles=[*source_gates, *gates])
                if field != "power":
                    bind_control_active(control, handles[0], ctx, definitions)
                items.append(control)
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
