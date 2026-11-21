# UI Map: LFO Page (`lfoui`)

## Purpose

The LFO page provides complete control over the low-frequency modulation generators of SynthyBoy.
In the overlay layout (`shell-mpc/overlay/overlay_layout.json:121-240`), the page kind `lfoui` defines four dedicated tabs: `lfo1` ("LFO 1"), `lfo2` ("LFO 2"), `lfo3` ("LFO 3"), and `lfo4` ("LFO 4").
Each tab instantiates `PageView<LfoUi>` via `pages::make()` (`shell-mpc/overlay/pages.h:36,66`), which configures `LfoUi` in full-screen tab mode (`shell-mpc/overlay/lfo_ui.h:168-179`).
The same underlying `LfoUi` implementation powers the compact LFO panel on the right third of the Play tab (`shell-mpc/overlay/play_view.h:203`), whose full-screen button expands into the same full-screen view (`shell-mpc/overlay/lfo_ui.h:484, 2104`).

The page supports two generator types (`shell-mpc/overlay/lfo_ui.h:1-17`, `shell-mpc/ui/lfo_screen.h:1-10`):
1. Standard LFOs: 10 cycle-based oscillators (`lfo1` through `lfo10`) with 12 built-in shapes or arbitrary user-drawn shapes, tempo synchronization, trigger envelopes, swing, phase, delay, smoothing, stereo offsets, and mono/polyphonic voice assignment.
2. Random LFOs: 4 stochastic modulation generators (`rlfo1` through `rlfo4`) featuring 5 distinct styles (Perlin noise, sample & hold, sine interpolation, Lorenz attractor, and Rössler attractor), tempo synchronization, trigger modes, and stereo randomization.

The page opens a dedicated multi-point custom shape editor (`ShapeEditorPage`), modulation matrix destination routing (`ModTargetsPage`), per-point modulation (`PointModPage`), on-screen shape preset saving/renaming (`TextEntry`), and parameter modulation depth inspection (`ModulationsPage`).

## Layout

The overlay operates on a 1280 x 800 display, occupying the primary tab area from y = 110 to y = 738 (width 1280, height 628; `shell-mpc/overlay/overlay_layout.json:125-129`, `shell-mpc/overlay/lfo_ui.h:97, 108`).

```
+----------------------------------------------------------------------------------------------------+ y=110
| [CLOSE]  [ 1 ][ 2 ][ 3 ][ 4 ][ 5 ][ 6 ][ 7 ][ 8 ][ 9 ][ 10 ][ R1 ][ R2 ][ R3 ][ R4 ]  [EDIT] [TARGETS]| Top bar
+-----------------------------------------------------------------------------------+----------------+ y=168
|                                                                                   | [SINE][TRI ]...|
|                                                                                   | [SAW ][SQU ]...| Shape
|                               WAVEFORM / TRACE DISPLAY                            | [PULS][DRAW]...| Picker
|                                                                                   +----------------+
|                                                                                   |     [HOST]     |
+------------------------------------+----------------------------------------------+----------------+ y=502
| RATE                               | TIMING                                       | MOTION         |
|  [RATE / DIV]      [RISE]          |  [HZ / BPM / KEY]   [PHASE]                  |  [DIRECTION]   [SWING] |
|                    [SMOOTH]        |  [TRIGGER MODE]     [DELAY]                  |  [MONO]        [STEREO]|
+------------------------------------+----------------------------------------------+----------------+ y=738
```

### 1. Top Bar (y = 112 to 168, h = 56; `shell-mpc/ui/lfo_screen.h:351-362`)
- Left: `CLOSE` button (width 84 px at x = 8; hidden when running as a dedicated tab page via `show (close, !pageMode_)`, visible when opened as a full-screen view from the Play page; `shell-mpc/ui/lfo_screen.h:353-354`).
- Center-Left: `InstanceBar` (width 846 px; 10 LFO buttons labelled "1" through "10" and 4 Random LFO buttons labelled "R1" through "R4"; `shell-mpc/ui/lfo_parts.h:280-290`, `shell-mpc/ui/lfo_screen.h:355`).
- Center-Right: `EDIT` button (width 104 px; hidden in Random LFO mode; opens `ShapeEditorPage`; `shell-mpc/ui/lfo_screen.h:360-362`).
- Right: `TARGETS` button (width 200 px at x = 1072; opens `ModTargetsPage`; `shell-mpc/ui/lfo_screen.h:360-361`).

### 2. Middle Section (y = 183 to 494 for LFO, y = 183 to 592 for RND; `shell-mpc/ui/lfo_screen.h:363-373`)
- Waveform / Trace Display (`display_`): Left area (x = 8, y = 183, w = 952, h = 303 for standard LFO; w = 1024, h = 405 for Random LFO; `shell-mpc/ui/lfo_screen.h:365-366`).
  In standard LFO mode, it renders the 256-point computed waveform curve with lead-in delay/rise shading, loop start/point markers, and animated voice phase cursors. In Random LFO mode, it renders a 192-point scrolling output history trace (`shell-mpc/overlay/lfo_ui.h:1058-1074`).
- Shape Picker (`picker`): Right area (width 304 px at x = 968; `shell-mpc/ui/lfo_screen.h:367-369`).
  * Standard LFO: Grid of 5 columns x 3 rows displaying 13 shapes (12 built-in shapes: Sine, Triangle, Saw Up, Saw Down, Square, Pulse, Step Up, Step Down, Random Steps, Exp Up, Exp Down, Half Sine; plus DRAWN for custom shapes; `shell-mpc/ui/lfo_parts.h:19-48, 81-144`).
  * Random LFO: Grid of 2 columns x 3 rows displaying 5 styles (Perlin, S & H, Sine Int, Lorenz, Rössler; `shell-mpc/ui/lfo_parts.h:51-80`).
- `HOST` Sync Toggle (`hostPick`): Placed directly below the Shape Picker in standard LFO mode (x = 968, y = 366, w = 304, h = 56; `shell-mpc/ui/lfo_screen.h:370-373`).

