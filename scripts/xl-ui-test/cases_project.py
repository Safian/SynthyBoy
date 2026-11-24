"""Native MPC XL Project As and reload on the independent Main-mode Sbtest fixture."""
import gzip
import hashlib
import json
from pathlib import PurePosixPath
import secrets
import shlex
import time

from device import Unavailable, recognise

PROJECTS = PurePosixPath("/nvme/MPC Documents/Projects")
SOURCE = "Sbtest"
FIXTURE = "SbUI-1124-1838"
ACID = "Acid Saw Reso"
ANALOG = "Analog Sweep Bass"


def project(dev, name):
    raw = dev.remote("cat " + shlex.quote(str(PROJECTS / (name + ".xpj"))))
    header = gzip.decompress(raw).split(b"\n", 5)
    if len(header) != 6 or header[0] != b"ACVS" or header[2:4] != [b"SerialisableProjectData", b"json"]:
        raise Unavailable("Unrecognised MPC project format: " + name)
    return hashlib.sha256(raw).hexdigest(), json.loads(header[5])["data"]


def instrument(data):
    plugins = [t["program"]["plugin"]["plugin"] for t in data["tracks"]
               if t.get("program", {}).get("plugin", {}).get("plugin", {}).get("state") and
               t["program"]["plugin"]["plugin"].get("description", {}).get("name") == "SynthyBoy"]
    if len(plugins) != 1:
        raise Unavailable("Expected exactly one SynthyBoy instrument in the project")
    return plugins[0]


def words(image, rect=None):
    frame = image.crop(rect) if rect else image
    return " ".join(row["text"] for row in recognise(frame) if row["text"].strip())


def require(image, phrase, rect=None):
    actual = words(image, rect)
    if phrase.casefold() not in actual.casefold():
        raise Unavailable(f"XL screen is missing {phrase!r}: {actual!r}")


def capture(dev, out, tag):
    frame = dev.image()
    frame.save(out / (tag + ".png"))
    return frame


def wait_main(dev, out, name, expected):
    for _ in range(8):
        frame = dev.image()
        title = words(frame, (95, 35, 620, 99)).strip(" *|")
        if title.casefold() == name.casefold() and expected.casefold() in words(frame, (294, 546, 895, 610)).casefold():
            frame.save(out / (name + "." + expected.replace(" ", "-") + ".png"))
            return
        time.sleep(.5)
    raise Unavailable(f"Could not identify project {name} with preset {expected} on Main")


def exact_row(image, name):
    matches = [r for r in recognise(image) if r["text"].strip().casefold() == name.casefold()]
    if len(matches) == 1:
        item = matches[0]
        return int(item["left"]) + int(item["width"]) // 2, int(item["top"]) + int(item["height"]) // 2
    return None


def open_project(dev, out, name, expected):
    dev.gesture(2, 398, 418, 0, 400)
    require(dev.image(), "Browser")
    dev.gesture(117, 69)
    dev.gesture(1082, 50)
    dev.gesture(496, 217)
    file = name + ".xpj"
    for _ in range(12):
        frame = dev.image()
        require(frame, "MPC Documents/Projects")
        position = exact_row(frame, file)
        if position:
            break
        dev.gesture(915, 665, 0, -300, 490)
    else:
        raise Unavailable("Exact project file is not visible in the XL browser: " + file)
    dev.gesture(*position)
    dev.gesture(1170, 770)
    time.sleep(1)
    frame = capture(dev, out, name + ".load")
    if "save changes" in words(frame).casefold():
        raise Unavailable("The current project has unsaved changes. The suite left the save prompt untouched")
    wait_main(dev, out, name, expected)


def preset(dev, out, name, old, new):
    wait_main(dev, out, name, old)
    # Both taps are sent in one device-side sequence inside the host's double-tap window.
    dev.remote(f"{dev.touch} tap 510 584 && {dev.touch} tap 510 584")
    time.sleep(.6)
    frame = dev.image()
    require(frame, "PRESET", (0, 0, 380, 109))
    for y in range(150, 800, 80):
        if words(frame, (24, y - 28, 400, min(y + 29, 800))).casefold() == new.casefold():
            dev.gesture(185, y)
            break
    else:
        raise Unavailable("Required preset is not in the current native preset list: " + new)
    frame = dev.image()
    if "preset" in words(frame, (0, 0, 380, 109)).casefold() and words(frame, (95, 35, 620, 99)).strip(" *|").casefold() != name.casefold():
        dev.gesture(106, 49)
    wait_main(dev, out, name, new)


