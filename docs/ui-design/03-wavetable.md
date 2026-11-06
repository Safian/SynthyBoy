# Wavetable view and wavetable editor – mobile UI design

Scope: the wavetable VIEW inside an oscillator panel and the full-screen wavetable EDITOR (draw, spectrum, frames, process, formula, import). Oscillator parameters are in 02. The preset / wavetable BROWSER (folders, favourites, search) is excluded: Kick.
Cites: `[S p.n]` Serum 2 manual, `[V p.n]` Vital unofficial manual, `[V src]` = imported Vital source `core/engine-vital/upstream/src/common/wavetable/` (the real Vital reference for the editor, ARCHITECTURE 3.3). Key family: `oscN` = osc1..osc3.
Status: `HAVE` = engine and params support it today; `GAP` = missing, UI still designed here. For editor functions the algorithm often exists in `[V src]` but no core API, `ParamInfo`, state path or shell reaches it yet (PARAMETERS "Not exposed": no editor, no import); those rows are `GAP` and say "model in `[V src]`".

Data model used by the design (ARCHITECTURE 3.3): wavetable = group of components (sources and modifiers) with keyframes at frame positions 0..255; rendered result = up to 256 frames x 2048 samples; keyframes show as real (green) frames, interpolated frames as grey [S p.38, p.289]. Serum "frames" = keyframes of the main Wave Source; Serum "Morph" = the interpolation mode.

## 1. Function inventory

### 1.1 Wavetable view (in the oscillator panel)

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| 2D view of the current frame (single cycle) [S p.39] [V p.7] | both | `oscN.frame`, `oscN.wavetable` | HAVE (UI draws from built-in table data) |
| 3D view of all frames; real frames green, interpolated grey, current yellow, frame number lower left [S p.39] | Serum | `oscN.frame` | HAVE (UI) |
| Spectral view (SP): harmonics of the current frame [V p.7-8] | Vital | `oscN.frame` | HAVE (UI) |
| Toggle 2D / 3D by clicking the view; default view preference [S p.39, p.314] | Serum | none | HAVE (UI) |
| Wavetable position slider under the view [V p.7] | Vital | `oscN.frame` | HAVE |
| Warp preview drawn in 2D (Sync, Alt Warp, Distortion) [S p.49] | Serum | `oscN.warp.type`, `oscN.warp.amount`, `oscN.warp.phase` | HAVE (UI) |
| Phase marker (yellow line where the note starts) in 2D [S p.42] | Serum | `oscN.phase`, `oscN.random_phase` | HAVE (UI) |
| Unison frame spread shown as extra frame markers | design | `oscN.frame_spread` | HAVE (UI) |
| Live modulation animation of the frame position | Vital | `oscN.frame` (modulatable) | HAVE (UI) |
| Wavetable name field, "modified, not saved" tint [S p.290] | Serum | none | GAP |
| Wavetable menu: Save Table, Embed in Preset, Load Table [S p.22, p.290, p.303] | Serum | `oscN.wavetable` selects built-ins only | GAP (user tables); see 01 for Embed |
| Open editor button [S p.40] [V p.8] | both | none | HAVE (UI) |
| Drop audio / image on the view to import [S p.291, p.300] | Serum | none | GAP (mobile: Import button, section 2.6) |
| Switch OSC type on import (Sample / Granular / Spectral) [S p.294] | Serum | none | GAP (needs 02 types) |
| Copy wavetable frame to an LFO, copy LFO shape to a wavetable [S p.197-198] | Serum | `lfoN.shape` (built-in list only) | GAP (free-form LFO shapes, see 05) |
| Render OSC Warp: current frame -> 256 frames across the WARP knob 0..100 % [S p.324] | Serum | `oscN.warp.type`, `oscN.warp.amount` | GAP |
| Resample: capture one bar of the preset as a wavetable [S p.324] | Serum | none | GAP |
| Mod wheel scans WT POS while the editor is open [S p.315] | Serum | event ModWheel, `oscN.frame` | HAVE (UI: preview only) |
| Wavetable memory: 2048 samples per frame, <= 256 frames, 2 MB max [S p.38] | Serum | none | GAP (built-ins have 1 or 64 frames, README) |

