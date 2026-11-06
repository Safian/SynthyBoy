# Voice, MIDI & Global Settings – mobile UI design

Covers Master output, Voicing, Portamento, Keyboard/Pitch/Velocity settings, Oscillator Key/Velocity Mapping, MPE, MIDI CC mapping, Microtonal Tuning, Per-Voice Control, Quality Tiers, and Global Preferences (`docs/PARAMETERS.md`, `core/include/synthyboy/quality.h`, Serum 2 `[S p.215-218, 267-273, 312-353]`, Vital `[V p.6-7, 39-43]`).

* **Out-of-scope exclusions (`00-BRIEF.md`):**
  * **Preset Browser** (`[S p.327-339]`, `[V p.3-4]`): excluded: Kick.
  * **Clips** (`[S p.219-243]`): excluded: Kick.
  * **Arpeggiator** (`[S p.244-266]`): excluded: Kick.

---

## 1. Function inventory

| Function | Origin (Vital / Serum / both) | Our params (keys) | Engine status (HAVE / GAP) |
|----------|-------------------------------|-------------------|----------------------------|
| **Master Volume & Stereo Level Meter** (`MAIN` volume knob + stereo peak/clip meter) | Both `[S p.27, 151]`, `[V p.3]` | `master.volume` (`-60..6 dB`, skew `2.5`, def `-12 dB`, `mod`, `ownEngine: true`) | **HAVE** |
| **Master Transpose (Semitones)** (shifts all incoming MIDI notes; `-24..+24 st` on Serum keyboard bar, `-48..+48 st` in Vital/SynthyBoy) | Both `[S p.25, 267]`, `[V p.43]` | `master.transpose` (`-48..48 st`, step `1`, def `0 st`, `ownEngine: true`) | **HAVE** |
| **Master Fine Tune (Cents)** (global fine pitch offset `-100..+100 ct`) | Vital `[V p.43]` | `master.tune` (`-100..100 ct`, def `0 ct`, `mod`, `ownEngine: true`) | **HAVE** |
| **Polyphony (`POLY` / `Voices`)** (max simultaneous notes `1..32`; capped by `Quality::maxVoices`: Low `8`, Med `16`, High `32`) | Both `[S p.216]`, `[V p.39]` | `master.polyphony` (`1..32 voices`, step `1`, def `16 voices`) | **HAVE** |
| **Mono Mode Switch (`MONO`)** (restricts synth to 1 active note; new note re-pitches active voice) | Serum `[S p.216]` (Vital uses `Voices = 1` `[V p.39]`) | `master.polyphony` = `1` (UI `MONO` toggle sets `master.polyphony = 1` and remembers previous poly count) | **HAVE** (via `master.polyphony = 1`) |
| **Legato Switch (`LEGATO`)** (in Mono mode, overlapping notes do not retrigger envelopes/LFOs; in Serum with `MONO` off, behaves paraphonically for shared filter/FX envelopes) | Both `[S p.216]`, `[V p.39]` | `master.legato` (`0 Off, 1 On`, def `Off`) | **HAVE** (mono legato); **GAP** (paraphonic poly legato mode) |
| **Active Voice Count Readout (`0 / 16`)** (shows sounding voices vs allowed voices; Serum also totals active unison + granular voices) | Serum `[S p.216]` (`IEngine::activeVoices()` in `engine.h`) | Read-only from `IEngine::activeVoices()` vs `min(master.polyphony, Quality::maxVoices)` | **HAVE** |
| **Limit Same Note Poly to 1** (right-click `POLY`: re-pressing an already sounding MIDI note re-triggers/releases that voice instead of stacking duplicates) | Serum `[S p.217]` | Built-in engine behavior (`core/engine-vital/README.md` l.79: a still-ringing note of the same key is always released first); toggle to allow stacking is `GAP` | **HAVE** (always on; switchable toggle is **GAP**) |
| **Voice Steal Priority (`Newest`, `Oldest`, `Highest`, `Lowest`, `Velocity` / `Round Robin`)** | Both `[S p.217]`, `[V p.43]` | `GAP` (`docs/PARAMETERS.md` l.983: engine steals `Oldest`) | **GAP** |
| **Portamento Time (`PORTA` / `Glide`)** (glide duration between notes) | Both `[S p.217-218]`, `[V p.39]` | `master.portamento.time` (`0..8000 ms`, skew `0.3`, def `0 ms`) | **HAVE** |
| **Portamento Curve / Slope (`CURVE` / `Slope`)** (convex = fast start / slow arrival; concave = slow start / fast arrival) | Both `[S p.218]`, `[V p.39]` | `master.portamento.slope` (`-8..8`, def `0`) | **HAVE** |
| **Portamento Always (`ALWAYS` / `Always Glide`)** (when on, always glides from last note even after note-off; when off, glides only on overlapping notes) | Both `[S p.218]`, `[V p.39]` | `master.portamento.always` (`0 Off, 1 On`, def `Off`) | **HAVE** |
| **Portamento Scaled by Interval (`SCALED` / `Octave Scale`)** (scales glide time proportionally to semitone distance, 1 octave = set time) | Both `[S p.218]`, `[V p.39]` | `master.portamento.scaled` (`0 Off, 1 On`, def `Off`) | **HAVE** |
| **Pitch Bend Range (`Bend`)** (semitone range for pitch wheel; Serum has separate Up/Down on main screen + MPE Bend Range `1..96` in menu) | Both `[S p.326]`, `[V p.39]` | `master.bend_range` (`0..24 st`, step `1`, def `2 st`, `ownEngine: true`) | **HAVE** (symmetric `0..24 st`); **GAP** (separate Up/Down bend & MPE `1..96 st`) |
| **Master Velocity Tracking (`Vel Trk`)** (scales amplitude sensitivity to note-on velocity `-100..100%`) | Vital `[V p.39]` | `master.velocity_track` (`-100..100 %`, def `100 %`) | **HAVE** |
| **Global Stereo Spread & Stereo Rotate** (sums output to mono or rotates L/R stereo field) | Vital `[V p.39]` | `GAP` (`docs/PARAMETERS.md` l.980) | **GAP** |
| **Key & Scale Quantizer (`KEY`, `SCALE` / Vital `Global Snap` & `Transpose Snap`)** (conforms incoming MIDI notes or oscillator transpose to a musical key & scale) | Both `[S p.268]`, `[V p.6-7]` | `GAP` (`docs/PARAMETERS.md` l.976) | **GAP** |
| **Global Swing & Swing Division (`SWING`)** (adds shuffle/groove to tempo-synced LFOs, Arp, Clips) | Serum `[S p.269, 192]` | `GAP` (host/Kick handles sequence swing; LFO swing is `GAP`) | **GAP** |
| **Oscillator Mapping Editor: Key Ranges (`KEY`, `FOLD`, `WARP`)** (per-oscillator low/high note bounds `0..127` for splits/layers; `FOLD` wraps out-of-range notes into active octave; `WARP` keeps modulator osc active for FM/RM outside its key range) | Serum `[S p.269-272]` | `GAP` | **GAP** |
| **Oscillator Mapping Editor: Velocity Ranges (`VEL`)** (per-oscillator low/high velocity bounds `1..127` for velocity-switched layers) | Serum `[S p.272-273]` | `GAP` | **GAP** |
| **Oscillator Note Tracking Switch** (disables MIDI pitch tracking so oscillator plays fixed C3 + transpose/tune) | Vital `[V p.40]` | `GAP` (`docs/PARAMETERS.md` l.976) | **GAP** |
| **Sustain Pedal (CC64)** (holds notes while pedal is depressed) | Both (`docs/QUALITY.md` l.100) | `GAP` (`sbvital` engine supports it, not yet exposed in `synthyboy::Event`) | **GAP** |
| **Global Preferences: UI Options** (`Help Tooltips`, `Param value tooltips`, `Double-click params` Reset vs Text box, `Mouse wheel param control`, `Keyboard shortcuts`, `Default waveform view` 2D/3D, Vital `Frequency Units` Hz/Semitones, UI Zoom size) | Both `[S p.36, 313-314]`, `[V p.3, 42]` | App/UI preferences JSON (not DSP parameters) | **HAVE** (UI settings state) |
| **Global Preferences: MPE & Note Expression** (`MPE Enabled`, `MPE enabled by default`, `MPE Pitch Bend (also) maps to Expr X`, `MPE Expr Y acts bi-directional`, `MPE: XYZ -> Macro 1,2,3`, `MPE: YZ -> Macro 1,2`, `MPE: Y -> Mod Wheel`, `MPE Bend Range: 1..96`) | Both `[S p.314, 325-326]`, `[V p.23]` | `GAP` in `synthyboy::Event` (CLAP note-port MPE planned in `docs/ARCHITECTURE.md` §4.1; quick Macro mappings can be wired by host/shell) | **GAP** |
| **Global Preferences: Modulation & Audio Behavior** (`Limit Mod depth on drop`, `Mod Wheel -> WT Pos when WT Editor open`, `Silence note + FX tails when host transport stops`, `Use Ultra quality when rendering`) | Serum `[S p.314-315]` | UI preferences + `IEngine::reset()` on transport stop + `clap.render` -> `Tier::high` (`docs/ARCHITECTURE.md` §4.1) | **HAVE** |
| **Global Preferences: MIDI CC Maps & Rocker CC/Note Preset Switching** (`MIDI Learn`, `Remove MIDI CC`, `Load/Save MIDI Map`, `Load MIDI Map from Presets`, `Serum2Prefs.json` `Enable CCForRockers` + `CCForRocker Preset +/-` & `OSC A/B/C/N +/-`) | Serum `[S p.26-27, 315, 325, 341-343]` | Shell / Kick MIDI learn table (stored in host/shell config) | **HAVE** (shell/UI layer) |
| **Global Voice Control (Per-Voice Sequencer, Randomization & Env/LFO Scaling)** (Target `OSC A/B/C`, Preset load/save/init, `SEQ` length `1..8` per-voice pitch/pan/WT/mod1/mod2, Per-voice `RANDOM` for `PAN`, `DETUNE`, `CUTOFF`, `ENVS`, and global `SCALING` for all Envelopes and all LFOs) | Serum `[S p.316-319]` | `GAP` (note: per-oscillator `oscN.random_phase` and `modNN.source = random` exist; per-voice pan/detune/cutoff/env random & global env/LFO time scaling are `GAP`) | **GAP** |
| **Quality Tiers & Oversampling (`Low`, `Medium`, `High` / Serum `Draft 1x`, `High 2x`, `Ultra 4x` / `Lock Quality` / `Disable Smoothing`)** | Both `[S p.319-320, 348]`, `[V p.40, 42]`, `quality.h` | `IEngine::setQuality(Quality)` (`Tier::low`, `Tier::medium`, `Tier::high`: caps `maxVoices`, `maxUnison`, `oversampling`, `interpolation`, `fxMask`) | **HAVE** (`Quality` tiers & lock); **GAP** (`4x` oversampling & `Disable Smoothing` sample-accurate bypass) |
| **Global Oscilloscope & Spectrum Analyzer** (real-time time-domain waveform + FFT spectrum of master output) | Vital `[V p.42]` | UI audio ring-buffer visualizer fed from `IEngine::process` output | **HAVE** (UI meter/scope) |
| **Tuning & Microtonal Settings** (`Concert Pitch` A4 Hz default `440 Hz`, `.tun`/`.scl`/`.kbm` file loader, `Clear Tuning`, built-in `Default 12-TET` / `Pythagorean` / `Just Intonation`, `MTS-ESP` client & `MTS-ESP Note-On Only`, oscillator `Steps` pitch mode, `Lock Tuning`) | Both `[S p.320-321, 325]`, `[V p.4, 43]` | `GAP` (`docs/PARAMETERS.md` l.975: `sbvital::Tuning` exists in core, not exposed in `IEngine`) | **GAP** |
| **Main Menu Utilities & Audio Render/Resample** (`Init Preset`, `Init LFOs + LFO Mods`, `Init Modulations`, `Revert to Saved`, `Save as Default Preset`, `Render OSC Warp`, `Resample to OSC`, `Drag Audio to DAW`, `Build Version & Date`, `ContentPack Status`) | Serum `[S p.22-23, 322-325, 344-345]`, `ARCHITECTURE.md` §5-6 | State I/O (`StateIO`, `Preset`, `ContentRegistry`) **HAVE**; `Render OSC Warp`, `Resample to OSC`, `Drag Audio to DAW` are **GAP** | **HAVE** (Init/State/ContentPack); **GAP** (offline resample/WAV drag) |

