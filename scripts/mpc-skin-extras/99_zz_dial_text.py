# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""After every screen hook: all dials of the skin use the one dial text style of the Level / Pan components."""


def apply(tui, qlinks, ctx):
    from components.native.level.level import unify_dial_text
    unify_dial_text(tui)
