#!/usr/bin/env python3
"""Acceptance test for scripts/check-xpl-set.py (run from the repository root)."""
import os, shutil, subprocess, sys, tempfile, glob
SRC = os.environ.get("XPL_SET", "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/xpl-presets/xl")
TOOL = [sys.executable, "scripts/check-xpl-set.py"]
def run(*args):
    p = subprocess.run(TOOL + list(args), capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr
def fail(msg, out=""):
    print("FAIL:", msg); print(out[-800:]); sys.exit(1)
files = sorted(glob.glob(os.path.join(SRC, "*.xpl")))
if not files:
    print("SKIP: no reference set under %s (set XPL_SET to a folder of generated .xpl files)" % SRC); sys.exit(0)
N = len(files)   # the reference set is whatever the generator wrote: its size is the count, the programs are Init and the set
tmp = tempfile.mkdtemp(prefix="xplacc")
try:
    good = os.path.join(tmp, "good"); shutil.copytree(SRC, good)
    rc, out = run(good, "--expect-count", str(N), "--expect-programs", str(N + 1), "--plugin-path", "/nvme/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm64.so")
    if rc != 0: fail("a good set must pass (exit 0)", out)
    if "%d files OK" % N not in out: fail("the summary line '%d files OK' is missing" % N, out)
    rc, out = run(good, "--expect-count", str(N - 1))
    if rc != 1 or "count" not in out.lower(): fail("wrong count must exit 1 and say so", out)
    rc, out = run(good, "--plugin-path", "/sdcard/x.so")
    if rc != 1 or "file=" not in out: fail("wrong plugin path must exit 1 and name the file= attribute", out)
    rc, out = run(good, "--expect-programs", str(N + 2))
    if rc != 1 or "numPrograms" not in out: fail("wrong program count must exit 1 and name numPrograms", out)
    # a file with LF line ends
    bad = os.path.join(tmp, "lf"); shutil.copytree(SRC, bad)
    f = sorted(glob.glob(os.path.join(bad, "*.xpl")))[3]
    data = open(f, "rb").read().replace(b"\r\n", b"\n"); open(f, "wb").write(data)
    rc, out = run(bad)
    if rc != 1 or "CRLF" not in out or os.path.basename(f) not in out: fail("LF line ends must exit 1, say CRLF and name the file", out)
    # a truncated state
    bad2 = os.path.join(tmp, "trunc"); shutil.copytree(SRC, bad2)
    f = sorted(glob.glob(os.path.join(bad2, "*.xpl")))[5]
    data = open(f, "rb").read(); i = data.index(b"</state>"); open(f, "wb").write(data[:i-40] + data[i:])
    rc, out = run(bad2)
    if rc != 1 or os.path.basename(f) not in out: fail("a damaged state must exit 1 and name the file", out)
    # preset name not matching the file name
    bad3 = os.path.join(tmp, "name"); shutil.copytree(SRC, bad3)
    f = sorted(glob.glob(os.path.join(bad3, "*.xpl")))[7]
    os.rename(f, os.path.join(bad3, "007-Bass-Wrong Name.xpl"))
    rc, out = run(bad3)
    if rc != 1 or "Wrong Name" not in out: fail("a file name that does not end with the <preset> name must exit 1 and name it", out)
    # duplicate number
    bad4 = os.path.join(tmp, "dup"); shutil.copytree(SRC, bad4)
    fs = sorted(glob.glob(os.path.join(bad4, "*.xpl")))
    b = os.path.basename(fs[9]); os.rename(fs[9], os.path.join(bad4, "008" + b[3:]))
    rc, out = run(bad4)
    if rc != 1 or "008" not in out: fail("a duplicate number must exit 1 and name it", out)
    rc, out = run(os.path.join(tmp, "missing"))
    if rc != 2: fail("a missing directory must exit 2", out)
    print("ACCEPT OK")
finally:
    shutil.rmtree(tmp, ignore_errors=True)
