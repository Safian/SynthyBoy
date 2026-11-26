# LFO / RND standalone screen

Updated 2026-11-26 (one tab page = one screen). The LFO has FOURTEEN hardware sub-tabs on two function keys: `LFO`, `LFO 2` .. `LFO 8` (key `LFO`) and `LFO 9`, `LFO 10`, `RND 1` .. `RND 4` (key `LFO 9`); the views `lfo1` .. `lfo10`, `rnd1` .. `rnd4` of `shell-mpc/overlay/overlay_layout.json` have the index 1 .. 14, which the page takes from `page::Input::instance` (`LfoUi::update`, `pageSlot_`). The page has no SOURCE cell, no dropdown and no selector Q dial, and shares nothing with the Play selection; the handle `LFO Reserved` (`nativeui::lfo::kSelection`) stays in the handle block so that no index moves, and is never published. The sections below that speak of the SOURCE selector describe the old single tab.

This describes the current standalone native skin and its production C++ overlay, rather than the historical shape-grid layout.

The header contains WAVEFORM or STYLE (the whole width the SOURCE cell left, x 8, 1056 px) and TARGETS. The graph spans the page below it. The standard LFO graph has the existing expand icon in its upper-right corner, which opens the real nested waveform editor. There is no EDIT text button or neighbouring-source carousel.

The native host draws controls and owns their normal input. The production `PageView<LfoUi>` / `LfoFullScreen` overlay draws the graph, live cursor, functional block outlines and generation-dependent Q focus. Source selection, parameter values and physical routing use the existing selected-instance contract; this change adds no parameter slots.

## Native components and six layouts

The [native contract](../../scripts/mpc-skin-extras/lfo-native-contract.json) is exported by `lfo_ui_host --skin-contract`. It contains Standard and Random × Hz, BPM and KEY. The Python hook consumes those exact rectangles.

| Area | Controls and current shared primitives |
|---|---|
| Header | Shared native selector for WAVEFORM/STYLE; shared ActionButton for TARGETS |
| Graph | Production LFO shape / Random trace and cursor; shared expand icon for the standard editor |
| RATE | Own blue LEVEL dial or native DIVISION selector; KEY substitutes TRANSPOSE and TUNE number fields |
| Standard RATE additions | RISE plus one SMOOTH ActiveText state caption and the shared Smooth time dial |
| TIMING | HZ/BPM/KEY radio group, MODE, PHASE, DELAY and HOST ActiveText |
| MOTION | DIRECTION, SWING, bipolar PAN-style STEREO dial and MONO ActiveText |
| Random bottom band | RATE/DIVISION or KEY fields, HZ/BPM/KEY, STEREO ActiveText and MODE; no invented Smooth parameter |

The HZ/BPM/KEY buttons use the current `components/native/radio_button` **segmented** variant. The public Sync handle remains the existing five-state engine parameter: Hz=0, Tempo=1, dotted=2, triplet=3, Key=4. The three visible choices write the corresponding existing indices; the BPM indicator also stays active for dotted/triplet states. Physical Q-Link access to those five engine states is retained.

Dials use the current LEVEL/PAN FilmStrip assets. Their real Knob children are square and at least 56×56 px. Side or stacked name/value positions are chosen from the actual contract cell; every child remains inside its own cell. Controls are registered as shared definitions and instances bind to them, without copying complete subtrees per layout.

SMOOTH, HOST, MONO and Random STEREO each have their own correctly named ActiveText assets and a single outward-facing `Active` state. They do not share a fixed SMOOTH picture. Smooth time uses the shared LEVEL/Smooth factory: ON is blue, OFF is gray, the saved time remains visible, and touch/data-wheel/Q writes to time are rejected while OFF. The existing native publication/revision guard also rejects a time gesture queued while OFF immediately before enabling it.

The nested Drawn editor retains its real canvas, tools, point controls, undo, grid, loop and modulation navigation. Its Number/Choice definitions use their exported cell sizes; optional controls retain their existing availability gates. Editor toggles such as SNAP, LOOP and CYCLE LINK have their own captions. No stock red segmented artwork or unresolved SmoothTime definition is used.