### 3. Bottom Band (y = 502 to 792, h = 290 for LFO; y = 596 to 792, h = 196 for RND; `shell-mpc/ui/lfo_screen.h:374-453`)
- Standard LFO mode (3 titled blocks with width weights 3:4:3; `shell-mpc/ui/lfo_screen.h:377-389`):
  * Block 0 ("RATE", or "RATE - FOLLOWS THE KEY" when Key sync is active; `shell-mpc/ui/lfo_screen.h:407-415`):
    - Left column: Large `RATE` knob in Hz/Key mode, or `DIVISION` stepper with `TRIP` and `DOT` toggle buttons underneath in BPM mode. In Key follow sync mode, the rate knob is replaced by two stacked number fields: `TRANSPOSE` (-60..+36 semitones) and `TUNE` (-100..+100 cents).
    - Right column: Medium knob `RISE` (fade-in time) stacked over medium knob `SMOOTH` (smoothing time, with an embedded LED toggle switch on its label to enable/disable smoothing).
  * Block 1 ("TIMING"; `shell-mpc/ui/lfo_screen.h:417-426`):
    - Left column: `SYNC` 3-row LED list (HZ, BPM, KEY) stacked over `MODE` trigger stepper (Free-run, Retrigger, Envelope, Sustain envelope, Loop at point, Loop and hold).
    - Right column: Medium knob `PHASE` (start/loop angle) stacked over medium knob `DELAY` (pre-delay time).
  * Block 2 ("MOTION"; `shell-mpc/ui/lfo_screen.h:428-436`):
    - Left column: `DIRECTION` 3-row LED list (Forward, Reverse, Ping pong) stacked over `MONO` LED toggle (polyphonic vs mono voice phase sharing).
    - Right column: Medium knob `SWING` (duty cycle skew) stacked over medium knob `STEREO` (stereo phase offset).
- Random LFO mode (1 titled block "RANDOM", or "RANDOM - RATE FOLLOWS THE KEY"; `shell-mpc/ui/lfo_screen.h:439-453`):
  * Left column: Large `RATE` knob in Hz/Key mode, or `DIVISION` stepper in BPM mode, or `TRANSPOSE` and `TUNE` in Key follow mode.
  * Right area: A single horizontal row containing the `SYNC` LED list (HZ, BPM, KEY), `STEREO` LED toggle, and `MODE` LED list (Free-run, Retrigger).

