# Modulation – mobile UI design

Covers all modulation routing, touch assignment gestures, modulation depth rings, context menus, macros, MIDI/expression sources, LFO point modulation, and the 64-slot Modulation Matrix (`docs/PARAMETERS.md`, Serum 2 `[S p.183-214, 26, 314, 324, 351]`, Vital `[V p.16-26]`). Detailed envelope, LFO, and Random LFO shape editors are in `05-envelopes-lfos.md`; their modulation source roles and Serum 2 chapter items (`[S p.183-200]`) are inventoried below.

* **Sequencer exclusions (`00-BRIEF.md`):** Serum 2 **Clips** `[S p.219]` — excluded: Kick. Serum 2 **Arpeggiator** `[S p.244]` — excluded: Kick. Preset browser `[S p.327]` — excluded: Kick.

---

## 1. Function inventory

| Function | Origin (Vital / Serum / both) | Our params (keys) | Engine status (HAVE / GAP) |
|----------|-------------------------------|-------------------|----------------------------|
| **Envelopes as Mod Sources (Env 1..6)** (`ENV 1..4` in Serum, `ENV 1` hardwired to voice amp and shown in gray behind `ENV 2..4`; 6 envelopes in Vital/SynthyBoy, unipolar `0..1`) | Both `[S p.183-187]`, `[V p.17-18]` | `modNN.source` = `1..6` (`env1..env6`); `env1..6.{delay,attack,hold,decay,sustain,release,attack_curve,decay_curve,release_curve}` (54 keys, see `05-envelopes-lfos.md`) | **HAVE** (6 envelopes; `env1` hardwired to voice amp + usable as mod source) |
| **Envelope Zoom Lock / Manual Zoom** (lock normalizes graph to fit display; unlock drags vertically to zoom time ruler) | Both `[S p.184-185]`, `[V p.17]` | UI view state (no DSP param; see `05-envelopes-lfos.md`) | **HAVE** (UI-only zoom state) |
| **Envelope Legato Inverted** (right-click envelope graph -> force envelope retrigger on note-on even when global Legato is on) | Serum `[S p.185, 216]` | `GAP` (per-envelope legato override flag) | **GAP** |
| **Envelope Parameter Units (MS / BPM)** (switch envelope times between milliseconds and host tempo subdivisions) | Serum `[S p.185]` | `GAP` (`env1..6` times are `ms` only in `docs/params.json`) | **GAP** |
| **Envelope Background Grid (Time / Beats)** (vertical ruler lines in ms or beat intervals) | Both `[S p.185]`, `[V p.17]` | UI view state (see `05-envelopes-lfos.md`) | **HAVE** (UI-only grid) |
| **LFOs as Mod Sources (LFO 1..8)** (`LFO 1..10` in Serum where `7..10` appear after `6` is assigned; 8 LFOs in Vital/SynthyBoy, bipolar `-1..1`) | Both `[S p.188-194]`, `[V p.18-20]` | `modNN.source` = `7..14` (`lfo1..lfo8`); `lfo1..8.{shape,rate,sync,division,sync_type,phase,fade_in,delay,smooth,smooth_time,stereo,keytrack_transpose,keytrack_tune}` (104 keys, see `05-envelopes-lfos.md`) | **HAVE** (8 LFOs, 12 built-in shapes; LFO 9..10 and free-form LFO editor are **GAP**) |
| **LFO Copy to LFO** (`Opt/Alt-drag` LFO tab onto another LFO tab to copy all settings) | Serum `[S p.189, 351]` | UI action copying `lfoA.*` -> `lfoB.*` (13 keys) | **HAVE** (UI state copy) |
| **LFO Graph Drawing Tools & Modifiers** (`Point`, `Flat`, `Ramp Up`, `Ramp Down`, `Double-click` add/delete point, `Shift-click` step draw, `Opt-drag` snap to grid / move all curves, marquee select, `Cmd/Ctrl-drag` rainbow relative multi-point move, `Shift-Cmd-click` loopback point, dedicated `LFO Editor` popup) | Both `[S p.189-193, 351]`, `[V p.18-19]` | `lfo1..8.shape` (12 built-in shapes; free-form `LineGenerator` exists in `sbvital` core, not exposed in `ParamInfo` — see `05-envelopes-lfos.md`) | **GAP** (free-form LFO node editing in `ParamInfo`; built-in 12 shapes **HAVE**) |
| **LFO Type (`Normal`, `Path`, `Chaos: Lorenz`, `Chaos: Rossler`, `S&H`)** | Serum `[S p.191]` (Vital splits Random LFO into separate module `[V p.20-22]`) | `lfo1..8` (Normal) + `rlfo1..4.style` (`0 Perlin`, `1 Sample and hold`, `2 Sine interpolate`, `3 Lorenz attractor`) | **HAVE** (`Normal`, `S&H`, `Lorenz`, `Perlin`, `Sine interp`); **GAP** (`Path` 2D LFO, `Chaos: Rossler`) |
| **LFO Mode (`FREE`, `RETRIG`, `ENVELOPE` + Loopback point)** | Both `[S p.191]`, `[V p.19-20]` | `lfo1..8.sync_type` (`0 Retrigger`, `1 Free-run`, `2 Envelope`, `3 Sustain envelope`, `4 Loop at point`, `5 Loop and hold`) | **HAVE** |
| **LFO Mono / Poly Switch** (`MONO` applies shared LFO phase across all voices vs polyphonic per-voice LFO) | Serum `[S p.191]` | `GAP` (in Vital/SynthyBoy, `sync_type = 1 Free-run` is global, `0 Retrigger` is per-voice; separate `MONO` retrigger switch is `GAP`) | **GAP** |
| **LFO Direction (`Forward`, `Reverse`, `Ping Pong`)** | Serum `[S p.191]` | `GAP` (`Saw Up`/`Saw Down` shapes exist; general `Direction` mode is `GAP`) | **GAP** |
| **LFO Host Anchor (`HOST`), Rate (`BPM/HZ`, `TRIP/DOT`, `10x` Hz range, `Swing`), `RISE`, `DELAY`, `SMOOTH`, `PHASE` (`Snap to Grid`)** | Both `[S p.192-193]`, `[V p.20]` | `lfo1..8.{rate,sync,division,fade_in,delay,smooth,smooth_time,phase,stereo,keytrack_transpose,keytrack_tune}` | **HAVE** (`rate` up to 200 Hz covers `10x`; `sync` covers `Free/Tempo/Dotted/Triplet/Key follow`; `fade_in` = `RISE`); **GAP** (LFO `Swing` link, transport sample anchor when `HZ` free) |
| **Copy Wavetable Frame to LFO / Copy LFO Shape to Wavetable** (`Default -> Wavetable A/B/C to LFO`; `Opt/Alt-drag` LFO tab to Osc wavetable display) | Serum `[S p.197-198, 351]` | `GAP` (requires custom LFO/WT frame transfer API) | **GAP** |
| **Modulating LFO Points (X / Y via LFO Busses)** (Right-click LFO point -> `Modulate X` / `Modulate Y` -> `LFO Bus 1..N`, or drag mod source onto point `X` / `Y` button; multi-point selection) | Serum `[S p.198-200]` | `GAP` (LFO shapes are static tables in `vital_tables.cpp`) | **GAP** |
| **Random LFOs as Mod Sources (Random LFO 1..4)** (Perlin, S&H, Sine Interpolate, Lorenz Attractor, bipolar `-1..1`) | Vital `[V p.20-22]` | `modNN.source` = `15..18` (`rlfo1..rlfo4`); `rlfo1..4.{style,rate,sync,division,sync_type,stereo,keytrack_transpose,keytrack_tune}` (32 keys, see `05-envelopes-lfos.md`) | **HAVE** |
| **Macros (Macro 1..4 as Source & Destination)** (Serum has 8 macros `MACRO 1..8`, usable as both source and destination `[S p.206]`; Vital/SynthyBoy has 4 macros `macro.1..4`) | Both `[S p.206-208]`, `[V p.23]` | `macro.1`, `macro.2`, `macro.3`, `macro.4` (`0..100 %`); `modNN.source` = `19..22` (`macro1..macro4`); `modNN.destination` = `207..210` (`macro.1..4`) | **HAVE** (4 macros, both source and modulatable destination; Macros 5..8 are **GAP**) |
| **Swap Macros** (drag one macro onto another to swap all their modulation assignments and values) | Serum `[S p.208]` | UI operation swapping two `macro.N` keys (e.g. `macro.1` <-> `macro.2`) and rewriting `modNN.source`/`destination` | **HAVE** (UI state operation) |
| **Velocity Mod Source (`VELO`) & Custom Velocity Graph** (MIDI note-on velocity `0..1`; Serum adds editable multi-point `VELO` curve graph, `Legato (Portamento Time)` slew, `Init Graph`) | Both `[S p.201-204]`, `[V p.23]` | `modNN.source` = `23` (`velocity`, unipolar) + `master.velocity_track` (`-100..100 %`) | **HAVE** (`velocity` source + per-slot `modNN.power` curve); **GAP** (free-form multi-point `VELO` graph & legato velocity glide) |
| **Note Mod Source (`NOTE`) & Custom Note Graph** (MIDI key number `0..127` -> `0..1`; Serum adds editable multi-point `NOTE` curve graph, `Legato (Portamento Time)` slew, `Init Graph`) | Both `[S p.201, 204-205]`, `[V p.23]` | `modNN.source` = `24` (`note`, unipolar) | **HAVE** (`note` source + per-slot `modNN.power` curve); **GAP** (free-form multi-point `NOTE` graph & legato note glide) |
| **Aftertouch / Channel Pressure Source** | Both `[S p.211]`, `[V p.23]` | `modNN.source` = `25` (`aftertouch`, unipolar) | **HAVE** |
| **Pitch Wheel / Pitch Bend Source** | Both `[S p.211]`, `[V p.23]` | `modNN.source` = `26` (`pitch_wheel`, bipolar) | **HAVE** |
| **Mod Wheel (CC1) Source** | Both `[S p.211]`, `[V p.23]` | `modNN.source` = `27` (`mod_wheel`, unipolar) | **HAVE** |
| **Random per Note Source** (`Random` in Vital; `Note-On Rand 1`, `Note-On Rand 2`, `Note-On Rand (Discrete)` in Serum) | Both `[S p.210]`, `[V p.23]` | `modNN.source` = `28` (`random`, unipolar) | **HAVE** (`random`); **GAP** (`Note-On Rand 2` & `Note-On Rand (Discrete)`) |
| **Release Velocity / Lift Source** | Both `[S p.210]`, `[V p.23]` | `GAP` (`docs/PARAMETERS.md` l.981) | **GAP** |
| **Note in Octave (`Oct Note`) Source** | Vital `[V p.23]` | `GAP` (`docs/PARAMETERS.md` l.981) | **GAP** |
| **MPE / Expression X, Y (Slide), Z (Pressure) & Poly Aftertouch Sources** | Both `[S p.210-211]`, `[V p.23]` | `GAP` (`aftertouch` is channel pressure; per-note MPE X/Y/Z & Poly AT are `GAP`) | **GAP** |
| **Static Stereo Source** (Vital `Stereo` 4x2 source tile) | Vital `[V p.23]` | `modNN.stereo` (`0 Off, 1 On` per matrix slot) | **HAVE** (implemented per-slot via `modNN.stereo`) |
| **Oscillators & Filters as Audio-Rate Mod Sources** (drag `OSC A/B/C/SUB/NOISE` or `FILTER 1/2` header to any knob, or pick in Matrix `SOURCE`) | Serum `[S p.208, 210]` | `GAP` in Matrix (`oscN.warp.type` has `FM/RM from Osc`, but general audio-rate mod of any knob is `GAP`) | **GAP** |
| **Active Voices, Note-On Alt 1/2, Voice Index, Voice Mod 1/2, Fixed Mod Sources** | Serum `[S p.210-211]` | `GAP` | **GAP** |
| **Direct Modulation Assignment & Depth Ring** (Desktop: drag source tile to knob, hover audition `[V p.16]`, `+` badge & destination counter `[S p.194]`; Mobile (`00-BRIEF.md` #3): tap source chip -> drag knob up/down -> purple ring shows depth; positive vs negative color `[S p.196]`) | Both `[S p.187, 194-196]`, `[V p.16]` | Allocates first free `modNN` slot (`mod01..mod64`) and sets `modNN.source`, `modNN.destination`, `modNN.amount` (`-100..100 %`) | **HAVE** |
| **Limit Mod Depth on Drop (based on knob value)** (clamps initial positive mod depth so `knob + depth <= 100%`) | Serum `[S p.314]` | UI preference applied when creating a new `modNN` assignment | **HAVE** (UI-side clamping option) |
| **Auto & Manual Polarity on Assignment (Unipolar / Bipolar)** (Serum auto-sets bipolar on centered knobs like Pan/Detune and unipolar on non-centered knobs; `Shift-Opt/Alt-click` knob or Matrix `POL` switch toggles) | Both `[S p.196, 211]`, `[V p.17, 24-25]` | `modNN.bipolar` (`0 Off, 1 On`) | **HAVE** |
| **Source Tile Context Menu** (Right-click / long-press source tab to list assigned destinations, bypass all/individual, or remove all/individual assignments) | Both `[S p.187, 195, 200]`, `[V p.17]` | Updates `modNN.bypass` or clears `modNN.source`/`destination` matching that source | **HAVE** |
| **Control Context Menu (Long-Press Knob Menu)** (`Mod Source` submenu, `Aux Source` submenu, `Edit Custom Curve`, `Bypass Modulator`, `Remove Modulator`, `Remove All Modulators`, `Reset Control`, `MIDI Learn`, `Remove MIDI CC`, `Lock Parameter`) | Both `[S p.26, 200-201]`, `[V p.4, 16-17]` | Reads/writes `modNN.*`, resets param to `ParamInfo::def`, host/shell MIDI CC map, UI parameter lock | **HAVE** (core mod & reset actions); **GAP** (`Aux Source`, custom remap curve, preset parameter lock) |
| **Modulation Matrix (64 Slots)** (Table of all active routings: `SOURCE`, `CRV`, `AMOUNT`, `POL`, `STEREO`, `DESTINATION`, `OUT` meter, `BYPASS`, `REMOVE`, drag handle reorder, `Opt/Alt-F` expand view) | Both `[S p.209-213]`, `[V p.24-25]` | `mod01..mod64.{source,destination,amount,power,bipolar,stereo,bypass}` (448 keys total) | **HAVE** (all 64 slots x 7 keys = 448 params) |
| **Slot Curve (`CRV` / Power Morph vs Editable Remap Curve & Slew)** (`modNN.power` `-10..10` bends response curve; Serum double-click `CRV` & Vital Matrix bottom panel open free-form multi-point Remap Curve + Serum `RISE`/`FALL` slew limiter) | Both `[S p.211]`, `[V p.25-26]` | `modNN.power` (`-10..10`, default `0`) | **HAVE** (`modNN.power` curve); **GAP** (free-form per-slot multi-point remap graph & `RISE`/`FALL` slew) |
| **Matrix Secondary / Aux Source (`AUX SOURCE`, `INV`, `AUX CRV`, `OUTPUT` scalar)** (multiplies primary source by a second modulator with optional inversion, curve, and final output trim) | Serum `[S p.211-212]` | `GAP` (workaround in `[S p.206]`: modulate `macro.1..4` with source A and use `macro1..4` as source B) | **GAP** |
| **Matrix Operations Menu** (`Sort by Source`, `Sort by Destination`, `Lock Matrix`, `Create Vibrato (Unused LFO->Pitch via Wheel)`, `Create Velo->Amp Assignment`, `Apply and Delete Macros`, `Init Modulations`, `Init LFOs + LFO Mods`) | Serum `[S p.214, 324]` | UI batch operations over `mod01..mod64.*` and target parameter values | **HAVE** (all executable in UI over existing `ParamInfo` table; `Create Vibrato` uses `master.tune` + `macro` or direct `lfoN -> master.tune` if `Aux Source` is `GAP`) |

---

## 2. Screen design

### 2.1 Global Touch Modulation Bar & Knob Assignment (`00-BRIEF.md` #3, #4)

On phone portrait, the **Macro & Mod Strip** sits immediately above the bottom performance/keyboard strip and is visible on **every** synth page (`OSC`, `WT`, `FILT`, `MOD`, `FX`, `GLOBAL`). No desktop drag-and-drop across screens is ever required.

```
+-------------------------------------------------------------------+
| [<] Preset: Acid Saw Reso [>]        [16/16v] [CPU 18%] [Menu v]  |
+-------------------------------------------------------------------+
| MACRO BAR (always visible, tap knob to edit, tap chip to assign)  |
|  ( M1 )       ( M2 )       ( M3 )       ( M4 )      [+MOD CHIP v] |
|  Cutoff       Reso         EnvMod       Space       [LFO 1 (3) x] |
|  [M1: 2]      [M2: 1]      [M3: 1]      [M4: 2]                   |
+-------------------------------------------------------------------+
| ACTIVE PAGE CONTENT (e.g. Filter 1)                               |
|   +-----------------------------------------------------------+   |
|   |  FILTER 1 RESPONSE CURVE (drag node = Cutoff / Reso)      |   |
|   |        ______                                             |   |
|   |       /      \    <-- Live animated cyan modulation span  |   |
|   |                  \                                        |   |
|   +-----------------------------------------------------------+   |
|                                                                   |
|   (( CUTOFF ))      (( RESO ))       (( DRIVE ))     (( BLEND ))  |
|    1.20 kHz          45.0 %            6.0 dB          0.0 %      |
|   [Purple Ring]     [Gray Ring]                                   |
|   [+42% BIP ST]                                                   |
+-------------------------------------------------------------------+
| MOD SOURCE DRAWER (when Mod Assign Mode is active):               |
| [ENV 1][ENV 2*][ENV 3][LFO 1*][LFO 2][RND 1][VEL][NOTE][AT][MW]   |
+-------------------------------------------------------------------+
| [OSC]   [WT]   [FILT]   [ENV/LFO]   [MATRIX]   [FX]   [GLOBAL]    |
+-------------------------------------------------------------------+
| [PB] [MW] | C2 | D2 | E2 | F2 | G2 | A2 | B2 | C3 | D3 | E3 | F3  |
+-------------------------------------------------------------------+
```

#### How Touch Modulation Assignment Works (`00-BRIEF.md` #3)
1. **Enter Mod Assign Mode:** Tap any source chip (`[M1]..[M4]`, `[ENV 1]..[ENV 6]`, `[LFO 1]..[LFO 8]`, `[RND 1]..[RND 4]`, `[VEL]`, `[NOTE]`, `[AT]`, `[PB]`, `[MW]`, `[RND]`) in the Mod Source bar/drawer.
   - The tapped source chip highlights in **bright purple** (`#A855F7`), and all 269 modulatable knobs across all pages show a subtle purple outer track ring indicating valid targets (`[S p.187, 194]`). Non-modulatable stepped controls dim to 50% opacity.
2. **Assign & Set Depth:** Drag any modulatable knob vertically up or down.
   - If no slot connects `(activeSource, knob)` yet, the UI claims the first empty slot in `mod01..mod64` (`source == 0` or `destination == 0`).
   - Auto-polarity (`[S p.196]`): if the target parameter's default is centered (`oscN.pan`, `oscN.tune`, `oscN.transpose`, `master.tune`, `lfoN.stereo`), `modNN.bipolar` defaults to `1 (On)`; otherwise `0 (Off)`.
   - If the global preference **Limit Mod Depth on Drop** `[S p.314]` is enabled, dragging upward clamps positive depth to `(max - currentVal) / range` unless the user drags past a detent at the top of the arc.
   - **Visual feedback:** A **bright purple arc** (`#A855F7`) around the knob shows positive `modNN.amount` (`0..+100%`), while a **lighter cyan-purple arc** (`#38BDF8` / `#C084FC`) shows negative `modNN.amount` (`-100..0%`) `[S p.196]`. During note playback, a live moving dot/line inside the arc shows real-time modulated value `[V p.16]`.
   - **Quick Slot Pill under Knob:** While a source chip is active and connected to a knob, a compact floating pill appears directly below that knob: `[+42% | BIP | ST | CRV | x]`. Tapping `BIP` toggles `modNN.bipolar`, `ST` toggles `modNN.stereo`, `CRV` opens the curve popover (`modNN.power`), and `x` removes the slot.
3. **Exit Mod Assign Mode:** Tap the active source chip again (or tap `[Done]`). Inactive modulations on a knob show as thin **gray outer arcs** (`[S p.196]`) and small source-dot badges (`o`) at the top-left of the knob; tapping a badge selects that source.

---

### 2.2 Long-Press Knob Context Sheet (`[S p.26, 200-201]`, `[V p.4, 16-17]`)

Long-pressing (>= 400 ms) or tapping the `[...]` badge on any knob opens the **Parameter & Modulation Sheet**:

```
+-------------------------------------------------------------------+
|  Filter 1 Cutoff (`filter1.cutoff`)                        [Close]|
|  Current Value: [ 1200.0 Hz ]   [Reset to 12000 Hz]  [Lock Param] |
+-------------------------------------------------------------------+
|  ACTIVE MODULATORS (2 of 64 slots)                     [Clear All]|
|  +-------------------------------------------------------------+  |
|  | 1. [LFO 1 v]  --->  Amount: [====|==== +42.0% ]             |  |
|  |    [Bipolar: ON] [Stereo: OFF] [Curve: +2.0] [Byp] [Remove] |  |
|  |    Aux Source: [None (GAP) v]  [Edit Remap Curve (GAP)]     |  |
|  +-------------------------------------------------------------+  |
|  | 2. [Env 2 v]  --->  Amount: [====|====== +65.0% ]           |  |
|  |    [Bipolar: OFF] [Stereo: OFF] [Curve:  0.0] [Byp] [Remove]|  |
|  +-------------------------------------------------------------+  |
|  [+ Add Mod Source]   [+ Add Aux via Macro Workaround]            |
+-------------------------------------------------------------------+
|  HARDWARE & MIDI                                                  |
|  [MIDI Learn (Waiting...)]     [Remove MIDI CC (CC #74)]          |
+-------------------------------------------------------------------+
```

---

### 2.3 Modulation Matrix Page (Phone Portrait) (`[S p.209-214]`, `[V p.24-26]`)

Tapping the `[MATRIX]` page tab displays the 64-slot Modulation Matrix, plus sub-tabs for **Matrix List**, **Mod Curves**, **Vel / Note Graphs (`GAP`)**, and **Macros**:

```
+-------------------------------------------------------------------+
| MATRIX (4 / 64 Active)     [Filter: Active v] [Search] [Ops Menu] |
| Sub-tabs: [ Matrix List ] [ Remap Curve ] [ Vel/Note ] [ Macros ] |
+-------------------------------------------------------------------+
| +---------------------------------------------------------------+ |
| | #01 [Byp:O]  [Env 2      v]  -->  [Filter 1 Cutoff v]     [x] | |
| |     Amt: [-----|====== +65.0%]   [UNI] [MONO] [Crv:  0.0]     | |
| |     Out: [:::::::::::::      ]   Aux: [None (GAP)]            | |
| +---------------------------------------------------------------+ |
| +---------------------------------------------------------------+ |
| | #02 [Byp:O]  [LFO 1      v]  -->  [Osc 1 Frame     v]     [x] | |
| |     Amt: [==|--------- -38.5%]   [BIP] [STER] [Crv: -3.5]     | |
| |     Out: [     :::::::       ]   Aux: [Mod Wheel (GAP)]       | |
| +---------------------------------------------------------------+ |
| +---------------------------------------------------------------+ |
| | #03 [Byp:X]  [Macro 1    v]  -->  [Distortion Drive v]    [x] | |
| |     (BYPASSED - dimmed 45%)                                   | |
| |     Amt: [-----|====   +40.0%]   [UNI] [MONO] [Crv:  0.0]     | |
| +---------------------------------------------------------------+ |
| +---------------------------------------------------------------+ |
| | #04 [LOOP!]  [LFO 2      v]  -->  [LFO 1 Rate      v]     [x] | |
| |     [!] Feedback loop with #05 (LFO 1 -> LFO 2 Rate): Unwired | |
| +---------------------------------------------------------------+ |
|                        [ + Add Mod Slot ]                         |
+-------------------------------------------------------------------+
```

#### Matrix Slot Detail & Curve Bottom Sheet (tapping `[Crv]` or `[Remap Curve]` sub-tab)
遵循 Principle #1 (1 big visual + <= 4 main knobs):

```
+-------------------------------------------------------------------+
| SLOT #02 CURVE & DETAILS: LFO 1 -> Osc 1 Frame             [Done] |
+-------------------------------------------------------------------+
|   1.0 +---------------------------------------+                   |
|       |                             .  *      |  Mode:            |
|       |                        .  *           |  [Power Crv (HAVE)|
|   Out |                    . *                |  [Free Draw (GAP)]|
|       |                . *                    |                   |
|       |          .  *                         |  [Bipolar: ON]    |
|   0.0 +------*--------------------------------+  [Stereo:  ON]    |
|      0.0                Input                1.0 [Bypass: OFF]    |
+-------------------------------------------------------------------+
|  (( AMOUNT ))     (( CURVE / PWR ))   (( RISE (GAP) )) (( FALL )) |
|    -38.5 %             -3.5               0.0 ms         0.0 ms   |
|  (`mod02.amount`)  (`mod02.power`)     (Slew Rise)    (Slew Fall) |
+-------------------------------------------------------------------+
|  [More: Aux Source (GAP) | Aux Inv (GAP) | Aux Crv | Out Trim]    |
+-------------------------------------------------------------------+
```

#### Matrix Operations Menu Sheet (`[Ops Menu]` button, `[S p.214, 324]`)
- **Sort by Source** (`[S p.214]`): Reorders active slots in `mod01..mod64` ascending by `modNN.source`.
- **Sort by Destination** (`[S p.214]`): Reorders active slots in `mod01..mod64` ascending by `modNN.destination`.
- **Lock Matrix (Keep Assignments on Preset Change)** (`[S p.214]`): Preserves `mod01..mod64.*` values when loading a new preset.
- **Create Vibrato (Unused LFO -> Pitch via Wheel)** (`[S p.214]`): Allocates the first unused LFO (`lfoN` at 5.5 Hz Sine) to `master.tune` (`+15 ct`, bipolar). Because `Aux Source` is an engine `GAP`, offers two modes: direct `mod_wheel -> lfoN.rate/amount` workaround via Macro, or marks `Aux = Mod Wheel` (`GAP`).
- **Create Velo -> Amp Assignment** (`[S p.214]`): Maps `velocity -> master.volume` (or sets `master.velocity_track = 100%`).
- **Apply and Delete Macros ("Bake Macros")** (`[S p.214]`): For every slot where `modNN.source` is `macro1..macro4`, computes `newBase = constrainValue(dest + macroVal * amount)`, updates the destination parameter, resets `macro.1..4 = 0%`, and clears those matrix slots.
- **Init Modulations / Init LFOs + LFO Mods** (`[S p.324]`): Resets all `mod01..mod64` slots to `None` (and optionally resets `lfo1..8.*` and `rlfo1..4.*` to defaults).

---

### 2.4 Velocity & Note Graph Sub-Tab (`[S p.201-205]` — Engine `GAP`) and LFO Point Mod (`[S p.198-200]` — Engine `GAP`)

```
+-------------------------------------------------------------------+
| VELOCITY / NOTE CURVE EDITOR  [VELO Tab] [NOTE Tab]  [GAP Badge]  |
+-------------------------------------------------------------------+
|   100%+---------------------------------------+                   |
|       |                                  o    | [Init Graph]      |
|       |                         o      /      | [Legato Glide:Off]|
|   Out |                   *          /        |                   |
|       |             o              /          | Drag Chip:        |
|       |       *                  /            | [Assign VELO +]   |
|     0%+-----o------------------+--------------+                   |
|       0    32                 64             127 (MIDI Velocity)  |
+-------------------------------------------------------------------+
|  (( VEL TRACK ))   (( SLOT CURVE ))   (( GLIDE TIME )) (( SCALE ))|
|     100.0 %             0.0               0.0 ms         100 %    |
| (`master.velocity_track`) (`modNN.power`)   (GAP)        (GAP)    |
+-------------------------------------------------------------------+
```

* **LFO Point Modulation (`[S p.198-200]`, Engine `GAP`):** In the LFO Editor (`05-envelopes-lfos.md`), long-pressing any LFO node opens a popover: `[Modulate X (GAP)] [Modulate Y (GAP)] -> [LFO Bus 1..8] -> [Select Mod Source]`, drawing a shaded horizontal/vertical range bar around the node.

---

### 2.5 Tablet & MPC Key 61 Landscape Layout (`00-BRIEF.md` #6)

On tablet and MPC Key 61 landscape, the **Matrix List** (left 60%) and the **Slot Curve / VELO-NOTE / Macro Inspector** (right 40%) sit side by side above the persistent Mod Source Drawer:

```
+---------------------------------------------------------------------------------------+
| Top Bar: Preset | 4 Macros (M1..M4) | Voices 4/16 | Tier: High | [OSC][FILT][MOD][FX] |
+---------------------------------------------------+-----------------------------------+
| MODULATION MATRIX (Slots 01..64)   [Sort][Lock][+]| SLOT #02 INSPECTOR & CURVE        |
| # | Byp | Source   | Curve | Amount  | Pol|St|Dst | +-------------------------------+ |
| 01| [O] | Env 2    |  0.0  | +65.0%  | UNI|M |Cut1| |       . *                     | |
| 02| [O] | LFO 1    | -3.5  | -38.5%  | BIP|S |Frm1| |   . *     (Power Curve -3.5)  | |
| 03| [X] | Macro 1  |  0.0  | +40.0%  | UNI|M |Drv | +-------------------------------+ |
| 04| [!] | LFO 2    |  0.0  | +20.0%  | BIP|M |Rate| ((Amt)) ((Crv)) ((Rise)) ((Fall)) |
|   |     |          |       |         |    |  |    | Aux Source (GAP): [None v] [Inv]  |
+---------------------------------------------------+-----------------------------------+
| SOURCE CHIPS: [ENV 1..6] | [LFO 1..8] | [RND 1..4] | [M1..M4] | [VEL][NOTE][AT][PB][MW]|
+---------------------------------------------------------------------------------------+
```

---

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| **Mod Source Chip** (`ENV 1..6`, `LFO 1..8`, `RND 1..4`, `M1..M4`, `VEL`, `NOTE`, `AT`, `PB`, `MW`, `RND`) | Single tap | Toggles **Mod Assign Mode** for that source (purple highlight; modulatable knobs show target rings) | Long-press target knob -> `Mod Source` submenu -> pick source `[S p.200]` |
| **Mod Source Chip** | Long-press | Opens Source Assignments Sheet listing all destinations of that source with `Bypass` and `Remove` buttons `[S p.187, 195; V p.17]` | Go to `MATRIX` page -> filter by `Source` |
| **Modulatable Knob (in Mod Assign Mode)** | Vertical drag up/down | Creates/updates `modNN` slot and sets `modNN.amount` (`-100..+100%`); purple arc grows clockwise (pos) or counter-clockwise (neg) `[S p.195-196]` | In `MATRIX` page, tap `[+ Add Mod Slot]` and set `modNN.amount` slider |
| **Modulatable Knob (Normal Mode)** | Long-press (>= 400 ms) | Opens **Parameter & Modulation Context Sheet** (exact value entry, active modulators list, reset, MIDI learn, lock) `[S p.26, 200-201]` | Tap `[...]` button in panel header -> select parameter |
| **Mod Depth Halo Badge (`o` top-left of knob)** | Tap badge, then drag vertically | Selects that specific assigned modulator and adjusts its `modNN.amount` `[S p.195-196]` | Adjust `modNN.amount` in `MATRIX` row or Long-Press Knob Sheet |
| **Quick Slot Pill (`[BIP | ST | CRV | x]`)** | Tap `BIP` / `ST` / `CRV` / `x` | Toggles `modNN.bipolar` (`[S p.196, 351]`), `modNN.stereo` (`[V p.17]`), opens curve popover (`modNN.power`), or disconnects slot | Toggle switches in `MATRIX` row |
| **Macro Knob (`macro.1..4`)** | Vertical drag / double-tap | Adjusts `macro.1..4` (`0..100%`) / resets to `0%` (`[S p.206; V p.23]`) | Numeric entry in Long-Press Sheet or Kick hardware knob 1..4 |
| **Macro Chip Swap** | Drag `M1..M4` header onto another `M1..M4` | Swaps all matrix assignments and values between the two macros `[S p.208]` | `MATRIX` -> `[Ops Menu]` -> `Swap Macros...` dialog |
| **Matrix Row Handle (`=`)** | Drag row up/down | Reorders slot `modAA` and `modBB` in the 64-slot table `[S p.213]` | Row menu `[Move Up]` / `[Move Down]` buttons |
| **Matrix Curve Graph (`modNN.power`)** | Vertical drag on curve line | Adjusts `modNN.power` (`-10..+10`, `0` = linear) `[S p.211; V p.25]` | Turn `(( CURVE ))` knob or type numeric value |
| **Matrix Row Bypass / Remove** | Tap `[Byp]` / Tap `[x]` | Toggles `modNN.bypass` (`0/1`) / clears `modNN.source = 0, destination = 0, amount = 0` `[S p.213; V p.24]` | Long-press knob -> `Bypass Modulator` / `Remove Modulator` |

---

## 4. States and edge cases

1. **Modulation Scale & Range Clamping (`docs/PARAMETERS.md`, `core/engine-vital/README.md`):**
   - `modNN.amount` (`-100..100%`, default `0%`, step `0.1%`) represents a fraction of the destination parameter's full range **in the engine's internal scale** (e.g., semitones across the keyboard range for filter cutoffs, fourth-root seconds for envelope times, log2 for LFO rates). The purple ring on the knob displays the true effective sweep clamped to the knob's `[min, max]` arc.
2. **Feedback Loop Prevention (`LOOP!` State, `docs/PARAMETERS.md` l.48, `core/engine-vital/README.md` l.72):**
   - If wiring a slot `modNN` would close a cycle in the modulation dependency graph (e.g., `mod01: LFO 1 -> LFO 2 Rate` and `mod02: LFO 2 -> LFO 1 Rate`), the audio thread leaves the cycle-closing slot **unwired** to guarantee zero audio-thread allocation.
   - **UI State:** The UI runs the same topological cycle check on the message thread whenever `modNN.source` or `modNN.destination` changes. Any unwired cycle-closing row displays an amber `[LOOP!]` badge and tooltip: *"Feedback loop with Slot #NN — unwired by engine."*
3. **Slot Exhaustion (All 64 Slots Full):**
   - When all 64 slots (`mod01..mod64`) have `source != 0` and `destination != 0`, entering Mod Assign Mode shows a toast banner: *"Mod Matrix full (64/64 slots used). Tap to open Matrix and free a slot."*
4. **Bypassed Slot vs Zero Amount vs Unassigned Slot:**
   - **Unassigned (`source == 0` or `destination == 0`):** Hidden under the default `Filter: Active` Matrix view; shown as an empty row under `Filter: All 64`. Produces exact bit-identical output to an unwired patch (`docs/QUALITY.md` item e).
   - **Bypassed (`modNN.bypass == 1`):** Row dims to 45% opacity, bypass icon turns red (`[S p.212-213]`), and knob ring renders as a dashed gray arc.
   - **Zero Amount (`modNN.amount == 0%`):** Active slot remains wired so automating `modNN.amount` is click-free; badge shows `0.0%`.
5. **Polarity (`modNN.bipolar`) & Intrinsic Source Polarity (`docs/PARAMETERS.md` l.645-675):**
   - `lfo1..8`, `rlfo1..4`, and `pitch_wheel` are intrinsically bipolar in `modSourceTable`, whereas `env1..6`, `macro1..4`, `velocity`, `note`, `aftertouch`, `mod_wheel`, and `random` are unipolar (`0..1`). Turning `modNN.bipolar = 1` centers a `0..1` source around `0` (`-0.5..+0.5` x `2`), moving the destination symmetrically above and below its knob setting `[V p.25]`.
6. **Stereo Modulation (`modNN.stereo = 1`):**
   - Inverts the modulation amount in the right audio channel (`L = +amount`, `R = -amount`; measured L/R envelope correlation `-0.994`, `docs/QUALITY.md` item e). Shown with a split `[L|R]` stereo badge on the knob ring and matrix row.
7. **Own Engine (`core/engine-own`, iOS build) Compatibility:**
   - `macro.1..4` and `mod01..mod64.*` have `ownEngine: false` in `docs/params.json` (stored/saved in presets and state v2 on all builds, acted on by `engine-vital`). On iOS (`engine-own`), modulated parameters that are not yet wired in `engine-own` display a subtle `[iOS: Static]` info indicator in the Matrix header.
8. **Engine GAP Visual Treatment:**
   - Controls for `Aux Source`, `Free-Form Remap Curve`, `Slew Rise/Fall`, `LFO Point Mod`, `VELO/NOTE Custom Graph`, and `Audio-Rate Osc/Filter Sources` are rendered in the UI with a `[GAP]` pill badge so sound designers and developers see the complete Serum 2 / Vital design without dead silent controls.

---

## 5. Kick remote pages

Each page exposes at most 8 parameters from `docs/params.json` (`00-BRIEF.md` #7). Full schema and all 880-key coverage are in `09-kick-integration.md`.

| Page ID | Page Name | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Knob 5 | Knob 6 | Knob 7 | Knob 8 |
|---------|-----------|--------|--------|--------|--------|--------|--------|--------|--------|
| `macros` | **Macros & Master** | `macro.1` | `macro.2` | `macro.3` | `macro.4` | `master.volume` | `master.tune` | `master.transpose` | `master.portamento.time` |
| `mod_slot_1` | **Mod Matrix Slot 1** | `mod01.source` | `mod01.destination` | `mod01.amount` | `mod01.power` | `mod01.bipolar` | `mod01.stereo` | `mod01.bypass` | — |
| `mod_slot_2` | **Mod Matrix Slot 2** | `mod02.source` | `mod02.destination` | `mod02.amount` | `mod02.power` | `mod02.bipolar` | `mod02.stereo` | `mod02.bypass` | — |
| `mod_slot_3` | **Mod Matrix Slot 3** | `mod03.source` | `mod03.destination` | `mod03.amount` | `mod03.power` | `mod03.bipolar` | `mod03.stereo` | `mod03.bypass` | — |
| `mod_slot_4` | **Mod Matrix Slot 4** | `mod04.source` | `mod04.destination` | `mod04.amount` | `mod04.power` | `mod04.bipolar` | `mod04.stereo` | `mod04.bypass` | — |
| `mod_amounts_1_8` | **Mod Amounts 1–8** | `mod01.amount` | `mod02.amount` | `mod03.amount` | `mod04.amount` | `mod05.amount` | `mod06.amount` | `mod07.amount` | `mod08.amount` |
| `mod_amounts_9_16` | **Mod Amounts 9–16** | `mod09.amount` | `mod10.amount` | `mod11.amount` | `mod12.amount` | `mod13.amount` | `mod14.amount` | `mod15.amount` | `mod16.amount` |

*(Slots `mod05`..`mod64` follow the exact 7-knob template `mod_slot_NN` or are edited in the Kick Matrix view; see `09-kick-integration.md`.)*

---

## 6. Coverage check

Every parameter group in `docs/params.json` belonging to Modulation & Macros is accounted for in Section 2:

| Parameter Group (`docs/params.json`) | Key Pattern | Count | Where it appears in Section 2 |
|--------------------------------------|-------------|-------|-------------------------------|
| **Macros** | `macro.1` .. `macro.4` | 4 | §2.1 Always-visible Macro Bar (`M1..M4` knobs + source chips) & §2.3 `[Macros]` sub-tab |
| **Mod Matrix (`Slot 01` .. `Slot 64`)** | `mod01.source` .. `mod64.source` | 64 | §2.1 Touch Mod Assign (auto-sets source), §2.2 Long-Press Knob Sheet, §2.3 Matrix List row source picker |
| **Mod Matrix (`Slot 01` .. `Slot 64`)** | `mod01.destination` .. `mod64.destination` | 64 | §2.1 Touch Mod Assign (auto-sets target knob), §2.3 Matrix List row destination picker |
| **Mod Matrix (`Slot 01` .. `Slot 64`)** | `mod01.amount` .. `mod64.amount` | 64 | §2.1 Knob purple depth ring & drag gesture, §2.2 Long-Press Knob Sheet slider, §2.3 Matrix List slider & Curve Sheet `(( AMOUNT ))` knob |
| **Mod Matrix (`Slot 01` .. `Slot 64`)** | `mod01.power` .. `mod64.power` | 64 | §2.1 Quick Slot Pill `[CRV]`, §2.2 Long-Press Knob Sheet, §2.3 Matrix List `[Crv]` & Slot Curve Sheet `(( CURVE / PWR ))` knob |
| **Mod Matrix (`Slot 01` .. `Slot 64`)** | `mod01.bipolar` .. `mod64.bipolar` | 64 | §2.1 Quick Slot Pill `[BIP]`, §2.2 Long-Press Knob Sheet, §2.3 Matrix List `[UNI/BIP]` toggle & Curve Sheet |
| **Mod Matrix (`Slot 01` .. `Slot 64`)** | `mod01.stereo` .. `mod64.stereo` | 64 | §2.1 Quick Slot Pill `[ST]`, §2.2 Long-Press Knob Sheet, §2.3 Matrix List `[MONO/STER]` toggle & Curve Sheet |
| **Mod Matrix (`Slot 01` .. `Slot 64`)** | `mod01.bypass` .. `mod64.bypass` | 64 | §2.2 Long-Press Knob Sheet `[Byp]`, §2.3 Matrix List `[Byp:O/X]` toggle & Source Chip Sheet |
| **Modulation Source Generators** *(detailed in `05-envelopes-lfos.md`)* | `env1..6.*` (54), `lfo1..8.*` (104), `rlfo1..4.*` (32) | (190) | §2.1 Mod Source Drawer chips (`[ENV 1..6]`, `[LFO 1..8]`, `[RND 1..4]`); full editors in `05-envelopes-lfos.md` |
| **Total covered directly in `06-modulation.md`** | `macro.1..4` + `mod01..mod64.*` | **452** | 100% of Macros (4) and Mod Matrix (448) parameters |

---

## 7. Open questions (owner)

1. **Sequencer exclusions (`00-BRIEF.md`):** Serum 2 **Clips** `[S p.219-243]` and **Arpeggiator** `[S p.244-266]` are excluded because Kick provides sequencing/arranging. Confirm that no internal Arp/Clip player is desired inside SynthyBoy.
2. **Macro count (4 vs 8):** Our parameter table has 4 macros (`macro.1..4`, matching Vital `[V p.23]`), whereas Serum 2 has 8 macros (`MACRO 1..8` `[S p.206]`). Keep 4 macros for mobile screen clarity, or append `macro.5..8` in a future `ParamInfo` revision?
3. **Matrix Aux Source (`modNN.aux_source`, `aux_inv`, `aux_power`):** Serum 2's per-slot `AUX SOURCE` `[S p.211]` is standard for Mod Wheel scaling of vibrato (`LFO -> Pitch x ModWheel`). Should we add `modNN.aux_source` to `ParamInfo` & `vital_bindings.cpp`, or rely on the Macro-as-destination workaround (`[S p.206]`)?
4. **Additional Mod Sources (`Lift`, `Slide / MPE Y`, `Oct Note`, `Poly Aftertouch`):** `sbvital` already has internal processors for `lift`, `slide`, and `note_in_octave` (`docs/PARAMETERS.md` l.981). Should we expose them in `modSourceTable` alongside MPE support?
5. **Free-form Per-Slot Remap Curve & VELO/NOTE Graphs (`[S p.201-205, 211]`, `[V p.25-26]`):** Should custom multi-point remap curves be added to state JSON (like wavetables/LFO shapes), or is `modNN.power` (`-10..10`) sufficient for mobile/MPC targets?
6. **LFO Point Modulation (`[S p.198-200]`) & Audio-Rate Osc/Filter Mod Sources (`[S p.208]`):** Both are Serum 2 features not present in Vital's architecture. Confirm these remain `GAP` (out of DSP scope).
