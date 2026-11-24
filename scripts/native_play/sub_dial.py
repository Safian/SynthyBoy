# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native Play/Sub dials with a 90-pixel SynthyBoy side-knob filmstrip."""
import sys
from pathlib import Path

_ROOT = str(Path(__file__).resolve().parents[2])
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from components.native.level.level import SIZE, FRAMES, _value_label
import components.native.level.level as _level
import components.native.pan.pan as _pan


def write_strip(path, bipolar, frame_size=SIZE):
    if bipolar:
        _pan.write_strip(path, frame_size=frame_size, bipolar=True)
    else:
        _level.write_strip(path, frame_size=frame_size, bipolar=False)


def synthyboy_dial_data(skins_dir, *, bipolar, frame_size=SIZE):
    if bipolar:
        return _pan.synthyboy_dial_data(skins_dir, frame_size=frame_size, bipolar=True)
    else:
        return _level.synthyboy_dial_data(skins_dir, frame_size=frame_size, bipolar=False)


def sub_dial_definition(template, skins_dir, *, bipolar):
    if bipolar:
        return _pan.sub_dial_definition(template, skins_dir, bipolar=True)
    else:
        return _level.sub_dial_definition(template, skins_dir, bipolar=False)
