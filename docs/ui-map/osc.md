# UI Map: Oscillator Page (`oscui`)

## Purpose

The Oscillator page provides comprehensive sound generator editing for all five oscillator instances of SynthyBoy.
In the overlay layout (`shell-mpc/overlay/overlay_layout.json:675-859`), the page kind `oscui` defines five dedicated tabs corresponding to the synth's generator instances:
- `oscsub`: Sub oscillator tab ("Sub", instance index 1; `shell-mpc/overlay/overlay_layout.json:786-822`)
- `osca`: Oscillator A tab ("Osc A", instance index 2; `shell-mpc/overlay/overlay_layout.json:675-711`)
- `oscb`: Oscillator B tab ("Osc B", instance index 3; `shell-mpc/overlay/overlay_layout.json:712-748`)
- `oscc`: Oscillator C tab ("Osc C", instance index 4; `shell-mpc/overlay/overlay_layout.json:749-785`)
- `oscnoise`: Noise generator tab ("Noise", instance index 5; `shell-mpc/overlay/overlay_layout.json:823-859`)

Each tab is registered in `scripts/overlay-skin/mpc-ui-pages-overlay.json:20-55` and instantiated via `pages::make()` (`shell-mpc/overlay/pages.h:37, 67`) as `PageView<OscPageUi>`. `OscPageUi` (`shell-mpc/overlay/osc_page.h:16-91`) wraps `OscUi` (`shell-mpc/overlay/osc_ui.h:43-2170`), `OscDisplay` (`shell-mpc/overlay/osc_display.h:28-380`), and `OscMarks` (`shell-mpc/overlay/osc_marks.h:16-103`).

The same underlying `OscUi` class also powers the compact oscillator view on the left third of the Play tab (`shell-mpc/overlay/play_view.h:201`), whose full-screen button expands into the same full-screen view (`shell-mpc/overlay/osc_ui.h:481, 490`). When displayed as a dedicated tab page, `OscUi` runs in page mode (`setPage(sel)` at `shell-mpc/overlay/osc_ui.h:177`), configuring `OscillatorFullScreen` (`shell-mpc/ui/screen.h:96-370`) across the entire tab area (1280 x 628), with the CLOSE button replaced by the oscillator name title ("SUB", "OSC A", "OSC B", "OSC C", "NOISE"; `shell-mpc/ui/screen.h:158-163`).

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

The page occupies the full MPC overlay tab rectangle `(0, 110, 1280, 628)` (`shell-mpc/overlay/overlay_layout.json:680-685`, `shell-mpc/overlay/osc_ui.h:111, 116`).

```
+----------------------------------------------------------------------------------------------------+ y=110
| [TITLE / CLOSE] [POWER]  [BASIC: TYPE / SOUND | SOURCE / OCT / FLT | PAN | LEVEL]          [ROUTE] | Top band (h=78)
+-----------------------------------------------------------------------------------+----------------+ y=210
| [TYPE BAR: FRAME | PHASE | RAND | MAP / GRAIN / REGION / SLICE / FILTER / SCAN]   | TUNE COLUMN    |
+-----------------------------------------------------------------------------------+ [OCT]   [SEM]  |
|                                                                                   | [FIN]   [CRS]  |
|                         OSCILLATOR REAL-TIME DISPLAY                              | [TRACK] [MORE] |
|                       (WAVETABLE / SAMPLE / NOISE / SUB)                          |                |
|                                                                                   |                |
+-----------------------------------------------------------------------------------+----------------+ y=514
| BOTTOM PANEL (h=216)                                                                               |
|  [UNISON / GRAIN / TUNE / SOURCE]             | [WARP / PLAYBACK / PHASE / ROUTING]                |
+----------------------------------------------------------------------------------------------------+ y=738
```

The layout is arranged into three primary vertical sections (`shell-mpc/ui/screen.h:208-281`):

### 1. Top Band (`rect.y + 22` to `rect.y + 100`, height 78 px; `shell-mpc/ui/screen.h:215-226`):
- **Title / Close Area** (`(rect.x + 8, top.y + 11, 84, 56)`): Displays the oscillator title string ("SUB", "OSC A", "OSC B", "OSC C", "NOISE"; `shell-mpc/ui/screen.h:341-342`) in dedicated tab page mode. When opened as a full-screen view from the Play page, this area shows the `CLOSE` menu button (`shell-mpc/ui/screen.h:161`).
- **Power Toggle** (`power`, `(rect.x + 100, top.y + 11, 64, 56)`): ToggleIcon displaying a power icon. When switched off, the icon turns red (`negativeOff = true`, `shell-mpc/ui/screen.h:112`) and dims the rest of the page to `inactiveGrey` (`shell-mpc/overlay/osc_ui.h:1561-1571`).
- **Basic Controls** (`basic`, `(rect.x + 172, top.y, flex_w, 78)`; `shell-mpc/ui/composite.h:613-739`):
  - Slot 0: Type selector (`selector`: 5 pictograms WAVE, MULTI, SAMPLE, GRAIN, SPEC for Osc A/B/C; or 6 pictograms SINE, HALF, TRI, SAW, SQR, PULSE for Sub; `shell-mpc/ui/composite.h:640-655`) or noise profile stepper (`noiseType` with caption "SOUND"; `shell-mpc/ui/composite.h:629`).
  - Slot 1: Content/table selector (source.stepper + source.dropdown for Osc A/B/C), octave number field (`oct`: -4..+4 for Sub), or filter cutoff knob (`filter` for Noise).
  - Slot 2: Panning knob (`pan`: -100% to +100%, bipolar horizontal dial; `shell-mpc/ui/composite.h:627`).
  - Slot 3: Output level knob (`level`: -96 dB to +6 dB; `shell-mpc/ui/composite.h:628`).
- **Route Button** (`route`, `(rect.right() - 80, top.y + 7, 72, 64)`): RouteButton displaying active filter target (F1, F2, F12, Mix, Direct, Balance, Off). Opens `RoutePanel` (`kPageRoute = 13`). Hidden on Sub oscillator (`shell-mpc/ui/screen.h:221`).

### 2. Middle Section (`top.bottom() + 8` to `panelY - 8`; `shell-mpc/ui/screen.h:235-279`):
- **For Oscillators A, B, and C** (`shell-mpc/ui/screen.h:272-279`):
  - **TypeBar Block** (`barBlock_` at `(rect.x + 8, top.bottom() + 8, 985, 70)`): Contains `bar` (`shell-mpc/ui/composite.h:296-369`, h = 56) with 1 to 4 slots in reading order based on active engine kind (`barpresets`: Wavetable, Granular, Sample, Spectral, Multisample; `shell-mpc/ui/composite.h:284-294`).
  - **Display Rectangle** (`display_` at `(rect.x + 8, barBlock_.bottom() + 8, 985, 228)`): Renders real-time single-cycle waveform curve, 3D stack perspective frames, harmonic spectrum, audio sample min/max overview, active loop/slice markers, grain windows, and sounding voice cursors (`shell-mpc/overlay/osc_display.h:190-330`, `shell-mpc/overlay/osc_marks.h:24-101`). If sample content is missing, displays "LOAD SAMPLE" button and missing content alert (`shell-mpc/overlay/osc_ui.h:1616-1632, 2141`).
  - **Tuning Column** (`tuneBlock_` at `(1001, top.bottom() + 8, 271, 306)`): Contains vertical `tune` (`TuneRow`; `shell-mpc/ui/composite.h:143-250`) with three rows:
    - Row 0: `OCT` (-4..+4) and `SEM` (-12..+12) number fields (`osc1.transpose`).
    - Row 1: `FIN` (-100..+100 cents; `osc1.tune`) and `CRS` (-48..+48 semitones; `osc1.pitch_coarse`) number fields.
    - Row 2: `TRACK` LED toggle (`osc1.pitch_track`) and `MORE` detail button (tune.more, opens `oscspec::kMore`).
