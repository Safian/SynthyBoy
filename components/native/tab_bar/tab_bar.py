# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A continuous MPC tab bar with independent selection and navigation handles."""
from pathlib import Path

from PIL import Image, ImageDraw

from components.native.button.button import TRANSPARENT_HIT, button, entry, semibold
from components.style import color, size


def tab_cells(width, count):
    edges = [i * width // count for i in range(count + 1)]
    return zip(edges[:-1], edges[1:])


def render_tab_bar(labels, selected, width, height):
    face = Image.new("RGBA", (width, height))
    draw = ImageDraw.Draw(face)
    font = semibold(size("font.button"))
    draw.line((0, height - 1, width - 1, height - 1), fill=color("button.border"))
    for index, (left, right) in enumerate(tab_cells(width, len(labels))):
        active = index == selected
        draw.text(((left + right) // 2, height // 2), labels[index], font=font, anchor="mm",
                  fill=color("text.white" if active else "text.muted"))
        if active:
            draw.rectangle((left, height - size("tab_bar.indicator_height"), right - 1, height - 1), fill=color("accent.cyan"))
    return face


def tab_bar(name, labels, rect, selected_handle, navigation_handle, skins_dir, gates=()):
    x, y, width, height = rect
    count = len(labels)
    filename = name.replace(" ", "_").lower() + "_tabs.png"
    if skins_dir:
        film = Image.new("RGBA", (width, height * count))
        for index in range(count):
            film.paste(render_tab_bar(labels, index, width, height), (0, index * height))
        film.save(Path(skins_dir) / filename)
    data = {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": filename,
            "numFrames": count, "handleName": "Data"}
    yield entry(name + " Tabs", "Image", data, rect, (("Data", selected_handle),), gates, focus="No")
    for index, (left, right) in enumerate(tab_cells(width, count)):
        yield button(f"{name} {labels[index]} Peek", (x + left, y, right - left, height), TRANSPARENT_HIT,
                     index=index, count=count, remaps=(("Data", navigation_handle),), gates=gates)
