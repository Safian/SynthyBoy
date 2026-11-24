# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared MPC Meter artwork and data model component."""
import struct

from components.style import (
    argb as _style_argb,
    rgb as _style_rgb,
    size as _style_size,
)
import zlib

METER_ASSETS = ("mixer_meter_idle.png", "mixer_meter_peak.png")


def meter_data(direction="Up", hold=True, assets=METER_ASSETS):
    return {
        "version": 3,
        "direction": direction,
        "invert": False,
        "inactiveImage": assets[0],
        "peakImage": assets[1],
        "rmsImage": "",
        "peakHandle": "Peak",
        "rmsHandle": "",
        "peakHoldHandle": "Hold" if hold else "",
        "peakHoldSize": _style_size('meter.peak_hold.height'),
        "peakHoldColour": _style_argb("text.icon"),
    }


def write_meter_assets(directory):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    for filename, colour in zip(METER_ASSETS, (_style_rgb("meter.idle"), _style_rgb("meter.peak"))):
        width, height = _style_size('meter.width'), _style_size('meter.height')
        rows = (b"\0" + bytes(colour) * width) * height
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        (directory / filename).write_bytes(png + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


__all__ = [
    "METER_ASSETS",
    "meter_data",
    "write_meter_assets",
]


def stereo_meter_images(height, clip=False):
    """SDK Meter full-size images. Peak reveals these pixels; this is not a filmstrip.

    -60..0 dBFS, green below -6, amber -6..-1, red only -1..0. No RMS is supplied.
    """
    from PIL import Image, ImageDraw
    width = _style_size('meter.stereo.channel_width')
    if clip:
        return (Image.new('RGBA', (width, height), _style_rgb('meter.idle')),
                Image.new('RGBA', (width, height), _style_rgb('accent.red')))
    idle = Image.new('RGBA', (width, height), _style_rgb('surface.control'))
    peak = idle.copy()
    off, on = ImageDraw.Draw(idle), ImageDraw.Draw(peak)
    segment = _style_size('meter.stereo.segment_height')
    gap = _style_size('meter.stereo.segment_gap')
    for bottom in range(height - 1, -1, -(segment + gap)):
        top = max(0, bottom - segment + 1)
        db = -60.0 + 60.0 * (height - 1 - (top + bottom) / 2) / max(1, height - 1)
        colour = 'accent.red' if db >= -1.0 else 'meter.warning' if db >= -6.0 else 'meter.peak'
        off.rectangle((0, top, width - 1, bottom), fill=_style_rgb('meter.idle'))
        on.rectangle((0, top, width - 1, bottom), fill=_style_rgb(colour))
    return idle, peak


def stereo_meter_assets(height):
    return (f'mixer_meter_{height}_idle.png', f'mixer_meter_{height}_peak.png')


def write_stereo_meter_assets(directory, height):
    from pathlib import Path
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    cap = _style_size('meter.stereo.clip_height')
    signal_height = height - cap - _style_size('meter.stereo.clip_gap')
    for filename, image in zip(stereo_meter_assets(signal_height), stereo_meter_images(signal_height)):
        image.save(directory / filename)
    for filename, image in zip(('mixer_meter_clip_idle.png', 'mixer_meter_clip_peak.png'), stereo_meter_images(cap, clip=True)):
        image.save(directory / filename)
