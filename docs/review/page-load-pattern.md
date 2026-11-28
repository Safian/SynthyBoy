# SynthyBoy MPC Page-Loading Pattern Analysis

**Scope:** Read-only analysis of `skin/TUI.json`, `skin/defs_*.json`, `shell-mpc/synthyboy_mpc.cpp`, `shell-mpc/native_*.h`, `shell-mpc/overlay/*.h`, `docs/dsp-knowledge/mpc-display.md`, `docs/dsp-knowledge/skin-host-behaviour.md`, `docs/dsp-knowledge/key61-native-control-arrival.md`, and Key 61 hardware traces under `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/`.  
**Base commit:** `ac8b3c34` (`agy/loadpattern`).  
**Owner decision (2026-11-28):** Every screen must use the **same** page-loading pattern; where one screen was made fast on the Key 61, all other screens must follow the same skin and plugin-publishing principles.

---

## 1. The GOOD Page-Loading Pattern

The Key 61 measurements establish two distinct latency regimes:
1. **Multi-second stalls (2.3–6.8 s):** Caused by (a) triggering the host's plugin-wide metadata re-read (`audioMasterUpdateDisplay`: 14,400–36,650 `effGetParam*` calls on the host main thread, driving Key 61 main thread to ~80 % busy / 1–2 s lag) *before* the host has painted the destination tab (`docs/dsp-knowledge/mpc-display.md:598-622`, `.hw-local/mpc-perf/key61-control-audit-wpt592/metadata-timing.csv`), (b) asserting the Frame-24 full-screen mask (`Overlay Instance` = `Parameter 2675` frame 24) at a native root (`mpc-display.md:549,568,576-580`), (c) tearing down native display bindings to `Hidden`/`0` on tab exit so the host paints a blank body until overlay detection + display-thread repopulation (`mpc-display.md:549-553,568,586-590`), or (d) combinatorial skin duplication (`FX` 38 header states × 149 cells = 5.95–6.83 s, `mpc-display.md:562`).
2. **Sub-second steady floor (0.25–0.68 s):** Once the multi-second stalls are removed, arrival time is bounded by host tab construction/paint (~0.30–0.50 s on compact tabs ≤ ~430 expanded nodes, rising to ~1.15–1.68 s on tabs with 1,978–2,977 expanded nodes; exact per-node host slope is `UNSURE` because device captures include host scheduling and ~170–220 ms sampling intervals).

The proven GOOD pattern consists of **five skin rules (S1–S5)** and **seven plugin runtime rules (P1–P7)**.

### 1.1 Skin Rules (TUI.json + `defs_*.json`)

| ID | Rule | Code / Skin Reference | Measured Evidence |
|---|---|---|---|
| **S1** | **Keep expanded component count compact per tab (target ≤ ~450 expanded nodes, never > 1,000).** The host instantiates the tab's entire expanded `componentsData` tree (including hidden children) when switching tabs. | `skin/TUI.json` tabs + `skin/defs_*.json` (expanded via `object_pairs_hook=list`, Section 2). | Key 61: removing 952 repeated OSC detail nodes cut FX → Play median from **1.05 s → 0.92 s** (`mpc-display.md:592`); omitting the closed Play OSC panel cut FX → Play median by **~0.25 s** (`0.975 s → 0.73 s`, `mpc-display.md:582`). Compact tabs (Tuning 87, Master 149, Env 155, Macros 272, Play 375, Voice 424) settle in **0.30–0.68 s** vs Osc (1,978 nodes) at **1.31–1.69 s** and FX (2,977 nodes) at **1.15–1.34 s** (`.hw-local/mpc-perf/key61-control-audit-wpt592/arrival-comparison.csv`). *Note:* component count alone does not cause 3–6 s stalls (`mpc-display.md:619`), and exact ms/node on Key 61 vs XL is `UNSURE`. |
| **S2** | **Use shared-definition retargeting instead of per-state, per-source, per-model, or per-item duplicates.** Repeating tab families share one `<family>Screen` definition (`oscScreen`, `envScreen`, `lfoScreen`, `filterScreen`), and *inside* a screen, mutually exclusive states (effect slot headers, oscillator sources/types, filter style lists, matrix scroll vs normal rows, ON/OFF twins) must retarget ONE shared set of slots via plugin proxy handles rather than instantiating N gated copies. | `scripts/validate_synthyboy_skin.py:46-51` (`SCREEN_FAMILIES`); `skin/defs_fx.json` (`fxNativeParameterCells`); `skin/defs_osc.json` (`oscNativeSettingsLayer`); `skin/defs_filter.json` (`filterNativeEditorSurface`). | Key 61: replacing 38 FX header states × 149 cells with two shared cell layers + shared header labels cut Distortion ↔ Chorus from **5.95–6.83 s → 0.246–0.271 s** (`mpc-display.md:562-564`). Sharing OSC detail menu faces across 10 types removed 952 nodes and cut FX → Play by **0.13 s** (`mpc-display.md:592`). |
| **S3** | **Minimize hidden-by-default layers, and never hide a standalone tab's root body behind a zero-initialized or cross-tab view gate.** Root controls of a dedicated physical tab should bind directly to engine parameters or always-valid cached proxies (valid at default state 0) so the host paints the real controls on its very first frame. Reserve hidden layers for rare modals/sub-pages, and gate text-owned cells only on an explicit 0/1 presence handle (`IndexedEnabling/1/2/Parameter <kTextGate>`), never `NonZero` on a text-display handle. | `shell-mpc/native_ui_params.h:150-154` (`kTextGate`); `docs/dsp-knowledge/skin-host-behaviour.md:69-70`; `skin/TUI.json` (`Voice` direct step bindings vs `Master`/`Tuning` `Parameter 2879`). | Key 61: cold Play showed ENV ADSR immediately (direct proxies) while hidden-at-0 OSC/LFO layouts lagged by **4.04 s** until pre-populated (`mpc-display.md:586-590`). Voice (direct engine params + cached selection) reached **0.437–0.467 s** (`mpc-display.md:553`). `kTextGate` 0/1 fix on Play keyline is not yet verified on Key 61 (`UNSURE`, `skin-host-behaviour.md:71`). |
| **S4** | **Minimize unique images and decoded RGBA footprint per tab; use plain `backgroundData` colour (`ff090d19`, `image: ""`) and shared control filmstrips.** | `skin/TUI.json` tab `backgroundData` (`colour: "ff090d19", image: ""`); `docs/dsp-knowledge/skin-host-behaviour.md:22-25`; `docs/dsp-knowledge/key61-native-control-arrival.md:10,15`. | Key 61: dropping `overlay_canvas.png` saved **3.2 MB** decoded RGBA per tab with pixel-identical ground `(9,13,25)` (`skin-host-behaviour.md:24-25`). Sharing strips/canvases across the skin cut estimated one-decode RGBA from **126.8 MB → 81.4 MB** (**−43.30 MiB**, `key61-native-control-arrival.md:15`). Host-internal cross-tab texture cache eviction policy is `UNSURE` (`key61-native-control-arrival.md:9`), and repeated restarts can exhaust `/data` with `temp_*.img` filmstrips (`.hw-local/mpc-perf/key61-fx-play-20261125/README.md:11`). |
| **S5** | **Keep every native root tab on the ordinary `Overlay Instance` frame (frames 0..15, matching `RangedEnabling/0/18/33/Parameter 2675`); never assert Frame 24 at a native root.** | `shell-mpc/synthyboy_mpc.cpp:3030-3036` (`overlayWantedValue`); `skin/TUI.json` (`Parameter 2675`). | Key 61: stopping native roots from asserting Frame 24 cut Master ↔ Voice (**2.32–2.93 s → 0.435–0.467 s**), FX → Play (**4.19–4.84 s → 0.95–1.15 s**), ENV → Play (**2.54 s → 0.73–0.83 s**), and LFO → Play (**3.74 s → 0.69–0.70 s**) (`mpc-display.md:549,553,568,574-580`). |

