# Envelopes, LFOs, random LFOs – mobile UI design

Scope: 6 envelopes, 8 LFOs, 4 random LFOs, shape drawing / editing, modes, grid / snap, retrigger, rate / sync, smooth, delay / fade, LFO point modulation, velocity / note curves. The modulation MATRIX, assign gestures (drag a tab to a control), depth halo, bipolar / stereo, macros are in 06: this file only references them.
Cites: `[S p.n]` Serum 2 manual, `[V p.n]` Vital unofficial manual. Key families: `envN` = env1..env6, `lfoN` = lfo1..lfo8, `rlfoN` = rlfo1..rlfo4.
Status: `HAVE` = engine and params support it; `GAP` = missing, UI still designed here. Counts differ from Serum 2: Serum has 4 envelopes and 10 LFOs [S p.183, p.188]; we have 6 + 8 + 4.

## 1. Function inventory

### 1.1 Envelopes

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Envelope tabs / selection, assigned-destination count badge [S p.183-184] [V p.17] | both | `env1.*` .. `env6.*` | HAVE (6; Serum shows 4) |
| ENV 1 is the amplifier envelope (always active; mapped to oscillator volume by default) [S p.185] [V p.17] | both | `env1.*` | HAVE |
| ENV 1 shown faintly behind ENV 2..n [S p.185] | Serum | none | HAVE (UI) |
| Delay (time before the envelope starts) [V p.17] | Vital | `envN.delay` (0..4000 ms) | HAVE |
| Attack [S p.186] [V p.18] | both | `envN.attack` (0..10000 ms) | HAVE |
| Hold [S p.186] [V p.18] | both | `envN.hold` (0..4000 ms) | HAVE |
| Decay [S p.186] [V p.18] | both | `envN.decay` (0..10000 ms) | HAVE |
| Sustain [S p.186] [V p.18] | both | `envN.sustain` (0..100 %) | HAVE |
| Release [S p.186] [V p.18] | both | `envN.release` (0..10000 ms) | HAVE |
| Attack curve, decay curve, release curve (drag the curve points; graph only in Serum) [S p.186] [V p.17] | both | `envN.attack_curve`, `envN.decay_curve`, `envN.release_curve` (-20..20, not modulatable) | HAVE |
| Direct graph editing: closed dots = segment end points, open dots = curves [S p.186] [V p.17] | both | all `envN.*` above | HAVE (UI) |
| Envelope times are modulation destinations | both | `envN.delay` .. `envN.release`, `envN.sustain` (modulatable) | HAVE |
| Zoom lock (auto-normalise) and manual zoom slider / magnifier [S p.184-185] [V p.17] | both | none | HAVE (UI) |
| Units MS or BPM (times as note divisions of host tempo) [S p.185] | Serum | none | GAP |
| Vertical grid at time (ms) or beat intervals [S p.185] | Serum | none | HAVE (time); GAP (beats, needs BPM units) |
| Legato Inverted: envelope always retriggers on note-on even with Legato [S p.185] | Serum | none (`master.legato` is global) | GAP |
| Assign envelope to a control, bypass / remove assignments [S p.187] | Serum | `mod01.source` .. `mod64.bypass` (source `env1` .. `env6`) | see 06 |
| Envelope mapped to oscillator / sampler volume cannot be unmapped without another source [V p.17] | Vital | none | see 06 |

