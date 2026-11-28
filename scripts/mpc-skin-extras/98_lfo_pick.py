# SynthyBoy - the host choices of the LFO: ADD SOURCE (MODULATIONS page) and ADD TARGET (TARGETS page).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""ADD SOURCE and ADD TARGET of an LFO are Selectors that open the MenuOverlay of the host (`fx editor choice menu`).

The host holds the rows, the scrolling and the check mark: a row tap marks, the tap on the header applies and closes
(docs/dsp-knowledge/skin-host-behaviour.md). The plugin only sees the index that was applied (shell-mpc/native_lfo_pick.h).

* ADD SOURCE: one choice, the 57 sources of the matrix (index 0 is the placeholder row).
* ADD TARGET: the 1262 destinations are far too many for one list. GROUP is one choice of the groups of the matrix (the grouping of
  `uibind::DestIndex`: the group of the parameter before " / ", in the order of first appearance), and one TARGET choice per group
  that is shown while its group is the chosen one.

All of them stand over the ADD cell of the shared TARGETS / MODULATIONS editor (98_mod_targets.py), which stays beneath, unchanged. They are
gated by the page of the editor (the LFO tabs and the Play page's expanded LFO) and by the view the plugin publishes (native_lfo_pick.h), so
the other owners of the shared editor (Macros, Env, Filter, Mod, Osc) keep their own ADD cell.
"""

import re
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
for _path in (str(_REPO_ROOT), str(_REPO_ROOT / "scripts")):
    if _path not in sys.path:
        sys.path.insert(0, _path)

from components import style as _style
from components.native.selector.selector import passive_layer, selector_definition
from native_ui_params import (LFO_PICK_ADD_SOURCE, LFO_PICK_GROUP, LFO_PICK_MAX_GROUPS, LFO_PICK_TARGET, LFO_PICK_VIEW,
                              LFO_TAB_NAMES, MOD_TARGETS_VIEW, native_base_index)

CHOICE_MENU = "fx editor choice menu"
ADD_RECT = (1076, 32, 200, 56)       # the ADD cell of the shared editor (mod-targets-native-contract.json "add")
TARGET_RECT = (440, 32, 420, 56)     # left of the page arrows (864 ..), right of BACK
FRAME_LFO, FRAME_PLAY, FRAMES = 3, 6, 8   # the frames of the shared editor (98_mod_targets.py): the LFO tabs, the Play page
# the views the plugin publishes (nativeui::lfopick::View): Hidden, PointSource, AddSource, AddTarget
VIEW_ADD_SOURCE, VIEW_ADD_TARGET, VIEWS = 2, 3, 4
KIND = "lfoNativePicks"


def destination_groups(doc):
    """[(group name, [destination names])] exactly as `uibind::DestIndex` groups them: by the first segment of the parameter group, in order of first appearance."""
    by_key = {p["key"]: p for p in doc["params"]}
    groups = {}
    for item in sorted(doc["modDestinations"], key=lambda d: int(d["choice"])):
        param = by_key[item["key"]]
        group = (param.get("group") or "").split(" / ")[0]
        groups.setdefault(group, []).append(param["name"])
    return list(groups.items())


def _entry(name, kind, rect, remap, gates, focus):
    bounds = {"version": 2, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
              "boundsType": "Absolute", "bounds": " ".join(str(v) for v in rect),
              "additionalInvalidatingHandles": list(gates)}
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]}, "bounds": bounds}


def _choice(ctx, kind, rect, caption):
    ctx.add_component_definition(kind, selector_definition(
        None, CHOICE_MENU, rect[2:], ctx.skins_dir, title="Data", caption_text=caption,
        label_height=_style.size("screen.lfo_smooth.choice.label_height")))


POINT_SOURCE_RECT = [636, 24, 220, 56]   # the SOURCE choice of the point modulation page, right of REMOVE (native_lfo_pick.h)


def _is_lfo_tab(name):
    return name in LFO_TAB_NAMES or re.fullmatch(r"(LFO|RND) \d+", name) is not None


def point_source_cells(ctx, tui):
    """SOURCE of a point modulation: two cells (available / unavailable face) inside the shared `lfoNativeFullEditor`, beside REMOVE, available like MODULATE (points selected)."""
    from native_ui_params import LFO_DETAIL, LFO_PICK_SOURCE
    page = tui["pageData"]
    definitions = {d["key"]: d["value"] for d in page["componentDefinitions"]["localComponentDefinitions"]}
    editor = definitions["lfoNativeFullEditor"]["componentsData"]
    if any(c["componentData"]["name"] == "LFO 1 Native Detail SOURCE" for c in editor):
        return
    base = native_base_index(ctx.param_count)
    detail = base + LFO_DETAIL
    point_view, point = f"IndexedEnabling/1/4/Parameter {detail}", f"Parameter {detail + 5}"
    source = f"Parameter {base + LFO_PICK_SOURCE}"
    kind = "lfoNativeChoice_220x56_source"
    _choice(ctx, kind, POINT_SOURCE_RECT, "SOURCE")
    inactive = _style.apply_control_state(
        {d["key"]: d["value"] for d in page["componentDefinitions"]["localComponentDefinitions"]}[kind], ctx.skins_dir, active=False)
    ctx.add_component_definition(kind + "Inactive", inactive)
    editor.append(_entry("LFO 1 Native Detail SOURCE", kind, POINT_SOURCE_RECT, (("Data", source),),
                         (point_view, f"RangedEnabling/1/4/5/{point}"), "Yes"))
    editor.append(_entry("LFO 1 Native Detail SOURCE unavailable 0", kind + "Inactive", POINT_SOURCE_RECT, (("Data", source),),
                         (point_view, f"IndexedEnabling/0/5/{point}"), "No"))
    page.setdefault("replacementStrings", []).append(
        {"key": source, "value": ["NONE"] + [m["name"] for m in sorted(ctx.doc["modSources"], key=lambda m: int(m["choice"]))]})


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    tabs = [tab for tab in tui["pageData"]["tabs"] if _is_lfo_tab(tab["tabName"]) or tab["tabName"] == "Play"]
    if not tabs:
        return
    point_source_cells(ctx, tui)
    groups = destination_groups(ctx.doc)
    if len(groups) > LFO_PICK_MAX_GROUPS:
        raise ValueError("the matrix has %d destination groups, native_lfo_pick.h holds %d" % (len(groups), LFO_PICK_MAX_GROUPS))
    base = native_base_index(ctx.param_count)
    view = f"Parameter {base + LFO_PICK_VIEW}"
    add_source = f"Parameter {base + LFO_PICK_ADD_SOURCE}"
    group = f"Parameter {base + LFO_PICK_GROUP}"
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    sources = [m["name"] for m in sorted(ctx.doc["modSources"], key=lambda m: int(m["choice"]))]

    # the sizes the plugin knows (uibind::lfoPickLast): index 0 is the placeholder row of every list
    _choice(ctx, "lfoNativePickAddSource", ADD_RECT, "ADD SOURCE")
    _choice(ctx, "lfoNativePickGroup", ADD_RECT, "ADD TARGET")
    _choice(ctx, "lfoNativePickTarget", TARGET_RECT, "TARGET")
    replacements.append({"key": add_source, "value": ["ADD SOURCE"] + sources})
    replacements.append({"key": group, "value": ["ADD TARGET"] + [name for name, _ in groups]})

    children = [
        _entry("Add Source", "lfoNativePickAddSource", ADD_RECT, (("Data", add_source),),
               (f"IndexedEnabling/{VIEW_ADD_SOURCE}/{VIEWS}/{view}",), "Yes"),
        _entry("Add Target Group", "lfoNativePickGroup", ADD_RECT, (("Data", group),),
               (f"IndexedEnabling/{VIEW_ADD_TARGET}/{VIEWS}/{view}",), "Yes"),
    ]
    for index, (name, targets) in enumerate(groups):
        handle = f"Parameter {base + LFO_PICK_TARGET + index}"
        replacements.append({"key": handle, "value": ["TARGET"] + targets})
        children.append(_entry("Add Target " + name, "lfoNativePickTarget", TARGET_RECT, (("Data", handle),),
                               (f"IndexedEnabling/{index + 1}/{len(groups) + 1}/{group}",
                                f"IndexedEnabling/{VIEW_ADD_TARGET}/{VIEWS}/{view}"), "Yes"))
    ctx.add_component_definition(KIND, passive_layer(children))

    editor_view = f"Parameter {base + MOD_TARGETS_VIEW}"
    for tab in tabs:
        frame = FRAME_PLAY if tab["tabName"] == "Play" else FRAME_LFO
        entry = _entry("LFO Native Picks", KIND, (0, 0, 1280, 628), (("Data", view),),
                       (f"IndexedEnabling/{frame}/{FRAMES}/{editor_view}",), "Children")
        tab["componentDefinition"]["componentsData"].append(entry)
