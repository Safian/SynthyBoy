#!/usr/bin/env python3
# SynthyBoy - sound set survey tool (analyses factory presets as numbers).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/survey-presets.py [--presets DIR] [--params FILE] [--out DIR] [--json]

import argparse
import glob
import json
import math
import os
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
DEFAULT_PARAMS = ROOT_DIR / "docs" / "params.json"
DEFAULT_PRESETS = ROOT_DIR / "content" / "presets"


def load_json(path: Path) -> Any:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:
        return None


def values_equal(a: Any, b: Any, tol: float = 1e-6) -> bool:
    if a is None or b is None:
        return False
    if isinstance(a, (int, float)) and isinstance(b, (int, float)) and not isinstance(a, bool) and not isinstance(b, bool):
        return abs(a - b) <= tol
    return a == b


def calc_stats(values: List[float]) -> Dict[str, Any]:
    if not values:
        return {"count": 0, "min": 0.0, "max": 0.0, "mean": 0.0, "median": 0.0, "std": 0.0}
    s_vals = sorted(values)
    n = len(s_vals)
    min_v = s_vals[0]
    max_v = s_vals[-1]
    mean_v = sum(s_vals) / n
    if n % 2 == 1:
        median_v = s_vals[n // 2]
    else:
        median_v = (s_vals[n // 2 - 1] + s_vals[n // 2]) / 2.0
    var = sum((x - mean_v) ** 2 for x in s_vals) / n if n > 1 else 0.0
    std_v = math.sqrt(var)
    return {
        "count": n,
        "min": round(min_v, 2),
        "max": round(max_v, 2),
        "mean": round(mean_v, 2),
        "median": round(median_v, 2),
        "std": round(std_v, 2),
    }


def compute_histogram(values: List[int], bin_size: int = 10) -> Dict[str, int]:
    if not values:
        return {}
    min_b = (min(values) // bin_size) * bin_size
    max_b = (max(values) // bin_size) * bin_size + bin_size
    bins = {f"{b}-{b + bin_size - 1}": 0 for b in range(min_b, max_b, bin_size)}
    for v in values:
        b = (v // bin_size) * bin_size
        k = f"{b}-{b + bin_size - 1}"
        bins[k] = bins.get(k, 0) + 1
    return bins


class PresetSurvey:
    def __init__(self, params_path: Path, presets_dir_or_files: Any):
        self.params_path = params_path
        self.presets_arg = presets_dir_or_files
        self.param_doc: Dict[str, Any] = {}
        self.param_table: Dict[str, Dict[str, Any]] = {}
        self.param_keys: List[str] = []
        self.defaults: Dict[str, float] = {}
        self.mod_sources: Dict[int, Dict[str, Any]] = {}
        self.mod_destinations: Dict[int, Dict[str, Any]] = {}
        self.presets: List[Dict[str, Any]] = []

        self._load_params()
        self._load_presets()

    def _load_params(self) -> None:
        doc = load_json(self.params_path)
        if doc is None or not isinstance(doc, dict):
            raise ValueError(f"Cannot load parameter table: {self.params_path}")
        self.param_doc = doc
        params_list = doc.get("params", [])
        self.param_table = {p["key"]: p for p in params_list}
        self.param_keys = [p["key"] for p in params_list]
        self.defaults = {p["key"]: float(p["default"]) for p in params_list if "default" in p}
        for s in doc.get("modSources", []):
            if "choice" in s:
                self.mod_sources[int(s["choice"])] = s
        for d in doc.get("modDestinations", []):
            if "choice" in d:
                self.mod_destinations[int(d["choice"])] = d

    def _load_presets(self) -> None:
        files: List[Path] = []
        if isinstance(self.presets_arg, (list, tuple)):
            for item in self.presets_arg:
                p = Path(item)
                if p.is_dir():
                    files.extend(sorted(p.rglob("*.sbpreset")))
                elif p.is_file():
                    files.append(p)
        else:
            p = Path(self.presets_arg)
            if p.is_dir():
                files.extend(sorted(p.rglob("*.sbpreset")))
            elif p.is_file():
                files.append(p)

        loaded = []
        for fpath in files:
            data = load_json(fpath)
            if not data or not isinstance(data, dict):
                continue
            if data.get("format") != "synthyboy-preset":
                continue
            meta = data.get("meta", {})
            state = data.get("state", {})
            params = state.get("params", {})
            if not isinstance(params, dict):
                continue

            name = meta.get("name", fpath.stem)
            category = meta.get("category", fpath.parent.name if fpath.parent != fpath else "Unknown")
            tags = meta.get("tags", "")
            description = meta.get("description", "")

            # Precompute non-default parameter differences
            non_def = {}
            for k, def_val in self.defaults.items():
                val = params.get(k, def_val)
                try:
                    val_f = float(val)
                except (ValueError, TypeError):
                    val_f = val
                if not values_equal(val_f, def_val):
                    non_def[k] = val_f

            # Unknown parameters (not in param_table)
            unknown_params = {}
            for k, val in params.items():
                if k not in self.param_table:
                    unknown_params[k] = val

            loaded.append({
                "path": str(fpath),
                "filename": fpath.name,
                "name": name,
                "category": category,
                "tags": tags,
                "description": description,
                "params": params,
                "non_default": non_def,
                "unknown": unknown_params,
                "diff_from_default_count": len(non_def) + len(unknown_params),
            })
        self.presets = loaded

    def analyze_parameter_differences(self) -> Dict[str, Any]:
        overall_counts = [p["diff_from_default_count"] for p in self.presets]
        by_cat_counts: Dict[str, List[int]] = defaultdict(list)
        for p in self.presets:
            by_cat_counts[p["category"]].append(p["diff_from_default_count"])

        res = {
            "overall": {
                **calc_stats([float(x) for x in overall_counts]),
                "histogram": compute_histogram(overall_counts, bin_size=5),
            },
            "by_category": {},
        }
        for cat in sorted(by_cat_counts.keys()):
            c_counts = by_cat_counts[cat]
            res["by_category"][cat] = {
                **calc_stats([float(x) for x in c_counts]),
                "histogram": compute_histogram(c_counts, bin_size=5),
            }
        return res

    def analyze_similarity(self, closest_n: int = 30) -> Dict[str, Any]:
        n = len(self.presets)
        if n < 2:
            return {
                "total_pairs": 0,
                "stats": calc_stats([]),
                "closest_pairs": [],
                "clusters_n15": [],
                "clusters_n30": [],
            }

        pairs = []
        diff_matrix: List[List[int]] = [[0] * n for _ in range(n)]

        for i in range(n):
            p1 = self.presets[i]
            nd1 = p1["non_default"]
            un1 = p1["unknown"]
            for j in range(i + 1, n):
                p2 = self.presets[j]
                nd2 = p2["non_default"]
                un2 = p2["unknown"]

                # Candidate keys: union of non-defaults
                candidate_keys = set(nd1.keys()) | set(nd2.keys())
                diff_count = 0
                diff_items = []
                for k in candidate_keys:
                    v1 = nd1.get(k, self.defaults.get(k, 0.0))
                    v2 = nd2.get(k, self.defaults.get(k, 0.0))
                    if not values_equal(v1, v2):
                        diff_count += 1
                        p_info = self.param_table.get(k, {})
                        diff_items.append({
                            "key": k,
                            "name": p_info.get("name", k),
                            "unit": p_info.get("unit", ""),
                            "val_a": v1,
                            "val_b": v2,
                        })

                # Check unknown keys differences
                unknown_keys = set(un1.keys()) | set(un2.keys())
                for uk in unknown_keys:
                    if uk in un1 and uk in un2:
                        if not values_equal(un1[uk], un2[uk]):
                            diff_count += 1
                            diff_items.append({"key": uk, "name": f"Unknown: {uk}", "unit": "", "val_a": un1[uk], "val_b": un2[uk]})
                    else:
                        diff_count += 1
                        diff_items.append({"key": uk, "name": f"Unknown: {uk}", "unit": "", "val_a": un1.get(uk), "val_b": un2.get(uk)})

                diff_matrix[i][j] = diff_count
                diff_matrix[j][i] = diff_count
                pairs.append({
                    "diff_count": diff_count,
                    "preset_a": p1["name"],
                    "category_a": p1["category"],
                    "path_a": p1["path"],
                    "preset_b": p2["name"],
                    "category_b": p2["category"],
                    "path_b": p2["path"],
                    "differences": diff_items,
                })

        # Sort pairs ascending by diff count
        pairs.sort(key=lambda p: (p["diff_count"], p["preset_a"], p["preset_b"]))
        all_diffs = [float(p["diff_count"]) for p in pairs]
        pair_stats = calc_stats(all_diffs)

        # Clustering algorithm: connected components at distance <= threshold
        def compute_clusters(threshold: int) -> List[Dict[str, Any]]:
            visited = [False] * n
            clusters = []
            for start in range(n):
                if not visited[start]:
                    component = []
                    q = [start]
                    visited[start] = True
                    while q:
                        curr = q.pop()
                        component.append(curr)
                        for neighbor in range(n):
                            if not visited[neighbor] and diff_matrix[curr][neighbor] <= threshold:
                                visited[neighbor] = True
                                q.append(neighbor)
                    member_presets = [self.presets[idx] for idx in component]
                    member_presets.sort(key=lambda x: (x["category"], x["name"]))
                    cats = sorted(set(p["category"] for p in member_presets))
                    clusters.append({
                        "size": len(component),
                        "categories": cats,
                        "presets": [p["name"] for p in member_presets],
                        "details": [{"name": p["name"], "category": p["category"]} for p in member_presets],
                    })
            clusters.sort(key=lambda c: (-c["size"], c["categories"]))
            for idx, c in enumerate(clusters, 1):
                c["id"] = idx
            return clusters

        return {
            "total_pairs": len(pairs),
            "stats": pair_stats,
            "closest_pairs": pairs[:closest_n],
            "clusters_n15": compute_clusters(15),
            "clusters_n30": compute_clusters(30),
        }

    def analyze_feature_use(self) -> Dict[str, Any]:
        # Definition of "kind of thing" parameter groups
        kind_of_thing_defs = {
            "oscillator_type_wavetable": {
                "title": "Oscillator Types & Wavetables",
                "keys": [
                    "osc1.type", "osc2.type", "osc3.type",
                    "osc1.wavetable", "osc2.wavetable", "osc3.wavetable",
                    "osc1.morph.type", "osc2.morph.type", "osc3.morph.type",
                    "osc1.gran.window_shape", "osc2.gran.window_shape", "osc3.gran.window_shape",
                    "osc1.sample.loop_mode", "osc2.sample.loop_mode", "osc3.sample.loop_mode",
                ]
            },
            "warp_types": {
                "title": "Oscillator Warp Types",
                "keys": [
                    "osc1.warp.type", "osc2.warp.type", "osc3.warp.type",
                    "osc1.warp2.type", "osc2.warp2.type", "osc3.warp2.type",
                ]
            },
            "sub_noise": {
                "title": "Sub Oscillator & Noise Types",
                "keys": [
                    "sub.waveform", "noise.type", "noise.sample"
                ]
            },
            "filter_models_styles": {
                "title": "Filter Models & Styles",
                "keys": [
                    "filter1.model", "filter2.model", "fxfilter.model",
                    "filter1.style", "filter2.style", "fxfilter.style",
                ]
            },
            "filter_routing": {
                "title": "Filter Routing & Destinations",
                "keys": [
                    "filter.routing",
                    "osc1.destination", "osc2.destination", "osc3.destination",
                    "sub.destination", "noise.destination",
                ]
            },
            "fx_slots": {
                "title": "FX Slot Effect Types",
                "keys": [f"fxi{i:02d}.type" for i in range(1, 17)],
            },
            "lfo_shapes_modes": {
                "title": "LFO Shapes & Sync Modes",
                "keys": [
                    f"lfo{i}.shape" for i in range(1, 11)
                ] + [
                    f"lfo{i}.sync_type" for i in range(1, 11)
                ] + [
                    f"lfo{i}.frequency_type" for i in range(1, 11)
                ] + [
                    f"rlfo{i}.sync_type" for i in range(1, 5)
                ] + [
                    f"rlfo{i}.frequency_type" for i in range(1, 5)
                ]
            },
            "envelope_curves": {
                "title": "Envelope Curves & Curve Settings",
                "keys": [
                    f"env{i}.attack_curve" for i in range(1, 7)
                ] + [
                    f"env{i}.decay_curve" for i in range(1, 7)
                ] + [
                    f"env{i}.release_curve" for i in range(1, 7)
                ] + [
                    "curve.velocity.legato", "curve.note.legato"
                ]
            },
            "unison": {
                "title": "Unison Voice Counts & Voicing Modes",
                "keys": [
                    "osc1.unison.voices", "osc2.unison.voices", "osc3.unison.voices",
                    "osc1.unison.stack", "osc2.unison.stack", "osc3.unison.stack",
                    "osc1.unison.mode", "osc2.unison.mode", "osc3.unison.mode",
                    "osc1.unison.stack_repeat", "osc2.unison.stack_repeat", "osc3.unison.stack_repeat",
                    "osc1.unison.law", "osc2.unison.law", "osc3.unison.law",
                    "hyperdim.unison",
                ]
            },
            "voice_glide": {
                "title": "Voice, Polyphony & Glide Modes",
                "keys": [
                    "master.voices", "master.voice_priority", "master.voice_override",
                    "master.voice.steal_priority", "master.voice.same_note",
                    "master.legato", "master.portamento.always", "master.portamento.scaled",
                    "sub.mono", "voice.phase_legato",
                ]
            },
            "mpe_tuning": {
                "title": "MPE & Tuning Settings",
                "keys": [
                    "master.mpe.on", "master.mpe.xyz_macro", "master.mpe.y_modwheel",
                    "master.mpe.bend_to_x", "master.mpe.y_bipolar",
                    "master.tuning.scale", "master.tuning.mts_esp", "master.tuning.mts_note_on_only",
                    "master.scale.key", "master.scale.type", "master.scale.mode",
                ]
            },
        }

        kind_of_thing_results = {}
        for group_id, group_info in kind_of_thing_defs.items():
            params_res = {}
            for k in group_info["keys"]:
                if k not in self.param_table:
                    continue
                pinfo = self.param_table[k]
                choices = pinfo.get("choices")
                p_min = int(pinfo.get("min", 0))
                p_max = int(pinfo.get("max", 0))
                default_val = pinfo.get("default", 0)

                # Collect counts per preset
                counts_by_val: Counter = Counter()
                for p in self.presets:
                    val = p["params"].get(k, default_val)
                    try:
                        val_num = int(round(float(val)))
                    except (ValueError, TypeError):
                        val_num = val
                    counts_by_val[val_num] += 1

                # Format values
                val_stats = []
                unused_values = []
                if choices:
                    for idx, c_name in enumerate(choices):
                        cnt = counts_by_val.get(idx, 0)
                        item = {"index": idx, "label": c_name, "count": cnt}
                        val_stats.append(item)
                        if cnt == 0:
                            unused_values.append(f"{c_name} (idx {idx})")
                else:
                    # Integer range e.g. unison voices or curves
                    if p_max - p_min <= 32:
                        for val_i in range(p_min, p_max + 1):
                            cnt = counts_by_val.get(val_i, 0)
                            item = {"index": val_i, "label": str(val_i), "count": cnt}
                            val_stats.append(item)
                            if cnt == 0:
                                unused_values.append(str(val_i))
                    else:
                        for val_k, cnt in sorted(counts_by_val.items()):
                            val_stats.append({"index": val_k, "label": str(val_k), "count": cnt})

                params_res[k] = {
                    "name": pinfo.get("name", k),
                    "default": default_val,
                    "choices": choices or [],
                    "value_counts": val_stats,
                    "unused_values": unused_values,
                }
            kind_of_thing_results[group_id] = {
                "title": group_info["title"],
                "params": params_res,
            }

        # Modulation source and destination tracking
        total_routes = 0
        src_counter: Counter = Counter()
        dst_counter: Counter = Counter()
        routes_per_preset: Dict[str, int] = Counter()

        # Macro tracking: macro_id -> {preset_count, destination_counter}
        macro_sources = {
            19: 1, 20: 2, 21: 3, 22: 4, 29: 5, 30: 6, 31: 7, 32: 8
        }
        macro_targets_by_macro: Dict[int, Counter] = defaultdict(Counter)
        macro_presets_by_macro: Dict[int, Set[str]] = defaultdict(set)

        for p in self.presets:
            p_params = p["params"]
            p_name = p["name"]
            preset_route_count = 0
            for slot in range(1, 65):
                s_val = p_params.get(f"mod{slot:02d}.source", 0)
                d_val = p_params.get(f"mod{slot:02d}.destination", 0)
                amt_val = p_params.get(f"mod{slot:02d}.amount", 0)
                try:
                    src = int(round(float(s_val)))
                    dst = int(round(float(d_val)))
                    amt = float(amt_val)
                except (ValueError, TypeError):
                    continue

                if src != 0 and dst != 0:
                    total_routes += 1
                    preset_route_count += 1
                    src_counter[src] += 1
                    dst_counter[dst] += 1

                    if src in macro_sources:
                        m_num = macro_sources[src]
                        macro_presets_by_macro[m_num].add(p_name)
                        dst_entry = self.mod_destinations.get(dst)
                        dst_key = dst_entry.get("key", f"dst_{dst}") if dst_entry else f"dst_{dst}"
                        macro_targets_by_macro[m_num][dst_key] += 1

            routes_per_preset[p_name] = preset_route_count

        # Unused sources and destinations
        used_sources = []
        unused_sources = []
        for s_idx, s_info in sorted(self.mod_sources.items()):
            cnt = src_counter.get(s_idx, 0)
            item = {"choice": s_idx, "key": s_info.get("key", ""), "name": s_info.get("name", ""), "count": cnt}
            if cnt > 0:
                used_sources.append(item)
            else:
                unused_sources.append(item)
        used_sources.sort(key=lambda x: x["count"], reverse=True)

        used_destinations = []
        unused_destinations = []
        for d_idx, d_info in sorted(self.mod_destinations.items()):
            cnt = dst_counter.get(d_idx, 0)
            item = {"choice": d_idx, "key": d_info.get("key", ""), "name": d_info.get("name", ""), "count": cnt}
            if cnt > 0:
                used_destinations.append(item)
            else:
                unused_destinations.append(item)
        used_destinations.sort(key=lambda x: x["count"], reverse=True)

        macros_res = {}
        for m_num in range(1, 9):
            p_cnt = len(macro_presets_by_macro[m_num])
            d_counts = macro_targets_by_macro[m_num]
            favoured = []
            for d_k, cnt in d_counts.most_common():
                p_info = self.param_table.get(d_k, {})
                favoured.append({
                    "key": d_k,
                    "name": p_info.get("name", d_k),
                    "count": cnt,
                })
            macros_res[f"macro{m_num}"] = {
                "macro_num": m_num,
                "name": f"Macro {m_num}",
                "preset_count": p_cnt,
                "total_routes": sum(d_counts.values()),
                "favoured_destinations": favoured,
            }

        # Module usage counts
        module_usage = {
            "osc_a": sum(1 for p in self.presets if p["params"].get("osc1.on", 1) > 0.5),
            "osc_b": sum(1 for p in self.presets if p["params"].get("osc2.on", 0) > 0.5),
            "osc_c": sum(1 for p in self.presets if p["params"].get("osc3.on", 0) > 0.5),
            "sub": sum(1 for p in self.presets if p["params"].get("sub.on", 0) > 0.5),
            "noise": sum(1 for p in self.presets if p["params"].get("noise.on", 0) > 0.5),
            "filter1": sum(1 for p in self.presets if p["params"].get("filter1.on", 1) > 0.5),
            "filter2": sum(1 for p in self.presets if p["params"].get("filter2.on", 0) > 0.5),
            "bus1": 0,
            "bus2": 0,
            "effects": {},
        }

        # Bus usage: send > 0 or bus level != 0 or destination != 0
        b1_used = 0
        b2_used = 0
        for p in self.presets:
            p_params = p["params"]
            b1_active = False
            b2_active = False
            for k, v in p_params.items():
                if abs(float(v)) > 1e-5:
                    if "bus1" in k:
                        b1_active = True
                    if "bus2" in k:
                        b2_active = True
            if b1_active:
                b1_used += 1
            if b2_active:
                b2_used += 1
        module_usage["bus1"] = b1_used
        module_usage["bus2"] = b2_used

        # Effect types
        fx_module_names = [
            "chorus", "compressor", "delay", "distortion", "eq", "flanger",
            "phaser", "reverb", "hyperdim", "utility", "bode", "convolve"
        ]
        # Mapping from FX slot choice to module name
        slot_fx_map = {
            1: "chorus", 2: "compressor", 3: "delay", 4: "distortion", 5: "eq",
            6: "fxfilter", 7: "flanger", 8: "phaser", 9: "reverb",
            10: "hyperdim", 11: "utility", 12: "bode", 13: "convolve"
        }
        fx_counts = {}
        for fx in fx_module_names:
            cnt = 0
            for p in self.presets:
                p_params = p["params"]
                is_on = p_params.get(f"{fx}.on", 0) > 0.5
                # Also check FX slots
                in_slot = False
                for slot in range(1, 17):
                    s_type = int(round(float(p_params.get(f"fxi{slot:02d}.type", 0))))
                    s_on = float(p_params.get(f"fxi{slot:02d}.on", 0)) > 0.5
                    if s_on and slot_fx_map.get(s_type) == fx:
                        in_slot = True
                        break
                if is_on or in_slot:
                    cnt += 1
            fx_counts[fx] = cnt
        module_usage["effects"] = fx_counts

        return {
            "kind_of_thing": kind_of_thing_results,
            "modulation_matrix": {
                "total_routes": total_routes,
                "used_sources_count": len(used_sources),
                "unused_sources_count": len(unused_sources),
                "used_sources": used_sources,
                "unused_sources": unused_sources,
                "used_destinations_count": len(used_destinations),
                "unused_destinations_count": len(unused_destinations),
                "used_destinations": used_destinations,
                "unused_destinations": unused_destinations,
            },
            "macros": macros_res,
            "module_usage": module_usage,
        }

    def analyze_performance_response(self) -> Dict[str, Any]:
        # Performance source IDs:
        # velocity: 23, release_velocity: 55
        # aftertouch: 25 (channel), 38 (poly)
        # mod_wheel: 27
        # pitch_wheel: 26
        # slide: 36, lift: 35, expression_x: 56
        # key tracking: 24 (note), 37 (oct_note)
        # random-per-note: 28 (random), 51 (note_on_alt1), 52 (note_on_alt2), 53 (note_on_rand2), 54 (note_on_rand_discrete)
        perf_source_map = {
            23: "velocity",
            55: "release_velocity",
            25: "aftertouch_channel",
            38: "aftertouch_poly",
            27: "mod_wheel",
            26: "pitch_wheel",
            36: "slide",
            35: "lift",
            56: "expression_x",
            24: "key_tracking_note",
            37: "key_tracking_oct",
            28: "random_per_note",
            51: "random_per_note_alt1",
            52: "random_per_note_alt2",
            53: "random_per_note_rand2",
            54: "random_per_note_discrete",
        }

        presets_perf = []
        counts = {
            "velocity": 0,
            "release_velocity": 0,
            "aftertouch": 0,
            "aftertouch_channel": 0,
            "aftertouch_poly": 0,
            "mod_wheel": 0,
            "pitch_wheel": 0,
            "slide_lift_expression": 0,
            "key_tracking": 0,
            "random_per_note": 0,
            "any_performance_source": 0,
        }
        vel_track_counts: Counter = Counter()

        for p in self.presets:
            p_params = p["params"]
            vel_track = float(p_params.get("master.velocity_track", 100))
            vel_track_counts[vel_track] += 1

            has_vel = False
            has_rel_vel = False
            has_at_ch = False
            has_at_poly = False
            has_mw = False
            has_pw = False
            has_sle = False
            has_kt = False
            has_rand = False
            active_perf_routes = []

            for slot in range(1, 65):
                s_val = p_params.get(f"mod{slot:02d}.source", 0)
                d_val = p_params.get(f"mod{slot:02d}.destination", 0)
                a_val = p_params.get(f"mod{slot:02d}.amount", 0)
                b_val = p_params.get(f"mod{slot:02d}.bypass", 0)
                try:
                    src = int(round(float(s_val)))
                    dst = int(round(float(d_val)))
                    amt = float(a_val)
                    byp = int(round(float(b_val)))
                except (ValueError, TypeError):
                    continue

                if src in perf_source_map and dst != 0:
                    src_name = perf_source_map[src]
                    dst_entry = self.mod_destinations.get(dst, {})
                    dst_name = dst_entry.get("name", f"Dest {dst}")
                    active_perf_routes.append({
                        "slot": slot,
                        "source_id": src,
                        "source_name": self.mod_sources.get(src, {}).get("name", src_name),
                        "destination_id": dst,
                        "destination_name": dst_name,
                        "amount": amt,
                        "bypass": byp,
                    })

                    if src == 23:
                        has_vel = True
                    elif src == 55:
                        has_rel_vel = True
                    elif src == 25:
                        has_at_ch = True
                    elif src == 38:
                        has_at_poly = True
                    elif src == 27:
                        has_mw = True
                    elif src == 26:
                        has_pw = True
                    elif src in (35, 36, 56):
                        has_sle = True
                    elif src in (24, 37):
                        has_kt = True
                    elif src in (28, 51, 52, 53, 54):
                        has_rand = True

            has_at = has_at_ch or has_at_poly
            has_any = has_vel or has_rel_vel or has_at or has_mw or has_pw or has_sle or has_kt or has_rand

            if has_vel:
                counts["velocity"] += 1
            if has_rel_vel:
                counts["release_velocity"] += 1
            if has_at:
                counts["aftertouch"] += 1
            if has_at_ch:
                counts["aftertouch_channel"] += 1
            if has_at_poly:
                counts["aftertouch_poly"] += 1
            if has_mw:
                counts["mod_wheel"] += 1
            if has_pw:
                counts["pitch_wheel"] += 1
            if has_sle:
                counts["slide_lift_expression"] += 1
            if has_kt:
                counts["key_tracking"] += 1
            if has_rand:
                counts["random_per_note"] += 1
            if has_any:
                counts["any_performance_source"] += 1

            presets_perf.append({
                "name": p["name"],
                "category": p["category"],
                "velocity_track": vel_track,
                "has_velocity": has_vel,
                "has_release_velocity": has_rel_vel,
                "has_aftertouch": has_at,
                "has_aftertouch_channel": has_at_ch,
                "has_aftertouch_poly": has_at_poly,
                "has_mod_wheel": has_mw,
                "has_pitch_wheel": has_pw,
                "has_slide_lift_expression": has_sle,
                "has_key_tracking": has_kt,
                "has_random_per_note": has_rand,
                "has_any_performance": has_any,
                "performance_routes": active_perf_routes,
            })

        return {
            "summary_counts": counts,
            "velocity_track_distribution": dict(sorted(vel_track_counts.items())),
            "per_preset": presets_perf,
        }

    def analyze_housekeeping(self) -> Dict[str, Any]:
        disabled_module_routes = []
        zero_amount_routes = []
        zero_range_macros = []

        macro_source_ids = {19, 20, 21, 22, 29, 30, 31, 32}

        for p in self.presets:
            p_params = p["params"]
            p_name = p["name"]
            p_cat = p["category"]

            for slot in range(1, 65):
                s_val = p_params.get(f"mod{slot:02d}.source", 0)
                d_val = p_params.get(f"mod{slot:02d}.destination", 0)
                a_val = p_params.get(f"mod{slot:02d}.amount", 0)
                b_val = p_params.get(f"mod{slot:02d}.bypass", 0)
                try:
                    src = int(round(float(s_val)))
                    dst = int(round(float(d_val)))
                    amt = float(a_val)
                    byp = int(round(float(b_val)))
                except (ValueError, TypeError):
                    continue

                if src != 0 and dst != 0:
                    dst_entry = self.mod_destinations.get(dst, {})
                    dst_key = dst_entry.get("key", "")
                    dst_name = dst_entry.get("name", dst_key)

                    # 1. Zero amount routes
                    if abs(amt) < 1e-6:
                        zero_amount_routes.append({
                            "preset": p_name,
                            "category": p_cat,
                            "slot": slot,
                            "source": self.mod_sources.get(src, {}).get("name", f"Src {src}"),
                            "destination": dst_name,
                            "bypass": byp,
                        })
                        if src in macro_source_ids:
                            zero_range_macros.append({
                                "preset": p_name,
                                "category": p_cat,
                                "slot": slot,
                                "macro": self.mod_sources.get(src, {}).get("name", f"Macro {src}"),
                                "destination": dst_name,
                            })

                    # 2. Disabled module routes (Rule 13 in check-presets.py)
                    if byp == 0 and abs(amt) >= 1e-6 and dst_key:
                        gate = dst_key.split(".")[0]
                        on_param = f"{gate}.on"
                        if on_param in self.param_table:
                            on_val = float(p_params.get(on_param, self.defaults.get(on_param, 1.0)))
                            if on_val < 0.5:
                                disabled_module_routes.append({
                                    "preset": p_name,
                                    "category": p_cat,
                                    "slot": slot,
                                    "destination_key": dst_key,
                                    "destination_name": dst_name,
                                    "module": gate,
                                    "module_on_param": on_param,
                                    "module_on_value": on_val,
                                    "source": self.mod_sources.get(src, {}).get("name", f"Src {src}"),
                                    "amount": amt,
                                })

        # 3. Near-identical and identical presets
        sim = self.analyze_similarity(closest_n=50)
        closest = sim.get("closest_pairs", [])
        identical_pairs = [pair for pair in closest if pair["diff_count"] == 0]
        near_identical_pairs = [pair for pair in closest if 0 < pair["diff_count"] <= 6]

        return {
            "disabled_module_routes_count": len(disabled_module_routes),
            "disabled_module_routes": disabled_module_routes,
            "zero_amount_routes_count": len(zero_amount_routes),
            "zero_amount_routes": zero_amount_routes,
            "zero_range_macros_count": len(zero_range_macros),
            "zero_range_macros": zero_range_macros,
            "identical_presets_count": len(identical_pairs),
            "identical_presets": identical_pairs,
            "near_identical_presets_count": len(near_identical_pairs),
            "near_identical_presets": near_identical_pairs,
        }

    def analyze_levels(self) -> Dict[str, Any]:
        overall_levels = []
        by_cat_levels: Dict[str, List[float]] = defaultdict(list)
        per_preset = {}

        for p in self.presets:
            p_params = p["params"]
            v = float(p_params.get("master.volume", self.defaults.get("master.volume", -12.0)))
            overall_levels.append(v)
            by_cat_levels[p["category"]].append(v)
            per_preset[p["name"]] = v

        res = {
            "overall": {
                **calc_stats(overall_levels),
                "histogram": compute_histogram([int(round(x)) for x in overall_levels], bin_size=3),
            },
            "by_category": {},
            "per_preset": per_preset,
        }
        for cat in sorted(by_cat_levels.keys()):
            c_levels = by_cat_levels[cat]
            res["by_category"][cat] = {
                **calc_stats(c_levels),
                "histogram": compute_histogram([int(round(x)) for x in c_levels], bin_size=3),
            }
        return res

    def run_full_survey(self) -> Dict[str, Any]:
        return {
            "summary": {
                "total_presets": len(self.presets),
                "categories": dict(Counter(p["category"] for p in self.presets)),
                "total_engine_parameters": len(self.param_table),
                "total_mod_sources": len(self.mod_sources),
                "total_mod_destinations": len(self.mod_destinations),
            },
            "parameter_differences": self.analyze_parameter_differences(),
            "similarity": self.analyze_similarity(closest_n=30),
            "feature_use": self.analyze_feature_use(),
            "performance_response": self.analyze_performance_response(),
            "housekeeping": self.analyze_housekeeping(),
            "levels": self.analyze_levels(),
        }


def format_markdown_report(survey: Dict[str, Any]) -> str:
    lines = []
    lines.append("# SynthyBoy Factory Sound Set Survey")
    lines.append("")
    lines.append("Comprehensive quantitative analysis of the factory sound set across presets, parameters, similarity clusters, feature utilisation, performance response, housekeeping integrity, and output levels.")
    lines.append("")

    # Summary
    sum_data = survey["summary"]
    lines.append("## 1. Executive Summary")
    lines.append("")
    lines.append(f"- **Total Presets Analyzed**: {sum_data['total_presets']}")
    lines.append(f"- **Engine Parameter Count**: {sum_data['total_engine_parameters']}")
    lines.append(f"- **Modulation Sources Available**: {sum_data['total_mod_sources']}")
    lines.append(f"- **Modulation Destinations Available**: {sum_data['total_mod_destinations']}")
    lines.append("")
    lines.append("| Category | Preset Count | Percentage |")
    lines.append("| :--- | :---: | :---: |")
    for cat, cnt in sorted(sum_data["categories"].items()):
        pct = (cnt / sum_data["total_presets"]) * 100 if sum_data["total_presets"] else 0
        lines.append(f"| {cat} | {cnt} | {pct:.1f}% |")
    lines.append("")

    # Parameter Differences from Default
    p_diffs = survey["parameter_differences"]
    lines.append("## 2. Parameter Differences from Engine Defaults")
    lines.append("")
    lines.append("Number of parameters per preset that differ from factory defaults (2637 total engine parameters).")
    lines.append("")
    lines.append("| Scope | Count | Min Diffs | Max Diffs | Mean Diffs | Median Diffs | Std Dev |")
    lines.append("| :--- | :---: | :---: | :---: | :---: | :---: | :---: |")
    ov = p_diffs["overall"]
    lines.append(f"| **Overall** | {ov['count']} | {ov['min']} | {ov['max']} | {ov['mean']:.1f} | {ov['median']:.1f} | {ov['std']:.1f} |")
    for cat, c_stat in sorted(p_diffs["by_category"].items()):
        lines.append(f"| {cat} | {c_stat['count']} | {c_stat['min']} | {c_stat['max']} | {c_stat['mean']:.1f} | {c_stat['median']:.1f} | {c_stat['std']:.1f} |")
    lines.append("")

    # Similarity & Template Families
    sim = survey["similarity"]
    lines.append("## 3. Preset Similarity & Template Families")
    lines.append("")
    s_stat = sim["stats"]
    lines.append(f"- **Total Preset Pairs**: {sim['total_pairs']}")
    lines.append(f"- **Pair Difference Range**: {s_stat['min']} .. {s_stat['max']} parameters (Mean: {s_stat['mean']:.1f}, Median: {s_stat['median']:.1f})")
    lines.append("")
    lines.append("### 30 Closest Preset Pairs")
    lines.append("")
    lines.append("| Rank | Preset A | Category A | Preset B | Category B | Differing Params | Differing Parameter Keys |")
    lines.append("| :---: | :--- | :--- | :--- | :--- | :---: | :--- |")
    for idx, pair in enumerate(sim["closest_pairs"][:30], 1):
        diff_keys_str = ", ".join(d["key"] for d in pair["differences"][:4])
        if len(pair["differences"]) > 4:
            diff_keys_str += f" (+{len(pair['differences']) - 4} more)"
        lines.append(f"| {idx} | {pair['preset_a']} | {pair['category_a']} | {pair['preset_b']} | {pair['category_b']} | **{pair['diff_count']}** | `{diff_keys_str}` |")
    lines.append("")

    lines.append("### Template Family Clusters (N <= 15 Differences)")
    lines.append("")
    lines.append("| Cluster | Size | Categories | Sample Presets |")
    lines.append("| :---: | :---: | :--- | :--- |")
    for c in sim["clusters_n15"]:
        cats_str = ", ".join(c["categories"])
        sample_str = ", ".join(c["presets"][:4])
        if len(c["presets"]) > 4:
            sample_str += f" (+{len(c['presets']) - 4} more)"
        lines.append(f"| {c['id']} | {c['size']} | {cats_str} | {sample_str} |")
    lines.append("")

    lines.append("### Template Family Clusters (N <= 30 Differences)")
    lines.append("")
    lines.append("| Cluster | Size | Categories | Sample Presets |")
    lines.append("| :---: | :---: | :--- | :--- |")
    for c in sim["clusters_n30"]:
        cats_str = ", ".join(c["categories"])
        sample_str = ", ".join(c["presets"][:5])
        if len(c["presets"]) > 5:
            sample_str += f" (+{len(c['presets']) - 5} more)"
        lines.append(f"| {c['id']} | {c['size']} | {cats_str} | {sample_str} |")
    lines.append("")

    # Feature Use
    feat = survey["feature_use"]
    lines.append("## 4. Feature Use & Engine Utilization")
    lines.append("")
    lines.append("### Module Usage Across 112 Presets")
    lines.append("")
    mod_use = feat["module_usage"]
    lines.append("| Module | Active Presets | Inactive Presets | Utilization Rate |")
    lines.append("| :--- | :---: | :---: | :---: |")
    total_p = sum_data["total_presets"]
    for mod_key, label in [
        ("osc_a", "Oscillator A (Osc 1)"),
        ("osc_b", "Oscillator B (Osc 2)"),
        ("osc_c", "Oscillator C (Osc 3)"),
        ("sub", "Sub Oscillator"),
        ("noise", "Noise Generator"),
        ("filter1", "Filter 1"),
        ("filter2", "Filter 2"),
        ("bus1", "Mixer Bus 1"),
        ("bus2", "Mixer Bus 2"),
    ]:
        cnt = mod_use.get(mod_key, 0)
        pct = (cnt / total_p) * 100 if total_p else 0
        lines.append(f"| {label} | {cnt} | {total_p - cnt} | {pct:.1f}% |")
    lines.append("")

    lines.append("### Effect Usage Across 112 Presets")
    lines.append("")
    lines.append("| Effect Module | Active Presets | Utilization Rate | Status in Sound Set |")
    lines.append("| :--- | :---: | :---: | :--- |")
    for fx_k, fx_cnt in sorted(mod_use["effects"].items(), key=lambda x: -x[1]):
        pct = (fx_cnt / total_p) * 100 if total_p else 0
        status = "Used" if fx_cnt > 0 else "**COMPLETELY UNUSED (0 presets)**"
        lines.append(f"| {fx_k.capitalize()} | {fx_cnt} | {pct:.1f}% | {status} |")
    lines.append("")

    lines.append("### Kind of Thing Parameter Breakdown")
    lines.append("")
    for group_id, group_data in feat["kind_of_thing"].items():
        lines.append(f"#### {group_data['title']}")
        lines.append("")
        lines.append("| Parameter | Name | Active Values & Counts | Explicit Unused Values |")
        lines.append("| :--- | :--- | :--- | :--- |")
        for p_k, p_info in group_data["params"].items():
            used_str_list = [f"{v['label']}: {v['count']}" for v in p_info["value_counts"] if v["count"] > 0]
            used_str = ", ".join(used_str_list) if used_str_list else "None"
            unused_str = ", ".join(p_info["unused_values"][:6])
            if len(p_info["unused_values"]) > 6:
                unused_str += f" (+{len(p_info['unused_values']) - 6} more)"
            if not unused_str:
                unused_str = "None"
            lines.append(f"| `{p_k}` | {p_info['name']} | {used_str} | {unused_str} |")
        lines.append("")

    # Modulation Matrix
    mod_matrix = feat["modulation_matrix"]
    lines.append("### Modulation Matrix Utilization")
    lines.append("")
    lines.append(f"- **Total Active Routes**: {mod_matrix['total_routes']}")
    lines.append(f"- **Sources**: {mod_matrix['used_sources_count']} used / {mod_matrix['unused_sources_count']} unused (out of {sum_data['total_mod_sources']})")
    lines.append(f"- **Destinations**: {mod_matrix['used_destinations_count']} used / {mod_matrix['unused_destinations_count']} unused (out of {sum_data['total_mod_destinations']})")
    lines.append("")
    lines.append("#### Active Modulation Sources")
    lines.append("")
    lines.append("| Source ID | Source Name | Key | Total Routes | Presets Using Source |")
    lines.append("| :---: | :--- | :--- | :---: | :---: |")
    for s in mod_matrix["used_sources"]:
        lines.append(f"| {s['choice']} | {s['name']} | `{s['key']}` | {s['count']} | - |")
    lines.append("")
    lines.append("#### Unused Modulation Sources (43 total)")
    lines.append("")
    unused_src_names = [f"{s['name']} ({s['choice']})" for s in mod_matrix["unused_sources"]]
    lines.append(", ".join(unused_src_names))
    lines.append("")

    # Macro Targets
    lines.append("### Macro Target Routing Breakdown")
    lines.append("")
    lines.append("| Macro | Presets Defining Targets | Total Routes | Favoured Destinations |")
    lines.append("| :---: | :---: | :---: | :--- |")
    for m_k, m_info in sorted(feat["macros"].items()):
        fav_list = [f"{d['name']} ({d['count']})" for d in m_info["favoured_destinations"][:4]]
        fav_str = ", ".join(fav_list) if fav_list else "None"
        lines.append(f"| {m_info['name']} | {m_info['preset_count']} | {m_info['total_routes']} | {fav_str} |")
    lines.append("")

    # Performance Response
    perf = survey["performance_response"]
    lines.append("## 5. Performance Response")
    lines.append("")
    p_sum = perf["summary_counts"]
    lines.append("| Performance Source | Presets Routing Source | Percentage of Sound Set |")
    lines.append("| :--- | :---: | :---: |")
    lines.append(f"| Velocity | {p_sum['velocity']} | {(p_sum['velocity']/total_p)*100:.1f}% |")
    lines.append(f"| Release Velocity | {p_sum['release_velocity']} | {(p_sum['release_velocity']/total_p)*100:.1f}% |")
    lines.append(f"| Aftertouch (Channel) | {p_sum['aftertouch_channel']} | {(p_sum['aftertouch_channel']/total_p)*100:.1f}% |")
    lines.append(f"| Polyphonic Aftertouch | {p_sum['aftertouch_poly']} | {(p_sum['aftertouch_poly']/total_p)*100:.1f}% |")
    lines.append(f"| Mod Wheel | {p_sum['mod_wheel']} | {(p_sum['mod_wheel']/total_p)*100:.1f}% |")
    lines.append(f"| Pitch Wheel | {p_sum['pitch_wheel']} | {(p_sum['pitch_wheel']/total_p)*100:.1f}% |")
    lines.append(f"| Slide / Lift / Expression | {p_sum['slide_lift_expression']} | {(p_sum['slide_lift_expression']/total_p)*100:.1f}% |")
    lines.append(f"| Key Tracking | {p_sum['key_tracking']} | {(p_sum['key_tracking']/total_p)*100:.1f}% |")
    lines.append(f"| Random per Note | {p_sum['random_per_note']} | {(p_sum['random_per_note']/total_p)*100:.1f}% |")
    lines.append(f"| **ANY Performance Source** | **{p_sum['any_performance_source']}** | **{(p_sum['any_performance_source']/total_p)*100:.1f}%** |")
    lines.append("")
    lines.append(f"- **Master Velocity Tracking (`master.velocity_track`)**: {perf['velocity_track_distribution']}")
    lines.append("")

    # Housekeeping
    hk = survey["housekeeping"]
    lines.append("## 6. Housekeeping & Matrix Integrity")
    lines.append("")
    lines.append(f"- **Routes to Disabled Modules**: {hk['disabled_module_routes_count']}")
    lines.append(f"- **Routes with Zero Amount**: {hk['zero_amount_routes_count']}")
    lines.append(f"- **Macros with Targets but Zero Range**: {hk['zero_range_macros_count']}")
    lines.append(f"- **Identical Presets (Different Names)**: {hk['identical_presets_count']}")
    lines.append(f"- **Near-Identical Presets (<= 6 Differences)**: {hk['near_identical_presets_count']}")
    lines.append("")
    lines.append("### Disabled Module Route Breakdown (Sample)")
    lines.append("")
    lines.append("| Category | Preset | Slot | Target Parameter | Target Module | Switch State |")
    lines.append("| :--- | :--- | :---: | :--- | :--- | :---: |")
    for r in hk["disabled_module_routes"][:15]:
        lines.append(f"| {r['category']} | {r['preset']} | {r['slot']:02d} | `{r['destination_key']}` | `{r['module']}` | `{r['module_on_param']}={r['module_on_value']}` |")
    if len(hk["disabled_module_routes"]) > 15:
        lines.append(f"| ... | *(+{len(hk['disabled_module_routes']) - 15} more routes)* | | | | |")
    lines.append("")

    # Output Levels
    lvl = survey["levels"]
    lines.append("## 7. Master Volume Level Distribution")
    lines.append("")
    lines.append("| Scope | Count | Min (dB) | Max (dB) | Mean (dB) | Median (dB) | Std Dev |")
    lines.append("| :--- | :---: | :---: | :---: | :---: | :---: | :---: |")
    l_ov = lvl["overall"]
    lines.append(f"| **Overall** | {l_ov['count']} | {l_ov['min']} | {l_ov['max']} | {l_ov['mean']:.1f} | {l_ov['median']:.1f} | {l_ov['std']:.1f} |")
    for cat, c_l in sorted(lvl["by_category"].items()):
        lines.append(f"| {cat} | {c_l['count']} | {c_l['min']} | {c_l['max']} | {c_l['mean']:.1f} | {c_l['median']:.1f} | {c_l['std']:.1f} |")
    lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="SynthyBoy factory preset sound set survey tool")
    parser.add_argument("presets", nargs="*", help="Optional preset paths or directory")
    parser.add_argument("--presets", dest="presets_dir", default=None, help="Preset directory (default: content/presets)")
    parser.add_argument("--params", default=str(DEFAULT_PARAMS), help="Path to docs/params.json")
    parser.add_argument("--out", default=None, help="Output directory to write survey.md and survey.json")
    parser.add_argument("--json", action="store_true", help="Print JSON to stdout")
    args = parser.parse_args()

    presets_target = args.presets or (args.presets_dir if args.presets_dir else str(DEFAULT_PRESETS))
    params_path = Path(args.params)
    if not params_path.is_file():
        print(f"Error: Parameter table not found at {params_path}", file=sys.stderr)
        return 2

    try:
        survey = PresetSurvey(params_path, presets_target)
        result = survey.run_full_survey()
    except Exception as e:
        print(f"Error during survey: {e}", file=sys.stderr)
        return 2

    # Write output if --out specified
    if args.out:
        out_dir = Path(args.out)
        out_dir.mkdir(parents=True, exist_ok=True)
        md_text = format_markdown_report(result)
        (out_dir / "survey.md").write_text(md_text, encoding="utf-8")
        (out_dir / "survey.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(format_markdown_report(result))

    return 0


if __name__ == "__main__":
    sys.exit(main())
