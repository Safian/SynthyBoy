# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from pathlib import Path

from native_play import env_lfo


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    play = next((tab for tab in tui["pageData"]["tabs"] if tab.get("tabName") == "Play"), None)
    if play is not None:
        env_lfo.apply(tui, play["componentDefinition"]["componentsData"], ctx,
                      Path(__file__).with_name("play-env-lfo-contract.json"))
