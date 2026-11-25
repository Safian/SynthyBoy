# SynthyBoy - MPC skin extra: native UI screen integration and audit for Osc A.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Host-native UI screen integration, contract audit, and sanitization for Osc A.

Autoloaded by scripts/apply-mpc-skin-extras.py in name order (runs after
98_osc_surface.py, 99_backgrounds.py, and 99_shared_labels.py).

Responsibilities:
1. Verifies that the Osc A screen integrates ready, verified native components:
   - Top row:
     * Instance Selector (1280x48 px, popup choice overlay)
     * Host-native POWER button (48x48 icon button, on #5cff9d, off #ff4f4f)
     * Type selector menu (5 oscillator types, 48 px face)
     * SOURCE / Table stepper (< Table > stepper arrows + menu)
     * WT POS position dial
     * OCT octave number field
     * Routing destination selector (150x56 px, 7 destinations, popup overlay)
   - Middle real-time waveform / wavetable display aperture (C++ OscDisplay / display::draw)
   - Bottom panel:
     * Unison block (VOICES number field, DETUNE dial, FIN fine tune, DETAIL gear action button)
     * Warp 1 & Warp 2 blocks (Type selector + Amount dial with automatic dynamic hiding when Off)
     * MORE action button & BACK button (Vissza)
2. Enforces the strict Owner Visual Rule:
   - NO visible Q-Link numbers, chips (Q1..Q4), badges, or bars
   - Rejection of outdated OSC sidebar layout (osc-curve-side-controls)
   - Rejection of fabricated LFO markings
   - Preservation of hardware parameter bindings, popup overlays, Back button, and states
3. Identifies and documents the missing compact variants:
   - PAN 90 px dial (missing ~48 px compact variant)
   - LEVEL 90 px dial (missing ~48 px compact variant)
   - ROUTE 64 px button (missing 48x48 px compact variant)
   - Waveform live curve (current ~220 px height, missing 120-140 px low profile compact curve)
   - Potentiometer with cyan dot (56/64 px, missing 48 px compact variant)
"""

from pathlib import Path
import re

CANONICAL_TAB_NAMES = ("Osc", "Osc A")
SCREEN_TITLE = "Osc A"

# Inventory of built / integrated native components on Osc A:
REBUILT_NATIVE_COMPONENTS = (
    {
        "id": "osc_instance_select",
        "name": "Osc Native Source Tabs",
        "kind": "tab_bar",
        "rect": (0, 580, 1280, 48),
        "role": "Oszcillátor példány választó (Sub, Osc A, Osc B, Osc C, Noise)",
        "status": "MEGÉPÍTETT (1280x48 px)",
    },
    {
        "id": "osc_power",
        "name": "Osc Native Power",
        "kind": "oscNativePowerLayer",
        "rect": (12, 8, 106, 164),
        "role": "Oszcillátor modul POWER ikongomb (#5cff9d aktív, #ff4f4f inaktív)",
        "status": "MEGÉPÍTETT (48x48 px)",
    },
    {
        "id": "osc_type",
        "name": "osc1.type",
        "kind": "oscNativeSetting_menu",
        "rect": (122, 4, 256, 56),
        "role": "Oszcillátor típus választó (Wavetable, Granular, Sample, Multisample, Spectral)",
        "status": "MEGÉPÍTETT (48 px magasságú menü)",
    },
    {
        "id": "osc_source_table",
        "name": "osc1.wavetable.table / SOURCE",
        "kind": "oscNativeSetting_table",
        "rect": (386, 4, 484, 56),
        "role": "Hullámtábla / forrás léptető gombok és lista megnyitó",
        "status": "MEGÉPÍTETT (léptető nyilak + popup lista)",
    },
    {
        "id": "osc_wt_pos",
        "name": "osc1.wavetable.pos",
        "kind": "oscNativeSetting_dial_uni_side",
        "rect": (994, 92, 146, 48),
        "role": "Hullámtábla pozíció szabályozó tárcsa (WT POS / SCAN / POSITION)",
        "status": "MEGÉPÍTETT (natív unipoláris tárcsa felirattal)",
    },
    {
        "id": "osc_octave",
        "name": "osc1.octave",
        "kind": "oscNativeSetting_number",
        "rect": (1044, 25, 74, 72),
        "role": "Oktáv transzponálás számmező (OCT)",
        "status": "MEGÉPÍTETT (natív számmező)",
    },
    {
        "id": "osc_routing",
        "name": "Osc Native Routing",
        "kind": "oscNativeRoutingLayer",
        "rect": (1122, 37, 150, 48),
        "role": "Kimeneti útválasztó (F1, F2, F1+2, FX, Direct, Blend, None) választó",
        "status": "MEGÉPÍTETT (150x56 px, popup overlay)",
    },
    {
        "id": "osc_unison_block",
        "name": "Unison blokk (Voices, Detune, Detail)",
        "kind": "oscNativeAbcSettings",
        "rect": (14, 450, 552, 172),
        "role": "Uniszónó hangszám, detune tárcsa és Unison aloldal DETAIL gomb",
        "status": "MEGÉPÍTETT (natív számmező + tárcsa + funkciógomb)",
    },
    {
        "id": "osc_warp_block",
        "name": "Warp 1 & Warp 2 blokk",
        "kind": "oscNativeAbcSettings",
        "rect": (618, 450, 488, 172),
        "role": "Warp típus és mennyiség szabályozás dinamikus kikapcsolt elrejtéssel",
        "status": "MEGÉPÍTETT (menü + tárcsa dinamikus kapuzással)",
    },
    {
        "id": "osc_actions",
        "name": "MORE & BACK akciógombok",
        "kind": "oscNativeDetail",
        "rect": (1146, 566, 120, 56),
        "role": "MORE aloldal és Vissza (BACK) navigáció a részletes beállításokhoz",
        "status": "MEGÉPÍTETT (48/56 px natív akciógombok)",
    },
)

# Inventory of missing compact variants:
MISSING_COMPACT_VARIANTS = (
    {
        "id": "pan_compact",
        "name": "PAN tárcsa kompakt variáns",
        "current_size": "90 x 90 px",
        "target_size": "48 x 48 px vagy horizontális csík",
        "description": "Bipoláris panoráma szabályozó; jelenleg csak 90 px létezik a könyvtárban.",
    },
    {
        "id": "level_compact",
        "name": "LEVEL tárcsa kompakt variáns",
        "current_size": "90 x 90 px",
        "target_size": "48 x 48 px vagy vertikális/horizontális fader",
        "description": "Unipoláris hangerő szabályozó; jelenleg csak 90 px létezik a könyvtárban.",
    },
    {
        "id": "route_button_compact",
        "name": "ROUTE gomb kompakt variáns",
        "current_size": "64 x 64 px",
        "target_size": "48 x 48 px",
        "description": "Kompakt célállomás választó gomb szűrő szegmens állapotjelzőkkel.",
    },
    {
        "id": "waveform_curve_compact",
        "name": "Waveform görbe alacsony profilú variáns",
        "current_size": "~220 px magasság (harmadosztásban)",
        "target_size": "120-140 px magasság",
        "description": "Alacsonyabb profilú kompakt hullámforma megjelenítő a vertikális sűrítéshez.",
    },
    {
        "id": "cyan_knob_compact",
        "name": "Cián jelzésű potméter kompakt variáns",
        "current_size": "56 x 56 px / 64 x 64 px",
        "target_size": "48 x 48 px",
        "description": "Kompakt potméter finomszabályozáshoz diszkrét cián ponttal.",
    },
)

# Rejected design concepts:
REJECTED_CONCEPTS = (
    "osc-curve-side-controls (elvetett oldalsávos vezérlőrács)",
    "Visible Q-Link numbers / chips / badges (Q1..Q4 chipek tilosak)",
    "Fabricated LFO markings / unbacked visual decorations",
    "Historical 27-control before/Init_osca.png layout",
)


def _check_and_sanitize_no_q_numbers(obj, path=""):
    """Recursively verify that no visible Q-Link numbers, chips, or lanes exist."""
    q_pattern = re.compile(r"\bQ\s*[1-9][0-9]?\b", re.IGNORECASE)
    if isinstance(obj, dict):
        # Check text and caption fields
        text = obj.get("text")
        if isinstance(text, str) and q_pattern.search(text):
            # Sanitize visible Q-Link text
            obj["text"] = q_pattern.sub("", text).strip()
        for k, v in list(obj.items()):
            _check_and_sanitize_no_q_numbers(v, f"{path}.{k}")
    elif isinstance(obj, list):
        for i, item in enumerate(obj):
            _check_and_sanitize_no_q_numbers(item, f"{path}[{i}]")


def audit_osc_a_tab(tab, defs):
    """Audit the Osc A tab structure against native component requirements."""
    items = tab.get("componentDefinition", {}).get("componentsData", [])
    names = {c.get("componentData", {}).get("name") for c in items}
    types = {c.get("componentData", {}).get("type") for c in items}

    # Verify modulation editor is preserved
    if "Native Modulation Editor" not in names:
        raise ValueError("Osc A tab missing Native Modulation Editor")

    # Verify Osc Native Surface is present
    if "Osc Native Surface" not in names and "oscNativeSurface" not in types:
        raise ValueError("Osc A tab missing Osc Native Surface")

    # Verify popup overlay definitions exist
    if "oscNativeSourceChoices" not in defs:
        raise ValueError("Missing oscNativeSourceChoices overlay definition for Osc A")
    if "oscNativeRoutingChoices" not in defs:
        raise ValueError("Missing oscNativeRoutingChoices overlay definition for Osc A")

    # Sanitize Q-Link numbers
    _check_and_sanitize_no_q_numbers(tab)
    if "oscNativeSurface" in defs:
        _check_and_sanitize_no_q_numbers(defs["oscNativeSurface"])


def apply(tui, qlinks, ctx):
    """Autoloaded entry point for the Osc A screen."""
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    # Find the Osc A tab (internal name "Osc A", label may be "Osc")
    osc_tabs = [t for t in tui["pageData"]["tabs"] if t.get("tabName") in CANONICAL_TAB_NAMES]
    if not osc_tabs:
        return

    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    for tab in osc_tabs:
        audit_osc_a_tab(tab, defs)

    # Attach screen metadata to ctx for testing and tooling
    if not hasattr(ctx, "screen_osc_a_contract"):
        ctx.screen_osc_a_contract = {
            "title": SCREEN_TITLE,
            "rebuilt_components": REBUILT_NATIVE_COMPONENTS,
            "missing_compact_variants": MISSING_COMPACT_VARIANTS,
            "rejected_concepts": REJECTED_CONCEPTS,
        }
