# Shell layout and interaction model – mobile UI design

Scope: the frame around every page (top bar, macros, page tabs, keyboard / performance strip) and the rules every control follows.
Excluded: Kick (preset browser, grid / sequencer / arranger / clips). Serum Arpeggiator and Clips: see section 7.
Cites: `[S p.n]` Serum 2 manual, `[V p.n]` Vital unofficial manual. Key families: `oscN` = osc1..osc3, `envN` = env1..env6, `lfoN` = lfo1..lfo8, `modNN` = mod01..mod64 (slot keys: source, destination, amount, power, bipolar, stereo, bypass).
Status words: `HAVE` = engine/core supports it today; `HAVE (UI)` = pure UI, no engine need; `GAP` = engine/core lacks it, UI is still designed here.

## 1. Function inventory

### 1.1 Top bar, presets, main menu

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Preset name with prev / next arrows (same folder) [S p.18] [V p.3] | both | none (preset metadata, ARCHITECTURE 5.2) | HAVE |
| Preset browser, search, tags, ratings, preview, favourites [S p.18-19, App. B] [V p.3] | both | none | excluded: Kick |
| Save preset (disk button), overwrite-same-name shortcut [S p.21] | both | none | HAVE (`.sbpreset`) |
| Preset metadata ARTIST / DESC [S p.21] | Serum | none (`author`, `tags` in `.sbpreset`) | HAVE |
| Embed wavetable / sample in preset [S p.22] | Serum | none (embedded content, ARCHITECTURE 5.2) | HAVE (wavetable); GAP (sample: no sample oscillator exposed, PARAMETERS "Not exposed") |
| Init Preset [S p.20, p.324] [V p.4] | both | all keys to `ParamInfo` default | HAVE |
| Init LFOs + LFO mods, Init Modulations [S p.324] | Serum | `lfoN.*`, `rlfoN.*`, `mod01.source` .. `mod64.bypass` to default | HAVE (UI) |
| Revert to Saved [S p.324] | Serum | whole state | HAVE (UI: keeps loaded state copy) |
| Save as Default Preset (Init loads it) [S p.324] | Serum | whole state | GAP (core has fixed defaults; needs a user default-state slot) |
| Undo / Redo buttons [S p.27] | Serum | any key, mod slot, wavetable, LFO / envelope shape | HAVE (UI) |
| A/B compare of two states (owner request; Serum only suggests undo/redo comparison [S p.27]) | design | whole state | HAVE (UI, uses `StateIO`) |
| MAIN volume knob with stereo meter [S p.27] | Serum | `master.volume` | HAVE; meter HAVE (UI, from output buffer) |
| Oscilloscope and spectrum analyzer of the output [V p.42] | Vital | none | HAVE (UI) |
| Drag last note / chord out as audio to the DAW, Renders folder [S p.22-23] | Serum | none | GAP (needs offline render of last note; desktop: drag, mobile: Share button) |
| Main menu: About, manual, update check [S p.324] | Serum | none | not applicable (store / host handles updates) |
| Main menu: Load / Save MIDI map, Load tuning, MPE entries [S p.325-326] | Serum | `master.bend_range` | owned by 07 (voice / MIDI / global) |
| Main menu: Render OSC Warp, Resample to wavetable [S p.324] | Serum | `oscN.warp.*` | owned by 03 |
| UI resize menu, Set x% as default, Default (100 %) [S p.36] [V p.3] | both | none | HAVE (UI) |
| Preferences: Help tooltips, Param value tooltips, Double-click action, Mouse-wheel control, Keyboard shortcuts, Default waveform view 2D/3D [S p.313-314] | Serum | none (UI settings file) | HAVE (UI) |
| Preferences: MPE, limit mod depth on drop, mod wheel to WT pos, silence on host stop, MIDI map from preset, ultra quality render [S p.314-315] | Serum | none | owned by 06 / 07 / 03 |

### 1.2 Macros, page tabs

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| 4 macro knobs, always visible [V p.23] | Vital | `macro.1`, `macro.2`, `macro.3`, `macro.4` | HAVE |
| Macros 5-8 (Serum has 8) [S p.206] | Serum | none | GAP |
| Assign macro to a control (drag, count badge, tooltip lists destinations, swap two macros) [S p.207-208] | Serum | `mod01.source` .. `mod64.amount` (source `macro1`..`macro4`) | HAVE; gesture design in 06 |
| Macro as modulation destination (aux source chain) [S p.206] | Serum | `macro.1` .. `macro.4` are modulatable | HAVE |
| Page tabs: Serum OSC / MIX / FX / MOD / GLOBAL (+ CLIP, ARP excluded) [S p.16, p.144, p.312]; Vital Voice / Effects / Matrix / Advanced [V p.3] | both | none | HAVE (UI) |