### 1.2 LFOs

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| LFO tiles 1..N; Serum shows LFO 7-10 after LFO 6 is used [S p.188] | Serum | `lfo1.*` .. `lfo8.*` | HAVE (8); GAP (LFO 9, 10) |
| Copy all settings of one LFO to another by dragging the title [S p.189] | Serum | all `lfoN.*` | HAVE (UI) |
| Shape chooser with built-in shapes (Sine, Triangle, Saw Up, Saw Down, Square, Pulse 25%, Steps Up, Steps Down, Random Steps, Exp Up, Exp Down, Half Sine) [S p.191] [V p.19] | both | `lfoN.shape` | HAVE |
| Free-form shape: add / move / delete points, curves [S p.189-190] [V p.19] | both | none (PARAMETERS "Not exposed": no free-form shapes) | GAP |
| Drawing tools: Point, Flat, Ramp Up, Ramp Down [S p.189] | Serum | none | GAP |
| Brush tool with selectable brush shape, Ctrl toggles the brush [V p.19] | Vital | none | GAP |
| Double-click add / remove point; Shift-click draws steps at grid size (step sequencer) [S p.190] | Serum | none | GAP |
| Alt-drag snaps a point to the grid; Alt-drag a curve point moves all curves [S p.190] [V p.19 Shift for all curves] | both | none | GAP |
| Marquee multi-select; Cmd-drag proportional (rainbow) move [S p.190] | Serum | none | GAP |
| Context menu: segment shape for Shift-click, remove selected points, assign start / loopback point [S p.190] | Serum | none | GAP |
| Smooth-all-angles button on the shape [V p.19] | Vital | none | GAP |
| Grid: horizontal and vertical divisions, double-click to type a number [S p.192] [V p.19] | both | none (drawing aid) | GAP |
| LFO Editor on a large canvas [S p.193] | Serum | none | GAP |
| Save LFO shape as user preset, load preset (overwrites graph) [S p.191] | Serum | none | GAP (built-in list HAVE) |
| Copy wavetable frame to an LFO shape (Wavetable A / B / C to LFO) [S p.197] | Serum | none | GAP (see 03) |
| Copy LFO shape to a wavetable (Alt-drag tab onto the wavetable view) [S p.198] | Serum | none | GAP (see 03) |
| LFO TYPE: Normal | Serum | `lfoN.*` | HAVE |
| LFO TYPE: Path [S p.191] | Serum | none | GAP `[S unclear]` (manual lists the type, does not describe it) |
| LFO TYPE: Chaos Lorenz [S p.191] | Serum | `rlfoN.style` = Lorenz attractor | HAVE |
| LFO TYPE: Chaos Rossler [S p.191] | Serum | none | GAP |
| LFO TYPE: S&H [S p.191] | Serum | `rlfoN.style` = Sample and hold | HAVE |
| MODE FREE (follows host clock, ignores notes) [S p.191] | Serum | `lfoN.sync_type` = Free-run | HAVE |
| MODE RETRIG (restart on note) [S p.191] [V p.19 Trigger] | both | `lfoN.sync_type` = Retrigger | HAVE |
| MODE ENVELOPE (one cycle then stops; loopback point) [S p.191] [V p.20] | both | `lfoN.sync_type` = Envelope, Loop at point | HAVE (loopback point position needs free-form shapes: GAP) |
| Vital modes Sustain Envelope, Loop Point, Loop Hold [V p.20] | Vital | `lfoN.sync_type` = Sustain envelope, Loop at point, Loop and hold | HAVE |
| MONO (one LFO for all voices) vs. polyphonic LFO [S p.191] | Serum | none | GAP |
| DIRECTION: Forward, Reverse, Ping Pong [S p.191] | Serum | none | GAP |
| HOST switch (phase anchored to host transport) [S p.192-193] | Serum | none | GAP |
| BPM / HZ time base [S p.192] | Serum | `lfoN.sync` = Free (Hz), Tempo | HAVE |
| TRIP / DOT time selection [S p.192] | Serum | `lfoN.sync` = Tempo dotted, Tempo triplet | HAVE |
| Vital frequency styles: Seconds, Tempo, Tempo Dotted, Tempo Triplets, Key track [V p.20] | Vital | `lfoN.sync` (5 choices) | HAVE |
| RATE in Hz (0.01..200) | both | `lfoN.rate` | HAVE |
| RATE as note division (Freeze, 32 bars .. 1/64) | both | `lfoN.division` | HAVE |
| RATE swing; 10x range multiplier [S p.192] | Serum | none | GAP |
| Key track: LFO frequency follows the note, offset in semitones / cents [V p.20] | Vital | `lfoN.keytrack_transpose` (-60..36 st), `lfoN.keytrack_tune` (-100..100 ct) | HAVE |
| RISE (fade-in of the LFO influence) [S p.192] / FADE IN [V p.20] | both | `lfoN.fade_in` (0..8000 ms) | HAVE |
| DELAY before rise [S p.193] [V p.20] | both | `lfoN.delay` (0..4000 ms) | HAVE |
| SMOOTH (cross-fades start and end / smooths jumps) [S p.193] [V p.20] | both | `lfoN.smooth` (on / off), `lfoN.smooth_time` (1..1000 ms) | HAVE |
| PHASE slider (start phase; in Envelope modes also stop / loop point) [S p.193] [V p.19-20] | both | `lfoN.phase` (0..360 deg) | HAVE |
| PHASE snap to grid (right-click) [S p.193] | Serum | none | GAP |
| STEREO phase offset left / right [V p.20] | Vital | `lfoN.stereo` (-180..180 deg) | HAVE |
| Playhead highlight on the shape while a note plays [V p.19] | Vital | none | HAVE (UI) |
| Modulate LFO points: X or Y per point via LFO bus menu (Bus 1..) [S p.198-199] | Serum | none | GAP |
| Modulate LFO points by dragging a source onto the point (X / Y buttons) [S p.200] | Serum | none | GAP |
| Assign LFO to a control, depth halo, negative depth, bypass / remove [S p.194-196] | Serum | `modNN.*` | see 06 |
| LFO tab context menu: bypass / remove destinations [S p.195] | Serum | `mod01.bypass` (slots 01..64) | see 06 |