---

## 2. Screen design

On mobile portrait, tapping the `[GLOBAL]` tab opens the **Voice, MIDI & Global** page, structured with five clean sub-tabs matching all Serum 2 and Vital global sections while respecting Principle #1 (1 visual + at most 4 main knobs per screen, secondary settings in clean cards/sheets):
- **`[Voicing]`** — Master Volume/Tune/Transpose, Polyphony, Mono/Legato, Portamento curve & knobs, Osc Key/Vel Mapping sheet.
- **`[Quality]`** — Real-time Master Oscilloscope/FFT + CPU/Voice meter, Quality Tier selector (`Low / Med / High`), Oversampling, Tier Caps.
- **`[Voice Ctrl]`** — Serum 2 Per-Voice Randomization (`PAN`, `DETUNE`, `CUTOFF`, `ENVS`), 1–8 Voice Sequence, Global Env/LFO Scaling (`GAP`).
- **`[MIDI / MPE]`** — Pitch Bend range, Velocity tracking, Key/Scale Snap (`GAP`), MPE setup (`GAP`), MIDI CC Map manager.
- **`[Tuning / Prefs]`** — Concert Pitch A4, Microtonal `.tun/.scl` & MTS-ESP (`GAP`), UI Preferences, Init/Render utilities, About/Version.