## Q-Link generation policy

Gen1 four-knob mode draws **one outline and one unnumbered Q** around the active standalone functional block (the dedicated Smooth area for that bank). It follows the existing active group, without changing parameter routing. Gen1 sixteen-knob mode suppresses the active-bank frame.

Gen2 compiles out the entire local frame bounds/cell/drawing branch, regardless of four- or sixteen-knob runtime mode. No shared Qgroup, core or build policy implementation is duplicated or changed here. Existing expanded Play drawing remains separate from this standalone-only framing path.

## Review images and exact provenance

These are **offline synthetic composites, not device captures**. Each 1280×800 image contains a 1280×628 actual UI tab at y=110 and an outside-UI provenance header. The host layer is interpreted from the generated TUI.json, including nested definitions, real button/FilmStrip assets, handles and visibility gates. The overlay comes from the production C++ page and `scene_cpu.h`; there are no procedural replacement controls or missing-asset fallbacks.

The canonical images use an explicitly compiled **Gen2 / sixteen-knob** fixture. Each fixture exports its own selected source, real normalized native values, formatted engine values, editor availability and generation/knob-count metadata. ON and OFF both retain a 150 ms saved time and the same explicitly seeded 0.42 voice phase; the curve itself may change when smoothing is disabled. The Random trace is the real UI's trace of the documented synthetic live input, not a painted random curve.

- [Standard Hz / Smooth ON](../screens/lfo.jpg)
- [Standard Hz / Smooth OFF](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/lfo-standard_smooth_off-preview.png)
- [Standard BPM / quarter note](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/lfo-standard_bpm-preview.png)
- [Standard KEY](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/lfo-standard_key-preview.png)
- [Drawn expanded editor](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/lfo-drawn_expanded-preview.png)
- [Random Hz](../screens/rnd.jpg)
- [Random BPM](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/lfo-rnd_bpm-preview.png)
- [Random KEY](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/lfo-rnd_key-preview.png)
- [Gen1 / four-knob example](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/lfo-standard-key61-preview.png)
- [Native / overlay / composite breakdown](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-components/komponensek/lfo-screen-breakdown.png)

Generate the canonical states with `python3 scripts/render_ui_screen_lfo_preview.py --mode all --work-dir <temporary-directory> --build-dir <this-checkout-CMake-cache>` and `MPC_SDK` set to the SDK directory. The core cache must belong to the same checkout. A Gen1 example uses `--ui-gen 1 --knobs 4 --mode standard_hz --out <png> --full-frame`. Source changes invalidate the generated-skin cache. Missing assets or a nonsquare production FilmStrip child fail the render instead of silently drawing a substitute.

## Verification and remaining device checks

The targeted native-skin regressions validate referenced definitions, source/shape menus, physical proxies, parameter-specific ActiveText identities, Smooth ON/OFF data, square ≥56 px dial children and recursive child containment across root/editor/detail contracts. The C++ LFO and UI suites cover selected-instance writes, Smooth OFF/queued-input protection, editor navigation and Gen1/Gen2 Q scene behavior.

The opaque fallback and transparent host plane use different ARGB8 blend association orders. Their strict geometry/alpha checks remain. An identical-foreground replay onto opaque ground must agree with the fallback within **one** channel level. A two-level flattened-plane difference is allowed only when that independent replay still passes. The measured RND sample at (12,185) is ground `ff0a0f23`, foreground `cd171d42`: the separate-plane composite is `ff181f48`, while both opaque replay and fallback are `ff19204a`. This proves the two-level blue difference comes from integer blend reassociation rather than an aperture or layout mismatch.

On Key 61 and XL, still verify native popup/focus behavior, source and shape selection, TARGETS return paths, physical Q bank changes, actual smoothing ON/OFF gestures and editor/preset transitions with the final installed package. Offline images and host tests do not establish device acceptance.
