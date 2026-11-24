# SynthyBoy - prune only unused engine choice tables from the final overlay skin.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import re


PARAMETER = re.compile(r"\bParameter (\d+)\b")
INCREMENT = re.compile(r"\bParameter \{Increment:([^}]+)\}")


def _strings(node):
    if isinstance(node, str):
        yield node
    elif isinstance(node, dict):
        for value in node.values():
            yield from _strings(value)
    elif isinstance(node, list):
        for item in node:
            yield from _strings(item)


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    page = tui["pageData"]
    tables = page.get("replacementStrings", [])
    used = set()
    max_repeats = max((item["value"].get("repeats", 1) for item in
                       page["componentDefinitions"]["localComponentDefinitions"]), default=1)
    for name, value in page.items():
        if name == "replacementStrings":
            continue
        for handle in _strings(value):
            # A form we cannot interpret makes pruning unsafe.
            if "Parameter *" in handle or "Parameter {" in INCREMENT.sub("", handle):
                return
            used.update(int(match) for match in PARAMETER.findall(handle))
            for expression in INCREMENT.findall(handle):
                numbers = dict(re.findall(r"(Base|Step|Offset)=(-?\d+)", expression))
                if "Base" not in numbers:
                    return
                for copy in range(max_repeats):
                    used.add(max(0, int(numbers["Base"]) + copy * int(numbers.get("Step", 1))
                                 + int(numbers.get("Offset", 0))))

    # Q-Link files use numeric indices outside TUI.json. Preserve their choice tables too.
    for tab in qlinks.get("Screen Mode Q-Links", {}).get("map", []):
        used.update(value for value in tab.get("Q-Links", {}).values() if isinstance(value, int))

    def reachable(table):
        direct = PARAMETER.fullmatch(table["key"])
        if direct is None:
            return True
        index = int(direct.group(1))
        return index >= ctx.param_count or index in used

    tables[:] = [table for table in tables if reachable(table)]
