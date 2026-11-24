#!/usr/bin/env python3
"""Combine XL and Key 61 evidence. Each observed result retains its device, run, date and skin."""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

from disabled import candidates
from inventory import VIEW_IDS, inventory


def read(path):
    return json.loads(path.read_text())


def lines(path):
    with path.open() as file:
        for line in file:
            if line.strip():
                yield json.loads(line)


def nearest(path, root, name):
    directory = path.parent
    while directory.is_relative_to(root):
        match = directory / name
        if match.is_file():
            return match
        if directory == root:
            break
        directory = directory.parent


def utc(seconds):
    return datetime.fromtimestamp(seconds, timezone.utc).isoformat()


def evidence_info(path, root, device):
    meta_path = nearest(path, root, "run.json")
    meta = read(meta_path) if meta_path else {}
    image_inventory = path.with_name("inventory.json")
    inv = read(image_inventory) if image_inventory.exists() else {}
    events = path.with_name("events.jsonl")
    begin = next((e for e in lines(events) if e.get("event") == "begin"), {}) if events.exists() else {}
    summary = path.with_name("scalar-summary.json" if path.name == "scalars.jsonl" else "disabled-summary.json")
    summary = read(summary) if summary.is_file() else {}
    tui = nearest(path, root, "installed-TUI.json")
    phase_hashes = {v for v in (inv.get("tui_sha256"), begin.get("tui_sha256"), summary.get("tui_sha256")) if v}
    if len(phase_hashes) > 1:
        raise ValueError(f"conflicting phase TUI hashes in {path}")
    file_hash = hashlib.sha256(tui.read_bytes()).hexdigest() if tui else None
    digest = next(iter(phase_hashes), None) or meta.get("tui_sha256") or file_hash
    # A nested retry can use a newer installation than its enclosing evidence directory.
    if meta.get("tui_sha256") and meta["tui_sha256"] != digest:
        meta = {}
        meta_path = None
    if file_hash != digest:
        tui = None
    if meta.get("device", device) != device or inv.get("device", device) != device:
        raise ValueError(f"wrong device in {path}")
    start = begin.get("local_unix_s")
    if not start and path.name.endswith(".jsonl"):
        first = next(lines(path), {})
        start = first.get("local_unix_s")
    if not start:
        phase = meta.get("phases", {}).get(path.parent.name, {})
        start = phase.get("start_unix_s")
    date_source = "acquisition" if start else "evidence file mtime"
    start = start or path.stat().st_mtime
    views = inv.get("assigned_views") or meta.get("assigned_views")
    if not views:
        views = [v for v in VIEW_IDS.values() if (v == "oscnoise") == (device == "key61")]
    return dict(device=device, source=str(path), date=utc(start), date_source=date_source, time=start,
                session=str(meta_path.parent if meta_path else (path.parent.parent if path.parent.name in ("quick","full","disabled","scalars") else path.parent)),
                tui_sha256=digest, version=meta.get("revision") or meta.get("version"),
                binary_sha256=meta.get("binary_sha256"), project=meta.get("project"),
                project_before=meta.get("before_project_sha256"), project_after=meta.get("after_project_sha256"),
                views=views, inventory_path=str(image_inventory) if inv else None, tui_path=str(tui) if tui else None)


def verdict(row, kind):
    status = row.get("status", "NOT_RUN")
    if status != "PASS":
        return status, []
    if kind == "quick":
        return "VISITED", []
    if kind == "scalar":
        key = row.get("expected_engine_key")
        ok = (key and row.get("gesture") and set(row.get("engine_changes", {})) == {key}
              and row.get("bound_before") != row.get("bound_after"))
        return ("VERIFIED" if ok else "UNPROVEN"), []
    if kind == "disabled":
        obs = row.get("observations", [])
        ok = (bool(row.get("value_readouts")) and [o.get("action") for o in obs] == ["tap", "drag"]
              and all(o.get("engine_changes") == {} and o.get("vst_changes") == {} for o in obs))
        return ("VERIFIED" if ok else "UNPROVEN"), []
    # A read-only page, a stale baseline, or an action followed only by capture cannot verify interaction.
    pending, verified, acted, witnessed = None, set(), False, False
    for step in row.get("steps", []):
        if step.get("status") != "PASS":
            return "UNPROVEN", []
        spec = step.get("spec", {})
        op = spec.get("op")
        if op in ("tap", "drag", "qlink", "wheel", "save_reload", "navigate"):
            acted = not step.get("already_restored")
            pending = step.get("control") if op in ("tap", "drag") else None
        elif op == "expect" and acted and any(k in spec for k in ("target", "vst", "status", "ocr", "overlay_clear", "overlay_visible")):
            witnessed = True
            if pending:
                verified.add(pending)
            pending = None
    if any(s.get("status") != "PASS" for s in row.get("restore", [])):
        return "UNPROVEN", []
    return ("VERIFIED", sorted(verified)) if witnessed else ("UNPROVEN", [])


