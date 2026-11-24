#!/usr/bin/env python3
"""Prepare a local-only, hash-pinned compact Play factory FilmStrip probe.

Does not deploy or copy factory assets. Absolute host asset resolution and the
40px rendering require an actual host capture; passive/OFF art stays unchanged.
"""
# Copyright (C) 2026 Safian. SPDX-License-Identifier: AGPL-3.0-or-later
import argparse
import copy
import hashlib
import json
import re
from pathlib import Path

ASSET = '/usr/share/Akai/Content/Synths/AKAI Components/knob_blue.png'
TARGETS = {
    'oscNativeCompactPan': 'play_sub_pan_40.png',
    'oscNativeCompactLevel': 'play_sub_level_40.png',
}


def prepare(raw, expected_sha256, asset=ASSET):
    if asset != ASSET:
        raise ValueError('Only the exact host-owned factory asset is allowed')
    digest = hashlib.sha256(raw).hexdigest()
    if digest != expected_sha256:
        raise ValueError('Base TUI SHA256 mismatch')
    original = json.loads(raw)
    result = copy.deepcopy(original)
    definitions = result['pageData']['componentDefinitions']['localComponentDefinitions']
    changes = []
    for key, previous in TARGETS.items():
        matches = [(i, d) for i, d in enumerate(definitions) if d.get('key') == key]
        if len(matches) != 1:
            raise ValueError(f'Expected exactly one {key} definition')
        index, definition = matches[0]
        children = definition['value']['componentsData']
        knobs = [(i, c) for i, c in enumerate(children)
                 if c.get('componentData', {}).get('type') == 'Knob']
        if len(knobs) != 1:
            raise ValueError(f'Unsafe Knob selection: {key}')
        child_index, child = knobs[0]
        component = child['componentData']
        data = component['data']
        if (component.get('name') != 'knob' or data.get('knobType') != 'FilmStrip'
                or data.get('filmStrip') != previous or data.get('numFrames') != 127
                or data.get('handleName') != 'Data' or data.get('invert') is not False
                or child['bounds'].get('bounds') != '2 4 40 40'):
            raise ValueError(f'Unexpected active Knob contract: {key}')
        pointer = (f'/pageData/componentDefinitions/localComponentDefinitions/{index}'
                   f'/value/componentsData/{child_index}/componentData/data/filmStrip')
        changes.append({'definition': key, 'path': pointer, 'before': previous, 'after': asset})
        data['filmStrip'] = asset
    # Undo exactly the allowed field changes to prove everything else survived.
    restored = copy.deepcopy(result)
    for change in changes:
        node = restored
        parts = change['path'].split('/')[1:]
        for part in parts[:-1]:
            node = node[int(part)] if isinstance(node, list) else node[part]
        node[parts[-1]] = change['before']
    if restored != original:
        raise ValueError('Unexpected structural change outside selected filmStrip fields')
    manifest = {'base_sha256': digest, 'changes': changes,
                'asset_sha256_on_xl_20261124': '387ca9ecbc54cb47ff4d2bcc2035e3250709f8bc146b971c3dc0cf0c3965b117',
                'status': 'Prepared only; external-plugin absolute asset resolution unverified',
                'passive_art': 'Unchanged; no completed OFF variant claimed'}
    return result, manifest


def patch_bytes(raw, expected_sha256):
    """Replace only the two selected value tokens, preserving every other byte."""
    result, manifest = prepare(raw, expected_sha256)
    encoded = raw
    for change in manifest['changes']:
        # Restrict the raw token search to the exact keyed definition object.
        anchor = rb'\{\s*"key"\s*:\s*' + re.escape(json.dumps(change['definition']).encode())
        anchors = list(re.finditer(anchor, encoded))
        if len(anchors) != 1:
            raise ValueError('Definition source token is not unique')
        start = anchors[0].start()
        tail = encoded[start:].decode('utf-8')
        definition, end = json.JSONDecoder().raw_decode(tail)
        block = tail[:end].encode('utf-8')
        if definition.get('key') != change['definition']:
            raise ValueError('Definition boundary mismatch')
        pattern = rb'("filmStrip"\s*:\s*)' + re.escape(json.dumps(change['before']).encode())
        if len(list(re.finditer(pattern, block))) != 1:
            raise ValueError('FilmStrip source token is not unique in selected definition')
        replacement = re.sub(pattern, lambda match: match.group(1) +
                             json.dumps(change['after']).encode(), block, count=1)
        encoded = encoded[:start] + replacement + encoded[start + len(block):]
    if json.loads(encoded) != result:
        raise ValueError('Raw patch differs from the verified structural selection')
    return encoded, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--base-sha256', required=True)
    args = parser.parse_args()
    paths = [p.resolve() for p in (args.input, args.output, args.manifest)]
    if len(set(paths)) != 3 or args.output.exists() or args.manifest.exists():
        parser.error('Input/output/manifest must be distinct; output files must not exist')
    encoded, manifest = patch_bytes(args.input.read_bytes(), args.base_sha256)
    manifest['output_sha256'] = hashlib.sha256(encoded).hexdigest()
    with args.output.open('xb') as output:
        output.write(encoded)
    with args.manifest.open('x') as output:
        json.dump(manifest, output, indent=2)
        output.write('\n')


if __name__ == '__main__':
    main()
