#!/usr/bin/env python3
# SynthyBoy - single source of product version and build number for Python scripts.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
from pathlib import Path
from typing import Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent


def parse_version_build(version_text: str, build: int) -> Tuple[int, str]:
    """Calculate VST2 integer and four-part version string from semver text and build number."""
    parts = [int(p) for p in version_text.split(".")]
    while len(parts) < 3:
        parts.append(0)
    major, minor, patch = parts[0], parts[1], parts[2]
    build_int = int(build)
    vst2_int = major * 1000 + minor * 100 + patch * 10 + build_int
    four_part_text = f"{major}.{minor}.{patch}.{build_int}"
    return vst2_int, four_part_text


def load(repo_root: Optional[Path] = None) -> Tuple[str, int, int, str]:
    """Load product version from root plugin.json.

    Args:
        repo_root: Path to repository root. If None, derived from this file's location.

    Returns:
        tuple: (version_text, build, vst2_int, four_part_text)
            version_text (str): e.g. "0.1.0"
            build (int): e.g. 1
            vst2_int (int): major*1000 + minor*100 + patch*10 + build (e.g. 101)
            four_part_text (str): e.g. "0.1.0.1"
    """
    root = Path(repo_root) if repo_root is not None else ROOT
    manifest_path = root / "plugin.json"
    data = json.loads(manifest_path.read_text(encoding="utf-8"))

    version_text = str(data["version"])
    build = int(data.get("build", 1))
    vst2_int, four_part_text = parse_version_build(version_text, build)

    return version_text, build, vst2_int, four_part_text
