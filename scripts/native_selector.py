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


import importlib.util as _importlib_util

_sel95_path = Path(__file__).resolve().parent / "mpc-skin-extras" / "95_selectors.py"
if _sel95_path.is_file():
    _sel95_spec = _importlib_util.spec_from_file_location("mpc_skin_extra_95_selectors", _sel95_path)
    _sel95_mod = _importlib_util.module_from_spec(_sel95_spec)
    _sel95_spec.loader.exec_module(_sel95_mod)
    LFO_INSTANCE_MENU = _sel95_mod.LFO_INSTANCE_MENU
    ENV_INSTANCE_MENU = _sel95_mod.ENV_INSTANCE_MENU
    FILTER_VIEW_MENU = _sel95_mod.FILTER_VIEW_MENU
    FILTER_SCALAR_MENU = _sel95_mod.FILTER_SCALAR_MENU
    LFO_INSTANCE_CHOICES = _sel95_mod.LFO_INSTANCE_CHOICES
    ENV_INSTANCE_CHOICES = _sel95_mod.ENV_INSTANCE_CHOICES
    FILTER_VIEW_CHOICES = _sel95_mod.FILTER_VIEW_CHOICES
    FILTER_SCALAR_CHOICES = _sel95_mod.FILTER_SCALAR_CHOICES
    build_lfo_instance_selector = _sel95_mod.build_lfo_instance_selector
    build_env_instance_selector = _sel95_mod.build_env_instance_selector
    build_filter_view_selector = _sel95_mod.build_filter_view_selector
    build_filter_scalar_view_selector = _sel95_mod.build_filter_scalar_view_selector

