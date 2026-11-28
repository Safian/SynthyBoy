# Osc A -> MORE (and UNISON) Empty Controls Audit (2026-11-28)

Worktree: `/Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-oscmore-audit` (`HEAD`: `16316492`, branch `agy/oscmore-audit`).
Evidence basis: read-only inspection of `skin/` (`HEAD` and git commits `3bb27424`, `33ca2ed6`, `a1282cda`, `8cd10a2d`, `11fb5764`, `41b2bef9`), `shell-mpc/`, `.hw-local/mpc-builds/old-ui-3bb27424/`, `.hw-local/mpc-perf/key61-lfo-targets-cB0geE/`, `.hw-local/mpc-perf/key61-control-audit-wpt592/`, `.hw-local/mpc-perf/key61-osc-pattern-20261128/`, `.hw-local/mpc-knowledge/docs-official/mpc-sdk/docs/ui-reference.md`, and `docs/screens/old/`. No builds or tests were run.

## 1. Gate Chain Table for Wavetable Osc A -> MORE

### 1.1 Parameter base and `oscspec::kMore` layout on `HEAD` (`16316492`)

- **FACT (`docs/params.json`, `shell-mpc/synthyboy_mpc.cpp`, `shell-mpc/native_ui_params.h`, `shell-mpc/native_osc_detail.h`)**:
  - `docs/params.json`: `count = 2637`.
  - `synthyboy_mpc.cpp`: `nativeUiBase() = 2637 + 4 (lfo_live) + 32 (spectrum) + 2 (pad_pressure) + 1 (overlayParamIndex) + 27 (play::kParams) + 16 (QRouter::kCount) = 2719`.
  - `native_ui_params.h`: `kOscDetail = 615`, so `Parameter 3334` is `kView` (`0..5`, `LastFrame = 5`), `Parameter 3335..3366` are `Value0..31` (`kValue + slot`), `Parameter 3367..3398` are `Step0..31` (`kStep + slot`), `Parameter 3399..3430` are `Kind0..31` (`kKind + slot`, `0..16`, `kVisualKinds = 16`), `Parameter 3431` is `kMore`, `Parameter 3432` is `kBack`, `Parameter 3433` is `kLoad`, `Parameter 3434..3443` are `kMenusBase + 0..9`.
  - `native_text.h` + `native_ui_params.h`: `OscDetailTitle = 3847`, `OscDetailBlock0..7 = 3848..3855`, `OscDetailGraph = 3859`, `kTextGate + 1 = 3901` (`OscDetailTitle` keyline gate).
- **FACT (`33ca2ed6` 05:27 build vs `old-ui-3bb27424` vs `HEAD`)**:
  - In `3bb27424`, `kOscDetail` was `Parameter 3350` (`Value0 = 3351`, `Kind0 = 3415`).
  - In `33ca2ed6` (05:27), removing `kMixerPeek` (1) and `kMixerPeekView` (1) shifted `kOscDetail` to `Parameter 3348` (`Value0 = 3349`, `Kind0 = 3413`), and `skin/TUI.json` matched (`3348`, `3349`, `3413`).
  - Commit `7999d4c7` removed 14 reserved Play-expansion handles before `kOscDetail`, shifting `3348 -> 3334` on `HEAD`; `skin/defs_osc.json` matches (`3334`, `3335`, `3399`).

### 1.2 Container & Header Gate Chain (`Osc A` tab down to `oscDetailTile*`)