### 4. Compact View (Play Page Right Third: x = 858, y = 114, w = 408, h = 620; `shell-mpc/ui/lfo_screen.h:20-110`, `shell-mpc/overlay/lfo_ui.h:98, 109`)
- Top: `instance` Carousel (x = 864, y = 120, w = 396, h = 56; steps through LFO 1..10 and RND 1..4; tapping name opens instance list popup; `shell-mpc/ui/lfo_screen.h:72`).
- Waveform Display: (x = 864, y = 184, w = 396, h = 166; `shell-mpc/ui/lfo_screen.h:77-84`), with header band containing title and full-screen expand button `full` at top right (`shell-mpc/ui/lfo_screen.h:84`).
- Control block:
  * Standard LFO: `SHAPE` stepper (h = 56), `RATE` control (h = 72: knob or division stepper with inline HZ/BPM/KEY mode cells), `RISE` knob and `SMOOTH` knob side-by-side (`shell-mpc/ui/lfo_screen.h:97-105`).
  * Random LFO: `STYLE` stepper (h = 56), `RATE` control (h = 72), `SYNC` stepper (h = 56), `STEREO` toggle (h = 56; `shell-mpc/ui/lfo_screen.h:88-94`).

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| CLOSE | - | Tap | Closes full-screen LFO page and returns to previous stack page (`stack.back()`; hidden in dedicated tab page mode) | `shell-mpc/overlay/lfo_ui.h:495` |
| 1 .. 10 | - | Tap cell | Selects active cycle LFO 1 through 10 | `shell-mpc/overlay/lfo_ui.h:496` |
| R1 .. R4 | - | Tap cell | Selects active Random LFO 1 through 4 | `shell-mpc/overlay/lfo_ui.h:497` |
| EDIT | - | Tap | Converts built-in shape to custom if needed and opens `ShapeEditorPage` (hidden in Random mode) | `shell-mpc/overlay/lfo_ui.h:506` |
| TARGETS | - | Tap | Opens `ModTargetsPage` showing modulation destinations routed from this LFO | `shell-mpc/overlay/lfo_ui.h:502` |
| SHAPE (Grid) | `lfo1.shape` | Tap cell / Turn Q-Link | Selects one of 13 shapes (0..11 built-in, 12 DRAWN custom shape) | `shell-mpc/overlay/lfo_ui.h:503` |
| STYLE (Grid) | `rlfo1.style` | Tap cell / Turn Q-Link | Selects one of 5 random styles (Perlin, S&H, Sine Int, Lorenz, Rössler) | `shell-mpc/overlay/lfo_ui.h:503` |
| HOST | `lfo1.host` | Tap / Turn Q-Link | Toggles host transport playback synchronization on/off | `shell-mpc/overlay/lfo_ui.h:505` |
| RATE | `lfo1.rate` | Drag / Turn Q-Link / Wheel | Sets LFO frequency in Hz (0.01..200 Hz; dim in Key follow sync mode) | `shell-mpc/overlay/lfo_ui.h:508` |
| RATE (Random) | `rlfo1.rate` | Drag / Turn Q-Link / Wheel | Sets Random LFO rate in Hz (0.01..200 Hz; dim in Key follow sync mode) | `shell-mpc/overlay/lfo_ui.h:508` |
| DIVISION | `lfo1.division` | Tap left/right / Turn Q-Link / Tap name for list | Sets tempo-synced beat division (1/64 note to 32 bars; BPM sync mode only) | `shell-mpc/overlay/lfo_ui.h:509` |
| DIVISION (Random) | `rlfo1.division` | Tap left/right / Turn Q-Link / Tap name for list | Sets tempo-synced beat division for Random LFO | `shell-mpc/overlay/lfo_ui.h:509` |
| TRIP | `lfo1.sync` | Tap | Toggles triplet timing flag (value 3; BPM sync mode only) | `shell-mpc/overlay/lfo_ui.h:499` |
| DOT | `lfo1.sync` | Tap | Toggles dotted timing flag (value 2; BPM sync mode only) | `shell-mpc/overlay/lfo_ui.h:500` |
| RISE | `lfo1.fade_in` | Drag / Turn Q-Link / Wheel | Sets LFO amplitude fade-in time after note-on (0..8000 ms) | `shell-mpc/overlay/lfo_ui.h:510` |
| SMOOTH | `lfo1.smooth_time` | Drag / Turn Q-Link / Wheel | Sets low-pass smoothing filter transition time (1..1000 ms) | `shell-mpc/overlay/lfo_ui.h:511` |
| SMOOTH (LED switch) | `lfo1.smooth` | Tap label LED / Turn Q-Link | Toggles waveform smoothing low-pass filter on/off | `shell-mpc/overlay/lfo_ui.h:504` |
| SYNC (HZ / BPM / KEY) | `lfo1.sync` | Tap option / Turn Q-Link | Sets sync mode: 0 Free (Hz), 1 Tempo (BPM), 4 Key follow | `shell-mpc/overlay/lfo_ui.h:498` |
| SYNC (Random) | `rlfo1.sync` | Tap option / Turn Q-Link | Sets Random LFO sync mode: 0 Free (Hz), 1 Tempo (BPM), 4 Key follow | `shell-mpc/overlay/lfo_ui.h:498` |
| MODE | `lfo1.sync_type` | Tap left/right / Turn Q-Link / Tap name for list | Sets trigger mode: Free-run, Retrigger, Envelope, Sustain envelope, Loop at point, Loop and hold | `shell-mpc/overlay/lfo_ui.h:512` |
| MODE (Random) | `rlfo1.sync_type` | Tap option / Turn Q-Link | Sets Random LFO trigger mode: Free-run, Retrigger | `shell-mpc/overlay/lfo_ui.h:513` |
| PHASE | `lfo1.phase` | Drag / Turn Q-Link / Wheel | Sets initial waveform cycle phase or loop start point (0..360 degrees) | `shell-mpc/overlay/lfo_ui.h:514` |
| DELAY | `lfo1.delay` | Drag / Turn Q-Link / Wheel | Sets delay time before LFO oscillation begins after note-on (0..4000 ms) | `shell-mpc/overlay/lfo_ui.h:515` |
| DIRECTION | `lfo1.direction` | Tap option / Turn Q-Link | Sets cycle playback direction: Forward, Reverse, Ping pong | `shell-mpc/overlay/lfo_ui.h:516` |
| SWING | `lfo1.swing` | Drag / Turn Q-Link / Wheel | Skews time duration of first vs second half of cycle (0..100%) | `shell-mpc/overlay/lfo_ui.h:517` |
| STEREO | `lfo1.stereo` | Drag / Turn Q-Link / Wheel | Sets stereo phase offset angle between left and right output (-180..+180 degrees) | `shell-mpc/overlay/lfo_ui.h:518` |
| MONO | `lfo1.mono` | Tap / Turn Q-Link | Toggles mono shared LFO across all voices vs polyphonic per-voice phase | `shell-mpc/overlay/lfo_ui.h:519` |
| STEREO (Random) | `rlfo1.stereo` | Tap / Turn Q-Link | Toggles stereo random output (independent left/right channels) on/off | `shell-mpc/overlay/lfo_ui.h:520` |
| TRANSPOSE | `lfo1.keytrack_transpose` | Drag / Turn Q-Link / Wheel | Sets key-tracking pitch transposition (-60..+36 semitones; Key sync only) | `shell-mpc/overlay/lfo_ui.h:521` |
| TUNE | `lfo1.keytrack_tune` | Drag / Turn Q-Link / Wheel | Sets key-tracking fine tuning (-100..+100 cents; Key sync only) | `shell-mpc/overlay/lfo_ui.h:522` |
| TRANSPOSE (Random) | `rlfo1.keytrack_transpose` | Drag / Turn Q-Link / Wheel | Sets key-tracking pitch transposition for Random LFO (-60..+36 semitones) | `shell-mpc/overlay/lfo_ui.h:521` |
| TUNE (Random) | `rlfo1.keytrack_tune` | Drag / Turn Q-Link / Wheel | Sets key-tracking fine tuning for Random LFO (-100..+100 cents) | `shell-mpc/overlay/lfo_ui.h:522` |
| Instance (Compact) | - | Tap left/right / Turn / Tap name | Steps through LFO 1..10 and RND 1..4; tap name opens instance list popup | `shell-mpc/overlay/lfo_ui.h:483` |
| Full Screen (Compact) | - | Tap | Expands compact third into full-screen LFO view (`kElementLfo`) | `shell-mpc/overlay/lfo_ui.h:484` |
| SHAPE (Compact) | `lfo1.shape` | Tap left/right / Turn / Tap name | Cycles active waveform shape; tap name opens choices popup | `shell-mpc/overlay/lfo_ui.h:485` |
| STYLE (Compact) | `rlfo1.style` | Tap left/right / Turn / Tap name | Cycles Random LFO style; tap name opens choices popup | `shell-mpc/overlay/lfo_ui.h:486` |
| RATE (Compact) | `lfo1.rate` | Drag / Turn / Wheel | Sets LFO rate in Hz (0.01..200 Hz; compact panel) | `shell-mpc/overlay/lfo_ui.h:487` |
| DIVISION (Compact) | `lfo1.division` | Tap left/right / Turn / Tap name | Sets beat division (BPM sync mode; compact panel) | `shell-mpc/overlay/lfo_ui.h:488` |
| SYNC Mode (Compact) | `lfo1.sync` | Tap cell | Selects HZ, BPM, or KEY sync mode directly inside RateControl | `shell-mpc/overlay/lfo_ui.h:489` |
| RISE (Compact) | `lfo1.fade_in` | Drag / Turn / Wheel | Sets fade-in time (0..8000 ms; compact panel) | `shell-mpc/overlay/lfo_ui.h:490` |
| SMOOTH (Compact) | `lfo1.smooth_time` | Drag / Turn / Wheel | Sets smoothing time (1..1000 ms; compact panel); label LED toggles `lfo1.smooth` | `shell-mpc/overlay/lfo_ui.h:491` |
| SYNC (Compact RND) | `rlfo1.sync` | Tap left/right / Turn / Tap name | Sets sync mode for Random LFO (compact panel) | `shell-mpc/overlay/lfo_ui.h:492` |
| STEREO (Compact RND) | `rlfo1.stereo` | Tap | Toggles stereo random output on/off (compact panel) | `shell-mpc/overlay/lfo_ui.h:493` |

