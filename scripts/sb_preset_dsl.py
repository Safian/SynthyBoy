# SynthyBoy - a small vocabulary to write a factory preset as data (used by factory-presets-batch1.py and the sound design tools).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# A preset is a dict of parameter overrides (keys of docs/params.json; a choice parameter may be given by its choice NAME, "Ladder", it is turned into the index), plus a list of
# modulation routes R(source, destination, amount, ...), plus eight macro names. The routes are written to mod01 .. mod64 in the order given. Nothing is invented: every key,
# choice, source and destination is checked against docs/params.json.

import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
with open(os.path.join(ROOT, "docs", "params.json"), "r", encoding="utf-8") as _f:
    _doc = json.load(_f)
PARAM = {p["key"]: p for p in _doc["params"]}
DEST = {d["key"]: int(d["choice"]) for d in _doc["modDestinations"]}
SRC = {s["key"]: int(s["choice"]) for s in _doc["modSources"]}


def choice_index(key, name):
    p = PARAM[key]
    ch = p.get("choices")
    if not ch:
        raise ValueError(f"{key} is not a choice parameter")
    for i, c in enumerate(ch):
        if c == name:
            return i
    raise ValueError(f"{key}: no choice {name!r} (choices: {ch})")


def resolve(key, value):
    if key not in PARAM:
        raise KeyError(f"unknown key {key}")
    if isinstance(value, str):
        return float(choice_index(key, value))
    if isinstance(value, bool):
        return 1.0 if value else 0.0
    return float(value)


def R(source, dest, amount, aux=None, aux_inv=False, power=0.0, bipolar=False, stereo=False, rise=0.0, fall=0.0, delay=0.0, aux_power=0.0, label=None):
    """One modulation route. Amount in percent of the destination's control range (see docs/engine-spec/04-modulation-matrix.md: 100 % of a cutoff is 128 semitones)."""
    if source not in SRC:
        raise KeyError(f"unknown source {source}")
    if dest not in DEST and not dest.startswith("@"):      # "@label.amount": the amount of the route that has that label (its slot number is only known when the routes are written)
        raise KeyError(f"unknown destination {dest}")
    if aux is not None and aux not in SRC:
        raise KeyError(f"unknown aux source {aux}")
    return dict(source=source, dest=dest, amount=amount, aux=aux, aux_inv=aux_inv, power=power, bipolar=bipolar, stereo=stereo, rise=rise, fall=fall, delay=delay, aux_power=aux_power, label=label)


def route_params(routes):
    out = {}
    if len(routes) > 64:
        raise ValueError("more than 64 routes")
    slot_of = {r["label"]: i for i, r in enumerate(routes, 1) if r.get("label")}
    for i, r in enumerate(routes, 1):
        p = f"mod{i:02d}."
        out[p + "source"] = float(SRC[r["source"]])
        dest = r["dest"]
        if dest.startswith("@"):
            label, _, what = dest[1:].partition(".")
            if label not in slot_of or what != "amount":
                raise KeyError(f"route label {label!r} / {what!r} is not a route amount")
            dest = f"mod{slot_of[label]:02d}.amount"
        out[p + "destination"] = float(DEST[dest])
        out[p + "amount"] = float(r["amount"])
        if r["power"]:
            out[p + "power"] = float(r["power"])
        if r["bipolar"]:
            out[p + "bipolar"] = 1.0
        if r["stereo"]:
            out[p + "stereo"] = 1.0
        if r["aux"] is not None:
            out[p + "aux_source"] = float(SRC[r["aux"]])
            if r["aux_inv"]:
                out[p + "aux_inv"] = 1.0
            if r["aux_power"]:
                out[p + "aux_power"] = float(r["aux_power"])
        for k in ("rise", "fall", "delay"):
            if r[k]:
                out[p + k] = float(r[k])
    return out


def build_params(params, routes):
    out = {k: resolve(k, v) for k, v in params.items()}
    out.update(route_params(routes))
    return out
