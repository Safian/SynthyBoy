#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Hand-run tool: write the Header definitions, instances and faces of one screen family into the skin from its spec list.

Nothing here runs at build time. The input is a skin spec file (components/native/header/skin-<family>.json):

  {"file": "skin/defs_osc.json",            the definition file of the family
   "layer": "oscNativeSettingsLayer",        the definition that holds the header instances
   "prefix": "oscHeader",                    every definition / instance type with this prefix is owned (replaced) by the tool
   "forms": ["osc-abc", ...],                catalogue example ids (components/native/header/catalog.json) that have a "skin" block
   "instances": [{"name", "form", "title", "power": "Parameter N", "data", "gate": [...], "remap": {...}}]}

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
    power = inst["power"]
    remap = [("Data", inst["data"]), ("HdrTitle", f"WithName[{inst['title']}]/{power}"), ("HdrPower", power),
             ("HdrOn", f"IndexedEnabling/1/2/{power}"), ("HdrOff", f"IndexedEnabling/0/2/{power}")] + list(inst["remap"].items())
    bounds = {"version": 2, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute",
              "bounds": "0 0 1280 56", "additionalInvalidatingHandles": list(inst["gate"])}
    return {"version": 2, "componentData": {"version": 1, "name": inst["name"], "type": form_key, "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]}, "bounds": bounds}


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
    for e in kept:
        if e["key"] == family["layer"]:
            comps = e["value"]["componentsData"]
            at = next((i for i, c in enumerate(comps) if c["componentData"]["type"].startswith(prefix)), len(comps))
            rest = [c for c in comps if not c["componentData"]["type"].startswith(prefix)]
            rest[at:at] = [instance_component(i, form_key[i["form"]]) for i in family["instances"]]
            e["value"]["componentsData"] = rest
    doc["componentDefinitions"]["localComponentDefinitions"] = kept
    path.write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding="utf-8")
    for name, face in images.items():
        save_png_filter0(face, SKIN / name)
    used = {name for name in images}
    stale = sorted(p.name for p in SKIN.glob("hdr_*.png") if p.name not in used)
    for name in stale:
        (SKIN / name).unlink()
    print(f"definitions: {before} -> {len(new_defs)}; instances: {len(family['instances'])}; faces written: {len(images)}; stale faces removed: {len(stale)}")


if __name__ == "__main__":
    main(sys.argv[1])
