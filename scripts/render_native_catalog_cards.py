#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone preview generator for catalog cards of native SynthyBoy MPC components.

Generates catalog-specific preview images (prefixed catalog-) inside components/native/<component>/img/:
- Button / IconButton: size samples in the legacy and ghost looks (button/img, icon_button/img)
- Selector: Filter MODEL and Voice step selector at their runtime sizes
- MacroCard: direct single MacroCard component sample (298x172)
"""

import json
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


def render_filter_selector_cards():
    """The FilterSelector states from the real chain-chip recipe (selector.filter_chain_definition)."""
    from components.native.filter_selector import filter_selector
    directory = REPO_ROOT / "components/native/filter_selector/img"
    directory.mkdir(parents=True, exist_ok=True)
    for _, f1, f2, file, _ in filter_selector.STATES:
        filter_selector.render_state(f1, f2).save(directory / file)
    print("Rendered FilterSelector states")


def render_header_cards():
    """Render every example of components/native/header/catalog.json from the header module (existing recipes only)."""
    from components.native.header import header
    directory = REPO_ROOT / "components/native/header"
    entry = json.loads((directory / "catalog.json").read_text(encoding="utf-8"))["header"]
    for example in header.header_examples(entry):
        header.render_header(example).save(directory / "img" / example["file"])
    print(f"Rendered {len(entry['examples'])} header examples")


def render_button_cards():
    """Button and IconButton samples from their catalogue entries (the Button recipe, look legacy / ghost)."""
    from components.native.button import button
    from components.native.icon_button import icon_button
    for name, module, key, samples in (("button", button, "button", button.button_samples),
                                       ("icon_button", icon_button, "icon_button", icon_button.icon_samples)):
        directory = REPO_ROOT / "components/native" / name
        entry = json.loads((directory / "catalog.json").read_text(encoding="utf-8"))[key]
        (directory / "img").mkdir(exist_ok=True)
        for sample in samples(entry):
            module.render_sample(sample).save(directory / "img" / sample["file"])
        print(f"Rendered {name} samples")


def render_sample_cards(name, samples_of):
    """Render every sample of components/native/<name>/catalog.json with the component's own module."""
    import importlib
    module = importlib.import_module(f"components.native.{name}.{name}")
    directory = REPO_ROOT / "components/native" / name
    entry = json.loads((directory / "catalog.json").read_text(encoding="utf-8"))[name]
    (directory / "img").mkdir(exist_ok=True)
    for sample in getattr(module, samples_of)(entry):
        module.render_sample(sample).save(directory / "img" / sample["file"])
    print(f"Rendered {name} samples")


def render_radio_list_cards():
    render_sample_cards("radio_list", "radio_samples")


def render_slide_switch_cards():
    render_sample_cards("slide_switch", "slide_samples")


def render_wave_icons_cards():
    render_sample_cards("wave_icons", "sheet_samples")


def render_stepped_dial_cards():
    render_sample_cards("stepped_dial", "stepped_samples")


def render_rotary_selector_cards():
    render_sample_cards("rotary_selector", "rotary_samples")


def render_radio_grid_cards():
    render_sample_cards("radio_grid", "grid_samples")


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
        "meter": (REPO_ROOT / "components/native/meter/img", "catalog-active.png"),
    }
    for name, (directory, filename) in targets.items():
        directory.mkdir(parents=True, exist_ok=True)
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
    render_filter_selector_cards()
    render_header_cards()
    render_button_cards()
    render_radio_list_cards()
    render_slide_switch_cards()
    render_wave_icons_cards()
    render_stepped_dial_cards()
    render_rotary_selector_cards()
    render_radio_grid_cards()
    from components.native.mixer_strip import mixer_strip
    mixer_strip.render_catalog()
    from components.native.hslider import hslider
    hslider.render_catalog()
    render_macro_card_sample()
    render_tune_card_sample()
    render_remaining_cards()
    print("Done rendering all catalog cards.")


if __name__ == "__main__":
    main()
