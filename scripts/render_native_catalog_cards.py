#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone preview generator for catalog cards of native SynthyBoy MPC components.

Generates catalog-specific preview images (prefixed catalog-) inside components/native/<component>/img/:
- Button: 48 px icon + text native buttons (catalog-button-48.png)
- Selector: Filter MODEL and Voice step selector at their runtime sizes
- RadioButton: segmented native radio at the runtime 56 px height
- Name-value field: Sub OCT at its runtime 40 px visual height
- MacroCard: direct single MacroCard component sample (298x172)
"""

import json
import runpy
import sys
import tempfile
from pathlib import Path

# Ensure dependencies and local imports are resolvable
REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
if str(REPO_ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "scripts"))

from PIL import Image, ImageDraw

# Component-specific native imports
from components.style import color as _style_color, native_font_em, rgba as _style_rgba, opacity as _style_opacity, size as _style_size
from components.native.button.button import action_art
from components.native.selector.selector import filter_chain_definition, render_selector_face as _render_selector_face
from native_name_value import render_name_value_preview
from components.native.macro_card.macro_card import render_macro_card, macro_level_definition, CARD_WIDTH, CARD_HEIGHT
from components.native.tune.tune import render_tune_card



def render_dial_cards():
    """Refresh bundled strips and previews from the real Pan/Level definitions."""
    from components.native.pan import pan
    from components.native.level import level
    for name, module in (("pan", pan), ("level", level)):
        directory = REPO_ROOT / "components/native" / name / "img"
        for path in sorted(directory.glob(f"play_sub_{name}_*.png")):
            frame_size = int(path.stem.split("_")[3])
            kwargs = {"active": not path.stem.endswith("_inactive")} if name == "level" else {}
            module.write_strip(path, frame_size=frame_size, **kwargs)
        # Text-free size variants from the catalogue entry: the same recipe as the filmstrips, each at its real size.
        dial = json.loads((directory.parent / "catalog.json").read_text(encoding="utf-8"))["dial"]
        for variant in level.dial_variants(dial):
            level.render_dial_preview(variant["diameter"], dial["states"], bipolar=dial["bipolar"],
                                      gap=_style_size(dial["state_gap"])).save(directory / variant["file"])
        for item in level.dial_state_images(dial):
            level.render_dial_preview(level.dial_variants(dial)[0]["diameter"], [item["state"]],
                                      bipolar=dial["bipolar"]).save(directory / item["file"])
        text, variants = level.dial_text_variants(dial)
        for item in variants:
            level.render_dial_labelled(text["diameter_px"], text["state"], text["caption"], text["value"],
                                       item["layout"], bipolar=dial["bipolar"],
                                       gap=_style_size(dial["state_gap"])).save(directory / item["file"])
        print(f"Refreshed native {name} strips and catalogue previews")


def render_label_cards():
    """Label variants and the dial-size series from components/native/label/catalog.json (Label recipe + dial graphic)."""
    from components.native.label import label
    directory = REPO_ROOT / "components/native/label"
    entry = json.loads((directory / "catalog.json").read_text(encoding="utf-8"))["label"]
    for v in label.label_variants(entry):
        label.render_label(v["text"], v["font"], v["colour"], unit=v["unit"], case=v["case"]).save(directory / "img" / v["file"])
    for item in label.dial_examples(entry):
        label.render_dial_example(item).save(directory / "img" / item["file"])
    print("Rendered label examples")


def render_on_off_cards():
    """ON/OFF LABEL examples from components/native/on_off_label/catalog.json."""
    from components.native.on_off_label import on_off_label
    directory = REPO_ROOT / "components/native/on_off_label"
    entry = json.loads((directory / "catalog.json").read_text(encoding="utf-8"))["on_off"]
    for example in on_off_label.examples(entry):
        on_off_label.render_example(example).save(directory / "img" / example["file"])
    print("Rendered on/off label examples")


def render_dropdown_cards():
    """The three dropdown layouts of components/native/selector/catalog.json from the one selector recipe."""
    from components.native.selector import selector
    directory = REPO_ROOT / "components/native/selector"
    entry = json.loads((directory / "catalog.json").read_text(encoding="utf-8"))["dropdown"]
    for v in selector.dropdown_variants(entry):
        selector.render_dropdown_variant(_style_size(v["width"]), _style_size(v.get("height", "height.medium")), v["value"],
                                         label=v.get("label"), placement=v["placement"], align=v["align"]).save(
            directory / "img" / v["file"])
    print("Rendered dropdown variants")


def render_header_cards():
    """Render every example of components/native/header/catalog.json from the header module (existing recipes only)."""
    from components.native.header import header
    directory = REPO_ROOT / "components/native/header"
    entry = json.loads((directory / "catalog.json").read_text(encoding="utf-8"))["header"]
    for example in header.header_examples(entry):
        header.render_header(example).save(directory / "img" / example["file"])
    print(f"Rendered {len(entry['examples'])} header examples")


def render_button_card():
    """Render ONE icon button and ONE text button side-by-side at 48 px."""
    out_dir = REPO_ROOT / "components/native/button/img"
    out_dir.mkdir(parents=True, exist_ok=True)
    
    icon_btn = action_art(icon="power", colour=_style_color("accent.green"))  # 48x48
    text_btn = action_art(text="MORE", width=_style_size('catalog.render_button_card.text.btn'), colour=_style_color("text.icon"))  # 96x48
    
    gap = _style_size('catalog.render_button_card.gap')
    total_w = icon_btn.width + gap + text_btn.width
    total_h = _style_size('catalog.render_button_card.total.h')
    
    combined = Image.new("RGBA", (total_w, total_h), _style_rgba("ink.black", _style_opacity("transparent")))
    combined.paste(icon_btn, (0, 0), icon_btn)
    combined.paste(text_btn, (icon_btn.width + gap, 0), text_btn)
    
    out_file = out_dir / "catalog-button-48.png"
    combined.save(out_file)
    print(f"Generated {out_file} ({combined.size})")
    return out_file


def render_selector_cards():
    """Render supported selector variants from their actual native definitions."""
    out_dir = REPO_ROOT / "components/native/selector/img"
    out_dir.mkdir(parents=True, exist_ok=True)
    
    # F1 selected, F1 powered, F2 bypassed. Compose the live control's image entries.
    definition = filter_chain_definition(out_dir, "fx editor choice menu")
    geom = _style_size("filter.chain")
    sample = Image.new("RGBA", (geom["width"], geom["height"]))
    for child in definition["componentsData"]:
        item = child["componentData"]
        x, y, w, h = map(int, child["bounds"]["bounds"].split())
        with Image.open(out_dir / item["data"]["image"]) as image:
            first_y = h if item["name"] == "F1 power" else 0
            sample.alpha_composite(image.crop((0, first_y, w, first_y + h)), (x, y))
    sample.save(out_dir / "catalog-filter-chain.png")

    # Filter MODEL uses a named selector in the canonical runtime geometry.
    contract = json.loads((REPO_ROOT / "scripts/mpc-skin-extras/filter-native-contract.json").read_text())
    width, height = contract["filter"][0][17][2:]
    _render_selector_face(width, height, name="MODEL", value="Analog", label_height=_style_size("font.button")).save(
        out_dir / "catalog-selector-56-nameval.png")

    # Voice step selector has a value-only 56 px native field.
    # Its dimensions come from the actual definition, not a catalog-only preset.
    from native_voice.voice_tab import voice_definitions
    with tempfile.TemporaryDirectory() as tmp:
        definition = voice_definitions(Path(tmp))["voiceNativeStepSelect"]
        bounds = definition["componentsData"][0]["bounds"]["bounds"]
        width, height = map(int, bounds.split()[2:])
    _render_selector_face(width, height).save(out_dir / "catalog-selector-56-valonly.png")


def render_radio_cards():
    """Render the 56 px segmented style used by ENV, Voice and FX."""
    out_dir = REPO_ROOT / "components/native/radio_button/img"
    out_dir.mkdir(parents=True, exist_ok=True)
    
    # Read the actual ENV selector filmstrip, including its selected state.
    env = runpy.run_path(str(REPO_ROOT / "scripts/mpc-skin-extras/96_env_native.py"))
    with tempfile.TemporaryDirectory() as tmp:
        env["_selector_assets"](tmp)
        with Image.open(Path(tmp) / env["SELECTOR_ART"]) as strip:
            side = env["SELECTOR_H"]
            selected_frame = 1 + 2  # ENV 1, selected variant in _selector_assets.
            strip.crop((0, selected_frame * side, env["SELECTOR_W"],
                        (selected_frame + 1) * side)).save(out_dir / "catalog-radio-56.png")
            group = Image.new("RGBA", (env["SELECTOR_W"] * env["ENV_SELECTOR_CELLS"], side))
            for slot in range(env["ENV_SELECTOR_CELLS"]):
                # Real ENV frames: selected, used, then available instances.
                variant = 2 if slot == 0 else 1 if slot == 1 else 0
                frame = 1 + slot * env["SELECTOR_VARIANTS"] + variant
                group.paste(strip.crop((0, frame * side, env["SELECTOR_W"],
                                        (frame + 1) * side)), (slot * env["SELECTOR_W"], 0))
            group.save(out_dir / "catalog-radio-env-group.png")

    from scripts.native_voice.voice_tab import voice_definitions
    with tempfile.TemporaryDirectory() as tmp:
        definition = voice_definitions(tmp)["voiceNativeRowList"]
        rows = [entry for entry in definition["componentsData"]
                if entry["componentData"]["type"] == "Button"]
        bounds = [tuple(map(int, entry["bounds"]["bounds"].split())) for entry in rows]
        left, top = min(b[0] for b in bounds), min(b[1] for b in bounds)
        right, bottom = max(b[0] + b[2] for b in bounds), max(b[1] + b[3] for b in bounds)
        group = Image.new("RGBA", (right - left, bottom - top))
        for index, (entry, rect) in enumerate(zip(rows, bounds)):
            data = entry["componentData"]["data"]
            with Image.open(Path(tmp) / data["onImage" if index == 0 else "offImage"]) as art:
                group.paste(art, (rect[0] - left, rect[1] - top))
        group.save(out_dir / "catalog-radio-voice-group.png")



def render_name_value_cards():
    """Render the actual Sub OCT field size."""
    out_dir = REPO_ROOT / "components/native/name_value/img"
    out_dir.mkdir(parents=True, exist_ok=True)
    
    contract = json.loads((REPO_ROOT / "scripts/mpc-skin-extras/play-osc-native-contract.json").read_text())
    width = contract["sub"]["second"][2]
    height = _style_size("height.intermediate")
    render_name_value_preview("OCT", "-1", width=width, height=height).save(
        out_dir / "catalog-name-value-40.png")


def render_macro_card_sample():
    """Render a single MacroCard sample (298x172) using shared button/dial components."""
    out_dir = REPO_ROOT / "components/native/macro_card/img"
    out_dir.mkdir(parents=True, exist_ok=True)
    
    with tempfile.TemporaryDirectory() as td:
        skins_dir = Path(td)
        level_def = macro_level_definition(None, skins_dir)
        strip_path = skins_dir / "play_sub_level_90.png"
        with Image.open(strip_path) as strip_im:
            card_im = render_macro_card(
                level_def, strip_im, "MACRO 1", 50, "F1 CUTOFF",
                width=CARD_WIDTH, height=CARD_HEIGHT
            )
            f = out_dir / "catalog-macro-card.png"
            card_im.save(f)
            print(f"Generated {f} ({card_im.size})")


def render_tune_card_sample():
    """Render the accepted native Tune panel directly from its component."""
    output = REPO_ROOT / "components/native/tune/img/osc-tune-compact-card.png"
    render_tune_card({"oct": "-1", "sem": "+7", "fin": "-25", "crs": "-12.50"},
                     track_active=True, field_h=_style_size("tune.field.height"),
                     field_gap=_style_size("gap.field")).save(output)
    print(f"Generated {output}")


def render_remaining_cards():
    """Reuse the accepted component preview sources for the remaining cards."""
    from components.native.button.button import in_slot
    targets = {
        "filter_power": (REPO_ROOT / "components/native/button/img", "filter-catalog-active.png"),
        "meter": (REPO_ROOT / "components/native/meter/img", "catalog-active.png"),
    }
    for name, (directory, filename) in targets.items():
        directory.mkdir(parents=True, exist_ok=True)
        if name == "filter_power":
            preview = in_slot(action_art(icon="power"),
                              (_style_size('catalog.render_source.geometry.offset3'),
                               _style_size('catalog.render_source.geometry.offset4')))
        else:
            with Image.open(directory / "mixer_meter_idle.png") as idle, Image.open(directory / "mixer_meter_peak.png") as peak:
                preview = idle.convert("RGBA")
                start = preview.height // 2
                preview.paste(peak.crop((0, start, preview.width, preview.height)), (0, start))
        preview.save(directory / filename)


def main():
    print("Rendering native catalog cards...")
    render_dial_cards()
    render_label_cards()
    render_on_off_cards()
    render_dropdown_cards()
    render_header_cards()
    render_button_card()
    render_selector_cards()
    render_radio_cards()
    render_name_value_cards()
    render_macro_card_sample()
    render_tune_card_sample()
    render_remaining_cards()
    print("Done rendering all catalog cards.")


if __name__ == "__main__":
    main()
