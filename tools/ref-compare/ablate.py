#!/usr/bin/env python3
"""SynthyBoy - ref-compare: ablation sets. Writes a copy of every selected Serum preset with a part of it removed (the effect racks, the modulation matrix), so that the
reference render of the real Serum 2 and ours can be compared for the voice alone: the score of the 'nofx' set against the full set says how much of the difference is in the effects.

  ablate.py --presets DIR --out DIR --variant nofx|nomod|nofx+nomod [--filter RX]

The copies are .SerumPreset files (derived from the licensed presets: gitignored under tests/serum-reference/). Run tools/serum-reference/render_all.py and
ref_compare.py with --presets <out>/Presets on them, like for the originals.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import experiment as ex  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--presets', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--variant', required=True)
    ap.add_argument('--filter', default='')
    a = ap.parse_args()
    rx = re.compile(a.filter) if a.filter else None
    parts = set(a.variant.split('+'))
    n = 0
    for root, _, files in os.walk(a.presets):
        for f in sorted(files):
            if not f.endswith('.SerumPreset'):
                continue
            full = os.path.join(root, f)
            rel = os.path.relpath(full, a.presets)
            if rx and not rx.search(rel):
                continue
            dst = os.path.join(a.out, 'Presets', rel)
            if os.path.exists(dst):
                continue
            hdr, val = ex._audit.load_serum(full)
            if 'nofx' in parts:
                for r in range(3):
                    rk = val.get('FXRack%d' % r)
                    if isinstance(rk, dict):
                        rk['FX'] = []
                for i in range(64):   # a modulation slot that points at an effect module: the plug-in aborts (out_of_range) when the module is gone
                    m = val.get('ModSlot%d' % i)
                    if isinstance(m, dict) and str(m.get('destModuleTypeString', '')).startswith('FX'):
                        m['source'] = [0, 0]
                        m['destModuleTypeString'], m['destModuleID'], m['destModuleParamName'], m['destModuleParamID'] = '', 0, '', 0
            if 'nomod' in parts:
                for i in range(64):
                    m = val.get('ModSlot%d' % i)
                    if isinstance(m, dict):
                        m['source'] = [0, 0]
            h = {k: hdr[k] for k in hdr if k != 'hash'}
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(ex.make_container(h, val))
            n += 1
    print('wrote %d presets' % n)


if __name__ == '__main__':
    main()
