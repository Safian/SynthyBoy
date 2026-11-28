# Play screen

**Tab:** Play (`play1`, function key Play, first sub-tab): one tab body, no screen family. **Picture:** `../../screens/play.jpg`.

## What it shows

Three panels side by side (`shell-mpc/overlay/play_view.h` draws the oscillator cycle, the envelope curve with the live dot and the LFO shape with the live cursor of the instances the three selectors name; the controls are native components):

- **Oscillator panel** (left third): the header selector (`playOscHeaderSelectorW360`, or `...RoutedW288` beside the route field), per source (Sub, Osc A, Osc B, Osc C, Noise) a power button, the type / source / waveform field, the pan and level dials (`playOscPan56`, `playOscLevel56`, `playSubLevel56`), the number or filter field, and the route header (`playOscRouteHeader`). The controls of the five sources occupy the same place; the plugin publishes the set of the source `Play Osc` selects, and each field is a `NativeActive` / `NativeInactive` pair gated by that source's power handle (`IndexedEnabling` on the power parameter).
- **Envelope panel** (middle): the header selector (`playEnvHeaderSelectorW388`) and the Attack, Decay, Sustain, Release dials (`compactNativeDial56`).
- **LFO panel** (right): the header selector (`playLfoHeaderSelectorW396`), the shape / style, rate, division, sync (HZ, BPM, KEY buttons or the sync field), stereo, rise and smoothing controls; the LFO display frame (`drawNativeBlock_396x328`, 864 74 396 328). The set of controls depends on the LFO kind (LFO or random) and on the sync mode (rate or division); smoothing has an `Off` readout twin.

Texts: `Native Osc Missing Compact` (a source that is missing) and `Native Osc Message`.

## Definitions

`skin/defs_play.json` (47 definitions: the four header selectors and the titled `play*Titled` fields, each list field with a "list open" Button (ListOpen, buttonId = list code), the plugin list modal `nativeListModal` (shared, now in `defs_common.json`), the 56 px dials `compactNativeDial56`, `compactNativeRiseDial56`, `compactNativeSmoothWithIndicator56`, `compactNativeSmoothReadout56` and their NativeActive/NativeInactive pairs).

## Data

The three selector parameters `Play Osc`, `Play Env`, `Play LFO` and the 24 Play proxies (the legacy prefix, [../../MPC.md](../../MPC.md), "Parameter order" and "Play selectors and proxies"); blocks of `shell-mpc/native_ui_params.h`: `kPlayOsc` (`native_play_osc.h`), `kPlayEnvLfo` (`native_play_env_lfo.h`), `kPlayLfoStandard`, `kPlayLfoStandardSync`, `kOscPower`, `kNativeText`, `kPlayList` ("Play List", Parameter 3902), the modulation picker's list cells (`kModBoardControl`, `nativeui::modboard`) and the list names (`setNativeListText`). The Play page keeps its own selection, independent of the Env, LFO and Osc tabs.

### Lists: the plugin's own list, one tap chooses and closes

