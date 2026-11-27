#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the approved native component catalogue from each component's metadata."""
import html
import hashlib
import importlib
import json
import re
from pathlib import Path, PurePosixPath
import sys
from urllib.parse import quote

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from components.style import color as _style_color, named_sizes, palette, size as _style_size

LIBRARY = ROOT / "components/native"
OUTPUT = ROOT / "docs/ui-components/kepes-katalogus.html"
TEMPLATE = ROOT / "scripts/templates/native-component-catalog.html"
# Catalogue entries a component module renders into sections (module.catalog_sections(entry)).
EXAMPLE_KEYS = ("label", "on_off", "dropdown", "header")
ORDER = LIBRARY / "catalog-order.json"
OLD_SCREENS = ROOT / "scripts/old-ui-screens.json"
SCREEN_CATALOG = ROOT / "scripts/screen-catalog.json"
OLD_DIRECTORY = ROOT / "docs/screens/old"
DEVICE_LABELS = {"key61": "Key 61", "xl": "XL"}
SAFETY_GROUP = "Biztonsági másolatok (a korábbi aktuális képek)"
COMMAND = "python3 scripts/gen-native-component-catalog.py"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def fields(value, required, optional=()):
    require(isinstance(value, dict), "expected an object")
    require(set(required) <= value.keys(), f"missing fields: {sorted(set(required) - value.keys())}")
    require(value.keys() <= set(required) | set(optional),
            f"unknown fields: {sorted(value.keys() - set(required) - set(optional))}")


def text(value):
    require(isinstance(value, str) and bool(value.strip()), "expected nonempty text")
    def replace(match):
        kind, token = match.groups()
        if kind == "size_label":
            labels = {item["token"]: item["label"] for item in named_sizes()}
            require(token in labels, f"unknown named size: {token}")
            return labels[token]
        number = _style_size(token)
        require(type(number) in (int, float), f"expected scalar size token: {token}")
        return str(number)
    value = re.sub(r"\{(size|size_label):([^{}]+)\}", replace, value)
    require("{" not in value and "}" not in value, "unknown metadata text placeholder")
    return html.escape(value, quote=True)


def image_markup(component, item):
    fields(item, ("file", "alt"), ("caption", "presentation", "display_size"))
    filename = item["file"]
    require(isinstance(filename, str) and filename and "\\" not in filename,
            "image file must be a relative path inside the component's img directory")
    relative = PurePosixPath(filename)
    require(not relative.is_absolute() and ".." not in relative.parts,
            f"unsafe image path: {filename!r}")
    directory = component / "img"
    require(directory.resolve().is_relative_to(component.resolve()), "img directory escapes its component")
    image_path = directory / filename
    require(image_path.resolve().is_relative_to(directory.resolve()), f"image escapes img directory: {filename!r}")
    require(image_path.is_file(), f"selected image is missing: {image_path}")
    with Image.open(image_path) as image:
        width, height = image.size
    if "display_size" in item:
        require(isinstance(item["display_size"], str), "display_size must name a shared sizes.json token")
        size = _style_size(item["display_size"])
        require(isinstance(size, list) and len(size) == 2 and
                all(type(number) is int and number > 0 for number in size),
                f"display_size must contain two positive integers: {filename}")
        require(size[0] <= width and size[1] <= height and size[0] * height == size[1] * width,
                f"display_size must be a proportional downscale: {filename}")
        width, height = size
    presentation = item.get("presentation", "sample")
    require(presentation in ("hero", "sample"), f"unknown presentation: {presentation!r}")
    url = html.escape(quote("../../" + image_path.relative_to(ROOT).as_posix(), safe="/"), quote=True)
    url += "?v=" + hashlib.sha256(image_path.read_bytes()).hexdigest()[:12]
    css = ' class="size-sample"' if presentation == "sample" else ""
    caption = f"<figcaption>{text(item['caption'])}</figcaption>" if "caption" in item else ""
    return (f"<figure{css}>{caption}<a href=\"{url}\"><img src=\"{url}\" "
            f"width=\"{width}\" height=\"{height}\" alt=\"{text(item['alt'])}\"></a></figure>")