- **For Sub Oscillator** (`shell-mpc/ui/screen.h:235-243`):
  - TypeBar and Tuning columns are hidden (`barBlock_` and `tuneBlock_` empty).
  - Waveform Display occupies the entire width `(rect.x + 8, top.bottom() + 8, 1264, 298)`, rendering the ideal geometric waveform with live phase cursor.
- **For Noise Generator (Generated mode)** (`shell-mpc/ui/screen.h:244-258`):
  - TypeBar is hidden.
  - Display occupies full width `(rect.x + 8, top.bottom() + 8, 1264, 386)`.
  - TUNE block (`PITCH`, `FINE`, `KEY TRACK` in one horizontal row of three cells) and NOISE block (`KIND`, `STEREO`, `ENV 1` in one horizontal row) sit side by side in the bottom band.
- **For Noise Generator (Sample mode)** (`shell-mpc/ui/screen.h:259-270`):
  - Display sits on the left (w = 985), Tuning column sits on the right (w = 271) with 3 stacked rows (`PITCH`, `FINE`, `KEY TRACK`), and SOURCE / PLAYBACK blocks sit in the bottom band.

### 3. Bottom Panel (`panel`, `(rect.x + 8, 522, 1264, 216)`; `shell-mpc/ui/panel.h:118-263`):
- **Wavetable** (`kWavetableFull`): UNISON block (`WT POS`, `UNISON` box [gear + voice stepper], `DETUNE`, `BLEND`) and WARP block (`WarpPair` for Warp 1 and Warp 2 with type stepper, amount knob, phase knob).
- **Multisample** (`kMultisampleFull`): UNISON block (`TIMBRE`, `UNISON` box, `DETUNE`, `BLEND`) and WARP block.
- **Sample** (`kSampleFull`): UNISON block (`SCAN`, `UNISON` box, `DETUNE`, `BLEND`) and WARP block.
- **Spectral** (`kSpectralFull`): UNISON block (`POSITION`, `UNISON` box, `DETUNE`, `BLEND`) and WARP block.
- **Granular** (`kGranularFull`): GRAIN block (`SCAN`, `DENS`, `LENGTH`, `WARP` menu), PLAYBACK block (`OFFSET`, `DIR`, `PITCH`), and RANDOM block (`LEN RAND`, `PAN RAND`, `LVL RAND`).
- **Sub** (`kSubFull`): TUNE block (`CRS`, `ENV 1`, `MONO`), PHASE block (`PHASE`, `FREE RUN`), and ROUTING block (`DEST`, `BLEND`, `BUS 1`, `BUS 2`).
- **Noise Generated** (`kNoiseGenFull`): TUNE block (`PITCH`, `FINE`, `KEY TRACK`) and NOISE block (`KIND`, `STEREO`, `ENV 1`).
- **Noise Sample** (`kNoiseSampleFull`): SOURCE block (`KIND`, `START`, `STEREO`, `ENV 1`) and PLAYBACK block (`RANDOM`, `LOOP`, `BOUNCE`).

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| CLOSE | - | Tap | Closes full-screen oscillator page (`stack.back()`; hidden in dedicated tab page mode) | `shell-mpc/overlay/osc_ui.h:491` |
| POWER | `osc1.on` | Tap / Q-Link turn | Toggles active oscillator on/off (power icon turns red when off) | `shell-mpc/overlay/osc_ui.h:493` |
| ROUTE | `osc1.destination` | Tap / Q-Link turn | Opens `RoutePanel` popup to set filter routing and bus sends (hidden on Sub) | `shell-mpc/overlay/osc_ui.h:495` |
| TYPE | `osc1.type` | Tap cell / Q1 turn | Selects engine type: WAVE (0), MULTI (4), SAMPLE (1), GRAIN (2), SPEC (3) | `shell-mpc/overlay/osc_ui.h:500` |
| WAVEFORM | `sub.waveform` | Tap pictogram / Q1 turn | Selects sub waveform: SINE (0), HALF (1), TRI (2), SAW (3), SQR (4), PULSE (5) | `shell-mpc/overlay/osc_ui.h:500` |
| SOUND | `noise.sample` | Tap arrow / drag / Q1 turn | Selects algorithmic noise type (white, pink, etc.) or sample instrument index | `shell-mpc/overlay/osc_ui.h:502` |
| TABLE | `osc1.wavetable` | Tap arrow / wheel / Q2 turn | Steps built-in wavetable preset; tap name opens wavetable browser (`ListPopup`) | `shell-mpc/overlay/osc_ui.h:508` |
| TABLE (Browse) | - | Tap | Opens wavetable category and preset browser popup (`ListPopup`) | `shell-mpc/overlay/osc_ui.h:510` |
| SOURCE | - | Tap / Q2 turn | Displays loaded audio file; tap opens Content Browser (`ListPopup`) | `shell-mpc/overlay/osc_ui.h:508` |
| SOURCE (Browse) | - | Tap | Opens Content Browser popup (`ListPopup`) to load WAV / SFZ file | `shell-mpc/overlay/osc_ui.h:510` |
| OCT | `sub.octave` | Drag / wheel / Q2 turn | Sets sub oscillator octave transposition (-4 to +4 octaves) | `shell-mpc/overlay/osc_ui.h:515` |
| FILTER | `noise.filter` | Rotary drag / wheel / Q2 turn | Sets noise generator low-pass / high-pass filter cutoff (0% to 100%) | `shell-mpc/overlay/osc_ui.h:517` |
| PAN | `osc1.pan` | Rotary drag / wheel / Q3 turn | Sets oscillator stereo pan offset (-100% left to +100% right) | `shell-mpc/overlay/osc_ui.h:518` |
| LEVEL | `osc1.level` | Rotary drag / wheel / Q4 turn | Sets oscillator output volume level (-96 dB to +6 dB) | `shell-mpc/overlay/osc_ui.h:519` |
| OCT (Tune) | `osc1.transpose` | Drag / wheel / Q1 turn | Sets pitch octave transposition in steps of 12 semitones (-4 to +4) | `shell-mpc/overlay/osc_ui.h:525` |
| SEM (Tune) | `osc1.transpose` | Drag / wheel / Q2 turn | Sets pitch semitone remainder (-12 to +12 semitones) | `shell-mpc/overlay/osc_ui.h:526` |
| FIN (Tune) | `osc1.tune` | Drag / wheel / Q3 turn | Sets fine pitch tuning in cents (-100 to +100 cents) | `shell-mpc/overlay/osc_ui.h:527` |
| CRS (Tune) | `osc1.pitch_coarse` | Drag / wheel / Q4 turn | Sets coarse pitch offset (-48 to +48 semitones, 0.01 semitone steps) | `shell-mpc/overlay/osc_ui.h:528` |
| TRACK (Tune) | `osc1.pitch_track` | Tap toggle / Q-Link turn | Toggles keyboard MIDI pitch tracking on/off | `shell-mpc/overlay/osc_ui.h:529` |
| MORE (Tune) | - | Tap | Opens Oscillator Options detail page (`oscspec::kMore`) | `shell-mpc/overlay/osc_ui.h:530` |
| PITCH (Noise) | `noise.pitch` | Drag / wheel / Q1 turn | Sets noise coarse pitch offset (-48 to +48 semitones) | `shell-mpc/overlay/osc_ui.h:534` |
| FINE (Noise) | `noise.fine` | Drag / wheel / Q2 turn | Sets noise fine tuning (-100 to +100 cents) | `shell-mpc/overlay/osc_ui.h:535` |
| KEY TRACK (Noise) | `noise.keytrack` | Tap toggle / Q3 turn | Toggles noise keyboard pitch tracking on/off | `shell-mpc/overlay/osc_ui.h:536` |
| FRAME (Bar) | `osc1.frame` | Readout only | Displays current wavetable frame number (0 to 255) | `shell-mpc/overlay/osc_ui.h:545` |
| PHASE (Bar) | `osc1.phase` | Drag / wheel / Q2 turn | Sets oscillator cycle start phase (0 to 360 degrees) | `shell-mpc/overlay/osc_ui.h:546` |
| RAND (Bar) | `osc1.random_phase` | Drag / wheel / Q3 turn | Sets start phase randomization amount (0% to 100%) | `shell-mpc/overlay/osc_ui.h:547` |
| MAP (Bar) | - | Tap | Opens Frame Map detail page (`oscspec::kWtMap`) | `shell-mpc/overlay/osc_ui.h:554` |
| GRAIN (Bar) | - | Tap | Opens Grain Engine detail page (`oscspec::kGrain`) | `shell-mpc/overlay/osc_ui.h:554` |
| AMOUNT (Bar) | `osc1.gran.window_amount` | Drag / wheel / Q2 turn / Tap | Sets grain window envelope depth; tap opens Grain Window detail page | `shell-mpc/overlay/osc_ui.h:556` |
| LOOP (Bar) | `osc1.gran.loop_grains` | Tap arrow / wheel / Q3 turn | Sets granular loop mode: One-shot, Loop grains, Manual (`osc1.gran.manual`) | `shell-mpc/overlay/osc_ui.h:550` |
| UNISON (Bar) | `osc1.unison.voices` | Drag / wheel / Q4 turn / Tap | Sets unison voices (1..16); tap opens Unison detail page (`kPageUnison`) | `shell-mpc/overlay/osc_ui.h:557` |
| REGION (Bar) | - | Tap | Opens Sample Region detail page (`oscspec::kSampleRegion`) | `shell-mpc/overlay/osc_ui.h:554` |
| SLICE (Bar) | - | Tap | Opens Sample Slices and Scan detail page (`oscspec::kSampleSlice`) | `shell-mpc/overlay/osc_ui.h:554` |
| FILTER (Bar) | - | Tap | Opens Spectral Filter detail page (`oscspec::kSpecFilter`) | `shell-mpc/overlay/osc_ui.h:554` |
| SCAN (Bar) | - | Tap | Opens Spectral Scan detail page (`oscspec::kSpecScan`) | `shell-mpc/overlay/osc_ui.h:554` |
| ENV (Bar) | - | Tap | Opens Multisample Envelope detail page (`kPageEnvelope`) | `shell-mpc/overlay/osc_ui.h:558` |
| VEL TRACK Toggle | `osc1.multi.vel_override` | Tap toggle / Q2 turn | Enables velocity tracking override for multisample oscillator | `shell-mpc/overlay/osc_ui.h:562` |
| VEL TRACK Amount | `osc1.multi.vel_track` | Drag / wheel / Q2 turn | Sets multisample velocity tracking sensitivity (0% to 100%) | `shell-mpc/overlay/osc_ui.h:563` |
| STEREO (Bar) | `noise.stereo` | Drag / wheel / Q1 turn | Sets noise stereo spread width (0% to 100%) | `shell-mpc/overlay/osc_ui.h:555` |
| ENV 1 (Bar) | `noise.env1` | Tap toggle / Q2 turn | Routes Envelope 1 to modulate noise amplitude | `shell-mpc/overlay/osc_ui.h:566` |
| WT POS | `osc1.frame` | Rotary drag / wheel / Q1 turn | Sets wavetable frame position (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| UNISON Gear | - | Tap | Opens Unison detail page (`kPageUnison = 10`) | `shell-mpc/overlay/osc_ui.h:608` |
| UNISON Count | `osc1.unison.voices` | Tap arrows / Q2 turn | Sets unison voice count (1 to 16 voices) | `shell-mpc/overlay/osc_ui.h:609` |
| DETUNE | `osc1.unison.detune` | Rotary drag / wheel / Q3 turn | Sets unison voice detune spread (0 to 100 cents) | `shell-mpc/overlay/osc_ui.h:601` |
| BLEND | `osc1.unison.blend` | Rotary drag / wheel / Q4 turn | Sets center-to-side unison voice volume balance (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| WARP 1 Type | `osc1.warp.type` | Tap / wheel / Q1 turn / Tap label | Sets warp 1 distortion algorithm; tap label opens list popup | `shell-mpc/overlay/osc_ui.h:616` |
| WARP 1 Amount | `osc1.warp.amount` | Rotary drag / wheel / Q2 turn | Sets warp 1 transformation intensity (0% to 100%) | `shell-mpc/overlay/osc_ui.h:617` |
| WARP 1 Phase | `osc1.warp.phase` | Rotary drag / wheel / Q3 turn | Sets warp 1 phase / parameter offset (0% to 100%) | `shell-mpc/overlay/osc_ui.h:618` |
| WARP 2 Type | `osc1.warp2.type` | Tap / wheel / Q1 turn / Tap label | Sets warp 2 distortion algorithm; tap label opens list popup | `shell-mpc/overlay/osc_ui.h:619` |
| WARP 2 Amount | `osc1.warp2.amount` | Rotary drag / wheel / Q2 turn | Sets warp 2 transformation intensity (0% to 100%) | `shell-mpc/overlay/osc_ui.h:620` |
| WARP 2 Phase | `osc1.warp2.phase` | Rotary drag / wheel / Q3 turn | Sets warp 2 phase / parameter offset (0% to 100%) | `shell-mpc/overlay/osc_ui.h:621` |
| TIMBRE | `osc1.multi.timbre` | Rotary drag / wheel / Q1 turn | Sets multisample brightness / timbre shift (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| SCAN (Sample) | `osc1.sample.scan` | Rotary drag / wheel / Q1 turn | Sets sample scrub playback position (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| POSITION (Spec) | `osc1.spec.position` | Rotary drag / wheel / Q1 turn | Sets spectral frame extraction position (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| SCAN (Grain) | `osc1.gran.position` | Rotary drag / wheel / Q1 turn | Sets grain extraction center position (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| DENS | `osc1.gran.density` | Rotary drag / wheel / Q2 turn | Sets grain trigger rate / density (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| LENGTH (Grain) | `osc1.gran.length` | Rotary drag / wheel / Q3 turn | Sets grain window playback duration (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| WARP (Grain) | `osc1.warp.type` | Tap / wheel / Q4 turn / Tap label | Selects granular warp distortion mode; tap opens list popup | `shell-mpc/overlay/osc_ui.h:604` |
| OFFSET (Grain) | `osc1.gran.rand_offset` | Rotary drag / wheel / Q1 turn | Sets random position scatter around scan center (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| DIR (Grain) | `osc1.gran.rand_dir` | Rotary drag / wheel / Q2 turn | Sets probability of reverse grain playback (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| PITCH (Grain) | `osc1.gran.rand_pitch` | Rotary drag / wheel / Q3 turn | Sets random pitch deviation per grain (-100% to +100%) | `shell-mpc/overlay/osc_ui.h:601` |
| LEN RAND | `osc1.gran.rand_length` | Rotary drag / wheel / Q1 turn | Sets random grain duration variation (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| PAN RAND | `osc1.gran.rand_pan` | Rotary drag / wheel / Q2 turn | Sets random grain stereo position scatter (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| LVL RAND | `osc1.gran.rand_level` | Rotary drag / wheel / Q3 turn | Sets random grain output volume variation (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| CRS (Sub) | `sub.tune` | Rotary drag / wheel / Q1 turn | Sets sub coarse pitch offset (-48 to +48 semitones) | `shell-mpc/overlay/osc_ui.h:601` |
| ENV 1 (Sub) | `sub.env1` | Tap toggle / Q2 turn | Routes Envelope 1 to modulate sub oscillator amplitude | `shell-mpc/overlay/osc_ui.h:603` |
| MONO | `sub.mono` | Tap toggle / Q3 turn | Forces sub oscillator to monophonic voice allocation | `shell-mpc/overlay/osc_ui.h:603` |
| PHASE (Sub) | `sub.phase` | Rotary drag / wheel / Q1 turn | Sets sub oscillator starting phase (0 to 360 degrees) | `shell-mpc/overlay/osc_ui.h:601` |
| FREE RUN | `sub.phase_contiguous` | Tap toggle / Q2 turn | Enables continuous phase free running across new notes | `shell-mpc/overlay/osc_ui.h:603` |
| DEST (Sub) | `sub.destination` | Tap arrow / wheel / Q1 turn | Sets sub output destination (Direct, F1, F2, F12, Mix, Off) | `shell-mpc/overlay/osc_ui.h:602` |
| BLEND (Sub) | `sub.filter_blend` | Rotary drag / wheel / Q2 turn | Sets sub balance between Filter 1 and Filter 2 | `shell-mpc/overlay/osc_ui.h:601` |
| BUS 1 (Sub) | `sub.bus1_send` | Rotary drag / wheel / Q3 turn | Sets sub send volume to Aux Bus 1 | `shell-mpc/overlay/osc_ui.h:601` |
| BUS 2 (Sub) | `sub.bus2_send` | Rotary drag / wheel / Q4 turn | Sets sub send volume to Aux Bus 2 | `shell-mpc/overlay/osc_ui.h:601` |
| KIND (Noise) | `noise.type` | Tap arrow / wheel / Q1 turn | Selects noise algorithm type: White, Pink, Brown, etc. | `shell-mpc/overlay/osc_ui.h:602` |
| STEREO (Noise) | `noise.stereo` | Rotary drag / wheel / Q2 turn | Sets noise stereo width spread (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| ENV 1 (Noise) | `noise.env1` | Tap toggle / Q3 turn | Routes Envelope 1 to modulate noise amplitude | `shell-mpc/overlay/osc_ui.h:603` |
| START (Noise) | `noise.phase` | Rotary drag / wheel / Q2 turn | Sets noise sample playback start position (0% to 100%) | `shell-mpc/overlay/osc_ui.h:601` |
| RANDOM (Noise) | `noise.random_phase` | Tap toggle / Q1 turn | Enables random start position on each note trigger | `shell-mpc/overlay/osc_ui.h:603` |
| LOOP (Noise) | `noise.loop` | Tap toggle / Q2 turn | Enables loop playback of noise sample | `shell-mpc/overlay/osc_ui.h:603` |
| BOUNCE (Noise) | `noise.bounce` | Tap toggle / Q3 turn | Enables bidirectional ping-pong loop playback of noise sample | `shell-mpc/overlay/osc_ui.h:603` |
| LOAD SAMPLE | - | Tap | Overlay button on empty file display; opens Content Browser | `shell-mpc/overlay/osc_ui.h:324` |

*(Note: Parameter keys for Oscillators B and C use prefixes `osc2.` and `osc3.` for the corresponding controls).*

## Opens

The Oscillator page can open ten distinct detail pages, one modal routing popup, one modulation matrix inspector, and one full-screen list/content browser:

### 1. Route Popup (`RoutePanel`, `kPageRoute = 13`)
Opened by tapping the `ROUTE` button on the top band (`shell-mpc/overlay/osc_ui.h:495, 1407`). It displays a framed modal dialog `(300, 294, 680, 220)` over a dimmed backdrop (`shell-mpc/ui/composite.h:744-843`):
- `CLOSE`: MenuButton (route_.close, `kRouteClose`). Tap -> closes popup (`stack.back()`; `shell-mpc/overlay/osc_ui.h:1405`).
- Backdrop: Tap outside dialog box (`kRouteBackdrop`). Tap -> closes popup (`shell-mpc/overlay/osc_ui.h:1405`).
- `DEST`: Stepper (route_.target, `kRouteTarget`). Parameter: `osc1.destination` (or `noise.destination`). Tap arrow / wheel / Q1 turn -> sets filter destination routing (`shell-mpc/overlay/osc_ui.h:815`).
- `F1 F2`: Knob (route_.balance, `kRouteBalance`). Parameter: `osc1.filter_blend` (or `noise.filter_blend`). Rotary drag / wheel / Q2 turn -> sets filter blend balance (`shell-mpc/overlay/osc_ui.h:816`).
- `BUS 1`: Knob (route_.bus1, `kRouteBus1`). Parameter: `osc1.bus1_send` (or `noise.bus1_send`). Rotary drag / wheel / Q3 turn -> sets Aux Bus 1 send level (`shell-mpc/overlay/osc_ui.h:817`).
- `BUS 2`: Knob (route_.bus2, `kRouteBus2`). Parameter: `osc1.bus2_send` (or `noise.bus2_send`). Rotary drag / wheel / Q4 turn -> sets Aux Bus 2 send level (`shell-mpc/overlay/osc_ui.h:818`).

### 2. Unison Detail Page (`DetailPage`, `kPageUnison = 10`)
Opened by tapping the gear icon `kUnisonGear` in `UnisonBox` (`shell-mpc/overlay/osc_ui.h:1430`) or tapping `UNISON` on the TypeBar (`shell-mpc/overlay/osc_ui.h:1437`). Displays waveform preview with unison voice spread fans (`shell-mpc/overlay/osc_ui.h:190`):
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
Opened by tapping `AMOUNT` on the Granular TypeBar (`shell-mpc/overlay/osc_ui.h:1436`). Displays real-time grain window envelope curve preview:
- `BACK`: MenuButton (detail_.back, `kDetailBack`). Tap -> returns to main page (`shell-mpc/overlay/osc_ui.h:1405`).
- WINDOW Block (`kGrainDetailBlocks`, `shell-mpc/ui/panel.h:115`):
  - `SHAPE`: Stepper (`PanelSlot::Shape`). Parameter: `osc1.gran.window_shape`. Tap arrow / Q1 turn -> selects window envelope profile (`shell-mpc/ui/panel.h:109`).
  - `AMOUNT`: Knob (`PanelSlot::Amount`). Parameter: `osc1.gran.window_amount`. Rotary drag / Q1 turn -> sets window modulation depth (`shell-mpc/ui/panel.h:110`).
  - `RAND` (Amount): Knob (`PanelSlot::AmountRand`). Parameter: `osc1.gran.rand_window`. Rotary drag / Q2 turn -> sets random window amount deviation (`shell-mpc/ui/panel.h:111`).
  - `SKEW`: Knob (`PanelSlot::Skew`). Parameter: `osc1.gran.window_skew`. Rotary drag / Q3 turn -> sets window attack/decay asymmetry (`shell-mpc/ui/panel.h:112`).
  - `RAND` (Skew): Knob (`PanelSlot::SkewRand`). Parameter: `osc1.gran.rand_skew`. Rotary drag / Q4 turn -> sets random window skew variation (`shell-mpc/ui/panel.h:113`).

### 4. Multisample Envelope Detail Page (`DetailPage`, `kPageEnvelope = 12`)
Opened by tapping `ENV` on the Multisample TypeBar (`shell-mpc/overlay/osc_ui.h:1435`). Displays dynamic 6-segment ADHSR envelope curve (`shell-mpc/overlay/osc_ui.h:238-250`):
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

### 5. Spec Detail Pages (`OscSpecPage`, pages 16..22)
All `OscSpecPage` views include `BACK` (spec_.back, `kDetailBack` -> `stack.back()`) and an optional `LOAD SAMPLE` button (`kSpecLoad` -> `openBrowser`):
- **Page 16: Frame Map (`oscspec::kWtMap`)**: Opened by `MAP` button on Wavetable TypeBar (`shell-mpc/overlay/osc_ui.h:1438, 1962`).
  - `REMAP`: Toggle (`osc1.remap.on`). Enables non-linear frame remapping curve (`shell-mpc/overlay/osc_spec_page.h:34`).
  - `CURVE`: List (`osc1.remap.slot`). Selects remap transfer curve (`shell-mpc/overlay/osc_spec_page.h:34`).
  - `RISE`: Knob (`osc1.remap.rise`). Sets remap attack slope (`shell-mpc/overlay/osc_spec_page.h:34`).
  - `FALL`: Knob (`osc1.remap.fall`). Sets remap decay slope (`shell-mpc/overlay/osc_spec_page.h:34`).
  - `TYPE`: Stepper (`osc1.morph.type`). Selects spectral morph mode (`shell-mpc/overlay/osc_spec_page.h:35`).
  - `AMOUNT`: Knob (`osc1.morph.amount`). Sets spectral morph depth (`shell-mpc/overlay/osc_spec_page.h:35`).
  - `SPREAD`: Knob (`osc1.morph.spread`). Sets spectral morph stereo spread (`shell-mpc/overlay/osc_spec_page.h:35`).
- **Page 17: Oscillator Options (`oscspec::kMore`)**: Opened by `MORE` button on TuneRow (`shell-mpc/overlay/osc_ui.h:1406`).
  - OSCILLATOR: `ON` (`osc1.on`), `ENV 1` (`osc1.env1`), `SMOOTH` (`osc1.smooth_interp`), `TRACK` (`osc1.pitch_track`; `shell-mpc/overlay/osc_spec_page.h:40`).
  - PITCH: `MODE` (`osc1.pitch_mode`), `BEND` (`osc1.pitch_bend_track`), `FIXED` (`osc1.fixed_pitch`; `shell-mpc/overlay/osc_spec_page.h:41`).
  - PHASE: `MEMORY` (`osc1.phase_memory`), `LEGATO` (`osc1.phase_legato`), `RATIO` (`osc1.ratio_src`; `shell-mpc/overlay/osc_spec_page.h:42`).
  - KEY ZONE: `LOW` (`osc1.map.key_low`), `HIGH` (`osc1.map.key_high`), `FOLD` (`osc1.map.key_fold`), `WARP` (`osc1.map.key_warp`; `shell-mpc/overlay/osc_spec_page.h:43`).
  - VELOCITY ZONE: `LOW` (`osc1.map.vel_low`), `HIGH` (`osc1.map.vel_high`), `OVERRIDE` (`osc1.multi.vel_override`; `shell-mpc/overlay/osc_spec_page.h:44`).
  - TRANSPOSE SNAP: `NOTES` (`osc1.transpose_snap`), `SNAP NOTE` (`osc1.transpose_snap`; `shell-mpc/overlay/osc_spec_page.h:45`).
  - UNISON SHAPE: `CURVE` (`osc1.unison.detune_curve`), `SHAPE` (`osc1.unison.blend_shape`), `LAW` (`osc1.unison.law`), `STACK` (`osc1.unison.stack_repeat`; `shell-mpc/overlay/osc_spec_page.h:46`).
- **Page 18: Sample Region (`oscspec::kSampleRegion`)**: Opened by `REGION` button on Sample TypeBar (`shell-mpc/overlay/osc_ui.h:1438, 1962`).
  - REGION: `START` (`osc1.sample.start`), `END` (`osc1.sample.end`), `ROOT` (`osc1.sample.root`), `KEY TRACK` (`osc1.sample.keytrack`; `shell-mpc/overlay/osc_spec_page.h:51`).
  - LOOP: `MODE` (`osc1.sample.loop_mode`), `START` (`osc1.sample.loop_start`), `END` (`osc1.sample.loop_end`), `XFADE` (`osc1.sample.loop_xfade`; `shell-mpc/overlay/osc_spec_page.h:52`).
  - LOOP OPTIONS: `RELATIVE` (`osc1.sample.loop_relative`), `LINK` (`osc1.sample.loop_link`), `EXIT` (`osc1.sample.loop_exit`; `shell-mpc/overlay/osc_spec_page.h:53`).
  - EDIT: `FADE` (`osc1.sample.fade_edges`), `NORMALIZE` (`osc1.sample.normalize`), `REVERSE` (`osc1.sample.reverse`; `shell-mpc/overlay/osc_spec_page.h:54`).
- **Page 19: Sample Slices and Scan (`oscspec::kSampleSlice`)**: Opened by `SLICE` button on Sample TypeBar (`shell-mpc/overlay/osc_ui.h:1438, 1962`).
  - SLICE: `MODE` (`osc1.sample.slice_mode`), `THRESHOLD` (`osc1.sample.slice_threshold`), `SLICE` (`osc1.sample.slice`; `shell-mpc/overlay/osc_spec_page.h:58`).
  - PLAYBACK: `PLAY` (`osc1.sample.slice_play`), `ROOT` (`osc1.sample.slice_root`; `shell-mpc/overlay/osc_spec_page.h:59`).
  - SCAN: `RANGE` (`osc1.sample.scan_range`), `REVERSE` (`osc1.sample.scan_reverse`; `shell-mpc/overlay/osc_spec_page.h:60`).
  - TEMPO: `LOCK` (`osc1.sample.scan_lock_tempo`), `TO BPM` (`osc1.sample.scan_to_bpm`), `BEATS` (`osc1.sample.length_beats`; `shell-mpc/overlay/osc_spec_page.h:61`).
- **Page 20: Grain Engine (`oscspec::kGrain`)**: Opened by `GRAIN` button on Granular TypeBar (`shell-mpc/overlay/osc_ui.h:1438, 1962`).
  - DENSITY: `MODE` (`osc1.gran.density_mode`), `DIVISION` (`osc1.gran.density_sync`), `GRAINS` (`osc1.gran.density_grains`; `shell-mpc/overlay/osc_spec_page.h:66`).
  - LENGTH: `MODE` (`osc1.gran.length_mode`), `DIVISION` (`osc1.gran.length_sync`), `PERCENT` (`osc1.gran.length_percent`; `shell-mpc/overlay/osc_spec_page.h:67`).
  - SPAWN: `PATTERN` (`osc1.gran.spawn_pattern`), `MAX` (`osc1.gran.max_grains`), `JUMP` (`osc1.gran.jump_start`), `REVERSE` (`osc1.gran.reverse_grains`; `shell-mpc/overlay/osc_spec_page.h:68`).
  - RANDOM WARP: `WARP 1` (`osc1.gran.rand_warp`), `WARP 2` (`osc1.gran.rand_warp2`; `shell-mpc/overlay/osc_spec_page.h:69`).
  - POSITION: `MANUAL` (`osc1.gran.manual`), `KEY TRACK` (`osc1.gran.scan_keytrack`), `XFADE` (`osc1.sample.loop_xfade`; `shell-mpc/overlay/osc_spec_page.h:70`).
  - Y AXIS: `AXIS` (`osc1.gran.y_axis`), `Y` (`osc1.gran.xy_y`; `shell-mpc/overlay/osc_spec_page.h:71`).
- **Page 21: Spectral Filter (`oscspec::kSpecFilter`)**: Opened by `FILTER` button on Spectral TypeBar (`shell-mpc/overlay/osc_ui.h:1438, 1962`).
  - BAND: `LOW` (`osc1.spec.freq_lo`), `HIGH` (`osc1.spec.freq_hi`), `SMOOTH` (`osc1.spec.lohi_smooth`), `AFTER WARP` (`osc1.spec.lohi_post_warp`; `shell-mpc/overlay/osc_spec_page.h:76`).
  - FILTER: `CUT` (`osc1.spec.cut`), `MIX` (`osc1.spec.mix`), `SHIFT` (`osc1.spec.freq_shift`; `shell-mpc/overlay/osc_spec_page.h:77`).
  - FILTER FROM WAVETABLE: `WAVETABLE` (`osc1.spec.flt_use`), `TABLE` (`osc1.spec.flt_table`), `FRAME` (`osc1.spec.flt_frame`; `shell-mpc/overlay/osc_spec_page.h:78`).
- **Page 22: Spectral Scan (`oscspec::kSpecScan`)**: Opened by `SCAN` button on Spectral TypeBar (`shell-mpc/overlay/osc_ui.h:1438, 1962`).
  - SCAN: `MANUAL` (`osc1.spec.manual`), `KEY TRACK` (`osc1.spec.scan_keytrack`), `PHASE LOCK` (`osc1.spec.phase_lock`), `TRANSIENTS` (`osc1.spec.transients`; `shell-mpc/overlay/osc_spec_page.h:82`).
  - SPECTRAL MORPH: `TYPE` (`osc1.morph.type`), `AMOUNT` (`osc1.morph.amount`), `SPREAD` (`osc1.morph.spread`; `shell-mpc/overlay/osc_spec_page.h:83`).
  - Y AXIS: `AXIS` (`osc1.spec.y_axis`), `Y` (`osc1.spec.xy_y`; `shell-mpc/overlay/osc_spec_page.h:84`).

### 6. Modulations Page (`ModulationsPage`, `kPageMods = 14`)
Opened by 600 ms long press on any knob or number field (`shell-mpc/overlay/osc_ui.h:1459-1463`).
- `BACK`: MenuButton (mods_.back, `kModsBack`). Tap -> returns to main page (`shell-mpc/overlay/osc_ui.h:1405`).
- Modulation Rows (0..7; `shell-mpc/ui/panel.h:682-690`, `shell-mpc/overlay/osc_ui.h:1502-1517`):
  - `depth`: HBar (`kModsRow + 4 * row`). Drag -> sets modulation amount (-100% to +100%).
  - `polarity`: ToggleIcon (`kModsRow + 4 * row + 1`). Tap -> toggles bipolar ("BI") vs unipolar ("UNI").
  - `bypass`: ToggleIcon (`kModsRow + 4 * row + 2`). Tap -> toggles modulation active ("ON") vs bypassed ("OFF").
  - `remove`: MenuButton (`kModsRow + 4 * row + 3`, labelled "X"). Tap -> deletes modulation routing from slot.
- `board`: `ModSourceBoard`. Tapping any source chip instantly routes that modulation source to this parameter (`shell-mpc/ui/panel.h:696, 709`).
- `addSource`: Stepper (`kModsAddStep`). Selects modulation source (`shell-mpc/overlay/osc_ui.h:1518`).
- `add`: MenuButton (`kModsAdd`). Tap -> routes selected source (`shell-mpc/overlay/osc_ui.h:1465`).

### 7. List and Browser Popup (`ListPopup`, `kPageList = 15`)
Opened by tapping wavetable name (`kBasic + 1`), warp type steppers (`kWarp1Type`, `kWarp2Type`), or content browser (`kBasic + 1`, `kSpecLoad`; `shell-mpc/overlay/osc_ui.h:1814-1882, 1950-1956`):
- `Header`: Tap -> navigates up one folder or parent category (`shell-mpc/overlay/osc_ui.h:1413-1418`).
- `Rows`: Tap row -> selects wavetable preset, audio file, or choice option (`shell-mpc/overlay/osc_ui.h:1470-1494`).
- `Backdrop`: Tap outside list -> dismisses popup (`shell-mpc/overlay/osc_ui.h:1412`).

## Q-Link groups

The hardware encoders Q1..Q4 map to the active group, while subsequent groups wrap around to Q5..Q16 (`shell-mpc/overlay/osc_ui.h:2079-2128`, `shell-mpc/ui/screen.h:283-328`):

### Main Page (Osc A/B/C Wavetable mode):
1. **Group 0: "Basic"** (`shell-mpc/ui/screen.h:290-292`):
   - Q1: `osc1.type`
   - Q2: `osc1.wavetable`
   - Q3: `osc1.pan`
   - Q4: `osc1.level`
2. **Group 1: "Type"** (`shell-mpc/ui/screen.h:293-294`):
   - Q1: `osc1.frame` (read-only frame readout)
   - Q2: `osc1.phase`
   - Q3: `osc1.random_phase`
   - Q4: unassigned (`MAP` button)
3. **Group 2: "Tune"** (`shell-mpc/ui/screen.h:295-300`):
   - Q1: `osc1.transpose` (OCT)
   - Q2: `osc1.transpose` (SEM)
   - Q3: `osc1.tune` (FIN)
   - Q4: `osc1.pitch_coarse` (CRS)
4. **Group 3: "Unison"** (`shell-mpc/ui/panel.h:127`, `shell-mpc/ui/screen.h:305-317`):
   - Q1: `osc1.frame` (WT POS)
   - Q2: `osc1.unison.voices`
   - Q3: `osc1.unison.detune`
   - Q4: `osc1.unison.blend`
5. **Group 4: "Warp 1"** (`shell-mpc/ui/screen.h:318-325`):
   - Q1: `osc1.warp.type`
   - Q2: `osc1.warp.amount`
   - Q3: `osc1.warp.phase`
   - Q4: unassigned
6. **Group 5: "Warp 2"** (`shell-mpc/ui/screen.h:318-325`):
   - Q1: `osc1.warp2.type`
   - Q2: `osc1.warp2.amount`
   - Q3: `osc1.warp2.phase`
   - Q4: unassigned

### Main Page (Sub Oscillator mode):
1. **Group 0: "Basic"**: Q1 `sub.waveform`, Q2 `sub.octave`, Q3 `sub.pan`, Q4 `sub.level`.
2. **Group 1: "Tune"**: Q1 `sub.tune` (CRS), Q2 `sub.env1`, Q3 `sub.mono`, Q4 unassigned.
3. **Group 2: "Phase"**: Q1 `sub.phase`, Q2 `sub.phase_contiguous` (FREE RUN), Q3..Q4 unassigned.
4. **Group 3: "Routing"**: Q1 `sub.destination`, Q2 `sub.filter_blend`, Q3 `sub.bus1_send`, Q4 `sub.bus2_send`.

### Main Page (Noise Generator mode):
1. **Group 0: "Basic"**: Q1 `noise.sample`, Q2 `noise.filter`, Q3 `noise.pan`, Q4 `noise.level`.
2. **Group 1: "Tune"**: Q1 `noise.pitch`, Q2 `noise.fine`, Q3 `noise.keytrack`, Q4 unassigned.
3. **Group 2: "Noise"** (or "Source" in Sample mode): Q1 `noise.type`, Q2 `noise.stereo`, Q3 `noise.env1`, Q4 unassigned.
4. **Group 3: "Playback"** (Sample mode only): Q1 `noise.random_phase`, Q2 `noise.loop`, Q3 `noise.bounce`, Q4 unassigned.

### Detail Pages:
- **Route Popup**: Q1 `osc1.destination`, Q2 `osc1.filter_blend`, Q3 `osc1.bus1_send`, Q4 `osc1.bus2_send` (`shell-mpc/overlay/osc_ui.h:2119`).
- **Unison Page**: Group 0 "UNISON" (Q1 `osc1.unison.mode`, Q2 `osc1.unison.stack`, Q3 `osc1.unison.spread`, Q4 `osc1.unison.range`); Group 1 "VOICE SPREAD" (Q1 `osc1.frame_spread`, Q2 `osc1.warp.spread`, Q3 `osc1.warp2.spread`).
- **Grain Page**: Group 0 "WINDOW" (Q1 `osc1.gran.window_amount`, Q2 `osc1.gran.rand_window`, Q3 `osc1.gran.window_skew`, Q4 `osc1.gran.rand_skew`); Group 1 (Q1 `osc1.gran.window_shape`).
- **Envelope Page**: Group 0 "TIMES" (Q1 `osc1.multi.delay`, Q2 `osc1.multi.attack`, Q3 `osc1.multi.hold`, Q4 `osc1.multi.decay`); Group 1 "SUSTAIN AND RELEASE" (Q1 `osc1.multi.sustain`, Q2 `osc1.multi.release`, Q3 `osc1.multi.override`).
- **Spec Pages**: Each titled block forms one Q group of up to 4 controls in reading order (`shell-mpc/overlay/osc_ui.h:2101-2103`).
- **Modulations Page**: Active row forms one Q group: Q1 depth, Q2 polarity, Q3 bypass (`shell-mpc/overlay/osc_ui.h:2108`).

## Live parts

1. **Wavetable Cycle, 3D Stack Frames, and Harmonic Spectrum**:
   - In `OscDisplay::update` (`shell-mpc/overlay/osc_display.h:103-150`), real-time readout targets (frame, warp 1 & 2 amounts, spectral morph) are received from the audio engine.
   - Computes the active single-cycle waveform curve using `synthyboy::OscCycle::computeDisplay` (`shell-mpc/overlay/osc_display.h:121`).
   - Renders 3D-perspective wavetable stack frames behind the active cycle (`shell-mpc/overlay/osc_display.h:144-150`).
   - Generates and draws a faint real-time FFT harmonic spectrum behind the cycle (`shell-mpc/overlay/osc_display.h:160-180`).
   - In `OscMarks::of` (`shell-mpc/overlay/osc_marks.h:24-95`), renders live remap transfer curves and spectral morph split markers.

2. **Sample Overview, Boundaries, and Animated Voice Cursors**:
   - In Sample, Multisample, and Granular modes, renders 256-point min/max overview polylines of loaded audio data (`shell-mpc/overlay/osc_display.h:120-121`).
   - Draws active sample region start (green line) and end (red line) boundary markers from `osc1.sample.start` and `osc1.sample.end` (`shell-mpc/overlay/osc_marks.h:43-55`).
   - Renders animated voice playhead cursors and active grain windows in real time from in.live engine state (`shell-mpc/overlay/osc_display.h:230-310`).

3. **Sub Oscillator Geometric Waveform**:
   - Computes and draws the mathematical waveform profile (Sine, Half-sine, Triangle, Saw, Square, Pulse) updated dynamically with start phase offset (`fillSub`, `shell-mpc/overlay/osc_display.h:81, 330-370`).

4. **Noise Distribution Preview**:
   - Renders frequency spectrum distribution curve for algorithmic noise or min/max overview for sample noise (`shell-mpc/overlay/osc_display.h:98-135`).

5. **Power-Off Inactive Grey Look**:
   - When `osc1.on` is switched off, `applyInactive()` (`shell-mpc/overlay/osc_ui.h:1561-1571`) sets all controls, titled blocks, and Q frames to `inactiveGrey` (`theme::L().inactiveGrey`). `OscDisplay` renders the waveform ghosted in dim monochrome with a small "OFF" badge (`shell-mpc/overlay/osc_display.h:124-138`).

6. **Missing Content Alert Overlay**:
   - If a referenced audio sample file or user wavetable is missing from disk, `drawMissing()` (`shell-mpc/overlay/osc_ui.h:1616-1632`) draws an amber alert box "content missing: <name>" across the display area with a dashed outline.

7. **Knob Dynamic Modulation Arc Rings**:
   - In `OscUi::update` (`shell-mpc/overlay/osc_ui.h:133-172`), each knob displays real-time modulated arc rings (`modValue`) reflecting dynamic modulation excursion from active LFOs and envelopes.

## Duplicates

Several parameter keys controlled on the Oscillator page also appear on other overlay pages:

1. **Mixer Page (`shell-mpc/overlay/mixer_ui.h`)**:
   - `osc1.on`, `osc2.on`, `osc3.on`, `sub.on`, `noise.on`: Duplicated as channel mute / power toggle buttons in mixer strips (`shell-mpc/overlay/mixer_ui.h:352`).
   - `osc1.level`, `osc2.level`, `osc3.level`, `sub.level`, `noise.level`: Duplicated as vertical channel faders in mixer strips (`shell-mpc/overlay/mixer_ui.h:354`).
   - `osc1.pan`, `osc2.pan`, `osc3.pan`, `sub.pan`, `noise.pan`: Duplicated as pan knobs in mixer channel strips (`shell-mpc/overlay/mixer_ui.h:353`).
   - `osc1.destination`, `osc2.destination`, `osc3.destination`, `sub.destination`, `noise.destination`: Duplicated as channel output routing targets in mixer strips (`shell-mpc/overlay/mixer_ui.h:350`).
   - `osc1.bus1_send`, `osc2.bus1_send`, `osc3.bus1_send`, `sub.bus1_send`, `noise.bus1_send`: Duplicated as Aux Send 1 knobs in mixer strips (`shell-mpc/overlay/mixer_ui.h:355`).
   - `osc1.bus2_send`, `osc2.bus2_send`, `osc3.bus2_send`, `sub.bus2_send`, `noise.bus2_send`: Duplicated as Aux Send 2 knobs in mixer strips (`shell-mpc/overlay/mixer_ui.h:356`).

2. **Play Page (`shell-mpc/overlay/play_view.h`)**:
   - The left third of the Play page embeds `OscUi compact_` (`shell-mpc/overlay/play_view.h:201`), exposing `osc1.on`, `osc1.type`, `osc1.wavetable`, `osc1.destination`, `osc1.pan`, `osc1.level`, `sub.waveform`, `sub.octave`, `sub.pan`, `sub.level`, `noise.sample`, `noise.filter`, `noise.pan`, and `noise.level`.

3. **Master Page (`shell-mpc/overlay/master_ui.h`)**:
   - Parameter `voice.phase_legato` (`shell-mpc/overlay/master_ui.h:252`) controls voice-wide phase legato across all voices on the Master page, whereas per-oscillator phase legato `osc1.phase_legato` is located on the oscillator Options page (`oscspec::kMore`, `shell-mpc/overlay/osc_spec_page.h:42`).

4. **Internal Duplicate Controls within the Oscillator Page**:
   - `osc1.frame`: Bound to read-only NumberField `FRAME` on the TypeBar (`shell-mpc/overlay/osc_ui.h:545`) AND bound to rotary knob `WT POS` in the UNISON block of the panel (`shell-mpc/overlay/osc_ui.h:601, 657`).
   - `osc1.unison.voices`: Bound to DetailButton `UNISON` on the TypeBar (`shell-mpc/overlay/osc_ui.h:557`) AND bound to stepper `u->count` in `UnisonBox` on the panel (`shell-mpc/overlay/osc_ui.h:609`).
   - `osc1.on`: Bound to top-band `power` toggle (`shell-mpc/overlay/osc_ui.h:493`) AND duplicated inside the OSCILLATOR block of `oscspec::kMore` (`shell-mpc/overlay/osc_spec_page.h:40`).
   - `osc1.pitch_track`: Bound to Tuning column toggle `track` (`shell-mpc/overlay/osc_ui.h:529`) AND duplicated inside the OSCILLATOR block of `oscspec::kMore` (`shell-mpc/overlay/osc_spec_page.h:40`).

## Questionable

1. **Duplicate WT POS between TypeBar and Panel (`osc1.frame`)**:
   - In Wavetable mode, the TypeBar features a read-only NumberField `FRAME` displaying frame 0..255 (`shell-mpc/overlay/osc_ui.h:545`), while the panel's UNISON block directly underneath features a rotary knob `WT POS` controlling the exact same parameter (`shell-mpc/overlay/osc_ui.h:601, 657`). Having both a read-only field and a knob on the same screen consumes two Q-Link encoder slots (Q1 in "Type" and Q1 in "Unison") unnecessarily.

2. **Redundant Unison Entry Points and Voice Steppers (`osc1.unison.voices`)**:
   - In Wavetable, Multisample, Sample, and Spectral modes, the TypeBar contains a `UNISON` detail button (`shell-mpc/overlay/osc_ui.h:557`) that displays voice count (1..16) and opens `kPageUnison = 10`.
   - Simultaneously, the UNISON block in the panel contains `UnisonBox` (`shell-mpc/ui/panel.h:35-50`) which has both a gear button opening `kPageUnison = 10` (`shell-mpc/overlay/osc_ui.h:608, 1430`) AND a voice count stepper `u->count` (`shell-mpc/overlay/osc_ui.h:609`). Having two separate controls to open the unison detail page and two separate controls to change voice count on the same screen is redundant.

3. **Inconsistent Routing UI between Sub and Main Oscillators**:
   - For Sub oscillator, the top-band `ROUTE` button is explicitly hidden (`shell-mpc/ui/screen.h:221`), and routing controls (`DEST`, `BLEND`, `BUS 1`, `BUS 2`) are placed in the bottom panel (`shell-mpc/overlay/osc_ui.h:708-711`).
   - For Oscillators A/B/C and Noise, the bottom panel has no routing block; routing requires tapping `ROUTE` to open the modal `RoutePanel` popup (`shell-mpc/overlay/osc_ui.h:815-818`).
   - Meanwhile, all four routing parameters are already exposed on the dedicated Mixer page (`shell-mpc/overlay/mixer_ui.h:350, 355-356`).

4. **Redundant Dropdown Button next to Content Stepper**:
   - In `BasicControls`, source.stepper and source.dropdown sit adjacent to each other (`shell-mpc/ui/composite.h:618`).
   - In `OscUi::handle` (`shell-mpc/overlay/osc_ui.h:1419-1425`), tapping either the stepper (`kBasic + 1`) or the dropdown arrow (`kBasic + 2`) executes the exact same action (`openBrowser` or `openWavetableBrowser`). Having both widgets side by side adds visual clutter without distinct functionality.

5. **Split Unison Parameters across Disconnected Detail Pages**:
   - Core unison settings (`osc1.unison.mode`, `osc1.unison.stack`, `osc1.unison.spread`, `osc1.unison.range`, and voice spreads `frame_spread`, `osc1.warp.spread`, `osc1.warp2.spread`) live on `kPageUnison` (`DetailPage`, `shell-mpc/ui/panel.h:96-107`).
   - However, additional unison parameters (`osc1.unison.detune_curve`, `osc1.unison.blend_shape`, `osc1.unison.law`, `osc1.unison.stack_repeat`) are placed in the "UNISON SHAPE" block of `oscspec::kMore` (`shell-mpc/overlay/osc_spec_page.h:46`), which is accessed from an unrelated button on the Tuning column. Users configuring unison voicing are forced to navigate two disconnected detail pages.

6. **Duplicated Controls inside `kMore` Options Page**:
   - In `oscspec::kMore` (`shell-mpc/overlay/osc_spec_page.h:40`), the first block ("OSCILLATOR") contains `ON` (`osc1.on`), `ENV 1` (`osc1.env1`), and `TRACK` (`osc1.pitch_track`).
   - `osc1.on` is already accessible as the top-band power toggle (`shell-mpc/overlay/osc_ui.h:493`), and `osc1.pitch_track` is accessible on the Tuning column (`shell-mpc/overlay/osc_ui.h:529`). Duplicating them inside `kMore` wastes 2 out of the 4 slots in that block.

7. **Granular WARP Menu on Panel Lacks Warp Controls**:
   - In Granular mode, the panel GRAIN block contains a `WARP` menu stepper (`PanelSlot::WarpMenu`, bound to `osc1.warp.type`, `shell-mpc/overlay/osc_ui.h:604, 664`), but Granular mode has no `WarpPair` for warp amount or phase. Instead, random warp amounts `osc1.gran.rand_warp` and `osc1.gran.rand_warp2` are buried in the `oscspec::kGrain` detail page (`shell-mpc/overlay/osc_spec_page.h:69`).