### 1.3 Keyboard and performance strip

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| On-screen keyboard: play, show incoming notes [S p.25, p.267] [V p.23] | both | NoteOn / NoteOff events | HAVE |
| Velocity from touch (Serum / Vital: mouse, fixed) | design | NoteOn velocity; `master.velocity_track` | HAVE |
| Pitch wheel (spring) [V p.23] | Vital | event PitchBend; `master.bend_range`; source `pitch_wheel` | HAVE |
| Mod wheel [V p.23] | Vital | event ModWheel; source `mod_wheel` | HAVE |
| Pressure / aftertouch from touch | design | event Aftertouch; source `aftertouch` | HAVE |
| TRANSPOSE field, +-24 semitones, arrows, wheel = 12 [S p.267] | Serum | `master.transpose` (range -48..48; UI limits the strip to +-24, the sheet allows full range) | HAVE |
| Octave shift of the visible keys (buttons) | design | none | HAVE (UI) |
| KEY + SCALE: incoming notes conform to the scale [S p.268] | Serum | none | GAP |
| Global snap: force played notes into a scale [V p.7] | Vital | none | GAP |
| SWING [S p.269] | Serum | none | excluded: Kick (applies to CLIP / ARP grid only) |
| OSC MAPPING: note ranges, FOLD, WARP flag, velocity ranges per oscillator [S p.269-273] | Serum | none | GAP |
| Hold / latch (mobile need: no sustain pedal) | design | NoteOn / NoteOff events | HAVE (UI) |

### 1.4 Knobs, sliders, context

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Drag to change, value pop-up [S p.25] [V p.4] | both | any key | HAVE (UI) |
| Fine adjust (Shift / Ctrl drag) [S p.25, p.349] [V p.4] | both | any key | HAVE (UI) |
| Mouse-wheel adjust [S p.25] | Serum | any key | HAVE (UI, desktop shell only) |
| Numeric entry (double-click / Alt-click / "Enter Value"), cancel by empty text [S p.25] [V p.4] | both | any key | HAVE (UI) |
| Reset to default (Cmd/Ctrl-click, double-click, "Set to Default") [S p.26, p.349] [V p.4] | both | any key | HAVE (UI) |
| Context menu: mod source, aux source, custom curve, bypass / remove modulator, remove all [S p.200-201] | Serum | `modNN.*` | HAVE (except custom curve: GAP, PARAMETERS "Line-map curve"); sheet in 06 |
| MIDI Learn, Remove MIDI CC [S p.26, p.201] | Serum | none | GAP (no CC to parameter map in core) |
| Lock Parameter (survives preset load) [S p.26] | Serum | none | GAP |
| Tooltips with name / description; modulation sources listed on hover [S p.24] | Serum | `ParamInfo.name`, `mod01.source`, `mod01.destination` (slots 01..64) | HAVE (UI) |
| Module menu: Lock Module, Init Module, Copy, Paste, Copy (w/ mods) [S p.33-34] | Serum | all keys of the module + its `modNN` slots | Init / Copy / Paste HAVE (UI); Lock GAP |
| Drag module label onto another: swap, Alt-copy, Shift-Alt-copy with mods [S p.34, p.145, p.349] | Serum | same | HAVE (UI) |
| Enable oscillator / filter power button, dimmed panel when off [S p.28] [V p.6] | both | `oscN.on`, `filterN.on`, `noise.on`, `fxfilter.on` | HAVE |
| Choose option with drop-down, < > arrows, wheel [S p.29] | Serum | stepped keys (`oscN.wavetable`, `filterN.model`, ...) | HAVE (UI) |

## 2. Screen design

Breakpoints (shell width in points): phone portrait < 600; tablet portrait and phone landscape 600-1000; tablet landscape and MPC-class landscape >= 1000 (MPC screen size and ratio **(unverified)**, read it from the device). Minimum touch target 44 pt, minimum gap 8 pt.

