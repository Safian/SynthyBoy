#!/usr/bin/env python3
# SynthyBoy - prints a Markdown table of factory presets' unison voices, filters and effects.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import argparse
import json
import sys
from pathlib import Path

FX_KEYS = [
    "chorus", "compressor", "delay", "distortion", "eq", "fxfilter",
    "flanger", "phaser", "reverb", "hyperdim", "utility", "bode", "convolve",
]

HEADER = "| Preset | Category | Osc 1 | Osc 2 | Osc 3 | Max unison | Filters | FX |"
SEPARATOR = "|---|---|---|---|---|---|---|---|"


def main(argv=None):
    parser = argparse.ArgumentParser(description="Unison report for SynthyBoy presets")
    parser.add_argument("--presets", default=None, help="presets directory (default: content/presets)")
    parser.add_argument("--out", default=None, help="output file (default: stdout)")
    args = parser.parse_args(argv)

    if args.presets:
        presets_dir = Path(args.presets)
    else:
        presets_dir = Path(__file__).resolve().parent.parent / "content" / "presets"

    if not presets_dir.is_dir():
        print("preset-unison-report: %s is not a directory" % presets_dir, file=sys.stderr)
        return 1

    preset_files = sorted(presets_dir.rglob("*.sbpreset"))
    if not preset_files:
        print("preset-unison-report: no *.sbpreset found in %s" % presets_dir, file=sys.stderr)
        return 1

    rows = []
    for pf in preset_files:
        try:
            data = json.loads(pf.read_text(encoding="utf-8"))
        except (ValueError, OSError) as e:
            print("preset-unison-report: %s: %s" % (pf.name, e), file=sys.stderr)
            return 1
        state = data.get("state") if isinstance(data, dict) else None
        params = state.get("params") if isinstance(state, dict) else None
        if not isinstance(params, dict):
            print("preset-unison-report: %s: no state.params" % pf.name, file=sys.stderr)
            return 1

        meta = data.get("meta") if isinstance(data.get("meta"), dict) else {}
        name = str(meta.get("name") or pf.stem)
        category = str(meta.get("category") or "")
        name_escaped = name.replace("|", "\\|")

        osc_vals = []
        osc_ints = []
        for n in (1, 2, 3):
            if params.get("osc%d.on" % n, 0):
                v = int(round(params.get("osc%d.unison.voices" % n, 1)))
                osc_vals.append(v)
                osc_ints.append(v)
            else:
                osc_vals.append("off")

        max_unison = max(osc_ints) if osc_ints else 0
        filters = sum(1 for n in (1, 2) if params.get("filter%d.on" % n, 0))
        fx_count = sum(1 for f in FX_KEYS if params.get("%s.on" % f, 0))

        rows.append((pf.relative_to(presets_dir).as_posix(), name_escaped, category, osc_vals, max_unison, filters, fx_count))

    rows.sort(key=lambda r: r[0])

    lines = [HEADER, SEPARATOR]
    for _, name, cat, osc, mu, filt, fx in rows:
        lines.append("| %s | %s | %s | %s | %s | %d | %d | %d |" % (name, cat, osc[0], osc[1], osc[2], mu, filt, fx))

    counts = {3: 0, 4: 0, 5: 0}
    for _, _, _, _, mu, _, _ in rows:
        for n in counts:
            if mu > n:
                counts[n] += 1

    lines.append("")
    lines.append("Presets: %d" % len(rows))
    for n in (3, 4, 5):
        lines.append("Unison above %d on an active oscillator: %d presets" % (n, counts[n]))

    text = "\n".join(lines) + "\n"

    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)

    return 0


if __name__ == "__main__":
    sys.exit(main())
