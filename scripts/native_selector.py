# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compatibility adapter for native list selector component.

Moved to components/native/selector/selector.py.
"""
import sys
from pathlib import Path

_ROOT = str(Path(__file__).resolve().parent.parent)
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from components.native.selector.selector import *
from components.native.selector.selector import (
    STANDALONE_OSC_SIZE,
    COMPACT_SELECTOR_HEIGHT,
    MODAL_MARKER_COLS,
    save_png_filter0,
    render_pipa_icon,
    render_wave_icon,
    build_step_buttons,
    selector_definition,
    build_shape_selector,
    build_native_list_overlay,
    bind_list_choices,
)


def ensure_routing_replacement_strings(tui, dest_param_indices, choices=()):
    """Keep the generic selector adapter's original empty default."""
    from components.native.selector.selector import ensure_routing_replacement_strings as apply_choices
    return apply_choices(tui, dest_param_indices, choices)