### 2.1 Phone portrait (390 x 844 reference)

```
+------------------------------------------+
| [<] Preset name here          [>]  U R A?|  top bar 44: prev/next, Undo, Redo, menu
+------------------------------------------+
| (M1)  (M2)  (M3)  (M4)     MAIN (v) ||   |  macros 64: 4 knobs + main vol + meter
+------------------------------------------+
| OSC MIX FLT ENV LFO MOD FX GLB  ->       |  page tabs 44, scrolls sideways
+------------------------------------------+
|                                          |
|        PAGE CONTENT                      |
|   one big visual + max 4 main knobs      |
|   + "more" row                           |
|                                          |
+------------------------------------------+
| [P]| [M]|  C  D  E  F  G  A  B  C  D     |  strip 120: pitch, mod, keys
| oct-/+  TRANSPOSE +0  HOLD  KEY/SCALE     |  strip header 36
+------------------------------------------+
```

* Top bar: left to right: prev arrow, preset name (tap = Kick opens browser: excluded), next arrow, Undo, Redo, menu (A/B, Init, Save, Revert, scale, help mode, resize, settings).
* Macros: four 56 pt knobs; the macro with assigned destinations shows a count badge; tap a macro chip while a control is in "assign" mode = modulation by touch (06).
* Tab order: `OSC` (02, 03), `MIX` (02), `FLT` (04), `ENV` (05), `LFO` (05), `MOD` (06), `FX` (08), `GLB` (07). `ENV` and `LFO` are two tabs on phone, one page on tablet.
* Keyboard: 10 white keys visible on 390 pt (1.5 octaves); horizontal drag on the octave bar scrolls; `oct -/+` shifts by 12.
* Page content area is what is left (about 530 pt): enough for the visual (>= 200 pt) plus two rows of knobs.

### 2.2 Phone landscape and tablet portrait (844 x 390 / 820 x 1180)

```
+----------------------------------------------------------------------+
| [<] Preset name [>]  U R | (M1)(M2)(M3)(M4)  MAIN | OSC MIX FLT ... GLB |
+----------------------------------------------------------------------+
|  PAGE A (visual + knobs)        |  PAGE B (e.g. filter visual + knobs)|
|                                 |                                     |
+---------------------------------+-------------------------------------+
| [P][M] |  keyboard, 2 octaves           | oct  TRANSPOSE  HOLD  SCALE |
+----------------------------------------------------------------------+
```

Landscape phone: top bar and macros share one 44 pt row; keyboard strip 72 pt; two panels side by side (the panel pairs below). Tablet portrait: same as landscape phone with taller panels and a 3-row content area.

### 2.3 Tablet landscape and MPC landscape (>= 1000)

```
+-----------------------------------------------------------------------------+
| [<] Preset name [>]  U R A/B  menu     (M1)(M2)(M3)(M4)       MAIN [meter]   |
+-----------------------------------------------------------------------------+
| OSC1  | OSC2  | OSC3 | NOISE         | FILTER 1  | FILTER 2   (Vital-like    |
| view+ | view+ | ...  | MIX           | view+     | view+      grid: every    |
| knobs | knobs |      |               | knobs     | knobs      page = tile)   |
+-----------------------------------------------------------------------------+
| ENV 1-6 tile            | LFO 1-8 tile            | MOD / FX tile (tabs)    |
+-----------------------------------------------------------------------------+
| [P][M] | keyboard 3 octaves                        | oct TRANSPOSE HOLD SCALE|
+-----------------------------------------------------------------------------+
```

* The tab bar disappears: each page is a tile; tapping a tile header expands it to 2x2 tiles ("focus"); the tab names remain as a jump list in the menu.
* MPC-class: same tiles, no hover, no right-click, everything reachable by touch and by the hardware encoders through Kick remote pages (section 5).

### 2.4 Sheets owned by this file

Long-press sheet (any control):

```
+--------------------------------------+
| Filter 1 Cutoff              [?]     |
| 1 250 Hz      [-]  [ value box ]  [+]|
| default 12 000 Hz     range 20..20k  |
| [Reset] [MIDI learn] [Lock] [Assign mod] |
| Mod: LFO 3 +35%   Env 2 -12%         |
+--------------------------------------+
```

Module sheet (long-press module header: `OSC 1`, `FILTER 2`, ...): `Copy | Copy with mods | Paste | Paste with mods | Swap with ... | Init | Lock module | Edit note range (osc only)`.