def get_rows(path, catalog):
    if path.name == "results.json":
        data = read(path)
        if not isinstance(data, list):
            return
        for row in data:
            if not isinstance(row, dict) or "id" not in row:
                continue
            name = row["id"]
            kind = "quick" if name.startswith("quick.") else "semantic"
            view = row.get("view") or catalog.get(name) or name.split(".")[-1]
            yield kind, name, view, row
    else:
        kind = "scalar" if path.name == "scalars.jsonl" else "disabled"
        for row in lines(path):
            yield kind, row.get("id", "@view"), row["view"], row


def build(roots, catalog):
    configs, attempts = [], []
    for device, root in roots.items():
        root = root.resolve()
        if not root.is_dir():
            raise ValueError(f"missing {device} evidence directory: {root}")
        paths = sorted([p for name in ("results.json", "scalars.jsonl", "disabled.jsonl") for p in root.rglob(name)])
        for path in paths:
            meta = evidence_info(path, root, device)
            run_id = len(configs)
            configs.append(meta)
            for kind, name, view, row in get_rows(path, catalog):
                state, hits = verdict(row, kind)
                # Preserve instance identities. Across skins only case IDs and confirmed engine keys are comparable.
                key = row.get("expected_engine_key") if kind == "scalar" else name if kind == "semantic" else None
                attempts.append(dict(run=run_id, view=view, kind=kind, id=name, comparable_key=key,
                                     result=state, reported=row.get("status"), controls=hits,
                                     error=row.get("error") or row.get("reason") or row.get("recovery_error")))
    latest = {}
    for index, meta in enumerate(configs):
        d = meta["device"]
        if d not in latest or meta["time"] >= configs[latest[d]]["time"]:
            latest[d] = index

    def active(index):
        run = configs[index]
        last = latest[run["device"]]
        newest = configs[last]
        if index == last:
            return True
        if not run["tui_sha256"] or run["tui_sha256"] != newest["tui_sha256"]:
            return False
        if (run["binary_sha256"], run["version"]) != (newest["binary_sha256"], newest["version"]):
            return False
        # Same skin can be shipped with a different binary. Only combine separate run folders
        # without binary identity when their evidence belongs to the same acquisition root.
        return bool(run["binary_sha256"] or run["version"] or run["session"] == newest["session"])

    current = {}
    for attempt in sorted(attempts, key=lambda a: configs[a["run"]]["time"]):
        if active(attempt["run"]):
            device = configs[attempt["run"]]["device"]
            current[device, attempt["view"], attempt["kind"], attempt["id"]] = attempt
    controls, disabled, scalars, indexed = defaultdict(set), defaultdict(set), defaultdict(set), set()
    known = set()
    for i, meta in enumerate(configs):
        if not active(i):
            continue
        device = meta["device"]
        path = meta["inventory_path"] or meta["tui_path"]
        if not path:
            continue
        key = (device, path)
        if key in indexed:
            continue
        indexed.add(key)
        if meta["inventory_path"]:
            widgets = read(Path(path))["controls"]
        else:
            _, widgets = inventory(Path(path))
        for view in meta["views"]:
            known.add((device,view))
        for w in widgets:
            if w["view"] in meta["views"]:
                controls[device, w["view"]].add(w["id"])
                if w["type"] in ("Knob", "Slider"):
                    scalars[device, w["view"]].add(w["id"])
        if meta["tui_path"]:
            for r in candidates(Path(meta["tui_path"])):
                if r["values"] and r["view"] in meta["views"]:
                    disabled[device,r["view"]].add(r["id"])
    table = []
    for view in VIEW_IDS.values():
        assigned = [d for d in roots if any(active(i) and c["device"] == d and view in c["views"] for i,c in enumerate(configs))]
        if not assigned:
            assigned = ["key61" if view == "oscnoise" else "xl"]
        for device in assigned:
            rows = [a for (d,v,k,n),a in current.items() if d == device and v == view]
            ids = {a["id"] for a in rows if a["kind"] == "semantic" and a["result"] == "VERIFIED"}
            by_kind = {kind:dict(Counter(a["result"] for a in rows if a["kind"] == kind))
                       for kind in ("semantic","scalar","disabled","quick")}
            hits = {hit for a in rows if a["kind"] == "semantic" and a["result"] == "VERIFIED" for hit in a["controls"]}
            hits.update(a["id"] for a in rows if a["kind"] == "scalar" and a["result"] == "VERIFIED")
            total = controls[device,view]
            table.append(dict(view=view, device=device, counts=by_kind, inventory_known=(device,view) in known,
                              packaged_controls=len(total), witnessed_controls=sorted(hits & total),
                              unverified_controls=sorted(total - hits), missing_cases=sorted(
                                  c for c,v in catalog.items() if v == view and c not in ids),
                              missing_phases=[k for k,c in by_kind.items() if not c],
                              unverified_scalars=sorted(scalars[device,view] -
                                  {a["id"] for a in rows if a["kind"]=="scalar" and a["result"]=="VERIFIED"}),
                              unverified_disabled=sorted(disabled[device,view] -
                                  {a["id"] for a in rows if a["kind"]=="disabled" and a["result"]=="VERIFIED"}),
                              issues=[a for a in rows if a["result"] not in ("VERIFIED","VISITED")]))
    repeated = defaultdict(list)
    for a in sorted(attempts, key=lambda entry: configs[entry["run"]]["time"]):
        source = configs[a["run"]]
        repeated[source["device"], a["view"], a["kind"], a["id"]].append(
            dict(run=a["run"], result=a["result"], date=source["date"], tui_sha256=source["tui_sha256"]))
    retries = [dict(device=d,view=v,kind=k,id=n,attempts=history) for (d,v,k,n),history in repeated.items()
               if len({a["run"] for a in history}) > 1]
    return dict(sources=configs, attempts=attempts, retries=retries, views=table,
                comparison="Case IDs and confirmed engine keys are comparable across skins. Native instance paths are local to each device and TUI.")