### 2.1 Sub-Tab 1: `[Voicing]` (Voicing, Portamento & Master) (`[S p.215-218, 267]`, `[V p.39, 43]`)

```
+-------------------------------------------------------------------+
| GLOBAL & VOICE   [Voicing] [Quality] [Voice Ctrl] [MIDI] [Prefs]  |
+-------------------------------------------------------------------+
|  +-------------------------------------------------------------+  |
|  | PORTAMENTO GLIDE CURVE & VOICE MONITOR                      |  |
|  |   Pitch ^                      [Voices: 3 / 16 (Cap: 16)]   |  |
|  |         |          .  *  *     [MONO: OFF]   [LEGATO: ON ]  |  |
|  |         |      . *             [ALWAYS: OFF] [SCALED: ON ]  |  |
|  |         +----*------------->                                |  |
|  |             0 ms    180 ms     Slope: +2.5 (Convex)         |  |
|  +-------------------------------------------------------------+  |
|                                                                   |
|   (( MASTER VOL ))  (( POLYPHONY ))  (( PORTA TIME )) (( SLOPE )) |
|      -12.0 dB          16 voices         180.0 ms        +2.5     |
|   (`master.volume`) (`master.polyphony`)(`portamento.time`)(`slope`)|
+-------------------------------------------------------------------+
|  [More: Transpose (0 st) | Tune (0 ct) | Vel Trk (100%) | Bend]   |
|  [Osc Key/Vel Mapping (GAP)]        [Voice Steal Priority (GAP)]  |
+-------------------------------------------------------------------+
```