Osc mapping sheet (GAP in engine):

```
+----------------------------------------------+
| MAPPING  (KEY) (VEL)                         |
| OSC1  |======[ C1 ------- B5 ]==========|    |
| OSC2  |   [ C3 -------------------- C8 ]|    |
| OSC3  ... NOISE ...                          |
| keyboard graphic:  | | |_|_| | | |_|_|       |
|  [x] FOLD   [ ] WARP  (per row)              |
+----------------------------------------------+
```

Scale sheet (GAP): `KEY` (C..B), `SCALE` list, mode `Conform` (snap to nearest in-scale) or `Mark only` (highlight keys, no snapping). In-scale keys have a tinted background, the root is accent coloured [S p.268].

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| Knob / slider | Vertical drag (horizontal for sliders) | Value changes; floating value bubble above the finger [S p.25] | Long-press sheet: `-` / `+` buttons (one step) |
| Knob / slider | Drag, then slide the finger sideways away from the control: 3 zones (normal, x0.25, x0.05) shown as a ladder in the bubble | Fine adjust (replaces Shift / Ctrl) [S p.25] [V p.4] | `-` / `+` nudge buttons in the sheet; settings: "Fine step" |
| Knob / slider | Double-tap | Per setting "Double-tap action" (default: reset to default; option: open numeric entry) [S p.25, p.313] [V p.4] | Sheet: `Reset` button / value box |
| Knob / slider | Long-press (400 ms) without moving | Opens the long-press sheet: exact value, numeric keypad, reset, MIDI learn, lock, assign / list modulation [S p.26, p.200] | Menu "Help mode" then tap |
| Numeric entry field | Type value (units allowed: `1.2k`, `-6`), Enter | Value set, clamped to range; empty text + Enter = cancel [V p.4] | `-` / `+` buttons |
| Stepped control (model, shape, type) | Tap | Opens option list; `<` `>` arrows beside it step through [S p.29] | Swipe left / right on the control = previous / next |
| Power button on module header | Tap | `oscN.on` / `filterN.on` / `noise.on` toggles; panel dims when off [S p.28] | Module sheet |
| Module header | Long-press | Module sheet (copy / paste / init / lock) [S p.33] | Top-bar menu > Module |
| Module header | Drag onto another header (tablet / landscape) | Swap; with copy toggle held: copy; with copy + mods toggle: copy with modulation [S p.34] | Module sheet > Copy, then Paste on the other module |
| Visual (wavetable, filter, envelope, LFO) | Drag / pinch / double-tap on elements | Direct manipulation (details in 03, 04, 05) | Knobs under the visual |
| Help mode (`?` in top bar) | Toggle, then tap any control | Tooltip card instead of change: name, description, value, range, modulation sources [S p.24] | Long-press |
| Undo / Redo | Tap; two-finger tap = Undo, three-finger tap = Redo (tablet) | One entry per gesture (a whole drag = one entry); covers parameters, mod slots, wavetable, shapes [S p.27] | Menu entries; desktop Cmd/Ctrl-Z |
| A/B | Tap `A/B` in menu | Swaps live state with the stored other state; `Copy A to B` | Menu |
| Macro knob | Drag | `macro.1`..`macro.4` change; destinations move [V p.23] | Long-press sheet |
| Pitch wheel strip | Vertical drag, springs back on release | PitchBend event, range `master.bend_range` [V p.23] | Bend buttons +/- 1 semitone held (tablet) |
| Mod wheel strip | Vertical drag, stays | ModWheel event [V p.23] | Long-press sheet value |
| Key | Touch (Y position inside key = velocity: lower = louder) | NoteOn with velocity; finger slide up/down while held = aftertouch | Settings: fixed velocity slider 1..127 |
| Key | Slide horizontally across keys | Glissando (new NoteOn, previous NoteOff) | none needed |
| `oct -/+` | Tap | Visible keys shift by 12 semitones (UI only) | Horizontal drag on octave bar |
| TRANSPOSE | Tap arrows = +-1; vertical drag; double-tap = 0 | `master.transpose` [S p.267] | Sheet numeric entry |
| HOLD | Tap | Notes stay on until HOLD is off or key tapped again | none needed |
| KEY / SCALE | Tap | Opens scale sheet [S p.268] | Menu |
| UI size | Pinch on empty area of top bar, or menu `Size 75/100/125/150/200 %`; desktop: drag corner [S p.36] | Whole UI scales, tab bar and strip keep >= 44 pt targets | Menu, `Set as default` |
| Two-finger swipe up/down on the page content | Switch page (next / previous tab) | tab changes | Tab bar |

