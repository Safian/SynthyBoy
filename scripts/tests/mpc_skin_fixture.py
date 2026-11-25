"""Minimal native-skin fixture for build and archive tests using synthetic binaries."""
import json
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from validate_synthyboy_skin import SURFACES


def write_native_skin_stub(skins):
    """Minimal native tab structure for the Docker/packaging stub, without an SDK install."""
    views = json.loads((ROOT / "shell-mpc/overlay/overlay_layout.json").read_text())["views"]
    layout = json.loads((ROOT / "scripts/overlay-skin/mpc-ui-pages-overlay.json").read_text())["pages"]
    labels = {p["name"]: p.get("label", p["name"]) for p in layout}
    detectors = {labels[v["tab"]]: v["name"] for v in views}
    tabs, mappings = [], []
    for index, (tab, surface) in enumerate(SURFACES.items()):
        view = detectors[tab]
        children = [{"componentData": {"name": surface, "type": surface, "data": {}}}]
        for name in (f"Overlay Marker {view} left", f"Overlay Marker {view} right", f"Overlay Instance {view}"):
            children.append({"componentData": {"name": name, "type": "Image", "data": {"image": "marker.png"}}})
        tabs.append({"tabName": tab, "fnKeyIndex": index, "fnKeySubIndex": 0, "qlinkBoundsData": [],
                     "componentDefinition": {"componentsData": children}})
        mappings.append({"Tab": index + 1, "SubTab": 1, "Bank Direction": "Row", "Q-Links": {f"Q-Link {n}": -1 for n in range(1, 17)}})
    definitions = [{"key": k, "value": {"componentsData": []}} for k in sorted(set(SURFACES.values()))]
    info = {"version": 1, "type": "CompleteDescription"}
    (skins / "TUI.json").write_text(json.dumps({"pageData": {"version": 1, "info": info, "tabs": tabs,
        "componentDefinitions": {"version": 2, "importFiles": [], "localComponentDefinitions": definitions}}}))
    (skins / "Q-Links.json").write_text(json.dumps({"version": 4, "info": info, "Screen Mode Q-Links": {"version": 4, "map": mappings},
        "Program Mode Q-Links": {f"Q-Link {n}": n - 1 for n in range(1, 17)}}))
    Image.new("RGB", (1, 1)).save(skins / "marker.png")