| Level | Definition / Instance (`file:line`) | `repeats` | `acceptsHWFocus` | `showWhenDataModelInvalid` | Handle Remapping (`map`) | Skin Expects (`additionalInvalidatingHandles` / `Data`) | Plugin Publishes (`HEAD`) | Match? |
|---|---|---|---|---|---|---|---|---|
| 0 | Tab `Osc A` -> `Screen` (`skin/TUI.json:8668-8691`) | — | `"No"` | `"Show"` | `[]` | Unconditional (`[]`) | — | **FACT: Match** (visibility; note `acceptsHWFocus: "No"` blocks HW cursor entry into `oscScreen`) |
| 0b | Tab `Osc A` -> `Screen Top` (`skin/TUI.json:8778-8801`, `oscScreenTop`, `skin/defs_osc.json:21424-22058`) | — | `"No"` | `"Show"` | `[]` | Unconditional; `Native Osc Detail Title` (`3847`), `Keyline` (`IndexedEnabling/1/2/Parameter 3901`), `Native Osc Block 0..7` (`3848..3855`, `"Show"`), `Native Osc Graph Frame` (`NonZero/Parameter 3859`, `"Hide"`) | `OscUi::publishTexts` (`osc_ui.h:1325-1338`): `"MORE"`, gate `1`, blocks `0,1,3,4` (`PITCH`, `UNISON`, `PHASE`, `PAGES`; plus `2 = WARP` if Warp 1 is on), Graph `""` (`0`) | **FACT: Match** (explains why `MORE`, `PITCH / UNISON / PHASE` titles ARE visible on Key 61) |
| 1 | `oscScreen` -> `Osc Native Surface` (`skin/defs_osc.json:21397-21419`) | — | `"Children"` | `"Show"` | `[]` | Unconditional (none) | — | **FACT: Match** (`oscScreen` has `ignoreMousePresses: true`, line `21197`) |
| 2 | `oscNativeSurface` -> `Osc Native Detail` (`skin/defs_osc.json:19631-19661`) | — | `"Children"` | `"Hide"` | `Data -> Parameter 3334` | `NonZero/Parameter 3334` (`Parameter 3334 != 0`) and valid `Data` | `nativeUiValue(3334) = 3/5 = 0.6f` (`synthyboy_mpc.cpp:2044-2046`, `maxInteger = 5` at `5508`) | **FACT: Match** (sibling `back` at `19315-19349` renders on Key 61, proving level 2 passes) |
| 3 | `oscNativeDetail` -> `Grid` (`oscDetailGrid`, `skin/defs_osc.json:19172-19202`) | — | `"Children"` | `"Hide"` | `Data -> Parameter 3334` | `RangedEnabling/3/5/6/Parameter 3334` (step in `3..5` of `6` steps `0..5`) and valid `Data` | `0.6f` (`round(0.6 * 5) = 3`, in `[3,5]`; `maxInteger = 5`) | **FACT: Numeric Match** (`back` at `19346` uses the identical gate `RangedEnabling/3/5/6/Parameter 3334` and renders) |
| 4 | `oscDetailGrid` -> `"0".."3"` (`oscDetailTile*`, `skin/defs_osc.json:18966-19149`) | `8` (on `oscDetailGrid`, col `r = 0..7`) | `"Children"` | `"Hide"` | `Data -> Parameter {Increment:Base=3335+row,Step=4}`<br>`Step -> FromGesture/Parameter {Increment:Base=3367+row,Step=4}`<br>`Kind -> Parameter {Increment:Base=3399+row,Step=4}` | `NonZero/Kind` (indirect alias `Kind` -> `{Increment}` inside `handle remapping`) and valid `Data` | `Kind` at `3399 + col*4 + row`: publishes `n::kind(cell.kind) / 16.0f` (`synthyboy_mpc.cpp:1864-1865`, `maxInteger = 16` at `5509`) — non-zero for every occupied cell (`1/16..6/16`) | **HYPOTHESIS / SKIN BREAK** (numeric plugin values match, yet even explicit non-`{Increment}` `NonZero/Parameter N` tiles stayed blank on Key 61; see Part 2) |
| 5 | `oscDetailTile*` -> `"1".."6"` / `"Menu face"` / `"7".."16"` (`skin/defs_osc.json:24785-26098`) | — | `"Yes"` (`"Children"` on `"5"`, `"No"` on `"Menu face"`) | `"Hide"` | `"1".."5"`, `"Menu face"`: `map: [{"key":"Caption","value":"Data"}]` (omits `"Data"`)<br>`"6"`: `Caption -> Data`, `Data -> FromGesture/Data`<br>`"7".."16"`: `Caption -> Data`, `Data -> Parameter 3434..3443` | `boundsType: "Proportional"`, `bounds: "0 0 1 1"`; `IndexedEnabling/<k>/17/Kind` (`RangedEnabling/7/16/17/Kind` for `"Menu face"`); relies on `"Kind"` chaining through `oscDetailTile*` -> `oscDetailGrid` (`"1".."5"` also rely on `"Data"` pass-through when `map` is non-empty) | `Kind` normalized `k / 16.0f` with `maxInteger = 16` (`17` steps `0..16`); leaf `Knob`/`Label`/`Button`/`Action` | **FACT: Partial `map` omits `"Data"` on `"1".."5"`/`"Menu face"`**; **HYPOTHESIS: Nested `oscDetailTile*` (`Proportional` + chained `Kind`) fails on Key 61** (proven blank even when `"6"` and `"14"` include `"Data"` in `map`) |

### 1.3 Cell-by-Cell Table for Wavetable Osc A (`typeRaw_ = 0`) on `MORE` (`oscspec::kMore`)

`oscspec::kMore` (`shell-mpc/overlay/osc_spec_page.h:43-53, 107`) has `display = false` (`graph = false` in `osc_ui.h:961`), so `nativeDetail_ = n::Plain` (`3`, `Parameter 3334 = 0.6f`) and `field = n::slot((block % 2) * 4 + cell, block / 2)` (`osc_ui.h:1003`). Only `oscspec::Cond` true for `typeRaw_ == 0` (Wavetable) enters `add()` (`osc_ui.h:1035-1038`).

