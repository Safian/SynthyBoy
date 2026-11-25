# Host-native instance selector for standalone oscillator tabs.
from pathlib import Path

import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))
_SCRIPTS = _REPO_ROOT / "scripts"
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from components.native.tab_bar.tab_bar import tab_bar
from components.style import size
from components.native.selector.selector import (
    OSC_INSTANCE_NAMES,
    OSC_POWER_PARAM_KEYS,
    OSC_SOURCE_OVERLAY_NAME,
    build_standalone_osc_source_overlay,
    sanitize_osc_tab_navigation,
)
from native_ui_params import OSC_SOURCE, OSC_VISIBILITY, mixer_peek_index


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    tabs = [tab for tab in tui["pageData"]["tabs"] if tab.get("tabName") in {"Osc", *OSC_INSTANCE_NAMES}]
    if not tabs:
        return
    base = mixer_peek_index(ctx.param_count)
    selector_idx = base + OSC_SOURCE
    state_idx = base + OSC_VISIBILITY
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    power_params = {k: ctx.param_index(k) for k in OSC_POWER_PARAM_KEYS}
    if not any(d["key"] == OSC_SOURCE_OVERLAY_NAME for d in defs):
        defs.append({"key": OSC_SOURCE_OVERLAY_NAME, "value": build_standalone_osc_source_overlay(power_params.values())})
    replacement = tui["pageData"].setdefault("replacementStrings", [])
    pkey = "Parameter %d" % selector_idx
    entry = next((r for r in replacement if r.get("key") == pkey), None)
    choices = list(OSC_INSTANCE_NAMES)
    if entry is None:
        replacement.append({"key": pkey, "value": choices})
    else:
        entry["value"] = choices

    for tab in tabs:
        items = tab["componentDefinition"]["componentsData"]
        items.extend(tab_bar("Osc Native Source", ("SUB", "A", "B", "C", "NOISE"),
                             size("osc.source_tabs.rect"), pkey, pkey, getattr(ctx, "skins_dir", None),
                             gates=(f"NonZero/Parameter {state_idx}",)))
        sanitize_osc_tab_navigation(tab, power_params)
