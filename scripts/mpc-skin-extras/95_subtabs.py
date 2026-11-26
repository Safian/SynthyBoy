# SynthyBoy - MPC skin extra: function keys and sub-tabs.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The SDK generator makes one function key (fnKeyIndex) with one tab (fnKeySubIndex 0) of every page. The layout file (scripts/overlay-skin/mpc-ui-pages-overlay.json) groups
# pages into function keys: skin-meta.json lists them ("fnKeys": [{"name", "tabs": [page names]}]). This module gives the tabs of one key the same
# fnKeyIndex and the sub-tab indexes 0 .. k - 1 (the host steps through them when the key is pressed again; the key shows the tabName of the current
# sub-tab, observed on the Key 61), puts the tab list in that order and renumbers Q-Links.json: Tab = fnKeyIndex + 1, SubTab = fnKeySubIndex + 1,
# entries in the order of the tabs. Every module before this one finds its tab by name, and the Q-Link entry of a tab by the generator's numbering
# (one key per page), before this hook groups them.
# Labels: the finished skin carries the "label" of a page of the layout file as its tabName (the host draws the tabName of the current sub-tab on the function key: the first row of keys reads
# Play, Osc A, Env 1, ...). That is done by the LAST module (99_zzz_labels.py), so every module (this one included) refers to a page by its unique INTERNAL name.

SUB_LIMIT = 8   # SDK ui-reference 3.5: at most 8 sub-tabs per function key


def apply(tui, qlinks, ctx):
    if ctx.meta is None or "fnKeys" not in ctx.meta:
        raise ValueError("needs skin-meta.json with fnKeys (apply-mpc-skin-extras.py --meta)")
    tabs = tui["pageData"]["tabs"]
    by_name = {t["tabName"]: t for t in tabs}
    if len(by_name) != len(tabs):
        raise ValueError("tab names are not unique")
    entries = qlinks["Screen Mode Q-Links"]["map"]
    by_slot = {(e["Tab"], e["SubTab"]): e for e in entries}
    ordered, new_entries = [], []
    for fn_index, key in enumerate(ctx.meta["fnKeys"]):
        if len(key["tabs"]) > SUB_LIMIT:
            raise ValueError("function key %r has %d sub-tabs (at most %d)" % (key["name"], len(key["tabs"]), SUB_LIMIT))
        for sub_index, name in enumerate(key["tabs"]):
            tab = by_name.pop(name, None)
            if tab is None:
                raise ValueError("skin-meta.json lists the tab %r, which the skin does not have (or lists it twice)" % name)
            entry = by_slot.pop((tab["fnKeyIndex"] + 1, tab["fnKeySubIndex"] + 1), None)
            if entry is None:
                raise ValueError("Q-Links.json has no entry for the tab %r" % name)
            tab["fnKeyIndex"], tab["fnKeySubIndex"] = fn_index, sub_index
            entry["Tab"], entry["SubTab"] = fn_index + 1, sub_index + 1
            ordered.append(tab)
            new_entries.append(entry)
    if by_name or by_slot:
        raise ValueError("tabs %s are in no function key" % sorted(by_name))
    tabs[:] = ordered
    entries[:] = new_entries
