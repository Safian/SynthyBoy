#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Hand-run tool: regenerate the catalogue images of the native components (components/native/<name>/img/*.png).

Not part of any build. Every image is rendered by the component's own public recipe functions according to its
catalog.json entry, into a staging directory first; each one is then compared pixel by pixel with the committed PNG
and a table (component, image, identical / changed / new) is printed. With --write the changed and new images are
copied over the committed ones (every committed PNG the recipes produce, catalogue pictures and strips alike). Images that no recipe renders are listed as "no renderer". Images that also exist in
skin/ (same file name) are compared with the skin copy and reported as findings only; skin/ is never written.

  python3 scripts/render-component-images.py                  # table only
  python3 scripts/render-component-images.py --write          # also update the changed images
  python3 scripts/render-component-images.py --only label,pan  # a subset
  python3 scripts/render-component-images.py --skip header     # the header has its own owner
"""
import argparse
import importlib
import json
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
for p in (ROOT, ROOT / "scripts"):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

from PIL import Image, ImageChops  # noqa: E402

from components.style import size as _size  # noqa: E402

NATIVE = ROOT / "components/native"


def entry(name, key):
    return json.loads((NATIVE / name / "catalog.json").read_text(encoding="utf-8"))[key]


def mod(name):
    return importlib.import_module(f"components.native.{name}.{name}")


def samples(name, samples_of):
    """Components whose catalogue entry (key = name) lists samples rendered by render_sample(sample)."""
    def stage(out):
        m = mod(name)
        for s in getattr(m, samples_of)(entry(name, name)):
            m.render_sample(s).save(out / s["file"])
    return stage


def stage_button(out):
    m = mod("button")
    for s in m.button_samples(entry("button", "button")):
        m.render_sample(s).save(out / s["file"])


def stage_icon_button(out):
    m = mod("icon_button")
    for s in m.icon_samples(entry("icon_button", "icon_button")):
        m.render_sample(s).save(out / s["file"])


def stage_header(out):
    m = mod("header")
    for e in m.header_examples(entry("header", "header")):
        m.render_header(e).save(out / e["file"])


def stage_label(out):
    m = mod("label")
    e = entry("label", "label")
    for v in m.label_variants(e):
        m.render_label(v["text"], v["font"], v["colour"], unit=v["unit"], case=v["case"]).save(out / v["file"])
    for item in m.dial_examples(e):
        m.render_dial_example(item).save(out / item["file"])


def stage_on_off(out):
    m = mod("on_off_label")
    for e in m.examples(entry("on_off_label", "on_off")):
        m.render_example(e).save(out / e["file"])


def stage_dial(name):
    """Pan / Level: the play_sub_* filmstrips (frame size from the file name) and the catalogue "dial" entry pictures."""
    def stage(out):
        m = mod(name)
        level = mod("level")
        committed = NATIVE / name / "img"
        for path in sorted(committed.glob(f"play_sub_{name}_*.png")):
            kwargs = {"active": not path.stem.endswith("_inactive")} if name == "level" else {}
            m.write_strip(out / path.name, frame_size=int(path.stem.split("_")[3]), **kwargs)
        dial = entry(name, "dial")
        variants = level.dial_variants(dial)
        for v in variants:
            level.render_dial_preview(v["diameter"], dial["states"], bipolar=dial["bipolar"],
                                      gap=_size(dial["state_gap"])).save(out / v["file"])
        for item in level.dial_state_images(dial):
            level.render_dial_preview(variants[0]["diameter"], [item["state"]], bipolar=dial["bipolar"]).save(out / item["file"])
        text, text_variants = level.dial_text_variants(dial)
        for item in text_variants:
            level.render_dial_labelled(text["diameter_px"], text["state"], text["caption"], text["value"], item["layout"],
                                       bipolar=dial["bipolar"], gap=_size(dial["state_gap"])).save(out / item["file"])
    return stage


def stage_selector(out):
    m = mod("selector")
    for v in m.dropdown_variants(entry("selector", "dropdown")):
        m.render_dropdown_variant(_size(v["width"]), _size(v.get("height", "height.medium")), v["value"],
                                  label=v.get("label"), placement=v["placement"], align=v["align"]).save(out / v["file"])


def stage_filter_selector(out):
    m = mod("filter_selector")
    for _, f1, f2, file, _ in m.STATES:
        m.render_state(f1, f2).save(out / file)


def stage_hslider(out):
    m = mod("hslider")
    for e in m.hslider_examples(entry("hslider", "hslider")):
        m.render_example(e).save(out / e["file"])


def stage_mixer_strip(out):
    m = mod("mixer_strip")
    for e in m.strip_examples(entry("mixer_strip", "mixer_strip")):
        m.render_strip(e).save(out / e["file"])


def stage_macro_card(out):
    from components.native.macro_card.macro_card import CARD_HEIGHT, CARD_WIDTH, macro_level_definition, render_macro_card
    with tempfile.TemporaryDirectory() as td:
        level_def = macro_level_definition(None, Path(td))
        with Image.open(Path(td) / "play_sub_level_90.png") as strip:
            render_macro_card(level_def, strip, "MACRO 1", 50, "F1 CUTOFF", width=CARD_WIDTH, height=CARD_HEIGHT).save(
                out / "catalog-macro-card.png")


def stage_tune(out):
    m = mod("tune")
    m.render_tune_card({"oct": "-1", "sem": "+7", "fin": "-25", "crs": "-12.50"}, track_active=True,
                       field_h=_size("tune.field.height"), field_gap=_size("gap.field")).save(out / "osc-tune-compact-card.png")


def stage_panel(out):
    mod("panel").render_panel_preview(output_path=out / "catalog-panel.png")


def stage_band_lane(out):
    mod("band_lane").render_band_lane(out / "catalog-band-lane.png")


def stage_keyboard(out):
    mod("keyboard").render_sample().save(out / "keyboard-preview.png")


def stage_mod_row(out):
    mod("mod_row").write_assets(out)


def stage_meter(out):
    """catalog-active.png: the idle meter with the lower half of the peak meter pasted over it (as the catalogue card shows)."""
    m = mod("meter")
    m.write_meter_assets(out)
    with Image.open(out / "mixer_meter_idle.png") as idle, Image.open(out / "mixer_meter_peak.png") as peak:
        card = idle.convert("RGBA")
        start = card.height // 2
        card.paste(peak.crop((0, start, card.width, card.height)), (0, start))
    card.save(out / "catalog-active.png")


def stage_active_text(out):
    # the card pictures are the ON and OFF face of the 50 x 24 default at the catalogue text
    m = mod("active_text")
    m.write_active_text_assets(out, on_name="active-text-on.png", off_name="active-text-off.png")


def stage_name_value(out):
    m = mod("name_value")
    m.render_name_value_preview("OCT", "-1", width=112, height=40).save(out / "catalog-name-value-40.png")


def stage_fader(out):
    mod("fader").write_fader_assets(out)


def stage_solo_mute(out):
    mod("solo_mute").write_solo_mute_assets(out)


STAGES = {
    "active_text": stage_active_text,
    "band_lane": stage_band_lane,
    "button": stage_button,
    "fader": stage_fader,
    "filter_selector": stage_filter_selector,
    "header": stage_header,
    "hslider": stage_hslider,
    "icon_button": stage_icon_button,
    "keyboard": stage_keyboard,
    "label": stage_label,
    "level": stage_dial("level"),
    "macro_card": stage_macro_card,
    "meter": stage_meter,
    "mixer_strip": stage_mixer_strip,
    "mod_row": stage_mod_row,
    "name_value": stage_name_value,
    "note_mask": samples("note_mask", "mask_samples"),
    "on_off_label": stage_on_off,
    "pan": stage_dial("pan"),
    "panel": stage_panel,
    "radio_grid": samples("radio_grid", "grid_samples"),
    "radio_list": samples("radio_list", "radio_samples"),
    "rotary_selector": samples("rotary_selector", "rotary_samples"),
    "selector": stage_selector,
    "slide_switch": samples("slide_switch", "slide_samples"),
    "solo_mute": stage_solo_mute,
    "stepped_dial": samples("stepped_dial", "stepped_samples"),
    "tune": stage_tune,
    "wave_icons": samples("wave_icons", "sheet_samples"),
}


def differing_pixels(a, b):
    """-1 when the sizes differ, else the number of pixels that differ (RGBA)."""
    if a.size != b.size:
        return -1
    diff = ImageChops.difference(a.convert("RGBA"), b.convert("RGBA"))
    return sum(1 for px in diff.getdata() if px != (0, 0, 0, 0))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true", help="copy the changed and new images over the committed ones")
    ap.add_argument("--only", default="", help="comma separated component names")
    ap.add_argument("--skip", default="", help="comma separated component names to leave out")
    args = ap.parse_args()
    only = {n for n in args.only.split(",") if n}
    skip = {n for n in args.skip.split(",") if n}
    all_names = sorted(p.name for p in NATIVE.iterdir() if (p / "catalog.json").is_file())
    rows, findings, failures = [], [], []
    for name in all_names:
        if (only and name not in only) or name in skip:
            continue
        committed = NATIVE / name / "img"
        referenced = set()
        stack = [json.loads((NATIVE / name / "catalog.json").read_text(encoding="utf-8"))]
        while stack:
            o = stack.pop()
            if isinstance(o, dict):
                for k, v in o.items():
                    if k == "file" and isinstance(v, str):
                        referenced.add(v)
                    else:
                        stack.append(v)
            elif isinstance(o, list):
                stack.extend(o)
        if name not in STAGES:
            rows.append((name, "(all)", "no renderer", ""))
            continue
        with tempfile.TemporaryDirectory() as td:
            out = Path(td)
            try:
                STAGES[name](out)
            except Exception as exc:  # report, go on with the next component
                failures.append((name, f"{type(exc).__name__}: {exc}"))
                continue
            produced = {p.name for p in out.glob("*.png")}
            for file in sorted(produced):
                new = Image.open(out / file)
                new.load()
                old_path = committed / file
                if not old_path.is_file():
                    # only the pictures a catalogue entry names, or the strips already committed, are component images
                    if file in referenced:
                        rows.append((name, file, "new", ""))
                        if args.write:
                            shutil.copyfile(out / file, old_path)
                    continue
                diff = differing_pixels(new, Image.open(old_path))
                rows.append((name, file, "identical" if diff == 0 else "changed", "" if diff == 0 else
                             ("size differs" if diff < 0 else f"{diff} px")))
                if diff != 0 and args.write:
                    shutil.copyfile(out / file, old_path)
                skin_path = ROOT / "skin" / file
                if skin_path.is_file():
                    sdiff = differing_pixels(new, Image.open(skin_path))
                    if sdiff != 0:
                        findings.append((name, file, "size differs" if sdiff < 0 else f"{sdiff} px"))
            for file in sorted(referenced - produced):
                rows.append((name, file, "no renderer", ""))
    width = max((len(r[0]) for r in rows), default=4)
    wfile = max((len(r[1]) for r in rows), default=5)
    print(f"{'component':<{width}}  {'image':<{wfile}}  result")
    for comp, file, result, detail in rows:
        print(f"{comp:<{width}}  {file:<{wfile}}  {result} {detail}".rstrip())
    counts = {}
    for r in rows:
        counts[r[2]] = counts.get(r[2], 0) + 1
    print("\ncounts:", counts)
    if findings:
        print("\nskin/ images whose recipe output differs from the committed skin PNG (finding only, skin/ not touched):")
        for comp, file, detail in findings:
            print(f"  {comp}: skin/{file} ({detail})")
    if failures:
        print("\nrenderer failures:")
        for comp, msg in failures:
            print(f"  {comp}: {msg}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
