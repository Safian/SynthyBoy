# Play screen

**Tab:** Play (`play1`, function key Play, first sub-tab): one tab body, no screen family. **Picture:** `../../screens/play.jpg`.

## What it shows

Three panels side by side (`shell-mpc/overlay/play_view.h` draws the oscillator cycle, the envelope curve with the live dot and the LFO shape with the live cursor of the instances the three selectors name; the controls are native components):

- **Oscillator panel** (left third): the header selector (`playOscHeaderSelectorW360`, or `...RoutedW288` beside the route field), per source (Sub, Osc A, Osc B, Osc C, Noise) a power button, the type / source / waveform field, the pan and level dials (`playOscPan56`, `playOscLevel56`, `playSubLevel56`), the number or filter field, and the route header (`playOscRouteHeader`). The controls of the five sources occupy the same place; the plugin publishes the set of the source `Play Osc` selects, and each field is a `NativeActive` / `NativeInactive` pair gated by that source's power handle (`IndexedEnabling` on the power parameter).
- **Envelope panel** (middle): the header selector (`playEnvHeaderSelectorW388`) and the Attack, Decay, Sustain, Release dials (`compactNativeDial56`).
- **LFO panel** (right): the header selector (`playLfoHeaderSelectorW396`), the shape / style, rate, division, sync (HZ, BPM, KEY buttons or the sync field), stereo, rise and smoothing controls; the LFO display frame (`drawNativeBlock_396x328`, 864 74 396 328). The set of controls depends on the LFO kind (LFO or random) and on the sync mode (rate or division); smoothing has an `Off` readout twin.

Texts: `Native Osc Missing Compact` (a source that is missing) and `Native Osc Message`.

## Definitions

`skin/defs_play.json` (55 definitions: the four header selectors, the titled `play*Titled` fields, the own-row choice overlays `playEnvChoices`, `playOscTypeChoices`, `playOscRouteChoices`, `playLfoStyleChoices`, `playLfoSyncChoices`, the plugin list `playNativeList`, the 56 px dials `compactNativeDial56`, `compactNativeRiseDial56`, `compactNativeSmoothWithIndicator56`, `compactNativeSmoothReadout56` and their NativeActive/NativeInactive pairs).

## Data

The three selector parameters `Play Osc`, `Play Env`, `Play LFO` and the 24 Play proxies (the legacy prefix, [../../MPC.md](../../MPC.md), "Parameter order" and "Play selectors and proxies"); blocks of `shell-mpc/native_ui_params.h`: `kPlayOsc` (`native_play_osc.h`), `kPlayEnvLfo` (`native_play_env_lfo.h`), `kPlayLfoStandard`, `kPlayLfoStandardSync`, `kOscPower`, `kNativeText`, `kPlayList` ("Play List", Parameter 3902), the modulation picker's list cells (`kModBoardControl`, `nativeui::modboard`) and the list names (`setNativeListText`). The Play page keeps its own selection, independent of the Env, LFO and Osc tabs.

### Lists with more than eight options

The host MenuOverlay cannot show a VST2's choice names, so the long Play lists are the plugin's own paged native list (7 rows a page, BACK, UP/GROUPS, PREVIOUS, NEXT), the same cells the Osc pages use. A radio Button of the skin writes a code into "Play List" (`shell-mpc/native_play_list.h`: stepped integer 0..6, not automatable, never stored, getParameter 0, empty display; the host's double press of one tap is debounced, 250 ms): 1 Source (two levels: the wavetable categories and tables, or the content roots, folders and files; `OscUi::openCompactSourceList`, the same browsers as the Osc list page), 2 LFO 1..10 / RND 1..4, 3 Noise sound (the nine `noise.sample` choices, through the compact Noise First item), 4 LFO shape, 5 LFO division (through `LfoUi::applyCompactNative`), 6 LFO 1..10. `PlayView` opens the list on the page of the current row and publishes it with `NativeListCells`; the skin instance `playNativeList` (opaque backdrop + the `oscNativeList` cells) is gated by the modboard ListGate like the Note tab. The list closes after a final choice, on BACK at the top level (BACK in a category or folder goes up), on a preset change, when the Play oscillator (shape / division: the shown LFO; source: the oscillator type) changes, when a full screen covers the page and on tab exit. While it is open the view draws nothing on the GPU plane (it lies above the host plane) and takes no touch or wheel.

## Tab exit and page loading

Play uses a read-only suspension pattern on exit (`suspendNativePlayOsc`, `suspendNativePlayEnvLfo`), disarming input while keeping compact oscillator, envelope, and LFO parameter bindings valid for host paint. On startup and after every program or chunk restore, `preparePlayPresentation` (`synthyboy_mpc.cpp:2256-2276,3715`) pre-populates these read-only bindings before host exposure, eliminating cold-load widget arrival lag.

## Q groups

Four groups published together (`PlayView::publishQ`): the oscillator's Basic, the envelope's ADSR, the LFO's RATE, and a fourth [LFO SYNC, OSC POWER, LFO MONO, none] of the shown LFO and oscillator. All are stepped targets. The header dropdowns are not Q members.

## C++ owners

`shell-mpc/overlay/play_view.h`, `play_osc.h`, `play_env_lfo.h`, `osc_ui.h` / `env_ui.h` / `lfo_ui.h` (their compact views), `shell-mpc/play_params.h`, `shell-mpc/native_play_osc.h`, `native_play_env_lfo.h`, `native_play_list.h` (the Play List codes and the request port; `IInstanceView::takePlayListRequest`), `overlay/native_list_cells.h` (the list cells), `play_osc_native_contract.h`. Retired unreferenced symbols (`85e92dbb`): `compactLabelRect`, 40 dead skin definitions, and 15 orphaned images.