### 1.2 Editor: drawing, FFT, frames

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Thumbnails: click select, drag reorder (yellow insertion cursor), Shift-click range [S p.275] | Serum | none | GAP (model in [V src] keyframes) |
| Drawing tool: Flat Line (default) [S p.276] | Serum | none | GAP |
| Drawing tool: Sine (adds the harmonic of the grid size) [S p.276] | Serum | none | GAP |
| Drawing tool: Slope Up, Slope Down [S p.277] | Serum | none | GAP |
| Drawing tool: Curve Up, Curve Down (quarter sine) [S p.277] | Serum | none | GAP |
| Drawing tool: Half Sine peak, Half Sine valley [S p.277] | Serum | none | GAP |
| Drawing tool: Interpolate Linear, Interpolate Curved [S p.277] | Serum | none | GAP |
| Drawing tool: Nudge (vertical move, clips at the limit) [S p.277] | Serum | none | GAP |
| Drawing tool: Noise (drag up adds, down removes) [S p.277] | Serum | none | GAP |
| Drawing tool: Mirror (symmetrical drawing) [S p.277] | Serum | none | GAP |
| Grid size (horizontal) for tools and Process commands [S p.276] | Serum | none | GAP |
| Vital Wave Source: drawn frames with Time or Frequency interpolation [V src wave_source.h] | Vital | none | GAP |
| Vital Line Source: points with power (curve) per segment, keyframed [V src wave_line_source.h] | Vital | none | GAP |
| FFT area: draw amplitude of harmonic bins (bin 1 = fundamental, 2:1 octave, ...), up to 512 bins [S p.278, p.306] | both | none | GAP |
| FFT area: draw phase offset per harmonic; Shift-Cmd-click nudges phase right [S p.278] | both | none | GAP |
| FFT menu: Clear All, Clear HF (low-pass), Clear LF (high-pass) [S p.279] | Serum | none | GAP |
| FFT menu: Generate Saw [S p.279] | Serum | none | GAP |
| FFT menu: Randomize Low x Bins, Randomize Low x Bins (with Half), Randomize All, Create Random Series Gaps [S p.279-280] | Serum | none | GAP |
| FFT menu: Progressive Fade, Shift Octave Up, Shift Octave Down, Repeat Bin Group [S p.280] | Serum | none | GAP |
| FFT menu: Draw Even Harmonics Only, Draw Odd Harmonics Only, Snap Vertical Draw to Quarters, Scale Freq Values by Bin Index [S p.280] | Serum | none | GAP |
| Frame buttons: COPY, PASTE (contiguous range, paste at target frame) [S p.282] | Serum | none | GAP |
| Frame buttons: + (insert after current), - (remove current) [S p.283] | Serum | none | GAP |
| REMOVE menu: Init All (default single saw frame), Insert, Remove current, Remove multiselection, Remove beginning->selected, Remove selected->end, Remove All except Selected (crop), Reduce to (every nth frame) [S p.282-283] | Serum | none | GAP |
| SORT menu: by spectrum Peak Spect, Average Spect, Peak Amount, Num w/Spect, Highest w/Spect, Fundamental Amt; Sort Randomize; Reverse Entire Table Order [S p.283] | Serum | none | GAP |
| WT POS number box in the editor to audition frames while a note plays [S p.300] | Serum | none | HAVE (UI, preview) |
| Undo / Redo in the editor [S p.73 pattern, p.289] | Serum | none | HAVE (UI) |
| Keyframe model with per-component interpolation None / Linear / Cubic [V src wavetable_component.h] | Vital | none | GAP |

