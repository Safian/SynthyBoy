#!/usr/bin/env python3
# SynthyBoy - shows what differs between two SynthyBoy preset files, parameter by parameter.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: preset-diff.py A.sbpreset B.sbpreset [--params PATH] [--json] [--strict-meta]     exit 0: no param diff; 1: at least one differs; 2: bad input / usage
import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PARAMS = os.path.join(SCRIPT_DIR, "..", "docs", "params.json")


def load_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):   # ValueError covers a JSON error and bytes that are not UTF-8
        return None


def validate_preset(data):
    if data is None or not isinstance(data, dict):
        return False
    if data.get("format") != "synthyboy-preset":
        return False
    state = data.get("state")
    if not isinstance(state, dict):
        return False
    params = state.get("params")
    if not isinstance(params, dict):
        return False
    return True


def fmt_value(v):
    """Format a number: whole numbers without decimal point, others as Python float."""
    if isinstance(v, float) and v == int(v) and not (v != v):  # not NaN
        return str(int(v))
    return str(v)


def values_equal(a, b):
    if a is None or b is None:
        return False
    num = (int, float)
    if isinstance(a, num) and isinstance(b, num) and not isinstance(a, bool) and not isinstance(b, bool):
        return abs(a - b) <= 1e-6
    return a == b   # a value that is not a number (a damaged or foreign preset) is compared as it is, never subtracted


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print("Usage: preset-diff.py A.sbpreset B.sbpreset [--params PATH] [--json] [--strict-meta]", file=sys.stderr)
        sys.exit(2)

    a_path = args[0]
    b_path = args[1]
    params_path = DEFAULT_PARAMS
    json_output = False
    strict_meta = False

    i = 2
    while i < len(args):
        if args[i] == "--params":
            if i + 1 >= len(args):
                print("Error: --params requires a path", file=sys.stderr)
                sys.exit(2)
            params_path = args[i + 1]
            i += 2
        elif args[i] == "--json":
            json_output = True
            i += 1
        elif args[i] == "--strict-meta":
            strict_meta = True
            i += 1
        else:
            print(f"Error: unknown option {args[i]}", file=sys.stderr)
            sys.exit(2)

    # Load parameter table
    params_data = load_json(params_path)
    if params_data is None or "params" not in params_data:
        print(f"Error: cannot load params file {params_path}", file=sys.stderr)
        sys.exit(2)

    param_table = params_data["params"]
    table_keys = {p["key"] for p in param_table}

    # Load presets
    a_data = load_json(a_path)
    b_data = load_json(b_path)

    if not validate_preset(a_data):
        print(f"Error: {a_path} is not a valid synthyboy-preset", file=sys.stderr)
        sys.exit(2)
    if not validate_preset(b_data):
        print(f"Error: {b_path} is not a valid synthyboy-preset", file=sys.stderr)
        sys.exit(2)

    a_params = a_data["state"]["params"]
    b_params = b_data["state"]["params"]
    a_meta = a_data.get("meta", {})
    b_meta = b_data.get("meta", {})

    # Collect parameter differences in table order
    param_diffs = []
    for p in param_table:
        key = p["key"]
        a_val = a_params.get(key, p["default"])
        b_val = b_params.get(key, p["default"])
        if not values_equal(a_val, b_val):
            param_diffs.append({
                "key": key,
                "name": p["name"],
                "unit": p.get("unit", ""),
                "a": a_val,
                "b": b_val,
            })

    # Collect unknown parameters (present in one or both, not in table)
    all_keys = set(a_params.keys()) | set(b_params.keys())
    unknown_keys = sorted(all_keys - table_keys)
    unknown_diffs = []
    for key in unknown_keys:
        a_val = a_params.get(key)
        b_val = b_params.get(key)
        a_present = key in a_params
        b_present = key in b_params
        if a_present and b_present:
            if not values_equal(a_val, b_val):
                unknown_diffs.append({"key": key, "a": a_val, "b": b_val})
        elif a_present or b_present:
            unknown_diffs.append({"key": key, "a": a_val, "b": b_val})

    # Collect meta differences (keys sorted)
    all_meta_keys = sorted(set(a_meta.keys()) | set(b_meta.keys()))
    meta_diffs = []
    for key in all_meta_keys:
        a_val = a_meta.get(key)
        b_val = b_meta.get(key)
        if a_val != b_val:
            meta_diffs.append({"key": key, "a": a_val, "b": b_val})

    # Compute exit code
    param_count = len(param_diffs) + len(unknown_diffs)
    if param_count > 0:
        exit_code = 1
    elif strict_meta and meta_diffs:
        exit_code = 1
    else:
        exit_code = 0

    if json_output:
        # JSON output
        result = {
            "params": [],
            "unknown": [],
            "meta": [],
        }
        for d in param_diffs:
            result["params"].append({
                "key": d["key"],
                "name": d["name"],
                "unit": d["unit"],
                "a": int(d["a"]) if isinstance(d["a"], float) and d["a"] == int(d["a"]) else d["a"],
                "b": int(d["b"]) if isinstance(d["b"], float) and d["b"] == int(d["b"]) else d["b"],
            })
        for d in unknown_diffs:
            result["unknown"].append({
                "key": d["key"],
                "a": d["a"],
                "b": d["b"],
            })
        for d in meta_diffs:
            result["meta"].append({
                "key": d["key"],
                "a": d["a"],
                "b": d["b"],
            })
        print(json.dumps(result))
    else:
        # Text output
        total_diffs = param_count + len(meta_diffs)
        if total_diffs == 0:
            print("no differences")
        else:
            for d in param_diffs:
                a_str = fmt_value(d["a"])
                b_str = fmt_value(d["b"])
                unit = d["unit"]
                if unit:
                    print(f"{d['key']}  {d['name']}  {a_str} -> {b_str} {unit}")
                else:
                    print(f"{d['key']}  {d['name']}  {a_str} -> {b_str}")
            for d in unknown_diffs:
                a_str = fmt_value(d["a"]) if d["a"] is not None else "-"
                b_str = fmt_value(d["b"]) if d["b"] is not None else "-"
                print(f"unknown parameter {d['key']} (A: {a_str}, B: {b_str})")
            for d in meta_diffs:
                a_str = json.dumps(d["a"])
                b_str = json.dumps(d["b"])
                print(f"meta {d['key']}: {a_str} -> {b_str}")
            print(f"{param_count} parameters differ")

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
