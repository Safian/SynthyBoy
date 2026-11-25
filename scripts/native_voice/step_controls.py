# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Voice sequencer bindings for the shared bipolar dial."""
import json
from pathlib import Path

from components.native.button.button import entry

_LAYOUT = json.loads(Path(__file__).with_name("voice_layout.json").read_text())["steps"]
VOICE_STEP_ROWS = ("PAN", "DETUNE", "CUTOFF", "ENV", "MOD 1", "MOD 2")
VOICE_STEP_ROW_KEYS = ("pan", "detune", "cutoff", "env", "mod1", "mod2")
VOICE_STEP_KNOB_PREFIX = "Voice Native Step Knob "
VOICE_STEP_KNOB_NAMES = tuple(
    f"{VOICE_STEP_KNOB_PREFIX}{row} {step}" for row in VOICE_STEP_ROWS for step in range(1, 9)
)


def voice_step_knob_rect(step):
    if not 1 <= step <= 8:
        raise ValueError("Voice step must be in 1..8")
    x, y, width, _ = _LAYOUT["rect"]
    pad, gap, diameter = _LAYOUT["pad"], _LAYOUT["gap"], _LAYOUT["diameter"]
    column = (width - 2 * pad - 7 * gap) // 8
    left = x + pad + (step - 1) * (column + gap) + (column - diameter) // 2
    return left, y + pad + _LAYOUT["top"], diameter, diameter


def build_voice_step_knobs(ctx, root_gate, row_handle):
    """Each row binds the same bipolar knob to eight sequence parameters."""
    entries = []
    for row, (label, key) in enumerate(zip(VOICE_STEP_ROWS, VOICE_STEP_ROW_KEYS)):
        row_gate = f"IndexedEnabling/{row}/{len(VOICE_STEP_ROWS)}/{row_handle}"
        for step in range(1, 9):
            param = ctx.param_index(f"voicectl.s{step}.{key}")
            entries.append(entry(
                f"{VOICE_STEP_KNOB_PREFIX}{label} {step}", "voiceNativeStepLevel",
                {"version": 1, "handleName": "Data"}, voice_step_knob_rect(step),
                remaps=[("Data", f"Parameter {param}")], gates=[root_gate, row_gate],
            ))
    return entries