### 1.3 Editor: Process menus, Morph, formula, export, save

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| SINGLE (current frame): Normalize, Remove DC Offset, Flip Vertical, Flip Horizontal, Shift Horizontal to Zero Crossing, Init (Silence) [S p.286] | Serum | none | GAP |
| SINGLE: Fade Edges (Grid Size), X-Fade Edges (Grid Size), Filter (Grid Size), Sample Redux at Grid Size [S p.286] | Serum | none | GAP |
| SINGLE: Send to Noise Oscillator [S p.286] | Serum | none | GAP (noise sample missing, 02) |
| ALL (frames 1-256): Normalize Each, Normalize Same, Remove DC Offset, Flip Vertical, Flip Horizontal [S p.287] | Serum | none | GAP |
| ALL: Fade Edges (Grid Size), X-Fade Edges (Grid Size), X-Fade Edges (16 Samples), Filter (Grid Size), Sample Redux [S p.287] | Serum | none | GAP |
| ALL: Remove Fundamental (HPF), Remove Low Spectra (Grid Size), Remove Low Phases (Grid Size) [S p.287] | Serum | none | GAP |
| ALL: Resize Tables to be Half (2x number), Resize to be Double (1/2 number) [S p.288] | Serum | none | GAP |
| ALL: Create PWM from This Table to All [S p.288] | Serum | none | GAP |
| ALL: Nudge All Phases for Fundamental to 50 % [S p.288] | Serum | none | GAP |
| ALL: Set Spectra (This Frame to All), Set Phases (This Frame to All) [S p.288] | Serum | none | GAP |
| ALL: Set Spectra / Set Phases / Subtract Spectra from Other Osc [S p.288] | Serum | none | GAP |
| ALL: Blur Spectra / Phases - Adjacent Bins (Grid Size); Blur Spectra / Phases - Adjacent Frames (Grid Size) [S p.288] | Serum | none | GAP |
| ALL: Shift Horizontal to Zero-Crossing [S p.288] | Serum | none | GAP |
| MORPH: Crossfade, Spectral, Spectral (Zero Fund. Phase), Spectral (Zero All Phases), Remove Morph Tables (needs 2..255 frames) [S p.289] | Serum | none | GAP (model: [V src] Time / Frequency interpolation) |
| Vital modifier: Phase shift (Normal, Even/Odd, Harmonic, Harmonic Even/Odd, Clear; phase, mix) [V src phase_modifier.h] | Vital | none | GAP |
| Vital modifier: Wave Window (Cos, Half Sin, Linear, Square, Wiggle; left / right) [V src wave_window_modifier.h] | Vital | none | GAP |
| Vital modifier: Frequency Filter (Low Pass, Band Pass, High Pass, Comb; cutoff, shape, normalize) [V src frequency_filter_modifier.h] | Vital | none | GAP |
| Vital modifier: Slew Limiter (up / down limits) [V src slew_limit_modifier.h] | Vital | none | GAP |
| Vital modifier: Wave Folder (boost) [V src wave_fold_modifier.h] | Vital | none | GAP |
| Vital modifier: Wave Warp (horizontal / vertical power, asymmetric flags) [V src wave_warp_modifier.h] | Vital | none | GAP |
| Vital Shepard Tone source (deprecated upstream) [V src shepard_tone_source.h] | Vital | `oscN.morph.type` = Shepard Tone is the live morph, not a source | not offered (deprecated) |
| Formula field (type formula, Enter plots; single-cycle or multi-table by y / z) [S p.284, p.304] | Serum | none | GAP |
| Formula functions: sin cos tan asin acos atan sinh cosh tanh asinh acosh atanh log2 log10 log ln exp sqrt sign rint abs min max sum avg [S p.304-305] | Serum | none | GAP |
| Formula operators: && \|\| <= >= != == > < + - * / ^ and conditional `a ? b : c`, `%` (used in examples) [S p.305, p.310] | Serum | none | GAP |
| Formula variables: pi, e, w, x, y, z, q (plots to FFT bins 1..512), in, sel, rand [S p.306] | Serum | none | GAP |
| Formula presets menu: Singles, Multis, User Singles, User Multis; Save Formula; file management [S p.307-309] | Serum | none | GAP |
| Export menu: All as 8-bit (.256), All as 16-bit wav (44.1 kHz, 2048 per frame), All as 32-bit wav, All as Single-Cycle Waves, Export Selection (Serum file) [S p.285] | Serum | none | GAP |
| Save Table (User folder, never overwrite factory), wavetable always stored inside the preset unless factory [S p.290] | Serum | none | GAP |
| Wavetable stored as content (`.sbwt`, ContentPack) or embedded in `.sbpreset` [ARCHITECTURE 3.3, 5.2] | both | none | GAP (format exists, Phase 4; no UI path) |

