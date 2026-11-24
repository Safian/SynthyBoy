# UI Map: Oscillator Page (`oscui`)
Refreshed 2026-11-23 for standalone source selection and routing.

## Purpose

The Oscillator page provides comprehensive sound generator editing for all five oscillator instances of SynthyBoy.
In the overlay layout (`shell-mpc/overlay/overlay_layout.json:120-304`), the page kind `oscui` defines five dedicated tabs corresponding to the synth's generator instances:
- `oscsub`: Sub oscillator tab ("Sub", instance index 1; `shell-mpc/overlay/overlay_layout.json:231-267`)
- `osca`: Oscillator A tab ("Osc A", instance index 2; `shell-mpc/overlay/overlay_layout.json:120-156`)
- `oscb`: Oscillator B tab ("Osc B", instance index 3; `shell-mpc/overlay/overlay_layout.json:157-193`)
- `oscc`: Oscillator C tab ("Osc C", instance index 4; `shell-mpc/overlay/overlay_layout.json:194-230`)
- `oscnoise`: Noise generator tab ("Noise", instance index 5; `shell-mpc/overlay/overlay_layout.json:268-304`)

Opening an OSC tab selects its namesake. The SOURCE menu can switch that open page to any other source, including an OFF source. Controls, routing, waveform and Q-Links follow the selection. Leaving the tab or loading a preset restores its default. Play keeps a separate selection.

Each tab is registered in `scripts/overlay-skin/mpc-ui-pages-overlay.json:20-55` and instantiated via `pages::make()` (`shell-mpc/overlay/pages.h:42,78`) as `PageView<OscPageUi>`. `OscPageUi` (`shell-mpc/overlay/osc_page.h:16-91`) wraps `OscUi` (`shell-mpc/overlay/osc_ui.h:43-2170`), `OscDisplay` (`shell-mpc/overlay/osc_display.h:28-380`), and `OscMarks` (`shell-mpc/overlay/osc_marks.h:16-103`).

The same underlying `OscUi` class also powers the compact oscillator view on the left third of the Play tab (`shell-mpc/overlay/play_view.h:184-185`), whose full-screen button expands into the same full-screen view (`shell-mpc/overlay/osc_ui.h:481,490`). When displayed as a dedicated tab page, `OscUi` runs in page mode (`setPage(sel)` at `shell-mpc/overlay/osc_ui.h:177`), configuring `OscillatorFullScreen` (`shell-mpc/ui/screen.h:96-370`) across the entire tab area (1280 x 628), with the CLOSE button replaced by the oscillator name title ("SUB", "OSC A", "OSC B", "OSC C", "NOISE"; `shell-mpc/ui/screen.h:158-163`).

The page manages:
1. **Oscillators A, B, and C** (`osc1.`, `osc2.`, `osc3.`): 5 engine types selectable via the type selector (`osc1.type`):
   - Wavetable (type 0): 256-frame wavetable synthesis, phase / random phase, unison voicing, dual warp engines (Warp 1 & 2), and frame remapping.
   - Multisample (type 4): multi-velocity SFZ instruments, velocity tracking, dedicated ADHSR amplitude envelope (`kPageEnvelope`), unison, and dual warps.
   - Sample (type 1): audio sample playback, start/end bounds, loop points, auto/manual transient slicing, tempo lock, unison, and dual warps.
   - Granular (type 2): audio file granular synthesis, position scanning, grain density/duration, window envelope shape/skew, random playback directions, and unison.
   - Spectral (type 3): additive spectral processing, dual-band filter, wavetable-based spectral filtering, morphing, and position scanning.
2. **Sub Oscillator** (`sub.`): 6 basic waveforms (Sine, Half Sine, Triangle, Saw, Square, Pulse) with octave transposition (`sub.octave`), pitch tuning (`sub.tune`), free-run phase, dedicated routing, and mono switch (`sub.mono`).
3. **Noise Generator** (`noise.`): algorithmic noise (white, pink, etc.; `noise.sample`) or sample-based noise playback (`noise.type`), filtering (`noise.filter`), pitch tuning (`noise.pitch`), fine tune (`noise.fine`), key tracking (`noise.keytrack`), stereo spread (`noise.stereo`), and Envelope 1 modulation (`noise.env1`).

The page opens popups and sub-pages for routing (`RoutePanel`), unison voice voicing (`DetailPage`), grain window envelope shaping (`DetailPage`), multisample envelope times (`DetailPage`), engine spec pages (`OscSpecPage` for map, options, region, slices, grain, spectral filter, and spectral scan), modulation matrix depth inspection (`ModulationsPage`), and preset/file selection (`ListPopup`).

## Layout

The page occupies the full MPC overlay tab rectangle `(0, 110, 1280, 628)` (`shell-mpc/overlay/overlay_layout.json:125-130`, `shell-mpc/overlay/osc_ui.h:111`). The default view (bold cut, `docs/ui-map/osc-play-bold-cut/README.md`) has three bands (`shell-mpc/ui/screen.h`, `OscillatorFullScreen::layout`):

```
+------------------------------------------------------------------------------------------------+ y=110
| [TITLE] [POWER] [TOP ROW = the Basic Q group: TYPE | SOURCE | position knob | OCT] [MIX INDICATOR]  | Top band (h 78)
+------------------------------------------------------------------------------------------------+
|                                                                                                |
|                        OSCILLATOR REAL-TIME DISPLAY (the whole width)                         |
|                                                                                                |
+--------------------------------------------------------------------------------+---------------+
| BOTTOM BAND: blocks = the other Q groups (UNISON + WARP, GRAIN + PLAYBACK, ...)  |     [MORE]    |
+--------------------------------------------------------------------------------+---------------+ y=738
```