| Block (`osc_spec_page.h:53`) | Ctl (`osc_spec_page.h:44-50`) | Condition (`typeRaw_ = 0`) | `(col, row)` & `slot` | Engine Param (`docs/params.json`) | Native `Value` / `Step` / `Kind` Params | `osc_ui.h` `T` -> `nativeDetailKind` (`osc_ui.h:1261-1281`, `native_osc_detail.h:29-34`) | Plugin Publishes: `Value` / `Kind` (`synthyboy_mpc.cpp:1861-1874`) | Row Tile Variant (`skin/defs_osc.json`) & Child Gate Expected | Match? |
|---|---|---|---|---|---|---|---|---|---|
| `0`: `PITCH` (`Native Osc Block 0`, `3848`) | `SEM` (`transpose_sem`) | `Always` -> **Shown** | `(0, 0)` -> `0` | `14` (`osc1.transpose`) | `Value 3335`, `Step 3367`, `Kind 3399` | `T::NumberSem` -> `n::Semitone (7)` -> `n::kind(7) = 4` (`Number`) | `Value`: `(semOf(p14)+11)/22`; `Kind 3399`: `4/16 = 0.25f` (`maxInteger=16`) | `oscDetailTileNoMenu` (`18992`): `NonZero/Kind`, child `"4"` `IndexedEnabling/4/17/Kind` (`map` omits `Data`) | **Numeric Match**; **Skin Chain Broken** |
| `0`: `PITCH` | `CRS` (`pitch_coarse`) | `Always` -> **Shown** | `(1, 0)` -> `4` | `1508` (`osc1.pitch_coarse`) | `Value 3339`, `Step 3371`, `Kind 3403` | `T::Number` -> `n::Number (4)` -> `n::kind(4) = 4` | `Value`: `qReadNorm(1508)`; `Kind 3403`: `4/16 = 0.25f` | `oscDetailTileNoMenu`: `NonZero/Kind`, child `"4"` `IndexedEnabling/4/17/Kind` | **Numeric Match**; **Skin Chain Broken** |
| `0`: `PITCH` | `TRACK` (`pitch_track`) | `Always` -> **Shown** | `(2, 0)` -> `8` | `1509` (`osc1.pitch_track`) | `Value 3343`, `Step 3375`, `Kind 3407` | `T::Toggle` -> `n::Toggle (3)` -> `n::kind(3) = 3` | `Value`: `0.0` or `1.0`; `Kind 3407`: `3/16 = 0.1875f` | `oscDetailTileNoMenu`: `NonZero/Kind`, child `"3"` `IndexedEnabling/3/17/Kind` (`map` omits `Data`) | **Numeric Match**; **Skin Chain Broken** |
| `0`: `PITCH` | `FIN` (`tune`) | `Cond::KSpec` -> Hidden | — | — | — | — | — | — | **Expected Hidden** |
| `1`: `UNISON` (`Native Osc Block 1`, `3849`) | `BLEND` (`unison.blend`) | `Cond::NotGran` -> **Shown** | `(4, 0)` -> `16` | `26` (`osc1.unison.blend`) | `Value 3351`, `Step 3383`, `Kind 3415` | `T::Dial` -> `n::Uni (1)` -> `n::kind(1) = 1` | `Value`: `qReadNorm(26)`; `Kind 3415`: `1/16 = 0.0625f` | `oscDetailTileNoMenu`: `NonZero/Kind`, child `"1"` `IndexedEnabling/1/17/Kind` (`map` omits `Data`) | **Numeric Match**; **Skin Chain Broken** |
| `1`: `UNISON` | `VOICES`, `UNISON` | `Cond::KGran` -> Hidden | — | — | — | — | — | — | **Expected Hidden** |
| `2`: `WARP` (`Native Osc Block 2`, `3850`) | `W1 PHASE` (`warp.phase`) | `Cond::Warp1On` (`osc1.warp.type != 0`) | `(0, 1)` -> `1` (if Warp 1 active) | `33` (`osc1.warp.phase`) | `Value 3336`, `Step 3368`, `Kind 3400` | `T::Dial` -> `n::Uni (1)` -> `n::kind(1) = 1` | When Warp 1 off: `Kind 3400 = 0.0f` (`Block 2 = ""`). When on: `Kind 3400 = 1/16 = 0.0625f` | `oscDetailTilem0m1m2m4m7m8` (`19032`): `NonZero/Kind`, child `"1"` `IndexedEnabling/1/17/Kind` | **Numeric Match**; **Skin Chain Broken** |
| `2`: `WARP` | `WARP 2`, `W2 AMOUNT`, `W2 PHASE` | `MultiSampleSpec` / `Warp2AmtOn` / `Warp2On` | `W2 PHASE` at `(1, 1)` -> `5` if Warp 2 active (`Warp2On` is `typeRaw_ != 2 && warp2.type != 0`) | `1296` (`osc1.warp2.phase`) | `Value 3340`, `Step 3372`, `Kind 3404` | `T::Dial` -> `n::Uni (1)` | When Warp 2 off: `0.0f`; when on: `1/16 = 0.0625f` | `oscDetailTilem0m1m2m4m7m8`: child `"1"` `IndexedEnabling/1/17/Kind` | **Numeric Match**; **Skin Chain Broken** |
| `3`: `PHASE` (`Native Osc Block 3`, `3851`) | `PHASE` (`phase`) | `Cond::PhaseKinds` -> **Shown** | `(4, 1)` -> `17` | `19` (`osc1.phase`) | `Value 3352`, `Step 3384`, `Kind 3416` | `T::Number`, `Degrees` -> `n::Phase (9)` -> `n::kind(9) = 4` | `Value`: `qReadNorm(19)`; `Kind 3416`: `4/16 = 0.25f` | `oscDetailTilem0m1m2m4m7m8`: `NonZero/Kind`, child `"4"` `IndexedEnabling/4/17/Kind` (`map` omits `Data`) | **Numeric Match**; **Skin Chain Broken** |
| `3`: `PHASE` | `RAND` (`random_phase`) | `Cond::RandKinds` -> **Shown** | `(5, 1)` -> `21` | `20` (`osc1.random_phase`) | `Value 3356`, `Step 3388`, `Kind 3420` | `T::Number` -> `n::Number (4)` -> `n::kind(4) = 4` | `Value`: `qReadNorm(20)`; `Kind 3420`: `4/16 = 0.25f` | `oscDetailTilem0m1m2m4m7m8`: `NonZero/Kind`, child `"4"` `IndexedEnabling/4/17/Kind` (`map` omits `Data`) | **Numeric Match**; **Skin Chain Broken** |
| `4`: `PAGES` (`Native Osc Block 4`, `3852`) | `MAP` (`"map"`) | `Cond::KWave` -> **Shown** | `(0, 2)` -> `2` | `-1` (`T::Inert`, `id = kSpec+13 = 813`) | `Value 3337`, `Step 3369`, `Kind 3401` | `T::Inert` -> `n::Action (6)` -> `n::kind(6) = 6` | `Value`: `0.0f`; `Kind 3401`: `6/16 = 0.375f`; `effGetParamName(3337) = "MAP"` | `oscDetailTilem5m6m9` (`19072`): `NonZero/Kind`, child `"6"` `IndexedEnabling/6/17/Kind` (`map` has `Caption->Data`, `Data->FromGesture/Data`) | **Numeric Match**; **Skin Chain Broken** |
| `4`: `PAGES` | `OPTIONS` (`"options"`) | `Cond::Always` -> **Shown** | `(1, 2)` -> `6` | `-1` (`T::Inert`, `id = kSpec+21 = 821`) | `Value 3341`, `Step 3373`, `Kind 3405` | `T::Inert` -> `n::Action (6)` -> `n::kind(6) = 6` | `Value`: `0.0f`; `Kind 3405`: `6/16 = 0.375f`; `effGetParamName(3341) = "OPTIONS"` | `oscDetailTilem5m6m9`: `NonZero/Kind`, child `"6"` `IndexedEnabling/6/17/Kind` (`map` has `Caption->Data`, `Data->FromGesture/Data`) | **Numeric Match**; **Skin Chain Broken** |
| `4`: `PAGES` | `ENV`..`WINDOW` | `KMulti`/`KSample`/`KSpec`/`KGran` -> Hidden | — | — | — | — | — | — | **Expected Hidden** |
| `5`: `VELOCITY`, `6`: `GRAIN` | — | `KMulti` / `KGran` -> Hidden | — | — | — | — | — | — | **Expected Hidden** |

---

## 2. Break Points (with `file:line`)

### 2.1 Historical correction and C++ boundary

