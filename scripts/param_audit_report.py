#!/usr/bin/env python3
# SynthyBoy - merges the shards of the parameter effect audit (tools/param-audit) into report.md / keys.csv / keys.json / summary.json,
# compares them with the committed baseline and decides the verdict of the run. Called by scripts/run-param-audit.sh; usable alone:
#
#   param_audit_report.py --in DIR --out DIR [--baseline FILE] [--update-baseline] [--exemptions FILE] [--semantics-log FILE] [--mode quick|full]
#                         [--wall SECONDS] [--keys-regex RX]
#
# Verdicts per key: ALIVE, DEAD (no metric moved and the audio changed less than the noise floor in every context of the ladder), UNSUPPORTED (the engine does not act on it),
# EXEMPT (cannot be rendered in the harness, reason in the report). A DEAD key is EXPLAINED when scripts/../tools/param-audit/exemptions.tsv names it (regex, category, reason).
# Modulatable keys are swept through a routed source too: MOD-ALIVE / MOD-DEAD (explained the same way with the prefix "mod:").
# Exit code 0: clean; 1: a regression against the baseline or an unexplained DEAD / MOD-DEAD / finding key.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import argparse, csv, glob, json, math, os, re, sys, zlib


def load(indir):
    rows = []
    for f in sorted(glob.glob(os.path.join(indir, "keys*.jsonl"))):
        with open(f) as fh:
            for line in fh:
                line = line.strip()
                if line:
                    rows.append(json.loads(line))
    rows.sort(key=lambda r: r["key"])
    return rows


def load_exemptions(path):
    out = []
    if path and os.path.exists(path):
        with open(path) as fh:
            for line in fh:
                if not line.strip() or line.startswith("#"):
                    continue
                parts = line.rstrip("\n").split("\t")
                if len(parts) >= 3:
                    out.append((re.compile(parts[0]), parts[1], parts[2]))
    return out


def explained(exemptions, key, kind):
    for rx, cat, why in exemptions:
        pat = rx.pattern
        if kind == "mod" and not pat.startswith("mod:"):
            continue
        if kind != "mod" and pat.startswith("mod:"):
            continue
        k = key
        if kind == "mod":
            rx2 = re.compile(pat[4:])
            if rx2.search(k):
                return cat, why
        elif rx.search(k):
            return cat, why
    return None


def quant(row):
    """Compact curve of a key for the baseline: nine grid points x (rms dB, centroid in quarter octaves, f0 in semitones, balance dB) + the modulation curve (5 points x rms, centroid)."""
    q = []
    for p in row.get("grid", []):
        rms, cen, bal, f0 = p[1], p[3], p[4], p[5]
        q += [int(round(rms)), int(round(4 * math.log2(cen + 1))), int(round(12 * math.log2(f0 + 1))), int(round(bal))]
    m = row.get("mod", {})
    for p in m.get("grid", []):
        q += [int(round(p[1])), int(round(4 * math.log2(p[3] + 1)))]
    return q


def checksum(q):
    return "%08x" % (zlib.crc32(",".join(map(str, q)).encode()) & 0xffffffff)


def make_baseline(rows, mode):
    keys = {}
    for r in rows:
        mv = r.get("mod", {}).get("verdict", "n/a")
        q = quant(r)
        entry = {"v": r["verdict"], "m": mv, "h": checksum(q), "q": q}
        if r.get("noiseDb", -180.0) > -60.0:
            entry["n"] = 1   # the context is random (noise above the floor): the curve is not comparable between runs, only the verdict is
        keys[r["key"]] = entry
    return {"format": "synthyboy-param-audit-baseline", "version": 1, "mode": mode, "keys": keys}