- **Top band** (`rect.y + 22`, height 78): native source selector (or CLOSE in expanded Play), host-native POWER on the standalone root (green ON, red OFF, routes to the selected source’s `*.on`), the **top row** `OscTopRow` = the first Q group, and the right-hand mix/routing slot: on the standalone OSC overlay tabs while powered ON, host-native destination menu `Osc Native Routing` replaces the read-only `OscMixIndicator` inside an open aperture at `(1122, 143, 150, 56)` (strictly 3 px below the right detector marker); when powered OFF (or when native routing is inactive), the dimmer read-only `OscMixIndicator` (destination, level in dB, pan) renders opaquely in `inactiveGrey`. LEVEL and PAN remain edited on the Mixer; sends stay on the Mixer; the Play page's compact view keeps LEVEL, PAN, the route button and on/off (intentional shortcuts).
  - All five standalone roots use host controls for the top-row and bottom-panel parameters in their eight layouts. Their rectangles, choices and numeric ranges come from the actual C++ bindings. Noise KIND switches the complete layout and hides SOUND in sample mode. Off roots retain their grey overlay controls and reject old settings edits.
  - Osc A/B/C have distinct native bindings in every oscillator type. SOURCE displays the current content label and opens the existing wavetable or file browser. The wavetable SOURCE arrows keep the existing table and file stepping. The native UNISON DETAIL gear opens the Unison page. A Warp set to OFF widens its type selector and hides its amount.
  - The SDK MenuOverlay lists the seven destinations from the replacement strings of the active source’s transient handle. Its current binding rejects a delayed callback to a different source’s handle and discards queued edits when the view, source or preset changes.
  - POWER and the source selector stay operable while the oscillator is OFF. Detail pages hide both native controls. The POWER binding discards pending requests after a source switch, preset recall, detail open or tab departure. Expanded Play keeps its existing overlay POWER control.
  - A, B, C: `TYPE` (five pictograms), `SOURCE` (the table stepper + the list button; a file name for the content types), the position knob of the type (`WT POS`, `TIMBRE`, `SCAN`, `POSITION`, `SCAN`), `OCT`.
  - Sub: `WAVEFORM` (six pictograms), `OCT` (`sub.octave`), `CRS` (`sub.tune`), `PHASE` (`sub.phase`).
  - Noise: `KIND` (`noise.type`), `SOUND` (`noise.sample`; hidden while the noise plays a sample), `FILTER` (`noise.filter`), `STEREO` (`noise.stereo`).
- **Display**: the whole width, at least 240 px; the picture of the oscillator, its live read-outs and markers (`OscDisplay`, `OscMarks`).
- **Bottom band** (`panel`, a `KnobPanel` of titled blocks, one block = one Q group; `shell-mpc/ui/panel.h` `presets::k*Full`) and the ONE **MORE** button at its right (`kMoreW` = 120 px, id `kTuneMore`, opens `oscspec::kMore`):
  - Wavetable: UNISON (`UNISON` voices with the DETAIL gear, `DETUNE`, `FIN`) and WARP (warp 1 and 2: TYPE and AMOUNT each; the amount is hidden while its type is Off).
  - Multisample, Sample: UNISON as above, WARP with warp 1 only. Spectral: UNISON (`UNISON`, `DETUNE`), WARP with warp 1 only.
  - Granular: GRAIN (`DENS`, `LENGTH`, `WARP` type) and PLAYBACK (`OFFSET`, `DIR`, `PITCH`, `FIN`).
  - Sub: BEHAVIOUR (`ENV 1`, `MONO`, `FREE RUN`).
  - Noise generated: TUNE (`PITCH`, `FINE`, `KEY TRACK`, `ENV 1`). Noise sample: TUNE and PLAYBACK (`START`, `RANDOM`, `LOOP`, `BOUNCE`).

Control counts of the default view (POWER counted, MORE and the indicator not): wavetable 12, granular 12, multisample 10, sample 10, spectral 9, sub 8, noise generated 9, noise sample 11 (12 with LOOP on). Before the bold cut every type had about 74.

## Controls

Keys are given for Osc A (`osc1.`); B and C use `osc2.` and `osc3.`. "Q" says where the control sits on the Q-Links (the group of the default view; XL and Key 61 map the groups as in "How the groups sit on the two devices").

