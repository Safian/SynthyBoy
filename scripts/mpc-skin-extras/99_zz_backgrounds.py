# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Install fixed canvas assets and detector widgets after screen hooks finish."""
from pathlib import Path
import runpy

# Reuse the one packaging implementation. Later screen hooks may rebuild their
# root children, so detector widgets and captions must be installed last.
apply = runpy.run_path(str(Path(__file__).with_name("99_backgrounds.py")))["apply"]
