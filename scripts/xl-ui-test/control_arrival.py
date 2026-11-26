"""Measure individual Key 61 native control regions on recorded composite frames.

The producer captures full-size raw BGRX (800x1280), before touch, a numbered
frame series and a separately sampled settled reference. Device uptime in the
'times' member includes the pre-tap origin followed by each numbered sample.
Pass settled VST values from the same state to resolve SDK visibility gates.
"""
import argparse
from collections import Counter, defaultdict
from functools import lru_cache
import json
from pathlib import Path
import re
import tarfile

import numpy as np
from PIL import Image

from disabled import gate_state
from inventory import inventory
from scalars import parameter


def read_capture(path):
    frames = {}
    times = None
    settled = None
    ref_time = None
    with tarfile.open(path, 'r|*') as archive:
        for member in archive:
            name = member.name.removeprefix('./')
            if name == 'times':
                times = [float(x) for x in archive.extractfile(member).read().split()]
            if name == 'ref_time':
                ref_time = float(archive.extractfile(member).read())
            if name in ('before', 'ref') or re.fullmatch(r'f[1-9][0-9]*', name):
                if name in frames or member.size != 800 * 1280 * 4:
                    raise ValueError(f'{path}: invalid frame {name}')
                image = Image.frombytes('RGB', (800, 1280), archive.extractfile(member).read(), 'raw', 'BGRX')
                # The half-resolution grid retains small text while bounding cold-boot memory.
                image = image.transpose(Image.Transpose.ROTATE_270)
                if name == 'ref':
                    settled = image
                frames[name] = np.asarray(image.resize((640, 400), Image.Resampling.BOX))
    if times is None or 'ref' not in frames or 'before' not in frames:
        raise ValueError(f'{path}: missing times, before or independent settled reference')
    before, reference = frames.pop('before'), frames.pop('ref')
    if len(times) != len(frames) + 1 or len(frames) < 3 or any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError(f'{path}: invalid time series')
    ordered = [frames[f'f{i}'] for i in range(1, len(frames) + 1)]
    if ref_time is not None and ref_time <= times[-1]:
        raise ValueError(f'{path}: reference predates the end of the sample train')
    reference_seconds = None if ref_time is None else round(ref_time - times[0], 3)
    return before, reference, ordered, [round(t - times[0], 3) for t in times[1:]], settled, reference_seconds


def stable_arrival(times, fractions, required=.90):
    """Require three real samples and agreement through the rest of the recording."""
    start = len(times)
    while start > 0 and fractions[start - 1] >= required:
        start -= 1
    return times[start] if len(times) - start >= 3 else None


def delta(a, b):
    return np.max(np.abs(a.astype(np.int16) - b), axis=2)


