#!/usr/bin/env python3
"""Collect process and device telemetry over SSH during an MPC XL UI test."""

import argparse
import csv
import json
import os
from pathlib import Path
import re
import shlex
import selectors
import signal
import subprocess
import threading
import time


# /proc/PID/stat fields after the last ')' start at state (field 3).
PROBE = r'''
if command -v getconf >/dev/null; then
    ticks=$(getconf CLK_TCK)
else
    # BusyBox od prints four 32-bit words per row. AT_CLKTCK has type 17.
    case "$(uname -m)" in
        aarch64) ticks=$(od -Dv /proc/self/auxv | awk '$2+0 == 17 {print $4+0; exit}') ;;
        armv7*) ticks=$(od -Dv /proc/self/auxv | awk '{for(i=2;i<NF;i+=2) if($i+0==17){print $(i+1)+0;exit}}') ;;
        *) exit 1 ;;
    esac
fi
[ "${ticks:-0}" -gt 0 ] || exit 1
while :; do
    read -r uptime unused < /proc/uptime || exit 1
    pid=$(pidof MPC)
    pid=${pid%% *}
    cpu=-; started=-; threads=-; rss=-; peak=-; swap=-; avail=-; pss=-; fds=-; reads=-; writes=-
    if [ -n "$pid" ] && [ -r "/proc/$pid/stat" ]; then
        stat=$(cat "/proc/$pid/stat") || continue
        set -- ${stat##*) }
        cpu=$((${12} + ${13})); threads=${18}; started=${20}
        set -- $(awk '/^VmRSS:/{r=$2} /^VmHWM:/{h=$2} /^VmSwap:/{s=$2} END{print r+0,h+0,s+0}' "/proc/$pid/status")
        rss=$1; peak=$2; swap=$3
        fds=$(ls -1 "/proc/$pid/fd" 2>/dev/null | wc -l)
        if [ -r "/proc/$pid/smaps_rollup" ]; then
            pss=$(awk '/^Pss:/{print $2}' "/proc/$pid/smaps_rollup")
        fi
        if [ -r "/proc/$pid/io" ]; then
            set -- $(awk '/^read_bytes:/{r=$2} /^write_bytes:/{w=$2} END{print r+0,w+0}' "/proc/$pid/io")
            reads=$1; writes=$2
        fi
    fi
    avail=$(awk '/^MemAvailable:/{print $2}' /proc/meminfo)
    temp=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null)
    tmpfree=$(df -Pk /var/tmp | awk 'END{print $4}')
    datafree=$(df -Pk /data | awk 'END{print $4}')
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$uptime" "${pid:--}" "$started" "$cpu" "$ticks" "$rss" "$peak" "$swap" "${pss:--}" \
        "$threads" "$fds" "${avail:--}" "$tmpfree" "$datafree" "${temp:--}" "$reads" "$writes" || exit
    sleep "$interval"
done
'''

# Firmware diagnostics publish complete, atomically replaced text snapshots. Capture only
# files rewritten since this monitor started, so old instances cannot supply evidence.
CALLBACK_PROBE = r'''
started=$(date +%s)
while :; do
    for path in /tmp/synthyboy-callback-stats.txt /tmp/synthyboy-callback-stats.txt.[0-9]*; do
        [ -f "$path" ] || continue
        stamp=$(stat -c %Y "$path") || continue
        [ "$stamp" -ge "$started" ] || continue
        printf '@@sample %s %s\n' "$stamp" "$path"
        cat "$path" || exit
        printf '@@end\n' || exit
    done
    sleep 2
done
'''


FIELDS = ("uptime_s pid start_ticks cpu_ticks ticks_per_s rss_kib peak_rss_kib swap_kib pss_kib "
          "threads fds available_kib tmp_free_kib data_free_kib thermal_millidegrees read_bytes written_bytes").split()
ERROR = re.compile(r'segfault|SIGSEGV|status=11/SEGV|out of memory|oom.kill|xrun|underrun|'
                   r'overrun|assertion failed|crashpad|failed to|no space left', re.I)


def read_sample(line, previous):
    cells = line.rstrip().split('\t')
    if len(cells) != len(FIELDS):
        raise ValueError(f'expected {len(FIELDS)} fields, received {len(cells)}')
    sample = {k: (None if v.strip() in ('', '-') else float(v) if k == 'uptime_s' else int(v))
              for k, v in zip(FIELDS, cells)}
    sample['local_unix_s'] = time.time()
    sample['cpu_percent_one_core'] = None
    if previous and sample['pid'] and (sample['pid'], sample['start_ticks']) == (previous['pid'], previous['start_ticks']):
        elapsed = sample['uptime_s'] - previous['uptime_s']
        ticks = sample['cpu_ticks'] - previous['cpu_ticks']
        if elapsed > 0 and ticks >= 0:
            sample['cpu_percent_one_core'] = round(100 * ticks / (sample['ticks_per_s'] * elapsed), 2)
    return sample


def atomic_json(path, data):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(data, indent=2) + '\n')
    os.replace(tmp, path)


