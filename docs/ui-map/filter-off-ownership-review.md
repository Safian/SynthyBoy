# Filter OFF ownership mini-review (2026-11-24, develop 4a7002cb)

Read-only review. No code change, build or test. Sources: `shell-mpc/overlay/filter_ui.h`, `shell-mpc/ui/filter_display.h`, `core/src/param_table.cpp`.

## Ownership

| Control | Real key | Scope | Correct rule when the edited filter is OFF |
|---|---|---|---|
| POWER | `filterN.on` | per filter (param_table.cpp:254) | stays writable |
| MODEL STYLE CUTOFF RESO DRIVE MIX KEYTRACK BLEND COMB FORMANT | `filterN.<key>` (filter_ui.h:913-940) | per filter (SB_FILTER, param_table.cpp:253-268; BLEND is :260) | disabled (correct) |
| CLEAN | `filterN.clean_mode` | per filter (param_table.cpp:874-875) | disabled (correct) |
| ENV1, VAR | `filterN.env1` / `filterN.var` (filter_ui.h:930-931) | per filter (by key) | disabled (correct) |
| ROUTING | `filter.routing` (filter_ui.h:915-916) | **global**, shared by both filters (param_table.cpp:100, :585) | gated by the power of the edited filter only. Not owned by that filter |
| NOTE / VELO | mod sources 24 / 23 | no on/off parameter. The dot shows only that a mod slot uses the source (filter_ui.h:1523-1533) | stays independent (correct) |

## Current backend
ROUTING is not writable while the edited filter is OFF:
- touch: `routing.setEnabled (on)`, filter_display.h:749-754
- native/stale: the `power >= 0.5` gate, filter_ui.h:714-716
- Q: empty publish, filter_ui.h:1829-1836
- wheel: filter_ui.h:531-535

## Routing, gain and readout semantics
These files do not show what F1 OFF/F2 ON or both OFF does to routing or gain. That lives in the engine DSP, which is out of scope here. Not proven.

## Policy proposal (not implemented)
Because `filter.routing` is one global parameter, disabling it on the F1 page alone is inconsistent: the same key stays editable from the F2 page while F2 is ON. Make ROUTING passive only when **both** filters are OFF, or always passive with F1/F2 status shown, depending on the engine fact. Before choosing, verify how routing behaves with one filter OFF in the engine (open TODO). All other per-filter disabling in the prototype is correct.

## Coordinator plan choice
The layout prototype now keeps the shared ROUTING control editable whenever at least one filter is ON, and dims/disables it only with both OFF. Per-filter controls follow their own power; NOTE/VELO remains independent. This is a proposed interaction rule in the layout demo, not a claim that production gates or engine routing have changed. The production change still requires source-backed engine verification and touch/Q/stale-input acceptance.
