#!/usr/bin/env python3
"""Check installed XL disabled controls against their native Value labels and live diagnostic values."""
import argparse
import hashlib
import json
import re
import shlex
import time
from collections import Counter
from contextlib import contextmanager
from pathlib import Path

from PIL import Image, ImageChops, ImageOps

from device import XL, Unavailable, navigate, recognise
from shard import MODELS, add_shard, selected
from inventory import VIEW_IDS, absolute_rect, inventory, locate, resolve


OSC = {"osca": ("Osc A", "osc1.on"), "oscb": ("Osc B", "osc2.on"), "oscc": ("Osc C", "osc3.on"),
       "oscsub": ("Sub", "sub.on"), "oscnoise": ("Noise", "noise.on")}


def disabled_name(name):
    return bool(re.search(r'(?:Off|Inactive|Disabled)(?:$|[0-9_ ])|(?:^|[ _])(?:off|disabled|inactive)(?:$|[ _])', name))


def candidates(tui):
    page = json.loads(tui.read_text())["pageData"]
    defs = {entry["key"]: entry["value"] for entry in page["componentDefinitions"]["localComponentDefinitions"]}
    replacements = {entry["key"]: entry["value"] for entry in page.get("replacementStrings", [])}
    rows = []

    def visit(item, path, parent, mapping, gates, group=None, occurrence=1):
        obj, bounds = item["componentData"], item["bounds"]
        kind, name = obj["type"], obj["name"]
        box = absolute_rect(bounds, parent)
        path = path + "/" + name + (f" [{occurrence}]" if occurrence > 1 else "")
        local = dict(mapping)
        local.update({e["key"]: resolve(e["value"], mapping) for e in item.get("handle remapping", {}).get("map", [])})
        gates = gates + [resolve(g, local) for g in bounds.get("additionalInvalidatingHandles", [])]
        declared = disabled_name(kind) or disabled_name(name)
        children = defs.get(kind, {}).get("componentsData", [])
        def contains_readout(child):
            obj = child["componentData"]
            if obj["type"] == "Label":
                return obj.get("data", {}).get("type") == "Value"
            # Monolithic TUI packages can intern a native Label in its own shared definition.
            leaves = defs.get(obj["type"], {}).get("componentsData", [])
            return (len(leaves) == 1 and leaves[0]["componentData"]["type"] == "Label" and
                    leaves[0]["componentData"].get("data", {}).get("type") == "Value")
        direct_values = any(contains_readout(child) for child in children)
        label_wrapper = len(children) == 1 and children[0]["componentData"]["type"] == "Label"
        if (declared and group is None) or (group is not None and direct_values and not label_wrapper):
            group = dict(id=path, view=path.split("/")[0], rect=box, gates=gates, values=[], art=[])
            rows.append(group)
        if kind in defs:
            occurrences = Counter()
            for child in defs[kind]["componentsData"]:
                child_name = child["componentData"]["name"]
                occurrences[child_name] += 1
                visit(child, path, box, local, gates, group, occurrences[child_name])
        elif group is not None:
            data = obj.get("data", {})
            h = resolve(data.get("handleName", ""), local)
            if kind == "Label" and data.get("type") == "Value":
                group["values"].append(dict(rect=box, handle=h, gates=gates, choices=replacements.get(h, [])))
            if kind == "Image" and data.get("image"):
                group["art"].append(dict(rect=box, asset=data["image"], frames=data.get("numFrames", 1),
                                         handle=h, gates=gates))

    for tab in page["tabs"]:
        for item in tab["componentDefinition"]["componentsData"]:
            visit(item, VIEW_IDS[tab["tabName"]], (0, 110, 1280, 628), {}, [])
    return rows


def crop(image, rect):
    x, y, w, h = rect
    if w < 1 or h < 1 or x < 0 or y < 110 or x+w > 1280 or y+h > 738:
        raise Unavailable(f"outside native editor: {rect}")
    return image.crop((x, y, x + w, y + h))


def normal(text):
    return "".join(text.upper().replace("−", "-").split())


def read_value(image, rect, expected):
    if not normal(expected):
        raise Unavailable("empty host display")
    region = crop(image, rect).convert("L")
    lo, hi = region.getextrema()
    if hi - lo < 12:
        raise AssertionError("empty native Value label")
    scaled = region.resize((4 * region.width, 4 * region.height), Image.Resampling.LANCZOS)
    # Separate light native text from the dark host field, keeping punctuation and decimal points.
    mask = scaled.point(lambda px: 255 if px >= lo + (hi - lo) * .52 else 0)
    box = mask.getbbox()
    if not box:
        raise AssertionError("empty native Value mask")
    enlarged = ImageOps.expand(ImageOps.invert(mask.crop(box)), border=18, fill=255)
    readings = []
    for mode in (7, 8):
        words = recognise(enlarged, mode=mode)
        readings.append(" ".join(row["text"] for row in words if row["text"].strip()))
        if normal(readings[-1]) == normal(expected):
            return readings[-1]
    raise AssertionError(f"expected native Value {expected!r}, read {readings!r}")


