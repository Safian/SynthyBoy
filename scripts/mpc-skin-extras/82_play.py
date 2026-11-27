# SynthyBoy - MPC skin extra: the Play page of the OVERLAY skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json: the page "Play", the only tab of the first function key since 2026-11-18).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Inactive unless skin-meta.json says "variant": "overlay" (the production skin stays byte for byte as it is). Made for the plugin built with SYNTHYBOY_MPC_OVERLAY: its parameters after the
# pad pressure ones are "Overlay Instance", the selectors "Play Osc", "Play Env", "Play LFO" and the 24 proxy parameters (shell-mpc/play_params.h, docs/MPC.md "Play page"):
#   selectors  index = entries of docs/params.json + 4 LFO cursors + 32 spectrum columns + 2 pad pressure + 1 Overlay Instance + 0 .. 2
#   proxies    after the selectors, group after group (oscillator, envelope, LFO), eight each
#   Q proxies  "Q 1" .. "Q 16" after the last proxy (shell-mpc/q_router.h): what the 16 hardware knobs (Q-Link 1 .. 16) of EVERY Play sub-tab are mapped to
# Compact Play combines native selectors and dials with overlay graphs. Physical Q-Links use the Q proxies; the expanded editors have their own drawing and input paths.
import copy
import json
import os
from pathlib import Path
import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components.native.selector.selector import (
    OSC_INSTANCE_NAMES,
    OSC_SOURCE_OVERLAY_NAME,
    build_choice_definition,
    build_standalone_osc_source_definitions,
    selector_definition,
)
from components.native.active_text.active_text import (
    active_text_child,
    write_active_text_assets,
)
from components.native.level.level import (
    PLAY_SMOOTH_RECTS,
    build_native_smooth_dial_with_indicator_definition,
    build_native_smooth_dual_state_pair,
    compact_dial_definition,
    synthyboy_dial_data,
)
from components.style import size as _style_size
from native_play.osc import add_compact_osc
from native_ui_params import PLAY_LFO_STANDARD, PLAY_OSC, native_base_index

HERE = Path(__file__).resolve().parent
LAYOUT_JSON = HERE.parent.parent / "shell-mpc" / "overlay" / "overlay_layout.json"   # (skin-meta "layout_path" or the environment MPC_OVERLAY_LAYOUT name another; a layout without play views leaves this module idle)
TABS = ("Play",)   # ONE Play page since 2026-11-18 (the sub-tabs "Play Env" and "Play LFO" are gone: one page, the Q groups of the three thirds in reading order on Q 1 .. 12)
LFO_CURSORS, SPECTRUM_COLUMNS, PAD_PRESSURE_PARAMS, OVERLAY_INSTANCE = 4, 32, 2, 1
GROUPS = 3
SLOTS = 8
Q_PROXIES = 16
# (prefix, instances, [(key suffix of the real parameter, template suffix if the production skin has no control for it, short name)] x 8) ; the names are the ones of play_params.h
TABLE = (
    ("osc", 3, [("frame", None, "WT Position"), ("unison.voices", None, "Unison"), ("unison.detune", None, "Detune"), ("unison.blend", None, "Blend"),
                ("phase", None, "Phase"), ("random_phase", None, "Rand Phase"), ("pan", None, "Pan"), ("level", None, "Level")]),
    ("env", 6, [("attack", None, "Attack"), ("hold", None, "Hold"), ("decay", None, "Decay"), ("sustain", None, "Sustain"),
                ("release", None, "Release"), ("attack_curve", None, "Atk Curve"), ("decay_curve", None, "Dec Curve"), ("release_curve", "decay_curve", "Rel Curve")]),
    ("lfo", 10, [("rate", None, "Rate"), ("fade_in", None, "Rise"), ("delay", None, "Delay"), ("smooth_time", "fade_in", "Smooth"),
                 ("smooth", "@toggle", "Smooth On"), ("sync", None, "Sync"), ("sync_type", None, "Trigger"), ("division", None, "Division")]),
)
SELECTOR_KEYS = ("shell.play_osc", "shell.play_env", "shell.play_lfo")
SELECTOR_SHORT = ("Osc", "Env", "LFO")
SELECTOR_TEMPLATES = ("sliderWithFocusMenu",) * 3   # the widget of a selector: the selector slider of the skin (touch and drag; observed on the Key 61: the radio group did not answer a tap, the selector does)
SELECTOR_COMPONENTS = ("playOscSelector", "playModSelector", "playModSelector")
# Detection markers occupy the unused top eight pixels, not selector space.
SELECTOR_RECTS = tuple(tuple(rect) for rect in _style_size('play.selector.rects'))
SLOT_H = _style_size("play.grid.slot_heights")
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = _style_size('play.qlink.bounds_yh')