#### Voicing "More" & Oscillator Key/Velocity Mapping Sheet (`[S p.217, 267-273]`, `[V p.39, 43]`)

```
+-------------------------------------------------------------------+
| PITCH, STEALING & OSCILLATOR MAPPING                       [Done] |
+-------------------------------------------------------------------+
|  (( TRANSPOSE ))    (( FINE TUNE ))   (( BEND RNG ))  (( VEL TRK))|
|       0 st              0.0 ct             2 st          100.0 %  |
| (`master.transpose`) (`master.tune`) (`master.bend_range`)(`vel`) |
+-------------------------------------------------------------------+
|  VOICE STEALING & POLY RULES                                      |
|  Steal Priority (GAP): [Oldest*][Newest][Highest][Lowest][Vel]    |
|  Limit Same-Note Poly to 1: [ON (Engine Default)]                 |
|  Key & Scale Snap (GAP):    [Key: C v]  [Scale: Chromatic (Off) v]|
+-------------------------------------------------------------------+
|  OSCILLATOR KEY & VELOCITY MAPPING (GAP)        Mode: [KEY] [VEL] |
|  |C0      |C2        |C4        |C6        |C8  [Fold] [Warp]     |
|  |[======= OSC 1 (C0..B3) ======]          |     [x]    [ ]       |
|  |         [======= OSC 2 (C2..C8) ========]|    [ ]    [x]       |
|  |[================ OSC 3 (C0..C8) ========]|    [ ]    [ ]       |
+-------------------------------------------------------------------+
```

