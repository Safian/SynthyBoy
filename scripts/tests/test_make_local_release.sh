#!/usr/bin/env bash
# SynthyBoy - test of scripts/make-local-release.sh: options, and a complete run on fake binaries (no compiler, no Docker needed).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
script="$repo/scripts/make-local-release.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

bash -n "$script" || fail "syntax"
bash -n "$repo/scripts/build-armv7-clap.sh" || fail "syntax of build-armv7-clap.sh"

"$script" --help | grep -q "plugin.json.release" || fail "--help"
set +e
"$script" --bogus >/dev/null 2>&1; [[ $? == 2 ]] || fail "unknown option must exit 2"
"$script" --jobs x >/dev/null 2>&1; [[ $? == 2 ]] || fail "--jobs x must exit 2"
"$script" --no-armv7 --no-host --out "$tmp/none" >/dev/null 2>&1; [[ $? != 0 ]] || fail "nothing built must fail"
set -e

# a complete run with prebuilt (fake, header only) binaries: ARMv7 hard-float ELF and the binary of the host platform
python3 -I - "$tmp" <<'PY'
import struct, sys
tmp = sys.argv[1]
ident = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
open(tmp + "/armv7.clap", "wb").write(ident + struct.pack("<HHIIIII", 3, 40, 1, 0, 0, 0, 0x05000400) + bytes(64))
ident64 = b"\x7fELF" + bytes([2, 1, 1]) + bytes(9)
machine = {"x86_64": 62, "aarch64": 183, "arm64": 183}
import platform
if platform.system() == "Darwin":
    data = struct.pack(">II", 0xCAFEBABE, 2)
    for t in (0x01000007, 0x0100000C):
        data += struct.pack(">IIIII", t, 0, 4096, 100, 14)
    open(tmp + "/host.clap", "wb").write(data + bytes(4096))
else:
    open(tmp + "/host.clap", "wb").write(ident64 + struct.pack("<HHIQQQI", 3, machine.get(platform.machine(), 62), 1, 0, 0, 0, 0) + bytes(64))
PY
case "$(uname -s)" in Darwin|Linux) ;; *) echo "skip the full run on $(uname -s)"; exit 0 ;; esac
"$script" --out "$tmp/dist" --armv7-clap "$tmp/armv7.clap" --host-clap "$tmp/host.clap" >"$tmp/log" 2>&1 || { cat "$tmp/log"; fail "full run"; }

python3 -I - "$tmp/dist" "$repo" <<'PY'
import json, os, sys
dist, repo = sys.argv[1:3]
m = json.load(open(dist + "/plugin.json.release"))
assert "linux-armv7" in m["builds"] and len(m["builds"]) == 2, m["builds"].keys()
for b in m["builds"].values():
    assert b["status"] == "released" and len(b["sha256"]) == 64 and b["size"] > 0 and b["entry_point"] == "synthyboy.clap"
    assert not b["url"].startswith(("/", "file:", "http")), b["url"]   # relative to the manifest
    assert os.path.isfile(os.path.join(dist, b["url"]))
assert [a["id"] for a in m["assets"]] == ["com.safian.synthyboy.factory-presets"]
assert os.path.isfile(dist + "/SHA256SUMS")
PY
"$script" --out "$tmp/dist2" --no-host --armv7-clap "$tmp/armv7.clap" --file-urls >/dev/null 2>&1 || fail "file urls run"
grep -q '"url": "file:///' "$tmp/dist2/plugin.json.release" || fail "file:// urls expected"
python3 -I "$repo/scripts/check-manifest.py" --local "$tmp/dist2/plugin.json.release" >/dev/null || fail "check-manifest --local"
echo "OK"
