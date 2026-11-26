# UI Map — Overview

> Owner order (2026-11-21): page order after Play is **Osc → Env → LFO → FX**; Play + Macros share the first key.

---

## All pages at a glance

| Page | Purpose | Controls (own + popups) | What it opens | Q-Link groups | Live parts |
|---|---|---|---|---|---|
| **Play** | Live synthesis view — oscillator, envelope and LFO side by side in one screen | ~87 own + full-screen popups | OscFullScreen, EnvFullScreen, LfoFullScreen, ShapeEditorPage, ModTargetsPage, ModulationsPage, ListPopup | 3 (Osc, Env, LFO; each switches on highlight) | Oscillator cycle waveform, envelope voice dots, LFO voice cursors / RND trace |
| **Macros** | 8 macro knobs with live spectrum display and modulation-target chips | 11 own + targets/list/keyboard popups | Targets page, Destination list, Keyboard (rename) | 2 (MACROS 1–4 / 5–8) + targets row | Live spectrum strip, macro card chips |
| **Osc** | Full editor for Sub / A / B / C / Noise oscillators (5 tabs) | ~74 own + 10 detail/popup pages | Native routing, Unison, GrainWindow, MSEnvelope, 7 OscSpecPages, ModulationsPage, ListPopup | 6 (Basic, Type, Tune, Unison, Warp1, Warp2) per tab | Waveform / 3D stack / FFT / sample cursor, voice playheads, power-off grey |
| **Mixer** | 10-channel mixing console (Sub, A, B, C, Noise, F1, F2, BUS1, BUS2, MASTER) | 50 own (bold cut: no OUT button) + destination list popup | Destination list popup | 10 (one per strip) | Peak meters, routing animation |
| **Filter** | Filter 1 and Filter 2 with live frequency-response curve and dual-filter routing | ~47 own + RoutePanel / ModelList / Mods | RoutePanel, Model/Style ListPopup, ModulationsPage | 4 (MAIN, TRACK, OUT, FORMANT) | Real-time response curve, voice cutoff markers, formant puck |
| **Env** | DAHDSR envelope editor for 6 envelopes with interactive graph | ~19 own + targets/mods/list/keyboard | ModTargetsPage, Destination list, ModulationsPage, Instance list | 3 (ADSR, SHAPE, TIME) | Voice progress dots, modulation arcs |
| **LFO** | Cycle LFOs (1–10) and Random LFOs (1–4) with shape picker | ~40 own + ShapeEditorPage / PointModPage / targets / keyboard | ShapeEditorPage, PointModPage, ModTargetsPage, ModulationsPage, ListPopup, Keyboard | 5 (Rate, Timing, Motion, Shape, Key) | Voice phase cursors, RND oscilloscope trace |
| **FX** | Effect chain rack — Main, Bus 1, Bus 2, and band racks via Splitters | ~200 (all effect params) + type list / param list | Type-select ListPopup, Param ListPopup, Splitter band scope | Per-effect row blocks | Response curves (EQ, compressor, delay, distortion, FX filter), splitter band info |
| **Mod** | Full 64-slot modulation matrix with quick-add source board | ~16 matrix controls + slot detail / targets / list | ModSlotDetail, ModTargetsPage (from board), ListPopup | 1 per active slot (source, dest, amount) | Live amount deflection needle |
| **Master** | Master output, pitch, voices, glide, scale, tuning, MPE | Host-native dials, number fields, selectors and toggles (`scripts/native_master/master_controls.py`), plus the overlay glide curve | Host `MenuOverlay` (selectors, incl. SCALE and KEY) and host `NumericOverlay` (dials by double tap, whole-number fields by touch); the overlay opens no page of its own | 9 (OUTPUT, PITCH, VOICES, VOICE OPTS, GLIDE, SCALE, TUNING, MPE, MPE Y; LINK touch only) | Peak meters, clip indicator, voice count bar, glide curve |
| **Voice** | Per-voice step sequencer (8 steps × 6 rows) + random spread and scaling | ~59 own (48 step params + 11 controls) | (none — list popup is dead code) | 6 (STEP, RANDOM, RAND 10x, SCALING, TARGETS, NOISE) | Active-step highlight dots |

> **Control counts** are approximate; they include all interactive controls in the main view but not every parameter variant inside popups. The FX page count is the sum of all effect module parameters across the DSP table.

---

## Where is X — alphabetical function index

