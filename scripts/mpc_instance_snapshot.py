# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Read the complete parameter mirror and factory identity emitted by MPC playDump()."""
import json
import math
import re


def read_instance_snapshot(text, expected):
    # playDump() appends the project chunk after the parameter mirror.
    marker = re.search(r"^chunk ([0-9]+)\n", text, re.MULTILINE)
    program = re.findall(r"^program ([0-9]+) protected -?[0-9]+ window [01]$", text, re.MULTILINE)
    if marker is None or len(program) != 1:
        raise ValueError("Instance dump is missing a unique program header or project chunk")
    chunk = text[marker.end():].removesuffix("\n")
    if len(chunk.encode("utf-8")) != int(marker[1]):
        raise ValueError("Instance dump contains a truncated project chunk")
    state = json.loads(chunk)
    index = int(program[0])
    if state["mpcProgram"] != index:
        raise ValueError("Instance dump program and project chunk disagree")

    params = {}
    for line in text[:marker.start()].splitlines():
        if line.startswith("p "):
            _, ordinal, key, value = line.split()
            position = int(ordinal)
            if not 0 <= position < len(expected) or expected[position] != key or key in params:
                raise ValueError(f"Invalid parameter index or duplicate key in instance dump: {line}")
            reading = float(value)
            if not math.isfinite(reading):
                raise ValueError("Non-finite parameter reading")
            params[key] = reading
    if len(params) != len(expected):
        raise ValueError("Incomplete parameter mirror")
    name = state.get("mpcProgramName")
    if not isinstance(name, str) or not name.strip() or (index == 0 and name != "Init"):
        raise ValueError("Instance dump has no restorable factory identity")
    return index, name, params
