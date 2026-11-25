# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Host-native step selection and readouts for the Voice sequence."""
from components import style
from components.native.button.button import button, entry, write_transparent_hit
from components.native.name_value.name_value import _label_component
from components.native.selector.selector import passive_layer
from components.native.radio_button.radio_button import segmented_radio_entry
from .step_controls import _LAYOUT, VOICE_STEP_ROWS, VOICE_STEP_ROW_KEYS

_STEPS = range(1, 9)
_HEADERS = "Voice Step Buttons"
_FOOTERS = "Voice Step Value Buttons"
_COLUMNS = "Voice Column Values"
_STRIP = "Voice Selected Values"
SEQUENCE_READOUT_NAMES = {
    _HEADERS, _FOOTERS, *(f"{_COLUMNS} {row}" for row in VOICE_STEP_ROWS),
    *(f"{_STRIP} {step}" for step in _STEPS),
}


def _label(handle, rect, *, kind="Value"):
    return _label_component(handle, kind, handle, rect, style.size("font.value"),
                            style.argb("text.caption" if kind == "Name" else "text.value"),
                            "horizontallyCentred verticallyCentred")


def _geometry():
    x, y, w, _ = _LAYOUT["rect"]
    pad, gap = _LAYOUT["pad"], _LAYOUT["gap"]
    inner = w - 2 * pad
    column = (inner - 7 * gap) // 8
    return x + pad, y + pad, inner, column, column + gap


def _footer_height():
    return _LAYOUT["rect"][3] - 2 * _LAYOUT["pad"] - _LAYOUT["top"] - _LAYOUT["diameter"]


def sequence_readout_definitions(skins):
    _, _, _, width, pitch = _geometry()
    height = _LAYOUT["top"]
    buttons = [segmented_radio_entry(str(s), str(s), ((s-1)*pitch, 0, width, height),
                                     s-1, 8, None, skins) for s in _STEPS]
    columns = [_label(f"Step{s}", ((s-1)*pitch, 0, width, height)) for s in _STEPS]
    hit_art = write_transparent_hit(skins)
    footers = [button(str(s), ((s - 1) * pitch, 0, width, _footer_height()), hit_art,
                      index=s - 1, count=len(_STEPS)) for s in _STEPS]
    _, _, w, h = style.size("screen.voice_tab.rect.rect.2")
    step_width = w // 10
    cell_width = (w - step_width) // len(VOICE_STEP_ROWS)
    strip = [_label("Step", (0, 0, step_width, h))]
    for i, key in enumerate(VOICE_STEP_ROW_KEYS):
        left = step_width + i * cell_width
        half = cell_width // 2
        strip.extend((_label(f"Name{i}", (left, 0, half, h), kind="Name"),
                      _label(key, (left + half, 0, cell_width-half, h))))
    return {"voiceStepButtons": dict(passive_layer(buttons), ignoreMousePresses=False),
            "voiceStepValueButtons": dict(passive_layer(footers), ignoreMousePresses=False),
            "voiceColumnValues": passive_layer(columns), "voiceSelectedValues": passive_layer(strip)}


def sequence_readout_entries(ctx, root_gate, row_handle, step_handle):
    x, y, inner, _, _ = _geometry()
    top = _LAYOUT["top"]
    yield entry(_HEADERS, "voiceStepButtons", {"version": 1, "handleName": "Data"},
                (x, y, inner, top), [("Data", step_handle)], [root_gate])
    def model(step, key):
        return f'Parameter {ctx.param_index(f"voicectl.s{step}.{key}")}'

    def group(name, kind, rect, mappings, gate):
        return entry(name, kind, {"version": 1, "handleName": "Data"}, rect, mappings,
                     [root_gate, gate], focus="No")

    rect = (x, y + top + _LAYOUT["diameter"] + _LAYOUT["value_gap"], inner, top)
    for i, (row, key) in enumerate(zip(VOICE_STEP_ROWS, VOICE_STEP_ROW_KEYS)):
        mappings = [("Data", row_handle), *((f"Step{s}", model(s, key)) for s in _STEPS)]
        yield group(f"{_COLUMNS} {row}", "voiceColumnValues", rect, mappings,
                    f"IndexedEnabling/{i}/6/{row_handle}")
    yield entry(_FOOTERS, "voiceStepValueButtons", {"version": 1, "handleName": "Data"},
                (x, y + top + _LAYOUT["diameter"], inner, _footer_height()),
                [("Data", step_handle)], [root_gate], focus="No")
    for s in _STEPS:
        mappings = [("Data", step_handle), ("Step", step_handle)]
        for i, (row, key) in enumerate(zip(VOICE_STEP_ROWS, VOICE_STEP_ROW_KEYS)):
            handle = model(s, key)
            mappings.extend(((f"Name{i}", f"WithName[{row}]/{handle}"), (key, handle)))
        yield group(f"{_STRIP} {s}", "voiceSelectedValues", style.size("screen.voice_tab.rect.rect.2"),
                    mappings, f"IndexedEnabling/{s-1}/8/{step_handle}")