## Opens

### 1. Shape Editor (`ShapeEditorPage`, `kPageEditor = 40`)
- Opened by: Tapping `EDIT` on the full-screen LFO page (`shell-mpc/overlay/lfo_ui.h:1091, 2115`).
- Purpose: Vector-style multi-point waveform designer for custom LFO shapes (up to 128 points).
- Controls on `ShapeEditorPage`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| BACK | - | Tap | Flushes edited shape to engine content and returns to LFO page | `shell-mpc/overlay/lfo_ui.h:1319` |
| POINT | - | Tap tool cell / Turn | Selects active canvas tool: POINT (edit single vertex), FLAT (zero-slope step), RAMP+ (linear up), RAMP- (linear down), SELECT (marquee box / point range), SOFT (proportional Gaussian neighborhood drag), BEND (simultaneous curvature bend) | `shell-mpc/overlay/lfo_ui.h:1320` |
| SHAPES | - | Tap | Opens SHAPES list popup (`Ed::ShapesRoot`: Factory shapes, My shapes, Save as, Replace, Rename, Delete) | `shell-mpc/overlay/lfo_ui.h:1468` |
| MORE | - | Tap | Opens MORE actions list popup (`Ed::ActionsRoot`: EDIT, GRID, COPY menus) | `shell-mpc/overlay/lfo_ui.h:1469` |
| UNDO | - | Tap | Reverts previous shape modification from the undo history stack | `shell-mpc/overlay/lfo_ui.h:1321` |
| DELETE | - | Tap | Deletes selected inner point(s) from the waveform (endpoints are protected) | `shell-mpc/overlay/lfo_ui.h:1336` |
| MOD | - | Tap | Opens `PointModPage` (`kPagePointMod = 44`) to modulate TIME, LEVEL, or CURVE of selected point(s) | `shell-mpc/overlay/lfo_ui.h:1470` |
| Canvas | `lfo1.phase` | Tap / Drag point / Double-tap / Drag marker | Directly edits waveform vertices, curvatures, rubber-band marquee, or start/loop phase marker | `shell-mpc/overlay/lfo_ui.h:1371` |
| POINT (Field) | - | Drag / Turn Q-Link / Wheel | Selects point index (1..N), or displays selection count ("N SEL") | `shell-mpc/overlay/lfo_ui.h:1451` |
| X | - | Drag / Turn Q-Link / Wheel | Adjusts time position (0.00..1.00) of selected point or center of selection | `shell-mpc/overlay/lfo_ui.h:1392` |
| Y | - | Drag / Turn Q-Link / Wheel | Adjusts vertical level (0.00..1.00) of selected point or center of selection | `shell-mpc/overlay/lfo_ui.h:1392` |
| CURVE / SCALE | - | Drag / Turn Q-Link / Wheel | Adjusts power curvature (-20..+20) of segment, or scales selection height (0.01..1.00) when multi-selected | `shell-mpc/overlay/lfo_ui.h:1414` |
| GRID X | - | Drag / Turn Q-Link / Wheel | Sets horizontal cycle grid divisions (2..64) | `shell-mpc/overlay/lfo_ui.h:1442` |
| GRID Y | - | Drag / Turn Q-Link / Wheel | Sets vertical level grid divisions (2..64) | `shell-mpc/overlay/lfo_ui.h:1442` |
| SNAP | - | Tap / Turn Q-Link | Toggles snapping points to the active X/Y grid on/off | `shell-mpc/overlay/lfo_ui.h:1352` |
| MODE | `lfo1.sync_type` | Tap left/right / Turn Q-Link / Tap name | Sets trigger mode (0 Free-run, 1 Retrigger, 2 Envelope, 3 Sustain envelope, 4 Loop at point, 5 Loop and hold) | `shell-mpc/overlay/lfo_ui.h:1455` |
| PHASE | `lfo1.phase` | Drag / Turn Q-Link / Wheel | Adjusts cycle start phase angle or loop point position (0..360 degrees) | `shell-mpc/overlay/lfo_ui.h:1461` |
| BEND ALL | - | Drag / Turn Q-Link / Wheel | Uniformly bows or bends all curved segments across the entire waveform | `shell-mpc/overlay/lfo_ui.h:1431` |
| LOOP | - | Tap | Sets or clears the selected point as the cycle loop position | `shell-mpc/overlay/lfo_ui.h:1353` |
| SMOOTH | - | Tap | Toggles smooth S-curve interpolation for all segments of the custom shape | `shell-mpc/overlay/lfo_ui.h:1363` |

