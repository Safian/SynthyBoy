"""Generate the production MPC skin for offline screen previews."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

REPO_ROOT = Path(__file__).resolve().parents[1]
PARAMS_JSON = REPO_ROOT / "docs" / "params.json"
PAGES_JSON = REPO_ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = REPO_ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
GEN_SCRIPT = REPO_ROOT / "scripts" / "gen-mpc-ui.py"
RUN_GEN_UI = REPO_ROOT / "scripts" / "run-gen-ui.py"
APPLY_SCRIPT = REPO_ROOT / "scripts" / "apply-mpc-skin-extras.py"
DEFAULT_MPC_SDK = REPO_ROOT.parent / ".hw-local/mpc-knowledge/docs-official/mpc-sdk"


def _find_mpc_sdk() -> Path:
    env = os.environ.get("MPC_SDK")
    if env and Path(env).is_dir():
        return Path(env)
    if DEFAULT_MPC_SDK.is_dir():
        return DEFAULT_MPC_SDK
    home_sdk = Path.home() / ".local" / "share" / "local-worker" / "ext" / "mpc-sdk"
    if home_sdk.is_dir():
        return home_sdk
    raise FileNotFoundError("MPC_SDK not found; set MPC_SDK environment variable.")


def ensure_generated_skin(work_dir: Path, mpc_sdk: Path | None = None) -> Path:
    skins_dir = work_dir / "skins"
    sdk = mpc_sdk or _find_mpc_sdk()
    ui_dir = work_dir / "ui"
    ui_dir.mkdir(parents=True, exist_ok=True)
    skins_dir.mkdir(parents=True, exist_ok=True)

    steps = [
        [
            sys.executable, str(GEN_SCRIPT),
            "--params", str(PARAMS_JSON),
            "--pages", str(PAGES_JSON),
            "--layout", str(LAYOUT_JSON),
            "--out", str(ui_dir),
        ],
        [
            sys.executable, str(RUN_GEN_UI),
            "--sdk", str(sdk),
            "--params", str(ui_dir / "params-hostsim.json"),
            "--ui", str(ui_dir / "ui.json"),
            "--out", str(skins_dir),
            "--quiet",
        ],
        [
            sys.executable, str(APPLY_SCRIPT),
            "--skins", str(skins_dir),
            "--params", str(PARAMS_JSON),
            "--meta", str(ui_dir / "skin-meta.json"),
        ],
    ]
    for cmd in steps:
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"Skin generation command failed ({cmd[1]}):\n{res.stderr}")
    return skins_dir