def delta(before, after):
    return {key: [before.get(key), after.get(key)] for key in before.keys() | after.keys()
            if before.get(key) != after.get(key)}


def gate_state(gates, values):
    from scalars import visible
    verdicts = [visible(g, values) for g in gates]
    return False if False in verdicts else None if None in verdicts else True


def native_fields(row, snapshot):
    fields = []
    for field in row["values"]:
        visible = gate_state(field["gates"], snapshot["vst"])
        if visible is None:
            raise Unavailable(f"unknown Value visibility: {field['gates']}")
        if not visible:
            continue
        match = re.fullmatch(r"Parameter (\d+)", field["handle"])
        if not match:
            raise Unavailable(f"unresolved Value: {field['handle']}")
        idx = int(match[1])
        if idx not in snapshot.get("display", {}):
            raise Unavailable(f"missing diagnostic display for {idx}")
        choices = field.get("choices")
        value = snapshot["display"][idx]
        if choices and idx in snapshot["vst"]:
            frame = int(snapshot["vst"][idx] * (len(choices) - 1) + .5)
            value = choices[frame]
        fields.append((field["rect"], value))
    if not fields:
        raise Unavailable("no visible data-bound native Value in packaged disabled variant")
    return fields


def verify_art(screen, art, skin, values):
    asset = Image.open(skin / art["asset"]).convert("RGBA")
    frames = art["frames"]
    if frames > 1:
        binding = re.fullmatch(r"Parameter (\d+)", art["handle"])
        if not binding or int(binding[1]) not in values or asset.height % frames:
            raise Unavailable(f"unresolved filmstrip {art['asset']}")
        frame = int(values[int(binding[1])] * (frames - 1) + .5)
        h = asset.height // frames
        asset = asset.crop((0, frame*h, asset.width, (frame+1)*h))
    observed = crop(screen, art["rect"]).convert("RGB")
    asset = asset.resize(observed.size, Image.Resampling.LANCZOS)
    pixels = [(a[:3], b) for a, b in zip(asset.getdata(), observed.getdata()) if a[3] >= 245]
    if len(pixels) < 16:
        raise Unavailable("insufficient opaque native art")
    # Opaque pixels with the packaged color must dominate. Compare independently of the pane behind it.
    fraction = sum(max(abs(a-b) for a, b in zip(left, right)) <= 15 for left, right in pixels) / len(pixels)
    if fraction < .85:
        raise AssertionError(f"native art {art['asset']} match fraction {fraction:.3f}")
    return round(fraction, 4)


def observe(dev, row, before, skin, record, output):
    fields = native_fields(row, before)
    image = dev.image()
    crop(image, row["rect"]).save(output)
    record["value_readouts"] = [read_value(image, box, value) for box, value in fields]
    illustrations = [a for a in row["art"] if gate_state(a["gates"], before["vst"]) is True]
    record["art_matches"] = {a["asset"]: verify_art(image, a, skin, before["vst"]) for a in illustrations}
    rect = next((a["rect"] for a in illustrations if a["frames"] > 1), row["rect"])
    x, y, w, h = rect
    x, y = x + w//2, y + h//2
    status = dev.status()
    record["observations"] = []
    for action in ("tap", "drag"):
        amount = 30 if y < 145 else -30
        if action == "tap":
            dev.gesture(x, y)
        else:
            dev.gesture(x, y, 0, amount)
        after = dev.snapshot()
        obs = {"action": action, "x": x, "y": y, "engine_changes": delta(before["engine"], after["engine"]),
               "vst_changes": delta(before["vst"], after["vst"])}
        record["observations"].append(obs)
        if obs["engine_changes"] or obs["vst_changes"]:
            raise AssertionError("disabled gesture changed plugin state")
        state = dev.status()
        if any(state.get(k) != status.get(k) for k in ("state", "view", "ui_depth")):
            raise AssertionError("disabled gesture changed native editor context")
        fresh = dev.image()
        # Catch host popup overlays which can leave the selected plugin page and all values unchanged.
        changed = ImageChops.difference(crop(image, (0,110,1280,628)), crop(fresh, (0,110,1280,628))).convert('L')
        if sum(changed.histogram()[24:]) > 1280 * 628 * .005:
            raise AssertionError("disabled gesture changed the native editor picture")
        for box, value in fields:
            read_value(fresh, box, value)