### 2. Point Modulation Page (`PointModPage`, `kPagePointMod = 44`)
- Opened by: Tapping `MOD` (`editor_.modButton`) on `ShapeEditorPage` when 1 or more points are selected (`shell-mpc/overlay/lfo_ui.h:1470`).
- Purpose: Assigns any synth modulation source to dynamically modulate the TIME, LEVEL, or CURVE of individual vertices in the custom LFO shape.
- Controls on `PointModPage`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| BACK | - | Tap | Returns to `ShapeEditorPage` | `shell-mpc/overlay/lfo_ui.h:2020` |
| MODULATE | - | Tap left/right / Turn Q-Link 1 | Selects point property to modulate: TIME (X), LEVEL (Y), or CURVE (curvature) | `shell-mpc/overlay/lfo_ui.h:2021` |
| AMOUNT | - | Drag / Turn Q-Link 2 / Wheel | Sets modulation depth (-100% to +100%) for chosen property across selected points | `shell-mpc/overlay/lfo_ui.h:2022` |
| REMOVE | - | Tap | Removes modulation routing from chosen property for selected points | `shell-mpc/overlay/lfo_ui.h:2023` |
| ModSourceBoard | - | Tap source chip | Directly assigns modulation source (Envelopes, other LFOs, Macros, MIDI Velocity/Aftertouch, etc.) | `shell-mpc/overlay/lfo_ui.h:2024` |

### 3. Modulation Matrix Targets Page (`ModTargetsPage`, `kPageTargets = 41`)
- Opened by: Tapping `TARGETS` (full_.targets) on the full-screen LFO page (`shell-mpc/overlay/lfo_ui.h:2107`).
- Purpose: Displays and manages all modulation matrix slots where this LFO is the source.
- Controls on `ModTargetsPage`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| BACK | - | Tap | Returns to LFO page | `shell-mpc/overlay/lfo_ui.h:2105` |
| Page Indicator | - | Tap left/right / Turn | Pages through target entries (up to 6 rows per page) | `shell-mpc/overlay/lfo_ui.h:2189` |
| ADD TARGET | - | Tap | Opens destination selection list popup (`ListKind::Dest`) | `shell-mpc/overlay/lfo_ui.h:2108` |
| Row Depth | - | Drag / Turn Q-Link | Sets modulation depth (-100% to +100%) for that routing slot | `shell-mpc/overlay/lfo_ui.h:2234` |
| Row Polarity (+/-) | - | Tap / Turn Q-Link | Toggles unipolar (0..+1) vs bipolar (-1..+1) modulation mode | `shell-mpc/overlay/lfo_ui.h:2235` |
| Row Bypass (ON/OFF) | - | Tap / Turn Q-Link | Toggles whether that modulation slot is active or bypassed | `shell-mpc/overlay/lfo_ui.h:2236` |
| Row Remove (X) | - | Tap | Deletes the modulation connection and frees the matrix slot | `shell-mpc/overlay/lfo_ui.h:2124` |

### 4. Control Modulations Detail Page (`ModulationsPage`, `kPageMods = 43`)
- Opened by: Long-pressing on any knob or number field on the LFO page (`shell-mpc/overlay/lfo_ui.h:2148-2155`).
- Purpose: Inspects, adds, or adjusts all modulation sources targeting that specific parameter.
- Controls on `ModulationsPage`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| BACK | - | Tap | Returns to LFO page | `shell-mpc/overlay/lfo_ui.h:2105` |
| Source Stepper | - | Tap left/right / Turn | Selects modulation source to add (e.g. Env 1..6, other LFOs, Macros) | `shell-mpc/overlay/lfo_ui.h:2195` |
| ADD | - | Tap | Adds modulation from chosen source to this parameter | `shell-mpc/overlay/lfo_ui.h:2156` |
| ModSourceBoard | - | Tap chip | Directly adds modulation from tapped source chip (shown when 0 modulations exist) | `shell-mpc/overlay/lfo_ui.h:770, 2156` |
| Row Depth | - | Drag / Turn Q-Link | Sets modulation amount (-100% to +100%) | `shell-mpc/overlay/lfo_ui.h:2234` |
| Row Polarity | - | Tap / Turn Q-Link | Toggles unipolar vs bipolar modulation | `shell-mpc/overlay/lfo_ui.h:2235` |
| Row Bypass | - | Tap / Turn Q-Link | Toggles whether modulation is active | `shell-mpc/overlay/lfo_ui.h:2236` |
| Row Remove (X) | - | Tap | Deletes modulation from that source | `shell-mpc/overlay/lfo_ui.h:2136` |

### 5. Selection List Popup (`ListPopup`, `kPageList = 42` or `instPopup_`)
- Opened by:
  * Tapping instance carousel in compact view (`shell-mpc/overlay/lfo_ui.h:664, 2116`) -> lists LFO 1..10 and RND 1..4.
  * Tapping caption of choice steppers (`kShape`, `kStyle`, `kSync`, `kMode`, `kDirection`, `kDivision`; `shell-mpc/overlay/lfo_ui.h:2117-2121`).
  * Tapping `ADD TARGET` on `ModTargetsPage` (`shell-mpc/overlay/lfo_ui.h:2108`) -> lists destination categories, then parameters.
  * Tapping `SHAPES` on `ShapeEditorPage` (`shell-mpc/overlay/lfo_ui.h:1515, 1547`) -> lists Factory Shapes, My Shapes, Save As, Replace, Rename, Delete.
  * Tapping `MORE` on `ShapeEditorPage` (`shell-mpc/overlay/lfo_ui.h:1515, 1571`) -> lists EDIT, GRID, COPY action submenus.
  * Tapping `COPY TO LFO ...` in MORE -> EDIT (`shell-mpc/overlay/lfo_ui.h:1586`) -> lists destination LFOs.
  * Confirmation dialogs for overwriting or deleting custom shapes (`shell-mpc/overlay/lfo_ui.h:1623`).
- Controls on `ListPopup`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| Backdrop | - | Tap outside popup box | Dismisses popup without changing value | `shell-mpc/overlay/lfo_ui.h:2096, 2106` |
| Header | - | Tap header bar | Navigates up one menu level (when back arrow is displayed) | `shell-mpc/overlay/lfo_ui.h:2109` |
| List Row | - | Tap row / Turn Q-Link / Wheel | Selects tapped option or action item and closes popup | `shell-mpc/overlay/lfo_ui.h:2091, 2161, 2241` |

