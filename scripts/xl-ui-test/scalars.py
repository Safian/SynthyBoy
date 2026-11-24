#!/usr/bin/env python3
"""Exercise the visible native dials and sliders of an XL test project in their real UI."""
import argparse
from collections import Counter
from contextlib import contextmanager
import hashlib
import json
import math
from pathlib import Path
import re
import shlex
import sys
import time

from device import navigate
from shard import MODELS, add_shard, selected
from inventory import inventory, locate
from disabled import OSC

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import native_ui_params as native


def snapshot(device):
    state = device.snapshot()
    engine, values = state["engine"], state["vst"]
    if len(values) <= len(engine):
        raise ValueError("The installed XL test package must expose normalized native values")
    return dict(enumerate(engine)), engine, values


def parameter(handle):
    m = re.fullmatch(r'Parameter (\d+)', handle)
    return int(m.group(1)) if m else None


def visible(gate, values):
    parts = gate.split('/')
    if gate == 'Invalid':
        return False
    n = parameter(parts[-1])
    if n not in values:
        return None
    value = values[n]
    if parts[0] == 'NonZero' and len(parts) == 2:
        return value != 0
    if parts[0] == 'IndexedEnabling' and len(parts) == 4:
        low = high = int(parts[1])
    elif parts[0] == 'RangedEnabling' and len(parts) == 5:
        low, high = int(parts[1]), int(parts[2])
    else:
        return None
    frame = math.floor(value * (int(parts[-2]) - 1) + 0.5)
    return low <= frame <= high


def expected_key(control, engine, context, vst=None):
    path = control["id"]
    parts = path.split("/")
    base = native.mixer_peek_index(len(engine))
    def frame(offset, last):
        value = (vst or {}).get(base + offset)
        if value is None:
            return None
        scaled = value * last
        rounded = round(scaled)
        return rounded if 0 <= rounded <= last and abs(scaled-rounded) < .0001 else None

    env = re.search(r"/Env Native (Attack|Decay|Release|Sustain|Attack Curve|Decay Curve|Release Curve|Hold|Delay)/knob$", path)
    if env:
        selection = frame(native.ENV_SELECTION, 5)
        if selection is not None:
            prefix = f"env{selection+1}."
            field = env[1].lower().replace(" ", "_")
            if field in ("attack","decay","release","hold","delay") and engine.get(prefix+"units",0) >= .5:
                field += "_beats"
            key = prefix + field
            return key if key in engine else None
    lfo = re.search(r"/LFO 1 Native [0-9-]+\.(\d+)\.0/knob$", path)
    if lfo:
        # native_lfo_controls.h publishes selection as (slot - 1)/13, including Random 1..4.
        selection = frame(native.LFO_OFFSET, 13)
        suffix = {4:"smooth_time", 7:"rate", 9:"fade_in", 13:"phase", 14:"delay", 17:"swing",18:"stereo"}.get(int(lfo[1]))
        if selection is not None and suffix:
            owner = f"lfo{selection+1}" if selection < 10 else f"rlfo{selection-9}"
            key = owner + "." + suffix
            return key if key in engine else None
    mod = re.search(r"/Mod Native (?:Scroll |Normal )?Row ([1-7])/AMOUNT/amount$", path)
    if mod:
        slot = frame(native.MOD_ROWS + int(mod[1]) - 1, 64)
        if slot:
            key = f"mod{slot:02d}.amount"
            return key if key in engine else None
    filt = re.search(r"/Filter Native (?:Split )?(Cutoff|Reso|Drive|Mix|Keytrack|Var|Comb|Blend|X|Y|Transpose|F Reso|Spread) \d+/knob$", path)
    if filt:
        instance = frame(native.FILTER_STATE_OFFSET, 4)
        if instance in (1,2):
            suffix = {"Reso":"resonance","Comb":"comb_offset","X":"formant.x","Y":"formant.y","F Reso":"formant.resonance",
                      "Transpose":"formant.transpose","Spread":"formant.spread"}.get(filt[1],filt[1].lower())
            key = f"filter{instance}." + suffix
            return key if key in engine else None
    if "Osc Native Settings" in parts:
        # 57_osc_settings serializes field.key. All three ABC surfaces reuse the osc1 form
        # and bind that field to the source specified by their enclosing native instance.
        key = re.sub(r" \[\d+\]$", "", parts[-2])
        for i, label in enumerate(("Osc A", "Osc B", "Osc C"), 1):
            if label in parts and key.startswith("osc1."):
                key = f"osc{i}." + key[5:]
        return key if key in engine else None
    if "Osc Native Detail" in parts:
        owner = {"sub_blend":"sub", "noise_blend":"noise", "noise_sample_blend":"noise"}.get(parts[-2])
        if owner:
            return owner + ".filter_blend"
    play = re.search(r"/Play Native OSC (Sub|Osc A|Osc B|Osc C|Noise)[/ ](pan|level)/knob$", path)
    if play:
        source = {"Sub":"sub","Osc A":"osc1","Osc B":"osc2","Osc C":"osc3","Noise":"noise"}[play[1]]
        return source + "." + play[2]
    match = re.fullmatch(r"play1/Play (Env|LFO) (Attack|Decay|Sustain|Release|Rise|Smooth)/knob", path)
    if match:
        group, field = match[1].lower(), match[2].lower()
        instance = context.get("play_" + group)
        prefix = group + str(instance) + "."
        if group == "env" and field in ("attack","decay","release") and engine.get(prefix+"units",0) >= .5:
            field += "_beats"
        if group == "lfo":
            field = {"rise":"fade_in","smooth":"smooth_time"}.get(field, field)
        key = prefix + field
        return key if key in engine else None