| Label on screen | Parameter key | Gesture | Effect | Q | Source |
|---|---|---|---|---|---|
| CLOSE | - | Tap | Closes the full screen the Play page opened (hidden on a tab: the title is there) | - | `shell-mpc/overlay/osc_ui.h:491` |
| POWER | `osc1.on` | Tap (touch only) | Oscillator on / off (red when off; the page goes grey) | none | `shell-mpc/overlay/osc_ui.h:493` |
| Mix indicator (destination, level, pan) | `osc1.destination`, `osc1.level`, `osc1.pan` shown, nothing written | none (read-only) | Shows where the oscillator goes and how loud; edit them on the Mixer | none | `shell-mpc/ui/screen.h` |
| TYPE | `osc1.type` | Tap cell / Q1 | WAVE (0), MULTI (4), SAMPLE (1), GRAIN (2), SPEC (3) | Basic 1 | `shell-mpc/overlay/osc_ui.h:500` |
| WAVEFORM | `sub.waveform` | Tap pictogram / Q1 | SINE, HALF, TRI, SAW, SQR, PULSE | Basic 1 | `shell-mpc/overlay/osc_ui.h:500` |
| TABLE | `osc1.wavetable` | Tap arrow / wheel / Q2; the name or the list button opens the wavetable browser | Built-in table or table file | Basic 2 | `shell-mpc/overlay/osc_ui.h:508` |
| SOURCE | - (content) | Tap the name or the list button | Opens the content browser (WAV / SFZ); the Q slot is empty (no parameter) | Basic 2 (empty) | `shell-mpc/overlay/osc_ui.h:508` |
| WT POS | `osc1.frame` | Drag / wheel / Q3 | Wavetable frame position | Basic 3 | `shell-mpc/overlay/osc_ui.h:601` |
| TIMBRE | `osc1.multi.timbre` | Drag / Q3 | Multisample brightness | Basic 3 | `shell-mpc/overlay/osc_ui.h:601` |
| SCAN (Sample) | `osc1.sample.scan` | Drag / Q3 | Sample scrub position | Basic 3 | `shell-mpc/overlay/osc_ui.h:601` |
| POSITION | `osc1.spec.position` | Drag / Q3 | Spectral frame position | Basic 3 | `shell-mpc/overlay/osc_ui.h:601` |
| SCAN (Grain) | `osc1.gran.position` | Drag / Q3 | Grain centre position | Basic 3 | `shell-mpc/overlay/osc_ui.h:601` |
| OCT | `osc1.transpose` | Drag / wheel / Q4 (octaves of the transpose) | Transpose in octaves (-4 .. +4); SEM is on MORE | Basic 4 | `shell-mpc/overlay/osc_ui.h:525` |
| UNISON (voices) | `osc1.unison.voices` | Up / down / Q1 | Unison voices 1 .. 16 | Unison 1 | `shell-mpc/overlay/osc_ui.h:609` |
| DETAIL (the gear of the unison box) | - | Tap | Opens the unison detail page | - | `shell-mpc/overlay/osc_ui.h:608` |
| DETUNE | `osc1.unison.detune` | Drag / Q2 | Unison detune | Unison 2 | `shell-mpc/overlay/osc_ui.h:601` |
| FIN | `osc1.tune` | Drag / Q3 | Fine tune in cents (not on the spectral default view: it is on MORE) | Unison 3 (Playback 4 for granular) | `shell-mpc/overlay/osc_ui.h:527` |
| WARP 1 TYPE, AMOUNT | `osc1.warp.type`, `osc1.warp.amount` | Tap / list, drag / Q1, Q2 | Warp 1; the amount is hidden while the type is Off | Warp 1, 2 | `shell-mpc/overlay/osc_ui.h:616` |
| WARP 2 TYPE, AMOUNT | `osc1.warp2.type`, `osc1.warp2.amount` | Tap / list, drag / Q3, Q4 | Warp 2 (wavetable default view; the other types: MORE) | Warp 3, 4 | `shell-mpc/overlay/osc_ui.h:619` |
| DENS | `osc1.gran.density` | Drag / Q1 | Grain rate | Grain 1 | `shell-mpc/overlay/osc_ui.h:601` |
| LENGTH | `osc1.gran.length` | Drag / Q2 | Grain length | Grain 2 | `shell-mpc/overlay/osc_ui.h:601` |
| WARP (Grain) | `osc1.warp.type` | Tap opens the list / Q3 | Granular warp type | Grain 3 | `shell-mpc/overlay/osc_ui.h:604` |
| OFFSET | `osc1.gran.rand_offset` | Drag / Q1 | Random position scatter | Playback 1 | `shell-mpc/overlay/osc_ui.h:601` |
| DIR | `osc1.gran.rand_dir` | Drag / Q2 | Reverse-grain probability | Playback 2 | `shell-mpc/overlay/osc_ui.h:601` |
| PITCH (Grain) | `osc1.gran.rand_pitch` | Drag / Q3 | Random pitch per grain | Playback 3 | `shell-mpc/overlay/osc_ui.h:601` |
| CRS (Sub) | `sub.tune` | Drag / Q3 | Sub coarse pitch | Basic 3 | `shell-mpc/overlay/osc_ui.h:601` |
| OCT (Sub) | `sub.octave` | Drag / Q2 | Sub octave | Basic 2 | `shell-mpc/overlay/osc_ui.h:515` |
| PHASE (Sub) | `sub.phase` | Drag / Q4 | Sub start phase | Basic 4 | `shell-mpc/overlay/osc_ui.h:601` |
| ENV 1, MONO, FREE RUN (Sub) | `sub.env1`, `sub.mono`, `sub.phase_contiguous` | Tap toggles / Q1 .. 3 | Envelope 1 routing, mono, free-running phase | Behaviour 1 .. 3 | `shell-mpc/overlay/osc_ui.h:603` |
| KIND (Noise) | `noise.type` | Tap arrows / Q1 | Noise or Sample | Basic 1 | `shell-mpc/overlay/osc_ui.h:602` |
| SOUND (Noise) | `noise.sample` | Tap arrows / Q2 | Which noise (hidden in sample mode) | Basic 2 | `shell-mpc/overlay/osc_ui.h:502` |
| FILTER (Noise) | `noise.filter` | Drag / Q | Noise filter | Basic 2 or 3 | `shell-mpc/overlay/osc_ui.h:517` |
| STEREO (Noise) | `noise.stereo` | Drag / Q | Stereo width | Basic 3 or 4 | `shell-mpc/overlay/osc_ui.h:601` |
| PITCH, FINE (Noise) | `noise.pitch`, `noise.fine` | Drag / Q1, Q2 | Noise pitch and fine | Tune 1, 2 | `shell-mpc/overlay/osc_ui.h:534` |
| KEY TRACK, ENV 1 (Noise) | `noise.keytrack`, `noise.env1` | Tap / Q3, Q4 | Key tracking, envelope 1 routing | Tune 3, 4 | `shell-mpc/overlay/osc_ui.h:536` |
| START, RANDOM, LOOP, BOUNCE (Noise sample) | `noise.phase`, `noise.random_phase`, `noise.loop`, `noise.bounce` | Drag, taps / Q1 .. 4 | Playback start, random start, loop, ping-pong | Playback 1 .. 4 | `shell-mpc/overlay/osc_ui.h:601` |
| MORE | - | Tap | Opens the MORE page (everything else of the oscillator, `oscspec::kMore`) | - | `shell-mpc/overlay/osc_ui.h:530` |
| LOAD SAMPLE, LOAD INSTRUMENT | - | Native tap | Above the graph of an empty Sample, Granular, Spectral or Multisample source. Opens the content browser for that source. Hidden during loading and while OFF. | - | `OscUi::prepareNativeDetail` |

The controls of MORE and its pages are in "Opens" (5). Gone from the default view and reachable on MORE: `osc1.unison.blend`, the warp PHASE knobs (`osc1.warp.phase`, `osc1.warp2.phase`), SEM and CRS (`osc1.pitch_coarse`), TRACK (`osc1.pitch_track`), PHASE and RAND (`osc1.phase`, `osc1.random_phase`), the velocity tracking of the multisample, the random grain values and LOOP, `osc1.filter_blend` for Sub and Noise. The FRAME read-out of the type bar is gone (the display shows the frame); the dead bound-but-never-shown rows of the Sub and Noise bar presets (`STEREO (Bar)`, `ENV 1 (Bar)`) are gone with the type bar.

