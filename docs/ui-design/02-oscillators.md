# Oscillators, sub, noise and mixer – mobile UI design

Scope: oscillators 1-3 in every type, noise, sub, mixer / main output. The wavetable VIEW internals and the editor are in 03; filters in 04.
Cites: `[S p.n]` Serum 2 manual, `[V p.n]` Vital unofficial manual. Key families: `oscN` = osc1..osc3, `modNN` = mod01..mod64.
Status: `HAVE` = in engine and `docs/params.json`; `GAP` = missing in engine / params, UI still designed here. Serum names OSC A/B/C = our Osc 1/2/3.

## 1. Function inventory

### 1.1 Oscillator module (all types)

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Power / enable, panel dims when off [S p.28] [V p.6] | both | `oscN.on` | HAVE |
| Oscillator type menu: Wavetable, Multisample, Sample, Granular, Spectral [S p.17, p.29] | Serum | none | Wavetable HAVE; the other four GAP |
| Choose wavetable: menu, < > arrows, wheel [S p.40] [V p.7] | both | `oscN.wavetable` (13 built-in tables only) | HAVE (built-ins); GAP (user / ContentPack wavetables: no content-id parameter) |
| Wavetable display 2D / 3D toggle (tap), default view preference [S p.39, p.314] | Serum | none | HAVE (UI), see 03 |
| Spectral view (SP) of the wavetable [V p.7-8] | Vital | none | HAVE (UI), see 03 |
| Open wavetable editor [S p.40] [V p.8] | both | none | see 03 |
| Embed in preset button [S p.22, p.303] | Serum | none | HAVE (wavetable); see 01 |
| Pitch: OCT + SEM (octave, semitone) [S p.29] | Serum | `oscN.transpose` (-48..48, step 1; UI shows OCT = transpose div 12, SEM = rest) | HAVE |
| Pitch: FIN (fine, cents) [S p.29] [V p.6] | both | `oscN.tune` (-100..100 ct) | HAVE |
| Pitch: CRS (continuous coarse, modulation-friendly) [S p.29-30] | Serum | none (`oscN.transpose` is stepped) | GAP |
| Pitch mode: Semitones | Serum | `oscN.transpose` | HAVE |
| Pitch mode: Harmonics [S p.30] | Serum | none | GAP |
| Pitch mode: Ratio with SRC oscillator [S p.30] | Serum | none | GAP |
| Pitch mode: Step (MTS-ESP) [S p.30] | Serum | none | GAP |
| Global main tuning destination [S p.30] | Serum | `master.tune` (modulatable) | HAVE |
| Transpose snap keyboard (limit pitch knob to chosen semitones) [V p.6-7] | Vital | none (PARAMETERS: "transpose quantize" not exposed) | GAP |
| Global snap (force notes into a scale) [V p.7] | Vital | none | GAP (see 01 KEY / SCALE) |
| Enable Pitch Tracking toggle; unpitched oscillators play C3 (sample types) / C-2 (wavetable) [S p.35] [V p.40 "Note tracking"] | both | none (PARAMETERS: "MIDI tracking" not exposed) | GAP |
| Pitch Bend Tracking when pitch tracking is off [S p.36] | Serum | none | GAP |
| Routing: Filter 1 / Filter 2 / both / effects only / direct out [V p.7] | both | `oscN.destination` | HAVE |
| Routing: Filter knob blends smoothly between Filter 1 and 2 [S p.31] | Serum | none (`destination` is discrete) | GAP |
| Routing: Main (through FX), Direct (bypass filters and FX), None (modulation source only) [S p.31] | Serum | `oscN.destination` (Effects only = Main, Direct out = Direct) | HAVE (Main, Direct); GAP (None) |
| Routing: send to Bus 1 / Bus 2 [S p.32] | Serum | none | GAP |
| Oscillator output as modulation source (drag module label to a control) [S p.208] | Serum | none (mod source list has no oscillator) | GAP |
| Module menu: Lock, Init, Copy, Paste, Copy (w/ mods) [S p.33-34] | Serum | all `oscN.*` | see 01 |
| Edit note / velocity ranges [S p.269-273] | Serum | none | see 01 (GAP) |
| Level [S p.57] [V p.6] | both | `oscN.level` | HAVE |
| Pan [S p.57] [V p.6] | both | `oscN.pan` | HAVE |