---

### 2.2 Sub-Tab 2: `[Quality]` (Quality Tiers, Oversampling & Scope) (`[S p.319-320, 348]`, `[V p.42]`, `quality.h`)

```
+-------------------------------------------------------------------+
| GLOBAL & VOICE   [Voicing] [Quality] [Voice Ctrl] [MIDI] [Prefs]  |
+-------------------------------------------------------------------+
|  +-------------------------------------------------------------+  |
|  | MASTER OSCILLOSCOPE & FFT SPECTRUM           [Wave] [FFT]   |  |
|  |      /\      /\      /\         Peak: -3.2 dB   CPU: 22%    |  |
|  |   __/  \____/  \____/  \__      Voices: 6/16    Unison: 14  |  |
|  +-------------------------------------------------------------+  |
|                                                                   |
|  QUALITY TIER (`IEngine::setQuality` — Lock Across Presets: [ON]) |
|  +------------------+ +-------------------+ +------------------+  |
|  |   LOW (MPC)      | | * MEDIUM (Phone) *| |  HIGH (Desktop)  |  |
|  | Max Voices: 8    | | Max Voices: 16    | | Max Voices: 32   |  |
|  | Max Unison: 4    | | Max Unison: 8     | | Max Unison: 16   |  |
|  | Oversample: 1x   | | Oversample: 2x    | | Oversample: 2x   |  |
|  | WT Interp: Lin   | | WT Interp: Lin    | | WT Interp: Spec  |  |
|  | FX: No Rev/Flg   | | FX: All 9 Active  | | FX: All 9 Active |  |
|  +------------------+ +-------------------+ +------------------+  |
|                                                                   |
|  [Ultra 4x Offline Render: ON]  [Disable Param Smoothing (GAP)]   |
|  [Engine: Vital DSP (AGPL)]     [Silence Tails on Host Stop: ON]  |
+-------------------------------------------------------------------+
```

---

### 2.3 Sub-Tab 3: `[Voice Ctrl]` (Per-Voice Variation & Env/LFO Scaling — Serum `[S p.316-319]`, Engine `GAP`)

```
+-------------------------------------------------------------------+
| GLOBAL & VOICE   [Voicing] [Quality] [Voice Ctrl] [MIDI] [Prefs]  |
+-------------------------------------------------------------------+
|  Target Oscillators: [x OSC 1] [x OSC 2] [x OSC 3]  [Preset: Init]|
|  +-------------------------------------------------------------+  |
|  | PER-VOICE SEQUENCE (Steps: [1][2][3][4*][5][6][7][8]) (GAP) |  |
|  | Step:   1      2      3      4     Parameter: [Pan Offset v]|  |
|  |       [+25%] [-25%] [+50%] [-50%]  (Also: Pitch, WT, Mod1/2)|  |
|  +-------------------------------------------------------------+  |
|                                                                   |
|  PER-VOICE RANDOMIZATION (GAP) & GLOBAL SCALING (GAP)             |
|   (( RND PAN ))   (( RND DETUNE )) (( RND CUTOFF )) (( RND ENVS ))|
|       0.0 %            0.0 ct           0.0 %           0.0 %     |
|                                                                   |
|  Global Time Scaling (GAP): [Env Scale: 100%]  [LFO Scale: 100%]  |
+-------------------------------------------------------------------+
```

---

### 2.4 Sub-Tab 4 & 5: `[MIDI]` and `[Prefs / Tuning]` (`[S p.313-315, 320-326, 340-343]`, `[V p.42-43]`)

