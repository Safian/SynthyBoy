# SynthyBoy - native MPC Mixer controls, meters and PEEK buttons.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
from copy import deepcopy
from pathlib import Path

from native_ui_params import (
    MIXER_STEREO_BASE,
    MIXER_ACTIVE_TAB,
    MIXER_CONTROL,
    MIXER_HOLD_OFFSET,
    MIXER_MUTE_OFFSET,
    MIXER_MUTE_VISUAL,
    MIXER_PEAK_OFFSET,
    MIXER_SOLO_OFFSET,
    MIXER_SOLO_VISUAL,
    MIXER_TABS,
    mixer_peek_index,
)
from components.style import size as _style_size
from components.native.tab_bar.tab_bar import tab_bar

from components.native.mixer_strip.mixer_strip import (
    build_mixer_strip_definitions,
    create_mixer_strip_instance,
    write_mixer_strip_assets,
    render_mixer_strip_background,
    SCALE_LINEAR_DB,
    supply_replacement_strings,
    VARIANT_SOURCE,
    VARIANT_BUS,
    VARIANT_MASTER,
    SOURCE_DESTINATIONS,
    FILTER_DESTINATIONS,
    BUS_DESTINATIONS,
)

# Screen geometry follows MixerFullScreen::layout and MixerStrip::layout.
STRIPS = (
    ("SUB", "sub"), ("A", "osc1"), ("B", "osc2"), ("C", "osc3"), ("NOISE", "noise"),
    ("F1", "filter1"), ("F2", "filter2"), ("BUS 1", "bus1"), ("BUS 2", "bus2"), ("MAIN", "main")
)
PEEK_LABELS = ("FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2")
NATIVE = "Mixer Native "


