# SynthyBoy - the native parameter offsets the XL UI tests use, read from shell-mpc/native_ui_params.h (the single source).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The header is compiled once (c++ -std=c++20) into a probe that prints the constants; there is no second copy of the numbers here.
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOST_EXTRAS = 82   # the host parameters between docs/params.json and the first native handle (skin/TUI.json); not in the header

_NAMES = {
    "FILTER_STATE_OFFSET": "kFilterBase", "FX_CHAIN": "kFxChain", "MOD_DETAIL_RESERVED": "kModDetailReserved",
    "MOD_NAVIGATION": "kModNavigation", "ENV_SELECTION": "kEnvFields + env::Selection", "LFO_OFFSET": "kLfoBase", "MOD_ROWS": "kModRows",
}


def _load():
    lines = ['#include "native_ui_params.h"', "#include <cstdio>", "using namespace synthyboy_mpc::nativeui;", "int main() {"]
    lines += ['std::printf("%s %%d\\n", (int)(%s));' % (k, v) for k, v in _NAMES.items()] + ["}"]
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / "probe.cpp", Path(tmp) / "probe"
        src.write_text("\n".join(lines), encoding="utf-8")
        subprocess.run(["c++", "-std=c++20", "-w", "-I", str(ROOT / "shell-mpc"), str(src), "-o", str(exe)], check=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    return {k: int(v) for k, v in (line.split() for line in out.splitlines())}


globals().update(_load())


def native_base_index(param_count):
    return param_count + HOST_EXTRAS
