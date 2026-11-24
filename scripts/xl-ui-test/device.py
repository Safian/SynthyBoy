"""XL native framebuffer, status and touch adapter. Only the operator-selected XL is contacted."""
import csv
import gzip
import io
import json
import math
from pathlib import Path
import re
import shlex
import subprocess
import time

from PIL import Image, ImageChops, ImageOps


class Unavailable(Exception):
    """A prerequisite or independent oracle is unavailable."""


class XL:
    footer_settle = .65
    footer_confirm = 0
    card = 0
    rotation = Image.Transpose.ROTATE_90

    def __init__(self, host, tool_dir):
        if not re.fullmatch(r"[A-Za-z0-9_@.:-]+", host) or host.startswith("-"):
            raise ValueError("invalid SSH host")
        self.host = host
        self.touch = shlex.quote(tool_dir.rstrip("/") + "/xtouch")
        self.grab = shlex.quote(tool_dir.rstrip("/") + "/drmgrab")
        self.plane_grab = shlex.quote(tool_dir.rstrip("/") + "/pgrab")

    def remote(self, command, timeout=15):
        output = subprocess.run(["ssh", "-oBatchMode=yes", "-oConnectTimeout=5", self.host, command],
                                capture_output=True, timeout=timeout)
        if output.returncode:
            raise RuntimeError(f"XL SSH {output.returncode}: {output.stderr.decode(errors='replace')[:500]}")
        return output.stdout

    def status(self):
        raw = self.remote("cat /tmp/synthyboy-overlay-status.txt").decode()
        state = dict(line.split(": ", 1) for line in raw.splitlines() if ": " in line)
        if not state.get("view"):
            raise Unavailable("no active SynthyBoy overlay status")
        return state

    def image(self):
        data = gzip.decompress(self.remote(f"{self.grab} /dev/dri/card{self.card} 2>/dev/null | gzip -1"))
        return self.decode(data).convert("RGB")

    @classmethod
    def decode(cls, data):
        header, raw = data.split(b"\n", 1)
        w, h, pitch = map(int, header.split()[:3])
        if (w, h) != (800, 1280) or len(raw) != pitch * h:
            raise Unavailable(f"unexpected XL framebuffer {header!r}")
        return Image.frombytes("RGBA", (w, h), raw, "raw", "BGRA", pitch).transpose(cls.rotation)

    def overlay(self):
        state = self.remote(f"cat /sys/kernel/debug/dri/{self.card}/state").decode()
        planes = [p for p in re.split(r"(?=plane\[\d+\]:)", state) if p.startswith("plane[") and
                  re.search(r"\n\s+fb=[1-9]", p) and re.search(r"\n\s+size=800x1280", p) and
                  re.search(r"\n\s+format=AR24", p)]
        if len(planes) != 1:
            raise Unavailable(f"expected one active AR24 plane, found {len(planes)}")
        plane = int(re.match(r"plane\[(\d+)", planes[0]).group(1))
        return self.decode(gzip.decompress(self.remote(f"{self.plane_grab} /dev/dri/card{self.card} {plane} 2>/dev/null | gzip -1")))

    @staticmethod
    def compose(primary, overlay):
        alpha = overlay.getchannel("A")
        background = Image.composite(Image.new("RGB", primary.size), primary, alpha)
        return ImageChops.add(background, overlay.convert("RGB"))

    def gesture(self, x, y, dx=None, dy=None, duration=350):
        x, y = int(x), int(y)
        points = [(x, y)] if dx is None else [(x, y), (x + int(dx), y + int(dy))]
        if any(not (0 <= x < 1280 and 0 <= y < 800) for x, y in points):
            raise ValueError(f"touch outside screen {points}")
        args = f"tap {x} {y}" if dx is None else f"drag {x} {y} {points[1][0]} {points[1][1]} {int(duration)}"
        self.remote(f"{self.touch} {args}")
        time.sleep(0.45)

    def snapshot(self, display=False):
        cmd = """set -eu
cd /tmp
lock=synthyboy-xl-ui-dump-lock
mkdir "$lock" || exit 27
request=synthyboy-play-log-dump
output=$request.txt
if test -e "$request"; then rmdir "$lock"; exit 28; fi
had=0
if test -e synthyboy-play-log; then had=1; fi
cleanup() {
 rm -f "$request"
 if test "$want_display" = 1; then rm -f "$request-display"; fi
 if test "$had" = 0; then rm -f synthyboy-play-log; fi
 if test -e "$lock/prior" && test ! -s "$output"; then mv "$lock/prior" "$output"; fi
 rm -f "$lock/prior"
 rmdir "$lock"
}
trap cleanup EXIT
if test -e "$output"; then mv "$output" "$lock/prior"; fi
if test "$want_display" = 1; then touch "$request-display"; fi
touch synthyboy-play-log "$request"
for i in $(seq 1 35); do
 if test ! -e "$request" && test -s "$output" && grep -aq '^chunk [0-9]' "$output"; then cat "$output"; exit; fi
 sleep .2
done
exit 29"""
        try:
            payload = self.remote("want_display=" + ("1" if display else "0") + "\n" + cmd)
        except RuntimeError as e:
            raise Unavailable(f"engine parameter dump unavailable ({e})") from e
        if not hasattr(self, "_parameter_keys"):
            manifest = Path(__file__).resolve().parents[2] / "docs/params.json"
            self._parameter_keys = [p["key"] for p in json.loads(manifest.read_text())["params"]]
        return parse_dump(payload, self._parameter_keys)

    def params(self):
        return self.snapshot()["engine"]


