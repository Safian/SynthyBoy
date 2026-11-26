# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Install fixed canvas assets and detector widgets after screen hooks finish."""
import json
from pathlib import Path
import runpy

# Reuse the one packaging implementation. Later screen hooks may rebuild their
# root children, so detector widgets and captions must be installed last.
_install_backgrounds = runpy.run_path(str(Path(__file__).with_name("99_backgrounds.py")))["apply"]


def apply(tui, qlinks, ctx):
    _install_backgrounds(tui, qlinks, ctx)
    if not getattr(ctx, "skins_dir", None):
        return
    # Some SDK base layouts supply stock switch pictures. Only retain a picture
    # when the final skin still binds a switch to it.
    from components.native.button.button import write_switch_asset

    serialized = json.dumps(tui, ensure_ascii=False)
    for name in ("toggle_on.png", "toggle_off.png"):
        path = Path(ctx.skins_dir) / name
        if json.dumps(name) not in serialized:
            path.unlink(missing_ok=True)
        elif not path.exists():
            write_switch_asset(path, active=name == "toggle_on.png")
