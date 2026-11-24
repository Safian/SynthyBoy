# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compatibility adapter for 48 px compact route selector.

Moved to components/native/selector/selector.py.
"""
import sys
from pathlib import Path

_ROOT = str(Path(__file__).resolve().parent.parent)
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from components.native.selector.selector import *
from components.native.selector.selector import (
    COMPACT_HEIGHT,
    STANDALONE_RECT,
    BUTTON_COMPACT_RECT,
    COMPACT_OSC_ROUTING_HEIGHT,
    STANDALONE_COMPACT_OSC_ROUTING_RECT,
    STANDALONE_OSC_ROUTING_RECT,
    OSC_ROUTING_CHOICES,
    OSC_ROUTING_SHORT_NAMES,
    DESTINATION_FILTER_SEGMENTS,
    OSC_ROUTING_COMPONENT_TYPE,
    OSC_COMPACT_ROUTING_COMPONENT_TYPE,
    OSC_ROUTING_CONTROL_NAME,
    OSC_ROUTING_OVERLAY_NAME,
    OSC_COMPACT_ROUTING_FRAME_IMAGE,
    OSC_ROUTING_FRAME_IMAGE,
    OSC_ROUTING_OVERLAY_BG_IMAGE,
    BG_DARK,
    BG_CELL,
    BORDER_CELL,
    BORDER_CELL_FOCUS,
    CYAN_F1,
    GREEN_F2,
    SEG_INACTIVE,
    TEXT_VALUE,
    TEXT_LABEL,
    CARET_COL,
    render_compact_route_face,
    render_compact_button_face,
    render_choice_overlay_art,
)