def _bind_strip_cells(strip, definitions, caption, frame):
    """Place shared native controls on the tab with host parameter bindings."""
    shell = deepcopy(strip)
    cells = [shell]
    root = strip["componentData"]["type"]
    shell["componentData"]["type"] = frame

    def place(parent, origin, bindings, gates):
        for template in definitions[parent]["componentsData"]:
            cell = deepcopy(template)
            model = cell["componentData"]
            bounds = cell["bounds"]
            x, y, w, h = map(int, bounds["bounds"].split())
            x, y = x + origin[0], y + origin[1]
            bounds["bounds"] = f"{x} {y} {w} {h}"
            remaps = cell["handle remapping"]["map"]
            def resolve(handle):
                prefix, slash, key = handle.rpartition("/")
                return prefix + slash + bindings.get(key, key)
            for pair in remaps:
                pair["value"] = resolve(pair["value"])
            children = dict(bindings)
            children.update((pair["key"], pair["value"]) for pair in remaps)
            visible = [*gates, *(resolve(v) for v in bounds.get("additionalInvalidatingHandles", []))]
            if model["type"].startswith(("mixerStripHeader", "soloMutePair", "mixerMeterBlock")):
                place(model["type"], (x, y), children, visible)
                continue
            if model["name"] == "name" and parent.startswith("mixerStripHeader"):
                model["data"].update(type="Text", text=caption)
            dial_caption = {"pan": "PAN", "send1": "B1", "send2": "B2"}.get(model["name"].removesuffix(" off"))
            if dial_caption:
                remaps.append({"key": "Caption", "value": f"WithName[{dial_caption}]/{children['Data']}"})
            model["name"] = strip["componentData"]["name"] + " " + model["name"]
            bounds.update(version=2, additionalInvalidatingHandles=visible)
            cells.append(cell)

    x, y, _, _ = map(int, strip["bounds"]["bounds"].split())
    handles = {m["key"]: m["value"] for m in strip["handle remapping"]["map"]}
    place(root, (x, y), handles, strip["bounds"]["additionalInvalidatingHandles"])
    return cells


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") in ("Mix", "Mixer")), None)
    if tab is None:
        return
    components = tab["componentDefinition"]["componentsData"]
    marker = next((c for c in components if c["componentData"]["name"].startswith("Overlay Instance")), None)
    if marker is None:
        raise ValueError("native Mixer needs its Overlay Instance marker")
    overlay_idx = int(marker["handle remapping"]["map"][0]["value"].split()[1])
    modal_gate = ("RangedEnabling/0/18/33/Parameter %d" % overlay_idx,)
    base = mixer_peek_index(ctx.param_count)
    ctx.add_param_key(base, "shell.mixer_peek")
    ctx.add_param_key(base + MIXER_ACTIVE_TAB, "shell.mixer_active_tab")
    for i in range(len(STRIPS)):
        ctx.add_param_key(base + MIXER_PEAK_OFFSET + i, "shell.mixer_peak_%d" % (i + 1))
        ctx.add_param_key(base + MIXER_HOLD_OFFSET + i, "shell.mixer_hold_%d" % (i + 1))
        ctx.add_param_key(base + MIXER_CONTROL + MIXER_SOLO_OFFSET + i, "shell.mixer_solo_%d" % (i + 1))
        ctx.add_param_key(base + MIXER_CONTROL + MIXER_MUTE_OFFSET + i, "shell.mixer_mute_%d" % (i + 1))
        for field, name in enumerate(("peak_l", "peak_r", "hold_l", "hold_r", "clip_l", "clip_r")):
            ctx.add_param_key(base + MIXER_STEREO_BASE + field * len(STRIPS) + i, f"shell.mixer_{name}_{i + 1}")
        ctx.add_param_key(base + MIXER_SOLO_VISUAL + i, "shell.mixer_solo_visual_%d" % (i + 1))
        ctx.add_param_key(base + MIXER_MUTE_VISUAL + i, "shell.mixer_mute_visual_%d" % (i + 1))

    # Register reusable compound component definitions
    skins_dir = getattr(ctx, "skins_dir", None)
    definitions = build_mixer_strip_definitions(skins_dir)
    for name, def_data in definitions.items():
        ctx.add_component_definition(name, def_data)
        if name in ("mixerStripSource", "mixerStripBus", "mixerStripMaster"):
            ctx.add_component_definition(name + "Frame", dict(def_data, componentsData=[], ignoreMousePresses=True))

    # F1/F2 use the source-strip controls with a -60..+12 dB fader scale.
    filter_art = "mixer_strip_filter_bg.png"
    filter_frame = dict(definitions["mixerStripSource"], componentsData=[], ignoreMousePresses=True)
    filter_frame["backgroundData"] = deepcopy(filter_frame["backgroundData"])
    for state in ("focussed", "unfocussed"):
        filter_frame["backgroundData"][state]["image"] = filter_art
    ctx.add_component_definition("mixerStripFilterFrame", filter_frame)
    if skins_dir:
        render_mixer_strip_background(VARIANT_SOURCE, scale_type=SCALE_LINEAR_DB).save(Path(skins_dir) / filter_art)

    # Write all native artwork assets into skins directory
    if hasattr(ctx, "skins_dir") and ctx.skins_dir:
        write_mixer_strip_assets(Path(ctx.skins_dir))

    # Keep overlay marker, instances, and docked PEEK surfaces
    components[:] = [c for c in components if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance", "Mixer Native Peek "))]

    # Layout tokens for mixer channel strips
    start_x = _style_size("mixer_strip.start_x")
    stride = _style_size("mixer_strip.stride")
    start_y = _style_size("mixer_strip.start_y")
    width = _style_size("mixer_strip.width")
    height = _style_size("mixer_strip.height")

    # Instantiate all 10 channel strips with independent handle remappings
    for i, (caption, source) in enumerate(STRIPS):
        x = start_x + stride * i
        rect = (x, start_y, width, height)
        is_master = (i == 9)
        is_bus = (i in (7, 8))
        variant = VARIANT_MASTER if is_master else VARIANT_BUS if is_bus else VARIANT_SOURCE

        handles = {}
        if variant == VARIANT_SOURCE:
            power = ctx.param_index(source + ".on")
            dest = ctx.param_index(source + ".destination")
            choices = SOURCE_DESTINATIONS if i < 5 else FILTER_DESTINATIONS
            supply_replacement_strings(tui, dest, choices)

            handles["Name"] = ctx.param_index(source + ".level")
            handles["Solo"] = base + MIXER_CONTROL + MIXER_SOLO_OFFSET + i
            handles["Mute"] = base + MIXER_CONTROL + MIXER_MUTE_OFFSET + i
            handles["SoloVisual"] = base + MIXER_SOLO_VISUAL + i
            handles["MuteVisual"] = base + MIXER_MUTE_VISUAL + i
            handles["Power"] = power
            handles["Destination"] = dest
            handles["Level"] = ctx.param_index(source + ".level")
            handles["Peak"] = base + MIXER_PEAK_OFFSET + i
            handles["Hold"] = base + MIXER_HOLD_OFFSET + i
            handles["Pan"] = ctx.param_index(source + ".pan")
            handles["Send1"] = ctx.param_index(source + ".bus1_send")
            handles["Send2"] = ctx.param_index(source + ".bus2_send")
        elif variant == VARIANT_BUS:
            dest = ctx.param_index(source + ".destination")
            supply_replacement_strings(tui, dest, BUS_DESTINATIONS)
            bypass = ctx.param_index(source + ".fx_bypass")

            handles["Name"] = ctx.param_index(source + ".level")
            handles["Solo"] = base + MIXER_CONTROL + MIXER_SOLO_OFFSET + i
            handles["Mute"] = base + MIXER_CONTROL + MIXER_MUTE_OFFSET + i
            handles["SoloVisual"] = base + MIXER_SOLO_VISUAL + i
            handles["MuteVisual"] = base + MIXER_MUTE_VISUAL + i
            handles["Destination"] = dest
            handles["Level"] = ctx.param_index(source + ".level")
            handles["Peak"] = base + MIXER_PEAK_OFFSET + i
            handles["Hold"] = base + MIXER_HOLD_OFFSET + i
        else:
            # Master
            bypass = ctx.param_index("main.fx_bypass")
            handles["Name"] = ctx.param_index("master.volume")
            handles["FxEnable"] = bypass
            handles["Mute"] = base + MIXER_CONTROL + MIXER_MUTE_OFFSET + 9
            handles["FxVisual"] = base + MIXER_SOLO_VISUAL + 9
            handles["MuteVisual"] = base + MIXER_MUTE_VISUAL + 9
            handles["Level"] = ctx.param_index("master.volume")
            handles["Peak"] = base + MIXER_PEAK_OFFSET + i
            handles["Hold"] = base + MIXER_HOLD_OFFSET + i

        for field, name in enumerate(("PeakL", "PeakR", "HoldL", "HoldR", "ClipL", "ClipR")):
            handles[name] = base + MIXER_STEREO_BASE + field * len(STRIPS) + i
        tag = f"{NATIVE}{caption} Strip"
        strip_gates = (modal_gate[0], f"IndexedEnabling/0/{MIXER_TABS}/Parameter {base + MIXER_ACTIVE_TAB}")
        strip = create_mixer_strip_instance(
            variant=variant,
            rect=rect,
            handles=handles,
            gates=strip_gates,
            focus="Yes",
            name=tag,
        )
        frame = "mixerStripFilterFrame" if source in ("filter1", "filter2") else strip["componentData"]["type"] + "Frame"
        components.extend(_bind_strip_cells(strip, definitions, caption, frame))

    # Native 6-tab navigation action row (MIXER=0, FLT 1=1, FLT 2=2, MAIN FX=3, BUS 1=4, BUS 2=5).
    # Gated on modal_gate (< 19) so tabs remain active in MIXER and all 5 docked editors (where
    # mixerPeek keeps Overlay Instance < 19), while hiding cleanly for full-screen modals (MODS/List).
    all_tabs = ("MIXER", *PEEK_LABELS)
    supply_replacement_strings(tui, base, all_tabs)
    peek_y = start_y + height + _style_size("mixer_strip.tab.gap_y")
    rect = (_style_size("mixer_strip.tab.start_x"), peek_y, _style_size("mixer_strip.tab.width"),
            _style_size("mixer_strip.tab.height"))
    components.extend(tab_bar(NATIVE.rstrip(), all_tabs, rect, f"Parameter {base + MIXER_ACTIVE_TAB}",
                              f"Parameter {base}", skins_dir, modal_gate))