def component_markup(component, metadata):
    fields(metadata, ("version", "approved", "order", "title", "sections"), ("dial", "wide") + EXAMPLE_KEYS)
    require(type(metadata.get("wide", False)) is bool, "wide must be true or false")
    require(type(metadata["order"]) is int, "order must be an integer")
    require(isinstance(metadata["sections"], list), "sections must be a list")
    sections = list(metadata["sections"])
    if "dial" in metadata:
        # A dial entry yields one text-free size variant per entry, captioned here (never inside the image).
        from components.native.level.level import dial_state_images, dial_text_variants, dial_variants
        dial = metadata["dial"]
        sections.append({"images": [
            {"presentation": "sample", "file": v["file"], "alt": v["alt"],
             "caption": f"{v['label']} · {{size:{v['token']}}} × {{size:{v['token']}}} px"}
            for v in dial_variants(dial)]})
        states = dial_state_images(dial)
        if states:
            token = dial_variants(dial)[0]["token"]
            sections.append({"heading": "Állapotok, szöveg nélkül", "images": [
                {"presentation": "sample", "file": v["file"], "alt": v["alt"],
                 "caption": f"{v['label']} · {{size:{token}}} × {{size:{token}}} px"} for v in states]})
        labelled, variants = dial_text_variants(dial)
        if variants:
            sections.append({"heading": "Szöveges változatok", "images": [
                {"presentation": "sample", "file": v["file"], "alt": v["alt"],
                 "caption": f"{v['label']} · tárcsa {{size:{labelled['token']}}} × {{size:{labelled['token']}}} px"}
                for v in variants]})
    for key in EXAMPLE_KEYS:
        if key in metadata:
            # The component's own module turns its entry into sections; captions come from the entry, never the image.
            module = importlib.import_module(f"components.native.{component.name}.{component.name}")
            sections.extend(module.catalog_sections(metadata[key]))
    require(sections, "sections must be a nonempty list")
    css = ' class="wide"' if metadata.get("wide") else ""
    lines = [f"    <article{css}>\n      <h3>{text(metadata['title'])}</h3>"]
    for section in sections:
        fields(section, (), ("images", "table", "heading"))
        require(("images" in section) != ("table" in section), "a section has images or a table")
        if "heading" in section:
            lines.append(f"      <h4>{text(section['heading'])}</h4>")
        if "table" in section:
            table = section["table"]
            lines.append("      <table>")
            lines.append("        <tr>" + "".join(f"<th>{text(c)}</th>" for c in table["head"]) + "</tr>")
            for row in table["rows"]:
                lines.append("        <tr>" + "".join(f"<td>{text(c)}</td>" for c in row) + "</tr>")
            lines.append("      </table>")
            continue
        require(isinstance(section["images"], list) and section["images"], "images must be a nonempty list")
        # Keep natural-size samples side by side; hero previews retain the page's overview styling.
        samples_open = False
        for item in section["images"]:
            markup = image_markup(component, item)
            sample = item.get("presentation", "sample") == "sample"
            if sample != samples_open:
                lines.append('      <div class="size-samples">' if sample else "      </div>")
                samples_open = sample
            lines.append("        " + markup)
        if samples_open:
            lines.append("      </div>")
    lines.append("    </article>")
    return "\n".join(lines)


def old_screen_groups():
    """Ordered (group title, [(file, name, device, shows, path)]) from old-ui-screens.json and screen-catalog.json."""
    data = json.loads(OLD_SCREENS.read_text(encoding="utf-8"))
    groups = {}
    for item in data["images"]:
        require((OLD_DIRECTORY / item["file"]).is_file(),
                f"{OLD_SCREENS.relative_to(ROOT)}: listed image is missing: docs/screens/old/{item['file']}")
        require(item["device"] in DEVICE_LABELS, f"unknown device for {item['file']}: {item['device']!r}")
        # The INDEX headers repeat the 'round 2' pass as a suffix; merge it into the base section.
        title = re.sub(r"\s*\(round 2\)$", "", item["section"]) or "Alap képernyők"
        groups.setdefault(title, []).append(
            (item["file"], item["source_name"], item["device"], item["shows"], item["path"]))
    safety = []
    for screen in json.loads(SCREEN_CATALOG.read_text(encoding="utf-8"))["screens"]:
        name = f"{screen['name']}_old.jpg"
        require((OLD_DIRECTORY / name).is_file(), f"safety copy is missing: docs/screens/old/{name}")
        safety.append((name, screen["title"], None, screen["status"], ""))
    groups[SAFETY_GROUP] = safety
    return sorted(groups.items(), key=lambda g: (g[0] == SAFETY_GROUP, g[0] == "Alap képernyők", g[0]))


