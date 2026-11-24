"""The XL page guard decodes the same marker pairs in the same order as the skin."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAYOUT = ROOT / "shell-mpc/overlay/overlay_layout.json"
SHIM = ROOT / "tools/xl-commit-shim"


def shifted(rgb, delta):
    channels = [max(0, min(255, ((rgb >> shift) & 255) + delta)) for shift in (16, 8, 0)]
    return (channels[0] << 16) | (channels[1] << 8) | channels[2]


def test_current_shim_markers():
    subprocess.run([sys.executable, "scripts/gen-overlay-layout-header.py", "--shim-markers", "--check"],
                   cwd=ROOT, check=True)
    layout = json.loads(LAYOUT.read_text())
    views = layout["views"]
    pairs = [tuple(int(m["rgb"], 16) for m in v["markers"]) for v in views]
    cases = []
    for i, (left, right) in enumerate(pairs):
        cases.extend([(left, right, i), (shifted(left, 2), shifted(right, -2), i),
                      (left, right + 3, -1), (left, pairs[(i + 1) % len(pairs)][1], -1)])
    # Deleted tabs in pre-consolidation skins: F2, LFO 2..4, ENV 2..4.
    for channel in (0x17, 0x25, 0x2c, 0x33, 0x56, 0x5d, 0x64):
        cases.append(((channel << 16) | 0x3a2c, 0x3a0064 | (channel << 8), -1))
    with tempfile.TemporaryDirectory() as tmp:
        binary = Path(tmp) / "check"
        source = '#include "view_markers.h"\n#include <stdio.h>\nint main(void) {\n'
        for i, view in enumerate(views):
            centers = []
            for marker in view["markers"]:
                centers.extend((marker["x"] + marker["w"] // 2, layout["tabOriginY"] + marker["y"] + marker["h"] // 2))
            for j, value in enumerate(centers):
                source += f"if (shim_marker_centers[{i}][{j}] != {value}) return 2;\n"
        for left, right, expected in cases:
            source += (f'if (shim_view_index(0x{left:06x}, 0x{right:06x}) != {expected}) '
                       f'{{ puts("marker {left:06x}/{right:06x}, expected {expected}"); return 1; }}\n')
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(SHIM),
                        "-x", "c", "-", "-o", str(binary)], input=source + "return 0; }\n", text=True, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    test_current_shim_markers()