### 1.2 Plugin Runtime Rules (Page Switch, Publishing, Caching, Deferral)

| ID | Rule | Code Reference (`file:line`) | Measured Evidence |
|---|---|---|---|
| **P1** | **Retain a read-only display/selection cache on `leftTab()` / `onHidden()` instead of resetting layout/view handles to `0`/`Hidden`.** On tab exit, disarm input (`suspended_ = true`, `view = Hidden`, close popups, invalidate in-flight gestures) while keeping the last published layout and cell bindings readable by host `getParameter` / `effGetParamDisplay`. On return, call `renewInputRevisions()` (`NativeCellBank::renewInputRevisions`) to reject gestures from the previous visit without blanking the screen. | Good references: `shell-mpc/native_voice_controls.h:49-59` (`displayCell`), `shell-mpc/synthyboy_mpc.cpp:2072-2076,4505`; `shell-mpc/overlay/master_ui.h:197-201` + `synthyboy_mpc.cpp:589-609,2094-2095` (`nativeMasterSkinPanel`); `shell-mpc/native_play_osc.h:38-60` + `native_play_env_lfo.h:51-73` + `play_view.h:322-329` (`suspend()` + `renewInputRevisions()` in `native_fx_cells.h:154-163`). | Key 61: adding the read-only `displayCell()` cache fixed the blank 109 px Voice `MID` readout, cutting Voice return from **2.75–2.78 s → 0.437–0.467 s** (`mpc-display.md:549,553`, `.hw-local/mpc-perf/key61-voice-master-latency-20261125/README.md:8-16`). Retaining Play compact bindings + Frame-0 root cut FX → Play from **4.19–4.84 s → 1.13–1.15 s** (`mpc-display.md:568`). |
| **P2** | **Pre-populate read-only presentation before host exposure and after every program/chunk restore.** Construct the initial read-only bindings before the host paints the cold page, without acquiring gesture ownership or altering Q groups. | `shell-mpc/synthyboy_mpc.cpp:2256-2276` (`preparePlayPresentation`), called at `synthyboy_mpc.cpp:3715` (`effSetProgram`) and chunk restore; `shell-mpc/native_voice_controls.h:41-45` (constructor seeds `voicectl.s1.pan`). | Key 61: eliminated the **4.04 s** cold-load separation between Play ENV dials and Play OSC/LFO controls across three cold boots (`mpc-display.md:586-594`). |
| **P3** | **Batch handle writes on the display thread: publish values, kinds, indicators, and text lines BEFORE the layout/view gate, suppress no-op reports, and never emit tear-down zero bursts on `leftTab()`.** Each `native*Poll()` compares against `nativeUiReported[offset]` (`reportNativeValue`), forcing a report only on binding change (`needsReport`), action ack, or same-normalised-value text-format change (e.g. PAN `MID` vs DETUNE `0 c`). Emit the layout/view gate last in the same poll so the host never renders an unpopulated layout. | `shell-mpc/synthyboy_mpc.cpp:2164-2177` (`reportNativeValue`), `2740-2763` (`nativeVoicePoll` format-change force), `2311-2330` (`nativeLfoPoll` values before `kLayout`), `2670-2694` (`nativeEnvPoll` fields before `kEnvView`), `3193-3197` (display-thread poll order: `overlayPoll` → `playPoll` → `nativeUiPoll` → `qPoll`). | Key 61: Voice `DETUNE` kept its `0 c` readout at identical midpoint 0.5 on return by **444 ms** (`mpc-display.md:551,553`). Avoiding redundant writes saves idle display CPU (`key61-native-control-arrival.md:21`: 3.84 → 1.28 µs/poll). |
| **P4** | **Protect the first paint of EVERY layout entry (0 → active) AND every visible-to-visible layout change (`nativePagePaintUntil` ≥ `kNativePaintWindowMs` = 150 ms) and use `requestNativeControlNames()`.** Never fire `requestProxyNames()` raw on a native rebind or layout change; arm `nativePagePaintUntil` in `reportNativeValue` and set `nativeNamesAwaitingPaint = true` via `requestNativeControlNames()`. | `shell-mpc/synthyboy_mpc.cpp:1578` (`kNativePaintWindowMs = 150`), `2168-2174` (`enteringLayout` / `switchingLfoLayout`), `2834-2838` (`requestNativeControlNames`), `2847-2856` (`flushProxyNames`). | Key 61: coalescing editor notifications + 150 ms paint window cut first-visible arrival on 7 pages: **OSC A 3.58→1.34 s, OSC B 3.39→1.50 s, ENV 2.59→0.65 s, LFO 2.56→0.52 s, FX 3.91→1.21 s, Filter 3.33→0.98 s, Mod 3.44→0.68 s** (`mpc-display.md:598-612`). Adding `switchingLfoLayout` fixed LFO 1 → RND 1 graph/control split from **0.86–1.04 s → 0.31–0.59 s** together (`mpc-display.md:616`). |
| **P5** | **Defer all page-switch `audioMasterUpdateDisplay` metadata refreshes on a single trailing-edge timer (`kPageSwitchSettleMs` = 800 ms after the latest request, capped at `kPageSwitchMaxMs` = 2500 ms), while sending Q-Link values immediately.** On a Q-group / page switch, `qBank.poll` immediately sends `audioMasterAutomate` for Q 1..16 values, then sets `pageSwitchNamesPending = true` via `requestProxyNames()`. Never bypass this queue with bare `audioMasterUpdateDisplay` or un-deferred `requestProxyNames()` on page switches. | `shell-mpc/synthyboy_mpc.cpp:1579-1587` (`kPageSwitchSettleMs = 800`, `kPageSwitchMaxMs = 2500`), `2812-2823` (`qPoll`), `2840-2865` (`requestProxyNames` / `flushProxyNames`, trailing-edge fix in `ac8b3c34`). | XL / Key 61: one `audioMasterUpdateDisplay` triggers 14,400–36,650 param-info calls (only 16–38 ms in plugin, rest on host main thread: `mpc-display.md:620`, `metadata-timing.csv`). Deferring 800 ms lets Key 61 finish its 435–467 ms paint first; trailing edge prevents mid-burst stalls during rapid Env 2..5 taps (`ac8b3c34`). **Device timing of `kPageSwitchSettleMs` (Master → Tuning) and `ac8b3c34` (Env rapid taps) is `UNSURE` — not yet re-measured on Key 61** (`mpc-display.md:618-622`). |
| **P6** | **Report view switches immediately and avoid spurious Q-group clears.** On a detected tab change, update `current_`, `needFrame_ = true`, and `statusDue_ = true` immediately; only clear `setQGroup(nullptr, 0)` when a host parameter modal opens, not on routine tab exit. | `shell-mpc/overlay/controller.h:628-643` (`statusDue_ = true`, `ac8b3c34`), `1031` (`setQGroup(nullptr, 0)` only on native modal), `1235-1241` (`view.onShown()`). | Eliminates the 1-second `overlay-status.txt` `view:` lag during rapid switches (`ac8b3c34`). |
| **P7** | **Never leak `uiFullOpen = true` across `leftTab()` / `onHidden()`.** Any screen that sets `setUiFullOpen(true)` (even if masked while active by another flag like `nativeMixerActive`) must explicitly reset `setUiFullOpen(false)` on exit so `overlayWantedValue()` never transiently emits Frame 24 during tab handoff. | `shell-mpc/synthyboy_mpc.cpp:3030-3036` (`hideHostCells = uiFullOpen && !nativeMixerActive`), `shell-mpc/overlay/controller.h:972,986`, `shell-mpc/overlay/macro_ui.h:396` (good: calls `setUiFullOpen(false)` on `leftTab`) vs `shell-mpc/overlay/mixer_ui.h:256,307` (defect: leaks `uiFullOpen = true`). | Proven indirectly by the Frame-24 masking regressions on Master, FX, ENV, LFO (`mpc-display.md:549,568,576-580`); isolated measurement of fixing the Mixer exit leak is `UNSURE` (not yet measured on device). |

