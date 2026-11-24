# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone Master tab host-native MPC controls migration module."""

from .master_controls import (
    MASTER_SPECS,
    UNCONVERTED_CONTROLS,
    apply_master_native_controls,
    get_master_control_specs,
    get_unconverted_controls,
)

__all__ = [
    "MASTER_SPECS",
    "UNCONVERTED_CONTROLS",
    "apply_master_native_controls",
    "get_master_control_specs",
    "get_unconverted_controls",
]