## 4. States and edge cases

* Dimmed module: power off = panel at 40 % opacity, still editable [S p.28]. A filter that is on but receives no routed input is also dimmed [S p.148].
* Value bubble format: always value + unit from `ParamInfo.unit` with `decimals` from the table; Hz above 1000 shown as kHz.
* Stepped keys: numeric entry accepts the choice index or a unique name prefix.
* Locked parameter: small padlock on the control; preset load keeps its value (GAP, needs a locked-key set in the state loader); manual edits still work [S p.26].
* Missing content: if a preset references a wavetable that is not installed, the oscillator shows a `missing` badge and plays the fallback table (ARCHITECTURE 5.2); the shell offers `Restore` (Kick content manager).
* Quality tiers (PARAMETERS "Quality tiers"): the tier caps voices, unison and bypasses reverb / flanger on Low. The shell shows a small tier chip in the menu; capped controls show the stored value but an amber outline and the tooltip "limited to N by quality tier". Values are never changed.
* Macros 5-8 do not exist: the macro row never shows them; the preset import of a Serum-like preset with 8 macros is out of scope.
* Keyboard on small phones: below 360 pt width the strip drops the TRANSPOSE field into the menu and keeps wheels + keys.
* Touch while a note is held and the page changes: held notes continue; wheels keep their value.
* Wheel mod sources (`pitch_wheel`, `mod_wheel`) are global per instance; MPE per-note is 07.
* Undo depth 200 entries; session-only, cleared on preset load (A/B states are kept).
* Right-click on desktop shells opens the same sheets as long-press; hover tooltips appear after 600 ms [S p.24].

## 5. Kick remote pages

| Page name | Params (<= 8) |
|-----------|---------------|
| Macros and Main | `macro.1`, `macro.2`, `macro.3`, `macro.4`, `master.volume`, `master.tune`, `master.transpose`, `master.bend_range` |

All other remote pages are defined in the area files 02 .. 08; the page order for Kick is assembled in 09.

## 6. Coverage check

| Key group (params.json) | Where in section 2 |
|-------------------------|--------------------|
| `macro.1` .. `macro.4` | 2.1 macro row, 2.3 header |
| `master.volume` | MAIN knob with meter (2.1) |
| `master.transpose` | keyboard strip TRANSPOSE (2.1) |
| `master.bend_range` | pitch wheel strip (2.1), range shown in the sheet |
| `master.tune` | long-press sheet / Kick page; main-tune row on the `GLB` page (07) |
| `master.velocity_track` | keyboard velocity mapping; edited on `GLB` (07) |
| `master.polyphony`, `master.legato`, `master.portamento.*` | not in the shell: 07 |
| `oscN.on`, `filterN.on`, `noise.on`, `fxfilter.on` | power button rule (section 3), pages in 02, 04 |
| `modNN.*` | context menu / macro assignment only referenced here; pages in 06 |
| `oscN.*`, `noise.*` | 02, 03 |
| `filterN.*`, `fxfilter.*`, `filter.routing` | 04 |
| `envN.*`, `lfoN.*`, `rlfoN.*` | 05 |
| `fx.order.*`, effect groups | 08 |

Mod sources used by the strip: `pitch_wheel`, `mod_wheel`, `aftertouch`, `velocity` (core `ModSource` table).

## 7. Open questions (owner)

1. Serum 2 Arpeggiator (manual p.244) and Clips (p.219): treated as sequencer features = excluded (Kick). Confirm.
2. Serum KEY / SCALE / OSC MAPPING and macros 5-8 need new engine / core features (all `GAP` above). Confirm they are wanted on the roadmap, because the UI is designed for them.
3. Double-tap default: reset (Vital) or numeric entry (Serum default)? The design uses reset and a setting.
4. Is the A/B compare (not in either synth) wanted in the first UI?
5. MPC screen resolution and which hardware controls (encoders, pads) reach the plug-in UI; the Kick remote pages above assume 8 encoders per page.
6. SWING is excluded with the grid features; confirm Kick owns it.