- **FACT (Key 61 archive `docs/screens/old/osc_more_wave_old.jpg`, `docs/ui-components/kepes-katalogus.html:811`)**: `MORE` did **not** work on Key 61 in the old UI either. `osc_more_wave_old.jpg` shows `MORE`, `BACK`, and the empty block outlines (`PITCH`, `UNISON`, `PHASE`, `PAGES`) with zero controls, documented as *"MORE page, wavetable: only block frames (PITCH, UNISON, WARP, PHASE, PAGES, VELOCITY), no controls, PAGES has no buttons (known old-build gap)"*. Likewise `old-ui-3bb27424/Safian - VST - SynthyBoy/Plugin Skins/TUI.json` and `33ca2ed6:skin/TUI.json` (`05:27` build) have byte-identical `oscDetailPlainGrid` and `oscDetailTile` structure (only shifted `-2` parameter indices `3350 -> 3348`, `3351.. -> 3349..`, `3415.. -> 3413..`).
- **FACT (`3bb27424:scripts/mpc-skin-extras/58_osc_detail.py:137-138`, `docs/screens/old/osc_unison_detail_wave_old.jpg`)**: What worked on Key 61 in `3bb27424` was **only `UNISON`**, and only because commit `a1282cda` bypassed `oscDetailGraphGrid` by mounting explicit `Unison 0..6` entries directly inside `oscNativeDetail` (*"On Key 61 the dynamic grid stayed blank even with seven live value/kind bindings in the VST"*).
- **FACT (`.hw-local/mpc-perf/key61-control-audit-wpt592/A1/detail-osca-unison.vst.json`)**: Real Key 61 VST snapshot while UNISON was open recorded `vst[3350] = 0.800000012` (`Graph`) and `vst[3415..3439] = 0.3125, 0.875, 0.0625, 0.0625, 0.125, 0.125, 0.125` (all 7 `Kind` handles non-zero) while `oscDetailGraphGrid` remained blank.
- **FACT (`shell-mpc/overlay/osc_ui.h:942-1060, 1261-1338`, `shell-mpc/synthyboy_mpc.cpp:1837-1874, 2504-2542, 4211-4252, 5495-5515`)**: On `HEAD` (`16316492`), the C++ path for `MORE` runs through the exact same `prepareNativeDetail()` -> `add()` -> `nativeOscDetailPoll()` -> `oscDetailValue()` pipeline. Because `PITCH / UNISON / PHASE` block titles are visible on Key 61, `OscUi::prepareNativeDetail` already entered `spec_.visible(i)` (`osc_ui.h:1035-1038`) and set `nativeDetailBlocks_[0,1,3]` right before `add()` populated `state.fields[]` and `nativeDetail_ = n::Plain (3)`. Commits `8cd10a2d`, `11fb5764`, and `41b2bef9` kept every reachable `MORE` slot and kind `{1, 3, 4, 6}` intact (`skin/defs_osc.json:18992-19149`, `osc_ui.h:1003`).
- **FACT (commits `873ee54c` 17:57 and `cf36b3ad` 18:30)**: Earlier today (`2026-11-28`), `oscScreen` missing `ignoreMousePresses: true` (from `6bd952e9`) blocked touches on the Osc body, and `osc_off_veil` hid settings blocks; both are already fixed in `HEAD` and are independent of the empty `MORE` blocks already observed in `osc_more_wave_old.jpg` and at `05:27` (`33ca2ed6`).

### 2.2 Skin break points on `HEAD` (`skin/defs_osc.json`, `skin/TUI.json`)

1. **Break Point 1 — `oscDetailTile*` omits `"Data"` from non-empty `handle remapping` (FACT in `skin/defs_osc.json`, partial cause)**:
   - `skin/defs_osc.json:24785-24943` (`oscDetailTileNoMenu` children `"1".."5"`), `25005-25163, 25205-25235` (`oscDetailTilem0m1m2m4m7m8` `"1".."5"`, `"Menu face"`), `25473-25631, 25673-25703` (`oscDetailTilem5m6m9`), `25833-25991, 26033-26063` (`oscDetailTilem3`).
   - All of `"1".."5"` (`oscNativeSetting_dial_uni`, `oscNativeSetting_dial_bi`, `oscNativeSetting_toggle`, `oscDetailStepper`) and `"Menu face"` (`oscDetailMenuFace`) declare `"data": {"version": 1, "handleName": "Data"}` and `"showWhenDataModelInvalid": "Hide"`, yet their `"handle remapping".map` contains **only** `[{"key": "Caption", "value": "Data"}]` (missing `{"key": "Data", "value": "Data"}`). Every other working nested custom component in `skin/` either has `map: []` (e.g. `NativeActive`, `skin/defs_osc.json:20250+`) or explicitly maps every handle used by the child.
   - However, child `"6"` (`MAP`/`OPTIONS` on `MORE`, `skin/defs_osc.json:24945-24980, 25637-25672`) **does** include `{"key": "Data", "value": "FromGesture/Data"}`, and child `"14"` (`STACK` in `explicit.json`) **did** include `{"key": "Data", "value": "Parameter 3457"}`, yet both still failed to appear on Key 61. Therefore Break Point 1 is **not** the sole failure.

