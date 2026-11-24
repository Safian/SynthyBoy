#!/usr/bin/env python3
# Acceptance check of one docs/ui-map/<page>.md file (written by the local model): the fixed sections are there,
# every parameter key it cites exists in docs/params.json, every source file it cites exists, and it lists controls.
import json, re, sys, pathlib
root = pathlib.Path(__file__).resolve().parents[2]
page = sys.argv[1]
refreshed = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--refreshed" else None
f = root / "docs" / "ui-map" / (page + ".md")
if not f.exists():
    sys.exit("missing " + str(f))
t = f.read_text()
if refreshed and ("Refreshed " + refreshed) not in t.splitlines()[1:2][0] if len(t.splitlines()) > 1 else refreshed:
    sys.exit("second line must say: Refreshed " + refreshed)
need = ["## Purpose", "## Layout", "## Controls", "## Opens", "## Q-Link groups", "## Live parts", "## Duplicates", "## Questionable"]
miss = [s for s in need if s not in t]
if miss:
    sys.exit("missing sections: " + ", ".join(miss))
keys = {p["key"] for p in json.load(open(root / "docs" / "params.json"))["params"]}
cited = set(re.findall(r"`([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+)`", t))
cited = {c for c in cited if not c.endswith((".h", ".cpp", ".md", ".json", ".py"))}
bad = sorted(c for c in cited if c not in keys)
if bad:
    sys.exit("unknown parameter keys: " + ", ".join(bad[:20]))
files = set(re.findall(r"`((?:shell-mpc|core|scripts|docs)/[^`:\s]+)", t))
nofile = sorted(x for x in files if not (root / x).exists())
if nofile:
    sys.exit("missing files: " + ", ".join(nofile[:10]))
rows = [l for l in t.split("## Controls", 1)[1].split("\n## ", 1)[0].splitlines() if l.startswith("|") and not set(l) <= set("|- :")]
if len(rows) < 4:
    sys.exit("controls table too short: %d rows" % len(rows))
print("ok", page, len(rows) - 1, "controls,", len(cited), "keys")
