#!/usr/bin/env python3
# SynthyBoy - writes <presets-dir>/index.json: one small file that lists every factory preset (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/build-preset-index.py [--presets-dir content/presets] [--categories scripts/factory-preset-categories.json] [--out FILE]
#        python3 scripts/build-preset-index.py --check [--presets-dir DIR]     (verifies an existing index against the directory; exit 1 when stale)
#
# The MPC plugin reads ONLY this file at start (docs/MPC.md "Start-up time", "The preset index"): program count and names come from it; a preset file is opened when it is selected.
# Format (one preset per line so a reader needs no general JSON parser):
#   {"index_version":1,"count":N,"listing_hash":"<16 hex>","presets":[
#   {"path":"Bass/808_boom.sbpreset","name":"808 Boom","category":"Bass","group":"Bass Sub+808","size":50333,"hash":"<16 hex>","format_version":1,"state_version":4},
#   ...]}
# listing_hash: FNV-1a 64 over, for every preset sorted by path (bytes), "<path>\0<size>\n". The plugin recomputes it from a directory listing (names and sizes, no file read) and trusts the index only when it matches.
# hash: FNV-1a 64 of the file's bytes. An INTEGRITY check against accidental change, not a security measure; nothing in the plugin depends on it at start.
import argparse
import json
import os
import sys

FNV_OFFSET = 0xcbf29ce484222325
FNV_PRIME = 0x100000001b3
MASK = 0xffffffffffffffff


def fnv1a64(data: bytes, h: int = FNV_OFFSET) -> int:
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK
    return h


def listing_hash(entries):
    h = FNV_OFFSET
    for path, size in sorted(entries, key=lambda e: e[0].encode('utf-8')):
        h = fnv1a64(path.encode('utf-8') + b'\0' + str(size).encode('ascii') + b'\n', h)
    return '%016x' % h


def list_presets(presets_dir):
    out = []
    for root, _, files in os.walk(presets_dir):
        for f in files:
            if f.endswith('.sbpreset'):
                full = os.path.join(root, f)
                out.append((os.path.relpath(full, presets_dir).replace(os.sep, '/'), os.path.getsize(full)))
    return out


def build(presets_dir, categories_path):
    groups = {}
    if categories_path and os.path.exists(categories_path):
        with open(categories_path, encoding='utf-8') as f:
            groups = {k: v.get('group', '') for k, v in json.load(f).get('presets', {}).items()}
    lines = []
    listing = []
    for rel, size in sorted(list_presets(presets_dir), key=lambda e: e[0].encode('utf-8')):
        with open(os.path.join(presets_dir, rel), 'rb') as f:
            raw = f.read()
        doc = json.loads(raw.decode('utf-8'))
        meta = doc.get('meta', {})
        name = meta.get('name', '')
        entry = {
            'path': rel, 'name': name, 'category': meta.get('category', ''), 'group': groups.get(name, ''),
            'size': size, 'hash': '%016x' % fnv1a64(raw),
            'format_version': int(doc.get('version', 0)), 'state_version': int(doc.get('state', {}).get('version', 0)),
        }
        lines.append(json.dumps(entry, ensure_ascii=False, separators=(',', ':')))
        listing.append((rel, size))
    head = '{"index_version":1,"count":%d,"listing_hash":"%s","presets":[\n' % (len(lines), listing_hash(listing))
    return head + ',\n'.join(lines) + '\n]}\n'


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument('--presets-dir', default=os.path.join(here, '..', 'content', 'presets'))
    ap.add_argument('--categories', default=os.path.join(here, 'factory-preset-categories.json'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    out = a.out or os.path.join(a.presets_dir, 'index.json')
    if a.check:
        want = build(a.presets_dir, a.categories)
        have = open(out, encoding='utf-8').read() if os.path.exists(out) else ''
        if want != have:
            print('index.json is stale or missing: %s' % out)
            return 1
        print('index.json is current')
        return 0
    text = build(a.presets_dir, a.categories)
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('wrote %s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