---

## 2. Per-Screen Audit (All 14 Screen Groups, 37 Tabs)

### 2.1 Counting Script (`skin/TUI.json` + `skin/defs_*.json`)

The skin is split into `skin/TUI.json` plus 13 flat wrapped import files (`skin/defs_*.json`) and preserves duplicate JSON keys (`scripts/skin-format.py`), so the counter uses `object_pairs_hook=list` and expands every tab's `componentDefinition` recursively through all `localComponentDefinitions`, resolving `handle remapping` chains top-down for both `bounds.additionalInvalidatingHandles` and `componentData.data.handleName`. A gate is counted as **hidden-by-default** (`HidL` = top-most hidden layer boundaries, `UndH` = total expanded nodes inside hidden-at-0 subtrees) when its resolved expression evaluates false at default parameter value `0` (`NonZero/...`, `IndexedEnabling/k/...` with `k > 0`, `RangedEnabling/lo/...` with `lo > 0`). Decoded RGBA bytes are summed once per unique PNG per tab (`w * h * 4` from PNG IHDR).

```python
import json, os, re, struct

IMAGE_KEYS = {"image", "onImage", "offImage", "filmStrip", "thumbImage",
              "revealedImage", "inactiveImage", "peakImage", "rmsImage"}

def load_pairs(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f, object_pairs_hook=list)

def get_val(pairs, key, default=None):
    for k, v in pairs:
        if k == key:
            return v
    return default

def png_rgba_bytes(fname):
    p = os.path.join("skin", fname)
    if not os.path.isfile(p):
        return 0
    with open(p, "rb") as f:
        hdr = f.read(24)
    if len(hdr) < 24 or hdr[:8] != b"\x89PNG\r\n\x1a\n":
        return 0
    w, h = struct.unpack(">II", hdr[16:24])
    return w * h * 4

tui = load_pairs("skin/TUI.json")
pd = get_val(tui, "pageData", [])
cd = get_val(pd, "componentDefinitions", [])
defs = {get_val(i, "key"): get_val(i, "value")
        for i in get_val(cd, "localComponentDefinitions", [])}
for imp in get_val(cd, "importFiles", []):
    obj = load_pairs(os.path.join("skin", imp))
    inner = get_val(obj, "componentDefinitions", obj)
    for i in get_val(inner, "localComponentDefinitions", []):
        defs[get_val(i, "key")] = get_val(i, "value")

def resolve_handle(h, env):
    if not isinstance(h, str):
        return h
    for _ in range(8):
        if h in env and env[h] != h:
            h = env[h]; continue
        if "/" in h:
            parts = h.split("/")
            if parts[-1] in env and env[parts[-1]] != parts[-1]:
                h = "/".join(parts[:-1] + [env[parts[-1]]]); continue
        break
    return h

def is_gate(s):
    return isinstance(s, str) and any(
        t in s for t in ("IndexedEnabling/", "RangedEnabling/", "NonZero/", "Zero/"))

def hidden_at_zero(s):
    if not isinstance(s, str):
        return False
    if "NonZero/" in s:
        return True
    m = re.search(r"IndexedEnabling/(\d+)/(\d+)/", s)
    if m:
        return int(m.group(1)) > 0
    m = re.search(r"RangedEnabling/([0-9.]+)/([0-9.]+)/", s)
    return bool(m and float(m.group(1)) > 0.0)

def collect_images(pairs, out):
    if isinstance(pairs, list):
        if pairs and isinstance(pairs[0], tuple):
            for k, v in pairs:
                if k in IMAGE_KEYS and isinstance(v, str) and v:
                    out.append(v)
                elif k != "componentsData":
                    collect_images(v, out)
        else:
            for item in pairs:
                collect_images(item, out)

def collect_overlays(pairs, out):
    if isinstance(pairs, list):
        if pairs and isinstance(pairs[0], tuple):
            if get_val(pairs, "handler") == "Show Overlay":
                ad = get_val(pairs, "additionalData")
                if isinstance(ad, str) and ad:
                    out.add(ad)
            for k, v in pairs:
                if k != "componentsData":
                    collect_overlays(v, out)
        else:
            for item in pairs:
                collect_overlays(item, out)

def expand(comp_def, env, st, anc_hid=False):
    collect_images(comp_def, st["imgs"])
    collect_overlays(comp_def, st["ovls"])
    for ch in get_val(comp_def, "componentsData", []):
        st["nodes"] += 1
        cdata = get_val(ch, "componentData", [])
        ctype = get_val(cdata, "type", "")
        bounds = get_val(ch, "bounds", [])
        cenv = {get_val(e, "key"): resolve_handle(get_val(e, "value"), env)
                for e in get_val(get_val(ch, "handle remapping", []), "map", [])}
        gates = [resolve_handle(h, env)
                 for h in get_val(bounds, "additionalInvalidatingHandles", [])
                 if is_gate(resolve_handle(h, env))]
        dh = get_val(get_val(cdata, "data", []), "handleName", "")
        if dh:
            rdh = resolve_handle(dh, cenv if dh in cenv else env)
            if is_gate(rdh):
                gates.append(rdh)
        hid_here = any(hidden_at_zero(g) for g in gates)
        if gates:
            st["gated_nodes"] += 1
            st["gate_exprs"] += len(gates)
            if hid_here and not anc_hid:
                st["hid_layers"] += 1
        now_hid = anc_hid or hid_here
        if now_hid:
            st["under_hid"] += 1
        collect_images(ch, st["imgs"])
        collect_overlays(ch, st["ovls"])
        if ctype in defs:
            expand(defs[ctype], cenv, st, now_hid)
```

Total expanded nodes across all 37 tabs at commit `ac8b3c34`: **25,951** (of which `Osc` 5 tabs = 9,890 and `LFO/RND` 14 tabs = 8,806 = 72 % of the whole skin). Note that `Play` dropped from 2,910 nodes (`mpc-display.md:619`, before commit `26e073ca`) to **375** nodes after the owner removed Play's expanded sub-views.

### 2.2 Per-Screen Comparison Table

