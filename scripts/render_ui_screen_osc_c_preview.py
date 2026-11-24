#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Render the OSC C screen using the production OSC fixture."""

import sys

from render_osc_screen_preview import render_page


def render_preview(output_path=None):
    return render_page("osc_c_on", output_path)


if __name__ == "__main__":
    print(render_preview(sys.argv[1] if len(sys.argv) > 1 else None))
