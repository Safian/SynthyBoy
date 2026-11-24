"""Rebuild Mixer furniture while its historical reference is still retained."""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "scripts/mpc-skin-extras/overlay-backgrounds"


def render(view, reference, left, top, height, sample_x):
    image = Image.open(ROOT / "docs/screens" / reference).convert("RGB")
    width = image.width
    page = image.crop((0, 110, 1, 738)).resize((width, 628), Image.Resampling.NEAREST)
    right = width - left
    # Copy the frame corners from the top/bottom, and the bare outer edge on
    # each side. Children start four pixels inside the FX chain bar.
    for y in range(top, top + height):
        edge = 3 if top + 4 <= y < top + height - 4 else 10
        row = image.crop((sample_x, y, sample_x + 1, y + 1)).resize((right - left, 1))
        row.paste(image.crop((left, y, left + edge, y + 1)), (0, 0))
        row.paste(image.crop((right - edge, y, right, y + 1)), (right - left - edge, 0))
        page.paste(row, (left, y - 110))
    page.save(ASSETS / f"overlay_bg_{view}.png")


if __name__ == "__main__":
    render("mixer", "mixer_old.jpg", 6, 114, 56, 24)