| Screen Group (Tabs) | Expanded Nodes / Tab (Family Total) | Images: Unique / Refs (Decoded MB) | Gates: Nodes / Exprs; Hidden@0 Layers (`UndH` Nodes; `Ovl` Menus) | Plugin Publish / Leave / Poll Path (`file:line`) | Key 61 Measured Arrival | Rules Broken |
|---|---|---|---|---|---|---|
| **Play** (1) | **375** (375) | 37 / 123 (8.4 MB) | 110 / 206; **49** layers (337 nodes; 11 ovls) | Enter/Update: `play_view.h:187-222`, `play_env_lfo.h:59-69`, `osc_ui.h:654-673` → `synthyboy_mpc.cpp:461-467,769-784`. Cold init: `preparePlayPresentation` (`synthyboy_mpc.cpp:2256-2276,3715`). Leave: `play_view.h:322-338` → `suspendNativePlayOsc`/`suspendNativePlayEnvLfo` (`native_play_osc.h:38-42`, `native_play_env_lfo.h:51-55`). Poll: `playPoll` (`2960-3017`), `nativePlayOscPoll` (`2278-2288`), `nativePlayEnvLfoPoll` (`2290-2309`). | 0.67–0.68 s (`arrival-comparison.csv:73,89`); 0.69–0.96 s post-fix (`mpc-display.md:574,580,592`); 4.19–4.84 s was pre-fix. Current 375-node skin: `UNSURE`. | **S2** (per-source `NativeActive` Osc layers: `playOscSourceTitled` ×6, `Pan` ×5, `Level` ×4, `Route` ×4), **P4/P5** (`nativePlayOscPoll:2287`, `nativePlayEnvLfoPoll:2308`, `playPoll:3003` call bare `requestProxyNames()` without `nativeNamesAwaitingPaint`/`pageSwitchNamesPending`). |
| **Macros** (1) | **272** (272) | 114 / 298 (7.6 MB) | 145 / 182; **19** layers (188 nodes; 0 ovls) | Update: `macro_ui.h:183,257-259,576-588` (`publishNativeMacroTargets`, `setNativeMacroText`, `setNativeMacroSubpage`, `setUiFullOpen(pageTop_!=0)`). Leave: `macro_ui.h:390-397` (`publishNativeMacroTargets(false)`, `setUiFullOpen(false)`, keeps `kMacroSubpage=0` & `kMacroText`). Poll: `synthyboy_mpc.cpp:2581-2591,2620-2626`. | 0.50–0.52 s (`arrival-comparison.csv:64,80`). Post-keyboard (`95` hidden nodes): `UNSURE`. | **S3** (95-node `native keyboard overlay` + 62-node `nativeModulationEditor` + 21-node list hidden inside tab), **S4** (114 unique PNGs, mostly keyboard keys), **P2** (`nativeMacroText` not pre-populated before first visit, `synthyboy_mpc.cpp:724,1653`). |
| **Osc A / B / C / Sub / Noise** (5) | **1,978** (9,890) | 144 / 671 (**23.8 MB**) | 585 / 809; **13** layers (**1,949** nodes; 8 ovls) | Update: `osc_page.h:62-96,361-486` (`setNativeOscState`, `setNativeOscAbcState`, `setNativeOscSettings`, `setNativeOscDetail`, `publishNativeNoteMask`, `osc_ui.h:282-283` `setUiFullOpen(pageTop_!=0)`). Leave: `osc_page.h:228-238` tears down ALL (`setNativeOscState({})`, `setNativeOscSettings(Hidden)`, `setNativeOscDetail(Hidden)`, `setNativeOscAbcState(0,"")`). Poll: `nativeOscPoll` (`synthyboy_mpc.cpp:2371-2392`), `nativeOscSettingsPoll` (`2451-2473`), `nativeOscDetailPoll` (`2413-2449`). | **1.31–1.69 s** first & revisit across all 5 tabs (`arrival-comparison.csv:68-72,84-88`); ENV → Osc A **2.05 s** (`mpc-display.md:614`). | **S1** (1,978 nodes/tab, 9,890 family total), **S2** (`oscNativeAbcSettings` ×3 + `Off` ×3 = 951 nodes, `oscNativeTune` ×3 = 135, 6 headers = 149, all inside every tab including Sub/Noise), **S3** (1,949/1,978 hidden@0 incl. 385-node `oscNativeDetail` + 62-node `nativeModulationEditor` + root gated on `kOscSettingsView`/`kOscBase`), **S4** (144 unique PNGs / **23.8 MB**), **P1/P2** (full teardown on `leftTab`, even Osc A → Osc B; no read-only cache), **P4** (`kOscBase`/`kOscSettingsView` absent from `reportNativeValue:2168-2174` `enteringLayout`). |
| **Env 1–6** (6) | **155** (930) | 30 / 112 (6.9 MB) | 47 / 47; **18** layers (136 nodes; 1 ovl) | Update: `env_ui.h:197,204-234,685-729` (`setUiFullOpen(stack.depth()>1)`, `setNativeEnvEditor(visible)`, `setNativeEnvTargetCount`, `publishNativeTags`). Leave: `env_ui.h:517-525` (`setNativeEnvEditor(false, nullptr)`, `hideNativeTargets`). Poll: `nativeEnvPoll` (`synthyboy_mpc.cpp:2670-2694`, calls `requestNativeControlNames()`). | 0.65–0.67 s single tap (`mpc-display.md:606`, `arrival-comparison.csv:60,76`); Osc → Env 1.35–2.1 s (owner); Env 2..5 burst fixed in `ac8b3c34` (`UNSURE` on device). | **S3** (136/155 hidden@0: root ADSR gated on `kEnvView=1` + 62-node `nativeModulationEditor` + 28-node `oscNativeList`), **P1/P2** (`leftTab` tears down `kEnvView=0` and tags, blanking host paint on Env *i* → Env *j*), **P4** (`kEnvView` absent from `reportNativeValue:2168-2174`; relies only on `requestNativeControlNames` 150 ms). |
| **LFO 1–10 + RND 1–4** (14) | **629** (8,806) | **192** / 446 (**15.3 MB**) | 265 / 332; **103** layers (**607** nodes; 8 ovls) | Update: `lfo_ui.h:204,212-222,739-789` (`setUiFullOpen(stack.depth()>1)`, `setNativeLfoState`, `publishNativeEditor/Detail/Pick`, `publishTexts`). Leave: `lfo_ui.h:1104-1116` tears down ALL (`setNativeLfoState({})`, `publishNativeEditor/Detail({})`, `hideNativeTargets`). Poll: `nativeLfoPoll` (`synthyboy_mpc.cpp:2311-2330`, `switchingLfoLayout` at `2171-2174`), `nativeLfoEditor/Detail/PickPoll` (`2179-2254`). | 0.49–0.53 s LFO 1, 0.30–0.33 s RND (`arrival-comparison.csv:63,74,79,90`); second pass 1.03 s (`mpc-display.md:614`). Post-keyboard (`+95` nodes ×14 tabs): `UNSURE`. | **S1** (629 nodes × 14 tabs = 8,806 total), **S2** (`lfoNativePickTarget` ×36 = 144 nodes inside `lfoNativePicks`), **S3** (607/629 hidden@0: root gated on `kLfoBase+kLayout` + 153-node `lfoNativePicks` + 95-node `native keyboard overlay` ×14 + 62-node `nativeModulationEditor`), **S4** (**192 unique PNGs** = highest in skin, 15.3 MB), **P1/P2** (full teardown on `leftTab`, even LFO *i* → LFO *j*; no read-only cache). |
| **Filter 1–2** (2) | **477** (954) | 45 / 213 (8.8 MB) | 132 / 236; **15** layers (457 nodes; 2 ovls) | Update: `filter_ui.h:301-320,925-947` (`setNativeFilterControls(nativeViewFrame())`, `setNativePzToolbar`, `publishNativeTexts`, `setUiFullOpen(nativeViewFrame()==0)`). Leave: `filter_ui.h:335-344` tears down ALL (`setNativeFilterControls(0)`, `setNativePzToolbar(0)`, `clearNativeTexts`). Poll: `nativeFilterPoll` (`synthyboy_mpc.cpp:2332-2369`, `enteringLayout` on `kFilterBase` at `2169`, calls bare `requestProxyNames()` at `2368`). | 0.82–1.03 s (`mpc-display.md:609,614`, `arrival-comparison.csv:61,77`); owner reports fast switching on 2026-11-28 (`TODO.md:58`, unmeasured `UNSURE`). | **S1** (477 nodes, near ceiling), **S2** (`filterNativeStyle` ×20 = 80 nodes + 16 `Off` dials = 64 + `OutOff/On` twins), **S3** (457/477 hidden@0: root gated on `kFilterBase`, plus `nativeModulationEditor` 62 + `oscNativeList` 28), **P1/P2** (`leftTab` resets `kFilterBase=0` and clears texts; no read-only cache), **P4/P5** (`nativeFilterPoll:2368` calls bare `requestProxyNames()`, and `enteringLayout:2168` only guards `0 → >0`, not Filter 1 ↔ Filter 2). |
| **Note** (1) | **169** (169) | 33 / 120 (1.3 MB) | 54 / 55; **16** layers (143 nodes; 1 ovl) | Shared `FilterUi` (`filter_ui.h:174-176,278-283,301-320,929-947`: `nativeViewFrame()=3`, `publishNativeTexts` writes `CurveUsed*`). Leave: `filter_ui.h:335-344` (`setNativeFilterControls(0)`, `clearNativeTexts`). Poll: `nativeFilterPoll` (`synthyboy_mpc.cpp:2332-2369`). | Owner reports fast on 2026-11-28 (`TODO.md:58`); no frame-timed capture (`UNSURE`). | **S3** (143/169 hidden@0: root `filterNativeCurveSurface` gated on `kFilterBase=3`, plus `nativeModulationEditor` 62 + `oscNativeList` 28; uses `NonZero` on `CurveUsed` text lines `Parameter 3882..3888`), **P1/P2** (`leftTab` tears down `kFilterBase=0` and curve texts), **P4/P5** (bare `requestProxyNames()` at `2368`). |
| **Velo** (1) | **169** (169) | 33 / 120 (1.3 MB) | 54 / 55; **16** layers (143 nodes; 1 ovl) | Identical to **Note** (`filter_ui.h:174-176`: `nativeViewFrame()=4`). | Owner reports fast on 2026-11-28 (`TODO.md:58`); no frame-timed capture (`UNSURE`). | Same as **Note**: **S3**, **P1/P2**, **P4/P5**. |
| **Mixer** (1) | **320** (320) | 23 / 216 (4.4 MB) | 161 / 210; **28** layers (98 nodes; 1 ovl) | Update: `mixer_ui.h:243-259` (`setNativeMixerMeter`/`StereoMeter`, **`setUiFullOpen(true)` at `256`**, `publishStripVisuals`, `setNativeMixerActive(nativeBase())` at `258`). Leave: `mixer_ui.h:305-314` (`setNativeMixerActive(false)` **WITHOUT `setUiFullOpen(false)`**, zeros 80 meter handles!). Poll: `nativeMixerPoll` (`synthyboy_mpc.cpp:2535-2556` emits 80 meter + 20 solo/mute zero writes on leave), `overlayWantedValue` (`3032`: `uiFullOpen && !nativeMixerActive` → **Frame 24 leak!**). | 1.67 s (`mpc-display.md:614`); 4.25–4.40 s with incorrect settled controls (`arrival-comparison.csv:66,82`). | **S5 / P7 (CRITICAL BUG)** (`mixer_ui.h:256,307` leaks `uiFullOpen=true` on `leftTab` → `synthyboy_mpc.cpp:3032` asserts Frame 24 on exit, hiding Mix/Play/Voice/Macros controls until next view clears it!), **P1/P3** (`leftTab` zeros `nativeMixerActive`, blanking Solo/Mute visuals via `synthyboy_mpc.cpp:1931-1946` and blasting 100 `audioMasterAutomate` writes on leave). |
| **FX** (1) | **2,977** (2,977) | 72 / **2,128** (9.9 MB) | **1,686 / 2,406**; **239** layers (**2,966** nodes; 5 ovls) | Update: `fx_ui.h:197-210,1057-1101` (`setUiFullOpen(false)`, `setNativeFxChain`, `setNativeFxBands`, `setNativeFxHeader`, `syncNativeCells`, `publishNativeTexts`). Leave: `fx_ui.h:149-156` tears down ALL 4 banks (`setNativeFxCells(0)`, `setNativeFxHeader({})`, `setNativeFxChain({})`, `setNativeFxBands({})`). Poll: `nativeUiPoll` (`synthyboy_mpc.cpp:2475-2511,2563-2568,2638-2668` → bare `requestProxyNames()` at `2644,2667`; `qPoll:2810-2816` `fxQNamesDue` skips `pageSwitchNamesPending`). | Tab entry: **1.15–1.34 s** (`mpc-display.md:608,614`, `arrival-comparison.csv:62,78`). In-tab Distortion ↔ Chorus: **0.246–0.271 s** (`mpc-display.md:564`). | **S1** (**2,977 nodes** = largest single tab in skin; 2,128 image refs; 2,406 gate exprs), **S2** (`fxNativeHeader` ×32 = **960 nodes** + `NativeActive/Inactive fxNativeParameterCells` = **1,387 nodes** + `fxNativeBandLane` ×3 = **432 nodes**), **S3** (2,966/2,977 hidden@0), **P1/P2** (tears down all 4 banks on `leftTab`; no read-only cache), **P4/P5** (`synthyboy_mpc.cpp:2644,2667,2811` call bare `requestProxyNames()`; `kFxLayout` absent from `enteringLayout:2168-2174`; `fxQNamesDue` bypasses `pageSwitchNamesPending`). |
| **Mod** (1) | **429** (429) | 62 / 285 (10.2 MB) | 102 / 183; **84** layers (417 nodes; 1 ovl) | Update: `mod_ui.h:203-219,445-700` (`nativeTargets_`, `publishNativeNavigation`, `publishNativeMatrix`, `publishNativeBoard`, `publishNativeText`, `setUiFullOpen(full)`). Leave: `mod_ui.h:100-109` tears down ALL (`setNativeModMatrix(Hidden)`, `setNativeModNavigation(0)`, `setNativeModDetailNavigation(nullptr)`, `setNativeModBoard(0)`, clears **all** `nativetext::kLines`). Poll: `nativeModPoll` (`2722-2738`), `nativeModNavigationPoll` (`2765-2790`), `nativeModTargetsPoll` (`2696-2720`), `nativeModBoard` (`2607-2617`), `nativeText` (`2618-2619`). | 0.50–0.68 s (`mpc-display.md:610`, `arrival-comparison.csv:67,83`). | **S2** (`modNativeRowNormal` ×7 + `modNativeRowScroll` ×7 = 224 duplicated row nodes), **S3** (417/429 hidden@0: root gated on `kModView`/`kModNavigation`, plus `nativeModulationEditor` 62 + `NonZero/Parameter 3806`), **S4** (10.2 MB decoded), **P1/P2/P3** (`leftTab` tears down all 4 banks AND clears every `nativeText` line across the whole plugin — clobbering/flooding `audioMasterAutomate`), **P4** (`kModView`/`kModNavigation`/`kModBoardControl` absent from `enteringLayout:2168-2174`). |
| **Master** (1) | **149** (149) | 37 / 53 (8.9 MB) | 38 / 43; **36** layers (143 nodes; 2 ovls) | Update: `master_ui.h:182-186` (`setUiFullOpen(pageTop_!=0)`, `setNativeMasterOutput`, `setNativeMasterView(Main)`). Leave: `master_ui.h:197-201` → `leaveNativeMasterView(Main)` (`synthyboy_mpc.cpp:589-609`: keeps `nativeMasterSkinPanel=Main`, disarms `nativeMasterView=Hidden`). Poll: `nativeMasterPoll` (`synthyboy_mpc.cpp:2394-2411`). | **0.33–0.49 s** (`mpc-display.md:553`, `arrival-comparison.csv:65,81`). Tuning → Master: `UNSURE` (see S3). | **S3** (36 root controls gated on shared `IndexedEnabling/1/3/Parameter 2879` = `kMasterView`, even though Master is now its own physical tab — so **Tuning → Master** starts with `Parameter 2879 == 2`, hiding Master until overlay detection!). |
| **Tuning** (1) | **87** (87) | 32 / 33 (2.1 MB) | 27 / 27; **17** layers (81 nodes; 2 ovls) | Shared `MasterUi` (`master_ui.h:152,182-186`: `side_=SideTuning`, `setNativeMasterView(Tuning)`). Leave: `master_ui.h:199` (`leaveNativeMasterView(Tuning)`). Poll: `nativeMasterPoll` (`synthyboy_mpc.cpp:2394-2411`). | Pre-deferral Master → Tuning: 2.72–2.90 s (`key61-voice-master-latency-20261125/README.md:7`); post-`kPageSwitchSettleMs`: `UNSURE` (`mpc-display.md:618-622`). | **S3** (16 root controls gated on shared `IndexedEnabling/2/3/Parameter 2879` = `kMasterView` — so **Master → Tuning** starts with `Parameter 2879 == 1`, hiding Tuning's body until overlay detects Tuning and `nativeMasterPoll` flips `2879` from 1 → 2!), **P4** (`kMasterView` absent from `enteringLayout:2168-2174` when switching 1 ↔ 2). |
| **Voice** (1) | **424** (424) | 51 / 118 (9.1 MB) | 83 / 145; **53** layers (264 nodes; 2 ovls) | Update: `voice_ui.h:193-197` (`setQVirtual`, `setNativeVoiceState`, `setUiFullOpen(pageTop_!=0)`). Leave: `voice_ui.h:246` (`setNativeVoiceState({})` → disarms writes while `nativeVoice.displayCell()` keeps read-only selection cache, `native_voice_controls.h:41-59`). Poll: `nativeVoicePoll` (`synthyboy_mpc.cpp:2740-2763`, forces report on row text-format change). | **0.32–0.50 s** (`mpc-display.md:553`, `arrival-comparison.csv:75,91`). **Best end-to-end reference screen.** | Minor **S2/S3** only: 40 step sliders gated on `voicectl.length` (expected) + `voiceSelectedValues` ×7 (98 nodes) & `voiceColumnValues` ×5 (45 nodes) label overlays. **Passes P1–P7 cleanly.** |

---

## 3. Load-Cost & Pattern-Distance Rankings and Worst-Five Remediations

### 3.1 Dual Ranking (All 14 Screen Groups)

| Rank (Load Cost) | Screen Group | Load-Cost Drivers | Rank (Distance from Pattern) | Pattern Distance Summary |
|---:|---|---|---:|---|
| **1** | **Osc A / B / C / Sub / Noise** | 1,978 nodes × 5 tabs (9,890 total), 23.8 MB decoded PNGs, 1,949 hidden@0, full teardown on `leftTab`, 1.31–1.69 s warm revisit / 2.05 s from Env. | **1** | Breaks **7** rules (S1, S2, S3, S4, P1, P2, P4). |
| **2** | **FX** | 2,977 nodes (heaviest single tab), 2,128 image refs, 2,406 gate exprs, 2,966 hidden@0, full 4-bank teardown on `leftTab`, 1.15–1.34 s entry. | **2** | Breaks **6+** rules (S1, S2, S3, P1, P2, P4/P5). |
| **3** | **Mixer** | 1.67 s (`mpc-display.md:614`) / 4.25–4.40 s (`arrival-comparison.csv`), **Frame-24 exit leak** (`mixer_ui.h:256,307`) poisons departing transitions to Play/Macros/Voice/Mix + 100 zero writes on `leftTab`. | **6** | Breaks **4** rules (S5/P7 critical cross-tab leak, P1, P3), despite clean 320-node skin. |
| **4** | **LFO 1–10 + RND 1–4** | 629 nodes × 14 tabs (8,806 total), **192 unique PNGs** (highest in skin) / 15.3 MB, 607 hidden@0 incl. 95-node keyboard ×14, full teardown on `leftTab` (post-keyboard timing `UNSURE`). | **3** | Breaks **6** rules (S1, S2, S3, S4, P1, P2). |
| **5** | **Filter 1–2** | 477 nodes × 2 tabs, 457 hidden@0 (`filterNativeStyle` ×20 + 16 `Off` dials), full teardown on `leftTab`, bare `requestProxyNames()`, 0.82–1.03 s arrival. | **4** | Breaks **6** rules (S1, S2, S3, P1, P2, P4/P5). |
| **6** | **Mod** | 429 nodes, 10.2 MB decoded, 417 hidden@0 (`modNativeRowNormal/Scroll` 224 duplicated nodes), full teardown + global `nativeText` wipe on `leftTab`, 0.50–0.68 s arrival. | **5** | Breaks **6** rules (S2, S3, S4, P1, P2/P3, P4). |
| **7** | **Play** | 375 nodes (down from 2,910 after expand removal), 8.4 MB, read-only `suspend()` + `preparePlayPresentation()` in place; 0.67–0.68 s (pre-trim; post-trim `UNSURE`). | **7** | Breaks **2** rule groups (S2 per-source Osc layers, P4/P5 bare `requestProxyNames()`). |
| **8** | **Env 1–6** | 155 nodes × 6 tabs, 6.9 MB, 0.65–0.67 s single tap (`ac8b3c34` burst fix `UNSURE` on device). | **8** | Breaks **3** rules (S3 root `kEnvView` gate, P1/P2 teardown on `leftTab`, P4 `kEnvView` not in `enteringLayout`). |
| **9** | **Note** | 169 nodes, 1.3 MB (lowest decoded MB); timing `UNSURE` (owner reports fast). | **9** | Breaks **3** rules (S3 root `kFilterBase` gate + `NonZero` on text lines, P1/P2 teardown, P4/P5 bare `requestProxyNames()`). |
| **10** | **Velo** | 169 nodes, 1.3 MB; identical to Note (timing `UNSURE`). | **10** | Same as Note (S3, P1/P2, P4/P5). |
| **11** | **Macros** | 272 nodes, 7.6 MB, 114 unique PNGs (95-node keyboard + 62-node mod editor hidden inside tab), 0.50–0.52 s (post-keyboard `UNSURE`). | **11** | Breaks **3** rules (S3 hidden keyboard/mod editor, S4 114 PNGs, P2 un-prepopulated `nativeMacroText`). |
| **12** | **Tuning** | 87 nodes (smallest tab), 2.1 MB, **but** gated on shared `Parameter 2879 == 2` so Master → Tuning paints blank until overlay flip (post-`kPageSwitchSettleMs` timing `UNSURE`). | **12** | Breaks **2** rules (S3 shared `kMasterView` gate, P4 `kMasterView` not in `enteringLayout`). |
| **13** | **Master** | 149 nodes, 8.9 MB, retains `nativeMasterSkinPanel` on exit; **0.33–0.49 s** from Voice (Tuning → Master `UNSURE`). | **13** | Breaks **1** rule (S3 shared `kMasterView` gate). |
| **14** | **Voice** | 424 nodes, 9.1 MB, direct engine step params + `nativeVoice.displayCell()` cache + Frame 0 root; **0.32–0.50 s** arrival. | **14** | **Reference pattern** (passes P1–P7; minor S2/S3 label overlays only). |

*(Quick wins outside the Worst Five: removing the obsolete `IndexedEnabling/{1,2}/3/Parameter 2879` cross-tab gate on `skin/TUI.json:31640` `Master` and `skin/TUI.json:33085` `Tuning` eliminates the Master ↔ Tuning blank-on-entry race; scoping `mod_ui.h:107-108` `leftTab` to suspend instead of wiping all `nativeText` lines and collapsing `modNativeRowNormal`/`modNativeRowScroll` cuts Mod by ~112 nodes.)*

---

### 3.2 Concrete Remediations for the Worst Five

#### Worst #1 — Osc A / B / C / Sub / Noise (`1,978` nodes/tab, `23.8 MB`, `1.31–1.69 s`)

* **What to change (Skin + Plugin):**
  1. **Collapse per-source & ON/OFF duplicates in `skin/defs_osc.json`** (`oscNativeSettingsLayer` at `skin/defs_osc.json:22808`, `oscNativeAbcSettings`/`Off` at `:19772,21290`, `oscNativeTune` at `:28825`, `oscNativeSurface` at `:28248`): replace the 3× `oscNativeAbcSettings` + 3× `oscNativeAbcSettingsOff` (951 nodes), 3× `oscNativeTune` (135 nodes), and 6 separate source headers (149 nodes) with **one** shared retargeted ABC settings block (plus Sub/Noise deltas — or split `oscScreen` into `oscAbcScreen`, `oscSubScreen`, `oscNoiseScreen` so Sub and Noise never instantiate ABC/Wavetable/Granular/Spectral/Detail trees) and use a single shared `kOscAbcSettings` handle bank retargeted by the active tab's oscillator index (`osc_page.h` already knows `in.instance`). Expected node drop: **1,978 → ~450–600 nodes/tab** (−70 %) and decoded PNGs on Sub/Noise by >60 %.
  2. **Add a read-only display cache (`suspend()` + `renewInputRevisions()`) to `nativeOsc` and `nativeOscSettings`** (`shell-mpc/native_osc_controls.h:56-90`, `shell-mpc/synthyboy_mpc.cpp:1620-1627,2040-2071`, `shell-mpc/overlay/osc_page.h:228-238`): on `osc_page.h:228` `leftTab()`, stop calling `hideNative` / `hideNativeSettings` to zero `kOscBase` and `kOscSettingsView`; instead disarm writes (`suspended_ = true`) while keeping the last published source/routing/settings readable for host paint (matching `nativePlayOsc` at `shell-mpc/native_play_osc.h:38-60`). Pre-populate Osc A's presentation on startup / `effSetProgram` alongside `preparePlayPresentation()` (`synthyboy_mpc.cpp:2256-2276`).
  3. **Protect Osc layout transitions in `reportNativeValue` and `flushProxyNames`** (`shell-mpc/synthyboy_mpc.cpp:2168-2174,2371-2392,2451-2473`): add `nativeui::kOscBase + nativeui::osc::kVisibility` and `nativeui::kOscSettingsView` (both `0 → >0` and distinct visible-to-visible switches, like `switchingLfoLayout`) to `nativePagePaintUntil = steadyMs() + kNativePaintWindowMs`, and route `nativeOscSettingsPoll`/`nativeOscDetailPoll` through `pageSwitchNamesPending` when the oscillator tab switches.
* **Single device measurement that confirms it:** Key 61 (`Sbtest` / `Acid Saw Reso`) interleaved A/B/A traversal `Env 1 → Osc A → Osc B → Osc C → Sub → Noise` using the `.hw-local/mpc-perf/key61-page-arrival-u0xtIq/` touch-to-settled-composite probe (`1280×690` primary + overlay): confirm every Osc tab's warm revisit drops from **1.31–1.69 s** (and `Env 1 → Osc A` from **2.05 s**) to **≤ 0.65 s** with zero blank-body intermediate frames and all 2,637 engine parameters unchanged on exit.

#### Worst #2 — FX (`2,977` nodes, `2,128` image refs, `2,406` gate exprs, `1.15–1.34 s`)

* **What to change (Skin + Plugin):**
  1. **Collapse `fxNativeHeader` × 32 (960 nodes) to ONE retargeted header** (`skin/defs_fx.json:3336` `fxNativeHeader`, instantiated 32× inside `skin/defs_fx.json:20455` `fxNativeEditorSurface`, and `shell-mpc/native_fx_header.h:75-149`, `shell-mpc/synthyboy_mpc.cpp:2045-2057,2475-2491`): the plugin already tracks the single active `state.item` and serves its title/path/notice strings from `nativeFxHeader.label()` (`synthyboy_mpc.cpp:4422-4425`). Bind ONE `fxNativeHeader` instance (30 nodes) to a single set of 4 active handles (`Power`, `Rack`, `Page`, `Remove`) instead of 32 gated copies (eliminates **930 nodes**, ~600 image refs, and >300 gates immediately; note that rack menu count differs between slot 0..12 and pool 13..31, so at most 2 rack menu actions are needed).
  2. **Audit/trim `NativeActive fxNativeParameterCells` (1,387 nodes = 2 × 694)** (`skin/defs_fx.json:7825` `fxNativeParameterCells`, `skin/defs_fx.json:20455`): investigate whether the `NativeInactive` twin (693 nodes) can share cell geometry or bind only active cells + one dim-overlay/inactive frame (`UNSURE` whether the host accepts a filmstrip/colour swap without duplicating all 179 cells — confirm via `mpc-expert` before skin edit).
  3. **Retain read-only display cache on `fx_ui.h:149-156` `leftTab()`** (`shell-mpc/overlay/fx_ui.h:149-156`, `shell-mpc/native_fx_cells.h:120-163`, `shell-mpc/native_fx_header.h:75-105`, `shell-mpc/native_fx_chain.h:66-100`, `shell-mpc/native_fx_bands.h:120-150`): replace the 4-bank tear-down (`setNativeFxCells(0, nullptr)`, `setNativeFxHeader({})`, `setNativeFxChain({})`, `setNativeFxBands({})`) with `suspend()` (disarming writes while keeping `layout_`, `state_`, chain cards, and cell links readable until return + `renewInputRevisions()`).
  4. **Unify FX metadata deferral with `requestNativeControlNames` / `pageSwitchNamesPending`** (`shell-mpc/synthyboy_mpc.cpp:2168-2174,2643-2644,2666-2667,2809-2822`): add `nativeui::kFxLayout` to `enteringLayout`/layout-change paint protection, replace bare `requestProxyNames()` at `synthyboy_mpc.cpp:2644,2667` with `requestNativeControlNames()`, and keep `fxQNamesDue` (`2810-2816`) gated by `nativePagePaintUntil` so in-tab Distortion ↔ Chorus stays ≤ 0.27 s while FX tab entry gets full paint protection.
* **Single device measurement that confirms it:** Key 61 (`Sbtest` / `Acid Saw Reso`) A/B/A probe measuring both (a) `LFO 1 → FX` tab entry (target **1.15–1.34 s → ≤ 0.65 s**, no blank chain/editor frame) and (b) in-tab `Distortion ↔ Chorus` card switch confirming no regression from **0.25–0.27 s** and correct Q-Link popup names (`key61-screen-review-20261125` + `key61-page-arrival-u0xtIq` method).

#### Worst #3 — LFO 1–10 + RND 1–4 (`629` nodes × 14 tabs = `8,806` total, `192` unique PNGs / `15.3 MB`)

* **What to change (Skin + Plugin):**
  1. **Replace the 36 duplicated `lfoNativePickTarget` definitions (144 nodes × 14 tabs = 2,016 nodes)** (`skin/defs_lfo.json:16318` `lfoNativePickTarget`, `skin/defs_lfo.json:16458` `lfoNativePicks`, `skin/defs_lfo.json:18356` `lfoScreenTop3`): instead of instantiating 36 separate group-gated target `MenuOverlay` trigger cells on every LFO/RND tab, route ADD TARGET through the shared 7-row dynamic list (`oscNativeList` / `nativeui::modboard` + `nativeListText`, already used by Env/Filter/Osc/Macros, 28 nodes) or confirm with `mpc-expert` whether a single host-retargeted `MenuOverlay` works (`UNSURE`, SDK says `replacementStrings` are static per handle, `skin-host-behaviour.md:12`). That removes **125–144 nodes × 14 tabs = ~1,750–2,016 nodes** and eliminates ~36 per-group image/control slots.
  2. **Audit unique PNG count (192 PNGs / 15.3 MB, highest in skin)** (`skin/defs_lfo.json:12150,18356`): the 95-node `native keyboard overlay` inside `lfoScreenTop3` contributes ~95 key button PNGs to all 14 LFO/RND tabs (`~1,330` nodes across the family) even though it is only opened during Save-As/Rename inside the Drawn shape editor; check via `mpc-expert` whether `native keyboard overlay` can be shown as a single `Show Overlay` modal definition (`actions: Show Overlay`) rather than instantiated inline in all 14 LFO tabs + Macros (`UNSURE` until tested on Key 61).
  3. **Retain read-only display cache in `nativeLfo` on `lfo_ui.h:1104-1116` `leftTab()`** (`shell-mpc/native_lfo_controls.h:51-111`, `shell-mpc/overlay/lfo_ui.h:1104-1116`, `shell-mpc/synthyboy_mpc.cpp:2311-2330`): stop publishing `{}` (`layout = 0`) when leaving an LFO/RND tab; add `suspend()` (disarming `post()`/`take()` while preserving `values_` and `layout` for host reads) + `renewInputRevisions` on return so switching `LFO 1 → LFO 2` or `Env 1 → LFO 1` paints immediately without blanking.
* **Single device measurement that confirms it:** Key 61 (`Sbtest` / `Acid Saw Reso`) A/B/A probe timing `Env 1 → LFO 1 → LFO 2 → RND 1 → LFO 1` (composite `1280×690`, `key61-page-arrival-u0xtIq` method): confirm every transition settles in **≤ 0.50 s** with graph and native controls arriving in the same sample and zero blank intermediate frames, plus confirm LFO Save-As keyboard still opens/types cleanly if moved to a modal overlay.

#### Worst #4 — Filter 1–2 (`477` nodes × 2 tabs, `457` hidden@0, `0.82–1.03 s`)

* **What to change (Skin + Plugin):**
  1. **Collapse `filterNativeStyle` × 20 (80 nodes) and duplicated `Off` dial/out twins (144 nodes)** (`skin/defs_filter.json:2621` `filterNativeStyle`, `skin/defs_filter.json:4008` `filterNativeEditorSurface`, `skin/defs_filter.json:7275` `filterScreen`): replace the 20 per-model `filterNativeStyle` copies with ONE shared dynamic-text choice cell (or shared selector face + single `MenuOverlay` per distinct style-list size, like the OSC detail menu collapse that saved 952 nodes, `mpc-display.md:592`), and unify the 16 `filterNativeDial_top_uni_off_100x178` / `bi_off` / `side_off` / `OutOff` twins once `mpc-expert` confirms disabled-visual sharing (`UNSURE`). Even collapsing `filterNativeStyle` ×20 alone removes **~76 nodes/tab**.
  2. **Retain read-only display cache in `nativeFilter` on `filter_ui.h:335-344` `leftTab()`** (`shell-mpc/overlay/filter_ui.h:335-344,658-661`, `shell-mpc/synthyboy_mpc.cpp:514-525,2098-2103,2332-2369`): stop resetting `kFilterBase` to `0` (`setNativeFilterControls(0, ...)`) and stop wiping `FilterTitle`/`FilterBlock*`/`CurveUsed*` via `clearNativeTexts()` on tab exit; instead `suspend()` writes while keeping the last Filter/Note/Velo layout and texts readable until return + `renewInputRevisions()`.
  3. **Fix `nativeFilterPoll` paint protection and metadata queueing** (`shell-mpc/synthyboy_mpc.cpp:2168-2174,2367-2368`): change `enteringLayout` at `2169` so ANY change of `nativeui::kFilterBase` (not only `<= 0.0f → > 0.0f`, catching `Filter 1 ↔ Filter 2 ↔ Note ↔ Velo`) arms `nativePagePaintUntil`, and replace bare `requestProxyNames()` at `synthyboy_mpc.cpp:2368` with `requestNativeControlNames()`.
* **Single device measurement that confirms it:** Key 61 (`Sbtest` / `Acid Saw Reso`) A/B/A probe timing `Mix → Filter 1 → Filter 2 → Note → Velo → Filter 1` (`key61-page-arrival-u0xtIq` method): confirm `Filter 1` / `Filter 2` arrival drops from **0.82–1.03 s** to **≤ 0.55 s** with curve and native controls arriving in the same sample (eliminating the one-sample control lag noted in `mpc-display.md:614`).

#### Worst #5 — Mixer (`320` nodes, `1.67–4.40 s`, critical Frame-24 exit leak)

* **What to change (Plugin — 10-line fix, zero skin changes required):**
  1. **Eliminate the Frame-24 exit leak** (`shell-mpc/overlay/mixer_ui.h:256-258,305-314`, `shell-mpc/synthyboy_mpc.cpp:3030-3036`): change `mixer_ui.h:256` from `inst.setUiFullOpen(true)` to `inst.setUiFullOpen(! nativeBase())` (matching every other native root: `master_ui.h:182`, `voice_ui.h:196`, `fx_ui.h:197`, `env_ui.h:197`, `lfo_ui.h:204`), AND explicitly call `inst.setUiFullOpen(false)` in `mixer_ui.h:305` `leftTab()`. This removes the trap in `synthyboy_mpc.cpp:3032` (`uiFullOpen && !nativeMixerActive`) where leaving Mixer immediately flipped `Parameter 2675` to Frame 24 and hid host controls on the destination tab.
  2. **Stop blanking Solo/Mute visuals and flooding 100 zero writes on `leftTab()`** (`shell-mpc/overlay/mixer_ui.h:307-313`, `shell-mpc/synthyboy_mpc.cpp:1929-1946,2535-2556`): keep the last `nativeMixerSoloVisual` and `nativeMixerMuteVisual` readable when `nativeMixerActive == false` (remove the `if (! nativeMixerActive) return 0.0f;` blanking at `synthyboy_mpc.cpp:1931-1932,1940-1941` and stop sending 20 zero visual writes at `2553-2554`), and zero live meter readouts only when meters were non-zero (suppressing redundant `audioMasterAutomate` writes on idle tabs).
* **Single device measurement that confirms it:** Key 61 (`Sbtest` / `Acid Saw Reso`) A/B/A probe timing `FX → Mix → Play` and `Mix → Filter 1` (`key61-voice-master-latency-20261125` / `key61-page-arrival-u0xtIq` composite probe): confirm `Mix` settles in **≤ 0.55 s** (down from **1.67 s** / **4.25–4.40 s**), confirm `Mix → Play` shows zero Frame-24 blank-body frames on Play, and confirm dragging inactive OSC B controls on Mix still leaves all 2,637 engine values unchanged.

---

## 4. Page-Loading Pattern Checklist (for `AGENTS.md` / `docs/dsp-knowledge`)

1. **Compact tab tree:** keep each tab ≤ ~450 expanded components (never > 1,000); measure with the ordered-pairs expander over `skin/TUI.json` + `skin/defs_*.json`.
2. **Retarget, do not duplicate:** share one `<family>Screen` per repeating tab family and retarget one shared slot bank instead of per-state, per-item, per-source, or per-model copies.
3. **No blank-at-0 root gates:** bind standalone tab roots to direct engine params or always-valid cached handles, never a zero-default or cross-tab view gate; gate text-owned cells only on 0/1 `kTextGate` (`UNSURE` on Key 61).
4. **Lean artwork:** use plain `backgroundData` colour (`ff090d19`, `image: ""`) and shared control filmstrips; avoid embedding large hidden modals (e.g. keyboard) across repeating tabs.
5. **Ordinary root frame:** keep `setUiFullOpen(false)` at every native root and on `leftTab()`; assert Frame 24 (`Parameter 2675`) only for true plugin-drawn modals.
6. **Read-only cache on exit:** on `leftTab()`/`onHidden()`, call `suspend()` (disarm writes, keep display state readable) and `renewInputRevisions()` on return—never reset layout/view handles or text lines to `0`/empty.
7. **Cold pre-population:** seed read-only display banks before host exposure and after every `effSetProgram`/`effSetChunk` without claiming gestures or Q groups.
8. **Ordered batched writes:** in `native*Poll()`, report cell values/kinds/texts *before* the layout/view gate, suppress unchanged values via `nativeUiReported`, and force-report only on rebind/ack/format change.
9. **Paint-window protection:** arm `nativePagePaintUntil` (`kNativePaintWindowMs = 150 ms`) on *every* layout entry and visible-to-visible layout change, and call `requestNativeControlNames()`, never bare `requestProxyNames()`.
10. **Trailing-edge metadata deferral:** send Q 1..16 values immediately, coalesce all `audioMasterUpdateDisplay` triggers onto `pageSwitchNamesPending` (`kPageSwitchSettleMs = 800 ms`, cap `2500 ms`; device timing `UNSURE`), and set `statusDue_ = true` on view change.
