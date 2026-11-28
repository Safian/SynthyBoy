# Osc screen (`oscScreen`, `oscScreenTop`)

**Tabs:** Osc A (`osca`), Osc B (`oscb`), Osc C (`oscc`), Sub (`oscsub`), Noise (`oscnoise`). Each tab holds `Screen` (`oscScreen`) and `Screen Top` (`oscScreenTop`) plus per-tab items: the marker squares (`Overlay Marker osca left/right` ...), the instance picture (`Overlay Instance osca`), `Native Osc Missing Page`, `Native Osc Message` and the background sentinel (1180 2). **Pictures:** `../../screens/osc_a.jpg`, `osc_b.jpg`, `osc_c.jpg`, `sub.jpg`, `noise.jpg`.

## What it shows

`oscScreen` (below the per-tab items in the z-order): the modulation editor and targets title and keyline (`Native Targets Keyline`, defined in `defs_osc.json`) and `oscNativeSurface` (0 0 1280 628): the power, routing, pan and level layers of the oscillator (gated by the selected source), the per-form settings (`oscNativeSettingsLayer`), the detail pages (`oscNativeDetail`: one unified `oscDetailGrid` 8 columns repeated × 4 rows at `y = 172 + 116*r`, tile height 92, blend dials, BACK/LOAD actions) and the shared A/B/C tune column (`oscNativeTune`). `oscScreenTop` (above the per-tab items): the modal list (`nativeListModal` wrapping the 640 px left panel `oscNativeList` for choice and browser lists; [AGENTS.md](../../../AGENTS.md#lists-owner-2026-11-28)), the note mask (`oscNoteMask`, twelve notes, 640 404 474 120), the detail title (`Native Osc Detail Title`, gated by `IndexedEnabling/1/2/Parameter 3901` so it does not intercept taps on `oscHeader*` when empty) and keyline, the graph frame (`drawNativeBlock_1272x200`, 4 68 1272 200) and the shared block title labels (`Native Osc Block 0..7` at `y = 156, 272, 388, 504`). The oscillator display (wavetable, spectrum, sample picture) is drawn by the plugin (`osc_display.h`) under the native controls. On the Key 61, the `oscHeader*` controls (`TYPE`, `SOURCE`, `BROWSE`, `ROUTE`) respond and injected taps reach the Osc body with the `Native Osc Detail Title` gate and the header focus chain (`ignoreMousePresses: true` on header forms, `acceptsHWFocus: "Children"` on header instances, `"Yes"` on action cells; `docs/dsp-knowledge/skin-host-behaviour.md`).

## One component, three forms

The five tabs run the same component. The plugin binds the oscillator by the view index (1 Sub, 2 A, 3 B, 4 C, 5 Noise) and publishes which settings layout is live; the layouts are in `oscNativeSettingsLayer` and are gated by the settings-view handle (`kOscSettingsView`, `IndexedEnabling/<k>/9/`) and by the on/off of the source (`kOscPower`). A source that is off shows its controls under ONE translucent veil (`oscNativeOffVeil`, one instance for Sub, one shared for A/B/C, one per Noise layout, gated on the power handle; it also takes the touches), not a disabled twin of every control.

Osc A/B/C share ONE `oscNativeAbcSettings` instance, ONE `oscHeaderAbc` instance, ONE `NativeActive oscNativeTune` instance, and ONE `oscNativeOffVeil` instance (gated `RangedEnabling/1/3/5/Parameter 2876`). The plugin retargets the shared A/B/C handles (`Parameter 3004..3021` `OscAbc0..17`, `Parameter 3022..3025` Tune SEM/FIN/CRS/TRACK, `Parameter 3026` `kOscAbcTitle`, `Parameter 2965` power, `Parameter 2999` routing) to the currently selected oscillator A, B, or C, invalidating revisions in `SettingsBank::publish` on source switch so in-flight gestures from the previous oscillator are rejected.

Detail pages share ONE `oscDetailGrid` (gated `RangedEnabling/3/5/6/Parameter 3334` for `Plain`, `Graph`, `GraphWithLoad`):
- `Plain` pages (`MORE`, `OPTIONS`) use unified rows `0..3` (`block / 2`) and `Native Osc Block 0..7`.
- `Graph` pages keep the graph display (`4 68 1272 200`, `kLoadY = 196`) above and map block rows to unified rows `1..3` (`block / 2 + 1`) and `Native Osc Block 2..7`.
- Per-row tile definitions carry only the menu kinds reachable in that row: row 0 `oscDetailTileNoMenu` (`{}`), row 1 `oscDetailTilem0m1m2m4m7m8` (`{0,1,2,4,7,8}`), row 2 `oscDetailTilem5m6m9` (`{5,6,9}`), row 3 `oscDetailTilem3` (`{3}`).

| Part | Osc A / B / C (wavetable-type) | Sub | Noise |
|---|---|---|---|
| Settings definition | `oscNativeAbcSettings` (45 controls: type menu, wavetable, BROWSE, DETAIL, unison, two warp slots, frame, phase, random phase, MAP, source, sample scan, granular, spectral, multisample controls, per type); ONE shared instance retargeted to `OscAbc0..17` (`kOscAbcSettings`) | `sub.*`: WAVEFORM menu (693x72), OCT, CRS, PHASE, ENV 1, MONO, FREE RUN | `noise.*`: KIND (417 wide beside SOUND, or 838 wide alone), SOUND, PITCH, FINE, KEY TRACK, FILTER, STEREO, ENV 1; sample variants add START, RANDOM, LOOP, BOUNCE (107 px cells) |
| Tune column | ONE shared `oscNativeTune` (994 202 278 154), retargeted via `kOscAbcSettings + 3` (OCT) and `18..21` (SEM, FIN, CRS, TRACK) | none (OCT and CRS are in the settings) | none |
| Missing Page / Message bounds | 84 72 826 48 / 8 210 978 40 (the right column of the display stays free) | 84 116 1112 48 / 8 234 1264 40 | 84 116 1112 48 / 8 234 1264 40 |
| Detail blend dial | none | `sub_blend` | `noise_blend`, `noise_sample_blend` |

Pan, level and routing (`oscNativePanLayer`, `oscNativeLevelLayer`, `oscNativeRoutingLayer`: bottom right and top right) are the same for all five and bound to the shown source.

## Data

Handles by block (`shell-mpc/native_ui_params.h`): `kOscBase` (source, routing, visibility; `shell-mpc/native_osc_controls.h`), `kOscPower`, `kOscRoutingBySource`, `kOscSettingsView`, `kOscSettings`, `kOscAbcSettings`, `kOscWarpLayout`, `kOscDetail` (`native_osc_detail.h`), `kOscRootLoad`, `kModTargets` (the targets pages), `kNoteMask`, and the text lines `kNativeText` / `kDrawText` / `kTextGate` (titles, missing-page and message labels). The engine parameters of the oscillators (`osc1.*` .. `osc3.*`, `sub.*`, `noise.*`) are bound directly by the cells.

## Q groups

Per form, by `docs/MPC.md`: A/B/C: Basic [TYPE, SOURCE, position, OCT], then UNISON, WARP (wavetable), GRAIN and PLAYBACK (granular), sample and spectral variants; Sub: Basic [WAVEFORM, OCT, CRS, PHASE], BEHAVIOUR [ENV 1, MONO, FREE RUN]; Noise: Basic [KIND, SOUND, FILTER, STEREO], TUNE [PITCH, FINE, KEY TRACK, ENV 1], PLAYBACK [START, RANDOM, LOOP, BOUNCE] for samples. Hidden controls are no members. Touching PAN, LEVEL or ROUTE selects the Mix group of the source.

## C++ owners

`shell-mpc/overlay/osc_ui.h` (rows, Q tables, native bridge, `prepareStandaloneRoot`), `osc_model.h` (cells and rectangles), `osc_page.h` (page of the tab), `osc_spec_page.h` (detail pages of the types), `osc_display.h`, `osc_presets.h`, `wavetable_browser.h`, `play_osc.h` (source numbering); `shell-mpc/native_osc_controls.h` (`osc::Bank`, `osc::SettingsBank`), `native_osc_detail.h` (`oscdetail::Bank`).

## Leave / enter and cold pre-population (AGENTS.md page-loading rules 6, 7, 9)

- **Cold pre-population (rule 7):** `PluginInstance::preparePlayPresentation()` (startup, `effSetProgram`, `effSetChunk`) seeds the Osc A root presentation (`nativeOsc`, `nativeOscSettings`, `nativeOscWarpMask`/`nativeOscSourceLabel`, `nativeOscDetail`) suspended (`interactive = false`) without claiming gestures or Q groups, and signals `preparedOscTabDue`/`preparedOscSettingsDue`/`preparedOscDetailDue` so the display pollers publish the initial read-only values once.
- **Leave (`OscPageUi::leftTab`, rule 6):** Closes popups/modulations/note-mask (`setNativeActionPopup(-1)`, `hideNativeModulations`, `hideNativeNoteMask`, `dropGesture`) and suspends input via `suspendNativeOsc()` (or `suspendNativeOscRoot()` if leaving from a detail subpage or switched source), disarming `post`/`take` and renewing input revisions while keeping the last published root handles readable by host `getParameter`/`effGetParamDisplay`. While suspended, `nativeOscPoll`, `nativeOscSettingsPoll`, and `nativeOscDetailPoll` return without emitting zero bursts.
- **Enter (`OscPageUi::update`, rule 9):** Re-arms all three banks (`interactive = true`) and publishes fresh values. `reportNativeValue()` arms `nativePagePaintUntil` (`kNativePaintWindowMs = 150 ms`) on `kOscBase + kVisibility`, `kOscBase + kSource`, and `kOscSettingsView` for both `0 -> visible` and `visible -> visible` transitions, and `nativeOscSettingsPoll`/`nativeOscDetailPoll` request metadata refreshes via `requestNativeControlNames()`.
