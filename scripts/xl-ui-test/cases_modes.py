"""Regenerate the navigation cases in cases.json from native MPC handle definitions."""
import json
from functools import lru_cache
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import native_ui_params as layout


def tap(control):
    return {"op": "tap", "control": control}


def point(x, y):
    return {"op": "tap", "x": x, "y": y}


@lru_cache(maxsize=1)
def native_base():
    manifest = json.loads((HERE.parents[1] / "docs/params.json").read_text())
    return layout.mixer_peek_index(len(manifest["params"]))


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
    tabs = ("MIXER", "FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2")

    def select(tab):
        index = tabs.index(tab)
        return [tap(f"mixer/Mixer Native {tab} Peek"), vst(layout.MIXER_ACTIVE_TAB, index / 5),
                top("mixer", 26 if index else 0)]

    # Check every direction. Returning to Mixer does not exercise a dock-to-dock transition.
    for start in tabs:
        for end in tabs:
            if start == end:
                continue
            name = f"mixer.{start}.{end}".lower().replace(" ", "-")
            yield case(name, "mixer", [*select(start), *select(end)], select("MIXER"))

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
                tap("mod/Mod Native Detail Delete/Delete unarmed"), vst(layout.MOD_DETAIL_RESERVED + 5, 1),
                tap("mod/Mod Native Detail Back"), top("mod", 0)])

    for number, x in ((1, 37), (2, 95)):
        for caption, cx, frame in (("note", 862, 3), ("velo", 1044, 4)):
            # Choose the filter and independent NOTE/VELO curve on the 60/40 split.
            expand = f"filter1/Filter Native Split Right {caption.title()}/Filter Native Split Expand Curve"
            close = "filter1/Filter Native Curve Surface/Filter Native Collapse Curve"
            yield case(f"filter.f{number}.{caption}.expand-collapse", "filter1",
                       [point(x, 154), vst(layout.FILTER_STATE_OFFSET, number / 4),
                        point(cx, 154), tap(expand), vst(layout.FILTER_STATE_OFFSET, frame / 4),
                        tap(close), vst(layout.FILTER_STATE_OFFSET, number / 4)],
                       [point(37, 154), point(862, 154)])

    # Test the first two chain cards against their individual selection readbacks.
    # The first 16 chain-select handles publish one bit per card independently of effect power.
    for index in (0, 1):
        other = 1 - index
        yield case(f"fx.select-slot-{index+1}", "fx",
                   [point(100 + other * 68, 206),
                    vst(layout.FX_CHAIN + 2 + other, 1), point(100 + index * 68, 206),
                    vst(layout.FX_CHAIN + 2 + index, 1), vst(layout.FX_CHAIN + 2 + other, 0)],
                   [point(100, 206)])


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
