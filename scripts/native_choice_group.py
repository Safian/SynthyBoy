# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compatibility adapter for native choice group variants.

Moved to components/native/radio_button/radio_button.py.
"""
import sys
from pathlib import Path

_ROOT = str(Path(__file__).resolve().parent.parent)
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from components.native.radio_button.radio_button import *
from components.native.radio_button.radio_button import (
    VARIANT_SEGMENTED,
    render_segmented_button_art,
    build_radio_button_entries as build_choice_group_entries,
    create_segmented_radio_button_group as create_segmented_choice_group,
)
