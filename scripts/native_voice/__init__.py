# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone native Voice tab definitions and screen builders."""
from .voice_tab import (
    KEEP,
    UNCONVERTED_CONTROLS,
    VOICE_NATIVE_CONTROLS,
    VOICE_NATIVE_CONTROL_NAMES,
    VOICE_TAB_NAME,
    populate_voice_tab,
    voice_definitions,
)

__all__ = [
    "KEEP",
    "VOICE_TAB_NAME",
    "VOICE_NATIVE_CONTROLS",
    "VOICE_NATIVE_CONTROL_NAMES",
    "UNCONVERTED_CONTROLS",
    "populate_voice_tab",
    "voice_definitions",
]