def monitor(host, out, interval, duration=None, ready=None):
    out.mkdir(parents=True, exist_ok=True)
    ssh = ['ssh', '-T', '-oBatchMode=yes', '-oConnectTimeout=5', '-oServerAliveInterval=5',
           '-oServerAliveCountMax=2', '--', host]
    stop = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.set())
    remote = 'interval=' + str(interval) + '; ' + PROBE
    journal = ['journalctl', '-f', '-n', '0', '--no-pager', '-o', 'short-unix',
               '_SYSTEMD_UNIT=acvs.service', '+', 'UNIT=acvs.service', '+', '_TRANSPORT=kernel']
    procs = []
    first, previous, peaks, low = None, None, {}, {}
    events, missing, count = 0, 0, 0
    identities = []
    callback_count = 0
    reason = 'stopped'
    with (out / 'monitor.stderr').open('x') as err, (out / 'journal.log').open('x') as logs, \
         (out / 'samples.csv').open('x', newline='') as rows, (out / 'events.jsonl').open('x') as alerts, \
         (out / 'callbacks.jsonl').open('x') as callbacks:
        def start(command):
            proc = subprocess.Popen(ssh + [command], stdout=subprocess.PIPE, stderr=err,
                                    text=True, errors='replace', bufsize=1)
            procs.append(proc)
            return proc
        def collect_journal(proc):
            nonlocal events
            for line in proc.stdout:
                logs.write(line)
                logs.flush()
                if ERROR.search(line):
                    alerts.write(json.dumps({'local_unix_s': time.time(), 'journal': line.rstrip()}) + '\n')
                    alerts.flush()
                    events += 1
        def collect_callbacks(proc):
            nonlocal callback_count
            seen = {}
            name, stamp, payload = None, None, []
            for line in proc.stdout:
                if line.startswith('@@sample '):
                    _, stamp, name = line.strip().split(maxsplit=2)
                    payload = []
                elif name is not None and line.strip() == '@@end':
                    snapshot = ''.join(payload)
                    if snapshot.startswith('synthyboy-callback-stats ') and seen.get(name) != snapshot:
                        seen[name] = snapshot
                        callbacks.write(json.dumps({'local_unix_s': time.time(), 'device_unix_s': int(stamp),
                                        'source': name, 'report': snapshot}) + '\n')
                        callbacks.flush()
                        callback_count += 1
                    name = None
                elif name is not None:
                    payload.append(line)

        audio = start(CALLBACK_PROBE)
        audio_reader = threading.Thread(target=collect_callbacks, args=(audio,), daemon=True)
        audio_reader.start()
        watcher = start(shlex.join(journal))
        reader = threading.Thread(target=collect_journal, args=(watcher,), daemon=True)
        reader.start()
        probe = start(remote)
        writer = csv.DictWriter(rows, fieldnames=['local_unix_s', *FIELDS, 'cpu_percent_one_core'])
        writer.writeheader()
        deadline = time.monotonic() + duration if duration else None
        try:
            with selectors.DefaultSelector() as poll:
                poll.register(probe.stdout, selectors.EVENT_READ)
                last_sample = time.monotonic()
                while not stop.is_set() and (deadline is None or time.monotonic() < deadline):
                    if audio.poll() is not None:
                        raise RuntimeError('audio diagnostics stream stopped unexpectedly')
                    if watcher.poll() is not None:
                        raise RuntimeError('journal stream stopped unexpectedly')
                    if not poll.select(0.25):
                        if time.monotonic() - last_sample > max(15, 4 * interval):
                            raise RuntimeError('device telemetry timed out')
                        continue
                    line = probe.stdout.readline()
                    if not line:
                        raise RuntimeError('telemetry stream stopped unexpectedly')
                    sample = read_sample(line, previous)
                    last_sample = time.monotonic()
                    writer.writerow(sample)
                    rows.flush()
                    count += 1
                    if first is None:
                        first = sample
                        if ready:
                            ready.touch()
                    if sample['pid'] is None:
                        missing += 1
                    else:
                        identity = (sample['pid'], sample['start_ticks'])
                        if not identities or identities[-1] != identity:
                            identities.append(identity)
                    for k in ('cpu_percent_one_core', 'rss_kib', 'peak_rss_kib', 'pss_kib', 'swap_kib',
                              'threads', 'fds', 'thermal_millidegrees'):
                        if sample[k] is not None:
                            peaks[k] = max(peaks.get(k, sample[k]), sample[k])
                    for k in ('available_kib', 'tmp_free_kib', 'data_free_kib'):
                        if sample[k] is not None:
                            low[k] = min(low.get(k, sample[k]), sample[k])
                    previous = sample
        except Exception as exc:
            reason = str(exc)
            raise
        finally:
            for proc in procs:
                if proc.poll() is None:
                    proc.terminate()
            for proc in procs:
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            reader.join(timeout=2)
            audio_reader.join(timeout=2)
            atomic_json(out / 'summary.json', {'host': host, 'interval_s': interval, 'samples': count,
                        'first_unix_s': first and first['local_unix_s'], 'last_unix_s': previous and previous['local_unix_s'],
                        'process_identities': identities, 'samples_without_mpc': missing, 'observed_restarts': max(0,len(identities)-1),
                        'callback_reports': callback_count, 'peak': peaks, 'minimum': low, 'matching_journal_lines': events, 'termination': reason})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='root@192.168.0.126')
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--interval', type=int, default=1, choices=range(1, 31), metavar='1..30')
    parser.add_argument('--seconds', type=int)
    parser.add_argument('--ready-file', type=Path)
    args = parser.parse_args()
    if args.seconds is not None and args.seconds < 1:
        parser.error('--seconds must be positive')
    monitor(args.host, args.out, args.interval, args.seconds, args.ready_file)
