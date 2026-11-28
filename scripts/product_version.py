#!/usr/bin/env python3
# SynthyBoy - single source of product version and build number for Python scripts.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import re
import sys
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
    """Load the product version from mpc-package/version.xml (the single source; its <version> is major.minor.patch.build).

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
    text = (root / "mpc-package" / "version.xml").read_text(encoding="utf-8")
    m = re.search(r"<version>\s*(\d+)\.(\d+)\.(\d+)\.(\d+)\s*</version>", text)
    if not m:
        raise ValueError("mpc-package/version.xml has no <version>major.minor.patch.build</version>")
    version_text = ".".join(m.group(i) for i in (1, 2, 3))
    build = int(m.group(4))
    vst2_int, four_part_text = parse_version_build(version_text, build)

    return version_text, build, vst2_int, four_part_text


if __name__ == "__main__":
    print(load()[3] if "--four-part" in sys.argv[1:] or len(sys.argv) == 1 else load()[0])