### 1.4 Import

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Import menu (file dialog) = same options as drag and drop [S p.285, p.298] | Serum | none | GAP |
| Multi-cycle import: DYNAMIC PITCH - ZERO SNAP [S p.292] | Serum | none | GAP |
| Multi-cycle import: DYNAMIC PITCH - FOLLOW [S p.292] | Serum | none | GAP (model: [V src] `kPitched`, `pitch_detector`) |
| Multi-cycle import: FREQUENCY ESTIMATION [S p.293] | Serum | none | GAP |
| Multi-cycle import: Constant framesize (PITCH AVERAGE), shows samples per frame in the formula field [S p.293] | Serum | none | GAP (model: [V src] `kWavetableSplice`) |
| Multi-cycle import: FFT 256 / 512 / 1024 / 2048 (spectral import) [S p.293-294] | Serum | none | GAP (model: [V src] `kVocoded`) |
| Vital import styles: Wavetable Splice, Vocoded, Time-to-Wavetable (Ttwt), Pitched [V src wavetable_creator.h] | Vital | none | GAP |
| Vital File Source options: fade style (Wave Blend, No Interpolate, Time Interpolate, Freq Interpolate), phase style (None, Clear, Vocode) [V src file_source.h] | Vital | none | GAP |
| Switch OSC type import (Sample / Granular / Spectral) [S p.294] | Serum | none | GAP |
| Advanced: formula field accepts a sample count (1..4 digits) or a note name to set cycle length [S p.294] | Serum | none | GAP |
| Text file overrides `name.txt` / `FolderInfo.txt`: `[samples]`, `[interp]` / `[no interp]` [S p.295] | Serum | none | not applicable on mobile (replaced by the "samples per cycle" and "interpolate" fields) |
| Stereo import: left channel to OSC A / C, right to OSC B [S p.291] | Serum | none | GAP |
| Single-cycle import onto one frame thumbnail [S p.296] | Serum | none | GAP |
| Import several single-cycle files at once = new table [S p.297] | Serum | none | GAP |
| Prepare a 2048-samples-per-cycle render for exact import [S p.298-300] | Serum | none | documentation only (help text in the import sheet) |
| Trim blank tables after import (Remove selected->end), fade edges to remove buzz [S p.300, p.294] | Serum | none | GAP (uses ops above) |
| PNG image import: 8-bit, width = frames (<= 256), luminance = amplitude [S p.300-301] | Serum | none | GAP |
| Embed imported wavetable in the preset, Missing Files dialog (add search folder) [S p.302-303] | Serum | none | GAP (embedding format exists, 01) |
| Animate WT POS with an LFO ramp after import [S p.301] | Serum | `lfoN.shape` (Saw Up), mod slot to `oscN.frame` | HAVE (built-in Saw Up shape; matrix in 06) |

## 2. Screen design

### 2.1 View inside the OSC page (phone portrait)

The view is the large visual of the OSC page in 02 (about 200 pt high).

```
+------------------------------------------+
| Saw to Square  [<][>]     [2D|3D|SP] [EDIT]|  name (oscN.wavetable), view switch, editor
| +--------------------------------------+ |
| |   3D: stacked frames, current yellow  | |  drag left/right = oscN.frame
| |   grey = interpolated, green = real   | |  pinch = zoom time axis; 2 fingers = tilt
| |                         frame 27 / 64 | |
| +--------------------------------------+ |
| FRAME ====o=========            42 %      |
+------------------------------------------+
```

* 2D mode overlays: phase line at `oscN.phase`, warp-processed frame as dotted line, morph preview, unison frame-spread ghosts (`oscN.frame_spread`).
* SP mode: bar graph of harmonics of the current frame; log frequency axis; tap a bar shows the harmonic number.
* A tiny `*` after the name = modified table (GAP); tapping the name opens the table sheet (Load / Save / Embed).