### 6. On-Screen Keyboard (`TextEntry`, `kPageKeyboard = 45`)
- Opened by: Selecting "SAVE AS ..." or "RENAME A SAVED SHAPE ..." in the SHAPES popup menu (`shell-mpc/overlay/lfo_ui.h:1634, 1707, 1745`).
- Purpose: Enters alphanumeric names for saving custom LFO shapes to storage (`SynthyBoy/LFO Shapes/`).
- Controls on `TextEntry`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| Keyboard Keys | - | Tap key | Types letters (A-Z), numbers (0-9), space, symbols, or deletes (backspace) | `shell-mpc/ui/text_entry.h:46` |
| OK | - | Tap | Confirms typed name and executes save/rename operation | `shell-mpc/overlay/lfo_ui.h:2113, 1651` |
| CANCEL | - | Tap | Dismisses keyboard without saving | `shell-mpc/overlay/lfo_ui.h:2114` |

## Q-Link groups

The hardware Q-Link knobs (4 knobs arranged vertically on Key 61 / One, or 16 knobs arranged in a 4x4 matrix on MPC X / XL) map to controls published through `QSlots::publish` (`shell-mpc/overlay/lfo_ui.h:2336-2422`).

### 1. Full Screen: Standard LFO Mode (`shell-mpc/ui/lfo_screen.h:315-328`)
The full-screen view publishes up to 5 groups in reading order (active group on Q1..Q4, next groups on Q5..Q16 on 16-knob devices):
- Group "Rate" (Block 0; frameless):
  * Q1: `picker` -> `lfo1.shape`
  * Q2: `rate` -> `lfo1.rate` in Hz mode, `division` -> `lfo1.division` in BPM mode (unassigned when Key sync active)
  * Q3: `rise` -> `lfo1.fade_in`
  * Q4: `smoothTime` -> `lfo1.smooth_time`
- Group "Key" (Block 0; only present when Key follow sync is active):
  * Q1: `keyTranspose` -> `lfo1.keytrack_transpose`
  * Q2: `keyTune` -> `lfo1.keytrack_tune`
  * Q3: unassigned
  * Q4: unassigned
- Group "Timing" (Block 1):
  * Q1: `syncSwitch.mode` -> `lfo1.sync` (knob turn steps selection: Free, Tempo, dotted, triplet, Key follow)
  * Q2: `phase` -> `lfo1.phase`
  * Q3: `delay` -> `lfo1.delay`
  * Q4: `mode` -> `lfo1.sync_type`
- Group "Motion" (Block 2):
  * Q1: `direction` -> `lfo1.direction`
  * Q2: `swing` -> `lfo1.swing`
  * Q3: `stereo` -> `lfo1.stereo`
  * Q4: `monoQ` -> `lfo1.mono`
- Group "Shape" (frameless, controls under picker / knob labels):
  * Q1: `hostPick` -> `lfo1.host`
  * Q2: `smoothSwitch` -> `lfo1.smooth`
  * Q3: unassigned
  * Q4: unassigned

### 2. Full Screen: Random LFO Mode (`shell-mpc/ui/lfo_screen.h:329-338`)
- Group "Random" (Block 0; frameless):
  * Q1: `picker` -> `rlfo1.style`
  * Q2: `rate` -> `rlfo1.rate` in Hz mode, `division` -> `rlfo1.division` in BPM mode (unassigned when Key sync active)
  * Q3: `syncSwitch.mode` -> `rlfo1.sync`
  * Q4: `stereoToggle` -> `rlfo1.stereo`
- Group "Key" (Block 0; only present when Key follow sync is active):
  * Q1: `keyTranspose` -> `rlfo1.keytrack_transpose`
  * Q2: `keyTune` -> `rlfo1.keytrack_tune`
- Group "Trigger" (Block 0; frameless):
  * Q1: `modeList` -> `rlfo1.sync_type`
  * Q2..Q4: unassigned

### 3. Compact View (Play Page; `shell-mpc/ui/lfo_screen.h:43-56`, `shell-mpc/overlay/lfo_ui.h:2318-2320`)
- Standard LFO ("Rate" group):
  * Q1: `shape` -> `lfo1.shape`
  * Q2: `rate` -> `lfo1.rate` (or `lfo1.division` in BPM mode)
  * Q3: `rise` -> `lfo1.fade_in`
  * Q4: `smoothTime` -> `lfo1.smooth_time`
- Random LFO ("Random" group):
  * Q1: `style` -> `rlfo1.style`
  * Q2: `rate` -> `rlfo1.rate` (or `rlfo1.division` in BPM mode)
  * Q3: `sync` -> `rlfo1.sync`
  * Q4: `stereo` -> `rlfo1.stereo`

### 4. Shape Editor (`ShapeEditorPage`; `shell-mpc/ui/shape_editor.h:797-800`, `shell-mpc/overlay/lfo_ui.h:2346-2371`)
Uses virtual Q targets handled via `takeQVirtual` / `setQVirtual` (`shell-mpc/overlay/lfo_ui.h:1200-1285`):
- Group 0 "Edit":
  * Q1: Virtual target 0 -> POINT index (`fpoint`)
  * Q2: Virtual target 1 -> X time position (`fx`)
  * Q3: Virtual target 2 -> Y level (`fy`)
  * Q4: Virtual target 3 -> CURVE power or SCALE height (`fcurve`)
- Group 1 "Grid":
  * Q1: Virtual target 4 -> GRID X divisions (`fgx`)
  * Q2: Virtual target 5 -> GRID Y divisions (`fgy`)
  * Q3: Virtual target 6 -> SNAP toggle (`snap`)
  * Q4: unassigned
- Group 2 "Trigger":
  * Q1: Table parameter -> `lfo1.sync_type` (`mode`)
  * Q2: Table parameter -> `lfo1.phase` (`fphase`)
  * Q3: Virtual target 7 -> BEND ALL (`fbend`)
  * Q4: unassigned