2. **Break Point 2 — The `oscDetailGrid` / `oscDetailTile*` nested dynamic-tile architecture fails on Key 61 even without `repeats` or `{Increment}` (FACT from `.hw-local/mpc-perf/key61-lfo-targets-cB0geE/explicit.json` vs `old-ui-3bb27424` `Unison 0..6`; HYPOTHESIS which exact host evaluation rule drops it)**:
   - In `.hw-local/mpc-perf/key61-lfo-targets-cB0geE/explicit.json` (`sha256 af97c588`, installed in `final-install.log` and checked by `detail.py` in `detail-final.log`, recorded in `a1282cda~1:TODO.md:1`), `repeats: 8` and `{Increment:...}` were completely unrolled into 24 explicit `oscDetailTile` entries (`boundsType: "Absolute"`), `NonZero/Kind` was replaced by `NonZero/Parameter 3415..3445`, and `IndexedEnabling/k/17/Kind` was replaced by `Kind1..16` mapped to `IndexedEnabling/k/17/Parameter 3415..3445` — **and Key 61 still showed zero controls**.
   - Comparing broken `explicit.json` (`oscNativeDetail -> oscDetailGraphGrid -> oscDetailTile -> "14"`) and broken `HEAD` (`skin/defs_osc.json:18966-19149` -> `24764-26098`) against the two proven-working Key 61 grids — `old-ui-3bb27424` `Unison 0..6` (`osc_unison_detail_wave_old.jpg`) and `fxNativeParameterCells` (`skin/defs_fx.json`, `docs/screens/fx.jpg`) — isolates three concrete defects in `skin/defs_osc.json`:
     - **(2a) `NonZero/Kind` (`skin/defs_osc.json:19010-19024, 19050-19064, 19090-19104, 19130-19144`)**: Every tile entry in `oscDetailGrid` gates on `["NonZero/Kind"]` where `Kind` maps to `Parameter {Increment:Base=3399,Step=4}`. Not only does this combine `NonZero/` with an unexpanded alias `"Kind"` -> `{Increment}` (whereas working `fxNativeGalleryRow1/2` in `skin/defs_fx.json` puts `Parameter {Increment:...}` directly inside `additionalInvalidatingHandles`), even direct `NonZero/Parameter 3415..3445` in `explicit.json` differed from working `Unison 0..6` (which uses `IndexedEnabling` directly with no `NonZero` wrapper on the `Kind` parameter).
     - **(2b) Indirect `Kind` handle chaining in `additionalInvalidatingHandles` (`skin/defs_osc.json:24812, ...`)**: Inside `oscDetailTile*`, every child (`"1".."16"`, `"Menu face"`) gates on `"IndexedEnabling/<k>/17/Kind"` (or `"RangedEnabling/7/16/17/Kind"`), relying on `"Kind"` passing unmapped through `oscDetailTile*` to `oscDetailGrid`'s `map`, where `"Kind"` is `Parameter {Increment:Base=3399,Step=4}`. Working `Unison 0..6` and `fxNativeParameterCells` put `"IndexedEnabling/<k>/<n>/Parameter <idx>"` directly on the gated control entry.
     - **(2c) 3-level `CustomComponent -> CustomComponent (Proportional 0 0 1 1) -> CustomComponent (Absolute)` wrapper (`oscDetailGrid` -> `oscDetailTile*` -> `oscNativeSetting_*`)**: Both working `Unison 0..6` (`old-ui-3bb27424`) and working `fxNativeParameterCells` (`skin/defs_fx.json`) mount the concrete control definitions (`oscNativeSetting_dial_uni`, `oscNativeSetting_dial_bi`, `oscNativeSetting_menu`, `fxNativeCell*`) directly in the view container (`Absolute` coordinates, direct `Parameter N` bindings and gates) with no intermediate multiplexer tile (`oscDetailTile*`).

3. **Break Point 3 — `acceptsHWFocus: "No"` on `oscScreen` and `oscScreenTop` instances (`skin/TUI.json:8684, 8794` and tabs `Osc B`, `Osc C`, `Sub`, `Noise`) (FACT)**:
   - Does not hide controls (the root `Osc A` page in `docs/screens/osc_a.jpg` renders despite `acceptsHWFocus: "No"`), but prevents hardware cursor navigation from entering `oscScreen` / `oscNativeDetail` / `oscDetailGrid`. Should be `"Children"` on `Screen` (`skin/TUI.json:8684, 8957, 9230, 9503, 9776`).

---

## 3. Exact Fix Proposal

### 3.1 Owning files and tooling (no active skin generator)

- **FACT (`AGENTS.md`, `ddfc962f`, `2fff7caf`)**: `skin/` was frozen as the sole source of truth in `ddfc962f` and `scripts/mpc-skin-extras/58_osc_detail.py` was deleted in `2fff7caf`. No build-time or script generator owns `oscDetailGrid` / `oscDetailTile*` anymore.
- **Owning source files**:
  - [skin/defs_osc.json](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-oscmore-audit/skin/defs_osc.json) (`oscDetailGrid`, `oscDetailTile*`, `oscDetailMenuFace`, `oscNativeSetting_menuDetailValue`)
  - [skin/TUI.json](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-oscmore-audit/skin/TUI.json) (`Osc A`, `Osc B`, `Osc C`, `Sub`, `Noise` tab `Screen` focus property)
- **Owning format/validation scripts**:
  - `python3 scripts/skin-format.py skin/TUI.json skin/defs_osc.json` (canonical one-element-per-line formatting; `scripts/skin-format.py:1-6`)
  - `python3 scripts/validate_synthyboy_skin.py skin` (JSON Schema + parameter-range + baseline check; `scripts/validate_synthyboy_skin.py`)
  - `python3 scripts/skin-baseline-update.py` (**only after** the Key 61 device check passes; `scripts/skin-baseline-update.py:1-5`)

### 3.2 Change A — Replace nested `oscDetailGrid` (`repeats: 8` + `oscDetailTile*` + `NonZero/Kind`) with direct flat controls (`skin/defs_osc.json:18966-19149, 24764-26098`)

Because `explicit.json` (`af97c588`) already proved on Key 61 that keeping the `oscDetailTile` wrapper (even with `repeats: 8`, `{Increment}`, and `"Kind"` aliases removed) still leaves the grid blank, the fix must follow the **proven Key 61 pattern** used by `fxNativeParameterCells` (`skin/defs_fx.json`, `docs/screens/fx.jpg`) and `Unison 0..6` in `3bb27424` (`docs/screens/old/osc_unison_detail_wave_old.jpg`):

