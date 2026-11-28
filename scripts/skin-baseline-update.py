#!/usr/bin/env python3
"""Rewrite scripts/skin-baseline/TUI.keys.json from skin/TUI.json plus its import files (skin/defs_*.json).

Run it only after the owner has proven the skin on the Key 61 (see docs/MPC.md, skin section)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_synthyboy_skin import BASELINE, ROOT, current_hashes  # noqa: E402

BASELINE.write_text(__import__("json").dumps(current_hashes(ROOT / "skin/TUI.json"), indent=1, sort_keys=True) + "\n")
print(f"wrote {BASELINE.relative_to(ROOT)}")
