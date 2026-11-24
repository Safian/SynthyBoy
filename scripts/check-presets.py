#!/usr/bin/env python3
# SynthyBoy - factory preset checker.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 -I scripts/check-presets.py [PATHS ...] [--params docs/params.json] [--json]
#
# Validates .sbpreset factory presets against parameter table (docs/params.json)
# and content pack manifest (contentpack.json).
#
# Exit codes:
#   0: No errors (warnings may be present)
#   1: One or more errors found
#   2: Usage or environment error (missing files, invalid CLI args)

import argparse
import hashlib
import json
import os
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

# Host display limit for program names (VST2 kVstMaxProgNameLen in shell-mpc)
HOST_MAX_NAME_LEN = 24


def rel_display_path(path_str: str) -> str:
    try:
        p = Path(path_str)
        if p.is_absolute():
            cwd = Path.cwd()
            if p.is_relative_to(cwd):
                return p.relative_to(cwd).as_posix()
    except (ValueError, AttributeError):
        pass
    return path_str


class Finding:
    def __init__(self, rule: str, severity: str, file: str, param: Optional[str], message: str):
        self.rule = rule
        self.severity = severity  # "error" or "warning"
        self.file = rel_display_path(file)
        self.param = param
        self.message = message

    def to_dict(self) -> Dict[str, Any]:
        d = {
            "rule": self.rule,
            "severity": self.severity,
            "file": self.file,
            "message": self.message,
        }
        if self.param is not None:
            d["param"] = self.param
        return d