### 1.3 Random LFOs (Vital) and Serum chaos / S&H types

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Random LFO 1..4 [V p.21] | Vital | `rlfo1.*` .. `rlfo4.*` | HAVE |
| Style: Perlin, Sample and hold, Sine interpolate, Lorenz attractor [V p.21-22] | Vital | `rlfoN.style` | HAVE |
| Frequency in Hz or tempo divisions, dotted, triplets, key track [V p.22] | Vital | `rlfoN.rate`, `rlfoN.sync`, `rlfoN.division` | HAVE |
| Sync option: continue from last value vs. restart on each note [V p.22] | Vital | `rlfoN.sync_type` (Retrigger, Free-run) | HAVE |
| Stereo option: separate left / right generators [V p.22] | Vital | `rlfoN.stereo` | HAVE |
| Key-follow offset (transpose, tune) [V p.22] | Vital | `rlfoN.keytrack_transpose`, `rlfoN.keytrack_tune` | HAVE |
| Randomness visual (modulation history as notes play) [V p.22] | Vital | none | HAVE (UI) |
| Serum Chaos Rossler type [S p.191] | Serum | none | GAP |

### 1.4 Velocity and note curves [S p.201-205]

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| VELO tab: draw a velocity -> value graph used as a modulation source [S p.202] | Serum | source `velocity` (linear only) | GAP (curve), source HAVE |
| NOTE tab: draw a note -> value graph used as a modulation source [S p.204] | Serum | source `note` (linear only) | GAP (curve), source HAVE |
| Graph editing: double-click add / remove point, drag point, drag curve point, Alt-drag all curves [S p.203] | Serum | none | GAP |
| Init Graph (straight diagonal) [S p.202, p.204] | Serum | none | GAP |
| Legato (Portamento Time) option on VELO and NOTE [S p.202, p.204] | Serum | `master.portamento.time` exists | GAP (option) |
| Assign VELO / NOTE tab to a control [S p.204-205] | Serum | `modNN.*` | see 06 |
| Per-slot remap curve in the matrix (Vital) | Vital | `mod01.power` only | see 06 (curve: GAP) |
| Other Vital sources Lift, Oct Note, Pressure, Slide, Stereo, Random [V p.22-23] | Vital | sources `random` (HAVE), `aftertouch` (HAVE), others not exposed | see 06 |

## 2. Screen design

### 2.1 ENV page, phone portrait

```
+------------------------------------------+
| [E1*][E2 2][E3][E4][E5][E6]   zoom [lock]|  tabs; badge = destination count (06)
+------------------------------------------+
|  +--------------------------------------+|
|  |  gray: ENV1 behind (when E2..E6)     ||
|  |   delay  attack hold decay  sustain release
|  |  ....o--/\o------o\_______o....\o       ||  closed dots = nodes (44 pt hit), open dots = curves
|  |                                      ||
|  | time ruler 0   200ms  400ms ...      ||
|  +--------------------------------------+|
| (ATK) (DEC) (SUS) (REL)                  |  4 main knobs
| DELAY | HOLD | CURVES | UNITS (GAP) | LEGATO INV (GAP) |  more row
+------------------------------------------+
```

