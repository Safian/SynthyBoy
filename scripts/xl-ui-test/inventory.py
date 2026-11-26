"""Resolve actions, controls, bounds and parameter mappings from the installed TUI's definition tree."""
import json
import re
from collections import Counter
from functools import lru_cache
from itertools import product
from pathlib import Path


VIEW_IDS = {"Play": "play1", "Macros": "macros", "Osc A": "osca", "Osc B": "oscb", "Osc C": "oscc",
            "Sub": "oscsub", "Noise": "oscnoise", "Env 1": "env1", "Env 2": "env2", "Env 3": "env3", "Env 4": "env4", "Env 5": "env5", "Env 6": "env6", "LFO 1": "lfo1", **{"LFO %d" % n: "lfo%d" % n for n in range(2, 11)}, **{"RND %d" % m: "rnd%d" % m for m in range(1, 5)}, "FX": "fx",
            "Mix": "mixer", "Filter 1": "filter1", "Filter 2": "filter2", "Note": "filternote", "Velo": "filtervelo", "Mod": "mod", "Master": "master", "Tuning": "tuning", "Voice": "voice"}
PASSIVE = {"Label", "Image", "Meter", "Indicator", "Focus"}
ACTUATORS = {"Button", "Knob", "Slider", "MenuOverlay", "KnobOverlay", "NumericOverlay"}


INCREMENT = re.compile(r"\{Increment:Base=(-?\d+)(?:,Step=(-?\d+))?(?:,Offset=(-?\d+))?\}")


def resolve(value, mapping, copy_index=None):
    if copy_index is not None and "{Increment:" in value:
        def increment(match):
            base, step, offset = match.groups()
            return str(max(0, int(base) + copy_index * int(step or 1) + int(offset or 0)))
        value = INCREMENT.sub(increment, value)
    # A composed SDK handle applies its adapters from left to right to the final data handle.
    head, sep, tail = value.rpartition("/")
    return head + sep + mapping.get(tail, tail)


def absolute_rect(bounds, parent):
    px, py, pw, ph = parent
    if bounds.get("boundsType") == "Relative":
        variables = {"left": 0, "top": 0, "right": pw, "bottom": ph, "width": pw, "height": ph}
        def coordinate(expression):
            match = re.fullmatch(r"parent\.(\w+)(?:\s*([+-])\s*(\d+(?:\.\d+)?))?", expression.strip())
            if match:
                axis, sign, offset = match.groups()
                return variables[axis] + (-1 if sign == "-" else 1) * float(offset or 0)
            return float(expression)
        left, top, right, bottom = [coordinate(v) for v in bounds["relativeBounds"].split(",")]
        return tuple(round(v) for v in (px + left, py + top, right - left, bottom - top))
    raw = tuple(float(v) for v in bounds["bounds"].split())
    if len(raw) != 4:
        raise ValueError("invalid TUI bounds: " + repr(bounds))
    x, y, w, h = raw
    if bounds.get("boundsType") == "Proportional":
        # The host truncates each proportional offset and size separately. This is visible
        # on the 216x128 Sub dial: 34.56px starts at pixel 34, while 146.88px occupies 146.
        return px + int(x*pw), py + int(y*ph), int(w*pw), int(h*ph)
    return tuple(round(v) for v in (px + x, py + y, w, h))