```
+-------------------------------------------------------------------+
| GLOBAL & VOICE   [Voicing] [Quality] [Voice Ctrl] [MIDI] [Prefs]  |
+-------------------------------------------------------------------+
|  MICROTONAL TUNING (GAP)                         [Lock Tuning: O] |
|  Concert Pitch A4: [ 440.0 Hz ]   Scale: [ 12-TET Default     v ] |
|  [Load .tun/.scl/.kbm (GAP)] [Clear] [MTS-ESP: ON] [Note-On Only] |
+-------------------------------------------------------------------+
|  MPE & MIDI CONFIGURATION                                         |
|  [MPE Enabled (GAP): OFF]   [MPE Bend Range: 48 st]               |
|  [Quick Map: XYZ -> Macro 1,2,3]  [YZ -> M1,2]  [Y -> Mod Wheel]  |
|  [Load MIDI CC Map] [Save MIDI CC Map] [Load CC Map from Preset]  |
|  [Rocker CC Preset Prev/Next: Off] [Sustain Pedal CC64 (GAP): On] |
+-------------------------------------------------------------------+
|  UI PREFERENCES & UTILITIES                                       |
|  Double-Tap Knob: [Reset* / Type Value]  Freq Units: [Hz* / Semi] |
|  Default WT View: [2D* / 3D / Spectral]  Limit Mod on Drop: [OFF] |
|  [Init Preset] [Init Mods] [Init LFOs] [Revert] [Save as Default] |
|  Build: SynthyBoy v0.1.0 (Engine: vital) | ContentPacks: OK [Info]|
+-------------------------------------------------------------------+
```

---

### 2.5 Tablet & MPC Key 61 Landscape Layout (`00-BRIEF.md` #6)

On tablet and MPC Key 61 landscape, the **Voicing & Portamento Panel** is always accessible in the right-hand master column (like Vital/Serum), and opening the `[GLOBAL]` tab shows a 4-quadrant dashboard (**Voicing & Osc Mapping**, **Quality & Scope**, **Tuning & MPE/MIDI**, **Voice Control & Utilities**) simultaneously without sub-tab switching.

---

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| **Portamento Curve Graph** | Drag curve up/down; drag endpoint left/right | Adjusts `master.portamento.slope` (`-8..+8`) and `master.portamento.time` (`0..8000 ms`) `[S p.218; V p.39]` | Turn `(( PORTA TIME ))` and `(( SLOPE ))` knobs |
| **`[MONO]` Toggle Pill** | Single tap | Toggles between `master.polyphony = 1` (Mono) and the last polyphonic voice count (`2..32`) `[S p.216]` | Set `(( POLYPHONY ))` knob to `1` |
| **`[LEGATO]`, `[ALWAYS]`, `[SCALED]` Pills** | Single tap | Toggles `master.legato`, `master.portamento.always`, `master.portamento.scaled` (`0/1`) `[S p.216, 218]` | Kick remote page `master_voicing` / `master_porta` stepped buttons |
| **Voice Count Badge (`3 / 16v`)** | Tap in Top Bar | Jumps directly to `GLOBAL -> [Voicing]` & `[Quality]` tier voice cap inspector | Navigate via `[GLOBAL]` page tab |
| **Quality Tier Card (`Low / Med / High`)** | Single tap | Calls `IEngine::setQuality(qualityForTier(tier))` lock-free (`oversampling` takes effect on next `prepare()`, caps apply on next audio block) | Host / Kick performance settings menu |
| **Oscillator Mapping Bars (`KEY` / `VEL`) (`GAP`)** | Drag left/right handle; drag bar body | Sets low/high MIDI note (`0..127`) or velocity (`1..127`) per oscillator `[S p.270-273]` | Double-tap range bar to type note names (`C2`..`G5`) `[S p.271]` |
| **Master Scope / FFT Display** | Single tap / pinch | Toggles between Waveform Oscilloscope and FFT Spectrum Analyzer `[V p.42]` | Tap `[Wave]` / `[FFT]` segment buttons |
| **Init / Utility Buttons** | Tap -> confirm toast | Executes `Init Preset` (loads default table values), `Init Modulations`, or `Revert to Saved` `[S p.324]` | Top bar `[Menu v]` dropdown |

---

## 4. States and edge cases

