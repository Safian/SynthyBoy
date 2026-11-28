"""Regenerate the navigation cases in cases.json from native MPC handle definitions."""
import json
from functools import lru_cache
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import native_ui_params as layout


def tap(control):
    return {"op": "tap", "control": control}


def point(x, y):
    return {"op": "tap", "x": x, "y": y}


@lru_cache(maxsize=1)
def native_base():
    manifest = json.loads((HERE.parents[1] / "docs/params.json").read_text())
    return layout.native_base_index(len(manifest["params"]))


def vst(offset, value):
    return {"op": "expect", "vst": {str(native_base() + offset): value}}


def top(view, value):
    return {"op": "expect", "status": {"view": view, "page_top": str(value)}}


def case(name, view, actions, cleanup=()):
    return {"id": "route." + name, "view": view,
            "steps": [{"op": "snapshot", "name": "baseline"}, *actions,
                      {"op": "expect", "target": "*", "change": "unchanged"}],
            "restore": [*cleanup, {"op": "navigate", "view": "play1"}]}


def cases():
    modes = ("ALL", "BY SOURCE", "BY DEST")
    def mode(name):
        return [tap(f"mod/Mod Native Mode {name}/mode"), vst(layout.MOD_NAVIGATION, (modes.index(name) + 1) / 3)]
    for start in modes:
        for end in modes:
            if start != end:
                name = f"mod.{start}.{end}".lower().replace(" ", "-")
                yield case(name, "mod", [*mode(start), *mode(end)], mode("ALL"))

    # A detail's Delete button first arms its confirmation. Back must leave the route intact.
    yield case("mod.delete-cancel", "mod",
               [*mode("ALL"), point(1180, 240), top("mod", 20),
                tap("mod/Mod Native Detail Delete/Delete unarmed"), vst(layout.MOD_DETAIL_CONFIRM, 1),
                tap("mod/Mod Native Detail Back"), top("mod", 0)])

    # Test the first two chain cards against their individual selection readbacks.
    # The first 16 chain-select handles publish one bit per card independently of effect power.
    def fx_slot(index):
        return tap(f"fx/FX Native Surface/FX fxNativeChainRow/Slot {index+1}/select")

    for index in (0, 1):
        other = 1 - index
        yield case(f"fx.select-slot-{index+1}", "fx",
                   [fx_slot(other),
                    vst(layout.FX_CHAIN + 2 + other, 1), fx_slot(index),
                    vst(layout.FX_CHAIN + 2 + index, 1), vst(layout.FX_CHAIN + 2 + other, 0)],
                   [fx_slot(0)])


def update(path):
    data = [row for row in json.loads(path.read_text()) if not row["id"].startswith("route.")]
    project = next(row for row in data if row["id"] == "project.copy.save-reload")
    data.remove(project)
    data.extend(cases())
    data.append(project)
    sections = []
    for row in data:
        fields = [f'    "{key}": {json.dumps(row[key])}' for key in ("id", "view")]
        for key in ("steps", "restore"):
            actions = ",\n".join("      " + json.dumps(step, ensure_ascii=False) for step in row[key])
            fields.append(f'    "{key}": [\n{actions}\n    ]')
        sections.append("  {\n" + ",\n".join(fields) + "\n  }")
    path.write_text("[\n" + ",\n".join(sections) + "\n]\n")


if __name__ == "__main__":
    update(HERE / "cases.json")