* Nodes: N1 end of delay, N2 peak (end of attack), N3 end of hold, N4 end of decay at the sustain level, N5 end of release. Horizontal drag of N1 = `envN.delay`, N2 = `envN.attack`, N3 = `envN.hold`, N4 = `envN.decay` (x) and `envN.sustain` (y), N5 = `envN.release`; open dot on each segment = `envN.attack_curve`, `envN.decay_curve`, `envN.release_curve` (vertical drag).
* CURVES sheet: three knobs for the curves (same keys) with numeric values; DELAY and HOLD sheet knobs.
* Zoom lock (default on): the graph always fits the display, the ruler rescales [S p.184-185]; unlocked: pinch / the magnifier slider zooms the time axis.

### 2.2 LFO page, phone portrait

```
+------------------------------------------+
| [L1*][L2][L3]..[L8]  | [R1][R2][R3][R4]  |  scrolling tab bar; random LFOs after a divider
| Shape [Sine v]  [<][>]   Mode [RETRIG v] |  shape chooser (lfoN.shape), mode (lfoN.sync_type)
+------------------------------------------+
|  +--------------------------------------+|
|  |  shape display, grid, playhead,      ||  tools (GAP while shapes are built-in):
|  |  phase marker                         ||  [pt][flat][ramp+][ramp-][brush][steps][snap][smooth]
|  +--------------------------------------+|
|  PHASE  ======o=========  0 deg             |  lfoN.phase
|  RATE: [ 2.00 Hz | Tempo 1/8 | dotted | triplet | Key follow ]  |  lfoN.sync + rate/division
+------------------------------------------+
| (RATE) (FADE IN) (DELAY) (SMOOTH)         |  4 main knobs
| STEREO | KEY FOLLOW | DIRECTION | MONO | HOST | GRID  |  more row (last four GAP)
+------------------------------------------+
```

* The rate knob is context dependent: Free = `lfoN.rate`, Tempo / dotted / triplet = `lfoN.division` (stepped), Key follow = disabled and KEY FOLLOW sheet opens (`lfoN.keytrack_transpose`, `lfoN.keytrack_tune`).
* SMOOTH knob = `lfoN.smooth_time`; the small toggle above it = `lfoN.smooth` on / off [V p.20].
* Tools row is shown always; each tool is disabled with the badge "built-in shapes only" while the shape is a built-in. Choosing a tool on a built-in shape converts a copy to a free-form shape (GAP in engine; UI flow is final).

### 2.3 Random LFO page

```
+------------------------------------------+
| [L1]..[L8] | [R1*][R2][R3][R4]            |
| Style: (Perlin)(S&H)(Sine interp)(Lorenz)|  rlfoN.style as 4 chips
|  +--------------------------------------+|
|  | modulation history, scrolling         ||
|  +--------------------------------------+|
| (RATE) (STEREO) (SYNC) (KEY FOLLOW)       |  rate knob (Hz / division), toggles
| RATE MODE [Hz|Tempo|dotted|triplet|Key]   |
+------------------------------------------+
```

STEREO = `rlfoN.stereo`; SYNC chip = `rlfoN.sync_type` (Retrigger = restart each note, Free-run = continue [V p.22]).

### 2.4 Point editor (LFO shape, VELO / NOTE curves) – used by every free-form graph

```
+------------------------------------------+
| tools: (pt)(flat)(ramp+)(ramp-)(brush v)(steps) | snap [x] prop [ ] all-curves [ ] |
| +--------------------------------------+ |
| |  o----.----o                          | |  o = point (44 pt), . = curve handle
| |       \     \___.___o                 | |  marquee: long-press empty + drag
| |  grid H x V  [8][4]                   | |
| +--------------------------------------+ |
| selected point: [Delete][Loop here][Start here][Modulate X][Modulate Y][Segment shape v] |
+------------------------------------------+
```

Point modulation sheet (GAP): choose LFO bus (Bus 1 ..), pick a source chip (`env1`..`rlfo4`, `macro1`..), range bar drawn on the graph [S p.199-200].

### 2.5 Tablet / landscape / MPC

```
+--------------------------------------------------------------------+
| ENV tile: tabs + graph + 6 knobs      | LFO tile: tabs + graph + knobs |
| (E1..E6)                              | (L1..L8 | R1..R4)              |
+--------------------------------------------------------------------+
| curves sheet inline | delay hold inline | mode / rate / key follow inline |
+--------------------------------------------------------------------+
```