class Key61(XL):
    footer_settle = 2.0
    footer_confirm = .35
    card = 1
    rotation = Image.Transpose.ROTATE_270

    def __init__(self, host, tool_dir="/tmp"):
        super().__init__(host, tool_dir)
        self.touch = shlex.quote(tool_dir.rstrip("/") + "/mtouch")
        self.plane_grab = shlex.quote(tool_dir.rstrip("/") + "/sb-restore-accept-pgrab")


def parse_dump(payload, expected_keys):
    marker = re.search(rb"^chunk (\d+)\n", payload, re.MULTILINE)
    if not marker or len(payload) - marker.end() != int(marker[1]) + 1 or not payload.endswith(b"\n"):
        raise Unavailable("incomplete engine dump state chunk")
    engine, vst, displays = {}, {}, {}
    for line in payload[:marker.start()].decode().splitlines():
        if line.startswith("display "):
            _, index, value = line.split(" ", 2)
            displays[int(index)] = value
            continue
        fields = line.split()
        if not fields or fields[0] not in ("p", "v"):
            continue
        try:
            index = int(fields[1])
            value = float(fields[-1])
            if not math.isfinite(value):
                raise ValueError("nonfinite value")
            if fields[0] == "p":
                if len(fields) != 4 or index != len(engine) or fields[2] != expected_keys[index]:
                    raise ValueError("parameter index/key mismatch")
                engine[fields[2]] = value
            else:
                if len(fields) != 3 or index != len(vst):
                    raise ValueError("incomplete VST index sequence")
                vst[index] = value
        except (IndexError, ValueError) as error:
            raise Unavailable(f"invalid dump row {line!r}: {error}") from error
    if len(engine) != len(expected_keys):
        raise Unavailable(f"incomplete engine parameter snapshot: {len(engine)}/{len(expected_keys)}")
    return {"engine": engine, "vst": vst, **({"display": displays} if displays else {})}


def recognise_word(image, alphabet=None):
    enlarged = image.convert("L").resize((image.width * (8 if alphabet else 4), image.height * (8 if alphabet else 4)), Image.Resampling.LANCZOS)
    letters = enlarged.point(lambda pixel: 255 if pixel > (90 if alphabet else 110) else 0)
    box = letters.getbbox()
    if not box:
        return ""
    word = ImageOps.expand(ImageOps.invert(letters.crop(box)), border=16 if alphabet else 20, fill=255)
    return " ".join(row["text"] for row in recognise(word, mode=7 if alphabet else 8, alphabet=alphabet) if row["text"].strip())


def recognise(image, mode=11, alphabet=None):
    buf = io.BytesIO()
    image.save(buf, "PNG")
    config = ["-c", "tessedit_char_whitelist=" + alphabet] if alphabet else []
    result = subprocess.run(["tesseract", "stdin", "stdout", "--psm", str(mode), *config, "tsv"], input=buf.getvalue(),
                            capture_output=True, check=True, timeout=12)
    return list(csv.DictReader(io.StringIO(result.stdout.decode()), delimiter="\t"))


ALIASES = [{"PLAY", "MACROS"}, {"OSC", "OSCA", "OSCB", "OSCC", "SUB", "NOISE"}, {"ENV"}, {"LFO"},
           {"FX"}, {"MIX", "MIXER"}, {"FILTER"}, {"MOD"}, {"MASTER", "VOICE"}]


def footer_labels(image):
    labels = {}
    for word in recognise(image.crop((0, 740, 1280, 799))):
        text = word["text"].strip().upper().strip(":.|")
        for i, names in enumerate(ALIASES):
            if text in names:
                labels[i] = int(word["left"]) + int(word["width"]) // 2
    return labels


def dismiss_fx_menu(dev, image):
    if footer_labels(image):
        return False
    # The factory picker captures the screen. Its labelled header dismisses it without choosing an effect.
    words = [row["text"].strip().upper() for row in recognise(image.crop((30, 20, 320, 84))) if row["text"].strip()]
    if words != ["FX", "ADD", "EFFECT"]:
        return False
    dev.gesture(106, 49)
    for attempt in range(6):
        time.sleep(.4)
        if footer_labels(dev.image()):
            return True
    raise Unavailable("FX ADD EFFECT picker did not restore the native editor footer")


def navigate(dev, tabs, target):
    group = tabs[target]["group"]
    attempts = []
    for attempt in range(18):
        state = dev.status()
        image = None
        if state["view"] == "fx":
            image = dev.image()
            if dismiss_fx_menu(dev, image):
                continue
        if dev.footer_confirm:
            time.sleep(dev.footer_confirm)
            confirmed = dev.status()
            if (confirmed["view"], confirmed.get("state")) != (state["view"], state.get("state")):
                continue
        if state["view"] == target and state.get("state") == "visible":
            return state
        if image is None:
            image = dev.image()
        labels = footer_labels(image)
        if not labels and state["view"] != "fx" and dismiss_fx_menu(dev, image):
            continue
        attempts.append((state["view"], sorted(labels)))
        if group in labels:
            x = labels[group]
        elif labels and group > max(labels):
            x = 1172
        elif labels and group < min(labels):
            x = 105
        else:
            raise Unavailable(f"cannot identify footer group {group}; observed {attempts[-1]}")
        dev.gesture(x, 770)
        time.sleep(dev.footer_settle)
    raise AssertionError(f"failed to enter {target} after {attempts}")
