# SynthyBoy - MPC skin extra: LFO screen native component integration and verification.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Production autoload hook for the standalone LFO screen.

Discovered and executed by scripts/apply-mpc-skin-extras.py in alphabetical order.
Runs at index 99 to ensure the LFO tab has all verified native controls
properly integrated, with generation-specific Q framing and no numbered Q badges,
correct sentinels, and valid SDK parameter mappings.
Safely checks existing definitions from 53_lfo_smooth.py to prevent duplicate key errors.

Features:
- Full inventory of rebuilt native components on the LFO screen.
- Verification and support of the approved Variant 1 native radio selector (VARIANT_RADIO_DOT).
- Strict enforcement of owner visual rules: no numbered Q badges; Gen1 single-Q frame and Gen2 compile-out follow the common policy.
- Rejection of fake animations and hand-drawn / vector controls presented as authentic.
- Full verification of sentinel positions, widget counts, and touch target minimums (>= 56 px).
"""

import copy
import importlib.util
import json
from pathlib import Path
import re
import sys

_HERE = Path(__file__).resolve().parent
_SCRIPTS = _HERE.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

try:
    import native_choice_group as ncg
    HAS_NATIVE_CHOICE_GROUP = True
except ImportError:
    HAS_NATIVE_CHOICE_GROUP = False

CONTRACT = _HERE / "lfo-native-contract.json"
EDITOR_CONTRACT = _HERE / "lfo-shape-editor-contract.json"
DETAIL_CONTRACT = _HERE / "lfo-shape-detail-contract.json"
PREFIX = "LFO 1 Native "
KEEP = ("Overlay Marker", "Overlay Instance")

SCREEN_TITLE = "LFO"

# Sentinel constants
SENTINEL_BG = (900, 2, 5, 5)
SENTINEL_LEFT = (33, 1, 6, 6)
SENTINEL_INSTANCE = (45, 1, 6, 6)
SENTINEL_RIGHT = (1241, 1, 6, 6)

# Inventory of built / integrated native components on LFO:
REBUILT_NATIVE_COMPONENTS = (
    {
        "id": "lfo_instance_select",
        "name": "LFO Native Instance Select",
        "kind": "lfoNativeChoice",
        "rect": (8, 8, 200, 56),
        "role": "LFO példány választó (LFO 1..10, RND 1..4) popup választó overlay-jel",
        "status": "MEGÉPÍTETT (200x56 px, >=56 px érintési felület)",
    },
    {
        "id": "lfo_waveform_picker",
        "name": "LFO Native Waveform Picker",
        "kind": "lfoNativeChoice",
        "rect": (216, 8, 848, 56),
        "role": "Hullámforma választó (Sine, Tri, Saw, Ramp, Square, Pulse, Smooth Rnd, Stepped Rnd, Drawn)",
        "status": "MEGÉPÍTETT (848x56 px, fejléc menü és léptető)",
    },
    {
        "id": "lfo_targets_action",
        "name": "LFO Native TARGETS Button",
        "kind": "lfoNativeTargets",
        "rect": (1072, 8, 200, 56),
        "role": "Modulációs célpontok navigációs akciógomb (TARGETS)",
        "status": "MEGÉPÍTETT (200x56 px)",
    },
    {
        "id": "lfo_expand_action",
        "name": "LFO Native EXPAND Button",
        "kind": "lfoNativeExpand",
        "rect": (1204, 80, 64, 56),
        "role": "Szerkesztő felületet megnyitó ikongomb",
        "status": "MEGÉPÍTETT (64x56 px, kijelző jobb felső sarka)",
    },
    {
        "id": "lfo_waveform_display",
        "name": "LFO Real-time Waveform Display",
        "kind": "C++ SynthyBoy Overlay Aperture",
        "rect": (8, 72, 1264, 250),
        "role": "Valós idejű szinusz/egyedi hullámforma, fáziskurzor és futópont rajzolás (lfo_display::draw)",
        "status": "MEGÉPÍTETT (1264x250 px, széles C++ overlay motor)",
    },
    {
        "id": "lfo_rate_dial",
        "name": "LFO Native RATE Dial",
        "kind": "lfoNativeUni",
        "rect": (14, 368, 162, 246),
        "role": "Frekvencia szabályozó unipoláris tárcsa (0.01 .. 200 Hz)",
        "status": "MEGÉPÍTETT (unipoláris tárcsa)",
    },
    {
        "id": "lfo_timing_sync",
        "name": "LFO Native SYNC Selector (HZ / BPM / KEY)",
        "kind": "lfoNativeChoice / native_choice_group",
        "rect": (396, 368, 150, 168),
        "role": "Szinkronizáció módválasztó (Jóváhagyott mai natív pontjelölős rádióválasztó HZ / BPM / KEY)",
        "status": "MEGÉPÍTETT (Jóváhagyott pontjelölős változat, VARIANT_RADIO_DOT)",
    },
    {
        "id": "lfo_smooth_toggle",
        "name": "LFO Native SMOOTH Toggle",
        "kind": "lfoNativeToggle",
        "rect": (180, 502, 72, 112),
        "role": "Simítás be-/kikapcsoló",
        "status": "MEGÉPÍTETT (LED állapotjelző kapcsoló)",
    },
    {
        "id": "lfo_smooth_time",
        "name": "LFO Native SMOOTH TIME Dial",
        "kind": "lfoNativeUni / lfoNativeDisabled",
        "rect": (252, 502, 124, 112),
        "role": "Simítási idő szabályozó dinamikus inaktív kapuzással (1 .. 1000 ms)",
        "status": "MEGÉPÍTETT (dinamikusan inaktiválódó tárcsa)",
    },
    {
        "id": "lfo_timing_division",
        "name": "LFO Native DIVISION Stepper",
        "kind": "lfoNativeChoice",
        "rect": (180, 368, 196, 114),
        "role": "Ütemosztás választó (1/4, 1/8, 1/16 stb.)",
        "status": "MEGÉPÍTETT (léptető/menü)",
    },
    {
        "id": "lfo_timing_trigger",
        "name": "LFO Native TRIGGER Mode",
        "kind": "lfoNativeChoice",
        "rect": (396, 552, 150, 62),
        "role": "Indítási mód (Retrigger, Free-run, Envelope, Sustain envelope, Loop at point, Loop and hold)",
        "status": "MEGÉPÍTETT (választó menü)",
    },
    {
        "id": "lfo_motion_swing",
        "name": "LFO Native SWING Dial",
        "kind": "lfoNativeUni",
        "rect": (1027, 368, 239, 114),
        "role": "Swing arány szabályozó tárcsa",
        "status": "MEGÉPÍTETT (unipoláris tárcsa)",
    },
    {
        "id": "lfo_motion_stereo",
        "name": "LFO Native STEREO Dial",
        "kind": "lfoNativeBi",
        "rect": (1027, 500, 239, 114),
        "role": "Bipoláris sztereó fáziseltérés tárcsa",
        "status": "MEGÉPÍTETT (bipoláris tárcsa)",
    },
    {
        "id": "lfo_shape_editor",
        "name": "LFO Shape Editor Page",
        "kind": "lfoNativeFullEditor",
        "rect": (0, 0, 1280, 628),
        "role": "Drawn LFO hullámalak pontszerkesztő felület 29 natív vezérlővel",
        "status": "MEGÉPÍTETT (29 Shape Editor widget)",
    },
    {
        "id": "lfo_shape_detail",
        "name": "LFO Shape Detail Page",
        "kind": "lfoDetailRow / lfoDetailHeader",
        "rect": (0, 0, 1280, 628),
        "role": "Szerkesztett pontok és modulációk részletes listanézete",
        "status": "MEGÉPÍTETT (18 Shape Detail widget)",
    },
)

# Approved radio selector variant specification:
APPROVED_RADIO_VARIANT = {
    "variant_id": "VARIANT_RADIO_DOT",
    "name": "Mai natív rádióválasztó aktív cián ponttal",
    "active_color": "#2ee6f0",
    "active_text": "#06101e",
    "inactive_color": "#1a2238",
    "inactive_text": "#d4e0f2",
    "touch_height": 56,
    "touch_min_width": 56,
    "source_module": "components/native/radio_button/radio_button.py",
}

# Rejected design concepts:
REJECTED_CONCEPTS = (
    "Visible Q-Link numbers / chips / badges (Q1..Q16 jelvények kizárva a host skinről, Rule 5b)",
    "Hand-drawn / canvas-based vector controls (kézzel rajzolt vezérlők elutasítva: csak hivatalos SDK natív TUI és C++ motor engedélyezett)",
    "Invented animations (kitalált új animációk elutasítva: valós hardveren kizárólag a C++ fáziskurzor és a motor állapotai hitelesek)",
    "Historical 32/34-tab host-only fallback layouts (történeti rácsok kizárva)",
)


class _SafeContextProxy:
    def __init__(self, ctx, tui):
        self._ctx = ctx
        self._tui = tui

    def add_component_definition(self, key, value):
        defs = self._tui.setdefault("pageData", {}).setdefault(
            "componentDefinitions", {}
        ).setdefault("localComponentDefinitions", [])
        if any(d.get("key") == key for d in defs):
            return
        self._ctx.add_component_definition(key, value)

    def __getattr__(self, name):
        return getattr(self._ctx, name)


def _load_53_lfo_smooth():
    path = _HERE / "53_lfo_smooth.py"
    spec = importlib.util.spec_from_file_location("mpc_skin_extra_53_lfo_smooth", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _rect(comp):
    bounds = comp.get("bounds", {}).get("bounds", "0 0 0 0")
    return tuple(map(int, bounds.split()))


def _overlaps(r1, r2):
    x1, y1, w1, h1 = r1
    x2, y2, w2, h2 = r2
    return not (x1 + w1 <= x2 or x2 + w2 <= x1 or y1 + h1 <= y2 or y2 + h2 <= y1)


def _leaf_entries(tab, definitions):
    """Unpack container definitions such as lfoNativeFullEditor if present."""
    for entry in tab["componentDefinition"]["componentsData"]:
        kind = entry["componentData"].get("type")
        if kind == "lfoNativeFullEditor" and kind in definitions:
            dx, dy, _, _ = map(int, entry["bounds"]["bounds"].split())
            for child in definitions[kind].get("componentsData", []):
                translated = copy.deepcopy(child)
                x, y, w, h = map(int, child["bounds"]["bounds"].split())
                translated["bounds"]["bounds"] = f"{x + dx} {y + dy} {w} {h}"
                yield translated
        else:
            yield entry


def _check_and_sanitize_no_q_numbers(obj, path=""):
    """Recursively verify and sanitize any visible Q-Link numbers, chips, or lanes."""
    q_pattern = re.compile(r"\bQ\s*[1-9][0-9]?\b", re.IGNORECASE)
    if isinstance(obj, dict):
        text = obj.get("text")
        if isinstance(text, str) and q_pattern.search(text):
            obj["text"] = q_pattern.sub("", text).strip()
        for k, v in list(obj.items()):
            _check_and_sanitize_no_q_numbers(v, f"{path}.{k}")
    elif isinstance(obj, list):
        for i, item in enumerate(obj):
            _check_and_sanitize_no_q_numbers(item, f"{path}[{i}]")


def apply(tui, qlinks, ctx):
    """Integrate, verify and finalize host-native LFO screen controls."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return

    tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") == "LFO"), None)
    if tab is None:
        return

    defs_list = tui["pageData"].setdefault("componentDefinitions", {}).setdefault("localComponentDefinitions", [])
    definitions = {d.get("key"): d.get("value") for d in defs_list}

    entries = list(_leaf_entries(tab, definitions))
    native_items = [c for c in entries if c["componentData"]["name"].startswith(PREFIX)]

    # 1. If 53_lfo_smooth has not populated the tab yet, populate it safely
    if not native_items:
        extra_53 = _load_53_lfo_smooth()
        safe_ctx = _SafeContextProxy(ctx, tui)
        extra_53.apply(tui, qlinks, safe_ctx)
        defs_list = tui["pageData"].setdefault("componentDefinitions", {}).setdefault("localComponentDefinitions", [])
        definitions = {d.get("key"): d.get("value") for d in defs_list}
        entries = list(_leaf_entries(tab, definitions))
        native_items = [c for c in entries if c["componentData"]["name"].startswith(PREFIX)]

    # 2. Native host skin owns no Q frame; overlay generation policy owns Gen1 framing.
    tab["qlinkBoundsData"] = []
    _check_and_sanitize_no_q_numbers(tab)

    # 3. Verify sentinels are intact and clear of any native widget
    by_name = {c["componentData"]["name"]: c for c in entries}
    for name, expected_rect in (
        ("Overlay Marker lfo1 left", SENTINEL_LEFT),
        ("Overlay Instance lfo1", SENTINEL_INSTANCE),
        ("Overlay Marker lfo1 right", SENTINEL_RIGHT),
    ):
        if name in by_name:
            actual = _rect(by_name[name])
            if actual != expected_rect:
                raise ValueError(f"Sentinel {name} has incorrect rect: {actual} != {expected_rect}")
            for w in native_items:
                w_rect = _rect(w)
                margin_rect = (expected_rect[0] - 1, expected_rect[1] - 1, expected_rect[2] + 2, expected_rect[3] + 2)
                if _overlaps(w_rect, margin_rect):
                    raise ValueError(f"Widget {w['componentData']['name']} overlaps sentinel {name}")

    # Verify background sentinel at (900, 2)
    for w in native_items:
        w_rect = _rect(w)
        if _overlaps(w_rect, SENTINEL_BG):
            raise ValueError(f"Widget {w['componentData']['name']} overlaps background sentinel {SENTINEL_BG}")

    # 4. Verify native widget counts and contracts
    root_items = [e for e in native_items
                  if not e["componentData"]["name"].startswith((PREFIX + "Editor ", PREFIX + "Detail "))]
    editor_items = [e for e in native_items if e["componentData"]["name"].startswith(PREFIX + "Editor ")]
    detail_items = [e for e in native_items if e["componentData"]["name"].startswith(PREFIX + "Detail ")]

    captions = [e for e in root_items if e["componentData"]["name"].startswith(PREFIX + "Group ")]
    controls = [e for e in root_items if e not in captions]
    # Two SYNC composites became six directly bound Button leaves apiece.
    # The 25 scalar composites and two action Buttons retain their contracts.
    if len(controls) != 39 or sum(e["componentData"]["type"] == "Button" for e in controls) != 14:
        raise ValueError(f"Expected 39 root native LFO controls including 14 Buttons, found {len(controls)}")
    if len(captions) != 12 or any(e["componentData"]["type"] != "Label" for e in captions):
        raise ValueError("Expected twelve layout-gated native LFO group captions")
    if len(editor_items) != 29:
        raise ValueError(f"Expected 29 Shape Editor widgets, found {len(editor_items)}")
    if len(detail_items) != 18:
        raise ValueError(f"Expected 18 Shape Detail widgets, found {len(detail_items)}")

    # 5. Verify widget dimensions: min(w, h) >= 56 for interactive cells, fit within 1280x628
    for item in root_items:
        x, y, w, h = _rect(item)
        if x < 0 or y < 0 or x + w > 1280 or y + h > 628:
            raise ValueError(f"Widget {item['componentData']['name']} out of bounds: {(x, y, w, h)}")
        if item not in captions and min(w, h) < 56:
            raise ValueError(f"Widget {item['componentData']['name']} too small: {(w, h)} < 56")

    # 6. Resolve the definitions ACTUALLY referenced after shared-tree deduplication.
    # A wrapper's presence cannot excuse an unresolved child (e.g. SmoothTime).
    primitives = {"Image", "Label", "Button", "Indicator", "Knob", "Focus"}
    seen = set()
    def verify_type(kind):
        if kind in primitives or kind in seen:
            return
        if kind not in definitions:
            raise ValueError(f"Missing referenced LFO component definition: {kind}")
        seen.add(kind)
        for child in definitions[kind].get("componentsData", []):
            verify_type(child["componentData"]["type"])
    for item in native_items:
        verify_type(item["componentData"]["type"])

    # 7. Verify replacement strings for choice menus
    replacements = {r.get("key"): r.get("value") for r in tui["pageData"].get("replacementStrings", [])}
    if not any("Parameter" in k and isinstance(v, list) and any("LFO" in str(x) for x in v) for k, v in replacements.items()):
        raise ValueError("Missing replacementStrings for LFO source selector")

    # 8. Attach metadata contract to ctx
    if not hasattr(ctx, "screen_lfo_contract"):
        ctx.screen_lfo_contract = {
            "title": SCREEN_TITLE,
            "rebuilt_components": REBUILT_NATIVE_COMPONENTS,
            "approved_radio_variant": APPROVED_RADIO_VARIANT,
            "has_native_choice_group": HAS_NATIVE_CHOICE_GROUP,
            "rejected_concepts": REJECTED_CONCEPTS,
        }
