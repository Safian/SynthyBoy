# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared native MeterBlock compound component composing existing Meter.

Builds an integrated metering block combining:
- Independent native L/R peak meters from components/native/meter/meter.py; no RMS alias
- Floating Peak Hold indicator (configured with peakHoldSize and peakHoldColour)
- Calibrated audio dBFS scale markings (-60, -24, -12, -6, 0 dBFS)
- Honest CLIP state: only if an actual engine clip readout is available and calibrated;
  never invents clip latch or red flags from fader gain position.
"""

import copy
import math
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

from components.style import (
    argb as _style_argb,
    color as _style_color,
    rgb as _style_rgb,
    rgba as _style_rgba,
    size as _style_size,
)
from components.native.button.button import semibold
from components.native.meter.meter import (
    meter_data as _raw_meter_data,
    write_meter_assets,
    METER_ASSETS,
)

TYPE_METER_BLOCK_SOURCE = "mixerMeterBlockSource"
TYPE_METER_BLOCK_BUS = "mixerMeterBlockBus"


def dbfs_to_normalized(db: float) -> float:
    """Maps dBFS onto [0, 1] matching OwnEngine::writeReadout (1 + db / 60 for -60..0 dBFS)."""
    return max(0.0, min(1.0, 1.0 + float(db) / 60.0))


def calibrated_dbfs_marks() -> List[Tuple[float, str, bool]]:
    """Returns calibrated engine-matched meter tick marks (norm, label, is_zero)."""
    return [
        (dbfs_to_normalized(0.0), "0", True),
        (dbfs_to_normalized(-6.0), "-6", False),
        (dbfs_to_normalized(-12.0), "-12", False),
        (dbfs_to_normalized(-24.0), "-24", False),
        (dbfs_to_normalized(-48.0), "-48", False),
        (dbfs_to_normalized(-60.0), "-60", False),
    ]


def build_meter_block_definition(
    def_name: str,
    width: Optional[int] = None,
    height: Optional[int] = None,
    hold: bool = True,
    with_clip: bool = True,
) -> Dict[str, Any]:
    """Build the mono Peak/Hold block or the Mixer's calibrated stereo block with clip."""
    from components.native.meter.meter import stereo_meter_assets
    w = _style_size("mixer_strip.meter.width") if width is None else width
    h = _style_size("mixer_strip.fader.height_source") if height is None else height
    clear = {"version": 1, "colour": _style_argb("ink.black", 0), "image": ""}
    components = []

    def add_meter(name, rect, assets, peak, peak_hold=""):
        data = _raw_meter_data(direction="Up", hold=bool(peak_hold), assets=assets)
        data.update(peakHandle=peak, peakHoldHandle=peak_hold)
        components.append({
            "version": 2,
            "componentData": {"version": 1, "name": name, "type": "Meter", "data": data},
            "handle remapping": {"version": 1, "map": [{"key": key, "value": key} for key in (peak, peak_hold) if key]},
            "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide",
                       "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % rect},
        })

    if with_clip:
        channel_w = _style_size("meter.stereo.channel_width")
        gap = _style_size("meter.stereo.channel_gap")
        clip_h = _style_size("meter.stereo.clip_height")
        meter_y = clip_h + _style_size("meter.stereo.clip_gap")
        signal_h = h - meter_y
        for channel, x in (("L", 0), ("R", channel_w + gap)):
            add_meter("Meter " + channel, (x, meter_y, channel_w, signal_h), stereo_meter_assets(signal_h),
                      "Peak" + channel, "Hold" + channel if hold else "")
            add_meter("Clip " + channel, (x, 0, channel_w, clip_h),
                      ("mixer_meter_clip_idle.png", "mixer_meter_clip_peak.png"), "Clip" + channel)
    else:
        add_meter("Meter", (0, 0, w, h), METER_ASSETS, "Peak", "Hold" if hold else "")
    return {"version": 2, "actions": [],
            "backgroundData": {"version": 1, "focussed": clear.copy(), "unfocussed": clear.copy()},
            "ignoreMousePresses": True, "disableCoarseDataWheel": False, "componentsData": components}