class PresetChecker:
    def __init__(self, params_path: Path):
        self.params_path = params_path
        self._load_params()
        self.findings: List[Finding] = []
        self.total_presets = 0
        self.valid_presets = 0
        self.longest_name: Optional[Dict[str, Any]] = None

    def _load_params(self) -> None:
        if not self.params_path.is_file():
            raise FileNotFoundError(f"Parameter table file not found: {self.params_path}")
        with open(self.params_path, "r", encoding="utf-8") as f:
            doc = json.load(f)

        self.param_doc = doc
        self.state_version = int(doc.get("stateVersion", 4))
        self.param_table = {p["key"]: p for p in doc.get("params", [])}
        self.mod_sources = {s["choice"]: s for s in doc.get("modSources", [])}
        self.mod_destinations = {d["choice"]: d for d in doc.get("modDestinations", [])}
        self.macro_sources = {
            s["choice"]: s for s in doc.get("modSources", [])
            if s.get("key", "").startswith("macro")
        }

    def add_error(self, rule: str, file: str, param: Optional[str], message: str) -> None:
        self.findings.append(Finding(rule, "error", file, param, message))

    def add_warning(self, rule: str, file: str, param: Optional[str], message: str) -> None:
        self.findings.append(Finding(rule, "warning", file, param, message))

    def check_file(self, file_path: Path, pack_dir: Optional[Path] = None, manifest_entry: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
        self.total_presets += 1
        path_str = str(file_path)

        try:
            raw_bytes = file_path.read_bytes()
        except Exception as e:
            self.add_error("file_read", path_str, None, f"Cannot read file: {e}")
            return None

        file_sha256 = hashlib.sha256(raw_bytes).hexdigest()

        # Rule 1: not valid JSON
        try:
            data = json.loads(raw_bytes.decode("utf-8"))
        except Exception as e:
            self.add_error("json_syntax", path_str, None, f"Not valid JSON: {e}")
            return None

        if not isinstance(data, dict):
            self.add_error("format_version", path_str, None, "Root JSON value is not an object")
            return None

        # Rule 2: wrong format or version fields
        preset_format = data.get("format")
        if preset_format != "synthyboy-preset":
            self.add_error("format_version", path_str, None, f"Wrong preset format: {preset_format!r} (expected 'synthyboy-preset')")

        preset_version = data.get("version")
        if preset_version != 1:
            self.add_error("format_version", path_str, None, f"Wrong preset version: {preset_version!r} (expected 1)")

        state = data.get("state")
        if not isinstance(state, dict):
            self.add_error("format_version", path_str, None, "Missing or invalid 'state' object")
            return None

        state_format = state.get("format")
        if state_format != "synthyboy-state":
            self.add_error("format_version", path_str, None, f"Wrong state format: {state_format!r} (expected 'synthyboy-state')")

        state_version = state.get("version")
        # The factory presets stay at state version 4 by decision (3fd5d3dd): the engine (docs/params.json "stateVersion", 5) migrates them on load.
        # A preset must not be NEWER than the engine and not older than 4.
        if not isinstance(state_version, int) or state_version < 4 or state_version > self.state_version:
            self.add_error("format_version", path_str, None, f"Wrong state version: {state_version!r} (expected 4..{self.state_version})")

        # Meta checks
        meta = data.get("meta")
        name = ""
        category = ""
        if not isinstance(meta, dict):
            self.add_error("meta_missing_fields", path_str, None, "Missing or invalid 'meta' object")
        else:
            name = meta.get("name", "")
            category = meta.get("category", "")

            # Rule 14: missing name / category
            if not isinstance(name, str) or not name.strip():
                self.add_error("meta_missing_fields", path_str, None, "Missing or empty preset name in meta")
            if not isinstance(category, str) or not category.strip():
                self.add_error("meta_missing_fields", path_str, None, "Missing or empty preset category in meta")

            # Rule 15: category that does not match folder
            folder_name = file_path.parent.name
            if pack_dir and file_path.parent != pack_dir:
                if category and category != folder_name:
                    self.add_error("meta_category_mismatch", path_str, None, f"Category {category!r} does not match folder name {folder_name!r}")

            # Rule 16: name differs from what contentpack.json lists for that file
            if manifest_entry and "name" in manifest_entry:
                expected_name = manifest_entry["name"]
                if name != expected_name:
                    self.add_error("meta_name_mismatch_contentpack", path_str, None, f"Preset name {name!r} differs from contentpack.json name {expected_name!r}")

            # Rule 18: names longer than the host can show (and track longest)
            if isinstance(name, str) and name:
                name_len = len(name)
                if self.longest_name is None or name_len > self.longest_name["length"]:
                    self.longest_name = {
                        "name": name,
                        "length": name_len,
                        "file": rel_display_path(path_str),
                        "limit": HOST_MAX_NAME_LEN,
                        "exceeds_limit": name_len > HOST_MAX_NAME_LEN,
                    }
                if name_len > HOST_MAX_NAME_LEN:
                    self.add_error("preset_name_length", path_str, None, f"Preset name {name!r} ({name_len} chars) exceeds host limit of {HOST_MAX_NAME_LEN} chars")

        # Parameters check
        params = state.get("params")
        if not isinstance(params, dict):
            self.add_error("format_version", path_str, None, "Missing or invalid 'params' object in state")
            return None

        # Rule 3, 4, 5: parameter table checks
        for k, v in params.items():
            if k not in self.param_table:
                # Rule 3: parameter key not in table
                self.add_error("unknown_parameter", path_str, k, f"Parameter key {k!r} is not in the parameter table")
                continue

            pinfo = self.param_table[k]
            try:
                val = float(v)
            except (ValueError, TypeError):
                self.add_error("value_range", path_str, k, f"Parameter {k!r} value {v!r} is not a valid number")
                continue

            # Rule 4: value outside min .. max
            p_min = float(pinfo.get("min", float("-inf")))
            p_max = float(pinfo.get("max", float("inf")))
            if val < p_min - 1e-5 or val > p_max + 1e-5:
                self.add_error("value_range", path_str, k, f"Parameter {k!r} value {val} is outside min..max range [{p_min}, {p_max}]")

            # Rule 5: value not on interval grid
            interval = float(pinfo.get("interval", 0))
            flags = pinfo.get("flags", [])
            scale = pinfo.get("scale", "")
            if interval > 0:
                rem = (val - p_min) / interval
                if abs(round(rem) * interval - (val - p_min)) > 1e-5:
                    self.add_error("interval_grid", path_str, k, f"Parameter {k!r} value {val} is not on interval grid {interval}")
            elif "stepped" in flags or scale == "stepped":
                if abs(val - round(val)) > 1e-5:
                    self.add_error("interval_grid", path_str, k, f"Stepped parameter {k!r} value {val} is not an integer")

        # Modulation slot checks (Rules 6, 7, 8, 9, 10, 12, 13)
        seen_routes: Dict[Tuple[int, int, int, int], int] = {}
        macro_targets: Dict[int, List[str]] = defaultdict(list)

        for slot in range(1, 65):
            s_k = f"mod{slot:02d}.source"
            d_k = f"mod{slot:02d}.destination"
            a_k = f"mod{slot:02d}.amount"
            b_k = f"mod{slot:02d}.bypass"
            aux_k = f"mod{slot:02d}.aux_source"
            bip_k = f"mod{slot:02d}.bipolar"

            src_val = params.get(s_k, 0)
            dst_val = params.get(d_k, 0)
            amt_val = params.get(a_k, 0)
            byp_val = params.get(b_k, 0)
            bip_val = params.get(bip_k, 0)

            try:
                src = int(round(float(src_val)))
            except (ValueError, TypeError):
                src = -1
            try:
                dst = int(round(float(dst_val)))
            except (ValueError, TypeError):
                dst = -1
            try:
                amt = float(amt_val)
            except (ValueError, TypeError):
                amt = 0.0
            try:
                byp = int(round(float(byp_val)))
            except (ValueError, TypeError):
                byp = 0
            try:
                bip = int(round(float(bip_val)))
            except (ValueError, TypeError):
                bip = 0

            # Rule 6: source index outside lists
            if src < 0 or (src != 0 and src not in self.mod_sources):
                self.add_error("mod_source_invalid", path_str, s_k, f"Slot {slot:02d}: source index {src} is outside modSources list")

            aux_src = 0
            if aux_k in params:
                try:
                    aux_src = int(round(float(params[aux_k])))
                except (ValueError, TypeError):
                    aux_src = -1
                if aux_src < 0 or (aux_src != 0 and aux_src not in self.mod_sources):
                    self.add_error("mod_source_invalid", path_str, aux_k, f"Slot {slot:02d}: aux_source index {aux_src} is outside modSources list")

            # Rule 7: destination index outside lists
            if dst < 0 or (dst != 0 and dst not in self.mod_destinations):
                self.add_error("mod_destination_invalid", path_str, d_k, f"Slot {slot:02d}: destination index {dst} is outside modDestinations list")

            # Rule 8: slot with source but no destination or reverse
            if src != 0 and dst == 0:
                self.add_error("mod_slot_unbalanced", path_str, s_k, f"Slot {slot:02d}: has source {src} but no destination")
            elif src == 0 and dst != 0:
                self.add_error("mod_slot_unbalanced", path_str, d_k, f"Slot {slot:02d}: has destination {dst} but no source")

            if src != 0 and dst != 0:
                # Rule 9: slot with amount 0 that is switched on
                if byp == 0 and abs(amt) < 1e-6:
                    self.add_error("mod_slot_amount_zero", path_str, a_k, f"Slot {slot:02d}: switched on (bypass=0) with amount 0")

                # Rule 10: two slots with the same source, aux source, destination and polarity
                route_key = (src, aux_src, dst, bip)
                if route_key in seen_routes:
                    prev_slot = seen_routes[route_key]
                    self.add_error("mod_slot_duplicate", path_str, s_k, f"Slot {slot:02d} duplicates route of slot {prev_slot:02d} (source {src}, destination {dst})")
                else:
                    seen_routes[route_key] = slot

                # Macro target tracking & nonexistent parameter check (Rule 12)
                dest_entry = self.mod_destinations.get(dst)
                dest_param_key = dest_entry.get("key") if dest_entry else None
                macro_sources_in_slot = []
                if src in self.macro_sources:
                    macro_sources_in_slot.append((src, s_k, "main"))
                if aux_src in self.macro_sources and aux_src != src:
                    macro_sources_in_slot.append((aux_src, aux_k, "aux"))

                for m_choice, field_key, role in macro_sources_in_slot:
                    macro_entry = self.macro_sources[m_choice]
                    macro_key = macro_entry.get("key", "")
                    m_num_match = re.search(r"\d+", macro_key)
                    macro_num = int(m_num_match.group(0)) if m_num_match else 0
                    if dest_param_key:
                        macro_targets[macro_num].append(dest_param_key)
                        if dest_param_key not in self.param_table:
                            self.add_error("macro_target_nonexistent", path_str, d_k, f"Macro {macro_num} slot {slot:02d} target points to non-existent parameter {dest_param_key!r}")
                    else:
                        self.add_error("macro_target_nonexistent", path_str, d_k, f"Macro {macro_num} slot {slot:02d} target index {dst} has no destination key")

                # Rule 13: oscillator / filter / effect switched off with modulation routed (WARNING)
                if byp == 0 and abs(amt) >= 1e-6 and dest_param_key:
                    gate = dest_param_key.split(".")[0]
                    on_param = f"{gate}.on"
                    if on_param in self.param_table:
                        on_val_raw = params.get(on_param, self.param_table[on_param].get("default", 1))
                        try:
                            on_val = float(on_val_raw)
                        except (ValueError, TypeError):
                            on_val = 1.0
                        if on_val < 0.5:
                            self.add_warning(
                                "disabled_module_modulation",
                                path_str,
                                dest_param_key,
                                f"Slot {slot:02d}: {dest_param_key} is modulated but module switch {on_param} is off ({on_val})"
                            )

        # Macro explicit declarations & Rule 11 (macro unused with targets)
        explicit_macros = data.get("macros") or state.get("macros") or data.get("macroDefaults")
        if explicit_macros:
            macro_items = []
            if isinstance(explicit_macros, list):
                macro_items = explicit_macros
            elif isinstance(explicit_macros, dict):
                for k, v in explicit_macros.items():
                    if isinstance(v, dict):
                        macro_items.append({"macroKey": k, **v})
                    elif isinstance(v, list):
                        macro_items.append({"macroKey": k, "targets": v})

            for m_item in macro_items:
                if not isinstance(m_item, dict):
                    continue
                m_key = str(m_item.get("macroKey", m_item.get("macro", m_item.get("id", ""))))
                m_num_match = re.search(r"\d+", m_key)
                m_num = int(m_num_match.group(0)) if m_num_match else 0
                label = str(m_item.get("name", m_item.get("label", "")))
                targets = m_item.get("targets", [])
                t_keys = []
                for t in targets:
                    dest = None
                    if isinstance(t, dict):
                        dest = t.get("destination") or t.get("key") or t.get("param")
                    elif isinstance(t, str):
                        dest = t
                    if dest:
                        t_keys.append(dest)
                        if dest not in self.param_table:
                            self.add_error("macro_target_nonexistent", path_str, None, f"Macro {m_num or label} target points to non-existent parameter {dest!r}")
                if m_num:
                    macro_targets[m_num].extend(t_keys)
                if ("unused" in label.lower() or label.strip() == "-") and t_keys:
                    self.add_error("macro_unused_with_targets", path_str, None, f"Macro {m_num or label} is named {label!r} (unused) but has {len(t_keys)} target(s)")

        # Rule 11 from meta description / tags / macro_names
        if isinstance(meta, dict):
            desc = str(meta.get("description", ""))
            tags = str(meta.get("tags", ""))
            macro_names_list = meta.get("macro_names") or data.get("import", {}).get("source", {}).get("macro_names") or []
            if isinstance(macro_names_list, list):
                for idx, m_name in enumerate(macro_names_list, 1):
                    if ("unused" in str(m_name).lower() or str(m_name).strip() == "-") and macro_targets[idx]:
                        self.add_error("macro_unused_with_targets", path_str, None, f"Macro {idx} is named {m_name!r} (unused) but has {len(macro_targets[idx])} target(s)")

            for m_idx in range(1, 9):
                pattern = rf"\b(?:macro\s*{m_idx}|m{m_idx})\b[^.]*?\bunused\b"
                if (re.search(pattern, desc, re.IGNORECASE) or re.search(pattern, tags, re.IGNORECASE)) and macro_targets[m_idx]:
                    self.add_error("macro_unused_with_targets", path_str, None, f"Macro {m_idx} is documented as unused in meta description/tags but has {len(macro_targets[m_idx])} target(s)")

        return {
            "path": path_str,
            "sha256": file_sha256,
            "data": data,
            "name": name,
            "category": category,
            "params": params,
        }

    def check_pack(self, pack_dir: Path, preset_files: List[Path]) -> None:
        manifest_path = pack_dir / "contentpack.json"
        manifest_files: Dict[str, Dict[str, Any]] = {}

        if manifest_path.is_file():
            try:
                manifest_doc = json.loads(manifest_path.read_text(encoding="utf-8"))
                for entry in manifest_doc.get("files", []):
                    if isinstance(entry, dict) and "path" in entry:
                        manifest_files[entry["path"]] = entry
            except Exception as e:
                self.add_error("contentpack_syntax", str(manifest_path), None, f"Cannot parse contentpack.json: {e}")

        # Check all preset files
        checked_info: List[Dict[str, Any]] = []
        found_rel_paths: Set[str] = set()

        for f in preset_files:
            rel_path = f.relative_to(pack_dir).as_posix()
            found_rel_paths.add(rel_path)
            mf_entry = manifest_files.get(rel_path)
            info = self.check_file(f, pack_dir=pack_dir, manifest_entry=mf_entry)
            if info:
                checked_info.append(info)

        # Rule 19: entries in contentpack.json without a file and files without an entry
        if manifest_path.is_file():
            for mf_path, entry in manifest_files.items():
                target_file = pack_dir / mf_path
                if not target_file.is_file():
                    self.add_error("contentpack_file_mismatch", str(manifest_path), None, f"contentpack.json entry {mf_path!r} does not exist on disk")
                else:
                    # check sha256
                    expected_sha = entry.get("sha256")
                    if expected_sha:
                        actual_sha = hashlib.sha256(target_file.read_bytes()).hexdigest()
                        if expected_sha != actual_sha:
                            self.add_error("contentpack_file_mismatch", str(target_file), None, f"SHA-256 mismatch with contentpack.json for {mf_path!r}: expected {expected_sha}, got {actual_sha}")

            for f in preset_files:
                rel_path = f.relative_to(pack_dir).as_posix()
                if rel_path not in manifest_files:
                    self.add_error("contentpack_file_mismatch", str(f), None, f"File {rel_path!r} exists on disk but is not listed in contentpack.json")

        # Pack-wide duplicate checks
        names_map: Dict[str, List[str]] = defaultdict(list)
        hashes_map: Dict[str, List[str]] = defaultdict(list)
        params_map: Dict[Tuple[Tuple[str, Any], ...], List[str]] = defaultdict(list)

        for info in checked_info:
            p_name = info["name"]
            if p_name:
                names_map[p_name].append(info["path"])

            hashes_map[info["sha256"]].append(info["path"])

            params_tuple = tuple(sorted(info["params"].items()))
            params_map[params_tuple].append(info["path"])

        # Rule 17: duplicate names across the pack
        for p_name, paths in names_map.items():
            if len(paths) > 1:
                for p in paths:
                    self.add_error("duplicate_preset_name", p, None, f"Duplicate preset name {p_name!r} shared by {len(paths)} files: {paths}")

        # Rule 20: presets that are byte-identical or have identical parameters
        for sha, paths in hashes_map.items():
            if len(paths) > 1:
                for p in paths:
                    self.add_error("duplicate_preset_content", p, None, f"Presets are byte-identical ({sha}): {paths}")

        for p_tuple, paths in params_map.items():
            if len(paths) > 1:
                for p in paths:
                    self.add_error("duplicate_preset_content", p, None, f"Presets have identical parameter settings: {paths}")

    def run(self, input_paths: List[Path]) -> int:
        if not input_paths:
            # Default to repo content/presets
            default_presets_dir = Path(__file__).resolve().parent.parent / "content" / "presets"
            if not default_presets_dir.is_dir():
                print(f"Error: Default presets directory not found: {default_presets_dir}", file=sys.stderr)
                return 2
            input_paths = [default_presets_dir]

        # Resolve directories vs standalone files
        # Group files by contentpack directory if applicable
        pack_groups: Dict[Optional[Path], List[Path]] = defaultdict(list)

        for p in input_paths:
            if not p.exists():
                print(f"Error: Path does not exist: {p}", file=sys.stderr)
                return 2

            if p.is_dir():
                # Check if this dir or parent is a pack
                manifest_file = p / "contentpack.json"
                pack_root = p if manifest_file.is_file() else None
                found = sorted(p.rglob("*.sbpreset"))
                if not found:
                    continue
                if pack_root:
                    pack_groups[pack_root].extend(found)
                else:
                    # check if any parent has contentpack.json
                    for f in found:
                        parent_pack = None
                        for anc in f.parents:
                            if (anc / "contentpack.json").is_file():
                                parent_pack = anc
                                break
                        pack_groups[parent_pack].append(f)
            else:
                if p.suffix == ".sbpreset":
                    parent_pack = None
                    for anc in p.parents:
                        if (anc / "contentpack.json").is_file():
                            parent_pack = anc
                            break
                    pack_groups[parent_pack].append(p)
                elif p.name == "contentpack.json":
                    pack_root = p.parent
                    found = sorted(pack_root.rglob("*.sbpreset"))
                    pack_groups[pack_root].extend(found)

        for pack_root, files in pack_groups.items():
            unique_files = sorted(set(files))
            if pack_root:
                self.check_pack(pack_root, unique_files)
            else:
                for f in unique_files:
                    self.check_file(f)

        errors = [f for f in self.findings if f.severity == "error"]
        return 1 if errors else 0


def format_text_output(checker: PresetChecker) -> str:
    lines = []
    lines.append("SynthyBoy Factory Preset Check")
    lines.append("=" * 70)

    errors = [f for f in checker.findings if f.severity == "error"]
    warnings = [f for f in checker.findings if f.severity == "warning"]

    if errors:
        lines.append(f"\nErrors ({len(errors)}):")
        # Group by rule
        by_rule: Dict[str, List[Finding]] = defaultdict(list)
        for e in errors:
            by_rule[e.rule].append(e)
        for rule, items in sorted(by_rule.items()):
            lines.append(f"\n  [Rule: {rule}] ({len(items)} error(s)):")
            for item in items:
                p_str = f" ({item.param})" if item.param else ""
                lines.append(f"    - {item.file}{p_str}: {item.message}")

    if warnings:
        lines.append(f"\nWarnings ({len(warnings)}):")
        by_rule_w: Dict[str, List[Finding]] = defaultdict(list)
        for w in warnings:
            by_rule_w[w.rule].append(w)
        for rule, items in sorted(by_rule_w.items()):
            lines.append(f"\n  [Rule: {rule}] ({len(items)} warning(s)):")
            for item in items:
                p_str = f" ({item.param})" if item.param else ""
                lines.append(f"    - {item.file}{p_str}: {item.message}")

    lines.append("\n" + "=" * 70)
    lines.append("Summary:")
    lines.append(f"  Total presets checked: {checker.total_presets}")
    lines.append(f"  Total errors: {len(errors)}")
    lines.append(f"  Total warnings: {len(warnings)}")
    if checker.longest_name:
        ln = checker.longest_name
        limit_note = f" (exceeds host limit {ln['limit']})" if ln["exceeds_limit"] else f" (within host limit {ln['limit']})"
        lines.append(f"  Longest preset name: {ln['name']!r} ({ln['length']} chars){limit_note} in {ln['file']}")

    lines.append(f"\nChecked {checker.total_presets} preset(s): {len(errors)} error(s), {len(warnings)} warning(s).")
    return "\n".join(lines)


def format_json_output(checker: PresetChecker) -> str:
    errors = [f for f in checker.findings if f.severity == "error"]
    warnings = [f for f in checker.findings if f.severity == "warning"]

    out = {
        "summary": {
            "total_presets": checker.total_presets,
            "total_errors": len(errors),
            "total_warnings": len(warnings),
            "longest_name": checker.longest_name,
        },
        "errors": [e.to_dict() for e in errors],
        "warnings": [w.to_dict() for w in warnings],
    }
    return json.dumps(out, indent=2)


def main() -> int:
    parser = argparse.ArgumentParser(description="SynthyBoy factory preset checker.")
    parser.add_argument("paths", nargs="*", default=[], help="Preset files or directories to check (default: content/presets)")
    parser.add_argument("--params", default=None, help="Path to parameter table docs/params.json")
    parser.add_argument("--json", action="store_true", help="Output results as JSON")

    try:
        args = parser.parse_args()
    except SystemExit as e:
        return 2

    # Locate params.json
    if args.params:
        params_path = Path(args.params).resolve()
    else:
        root_dir = Path(__file__).resolve().parent.parent
        params_path = root_dir / "docs" / "params.json"

    if not params_path.is_file():
        print(f"Error: Parameter table not found at {params_path}", file=sys.stderr)
        return 2

    try:
        checker = PresetChecker(params_path)
    except Exception as e:
        print(f"Error initializing preset checker: {e}", file=sys.stderr)
        return 2

    input_paths = [Path(p).resolve() for p in args.paths]
    code = checker.run(input_paths)
    if code == 2:
        return 2

    if args.json:
        print(format_json_output(checker))
    else:
        print(format_text_output(checker))

    return code


if __name__ == "__main__":
    sys.exit(main())
