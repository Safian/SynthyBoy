#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Hand-run tool: write the Header definitions, instances and faces of one screen family into the skin from its spec list.

Nothing here runs at build time. The input is a skin spec file (components/native/header/skin-<family>.json):

  {"file": "skin/defs_osc.json",            the definition file of the family
   "layer": "oscNativeSettingsLayer",        the definition that holds the header instances (omit it when every instance names a "tab")
   "tab_file": "skin/TUI.json",              optional: the file whose tabs hold the instances that name a "tab" (default skin/TUI.json)
   "prefix": "oscHeader",                    every definition / instance type with this prefix is owned (replaced) by the tool
   "forms": ["osc-abc", ...],                catalogue example ids (components/native/header/catalog.json) that have a "skin" block
   "instances": [{"name", "form", "title", "power": "Parameter N", "data", "gate": [...], "remap": {...}}],
   "retire": {"definitions": [...], "components": {"<definition>": ["<component name>", ...]}, "images": [...]}}      optional: what the header replaces

A header without POWER (Env, LFO, the curve screens, Mod, Master, ...) has no "power" and gets no HdrPower / HdrOn / HdrOff. Its instance
gives either "title_handle" (any always valid parameter: the title is `WithName[<title>]/<title_handle>`) or, for a static title Label
(catalogue skin option "static_title"), no "title" at all: its "remap" names HdrTitle. Optional per instance: "title_gate" (one handle: the
title alone follows it, remapped as HdrTitleGate), "show" (showWhenDataModelInvalid, default Hide), "focus" (acceptsHWFocus, default Yes) and
"gate" (the additional invalidating handles; omit it for version 1 bounds without the list, an empty list keeps version 2 bounds).
An instance that lives in a TUI tab body instead of a definition names "tab" (tab name) and "replaces" (the component names of that tab it
supersedes: they are removed and the instance takes the place of the first one; a later run finds the owned instance by its name).
"retire" lists the definitions, the components inside definitions (names already gone are ignored) and the pictures of the controls the
header replaces; all are removed (idempotent).

For every form `header.header_definitions(spec)` returns the definitions and the faces; the tool replaces the owned
definitions and instances in the file (ordered, in place), writes the faces to skin/ (hdr_*.png) and removes owned faces
nothing references any more. An instance gets the names HdrTitle (WithName[<title>]/<power>), HdrPower, HdrOn / HdrOff
(IndexedEnabling/1|0/2/<power>) from its `power`; everything else comes from its `remap`.

  python3 scripts/header-skin.py components/native/header/skin-osc.json
  python3 scripts/skin-format.py && python3 scripts/skin-baseline-update.py     # afterwards, as after every skin edit
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from components.native.header import header  # noqa: E402
from components.native.selector.selector import save_png_filter0  # noqa: E402

SKIN = ROOT / "skin"


def owned(key, prefix):
    return key.startswith(prefix) or re.match(r"Native(Active|Inactive) " + re.escape(prefix), key) is not None


