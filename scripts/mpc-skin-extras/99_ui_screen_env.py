# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Production autoload hook for the standalone Env screen.

Fallback delegator: uses importlib.util to discover and call the
96_env_native.py installer from the same directory. Re-exports
CELLS and selector constants for existing consumers (e.g. 98_play_expanded.py).
"""

from pathlib import Path
import importlib.util

# Discover the sibling 96_env_native.py installer
_SCRIPTS = Path(__file__).resolve().parent
_installer_path = _SCRIPTS / "96_env_native.py"
_spec = importlib.util.spec_from_file_location("env_native_installer", _installer_path)
_installer = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_installer)

# Re-export CELLS and selector constants for existing consumers
CELLS = _installer.CELLS
SELECTOR_X = _installer.SELECTOR_X
SELECTOR_Y = _installer.SELECTOR_Y
SELECTOR_W = _installer.SELECTOR_W
SELECTOR_H = _installer.SELECTOR_H
SELECTOR_VARIANTS = _installer.SELECTOR_VARIANTS
SCREEN_NAME = "Env"


def apply(tui, qlinks, ctx):
    """Delegate to the 96_env_native.py installer."""
    _installer.apply(tui, qlinks, ctx)