### 1.2 Wavetable oscillator

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| WT POS (frame position) [S p.45] | Serum | `oscN.frame` (0..100 %, over 64 frames or 1 frame for basic shapes) | HAVE |
| WT POS range 1..256 frames [S p.38, p.45] | Serum | none (built-in tables have 1 or 64 frames) | GAP |
| Smooth Interpretation of WT POS (right-click) [S p.45] | Serum | none (interpolation is the quality tier's, PARAMETERS) | GAP |
| Frame interpolation by quality tier: spectral on High, linear on Medium / Low | Vital | none (tier, not a parameter) | HAVE |
| Phase (start phase) [S p.41] [V p.8] | both | `oscN.phase` (0..360 deg) | HAVE |
| Random phase (RAND) [S p.42] [V p.8] | both | `oscN.random_phase` (0..100 %) | HAVE |
| Phase Legato (right-click PHASE) [S p.43] | Serum | none | GAP |
| Phase memory: All Voices / Contiguous / Per Voice [S p.44] | Serum | none | GAP |
| Unison voices 1..16 [S p.45] [V p.8] | both | `oscN.unison.voices` | HAVE (tier caps: 4 / 8 / 16) |
| CPU colour hint on the unison field [S p.45] | Serum | none | HAVE (UI) |
| Unison detune amount [S p.48] [V p.8] | both | `oscN.unison.detune` | HAVE |
| Unison detune MODE: Linear, Super, Exp, Inv, Random [S p.46-47] | Serum | partial: `oscN.unison.detune_curve` (-5..5) | GAP (five discrete laws); curve HAVE |
| Unison RANGE (max detune span) [S p.48] [V p.40] | both | none | GAP |
| Unison BLEND [S p.48] | both | `oscN.unison.blend` | HAVE |
| Unison STACK: Off, 12 (1-3x), 12+7 (1-3x), Center-12, Center-24 [S p.47] [V p.40-41] | both | `oscN.unison.stack` (11 choices incl. Unison, Center Drop 12 / 24, Octaves, Two Octaves, Power Chord, Two Power Chords, Major, Minor, Harmonic Series, Odd Harmonics) | HAVE (superset; "1-3x" repeat count GAP) |
| Unison WIDTH (stereo spread) [S p.47] [V p.40] | both | `oscN.unison.spread` | HAVE |
| Unison WT POS spread [S p.48] | Serum | `oscN.frame_spread` | HAVE |
| Unison WARP 1 spread [S p.48] | Serum | `oscN.warp.spread` | HAVE |
| Unison WARP 2 spread [S p.48] | Serum | none | GAP |
| Spectral unison switch [V p.40] | Vital | none (always on, PARAMETERS) | HAVE (fixed on) |
| Hi-resolution wavetable option [V p.40] | Vital | none | GAP |
| Left morph knob: 11 spectral algorithms (Vocode, Formant Scale, Harmonic Stretch, Inharmonic Stretch, Smear, Random Amplitudes, Low Pass, High Pass, Phase Disperse, Shepard Tone, Time Skew) [V p.9] | Vital | `oscN.morph.type`, `oscN.morph.amount`, `oscN.morph.spread` | HAVE |
| Warp slot 1 and slot 2 (two independent warp modes per oscillator) [S p.49, p.56] | Serum | `oscN.warp.type` + `oscN.morph.type` are two slots with fixed families | HAVE (one warp + one morph); GAP (two free warp slots, Swap Warps) |
| Warp OFF [S p.49] | Serum | `oscN.warp.type` = None | HAVE |
| Warp amount knob [S p.49] [V p.10] | both | `oscN.warp.amount` | HAVE |
| Warp phase (start of the warp cycle) [V p.10] | Vital | `oscN.warp.phase` (0..360 deg) | HAVE |
| Warp Sync (+ Var fader hard to soft sync) [S p.50] [V p.10] | both | `oscN.warp.type` = Sync | HAVE; GAP (Var fader) |
| Warp Alt group: Bend +, Bend -, Bend +/- [S p.50] | Serum | `oscN.warp.type` = Bend (single bidirectional) | HAVE (Bend +/-); GAP (Bend +, Bend - as separate modes) |
| Warp PWM [S p.50] [V p.10 Pulse] | both | `oscN.warp.type` = Pulse | HAVE |
| Warp Asym +, Asym -, Asym +/- [S p.50] | Serum | none | GAP |
| Warp Flip [S p.50] | Serum | none | GAP |
| Warp Mirror [S p.51] | Serum | none | GAP |
| Warp Remap 1-4 (drawn remap graph, pencil button) [S p.51] | Serum | `oscN.remap.on`, `oscN.remap.slot`, `oscN.remap.rise`, `oscN.remap.fall` + `content.remaps` (as a position remap of the wavetable frame, not as a warp mode: 13 §4.14) | HAVE (WTREMAP) |
| Warp Quantize [S p.51] [V p.10] | both | `oscN.warp.type` = Quantize | HAVE |
| Warp Odd/Even [S p.51] | Serum | none | GAP |
| Warp Formant [V p.10] | Vital | `oscN.warp.type` = Formant | HAVE |
| Warp Squeeze [V p.10] | Vital | `oscN.warp.type` = Squeeze | HAVE |
| Warp Filter group: LPF, HPF [S p.52] | Serum | none as warp (spectral Low Pass / High Pass exist as morph types) | GAP (as warp); morph equivalents HAVE |
| Warp Distortion group: Tube, Soft Clip, Hard Clip, Diode 1, Diode 2, Linear Fold, Sine Fold, Zero-Square, Asym, Rectify, Sine Shaper, Stomp Box, Tape Sat., Soft Sat. [S p.52-54] | Serum | none | GAP (14 modes) |
| Warp FM from the other oscillators [S p.54] [V p.10] | both | `oscN.warp.type` = FM from Osc x (the two other oscillators) | HAVE |
| Warp FM from Noise, Sub, Filter 1, Filter 2 [S p.54] | Serum | none | GAP |
| Warp FM variants Thru-Zero, Exp, Linear [S p.54-55] | Serum | none (Vital FM law fixed) | GAP |
| Warp FM from sampler [V p.9-10] | Vital | none (sample oscillator not exposed; adapter drops those two variants, README) | GAP |
| Warp PD (phase distortion) from Osc / Noise / Sub / Filter 1 / Filter 2 / Self [S p.55] | Serum | none | GAP |
| Warp AM from Osc / Noise / Sub / Filter 1 / Filter 2 [S p.56] | Serum | none | GAP |
| Warp RM from the other oscillators [S p.56] [V p.10] | both | `oscN.warp.type` = RM from Osc x | HAVE |
| Warp RM from Noise, Sub, Filter 1, Filter 2 [S p.56] | Serum | none | GAP |
| Swap Warps [S p.56] | Serum | none | GAP |
| Warp visible in 2D view (Sync, Alt, Distortion) [S p.49] | Serum | none | HAVE (UI, draws the warped frame) |

### 1.3 Multisample oscillator [S p.58-67]

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Select multisample instrument, Load SFZ (envelopes honoured) [S p.58-59] | Serum | none | GAP |
| Zone display; played zone highlighted; click shows last note waveform [S p.60] | Serum | none | GAP |
| Envelope pane: OVERRIDE, DELAY (with BPM sync), A, H, D, S, R, drag points [S p.61-62] | Serum | none | GAP |
| VEL TRACK [S p.62] | Serum | none | GAP |
| RAND (start phase) [S p.62] | Serum | none | GAP |
| TIMBRE (inverse zone mapping) [S p.63] | Serum | none | GAP |
| Unison, mode, stack, width, range, WARP 1 / 2 spread, detune, blend [S p.63-66] | Serum | same keys as 1.2 would apply | GAP |
| Warp, Pan, Level [S p.66] | Serum | `oscN.pan`, `oscN.level` (warp: GAP for this type) | HAVE (pan, level); GAP (warp) |
| Switch last note to single sample [S p.67] | Serum | none | GAP |

### 1.4 Sample oscillator [S p.68-84] (the Vital sampler [V p.10-12] is the same function)

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Sample menu: factory samples, wavetables as samples, Load Sample, show location, reload [S p.70] | both | none | GAP |
| Sample root note from file name suffix, WAV instrument chunk [S p.68] | Serum | none | GAP |
| Start / End markers [S p.71] | Serum | none | GAP |
| Operations: Show Marker Animation, Zoom to Start and End [S p.72] | Serum | none | GAP |
| Snap Off / Zero / Beats / Loop [S p.72-73] | Serum | none | GAP |
| Fade Edges (None, 1 ms .. 128 ms), Normalize, Reverse, Trim (non-destructive, undoable), Reload Sample [S p.73] | Serum | none | GAP |
| Slicing: Off, Auto (threshold line), Manual (add / remove / move slice handles), slice note shown on hover [S p.74-75] | Serum | none | GAP |
| Slice options: Play Slice to End, Play Single Slice (mod target), Root Note C-1..C8 [S p.76] | Serum | none | GAP |
| Send to Selected Clip, Auto-Sync to Clip [S p.76] | Serum | none | excluded: Kick (Clips) |
| Loop menu: One-shot, Fwd Loop, Rev Loop, Fwd/Rwd Loop, Tailed [S p.77-78] | Serum | none | GAP |
| Loop options: Relative Loop, Link Loop Length, Exit Loop on Release [S p.78] | Serum | none | GAP |
| Loop start / end (LS, LE fields and blue markers, reverse if end < start, reset by default modifier) [S p.79] | Serum | none | GAP |
| Loop crossfade [S p.79] | Serum | none | GAP |
| SCAN speed / direction; Range +-200 / 400 / 800 %; Reverse; Lock Scan Rate to Tempo; Sample Length to BPM (SCAN becomes RATE) [S p.80] | Serum | none | GAP |
| Unison incl. START (random start offset) and SPAN (fixed start offset) [S p.81-83] | Serum | none | GAP |
| Warp (same modes as wavetable), Pan, Level [S p.83-84] | Serum | none for this type | GAP |
| Pitch (semitone / cents) of the sample [V p.11] | Vital | none | GAP |
| Switch Sample to Wavetable (import options) [S p.84] | Serum | none | GAP; import options: 03 |
| Vital sampler level, pan, routing, transpose snap [V p.10-11] | Vital | none | GAP |

### 1.5 Granular oscillator [S p.85-103]

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Sample load, start / end, operations, snap, slicing (not in Manual mode) [S p.86-88] | Serum | none | GAP |
| Loop menu incl. Loop Grains (grains respect loop markers) [S p.89] | Serum | none | GAP |
| Manual mode: playhead replaced by X|Y dot, SCAN = horizontal position [S p.89] | Serum | none | GAP |
| Loop start / end, crossfade of grains (needs Loop Grains) [S p.90] | Serum | none | GAP |
| Window: AMOUNT, SKEW (with per-grain randomisation), SHAPE (Hann, Welch, Gaussian, Blackman-Harris, Sinc, Tukey, Triangle, Trapezoid, ExpDec, Exp Dec Rev); quick edit with Alt-drag [S p.91-93] | Serum | none | GAP |
| Unison incl. START, SPAN, SPAWN PATTERN (Together, Even, Exp, Random) [S p.93-95] | Serum | none | GAP |
| X|Y control, Y axis target (None, Level, Warp, Warp 2, Density, Grain Length, Window Amt, Window Skew, Rand Offset, Rand Dir, Rand Pitch, Rand Length, Rand Pan, Rand Gain, Rand Window, Rand Skew, Rand Warp, Rand Warp 2) and Y modulation menu [S p.96-98] | Serum | none | GAP |
| Warp settings panel (same warp modes) [S p.99] | Serum | none | GAP |
| SCAN rate; Range; Reverse; Key Track; Lock Scan Rate; Sample Length to BPM [S p.99-100] | Serum | none | GAP |
| DENS (Free Hz / BPM Sync with triplet + dotted / Grains), Jump Start, Max Grains [S p.101] | Serum | none | GAP |
| LENGTH (Free / BPM Sync with triplet + dotted / Percent) [S p.101-102] | Serum | none | GAP |
| Grain randomisation: OFFSET, DIR (Reverse Grains), PITCH, RAND length / pan / level [S p.102-103] | Serum | none | GAP |
| Pan, Level, Switch to Wavetable [S p.102-103] | Serum | `oscN.pan`, `oscN.level` (the rest GAP) | GAP (type); keys exist for pan / level |

### 1.6 Spectral oscillator [S p.104-122]

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Sample load, start / end, ops, snap, Show Waveform Display [S p.105-108] | Serum | none | GAP |
| Low / High frequency markers; Smooth (4th-order Butterworth), Post Warp; markers modulatable [S p.107] | Serum | none | GAP |
| Loop menu, Manual mode, LS / LE, crossfade [S p.109-110] | Serum | none | GAP |
| Unison incl. START, SPAN [S p.111-113] | Serum | none | GAP |
| X|Y control, Y axis target (None, Level, Warp, Warp 2, Spec Flt Cutoff, Spec Flt Wet/Dry, Freq Lo, Freq Hi) [S p.114-115] | Serum | none | GAP |
| SCAN (Range, Reverse, Key Track, Lock Scan Rate, Sample Length to BPM), Phase Lock, Transients [S p.116-117] | Serum | none | GAP |
| CUT (spectral filter cutoff) [S p.117] | Serum | none | GAP |
| FILTER: curve editor (add / remove / drag points, curve points, snap, multi-select, GRID), factory presets, wavetable as filter, Curve Filter [S p.117-121] | Serum | none | GAP |
| MIX (wet / dry), Warp mode, Pan, Level [S p.121] | Serum | none (`oscN.pan`, `oscN.level` only) | GAP |
| Switch to Wavetable [S p.122] | Serum | none | GAP |

### 1.7 Sub oscillator [S p.123-127]

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Enable [S p.124] | Serum | none | GAP |
| OCT, CRS pitch [S p.124] | Serum | none | GAP |
| Waveform: Sine, Rounded Rect, Triangle, Saw, Square, Pulse [S p.124-126] | Serum | none | GAP |
| PHASE, Contiguous phase option [S p.127] | Serum | none | GAP |
| Pan, Level, routing, busses, FM / PD / AM / RM source [S p.127, p.54-56] | Serum | none | GAP |

### 1.8 Noise oscillator [S p.128-132]

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Enable [S p.129] | both | `noise.on` | HAVE |
| Level, Pan [S p.132] | both | `noise.level`, `noise.pan` | HAVE |
| Routing (filters, effects, direct) [S p.31] [V p.7] | both | `noise.destination` | HAVE |
| Noise sample menu, < > arrows; colours White, Pink, Brown, Geiger and factory noises [S p.129-130] | Serum | none (engine has white noise only, PARAMETERS) | GAP |
| STEREO (colour modes) and FILTER knob (low / high pass) [S p.129] | Serum | none | GAP |
| Load own sample (menu or drag), Embed in Preset [S p.130] | Serum | none | GAP |
| One-shot / Looping switch [S p.131] | Serum | none | GAP |
| START, RAND (start phase) [S p.131] | Serum | none | GAP |
| PITCH (default 50 %), FINE [S p.132] | Serum | none | GAP |
| Noise as modulation / warp source [S p.128] | Serum | none | GAP |
| Send Wavetable frame to noise oscillator (editor) [S p.286] | Serum | none | GAP |

### 1.9 Mixer and main output [S p.144-151]

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| MIX tab: channels for enabled sources, header toggle enables the module [S p.144-145] | Serum | `oscN.on`, `noise.on`, `filterN.on` | HAVE |
| Channel routing menu: Filter (knob), Main, Direct, None [S p.145-146] | Serum | `oscN.destination`, `noise.destination` | HAVE (discrete); GAP (knob blend, None) |
| ENV 1 button on Main / Direct channels (ENV 1 on/off for that source) [S p.146] | Serum | none | GAP |
| Bus 1 / Bus 2 send knobs [S p.146] | Serum | none | GAP |
| Channel PAN and LEVEL [S p.147] | Serum | `oscN.pan`, `oscN.level`, `noise.pan`, `noise.level` | HAVE |
| Filter channels: routing (other filter, Main, Direct, None), cutoff / resonance drag display, bus sends, PAN, MIX, LEVEL [S p.148-149] | Serum | `filter.routing` (series / parallel), `filterN.mix`, `filterN.cutoff`, `filterN.resonance` | HAVE (routing, mix, cutoff, resonance); GAP (pan, level, per-filter destination, buses) |
| Bus channels: routing (Main, Direct, other Bus), FX bypass button, LEVEL [S p.150] | Serum | none | GAP |
| MAIN and DIRECT level; FX bypass for main channel [S p.151] | Serum | `master.volume` (MAIN) | HAVE (MAIN); GAP (DIRECT level, per-bus FX bypass) |
| Copy / swap oscillator channel by dragging label [S p.145] | Serum | all `oscN.*` | see 01 |

## 2. Screen design

### 2.1 OSC page, phone portrait (wavetable type)

```
+------------------------------------------+
| [OSC1*][OSC2][OSC3][NOISE]    (power)    |  osc chips: tap = select, power dot per chip
| Type: [Wavetable v]  [< Saw to Square >] |  type menu + wavetable arrows (oscN.wavetable)
+------------------------------------------+
|   +----------------------------------+   |
|   |      wavetable view 2D/3D/SP     |   |  tap = cycle view, drag = frame (03)
|   |      (200 pt high, full width)   |   |
|   +----------------------------------+   |
| FRAME  ====o========  42%        [edit]  |  slider oscN.frame + edit button -> 03
+------------------------------------------+
| (FRAME) (WARP) (MORPH) (LEVEL)            |  4 main knobs
|   frame warp.amount morph.amount level    |
+------------------------------------------+
| PITCH | UNISON | PHASE | WARP+ | ROUTE |PAN|  "more" row: each chip opens a sheet
+------------------------------------------+
```

Main knobs per type: Wavetable `oscN.frame`, `oscN.warp.amount`, `oscN.morph.amount`, `oscN.level`. The 5th-6th controls live in the chips.

Sheets (bottom sheets, 55 % screen height, swipe down to close):

```
PITCH sheet                     UNISON sheet
 OCT  [-2][-1][0][+1][+2]        VOICES  [1 ... 16]      (oscN.unison.voices, CPU chip)
 SEM  (wheel -12..+12)           DETUNE  (knob)   CURVE (knob, = law)   BLEND (knob)
 FIN  (knob, cents)              WIDTH   (knob)   FRAME SPREAD  WARP SPREAD  MORPH SPREAD
 CRS  (GAP, greyed)              STACK   [Unison v]   MODE [Linear v] (GAP)  RANGE (GAP)
 MODE [Semitones v] (Harmonics,
  Ratio, Step = GAP)             PHASE sheet
 SNAP keyboard (GAP)              PHASE (knob 0-360)  RAND (knob)   LEGATO (GAP)  MEMORY (GAP)
 TRACKING [on] (GAP)
                                 WARP+ sheet
 ROUTE sheet                      Morph type list  | Warp type list (grouped: Basic / Alt / Filter /
  ( ) Filter 1  ( ) Filter 2        Distortion / FM / PD / AM / RM; GAP groups shown greyed)
  ( ) Both  ( ) Effects only        amount  phase  spread  (Var fader when Sync: GAP)
  ( ) Direct  ( ) None (GAP)       [Swap warps] (GAP)
  BUS 1 / BUS 2 sends (GAP)
```

Each GAP control is drawn but disabled with the badge "not available in this engine".

### 2.2 OSC page, other oscillator types (phone portrait)

Common layout: type menu, display, 4 knobs, chip row. Tapping the display opens zoom; pinch zooms, horizontal drag pans.

```
SAMPLE                          GRANULAR                       SPECTRAL
+----------------------+        +----------------------+       +----------------------+
| [Sample v] [file < >]|        | [Granular v][file<>] |       | [Spectral v][file<>] |
| +------------------+ |        | +------------------+ |       | +------------------+ |
| |S| waveform  |E|   | |        | | waveform +X|Y dot| |       | | spectrogram   Hi | |
| | LS ===== LE (loop)| |        | |  playhead        | |       | |  S|   |E        Lo | |
| +------------------+ |        | +------------------+ |       | +------------------+ |
| (SCAN)(UNISON)(PAN)(LEVEL)    | (SCAN)(DENS)(LENGTH)(LEVEL)   | (SCAN)(CUT)(MIX)(LEVEL)|
| LOOP|SLICE|OPS|WARP|UNI|PITCH | LOOP|WINDOW|RAND|WARP|UNI|XY | LOOP|FILTER|WARP|UNI|XY |
+----------------------+        +----------------------+       +----------------------+

MULTISAMPLE
+----------------------+
| [Multisample v][inst<>]|
| zone map (note x vel) |
| (TIMBRE)(UNISON)(PAN)(LEVEL)
| ENV|VEL TRK|RAND|WARP|UNI
+----------------------+
```

Chip sheets: LOOP (mode list, options, LS / LE fields, crossfade), SLICE (Off / Auto / Manual, threshold, play options, root note), OPS (snap, fade edges, normalize, reverse, trim, reload), WINDOW (amount, skew, shape list with picture, per-grain random), RAND (offset, direction, pitch, length, pan, level), XY (show, Y target, Y modulation), FILTER (spectral curve editor, full screen), PITCH (semitone / cents).

### 2.3 Sub and Noise (phone portrait)

```
SUB (GAP)                           NOISE
+------------------------------+    +------------------------------+
| [SUB*] power                 |    | [NOISE*] power               |
| waveform picker: ~ sin | rr | /\ | saw | sq | pulse |   [White v][< >] (colour list GAP)
| scope of the chosen wave     |    | scope / spectrum of the noise |
| (OCT)(CRS)(PHASE)(LEVEL)     |    | (LEVEL) (PAN)  (STEREO)(FILTER)|
| PAN | ROUTE | BUS            |    | LOOP/1SHOT | START | RAND | PITCH | FINE | ROUTE |
+------------------------------+    +------------------------------+
```

Noise main knobs: `noise.level`, `noise.pan`; STEREO and FILTER knobs only for the colour modes (GAP).

### 2.4 MIX page, phone portrait

```
+------------------------------------------+
| O1   O2   O3   SUB  NOISE  F1   F2  B1  B2 |  strips scroll sideways, 88 pt each, 4 visible
| [on] [on] [off][off] [off] [on] [off]       |
| route v (Filter/Main/Direct/None)           |
|  |#|  |#|  | |  | |   | |   |#|  | |        |  vertical LEVEL fader (osc level / filter / bus)
|  (pan)(pan)                                 |  PAN knob under each fader
| bus1 bus2 sends (GAP)  ENV1 button (GAP)    |
+------------------------------------------+
| MAIN [fader master.volume + stereo meter] | DIRECT [fader] (GAP) |
+------------------------------------------+
```

Filter strips (F1, F2) show a mini cutoff / resonance drag display (`filterN.cutoff`, `filterN.resonance`) and a MIX knob (`filterN.mix`); the routing control for filters is `filter.routing` (Parallel / Series 1 to 2 / Series 2 to 1) shown as one chip between F1 and F2 (04).

### 2.5 Landscape / tablet / MPC

```
+----------------------------------------------------------------------------------+
| OSC1 [type v][wt <>]          | OSC2 ...                   | OSC3 ...              |
| +--------------------------+  | +------------------------+ | +-------------------+ |
| |  view (3D/2D/SP)         |  | |                        | | |                   | |
| +--------------------------+  | +------------------------+ | +-------------------+ |
| FRAME WARP MORPH LEVEL        | ...                        | ...                   |
| PITCH UNISON PHASE WARP+ ROUTE| ...                                                |
+----------------------------------------------------------------------------------+
| NOISE tile | SUB tile | MIX strips (all 9 + MAIN + DIRECT visible)               |
+----------------------------------------------------------------------------------+
```

Phone landscape: one oscillator panel at the left (view + knobs), sheets open at the right as a side drawer instead of a bottom sheet; the osc chips sit on top.

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| Osc chip | Tap | Select that oscillator in the page; long-press = module sheet (01) | Swipe left / right on the view area switches oscillator |
| Power dot / `oscN.on` | Tap | Enables / disables; panel dims [S p.28] | Module sheet |
| Type menu | Tap | Opens type list (Wavetable, Multisample, Sample, Granular, Spectral); non-HAVE types show the "not available" badge but still open their panel | Long-press sheet |
| Wavetable `< >` | Tap / swipe on the name | Previous / next table (`oscN.wavetable`) | Table list sheet |
| FRAME slider / visual | Horizontal drag on the view or the slider | `oscN.frame` | Numeric entry (01) |
| Main knob | Vertical drag (rules in 01) | Parameter | Long-press sheet |
| OCT / SEM | Tap octave segment; wheel for SEM | `oscN.transpose` = OCT*12 + SEM [S p.29] | Numeric entry of total semitones |
| FIN | Knob drag | `oscN.tune` | Numeric |
| Pitch MODE | Tap | Semitones / Harmonics / Ratio / Step (GAP modes disabled) [S p.30] | none |
| Transpose snap keyboard (GAP) | Tap semitone keys on a mini keyboard | Limits the pitch knob to the chosen semitones, lit keys purple [V p.6-7] | Clear button |
| UNISON voices | Drag on the number field or tap `-` / `+` | `oscN.unison.voices`; colour reflects CPU [S p.45]; amber outline if capped by the tier | Numeric |
| WARP+ type list | Tap a group, tap a mode; `<` `>` step through [S p.49] | `oscN.warp.type` / `oscN.morph.type` | Long-press sheet |
| Warp mode picture | The 2D view draws the warped frame live | Visual feedback [S p.49] | none |
| Remap graph (GAP) | Pencil button opens graph; drag points / curve [S p.51] | Custom remap | Presets (Remap 1-4) |
| ROUTE sheet radio | Tap | `oscN.destination` | Mix page channel menu |
| Mix strip level / pan | Drag fader / knob | `oscN.level`, `oscN.pan`, `noise.level`, `noise.pan` | Double-tap = numeric (setting) |
| Mix strip header | Tap = power; long-press = module sheet | `oscN.on` | Module sheet |
| Mix strip drag header to another strip | Drag (tablet) | Swap; copy / copy-with-mods with modifier toggle [S p.145] | Module sheet |
| Sample start / end marker | Drag handle (44 pt hit area, magnified loupe above finger) [S p.71] | Sets start / end | `S` / `E` numeric fields in the LOOP sheet |
| Loop marker pair | Drag marker, drag between markers moves both; double-tap between = reset to start / end [S p.79] | LS / LE | LS / LE fields |
| Sample zoom / pan | Pinch / two-finger drag (vertical drag zooms when Zoom-to-Start-End is off) [S p.72] | View only | Zoom buttons `+` `-` `fit` |
| Slice handle (Manual) | Drag; add slice: long-press on waveform (replaces Alt-click); remove: long-press a handle [S p.75] | Slices | `+` / `trash` tool in the SLICE sheet |
| Slice threshold line (Auto) | Drag line up / down [S p.74] | Fewer / more slices | Threshold slider in sheet |
| X|Y dot | Drag dot [S p.89, p.96] | Playback position (X) and Y target | X / Y numeric fields in XY sheet |
| Window button (granular) | Tap = sheet; Alt-drag replaced by vertical drag on the button with two fingers [S p.91] | Window amount + skew | WINDOW sheet knobs |
| Spectral Lo / Hi markers | Drag markers on the right of the spectrogram [S p.107] | Frequencies | Numeric in FILTER sheet |
| Spectral filter editor | Double-tap: add / remove point; drag point; drag curve handle; marquee select; snap toggle for grid [S p.118-119] | Filter mask | Add / remove buttons, GRID field |
| Sub waveform picker | Tap icon | Waveform | none |
| Noise sample `< >` | Tap | Next noise [S p.129] | List |
| MAIN fader | Drag | `master.volume` | Numeric |
| Bus FX bypass button (GAP) | Tap (red when bypassed) [S p.150] | Bypasses the bus FX | none |

## 4. States and edge cases

* Off oscillator: whole panel 40 % opacity, still editable [S p.28]. An oscillator routed to a filter that is off plays into the effects [V p.7].
* Routing "Effects only" = Serum Main; "Direct out" = Serum Direct [S p.31].
* Warp FM / RM source: the "from Osc x" entries name the two other oscillators and are disabled with a note when that oscillator is off ("source must be enabled" [S p.54]).
* Ranges and units: level and pan percent (pan -100..100), transpose semitone, tune cents, phase degrees, frame percent; defaults from `docs/params.json` (level 50 %, frame 0 %, unison voices 1, detune 25 %, blend 80 %, spread 100 %, warp phase 180 deg).
* Unison UI: `detune`, `blend`, `detune_curve` are disabled (dimmed) at 1 voice; blend only matters above 2 voices [S p.48].
* Quality tier: unison voices above the tier cap (4 Low, 8 Medium, 16 High) get an amber outline and a tooltip; spectral frame interpolation becomes linear on Medium / Low and the interpolation hint in the view says so.
* Table with 1 frame (basic shapes): FRAME slider is disabled (0 effect), the view shows the 2D wave; tables with 64 frames show the 3D view.
* Missing wavetable content: badge `missing`, fallback built-in table, `Restore` action (ARCHITECTURE 5.2).
* GAP controls: drawn, disabled, badge; they never write a value. If a GAP type is loaded from a future preset the page shows "type not supported by this engine" and keeps the oscillator silent.
* Mod display: purple ring on every modulatable knob (`osc*.frame`, `level`, `pan`, `phase`, ... are modulatable, 06); `oscN.wavetable`, `destination`, `warp.type`, `morph.type`, `unison.stack`, `on` are not.
* CPU colour: unison field and WARP+ picker tint (green, amber, red) by voices and tier.
* Noise colours (GAP): the engine plays white noise; the picker shows it as the only active entry.
* Pan law and level scaling follow the engine; the UI shows percent only.

## 5. Kick remote pages

Pages repeat for `osc1`, `osc2`, `osc3` (names "Osc 1 Main", ...):

| Page | Params (<= 8) |
|------|---------------|
| Osc N Main | `oscN.level`, `oscN.pan`, `oscN.frame`, `oscN.transpose`, `oscN.tune`, `oscN.warp.amount`, `oscN.morph.amount`, `oscN.unison.voices` |
| Osc N Unison | `oscN.unison.detune`, `oscN.unison.detune_curve`, `oscN.unison.blend`, `oscN.unison.spread`, `oscN.frame_spread`, `oscN.phase`, `oscN.random_phase`, `oscN.unison.stack` |
| Osc N Types | `oscN.on`, `oscN.wavetable`, `oscN.warp.type`, `oscN.morph.type`, `oscN.warp.phase`, `oscN.warp.spread`, `oscN.morph.spread`, `oscN.destination` |

| Page | Params (<= 8) |
|------|---------------|
| Noise and Mix | `noise.on`, `noise.level`, `noise.pan`, `noise.destination`, `osc1.level`, `osc2.level`, `osc3.level`, `master.volume` |

## 6. Coverage check

| Key group (params.json) | Where in section 2 |
|-------------------------|--------------------|
| `oscN.on` | OSC chips power dot (2.1), MIX strip header (2.4) |
| `oscN.wavetable` | wavetable `< >` row (2.1) |
| `oscN.frame` | FRAME slider and main knob (2.1) |
| `oscN.frame_spread` | UNISON sheet (2.1) |
| `oscN.level`, `oscN.pan` | main knob LEVEL (2.1), PAN chip, MIX strips (2.4) |
| `oscN.transpose`, `oscN.tune` | PITCH sheet (2.1) |
| `oscN.phase`, `oscN.random_phase` | PHASE sheet (2.1) |
| `oscN.destination` | ROUTE sheet (2.1), MIX route menu (2.4) |
| `oscN.unison.voices`, `.detune`, `.detune_curve`, `.spread`, `.blend`, `.stack` | UNISON sheet (2.1) |
| `oscN.morph.type`, `.amount`, `.spread` | WARP+ sheet and MORPH knob (2.1) |
| `oscN.warp.type`, `.amount`, `.phase`, `.spread` | WARP+ sheet and WARP knob (2.1) |
| `noise.on`, `noise.level`, `noise.pan`, `noise.destination` | NOISE panel (2.3), MIX strip (2.4) |
| `master.volume` | MAIN fader (2.4) |
| `filter.routing`, `filterN.mix` | MIX strips F1 / F2 (2.4); full filter page in 04 |
| Sub, Sample, Granular, Spectral, Multisample | GAP: 2.2, 2.3 (no keys) |

## 7. Open questions (owner)

1. Five oscillator types (multisample, sample, granular, spectral, sub) and 14 distortion warps have no engine today. Which engine delivers them (Vital sampler is in the engine but not exposed; the rest is new DSP)? The UI is designed for all of them.
2. User wavetable selection needs a content-id parameter next to `oscN.wavetable` (13 built-ins only). Confirm the state / ParamInfo design (ARCHITECTURE 5.2 has the content reference in the preset).
3. Two free warp slots per oscillator (Serum) versus fixed morph + warp slots (Vital): the UI keeps Vital's two families unless the engine grows a second warp.
4. Bus 1 / Bus 2 and DIRECT level change the effect routing (08). Confirm they belong to the engine roadmap.
5. Serum Clip integration for sample slices (`Send to Selected Clip`) is excluded (Kick).