@contextmanager
def source_on(device, view, controls, state, log):
    if view not in OSC:
        yield state
        return
    label, key = OSC[view]
    if state[1][key] == 1:
        yield state
        return
    button = locate(controls, view, f"{view}/Osc Native Surface/Osc Native Power/Osc Native {label} Power")
    if button["type"] != "Button" or button.get("passive") or not all(visible(g,state[2]) is True for g in button["gates"]):
        raise RuntimeError(f"native {key} power button is unavailable")
    x, y, w, h = button["rect"]

    def toggle(before, expected):
        device.gesture(x+w//2, y+h//2)
        after = snapshot(device)
        diff = {k:[v,after[1][k]] for k,v in before[1].items() if after[1][k] != v}
        log(dict(view=view, control=button["id"], expected=expected, changes=diff))
        if set(diff) != {key} or after[1][key] != expected:
            raise RuntimeError(f"{key} native POWER expected {expected}, changed {diff}")
        return after

    try:
        yield toggle(state,1)
    finally:
        if device.status().get("view") != view:
            raise RuntimeError("wrong native page for restoring " + key)
        before = snapshot(device)
        if before[1][key] != state[1][key]:
            toggle(before,state[1][key])


def exercise(device, control, state, context):
    names, before, values = state
    num = parameter(control['handles'].get('handleName', ''))
    if num not in values:
        return {'status': 'UNAVAILABLE', 'reason': 'unresolved Data handle'}, state
    x, y, width, height = control['rect']
    cx, cy = x + width // 2, y + height // 2
    if width <= 0 or height <= 0 or not (0 <= cx < 1280 and 110 <= cy < 738):
        return {'status': 'UNAVAILABLE', 'reason': 'outside native tab'}, state
    sign = -1 if values[num] > 0.5 else 1
    horizontal = control['type'] == 'Slider' and width > height
    distance = min(45, width // 2 if horizontal else height // 2)
    dx, dy = (sign * distance, 0) if horizontal else (0, -sign * distance)
    device.gesture(cx, cy, dx, dy, duration=240)
    after = snapshot(device)
    changed = {k: [v, after[1][k]] for k, v in before.items() if after[1][k] != v}
    target = expected_key(control, before, context, values) or names.get(num)
    if target:
        ok = set(changed) == {target} and after[2][num] != values[num]
        status = 'PASS' if ok else 'FAIL'
    else:
        status = 'PROXY_CHANGED' if len(changed) == 1 and after[2][num] != values[num] else 'FAIL'
    return {'status': status, 'parameter': num, 'expected_engine_key': target,
            'gesture': [cx, cy, dx, dy], 'bound_before': values[num],
            'bound_after': after[2][num], 'engine_changes': changed}, after


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tui', required=True, type=Path)
    p.add_argument('--installed-tui', default=None)
    p.add_argument('--host', default=None)
    p.add_argument('--tool-dir', default=None)
    p.add_argument('--out', required=True, type=Path)
    add_shard(p)
    args = p.parse_args()
    model = MODELS[args.device]
    views = selected(args, p)
    args.host = args.host or model.host
    args.tool_dir = args.tool_dir or model.helper_dir
    args.installed_tui = args.installed_tui or model.tui
    args.out.mkdir(parents=True, exist_ok=True)
    device = model.adapter(args.host, args.tool_dir)
    expected = hashlib.sha256(args.tui.read_bytes()).hexdigest()
    if device.remote('sha256sum ' + shlex.quote(args.installed_tui)).decode().split()[0] != expected:
        p.error('installed and inventoried skin hashes differ')
    tabs, controls = inventory(args.tui)
    if any(v not in tabs for v in views):
        p.error('unknown tab identifier')
    counts = Counter()
    pid = device.remote('pidof MPC').strip()
    if not pid:
        p.error('MPC process is missing')
    with (args.out / 'scalars.jsonl').open('x') as report:
        def record(item):
            counts[item['status']] += 1
            report.write(json.dumps(dict(local_unix_s=time.time(), **item)) + '\n')
            report.flush()

        try:
            for view in views:
                if device.remote('pidof MPC').strip() != pid:
                    raise RuntimeError('MPC restarted')
                try:
                    navigate(device, tabs, view)
                    state = snapshot(device)
                except Exception as exc:
                    record({'view': view, 'status': 'UNAVAILABLE', 'reason': str(exc)})
                    continue
                def log_power(event):
                    with (args.out / "source-power.jsonl").open("a") as power:
                        power.write(json.dumps(dict(local_unix_s=time.time(), **event)) + "\n")
                try:
                    with source_on(device, view, controls, state, log_power) as state:
                        for c in (x for x in controls if x['view'] == view and x['type'] in ('Knob', 'Slider')):
                            item = {'id': c['id'], 'view': view}
                            if c.get("passive"):
                                record(dict(item, status="PASSIVE", reason="packaged control disables touch or drag"))
                                continue
                            guards = [visible(gate, state[2]) for gate in c['gates']]
                            if False in guards or None in guards:
                                record(dict(item, status='HIDDEN' if False in guards else 'UNAVAILABLE', gates=c['gates']))
                                continue
                            try:
                                current = device.status()
                                if current['view'] != view or device.remote('pidof MPC').strip() != pid:
                                    raise RuntimeError('view or MPC process changed before gesture')
                                result, state = exercise(device, c, state, current)
                                record(dict(item, **result))
                            except Exception as exc:
                                record(dict(item, status='FAIL', reason=str(exc)))
                                break
                except RuntimeError as error:
                    record(dict(view=view, status="FAIL", reason=str(error)))
                    break
                print(view, dict(counts), flush=True)
        finally:
            (args.out / 'scalar-summary.json').write_text(json.dumps(
                {'counts': dict(counts), 'tui_sha256': expected, 'pid': pid.decode(),
                 'scope': 'currently reachable native dials and sliders; proxy changes require a separate target oracle'},
                indent=2) + '\n')
            if device.remote('pidof MPC').strip() == pid:
                navigate(device, tabs, 'play1')
    return int(counts['FAIL'] > 0 or counts['UNAVAILABLE'] > 0 or counts['PASS'] == 0)


if __name__ == '__main__':
    raise SystemExit(main())
