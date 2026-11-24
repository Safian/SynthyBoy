"""Check recorded MPC XL native controls, without contacting a device.

Run: python3 scripts/tests/check_xl_native_capture.py [--extended] CAPTURE [PAGE ...]

The default checks the visible Mixer/Voice/Master gesture, the Filter marker
pair before and after touch, and FX Chorus input plus native pixel exposure.
Extended mode also checks the first gesture at the declared Mixer/Voice/Master
control and the Filter power toggle. FX has one measured gesture in this format.
"""
import argparse
import json
import math
from pathlib import Path
import sys

TARGETS = {"mixer": "main.fx_bypass", "voice": "voicectl.rand.pan", "master": "master.volume"}
PAGES = (*TARGETS, "filter", "fx")


def read_record(directory, filename, key):
    records = json.loads((directory / filename).read_text())
    if not isinstance(records, dict) or not isinstance(records.get(key), dict):
        raise ValueError(f"{filename}: missing or malformed {key} record")
    return records[key]


def require_change(delta, target):
    if not isinstance(delta, dict) or set(delta) != {target}:
        raise ValueError(f"expected only {target} to change, observed {delta!r}")
    pair = delta[target]
    if not isinstance(pair, list) or len(pair) != 2 or any(
        type(v) not in (str, int, float) for v in pair
    ):
        raise ValueError(f"{target}: expected two numeric parameter values, got {pair!r}")
    before, after = map(float, pair)
    if not math.isfinite(before) or not math.isfinite(after) or before == after:
        raise ValueError(f"{target}: invalid or unchanged values {pair!r}")


def require_gesture(directory, filename, page, target):
    record = read_record(directory, filename, page)
    if record.get("target") != target:
        raise ValueError(f"{filename}: expected gesture for {target}")
    require_change(record["changed"], target)
    return record


def check_filter(directory, extended):
    data = json.loads((directory / "special-results.json").read_text())
    expected = ["filter1", [16, 58, 44], [58, 16, 100]]
    for stage in ("filter_before", "filter_after"):
        if data[stage] != expected:
            raise ValueError(f"{stage}: expected {expected}, got {data[stage]}")
    if extended:
        require_change(data["filter_changed"], "filter1.on")


def check_fx(directory):
    geometry = require_gesture(directory, "input-results.json", "fx", "chorus.on")["geometry"]
    rect = geometry["rect"]
    if not isinstance(rect, list) or len(rect) != 4 or any(type(n) is not int for n in rect):
        raise ValueError(f"invalid FX Chorus rectangle {rect!r}")
    width, height = rect[2:]
    if width <= 0 or height <= 0:
        raise ValueError(f"nonpositive FX Chorus dimensions: {width} x {height}")
    for field, expected in (("transparent", width * height), ("opaque", 0), ("composed_differs_primary", 0)):
        value = geometry[field]
        if type(value) is not int or value != expected:
            raise ValueError(f"FX Chorus {field}: expected {expected}, got {value!r}")


def check_page(directory, page, extended):
    if page == "filter":
        check_filter(directory, extended)
    elif page == "fx":
        check_fx(directory)
    elif page in TARGETS:
        record = require_gesture(directory, "targeted-results.json", page, TARGETS[page])
        if record.get("view_after") != page:
            raise ValueError(f"expected {page} after gesture, got {record.get('view_after')!r}")
        if extended:
            require_gesture(directory, "input-results.json", page, TARGETS[page])
    else:
        raise ValueError(f"unknown page; choose from {', '.join(PAGES)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extended", action="store_true")
    parser.add_argument("capture", type=Path)
    parser.add_argument("pages", nargs="*", metavar="PAGE")
    args = parser.parse_args()
    failed = False
    for page in args.pages or PAGES:
        try:
            check_page(args.capture, page, args.extended)
        except (KeyError, ValueError, OSError, TypeError, OverflowError) as exc:
            print(f"FAIL {page}: {exc}")
            failed = True
        else:
            print(f"PASS {page}")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