| Function / parameter | Primary page | Also in |
|---|---|---|
| A4 reference pitch | Master → TUNING block | — |
| Attack time (Env 1) | Env page (graph + ADSR knob) | Play compact ENV panel; host proxy |
| Bend range (pitch wheel) | Master → PITCH block | — |
| Bus sends (osc / filter) | Osc → native routing controls; Filter → OUTPUT popup | Mixer strips B1/B2 |
| Compressor | FX page (Compressor effect) | — |
| Cutoff (filter) | Filter page MAIN block (knob + display puck) | Mixer (no — only level/pan there) |
| Delay (FX echo) | FX page (Delay effect) | — |
| Delay time (Env / LFO) | Env full-screen TIME block; LFO TIMING block | — |
| Detune (unison) | Osc → bottom panel UNISON block | Osc → Unison detail page |
| Direction (LFO) | LFO → MOTION block | — |
| Drive (filter) | Filter page MAIN block | — |
| Envelope times (ADSR) | Env page graph handles + bottom ADSR knobs | Play compact ENV panel |
| Envelope curve shapes | Env page → SHAPE block (A/D/R curve knobs) | — |
| EQ | FX page (EQ effect) | — |
| Filter cutoff | Filter page MAIN (CUTOFF knob + display puck) | — |
| Filter model / style | Filter page top bar | — |
| Filter routing (F1/F2 order) | Filter page OUT block → ROUTING LED list | — |
| FX order / chain | FX page chain bar (drag to reorder) | — |
| FX power / bypass | FX page slot card power icon | Mixer BUS1/BUS2/MASTER power (inverted bypass) |
| Glide (portamento) | Master → GLIDE block | — |
| Grain parameters | Osc → Osc A/B/C, Granular type, bottom panel + GRAIN spec page | — |
| Keytrack (filter) | Filter page TRACK block | — |
| Legato | Master → VOICES block; Env page TIME block → LEGATO toggle | — |
| Level (oscillator) | Play compact OSC panel; Osc page top basic controls | Mixer strip LEVEL |
| Level (filter) | Filter page OUT block | Mixer strip LEVEL; Filter OUTPUT popup |
| LFO rate / shape | Play compact LFO panel (rate + shape); LFO page RATE block | — |
| LFO sync mode | LFO page → SYNC LED list (HZ/BPM/KEY) | Play compact LFO inline mode cells |
| Loop / bounce (sample) | Osc → Sample type, Noise sample type controls | — |
| Macros (1–8) | Macros page knobs | Mod matrix source board; Play/Macros key shortcut |
| Master volume | Master → OUTPUT block (VOLUME knob) | Mixer MASTER strip LEVEL |
| Mod routing (matrix) | Mod page (all 64 slots, full view) | Env TARGETS page; LFO TARGETS page; Macro TARGETS page; Osc / Filter ModulationsPage |
| Mod slot depth / polarity | Mod page matrix rows | Same per-source TARGETS pages |
| Noise kind / filter | Osc → Noise tab (SOUND / KIND / FILTER controls) | Play compact OSC panel |
| Oscillator on/off | Play compact OSC (power toggle); Osc page top bar POWER | Mixer strip on/off |
| Oscillator routing (dest) | Play compact OSC (ROUTE); OSC destination menu | Mixer strip dest dropdown |
| Oscillator type | Play compact OSC (TYPE); Osc page top bar TYPE selector | — |
| Pan (oscillator) | Play compact OSC panel; Osc page top basic controls | Mixer strip PAN |
| Pan (filter) | Filter page OUT block | Mixer strip PAN; Filter OUTPUT popup |
| Phase (LFO) | LFO page TIMING block → PHASE knob | Shape editor canvas marker |
| Phase (osc) | Osc page → Type bar PHASE field | — |
| Polyphony | Master → VOICES block (POLY) | — |
| Random LFO style | Play compact LFO (Random mode STYLE); LFO page shape picker | — |
| Resonance (filter) | Filter page MAIN block (RESO knob + display puck) | — |
| Reverb | FX page (Reverb effect) | — |
| Rise / fade-in (LFO/Env) | LFO page RATE block (RISE); Env page — not shown directly (rise is in full-screen) | — |
| Sample region / slices | Osc → Sample type → REGION / SLICE spec pages | — |
| Scale / tuning | Master → TUNING & MPE side | — |
| Stereo spread (osc unison) | Osc → Unison detail page (WIDTH knob) | — |
| Sub octave | Play compact OSC (OCT for Sub); Osc → Sub tab OCT control | — |
| Swing (LFO) | LFO page MOTION block | — |
| Transpose (osc) | Osc page Tune column (OCT / SEM fields) | — |
| Tuning (fine, osc) | Osc page Tune column (FIN / CRS fields) | — |
| Unison voices / detune | Osc page bottom panel UNISON block | Osc → Unison detail page (MODE, STACK, WIDTH, RANGE) |
| VAR (filter character) | Filter page TRACK block | — |
| Velocity tracking | Master → VOICES block (VEL TRACK) | — |
| Voice step sequencer | Voice page grid (8 steps × 6 rows) | — |
| Voice random spread | Voice page RANDOM block | — |
| Wavetable / source | Osc page top basic controls (TABLE / SOURCE) | Play compact OSC panel |
| Warp (osc) | Osc page bottom panel WARP 1 / WARP 2 blocks | — |