Phone landscape: ENV and LFO side by side, knobs under each graph.

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| Envelope node | Drag (horizontal = time, vertical on N4 = sustain) | `envN.delay` / `attack` / `hold` / `decay` / `sustain` / `release` with value bubble [S p.186] [V p.17] | Knobs, long-press sheet |
| Envelope curve dot | Vertical drag | `envN.attack_curve` / `decay_curve` / `release_curve` | CURVES sheet |
| Envelope graph | Pinch (unlocked) / magnifier drag [V p.17] | Time zoom | Lock toggle |
| Envelope tab | Tap; long-press | Select; context: bypass / remove destinations (06) [S p.187] | Menu |
| Envelope tab drag onto control | Drag (tablet) | Assign (06) | Tap source chip, then drag the target knob (06) |
| Shape chooser | Tap, `<` `>` | `lfoN.shape` [S p.191] | List |
| Mode chooser | Tap | `lfoN.sync_type` | Long-press |
| Rate area | Tap segment (Hz / Tempo / dotted / triplet / Key follow) | `lfoN.sync` [S p.192] [V p.20] | Menu |
| Rate knob | Drag | `lfoN.rate` or `lfoN.division` | Numeric |
| Phase slider | Drag | `lfoN.phase`; marker moves; in Envelope modes marks stop / loop point [V p.19-20] | Numeric |
| Fade in, Delay, Smooth, Stereo | Knob | `lfoN.fade_in`, `lfoN.delay`, `lfoN.smooth_time`, `lfoN.stereo` | Numeric |
| Smooth toggle | Tap | `lfoN.smooth` | Long-press |
| LFO tab drag onto another LFO tab | Drag (tablet) | Copy all settings [S p.189] | Module sheet: Copy / Paste |
| Shape display – Point tool | Double-tap on line | Add point; double-tap on point deletes [S p.190] | `+` / trash buttons in the selected-point bar |
| Shape display – Point tool | Drag point | Moves; snaps when Snap is on (replaces Alt) [S p.190] | Numeric x / y fields |
| Shape display – curve handle | Drag; "All curves" toggle moves all [S p.190] [V p.19] | Segment curve | Segment shape menu |
| Shape display – Steps tool | Drag | Draws steps at grid size (replaces Shift-click) [S p.190] | Grid + Flat tool |
| Shape display – Brush | Drag | Paints the chosen brush shape cell by cell [V p.19] | Flat / Ramp tools |
| Shape display – marquee | Long-press empty area, drag | Multi-select; "Proportional" toggle gives the rainbow falloff move [S p.190] | none |
| Selected point bar | Tap `Loop here` / `Start here` | Loopback / start point for Envelope modes [S p.190] | none |
| Selected point bar | Tap `Modulate X` / `Modulate Y` | Point modulation sheet (GAP) [S p.199] | Drop a source chip on the point |
| Grid fields | Tap | Number entry [S p.192] | Steppers |
| Random LFO style chips | Tap | `rlfoN.style` | List |
| Random LFO toggles | Tap | `rlfoN.stereo`, `rlfoN.sync_type` | Long-press |
| VELO / NOTE graph | Same gestures as the point editor; `Init graph` button [S p.202] | Curve | none |
| Undo / Redo | Per edit (one drag = one entry) | History in 01 | Menu |

## 4. States and edge cases

* LFO shape is built-in (12) until free-form shapes exist; all drawing tools are disabled and the display is read-only except for phase marker and playhead.
* `lfoN.sync` = Key follow: Rate and division disabled; the key-follow offset knobs are enabled. Any other mode: key-follow knobs dimmed. `lfoN.division` is only active for Tempo / dotted / triplet; `lfoN.rate` only for Free (Hz).
* Division value Freeze (index 0) stops the LFO; the display shows "frozen".
* Ranges: LFO rate 0.01..200 Hz (log), fade-in 0..8000 ms, delay 0..4000 ms, smooth time 1..1000 ms, stereo +-180 deg, key-follow transpose -60..+36 st; envelope times as in 1.1; defaults from `docs/params.json` (attack 5 ms, decay 400 ms, sustain 100 %, release 200 ms, curves 0 / -3 / -3).
* Envelope curves are not modulatable (no purple ring); times and sustain are.
* ENV 1 is the amplitude envelope: its tab is always marked "AMP" and cannot be disabled [S p.185].
* Mode semantics (design text shown in the mode list): Retrigger = restart on each note; Free-run = ignores notes; Envelope = one pass then stops (holds the last value); Sustain envelope = plays from start until the phase marker, then holds; Loop at point = loops from the phase marker to the end; Loop and hold = loops from the start to the phase marker [V p.19-20].
* Random LFO Lorenz and Perlin ignore Hz fine detail (frequencies are relative to each other, [V p.22]); the rate label shows "relative".
* Low quality tier: no change to LFO / envelope; the number of active modulations is unlimited (64 slots) but the matrix shows a warning when CPU is high (06).
* Phone: the display height keeps >= 180 pt; when the keyboard is large, the main knobs wrap into two rows of two.
* LFO point modulation (GAP) cannot target a built-in shape; the Modulate buttons are disabled.