1. **`skin/defs_osc.json:18966-19149` (`oscDetailGrid`)**:
   - Change `"version": 3` + `"repeats": 8` -> `"version": 2` (remove `"repeats"`).
   - Remove the 4 `oscDetailTile*` children gated by `"NonZero/Kind"`.
   - Mount one concrete control entry directly in `oscDetailGrid` for each `(col, row, kind)` slot reachable in `oscspec::kSpecs`, `oscmodel::presets`, and `kPageRoute`, at `"boundsType": "Absolute"`, `"bounds": "<col*158> <row*116> 158 92"`:
     - `kind == 1`: `oscNativeSetting_dial_uni` (`Data -> Parameter <3335+slot>`, `Caption -> Parameter <3335+slot>`, gate `IndexedEnabling/1/17/Parameter <3399+slot>`)
     - `kind == 2`: `oscNativeSetting_dial_bi` (`Data -> Parameter <3335+slot>`, `Caption -> Parameter <3335+slot>`, gate `IndexedEnabling/2/17/Parameter <3399+slot>`)
     - `kind == 3`: `oscNativeSetting_toggle` (`Data -> Parameter <3335+slot>`, `Caption -> Parameter <3335+slot>`, gate `IndexedEnabling/3/17/Parameter <3399+slot>`)
     - `kind == 4`: `oscNativeSetting_dial_uni` (`Data -> Parameter <3335+slot>`, `Caption -> Parameter <3335+slot>`, gate `IndexedEnabling/4/17/Parameter <3399+slot>`)
     - `kind == 5`: `oscDetailStepper` (`Data -> Parameter <3335+slot>`, `Step -> FromGesture/Parameter <3367+slot>`, `Caption -> Parameter <3335+slot>`, gate `IndexedEnabling/5/17/Parameter <3399+slot>`)
     - `kind == 6`: `oscNativeSetting_action` (`Data -> FromGesture/Parameter <3335+slot>`, `Caption -> Parameter <3335+slot>`, gate `IndexedEnabling/6/17/Parameter <3399+slot>`)
     - `kind == 7..16`: a combined `oscNativeSetting_menu` (or `oscNativeSetting_warpChoice`, restoring `oscNativeSetting_menu` removed in `41b2bef9` by merging `oscDetailMenuFace` + `oscNativeSetting_menuDetailValue` into one component, with `Data -> Parameter <3434 + kind - 7>`, `Caption -> Parameter <3335+slot>`, gate `IndexedEnabling/<kind>/17/Parameter <3399+slot>`). Note: `synthyboy_mpc.cpp:4225` already returns the slot's control label (e.g. `"STACK"`) for `effGetParamName(3335+slot)`.
2. **Delete dead `oscDetailTileNoMenu` (`24764-24982`), `oscDetailTilem0m1m2m4m7m8` (`24984-25450`), `oscDetailTilem5m6m9` (`25452-25810`), `oscDetailTilem3` (`25812-26100`)** (plus `oscDetailMenuFace`, `oscNativeSetting_menuDetailValue`, `oscNativeSettingWarpChoiceDetailValue` if folded into combined menu definitions).

#### Concrete `old -> new` excerpt for `skin/defs_osc.json:18966-19026` (showing `SEM` at `(0, 0)` `slot = 0` and `MAP` at `(0, 2)` `slot = 2`):

**Old (`skin/defs_osc.json:18966-19026`):**
```json
    "key": "oscDetailGrid",
    "value": {
     "version": 3,
     "actions": [],
     "backgroundData": { "version": 1, "focussed": { "version": 1, "colour": "0", "image": "" }, "unfocussed": { "version": 1, "colour": "0", "image": "" } },
     "ignoreMousePresses": true,
     "disableCoarseDataWheel": false,
     "repeats": 8,
     "componentsData": [
      {
       "version": 2,
       "componentData": { "version": 1, "name": "0", "type": "oscDetailTileNoMenu", "data": { "version": 1, "handleName": "Data" } },
       "handle remapping": {
        "version": 1,
        "map": [
         { "key": "Data", "value": "Parameter {Increment:Base=3335,Step=4}" },
         { "key": "Step", "value": "FromGesture/Parameter {Increment:Base=3367,Step=4}" },
         { "key": "Kind", "value": "Parameter {Increment:Base=3399,Step=4}" }
        ]
       },
       "bounds": {
        "version": 2,
        "acceptsHWFocus": "Children",
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "0 0 158 92",
        "additionalInvalidatingHandles": [ "NonZero/Kind" ]
       }
      }, ...
```

**New (`skin/defs_osc.json:18966+`, formatted by `scripts/skin-format.py`):**
```json
    "key": "oscDetailGrid",
    "value": {
     "version": 2,
     "actions": [],
     "backgroundData": { "version": 1, "focussed": { "version": 1, "colour": "0", "image": "" }, "unfocussed": { "version": 1, "colour": "0", "image": "" } },
     "ignoreMousePresses": true,
     "disableCoarseDataWheel": false,
     "componentsData": [
      {
       "version": 2,
       "componentData": { "version": 1, "name": "0.4", "type": "oscNativeSetting_dial_uni", "data": { "version": 1, "handleName": "Data" } },
       "handle remapping": {
        "version": 1,
        "map": [
         { "key": "Data", "value": "Parameter 3335" },
         { "key": "Caption", "value": "Parameter 3335" }
        ]
       },
       "bounds": {
        "version": 2,
        "acceptsHWFocus": "Yes",
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "0 0 158 92",
        "additionalInvalidatingHandles": [ "IndexedEnabling/4/17/Parameter 3399" ]
       }
      },
      ...
      {
       "version": 2,
       "componentData": { "version": 1, "name": "2.6", "type": "oscNativeSetting_action", "data": { "version": 1, "handleName": "Data" } },
       "handle remapping": {
        "version": 1,
        "map": [
         { "key": "Data", "value": "FromGesture/Parameter 3337" },
         { "key": "Caption", "value": "Parameter 3337" }
        ]
       },
       "bounds": {
        "version": 2,
        "acceptsHWFocus": "Yes",
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "0 232 158 92",
        "additionalInvalidatingHandles": [ "IndexedEnabling/6/17/Parameter 3401" ]
       }
      }, ...
```

### 3.3 Change B — Fix hardware focus propagation on `oscScreen` (`skin/TUI.json:8684, 8957, 9230, 9503, 9776`)

**Old (`skin/TUI.json` lines `8684`, `8957`, `9230`, `9503`, `9776`):**
```json
        "acceptsHWFocus": "No",
```
**New:**
```json
        "acceptsHWFocus": "Children",
```