@contextmanager
def disabled_source(dev, view, baseline, controls):
    if view not in OSC:
        yield baseline
        return
    label, target = OSC[view]
    if baseline["engine"][target] == 0:
        yield baseline
        return
    widget = locate(controls, view, f"Osc Native {label} Power")
    x, y, w, h = widget["rect"]
    if gate_state(widget["gates"], baseline["vst"]) is not True:
        raise Unavailable("native oscillator power button is not visible")
    try:
        dev.gesture(x+w//2, y+h//2)
        powered_off = dev.snapshot(display=True)
        if powered_off["engine"][target] != 0 or set(delta(baseline["engine"], powered_off["engine"])) != {target}:
            raise AssertionError("native power gesture did not disable only the requested oscillator")
        yield powered_off
    finally:
        if dev.status().get("view") != view:
            raise RuntimeError("cannot restore oscillator power in changed editor context")
        state = dev.snapshot()
        if state["engine"][target] == 0:
            dev.gesture(x+w//2, y+h//2)
        restored = dev.snapshot()
        if delta(restored["engine"], baseline["engine"]):
            raise RuntimeError("oscillator test did not restore the baseline")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tui", type=Path, required=True)
    parser.add_argument("--installed-tui", default=None)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--host", default=None)
    parser.add_argument("--tool-dir", default=None)
    add_shard(parser)
    args = parser.parse_args()
    model = MODELS[args.device]
    views = selected(args, parser)
    args.host = args.host or model.host
    args.tool_dir = args.tool_dir or model.helper_dir
    args.installed_tui = args.installed_tui or model.tui
    args.out.mkdir(parents=True, exist_ok=True)
    dev = model.adapter(args.host, args.tool_dir)
    digest = hashlib.sha256(args.tui.read_bytes()).hexdigest()
    if dev.remote("sha256sum " + shlex.quote(args.installed_tui)).decode().split()[0] != digest:
        parser.error("installed and supplied TUI differ")
    tabs, controls = inventory(args.tui)
    rows = candidates(args.tui)
    counts = Counter()
    pid = dev.remote("pidof MPC").strip()
    if not pid:
        parser.error("missing MPC process")
    with (args.out / "disabled.jsonl").open("x") as journal:
        def emit(item, status, **details):
            counts[status] += 1
            journal.write(json.dumps(dict(**item, **details, status=status, local_unix_s=time.time())) + "\n")
            journal.flush()

        for view in views:
            candidates_here = [r for r in rows if r["view"] == view]
            try:
                if dev.remote("pidof MPC").strip() != pid:
                    raise RuntimeError("MPC process restarted")
                navigate(dev, tabs, view)
                state = dev.snapshot(display=True)
                if not state.get("display"):
                    raise Unavailable("diagnostic display strings unavailable")
                with disabled_source(dev, view, state, controls) as state:
                    visible_count = 0
                    visible_ids = []
                    for n, row in enumerate(candidates_here):
                        item = {"view": view, "id": row["id"]}
                        visibility = gate_state(row["gates"], state["vst"])
                        if visibility is not True:
                            emit(item, "HIDDEN" if visibility is False else "NOT_RUN", gates=row["gates"])
                            continue
                        if not row["values"]:
                            emit(item, "NOT_RUN", reason="no Value label in candidate")
                            continue
                        visible_count += 1
                        visible_ids.append(row["id"].lower())
                        try:
                            state = dev.snapshot(display=True)
                            if dev.status().get("view") != view or gate_state(row["gates"], state["vst"]) is not True:
                                raise Unavailable("editor context or visibility changed")
                            observe(dev, row, state, args.tui.parent, item, args.out / f"{view}-{n}.png")
                            emit(item, "PASS")
                        except Unavailable as exc:
                            emit(item, "NOT_RUN", reason=str(exc))
                        except Exception as exc:
                            emit(item, "FAIL", reason=str(exc))
                            if any(o.get("engine_changes") or o.get("vst_changes") for o in item.get("observations", [])):
                                raise RuntimeError("unexpected edit; stopped to preserve the test project") from exc
                    if visible_count == 0:
                        emit({"view": view}, "FAIL" if view in OSC else "NOT_RUN", reason="no visible disabled native Value controls")
                    if view in OSC:
                        missing = [name for name in ("pan", "level") if not any(name in path for path in visible_ids)]
                        if missing:
                            emit({"view": view}, "FAIL", reason=f"disabled source lacks native {missing} readout(s)")
            except Unavailable as exc:
                emit({"view": view}, "NOT_RUN", reason=str(exc))
            except Exception as exc:
                emit({"view": view}, "FAIL", reason=str(exc))
                if isinstance(exc, RuntimeError):
                    break
    (args.out/"disabled-summary.json").write_text(json.dumps({"counts":counts, "tui_sha256":digest}, indent=2))
    navigate(dev, tabs, "play1")
    return int(bool(counts["FAIL"] or counts["NOT_RUN"] or not counts["PASS"]))


if __name__ == "__main__":
    raise SystemExit(main())