### 5. Point Modulation Page (`PointModPage`; `shell-mpc/ui/shape_editor.h:1016-1018`, `shell-mpc/overlay/lfo_ui.h:2372-2384`)
- Group "Modulation":
  * Q1: Virtual target 8 (`kVirtPmTarget`) -> MODULATE target property (TIME / LEVEL / CURVE; `pointMod_.target`)
  * Q2: Virtual target 9 (`kVirtPmAmount`) -> AMOUNT (-100%..+100%; `pointMod_.amount`)
  * Q3..Q4: unassigned

### 6. Targets Page & Modulations Page (`shell-mpc/overlay/lfo_ui.h:2390-2401`)
Each visible routing row maps across 4 Q-Link slots:
- Q1: Depth amount
- Q2: Polarity (+/-)
- Q3: Bypass (ON/OFF)
- Q4: Remove (X)

## Live parts

The LFO page incorporates several continuous real-time animations rendered directly from audio engine state (`shell-mpc/overlay/lfo_ui.h:261-271, 985-1086`):

1. Real-time Voice Phase Cursors (`lfodisplay::drawLive`):
   - In standard LFO mode (`rnd_ == 0`), `synthyboy::LfoDisplay::cursorX(live->lfoVoicePhase[lfoNum_ - 1][i])` computes the normalized playback phase for each actively sounding polyphonic voice (`shell-mpc/overlay/lfo_ui.h:1034-1053`).
   - If `lfo1.mono` is enabled (>= 0.5f), exactly one cursor is rendered representing the single shared oscillator phase (`shell-mpc/overlay/lfo_ui.h:1037`).
   - If polyphonic, up to 16 individual circular cursors travel simultaneously along the waveform path as notes are played.
   - The first voice cursor's vertical position is pinned directly to the audio engine's live output value `live->source[src - 1]` (`shell-mpc/overlay/lfo_ui.h:1050-1053`).

2. Continuous Oscilloscope Output Trace (`shell-mpc/overlay/lfo_ui.h:1058-1074`):
   - In Random LFO mode (`rnd_ != 0`), an internal ring buffer `trace_` holding 192 samples (`kTracePoints`) updates at approximately 30 Hz (`kTraceMs = 33.0` ms).
   - Each tick samples the live audio engine generator output (`live->source[14 + rnd - 1]`), shifting history left to produce a continuously scrolling oscilloscope trace of the random output waveform.

3. Reactive Waveform Recomputation (`shell-mpc/overlay/lfo_ui.h:1003-1016`):
   - When any parameter of the active LFO (`lfo1.shape`, `lfo1.swing`, `lfo1.direction`, `lfo1.phase`, `lfo1.sync_type`, `lfo1.smooth`), the user content state (`content_`), or the host tempo (`bpm_`) changes, `synthyboy::LfoDisplay::compute` recalculates the 256-point visual curve buffer `curve_`.
   - Redraws throttle at 30 ms (`nowMs - curveAtMs_ >= 30.0`). The `displayHot_` flag ensures continuous anti-aliasing updates during live manipulation.

## Duplicates

The following parameters of the LFO page also appear on other overlay views and host panels:

- `lfo1.rate`:
  * Play page compact third (`shell-mpc/overlay/lfo_ui.h:487`).
  * Play page host parameter "Play LFO Rate" (`shell-mpc/play_params.h:43`).
- `lfo1.division`:
  * Play page compact third (`shell-mpc/overlay/lfo_ui.h:488`).
  * Play page host parameter "Play LFO Division" (`shell-mpc/play_params.h:44`).
- `lfo1.fade_in`:
  * Play page compact third (`shell-mpc/overlay/lfo_ui.h:490`).
  * Play page host parameter "Play LFO Rise" (`shell-mpc/play_params.h:43`).
- `lfo1.delay`:
  * Play page host parameter "Play LFO Delay" (`shell-mpc/play_params.h:43`).
- `lfo1.smooth_time`:
  * Play page compact third (`shell-mpc/overlay/lfo_ui.h:491`).
  * Play page host parameter "Play LFO Smooth" (`shell-mpc/play_params.h:43`).
- `lfo1.smooth`:
  * Play page compact third knob label LED toggle (`shell-mpc/ui/lfo_screen.h:38`, `shell-mpc/overlay/lfo_ui.h:2286`).
  * Play page host parameter "Play LFO Smooth On" (`shell-mpc/play_params.h:44`).
- `lfo1.sync`:
  * Play page compact third `RateControl` inline mode cells (`shell-mpc/overlay/lfo_ui.h:489`).
  * Play page host parameter "Play LFO Sync" (`shell-mpc/play_params.h:44`).
- `lfo1.sync_type`:
  * Play page host parameter "Play LFO Trigger" (`shell-mpc/play_params.h:44`).
  * `ShapeEditorPage` trigger block `mode` stepper (`shell-mpc/ui/shape_editor.h:765`, `shell-mpc/overlay/lfo_ui.h:1458`).
- `lfo1.phase`:
  * `ShapeEditorPage` trigger block `fphase` number field (`shell-mpc/ui/shape_editor.h:764`, `shell-mpc/overlay/lfo_ui.h:1464`).
  * `ShapeEditorPage` interactive canvas start/loop marker (`shell-mpc/ui/shape_editor.h:1013`, `shell-mpc/overlay/lfo_ui.h:1383`).
- `lfo1.shape`:
  * Play page compact third `shape` stepper (`shell-mpc/ui/lfo_screen.h:25`, `shell-mpc/overlay/lfo_ui.h:485`).
- `rlfo1.style`:
  * Play page compact third `style` stepper (`shell-mpc/ui/lfo_screen.h:25`, `shell-mpc/overlay/lfo_ui.h:486`).
- `rlfo1.rate`:
  * Play page compact third `rate` knob (`shell-mpc/overlay/lfo_ui.h:487`).
- `rlfo1.division`:
  * Play page compact third `division` stepper (`shell-mpc/overlay/lfo_ui.h:488`).