def markdown(report):
    text = ["# MPC funkcionális lefedettség", "",
            "| Nézet | Gép | Megnyitás | Igazolt eset | Célparaméteres próba | Tiltás igazolva | Igazolt natív példány |",
            "|---|---|---|---:|---:|---:|---:|"]
    for row in report["views"]:
        c = row["counts"]
        count = f"{len(row['witnessed_controls'])}/{row['packaged_controls']}" if row["inventory_known"] else "?"
        text.append(f"| {row['view']} | {row['device']} | {', '.join(c['quick']) or 'NOT_RUN'} "
                    f"| {c['semantic'].get('VERIFIED',0)} | {c['scalar'].get('VERIFIED',0)} "
                    f"| {c['disabled'].get('VERIFIED',0)} | {count} |")
    text += ["", "A megnyitás külön mérés. Az igazolt natív példányokhoz konkrét kezelőn végzett művelet "
             "és visszaolvasott eredmény tartozik. Az eltérő skinek között az esetazonosítók és az "
             "igazolt engine-kulcsok hasonlíthatók össze.", "", "## Futások", ""]
    for i, source in enumerate(report["sources"]):
        text.append(f"- [{i}] {source['device']} · {source['date']} ({source['date_source']}) · "
                    f"TUI `{source['tui_sha256'] or '?'}` · verzió `{source['version'] or '?'}` · "
                    f"bináris `{source['binary_sha256'] or '?'}` · `{source['source']}`")
    if report["retries"]:
        text += ["", "## Újrapróbák", ""]
        for r in report["retries"]:
            text.append(f"- {r['device']}, {r['view']}, {r['kind']} `{r['id']}`: " +
                        " → ".join(f"[{a['run']}] {a['date']} {a['result']}" for a in r["attempts"]))
    text += ["", "## Nyitott ellenőrzések", ""]
    for row in report["views"]:
        text += [f"### {row['view']} · {row['device']}", ""]
        if not row["inventory_known"]:
            text += ["A csomagolt vezérlők leltára hiányzik.", ""]
        for key in ("missing_phases", "unverified_controls", "unverified_scalars", "unverified_disabled", "missing_cases"):
            values = row[key]
            if values:
                text.extend([f"<details><summary>{key} ({len(values)})</summary>", "",
                             *[f"- `{v}`" for v in values], "", "</details>", ""])
        if row["issues"]:
            text.extend([f"<details><summary>Esetenkénti eredmények ({len(row['issues'])})</summary>", "",
                         *[f"- [{a['run']}] {a['kind']} `{a['id']}` · {a['result']} · {a['error'] or ''}" for a in row["issues"]],
                         "", "</details>", ""])
    return "\n".join(text)+"\n"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--xl", type=Path, required=True)
    p.add_argument("--key61", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True, help="output directory for coverage-report.{md,json}")
    p.add_argument("--catalog", action="append", type=Path, help="override the bundled case catalogs")
    args = p.parse_args()
    bundled = [Path(__file__).with_name(n) for n in ("cases.json","cases_noise.json")]
    cases = {c["id"]:c["view"] for path in args.catalog or [p for p in bundled if p.exists()] for c in read(path)}
    report = build({"xl":args.xl, "key61":args.key61}, cases)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "coverage-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False)+"\n")
    (args.out / "coverage-report.md").write_text(markdown(report))
    print(f"{len(report['sources'])} evidence files, {len(report['attempts'])} observed results")


if __name__ == "__main__":
    main()
