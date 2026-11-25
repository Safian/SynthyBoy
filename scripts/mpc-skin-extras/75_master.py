# SynthyBoy - MPC skin extra: standalone Master tab migration to host-native MPC controls.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Register the shared native controls and meters of both Master panels."""

import sys
from pathlib import Path

# Ensure scripts root is in path for native_master imports
_HERE = Path(__file__).resolve().parent
_SCRIPTS = _HERE.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from native_master.master_controls import apply_master_native_controls, write_peak_scale
from native_meter import write_meter_assets


def apply(tui, qlinks, ctx):
    """Auto-loaded entry point invoked by scripts/apply-mpc-skin-extras.py."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    if apply_master_native_controls(tui, qlinks, ctx):
        write_peak_scale(ctx.skins_dir)
        write_meter_assets(ctx.skins_dir)