def create_meter_block_instance(
    name: str,
    def_name: str,
    rect: Tuple[int, int, int, int],
    peak_handle: Union[int, str],
    hold_handle: Optional[Union[int, str]],
    clip_handle: Optional[Union[int, str]] = None,
    gates: Sequence[str] = (),
    focus: str = "No",
    *,
    peak_right_handle: Optional[Union[int, str]] = None,
    hold_right_handle: Optional[Union[int, str]] = None,
    clip_right_handle: Optional[Union[int, str]] = None,
) -> Dict[str, Any]:
    """Instantiates a native MeterBlock with remapped Peak/Hold handles."""
    if peak_right_handle is None:
        if any(handle is not None for handle in (clip_handle, clip_right_handle, hold_right_handle)):
            raise ValueError("Stereo clip/hold bindings require a right peak readout")
        values = {"Peak": peak_handle, "Hold": hold_handle}
    else:
        values = {"PeakL": peak_handle, "PeakR": peak_right_handle,
                  "HoldL": hold_handle, "HoldR": hold_right_handle,
                  "ClipL": clip_handle, "ClipR": clip_right_handle}
    remaps = [{"key": key, "value": f"Parameter {value}" if isinstance(value, int) else str(value)}
              for key, value in values.items() if value is not None]

    bounds = {
        "version": 2 if gates else 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "%d %d %d %d" % rect,
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)

    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": def_name, "data": {}},
        "handle remapping": {"version": 1, "map": remaps},
        "bounds": bounds,
    }


def render_meter_scale(
    draw: Any,
    x: int,
    y: int,
    width: int,
    height: int,
    font: Optional[Any] = None,
) -> None:
    """Draws engine-calibrated dBFS ticks and labels beside meter track."""
    pad = _style_size("mixer_strip.meter.scale_pad")
    for norm, label, is_zero in calibrated_dbfs_marks():
        my = int(round(y + height - pad - norm * (height - 2 * pad)))
        col = _style_rgb("accent.red") if is_zero else _style_rgb("button.border")
        draw.line([(x, my), (x + width - 1, my)], fill=col, width=1)
        if font and label:
            tcol = _style_rgb("text.value") if is_zero else _style_rgb("preview.annotation")
            draw.text((x + width + 2, my), label, font=font, fill=tcol, anchor="lm")


__all__ = [
    "TYPE_METER_BLOCK_SOURCE",
    "TYPE_METER_BLOCK_BUS",
    "METER_ASSETS",
    "dbfs_to_normalized",
    "calibrated_dbfs_marks",
    "build_meter_block_definition",
    "create_meter_block_instance",
    "render_meter_scale",
    "write_meter_assets",
]


def render_stereo_meter(height, values):
    """Offline composition of the same full-size SDK Meter images and read-only fields."""
    from components.native.meter.meter import stereo_meter_images
    width = _style_size('mixer_strip.meter.width')
    channel_w = _style_size('meter.stereo.channel_width')
    gap = _style_size('meter.stereo.channel_gap')
    clip_h = _style_size('meter.stereo.clip_height')
    offset_y = clip_h + _style_size('meter.stereo.clip_gap')
    signal_h = height - offset_y
    out = Image.new('RGBA', (width, height), _style_rgb('surface.control'))
    for channel, x in (('L', 0), ('R', channel_w + gap)):
        off, on = stereo_meter_images(signal_h)
        peak = max(0.0, min(1.0, float(values.get('Peak' + channel, 0))))
        hold = max(0.0, min(1.0, float(values.get('Hold' + channel, 0))))
        visible = round(peak * signal_h)
        if visible: off.paste(on.crop((0, signal_h - visible, channel_w, signal_h)), (0, signal_h - visible))
        d = ImageDraw.Draw(off)
        if hold > 0:
            line_y = max(0, min(signal_h - 1, round((1.0 - hold) * signal_h)))
            d.rectangle((0, line_y, channel_w - 1, min(signal_h - 1, line_y + _style_size('meter.peak_hold.height') - 1)), fill=_style_rgb('text.icon'))
        out.alpha_composite(off, (x, offset_y))
        clip = float(values.get('Clip' + channel, 0)) > 0
        out.alpha_composite(stereo_meter_images(clip_h, clip=True)[int(clip)], (x, 0))
    return out