def instance_component(inst, form_key):
    power = inst.get("power")
    remap = [("Data", inst["data"])]
    if power:
        remap += [("HdrTitle", f"WithName[{inst['title']}]/{power}"), ("HdrPower", power),
                  ("HdrOn", f"IndexedEnabling/1/2/{power}"), ("HdrOff", f"IndexedEnabling/0/2/{power}")]
    elif inst.get("title_handle"):
        remap.append(("HdrTitle", f"WithName[{inst['title']}]/{inst['title_handle']}"))
    if inst.get("title_gate"):
        remap.append(("HdrTitleGate", inst["title_gate"]))
    remap += list(inst["remap"].items())
    gate = inst.get("gate", [])
    bounds = {"version": 2 if "gate" in inst else 1, "acceptsHWFocus": inst.get("focus", "Yes"),
              "showWhenDataModelInvalid": inst.get("show", "Hide"), "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 1280 56"}
    if "gate" in inst:
        bounds["additionalInvalidatingHandles"] = list(gate)
    return {"version": 2, "componentData": {"version": 1, "name": inst["name"], "type": form_key, "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]}, "bounds": bounds}


def place_in_tabs(family, form_key, prefix):
    """The instances with a "tab" go into that tab: at the place of the first owned instance or, the first time, of the first replaced item."""
    wanted = [i for i in family["instances"] if "tab" in i]
    if not wanted:
        return
    path = ROOT / family.get("tab_file", "skin/TUI.json")
    doc = json.loads(path.read_text(encoding="utf-8"))
    tabs = {t["tabName"]: t["componentDefinition"]["componentsData"] for t in doc["pageData"]["tabs"]}
    for inst in wanted:
        comps = tabs[inst["tab"]]
        mine = [i for i, c in enumerate(comps) if c["componentData"]["type"].startswith(prefix) and c["componentData"]["name"] == inst["name"]]
        gone = [i for i, c in enumerate(comps) if c["componentData"]["name"] in inst.get("replaces", [])]
        at = min(mine + gone) if mine + gone else len(comps)
        comps[:] = [c for i, c in enumerate(comps) if i not in mine and i not in gone]
        at -= sum(1 for i in mine + gone if i < at)
        comps.insert(at, instance_component(inst, form_key[inst["form"]]))
    path.write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding="utf-8")


def main(spec_path):
    family = json.loads(Path(spec_path).read_text(encoding="utf-8"))
    prefix = family["prefix"]
    examples = {e["id"]: e for e in header.header_examples(json.loads((ROOT / "components/native/header/catalog.json").read_text(encoding="utf-8"))["header"])}
    new_defs, images, form_key = {}, {}, {}
    for form in family["forms"]:
        built = header.header_definitions(examples[form])
        form_key[form] = built["form"]
        for key, definition in built["definitions"]:
            if key in new_defs and new_defs[key] != definition:
                raise SystemExit(f"definition {key!r} is built differently by two forms")
            new_defs[key] = definition
        for name, face in built["images"].items():
            images[name] = face
    path = ROOT / family["file"]
    doc = json.loads(path.read_text(encoding="utf-8"))
    entries = doc["componentDefinitions"]["localComponentDefinitions"]
    first = next((i for i, e in enumerate(entries) if owned(e["key"], prefix)), len(entries))
    before = sum(1 for e in entries if owned(e["key"], prefix))
    kept = [e for e in entries if not owned(e["key"], prefix)]
    kept[first:first] = [{"key": k, "value": v} for k, v in new_defs.items()]
    retire = family.get("retire", {})
    kept = [e for e in kept if e["key"] not in retire.get("definitions", [])]
    for e in kept:
        names = retire.get("components", {}).get(e["key"])
        if names:
            e["value"]["componentsData"] = [c for c in e["value"]["componentsData"] if c["componentData"]["name"] not in names]
        if e["key"] == family.get("layer"):
            comps = e["value"]["componentsData"]
            at = next((i for i, c in enumerate(comps) if c["componentData"]["type"].startswith(prefix)), len(comps))
            rest = [c for c in comps if not c["componentData"]["type"].startswith(prefix)]
            rest[at:at] = [instance_component(i, form_key[i["form"]]) for i in family["instances"] if "tab" not in i]
            e["value"]["componentsData"] = rest
    doc["componentDefinitions"]["localComponentDefinitions"] = kept
    path.write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding="utf-8")
    place_in_tabs(family, form_key, prefix)
    for name, face in images.items():
        save_png_filter0(face, SKIN / name)
    # a face is stale when no skin file names it any more (several families share the hdr_*.png namespace)
    text = "".join(p.read_text(encoding="utf-8") for p in SKIN.glob("*.json"))
    stale = sorted(p.name for p in list(SKIN.glob("hdr_*.png")) + [SKIN / n for n in retire.get("images", []) if (SKIN / n).exists()]
                   if f'"{p.name}"' not in text)
    for name in stale:
        (SKIN / name).unlink()
    print(f"definitions: {before} -> {len(new_defs)} (owned by {prefix}); instances: {len(family['instances'])}; faces written: {len(images)}; stale faces removed: {len(stale)}")


if __name__ == "__main__":
    main(sys.argv[1])
