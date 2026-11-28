# Osc screen (`oscScreen`, `oscScreenTop`)

**Tabs:** Osc A (`osca`), Osc B (`oscb`), Osc C (`oscc`), Sub (`oscsub`), Noise (`oscnoise`). Each tab holds `Screen` (`oscScreen`) and `Screen Top` (`oscScreenTop`) plus per-tab items: the marker squares (`Overlay Marker osca left/right` ...), the instance picture (`Overlay Instance osca`), the caption `Native Caption osca 0` (988 0 282 36), `Native Osc Missing Page`, `Native Osc Message` and the background sentinel (1180 2). **Pictures:** `../../screens/osc_a.jpg`, `osc_b.jpg`, `osc_c.jpg`, `sub.jpg`, `noise.jpg`.

## What it shows

`oscScreen` (below the per-tab items in the z-order): the modulation editor and targets title and keyline (`defs_common.json`, shared with every family) and `oscNativeSurface` (0 0 1280 628): the power, routing, pan and level layers of the oscillator (gated by the selected source), the per-form settings (`oscNativeSettingsLayer`), the detail pages (`oscNativeDetail`: unison, grain, envelope, blend, load buttons) and the tune column (`oscNativeTune`, Osc A/B/C). `oscScreenTop` (above the per-tab items): the list pages (`oscNativeList`), the note mask (`oscNoteMask`, twelve notes, 640 336 474 120), the detail title and keyline, the graph frame (`drawNativeBlock_1272x212`) and the block title labels. The oscillator display (wavetable, spectrum, sample picture) is drawn by the plugin (`osc_display.h`) under the native controls.

## One component, three forms

The five tabs run the same component. The plugin binds the oscillator by the view index (1 Sub, 2 A, 3 B, 4 C, 5 Noise) and publishes which settings layout is live; the layouts are in `oscNativeSettingsLayer` and are gated by the settings-view handle (`kOscSettingsView`, `IndexedEnabling/<k>/9/`) and by the on/off of the source (`kOscPower`; each control has an `Off` twin).

| Part | Osc A / B / C (wavetable-type) | Sub | Noise |
|---|---|---|---|
| Settings definition | `oscNativeAbcSettings` (45 controls: type menu, wavetable, BROWSE, DETAIL, unison, two warp slots, frame, phase, random phase, MAP, source, sample scan, granular, spectral, multisample controls, per type); one instance per oscillator, bound to its `OscAbc0..7` handles (`kOscAbcSettings`) | `sub.*`: WAVEFORM menu (693x72), OCT, CRS, PHASE, ENV 1, MONO, FREE RUN | `noise.*`: KIND (417 wide beside SOUND, or 838 wide alone), SOUND, PITCH, FINE, KEY TRACK, FILTER, STEREO, ENV 1; sample variants add START, RANDOM, LOOP, BOUNCE (107 px cells) |
| Tune column | `oscNativeTune` A/B/C (994 202 278 154) | none (OCT and CRS are in the settings) | none |
| Missing Page / Message bounds | 84 72 826 48 / 8 210 978 40 (the right column of the display stays free) | 84 116 1112 48 / 8 234 1264 40 | 84 116 1112 48 / 8 234 1264 40 |
| Detail blend dial | none | `sub_blend` | `noise_blend`, `noise_sample_blend` |

Pan, level and routing (`oscNativePanLayer`, `oscNativeLevelLayer`, `oscNativeRoutingLayer`: bottom right and top right) are the same for all five and bound to the shown source.

## Data

Handles by block (`shell-mpc/native_ui_params.h`): `kOscBase` (source, routing, visibility; `shell-mpc/native_osc_controls.h`), `kOscPower`, `kOscRoutingBySource`, `kOscSettingsView`, `kOscSettings`, `kOscAbcSettings`, `kOscWarpLayout`, `kOscDetail` (`native_osc_detail.h`), `kOscRootLoad`, `kModTargets` (the targets pages), `kNoteMask`, and the text lines `kNativeText` / `kDrawText` / `kTextGate` (titles, missing-page and message labels). The engine parameters of the oscillators (`osc1.*` .. `osc3.*`, `sub.*`, `noise.*`) are bound directly by the cells.

## Q groups

Per form, by `docs/MPC.md`: A/B/C: Basic [TYPE, SOURCE, position, OCT], then UNISON, WARP (wavetable), GRAIN and PLAYBACK (granular), sample and spectral variants; Sub: Basic [WAVEFORM, OCT, CRS, PHASE], BEHAVIOUR [ENV 1, MONO, FREE RUN]; Noise: Basic [KIND, SOUND, FILTER, STEREO], TUNE [PITCH, FINE, KEY TRACK, ENV 1], PLAYBACK [START, RANDOM, LOOP, BOUNCE] for samples. Hidden controls are no members. Touching PAN, LEVEL or ROUTE selects the Mix group of the source.

## C++ owners

`shell-mpc/overlay/osc_ui.h` (rows, Q tables, native bridge), `osc_model.h` (cells and rectangles), `osc_page.h` (page of the tab), `osc_spec_page.h` (detail pages of the types), `osc_display.h`, `osc_presets.h`, `wavetable_browser.h`, `play_osc.h` (source numbering); `shell-mpc/native_osc_controls.h`, `native_osc_detail.h`.