def _rect(c):
    return tuple(int(v) for v in c["bounds"]["bounds"].split())


def _set(c, rect):
    c["bounds"]["bounds"] = "%d %d %d %d" % tuple(rect)


def _param_of(c):
    m = c["handle remapping"]["map"]
    return int(m[0]["value"].split()[1]) if m else None


def _controls_by_param(tui):
    """{parameter index: (tab, control)} of the controls of the whole skin that are bound to one parameter (the first one found)."""
    found = {}
    for tab in tui["pageData"]["tabs"]:
        if tab["tabName"] in TABS:
            continue
        for c in tab["componentDefinition"]["componentsData"]:
            if c["componentData"]["type"] in SLOT_H and c["handle remapping"]["map"]:
                lst = found.setdefault(_param_of(c), [])
                if not lst or lst[0][0] is tab:   # the controls of the FIRST tab that has one; a control may have variants there (the LFO tab has three Division controls, each valid for one sync choice)
                    lst.append((tab, c))
    return found


def _seg_labels(tab, control):
    prefix = control["componentData"]["name"] + " seg "
    return [c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "Label" and c["componentData"]["name"].startswith(prefix)]


def _rebind_handles(bounds, rebind):
    """The handles a control is valid for ("IndexedEnabling/1/5/Parameter 189": valid when parameter 189 has the choice 1) follow the proxies: Parameter N of a real parameter becomes its proxy."""
    handles = bounds.get("additionalInvalidatingHandles")
    if handles:
        out = []
        for h in handles:
            head, _, tail = h.rpartition("Parameter ")
            if int(tail) not in rebind:
                raise ValueError("a control of the production skin depends on parameter %s, which has no Play proxy (%s)" % (tail, h))
            out.append("%sParameter %d" % (head, rebind[int(tail)]))
        bounds["additionalInvalidatingHandles"] = out


def _copy_control(template, name, param, rect, rebind=None):
    tab, c = template
    out = copy.deepcopy(c)
    out["componentData"]["name"] = name
    out["handle remapping"]["map"][0]["value"] = "Parameter %d" % param
    _rebind_handles(out["bounds"], rebind or {})
    _set(out, rect)
    segs = []
    for lab in _seg_labels(tab, c):
        s = copy.deepcopy(lab)
        s["componentData"]["name"] = lab["componentData"]["name"].replace(c["componentData"]["name"], name)
        s["handle remapping"]["map"] = [{"key": "Data", "value": "Parameter %d" % param}]
        dx, dy = _rect(lab)[0] - _rect(c)[0], _rect(lab)[1] - _rect(c)[1]
        _set(s, (rect[0] + dx, rect[1] + dy) + _rect(lab)[2:])
        segs.append(s)
    return out, segs


def _require(component, *gates):
    bounds = component["bounds"]
    bounds["version"] = 2
    bounds["showWhenDataModelInvalid"] = "Hide"
    bounds.setdefault("additionalInvalidatingHandles", []).extend(gates)


FULL_FRAME_LAST_VISIBLE = 18   # the host widgets of the Play page are visible while the "Overlay Instance" frame is 0 .. 18 (slots use 1 .. 17); the plugin takes frame 24 while a plugin-drawn full screen is open