## 5. Kick remote pages

Pages repeat per instance (default order and merging are decided in 09):

| Page | Params (<= 8) |
|------|---------------|
| Env N (N = 1..6) | `envN.delay`, `envN.attack`, `envN.hold`, `envN.decay`, `envN.sustain`, `envN.release`, `envN.attack_curve`, `envN.decay_curve` |
| Env N Curve 2 | `envN.release_curve` |
| LFO N (N = 1..8) | `lfoN.shape`, `lfoN.rate`, `lfoN.sync`, `lfoN.division`, `lfoN.sync_type`, `lfoN.phase`, `lfoN.fade_in`, `lfoN.delay` |
| LFO N Shaping | `lfoN.smooth`, `lfoN.smooth_time`, `lfoN.stereo`, `lfoN.keytrack_transpose`, `lfoN.keytrack_tune` |
| Random LFO N (N = 1..4) | `rlfoN.style`, `rlfoN.rate`, `rlfoN.sync`, `rlfoN.division`, `rlfoN.sync_type`, `rlfoN.stereo`, `rlfoN.keytrack_transpose`, `rlfoN.keytrack_tune` |

The one-key page "Env N Curve 2" is a candidate for merging (for example `env1.release_curve` .. `env6.release_curve` on one shared page) in 09.

## 6. Coverage check

| Key group (params.json) | Where in section 2 |
|-------------------------|--------------------|
| `envN.delay` | 2.1 DELAY sheet / node N1 |
| `envN.attack`, `envN.decay`, `envN.sustain`, `envN.release` | 2.1 main knobs and nodes |
| `envN.hold` | 2.1 HOLD sheet / node N3 |
| `envN.attack_curve`, `envN.decay_curve`, `envN.release_curve` | 2.1 curve dots, CURVES sheet |
| `lfoN.shape` | 2.2 shape chooser |
| `lfoN.rate`, `lfoN.sync`, `lfoN.division` | 2.2 rate area and rate knob |
| `lfoN.sync_type` | 2.2 mode chooser |
| `lfoN.phase` | 2.2 phase slider |
| `lfoN.fade_in`, `lfoN.delay` | 2.2 main knobs |
| `lfoN.smooth`, `lfoN.smooth_time` | 2.2 SMOOTH knob + toggle |
| `lfoN.stereo` | 2.2 more row |
| `lfoN.keytrack_transpose`, `lfoN.keytrack_tune` | 2.2 KEY FOLLOW sheet |
| `rlfoN.style` | 2.3 style chips |
| `rlfoN.rate`, `rlfoN.sync`, `rlfoN.division`, `rlfoN.sync_type` | 2.3 rate knob, mode, SYNC chip |
| `rlfoN.stereo` | 2.3 STEREO |
| `rlfoN.keytrack_transpose`, `rlfoN.keytrack_tune` | 2.3 KEY FOLLOW |
| `macro.1` .. `macro.4`, `modNN.*` | only referenced: 01, 06 |
| mod sources `velocity`, `note` | 1.4 curves (GAP), assignment in 06 |

## 7. Open questions (owner)

1. Free-form LFO shapes, point/curve editing, LFO point modulation, Velocity / Note curves, Rossler, Path, MONO, Direction, HOST, LFO 9-10 and beat-unit envelopes are all `GAP`: they need new core parameters / state fields (shape data is content, not a scalar parameter). Which of them are wanted, and is the shape stored in the preset state as a point list?
2. Serum LFO type "Path" is listed but not described in the manual `[S unclear]`; implement, or drop?
3. Do we keep all 6 envelopes and 8 + 4 LFOs (more than Serum's 4 / 10 in envelopes, fewer in LFOs)? The UI scales to the counts in `docs/params.json`.
4. Serum Clips / Arpeggiator are excluded (Kick); Serum "Send to clip" and the LFO tempo grid rely on the host tempo only.