---

## 4. UNISON Page Findings (`kPageUnison = 10`)

### 4.1 Historical timeline (`a1282cda` / `3bb27424` -> `11fb5764` -> `HEAD`)

- **FACT (`docs/screens/old/osc_unison_detail_wave_old.jpg`, `.hw-local/mpc-builds/old-ui-3bb27424/`)**:
  - In `a1282cda` (and `3bb27424`), UNISON originally failed inside `oscDetailGraphGrid` for the exact same reason `MORE` fails (`58_osc_detail.py:137-138`: *"On Key 61 the dynamic grid stayed blank even with seven live value/kind bindings in the VST"*).
  - `a1282cda` worked around it by mounting `Unison 0` (`oscDetailUnisonMode`), `Unison 1` (`oscNativeSetting_menu`, `STACK`), and `Unison 2..6` (`oscNativeSetting_dial_uni` / `oscNativeSetting_dial_bi`) **directly** inside `oscNativeDetail` at `"y = 312"`, with `"Absolute"` bounds, direct `"IndexedEnabling/.../Parameter 3419..3439"` gates, and explicit `"Data"` + `"Caption"` mappings (`old-ui-3bb27424/Plugin Skins/TUI.json`). That direct mount worked on Key 61 (`osc_unison_detail_wave_old.jpg` shows `MODE`, `STACK`, `WIDTH`, `RANGE`, `WT POS`, `WARP 1`, `WARP 2` all visible).
- **FACT (`11fb5764`, `skin/defs_osc.json:18966-19496`, `shell-mpc/overlay/osc_ui.h:1003`)**:
  - Commit `11fb5764` deleted those working `Unison 0..6` direct entries from `oscNativeDetail`, unified `Plain` + `Graph` into `oscDetailGrid` (`y = 172 + 116*r`), shifted `Graph` blocks down by `+1` row (`row = block / 2 + (graph ? 1 : 0)`, `osc_ui.h:1003`), and routed `UNISON` into `oscDetailGrid` row 1 (`oscDetailTilem0m1m2m4m7m8`, `skin/defs_osc.json:19032`).
  - As a result, `HEAD` (`16316492`) puts `UNISON` back onto the exact broken `oscDetailGrid` -> `oscDetailTile*` path, re-exposing the Key 61 empty-controls bug (`TODO.md:16`).

### 4.2 Gate & binding table for Wavetable Osc A `UNISON` on `HEAD`

`kPageUnison = 10` (`osc_ui.h:1948-1950`, `osc_presets.h:65-75, 191`): `graph = true`, `nativeDetail_ = n::Graph (4)` (`Parameter 3334 = 4/5 = 0.8f`), `oscDetailGrid` gate `RangedEnabling/3/5/6/Parameter 3334` matches (`round(0.8 * 5) = 4` in `[3,5]`), and `oscUi::publishTexts` (`osc_ui.h:1331-1338`) maps `blocks[0..1]` (`"UNISON"`, `"VOICE SPREAD"`) to `Native Osc Block 2..3` (`Parameter 3850..3851`, `y = 272`, above row 1 at `y = 288..380`) and sets `OscDetailGraph (3859) = "1"`.

For every cell `c = 0..6`, `block = g = 0` (`MODE..RANGE`) or `1` (`WT POS..WARP 2`), `row = 1`, `slot = c * 4 + 1` (`1, 5, 9, 13, 17, 21, 25`):

| Slot `(col, row)` | Label & Engine Suffix (`osc_presets.h:65-71`, `osc_ui.h:1622-1628`) | Native `Value` / `Step` / `Kind` Params | Plugin `Kind` Published (`synthyboy_mpc.cpp:1864-1865`) | Tile `oscDetailTilem0m1m2m4m7m8` (`skin/defs_osc.json:24984-25450`) | Match / Break |
|---|---|---|---|---|---|
| `(0, 1)` -> `1` | `MODE` (`unison.mode`) | `3336` / `3368` / `3400` | `n::Choice (5)` -> `5/16 = 0.3125f` | `NonZero/Kind`; child `"5"` (`oscDetailStepper`), `IndexedEnabling/5/17/Kind`, `map` omits `Data` | **Numeric Match**; broken by Break Points 1 & 2 (`oscDetailGrid` / `oscDetailTilem0m1m2m4m7m8`) |
| `(1, 1)` -> `5` | `STACK` (`unison.stack`) | `3340` / `3372` / `3404` | `kMenuSuffixes[7]` -> `kind = 17` -> `n::kind(17) = 14` (`14/16 = 0.875f`); value on `kMenusBase + 7 = Parameter 3441` | `NonZero/Kind`; `"Menu face"` (`map` omits `Data`) + child `"14"` (`oscNativeSetting_menuDetailValue`, `Data -> Parameter 3441`, `Caption -> Data`, `IndexedEnabling/14/17/Kind`) | **Numeric Match** (`menu 7` IS present in `oscDetailTilem0m1m2m4m7m8:25369-25403`); broken by Break Point 2 |
| `(2, 1)` -> `9` | `WIDTH` (`unison.spread`) | `3344` / `3376` / `3408` | `n::Uni (1)` -> `1/16 = 0.0625f` | `NonZero/Kind`; child `"1"` (`oscNativeSetting_dial_uni`), `IndexedEnabling/1/17/Kind`, `map` omits `Data` | **Numeric Match**; broken by Break Points 1 & 2 |
| `(3, 1)` -> `13` | `RANGE` (`unison.range`) | `3348` / `3380` / `3412` | `n::Uni (1)` -> `1/16 = 0.0625f` | `NonZero/Kind`; child `"1"` (`oscNativeSetting_dial_uni`), `IndexedEnabling/1/17/Kind`, `map` omits `Data` | **Numeric Match**; broken by Break Points 1 & 2 |
| `(4, 1)` -> `17` | `WT POS` (`frame_spread`) | `3352` / `3384` / `3416` | `n::Bi (2)` -> `2/16 = 0.125f` (`oscmodel.h` / `unison`: `pi.min < 0 && pi.max > 0`) | `NonZero/Kind`; child `"2"` (`oscNativeSetting_dial_bi`), `IndexedEnabling/2/17/Kind`, `map` omits `Data` | **Numeric Match**; broken by Break Points 1 & 2 |
| `(5, 1)` -> `21` | `WARP 1` (`warp.spread`) | `3356` / `3388` / `3420` | `n::Bi (2)` -> `2/16 = 0.125f` | `NonZero/Kind`; child `"2"` (`oscNativeSetting_dial_bi`), `IndexedEnabling/2/17/Kind`, `map` omits `Data` | **Numeric Match**; broken by Break Points 1 & 2 |
| `(6, 1)` -> `25` | `WARP 2` (`warp2.spread`) | `3360` / `3392` / `3424` | `n::Bi (2)` -> `2/16 = 0.125f` | `NonZero/Kind`; child `"2"` (`oscNativeSetting_dial_bi`), `IndexedEnabling/2/17/Kind`, `map` omits `Data` | **Numeric Match**; broken by Break Points 1 & 2 |