1. **Quality Tier Capping vs Stored Preset Values (`core/include/synthyboy/quality.h`, `docs/PARAMETERS.md` l.961):**
   - `Quality` never alters the stored parameter values (`master.polyphony`, `oscN.unison.voices`, `reverb.on`, `flanger.on`).
   - When `master.polyphony` (`1..32`) exceeds `Quality::maxVoices` (`8` on Low, `16` on Medium), the UI displays **both** numbers on the Polyphony knob and Voice badge: e.g., `8 (16)` with an amber `[TIER CAP]` pill so the user knows why only 8 voices sound (`docs/QUALITY.md` item i).
   - Lowering the tier while notes are playing releases excess voices smoothly (50 ms kill fade) rather than cutting audio (`core/include/synthyboy/engine.h` l.33).
   - Note that `IEngine::activeVoices()` includes released voices still fading out, so it can transiently read up to `maxVoices + 2` during fast note flurries (`core/engine-vital/README.md` l.88); the UI clamps the visual bar at 100% while showing the exact integer.
2. **Mono vs Poly Legato (`[S p.216]`, `[V p.39]`):**
   - In `engine-vital`, `master.legato` prevents envelope/LFO retriggering when `master.polyphony == 1` and notes overlap (`docs/QUALITY.md` item h). When `master.polyphony > 1`, the `[LEGATO]` pill shows a subtle `(Mono only)` subtitle unless paraphonic legato (`GAP`) is implemented.
3. **Own Engine (`core/engine-own`, iOS build) Parameter Support (`docs/PARAMETERS.md` l.52-65):**
   - On `engine-own`, `master.volume`, `master.bend_range`, `master.transpose`, and `master.tune` are active (`ownEngine: true`).
   - `master.polyphony`, `master.velocity_track`, `master.legato`, and `master.portamento.*` have `ownEngine: false` in the current spike engine; on iOS builds, those controls display an `[iOS: Fixed]` badge until `engine-own` adds portamento/legato.
4. **Missing ContentPack State (`docs/ARCHITECTURE.md` §5.2, §6):**
   - If a preset references a wavetable or pack absent on disk, the engine falls back to built-in Sine/Saw (`missing_content` flag true) and never crashes. The Top Bar and `GLOBAL -> [Prefs]` tab show a non-blocking banner: `[!] Content pack missing — playing built-in fallback [Restore in Kick]`.
5. **No Network Access Rule (`docs/ARCHITECTURE.md` §2.2 rule 2):**
   - Serum's `Automatically check for updates` `[S p.315]` and Vital's `Log Out` / auto-update `[V p.3-4]` are **omitted** by design: SynthyBoy contains zero network code.
6. **Parameter Ranges, Units & Defaults (`docs/PARAMETERS.md` l.50-65):**
   - `master.volume`: `-60..6 dB` (skew `2.5`, `-60 dB` = silence), default `-12.0 dB`.
   - `master.polyphony`: `1..32 voices` (step `1`), default `16 voices`.
   - `master.bend_range`: `0..24 st` (step `1`), default `2 st`.
   - `master.velocity_track`: `-100..100 %`, default `100.0 %`.
   - `master.transpose`: `-48..48 st` (step `1`), default `0 st`.
   - `master.tune`: `-100..100 ct`, default `0.0 ct`.
   - `master.legato`: `0 Off, 1 On`, default `Off`.
   - `master.portamento.time`: `0..8000 ms` (skew `0.3`), default `0.0 ms`.
   - `master.portamento.slope`: `-8..8`, default `0.0`.
   - `master.portamento.always`: `0 Off, 1 On`, default `Off`.
   - `master.portamento.scaled`: `0 Off, 1 On`, default `Off`.

---

## 5. Kick remote pages

All 11 `Master` parameters in `docs/params.json` are exposed across two 8-knob remote-control pages:

| Page ID | Page Name | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Knob 5 | Knob 6 | Knob 7 | Knob 8 |
|---------|-----------|--------|--------|--------|--------|--------|--------|--------|--------|
| `master_voicing` | **Master & Voicing** | `master.volume` | `master.transpose` | `master.tune` | `master.polyphony` | `master.legato` | `master.bend_range` | `master.velocity_track` | `master.portamento.time` |
| `master_porta` | **Portamento & Glide** | `master.portamento.time` | `master.portamento.slope` | `master.portamento.always` | `master.portamento.scaled` | `master.legato` | `master.polyphony` | `master.bend_range` | `master.volume` |