def inventory(tui_path, *, visual_leaves=False, skin_dir=None, popup=None):
    page = json.loads(Path(tui_path).read_text())["pageData"]
    defs = {d["key"]: d["value"] for d in page["componentDefinitions"]["localComponentDefinitions"]}
    tabs, controls = {}, []
    art_path = Path(skin_dir) if skin_dir is not None else Path(tui_path).parent

    @lru_cache(maxsize=None)
    def draws_pixels(filename):
        from PIL import Image
        if not (art_path / filename).is_file():
            return None
        with Image.open(art_path / filename) as image:
            return image.convert("RGBA").getchannel("A").getextrema()[1] != 0

    def visit(defn, view, path, parent, mapping, gates, ancestors, owner, disabled=False):
        counts = Counter()
        for copy_index, (slot, item) in product(range(defn.get("repeats", 1)), enumerate(defn.get("componentsData", []))):
            obj, bounds = item["componentData"], item["bounds"]
            name, kind = obj["name"], obj["type"]
            counts[name] += 1
            suffix = f" [{counts[name]}]" if counts[name] > 1 else ""
            identity = path + "/" + name + suffix
            box = absolute_rect(bounds, parent)
            if copy_index:
                x, y, w, h = box
                box = (x + copy_index * w, y, w, h)
            own = {e["key"]: resolve(e["value"], mapping, copy_index) for e in item.get("handle remapping", {}).get("map", [])}
            local = dict(mapping, **own)
            enabled = gates + [resolve(g, local, copy_index) for g in bounds.get("additionalInvalidatingHandles", [])]
            data = obj.get("data", {})
            sub = defs.get(kind)
            actions = (sub or data).get("actions", [])
            inert = disabled or data.get("ignoreMousePresses") is True or data.get("dragOrientation") == "None"
            disabled_variant = re.search(r"(Off|Inactive|Disabled)(?:$|[0-9_ ])|(?:^|[ _])(off|disabled|inactive)(?:$|[ _])", kind+" "+name)
            if sub and disabled_variant and sub.get("ignoreMousePresses") and sub.get("disableCoarseDataWheel") and not actions:
                inert = True
            if actions or (sub is None and (kind not in PASSIVE or visual_leaves)):
                handles = {k: resolve(v, local, copy_index) for k, v in data.items()
                           if isinstance(v, str) and (k == "handleName" or k.endswith("Handle"))}
                controls.append({"id": identity, "view": view, "type": kind, "rect": box, "gates": enabled,
                                 "handles": handles, "maps": own, "actions": actions,
                                 "data": {k: data[k] for k in ("buttonId", "numButtonsInGroup", "gestureBehaviour") if k in data},
                                 "passive": inert, "known_type": kind in ACTUATORS or bool(actions)})
                if visual_leaves:
                    controls[-1]["definition"] = owner
                    controls[-1]["local_leaf"] = (slot, name)
                    controls[-1]["visual_leaf"] = sub is None and kind in {"Button", "Knob", "Slider", "Label", "Meter", "Image", "Indicator"}
                    if data.get("knobType") == "FilmStrip" and not data.get("invert"):
                        controls[-1]["filmstrip"] = data["filmStrip"], data["numFrames"]
                    elif data.get("imageType") == "FilmStrip":
                        controls[-1]["filmstrip"] = data["image"], data["numFrames"]
                    if kind == "Button":
                        if data.get("offImage") and data.get("onImage"):
                            controls[-1]["button_art"] = data["offImage"], data["onImage"]
                        assets = {data[key] for key in ("onImage", "offImage") if data.get(key)}
                        visibility = [draws_pixels(name) for name in assets]
                        controls[-1]["visual_leaf"] = True in visibility
                        if None in visibility and True not in visibility:
                            controls[-1]["unknown_art"] = True
            if sub is not None:
                if kind in ancestors:
                    raise ValueError("recursive component: " + identity)
                visit(sub, view, identity, box, local, enabled, ancestors | {kind}, kind, inert)

    for tab in page["tabs"]:
        view = VIEW_IDS[tab["tabName"]]
        tabs[view] = {"name": tab["tabName"], "group": tab["fnKeyIndex"], "sub": tab["fnKeySubIndex"]}
        if popup is None:
            visit(tab["componentDefinition"], view, view, (0, 110, 1280, 628), {}, [], set(), f"tab:{view}")
        elif view == popup[0]:
            name = popup[1]
            visit(defs[name], view, f"{view}/Popup/{name}", (0, 0, 1280, 800), {}, [], {name}, name)
    if set(tabs) != set(VIEW_IDS.values()):
        raise ValueError(f"unexpected tab set: {sorted(tabs)}")
    return tabs, controls


def locate(controls, view, label, point=None):
    hits = [c for c in controls if c["view"] == view and (c["id"] == label or label.casefold() in c["id"].casefold())]
    exact = [c for c in hits if c["id"] == label]
    if exact:
        hits = exact
    if point is not None:
        x, y = point
        hits = [c for c in hits if c["rect"][0] <= x < sum(c["rect"][::2]) and
                c["rect"][1] <= y < sum(c["rect"][1::2])]
    if len(hits) != 1:
        raise ValueError(f"{view}: {label!r} matches {len(hits)} controls: {[c['id'] for c in hits[:5]]}")
    return hits[0]


def function_key(control):
    """One visible-tab command, ignoring copies of its art and selector states."""
    data = control["data"]
    handles = sorted(set(control["handles"].values()) | set(control["maps"].values()))
    actions = sorted((a.get("onAction", ""), a.get("handler", ""), a.get("additionalData", "")) for a in control["actions"])
    name = control["id"].rsplit("/", 1)[-1]
    signature = [control["view"], control["type"], handles, data.get("buttonId"), actions]
    if not handles:
        signature.append(name)
    return json.dumps(signature, separators=(",", ":"))
