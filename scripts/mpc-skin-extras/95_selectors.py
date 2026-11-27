# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native dropdown definitions for LFO, Env, and Filter selectors.

Replaces legacy C++ UsedSelector, Stepper, and Carousel controls with shared native
dropdowns (Selector component: render_dropdown_face, centered value, chevron, no arrows,
and host MenuOverlay choice drawer).
"""

import copy
from pathlib import Path
import sys

_ROOT = str(Path(__file__).resolve().parents[2])
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from components.native.selector.selector import (
    dropdown_definition,
    labelled_choice_drawer,
    render_dropdown_face,
    write_routing_assets,
)

LFO_INSTANCE_MENU = "lfoNativeInstanceChoiceMenu"
ENV_INSTANCE_MENU = "envNativeInstanceChoiceMenu"
FILTER_VIEW_MENU = "filterNativeViewChoiceMenu"
FILTER_SCALAR_MENU = "filterNativeScalarChoiceMenu"

LFO_INSTANCE_CHOICES = tuple([f"LFO {i}" for i in range(1, 11)] + [f"RND {i}" for i in range(1, 5)] + ["+"])
ENV_INSTANCE_CHOICES = tuple([f"ENV {i}" for i in range(1, 7)] + ["+"])
FILTER_VIEW_CHOICES = ("F1", "F2", "NOTE", "VELO")
FILTER_SCALAR_CHOICES = ("NOTE", "VELO")


def dropdown_actions(popup_name, handle_name="Data"):
    """Standard host overlay open actions on tap/enter."""
    return [
        {"version": 1, "onAction": "Mouse Down", "handler": "Show Overlay",
         "handleName": handle_name, "additionalData": popup_name},
        {"version": 1, "onAction": "Enter Pressed", "handler": "Show Overlay",
         "handleName": handle_name, "additionalData": popup_name},
    ]


def build_lfo_instance_selector(skins_dir, width=202, height=56, popup=LFO_INSTANCE_MENU, handle="Data"):
    """Native dropdown replacing LfoFullScreen.selector (UsedSelector: LFO 1..10 / R1..R4 / +)."""
    return dropdown_definition(
        width, height,
        actions=dropdown_actions(popup, handle),
        skins_dir=skins_dir,
        value_handle=handle,
        value_name=handle,
        frame_name="LFO selector face",
        filename=f"lfo_selector_dropdown_{width}x{height}.png",
        look="neon",
    )


def build_env_instance_selector(skins_dir, width=202, height=56, popup=ENV_INSTANCE_MENU, handle="Data"):
    """Native dropdown replacing EnvFullScreen.selector (UsedSelector: ENV 1..6 / +)."""
    return dropdown_definition(
        width, height,
        actions=dropdown_actions(popup, handle),
        skins_dir=skins_dir,
        value_handle=handle,
        value_name=handle,
        frame_name="Env selector face",
        filename=f"env_selector_dropdown_{width}x{height}.png",
        look="neon",
    )


def build_filter_view_selector(skins_dir, width=202, height=56, popup=FILTER_VIEW_MENU, handle="Data"):
    """Native dropdown replacing FilterFullScreen.view (UsedSelector: F1 | F2 | NOTE | VELO)."""
    return dropdown_definition(
        width, height,
        actions=dropdown_actions(popup, handle),
        skins_dir=skins_dir,
        value_handle=handle,
        value_name=handle,
        frame_name="Filter view selector face",
        filename=f"filter_view_dropdown_{width}x{height}.png",
        look="neon",
    )


def build_filter_scalar_view_selector(skins_dir, width=150, height=56, popup=FILTER_SCALAR_MENU, handle="Data"):
    """Native dropdown replacing FilterFullScreen.scalarView (UsedSelector: NOTE | VELO)."""
    return dropdown_definition(
        width, height,
        actions=dropdown_actions(popup, handle),
        skins_dir=skins_dir,
        value_handle=handle,
        value_name=handle,
        frame_name="Filter scalar view selector face",
        filename=f"filter_scalar_dropdown_{width}x{height}.png",
        look="neon",
    )


def apply(tui, qlinks, ctx):
    """Register native dropdown component and overlay definitions for LFO, Env, and Filter."""
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    existing_keys = {d.get("key") for d in defs}

    skins_dir = Path(ctx.skins_dir)
    write_routing_assets(skins_dir)

    # 1. Choice overlay drawers (host MenuOverlay list behavior)
    menus = [
        (LFO_INSTANCE_MENU, labelled_choice_drawer("LFO INSTANCE", LFO_INSTANCE_CHOICES, columns=2)),
        (ENV_INSTANCE_MENU, labelled_choice_drawer("ENVELOPE", ENV_INSTANCE_CHOICES, columns=1)),
        (FILTER_VIEW_MENU, labelled_choice_drawer("FILTER VIEW", FILTER_VIEW_CHOICES, columns=1)),
        (FILTER_SCALAR_MENU, labelled_choice_drawer("SCALAR VIEW", FILTER_SCALAR_CHOICES, columns=1)),
    ]
    for key, val in menus:
        if key not in existing_keys:
            ctx.add_component_definition(key, val)
            existing_keys.add(key)

    # 2. Native dropdown components (Selector face, centered text, chevron, no arrows)
    dropdowns = [
        ("lfoNativeInstanceDropdown", build_lfo_instance_selector(skins_dir, 202, 56)),
        ("envNativeSelectorDropdown", build_env_instance_selector(skins_dir, 202, 56)),
        ("filterNativeViewDropdown", build_filter_view_selector(skins_dir, 202, 56)),
        ("filterNativeScalarViewDropdown", build_filter_scalar_view_selector(skins_dir, 150, 56)),
    ]
    for key, val in dropdowns:
        if key not in existing_keys:
            ctx.add_component_definition(key, val)
            existing_keys.add(key)
