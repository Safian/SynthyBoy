# SynthyBoy - MPC skin extra: the function key labels (the last module).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# skin-meta.json may have "labels" ({page name: label}, from "label" in the layout file). The host draws the tabName of the current sub-tab on the function key, so a page that carries a label gets it as
# its tabName in the finished skin ("Osc A" under the Osc key reads OSC A, "Env" reads ENV 1, "Filter" reads FILTER 1, "Mixer" reads MIX). The page keeps its unique INTERNAL name for every other module
# (the name the layout, the overlay views and the tests refer to): this module runs after all of them (the file name sorts last). A skin-meta.json without "labels" (the production skin) is not touched.


def apply(tui, qlinks, ctx):
    if ctx.meta is None:
        return
    labels = ctx.meta.get("labels", {})
    tabs = tui["pageData"]["tabs"]
    unknown = sorted(set(labels) - {t["tabName"] for t in tabs})
    if unknown:
        raise ValueError("skin-meta.json has labels for pages the skin does not have: %s" % unknown)
    for tab in tabs:
        tab["tabName"] = labels.get(tab["tabName"], tab["tabName"])
    shown = [t["tabName"] for t in tabs]
    if len(set(shown)) != len(shown):
        raise ValueError("tab names are not unique after the labels: %s" % sorted({n for n in shown if shown.count(n) > 1}))