def compare(base, rows, full_keyset):
    """Returns (regressions, drifts, new_keys, missing). A regression is a changed verdict / mod verdict or a curve that moved by more than one quantum anywhere."""
    regress, drifts, new = [], [], []
    for r in rows:
        k = r["key"]
        b = base["keys"].get(k)
        mv = r.get("mod", {}).get("verdict", "n/a")
        if b is None:
            new.append(k)
            continue
        if b["v"] != r["verdict"] or b["m"] != mv:
            regress.append("%s: verdict %s/%s -> %s/%s" % (k, b["v"], b["m"], r["verdict"], mv))
            continue
        q = quant(r)
        if checksum(q) == b["h"] or b.get("n") or r.get("noiseDb", -180.0) > -60.0:
            continue
        bq = b["q"]
        if len(bq) != len(q):
            regress.append("%s: curve has %d points, baseline %d" % (k, len(q), len(bq)))
            continue
        worst = max(abs(a - c) for a, c in zip(q, bq)) if q else 0
        if worst > 1:
            idx = max(range(len(q)), key=lambda i: abs(q[i] - bq[i]))
            regress.append("%s: curve moved (point %d: baseline %d, now %d, quanta of 1 dB / quarter octave / semitone)" % (k, idx, bq[idx], q[idx]))
        else:
            drifts.append(k)
    missing = [k for k in base["keys"] if k not in {r["key"] for r in rows}] if full_keyset else []
    return regress, drifts, new, missing


