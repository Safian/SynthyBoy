#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Check that shell-mpc/ui/theme_tokens.h (C++) and components/colors.json + sizes.json (skin recipes) hold the same tokens.

A check only, no generator: the values are kept by hand in both places, and this script fails (exit 1) when they differ.
Compared: the three looks (kLook1 = neon, kLook2 = aurora, kLook3 = plasma): every colour against `theme.<look>.<field>[.<index>]`
in colors.json and every number against the `theme.<look>` entry of sizes.json; kNativeCanvas against `surface.canvas`;
kPageTitleSize against `font.page_title`.
Usage: python3 scripts/check-tokens.py
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOOKS = {"kLook1": "neon", "kLook2": "aurora", "kLook3": "plasma"}


def parse_header(text):
    """{look constant: {field: value or [values]}} plus the two stand-alone constants."""
    out = {}
    for const in LOOKS:
        m = re.search(r"inline const Look %s = \{(.*?)\n\};" % const, text, re.S)
        if not m:
            sys.exit("check-tokens: %s not found in theme_tokens.h" % const)
        fields = {}
        for line in m.group(1).splitlines():
            line = line.strip()
            m2 = re.match(r"(.+?),\s*//\s*(\w+)\s*$", line)
            if not m2:
                continue
            value, name = m2.group(1).strip(), m2.group(2)
            if value.startswith("{"):
                fields[name] = [parse_number(v) for v in value.strip("{} ").split(",")]
            else:
                fields[name] = parse_number(value)
        out[const] = fields
    canvas = re.search(r"kNativeCanvas\s*=\s*(0x[0-9a-fA-F]+)", text)
    title = re.search(r"kPageTitleSize\s*=\s*([0-9.]+)f", text)
    if not canvas or not title:
        sys.exit("check-tokens: kNativeCanvas / kPageTitleSize not found in theme_tokens.h")
    return out, int(canvas.group(1), 16), float(title.group(1))


def parse_number(v):
    v = v.strip()
    if v.startswith("0x"):
        return int(v, 16)
    return float(v.rstrip("f"))


def hex_of(s):
    return int(s.lstrip("#"), 16)


def main():
    header = (ROOT / "shell-mpc/ui/theme_tokens.h").read_text()
    colors = json.loads((ROOT / "components/colors.json").read_text())["colors"]
    sizes = json.loads((ROOT / "components/sizes.json").read_text())["sizes"]
    looks, canvas, title = parse_header(header)
    errors = []

    def cmp(what, cpp, js):
        if js is None:
            errors.append("%s: missing in the JSON (theme_tokens.h has %s)" % (what, cpp))
        elif abs(cpp - js) > 1e-9:
            errors.append("%s: theme_tokens.h %s, JSON %s" % (what, cpp, js))

    for const, look in LOOKS.items():
        entry = sizes.get("theme." + look, {})
        for field, value in looks[const].items():
            if isinstance(value, list):
                for i, v in enumerate(value):
                    js = colors.get("theme.%s.%s.%d" % (look, field, i))
                    cmp("%s %s[%d]" % (look, field, i), v, hex_of(js) if js else None)
            elif field in entry:
                cmp("%s %s" % (look, field), value, entry[field])
            else:
                js = colors.get("theme.%s.%s" % (look, field))
                cmp("%s %s" % (look, field), value, hex_of(js) if js else None)
        used = {"theme.%s.%s" % (look, f) for f in looks[const]}
        for key in colors:
            m = re.match(r"theme\.%s\.(\w+?)(\.\d+)?$" % look, key)
            if m and m.group(1) not in looks[const]:
                errors.append("%s: colors.json has %s, theme_tokens.h has no field %s" % (look, key, m.group(1)))
    js = colors.get("surface.canvas")
    cmp("kNativeCanvas / surface.canvas", canvas, hex_of(js) if js else None)
    cmp("kPageTitleSize / font.page_title", title, sizes.get("font.page_title"))
    if errors:
        print("check-tokens: theme_tokens.h and components/{colors,sizes}.json differ:")
        for e in errors:
            print("  " + e)
        return 1
    print("check-tokens: ok (3 looks, kNativeCanvas, kPageTitleSize)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
