# SynthyBoy - native MPC Mixer controls, meters and PEEK buttons.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
from PIL import Image, ImageDraw

from mixer_tabs import (
    MIXER_TABS,
    MIXER_TAB_LABELS,
    mixer_tab_rect,
    render_mixer_tab_filmstrip,
    render_mixer_tab_hit,
)
from native_skin_font import semibold
from native_ui_params import (
    MIXER_STEREO_BASE, MIXER_STEREO_FIELDS,
    MIXER_ACTIVE_TAB,
    MIXER_CONTROL,
    MIXER_HOLD_OFFSET,
    MIXER_MUTE_OFFSET,
    MIXER_MUTE_VISUAL,
    MIXER_PEAK_OFFSET,
    MIXER_PEEK_TARGETS,
    MIXER_SOLO_OFFSET,
    MIXER_SOLO_VISUAL,
    mixer_peek_index,
)
from components.style import color as _style_color, rgb as _style_rgb, size as _style_size

from components.native.mixer_strip.mixer_strip import (
    build_mixer_strip_definitions,
    create_mixer_strip_instance,
    write_mixer_strip_assets,
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


def entry(name, kind, data, rect, handles, gates=(), focus=False):
    bounds = {"version": 2, "acceptsHWFocus": "Yes" if focus else "No",
              "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute",
              "bounds": "%d %d %d %d" % rect, "additionalInvalidatingHandles": list(gates)}
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": key, "value": "Parameter %d" % idx}
                                                     for key, idx in handles]},
            "bounds": bounds}


def button(name, parameter, rect, gates, index=1, count=1, art=("toggle_on.png", "toggle_off.png")):
    data = {"version": 2, "onImage": art[0], "offImage": art[1], "buttonId": index, "numButtonsInGroup": count,
            "handleName": "Data", "gestureBehaviour": "Instant"}
    return entry(name, "Button", data, rect, (("Data", parameter),), gates, focus=True)


def tab_filmstrip_art(skins_dir, caption, tab_index):
    """Generates a 6-frame FilmStrip PNG highlighting tab `tab_index` when active."""
    return render_mixer_tab_filmstrip(skins_dir, caption, tab_index)


def tab_hit_art(skins_dir):
    return render_mixer_tab_hit(skins_dir)


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
    for name, def_data in build_mixer_strip_definitions(skins_dir).items():
        ctx.add_component_definition(name, def_data)

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
        components.append(create_mixer_strip_instance(
            variant=variant,
            rect=rect,
            handles=handles,
            gates=strip_gates,
            focus="Yes",
            name=tag,
        ))

    # Native 6-tab navigation action row (MIXER=0, FLT 1=1, FLT 2=2, MAIN FX=3, BUS 1=4, BUS 2=5).
    # Gated on modal_gate (< 19) so tabs remain active in MIXER and all 5 docked editors (where
    # mixerPeek keeps Overlay Instance < 19), while hiding cleanly for full-screen modals (MODS/List).
    all_tabs = MIXER_TAB_LABELS
    supply_replacement_strings(tui, base, all_tabs)
    hit_png = tab_hit_art(Path(ctx.skins_dir)) if skins_dir else "toggle_off.png"

    for k, caption in enumerate(all_tabs):
        rect = mixer_tab_rect(k)
        if skins_dir:
            film = tab_filmstrip_art(Path(skins_dir), caption, k)
            vis_data = {
                "version": 2,
                "imageType": "FilmStrip",
                "colour": "0",
                "image": film,
                "numFrames": MIXER_TABS,
                "handleName": "Data",
            }
            components.append(entry(f"{NATIVE}{caption} Tab Visual", "Image", vis_data,
                                    rect, (("Data", base + MIXER_ACTIVE_TAB),), modal_gate, focus=False))
        components.append(button(f"{NATIVE}{caption} Peek", base, rect, modal_gate,
                                 index=k, count=MIXER_TABS, art=(hit_png, hit_png)))
