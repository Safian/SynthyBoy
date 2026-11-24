#!/usr/bin/env python3
"""Produce a compact offline report from one hardware run's evidence."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import re
import statistics


def load_json(path):
    return json.loads(path.read_text())


def json_lines(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def callback_fields(report):
    def number(key):
        m = re.search(r'^' + key + r': (\d+(?:\.\d+)?)$', report, re.M)
        return float(m[1]) if m else None
    buckets = re.search(r'^gap_buckets[^:]*: (.+)$', report, re.M)
    counts = [int(v) for v in re.findall(r'x=(\d+)', buckets[1])] if buckets else []
    return number('callbacks'), counts, number('process_max_us'), number('nominal_block_us')


def summarize(root):
    lines = ['# MPC UI-teszt', '']
    if (root/'run.json').is_file():
        run = load_json(root/'run.json')
        lines += [f"Gép: {run.get('device', 'XL')} ({run.get('host', '')}).",
                  "Kiosztott nézetek: " + ", ".join(run.get("assigned_views", [])) + ".", ""]
        if run.get("project"):
            lines += [f"Projekt: `{run['project']}`. SHA-256 előtte `{run.get('before_project_sha256')}`, "
                      f"utána `{run.get('after_project_sha256')}`.", ""]
    for path in sorted(root.rglob('disabled-summary.json')):
        result = load_json(path)
        lines += ["## Inaktív natív kezelők", "", ", ".join(f"{k} {v}" for k,v in result["counts"].items()), ""]
    runs = sorted(root.rglob('results.json'))
    for path in runs:
        results = load_json(path)
        if not isinstance(results, list) or not all(isinstance(r, dict) and 'status' in r for r in results):
            continue
        label = path.parent.relative_to(root)
        counts = Counter(r['status'] for r in results)
        lines.extend([f'## {label}', '', ', '.join(f'{k} {v}' for k, v in sorted(counts.items())), ''])
        for row in results:
            if row['status'] != 'PASS':
                message = row.get('error') or row.get('reason') or row.get('status')
                lines.append(f"- `{row.get('id', '?')}` — {str(message).replace(chr(10), ' ')}")
        cov = path.with_name('coverage.json')
        if cov.exists():
            coverage = load_json(cov)
            if 'packaged_controls' in coverage:
                lines.extend(['', f"Azonosított TUI-bejegyzések: {coverage['packaged_controls']}. "
                              f"Funkcionális esettel igazolt bejegyzések: {len(coverage.get('verified', []))}."])
            if 'distinct_host_commands_by_tab' in coverage:
                total = coverage['distinct_host_commands_by_tab']
                verified = len(coverage['verified_commands'])
                lines.append(f"Fülönként elkülönített vezérlőparancsok közül igazolva {verified}/{total}.")
        lines.extend(['', f'[Esetenkénti eredmény]({path.relative_to(root)})', ''])
    for path in sorted(root.rglob('scalar-summary.json')):
        s = load_json(path)
        lines.extend(['## Natív potik és csúszkák', '', ', '.join(f'{k} {v}' for k, v in s['counts'].items()),
                      '', 'A PROXY_CHANGED a kijelzett és az engine-érték változását méri. A pontos célhoz külön ellenőrzés kell.',
                      '', f'[Paraméterváltozások]({path.with_name("scalars.jsonl").relative_to(root)})', ''])
    for path in sorted(root.rglob('summary.json')):
        if not path.with_name('samples.csv').is_file():
            continue
        s = load_json(path)
        with path.with_name('samples.csv').open() as f:
            rows = list(csv.DictReader(f))
        if not rows:
            lines.extend(['A telemetriából nem érkezett minta.', ''])
            continue
        elapsed = float(rows[-1]['uptime_s']) - float(rows[0]['uptime_s'])
        cpus = [float(r['cpu_percent_one_core']) for r in rows if r['cpu_percent_one_core']]
        lines.extend(['## Futás közbeni mérés', '', f"{len(rows)} minta, {elapsed:.1f} másodperc. "
                      f"Észlelt MPC-újraindítás: {s['observed_restarts']}. "
                      f"MPC nélküli minták: {s['samples_without_mpc']}.", ''])
        if cpus:
            lines.append(f"- MPC folyamat CPU-ja, egy maghoz viszonyítva: medián {statistics.median(cpus):.1f}%, maximum {max(cpus):.1f}%.")
        for field, caption in [('rss_kib','Mintákban mért RSS-csúcs'), ('pss_kib', 'Mintákban mért PSS-csúcs')]:
            if field in s['peak']:
                lines.append(f"- {caption}: {s['peak'][field]/1024:.1f} MiB.")
        if 'tmp_free_kib' in s['minimum']:
            lines.append(f"- Legkisebb szabad hely a firmware átmeneti fájlrendszerén: {s['minimum']['tmp_free_kib']/1024:.1f} MiB.")
        lines.append(f"- Hibamintára illeszkedő naplósorok: {s['matching_journal_lines']}.")
        cb = path.with_name('callbacks.jsonl')
        if cb.exists():
            sources = defaultdict(list)
            for row in json_lines(cb):
                sources[row['source']].append(callback_fields(row['report']))
            for source, snapshots in sources.items():
                samples, long_gaps = 0, 0
                for (old, old_b, _, _), (now, new_b, _, _) in zip(snapshots, snapshots[1:]):
                    if old is not None and now is not None and now >= old and len(old_b) == len(new_b) == 5:
                        samples += int(now-old)
                        long_gaps += max(0, new_b[-1]-old_b[-1])
                _, _, peak, nominal = snapshots[-1]
                lines.append(f"- {Path(source).name}: {len(snapshots)} audiojelentés, a jelentések között {samples} callback "
                             f"és {long_gaps} legalább nyolc blokknyi hívásköz.")
                if peak is not None and nominal is not None:
                    lines.append(f"- Az utolsó jelentésben az instance indulásától mért leghosszabb feldolgozás "
                                 f"{peak:.0f} µs, az utolsó blokk névleges ideje {nominal:.0f} µs.")
        lines.extend(['', f'[Mérési minták]({path.with_name("samples.csv").relative_to(root)}) · '
                      f'[Eseménynapló]({path.with_name("journal.log").relative_to(root)})', ''])
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    if not args.run.is_dir():
        parser.error('provide an existing hardware run directory')
    output = args.run / 'report.md'
    output.write_text(summarize(args.run))
    print(output)