### 2.2 Editor – phone portrait (full-screen modal)

```
+------------------------------------------+
| [x]  OSC1  "My table" *     [undo][redo][...]|  header: close, name, history, menu (Import, Export, Save)
| [WAVE] [SPECTRUM] [PROCESS] [FORMULA] [LAYERS]|  mode tabs
+------------------------------------------+
| +--------------------------------------+ |
| |   active editor canvas (220 pt)       | |
| +--------------------------------------+ |
| tools (scroll): -  ~  /  \  (  )  n  u  /: s  ^  #  M |  tool strip + GRID [16 v]
+------------------------------------------+
| FRAME [ 27 / 64 ] [-][+] [copy][paste] [REMOVE v][SORT v][MORPH v] |
| [1][2][3]...[27*]...[64]  thumbnails, scroll |
+------------------------------------------+
| (mini keyboard + mod wheel, from 01)       |
+------------------------------------------+
```

### 2.3 WAVE and SPECTRUM modes

```
WAVE                                   SPECTRUM
+------------------------------+       +------------------------------+
| frame wave, grid, ghost of    |       | amplitude bars (bins 1..64 visible, |
| previous / next frame (toggle)|       |  pinch to 512, scroll)               |
| finger draws with active tool |       |------------------------------|
|                               |       | phase bars (same bins)               |
+------------------------------+       +------------------------------+
| tool strip                    |       | [EVEN][ODD][SNAP 1/4][SCALE] [FFT ...]|
```

FFT menu (`...`) entries: Clear All, Clear HF from touch, Clear LF to touch, Generate Saw, Randomize (Low x / Low x with half / All), Random Series Gaps, Progressive Fade, Octave up / down, Repeat Bin Group; each is a one-tap command that uses the last touched bin as the cursor [S p.279-280]. Landscape shows WAVE and SPECTRUM side by side and 3D under them.

### 2.4 FRAMES strip and menus

* Thumbnails: 56 x 40 pt, tap = select; long-press, then drag = reorder (yellow insertion bar) [S p.275]; "Select range" toggle (replaces Shift-click) then tap first and last.
* Copy / Paste / `+` / `-` buttons as in [S p.281-283]; REMOVE and SORT menus open as lists with the descriptions of [S p.282-283] in one line; MORPH menu lists the five commands [S p.289].
* Interpolated frames are shown only in the 3D view and the FRAME slider (no thumbnail), exactly as in [S p.289].

### 2.5 PROCESS and FORMULA modes

```
PROCESS                                   FORMULA
+----------------------------------+      +----------------------------------+
| (SINGLE) (ALL)   search [      ] |      | q<17                  [Apply][x] |
| Normalize                 [run]  |      | chips: sin cos tan ... ^ ?: % ( )|
| Remove DC offset          [run]  |      | vars:  x y z q w in sel rand pi e|
| Fade edges   grid [16]    [run]  |      | [Presets v] [Save formula]       |
| ... (long list, descriptions)    |      | live preview of the frame / table |
+----------------------------------+      +----------------------------------+
```

PROCESS: every command shows what it affects ("this frame" / "all N frames") and the GRID value it uses; `[run]` applies, result can be undone. FORMULA: custom keyboard row replaces the OS keyboard punctuation; the field shows parse errors inline; presets menu groups Singles / Multis / User Singles / User Multis [S p.307]; `q` formulas plot to the FFT bins [S p.306, p.309-310].

### 2.6 IMPORT sheet

```
+--------------------------------------+
| IMPORT  (Audio) (Image) (Wavetable)  |
| [Choose file...]   file: pad.wav 2.3 s |
| Method:                              |
|  ( ) Constant framesize (try first)  |
|  ( ) Dynamic pitch - zero snap       |
|  ( ) Dynamic pitch - follow          |
|  ( ) Frequency estimation            |
|  ( ) FFT 256 | 512 | 1024 | 2048     |
|  ( ) Use as Sample/Granular/Spectral |
| Samples per cycle [ auto ]  Interpolate [x] |
| Preview: 87 frames   [Cancel][Import] |
+--------------------------------------+
```