def _hide_while_full_screen(component, overlay_param):
    """The component is hidden (and so is not touchable) while the plugin's full-screen page is open: the plugin reports the "Overlay Instance" frame 24, a frame the skin shows as blank (the instance
    pictures read it as "not told yet"), and RangedEnabling/0/18/33 is valid for the frames 0 .. 18 only (docs/ui-reference.md 7.4: quantised to 33 steps, the film strip's 33 frames)."""
    _require(component, "RangedEnabling/0/%d/33/Parameter %d" % (FULL_FRAME_LAST_VISIBLE, overlay_param))


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    layout = json.loads(Path((ctx.meta or {}).get("layout_path") or os.environ.get("MPC_OVERLAY_LAYOUT") or LAYOUT_JSON).read_text(encoding="utf-8"))
    play_views = [v for v in layout["views"] if v["kind"] == "play"]
    if not play_views:
        return   # another plugin's layout: no Play page
    if [v["tab"] for v in play_views] != list(TABS):
        raise ValueError("overlay_layout.json has the play views %s, this module builds %s" % ([v["tab"] for v in play_views], list(TABS)))
    first = ctx.param_count + int(layout.get("paramsBefore", LFO_CURSORS + SPECTRUM_COLUMNS + PAD_PRESSURE_PARAMS)) + OVERLAY_INSTANCE
    sel_idx = [first + g for g in range(GROUPS)]
    proxy_idx = [[first + GROUPS + g * SLOTS + s for s in range(SLOTS)] for g in range(GROUPS)]
    q_idx = [first + GROUPS + GROUPS * SLOTS + i for i in range(Q_PROXIES)]
    for i, idx in enumerate(q_idx):
        ctx.add_param_key(idx, "shell.q_%d" % (i + 1))
    for g, (prefix, instances, slots) in enumerate(TABLE):
        ctx.add_param_key(sel_idx[g], SELECTOR_KEYS[g])
        for s, (suffix, _, _) in enumerate(slots):
            key = "shell.play_%s_%s" % (prefix, suffix.replace(".", "_"))
            ctx.add_param_key(proxy_idx[g][s], key)
    # sliderWithFocusMenu gets its choices from pageData.replacementStrings;
    # without this entry the host opens an empty MenuOverlay for a valid
    # selector parameter.  Keep the Play LFO range at 1..10: RND is a
    # standalone LFO page, not a compact Play selector value.
    replacement = tui["pageData"].setdefault("replacementStrings", [])
    standard_lfo = native_base_index(ctx.param_count) + PLAY_LFO_STANDARD
    ctx.add_param_key(standard_lfo, "shell.play_lfo_standard")
    selector_choices = (
        list(OSC_INSTANCE_NAMES),
        ["Env %d" % n for n in range(1, 7)],
        ["LFO %d" % n for n in range(1, 11)],
    )
    for g in range(GROUPS):
        key = "Parameter %d" % sel_idx[g]
        entry = next((r for r in replacement if r.get("key") == key), None)
        if entry is None:
            replacement.append({"key": key, "value": selector_choices[g]})
        else:
            entry["value"] = selector_choices[g]
    definitions = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    stock_menu = next(d["value"] for d in definitions if d["key"] == "menu overlay")
    selector_source = next(d for d in definitions if d["key"] == "sliderWithFocusMenu")["value"]
    unipolar = next(d["value"] for d in definitions if d["key"] == "knobUni")
    ctx.add_component_definition("compactNativeDial", compact_dial_definition(unipolar, skins_dir=ctx.skins_dir))
    rise_dial = compact_dial_definition(unipolar, skins_dir=ctx.skins_dir)
    rise_rects = {name: tuple(rect) for name, rect in _style_size('play.rise.seed_rects').items()}
    for item in rise_dial["componentsData"]:
        name = item["componentData"]["name"]
        item["bounds"]["bounds"] = "%d %d %d %d" % tuple(rise_rects[name])
    ctx.add_component_definition("compactNativeRiseDial", rise_dial)
    on_img, off_img = write_active_text_assets(
        ctx.skins_dir,
        "SMOOTH",
        width=PLAY_SMOOTH_RECTS["name"][2],
        height=PLAY_SMOOTH_RECTS["name"][3],
    )
    smooth_active_def = build_native_smooth_dial_with_indicator_definition(
        rects=PLAY_SMOOTH_RECTS,
        on_image=on_img,
        off_image=off_img,
        interactive_toggle=True,
        handle_name="Active",
        skins_dir=ctx.skins_dir,
        active=True,
    )
    for comp in smooth_active_def["componentsData"]:
        if comp["componentData"]["name"] == "name":
            comp.update(active_text_child("name", on_img, off_img, PLAY_SMOOTH_RECTS["name"], handle="Active"))

    smooth_passive_def = build_native_smooth_dial_with_indicator_definition(
        rects=PLAY_SMOOTH_RECTS,
        on_image=on_img,
        off_image=off_img,
        interactive_toggle=True,
        handle_name="Active",
        skins_dir=ctx.skins_dir,
        active=False,
    )
    for comp in smooth_passive_def["componentsData"]:
        if comp["componentData"]["name"] == "name":
            comp.update(active_text_child("name", on_img, off_img, PLAY_SMOOTH_RECTS["name"], handle="Active"))

    ctx.add_component_definition("compactNativeSmoothWithIndicator", smooth_active_def)
    ctx.add_component_definition("compactNativeSmoothTime", smooth_active_def)
    ctx.add_component_definition("compactNativeSmoothReadout", smooth_passive_def)
    for component_name, overlay_name, frame_asset, neutral, width in (
        (SELECTOR_COMPONENTS[0], OSC_SOURCE_OVERLAY_NAME, "play_osc_selector_frame.png", True, SELECTOR_RECTS[0][2]),
        (SELECTOR_COMPONENTS[1], "fx editor choice menu", "play_selector_frame.png", False, SELECTOR_RECTS[1][2]),
    ):
        bounds = (0, 0, width, _style_size("height.high"))
        selector_def = selector_definition(
            selector_source, overlay_name, bounds[2:], ctx.skins_dir,
            title="Data", frame_name="Play selector frame",
            filename=frame_asset, neutral=neutral, arrows=False, label_height=_style_size("play.font.24"),
            justification="horizontallyCentred verticallyCentred"
        )
        definitions.append({"key": component_name, "value": selector_def})
        if overlay_name == OSC_SOURCE_OVERLAY_NAME and not any(d["key"] == overlay_name for d in definitions):
            definitions.extend(build_standalone_osc_source_definitions())
    by_param = _controls_by_param(tui)
    # the templates of the selectors: a control of the production skin of the same widget
    selector_template = {}
    for kind in set(SELECTOR_TEMPLATES):
        found = [v[0] for v in by_param.values() if v[0][1]["componentData"]["type"] == kind]
        if not found:
            raise ValueError("the skin has no %s control to copy for a Play selector" % kind)
        selector_template[kind] = found[0]
    for tab_name in TABS:
        tab = ctx.tab(tab_name)
        items = tab["componentDefinition"]["componentsData"]
        extra = []
        # the selectors, one under each panel
        for p in range(GROUPS):
            # The panel graph remains plugin-drawn, but its instance selector
            # is a host-native control in the compact Play layout.  It is
            # hidden atomically by the Overlay Instance frame while a full
            # screen/modal overlay is open, so touch and Q never have two
            # owners.
            kind = SELECTOR_TEMPLATES[p]
            x, y, w, h = SELECTOR_RECTS[p]
            c, segs = _copy_control(selector_template[kind], "Play %s Select" % SELECTOR_SHORT[p], sel_idx[p], (x, y, w, h))
            c["componentData"]["type"] = SELECTOR_COMPONENTS[p]
            if kind == "radio3":
                if len(segs) != 3:
                    raise ValueError("the radio3 template has %d captions" % len(segs))
                for k, lab in enumerate(sorted(segs, key=lambda s: _rect(s)[0])):
                    lab["componentData"]["data"]["text"] = str(k + 1)
            extra.append(c)
            extra.extend(segs)
        # Sustain and Rise are independent of time units. The existing
        # Play proxies follow the selected module instance, including preset changes.
        # Integrator verification: geometry matches LfoCompact
        LFO_COMPACT_RECT = _style_size('play.lfo.rise.rect')
        compact_dials = (
            ("Play Env Attack", 1, "attack", "Attack", _style_size('play.env.attack.rect'), "compactNativeDial"),
            ("Play Env Decay", 1, "decay", "Decay", _style_size('play.env.decay.rect'), "compactNativeDial"),
            ("Play Env Sustain", 1, "sustain", "Sustain", _style_size('play.env.sustain.rect'), "compactNativeDial"),
            ("Play Env Release", 1, "release", "Release", _style_size('play.env.release.rect'), "compactNativeDial"),
            ("Play LFO Rise", 2, "fade_in", "Rise", LFO_COMPACT_RECT, "compactNativeRiseDial"),
        )
        knob_template = next(v[0] for v in by_param.values() if v[0][1]["componentData"]["type"] == "knobUni"
                             and not v[0][1]["bounds"].get("additionalInvalidatingHandles"))
        for name, group_idx, slot_key, caption, rect, dial_type in compact_dials:
            param_idx = next(i for i, slot in enumerate(TABLE[group_idx][2]) if slot[0] == slot_key)
            dial, _ = _copy_control(knob_template, name, proxy_idx[group_idx][param_idx], rect)
            dial["componentData"]["type"] = dial_type
            dial["handle remapping"]["map"].append({"key": "Caption", "value": "WithName[%s]/Parameter %d" % (caption, proxy_idx[group_idx][param_idx])})
            if group_idx == 2:
                standard = f"IndexedEnabling/1/2/Parameter {standard_lfo}"
                _require(dial, standard)
            extra.append(dial)
        smooth_time_param = proxy_idx[2][next(i for i, slot in enumerate(TABLE[2][2]) if slot[0] == "smooth_time")]
        smooth_on_param = proxy_idx[2][next(i for i, slot in enumerate(TABLE[2][2]) if slot[0] == "smooth")]
        active_smooth, passive_smooth = build_native_smooth_dual_state_pair(
            smooth_time_param,
            smooth_on_param,
            _style_size('play.lfo.smooth.rect'),
            active_type="compactNativeSmoothWithIndicator",
            passive_type="compactNativeSmoothReadout",
            name_prefix="Play LFO Smooth",
        )
        active_smooth["componentData"]["name"] = "Play LFO Smooth"
        passive_smooth["componentData"]["name"] = "Play LFO Smooth Off"
        standard = f"IndexedEnabling/1/2/Parameter {standard_lfo}"
        _require(active_smooth, standard)
        _require(passive_smooth, standard)
        extra.extend((active_smooth, passive_smooth))
        add_compact_osc(tui, ctx, extra, native_base_index(ctx.param_count) + PLAY_OSC, sel_idx[0],
                        selector_source, stock_menu, unipolar)
        links = {f"Q-Link {i + 1}": index for i, index in enumerate(q_idx)}
        for c in extra:
            _hide_while_full_screen(c, first - OVERLAY_INSTANCE)
        items.extend(extra)
        entry = [m for m in qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1]
        if len(entry) != 1:
            raise ValueError("Q-Links.json has %d entries for the tab %r" % (len(entry), tab_name))
        entry[0]["Q-Links"] = links
        entry[0]["Bank Direction"] = "Row"
        tab["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H) for b in tab["qlinkBoundsData"]]