def group_of(key):
    parts = key.split(".")
    if re.match(r"mod\d\d$", parts[0]):
        return "mod (64 slots)"
    if re.match(r"fxi\d\d$", parts[0]):
        return "fxi (16 instances)"
    if re.match(r"(osc[123]|lfo\d+|rlfo\d|env\d)$", parts[0]):
        return re.sub(r"\d+$", "N", parts[0]) if parts[0].startswith(("lfo", "rlfo", "env")) else parts[0]
    return parts[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="indir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--baseline")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--exemptions")
    ap.add_argument("--semantics-log")
    ap.add_argument("--mode", default="quick")
    ap.add_argument("--wall", type=float, default=0.0)
    ap.add_argument("--keys-regex", default="")
    ap.add_argument("--engine", default="vital")
    a = ap.parse_args()

    rows = load(a.indir)
    exemptions = load_exemptions(a.exemptions)
    os.makedirs(a.out, exist_ok=True)
    total_keys = len(rows)
    sup = [r for r in rows if r["verdict"] not in ("UNSUPPORTED",)]
    alive = [r for r in rows if r["verdict"] == "ALIVE"]
    exempt = [r for r in rows if r["verdict"] == "EXEMPT"]
    dead = [r for r in rows if r["verdict"] == "DEAD"]
    dead_unexpl, dead_expl = [], []
    for r in dead:
        e = explained(exemptions, r["key"], "key")
        (dead_expl if e else dead_unexpl).append((r, e))
    moddead = [r for r in rows if r.get("mod", {}).get("verdict") == "DEAD"]
    moddead_unexpl, moddead_expl = [], []
    for r in moddead:
        e = explained(exemptions, r["key"], "mod")
        (moddead_expl if e else moddead_unexpl).append((r, e))
    modalive = [r for r in rows if r.get("mod", {}).get("verdict") == "ALIVE"]
    findings = []
    # instances 2 .. N of a family are the same code as instance 1: their verdicts must equal it (a difference is a wiring error of one instance)
    by_key = {r["key"]: r for r in rows}
    fam = [(re.compile(r"^osc([23])\.(.*)$"), "osc1."), (re.compile(r"^lfo(\d+)\.(.*)$"), "lfo1."), (re.compile(r"^rlfo(\d+)\.(.*)$"), "rlfo1."), (re.compile(r"^env(\d+)\.(.*)$"), "env2."),
           (re.compile(r"^mod(\d\d)\.(.*)$"), "mod01."), (re.compile(r"^fxi(\d\d)\.(.*)$"), "fxi01.")]
    for r in rows:
        for rx, first in fam:
            m = rx.match(r["key"])
            if not m or (first == "env2." and m.group(1) in ("1", "2")) or (first not in ("osc1.", "env2.") and m.group(1) in ("1", "01")):
                continue
            ref = by_key.get(first + m.group(2))
            if ref is None:
                continue
            va, vb = (r["verdict"], r.get("mod", {}).get("verdict")), (ref["verdict"], ref.get("mod", {}).get("verdict"))
            if va != vb and "UNSUPPORTED" not in va + vb:
                findings.append((r["key"], "FAMILY-INCONSISTENT: verdict %s/%s but %s is %s/%s" % (va[0], va[1], ref["key"], vb[0], vb[1]), explained(exemptions, r["key"] + " family", "key")))
    for r in rows:
        for f in r.get("findings", []):
            e = explained(exemptions, r["key"] + " " + f.split(" ")[0], "key")
            findings.append((r["key"], f, e))
    unexplained_findings = [x for x in findings if not x[2] and not x[1].startswith('INFO')]

    sem = {"passed": None, "failed": None, "cases": None}
    if a.semantics_log and os.path.exists(a.semantics_log):
        txt = open(a.semantics_log).read()
        m = re.search(r"(\d+) test case\(s\), (\d+) check\(s\), (\d+) failed case\(s\)", txt)
        if m:
            sem = {"cases": int(m.group(1)), "checks": int(m.group(2)), "failed": int(m.group(3)), "passed": int(m.group(1)) - int(m.group(3))}

    # baseline
    regress, drifts, new, missing = [], [], [], []
    base_note = "no baseline"
    if a.update_baseline and a.baseline:
        new_base = make_baseline(rows, a.mode)
        if os.path.exists(a.baseline) and (a.mode != "full" or a.keys_regex):
            old = json.load(open(a.baseline))   # a partial run only replaces the keys it covered
            old["keys"].update(new_base["keys"])
            new_base["keys"] = old["keys"]
            new_base["mode"] = old.get("mode", a.mode)
        with open(a.baseline, "w") as fh:
            json.dump(new_base, fh, separators=(",", ":"), sort_keys=True)
            fh.write("\n")
        base_note = "baseline updated (%d keys in the file)" % len(new_base["keys"])
    elif a.baseline and os.path.exists(a.baseline):
        base = json.load(open(a.baseline))
        regress, drifts, new, missing = compare(base, rows, a.mode == 'full' and not a.keys_regex)
        base_note = "compared with %s" % os.path.basename(a.baseline)

    # CSV / JSON
    with open(os.path.join(a.out, "keys.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["key", "verdict", "context", "moved", "mod_verdict", "mod_moved", "values", "distinct", "max_diff_db", "findings"])
        for r in rows:
            m = r.get("mod", {})
            w.writerow([r["key"], r["verdict"], r["context"], " ".join(r["moved"]), m.get("verdict", "n/a"), " ".join(m.get("moved", [])), r["values"], r["distinct"],
                        "%.1f" % r["maxDiffDb"], " | ".join(r["findings"])])
    with open(os.path.join(a.out, "keys.json"), "w") as fh:
        json.dump(rows, fh, separators=(",", ":"))
    compact = os.path.join(a.out, "keys-compact.csv")
    with open(compact, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["key", "verdict", "moved", "mod"])
        for r in rows:
            w.writerow([r["key"], r["verdict"], " ".join(r["moved"][:4]), r.get("mod", {}).get("verdict", "n/a")])

    # report.md
    L = []
    L.append("# Parameter effect audit - %s run" % a.mode)
    L.append("")
    L.append("| keys in the table | supported by the %s engine | alive | exempt | dead (explained) | dead (UNEXPLAINED) | modulation alive | modulation dead (explained / UNEXPLAINED) |" % a.engine)
    L.append("|---|---|---|---|---|---|---|---|")
    L.append("| %d | %d | %d | %d | %d | %d | %d | %d / %d |" % (total_keys, len(sup), len(alive), len(exempt), len(dead_expl), len(dead_unexpl), len(modalive), len(moddead_expl), len(moddead_unexpl)))
    L.append("")
    L.append("Findings (per value assertions): %d, unexplained %d. Baseline: %s; regressions %d, new keys %d, curve drifts within one quantum %d." % (len(findings), len(unexplained_findings), base_note, len(regress), len(new), len(drifts)))
    if sem["passed"] is not None:
        L.append("")
        L.append("Semantic checks (synthyboy_test_param_semantics): %d cases passed, %d failed (%d checks)." % (sem["passed"], sem["failed"], sem.get("checks", 0)))
    L.append("")
    L.append("## Per group")
    L.append("")
    L.append("| group | keys | alive | dead | exempt | unsupported | modulation alive | modulation dead |")
    L.append("|---|---|---|---|---|---|---|---|")
    groups = {}
    for r in rows:
        g = groups.setdefault(group_of(r["key"]), [0, 0, 0, 0, 0, 0, 0])
        g[0] += 1
        g[1] += r["verdict"] == "ALIVE"
        g[2] += r["verdict"] == "DEAD"
        g[3] += r["verdict"] == "EXEMPT"
        g[4] += r["verdict"] == "UNSUPPORTED"
        g[5] += r.get("mod", {}).get("verdict") == "ALIVE"
        g[6] += r.get("mod", {}).get("verdict") == "DEAD"
    for g in sorted(groups):
        v = groups[g]
        L.append("| %s | %d | %d | %d | %d | %d | %d | %d |" % ((g,) + tuple(v)))
    if regress:
        L.append("")
        L.append("## REGRESSIONS against the baseline")
        L.append("")
        for x in regress:
            L.append("- " + x)
    if new:
        L.append("")
        L.append("## Keys that the baseline does not know (run --update-baseline when they are intended)")
        L.append("")
        L.append(", ".join(new[:60]) + (" ..." if len(new) > 60 else ""))
    for title, items in (("DEAD keys, UNEXPLAINED", dead_unexpl), ("DEAD keys, explained", dead_expl), ("Modulation DEAD, UNEXPLAINED", moddead_unexpl), ("Modulation DEAD, explained", moddead_expl)):
        L.append("")
        L.append("## %s (%d)" % (title, len(items)))
        L.append("")
        if items:
            L.append("| key | context of the last try | category | reason |")
            L.append("|---|---|---|---|")
            for r, e in items:
                L.append("| `%s` | %s | %s | %s |" % (r["key"], (r.get("mod", {}).get("context") if "Modulation" in title else r["context"]) or "", e[0] if e else "-", e[1] if e else "-"))
    L.append("")
    L.append("## Findings of the per value assertions (%d)" % len(findings))
    L.append("")
    if findings:
        L.append("| key | finding | explained |")
        L.append("|---|---|---|")
        for k, f, e in findings[:400]:
            L.append("| `%s` | %s | %s |" % (k, f.replace("|", "/"), (e[0] + ": " + e[1]) if e else "-"))
        if len(findings) > 400:
            L.append("| ... | %d more in keys.csv | |" % (len(findings) - 400))
    L.append("")
    L.append("## Per key")
    L.append("")
    L.append("The full table is `keys.csv` (key, verdict, context, moved metrics, modulation verdict, values rendered, distinct sounds, max difference in dB, findings) and `keys.json` (with the metric curves).")
    open(os.path.join(a.out, "report.md"), "w").write("\n".join(L) + "\n")

    summary = {"keys": total_keys, "supported": len(sup), "alive": len(alive), "exempt": len(exempt), "dead": len(dead), "dead_unexplained": len(dead_unexpl), "mod_alive": len(modalive),
               "mod_dead": len(moddead), "mod_dead_unexplained": len(moddead_unexpl), "findings": len(findings), "findings_unexplained": len(unexplained_findings),
               "regressions": len(regress), "new_keys": len(new), "semantics": sem, "wall_seconds": a.wall}
    json.dump(summary, open(os.path.join(a.out, "summary.json"), "w"), indent=1)

    print("param-audit (%s, engine %s): keys %d / supported %d / alive %d / exempt %d / dead %d (unexplained %d) / modulation alive %d, dead %d (unexplained %d)" % (
        a.mode, a.engine, total_keys, len(sup), len(alive), len(exempt), len(dead), len(dead_unexpl), len(modalive), len(moddead), len(moddead_unexpl)))
    print("  findings %d (unexplained %d)  regressions %d  new keys %d  %s" % (len(findings), len(unexplained_findings), len(regress), len(new), base_note))
    if sem["passed"] is not None:
        print("  semantic checks: %d passed, %d failed" % (sem["passed"], sem["failed"]))
    for x in regress[:20]:
        print("  REGRESSION " + x)
    for r, _ in dead_unexpl[:20]:
        print("  DEAD " + r["key"])
    for r, _ in moddead_unexpl[:10]:
        print("  MOD-DEAD " + r["key"])
    bad = regress or dead_unexpl or moddead_unexpl or unexplained_findings or (sem["failed"] or 0) > 0
    return 1 if bad and not a.update_baseline else 0


if __name__ == "__main__":
    sys.exit(main())
