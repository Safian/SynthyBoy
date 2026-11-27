# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SLIDE SWITCH: a positional slider switch with 2 to 4 positions, horizontal or vertical, catalogue pictures.

A recessed rounded track (the Button's gradient turned over: `surface.control` at the top lightening to
`button.gradient.top`, keyline `button.gradient.border` over `.border.bottom`, so the top edge is the shadow) at
32 / 40 / 48 px thickness (`button.size.<px>`); a round light thumb (`text.icon` to `text.caption` gradient, a thin
`ink.black` shadow) of diameter thickness - 2 * `slide_switch.inset` that sits on one of N evenly spaced positions, with a
thin `accent.cyan` line across its centre. The labels are Label recipes (`label.render_label`, `font.label` em;
the selected one `text.value`, the others `text.caption`): horizontal above the track centred over each position,
vertical right of the track left aligned at the position height. An optional title is the Label block-title style
(`panel.title` font, `text.caption`) at the top left. Disabled: `style.dim_artwork`. Docs: docs/ui-components/slide_switch.md.
"""
from components.native.button.button import gradient_face
from components.native.label.label import render_label
from components.style import color as _style_color, dim_artwork, rgb as _style_rgb, size as _style_size

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    Image = ImageDraw = ImageFilter = None

ORIENTATIONS = ("horizontal", "vertical")
STATES = ("enabled", "disabled")
SIZES = (32, 40, 48)
SAMPLE_KEYS = {"id", "file", "alt", "caption", "orientation", "labels", "position", "size", "length", "title", "state"}
TITLE_FONT = "panel.title:font"


def _thickness(token):
    value = token if isinstance(token, int) else _style_size(token)
    if value not in SIZES:
        raise ValueError(f"track thickness must be one of {SIZES}: {token!r}")
    return value


def _label(text, selected):
    return render_label(text, _style_size("slide_switch")["label_font"],
                        "text.value" if selected else "text.caption", unit="em")


def layout(sample):
    """The geometry in final pixels: picture size, track rect, thumb diameter, thumb centres, label boxes, title origin."""
    tokens, thick = _style_size("slide_switch"), _thickness(sample["size"])
    vertical, count, length = sample["orientation"] == "vertical", len(sample["labels"]), sample["length"]
    line_h = render_label("0", tokens["label_font"], "text.value", unit="em").height
    title_h = render_label(sample["title"], TITLE_FONT, "text.caption").height + tokens["title_gap"] if sample["title"] else 0
    labels = [_label(t, i == sample["position"]) for i, t in enumerate(sample["labels"])]
    span = length - thick
    centres = [thick / 2 + i * span / (count - 1) for i in range(count)]
    if vertical:
        width = thick + tokens["label_gap"] + max(l.width for l in labels)
        track = (0, title_h, thick, title_h + length)
        thumb_centres = [(thick / 2, title_h + c) for c in centres]
        boxes = []
        for label, (_, cy) in zip(labels, thumb_centres):
            ink = label.getchannel("A").getbbox()
            boxes.append((thick + tokens["label_gap"], round(cy - (ink[1] + ink[3]) / 2)))
        size = (width, title_h + length)
    else:
        top = title_h + line_h + tokens["label_gap"]
        track = (0, top, length, top + thick)
        thumb_centres = [(c, top + thick / 2) for c in centres]
        boxes = [(min(max(round(cx - l.width / 2), 0), length - l.width), title_h) for l, (cx, _) in zip(labels, thumb_centres)]
        size = (length, top + thick)
    return {"size": size, "track": track, "diameter": thick - 2 * tokens["inset"], "centres": thumb_centres,
            "labels": labels, "label_boxes": boxes, "title_h": title_h}


def _thumb_layer(size, centre, diameter, vertical, scale):
    tokens = _style_size("slide_switch")
    shadow = tokens["shadow"]
    cx, cy, r = centre[0] * scale, centre[1] * scale, diameter * scale / 2
    layer = Image.new("RGBA", size)
    mask = Image.new("L", size)
    off = shadow["offset"] * scale
    ImageDraw.Draw(mask).ellipse((cx - r, cy - r + off, cx + r, cy + r + off), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(shadow["blur"] * scale)).point(lambda v: v * shadow["alpha"] // 255)
    layer = Image.new("RGBA", size, _style_rgb("ink.black") + (0,))
    layer.putalpha(mask)
    top, bottom = _style_rgb("text.icon"), _style_rgb("text.caption")
    disc = Image.new("RGBA", size)
    pen = ImageDraw.Draw(disc)
    for y in range(int(cy - r), int(cy + r) + 1):
        t = min(1., max(0., (y - (cy - r)) / (2 * r)))
        pen.line((0, y, size[0], y), fill=tuple(round(a + t * (b - a)) for a, b in zip(top, bottom)) + (255,))
    cut = Image.new("L", size)
    ImageDraw.Draw(cut).ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    layer.paste(disc, (0, 0), cut)
    half, width = tokens["line"]["ratio"] * r, tokens["line"]["width"] * scale
    box = (cx - half, cy - width / 2, cx + half, cy + width / 2) if vertical else (cx - width / 2, cy - half, cx + width / 2, cy + half)
    ImageDraw.Draw(layer).rectangle(box, fill=_style_color("accent.cyan"))
    return layer


def render_sample(sample):
    """One position of the switch: track, thumb at the position, labels (and the title); disabled is dimmed."""
    tokens = _style_size("slide_switch")
    scale, geo = tokens["scale"], layout(sample)
    vertical = sample["orientation"] == "vertical"
    width, height = geo["size"]
    big = (width * scale, height * scale)
    left, top, right, bottom = geo["track"]
    thick = _thickness(sample["size"])
    track, _ = gradient_face(
        ((right - left) * scale, (bottom - top) * scale), inset=0, radius=thick * scale // 2,
        frame=scale, span=_style_size("control.gradient"),
        fill=(_style_rgb("surface.control"), _style_rgb("button.gradient.top")),
        line=(_style_rgb("button.gradient.border"), _style_rgb("button.gradient.border.bottom")))
    canvas = Image.new("RGBA", big)
    canvas.alpha_composite(track, (left * scale, top * scale))
    canvas.alpha_composite(_thumb_layer(big, geo["centres"][sample["position"]], geo["diameter"], vertical, scale))
    image = canvas.resize((width, height), Image.Resampling.LANCZOS)
    for label, origin in zip(geo["labels"], geo["label_boxes"]):
        image.alpha_composite(label, origin)
    if sample["title"]:
        image.alpha_composite(render_label(sample["title"], TITLE_FONT, "text.caption"), (0, 0))
    return dim_artwork(image) if sample["state"] == "disabled" else image


def sample_size(sample):
    return layout(sample)["size"]


def slide_samples(entry):
    """The samples of a catalogue "slide_switch" entry, validated; a new example is one more entry."""
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("slide_switch entry needs a nonempty samples list")
    for item in entry["samples"]:
        if (set(item) != SAMPLE_KEYS or item["orientation"] not in ORIENTATIONS or item["state"] not in STATES
                or not isinstance(item["labels"], list) or not 2 <= len(item["labels"]) <= 4
                or type(item["position"]) is not int or not 0 <= item["position"] < len(item["labels"])
                or type(item["length"]) is not int or item["length"] <= _thickness(item["size"])
                or not (item["title"] is None or isinstance(item["title"], str))):
            raise ValueError(f"slide switch sample needs exactly {sorted(SAMPLE_KEYS)}, orientation in {ORIENTATIONS}, state in {STATES}, "
                             "2 to 4 labels, a position among them and a length above the thickness")
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("slide switch sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    samples = slide_samples(entry)
    groups = (("Vízszintes", lambda i: i["orientation"] == "horizontal" and i["state"] == "enabled"),
              ("Függőleges", lambda i: i["orientation"] == "vertical" and i["state"] == "enabled"),
              ("Disabled", lambda i: i["state"] == "disabled"))
    sections = []
    for heading, pick in groups:
        images = [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if pick(i)]
        if images:
            sections.append({"heading": heading, "images": images})
    sections.append({"heading": "Helyigény", "table": {"head": ["Példa", "Méret (px)"], "rows": [
        [i["caption"], "{} × {}".format(*sample_size(i))] for i in samples]}})
    return sections
