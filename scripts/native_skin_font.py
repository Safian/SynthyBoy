# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path

from PIL import ImageFont


def semibold(size):
    font = Path(__file__).resolve().parents[1] / "shell-mpc/overlay/fonts/TitilliumWeb-SemiBold.ttf"
    return ImageFont.truetype(str(font), size)