- `rlfo1.sync`:
  * Play page compact third `sync` stepper (`shell-mpc/ui/lfo_screen.h:25`, `shell-mpc/overlay/lfo_ui.h:492`).
- `rlfo1.stereo`:
  * Play page compact third `stereo` toggle (`shell-mpc/ui/lfo_screen.h:28`, `shell-mpc/overlay/lfo_ui.h:493`).
- Modulation matrix sources:
  * `lfo1` through `lfo10` and `rlfo1` through `rlfo4` are selectable sources in the modulation matrix across the entire synth (`shell-mpc/overlay/mod_ui.h`, `shell-mpc/overlay/ui_bind.h:118`).

## Questionable

1. Dead `monoTop` toggle in `LfoFullScreen` (`shell-mpc/ui/lfo_screen.h:135, 177, 192, 357`, `shell-mpc/overlay/lfo_ui.h:501`):
   - In `LfoFullScreen`'s constructor, `monoTop` is instantiated as a `ToggleIcon` with label "MONO".
   - In `configure()`, it is unconditionally hidden via `show (monoTop, false)` (`shell-mpc/ui/lfo_screen.h:192`).
   - In `layout()`, it is unplaced via `monoTop.place ({})` (`shell-mpc/ui/lfo_screen.h:357`).
   - In `LfoUi::configure()`, it is bound to `T::Inert` with an empty parameter suffix (`shell-mpc/overlay/lfo_ui.h:501`).
   - Meanwhile, `monoQ` in the "MOTION" block (`shell-mpc/ui/lfo_screen.h:152, 435`) is the real control bound to `lfo1.mono` (`shell-mpc/overlay/lfo_ui.h:519`). `monoTop` is obsolete leftover code that should be deleted.

2. Unused `style` stepper in `LfoFullScreen` (`shell-mpc/ui/lfo_screen.h:142, 179, 193`, `shell-mpc/overlay/lfo_ui.h:507`):
   - `LfoFullScreen` keeps a `Stepper style` member that is bound to `rlfo1.style` (`shell-mpc/overlay/lfo_ui.h:507`).
   - In `configure()`, the code explicitly states: `show (style, false); // the style grid IS the style control (a stepper beside it was the same control twice)` (`shell-mpc/ui/lfo_screen.h:193`).
   - The member, its child registration, and its binding are dead code.

3. Dead `shape` and `sync` steppers in `LfoFullScreen` (`shell-mpc/ui/lfo_screen.h:142, 146, 180, 196, 358`):
   - `shape` is unconditionally hidden (`show (shape, false);` at `shell-mpc/ui/lfo_screen.h:196`) because the `picker` grid handles shape selection.
   - `sync` is also unconditionally hidden (`show (sync, false);` at `shell-mpc/ui/lfo_screen.h:196` and `sync.place ({}); // gone from the page` at `shell-mpc/ui/lfo_screen.h:358`) because `syncSwitch.mode` in the TIMING block handles sync mode.
   - Both members are redundant and never shown.

4. Triple redundancy for trigger mode and phase/loop point:
   - On the full-screen page, `mode` (`shell-mpc/ui/lfo_screen.h:146`) controls `lfo1.sync_type` (`shell-mpc/overlay/lfo_ui.h:512`). Inside `ShapeEditorPage`, `mode` (`shell-mpc/ui/shape_editor.h:765`) ALSO controls `lfo1.sync_type` (`shell-mpc/overlay/lfo_ui.h:1458`).
   - On the full-screen page, `phase` (`shell-mpc/ui/lfo_screen.h:148`) controls `lfo1.phase` (`shell-mpc/overlay/lfo_ui.h:514`). Inside `ShapeEditorPage`, `fphase` (`shell-mpc/ui/shape_editor.h:764`) AND the canvas draggable marker (`canvas.state.markerX`, `shell-mpc/ui/shape_editor.h:112`, `shell-mpc/overlay/lfo_ui.h:1383`) both control `lfo1.phase`.
   - In loop modes, the loop point and phase are interlocked (`phaseBound_`, `shell-mpc/overlay/lfo_ui.h:1181-1183, 1187-1194`), creating three separate interactive controls for the same underlying parameter.

5. Phantom Q-Link proxy and disjoint UX for `SMOOTH` switch:
   - Waveform smoothing is controlled by two parameters: `lfo1.smooth` (boolean switch) and `lfo1.smooth_time` (knob, 1..1000 ms).
   - On the touchscreen, the switch is an LED drawn on the label of the knob (`smoothTime.state.led = true`, `shell-mpc/ui/lfo_screen.h:164`). Tapping the knob label toggles the switch (`shell-mpc/overlay/lfo_ui.h:2286-2287`).
   - Because hardware Q-Link knobs cannot tap labels, a phantom `LedSwitchProxy` (`smoothSwitch`, `shell-mpc/ui/lfo_screen.h:115-125, 139`) is placed beside the knob and assigned to a completely separate frameless Q-Link group ("Shape"; `shell-mpc/ui/lfo_screen.h:326`).
   - Users adjusting the "Rate" Q-Link group cannot toggle smoothing without switching Q-Link groups to "Shape", which is unintuitive.

6. Isolated placement of `HOST` sync toggle (`shell-mpc/ui/lfo_screen.h:372`, `shell-mpc/overlay/lfo_ui.h:505`):
   - `HOST` (`lfo1.host`) is placed directly underneath the shape picker grid (x = 968, y = 366, w = 304, h = 56).
   - This isolates it from the RATE and TIMING blocks where HZ/BPM/KEY tempo options live, making its relationship to tempo synchronization confusing. It is also placed in the frameless "Shape" Q-Link group alongside `smoothSwitch`.

7. Deep nesting for Point Modulation (`PointModPage`):
   - Applying modulation to a point on a custom shape requires navigating 3 levels deep: Full Screen -> `EDIT` -> `MOD` -> `PointModPage` (`shell-mpc/overlay/lfo_ui.h:1470`).
   - Modifying point modulation during live performance is difficult due to this hierarchy depth.