Every Play choice list opens the shared one-tap paged native list (`nativeListModal` wrapping `oscNativeList`, 640 px left panel, 7 rows a page, header back arrow on deeper levels, row arrows, `PREVIOUS`/`NEXT` only when that page exists, no `BACK`/`UP`/`GROUPS` buttons; see [AGENTS.md](../../../AGENTS.md#lists-owner-2026-11-28)). The host `MenuOverlay` is not used. A radio Button of the skin writes a code into "Play List" (`shell-mpc/native_play_list.h`: stepped integer 0..14, not automatable, never stored, getParameter 0, empty display; the host's double press of one tap is debounced, 250 ms). `PlayView` (`openList`, `publishList`, `applyListRow`) opens the list on the page of the current row, publishes it with `NativeListCells` and applies the tapped row through the same C++ path as the host writing that value into the field's own handle:

| Code | List (title) | Rows, names from | Applied through |
|---|---|---|---|
| 1 | Source | two levels: wavetable categories and tables, or content roots, folders, files (`OscUi::openCompactSourceList`) | the Osc list page's browsers |
| 2 | LFO / RND (SELECT LFO) | LFO 1..10, RND 1..4 (fixed names) | `uibind::chooseLfo` |
| 3 | NOISE SOUND | `noise.sample` choices | `OscUi::applyCompactNativeField` (Noise First) |
| 4 | LFO SHAPE | the shape choices of the standard LFO | `LfoUi::applyCompactNative` (Shape) |
| 5 | LFO DIVISION | the division choices | `LfoUi::applyCompactNative` (Division) |
| 6 | SELECT LFO | LFO 1..10 (fixed names) | `uibind::chooseLfo` |
| 7 | OSCILLATOR | Sub, Oscillator A, B, C, Noise (fixed names: "Play Osc" has no choice list) | `IInstanceView::setPlaySelection (0, row + 1)` = `playSetSelector`, the selector write of `setParameter` |
| 8 | ENVELOPE | Env 1..6 (fixed names) | `uibind::chooseEnv` = `playSetSelector (1, ...)` |
| 9 | TYPE | `oscN.type` choices in the pictogram order of the native cell (`nativeui::osc::kTypeChoices`: Wavetable, Multisample, Sample, Granular, Spectral) | `OscUi::applyCompactNativeField` (First: `applyNativeSetting`, row / 4) |
| 10 | ROUTE | the seven `oscN.destination` / `noise.destination` choices | `OscUi::applyCompactNativeField` (Route: destination index) |
| 11 | SUB WAVE | `sub.waveform` choices | `OscUi::applyCompactNativeField` (Sub First) |
| 12 | OCTAVE | -4 .. 4 (fixed names: `sub.octave` is a whole number) | `OscUi::applyCompactNativeField` (Sub Second, row / 8) |
| 13 | LFO STYLE | `rlfoN.style` choices | `LfoUi::applyCompactNative` (Style) |
| 14 | LFO SYNC | `rlfoN.sync` choices | `LfoUi::applyCompactNative` (Sync) |

A list of a field opens only while that field is active (`PlayView::fieldListParam`, the NativeActive gates of the compact cells): the oscillator fields (`OscUi::compactFieldParam`) need the shown oscillator ON and the field present (Type: A / B / C; Route: A / B / C and Noise; Sub wave and Octave: Sub; Noise sound: Noise), Style and Sync need a random LFO (a standard LFO's mode is its HZ / BPM / KEY cells), Shape a standard LFO. The host's own handles of these fields only show the value: a host write to them is refused (`playoscui::Bank::listOnly`, `playenvlfo::listOnly`). The list closes after a final choice, on a tap beside the panel (the header back arrow in a category or folder goes up one level and restores the page holding the row just left), on a preset change, when the Play oscillator changes, when the field of an oscillator list is no longer active (switched off, type changed), when the shown LFO changes (shape, division, style, sync), when a full screen covers the page and on tab exit. While it is open the view draws nothing on the GPU plane (it lies above the host plane) and takes no touch or wheel. The Play page has no host popup any more: the action-marker popup gating of the compact cells (`setNativePlayOscPopup`, `setNativePlayEnvLfoPopup`, `PlayView::setNativeActionPopup`) is deleted.

## Tab exit and page loading

Play uses a read-only suspension pattern on exit (`suspendNativePlayOsc`, `suspendNativePlayEnvLfo`), disarming input while keeping compact oscillator, envelope, and LFO parameter bindings valid for host paint. On startup and after every program or chunk restore, `preparePlayPresentation` (`synthyboy_mpc.cpp:2256-2276,3715`) pre-populates these read-only bindings before host exposure, eliminating cold-load widget arrival lag.

## Q groups

Four groups published together (`PlayView::publishQ`): the oscillator's Basic, the envelope's ADSR, the LFO's RATE, and a fourth [LFO SYNC, OSC POWER, LFO MONO, none] of the shown LFO and oscillator. All are stepped targets. The header dropdowns are not Q members.

## C++ owners

`shell-mpc/overlay/play_view.h`, `play_osc.h`, `play_env_lfo.h`, `osc_ui.h` / `env_ui.h` / `lfo_ui.h` (their compact views), `shell-mpc/play_params.h`, `shell-mpc/native_play_osc.h`, `native_play_env_lfo.h`, `native_play_list.h` (the Play List codes 1..14, their titles, the fixed row names and the request port; `IInstanceView::takePlayListRequest`), `overlay/native_list_cells.h` (the list cells), `play_osc_native_contract.h`. Retired unreferenced symbols (`85e92dbb`): `compactLabelRect`, 40 dead skin definitions, and 15 orphaned images.
