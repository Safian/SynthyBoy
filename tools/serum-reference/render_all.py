#!/usr/bin/env python3
"""SynthyBoy - serum-reference: renders every .SerumPreset of a folder through the real Serum 2 (synthyboy_serum_reference), unattended and resumable.

  render_all.py --host BIN --presets DIR --out DIR [--filter REGEX] [--scripts c3,c1,c5] [--timeout SEC] [--limit N] [--list]

One host process per preset and note script (a plug-in that blocks or crashes costs one preset, never the batch), low priority (nice 19), strictly sequential.
A render is skipped when its sidecar exists with the same preset sha256, note script, plug-in and tool version (resume). A failure writes <id>.<script>.fail.json
(reason, exit code, stderr tail) and the batch goes on; failures are retried only with --retry-failed.
The reference audio is licensed plug-in output: tests/serum-reference/ is gitignored, never commit it.
"""
import argparse, hashlib, json, os, re, subprocess, sys, time

TOOL_VERSION = 1
# note scripts: name -> (notes spec for the host, length in seconds)
SCRIPTS = {
    'c3': ('60:100:0:2.5', 4.0),   # the default: C3 (MIDI 60) velocity 100 held 2.5 s + 1.5 s of tail
    'c1': ('36:64:0:2.5', 4.0),    # basses
    'c5': ('84:127:0:2.5', 4.0),   # leads, plucks, chords: the top of the keyboard at full velocity
}
# which presets get the second pass (the folder under Presets/Factory, or a word in the file name of a User preset)
SECOND_PASS = [(re.compile(r'bass|\bsub\b', re.I), 'c1'), (re.compile(r'lead|pluck|chord|bell|mallet|arp', re.I), 'c5')]


def preset_id(rel):
    return re.sub(r'[^A-Za-z0-9._+-]+', '_', os.path.splitext(rel)[0].replace(os.sep, '__'))


def scripts_for(rel, wanted):
    out = ['c3'] if 'c3' in wanted else []
    for rx, name in SECOND_PASS:
        if name in wanted and rx.search(rel):
            out.append(name)
            break
    return out


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', required=True)
    ap.add_argument('--presets', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--filter', default='')
    ap.add_argument('--scripts', default='c3,c1,c5')
    ap.add_argument('--timeout', type=int, default=90)
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--retry-failed', action='store_true')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--plugin', default='')
    a = ap.parse_args()
    wanted = [s for s in a.scripts.split(',') if s]
    rx = re.compile(a.filter) if a.filter else None
    items = []
    for root, _, files in os.walk(a.presets):
        for f in files:
            if f.endswith('.SerumPreset'):
                full = os.path.join(root, f)
                rel = os.path.relpath(full, a.presets)
                if rx and not rx.search(rel):
                    continue
                items.append((rel, full))
    items.sort()
    if a.limit:
        items = items[:a.limit]
    jobs = [(rel, full, s) for rel, full in items for s in scripts_for(rel, wanted)]
    if a.list:
        for rel, _, s in jobs:
            print('%s\t%s\t%s' % (preset_id(rel), s, rel))
        return 0
    os.makedirs(a.out, exist_ok=True)
    try:
        os.nice(19)
    except OSError:
        pass
    done = skipped = failed = 0
    t0 = time.time()
    for n, (rel, full, s) in enumerate(jobs, 1):
        pid = preset_id(rel)
        base = os.path.join(a.out, '%s.%s' % (pid, s))
        digest = sha256(full)
        if os.path.exists(base + '.json') and os.path.exists(base + '.wav'):
            try:
                side = json.load(open(base + '.json'))
                if side.get('presetSha256') == digest and side.get('toolVersion') == TOOL_VERSION and side.get('scriptName') == s:
                    skipped += 1
                    continue
            except (OSError, ValueError):
                pass
        if os.path.exists(base + '.fail.json') and not a.retry_failed:
            try:
                f = json.load(open(base + '.fail.json'))
                if f.get('presetSha256') == digest and f.get('toolVersion') == TOOL_VERSION:
                    failed += 1
                    continue
            except (OSError, ValueError):
                pass
        spec, length = SCRIPTS[s]
        cmd = [a.host, '--preset', full, '--id', pid, '--out', base + '.wav', '--notes', spec, '--length', str(length), '--timeout', str(a.timeout)]
        if a.plugin:
            cmd += ['--plugin', a.plugin]
        for stale in (base + '.json', base + '.fail.json', base + '.wav'):
            if os.path.exists(stale):
                os.remove(stale)
        start = time.time()
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.timeout + 30)
            rc, err = r.returncode, (r.stderr or '')[-800:]
        except subprocess.TimeoutExpired:
            rc, err = 5, 'supervisor timeout (the host did not exit)'
        if rc == 0 and os.path.exists(base + '.json'):
            side = json.load(open(base + '.json'))
            side.update({'presetSha256': digest, 'toolVersion': TOOL_VERSION, 'scriptName': s, 'presetPath': rel, 'wallSeconds': round(time.time() - start, 2)})
            json.dump(side, open(base + '.json', 'w'), indent=1)
            done += 1
        else:
            reason = {2: 'usage', 3: 'plugin_not_loaded', 4: 'preset_not_accepted', 5: 'timeout_or_dialog', 6: 'render_failed'}.get(rc, 'crash_or_signal(%d)' % rc)
            json.dump({'presetId': pid, 'presetPath': rel, 'presetSha256': digest, 'toolVersion': TOOL_VERSION, 'scriptName': s, 'exitCode': rc, 'reason': reason, 'stderr': err},
                      open(base + '.fail.json', 'w'), indent=1)
            failed += 1
            print('FAIL %s [%s] %s' % (pid, s, reason), flush=True)
        if n % 25 == 0:
            print('%d/%d  rendered %d  skipped %d  failed %d  %.0fs' % (n, len(jobs), done, skipped, failed, time.time() - t0), flush=True)
    print('done: %d jobs, rendered %d, skipped %d, failed %d, %.0fs' % (len(jobs), done, skipped, failed, time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