File access: OS file picker / share sheet (mobile), drag-drop (desktop). Image tab: PNG 8-bit, shows resulting frame count (width, max 256). Wavetable tab: loads `.wav` Serum-style tables.

### 2.7 LAYERS mode (Vital component model)

```
+----------------------------------+
| COMPONENTS                [+ add]|
| 1 Wave Source   (frames)  [eye]  |
| 2 Phase Shift   style [v]   keyframes...
| 3 Frequency Filter  LP/BP/HP/Comb  cutoff shape
| keyframe timeline 0..255, marker per keyframe, tap = select, drag = move
| interpolation: None | Linear | Cubic     (per component)
+----------------------------------+
```

Each modifier lists its own controls (sections 1.3). Adding a source: Wave Source, Line Source, File Source. Selecting a keyframe jumps the WAVE view to it.

### 2.8 Tablet / landscape / MPC

```
+--------------------------------------------------------------------+
| header, mode tabs |  WAVE canvas        |  SPECTRUM canvas (amp+phase)|
|  tool strip       |                     |                             |
+--------------------------------------------------------------------+
| 3D overview (all frames, drag to scrub) | thumbnails, frame ops menus  |
+--------------------------------------------------------------------+
```

MPC-class landscape: the editor opens as a full-screen layer; no remote-control page exists for editing (touch only); the Kick encoders drive `oscN.frame` while editing (preview).

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| View | Tap | Cycle 2D -> 3D -> SP [S p.39] | `2D|3D|SP` segmented switch |
| View | Horizontal drag | `oscN.frame` scrub | FRAME slider |
| 3D view | Two-finger drag / pinch | Tilt / zoom | Zoom buttons |
| EDIT button | Tap | Opens editor [S p.40] | Module menu > Edit wavetable |
| Wave canvas | One-finger drag with the active tool | Draws along the grid; loupe magnifies under the finger | Tap tool then tap a grid cell |
| Wave canvas | Two-finger drag / pinch | Pan / zoom | Zoom buttons |
| Tool strip | Tap | Selects tool (Flat default) [S p.276] | none |
| Grid field | Tap | Choose 1..256 divisions [S p.276] | Numeric |
| Wave canvas | Long-press then drag with Nudge tool | Vertical move, clip at limits | Process commands |
| FFT bars | Drag across bins | Sets amplitude; Even / Odd toggles restrict bins; Snap toggles 0/25/50/75/100 % [S p.280] | Numeric bin entry (tap bar twice) |
| Phase bars | Drag; "Nudge" toggle moves all phases right [S p.278] | Phase offsets | Numeric |
| FFT menu | Tap command | Applies at the last touched bin [S p.279-280] | none |
| Thumbnail | Tap | Selects frame | FRAME stepper |
| Thumbnail | Long-press, drag | Reorders [S p.275] | SORT menu |
| Select range toggle | Tap, tap first, tap last | Range selection for copy / remove [S p.275] | none |
| Copy / Paste / + / - | Tap | [S p.282-283] | Menu |
| REMOVE / SORT / MORPH | Tap, choose entry | Command with confirm for destructive ones [S p.282-289] | none |
| Process `run` | Tap | Command applied to this frame / all frames | Undo |
| Formula field | Type, Apply (or Enter) | Plots; variables `y`/`z` replace all frames [S p.306] | Presets menu |
| Import | Choose file, method, Import | New table; progress bar for FFT methods [S p.292] | none |
| Export | Menu > Export > format | File via share sheet [S p.285] | none |
| Close `x` | Tap | Leaves editor, keeps table in the preset (embedded, [S p.290]) | Swipe down |
| Undo / Redo | Buttons (editor has its own stack, one entry per stroke or command) | [S p.289 note] | Two-finger tap |
| Modifier parameters (Layers) | Knob / slider as in 01 | Modifier value at the selected keyframe | Numeric |

## 4. States and edge cases