---

## 6. Coverage check

Every parameter in the `Master` section of `docs/params.json` (11 keys total across `Master` and `Master / Portamento`) is covered in Section 2:

| Parameter Group (`docs/params.json`) | Key | Name | Where it appears in Section 2 |
|--------------------------------------|-----|------|-------------------------------|
| **Master** | `master.volume` | Master Volume | §2.1 Main knob 1 (`(( MASTER VOL ))`) |
| **Master** | `master.polyphony` | Master Polyphony | §2.1 Main knob 2 (`(( POLYPHONY ))`) + `[MONO]` toggle |
| **Master** | `master.bend_range` | Master Pitch Bend Range | §2.1 Voicing "More" Sheet (`(( BEND RNG ))`) & §2.4 `[MIDI]` tab |
| **Master** | `master.velocity_track` | Master Velocity Tracking | §2.1 Voicing "More" Sheet (`(( VEL TRK ))`) & `06-modulation.md` §2.4 |
| **Master** | `master.transpose` | Master Transpose | §2.1 Voicing "More" Sheet (`(( TRANSPOSE ))`) & Keyboard strip header |
| **Master** | `master.tune` | Master Tune | §2.1 Voicing "More" Sheet (`(( FINE TUNE ))`) |
| **Master** | `master.legato` | Master Legato | §2.1 Portamento & Voice Monitor card (`[LEGATO: ON/OFF]`) |
| **Master / Portamento** | `master.portamento.time` | Portamento Time | §2.1 Main knob 3 (`(( PORTA TIME ))`) & Glide curve X drag |
| **Master / Portamento** | `master.portamento.slope` | Portamento Slope | §2.1 Main knob 4 (`(( SLOPE ))`) & Glide curve Y drag |
| **Master / Portamento** | `master.portamento.always` | Portamento Always | §2.1 Portamento & Voice Monitor card (`[ALWAYS: ON/OFF]`) |
| **Master / Portamento** | `master.portamento.scaled` | Portamento Scaled By Interval | §2.1 Portamento & Voice Monitor card (`[SCALED: ON/OFF]`) |
| **Total covered in `07-voice-midi-global.md`** | `master.*` | **11 / 11** | 100% of Master & Portamento parameters |

---

## 7. Open questions (owner)

1. **Sequencer exclusions (`00-BRIEF.md`):** Serum 2 **Clips** (`[S p.219-243]`) and **Arpeggiator** (`[S p.244-266]`) are treated as excluded (Kick provides them). Confirm whether Key/Scale quantization (`[S p.268]`, `[V p.7]`) and Global Swing (`[S p.269]`) also belong solely to Kick's MIDI input pipeline or should be added to SynthyBoy's core.
2. **Sustain Pedal (CC64) (`docs/QUALITY.md` l.100):** `sbvital::SoundEngine` already implements sustain pedal (`sustainOn`/`sustainOff`), but `synthyboy::Event` (`core/include/synthyboy/events.h`) does not yet have a `SustainPedal` event type. Should we add `Event::Type::sustainPedal` in the next core update?
3. **Microtonal Tuning & MTS-ESP (`[S p.320-321]`, `[V p.43]`):** `sbvital::Tuning` line parsers are still present in `core/engine-vital/upstream/common/tuning.cpp`. Should we expose `.scl`/`.kbm`/`.tun` loading and A4 concert pitch on `IEngine`, or keep 12-TET @ 440 Hz only?
4. **MPE Support (`[S p.314, 325-326]`):** Should per-note pitch bend, pressure (`Z`), and slide (`Y` / CC74) be wired through `clap_event_note_expression` + MPE channels into `engine-vital` and `engine-own`?
5. **Serum 2 Per-Voice Control (`[S p.316-319]`) & Oscillator Key/Velocity Ranges (`[S p.269-273]`):** These are new Serum 2 additions not present in Vital. Keep as documented `GAP`s, or schedule per-voice pan/detune drift for Phase 5?