## Opens

The Oscillator page opens the MORE page and, from it, the detail pages of the type (OPTIONS and the type's own pages), the unison, grain window and multisample envelope pages (from the unison box, MORE), one modulation matrix inspector, and one full-screen list / content browser; the route popup belongs to the Play page's compact view:

### 1. Route Popup (`RoutePanel`, `kPageRoute = 13`)
Opened by the ROUTE button of the Play page's compact view (the intentional shortcut; the standalone OSC tabs use a separate host-native destination menu while the oscillator is on). It displays a framed modal dialog `(300, 294, 680, 220)` over a dimmed backdrop (`shell-mpc/ui/composite.h:744-843`):
- `CLOSE`: MenuButton (route_.close, `kRouteClose`). Tap -> closes popup (`stack.back()`; `shell-mpc/overlay/osc_ui.h:1405`).
- Backdrop: Tap outside dialog box (`kRouteBackdrop`). Tap -> closes popup (`shell-mpc/overlay/osc_ui.h:1405`).
- `DEST`: Stepper (route_.target, `kRouteTarget`). Parameter: `osc1.destination` (or `noise.destination`). Tap arrow / wheel / Q1 turn -> sets filter destination routing (`shell-mpc/overlay/osc_ui.h:815`).
- `F1 F2`: Knob (route_.balance, `kRouteBalance`). Parameter: `osc1.filter_blend` (or `noise.filter_blend`). Rotary drag / wheel / Q2 turn -> sets filter blend balance (`shell-mpc/overlay/osc_ui.h:816`).
- `BUS 1`: Knob (route_.bus1, `kRouteBus1`). Parameter: `osc1.bus1_send` (or `noise.bus1_send`). Rotary drag / wheel / Q3 turn -> sets Aux Bus 1 send level (`shell-mpc/overlay/osc_ui.h:817`).
- `BUS 2`: Knob (route_.bus2, `kRouteBus2`). Parameter: `osc1.bus2_send` (or `noise.bus2_send`). Rotary drag / wheel / Q4 turn -> sets Aux Bus 2 send level (`shell-mpc/overlay/osc_ui.h:818`).

### 2. Unison Detail Page (`DetailPage`, `kPageUnison = 10`)
Opened by the DETAIL button (the gear) of the unison box (`kUnisonGear`) or by the `UNISON` button of MORE (granular). Displays waveform preview with unison voice spread fans (`shell-mpc/overlay/osc_ui.h:190`):
- `BACK`: MenuButton (detail_.back, `kDetailBack`). Tap -> returns to main page (`shell-mpc/overlay/osc_ui.h:1405`).
- UNISON Block (`kUnisonDetailBlocks`, `shell-mpc/ui/panel.h:105`):
  - `MODE`: Stepper (`PanelSlot::Mode`). Parameter: `osc1.unison.mode`. Tap arrow / Q1 turn -> selects unison detune distribution mode (`shell-mpc/ui/panel.h:97`).
  - `STACK`: Stepper (`PanelSlot::Stack`). Parameter: `osc1.unison.stack`. Tap arrow / Q2 turn -> selects octave/interval chord stacking mode (`shell-mpc/ui/panel.h:98`).
  - `WIDTH`: Knob (`PanelSlot::Width`). Parameter: `osc1.unison.spread`. Rotary drag / Q3 turn -> sets unison stereo width spread (`shell-mpc/ui/panel.h:99`).
  - `RANGE`: Knob (`PanelSlot::Range`). Parameter: `osc1.unison.range`. Rotary drag / Q4 turn -> sets detune range window (`shell-mpc/ui/panel.h:100`).
- VOICE SPREAD Block (`kUnisonDetailBlocks`, `shell-mpc/ui/panel.h:105`):
  - `WT POS`: Knob (`PanelSlot::WtSpread`). Parameter: `osc1.frame_spread`. Rotary drag / Q1 turn -> sets wavetable frame position spread per voice (`shell-mpc/ui/panel.h:101`).
  - `WARP 1`: Knob (`PanelSlot::Warp1Spread`). Parameter: `osc1.warp.spread`. Rotary drag / Q2 turn -> sets warp 1 spread per voice (`shell-mpc/ui/panel.h:102`).
  - `WARP 2`: Knob (`PanelSlot::Warp2Spread`). Parameter: `osc1.warp2.spread`. Rotary drag / Q3 turn -> sets warp 2 spread per voice (`shell-mpc/ui/panel.h:103`).
  *(In Granular mode, `START` `osc1.sample.uni_start` and `SPAN` `osc1.sample.uni_span` replace `WT POS` and `WARP 1/2`; `shell-mpc/ui/panel.h:226-227`).*

### 3. Grain Window Detail Page (`DetailPage`, `kPageGrain = 11`)
Opened by the `WINDOW` button of MORE (granular). Displays real-time grain window envelope curve preview:
- `BACK`: MenuButton (detail_.back, `kDetailBack`). Tap -> returns to main page (`shell-mpc/overlay/osc_ui.h:1405`).
- WINDOW Block (`kGrainDetailBlocks`, `shell-mpc/ui/panel.h:115`):
  - `SHAPE`: Stepper (`PanelSlot::Shape`). Parameter: `osc1.gran.window_shape`. Tap arrow / Q1 turn -> selects window envelope profile (`shell-mpc/ui/panel.h:109`).
  - `AMOUNT`: Knob (`PanelSlot::Amount`). Parameter: `osc1.gran.window_amount`. Rotary drag / Q1 turn -> sets window modulation depth (`shell-mpc/ui/panel.h:110`).
  - `RAND` (Amount): Knob (`PanelSlot::AmountRand`). Parameter: `osc1.gran.rand_window`. Rotary drag / Q2 turn -> sets random window amount deviation (`shell-mpc/ui/panel.h:111`).
  - `SKEW`: Knob (`PanelSlot::Skew`). Parameter: `osc1.gran.window_skew`. Rotary drag / Q3 turn -> sets window attack/decay asymmetry (`shell-mpc/ui/panel.h:112`).
  - `RAND` (Skew): Knob (`PanelSlot::SkewRand`). Parameter: `osc1.gran.rand_skew`. Rotary drag / Q4 turn -> sets random window skew variation (`shell-mpc/ui/panel.h:113`).

### 4. Multisample Envelope Detail Page (`DetailPage`, `kPageEnvelope = 12`)
Opened by the `ENV` button of MORE (multisample). Displays dynamic 6-segment ADHSR envelope curve (`shell-mpc/overlay/osc_ui.h:238-250`):
- `BACK`: MenuButton (detail_.back, `kDetailBack`). Tap -> returns to main page (`shell-mpc/overlay/osc_ui.h:1405`).
- TIMES Block (`kEnvelopeBlocks`, `shell-mpc/ui/panel.h:237`):
  - `DELAY`: Knob (`PanelSlot::Delay`). Parameter: `osc1.multi.delay`. Rotary drag / Q1 turn -> sets pre-attack delay time (`shell-mpc/ui/panel.h:232`).
  - `ATTACK`: Knob (`PanelSlot::Attack`). Parameter: `osc1.multi.attack`. Rotary drag / Q2 turn -> sets envelope attack duration (`shell-mpc/ui/panel.h:232`).
  - `HOLD`: Knob (`PanelSlot::Hold`). Parameter: `osc1.multi.hold`. Rotary drag / Q3 turn -> sets peak hold time (`shell-mpc/ui/panel.h:233`).
  - `DECAY`: Knob (`PanelSlot::Decay`). Parameter: `osc1.multi.decay`. Rotary drag / Q4 turn -> sets decay time to sustain level (`shell-mpc/ui/panel.h:233`).
- SUSTAIN AND RELEASE Block (`kEnvelopeBlocks`, `shell-mpc/ui/panel.h:237`):
  - `SUSTAIN`: Knob (`PanelSlot::Sustain`). Parameter: `osc1.multi.sustain`. Rotary drag / Q1 turn -> sets sustain volume level (`shell-mpc/ui/panel.h:234`).
  - `RELEASE`: Knob (`PanelSlot::Release`). Parameter: `osc1.multi.release`. Rotary drag / Q2 turn -> sets release time after note-off (`shell-mpc/ui/panel.h:234`).
  - `OVERRIDE`: Toggle (`PanelSlot::Override`). Parameter: `osc1.multi.override`. Tap toggle / Q3 turn -> overrides embedded instrument envelope (`shell-mpc/ui/panel.h:235`).

### 5. Spec Detail Pages (`OscSpecPage`, pages 16..23)
All `OscSpecPage` views include `BACK` (spec_.back, `kDetailBack` -> `stack.back()`) and an optional `LOAD SAMPLE` button (`kSpecLoad` -> `openBrowser`). A control that does not apply to the type or to the state is hidden (not dimmed) and its room goes to the others; a block with nothing to show is left out; a block of page buttons only is no Q group.
- **Page 17: MORE (`oscspec::kMore`)**, the ONE MORE of the full screen (the MORE button right of the bottom band). One page for the five types; the controls of a type are shown for it only (`oscspec::Cond`: `KWave`, `KMulti`, `KSample`, `KSpec`, `KGran`, `NotGran`, `Warp1On` ...):
  - PITCH: `SEM` (the semitones of `osc1.transpose`, the octaves are OCT of the default view), `CRS` (`osc1.pitch_coarse`), `TRACK` (`osc1.pitch_track`), `FIN` (`osc1.tune`, spectral only: the other types have it in the default view).
  - UNISON: `BLEND` (`osc1.unison.blend`, not granular), `VOICES` (`osc1.unison.voices`) and the `UNISON` page button (granular).
  - WARP: `W1 PHASE` (`osc1.warp.phase`), `WARP 2` type, `W2 AMOUNT` and `W2 PHASE` (`osc1.warp2.type`, `osc1.warp2.amount`, `osc1.warp2.phase`; the second warp is in the default view of the wavetable only); each shown only while its warp type is not Off.
  - PHASE: `PHASE` (`osc1.phase`: wavetable, sample, spectral), `RAND` (`osc1.random_phase`: wavetable, multisample).
  - PAGES: buttons `MAP` (wavetable), `ENV` (multisample), `REGION` and `SLICE` (sample), `FILTER` and `SCAN` (spectral), `GRAIN` and `WINDOW` (granular), `OPTIONS` (every type); no parameter, no Q slot.
  - VELOCITY (multisample): `OVERRIDE` (`osc1.multi.vel_override`), `VEL TRACK` (`osc1.multi.vel_track`). GRAIN (granular): `LEN RAND`, `PAN RAND`, `LVL RAND` (`osc1.gran.rand_length`, `osc1.gran.rand_pan`, `osc1.gran.rand_level`), `LOOP` (`osc1.gran.loop_grains`).
  - Sub and Noise have a MORE with one block, OUTPUT: `BLEND` (`sub.filter_blend`, `noise.filter_blend`): their destination, balance popup and sends are the Mixer's.
- **Page 23: OPTIONS (`oscspec::kOptions`)**, reached from MORE (the former options page of the tune column's MORE button): OSCILLATOR `ON` (`osc1.on`), `ENV 1` (`osc1.env1`), `SMOOTH` (`osc1.smooth_interp`); PITCH `MODE` (`osc1.pitch_mode`), `BEND` (`osc1.pitch_bend_track`), `FIXED` (`osc1.fixed_pitch`, only while TRACK is off); PHASE `MEMORY` (`osc1.phase_memory`), `LEGATO` (`osc1.phase_legato`), `RATIO` (`osc1.ratio_src`); KEY ZONE `LOW`, `HIGH` (`osc1.map.key_low`, `osc1.map.key_high`), `FOLD`, `WARP` (`osc1.map.key_fold`, `osc1.map.key_warp`); VELOCITY ZONE `LOW`, `HIGH` (`osc1.map.vel_low`, `osc1.map.vel_high`); TRANSPOSE SNAP `NOTES`, `SNAP NOTE` (`osc1.transpose_snap`); UNISON SHAPE `CURVE` (`osc1.unison.detune_curve`), `SHAPE` (`osc1.unison.blend_shape`), `LAW` (`osc1.unison.law`), `STACK` (`osc1.unison.stack_repeat`).
- **Page 16: Frame Map (`oscspec::kWtMap`)**: opened by `MAP` on MORE (wavetable). `REMAP` (`osc1.remap.on`), `CURVE` (`osc1.remap.slot`), `RISE`, `FALL` (`osc1.remap.rise`, `osc1.remap.fall`), spectral morph `TYPE`, `AMOUNT`, `SPREAD` (`osc1.morph.type`, `osc1.morph.amount`, `osc1.morph.spread`).
- **Pages 18, 19: Sample Region and Slices (`oscspec::kSampleRegion`, `oscspec::kSampleSlice`)**: `REGION` and `SLICE` on MORE (sample). REGION: `START`, `END` (`osc1.sample.start`, `osc1.sample.end`), `ROOT` (`osc1.sample.root`), `KEY TRACK` (`osc1.sample.keytrack`); LOOP `MODE`, `START`, `END`, `XFADE` (`osc1.sample.loop_mode`, `osc1.sample.loop_start`, `osc1.sample.loop_end`, `osc1.sample.loop_xfade`); `RELATIVE`, `LINK`, `EXIT` (`osc1.sample.loop_relative`, `osc1.sample.loop_link`, `osc1.sample.loop_exit`); EDIT `FADE`, `NORMALIZE`, `REVERSE` (`osc1.sample.fade_edges`, `osc1.sample.normalize`, `osc1.sample.reverse`). SLICE: `MODE`, `THRESHOLD`, `SLICE` (`osc1.sample.slice_mode`, `osc1.sample.slice_threshold`, `osc1.sample.slice`); PLAYBACK `PLAY`, `ROOT` (`osc1.sample.slice_play`, `osc1.sample.slice_root`); SCAN `RANGE`, `REVERSE` (`osc1.sample.scan_range`, `osc1.sample.scan_reverse`); TEMPO `LOCK`, `TO BPM`, `BEATS` (`osc1.sample.scan_lock_tempo`, `osc1.sample.scan_to_bpm`, `osc1.sample.length_beats`).
- **Page 20: Grain Engine (`oscspec::kGrain`)**: `GRAIN` on MORE (granular). DENSITY `MODE`, `DIVISION`, `GRAINS` (`osc1.gran.density_mode`, `osc1.gran.density_sync`, `osc1.gran.density_grains`); LENGTH `MODE`, `DIVISION`, `PERCENT` (`osc1.gran.length_mode`, `osc1.gran.length_sync`, `osc1.gran.length_percent`); SPAWN `PATTERN`, `MAX`, `JUMP`, `REVERSE` (`osc1.gran.spawn_pattern`, `osc1.gran.max_grains`, `osc1.gran.jump_start`, `osc1.gran.reverse_grains`); RANDOM WARP (`osc1.gran.rand_warp`, `osc1.gran.rand_warp2`); POSITION `MANUAL`, `KEY TRACK`, `XFADE` (`osc1.gran.manual`, `osc1.gran.scan_keytrack`); Y AXIS (`osc1.gran.y_axis`, `osc1.gran.xy_y`).
- **Pages 21, 22: Spectral Filter and Scan (`oscspec::kSpecFilter`, `oscspec::kSpecScan`)**: `FILTER` and `SCAN` on MORE (spectral). FILTER: BAND `LOW`, `HIGH`, `SMOOTH`, `AFTER WARP` (`osc1.spec.freq_lo`, `osc1.spec.freq_hi`, `osc1.spec.lohi_smooth`, `osc1.spec.lohi_post_warp`); `CUT`, `MIX`, `SHIFT` (`osc1.spec.cut`, `osc1.spec.mix`, `osc1.spec.freq_shift`); `WAVETABLE`, `TABLE`, `FRAME` (`osc1.spec.flt_use`, `osc1.spec.flt_table`, `osc1.spec.flt_frame`). SCAN: `MANUAL`, `KEY TRACK`, `PHASE LOCK`, `TRANSIENTS` (`osc1.spec.manual`, `osc1.spec.scan_keytrack`, `osc1.spec.phase_lock`, `osc1.spec.transients`); spectral morph; Y AXIS (`osc1.spec.y_axis`, `osc1.spec.xy_y`).

### 6. Modulations Page (`ModulationsPage`, `kPageMods = 14`)
A 600 ms stationary press on a modulatable knob or number field opens that parameter's routes. BACK returns to the sheet where the press began. Each page shows up to six sources, with amount (−100% to +100%), UNI/BI, ON/OFF and REMOVE. The pager reaches every assigned slot. Touching a row selects its amount, polarity and bypass Q-Link group.

The native list uses the shared TARGETS controls. ADD opens the source board. Selecting a source focuses its route, including when it was already assigned on a different page. An empty list goes directly to the board. The fallback page also provides an eight-source quick picker and ADD below the visible rows.

### 7. List and Browser Popup (`ListPopup`, `kPageList = 15`)
Opened by tapping wavetable name (`kBasic + 1`), warp type steppers (`kWarp1Type`, `kWarp2Type`), or content browser (`kBasic + 1`, `kSpecLoad`; `shell-mpc/overlay/osc_ui.h:1814-1882, 1950-1956`):
- `Header`: Tap -> navigates up one folder or parent category (`shell-mpc/overlay/osc_ui.h:1413-1418`).
- `Rows`: Tap row -> selects wavetable preset, audio file, or choice option (`shell-mpc/overlay/osc_ui.h:1470-1494`).
- `Backdrop`: Tap outside list -> dismisses popup (`shell-mpc/overlay/osc_ui.h:1412`).

## Q-Link groups

The hardware encoders Q1..Q4 map to the active group, while subsequent groups wrap around to Q5..Q16 (`OscUi::publishQGroup`, `shell-mpc/overlay/osc_ui.h`; the groups of the main page are built by `OscillatorFullScreen::buildGroups`, `shell-mpc/ui/screen.h`). A hidden control is no member and a group closes up from slot 1 (the Warp group of a warp that is Off has its type only); a group with no live member does not exist. LEVEL, PAN and destination have no Q slot in the standalone default views. The Mixer maps LEVEL, and the compact Play group maps PAN and LEVEL. The Play route popup has its own four Q targets.

| Instance | Group 1 (Basic, the top row) | Group 2 | Group 3 |
|---|---|---|---|
| Wavetable | `osc1.type`, `osc1.wavetable`, `osc1.frame`, OCT (`osc1.transpose`) | UNISON: `osc1.unison.voices`, `osc1.unison.detune`, `osc1.tune` | WARP: `osc1.warp.type`, `osc1.warp.amount`, `osc1.warp2.type`, `osc1.warp2.amount` (kept from the Q-Link mapping) |
| Multisample | `osc1.type`, - (source), `osc1.multi.timbre`, OCT | UNISON as above | WARP: `osc1.warp.type`, `osc1.warp.amount` |
| Sample | `osc1.type`, - (source), `osc1.sample.scan`, OCT | UNISON as above | WARP as multisample |
| Spectral | `osc1.type`, - (source), `osc1.spec.position`, OCT | UNISON: `osc1.unison.voices`, `osc1.unison.detune` | WARP as multisample |
| Granular | `osc1.type`, - (source), `osc1.gran.position`, OCT | GRAIN: `osc1.gran.density`, `osc1.gran.length`, `osc1.warp.type` | PLAYBACK: `osc1.gran.rand_offset`, `osc1.gran.rand_dir`, `osc1.gran.rand_pitch`, `osc1.tune` |
| Sub | `sub.waveform`, `sub.octave`, `sub.tune`, `sub.phase` | BEHAVIOUR: `sub.env1`, `sub.mono`, `sub.phase_contiguous` | - |
| Noise generated | `noise.type`, `noise.sample`, `noise.filter`, `noise.stereo` | TUNE: `noise.pitch`, `noise.fine`, `noise.keytrack`, `noise.env1` | - |
| Noise sample | `noise.type`, `noise.filter`, `noise.stereo` | TUNE as above | PLAYBACK: `noise.phase`, `noise.random_phase`, `noise.loop`, `noise.bounce` (BOUNCE keeps its slot when LOOP is off) |

Tests: `shell-mpc/overlay/tests/qslots_host.cpp` (`testOscillator`, `testOscillatorDefaultViews`, `testNoiseSample`, both devices), `shell-mpc/ui/tests/ui_screen_host.cpp`, `coverage_host.cpp`.

### How the groups sit on the two devices
- **MPC XL (16 knobs, no active group).** A page of at most four groups keeps each group on its own four slots (`QRouter::blockOfGroup`, `shell-mpc/q_router.h:71-79`): every instance of the default view has two or three groups, so Basic = Q1 .. 4, group 2 = Q5 .. 8, group 3 = Q9 .. 12 (Q13 .. 16 empty). MORE and OPTIONS have more groups and page by touch.
- **Key 61 (four knobs, banks).** Bank n = Q 4n-3 .. 4n of the same list: the active group is bank 1, the others follow in order and wrap (`QRouter::mapGroups`, `shell-mpc/overlay/q_publish.h:57-65`).
- The standalone instance selector, native routing menu, POWER, mix indicator and page navigation have no Q slot in the default view.

### Detail Pages:
- **Route Popup** (the Play page's shortcut): Q1 `osc1.destination`, Q2 `osc1.filter_blend`, Q3 `osc1.bus1_send`, Q4 `osc1.bus2_send`.
- **MORE**: one group per block with parameters (PITCH, UNISON, WARP, PHASE, VELOCITY, GRAIN); the PAGES block has no group. **OPTIONS**: seven blocks = seven groups.
- **Unison Page**: Group 0 "UNISON" (Q1 `osc1.unison.mode`, Q2 `osc1.unison.stack`, Q3 `osc1.unison.spread`, Q4 `osc1.unison.range`); Group 1 "VOICE SPREAD" (Q1 `osc1.frame_spread`, Q2 `osc1.warp.spread`, Q3 `osc1.warp2.spread`).
- **Grain Page**: Group 0 "WINDOW" (Q1 `osc1.gran.window_amount`, Q2 `osc1.gran.rand_window`, Q3 `osc1.gran.window_skew`, Q4 `osc1.gran.rand_skew`); Group 1 (Q1 `osc1.gran.window_shape`).
- **Envelope Page**: Group 0 "TIMES" (Q1 `osc1.multi.delay`, Q2 `osc1.multi.attack`, Q3 `osc1.multi.hold`, Q4 `osc1.multi.decay`); Group 1 "SUSTAIN AND RELEASE" (Q1 `osc1.multi.sustain`, Q2 `osc1.multi.release`, Q3 `osc1.multi.override`).
- **Other spec pages**: each titled block forms one Q group of up to 4 controls in reading order.
- **Modulations Page**: Active row forms one Q group: Q1 depth, Q2 polarity, Q3 bypass.

## Live parts

1. **Wavetable Cycle, 3D Stack Frames, and Harmonic Spectrum**:
   - In `OscDisplay::update` (`shell-mpc/overlay/osc_display.h:103-150`), real-time readout targets (frame, warp 1 & 2 amounts, spectral morph) are received from the audio engine.
   - Computes the active single-cycle waveform curve using `synthyboy::OscCycle::computeDisplay` (`shell-mpc/overlay/osc_display.h:121`).
   - Renders 3D-perspective wavetable stack frames behind the active cycle (`shell-mpc/overlay/osc_display.h:144-150`).
   - Generates and draws a faint real-time FFT harmonic spectrum behind the cycle (`shell-mpc/overlay/osc_display.h:140,155,299-300`).
   - In `OscMarks::of` (`shell-mpc/overlay/osc_marks.h:24-95`), renders live remap transfer curves and spectral morph split markers.

2. **Sample Overview, Boundaries, and Animated Voice Cursors**:
   - In Sample, Multisample, and Granular modes, renders 256-point min/max overview polylines of loaded audio data (`shell-mpc/overlay/osc_display.h:120-121`).
   - Draws active sample region start (green line) and end (red line) boundary markers from `osc1.sample.start` and `osc1.sample.end` (`shell-mpc/overlay/osc_marks.h:43-55`).
   - Renders animated voice playhead cursors and active grain windows in real time from in.live engine state (`shell-mpc/overlay/osc_display.h:230-310`).

3. **Sub Oscillator Geometric Waveform**:
   - Computes and draws the mathematical waveform profile (Sine, Half-sine, Triangle, Saw, Square, Pulse) updated dynamically with start phase offset (`fillSub`, `shell-mpc/overlay/osc_display.h:81,319`).

4. **Noise Distribution Preview**:
   - Renders frequency spectrum distribution curve for algorithmic noise or min/max overview for sample noise (`shell-mpc/overlay/osc_display.h:98-135`).

5. **Power-Off Inactive Grey Look**:
   - When `osc1.on` is switched off, `applyInactive()` (`shell-mpc/overlay/osc_ui.h`) sets all controls, titled blocks, and Q frames to `inactiveGrey` (`theme::L().inactiveGrey`). `OscDisplay` renders the waveform ghosted in dim monochrome with a small "OFF" badge (`shell-mpc/overlay/osc_display.h:124-138`).

6. **Missing Content Alert Overlay**:
   - If a referenced audio sample file or user wavetable is missing from disk, `drawMissing()` (`shell-mpc/overlay/osc_ui.h:1616-1632`) draws an amber alert box "content missing: <name>" across the display area with a dashed outline.

7. **Knob Dynamic Modulation Arc Rings**:
   - In `OscUi::update` (`shell-mpc/overlay/osc_ui.h:133-172`), each knob displays real-time modulated arc rings (`modValue`) reflecting dynamic modulation excursion from active LFOs and envelopes.

## Standalone source selection and routing

The native SOURCE selector occupies `(8, 143, 200, 56)` in screen coordinates. Its menu lists Sub, Osc A, Osc B, Osc C and Noise. Each row shows ON or OFF directly from that source's power parameter. Every row can be selected. Selection changes the open page without editing the sound or the Play selector.

The native routing trigger occupies `(1122, 143, 150, 56)`. It reads and writes the selected source's `destination` parameter while powered on. The choices are Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend and None. Switching the source, powering it off, leaving the tab or loading a preset cancels requests that still target the previous binding. With power off, the overlay draws the grey, read-only mix indicator in the routing position. POWER and the source menu remain available.

The standalone tabs share one native surface. Opening MORE or Unison hides the root controls and shows the active sheet's stock controls in their own apertures. MORE, OPTIONS, Unison, Frame Map, Region, Slice, Grain, Spectral Filter, Spectral Scan, Window and Envelope get their keys and captions from the same bindings used by the corresponding overlay page. Long named ranges, including the 99 Warp 2 values, use the SDK `MenuOverlay`. Short lists use arrows. BACK restores the previous page and its Q-Link group.

The overlay draws the oscillator plots and envelope curve, runs the OPTIONS note mask and opens the custom content browser and modulation inspector. A stationary 600 ms touch on a native dial opens the inspector for that parameter. A short drag is owned by the host dial. While a stock menu is visible, the controller withdraws the overlay and suspends its Q-Links. On dismissal it restores the active sheet. Power OFF returns to the source root and disables pending sheet edits.

The five standalone tabs and Play subscribe to one shared set of twelve live values for A, B and C. The selected oscillator supplies its frame and warp/morph values. Sub and Noise use their own live positions from the engine. The Mac tests exercise source-specific Q-Links, waveform readouts, ON/OFF routing and cancellation. Sources: `osc_page.h`, `osc_display.h`, `native_osc_controls.h`, `54_osc_selector.py`, `55_osc_routing.py`, `osc_page_host.cpp`, `controller_host.cpp`.

## Duplicates

1. **Mixer Page (`shell-mpc/overlay/mixer_ui.h`)**: the home of `osc1.level`, `osc1.pan`, `osc1.destination`, `osc1.bus1_send`, `osc1.bus2_send` (and the same of `osc2.`, `osc3.`, `sub.`, `noise.`) and the on switches (`osc1.on`, `sub.on`, `noise.on`). The standalone Osc tabs show read-only level and pan and offer a native destination menu while powered on. Send editing stays on the Mixer and in the Play route popup.
2. **Play Page (`shell-mpc/overlay/play_view.h`)**: the compact view keeps `osc1.on`, `osc1.type`, `osc1.wavetable`, `osc1.destination` (the route button and its popup), `osc1.pan`, `osc1.level`, `sub.waveform`, `sub.octave`, `sub.pan`, `sub.level`, `noise.sample`, `noise.filter`, `noise.pan`, `noise.level` (intentional shortcuts).
3. **Master Page (`shell-mpc/overlay/master_ui.h`)**: `voice.phase_legato` is voice-wide, `osc1.phase_legato` (OPTIONS) is per oscillator.
4. **Inside the oscillator page**: `osc1.on` is the POWER toggle and the `ON` of OPTIONS; `osc1.transpose` is OCT (default view) and SEM (MORE), one parameter in two controls (octaves and semitones). Every other parameter has one control per view: the frame read-out, the type bar's unison button, the TRACK of the tune column and the second FIN / warp PHASE places of the old layout are gone.

## Questionable

1. **SOURCE has no Q slot for the content types**: the first group of multisample, sample, spectral and granular has an empty slot 2 (the source is a file, not a parameter); the group does not close up there because the slot is a placeholder of the control (the slot order Basic 1 .. 4 is the same for every type). An owner decision whether the group should close up to three members.
2. **Noise `SOUND` and `KIND` are two controls**: SIMPLIFY counted one ("SOUND/KIND"); `noise.type` (noise or sample) and `noise.sample` (which noise) are two parameters, so the generated noise has 9 controls, the sample noise has 12 controls.
4. **Warp PHASE knobs are on MORE only**: a user who tunes a warp has to open MORE for the phase; the knob was a small side knob of the warp slot before. Not measured with users.
5. **FIN is a 128 px tall number field in the granular PLAYBACK block** (a one-row block gives every cell the same height); looks large next to the knobs. Cosmetic.