def box_slices(rect, shape):
    x, y, w, h = rect
    x1, x2 = max(0, x // 2), min(shape[1], (x + w + 1) // 2)
    # The device measurement helper only composites scanlines below the MPC header.
    y1, y2 = max(55, y // 2), min(369, shape[0], (y + h + 1) // 2)
    return (slice(y1, max(y1, y2)), slice(x1, max(x1, x2)))


def measure(before, reference, frames, times, controls, values, graphs=()):
    brightness = reference.max(axis=2)
    # Include dim disabled text and outlines. A change in flat background alone
    # does not prove a control arrived.
    contrast = np.zeros_like(brightness)
    for axis in (0, 1):
        change = np.abs(np.diff(brightness.astype(np.int16), axis=axis)).astype(np.uint8)
        if axis == 0:
            contrast[1:] = np.maximum(contrast[1:], change)
            contrast[:-1] = np.maximum(contrast[:-1], change)
        else:
            contrast[:, 1:] = np.maximum(contrast[:, 1:], change)
            contrast[:, :-1] = np.maximum(contrast[:, :-1], change)
    foreground = (brightness >= 68) | ((brightness >= 22) & (contrast >= 12))
    changed = delta(before, reference) > 18
    signature = foreground & changed
    changed_edges = changed & (contrast >= 12)
    matched = np.stack([delta(frame, reference) <= 9 for frame in frames])
    rows = []
    rects = defaultdict(list)
    for item in controls:
        state = gate_state(item['gates'], values)
        if state is False:
            continue
        row = {k: item[k] for k in ('id', 'type', 'rect')}
        row['result'] = 'unknown_gate' if state is None else 'input_or_container'
        if state is True and item.get('unknown_art'):
            row['result'] = 'unknown_art'
        if state is True and item['visual_leaf']:
            rects[tuple(item['rect'])].append(row)
        rows.append(row)
    for label, rect in graphs:
        row = {'id': label, 'type': 'plot', 'rect': rect}
        rects[tuple(rect)].append(row)
        rows.append(row)
    for rect, group in rects.items():
        ys, xs = box_slices(rect, reference.shape)
        mask = signature[ys, xs]
        if any(row['type'] == 'plot' for row in group):
            mask = mask & (contrast[ys, xs] >= 12)
        count = int(mask.sum())
        if count < 6:
            result = 'no_distinct_pixels'
            arrival = None
            # Equivalent before/after art can still disappear during a tab switch.
            # Report continuity without assigning an arrival time to identical art.
            unchanged = foreground[ys, xs] & (contrast[ys, xs] >= 12)
            if unchanged.sum() >= 6 and (delta(before[ys, xs], reference[ys, xs])[unchanged] <= 9).mean() >= .90:
                ratios = matched[:, ys, xs][:, unchanged].mean(axis=1)
                if np.all(ratios >= .90):
                    result = 'continuous_match'
                else:
                    arrival = stable_arrival(times, ratios)
                    result = 'reappeared' if arrival is not None else 'unsettled'
        else:
            local = matched[:, ys, xs]
            ratios = local[:, mask].mean(axis=1)
            # A selector's large face may arrive before the text or knob pointer.
            # Its bright fill must not outweigh a still-missing changed outline.
            edges = changed_edges[ys, xs]
            if edges.sum() >= 6:
                ratios = np.minimum(ratios, local[:, edges].mean(axis=1))
            arrival = stable_arrival(times, ratios)
            result = 'matched' if arrival is not None else 'unsettled'
        for row in group:
            row.update(result=result, seconds=arrival, distinct_pixels=count)
            if len(group) > 1:
                row['shared_box'] = len(group)
    return rows


@lru_cache(maxsize=16)
def changing_art_pixels(paths, count, w, h):
    """Keep only opaque pixels whose colour differs between frames of this strip."""
    if len(paths) == 2:
        samples = []
        for path in paths:
            with Image.open(path) as image:
                samples.append(np.asarray(image.convert('RGBA').resize((w, h), Image.Resampling.LANCZOS)))
        frames = np.stack(samples)
    else:
        with Image.open(paths[0]) as sheet:
            if sheet.height % count:
                raise ValueError(f'{paths[0]}: filmstrip height incompatible with {count} frames')
            step = sheet.height // count
            frames = np.stack([np.asarray(sheet.crop((0, i * step, sheet.width, (i + 1) * step))
                                          .convert('RGBA').resize((w, h), Image.Resampling.LANCZOS))
                               for i in range(count)])
    colours = frames[:, :, :, :3]
    span = colours.max(axis=0) - colours.min(axis=0)
    mask = (frames[:, :, :, 3].min(axis=0) >= 245) & (span.max(axis=2) > 20)
    return mask, colours[:, mask].astype(np.int16)


def check_discrete_art(rows, controls, values, skin_dir, settled):
    """Validate the independent reference itself against the bound host state.

    A five-state button can be 97% identical to the wrong frame. Mean error on
    the state-dependent pixels distinguishes its glyph from the shared ground.
    Exclude compositor markers: the overlay intentionally covers them.
    """
    definitions = {c['id']: c for c in controls if c.get('button_art') or
                   (c.get('filmstrip') and (c['type'] == 'Image' or (c['passive'] and c['filmstrip'][1] <= 32)))}
    for row in rows:
        control = definitions.get(row['id'])
        if not control or row['result'] == 'unknown_gate' or '/Overlay ' in row['id']:
            continue
        if row['result'] == 'input_or_container':
            continue
        handle = parameter(control['handles'].get('handleName', ''))
        x, y, w, h = row['rect']
        if handle not in values or not (0 <= x < x + w <= 1280 and 110 <= y < y + h <= 738):
            continue
        if control.get('button_art'):
            off, on = control['button_art']
            if off == on:
                continue
            names = (off, on)
            count = 2
            group = max(2, control['data'].get('numButtonsInGroup', 1))
            selected = int(values[handle] * (group - 1) + .5)
            expected = int(selected == control['data'].get('buttonId', 1))
        else:
            name, count = control['filmstrip']
            names = (name,)
            expected = int(values[handle] * (count - 1) + .5)
        paths = tuple(Path(skin_dir) / name for name in names)
        if not all(p.is_file() for p in paths):
            row['art'] = {'assets': names, 'status': 'missing_asset'}
            continue
        if not 0 <= expected < count:
            raise ValueError(f'{row["id"]}: normalized VST value outside filmstrip')
        mask, colours = changing_art_pixels(paths, count, w, h)
        record = dict(assets=names, expected=expected, pixels=int(mask.sum()))
        row['art'] = record
        if record['pixels'] < 6:
            record['status'] = 'indistinguishable'
            continue
        seen = np.asarray(settled.crop((x, y, x + w, y + h)))[mask]
        errors = np.abs(colours - seen).mean(axis=(1, 2))
        best = float(errors.min())
        closest = np.flatnonzero(errors <= best + 1.0).tolist()
        record.update(closest=closest, best_error=round(best, 3), expected_error=round(float(errors[expected]), 3))
        if best > 15:
            record['status'] = 'unclassified'
        elif closest == [expected]:
            record['status'] = 'expected'
        elif expected in closest or errors[expected] - best < 4:
            record['status'] = 'ambiguous'
        else:
            record['status'] = 'wrong_state'
            row['region_result'] = row['result']
            row['region_seconds'] = row.get('seconds')
            row.update(result='wrong_settled_state', seconds=None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tui', type=Path, required=True)
    parser.add_argument('--view', required=True)
    parser.add_argument('--skin-dir', type=Path, help='PNG directory if separate from TUI.json')
    parser.add_argument('--popup', help='full-screen popup definition independently confirmed in the final capture')
    parser.add_argument('--vst', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--graphs', type=Path, help='JSON array of [label, [screen x,y,w,h]] verified for this state')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    tabs, inventory_rows = inventory(args.tui, visual_leaves=True, skin_dir=args.skin_dir,
                                     popup=(args.view, args.popup) if args.popup else None)
    if args.view not in tabs:
        parser.error(f'unknown TUI view {args.view}')
    values = {int(k): v for k, v in json.loads(args.vst.read_text()).items()}
    controls = [x for x in inventory_rows if x['view'] == args.view]
    graphs = json.loads(args.graphs.read_text()) if args.graphs else []
    before, ref, frames, times, settled, ref_seconds = read_capture(args.capture)
    rows = measure(before, ref, frames, times, controls, values, graphs)
    check_discrete_art(rows, controls, values, args.skin_dir or args.tui.parent, settled)
    args.out.write_text(json.dumps({'view': args.view, 'sample_seconds': times, 'reference_seconds': ref_seconds,
                                    'results': dict(Counter(row['result'] for row in rows)),
                                    'rows': rows}, separators=(',', ':')) + '\n')


if __name__ == '__main__':
    main()