def free_name(dev, name):
    pattern = shlex.quote(name + "*")
    return not dev.remote(f"find {shlex.quote(str(PROJECTS))} -maxdepth 1 -iname {pattern} -print").strip()


def keyboard(dev, name):
    rows = (("qwertyuiop", 82, 124, 459), ("asdfghjkl", 143, 124, 552), ("zxcvbnm", 267, 124, 647))
    keys = {letter: (left + n * spacing, y) for letters, left, spacing, y in rows for n, letter in enumerate(letters)}
    for char in name.lower():
        dev.gesture(*keys[char])


def save_as(dev, out, name):
    # The isolation fixture opens Main. Track Edit's disk icon saves a preset.
    wait_main(dev, out, FIXTURE, ANALOG)
    dev.gesture(57, 52)
    require(dev.image(), "Save")
    dev.gesture(642, 718)
    require(dev.image(), "Project As", (485, 99, 775, 322))
    dev.gesture(637, 208)
    frame = capture(dev, out, name + ".project-as")
    require(frame, FIXTURE, (140, 615, 870, 731))
    dev.gesture(895, 679)
    require(dev.image(), "DOIT")
    keyboard(dev, name)
    dev.gesture(1168, 741)
    frame = capture(dev, out, name + ".before-save")
    require(frame, "MPC Documents/Projects", (130, 5, 800, 91))
    actual = words(frame, (170, 645, 855, 717)).strip(" *|")
    if actual.casefold() != name.casefold() or not free_name(dev, name):
        raise Unavailable(f"Native save name or absence check failed: {actual!r}")
    require(frame, "SAVE", (1080, 724, 1277, 799))
    dev.gesture(1175, 770)
    wait_main(dev, out, name, ANALOG)


def run(dev, spec, step, out):
    if spec.get("copy_only") is not True:
        raise ValueError("Only Project As into a new copy is supported")
    source_digest, source = project(dev, SOURCE)
    fixture_digest, fixture = project(dev, FIXTURE)
    if fixture.get("engineMode") != "Main Mode" or instrument(fixture)["presetName"].rstrip(" *") != ACID:
        raise Unavailable("The expected Main-mode Acid Saw Reso fixture is required")
    if dict(source, engineMode="Main Mode") != fixture:
        raise Unavailable("Main-mode fixture no longer matches the original Sbtest")
    name = "Sbuitest" + secrets.choice(("red", "green", "blue", "pink")) + secrets.choice(
        ("key", "bell", "beat", "tone", "drum", "bass", "wave", "pad"))
    if not free_name(dev, name):
        raise Unavailable("Generated test project name already exists: " + name)
    open_project(dev, out, FIXTURE, ACID)
    preset(dev, out, FIXTURE, ACID, ANALOG)
    save_as(dev, out, name)
    saved_digest, saved = project(dev, name)
    if instrument(saved)["presetName"].rstrip(" *") != ANALOG or instrument(saved)["state"] == instrument(source)["state"]:
        raise AssertionError("Saved project does not contain the changed SynthyBoy instrument state")
    open_project(dev, out, FIXTURE, ACID)
    open_project(dev, out, name, ANALOG)
    for existing, expected in ((SOURCE, source_digest), (FIXTURE, fixture_digest), (name, saved_digest)):
        if project(dev, existing)[0] != expected:
            raise AssertionError("An existing project changed during the copy/reload test: " + existing)
    dev.gesture(56, 53)
    require(dev.image(), "TRACK EDIT")
    dev.gesture(414, 430)
    step({"op": "navigate", "view": "play1"})
    result = {"project": str(PROJECTS / (name + ".xpj")), "sha256": saved_digest,
              "source_sha256": source_digest, "fixture_sha256": fixture_digest,
              "saved_preset": ANALOG, "intermediate_preset": ACID, "reopened_preset": ANALOG}
    (out / (name + ".project.json")).write_text(json.dumps(result, indent=2) + "\n")
    return result