* Built-in tables (13) are read-only: first edit makes a copy named "<table> (edit)" and marks `*`. Factory (ContentPack) tables cannot be overwritten, only "Save as" [S p.290].
* Frame limits: 2048 samples per frame, 1..256 frames; MORPH needs more than 1 and fewer than 256 frames [S p.289]; `+` disabled at 256.
* Built-in tables have 1 (basic shape) or 64 (morph) frames; FRAME slider is a 0..100 % position over 64 frames (README); a custom table keeps the same slider semantic (0..100 % over its frame count).
* Preset size: the state limit is 1 MiB (ARCHITECTURE 5.2); a 256-frame 32-bit table is 2 MB, so embedded tables are stored compressed and the editor shows the estimated size; above the limit the Save / Embed button warns.
* Quality tier: editor preview uses the tier's interpolation (linear on Medium / Low); the editor always edits at full precision.
* Destructive commands (Init All, Remove ..., Resize ...) ask confirmation; every command is undoable until the editor closes.
* Audio while editing: edits go to the audio engine as a pre-built table swap between blocks (README: `useExternalData`); the swap happens at stroke end (draw preview is UI only) so the audio never allocates.
* Imported stereo files use the left channel for Osc 1 / 3 and the right channel for Osc 2 [S p.291].
* Image import: non 8-bit PNG shows an error, colour images use luminance.
* Missing file on load: dialog lists the path, offers "Locate file" / "Use embedded copy" [S p.302].
* Disabled controls: Morph when 1 frame; Sort when 1 frame; Paste when clipboard empty; Spectral import when file shorter than 2048 samples.
* Mobile memory: the editor holds one undo copy per command (wavetable <= 2 MB); undo depth 50 in the editor.

## 5. Kick remote pages

The editor is touch only. The view gets one page per oscillator ("Osc N Wave"), repeated for osc1, osc2, osc3:

| Page | Params (<= 8) |
|------|---------------|
| Osc N Wave | `oscN.wavetable`, `oscN.frame`, `oscN.frame_spread`, `oscN.phase`, `oscN.random_phase`, `oscN.warp.amount`, `oscN.morph.amount`, `oscN.morph.spread` |

## 6. Coverage check

| Key group (params.json) | Where in section 2 |
|-------------------------|--------------------|
| `oscN.wavetable` | 2.1 name / arrows, 2.2 header table name |
| `oscN.frame` | 2.1 FRAME slider and view drag, 2.2 FRAME stepper, 2.8 |
| `oscN.frame_spread` | 2.1 ghost frame markers |
| `oscN.phase`, `oscN.random_phase` | 2.1 phase line overlay |
| `oscN.warp.type`, `.amount`, `.phase`, `.spread` | 2.1 warp preview |
| `oscN.morph.type`, `.amount`, `.spread` | 2.1 SP / morph preview |
| `oscN.level`, `oscN.pan`, `oscN.transpose`, `oscN.tune`, `oscN.on`, `oscN.destination`, `oscN.unison.*` | not part of the view: 02 |
| `lfoN.shape` | import tail (animate WT POS), 05 |

Wavetable data itself (frames, components, keyframes) has no parameter key: it is content (`.sbwt`) or embedded content in the preset (ARCHITECTURE 5.2, 6).

## 7. Open questions (owner)

1. ARCHITECTURE 3.3 says "Mobile / MPC: no editor". This design includes a mobile editor because the brief asks for it. Confirm, and confirm the editor data model is Vital's group / component / keyframe model with Serum's frame operations as commands on top.
2. The core needs an API to hand a user wavetable to the engine (content id + hash, plus a new `oscN` parameter or state field) and a core-side wavetable build function used by the editor and the content tools. Not designed in params; needs an owner decision (ParamInfo vs. state-only).
3. Preset size: embedded tables vs. the 1 MiB state limit (compression ratio, or raise the limit for presets only).
4. Export formats: keep all five Serum export options, or only WAV 16 / 32 bit and single-cycle (hardware `.256` is niche)?
5. Formula parser: implement the Serum grammar as is (including `q` FFT mode) or a subset? The manual does not give the full grammar for `?:` and `%` (they appear only in examples) `[S unclear]`.
6. Image import and Resample/Render OSC Warp are Serum-only; confirm both are wanted.