def old_screens_markup():
    esc = lambda value: html.escape(value, quote=True)
    lines = ['<section id="regi-ui-kepernyok">', "  <h2>Régi UI képernyők (összehasonlításhoz)</h2>",
             "  <p>A régi UI (develop 3bb27424) hardveres felvételei, Key 61 és XL, Sbtest / Acid Saw Reso; "
             "összehasonlításhoz a komponensekkel. Egyes host-menük ebben a buildben csak pipát mutatnak, "
             "sorszövegek nélkül.</p>",
             '  <p><input type="search" id="regi-szuro" placeholder="Szűrés névre, eszközre, leírásra" '
             'aria-label="Szűrés" size="40"></p>']
    total = 0
    for title, items in old_screen_groups():
        total += len(items)
        lines.append(f'  <details class="old-group"><summary>{esc(title)} ({len(items)})</summary>')
        lines.append('    <div class="old-grid">')
        for file, name, device, shows, path in items:
            url = esc(quote("../screens/old/" + file, safe="/"))
            where = f" · {DEVICE_LABELS[device]}" if device else ""
            tip = f' title="{esc(path)}"' if path else ""
            lines.append(f'      <figure class="old-shot"{tip}><a href="{url}"><img src="{url}" width="320" '
                         f'loading="lazy" decoding="async" alt="{esc(name)}"></a><figcaption><b>{esc(name)}</b>'
                         f'{where}<br>{esc(shows)}</figcaption></figure>')
        lines.append("    </div>\n  </details>")
    lines.append("</section>")
    return "\n".join(lines), total


def generate():
    cards = []
    for component in sorted(LIBRARY.iterdir()):
        # A distribution unit owns one identically named module. Aliases and removed helpers do not qualify.
        source = component / (component.name + ".py")
        if component.is_symlink() or not component.is_dir() or not source.is_file() or source.is_symlink():
            continue
        path = component / "catalog.json"
        if not path.exists():
            continue
        try:
            require(path.resolve().parent == component.resolve(), "metadata escapes its component")
            metadata = json.loads(path.read_text(encoding="utf-8"))
            require(isinstance(metadata, dict), "expected a metadata object")
            require(type(metadata.get("version")) is int and metadata["version"] == 1, "expected version 1")
            require(type(metadata.get("approved")) is bool, "approved must be true or false")
            if not metadata["approved"]:
                continue
            cards.append((metadata.get("order"), component.name, component_markup(component, metadata)))
        except (ValueError, OSError) as error:
            raise ValueError(f"{path.relative_to(ROOT)}: {error}") from error
    require(cards, "no approved component metadata found")
    # The order list (catalog-order.json) puts named components first and last; the rest keeps its `order` field.
    placement = json.loads(ORDER.read_text(encoding="utf-8"))
    require(set(placement) == {"first", "last"}, "catalog-order.json needs first and last")
    named = placement["first"] + placement["last"]
    require(len(set(named)) == len(named) and set(named) <= {card[1] for card in cards},
            "catalog-order.json lists an unknown or repeated component")
    def rank(card):
        if card[1] in placement["first"]:
            return (0, placement["first"].index(card[1]), card[1])
        if card[1] in placement["last"]:
            return (2, placement["last"].index(card[1]), card[1])
        return (1, card[0], card[1])
    cards.sort(key=rank)
    body = ('<section id="sajat-nativ-komponensek">\n  <h2>Komponensek</h2>\n'
            '  <div class="grid">\n' + "\n".join(card[2] for card in cards) + '\n  </div>\n</section>')
    template = TEMPLATE.read_text(encoding="utf-8")
    require(template.count("{{COMPONENTS}}") == 1 and template.count("{{OLD_SCREENS}}") == 1,
            "template must contain exactly one {{COMPONENTS}} and one {{OLD_SCREENS}} placeholder")
    swatches = []
    for item in palette():
        colour = _style_color(item["token"])
        swatches.append(f'<span class="swatch"><i aria-hidden="true" style="background:{colour}"></i>'
                        f'{text(item["label"])} – <code>{colour.upper()}</code></span>')
    size_list = "<p>" + "<br>".join(text(item["label"]) + " = " +
                                    str(_style_size(item["token"])) + " px" for item in named_sizes()) + "</p>"
    generated = template.replace("{{COMPONENTS}}", body).replace("{{OLD_SCREENS}}", old_screens_markup()[0]).replace("{{PALETTE}}", '<div class="swatches">\n' +
                         "\n".join(swatches) + '\n</div>').replace("{{NAMED_SIZES}}", size_list)
    generated = re.sub(r"\{\{COLOR:([^{}]+)\}\}", lambda match: _style_color(match[1]), generated)
    generated = re.sub(r"\{\{SIZE:([^{}]+)\}\}", lambda match: str(_style_size(match[1])), generated)
    require("{{" not in generated, "unknown template placeholder")
    marker = f"<!-- Generated from components/native/*/catalog.json; regenerate: {COMMAND} -->\n"
    OUTPUT.write_text(marker + generated, encoding="utf-8")
    print(f"Generated {OUTPUT.relative_to(ROOT)} from {len(cards)} approved components")


if __name__ == "__main__":
    try:
        generate()
    except (ValueError, OSError) as error:
        print(f"Catalogue generation failed: {error}", file=sys.stderr)
        sys.exit(1)