Note: Applying Fix 3.2 (flat direct controls in `oscDetailGrid` keyed by `(col, row, kind)` without `repeats: 8`, `NonZero/Kind`, or `oscDetailTile*`) fixes **both** `MORE` and `UNISON` (plus `OPTIONS`, `MAP`, `GRAIN`, `SAMPLE REGION/SLICE`, `SPECTRAL FILTER/SCAN`, `ENVELOPE`, `ROUTING`) uniformly through one mechanism.

---

## 5. What Could Not Be Decided From Source

1. **Exact Key 61 host rule(s) inside Break Point 2 that drop `oscDetailGrid` and `explicit.json` (`HYPOTHESIS`)**:
   - **Proven (`FACT`)**: C++ publishes the right parameter values (`detail-osca-unison.vst.json`, `osc_ui.h:942-1060`), direct controls in `oscNativeDetail` (`Unison 0..6` in `3bb27424`) render on Key 61 (`osc_unison_detail_wave_old.jpg`), and unrolling `repeats: 8` + removing `{Increment}` inside `oscDetailTile` (`explicit.json` `af97c588`) still renders zero controls on Key 61.
   - **Undecided from source alone**: Which exact subset of (a) `NonZero/Kind` / `NonZero/Parameter <KindP>` on the 17-step `Kind` parameter, (b) `IndexedEnabling/.../Kind` / custom-remapped gate alias chaining, (c) missing `Data -> Data` in `"1".."5"`/`"Menu face"` `map`, and (d) 3-level `oscDetailGrid -> oscDetailTile* (Proportional 0 0 1 1) -> oscNativeSetting_* (Absolute)` causes the Key 61 host to hide the tiles, and whether `"repeats": 8` works without `oscDetailTile*` (as in `fxNativeGalleryRow1/2` in `skin/defs_fx.json`).
   - **Single minimal Key 61 probe to settle it** (4 test instances for `SEM` `col=0, row=0`, `Value = Parameter 3335`, `Kind = Parameter 3399`, placed directly in `oscNativeDetail` alongside `back` on `MORE`):
     1. Direct `oscNativeSetting_dial_uni` (`Absolute`, `Data -> 3335`, `Caption -> 3335`, gate `["IndexedEnabling/4/17/Parameter 3399"]`) — tests the baseline Fix 3.2 control.
     2. Same as (1) with `["NonZero/Parameter 3399", "IndexedEnabling/4/17/Parameter 3399"]` — isolates `NonZero` on 17-step `Kind`.
     3. Wrapped in a 1-child `oscDetailTile` with `Proportional 0 0 1 1`, explicit `Data -> Data`, `Caption -> Data`, and direct gate `["IndexedEnabling/4/17/Parameter 3399"]` — isolates the `Proportional` 3-level wrapper.
     4. A `version: 3, repeats: 2` container holding (1) with `Data -> Parameter {Increment:Base=3335,Step=4}`, `Caption -> Parameter {Increment:Base=3335,Step=4}`, and gate `["IndexedEnabling/4/17/Parameter {Increment:Base=3399,Step=4}"]` — settles whether `repeats` can halve Fix 3.2's node count on `oscDetailGrid` without hiding controls.

2. **MPC XL (`aarch64`) behaviour (`HYPOTHESIS`)**:
   - No XL screenshot of `MORE` (`3bb27424`, `33ca2ed6`, or `HEAD`) exists in the repository or `.hw-local/mpc-perf/`. Because the skin parser differs in known ways between 32-bit Key 61 and 64-bit XL (`docs/dsp-knowledge/skin-host-behaviour.md`), one XL capture on `Osc A -> MORE` settles whether XL shows the same empty grid.

3. **Other Detail pages (`OPTIONS`, `MAP`, `GRAIN`, `SAMPLE REGION/SLICE`, `SPECTRAL FILTER/SCAN`, `ENVELOPE`) (`HYPOTHESIS`)**:
   - All of them traverse the exact same `oscDetailGrid` -> `oscDetailTile*` tree on `HEAD` (`skin/defs_osc.json:18966-19149`), and earlier Key 61 captures (`docs/screens/old/osc_more_*_old.jpg`, `osc_frame_map_old.jpg`) showed empty block frames there too. Exact per-control visibility, menu choices (`WARP 2` long list, `gran.*`, `spec.*`, `sample.*`), and `oscNoteMask` (`OPTIONS`) cannot be verified until Fix 3.2 is deployed and photographed on device (`TODO.md:21, 84, 103`).

4. **Live Key 61 `.vst.json` readout from `HEAD` (`16316492`) on `MORE` (`HYPOTHESIS` -> `FACT` parity check)**:
   - While `prepareNativeDetail`, `nativeOscDetailPoll`, and `oscDetailValue` are verified from source and `detail-osca-unison.vst.json` confirmed live VST values on `UNISON`, no `.vst.json` capture was taken during `05:27` or `HEAD` while `MORE` was open. One VST parameter dump on Key 61 with `Osc A -> MORE` open settles runtime values of `Parameter 3334` (`0.6`) and `3399, 3403, 3407, 3415, 3416, 3420, 3401, 3405`.
