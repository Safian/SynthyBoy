# SynthyBoy - reuse the standalone host control trees in the expanded Play editors.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from pathlib import Path
import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components.native.selector.selector import OSC_TAB_NAMES, passive_layer
from native_ui_params import ENV_TAB_NAMES, ENV_VIEW, LFO_OFFSET, LFO_TAB_NAMES, PLAY_EXPANDED, PLAY_EXPANDED_CLOSE, native_base_index


def _entry(name, kind, rect, data, gate=None, focus="Children", caption=None):
    bounds = {"version": 2 if gate else 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Hide",
              "whenVisible": "Always", "boundsType": "Absolute", "bounds": " ".join(map(str, rect))}
    if gate:
        bounds["additionalInvalidatingHandles"] = [gate]
    bindings = [{"key": "Data", "value": data}]
    if caption:
        bindings.append({"key": "Caption", "value": caption})
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind,
                                            "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": bindings}, "bounds": bounds}


def _components(tab):
    return tab["componentDefinition"]["componentsData"]


def _extract(ctx, tab, kind, match, origin=(0, 0)):
    items = _components(tab)
    children = [child for child in items if match(child)]
    if not children:
        raise ValueError(f"{kind} has no native controls")
    first = items.index(children[0])
    items[:] = [item for item in items if not match(item)]
    if origin != (0, 0):
        xo, yo = origin
        for child in children:
            x, y, w, h = map(int, child["bounds"]["bounds"].split())
            child["bounds"]["bounds"] = f"{x - xo} {y - yo} {w} {h}"
    ctx.add_component_definition(kind, passive_layer(children))
    return first


def _insert_play(play, widget):
    items = _components(play)
    first_marker = next((i for i, c in enumerate(items) if c["componentData"]["name"].startswith("Overlay ")), len(items))
    items.insert(first_marker, widget)


def _close(play, rect, frame, control, view, group):
    _insert_play(play, _entry(f"Play Expanded {group} Close", "oscNativeSetting_action", rect,
                              f"FromGesture/Parameter {control}", f"IndexedEnabling/{frame}/7/{view}",
                              "Yes", f"Parameter {control}"))


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    names = {tab["tabName"] for tab in tui["pageData"]["tabs"]}
    if "Play" not in names:
        return
    play = ctx.tab("Play")
    base = native_base_index(ctx.param_count)
    view = f"Parameter {base + PLAY_EXPANDED}"
    close = base + PLAY_EXPANDED_CLOSE

    if names.intersection((*OSC_TAB_NAMES, "Osc")):
        _insert_play(play, _entry("Play Expanded Oscillator Controls", "oscNativeSurface",
                                  (0, 0, 1280, 628), view, f"RangedEnabling/1/2/7/{view}"))
        _close(play, (1148, 116, 120, 56), 1, close, view, "Oscillator")

    env_names = [n for n in ENV_TAB_NAMES if n in names]
    if env_names:
        env = ctx.tab(env_names[0])
        env_view = f"Parameter {base + ENV_VIEW}"
        selector = "envNativeSelectorStrip"
        fields = "envNativeFullFields"
        root_selector = lambda c: c["componentData"]["type"] == "envNativeSelectorCell"
        # The selector strip belongs to the Play page's expanded editor only: the Env sub-tabs have none (the hardware sub-tab names the envelope)
        _extract(ctx, env, selector, root_selector, origin=(68, 12))
        first = _extract(ctx, env, fields,
                         lambda c: c["componentData"]["name"].startswith("Env Native ")
                         and c["componentData"]["type"] != selector)
        _components(env).insert(first, _entry("Env Native Full Controls", fields, (0, 0, 1280, 628), env_view))
        for other in env_names[1:]:
            tab = ctx.tab(other)
            items = _components(tab)
            at = next(i for i, c in enumerate(items) if c["componentData"]["name"].startswith("Env Native "))
            items[:] = [c for c in items if not c["componentData"]["name"].startswith("Env Native ")]
            items.insert(at, _entry("Env Native Full Controls", fields, (0, 0, 1280, 628), env_view))
        gate = f"IndexedEnabling/3/7/{view}"
        _insert_play(play, _entry("Play Expanded Envelope Controls", fields, (0, 0, 1280, 628), env_view, gate))
        _insert_play(play, _entry("Play Expanded Envelope Selector", selector, (164, 12, 528, 56), env_view, gate))
        _close(play, (58, 12, 96, 56), 3, close + 1, view, "Envelope")

    if "LFO" in names:
        lfo = ctx.tab("LFO")
        panel = "lfoNativeFullEditor"
        first = _extract(ctx, lfo, panel, lambda c: c["componentData"]["name"].startswith("LFO 1 Native "))
        lfo_layout = f"Parameter {base + LFO_OFFSET + 6}"
        _components(lfo).insert(first, _entry("LFO 1 Native Full Editor", panel, (0, 0, 1280, 628), lfo_layout))
        for other in [n for n in LFO_TAB_NAMES[1:] if n in names]:   # the 13 other LFO / RND sub-tabs carry the same surface (one tab page = one screen)
            _components(ctx.tab(other)).append(_entry("LFO 1 Native Full Editor", panel, (0, 0, 1280, 628), lfo_layout))
        _insert_play(play, _entry("Play Expanded LFO Controls", panel, (0, 0, 1280, 628), view,
                                  f"RangedEnabling/5/6/7/{view}"))
        _close(play, (968, 210, 96, 56), 5, close + 2, view, "LFO")
