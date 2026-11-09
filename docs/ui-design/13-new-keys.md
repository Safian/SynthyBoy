# New parameter keys for the owner-decided engine extensions – specification

Status: SPEC (2026-11-08; GROUPC 4.37 .. 4.39 added 2026-11-09); WP0 (table, state, content section, EngineCaps) implemented on the same day, see `WP0-REPORT.md` (corrections found while implementing are marked **[WP0 fix]**); WP1, WP2, WP3 implemented, see `WP123-REPORT.md` (assumptions A29-A37 there); WP4 to WP8 implemented, see `WP48-REPORT.md` (assumptions A38-A45 below; A45 by LFOVOICE-REPORT.md). Source decisions: `12-decisions.md` (Q2, Q6, Q8, Q9, Q13, Q14, Q15, Q16, Q17, Q18, Q19, Q22, Q23, Q24, Q26, Q27), gaps G01-G84 of `10-coverage-matrix.md` §3, design files 01-09 and 11. Where a design file is silent the text says **ASSUMPTION** (collected in §12).

Verified baseline (read from the repo on 2026-11-08):

| Fact | Value | Source |
|------|-------|--------|
| Keys in the table | 880 (table positions 0..879) | `docs/params.json` `count` |
| Modulatable keys / destination choices | 269 / `kNumModDestinations` = 270 (0 = none) | `param_table.cpp`, `params.json` |
| Mod sources | 28 (`kNumModSources`, choice 1..28) | `modulation.h` |
| Highest id value | 4293057762 = `hashKey("mod22.destination")` (table position 475). Ids are hashes, not a counter: see R1 | `params.json` |
| `kMaxParams` | 1024 (2048 from WP0, 4096 from B22) | `params.h` |
| State version | `kStateVersion` = 2, `kMaxStateBytes` = 1 MiB, `kMaxPresetFileBytes` = 1 MiB | `state.h`, `preset.cpp` |
| Built-in lists | `kNumBuiltinWavetables` 13, `kNumLfoShapes` 12, `kNumFxSlots` 9, 8 filter models, 6 filter styles | `params.h`, `param_table.cpp` |

## 1. Rules for every new key (apply to all tables below)

| # | Rule |
|---|------|
| R1 | **Id assignment.** `ParamInfo::id == hashKey(key)` (`hash.h`, 32-bit `h = 31 * h + c`). There is no sequential id to "continue after"; the highest existing id value (4293057762) carries no meaning. The sequential identity is the **table position** (`ParamInfo::order`). New keys take positions 880 and up, in the block order of §11. A new id is valid only if it collides with no other id (checked for all 604 keys below: no collision with the 880 existing ids or among themselves; snippet in §10). Ids are never reused or renumbered; a key string is never renamed. |
| R2 | **Physical append.** Every new key is appended at the END of `kRaw[]` (`param_table.cpp`) in the order of §11, even when it belongs to an existing group (for example `lfo9.*` after `reverb.chorus_rate`). Reason: the destination choice of `modNN.destination` is "every modulatable key in table order"; inserting a modulatable key anywhere but the end shifts the destination numbers stored in existing presets. The `group` string (UI hierarchy) is independent of the position. |
| R3 | **Choice lists are append-only.** Existing indices keep their name and meaning. Display names may be changed later (state stores the index), but an index never changes meaning once shipped. |
| R4 | **Host automation.** Every new key sets `kParamAutomatable` (like all 880 existing keys; the table has no non-automatable key). `kParamStepped` for choice, int, bool. No new key is `kParamHidden`. `kParamModulatable` only where the column "Mod" says yes; those 134 keys extend the destination choice (§3). |
| R5 | **Neutral defaults.** The default of every new key reproduces the version 2 sound exactly (no new module on, new sources none, new levels 0 dB, new ranges full). A version 2 document must load into the same audio. |
| R6 | **Naming.** Lowercase, dotted, ASCII, same grammar as the existing table (`<module>.<function>[.<sub>]`, `modNN` two digits, units never in the key). Display name `"Osc 1 Sample Start"` pattern, unique in the table (test `test_params.cpp:109`). Groups follow `"Area / Sub"`. All 604 keys were checked against `docs/params.json` for key-name collisions: none. |
| R7 | **Scope / smoothing legend** (column "Sc"): `V` = evaluated per voice (per-voice modulation sources apply), `G` = global, one value per instance (FX, mixer buses, global state, macros). Smoothing: `S` = ramped per block (control-rate smoothing, as `osc1.level`), `X` = stepped, applied at a block or note boundary (engine click guard: 10 ms crossfade when a running signal path changes), `N` = read at note-on only. |
| R8 | `ownEngine` (iOS `engine-own`) is **false** for all new keys at first (PARAMETERS "own engine" column); the UI shows the existing `[iOS: Fixed]` badge. |

Column legend of the key tables: **Type** float / int (grid of 1) / choice / bool; **Range** plain values; **Curve** `lin` or the `exp_skew` value; **Mod** y = `kParamModulatable`; **Kick** = page id in §9 (`more:` = `moreKeys` sheet of an existing page).

## 2. Mod-source and destination extension

`kNumModSources` 28 → 38. Choice value k + 1 for table index k, appended (never reorder):

| Choice | Key | Name | Polarity (`bipolar`) | Meaning | Event needed |
|-------:|-----|------|----------------------|---------|--------------|
| 29 | `macro5` | Macro 5 | no | `macro.5` | none |
| 30 | `macro6` | Macro 6 | no | `macro.6` | none |
| 31 | `macro7` | Macro 7 | no | `macro.7` | none |
| 32 | `macro8` | Macro 8 | no | `macro.8` | none |
| 33 | `lfo9` | LFO 9 | yes | `lfo9.*` | none |
| 34 | `lfo10` | LFO 10 | yes | `lfo10.*` | none |
| 35 | `lift` | Lift | no | Note-off (release) velocity 0..1, latched per voice at note-off; before note-off the value is 0 (Vital "Lift") | note-off velocity (CLAP `clap_event_note` velocity on `NOTE_OFF`, MIDI 0x80/0x90-0 velocity) |
| 36 | `slide` | Slide | no | MPE slide / Y: CLAP note expression `BRIGHTNESS` per note, or MIDI CC74 on the note's channel (MPE on); 0..1 | note expression |
| 37 | `oct_note` | Oct Note | no | Note within the octave, `(note mod 12) / 12` (verified in `voice_handler.cpp:191`, `note_in_octave`) | none |
| 38 | `poly_aftertouch` | Poly Aftertouch | no | Per-note pressure 0..1: CLAP note expression `PRESSURE`, MIDI poly key pressure, and MPE channel pressure of the member channel. `aftertouch` (choice 25) stays channel pressure | note expression |

**ASSUMPTION A1** (Q19 is "proposed", Q24 "decided and with Q19"): all four are included. **ASSUMPTION A2**: Serum's other sources (Active Voices, Voice Index, Voice Mod, Note-On Rand 2, Fixed; G67) are not in the delegation list and stay GAP.

Destination choice (`modNN.destination`, `kNumModDestinations` 270 → 442): the 172 modulatable new keys get the choice numbers 270..441 in table order. Per block: B01 270-273, B02 274-289, B04 290-319, B09 320-337, B10 338-356, B11 357-365, B12 366-403, B16 404-411, B14 412-423, B17 424-431, B18 432-441 (blocks without modulatable keys are skipped). Existing choice numbers 1..269 are unchanged.

## 3. Appended values of EXISTING choice lists (exact names, never renumber)

| List (keys using it) | Now | Appended index → exact name | New size |
|----------------------|----:|------------------------------|---------:|
| `oscN.wavetable` (osc1..3) | 13 | 13 → `Preset table` (the table named in the preset `content.wavetables[]`; Q8, 11 C1). `kNumBuiltinWavetables` stays 13 (built-ins only); new constant `kNumWavetableChoices` = 14 | 14 |
| `oscN.destination`, `noise.destination` | 5 | 5 → `Filter blend` (uses `oscN.filter_blend`), 6 → `None` (modulation source only, no audio) | 7 |
| `lfoN.shape` (lfo1..lfo10) | 12 | 12 → `Custom` (point list in `content.lfo_shapes[]`; 11 C2). `kNumLfoShapes` stays 12 (built-ins); new `kLfoShapeCustom` = 12 | 13 |
| `rlfoN.style` (rlfo1..4) | 4 | 4 → `Rossler attractor` | 5 |
| `filterN.model` (filter1, filter2, fxfilter) | 8 | 8 `Multi SVF`, 9 `Slope`, 10 `EQ 6 dB`, 11 `Ring Mod`, 12 `Sample Hold`, 13 `Smear`, 14 `French LP`, 15 `Add Bass`, 16 `Scream`, 17 `New Wsp`, 18 `DJ Mixer`, 19 `Diffusor`, 20 `EMS Ladder`, 21 `PZ SVF`, 22 `Normal`, 23 `Multi Morph`, 24 `Flanges`, 25 `Misc`, 26 `New` (22 .. 26: the remaining 43 Serum types, section 4.24; names provisional until the A/B listening of Q13; the index meaning in §4.11 / §4.24 is what is frozen; a display name may change) | 27 |
| `filterN.style` (filter1, filter2, fxfilter) | 6 | 6 `Style G`, 7 `Style H`, 8 `Style I`, 9 `Style J`, 10 `Style K`, 11 `Style L` (generic; the UI shows the per-model label of §4.11) | 12 |
| `fx.order.1` .. `fx.order.9` (+ new `fx.order.10..16`) via `kFxNames` | 9 | 9 `Hyper/Dimension`, 10 `Utility`, 11 `Bode`, 12 `Convolve`, 13 `Splitter L/H`, 14 `Splitter L/M/H`, 15 `Splitter M/S`. `kNumFxSlots` 9 → 16 | 16 |
| `delay.style` | 4 | 4 → `Tap->Delay` (Serum 2 manual p.166, MANUALCHECK 4.20; the four Vital styles keep their numbers) | 5 |
| `distortion.type` | 6 | 6 → `Tube` .. 13 → `X-Shaper (Asym)` (4.17) | 14 |
| `modNN.source` (mod01..mod64) | 28 | 29..38 of §2 (also the list of the new `modNN.aux_source`) | 38 |
| `modNN.destination` | 270 | 270..403 of §2 | 404 |

Block B21 appended the warp lists `kWarpTypes1/2/3` (shared by `oscN.warp.type` and `oscN.warp2.type`): 11 `Sync Var` .. 51 `AM from Y`, 41 entries, new size 52, `legacyChoiceMax` 10 (section 4.22). Lists that stay unchanged: `kLfoSync`, `kDivisions`, `kFxSync`, `kOscDestinations` of the existing keys, `kFilterRouting`, `kLfoSyncTypes`, `kMorphTypes`.

## 4. New keys, by block (block order = table append order)

Position ranges are table positions (`order`); counts in brackets.

### 4.1 B01 – Macros 5-8 (Q2, G01) – positions 880..883 [4] – area 01 / 06

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `macro.5` .. `macro.8` (4) | `Macro N` | float | 0 .. 100 | 0 | % | lin | G, S | y (macros are destinations, like `macro.1..4`, Serum "swap / macro as destination") | `macros_2` (new, Q2: "remote macro page x2") |

Group `Macros`. Same constructor as `SB_MACRO`. Source choices 29..32 (§2).

### 4.2 B02 – LFO 9 and 10 (Q17, G61) – positions 884..909 [26] – area 05

`lfo9.*` and `lfo10.*` are the 13 keys of `SB_LFO` with identical ranges, defaults, skews and flags as `lfo1.*`: `shape` (choice, 13 entries incl. `Custom`, def 0), `rate` (0.01..200 Hz, def 2, skew 0.25, mod), `sync` (`kLfoSync`, def 0), `division` (`kDivisions`, def 8), `sync_type` (`kLfoSyncTypes`, def 0), `phase` (0..360 deg, def 0, mod), `fade_in` (0..8000 ms, def 0, skew 0.4, mod), `delay` (0..4000 ms, def 0, skew 0.4, mod), `smooth` (bool, def on), `smooth_time` (1..1000 ms, def 5, skew 0.3, mod), `stereo` (-180..180 deg, def 0, mod), `keytrack_transpose` (int -60..36 st, def -12, mod), `keytrack_tune` (-100..100 ct, def 0, mod). Group `LFO 9` / `LFO 10` (+ `/ Key Follow`). Sc: `V` (retrigger types) or `G` per `sync_type` / `mono` like LFO 1..8, `S`. Mod: 8 per LFO = 16 destinations. Kick: `lfo9`, `lfo10` (new pages, same layout as page `lfoN`); More sheet as for LFO 1..8. **ASSUMPTION A3**: the engine cap of 8 LFOs (`kNumLfos` in `engine-vital`) is raised or the extra two run in adapter-side code; see §10 and WP2.

### 4.3 B03 – LFO additions for all ten LFOs (Q17, G64) – positions 910..939 [30] – area 05

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `lfoN.mono` (N = 1..10) | `LFO N Mono` | bool | Off / On | Off (= polyphonic, v2 behaviour) | - | - | X (applied at note-on; On: one LFO instance shared by all voices, phase continues across notes) | no | `more:lfoN` |
| `lfoN.direction` | `LFO N Direction` | choice | 0 `Forward`, 1 `Reverse`, 2 `Ping pong` | 0 | - | - | X | no | `more:lfoN` |
| `lfoN.host` | `LFO N Host Sync` | bool | Off / On | Off | - | - | X | no | `more:lfoN` |

Notes: `host` On anchors the phase to the host transport position (tempo-synced modes: `phase = fract(songBeats / divisionBeats)`; Free (Hz) mode: **ASSUMPTION A4**, seconds since transport start x rate); with the transport stopped the LFO runs free. Serum's "10x range" (G64) is not a key: `lfoN.rate` already reaches 200 Hz (06 marks it HAVE). "Phase snap" is a UI grid aid. LFO swing (G16) is not decided (§8). Rossler is a new value of `rlfoN.style` (§3), not a key. `Path` is dropped (Q17).

### 4.4 B04 – Envelope beat units and Legato Inverted (Q17, G59, G60) – positions 940..981 [42] – area 05

Per envelope N = 1..6 (7 keys each):

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `envN.units` | `Env N Time Units` | choice | 0 `Milliseconds`, 1 `Beats` | 0 | - | - | N | no | `more:envN` |
| `envN.legato_inverted` | `Env N Legato Inverted` | bool | Off / On | Off | - | - | X | no | `more:envN` |
| `envN.delay_beats` | `Env N Delay (Beats)` | float | 0 .. 8 | 0 | beats | exp_skew 0.4 | V, S | y | `more:envN` |
| `envN.attack_beats` | `Env N Attack (Beats)` | float | 0 .. 16 | 0.0625 | beats | 0.3 | V, S | y | `more:envN` |
| `envN.hold_beats` | `Env N Hold (Beats)` | float | 0 .. 8 | 0 | beats | 0.4 | V, S | y | `more:envN` |
| `envN.decay_beats` | `Env N Decay (Beats)` | float | 0 .. 16 | 1 | beats | 0.3 | V, S | y | `more:envN` |
| `envN.release_beats` | `Env N Release (Beats)` | float | 0 .. 16 | 0.5 | beats | 0.3 | V, S | y | `more:envN` |

Semantics: with `units = Beats` the five stage times come from the `*_beats` keys (converted to seconds with the host tempo at the start of each stage), `delay/attack/hold/decay/release` (ms) are ignored (their values are kept, so switching back restores the sound) and `sustain` and the three curves are shared. The ms keys stay destinations; their modulation is inert in Beats mode. The UI snaps the beat keys to the division grid but the key is continuous (**ASSUMPTION A5**: Serum shows note divisions; a continuous beat value is a superset and imports 1:1). `legato_inverted` On: the envelope retriggers on a legato note-on although `master.legato` is On. Group `Envelope N / Beat Units`. Decimals 3 for the beat keys.

### 4.5 B05 – Velocity / Note curve options (Q17, G66) – positions 982..983 [2] – area 05 / 06

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `curve.velocity.legato` | `Velocity Curve Legato` | bool | Off / On | Off | - | - | G, X | no | `master_expression` |
| `curve.note.legato` | `Note Curve Legato` | bool | Off / On | Off | - | - | G, X | no | `master_expression` |

On: during a legato transition the curve output glides with `master.portamento.time` (Serum "Legato (Portamento Time)"). The curve points are not keys: `content.curves` (§5). Sources `velocity` (23) and `note` (24) keep their choice numbers and read through the curve (identity by default).

### 4.6 B06 – Matrix aux source (Q18, G68) – positions 984..1239 [256] – area 06

Per slot NN = 01..64, appended as one block of 4 x 64 (slot-major: `mod01.aux_*` first). `ModSlotParams` (`modulation.h`) gets four more indices.

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `modNN.aux_source` | `Mod NN Aux Source` | choice (`ChoiceSet::modSource`, 0 = none + 38 sources) | 0 .. 38 | 0 (none) | - | - | per source (V / G), S | no | `mod_slot_N_aux` (slots 1-4), Matrix view for 5-64 |
| `modNN.aux_inv` | `Mod NN Aux Invert` | bool | Off / On | Off | - | - | S | no | same |
| `modNN.aux_power` | `Mod NN Aux Curve` | float | -10 .. 10 | 0 | none | lin (same law as `modNN.power`) | S | no | same |
| `modNN.aux_output` | `Mod NN Output Scale` | float | 0 .. 200 | 100 | % | lin | S | no | same |

Math (**ASSUMPTION A6**, the manual p.211-212 gives the words, not the formula): `out = curve(src) * amount * aux * (aux_output / 100)` with `aux = 1` when `aux_source = 0` (then `aux_inv`, `aux_power` have no effect); otherwise `aux = curve_aux(inv ? 1 - a : a)`, where `a` is the aux source mapped to 0..1 (a bipolar source maps as `(x + 1) / 2`). Not added: per-slot RISE / FALL slew and free-form remap curves (G69; Q20 is only proposed, §8). Group `Mod Matrix / Slot NN`. The `OUT` meter is UI.

### 4.7 B07 – Key/Scale, tuning, MPE (Q2, Q23, Q24; G05, G10, G11) – positions 1240..1248 [9] – area 07 / 01

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `master.scale.key` | `Scale Key` | choice | 0 `C`, 1 `C#`, 2 `D`, 3 `D#`, 4 `E`, 5 `F`, 6 `F#`, 7 `G`, 8 `G#`, 9 `A`, 10 `A#`, 11 `B` | 0 | - | - | G, X | no | `master_scale_tuning` |
| `master.scale.type` | `Scale Type` | choice | 0 `Chromatic (off)`, 1 `Major`, 2 `Natural Minor`, 3 `Harmonic Minor`, 4 `Melodic Minor`, 5 `Dorian`, 6 `Phrygian`, 7 `Lydian`, 8 `Mixolydian`, 9 `Locrian`, 10 `Major Pentatonic`, 11 `Minor Pentatonic`, 12 `Blues`, 13 `Whole Tone`, 14 `Diminished (H-W)`, 15 `Diminished (W-H)`, 16 `Phrygian Dominant`, 17 `Hungarian Minor`, 18 `Double Harmonic`, 19 `Hirajoshi` | 0 | - | - | G, X | no | `master_scale_tuning` |
| `master.scale.mode` | `Scale Mode` | choice | 0 `Conform`, 1 `Mark only` | 0 | - | - | G, X | no | `master_scale_tuning` |
| `master.tuning.a4` | `Concert Pitch A4` | float | 400 .. 480 | 440 | Hz | lin, 2 decimals | G, N (retunes new notes; sounding notes glide over 20 ms) | no | `master_scale_tuning` |
| `master.tuning.scale` | `Tuning Scale` | choice | 0 `12-TET`, 1 `Pythagorean`, 2 `Just intonation`, 3 `User file` | 0 | - | - | G, N | no | `master_scale_tuning` |
| `master.tuning.mts_esp` | `Tuning MTS-ESP` | bool | Off / On | Off | - | - | G, N | no | `master_scale_tuning` |
| `master.tuning.mts_note_on_only` | `Tuning MTS-ESP Note-On Only` | bool | Off / On | Off | - | - | G, X | no | `master_scale_tuning` |
| `master.mpe.on` | `MPE On` | bool | Off / On | Off | - | - | G, X | no | `master_expression` |
| `master.mpe.bend_range` | `MPE Pitch Bend Range` | int | 1 .. 96 | 48 | semitone | lin | G, S | no | `master_expression` |

Semantics:
* **Key/Scale** conforms incoming note numbers before the voice allocator (a note outside the scale moves to the nearest in-scale note; a tie goes down - **ASSUMPTION A7**). `Mark only` changes nothing audible (UI highlight). `Chromatic (off)` is the identity. The scale list is **ASSUMPTION A8** (Serum's manual p.268 says "a very thorough list" without naming it); the list is append-only after shipping.
* **Tuning.** `User file` takes `content.tuning` (§5). `mts_esp` On: when an MTS-ESP master is connected it overrides `scale` and `a4`; with no master the engine falls back to `scale` (the shell reports "no master"). `mts_note_on_only` (listed in 07 §2.4 UI, not named in the delegation text: **ASSUMPTION A9**, drop it if not wanted). `master.tune` and `oscN.tune` stay and add on top. The oscillator pitch mode "Step (MTS-ESP)" (G26) is not decided and not specified here.
* **MPE.** `on` makes the engine apply per-note CLAP note expressions (`TUNING` -> pitch, `PRESSURE` -> source `poly_aftertouch`, `BRIGHTNESS` -> source `slide`) and read MPE member channels. `bend_range` replaces `master.bend_range` (0..24, global channel) for the per-note bend while MPE is on (G14 separate up / down is not decided). Serum's "MPE Y bi-directional" and "Pitch Bend maps to Expr X" are not keys: the per-slot `modNN.bipolar` covers the first and per-note bend is applied to the pitch directly (**ASSUMPTION A10**); the quick maps (XYZ -> Macro, Y -> Mod Wheel) are UI actions that create matrix slots.
* **Sustain pedal (Q22):** no key. It is an event: `Event::Type::sustainPedal { bool down }` (engine-vital already implements `sustainOn/Off`, 07 §7.2); CLAP / VST3 CC64 is translated in the shells. Not automatable as a parameter.

### 4.8 B08 – Oscillator mapping (Q2, G06) – positions 1249..1266 [18] – area 07 / 01 / 02

Decided in Q2 ("Osc Mapping on the roadmap, in the core") although not in the delegation list; included (**ASSUMPTION A11**). Per oscillator N = 1..3:

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `oscN.map.key_low` | `Osc N Key Low` | int | 0 .. 127 (MIDI note) | 0 | none (UI shows a note name) | lin | N | no | `oscN_type_map` |
| `oscN.map.key_high` | `Osc N Key High` | int | 0 .. 127 | 127 | none | lin | N | no | `oscN_type_map` |
| `oscN.map.vel_low` | `Osc N Velocity Low` | int | 1 .. 127 | 1 | none | lin | N | no | `oscN_type_map` |
| `oscN.map.vel_high` | `Osc N Velocity High` | int | 1 .. 127 | 127 | none | lin | N | no | `oscN_type_map` |
| `oscN.map.key_fold` | `Osc N Key Fold` | bool | Off / On | Off | - | - | N | no | `oscN_type_map` |
| `oscN.map.key_warp` | `Osc N Key Warp` | bool | Off / On | Off | - | - | N | no | `oscN_type_map` |

A note or velocity outside the range silences the oscillator for that note. `key_fold`: notes outside the key range are folded by whole octaves into it (no effect when the range is narrower than an octave). `key_warp`: the oscillator keeps running silently as FM / RM source of another oscillator outside its range. If `low > high` the engine swaps them. A `Unit::note` is not added (append-only enum, optional later). Group `Osc N / Mapping`.

### 4.9 B09 – Oscillator type, sample oscillator, warp slot 2 (Q6, Q9; G20, G33) – positions 1267..1359 [93] – area 02

Layout per oscillator, osc 1 = 1267..1297, osc 2 = 1298..1328, osc 3 = 1329..1359 (31 keys each: `type`, 26 sample keys, 4 warp2 keys).

**Oscillator type.** Q6: "new oscillator type + key block". The type is a key, not a new `oscN.wavetable` value, so the wavetable choice stays a pure list of tables.

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `oscN.type` | `Osc N Type` | choice | 0 `Wavetable`, 1 `Sample` (append later: `Multisample`, `Granular`, `Spectral`; G19, G21, G22 stay GAP) | 0 | - | - | V, X (10 ms crossfade) | no | `oscN_type_map` |

With type `Sample`, `oscN.wavetable`, `frame`, `frame_spread` and `morph.*` are ignored (UI dims them); `on`, `level`, `pan`, `transpose`, `tune`, `phase` (start phase), `random_phase` (Serum RAND, Vital `sample_random_phase`), `destination`, `unison.*`, `warp.*`, `warp2.*` and the new mixer keys (§4.10) apply. The Vital sampler "pitch" is `oscN.transpose` / `oscN.tune` (no new key). **ASSUMPTION A12**: the Vital import (11 §1.2.3) maps a custom `settings.sample` to the first oscillator with `on = 0` (type `Sample`) and reports it; `noise.*` stays the built-in white noise. **Warp on a sample voice (decided in WP4, A38):** a sample has no cycle, so the phase domain warps (Sync, Formant, Bend, Squeeze, Pulse Width) are inert on a sample; Quantize reduces the resolution of the output, FM (osc A / B) modulates the read speed, RM (osc A / B) multiplies the output, in both slots.

**Sample block** (prefix `oscN.sample.`, group `Osc N / Sample`; the sample itself is `content.samples[]` §5, never in the key list):

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `start` | `Osc N Sample Start` | float | 0 .. 100 | 0 | % of file length | lin, 2 dec | V, N | no | `oscN_sample_1` |
| `end` | `Osc N Sample End` | float | 0 .. 100 | 100 | % | lin, 2 dec | V, N | no | `oscN_sample_1` |
| `root` | `Osc N Sample Root Note` | int | 0 .. 127 | 60 | none (note) | lin | V, N | no | `oscN_sample_1` |
| `keytrack` | `Osc N Sample Pitch Tracking` | bool | Off / On | On | - | - | V, X | no | `oscN_sample_1` |
| `loop_mode` | `Osc N Sample Loop Mode` | choice | 0 `One-shot`, 1 `Forward loop`, 2 `Reverse loop`, 3 `Forward/reverse loop`, 4 `Tailed` | 0 | - | - | V, N | no | `oscN_sample_1` |
| `loop_start` | `Osc N Sample Loop Start` | float | 0 .. 100 | 0 | % | lin, 2 dec | V, S | no | `oscN_sample_1` |
| `loop_end` | `Osc N Sample Loop End` | float | 0 .. 100 | 100 | % | lin, 2 dec | V, S | no | `oscN_sample_1` |
| `loop_xfade` | `Osc N Sample Loop Crossfade` | float | 0 .. 1000 | 0 | ms | 0.4 | V, N | no | `oscN_sample_1` |
| `loop_relative` | `Osc N Sample Relative Loop` | bool | Off / On | Off | - | - | V, N | no | `more:oscN_sample_2` |
| `loop_link` | `Osc N Sample Link Loop Length` | bool | Off / On | Off | - | - | V, N | no | `more:oscN_sample_2` |
| `loop_exit` | `Osc N Sample Exit Loop On Release` | bool | Off / On | Off | - | - | V, X | no | `more:oscN_sample_2` |
| `fade_edges` | `Osc N Sample Fade Edges` | choice | 0 `None`, 1 `1 ms`, 2 `2 ms`, 3 `4 ms`, 4 `8 ms`, 5 `16 ms`, 6 `32 ms`, 7 `64 ms`, 8 `128 ms` | 0 | - | - | V, N | no | `more:oscN_sample_2` |
| `normalize` | `Osc N Sample Normalize` | bool | Off / On | Off | - | - | V, N | no | `more:oscN_sample_2` |
| `reverse` | `Osc N Sample Reverse` | bool | Off / On | Off | - | - | V, N | no | `more:oscN_sample_2` |
| `slice_mode` | `Osc N Sample Slice Mode` | choice | 0 `Off`, 1 `Auto`, 2 `Manual` | 0 | - | - | V, N | no | `more:oscN_sample_2` |
| `slice_threshold` | `Osc N Sample Slice Threshold` | float | 0 .. 100 | 50 | % | lin | V, N | no | `more:oscN_sample_2` |
| `slice_play` | `Osc N Sample Slice Playback` | choice | 0 `Play slice to end`, 1 `Play single slice` | 0 | - | - | V, N | no | `more:oscN_sample_2` |
| `slice_root` | `Osc N Sample Slice Root Note` | int | 0 .. 127 | 36 | none (note) | lin | V, N | no | `more:oscN_sample_2` |
| `scan` | `Osc N Sample Scan` | float | -100 .. 100 | 100 | % of the range | lin | V, S | y | `oscN_sample_2` |
| `scan_range` | `Osc N Sample Scan Range` | choice | 0 `+-100 %`, 1 `+-200 %`, 2 `+-400 %`, 3 `+-800 %` | 0 | - | - | V, X | no | `oscN_sample_2` |
| `scan_reverse` | `Osc N Sample Scan Reverse` | bool | Off / On | Off | - | - | V, X | no | `oscN_sample_2` |
| `scan_lock_tempo` | `Osc N Sample Scan Lock To Tempo` | bool | Off / On | Off | - | - | V, X | no | `oscN_sample_2` |
| `scan_to_bpm` | `Osc N Sample Length To BPM` | bool | Off / On | Off | - | - | V, X | no | `oscN_sample_2` |
| `length_beats` | `Osc N Sample Length (Beats)` | float | 0.25 .. 64 | 4 | beats | 0.4 | V, N | no | `oscN_sample_2` |
| `uni_start` | `Osc N Sample Unison Start` | float | 0 .. 100 | 0 | % | lin | V, S | y | `oscN_sample_2` |
| `uni_span` | `Osc N Sample Unison Span` | float | 0 .. 100 | 0 | % | lin | V, S | y | `oscN_sample_2` |

Semantics (**ASSUMPTION A13** for every number the manual does not give; manual p.68-84): `scan` is the playback speed relative to the original, `scan = 100` with `scan_range = +-100 %` is forward at the original speed (the default plays the file as recorded), `-100` plays it backwards, `0` freezes; the other ranges scale the maximum (speed = scan/100 x range). `scan_to_bpm` On: the speed is set so that the (start..end) region lasts `length_beats` at the host tempo (the scan knob becomes RATE in the UI). `scan_lock_tempo` On: speed multiplied by host tempo / 120 (reference tempo 120). `uni_start` randomises the start of each unison voice within the given share of the region; `uni_span` spreads fixed start offsets over the unison voices. `loop_mode` `One-shot` ignores `loop_*`. `loop_exit` leaves the loop when the note is released. Auto slices use `slice_threshold` on the file's transient detection, deterministic (same file -> same slices); manual slice positions are `content.samples[].slices`. Slice selection as a modulation target (Serum "Play Single Slice (mod target)") is **not** a key here (**ASSUMPTION A14**: deferred; no decision names it). Snap (Off / Zero / Beats / Loop), zoom, marker animation and Trim are editor actions (Trim writes `start`/`end`). Send-to-clip is excluded (Q1). Granular / spectral / multisample (G19, G21, G22) are untouched (Q6).

**Warp slot 2** (Q9 "add a second free warp slot", proposed in 12; included because the delegation names it):

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `oscN.warp2.type` | `Osc N Warp 2 Type` | choice | the same list as `oscN.warp.type` (`kWarpTypesN`, 11 entries) | 0 `None` | - | - | V, X | no | `oscN_warp2` |
| `oscN.warp2.amount` | `Osc N Warp 2 Amount` | float | 0 .. 100 | 50 | % | lin | V, S | y | `oscN_warp2` |
| `oscN.warp2.phase` | `Osc N Warp 2 Phase` | float | 0 .. 360 | 180 | deg | lin, 0 dec | V, S | y | `oscN_warp2` |
| `oscN.warp2.spread` | `Osc N Warp 2 Spread` | float | -100 .. 100 | 0 | % | lin | V, S | y | `oscN_warp2` |

Group `Osc N / Warp 2`. Order of evaluation: `warp`, then `warp2`, then the morph family (**ASSUMPTION A15**; "Swap Warps" is a UI action exchanging the two value sets). The new warp modes of G34-G37 were appended to the shared list in block B21 (section 4.22), "Swap Warps" is a UI action there (A281).

### 4.10 B10 – Mixer routing, buses, DIRECT (Q16; G28, G39, G70) – positions 1360..1385 [26] – area 02 / 04 / 08

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `oscN.filter_blend` (N = 1..3), `noise.filter_blend` (4) | `Osc N Filter Blend` | float | 0 .. 100 (0 = Filter 1 only, 100 = Filter 2 only) | 0 | % | lin | V, S | y | `mix_routing` |
| `oscN.bus1_send`, `oscN.bus2_send` (6), `noise.bus1_send`, `noise.bus2_send` (2), `filter1.bus1_send`, `filter1.bus2_send`, `filter2.bus1_send`, `filter2.bus2_send` (4) = 12 | `Osc N Bus 1 Send` ... | float | 0 .. 100 | 0 | % (linear gain, 100 = unity) | lin | V, S | y | `mix_sends_osc` (osc, noise), `mix_sends_filter` (filters) |
| `filter1.destination`, `filter2.destination` (2) | `Filter N Destination` | choice | 0 `Effects only`, 1 `Direct out`, 2 `None` | 0 | - | - | V, X | no | `mix_routing` |
| `bus1.level`, `bus2.level` (2) | `Bus N Level` | float | -60 .. 6 | 0 | dB | 2.5 (as `master.volume`) | G, S | y | `mix_bus` |
| `bus1.destination`, `bus2.destination` (2) | `Bus N Destination` | choice | 0 `Main`, 1 `Direct`, 2 `Other bus` | 0 | - | - | G, X | no | `mix_bus` |
| `bus1.fx_bypass`, `bus2.fx_bypass` (2) | `Bus N FX Bypass` | bool | Off / On | Off | - | - | G, X | no | `mix_bus` |
| `direct.level` | `Direct Level` | float | -60 .. 6 | 0 | dB | 2.5 | G, S | y | `mix_routing` |
| `main.fx_bypass` | `Main FX Bypass` | bool | Off / On | Off | - | - | G, X | no | `mix_routing` |

Total: 4 + 12 + 2 + 2 + 2 + 2 + 1 + 1 = 26. Groups `Osc N`, `Noise`, `Filter N`, `Mixer / Bus N`, `Mixer`. Semantics (**ASSUMPTION A16**, 02 §1.9 / 08 §1.1 name the controls, not the graph): `filter_blend` is used only when `destination = Filter blend` (equal-power crossfade between Filter 1 and 2: 50 % = -3 dB each; `Filters 1+2` keeps both at unity as in v2). A send taps the source after its level / pan (filters: after `mix`, `pan`, `level`), independent of its `destination`, so a source can feed a filter and a bus at the same time. `filterN.destination` applies to the filter's output unless the filter feeds the other filter (`filter.routing` series). A bus sums its sends, runs through the effects whose `rack` is that bus unless `fx_bypass`, then `level`, then `destination` (`Other bus` = the other bus; if both buses choose `Other bus`, bus 2 is treated as `Main`, no feedback). `Main` = sum of sources with destination `Effects only`, filters to Main and buses to Main, run through the `Main` rack unless `main.fx_bypass`, then `master.volume` (= Serum MAIN level). `Direct` = sources with `Direct out`, buses to Direct, filters to Direct; it skips every rack and `master.volume` and is scaled by `direct.level`, then added at the output. The ENV 1 toggle on Main / Direct channels (G39) is specified in block B21 (section 4.22); the Sub oscillator (G23) in B18.

### 4.11 B11 – Filter additions (Q13, Q15; G55, G58) – positions 1386..1394 [9] – area 04

For `filter1`, `filter2`, `fxfilter` (the `SB_FILTER` macro, so the three stay identical in shape), group `Filter N`:

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `filterN.var` | `Filter N Var` | float | 0 .. 100 | 50 | % | lin | V (fxfilter: G), S | y | `filterN_extra` / `fx_filter_extra` |
| `filterN.pan` | `Filter N Pan` | float | -100 .. 100 | 0 | % | lin | V (fxfilter: G), S | y | same |
| `filterN.level` | `Filter N Level` | float | -60 .. 12 (-60 = silence) | 0 | dB | lin | V (fxfilter: G), S | y | same |

`pan` offsets the cutoff of the left and right channel in opposite directions, +-100 % = +-12 semitones each (**ASSUMPTION A17**; manual p.143 gives no scale). `level` is the channel / module output trim; the mixer strip uses it. `var` is the "4th knob" of the new models; on the eight existing models it has no effect (their extra controls are `drive`, `comb_offset`, `formant.*`). Clean Mode (G54) and key-follow-osc-pitch (G56, closed by Q14: keytrack follows the MIDI note) are not keys.

**Model / style meaning of the new values** (Q13: 37 types as real DSP; the index meaning is frozen, names final after A/B listening). `style` indexes inside a model; values beyond the model's own count clamp to the last:

| Model (index) | Style index → Serum type | Var knob (Serum) | Count |
|---------------|--------------------------|------------------|------:|
| `Multi SVF` (8) | 0 LH, 1 LB, 2 LP, 3 LN, 4 HB, 5 HP, 6 HN, 7 BP, 8 BN, 9 PP, 10 PN, 11 NN (first letter = primary SVF, second = secondary) | FREQ (cutoff of the second SVF) | 12 |
| `Slope` (9) | 0 MG Low 6, 1 MG Low 18, 2 Low 6, 3 Low 18, 4 High 6, 5 High 18 | FAT | 6 |
| `EQ 6 dB` (10) | 0 Low EQ 6, 1 Band EQ 6, 2 High EQ 6 | DB (pass-band gain) | 3 |
| `Ring Mod` (11) | 0 Ring Mod, 1 Ring Mod x2 | SPREAD (x2) | 2 |
| `Sample Hold` (12) | 0 SampHold, 1 SampHold- | none | 2 |
| `Smear` (13) | 0 Combs, 1 Allpasses, 2 Reverb | DAMP | 3 |
| `French LP` (14) | 0 | BOEUF (second resonance) | 1 |
| `Add Bass` (15) | 0 | THRU | 1 |
| `Scream` (16) | 0 Scream LP, 1 Scream BP | SCREAM | 2 |
| `New Wsp` (17) | 0 | MORPH (LPF / Notch / HPF) | 1 |
| `DJ Mixer` (18) | 0 (Xfer) | none | 1 |
| `Diffusor` (19) | 0 | STAGES | 1 |
| `EMS Ladder` (20) | 0 | SMOOTH | 1 |
| `PZ SVF` (21) | 0 (drawable) | SMOOTH; curve points in `content.filter_curves` | 1 |
| **Total** | | | **37** |

The 8 existing models and the nearest-mapping rows of 04 §1.3 are untouched. The 37 are the types of 04 §1.3 marked GAP as a type (the dual-SVF multis, the 6 / 18 dB slopes, the 6 dB EQs, Ring Mod x2, SampHold x2, Combs / Allpasses / Reverb, French LP, Add Bass, Scream x2, New Wsp, DJ Mixer, Diffusor, EMS Ladder, PZ SVF). The Var knobs of Serum types that map to existing models (LP FREQ, HP FREQ, HL WID, FRQ2, ...) are not implemented (**ASSUMPTION A18**: Q13 says "missing types", not missing Var functions of mapped types).

### 4.12 B12 – Effects: order, racks, Serum-only modules, reverb force (Q16, Q26, Q27; G70, G78-G82) – positions 1395..1483 [89] – area 08

**Chain and racks (Q16).** Representation (**ASSUMPTION A19**, 08 §7.3 is an open question and the design is UI only): one instance per effect type stays (G71 dynamic instancing is not decided). Each effect module is assigned to exactly one **rack** (`Main`, `Bus 1`, `Bus 2`, or a splitter band). The processing order inside a rack is the order of the effect indices in `fx.order.1..16` filtered to that rack. The 7 new effects are modules 9..15.

| Key | Name | Type | Range | Default | Sc | Mod | Kick |
|-----|------|------|-------|--------:|----|-----|------|
| `fx.order.10` .. `fx.order.16` (7) | `FX Order N` | choice (`kFxNames`, 16 entries) | 0 .. 15 | 9, 10, 11, 12, 13, 14, 15 (identity) | G, X | no | `fx_order_2` |
| `<fx>.rack` for the 16 modules `chorus`, `compressor`, `delay`, `distortion`, `eq`, `fxfilter`, `flanger`, `phaser`, `reverb`, `hyperdim`, `utility`, `bode`, `convolve`, `splitter_lh`, `splitter_lmh`, `splitter_ms` (16) | `Chorus Rack` ... | choice `kRacks` | 0 `Main`, 1 `Bus 1`, 2 `Bus 2`, 3 `Splitter L/H low`, 4 `Splitter L/H high`, 5 `Splitter L/M/H low`, 6 `Splitter L/M/H mid`, 7 `Splitter L/M/H high`, 8 `Splitter M/S mid`, 9 `Splitter M/S side` | 0 | G, X | no | `fx_rack_1` (modules 1-8), `fx_rack_2` (9-16) |
| `reverb.force_enable` | `Reverb Force Enable` | bool | Off / On | Off | G, X | no | `more:fx_reverb_1` |

Rules: the three splitters can only sit in `Main`, `Bus 1`, `Bus 2` (a nested splitter value is clamped to `Main`); an effect assigned to a splitter band whose splitter is off or in another rack is inert (the audio passes the splitter's band path unchanged). The permutation `fx.order.1..16` must contain each of 0..15 once; the repair rule of 08 (a later position takes the lowest effect not yet placed) extends to 16. `force_enable` On makes the reverb run even when the quality tier masks it (`Tier::low` removes `kFxReverb | kFxFlanger`, `quality.h`); the user trades polyphony for the reverb (Q27, 08 §7.4).

**Hyper / Dimension** (`hyperdim.*`, group `FX / Hyper Dimension`, Serum p.173-174) [9]:

| Key | Name | Type | Range | Default | Unit | Curve | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|-----|------|
| `hyperdim.on` | `Hyper Dimension On` | bool | Off/On | Off | - | - | no | `fx_hyperdim_1` |
| `hyperdim.rate` | `Hyper Rate` | float | 0.05 .. 10 | 0.5 | Hz | 0.3, 2 dec | y | `fx_hyperdim_1` |
| `hyperdim.unison` | `Hyper Unison` | int | 0 .. 7 (0 = Dimension only) | 4 | voices | lin | no | `fx_hyperdim_1` |
| `hyperdim.detune` | `Hyper Detune` | float | 0 .. 100 | 50 | % | lin | y | `fx_hyperdim_1` |
| `hyperdim.retrig` | `Hyper Retrigger` | bool | Off/On | Off | - | - | no | `fx_hyperdim_1` |
| `hyperdim.mix` | `Hyper Mix` | float | 0 .. 100 | 50 | % | lin | y | `fx_hyperdim_1` |
| `hyperdim.dim_size` | `Dimension Size` | float | 0 .. 100 | 50 | % | lin | y | `fx_hyperdim_2` |
| `hyperdim.dim_mix` | `Dimension Mix` | float | 0 .. 100 | 0 | % | lin | y | `fx_hyperdim_2` |
| `hyperdim.level` | `Hyper Dimension Level` | float | -24 .. 12 | 0 | dB | lin | y | `fx_hyperdim_2` |

`retrig` restarts the hyper voice offsets on every note-on (a global effect reading the note-on event). Hyper rate range and defaults: **ASSUMPTION A20** (the manual gives none).

**Utility** (`utility.*`, group `FX / Utility`, Serum p.182) [11]: `on` (bool, Off), `polarity_l` (bool, Off), `polarity_r` (bool, Off), `lpf` (float 20..20000 Hz, def 20000 = off, skew 0.3, mod), `hpf` (float 20..20000 Hz, def 20 = off, skew 0.3, mod), `mono_bass` (bool, Off), `mono_freq` (float 20..500 Hz, def 120, skew 0.5, mod), `width` (float 0..200 %, def 100, mod), `pan` (float -100..100 %, def 0, mod), `mix` (float 0..100 %, def 100, mod), `level` (float -24..12 dB, def 0, mod). Kick `fx_utility_1` (`on`, polarity_l, polarity_r, lpf, hpf, mono_bass, mono_freq, width), `fx_utility_2` (`pan`, `mix`, `level`). **ASSUMPTION A21**: Serum's LPF / HPF have no separate switch; the extreme position bypasses them.

**Bode frequency shifter** (`bode.*`, group `FX / Bode`, Serum p.161) [15]:

| Key | Name | Type | Range | Default | Unit | Curve | Mod |
|-----|------|------|-------|--------:|------|-------|-----|
| `bode.on` | `Bode On` | bool | Off/On | Off | - | - | no |
| `bode.mono_input` | `Bode Mono Input` | bool | Off/On | Off | - | - | no |
| `bode.shift` | `Bode Shift` | float | 0 .. 100 (share of `range`) | 0 | % | lin | y |
| `bode.range` | `Bode Range` | float | 1 .. 5000 | 100 | Hz | 0.3 | y |
| `bode.dir` | `Bode Direction` | float | -100 .. 100 (-100 down, 0 = channels in opposite directions, +100 up) | 100 | % | lin | y |
| `bode.width` | `Bode Width` | float | 0 .. 100 | 100 | % | lin | y |
| `bode.delay` | `Bode Delay` | float | 1 .. 2000 | 100 | ms | 0.3 | y |
| `bode.sync` | `Bode Sync` | choice (`kFxSync`) | 0..3 | 0 | - | - | no |
| `bode.division` | `Bode Division` | choice (window of `kDivisions`, entries 4..12 as `delay.division`) | 0..8 | 5 | - | - | no |
| `bode.feed` | `Bode Feedback` | float | 0 .. 100 | 0 | % | lin | y |
| `bode.balance` | `Bode Balance` | float | -100 .. 100 | 0 | % | lin | y |
| `bode.blur` | `Bode Blur` | float | 0 .. 100 | 0 | % | lin | y |
| `bode.retrig` | `Bode Retrigger` | bool | Off/On | Off | - | - | no |
| `bode.mix` | `Bode Mix` | float | 0 .. 100 | 50 | % | lin | y |
| `bode.level` | `Bode Level` | float | -24 .. 12 | 0 | dB | lin | y |

Kick `fx_bode_1` (on, mono_input, shift, range, dir, width, delay, sync), `fx_bode_2` (division, feed, balance, blur, retrig, mix, level). Ranges / defaults: **ASSUMPTION A22** (manual p.161 lists the controls without units). `delay` / `sync` / `division` follow the pattern of `delay.*` (time in ms when sync = Free).

**Convolve** (`convolve.*`, group `FX / Convolve`, Serum p.164-165) [14]:

| Key | Name | Type | Range | Default | Unit | Curve | Mod |
|-----|------|------|-------|--------:|------|-------|-----|
| `convolve.on` | `Convolve On` | bool | Off/On | Off | - | - | no |
| `convolve.ir` | `Convolve Impulse` | choice | 0 `None`, 1 `Preset IR` (impulse named in `content.impulses[]`; built-in IR names are appended later, R3) | 0 | - | - | no |
| `convolve.size` | `Convolve Size` | float | 0 .. 100 (50 = original length; stretch 0.25x .. 4x, exponential) | 50 | % | lin | y |
| `convolve.tone` | `Convolve Tone` | float | -100 .. 100 (negative low pass, positive high pass) | 0 | % | lin | y |
| `convolve.phi_min` | `Convolve Minimum Phase` | bool | Off/On | Off | - | - | no |
| `convolve.predelay` | `Convolve Pre-delay` | float | 0 .. 500 | 0 | ms | 0.4 | y |
| `convolve.sync` | `Convolve Sync` | choice (`kFxSync`) | 0..3 | 0 | - | - | no |
| `convolve.division` | `Convolve Division` | choice (window of `kDivisions`, 4..12) | 0..8 | 5 | - | - | no |
| `convolve.attack` | `Convolve Attack` | float | 0 .. 2000 | 0 | ms | 0.4 | y |
| `convolve.decay` | `Convolve Decay` | float | 0 .. 100 (100 = full length) | 100 | % | lin | y |
| `convolve.damp` | `Convolve Damping` | float | 0 .. 100 | 0 | % | lin | y |
| `convolve.ir_gain` | `Convolve IR Gain` | float | -24 .. 24 | 0 | dB | lin | y |
| `convolve.mix` | `Convolve Mix` | float | 0 .. 100 | 30 | % | lin | y |
| `convolve.level` | `Convolve Level` | float | -24 .. 12 | 0 | dB | lin | y |

Kick `fx_convolve_1` (on, ir, size, tone, phi_min, predelay, sync, division), `fx_convolve_2` (attack, decay, damp, ir_gain, mix, level). **ASSUMPTION A23**: the IR content source and the built-in IR set are not in the design files; impulses are ContentPack files referenced by `content.impulses[]`; the 0/1 choice is the minimal contract, built-ins can be appended without a state change.

**Splitters** (Serum p.177-181; the bands host effects via `<fx>.rack`):

| Key | Name | Type | Range | Default | Unit | Curve | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|-----|------|
| `splitter_lh.on` | `Splitter L/H On` | bool | Off/On | Off | - | - | no | `fx_splitter_lh` |
| `splitter_lh.freq` | `Splitter L/H Split Frequency` | float | 20 .. 20000 | 500 | Hz | 0.3 | y | same |
| `splitter_lh.low_bypass` | `Splitter L/H Low Bypass` | bool | Off/On | Off | - | - | no | same |
| `splitter_lh.high_bypass` | `Splitter L/H High Bypass` | bool | Off/On | Off | - | - | no | same |
| `splitter_lh.level` | `Splitter L/H Level` | float | -24 .. 12 | 0 | dB | lin | y | same |
| `splitter_lmh.on` | `Splitter L/M/H On` | bool | Off/On | Off | - | - | no | `fx_splitter_lmh` |
| `splitter_lmh.freq_low` | `Splitter L/M/H Low Split Frequency` | float | 20 .. 20000 | 200 | Hz | 0.3 | y | same |
| `splitter_lmh.freq_high` | `Splitter L/M/H High Split Frequency` | float | 20 .. 20000 | 3000 | Hz | 0.3 | y | same |
| `splitter_lmh.low_bypass` / `mid_bypass` / `high_bypass` (3) | `Splitter L/M/H Low Bypass` ... | bool | Off/On | Off | - | - | no | same |
| `splitter_lmh.level` | `Splitter L/M/H Level` | float | -24 .. 12 | 0 | dB | lin | y | same |
| `splitter_ms.on` | `Splitter M/S On` | bool | Off/On | Off | - | - | no | `fx_splitter_ms` |
| `splitter_ms.mid_bypass` / `side_bypass` (2) | `Splitter M/S Mid Bypass` ... | bool | Off/On | Off | - | - | no | same |
| `splitter_ms.level` | `Splitter M/S Level` | float | -24 .. 12 | 0 | dB | lin | y | same |

5 + 7 + 4 = 16 keys. Crossovers are linear-phase-compensated Linkwitz-Riley 24 dB (**ASSUMPTION A24**, not in the manual; summing all unbypassed bands with no effects must be transparent within 0.1 dB, to be tested). FX scope for all of B12: `G` (the FX run on the summed output; modulation of FX parameters uses the existing "sum output" rule of 08), smoothing `S` for continuous keys, `X` for switches. Group of the order / rack / force keys: `FX / Order`, `FX / Racks`, `FX / Reverb`. Not added here: per-module output LEVEL trim on the nine Vital effects (G72). Compressor limit / crossovers (G73), delay Tap / HQ / offset (G74), the distortion types and X-Shaper (G75), phaser DEPTH 2 (G76) and the reverb algorithms (G77) were added later as block B17 (4.17).

### 4.14 B16 – Wavetable position remap with RISE / FALL (G69) and per-module FX LEVEL (G72) – positions 1484..1503 [20] – area 02 / 08

Block B16 of the second extension round (WTREMAP); the three parts (4.14 remap and FX level, 4.15 Clean Mode) are appended in this order: 12 remap keys, 8 FX level keys, 3 Clean Mode keys = **23 keys, positions 1484..1506**, 8 modulatable (destination choices 404..411). Decision source: Q20 (proposed default "free-form per-slot remap curves in state"), G69, G72, G54.

**Remap (G69).** Serum's Warp "Remap 1-4" with a drawn graph (02 §1, row 02-14). A drawn curve maps the wavetable position of one oscillator. Per oscillator N = 1..3:

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `oscN.remap.on` | `Osc N Remap On` | bool | Off / On | Off | - | - | V, X | no | `oscN_remap` |
| `oscN.remap.slot` | `Osc N Remap Slot` | choice | 0 `Curve 1`, 1 `Curve 2`, 2 `Curve 3`, 3 `Curve 4` | 0 | - | - | V, X | no | `oscN_remap` |
| `oscN.remap.rise` | `Osc N Remap Rise` | float | 0 .. 2000 | 0 | ms | 0.3 | V, S | no | `oscN_remap` |
| `oscN.remap.fall` | `Osc N Remap Fall` | float | 0 .. 2000 | 0 | ms | 0.3 | V, S | no | `oscN_remap` |

Group `Osc N / Remap`. The curves are not keys: `content.remaps[]` (§5), `{osc, slot, name, points:[{x, y, power}]}`, at most 12 entries (3 oscillators x 4 slots), <= 64 points each, x ascending in 0..1, y in 0..1. A slot without an entry is the identity (the four slots are the "preset" choice of the Kick page; the graph itself is UI + content). Semantics (ASSUMPTIONS A120-A123):

* **A120** Domain: the position fed to the oscillator (the frame knob plus all frame modulation, per voice) is normalised to `u = frame / (frames - 1)` (63 for the built-in tables, the frame count - 1 of a `Preset table`), mapped through the baked curve (256 segments, linear between the baked values, input clamped to 0..1) and scaled back. `Remap On = Off` is bit-identical to the version 2 sound.
* **A121** Slot change and content change take effect at the next block; the oscillator's own 7 ms wave buffer fade prevents clicks. A curve for an oscillator with `type = Sample` is inert.
* **A122** RISE / FALL (the slew of Serum's matrix slot, here on the remapped position): a one pole glide, time constant = the key in ms (63 % of a step), RISE when the target position is above the current one, FALL when below, evaluated every wave buffer (7 ms) per voice; a note on jumps to its position (no glide from the previous note); 0 = no glide. The glide is on the remapped position, so it also smooths a drawn jump of the curve.
* **A123** The unison frame spread (`oscN.frame_spread`) is added after the remap, in frame units: the spread voices are not remapped individually.

The per-slot remap curve and RISE / FALL of the mod matrix slots (`modNN`) remain GAP (G69 matrix part): the task that defined this block names the wavetable position; see `14-morning-questions.md` row 18.

**What the manual says about it (MANUALCHECK, 4.20).** Serum 2 has two different things that this block mixes. (1) The WARP modes **Remap 1-4** (manual p.52, table "Alt Warp"): Remap 1 is a custom graph that remaps the *wave cycle* (x = phase in the cycle, y = remapped phase, diagonal = no change, the WARP knob is the strength from 0 = y=x to 100 %), Remap 2 applies the graph to each half of the cycle (mirrored), Remap 3 is a fixed sinusoidal remap, Remap 4 applies the graph four times. They are phase warps, not position remaps. (2) **RISE and FALL** are controls of the *modulation matrix* (manual p.211, CRV): "Draw the appropriate curve, and use the RISE and FALL smoothing controls to act as a slew limiter on the source." This block implements neither literally: the curve here remaps the wavetable *position* (our extension, kept because it is built and tested), and RISE / FALL are the slew limiter of that remapped position (A122 is a slew limiter in the sense of the manual: separate rise and fall times, one pole). The four choices of `oscN.remap.slot` are *curve slots* of this feature and are labelled `Curve 1` .. `Curve 4` so that they cannot be confused with the Serum warp modes. The Serum warp modes Remap 1-4 are not built: they need no key (the entries `Remap 1` .. `Remap 4` would be appended to the end of the `oscN.warp.type` / `oscN.warp2.type` lists and the graph would reuse `content.remaps[]`), only a phase-domain warp in the oscillator; the matrix RISE / FALL and curve need 2 keys x 64 slots. Both are proposals in MANUALCHECK-REPORT.md.

**FX LEVEL (G72).** `<fx>.level` for the eight Vital effects that have none: `chorus`, `compressor`, `delay`, `distortion`, `eq`, `flanger`, `phaser`, `reverb` (the FX filter has `fxfilter.level` from B11; Hyper / Dimension, Utility, Bode, Convolve and the splitters have their own):

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `<fx>.level` (8) | `<Module> Level` | float | -24 .. 12 | 0 | dB | lin | G, S | y | `fx_levels` |

Group `FX / <Module>`. **A124**: the trim is applied to the output of the module after its own dry / wet, ramped over the block, 0 dB leaves the signal untouched (bit-identical), the range matches the Serum modules (no "silence at the bottom" as the mixer faders); a module that is off has no level. Modulation destinations 404..411 in table order.

### 4.15 B16 – Clean Mode (G54) – positions 1504..1506 [3] – area 04 / 08

| Key | Name | Type | Range | Default | Unit | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|----|-----|------|
| `filter1.clean_mode`, `filter2.clean_mode`, `fxfilter.clean_mode` (3) | `Filter N Clean Mode` | bool | Off / On | Off | - | V (fxfilter: G), X | no | `filterN_extra` / `fx_filter_extra` |

**A125** (Serum 2 manual p.142 and p.171, checked by MANUALCHECK, 4.20): "Right-click the knob and choose Clean Mode ... to have the filter pre-gain stage the filter input -24 dB (with a +24 dB boost post-filter). This reduces saturation and input drive in the filter models." Off = version 2. On = the input of the filter models is scaled by -24 dB (`CleanInputGain`, ramped over the block) and the output of the models by +24 dB before the mix (the dry path of the mix is untouched); `filterN.drive` (0 .. 20 dB, still modulatable) keeps acting inside the models, now on a signal 24 dB lower, so the saturation of the models and of the drive is much weaker, and a signal in the linear range comes out at the same level as with the switch off. The models Comb and Formant have no drive stage and ignore the key. Earlier versions of this paragraph (drive = plain linear gain after the filter, the filter running without drive) were a guess from 04 §1.1 and were replaced. Proven by `test_wtremap.cpp` (hot signal with 12 dB drive: on / off differ by a relative RMS error of 0.85; quiet signal: on / off within 0.7 dB / 8 %; Comb: bit identical).

**A126** The capability bits are the next free ones of develop `629805b`: `kCapWtRemap` (26), `kCapFxLevel` (27), `kCapCleanMode` (28); the lead renumbers when the other blocks merge. **A127** the new blocks share the table end with other workers' blocks; the positions above are those of this branch (1484..1506), `kNumModDestinations` 412.

### 4.16 B14 – Pitch modes, CRS coarse, pitch tracking, transpose snap, unison laws & range, phase legato (G25-G27, G31, G32) – positions 1507..1543 [37] – area 02 / 07

12 keys per oscillator (N = 1..3) + 1 global voice key = 37 keys.

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|---------|------|-------|----|----:|------|
| `oscN.pitch_mode` (x3) | `Osc N Pitch Mode` | choice | Semitones / Harmonics / Ratio / Step | Semitones | - | - | V | no | `oscN_pitch_uni` |
| `oscN.pitch_coarse` (x3) | `Osc N Pitch Coarse` | float | -48 .. 48 | 0 | st | lin | V | y | same |
| `oscN.pitch_track` (x3) | `Osc N Pitch Tracking` | bool | Off/On | On | - | - | V | no | same |
| `oscN.pitch_bend_track` (x3) | `Osc N Pitch Bend Tracking` | bool | Off/On | On | - | - | V | no | `more:` |
| `oscN.fixed_pitch` (x3) | `Osc N Fixed Pitch` | float | 0 .. 127 | 0 | - | lin | V | y | `oscN_pitch_uni` |
| `oscN.transpose_snap` (x3) | `Osc N Transpose Snap` | int | 0 .. 8191 | 0 | - | lin | V | no | `more:` |
| `oscN.unison.mode` (x3) | `Osc N Unison Mode` | choice | Linear / Super / Exp / Inv / Random | Linear | - | - | V | no | `oscN_pitch_uni` |
| `oscN.unison.range` (x3) | `Osc N Unison Range` | float | 0 .. 48 | 2 | st | lin | V | y | same |
| `oscN.unison.blend_shape` (x3) | `Osc N Unison Blend Shape` | float | -100 .. 100 | 0 | % | lin | V | y | same |
| `oscN.unison.stack_repeat` (x3) | `Osc N Unison Stack Repeat` | choice | 1x / 2x / 3x | 1x | - | - | V | no | same |
| `oscN.phase_legato` (x3) | `Osc N Phase Legato` | bool | Off/On | Off | - | - | V | no | `more:` |
| `oscN.phase_memory` (x3) | `Osc N Phase Memory` | choice | All Voices / Contiguous / Per Voice | All Voices | - | - | V | no | `more:` |
| `voice.phase_legato` | `Voice Phase Legato` | bool | Off/On | Off | - | - | G | no | `master_expression` |

Order inside B14: `osc1` (12 keys), `osc2` (12 keys), `osc3` (12 keys), `voice.phase_legato` (1 key).

**MANUALCHECK (Serum 2 manual p.29-30, 35-36, 44-48; 4.20).** Pitch modes: the names Semitones / Harmonics / Ratio / Step match; in Serum the mode changes how the OCT and SEM controls work (Ratio also has a source SRC and a ratio), `Step` is shown only when an MTS-ESP tuning source is available, CRS stays continuous semitones. Here the mode value is carried by `pitch_coarse` (the CRS slot; no OCT / SEM re-labelling and no SRC) - a documented difference (A80/A81); proposal `oscN.ratio_src` in MANUALCHECK-REPORT.md. Pitch tracking (p.35): with tracking disabled the Multisample, Sample, Granular and Spectral oscillators play C3 (MIDI note 60), the Wavetable oscillator plays C-2 (MIDI note 0, "allowing it to be used as an LFO"): `oscN.fixed_pitch` now defaults to **0** (it was 48, Vital's value; the key can still be set to any note) and the sample oscillator follows `oscN.pitch_track` together with its own `sample.keytrack` (off = the recorded pitch). Pitch bend tracking (p.36): "determines whether MIDI pitch bend affects the frequency of an oscillator when pitch tracking is disabled ... only available for Sample, Granular, and Spectral oscillator types ... Wavetable and Multisample oscillator types always track pitch bend": a wavetable oscillator now always follows the wheel (the switch used to turn the wheel off for tracked wavetable oscillators too), the sample oscillator uses the switch when its pitch tracking is off; the UI dims the key unless pitch tracking is off and the type is Sample, and dims `fixed_pitch` unless tracking is off on a wavetable oscillator. Unison (p.46-48): MODE Linear / Super / Exp / Inv / Random (names match), WIDTH = `unison.spread`, RANGE = the detune range, BLEND default 75 % in Serum "an even blend between all voices" (the table's `unison.blend` default 80 % and its law are Vital's and stay). **STACK**: Serum offers Off, `12 (1-3x)`, `12+7 (1-3x)`, `Center-12`, `Center-24`; the Vital stack list (Unison, Center Drop 12, Center Drop 24, Octaves, Two Octaves, Power Chord, ...) contains them: Unison = Off, Octaves = 12, Power Chord = 12+7, Center Drop 12 / 24 = Center-12 / -24, and `oscN.unison.stack_repeat` (1x / 2x / 3x) is the "1-3x": **corrected** (it used to raise every stack multiplier to a power): for Octaves the voices cycle through the original pitch and the octaves up to the repeat count (1x: 1, 2; 2x: 1, 2, 4; 3x: 1, 2, 4, 8), for Power Chord through original, fifth and octaves (1x: 1, 1.5, 2; 2x: 1, 1.5, 2, 3, 4; 3x: ... 6, 8); the other stacks ignore it; 1x is the table of the stack itself (bit identical to before). Phase (p.43-44): Phase Legato is the right-click option of the PHASE control (here `oscN.phase_legato`, plus the global `voice.phase_legato` as an extension), the phase memory list All Voices (default) / Contiguous / Per Voice matches.

Block B14 follows block B16 in the table (merge order of the lead): the 12 modulatable keys are destination choices 412..423 (`oscN.pitch_coarse`, `oscN.fixed_pitch`, `oscN.unison.range`, `oscN.unison.blend_shape`, N = 1..3, in table order).

### 4.15 Choices made by WP0 where this file was silent

* **Order inside a block** (the destination numbers of §2 only depend on the counts, the order fixes the table positions): B03 is LFO-major (`lfo1.mono`, `lfo1.direction`, `lfo1.host`, `lfo2.mono` ...); B10 lists `osc1..3.filter_blend`, `noise.filter_blend`, then the sends source by source (`osc1.bus1_send`, `osc1.bus2_send`, `osc2.bus1_send` ... `noise.*`, `filter1.*`, `filter2.*`), `filter1.destination`, `filter2.destination`, `bus1.level`, `bus2.level`, `bus1.destination`, `bus2.destination`, `bus1.fx_bypass`, `bus2.fx_bypass`, `direct.level`, `main.fx_bypass`; B11 is filter-major (`filter1.var/pan/level`, `filter2.*`, `fxfilter.*`); B12 follows the row order of §4.12 (the 16 `<fx>.rack` keys in the order of the module list). `tests/test_new_keys.cpp` freezes the positions.
* **Display names not given above:** `Utility Polarity Left/Right`, `Utility Low Pass`, `Utility High Pass`, `Utility Mono Bass`, `Utility Mono Bass Frequency`, `Utility Width/Pan/Mix/Level`; the Bode, Convolve and Splitter names are the ones of the tables. Rack keys: `<Module> Rack` (`Hyper Dimension Rack`, `Splitter L/M/H Rack`, ...).
* **Groups not given above:** `Master / Curves` (B05), `Master / Scale`, `Master / Tuning`, `Master / MPE` (B07), `FX Filter` for the `fxfilter.*` additions of B11 (the existing group of that filter), `Osc N`, `Noise`, `Filter N` for the mixer keys of those sources.
* **Constants added to `params.h` / `modulation.h`:** `kNumKeysV2` = 880, `kNumLegacyFxSlots` = 9 (the effect slots the engines implement; `kNumFxSlots` = 16 is the table), `kNumModSourcesV2` = 28.
* **Engines:** an engine implements the version 2 keys only until `IEngine::capabilities ()` (engine_caps.h) says more; a stored value beyond what the engine implements acts like the last implemented one (filter model / style, `rlfoN.style`, oscillator destination), `Preset table` plays the saw and `Custom` the sine (§5 fallbacks).

### 4.17 B17 – Compressor, delay, distortion, phaser and reverb extras (G73-G77) – positions 1507..1520 [14] – area 08

Added by the fx-extras work package (FXEXTRA-REPORT.md), after block B16, as its own trailing block (R2: appended at the end of `kRaw[]`). The rows G73-G77 of `10-coverage-matrix.md` name the features only ("limit mode, adjustable crossovers", "Tap->Delay, HQ, scalar offset", "X-Shaper, Tube / Diode / Rectify, filter key track", "DEPTH 2", "Plate / Hall / Vintage / Nitrous + room modes"); the definitions below were first this file's own (assumptions A140-A159) and were then checked against the Serum 2 manual (p.162-177) by MANUALCHECK (4.20): where the manual speaks, the text below follows it, the rest is ours and says so. The per-module `LEVEL` trim (G72) is not part of B17.

**Compressor** (`compressor.*`, group `FX / Compressor`, G73) [4]:

| Key | Name | Type | Range | Default | Unit | Curve | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|-----|------|
| `compressor.limit` | `Compressor Limit` | bool | Off/On | Off | - | - | no | `fx_comp_limit` |
| `compressor.limit_latency_comp` | `Compressor Limiter Latency Comp` | bool | Off/On | On | - | - | no | `fx_comp_limit` |
| `compressor.crossover_low` | `Compressor Low Crossover` | float | 20 .. 1000 | 120 | Hz | 0.4, 0 dec | y | `fx_comp_limit` |
| `compressor.crossover_high` | `Compressor High Crossover` | float | 200 .. 16000 | 2500 | Hz | 0.4, 0 dec | y | `fx_comp_limit` |

`limit` replaces the downward stage of every enabled band by a look-ahead limiter whose ceiling is the band's `upper_threshold` (the ratio is ignored, the upward stage stays). The limiter is a peak limiter of 1.5 ms look-ahead, stereo linked, with an estimate of the peak between two samples (A141); `release` sets how fast the gain returns. `limit_latency_comp` On adds the look-ahead to the latency of the chain (`IEngine::latencySamples ()`, the other signal paths of the chain are delayed by it, A142); Off leaves the chain alone, the module still delays its own dry signal so that `mix` never combs. The crossovers are the two Linkwitz-Riley 24 dB crossovers of the multiband compressor; the defaults 120 Hz and 2500 Hz are the fixed values of the engine (the design file 08 says 150 Hz and 3 kHz, which is not what the engine does, A143). The high one is kept 1.25 times above the low one. Modulation moves them once per block (A144). Serum 2 (manual p.162-164): MODE Single / Multiband (= `compressor.bands`: `Single band` / `Multiband`, plus Vital's low / high band only), THRESH 0 .. -120 dB (ours -80 .. 0), RATIO at maximum shows LIMIT, a true peak limiter "a completely different DSP circuit", in which the attack range becomes 0-10 ms and the makeup gain range 0-36 dB (ours: `limit` is a separate switch, the limiter look-ahead is fixed at 1.5 ms so ATTACK has no effect in limit mode and the gain stays at 30 dB: documented differences, 14-morning-questions.md row 31), "Limiter Latency Comp" = `limit_latency_comp` (matches), X-LOW / X-HIGH crossovers (match), H / M / L band gains (= the `gain` of the three bands), BELOW = lower ratio.

**Delay** (`delay.*`, group `FX / Delay`, G74) [2]:

| Key | Name | Type | Range | Default | Unit | Curve | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|-----|------|
| `delay.hq` | `Delay High Quality` | bool | Off/On | Off | - | - | no | `fx_delay_3` |
| `delay.offset` | `Delay Offset` | float | 0.25 .. 4 | 1 | x | 0.5, 3 dec | y | `fx_delay_3` |

`hq` reads the delay with an 8-point Lagrange interpolator instead of the 4-point Catmull-Rom of the Vital delay (A145). `offset` multiplies the delay time of the right channel (A146); at 1 the two channels are as before. The UI snaps to 1.333 (triplet) and 1.5 (dotted). **Tap->Delay** (corrected by MANUALCHECK, manual p.166) is a *mode* of the delay, not a tap-tempo button: "Sets both delays to mono and in series, causing the left signal to fire once with no feedback (tap) followed by the right signal operating as a typical delay (feedback is applied here)". It is the entry 4 `Tap->Delay` appended to the end of `delay.style` (Mono, Stereo, Ping pong, Mid ping pong, Tap->Delay). The mono input goes into the left delay (time = `delay.time`, no feedback) and the output of that tap is the input of the right delay (time = `delay.time2` x `delay.offset`, feedback = `delay.feedback`); the output is the sum of the tap and the repeats of the right delay on both channels, so the first sound comes at the left time and the repeats at left + n x right (A147). The BPM / MS switch of Serum is one switch for both times: `delay.sync` now sets both the left and the right sync (before, the right time stayed tempo synced, so `delay.time2` never acted in Free mode). The TAP button of the delay panel is a tap tempo helper of this shell (not Serum's Tap->Delay mode) and stays as such. Serum has a scalar offset for each of the two channels ("1.1 = 110 %", Trip 1.333 and Dot 1.5 appear when dragging, which matches the snaps above); here only the right channel has one: the left offset is a proposal (`delay.offset_left`, MANUALCHECK-REPORT.md).

**Distortion** (`distortion.*`, group `FX / Distortion`, G75) [1 key, 8 appended choices, 2 content curves]:

| Key | Name | Type | Range | Default | Unit | Mod | Kick |
|-----|------|------|-------|--------:|------|-----|------|
| `distortion.filter_keytrack` | `Distortion Filter Key Track` | float | -100 .. 100 | 0 | % | y | `fx_distortion_2` |

`distortion.type` gets the entries 6 `Tube`, 7 `Diode 1`, 8 `Diode 2`, 9 `Zero-Square`, 10 `Asym`, 11 `Rectify`, 12 `X-Shaper`, 13 `X-Shaper (Asym)` (R3, section 3). Serum 2 has 13 types (manual p.167: Tube, Soft Clip, Hard Clip, Diode 1, Diode 2, Linear Fold, Sine Fold, Zero-Square, Downsample, Asym, Rectify, X-Shaper, X-Shaper (Asym)); ours are these 13 plus Vital's Bit crush (14), so the set is complete. Serum's default is Tube (ours stays Soft clip, the Vital default, to keep every preset). Definitions (v = input times the drive gain): Tube `tanh (v + 0.3) - tanh (0.3)`; Diode 1 `(1 - e^-v+) - 0.5 (1 - e^2v-)` (v+ and v- the positive and the negative part); Diode 2 `e^v- - e^-v+`; Zero square `tanh (32 v)`; Asym `tanh (v+) + max (v-, -0.5)`; Rectify `|clamp (v)|`. The types with an asymmetric curve (Tube, Diode 1, Asym, Rectify, X-Shaper (Asym)) pass a 10 Hz DC blocker (A148). `filter_keytrack` moves the cutoff of the filter by the note of the last key relative to C4 times the amount, like `fxfilter.keytrack` (A149). **X-Shaper** (corrected by MANUALCHECK, manual p.168-169): "a dual crossfading waveshaper". The two graphs A and B are drawn by the user and live in the `content` section as `content.curves.shaper_a` and `shaper_b` (same point list as the velocity curve: x 0..1, y 0..1, power per segment; empty = identity; section 5). **DRIVE is not a gain for these two types, it is the morph between the graphs**: "the DRIVE knob at 0 % presents waveshaper A while 100 % presents waveshaper B"; here `distortion.drive` -30 .. +30 dB is 0 .. 100 % and the input is not scaled. **X-Shaper** is symmetric: x = input magnitude, y = output magnitude (lower left = silence, top right = 0 dB), the sign of the input is kept. **X-Shaper (Asym)** is asymmetric: the graph spans the signed range, x = the input from -1 (left) to +1 (right), y = the output from -1 (bottom) to +1 (top), the middle of the graph is silence, so the two polarities are shaped differently (and even harmonics appear); it passes the DC blocker of the asymmetric types. Both curves exist for both types (A150). The curves are baked into 513-point tables on the message thread and copied to the effect between blocks.

**Phaser** (`phaser.*`, group `FX / Phaser`, G76) [1]:

| Key | Name | Type | Range | Default | Unit | Mod | Kick |
|-----|------|------|-------|--------:|------|-----|------|
| `phaser.depth2` | `Phaser Depth 2` | float | -48 .. 48 | 0 | semitones | y | `fx_phaser_3` |

`depth2` (corrected by MANUALCHECK, manual p.174: "DEPTH 2 - the offset between phaser stages") spreads the cutoffs of the 12 all-pass stages of the phaser evenly over `depth2` semitones around the swept cutoff (stage 0 lowest, the last stage highest; negative values reverse it), on both channels; at 0 every stage has the same cutoff and the effect is as before (A151). The other manual controls: RATE 0 .. 20 Hz or musical time (ours 0.04 .. 4 Hz, the Vital range), POLES = the number of stacked poles (ours: `phaser.blend` mixes the 4, 8 and 12 stage chains of the Vital phaser, 0 % = 4, 50 % = 8, 100 % = 12 stages; the key keeps its Vital name `Phaser Blend`, which the frozen first 880 keys fix), DEPTH, FREQ (= `phaser.center`), FEEDBACK, PHASE (0 % = same left and right, 50 % = 180 degrees; = `phaser.phase_offset`).

**Reverb** (`reverb.*`, group `FX / Reverb`, G77) [6]:

| Key | Name | Type | Range | Default | Unit | Mod | Kick |
|-----|------|------|-------|--------:|------|-----|------|
| `reverb.type` | `Reverb Type` | choice | 0 `Vital`, 1 `Plate`, 2 `Hall`, 3 `Vintage`, 4 `Nitrous`, 5 `Basin` | 0 | - | no | `fx_reverb_3` |
| `reverb.room` | `Reverb Room` | choice | 0 `Space`, 1 `Marble`, 2 `Rectangle`, 3 `Hexagon`, 4 `Box` | 2 | - | no | `fx_reverb_3` |
| `reverb.width` | `Reverb Width` | float | 0 .. 200 | 100 | % | y | `fx_reverb_3` |
| `reverb.er_size` | `Reverb ER Size` | float | 0 .. 100 | 50 | % | no | `fx_reverb_3` |
| `reverb.diff_a` | `Reverb Diffusion A` | float | 0 .. 100 | 70 | % | y | `fx_reverb_3` |
| `reverb.diff_b` | `Reverb Diffusion B` | float | 0 .. 100 | 60 | % | y | `fx_reverb_3` |

`type` 0 is the feedback delay network of the Vital reverb, bit for bit as before (R5). The other five are our own algorithms (`reverb_algorithms.h`, A152): Plate (after Dattorro's plate: four input all-passes, a figure-of-eight tank with a modulated all-pass, damping and 14 output taps), Hall (eight-line feedback delay network with a Hadamard matrix, an all-pass in every line, long lines), Vintage (eight feedback combs and four series all-passes per channel, darker output), Nitrous (the network with short dense bright lines and strong modulation), Basin (the network with line lengths from the modes of a room). `room` (corrected by MANUALCHECK, manual p.177: the MODE of **Nitrous**) chooses the room of Nitrous (dimensions in metres: Space 24x32x18, Marble 6.3x4.1x3, Rectangle 9x5.5x3.5, Hexagon 7.5x6.5x3.8, Box 5x5x5; the eight lines are the axial, tangential and oblique path lengths and their sum divided by the speed of sound) and is inert for the other types (A153). The earlier version put the rooms on Basin and gave Nitrous the dense bright network; the two were swapped: Basin is now the dense, bright, strongly modulated network. All types share `size` (scales the delay lines, 0.35x .. 2.8x), `decay` (T60, the loop gain of every line follows from its delay), the damping shelves, the pre cuts, the pre-delay, `chorus_amount` / `chorus_rate` (modulation of the lines) and `mix`. `er_size` scales the six early reflections per channel (A154); `diff_a` is the input diffusion, `diff_b` the diffusion inside the loops (A155).

**What the manual says per type (p.175-177)** and how the keys map onto it: PLATE has LO CUT, HI CUT, SIZE, PRE-DLY, DAMP (the speed of the high frequency attenuation) and WIDTH (`reverb.pre_low_cut`, `pre_high_cut`, `size`, `predelay`, the high shelf, `width`); HALL has LO CUT, HI CUT, SIZE, PRE-DLY, DECAY and SPIN RATE / SPIN DEPTH (modulation of the time differences = `reverb.chorus_rate` / `chorus_amount`); VINTAGE has LO CUT, HI CUT, SIZE, PRE-DLY, ER SIZE, DECAY, DAMP, DIFF A, DIFF B and CHORUS (speed and depth); NITROUS has LO CUT, HI CUT, SIZE, PRE-DLY, FEEDBACK, DIFFUSION, MODE (Space, Marble, Rectangle, Hexagon, Box) and CHORUS; BASIN has LO CUT, HI CUT, SIZE, PRE-DLY, FEEDBACK and CHORUS. The manual calls Plate and Hall "a modified version of the Tal Reverb algorithm (courtesy of Togu Audio Line)"; all five algorithms here stay our own designs (Dattorro plate, Hadamard FDN, Schroeder / Moorer combs, room-mode FDN, dense FDN) and sound different from Serum's, which cannot be derived from the manual. Differences that stay: FEEDBACK of Nitrous / Basin is the `decay` time here (there is no feedback key; proposal `reverb.feedback`), DIFFUSION of Nitrous is `diff_a`, WIDTH is available for every type (Serum: Plate only), the default type is the Vital reverb (Serum: Plate), and the UI dims the controls a type does not have: ER SIZE and DIFF B for Vintage only, DIFF A for Vintage and Nitrous, MODE for Nitrous only. `width` is mid / side width of the wet signal for every type including Vital (A156); `er_size`, `diff_a`, `diff_b` and `room` are inert for the Vital type and the UI dims them there (and for the types of Serum that do not have them, see above). Switching `type` or `room` clears the tail (stepped change, R7).

**Choices appended** (section 3): `distortion.type` 6 -> 14 entries (`kNumLegacyDistortionTypes` = 6). **Destinations**: the 8 modulatable keys take the destination choices 412-419 (after the B16 levels 404-411) in table order (`compressor.crossover_low` 412, `compressor.crossover_high` 413, `delay.offset` 414, `distortion.filter_keytrack` 415, `phaser.depth2` 416, `reverb.width` 417, `reverb.diff_a` 418, `reverb.diff_b` 419); `kNumModDestinations` = 420. **Caps** (`engine_caps.h`, bits 29-33 after the B16 bits 26-28, A157): `kCapFxCompressorLimit` (the four compressor keys), `kCapFxDelayExtras`, `kCapFxDistortionExtras` (the keytrack, `distortion.type` >= 6, the two curves), `kCapFxPhaserDepth2`, `kCapFxReverbAlgos` (all six reverb keys); each is set by the Vital adapter because `test_fxextra.cpp` renders the keys off and on. **Kick** pages 108-112: `fx_comp_limit`, `fx_delay_3`, `fx_distortion_2`, `fx_phaser_3`, `fx_reverb_3`. **UI** (A158): the FX panels of the five modules get the new controls (limit toggle and crossover handles on the band view, HQ toggle / offset knob / TAP button, the X-Shaper curve editor, depth 2 knob, type / room chips and the four reverb knobs).

### 4.18 B18 – Sub oscillator and Noise extras (G23, G38) – positions 1558..1575 [18] – area 02

Added by the osc-extras work package (OSCEXTRA-REPORT.md) as the last trailing block (R2). It was drafted as "B13" on a branch cut before B16/B14/B17 and renumbered at the merge: positions 1558..1575, destination choices 432..441, capability bits 35 (`kCapSubOsc`) and 36 (`kCapNoiseExtras`), Kick page 116 (`sub_main`), `noise` extras on the `more:` sheet of the `noise` page.

**Sub oscillator (`sub.*`, group `Sub`, Serum p.123-127) [11]:**

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `sub.on` | `Sub On` | bool | Off / On | Off | - | - | V, X | no | `sub_main` |
| `sub.waveform` | `Sub Waveform` | choice (`kSubWaveforms`, 6) | 0..5 (Sine, Rounded Rect, Triangle, Saw, Square, Pulse; the names of Serum 2 manual p.125-126) | 0 | - | - | V, X | no | `sub_main` |
| `sub.octave` | `Sub Octave` | whole | -4 .. 4 | -1 | - | lin | V, S | y | `sub_main` |
| `sub.tune` | `Sub Coarse` | cont | -48 .. 48 | 0 | semitones | lin | V, S | y | `sub_main` |
| `sub.level` | `Sub Level` | cont | 0 .. 100 | 50 | % | lin | V, S | y | `sub_main` |
| `sub.pan` | `Sub Pan` | cont | -100 .. 100 | 0 | % | lin | V, S | y | `sub_main` |
| `sub.destination` | `Sub Destination` | choice (`kOscDestinations`, 7) | 0..6 | 3 (`Effects only`) | - | - | V, X | no | `sub_main` |
| `sub.mono` | `Sub Mono` | bool | Off / On | Off | - | - | V, X | no | `sub_main` |
| `sub.filter_blend` | `Sub Filter Blend` | cont | 0 .. 100 | 0 | % | lin | V, S | y | `more:sub_main` |
| `sub.bus1_send` | `Sub Bus 1 Send` | cont | 0 .. 100 | 0 | % | lin | V, S | y | `more:sub_main` |
| `sub.bus2_send` | `Sub Bus 2 Send` | cont | 0 .. 100 | 0 | % | lin | V, S | y | `more:sub_main` |

Semantics (**ASSUMPTIONS A60..A63, A67, A68**; checked against the Serum 2 manual p.123-127 by MANUALCHECK, 4.20): `sub.waveform` selects one of 6 shapes (saw, square and pulse are PolyBLEP band-limited, sine / rounded rect / triangle are alias free by construction; pulse is 25 % duty, DC free; the manual only says that the pulse has unequal high and low states). `sub.octave` transposes by octaves (-1 = 12 semitones down; the manual has OCT and CRS only) and `sub.tune` is **CRS**, the continuous (no snap) semitone transpose, "most useful as a modulation destination" (it was ±100 cents, which is the FIN of the oscillators; the sub has no SEM or FIN). The manual's defaults: the SUB routes to MAIN (here `Effects only`, 3) like NOISE and OSC B / C; it describes no mono sub, so `sub.mono` is an extension and now defaults Off (the sub follows every voice). `sub.destination` matches the oscillator destinations (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None). When `sub.mono` is On, the sub plays monophonically on the lowest active voice note to prevent phase cancellation; when Off, it follows polyphonic voice allocation. `sub.filter_blend` crossfades between Filter 1 and Filter 2 when `sub.destination = Filter blend`. `sub.bus1_send` and `sub.bus2_send` tap post-level/pan into Bus 1 and Bus 2.

**Noise colours and sample extras (`noise.*`, group `Noise`, Serum p.128-132) [7]** (MANUALCHECK, 4.20: the noise oscillator is a stereo sample player with factory samples; the category of the colours White, Pink, Brown and Geiger and their descriptions match the first four names, their STEREO (0 = mono, 100 = no correlation) and FILTER (high / low pass) knobs are proposals `noise.stereo`, `noise.filter`; the other five names are ours, the manual lists no factory sample names; user samples loaded by WAV file, "Embed in Preset" and the display are not built; PITCH 50 % = the original pitch is `noise.pitch` 0 semitones, FINE, PAN, LEVEL, START (`noise.phase`, "sample start"), RAND (`noise.random_phase`), the loop / one-shot switch (loop is the default) and the keytrack switch match; with pitch tracking off the sample plays at its recorded pitch):

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `noise.sample` | `Noise Sample` | choice (`kNoiseSamples`, 9) | 0..8 (White Noise, Pink Noise, Brown Noise, Geiger, Tape Hiss, Vinyl Clicks, Digital Hash, Velvet Noise, Atmosphere) | 0 | - | - | V, X | no | `more:noise` |
| `noise.pitch` | `Noise Pitch` | cont | -48 .. 48 | 0 | semitones | lin | V, S | y | `more:noise` |
| `noise.fine` | `Noise Fine` | cont | -100 .. 100 | 0 | cents | lin | V, S | y | `more:noise` |
| `noise.phase` | `Noise Phase` | cont | 0 .. 100 | 0 | % | lin | V, S | y | `more:noise` |
| `noise.random_phase` | `Noise Random Phase` | bool | Off / On | Off | - | - | V, X | no | `more:noise` |
| `noise.keytrack` | `Noise Keytrack` | bool | Off / On | Off | - | - | V, X | no | `more:noise` |
| `noise.loop` | `Noise Loop` | bool | Off / On | On | - | - | V, X | no | `more:noise` |

Semantics (**ASSUMPTIONS A64..A66, A69**): `noise.sample` selects from built-in procedurally generated noise buffers (no external audio files needed). `noise.pitch` and `noise.fine` vary playback speed / pitch. `noise.keytrack` adjusts pitch relative to MIDI note C4 (note 60). `noise.phase` offsets sample start; when `noise.random_phase` is On, start phase is randomized per note-on. `noise.loop` controls looping vs one-shot playback (one-shot: silent after the sample end, no held value). All wire into the existing `noise.level`, `noise.pan`, `noise.destination`, `noise.filter_blend`, `noise.bus1_send`, `noise.bus2_send`.

### 4.19 B15 – Voice and Global additions (G12, G13, G14, G15, G16, G18) – positions 1576..1593 [18] – area 01 / 05 / 07

Added by the voice-global work package (VOICEGLB-REPORT.md) as the last trailing block (R2). The name B15 is the original number of the block; it sits behind B18 in the table (merge order of the lead). The definitions follow the Serum 2 user guide (VOICING section p.216-217, LFO RATE context menu p.192, Setting the Quality p.319-320); where the guide is silent (separate bend down, global stereo spread) they are this file's own (assumptions A100-A112).

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `master.voice.same_note` | `Master Same Note Mode` | choice | 0 `Steal`, 1 `Stack`, 2 `Ignore` | 0 | - | - | G, X | no | `voice_global` |
| `master.voice.steal_priority` | `Master Voice Steal Priority` | choice | 0 `Newest`, 1 `Oldest`, 2 `Highest`, 3 `Lowest`, 4 `Velocity` | 0 | - | - | G, X | no | `voice_global` |
| `master.poly_legato` | `Master Poly Legato` | bool | Off / On | Off | - | - | G, X | no | `voice_global` |
| `master.bend_down_range` | `Master Pitch Bend Down Range` | whole | 0 .. 24 | 2 | st | lin | G, X | no | `voice_global` |
| `master.bend_down_link` | `Master Pitch Bend Down Same As Up` | bool | Off / On | On | - | - | G, X | no | `voice_global` |
| `master.stereo_spread` | `Master Stereo Spread` | float | 0 .. 100 | 100 | % | lin | G, S | y | `voice_global` |
| `lfoN.swing` (N = 1..10) | `LFO N Swing` | float | 0 .. 100 | 0 | % | lin | V, S | y | `more:lfoN` |
| `quality.oversample_4x` | `Quality 4x Oversampling` | bool | Off / On | Off | - | - | G, X | no | `quality_options` |
| `quality.no_smoothing` | `Quality No Smoothing` | bool | Off / On | Off | - | - | G, X | no | `quality_options` |

18 keys total (11 modulatable: `master.stereo_spread` and `lfo1.swing` .. `lfo10.swing`, destination choices 442 .. 452 in table order). Groups: `Master / Voicing` for the two voice choices, `Master` for poly legato, bend and spread, `LFO N` for swing, `Quality` for the quality options. Order in the block: the six `master.*` keys, ten swings, the two quality keys. Every default leaves the version 2 sound alone (the 112 factory presets are unchanged except for the 18 explicit default values).

* **Same note and steal (G12, A100, A101).** Serum 2 has "Limit Same Note Poly to 1" (a key that already sounds gets no second layer) and a Voice Steal Priority submenu on the POLY field. `same_note`: `Steal` (default, version 2) ends the sounding voice of the key and starts a new one, `Stack` lets both sound (a note off releases every stacked voice of the key), `Ignore` is Serum's "limit to 1": the second note on is dropped. `steal_priority` uses Serum's five names and their sense (the kind of voice that is *kept* when the polyphony is used up): `Newest` (default: the earliest note is terminated), `Oldest` (the most recent one is terminated, which can be the note just played), `Highest` (the lowest note goes), `Lowest` (the highest note goes), `Velocity` (the softest note goes). A voice already in its release always goes first. Engine mapping (the first four are Vital's `voice_priority`): Newest = kRoundRobin, the engine default, so nothing changes; Oldest = kOldest; Highest = kHighest; Lowest = kLowest; Velocity = our `kVelocity` (voices sorted by note on velocity).
* **Poly legato (G13, A102).** Serum 2: with LEGATO on and MONO off it "behaves paraphonically": the envelopes do not retrigger on a note played while another is held (p.216). Here `master.poly_legato` On: a note on while another voice is held (its envelope in attack, hold or decay stage: key down or pedal) starts every envelope (env 1 .. 6) at the stage, position and value of that held voice's *same* envelope instead of at zero, so playing legato in poly mode does not re-attack; every voice keeps its own pitch and the older voices keep sounding. With nothing held the note starts from zero as always. Envelopes are kept apart (one bus per envelope and per engine; there is no global state). With several held voices the first one found supplies the state; in a steady sustain they are all equal. The state is the one at the end of the previous block (a block ends at the note on, the engine cuts blocks at events).
* **Bend (G14, A103).** `bend_down_link` On (default): the wheel has the range of `master.bend_range` in both directions (version 2). Off: a negative wheel position uses `bend_down_range` (0 .. 24 st), a positive one still `bend_range`. The link keeps old states, imported Vital patches and the presets with any `bend_range` sounding as before (a plain "down range 2" default would change them).
* **Stereo spread (G15, A104).** Mid / side width of the finished mix, after the effects and the output: `left = mid + spread x side`, `right = mid - spread x side`. 100 % is the version 2 sound (the loop is not entered), 0 % is exactly mono, the mid signal never changes. A modulation destination.
* **LFO swing (G16, A105).** Serum 2 applies the global SWING setting (an amount and a division, shown above the keyboard) to a tempo-synced LFO through "Swing" in the context menu of its RATE knob (p.192). Here every LFO has its own amount `swing`, on free and synced LFOs alike (a per LFO key is what a modulation matrix can move); the division is fixed: the cycle is a grid of four pairs of slots (eight slots: the eight steps of a step LFO, or eighths of any shape). Inside every pair the second slot is delayed: the boundary between the two halves of the pair moves from 1/2 to 1/2 + swing / 4 (swing 100 % = 3/4, 67 % is the classic triplet shuffle). The pair boundaries are fixed points, so the cycle length and the loop point do not change; 0 % is the unwarped phase (bit identical). Read once per block from the first voice (A111). A modulation destination. The Serum division choice is not built (Q28 in 14-morning-questions.md).
* **Quality options (G18, A106).** Serum 2 "Setting the Quality": Draft = 1x, High = 2x, Ultra = 4x oversampling, and "Disable Smoothing" (parameter changes, also from the mouse, land at once; Serum is sample accurate). `oversample_4x` runs the engine at 4x whatever the quality tier says (the tiers keep 1x / 2x; the option wins; above 44.1 kHz the engine reduces the factor as it does for the tiers). Changing the oversampling resizes the buffers of the effects and the voices, which must not happen on the audio thread, so the engine reads the key in `prepare ()` like the tier, reports `IEngine::needsPrepare ()` when it changed, and the shell prepares it again (JUCE: a 20 Hz message thread timer, processing suspended; CLAP: `request_restart`); sounding notes end, everything else is kept. `no_smoothing` bypasses the one pole smoothing of the parameter values (a 5 Hz filter in the engine) so that a changed value lands at once; the flag is per audio thread and is set before every block of an engine, so one instance never changes another.

**Caps** (`engine_caps.h`, bits 37-42 after the B18 bits, A107): `kCapVoiceSteal` (the two choices), `kCapPolyLegato`, `kCapBendDownRange` (range and link), `kCapStereoSpread`, `kCapLfoSwing` (the ten swings), `kCapQualityOptions`; each is set by the Vital adapter because `test_wp_voiceglb.cpp` renders the keys off and on and measures the difference (pitch of the bent note, which note survives, level of the new voice, side signal, level of the LFO at warped instants, aliasing of an FM patch, the instant a value lands). **Kick** pages 116 and 117: `voice_global` (same note, steal priority, poly legato, bend range, bend down range and link, stereo spread, polyphony) and `quality_options` (4x, no smoothing, reverb force, polyphony); the LFO swings are on the `more:` sheet of the `lfoN` pages. **UI** (A108): panel `GlbVoicePanel` (Voice / Stereo: four primary controls, bend down behind More), `GlbQualityPanel` gets the two options as primary toggles, `LfoPanel` the swing knob behind More; tests `glb_voiceglb_panels_and_modulation`.

### 4.20 MANUALCHECK - the Serum 2 manual against the earlier assumptions (no new keys)

The first workers had no access to the Serum 2 manual (354 pages, `docs/ui-design/_manuals/serum2/Serum2-Manual.txt`, gitignored, never committed). This block lists what was verified and what changed; the details are in the sections named in the second column and in `MANUALCHECK-REPORT.md`. No key was added, moved or renamed; changed are behaviours, defaults of unreleased keys, the end of one choice list, labels, UI and tests.

| Area | Result |
|------|--------|
| Sub waveform list, OCT, pan, level, Direct / Main / None routing | matches the manual (p.123-127, 145-146) |
| Sub CRS (`sub.tune`) | **fixed**: semitones, not cents (4.18, A61) |
| Sub default routing and mono | **fixed**: `Effects only` (MAIN) and mono Off (A62, A63) |
| Sub PHASE (start phase) and its "Contiguous" option | **missing** here: proposal keys `sub.phase`, `sub.phase_contiguous` |
| Noise: four colour names, loop default On, start, random, pitch, fine, pan, level | match (p.128-132) |
| Noise STEREO and FILTER of the colours; user samples; ENV 1 button of the Main / Direct channels | **missing**: proposals |
| Clean Mode | **fixed** (A125) |
| Compressor: single / multiband, crossovers, latency comp | match; limit attack 0-10 ms and gain 36 dB, threshold to -120 dB: documented differences |
| Delay HQ, Trip / Dot snaps, offset | match; Tap->Delay **fixed** (mode, A147); right sync **fixed**; left offset proposal |
| Distortion: type names, 13 types, key track, filter order | match (Zero-Square name fixed); X-Shaper DRIVE = A/B morph and the signed Asym graph **fixed** (A150) |
| Phaser DEPTH 2 | **fixed** (A151); POLES = the existing `phaser.blend` |
| Reverb: Plate / Hall / Vintage / Nitrous / Basin and their controls | names match; the room MODE belongs to Nitrous (**fixed**, A153); the controls per type are dimmed in the UI; the algorithms stay our own |
| Remap / RISE / FALL | the manual's Remap 1-4 are warp modes and RISE / FALL belong to the mod matrix (4.14): labels fixed, features proposed |
| Pitch modes names, Step only with an MTS-ESP source, ratio SRC | names match; mode carried by CRS (documented), SRC proposal |
| Pitch tracking off: C-2 for wavetables, the sample types at the recorded pitch | **fixed** (`fixed_pitch` 0, sample oscillator follows `pitch_track`) |
| Pitch bend tracking | **fixed** (wavetable always follows, sample types use the switch) |
| Unison laws, range, blend, stack `12 (1-3x)` / `12+7 (1-3x)` | laws match; stack repeat **fixed** |
| Phase legato options, phase memory list | match |

### 4.21 B20 – Mod amount destinations, per-connection curves, stereo source, stereo routing & voice override (G1-G6) – positions 1594..1595 [2] – area 05 / 06 / 01

Added by the mod-amount work package (MODAMT-REPORT.md) as the last trailing block (R2). It was drafted as the block at 1576..1577 on a branch cut before B15 and renumbered at the merge (assumptions A197..A205, the draft numbers A240..A248). Two keys only: everything else is a destination, a source or content.

**Master voicing extras (`master.*`, group `Master / Voicing`, G5) [2]:**

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `master.stereo_routing` | `Master Stereo Routing` | cont | 0 .. 100 | 100 | % | lin | G, S | yes (choice 453) | `master_expression` |
| `master.voice_override` | `Master Voice Override` | choice (`kVoiceOverrides`, 2) | 0 `Kill`, 1 `Steal` | 0 | - | - | G, X | no | `master_expression` |

Binding: `stereo_routing` and `voice_override` are Vital's own engine controls (the stereo decoder at the end of the chain, `sound_engine.cpp`; the voice handler). Capability bit **43** (`kCapModAmount`, A205) is set because `core/engine-vital/tests/test_modamt.cpp` renders every item off and on.

**New destinations** (the choice list `modNN.destination`, `kNumModDestinations` 453 -> **520**; stored in presets, so the numbers never move):

| Choices | Destination | Note |
|--------:|-------------|------|
| 453 | `master.stereo_routing` | the only new key of the table that is `kParamModulatable` |
| 454 .. 517 | `mod01.amount` .. `mod64.amount` (G1) | not flagged modulatable in the table (they sit in the version 2 region, which must not change); listed by `DestinationLists` |
| 518 | `master.portamento.time` (G3) | Vital `portamento_time` (per voice) |
| 519 | `master.polyphony` (G3) | Vital `polyphony` (the voice handler reads it every block) |

Rule for later blocks: the three appended groups sit right behind the last key of B20 (table index 1595, `kLastIndexOfB20` in `core/src/modulation.cpp`); a modulatable key of a later block gets the next choice number from **520** on, in table order. `test_new_keys.cpp` (`mod destination choices`) pins every number.

**New source**: choice **39** `stereo` (G4, bipolar): Vital's `stereo` source, a per-channel sign (left / right of the voice) that a connection turns into a different value on each channel. Needs `kCapModAmount`.

**New content** (section 5): `content.mod_curves[]` (G2).

Semantics (**ASSUMPTIONS A197..A205**): see the table in section 12. Short form: a connection whose destination is the amount of another connection evaluates per voice when its source is per voice (Vital routes a polyphonic source to the polyphonic `modulation_N_amount` input; a global source acts on all voices); the amount is clamped to -1 .. 1 after the modulation, as in Vital. A connection that would close a loop (a slot on its own amount, or A -> amount of B -> ... -> amount of A) is not wired by the engine (neutral: the base amount stays), the matrix row shows the loop badge and the destination menu greys the entries that would close one. A drawn per-connection curve maps the source value (0 .. 1) before the power curve and the amount, exactly as Vital's `line_mapping`; no entry = the straight line.

### 4.22 B21 – Serum 2 warp modes (G34-G37) and the ENV 1 buttons of the Main / Direct channels (G39) – positions 1596..1602 [7] – area 02 / 04

Added by the warp-env work package (WARPENV-REPORT.md) as the last trailing block (R2). The warp modes are **appended entries of the two shared warp lists** (no key), the only keys are the seven ENV 1 buttons. Assumptions A270 .. A284, the table is in section 12. Manual references are the Serum 2 manual p.49-56 (warp), p.146 and p.149 (ENV 1 button).

**Warp lists** (`oscN.warp.type`, `oscN.warp2.type`, N = 1..3; `kNumWarpTypes` = 52, `kNumLegacyWarpTypes` = 11). Entries 0 .. 10 are the Vital modes and never move; `legacyChoiceMax` (core/vital-import/vital_import.cpp) caps an imported Vital choice at 10. X is the first, Y the second other oscillator in the order of the Vital FM / RM entries (osc 1: X = Osc 2, Y = Osc 3; osc 2: X = Osc 1, Y = Osc 3; osc 3: X = Osc 1, Y = Osc 2). Each entry needs the capability bit of its row; `requiredCapsForValue` returns it for a value of the list.

| Entries | Names | Group in the picker | Cap bit | What it does (amount = the WARP knob 0 .. 100 %) |
|--------:|-------|---------------------|---------|--------------------------------------------------|
| 11 | `Sync Var` | Basic | 44 `kCapWarpAlt` | Sync (same ratio law as `Sync`) with a raised cosine fade of the slave cycle around its reset; the **phase knob of the slot is the VAR fader** (0 = hard sync, 100 % = fades half a cycle on each side) |
| 12, 13, 14 | `Bend +`, `Bend -`, `Bend +/-` | Alt | 44 | each half of the cycle is bent on its own: `+` pinches towards the middle of the cycle (exponent 1 .. 4), `-` pulls outwards (exponent 1 .. 1/4); `+/-` neutral at 50 % |
| 15, 16, 17 | `Asym +`, `Asym -`, `Asym +/-` | Alt | 44 | the same bend on the whole cycle, to the right / to the left; `+/-` neutral at 50 % |
| 18 | `Flip` | Alt | 44 | polarity flip of the waveform from the position `1 - amount` of the cycle on (0 % = never) |
| 19 | `Mirror` | Alt | 44 | the second half of the cycle plays the first half backwards; the amount bends both halves like `Asym +/-` (always audible) |
| 20, 21, 22, 23 | `Remap 1` .. `Remap 4` | Alt | 44 | 1: the drawn curve of the oscillator (below) remaps the cycle, the amount mixes the curve with the diagonal; 2: the curve on each half; 3: a sinusoidal remap; 4: the curve four times per cycle |
| 24 | `Odd/Even` | Alt | 44 | splits the wave into its even and odd harmonics (the wave and itself half a cycle later): 50 % original, 0 % odd only, 100 % even only |
| 25, 26 | `Filter LPF`, `Filter HPF` | Filter | 45 `kCapWarpFilter` | two one pole stages, the cutoff follows the pitch (multiples of the fundamental): LPF 1 .. 128 (open at 100 %), HPF 0.5 .. 64 (open at 0 %) |
| 27 .. 40 | `Dist Tube`, `Dist Soft Clip`, `Dist Hard Clip`, `Dist Diode 1`, `Dist Diode 2`, `Dist Linear Fold`, `Dist Sine Fold`, `Dist Zero-Square`, `Dist Asym`, `Dist Rectify`, `Dist Sine Shaper`, `Dist Stomp Box`, `Dist Tape Sat.`, `Dist Soft Sat.` | Distortion | 46 `kCapWarpDist` | waveshaper on the sample after the wavetable read, drive 0 .. 30 dB over the amount; the first ten use the curves of `distortion.type` |
| 41, 42 | `FM Thru-Zero from X / Y` | FM | 47 `kCapWarpFm` | frequency modulation by the other oscillator: the modulator is integrated into the phase, the instantaneous frequency may go negative (the phase runs backwards, manual p.54); index 8 x amount^2 |
| 43, 44 | `FM Exp from X / Y` | FM | 47 | the frequency is multiplied by 2^(4 x amount^2 x modulator) (exponential law) |
| 45, 46 | `FM Linear from X / Y` | FM | 47 | thru-zero FM with the frequency clamped at zero ("can't do thru-zero", manual p.55) |
| 47, 48 | `PD from X / Y` | PD | 48 `kCapWarpPdAm` | phase distortion: the other oscillator offsets the read phase by up to 0.9 cycle x amount |
| 49 | `PD Self` | PD | 48 | the wavetable read at the unmodified phase is the modulator of a second read |
| 50, 51 | `AM from X / Y` | AM | 48 | amplitude modulation: gain 1 - a + a x (0.5 + 0.5 x modulator) (RM, entries 9 and 10, uses the bipolar modulator) |

The `Remap 1`, `Remap 2` and `Remap 4` modes read the 257-value table of the oscillator's own remap curve (`content.remaps[]`, the slot `oscN.remap.slot` of block B16, the same `FrameRemap` table of its wavetable, whether `oscN.remap.on` is on or not; an empty slot is the diagonal = no change). The "Remap" of block B16 moves the wavetable position, the warp mode moves the phase of the wave cycle: two features that share the drawn curve (A273).

Evaluation order (A15 extended): slot 1, then slot 2, then the morph family. The **phase domain** modes (Sync Var, Bend, Asym, Flip, Mirror, Remap, FM, PD, AM) move the read phase before the wavetable read; the **value domain** modes (Odd/Even, Filter, Distortion) act on the sample after the read. When one slot is a value mode and the other a phase mode, the phase mode therefore always runs first (A275). The phase knob of the slot is a read phase offset for the modes that move the phase and for Sync Var the VAR fader; it is not used by the value and modulator modes. The Vital modes Shepard-tone morph and warp ignore the new modes (the Shepard code path has no warp 2 either).

**Warp on a sample voice** (A38 extended, A276): AM, the distortion group and the FM laws (Thru-Zero as the Vital FM, Exp, Linear) act on a sample (FM modulates the read speed); the filters, Odd/Even, PD and all phase modes are inert on a sample.

**Keys** (group of the module; type bool, Off / On; default **On** = today's sound, because ENV 1 is applied to every channel today; Sc: V, X; Mod: no; Kick: `mix_env1`):

| Key | Name | Cap bit |
|-----|------|---------|
| `sub.env1` | `Sub Env 1` | 49 `kCapEnv1Toggle` |
| `osc1.env1`, `osc2.env1`, `osc3.env1` | `Osc N Env 1` | 49 |
| `noise.env1` | `Noise Env 1` | 49 |
| `filter1.env1`, `filter2.env1` | `Filter N Env 1` | 49 |

Semantics (manual p.146, p.149; A277 .. A279): the button belongs to a channel whose destination is **Main** (Effects only) or **Direct**; for other destinations (Filter, Filter blend, bus only, None; for a filter: its output into the other filter, None) it has no effect and the UI greys it out. With the button **off** the channel is not multiplied by ENV 1; it is still multiplied by the velocity tracking and the voice amplitude (A277), so a held note plays at full level like a free-running oscillator. The voice then ends when the longest **enabled** envelope (ENV 1 and every ENV 2 .. 4 that is used as a source in the mod matrix) has finished its release (A278); with every button on, the voice ends with ENV 1 exactly as before (bit identical). The bus sends are not affected (a bus is a send, not a Main / Direct channel). The filters' buttons (`filterN.env1`) are not in the MANUALCHECK proposal but in the manual (p.149) and are built here (A279).

UI: the seven buttons are toggles behind More of the Mixer panel (`MixerPanel.cpp`), each greyed unless the destination of its channel is Main or Direct; the warp picker is the grouped list of `warpTypeGroups ()` (eight groups, no gaps any more) with **Swap Warps** at the end of the list of both slots (exchanges the four values type / amount / phase / spread of slot 1 and slot 2, A281); while `Sync Var` is the type of a slot, the phase knob of the slot is labelled **Var** (A282). Kick page `mix_env1` (119 pages).

### 4.23 B22 – Several instances of one effect: the pool of 16 slots (G73) – positions 1603..2050 [448] – area 08

Added by the fx-instances work package (FXINSTANCES-REPORT.md, design 18-FX-INSTANCES.md) as the last trailing block (R2). Per slot `fxi01 .. fxi16` 28 keys: `fxiNN.type` (None, Chorus, Compressor, Delay, Distortion, EQ, FX Filter, Flanger, Phaser, Reverb, Hyper/Dimension, Utility, Bode, Convolve; default None), `fxiNN.on` (Off), `fxiNN.rack` (Main, Bus 1, Bus 2), `fxiNN.order` (0 .. 1023, default 1023) and `fxiNN.p01 .. p24` (float 0..1, default 0, generic name "FX Slot NN Pkk", modulatable). What parameter k means is the k-th key of the type's own list (table order, without `.on` and `.rack`, without `reverb.force_enable`): `core/src/fx_types.cpp` is the descriptor, range / curve / unit / names are those of the source key. Defaults leave every slot None / off: the 112 presets sound as before. Capability bit 50 `kCapFxInstances`; a type value also needs the bit of its module. Destination choices 520 .. 903 (`kNumModDestinations` 904). `kMaxParams` 4096. Assumptions A290 .. A298 (section 12). Kick: pages `fxi01 .. fxi16` (type, on, rack, order, p01 .. p04; More: p05 .. p24), 135 pages.

### 4.24 WP-03 FILTERS80 – the remaining 43 Serum filter types and the PZ SVF curve editor (no new keys; choice entries 22 .. 26 of `filterN.model`) – area 04

Added by the filters80 work package (FILTERS80-REPORT.md). The 43 types that had DSP in `core/filters-serum` but no slot become the models 22 .. 26 of the one list `kFilterModels` (shared by `filter1.model`, `filter2.model`, `fxfilter.model`; the list is 27 entries long, `legacyChoiceMax` unchanged, no key added, `kMaxParams` 4096 untouched):

| model | name | styles (index = `filterN.style`) | Var |
|------:|------|-----------------------------------|-----|
| 22 | `Normal` | 0 MG Low 12, 1 MG Low 24, 2 Low 12, 3 Low 24, 4 High 12, 5 High 24, 6 Band 12, 7 Band 24, 8 Peak 12, 9 Peak 24, 10 Notch 12, 11 Notch 24 | FAT |
| 23 | `Multi Morph` | 0 LBH, 1 LPH, 2 LNH, 3 BPN | MORPH |
| 24 | `Flanges` | 0 Cmb L, 1 Flg L, 2 Phs L, 3 Cmb H, 4 Flg H, 5 Phs H, 6 Cmb HL, 7 Flg HL, 8 Phs HL | LP FREQ (0 .. 2), HP FREQ (3 .. 5), HL WID (6 .. 8) |
| 25 | `Misc` | 0 Low EQ 12, 1 Band EQ 12, 2 High EQ 12, 3 German LP, 4 Formant-I, 5 Formant-II, 6 Formant-III, 7 Bandreject, 8 Dist.Comb 1 LP, 9 Dist.Comb 1 BP, 10 Dist.Comb 2 LP, 11 Dist.Comb 2 BP | DB (0 .. 2), none (3), FORMNT (4 .. 6), WIDTH (7), COMBFRQ (8 .. 11) |
| 26 | `New` | 0 MG Ladder, 1 Acid Ladder, 2 MG Dirty, 3 Comb 2, 4 Exp MM, 5 Exp BPF | SMOOTH (0, 1), PAIN (2), FRQ2 (3), MIX (4), none (5) |

The mapping (model, style) -> `FilterType` is one table, `serumFilterTypeOf ()` in `core/filters-serum/src/filter_types.cpp`; the engine (`SerumFilterBank::filterTypeOf`) and the UI response view use it, and a test (`serum_filters_model_style_table_reaches_every_type_exactly_once`) proves that models 8 .. 26 reach each of the 80 types exactly once. A style beyond the last one of its model clamps to the last (as for 8 .. 21). Capability bit 51 `kCapFilterModels2`: a model value >= 22 needs it (`requiredCapsForValue`), `vital` reports it; the audible off / on proof is `all_43_remaining_types_are_stable_and_differ_from_each_other_and_from_analog` (core/engine-vital/tests/test_wp6.cpp), the own engine reports no bit. Defaults are untouched (model 0), the 112 factory presets sound as before (`scripts/gen-factory-presets.py --check`).

UI: the type menu of the Filter panels and the type picker sheet group the types in the categories of the Serum 2 manual (p.135-139: Normal, Multi, Flanges, Misc, New; the eight classic Vital models under `Classic` in the menu). A pick sets model and style (and the Blend knob only for the types that use Blend, A303). The `Var` knob shows the label of the type (table above, A307). The response view of the models 22 .. 26 is measured from the DSP itself (A304). The PZ SVF curve editor works on the response view of a filter set to `PZ SVF`: it edits `content.filter_curves[]` (A305); a curve of the content applies to the PZ SVF type only.

Assumptions A299 .. A308 (section 12). Morning questions 44 .. 47.


### 4.25 WP-01 – the content pipeline in the shells (no new keys, no new capability bit; `kMaxParams` untouched) – area 01 / 02 / 03 / 05 / 08 / 10 / 11

Added by the WP-01 work package (WP01-REPORT.md). Content (wavetables, samples, impulse responses, small JSON items) now really enters the plugin from files: the state keeps references (pack, file, SHA-256) exactly as before (state version 3, additive `content` section, nothing changes in the document), the shells supply the store that turns a reference into bytes.

**The user content library** (`core/include/synthyboy/user_content.h`, JUCE free): `UserContentStore` on `<userDir>/content/` (`userDir` = `userDataDirectory()`, `SYNTHYBOY_USER_DIR` in tests). Layout `<root>/<kind dir>/<sha12>/<name>.wav` with the packs `user.wavetables` (dir `wavetables`), `user.samples` (`samples`), `user.impulses` (`impulses`), `user.embedded` (`embedded`: "Embed in Preset", not listed); every other pack is a directory below the root (`user.imported-vital` of the Vital importer) or below `<userDir>/packs` (installed packs, read only). Content addressed (the same bytes are stored once, the hash directory keeps equal names apart), the original file stem is kept (`Piano Eb3.wav`: the root note parser reads it back). `readFile` verifies the SHA-256 of the reference; paths with `..`, `:` or a leading `/` are refused. Both shells call `IEngine::setContentStore` once, before any content reaches the engine, and reset it to null before the store dies; every load of a document (`setStateInformation`, `loadPatch`, `initPatch`, `revertToSaved`, the user default) hands the new `content` to the engine with `setContent`, which decodes against the store (a reference the store cannot resolve falls back as in section 5: saw / silence / bypass).

**Files by kind.** Wavetable: a 16-bit mono WAV, a multiple of 2048 samples is that many frames (the format of `UserWavetableStore`), anything else one resampled cycle. Sample: the original WAV bytes when the file is a WAV the engine reads, otherwise the file decoded by `juce::AudioFormatManager` (WAV, AIFF, FLAC, Ogg Vorbis, MP3 where the platform has it, CAF) and stored as a 24-bit WAV; the root note is taken from the file name (`Name F2`, `Eb3`, `C#4`; C3 = 60, `parseRootNoteFromFilename`) and otherwise from the WAV `inst` / `smpl` chunk, and written to `oscN.sample.root`. Impulse response: a WAV, converted like a sample.

**Shell flows** (`shell-juce/ui/ContentIO.h`, one function each, used by the panels and the tests): `loadSampleFile` (decode, store, `content.samples[osc]`, root note, `oscN.type` = Sample, START / END reset), `trimSample` (the START .. END part becomes a new 24-bit WAV, slices inside it are rescaled, the loop markers keep their place inside the region, START / END go back to 0 / 100), `saveWavetable(embed)` (store, `content.wavetables[osc]`, `oscN.wavetable` = Preset table), `loadImpulseFile` / `removeImpulse` (`content.impulses[0]`, `convolve.ir` = Preset IR / None), `importVitalFile`, `MissingFilesModel`. File access goes through `UiHost::requestFile` (JUCE shell: `juce::FileChooser`, last folder per purpose; tests install `setFileRequestHandler`), the library through `UiHost::contentLibrary` and `userLibrary`.

**Wavetable editor** (`WavetableEditorSheet`): `setTargetOsc` / `open` (the sheet opens on the stored file when `oscN.wavetable` is the Preset table, else on the built-in frames; unsaved edits survive closing), Save Table, Embed in Preset, Export WAV 16 / 32 bit / single cycle through a save dialog, the Import sheet has a Choose button and three real importers: audio (decoded, the existing worker thread), image (JUCE image formats, luminance, at most 256 x 1024), wavetable (`.wav` cycles, a single cycle, `.vitaltable`, other audio cut into 2048 sample cycles). The Missing Files dialog (`MissingFilesSheet`) opens after a load that has references the library cannot resolve (editor timer on `contentRevision`) and from the menu; rows offer Locate (file chooser; a file whose SHA-256 differs from the one the preset was saved with is refused) and Remove (the reference goes, the sound falls back).

**Sample view** (`OscWaveView`): Load, `...` menu (Reload, Show location, Trim, Remove, Show marker animation), Snap (Off / Zero / Beats / Loop), the real waveform (min / max columns of the decoded file over the visible window, `SamplePreview::peaks`), the header shows name and length, Auto slice markers are the onsets the engine uses (`SamplePreview::autoSlices` = first slice 0 + the onsets with strength >= 1 - threshold, which is the `detectOnsets` rule of `vital_engine.cpp:1543`; no `IEngine` accessor needed), the Preset table of an oscillator is drawn from the file (2D, 3D stack, spectrum) and the wavetable Edit button opens the editor for that oscillator.

**Library items** (`user_library.h`, `library_items.h`): `<userDir>/library/<category>/<name>.json` + `favourites.txt`; categories `lfo_shapes` (`.lfoshape.json` format of tools/vital-import, loadable from file and from `.vitallfo`), `formulas` (`{"format":"synthyboy-formula","version":1,"name","expression"}`; the Presets menu also lists the built-in examples), `fx_racks` (`{"format":"synthyboy-fx-rack","version":1,"name","values":{...}}`, the keys of the FX chain: module prefixes, `splitter_*`, `fx.order.N`, `fxiNN.*`), `voice_control` (reserved for WP-07, no UI yet). `LibraryMenu` is the one menu class: Save current as..., Load from file..., per item Load / favourite / Delete.

**CLAP** (`shell-clap/synthyboy_content_ext.h`, Q39 default): the extension `synthyboy.content` of the native shell: `count`, `get`, `import_file` (by path), `attach`, `remove`, `import_vital`; the JUCE shell uses file choosers; the CLAP build of the JUCE plugin (clap-juce-extensions wrapper) does not expose the extension (the wrapper has no hook for plugin-specific extensions; the editor has file choosers).

### 4.26 WP-21 – the Serum 2 preset import (no new keys, no new capability bit; `kMaxParams` untouched) – area 01 / 02 / 03 / 05 / 06 / 08 / 10

Added by the WP-21 work package (WP21-REPORT.md). **No keys** (decision A343): every Serum 2 value that has a home in the model is written to an existing key; a value without a home is dropped and reported (and listed as a backlog row in 17-BACKLOG.md), because a key without an engine behind it would only move the loss into the state. The container is documented in 19-serum-preset-format.md.

**The package.** `core/serum-import` (in `synthyboy_core`, no I/O, no JUCE, no engine; also part of the iOS build): `zstd_decode.cpp` (RFC 8878), `serum_container.cpp` (header, inflate, UBJSON), `cbor_reader.cpp`, `audio_decode.cpp` (FLAC, WAV), `serum_map.cpp` (the tables), `serum_import.cpp` / `serum_voice.cpp` / `serum_content.cpp` / `serum_fx.cpp` (the importer), `serum_api.cpp` (entry points, report, preset text), `serum_plugin_import.cpp` (the content store glue, `SerumFolderResolver`). Public headers `serum_import.h` and `serum_plugin_import.h`. `synthyboy_serum_import` (tools/serum-import) is the command line front end, `scripts/audit-serum-import.py` the independent audit. The shells: Editor menu "Import Serum 2 file..." (`Editor.cpp importSerumFromMenu`, `ContentIO.cpp importSerumFile`, report in the `ImportReportSheet`).

**Discipline (as the Vital importer).** One pass over the value tree of the file; every leaf is accounted exactly once as imported / approximated / dropped / ignored (`SerumImportReport::unaccounted` is 0 for every file; the "default" blocks are ignored leaves, the Serum defaults of absent parameters are applied and counted in `numDefaulted`). The files a preset only references (tables, samples, multisamples, noise, impulses) are asked from a `SerumContentResolver`; missing files are reported and replaced as documented (saw table, wavetable oscillator, no impulse). Never throws; bounded (file 512 MiB, body 256 MiB, working memory 768 MiB, depth 24, time 120 s, report entries 40000).

**File kinds.** `.SerumPreset` (a patch), `.SerumFX` / `.SerumFXRack` (an effect chain written onto the current patch: the parameters it touched), `.XferShape` (an LFO shape or a curve, saved as `.lfoshape.json` / put into an LFO slot), a `.flac` / `.wav` of an Impulses folder (stored in the library). `.XferPath`, `.XferClip(Bank)`, `.XferArp(Bank)`, `.SerumStyle`, `.XferFilterPZ*`, `.SerumMIDIMap` are read, accounted and reported as having no equivalent (backlog rows).

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A325 | The oscillator level `kParamVolume` 0 .. 1 is a linear gain: `oscN.level` = 100 x value (same for noise and sub). The master volume `kParamMasterVolume` is a linear gain: dB = 20 log10 (value). Many presets store 0.0 for an oscillator that a modulation (Velo, Env) opens, which the matrix import carries | 54 |
| A326 | Unison: `kParamDetune` 0 .. 1 -> `unison.detune` 100 x; BLEND = `kParamDetuneWid` (default 75) -> `unison.blend`; `kParamUnisonStereo` -> `unison.spread` (a negative value clamps to 0); stack, detune mode, range one to one (stack Octave 3 -> Two Octaves, Octave+Fifth 3 -> Two Power Chords: approximated) | |
| A327 | `kParamTablePos` 1 .. 256 spans the whole table whatever its frame count: `frame` = (pos - 1) / 255 x 100 | |
| A328 | `kParamWarpVar` 0 .. 1 -> the phase fader of the warp slot (360 x). Warp names -> our list (13-new-keys 4.22): kFM_* -> FM Thru-Zero, kFMX_* -> FM Exp, kFMP_* -> FM Linear (the enum has three FM families, the manual four menu entries), kDLM -> Mirror, kPWM -> Pulse, `_OSC` / `_OSC2` = the first / second other oscillator (the X / Y of 4.22); sources Noise, Sub, Filter 1, 2 have no home (dropped, backlog row 8) | 62 |
| A329 | The filter frequency `kParamFreq` 0 .. 1 spans 135 semitones: Hz = 440 x 2^((135 x v - 69) / 12) (v = 1 is 19.9 kHz), clamped to 20 Hz .. 20 kHz; also the distortion filter of the FX module | 55 |
| A330 | Filter drive 0 .. 100 % -> 0 .. 20 dB (x 0.2), `kParamStereo` 50 = centre -> `pan` = 2 x (v - 50), `kParamPad` = clean mode, X / Y -> formant X / Y. Filter off unless `kParamEnable`; the type of the Init preset is MG Low 6 | |
| A331 | The 111 type names of the binary map to our 80 types (model, style) through `serumFilterTypeOf`; names that are a variant of one of ours (LH6, LBH24, CombP / N, Phase12 .. 48, FlangePhase12HL6, RMT, FormantTWB, Scream, K35 -> Low 12) are approximated. PZ SVF poles / zeros are not imported | |
| A332 | Envelope times are seconds (x 1000), sustain a linear gain (x 100), curves 0 .. 100 with 50 straight: power = (50 - v) x 0.18, which makes the Serum defaults (50, 66.6, 66.6) our defaults (0, -3, -3). Beat sync is kept as seconds | 56 |
| A333 | LFO rate: Hz mode (`kParamBeatSync` = 0) is Hz (x 10 with the 10x flag); the tempo mode (the default) stores a number whose unit the file does not state, read as Hz at 120 BPM: the nearest division of the sync list (dotted / triplet from the flags). Mode: absent = trigger (Retrigger), `Free` = Free-run, `Envelope` = Envelope; Anchored = host; Rise / Delay seconds -> ms; Smooth % -> `smooth_time` 1000 x (v / 100)^2 ms | 57 |
| A334 | LFO and curve shapes: the polyline of `xVals` / `yVals` (y = 1 - screen y), the bend `curveVals` c (0.5 straight, below 0.5 fast start) -> power (c - 0.5) x 11; more than 128 points are resampled; an LFO without a drawn shape is a triangle; Path LFOs are not modelled (their curve and the Y source are dropped); Lorenz, Rossler, RandomSH LFOs take the Random LFO slots 1 .. 4 in file order (the sources and destinations that name them follow) | 58 |
| A335 | Modulation sources by the menu order (19 section 2.3): Mod Wheel, Env 1 - 4, LFO 1 - 10, Velo, Note, Aftertouch, Poly Aftertouch, Macro 1 - 8, Pitch Bend, Expr Y (-> Slide), Release Velo (-> Lift) map one to one; Note-On Rand 1 -> Random, Rand 2 and Rand (Discrete) -> Random (approximated), Expr Z -> Poly Aftertouch (approximated); Noise / OSC / Filter outputs, Alt. 1 / 2, Expr X, Fixed, LFO Y, Active Voices, Voice Mod / Index have no source here: the connection is dropped, an unsupported aux source is dropped alone | 59 |
| A336 | Matrix slot values: `kParamAmount` one to one, Bipolar / Inverted / Bypass switches, the CRV knobs `kParamCurveIn` / `kParamAuxCurve` +-100 -> power / aux power +-10 (v / 10), OUT -> `aux_output`; the drawn main curve -> the slot's mod curve (G2), the drawn aux curve, the slew (rise / fall) and the delay are dropped. A destination must be a modulatable key; the effect destinations follow the pool slot the effect became (rack x 100 + position) | |
| A337 | The modulation of LFO points: `lfoPointModAssignments` target 0 -> the X source of the point, 1 -> the Y source, 2 (curve) dropped; the source of the feeding slot and its amount / 100 become the point's `ContentModRef` | 60 |
| A338 | Effects: every module becomes a pool slot (`fxiNN`) in file order of the racks Main, Bus 1, Bus 2 (16 at most); the first convolution reverb that carries an IR goes to instance 0 (one IR per patch); the splitters are instance 0 modules (`splitter_*`), the modules that follow them stay in the rack (the bands are flattened, reported). EQ: the left band is the low band, the right band the high band; a band set to Peak uses the mid band (the second peak the shelf of its side). Compressor: the single band values go to all three bands; attack / release on the log scale 0.1 .. 1000 ms -> 0 .. 100 %; ratio r -> 100 (1 - 1/r) %. Reverb: Plate, Hall, Vintage, Space -> Nitrous, Abyss -> Basin; Feedback -> decay on the log scale 20 ms .. 30 s; Freq / FreqB (0 .. 100 %) -> low / high cut on the log scale 20 Hz .. 12 kHz; Delay / PreDelay -> pre-delay. Scales stated in `serum_map.cpp` with a `note` id: chorus depth x 4 %, distortion drive x 0.3 dB, phaser depth x 0.48 st, HyperDimension rate x 0.1 Hz, convolve size x 0.1, decay x 2.5, delay bandwidth x 12.12 %. Tempo sync of effects is flattened to the stored time | 61 |
| A339 | Routing: `kRoutingDestFilter` -> destination Filter blend with `filter_blend` = (balance + 100) / 2 (-100 = filter 1 only, as the Serum default), Master -> Effects only, Direct -> Direct out, None -> None; a filter channel routed to a filter -> `filter.routing` series; the bus sends one to one; ViaEnv1 -> the ENV 1 button | |
| A340 | Global: poly count, bend range up / down (link off when they differ), legato, portamento time (x 1000 ms) and curve (x 0.08), always / scaled, transpose, tuning A4, direct / bus volumes (linear -> dB) and destinations one to one; mono -> polyphony 1; limit same note -> Steal; oversampling 2 -> the 4x switch; MPE one to one; the scale quantizer by the scale name; swing, S1 compatibility, the tuning table, the voice panel, the arpeggiator and the clip player are dropped | |
| A341 | Content: a table (embedded or from `Tables`) is stored as a 16-bit table in the user library, a built-in shape is recognised; a sample is stored as a 24-bit WAV, root = `baseNote`; a multisample is reduced to the zone that holds C3 at medium velocity (root = pitch_keycenter, zone tune added to the oscillator tune); granular and spectral oscillators play their sample as a plain sample oscillator; the noise sample file is matched to a generator by its name (White, Pink, Brown, Geiger, Hiss, Vinyl, ...) or dropped; `trimHead` / `trimTail`, `HzOffset`, the sample position are dropped; the drawn remap curve (`flex`) goes to remap curve 1 of the oscillator | 63 |
| A342 | Serum defaults of absent parameters (counted as defaulted): OSC A on, the others, noise, sub, filters off; unison 1, blend 75, spread 100, range 2; envelope sustain 100 %, curves 50 / 66.6 / 66.6; LFO triangle, tempo mode, host on; OSC A through the filters, the rest to the main output; filter type MG Low 6; bend range 2; noise loop on; random phase 100 % on a wavetable oscillator; reverb type Plate, distortion Tube | |
| A343 | No new keys, no new state; the report, the content references and the `import` section of the `.sbpreset` are additive (`"import":{"source":{"format":"serum",...}}`) | |
| A344 | The category of a preset is the folder it sits in (Bass, Pad, Lead, Pluck, ... mapped to our categories), else the prefix of its name; tags `imported,serum2,<Serum tags>,src:<12 hex>` | |
| A346 | A modulation destination whose Serum value is a linear gain (oscillator / filter / effect levels, master volume, bus levels; stored in dB here) would move over the whole dB range with the same relative depth (a velocity depth of 75 % on a filter level = +54 dB); its depth is limited to 24 dB down and 12 dB up (`db_destination_depth_limited`, a depth that is already smaller stays) | 64 |
| A345 | The old container (format 0: zlib + UBJSON, 12 files) is read into the same value tree as the CBOR; the UBJSON reader supports the optimised containers and the `$` / `#` markers | |

**Numbers on the 656 real presets** (WP21-REPORT.md): 656 of 656 import, no crash or hang (slowest file 0.18 s), 0 unaccounted leaves; the independent audit recomputes 45543 voice values, 16924 effect values, 12810 matrix sources and destinations, 1433 stored tables (3.4 M samples compared with the embedded data) with 0 mismatches and finds all 7 injected mutations.

### 4.27 GROUPA1 item 0 and WP-04 – numeric robustness of the effect chain, the Serum limiter, Noise and Sub (B23: 6 keys, positions 2051..2056, destinations 904..906) – area 02 / 08

Added by the GROUPA1 work package (GROUPA1-REPORT.md). Assumptions A347 .. A361 are in the table at the end of this section.

**Item 0: NaN in the effect chain.** The Serum 2 preset `BA - Electric Bass - Deep Groover` drove the chain to NaN. The cause was not the compressor or the EQ: the loud signal they produce (compressor makeup +13.5 dB on three bands, EQ gains +4 / +9 dB at Q 63 / 47) reached a Diode 1 distortion at +12.7 dB, whose `futils::exp` (exponent below -126) built the float exponent field of `exp2` from an integer outside 0 .. 255 (pow2ToFloat) and returned garbage / NaN. `futils::exp2` now clamps the exponent to -126 .. 126 (the old working range is untouched: the two ends are the smallest normal float and 2^126), and the module outputs of the chain (`runRack`, `FxInstanceModule::run`) are scrubbed: a block with a non-finite sample is replaced by silence and the module is reset (a feedback path that blew up cannot latch NaN into the bus for ever). Regression tests with synthetic values: `fxi_loud_compressor_eq_diode_chain_stays_finite` (the slot values of the preset typed in; fails without the fix), `robust_exp2_stays_finite_and_monotone_for_any_exponent`, `robust_extreme_legal_values_never_produce_nan` (all 14 distortion types at both ends of the drive behind the loudest compressor / EQ settings).

**Item 0: the other render gate failures** (WP21-REPORT.md: 61 of 656 presets). After the fixes of this package 57 remain (33 peaks, 20 tails, 4 quiet, no NaN); each cause is decided:
* *Compressor in Limit mode (importer mapping bug, fixed).* Serum's compressor RATIO at its maximum is the LIMIT detent, "a completely different DSP circuit (a true peak limiter)" whose makeup gain range is 0-36 dB (manual p.163). The importer wrote ratio 100 % on a plain compressor and the makeup after it (128 compressor modules in 112 of the 656 presets, the file stores the ratio 1,000,000). A347: that value switches `compressor.limit` on, and in limit mode the makeup gain now drives the signal into the limiter (the peaks stay at the ceiling for any gain) instead of being added after it. Effect on the gate: 4 presets leave the peak list (`BA - Fretless Bass`, `DR - 909 Snare`, `GTR - Twelve String Guitar`, `LOOP - Synth Tech Drums`); three more (`FX - Church`, `PL - Moon Sailor`, `PL - Plucked Bottle`) leave it because their noise sample and delay offsets are imported now, and two presets whose dropped content now plays enter a list (`HIT - Gnarly Bass Stack` peak, `LD - Slide` tail). Taking one module type out at a time shows what is left: with all effects bypassed 16 of the 39 peaking presets pass, 14 pass without the compressor, 7 without the EQ, 6 without the distortion, 1 without the reverb.
* *Peak above +3 dBFS, 33 left (patch property, owner question 54).* 23 of the 39 peak with all effects bypassed as well, the others through compressor makeup of the Serum single-band compressor (14 pass without it) and EQ / distortion gain on top of a hot master; the chord peak of many is the clip of the output stage at +6.4 dBFS. Nothing in the importer is wrong that could be shown without Serum itself: the level law (oscillator and master volume as linear gains, A325) and the effect parameter scales (A338, question 61) decide it.
* *Tail longer than 12 s, 20 (patch property).* Without the effect chain every one of them ends within 9 s; the cause per preset (one module type removed at a time): 11 feedback delays, 4 FX filters in self-oscillation, 2 reverbs with a long decay, 2 compressors (makeup lifting the floor), 1 phaser, 1 distortion (two presets are cured by either of two modules). They are legitimate sounds (long feedback), not numeric faults: the gate's "silent below -90 dBFS within 12 s" is strict for them.
* *Quieter than -60 dBFS, 4 (patch property).* `PN - Emotional Upright`, `PL - Zakrium`, `SY - Air of Nostalgia`, `SY - Thoughtful Keys` open their level only through a modulation (velocity, macro); the render harness plays a note without moving a macro. Not a defect of the importer.

**The keys.**

| Key | Name | Type | Range | Default | Unit | Scale | Sc | Mod | Kick page | Group |
|-----|------|------|-------|--------:|------|-------|----|-----|-----------|-------|
| `sub.phase` | `Sub Phase` | float | 0 .. 100 | 0 | % | lin | V, N | y (904) | `sub_main` more sheet | `Sub` |
| `sub.phase_contiguous` | `Sub Phase Contiguous` | bool | Off / On | Off | - | - | G, X | no | `sub_main` more sheet | `Sub` |
| `noise.stereo` | `Noise Stereo` | float | 0 .. 100 | 0 | % | lin | V, S | y (905) | `noise` more sheet | `Noise` |
| `noise.filter` | `Noise Filter` | float | -100 .. 100 | 0 | % | lin | V, S | y (906) | `noise` more sheet | `Noise` |
| `noise.type` | `Noise Type` | choice | `Noise`, `Sample` | Noise | - | - | G, X | no | `noise` more sheet | `Noise` |
| `noise.bounce` | `Noise Bounce` | bool | Off / On | Off | - | - | G, X | no | `noise` more sheet | `Noise` |

Defaults are neutral: the 112 factory presets sound as before (`gen-factory-presets.py --check` passes after the regeneration that adds the six keys to the files). Capabilities (`engine_caps.h`, bits 52 .. 54, each set because `test_noisesample.cpp` renders the keys off and on): `kCapSubPhase` (`sub.phase`, `sub.phase_contiguous`), `kCapNoiseColour` (`noise.stereo`, `noise.filter`), `kCapNoiseSample` (`noise.type`, `noise.bounce`). `kNumModDestinations` 904 -> 907. Kick: the more sheets of the pages `sub_main` (3 -> 5 keys) and `noise` (7 -> 11 keys); the page count stays 135.

**Engine.** Sub: `SubSource` starts a note at the phase `sub.phase` (the sine starts at the quarter cycle at 25 %, inverted at 50 %); with `sub.phase_contiguous` a new note starts with the phase the note that started last had at the beginning of the block (A358). Noise colours (White, Pink, Brown, Geiger, the first four generators of `noise.sample`): they carry a second, independent channel (generated with a salted seed, no new random generator: the engine's seed counter must not move); STEREO mixes the right channel as `corr x L + sqrt (1 - corr^2) x R_independent` with `corr = 1 - stereo` (0 = left == right bit for bit, 100 = no correlation, the level of the right channel is kept, A355); FILTER is a two pole low pass (negative, 20 kHz at -0 % to 40 Hz at -100 %) or its complement (positive, 20 Hz to 16 kHz), poles follow the signal while it is off (no click), 0 is bypassed bit-exactly (A356). The other generators and a user sample are not touched by either knob (manual p.129). `noise.type` = Sample plays the content sample of the noise source (`content.samples[]` with `osc` 4, `kNoiseSampleOsc`): the file is decoded and its band limited buffers are built on the message thread (`Sample::makeUserData`, at most 1,764,000 frames), the audio thread only swaps a pointer (`Sample::useUserData`, as `setNoiseType` does); all of `noise.loop`, `.phase`, `.random_phase`, `.pitch`, `.fine`, `.keytrack` act on it; without a loaded or readable file the generator of `noise.sample` plays (A354). `noise.bounce` is Vital's `sample_bounce`: with `noise.loop` On a ping-pong loop, with it Off forward then back once (A357).

**State.** `content.samples[].osc` may be 4 (the noise source); `kMaxContentSamples` 3 -> 4, `validateContent` accepts 1 .. 4, the module lock "noise" keeps the entry; an older reader that knows 3 samples refuses a section with 4 (the section is additive: a state without the noise sample is unchanged).

**UI.** `NoisePanel`: the name button opens the NOISE menu (the nine generators, the loaded sample, Load Sample..., Remove Sample), a file row with Load... / Remove (touch sized), the picture shows the real waveform of the file and its name, the status line says what happened; the load goes through the WP-01 flow `loadSampleFile` for `osc` 4 (stored in the user library, `noise.type` switches to Sample, no oscillator is taken; the Missing Files dialog says "Noise sample"). More sheet: type, bounce, stereo, filter. `SubPanel`: the picture starts at the start phase, a dot marks the start, "CONT" for the contiguous option; more sheet: phase, contiguous. A relinked missing file now reaches the engine (`UiHost::reloadContent`, found by the noise sample test: `setContent` with an equal content did nothing).

**Importers.** Vital: a custom sample becomes the sample of the noise source (`noise.type` = Sample, `content.samples[]` osc 4) with `sample_loop` -> `noise.loop`, `sample_bounce` -> `noise.bounce`, `sample_keytrack`, `sample_random_phase`, `sample_transpose` -> `noise.pitch`, `sample_tune` x 100 -> `noise.fine` and the modulation of those two (A359); no free oscillator is needed any more (the five presets Primal Hunger, Gentle Driven EP, Cloudgazer, Edge of Space, Time Spiral kept their sample: 35 presets, 7 samples, 0 dropped); an engine without `kCapNoiseSample` keeps the old oscillator mapping. The warp "from sample" (`sample_input`) still has no home (WP-06). Serum 2: the sample of the noise oscillator is the data embedded in the preset, else the file of the Noises folder (stored as 24-bit WAV, `osc` 4), unless the preset names a colour (A360); `kParamColor` of a colour is the FILTER knob (A348), of a sample noise dropped; the Sub `kParamInitialPhase` (degrees) is `sub.phase` and `kParamContiguousPhase` `sub.phase_contiguous` (A349); a new AIFF / AIFF-C decoder (PCM and 32 bit float) reads the `.aif` files of the factory folders (A361). Numbers on the 656 presets: the stored files go from 2110 to 2662 (the noise samples, the one embedded sample of `TSP_S2LD_Bass_jump` among them), the audited audio samples from 476 to 1028, the dropped leaves from 966,999 to 886,903 (78,241 of them the embedded sample), 0 unaccounted; the audit (new check `audit_noise`: 656 noise sources, sample / generator / filter) has 0 errors and finds the 7 injected mutations.

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A347 | Serum compressor RATIO at its maximum (the file stores 1,000,000; from 100,000 on) is the LIMIT detent: `compressor.limit` On for the module (all bands), ceiling = the threshold; the makeup gain (`gain`, 30 dB at most, the manual says 36) is applied before the limiter, so the peaks stay at the ceiling. The old limit mode applied the gain after the limiter; no factory preset used the key | 65 |
| A348 | `kParamColor` of a colour noise (White, Pink, Brown, Geiger) is the FILTER knob, 0 .. 1 with 0.5 in the middle (off): `noise.filter` = (v - 0.5) x 200, below 0.5 a low pass. The file has no STEREO value (the colours import as mono, the engine default) | 66 |
| A349 | The Sub `kParamInitialPhase` is in degrees (the files hold 130.3, 140.6, 180): `sub.phase` = degrees / 3.6 %; `kParamContiguousPhase` -> `sub.phase_contiguous` | |
| A350 | `mpeConfig` (the main menu mappings) is 0 in all 656 presets: no mapping, the keys stay Off; a number nobody has seen is reported, not decoded | 67 |
| A351 | Delay offsets: `kParamOffsetL` -> `delay.offset_left`, `kParamOffsetR` -> `delay.offset`, a scalar on the time of each channel (manual p.166, 1.1 = 110 %). Before, the left one was written to the right-channel key and the right one dropped (151 modules) | |
| A352 | Reverb FEEDBACK (`kParamFeedback`) of Space (= Nitrous) and Abyss (= Basin) is `reverb.feedback` in percent; for the other types the same parameter carries the decay (`reverb.decay`, log scale, A338) | 68 |
| A353 | `futils::exp2` clamps the exponent to -126 .. 126; a non-finite block at the output of a module of the chain is replaced by silence and the module is reset | |
| A354 | `noise.type` Sample plays `content.samples[]` osc 4; without a sample (nothing loaded, file missing) the generator of `noise.sample` plays; `kMaxContentSamples` is 4 | |
| A355 | STEREO of the colours: independent second channel, `corr x L + sqrt (1 - corr^2) x R`, `corr` = 1 - stereo; the mid / side width of the colour is the only definition the manual gives (0 mono, 100 no correlation) | |
| A356 | FILTER of the colours: two one-pole stages, low pass 20000 x 0.002^m Hz, high pass 20 x 800^m Hz (m = |filter| / 100), complement for the high pass, the control rate is per block | |
| A357 | `noise.bounce` = Vital `sample_bounce`: loop on + bounce on = ping-pong loop; bounce on with the loop off plays forward and back once | |
| A358 | `sub.phase_contiguous`: the new note starts with the phase of the note that started last, as it was at the beginning of the block in which the new note starts; the phase of a sub that does not sound is frozen | |
| A359 | A Vital sample is a fourth source: it becomes the noise source (exact: the audio is stored as it is, `sample_transpose_quantize` is dropped); a patch whose engine lacks `kCapNoiseSample` keeps the oscillator mapping of earlier versions | |
| A360 | The noise sample of a Serum preset: embedded data first, then the file in the Noises folder; a file that cannot be read falls back to the generator picked by its name (as before) | |
| A361 | AIFF / AIFF-C PCM (8 .. 32 bit, big endian or 'sowt') and 'fl32' float are decoded in `core/serum-import` (the only non-WAV, non-FLAC audio in the Serum folders: 20 `.aif` files, 9 of them noise samples of factory presets) | |

### 4.28 GROUPA1 WP-09 – MPE completion (B24: 4 keys, positions 2057..2060, no destinations) – area 01 / 07

Serum 2 manual p.325-326 and p.314. Capability bit 55 `kCapMpeRouting` (proven by `test_mpe.cpp`).

| Key | Name | Type | Range | Default | Sc | Mod | Kick | Group |
|-----|------|------|-------|--------:|----|-----|------|-------|
| `master.mpe.xyz_macro` | `MPE XYZ to Macros` | choice | `Off`, `XYZ -> Macro 1, 2, 3`, `YZ -> Macro 1, 2` | Off | G, X | no | `master_expression` more sheet | `Master / MPE` |
| `master.mpe.y_modwheel` | `MPE Y to Mod Wheel` | bool | Off / On | Off | G, X | no | `master_expression` more sheet | `Master / MPE` |
| `master.mpe.bend_to_x` | `MPE Pitch Bend also to X` | bool | Off / On | Off | G, X | no | `master_expression` more sheet | `Master / MPE` |
| `master.mpe.y_bipolar` | `MPE Y Bi-directional` | bool | Off / On | Off | G, X | no | `master_expression` more sheet | `Master / MPE` |

**The axes (A362).** X is the note expression *Pan* (a new expression `Event::Pan` = 3: MIDI CC 10 of a member channel, CLAP `PAN`, 0 .. 1 with 0.5 at the centre; Serum maps "CC10 (pan) to note expression X"), Y is the slide (CC 74, CLAP `BRIGHTNESS`, the existing `slide` source) and Z the pressure (poly pressure, channel pressure of a member channel, CLAP `PRESSURE`). The mappings read the axes at the event: the value arrives at the macro (the control, not the knob: the knob shows the stored value; the last axis value that arrived wins, the macros are global), and the macro goes back to its knob when the mapping is switched off or changed. The mappings act on the host's note expressions as well with `master.mpe.on` Off (manual p.325: "the main menu options to map note expressions to macros remain available even when MPE is disabled"); the channel pressure of a member channel and the bend of a member channel count as axes only with MPE on. `bend_to_x`: the wheel of a member channel (-1 .. 1) is also X (0 .. 1, 0.5 at rest) *in addition to* the per-note bend of the pitch (manual p.314). `y_modwheel`: Y is also the mod wheel of its channel (per note). `y_bipolar` (A364): Y is centred on 0.5 and the macros / the mod wheel get the size of the deflection |2 Y - 1| (a bi-directional controller has its neutral position in the middle and a unipolar target cannot be negative); the `slide` source stays raw. `YZ -> Macro 1, 2` (A363): Y on macro 1, Z on macro 2 (the name of the menu entry; the manual's description says "the X and Y axes", which contradicts its own name).

**UI.** `GlbMpePanel`: the three quick-map buttons of the pad set `xyz_macro` / `y_modwheel` (they only changed their caption before), show their state as toggles (a second tap switches off) and follow the key when it changes elsewhere; the four keys are controls of the More sheet. **Importers.** Serum `mpeConfig` (A350); `mpeEnabled` and the bend range were imported before. The Preferences row "MPE enabled by default" and "MPE Pitch Bend (also) maps to Expr X" / "Expr Y acts bi-directional" (S15-04) are now the defaults of the keys through the preset; the preference store is WP-02.

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A362 | The MPE axes: X = Pan expression (CC 10, CLAP PAN), Y = slide, Z = pressure; the last value wins; the macro control is driven, not the knob; a mapping acts on host note expressions with MPE off | 67 |
| A363 | `YZ -> Macro 1, 2` is Y -> macro 1 and Z -> macro 2 | 67 |
| A364 | `y_bipolar` On: macros and mod wheel get |2 Y - 1| | 67 |
| A365 | `Event::Pan` (expression 3) is new: CC 10 of any channel becomes a note expression Pan of the whole channel (key -1); it has no effect while `xyz_macro` is Off | |

### 4.29 GROUPA1 WP-10 – FX remainder (B25: 2 keys, positions 2061..2062, destinations 907 and 908) – area 08

| Key | Name | Type | Range | Default | Unit | Scale | Sc | Mod | Kick | Group |
|-----|------|------|-------|--------:|------|-------|----|-----|------|-------|
| `delay.offset_left` | `Delay Offset Left` | float | 0.25 .. 4 | 1 | x | skew 0.5 | G, S | y (907) | `fx_delay_3` more sheet | `FX / Delay` |
| `reverb.feedback` | `Reverb Feedback` | float | 0 .. 100 | 50 | % | lin | G, S | y (908) | `fx_reverb_3` more sheet | `FX / Reverb` |

Capability bits 56 `kCapFxDelayOffsetLeft` and 57 `kCapReverbFeedback` (proven by `test_fxextra.cpp`: `delay_offset_left_multiplies_the_time_of_the_left_channel`, `reverb_feedback_scales_the_tail_of_nitrous_and_basin_only`). `delay.offset_left` multiplies the left delay time exactly as `delay.offset` does the right one (A367; 1 is the old sound bit for bit); the pool modules (`fxiNN`) take both through their descriptor (the delay has 14 parameters, the reverb 20 of the 24 a slot holds; the new keys come last, so no stored slot value moves). `reverb.feedback` is the FEEDBACK of Nitrous and Basin (manual p.177): the T60 of the decay time is scaled by 2^((f - 0.5) x 4), a quarter at 0 and four times at 100; 50 is the decay time as it is (exp2 (0) is exactly 1: the patches that never had the key sound as before), the other types do not read it (A366); `reverb.decay` stays a live control of both types (it is the base time). UI: the delay view draws the left echoes at time x offset; the reverb panel dims FEEDBACK for the types that do not have it (it applies to Nitrous and Basin), the Delay and Reverb panels carry the keys in their More sheets.

**Rack operations (S10-06, A368).** `core/fx_rack.h`: `fxCopyRack` / `fxClearRack` / `fxPasteRack` and the clipboard `FxRackClip`, the UI `FxRackView` (a "Bus ..." button in the strip, 44 pt, opens a menu with Cut / Copy / Paste into / Clear for each of Main, Bus 1, Bus 2; `fx_ops::copyBus` / `cutBus` / `pasteBus` / `clearBus`). A bus is the modules of a rack: the slots of the pool and the modules of instance 0 that are on (a splitter has no pool type and stays). Copy reads them into the clipboard with their normalised parameters and power (the modulation assignments do not travel, like a module copied without Shift); Cut = Copy + Clear; Clear removes the slots and bypasses the modules of instance 0 (removing one means bypassing it, A19); Paste adds the modules as new slots at the end of the target bus in order, as many as the pool has room for. All edits are the writes of one host gesture each, so undo / automation see them. **Rack presets** (S10-02) were closed by WP-01 (the library menu of the rack view, whole chain presets, A320); nothing is added. Several instances of one effect are the B22 pool (A19 was reversed by the owner), nothing to decide.

**Importers.** Serum delay offsets (A351) and the FEEDBACK of Space / Abyss (A352), 152 + 151 delay modules and 199 reverb modules of the 656 presets: the `delay_offset_left_only` approximation and 151 dropped `kParamOffsetR` leaves are gone.

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A366 | `reverb.feedback` scales the T60 of Nitrous and Basin by 2^((f - 0.5) x 4) and is inert for the other types; the manual gives no law for Serum's FEEDBACK of the two reverbs (our reverbs are our own designs) | 68 |
| A367 | `delay.offset_left` has the range, skew and snaps of `delay.offset` | |
| A368 | Bus operations on the modules of a rack, clipboard of the rack view, modulation not copied, no lock (Lock FX Bus of the manual is the preset lock of WP-02) | |

### 4.30 GROUPA2 WP-07 – Voice control (B26: 62 keys, positions 2063..2124, three modulation sources, no destinations) – area 01 / 05 / 07

Serum 2 manual p.316-319, the real files (`VoicePanel0` of 104 of the 656 presets, the 18 `.SerumStyle` files). **What was already there (A369):** polyphony, voice steal priority, same-note mode, legato and poly legato, portamento, the unison laws and the oscillator key / velocity mapping are B14 / B15 (`master.polyphony`, `master.voice.*`, `master.poly_legato`, `master.portamento.*`, `oscN.unison.*`, `oscN.map.*`); WP-07 is only the *per voice* panel of Serum (the sequence, the randomisation, the scaling, the mask) and its sources, so nothing is built twice. Capability bits 58 `kCapVoiceControl` (the keys) and 59 `kCapVoiceSources` (the source choice values 40 .. 42), both proven by `test_voicecontrol.cpp`.

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick | Group |
|-----|------|------|-------|--------:|------|-------|----|-----|------|-------|
| `voicectl.length` | `Voice Control Sequence Length` | whole | 1 .. 8 | 8 | - | lin | V, X | no | `voicectl_main` | `Voice Control` |
| `voicectl.sN.pan` (N = 1..8) | `Voice Control Step N Pan` | float | -100 .. 100 | 0 | % | lin | V, X | no | `voicectl_pan` | `Voice Control / Step N` |
| `voicectl.sN.detune` | `... Detune` | float | -100 .. 100 | 0 | cents | lin | V, X | no | `voicectl_detune` | same |
| `voicectl.sN.cutoff` | `... Cutoff` | float | -100 .. 100 | 0 | % | lin | V, X | no | `voicectl_cutoff` | same |
| `voicectl.sN.env` | `... Envelope Time` | float | -100 .. 100 | 0 | % | lin | V, X | no | `voicectl_env` | same |
| `voicectl.sN.mod1` / `.mod2` | `... Mod 1` / `Mod 2` | float | -100 .. 100 | 0 | % | lin | V, X | no | `voicectl_mod1` / `_mod2` | same |
| `voicectl.rand.pan` / `.cutoff` / `.env` | `Voice Control Random Pan` ... | float | 0 .. 100 | 0 | % | lin | V, X | no | `voicectl_random` | `Voice Control / Random` |
| `voicectl.rand.detune` | `Voice Control Random Detune` | float | 0 .. 100 | 0 | cents | lin | V, X | no | `voicectl_random` | same |
| `voicectl.rand.detune10x` | `Voice Control Random Detune x10` | bool | Off / On | Off | - | - | V, X | no | `voicectl_random` | same |
| `voicectl.scale.env` | `Voice Control Envelope Time Scaling` | float | 25 .. 400 | 100 | % | skew 0.43 | G, X | no | `voicectl_main` | `Voice Control / Scaling` |
| `voicectl.scale.lfo` | `Voice Control LFO Time Scaling` | float | 25 .. 400 | 100 | % | skew 0.43 | G, X | no | `voicectl_main` | same |
| `voicectl.scale.lfo_mode` | `Voice Control LFO Scaling Mode` | choice | `Percent`, `Rate` | Percent | - | - | G, X | no | `voicectl_main` | same |
| `voicectl.osc.a` / `.b` / `.c` / `.noise` / `.sub` | `Voice Control Applies to Osc A` ... | bool | Off / On | On | - | - | V, X | no | `voicectl_main` | `Voice Control / Targets` |

62 keys (1 + 48 + 5 + 3 + 5), none is a modulation destination (the destinations stay 908). Appended modulation sources (state version 3 list, choice values 40 .. 42): `voice_index` (`Voice Index`, 0 .. 1), `voice_mod1` (`Voice Mod 1`, -1 .. 1), `voice_mod2` (`Voice Mod 2`); `kNumModSources` 39 -> 42. All defaults are neutral: every step is 0, the scalings are 100 %, the mask has every target, the sequence has 8 steps; the 112 factory presets are unchanged (`gen-factory-presets.py --check`).

**The engine (`voice_handler.cpp:218` `evaluateVoiceControl`).** Every note gets a counter that restarts with `allSoundsOff ()` (`VoiceState::sequence`; the two warm-up notes of `prepare ()` do not count), the step is `(counter - 1) mod length`; the legato / steal re-triggers count as notes, a chord takes consecutive steps in event order. The values are evaluated for the voice in every block from the step and a hash of (seed, counter, stream) - the random draws are therefore stable for a note and repeat in every render (determinism test). Pan adds to the pan of the source (`osc_N_pan`, `sub_pan`, `sample_pan` through a `cr::Add` in the module), detune moves the oscillators through their key fold (`osc_fold`, so a sample oscillator keeps its slice key) and the sub and the noise through their tune, the cutoff adds semitones to the key tracking term of both filters (`FilterModule::kVoiceCutoff`), the envelope time is a per voice factor on delay / attack / hold / decay / release of all six envelopes (`EnvelopeModule::kTimeScale`), the LFO scaling a factor on the frequency of the ten LFOs and the four random LFOs (`LfoModule::kRateScale`, one mono value set by the adapter, `vital_engine.cpp:2145` `applyVoiceControl`). Defaults multiply by exactly 1 and add exactly 0.

**UI.** `GlbVoiceControlPanel` (`Page::glb`, order 55): the Pan / Detune / Cutoff / Env time / Mod 1 / Mod 2 chips choose the row, eight 44 point cells (two lines of four on a phone) are vertical bars (drag, double tap resets, long press opens the sheet), the number buttons 1 .. 8 above them are the sequence length (cells beyond it are dimmed), Init resets the 62 keys, Presets is the user library (`voice_control` category, item `synthyboy-voice-control`, `library_items.h`) and "Load from file" takes a `.SerumStyle`. The four primary knobs are the length, the two scalings and the LFO mode, the random amounts and the mask are the More sheet. Eight Kick pages `voicectl_*` (136 .. 143).

**Importers.** Serum `VoicePanel0` -> the keys (`serum_import.cpp:508` `voicePanel`; every value is exact, `kParamGlobalScalingLfoTimeSnap` approximated), a `.SerumStyle` is a complete configuration (absent = default; written as `touched` onto the patch, and as `<name>.voicecontrol.json` by the CLI), the sources 56 / 57 / 58 of the matrix map to the three new sources (`serum_map.cpp`), `Active Voices` (55) stays dropped. The 18 style files import with 0 dropped; in the 656 presets 52 carry a voice panel (5 more are all-default) and 501 leaves less are dropped. `scripts/audit-serum-import.py` checks every panel value independently and that no voicectl key is written that the file does not mention (`audit_voice_panel`).

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A369 | Scope: B14 / B15 already hold polyphony, steal, legato, portamento, unison; WP-07 adds only the per voice panel, its randomisation, scalings, mask and the three sources | |
| A370 | Layout: 8 steps x (pan, detune, cutoff, env time, mod 1, mod 2); the mask has A, B, C, Noise, Sub (the files carry `kParamOscA/B/C/N/S`); the default length is 8 (an absent `kParamVoiceCount`) | |
| A371 | The step is picked by a note counter (see above); Voice Index = step / (length - 1), 0 for a length of 1 | 72 |
| A372 | Value laws: pan +-100 % of the pan, detune +-100 cents, cutoff +-100 % = +-36 semitones, envelope time +-100 % = x 4^(+-1), Mod 1 / 2 = +-1; random amounts draw uniformly in +-amount (detune in cents, x10 with the switch) | 71 |
| A373 | The values follow the voice live (edits reach sounding notes), the random draw is fixed per note | 72 |
| A374 | Scaling: 25 .. 400 % on a log taper for the envelope times and the LFO times; Percent mode is a time (200 % = half the rate), Rate mode a speed (200 % = twice); applies to free and tempo synced LFOs, the random LFOs included; the Serum "Snap" flag is read as Rate | 71, 72 |
| A375 | Voice Mod 1 / 2 are bipolar, Voice Index unipolar | |
| A376 | Pan and detune of Osc A / B / C / Noise / Sub act as described above; detune of an oscillator with pitch tracking off is not applied (the fixed pitch ignores the fold) | |
| A377 | A target that is off ignores pan and detune; cutoff, envelope time, the scalings and the sources are not masked (filters and envelopes belong to no oscillator) | |
| A378 | Voice control presets are items of the user library; Init is the default configuration | |
| A379 | Serum import: `VoicePanel0/plainParams` as above, a style resets everything it does not mention, random detune up to 100 cents (x10 with `kParamGlobalRandomOscDetune10x`) | 71 |
| A380 | Sources 56 / 57 / 58 of the Serum matrix are Voice Mod 1 / 2 and Voice Index | |

### 4.31 GROUPA2 WP-02 – Preferences and dead toggles (no new keys; `kMaxParams` untouched) – area 01 / 07 / 10

`core/include/synthyboy/preferences.h`, `core/src/preferences.cpp`: the 14 preferences of Serum p.313-315 and Vital p.42 as `Preferences`, stored per user as `<userDir>/preferences.json` (atomic write, damaged file = defaults), read by both shells and by the UI through `UiHost::preferences ()` (`setPreferences`, `preferencesRevision`). The settings panel (`GlbSettingsPanel.h`) has one 44 point toggle for each (one column on a phone, two on a tablet) and the three buttons that only flipped a private bool are gone.

| Preference | What it does now (file:line) |
|------------|-------------------------------|
| `doubleClickResets` | a double tap resets (default) or opens the value box of the sheet with the cursor in it (`ParamControls.cpp:414`, `Sheet.cpp` `focusValueBox`) |
| `valueBubbles` | the bubble of a drag is shown or not (`ParamControls.cpp:201`) |
| `mouseWheel` | the wheel turns the control under it or is passed on to scroll the page (`ParamControls.cpp:457`) |
| `keyboardShortcuts` | gates `Editor::keyPressed` (`Editor.cpp:191`) and the Appendix G gestures below |
| `waveform3d` | the start view of `OscWaveView` and `WavetableView` (2D by default; the Wavetable panel used to start in 3D) |
| `freqSemitones` | cutoff-type readouts (`isCutoffReadout`: Hz keys with cutoff / centre / cut / lpf / hpf / freq / crossover in the key) show "+7.0 st" from middle C (A382) in every control, sheet and bubble (`valueText`), and the value box takes "7", "-12 st", "2 kHz" |
| `mpeByDefault`, `mpeBendToX`, `mpeYBipolar` | the three MPE keys of a new instance (JUCE `PluginProcessor.cpp` constructor, native `synthyboy_clap.cpp`); a patch saved as default wins (A383) |
| `limitModDepth` | `AssignFlow::dragBy` clamps the depth to the room of the knob (`ModModel.cpp:203`): a knob at 75 % takes +25 %, a bipolar slot the smaller side |
| `modWheelToWtPos` | `WavetableEditorSheet::followModWheel` (`WavetableEditorSheet.cpp:2914`) scans the frames with the mod wheel while the editor is open; the processor records the wheel (`UiHost::modWheel`) |
| `silenceOnStop` | the stop edge of the host transport (`TransportStopWatch`, `preferences.h`) calls `IEngine::silence ()` (JUCE `PluginProcessor.cpp:412`, native `synthyboy_clap.cpp:351`); the vital engine ends all voices, clears the effect tails and the note book (`vital_engine.cpp:541`), keeps controllers, parameters and content; a host without transport information never stops (A381) |
| `loadMidiMapFromPresets` | with it a preset that carries a CC map (a Serum preset's `midiMap`) brings that map, without it the default map file is loaded (`PluginProcessor.cpp:104` `applyMapOfPresetChange`) |
| `ultraRender` | a host bounce (JUCE `isNonRealtime`, CLAP `render` extension offline) runs at 4x oversampling: `IEngine::setUltraRender` (read by `prepare ()` like `quality.oversample_4x`, A385) |

**Lock Matrix, quality, tuning (A387).** `kMatrixLock` ("matrix": the 64 slots `modNN.*` with their aux keys and `content.mod_curves`), `kQualityLock` ("quality": `quality.*`) and `kTuningLock` ("master.tuning": the keys and the tuning table) are lock names of the existing shell lock list (saved with the session); the Lock Matrix item of the matrix menu (`ModMatrixPanel.cpp:785`), the "Lock Across Presets" button of the Quality panel (`GlbQualityPanel.cpp:30`, it only flipped a bool before) and a new "Tuning Lock" button of the Tuning panel (`GlbTuningPanel.cpp:130`) set them.

**MIDI map files and the default map (A386).** `{"format":"synthyboy-midi-map"}` files (`serializeMidiMap` / `parseMidiMap`), buttons Load MIDI Map... / Save MIDI Map... / Set Default Map / Clear Default Map in the settings; `<userDir>/midimap/Default.sbmidimap` is loaded by a new instance (unless the default patch has a map), Init Preset and every preset change. The Serum importer reads `.SerumMIDIMap` files and the `midiMap` of presets (ids `module x 1,000,000 + index x 1,000 + parameter`; only the macro module 7 is decoded, "Macros CC14-21" is the map of 6 factory presets, the other ids are reported `midi_map_parameter_not_decoded`): `SerumImportResult::midiMap`, `<name>.sbmidimap` from the CLI, `UiHost::loadPatch (..., presetMidiMap)`.

**Appendix G (A390).** Implemented, with the shortcut preference on: Cmd-click (Ctrl-click) resets a control, Shift-drag is fine, Shift-Alt-click on a modulated control switches the modulation between directional and bi-directional (`ParamControls.cpp:293`, `:306`), Alt-click on the filter display cycles the display modes (`FilterViews.h`), Alt-tap on the FX bypass buttons bypasses the bus (existed), module copy by Alt-drag of the label (existed), Cmd-Z / Shift-Cmd-Z / Ctrl-Y (existed). Not applicable: the preset browser and preview keys (Kick), the clip player, arpeggiator and piano roll (not in this synthesizer), Option-F to expand the matrix and rack (the pages are scrolling lists), the Renders folder, the LFO / sample gestures that the editors have in their own form (double tap adds a point, the grid snaps).

**About (S15-20, A392).** `UiHost::aboutText ()`: "SynthyBoy 0.1.0 | built Nov  9 2026 | Engine: vital DSP (AGPL-3.0) | 2125 parameters | content packs: N" - the version is the CMake project version (`SYNTHYBOY_VERSION_STRING`), the date `__DATE__`, the engine from its capability bits, the packs the directories of `<userDir>/packs`.

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A381 | Silence on stop: the running -> stopped edge with transport information; the live notes of a stopped host are not touched; `IEngine::silence ()` is the engine's panic (voices, effect tails; the first block after it may carry -69 dBFS of filter state, dead after two blocks) | 73 |
| A382 | Frequency units: semitones from middle C (261.6256 Hz), one decimal; only the cutoff-type readouts, not LFO rates, not A4 | 73 |
| A383 | The MPE preferences set the keys of a new instance only when no user default patch exists; Init Preset is not touched | |
| A384 | Defaults: silence on stop ON (what the panel showed), limit mod depth OFF, every other as in Serum (tooltips / bubbles / wheel / shortcuts on, reset on double click, 2D, MPE off, no map from presets, no ultra render) | 73 |
| A385 | Ultra render = 4x oversampling (the option of `quality.oversample_4x`) when the host renders offline and the preference is on; JUCE: set in `prepareToPlay` and `setNonRealtime` (the 20 Hz timer prepares again), CLAP: `render.set`, then the host restarts | |
| A386 | MIDI map: our file format and default file; applied on a new instance (if empty), Init Preset, every preset change; a preset's own map only with the preference; Serum ids: only the macros | 74 |
| A387 | Locks of the global settings are names of the existing lock list | |
| A388 | Limit mod depth acts on the touch drag (`AssignFlow`), not on the numeric entry of the sheet | |
| A389 | Mod wheel to WT position: the wheel (0 .. 1) picks frame round (wheel x (frames - 1)) when it moves, the editor is not fought when a frame is chosen by hand | |
| A390 | The Appendix G set above | |
| A391 | The default waveform view also applies to the wavetable panel, which started in 3D | |
| A392 | The About line as above | |

### 4.32 GROUPA2 WP-17 – Live read-out of the engine (no keys, no capability bit; `LiveValues` in `core/include/synthyboy/readout.h`) – area 05 / 06 / 07

`IEngine::setReadoutTargets (indices, count)` and `copyReadout (LiveValues&)` (any thread, relaxed atomics, no allocation: `synthyboy_test_rt` stays green). The vital engine writes at the end of every block (`vital_engine.cpp:1271` `updateReadout`): the value of each of the 42 modulation sources (the Vital status outputs of the voice that was heard last; macros from their controls), the phase of the ten LFOs and the stage / position of the six envelopes of the heard voice, the number of voices, and for up to 96 subscribed knobs the modulated plain value: the sum of what the connections add (`modulation_amount_N` status output x destination range, only slots that end at that knob) is added to the raw value of the plain value and converted back with `invertParam` (the new inverse of `convertParam`, tested for every binding); a knob that nothing modulates, a choice and the frame positions show the plain value. Unused LFOs and envelopes are switched off by the engine and have no phase (A393). `UiHost::setReadoutTargets / copyReadout` forward to the engine; the other hosts say false.

**UI.** `LiveBus` (`shell-juce/ui/LiveBus.cpp:172`, one per editor, `UiContext::live`): displays `bind` themselves (shown = visible with all parents), their knobs are merged into one target list, the values are copied 30 times a second while a display listens (5 Hz housekeeping while only hidden displays are bound, nothing when none is), plus the history of the four random LFOs (160 samples). Consumers: every modulated knob / slider gets a bright mark on its ring at the live value (`Overlays.cpp:115`, `ParamDragControl::liveTick`); the LFO display a playhead and a dot on the curve (`LfoPanel.cpp:268` `playheadFraction`); the random LFO a trace of the last 5 s; the envelope display a playhead through attack / hold / decay / release (`EnvCurveView.cpp:55` `livePlayhead`); the sample view the live unison start next to the range band of "Show marker animation" (`OscWaveView.cpp:409` `liveUniStart`); the MPE pad follows the pitch wheel / slide / pressure of the heard voice when it is not touched.

**Scope decision.** "Marker animation shows only the range band" (WP-01) is in scope and closed (the live marker of the unison start - the only modulatable position of the sample view). "The MPE pad does not send events" (GROUPA1) is an input gesture, but the pad is also the display of the three axes, so the package takes both: `GlbMpePadView::sendExpression` sends the pitch bend, CC 74 and the channel pressure on channel 1 (the manager channel of MPE moves every note, an ordinary channel without MPE) when the pad is touched and springs back on release (A396).

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A393 | The read-out is the engine's own: an LFO / envelope that nothing uses has no phase (no playhead); the sources show the voice that was heard last | 75 |
| A394 | The live value of a knob is raw + the connections' contribution, mapped back to plain; stereo / curve remaps of a slot are inside the engine's amount | |
| A395 | Displays: mark, playheads, trace (160 samples at 30 Hz), marker line; the envelope delay and attack are one engine state, the dot is placed by the level | |
| A396 | The pad sends on channel 1, X = pitch bend, Y = CC 74, Z = channel pressure; letting go centres X and zeroes Y and Z | 76 |

### 4.33 GROUPB WP-05 - Oscillator extras (B27: 9 keys, positions 2125..2133, destinations 909 .. 911) - area 02

Keys (per oscillator N, in this order, one macro `SB_OSC_EXTRA2`): `oscN.ratio_src` (choice Base note, Osc 1, Osc 2, Osc 3, Sub; default Base note), `oscN.smooth_interp` (switch, default Off), `oscN.sample.slice` (-100 .. 100 %, default 0, modulatable: destinations 909 .. 911). `kMaxParams` 4096 untouched, state version unchanged, capability bit 60 `kCapOscExtras2` (all nine keys; proven by `core/engine-vital/tests/test_oscextra2.cpp`). Kick: `oscN_pitch_uni` +2 (ratio_src, smooth_interp), `oscN_sample_2` +1 (slice); the page count stays 143. The 112 factory presets only gain the nine keys at their neutral defaults (`gen-factory-presets.py --check` passes).

**Ratio SRC (S p.30).** In the Ratio pitch mode the oscillator is tuned to another source: its pitch is the pitch of the source (note + fold + transposition + tune + the coarse term of the source's own pitch mode, as of the last block, per voice) plus its own transposition, tune and ratio. The source publishes that pitch in a tap (`SynthOscillator::pitchTap`, `SubSource::pitchTap`; a sample oscillator writes it too, `SampleOscillator::setPitchTap`) that the next oscillator reads (`ProducersModule::connectRatioSource`, `producers_module.cpp:154`; the taps are plain shared outputs: the voice pairs are processed one after the other, so a tap holds the pitch of the pair that is being processed, nothing is allocated). The oscillators are processed in the order the sources need (the existing FM ordering loop got a second dependency). A source that is the oscillator itself, an oscillator or sub that is switched off, or a chain of sources that leads back to the oscillator (osc 1 tuned to osc 2 and osc 2 to osc 1) is the note of the voice (`effectiveRatioSource`). Only the Ratio mode reads the key; Semitones, Harmonics and Step play the note as before. The sample oscillator has no pitch modes (as before) and ignores the key. The panel greys the oscillator itself and the entries that would close a loop (`model/RatioSource.h`, `ParamChoice::setEntryFilter`) and dims the control outside the Ratio mode.

**Smooth interpolation (S p.45).** The Vital-derived engine selects the table frame with the integer part of the position (`SynthOscillator::computeSpectralWaveBufferPair`): at a fractional position it plays the lower frame (the position steps through the frames; a modulated WT POS moves in steps). `smooth_interp` On blends the frame with the next one by the fraction of the position (the two spectra are mixed before the inverse transform, `synth_oscillator.cpp:1338`; the control `osc_N_smooth_interpolation` of the engine, which no code read before, is now this switch). The quality tier does not set it any more (it never changed a sound). Default Off, so every earlier patch sounds as before.

**Single slice (S p.76).** `oscN.sample.slice` is added to the slice of the key in Play Single Slice mode: +-100 % = +-(number of slices - 1) slices, taken at the note on (`sample_oscillator.cpp:128`); in Play Slice to End mode and without slicing it does nothing.

**Importers.** Vital: `osc_N_smooth_interpolation` is `oscN.smooth_interp` (it was ignored as "quality tier or UI"); `scripts/audit-vital-import.py` checks it. Serum 2: `kParamPitchSource` (the two presets that hold it: 0 = Osc A in Osc C, 4 = Sub in Osc A) is `ratio_src` in the Ratio mode (A398); the table crossfade mode of the WT block (`kParamXfadeMode` = 1 in 29 oscillators) is `smooth_interp` On (A399); the modulation destination `kParamSlicingSingleSlice` is `oscN.sample.slice`; `scripts/audit-serum-import.py` has the check `audit_osc_extras` (2 ratio sources, 26 smooth flags, 0 errors) and two new mutations (`osc1.smooth_interp`, `osc1.ratio_src`).

**UI.** `OscPanel.cpp:88` (three keys in the More list), `:112` (entry filter), `:159` (dimming); the slice knob takes a source by touch (it is modulatable). Tests: `test_oscextra2.cpp` (6 cases: caps, ratio to an oscillator / the sub / a chain / a chord / outside the Ratio mode / a switched off source / loops, smooth Off steps and On blends, slice by value and by macro), `test_ui_gaps.cpp` `uig_wp05_...` (phone and tablet), `test_vital_import.cpp` `vi_smooth_interpolation_is_imported`, `test_serum_import.cpp` `serum_ratio_source_...`.

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A397 | Ratio SRC: the pitch of the source as of the last block, per voice (modulation of the source's transposition, tune and coarse term follows), plus the oscillator's own transposition, tune and ratio; a loop, itself or an oscillator that is off is the note; the key is read in the Ratio mode only | 78 |
| A398 | The enumeration of `kParamPitchSource` is read from two presets: 0 .. 2 = Osc A .. C, 3 = Noise, 4 = Sub; the source in a mode other than Ratio is ignored, the Noise has no home (reported), the oscillator itself is the note | 77 |
| A399 | Smooth interpolation Off steps through the frames (what the engine always did), On blends the next frame in; `kParamXfadeMode` = 1 is read as On; Vital's `smooth_interpolation` is the key | 77 |
| A400 | `oscN.sample.slice` is an offset of the key's slice in Play Single Slice mode, +-100 % = +-(slices - 1), evaluated at the note on; the key is modulatable (destinations 909 .. 911) | |

### 4.34 GROUPB WP-06 - Modulation sources and audio-rate taps (no keys; sources 43 .. 57, warp modes 52 .. 75, capability bits 61 and 62) - area 06 / 02

**Sources.** `kNumModSources` 42 -> 57 (choice values 43 .. 57, capability bit 61 `kCapModSources2`): Oscillator A, B, C, Noise, Sub, Filter 1, Filter 2 (43 .. 49), Active Voices (50), Note-On Alt 1, Alt 2, Note-On Rand 2, Note-On Rand (Discrete) (51 .. 54), Release Velocity (55), Expression X (56), Fixed (57). `LiveValues::kMaxSources` 48 -> 64, the LFO point modulation has an input per source (64). Proven by `core/engine-vital/tests/test_modsources.cpp` (every source moves a destination).

* **Outputs of the sound sources (A401).** The module that makes the signal writes the last sample of its block after level and pan (`ProducersModule::kTapOsc1 .. kTapSub`, `FiltersModule::kTapFilter1 / 2`); `SourceTap` (`producers/source_tap.h`, a control rate processor, one per source, registered by `SynthVoiceHandler::createSourceTaps`, `synth_voice_handler.cpp:365`) turns the stereo pair into one value per voice (both channels carry the mean) and holds it for the block. A control rate source feeds audio rate destinations through the connection as every LFO does. A source that depends on its destination (osc A on its own level) is a loop of one block (it reads the previous block, nothing is allocated, the sound stays finite: `a_sound_source_that_depends_on_its_destination_is_a_stable_one_block_loop`).
* **Active Voices** = active voices / the polyphony limit (mono). **Note-On Alt 1 / 2**: 0 / 1 by turns at each note on (the parity of the note counter, the counter of the voice control), Alt 2 is the inverse. **Note-On Rand 2**: a second random number per note (a hash of the voice control seed, the note counter and a stream, so the same notes give the same numbers). **Note-On Rand (Discrete)**: a number of its own for every modulation slot and note (64 outputs, `VoiceHandler::note_on_discrete`; the adapter hands slot k its own, `sourceFor`). **Expression X** = the per note bend (the member channel bend plus the note expression tuning) over its 48 semitone range, bipolar. **Fixed** = 1. (A402)
* **Release Velocity (A403)** is the note-off velocity: the engine's source `lift` is exactly that (`Event::NoteOff` `value`, held for the rest of the voice), so the new choice is the same output under Serum's name (the coverage rows called it missing because the `lift` source was not recognised as that; Serum's id 37 maps to the new choice). No second piece of code, no new signal; a test proves a hard release against a soft one changes the tail.

**Warp modes from the Noise, Sub and Filters (A404).** `kNumWarpTypes` 52 -> 76, capability bit 62 `kCapWarpSources`: entries 52 .. 75 = FM Thru-Zero (52 .. 55), FM Exp (56 .. 59), FM Linear (60 .. 63), PD (64 .. 67), AM (68 .. 71) and RM (72 .. 75), each from the Noise, the Sub, Filter 1, Filter 2 (the engine enum continues after `kAmB`: `kSrcFmThruZeroNoise .. kSrcRmFilter2`, `SynthOscillator::isSourceWarp`; the adapter map is the same `index + 2` as before). They are the modes of the other oscillators with another modulator: the noise is the sample output the oscillators already knew, the sub is processed before them, the filters sit behind the oscillators, so an oscillator reads the filter of the previous block through an audio rate Feedback node of the voice router (`ProcessorRouter::connectAudioFeedback`, `processor_router.cpp:194`: the node keeps its own history per voice pair, so a chord equals the sum of its notes: test `warp_modes_from_a_filter_use_the_filter_of_the_same_voice`). A sample oscillator takes FM, AM and RM from them (`sample_oscillator.cpp` `warpKindOf`); PD is a phase mode and inert on a sample, as before. Vital's FM and RM "from sample" (the engine's modes 9 and 12, not in our list) are the noise source here: the importer maps them to FM Thru-Zero from Noise (52) and RM from Noise (72), `warp_from_sample_mapped_to_the_noise_source` (A406). `legacyChoiceMax` keeps every other imported Vital choice in the eleven Vital modes.

**Importers (A405).** Serum 2 source ids 20 (Noise), 22 (Rand 2), 23, 24 (Alt. 1, 2), 34 (Expr X), 37 (Release Velo), 38 (Fixed), 49 .. 54 (OSC A .. C, SUB, Filter 1, 2), 55 (Active Voices) and 59 (Rand (Discrete)) are the new sources (12,991 matrix connections of the 656 presets import, 120 more than before); the warp enumerations `kFM_NOISE .. kRM_FILT2` are entries 52 .. 75 (FM, FMX, FMP = Thru-Zero, Exp, Linear as in A328). Left without a home: LFO 1 .. 10 Y (ids 39 .. 48). `scripts/audit-serum-import.py` follows (source table, warp table); the Vital audit follows the warp map (9 -> 52, 12 -> 72).

**UI.** The matrix source menu got the sources it lacked: Stereo, a "Voice Control" group (40 .. 42), "Sound Sources" (43 .. 49) and "Note & Voice" (50 .. 57), greyed by the capability bits (`ModMatrixPanel.cpp:326`); the control sheet's source menu has the two submenus (`Sheet.cpp:293`); the source bar gets 15 chips; the warp menu lists the new modes in its FM / PD / AM / RM groups (`model/GestureModel.cpp:318`). Tests: `test_modsources.cpp` (9 cases incl. 0 allocations while wiring and unwiring all the new sources, and the 24 warp modes against None), `test_ui_gaps.cpp` `uig_wp06_...`, `test_ui_model.cpp` (warp groups), `test_serum_import.cpp` (two cases), `test_vital_import.cpp`.

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A401 | The output of a sound source as a source is the last sample of its block after level and pan, the mean of the two channels, held for the block (control rate); Serum 2 id 20 .. 55 map to them | 79 |
| A402 | Alt 1 / 2 = parity of the note counter; Rand 2 and Rand (Discrete) = hash of the seed, the counter and a stream (Discrete: one per slot); Expression X = per note bend / 48 semitones; Active Voices = voices / polyphony; Fixed = 1 | 79 |
| A403 | Release Velocity is the existing `lift` output under its Serum name | 79 |
| A404 | The warp modes from a filter read the filter of the previous block; FM / AM / RM from the sources act on a sample oscillator, PD does not | 80 |
| A405 | Serum source ids and warp names as listed above | |
| A406 | Vital FM / RM from the sample = FM Thru-Zero / RM from Noise | 80 |

### 4.35 GROUPB WP-08 - Matrix RISE / FALL and control sheet extras (no keys; content `mod_curves[].rise / .fall`) - area 06

**RISE / FALL (S p.211).** `ContentModCurve` got `riseMs` and `fallMs` (0 .. `kMaxModSlewMs` = 5000 ms, `content_state.h`): a slew limiter on the source in front of the curve (`ModulationConnectionProcessor::slewStep`, `modulation_connection_processor.cpp:72`): a one pole filter per voice, the time of the rise (input above the value) or of the fall; the time is the span of 90 % of a step (ln 10 time constants), so a RISE of 100 ms reaches 90 % of a step upwards after 100 ms (measured: 101 ms; 400 ms: 400 ms; FALL 300 ms: 304 ms; one block of resolution). A fresh voice starts on its value. It acts on control rate sources and, through a slewed copy, on audio rate sources (the amplitude envelope). Both 0 = off: no record, no change (bit identical: `slew_defaults_are_bit_identical_and_other_slots_do_not_leak`). A record holds points, a slew or both (no points: `"points":[]`); `rise` / `fall` are written only when above 0, so a document without a slew is byte for byte what it was; the validator accepts a record without points only with a slew, and rejects a negative, too large or non finite time. 0 keys: the 128 keys of the MANUALCHECK proposal are not built (they would not be modulatable per slot in the engine either; the decision is the owner's, question 81). Lock Matrix keeps the record (`keepLockedContent` already carried `mod_curves`). (A407)

**Serum 2.** `kParamSmoothRise` / `kParamSmoothFall` (0 .. 100 of the knob in the 39 slots that hold them) are read as 2000 ms x (v / 100)^2, `kParamSmoothLink` (absent: on) makes a missing one follow the stored one (A408); the importer writes the record, the audit has the check `audit_mod_slew` (39 slew records, 0 errors).

**UI.** `ModCurvePanel.cpp:320` RISE and FALL sliders (touch height, side by side or one under the other on a phone) that write the record of the shown slot and remove it when it is empty again; the matrix row marks a connection with a drawn curve or a slew by the star on its Crv button (`ModMatrixPanel.cpp:155`). The control sheet: a tap on a modulation row opens its menu: **Aux Source** submenu (None and every source, the current one ticked, greyed by capability; `Sheet.cpp:41`, the row text shows "source x aux") and **Edit Custom Curve...**, which opens the matrix page with the slot's free curve editor (`UiContext::ViewState::openSlotCurve`, `Editor.cpp:46`, `Panel::showModSlotCurve`). Tests: `test_modslew.cpp` (6 cases: the step response of rise and fall, defaults bit identical, slew before the curve, per voice, audio rate, 0 allocations), `test_state_v3.cpp` (round trip of both forms, 4 rejections), `test_ui_wtremap.cpp` `uim_wp08_...` (phone and tablet), `test_serum_import.cpp` `serum_modulation_slot_rise_and_fall_become_the_slot_record`.

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A407 | RISE / FALL are content of the slot record, not keys; the time is the 90 % time of a one pole slew in front of the curve | 81 |
| A408 | Serum's 0 .. 100 of RISE / FALL is 2000 ms x (v / 100)^2; absent link = linked | 81 |

### 4.36 GROUPB WP-18 - Importer follow-ups (0 keys; one appended choice, capability bit 63) - area 11

* `osc_N_detune_range` is `oscN.unison.range` (semitones, default 2) and `osc_N_midi_track` is `oscN.pitch_track` (they were ignored as "engine fixed value": all 105 oscillators of the 35 real presets hold the defaults, so no preset changes, but a patch that does not is imported now).
* `voice_priority` is `master.voice.steal_priority`: 4 Round Robin (the engine default) is our Newest, 1 .. 3 (Oldest, Highest, Lowest) are one to one, Vital's 0 Newest is the nearest entry (our Newest, reported as approximated) (A409). Vital has no Velocity entry.
* **Saw Down** (G9): `oscN.wavetable` choice 14 (the list "Preset table" = 13 stays; capability bit 63 `kCapWavetableSawDown`, the last of the 64): the falling saw whose jump is at mid cycle, `-saw (phase + 180 degrees)`, sine coefficients 2 (-1)^k / (pi k): the waveform of Vital's `Init` table (`vital_tables.cpp`, slot 13 of the set; `builtin_ref.cpp` for the importer). A Vital table that is a single cycle equal to it (level free) is not stored but becomes choice 14 when the target has the bit (`ImportCaps::sawDownTable`): 34 of the 105 tables the audit set stored (its `Init` tables). The three phase shifted `Sine` tables stay stored (a shift is not a built-in table: it would have to move into `oscN.phase`, which is not the same under warp). `golden_v2.h` counts 2 appended entries of `oscN.wavetable`.
* **The audit script** followed the noise sample model (WP-04) in no place: it assumed a free oscillator and failed with `KeyError: 'osc4.type'`; it now checks `noise.type`, loop, bounce, key tracking, random phase, pitch and tune of the noise source and the modulation of `sample_transpose` / `sample_tune`. Its first full run found the accounting hole of `sample_transpose_quantize` (a mapped noise sample dropped the setting without an item); it has an item now. Result on the 35 real presets (25 of the bank unzipped + the 10 loose ones): 35 presets, 18,249 value rows, **0 mismatches, 0 unclassified, errors 0**; wavetables stored 105 -> 71.
* **G8 (stored `mod_wheel`, 23 files non-zero)** is decided without the A/B that needs Vital: not applied (A410), question 82. **The A/B listening of the clean-room renderer against real Vital** needs the owner's Vital and ears (14 row 1, Q34): it stays with the owner (question 83).

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A409 | `voice_priority`: 4 -> Newest, 1 .. 3 one to one, 0 -> Newest (approximated) | |
| A410 | The stored `mod_wheel` position of a Vital preset is a controller state, not a patch value: not applied (reported as ignored, as before) | 82 |
| A411 | "Saw Down" is appended as the choice 14 (the position of "Preset table" is a stored number and cannot move) | |

### 4.37 GROUPC WP-11 - Granular oscillator (B28: 93 keys `oscN.gran.*`, positions 2134..2226, destinations 912 .. 962, `oscN.type` 2) - area 02

**Type and capability.** `oscN.type` got the entry 2 `Granular`. The 64 bits of `EngineCapsMask` were all in use (bit 63 = WP-18), so GROUPC added a **second capability word** (`EngineCaps2Mask`, `kCap2*` in `engine_caps.h`; `IEngine::capabilities2 ()`, `requiredCaps2 ()`, `requiredCapsForValue2 ()`, `capsCover2 ()`; `supportsParam ()`, the importer options and the UI dimming (`capStateForKey / ForValue`, `UiHost::engineCaps2 ()`, `CapState::missing2`) need both words; word 1 is unchanged). Bit `kCap2Granular` covers the type value 2 and every `oscN.gran.*` key (`requiredCapsForValue2 (oscN.type, 2)`, a modulation destination that is a granular key); proven by `core/engine-vital/tests/test_granular_osc.cpp` (on / off render, each key, determinism, extremes, state round trip) and `core/tests/test_engine_caps.cpp` (the second word).

**Content and shared keys.** The granular oscillator plays the sample of `content.samples[osc]` (the same entry as the Sample type: loading a sample into a granular oscillator keeps the type, `ContentIO::loadSampleFile`). The region keys (`oscN.sample.start / end / loop_* / loop_xfade / scan* / uni_start / uni_span / slice*`) are the ones of the manual p.88 - 90 (shared operations); only the type specific keys are new.

**Keys per oscillator (31, one macro `SB_OSC_GRAN`; modulatable: window_amount, window_skew, density, length, length_percent, position, rand_offset, rand_dir, rand_pitch, rand_length, rand_pan, rand_level, rand_window, rand_skew, rand_warp, rand_warp2, xy_y = 17):**

| Key | Range / default | Meaning (manual page) |
|-----|-----------------|-----------------------|
| `window_amount` / `window_skew` / `window_shape` | 0 .. 100 % (50) / -100 .. 100 % (0) / 10 shapes (Hann) | grain window influence, peak position, shape: Hann, Welch, Gaussian, Blackman-Harris, Sinc, Tukey, Triangle, Trapezoid, ExpDec, Exp Dec Rev (p.91 - 93) |
| `density_mode` / `density` / `density_sync` / `density_grains` | Free, BPM Sync, Grains (Free) / 0.1 .. 1000 Hz (20) / 24 divisions 1/1 .. 1/128 straight, triplet, dotted (1/16) / 1 .. 64 (8) | grain spawn rate (p.101) |
| `length_mode` / `length` / `length_sync` / `length_percent` | Free, BPM Sync, Percent (Free) / 1 .. 4000 ms (100) / divisions (1/16) / 5 .. 1000 % of the density period (200) | grain duration (p.101 - 102) |
| `jump_start`, `max_grains`, `loop_grains` | On, 1 .. 128 (64), Off | full density at the note on; the pool limit (divided over the unison clouds); the grains respect the loop markers (p.89, 101) |
| `manual`, `position`, `scan_keytrack` | Off, 0 .. 100 % (0), Off | the play head is placed by Position (Manual: the SCAN knob of the manual becomes this key, p.89); the scan rate follows the pitch of the note (p.100) |
| `rand_offset`, `rand_dir`, `reverse_grains`, `rand_pitch`, `rand_length`, `rand_pan`, `rand_level`, `rand_window`, `rand_skew`, `rand_warp`, `rand_warp2` | 0 .. 100 % (0), Off, 0 .. 24 st (0) | per-grain randomisation (p.102 - 103) |
| `spawn_pattern` | Together, Even, Exp, Random | timing offset of the unison clouds (p.95) |
| `y_axis`, `xy_y` | 18 targets (None), 0 .. 100 % (50) | the X|Y control (p.96 - 98) |

**Engine** (`core/granular`, namespace `synthyboy::granular`; the Vital-derived wrapper is the type 2 branch of `SampleOscillator`, `sample_oscillator.cpp`). A **cloud** (`GranularVoice`) is one unison voice: a play head scanning the region at the scan speed (negative reverses, 0 freezes, loop modes of the Sample type incl. ping pong and Tailed), a spawner and a pool of 48 grains; each grain has its own read rate (pitch of the note + unison detune + `rand_pitch`, Hermite interpolation from the mip level that fits the rate), direction, window, pan, level and length. Grains are normalised by 1 / sqrt (overlap) (constant power). Realtime safe: the pool and the window tables are built at construction, `process ()` allocates nothing. Changing `oscN.type` under a running note starts the new kind and fades the old one out over 10 ms (`Slot::old_kind`), for all five kinds.

**Importer.** Serum 2 `kOsc_Granular` is type 2 (it was a Sample oscillator, `granular_played_as_sample`, when the engine lacks `kCap2Granular`). The block `GranularOsc#` is read by `kGranular[]` in `serum_map.cpp` (window, skew, shape, spawn pattern, Y axis assignment, the nine randomisations) and in code (`Importer::granularRates`, `serum_voice.cpp`): density and grain length with their modes; the common block's `kParamManualPositionMode` / `kParamPosition` are `gran.manual` / `gran.position`. `scripts/audit-serum-import.py` recomputes all of it (`audit_granular`, 66 granular oscillators of the 656 presets, 0 errors; selftest mutations of type, density, length percent and window amount are found).

**UI.** `OscWaveView` (the sample display with markers) plus `paintGranular`: the grains the settings spawn (bars across the region, deterministic), the **window thumbnail** (top right: a tap steps the shape, a drag sets amount (up) and skew (sideways)) and, in the manual mode, the **X|Y dot** (red; a finger anywhere on the display places it: x = position in the region, y = `xy_y`). `OscPanel` lists all 31 keys behind More and dims by type, by the density / length mode (only the triple member the mode reads) and by the manual mode. Kick pages `oscN_gran_1` (density, length, window amount / skew / shape, position, manual, jump start; More: modes, divisions, grains, percent, max grains, loop grains, scan key track) and `oscN_gran_2` (the randomisations; More: warp randomisation, reverse grains, spawn pattern, Y axis, Y position): 6 pages, 149 in total.

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A412 | A cloud per unison voice, a pool of 48 grains per cloud, Max Grains divided by the unison count; grains are normalised by 1 / sqrt (overlap); every grain has a 1/64 edge ramp so that an amount of 0 does not click; the window peak of the skew is at 0.5 + 0.45 skew | 84 |
| A413 | A Serum file without `kParamLengthMode` has the length mode Percent and `kParamGrainLength` is a multiple of the density period (2.17 = 217 %); with `kLengthBPM` the value is beats (nearest division), in Free mode seconds (the 656 presets hold values 0 .. 10 with the Percent reading, e.g. density 215 Hz and length 2.17) | 84 |
| A414 | `kParamDensity` is Hz in Free mode (clamped to 0.1 .. 1000), beats between two grains in BPM mode (nearest division), a grain count in Grains mode; the third density mode name is guessed (`...Grain...`) | 84 |
| A415 | The window shapes are listed in the order of the manual; the Serum names seen in files are `kWindowTukey / BlackmanHarris / Triangle / ExpDec / Gaussian`, the others are guessed in the same pattern; the default shape is Hann | 84 |
| A416 | Randomisations: offset = +- half the region x amount; direction = probability of a backwards grain (Reverse Grains inverts it); pitch in semitones (+-); length = shortened by up to the share; pan = random position; level = lowered by up to the share; window amount / skew +-; warp amounts +- (applied to the per-grain warps) | 85 |
| A417 | The X|Y control: while an axis is assigned, the Y of the dot is the normalised value of that parameter (replacing the knob; Level multiplies); Manual replaces SCAN by the key `position`; the scan key tracking multiplies the speed by 2 ^ ((note - 60) / 12) | 85 |
| A418 | Spawn patterns: Together 0, Even u / n, Exp 2 ^ (u / n) - 1, Random, as a share of the spawn period; Jump Start spawns the grains of full density at the note on; the pointwise warps (Quantize, the distortion group) act on every grain, the other warps (FM, AM, RM) on the mix | 85 |

### 4.38 GROUPC WP-12 - Spectral oscillator (B29: 42 keys `oscN.spec.*`, positions 2227..2268, destinations 963 .. 983, `oscN.type` 3, warp entries 76 .. 98) - area 02

**Type and capability.** `oscN.type` 3 `Spectral`; `kCap2Spectral` (type, keys) and `kCap2SpectralWarps` (the warp entries 76 .. 98) of the second word; `kNumWarpTypes` 76 -> 99. Proven by `core/engine-vital/tests/test_spectral_osc.cpp` (7 cases: capability, pitch independent of the scan and note starts at full level, every key off / on, filter curve / CUT / MIX, position + modulation, the 23 warps incl. inert on the wavetable and sample types, determinism / extremes / state round trip).

**Content.** The sample of `content.samples[osc]` (as for the granular type) and the drawn filter mask `content.spectral_filters[]` (new, additive: `{osc 1..3, name, points:[{x, y, power}]}`, at most 3 entries and 64 points, validated like `content.remaps`; `StateContent::spectralFilters`, `kMaxContentSpectralFilters`).

**Keys per oscillator (14; modulatable: freq_lo, freq_hi, cut, mix, freq_shift, position, xy_y = 7):** `freq_lo` / `freq_hi` 20 .. 20000 Hz (20 / 20000), `lohi_smooth` (On), `lohi_post_warp` (Off), `cut` -100 .. 100 % (0), `mix` 0 .. 100 % (100), `freq_shift` -2000 .. 2000 Hz (0), `manual`, `position`, `scan_keytrack`, `phase_lock`, `transients`, `y_axis` (None, Level, Warp, Warp 2, Spec Flt Cutoff, Spec Flt Wet/Dry, Freq Lo, Freq Hi), `xy_y`. The warp slots are the shared `oscN.warp.*` / `oscN.warp2.*` keys; the VAR fader is `warp.phase`.

**Engine** (`core/spectral`, namespace `synthyboy::spectral`). `Analysis` (non real time, built in `setContent` for every sample): STFT 2048 / hop 512 of every channel (at most 1536 frames = 16 s at 48 kHz, longer files are cut), magnitude, phase and the **instantaneous frequency** of every bin, transient flags (relative spectral flux). `SpectralVoice` (real time): the play head moves at the scan speed (time stretch without a change of pitch, loop modes as the Sample type, Manual), per hop the spectrum is read at the head (magnitudes interpolated), filtered (the drawn mask), band limited, warped, **pitch shifted by peak regions** (every region around a peak moves as a whole, so the shape of a partial's lobe is kept, Laroche / Dolson), the phases of the partials advance by their own frequency (Phase Lock ties the bins of a region to its peak, Transients restarts the phases at a flagged frame), inverse FFT, Hann, overlap add (the first three frames are primed so that a note starts at full level). Real time safe; the work arrays are shared by the voices of an oscillator.

**Warps (entries 76 .. 98, `synthyboy::spectral::Warp`):** Spectral Comb, Add Harmonics, Add Subharmonics, Spectral Detune, Shepard Narrow, Shepard Filter, Peak Octave Down / Up, Peak Harmonic Down / Up, Spectral Gate, Spectral Pitch Shift, Phase Twist, Spectral Spread, Spectral Shift, Spectral Smear, Spectral Mirror, Mask from Osc X / Y / Noise, Vocode from Osc X / Y / Noise. They act on the Spectral type only (inert on the others: the wavetable and sample oscillators see them as None; `test_spectral_osc` proves it); Mask and Vocode take the magnitude spectrum of the first / second other oscillator or the noise (the last 2048 samples of the voice's lane, one FFT per block).

**Importer.** `kOsc_Spectral` is type 3. `kSpectral[]` (freq lo / hi, smooth, post warp, phase lock, transients, SpecFltShift = `cut`, SpecFltWetDry = `mix`), the 23 spectral warp names of `kWarpEnum`, the common block's Manual / Position / `kParamHzOffset` (= `freq_shift`) and the `flex` curve of the block (= `content.spectral_filters`, not stored when flat; the file stores the screen y). Audit: 53 spectral oscillators, 29 filter curves, 0 errors.

**UI.** `OscWaveView::paintSpectral`: the **spectrogram** of the loaded sample (`SamplePreview::spectrogram`, log frequency axis), the **frequency strip** at the right edge with the Lo and Hi markers (drag; Lo cannot pass Hi; the band between them is shaded), the X|Y dot in the manual mode. `RemapEditor` got a third mode (`SpectralFilterMode`): the filter graph under the display (drag points, double-tap adds / removes, power handles, pencil, undo, **presets** Flat, Low pass, High pass, Band pass, Notch, Tilt down, Comb, Formants, Reset removes the entry). Kick page `oscN_spec_1` (3 pages, 152 in total). The warp menu has two new groups, "Spectral" (76 .. 92) and "Spectral Mask" (93 .. 98).

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A419 | STFT 2048 / hop 512, an analysis of at most 1536 frames per channel, unison of the spectral type capped at 8 (an inverse FFT per hop per voice); the analysis of every sample is made when the content is set (about 4 ms per second of audio, 12 KB per frame and channel) | 88 |
| A420 | The filter mask is a gain curve over the log frequency axis (20 Hz = 0, 20 kHz = 1), y 0.5 = unity, gain = 2 y; CUT shifts the curve along the axis by +- half the axis; MIX is wet / dry; no curve = flat, so CUT and MIX do nothing until a curve is drawn (the Serum default curve is not known); the mask acts on the source bins | 86 |
| A421 | Freq Lo / Hi: a fourth order Butterworth edge (Smooth) or a brick wall, on the source bins (before the warps) or, with Post Warp, on the output bins | 86 |
| A422 | The 23 spectral warps carry Serum's names (read from the presets: kSpectralComb, kAddharmonics, ...), the algorithms are our own (the manual does not describe them): amount a, VAR v as listed in `applyWarp` (`spectral.cpp`) | 87 |
| A423 | Phase Lock = identity phase locking around the spectral peaks; Transients = a restart of the phases (from the analysis) when the head enters a frame whose relative spectral flux stands out (mean + 2 sigma and above 0.35) | 87 |
| A424 | `kParamHzOffset` is a frequency shift in Hz of every partial (`freq_shift`, clamped to +- 2000 Hz) | 86 |
| A425 | Mask / Vocode from another source use that source's own spectrum of the last 2048 samples, one block late | 87 |
| A426 | The Serum `flex` curve of a spectral block is read like the other curves of the files (x 0 .. 1, y flipped: up is 0); y 0.5 is a flat curve | 86 |

### 4.39 GROUPC WP-13 - Multisample oscillator (B30: 30 keys `oscN.multi.*`, positions 2269..2298, destinations 984 .. 986, `oscN.type` 4) - area 02

**Type and capability.** `oscN.type` 4 `Multisample`; `kCap2Multisample` (type 4 and the `oscN.multi.*` keys); proven by `core/engine-vital/tests/test_multisample_osc.cpp` (6 cases: capability, zones at the key pitch, velocity layers, envelope override and release, velocity tracking, timbre and its modulation, round robin, key switch, choke group, loops, last zone read-out, unison, determinism / extremes, state round trip incl. the content).

**Content.** `content.instruments[]` (new, additive): `{osc 1..3, pack, file, sha256, name, zones}`; `ref` is the SFZ file, the samples are the files of the same pack below the folder of the SFZ (`UserContentStore::addInstrument`: `<root>/instruments/<sha12>/<name>.sfz` + samples; 24-bit WAV; paths without `..`). `StateContent::instruments`, `kMaxContentInstruments` 3, `LiveValues::multisampleZone / multisampleKey` (the zone and key of the last note, for the display and Switch to Single Sample).

**Keys per oscillator (10; modulatable: timbre):** `override` (Off), `delay` 0 .. 2000 ms (0), `attack`, `hold` 0 .. 10000 ms (0), `decay` 0 .. 10000 ms (500), `sustain` 0 .. 100 % (100), `release` 0 .. 10000 ms (100), `vel_track` 0 .. 100 % (100), `vel_override` (Off), `timbre` -100 .. 100 % (0). RAND is `oscN.random_phase`; pan, level, unison and warp are the oscillator's own keys.

**Engine** (`core/multisample`, namespace `synthyboy::multisample`). `parseSfz` reads `<control>` (`default_path`), `<global>`, `<master>`, `<group>`, `<region>`, comments, `#define`, note names and the opcodes sample, key, lokey / hikey, pitch_keycenter, lovel / hivel, tune, transpose, volume, pan, offset, end, loop_mode (no_loop, one_shot, loop_continuous, loop_sustain), loop_start / loop_end / loop_crossfade, ampeg_delay / attack / hold / decay / sustain / release, amp_veltrack, amp_velcurve_N, amp_keytrack, group / off_by / off_mode, seq_length / seq_position (round robin), lorand / hirand, sw_default / sw_last / sw_lokey / sw_hikey; unknown opcodes are ignored (`writeSfz` writes the same set back). `Instrument::build` (off thread) decodes the samples (a missing or undecodable file leaves its zone silent), `selectZones` (real time) picks up to two zones for key, velocity, random number, round robin counter and key switch; a `LayerVoice` per zone and unison voice plays the zone with `sampleosc::SampleVoice` (pitch = note - key centre + tune + transpose + unison detune) under a delay / attack / hold / decay / sustain / release envelope: the SFZ's `ampeg_*` or, with OVERRIDE, the keys. The note-on handler (`SampleOscillator::startKind`, kind 4) chooses the zones with the key looked up through TIMBRE, `OscillatorModule::kVelocity` brings the velocity of the voice. A one shot zone ignores the note off; loop_sustain leaves its loop at the release; a choke group (`off_by`) releases the older layers of the group (fast 10 ms or normal, `off_mode`).

**Importer.** `kOsc_MultiSample` is type 4 and the instrument is kept: the embedded SFZ (or the file of `sfzPathRelative`) is parsed, every sample it names is read from the Serum content, decoded (FLAC) and stored as a 24-bit WAV below the instrument folder (`ImportedInstrument`, `SerumImportResult::instruments`), the SFZ is rewritten with the stored paths (`writeSfz`), the block keys `kParamEnvOverride / Delay / Attack / Hold / Decay / Sustain / Release`, `kParamVelTrack / VelTrackOverride`, `kParamTimbreShift`, `kParamRandomPhase` become the keys. Without `kCap2Multisample` the reduced path of WP-21 (one zone, Sample type) stays. Audit: 247 multisample oscillators, 10,287 regions, 0 samples missing, 0 errors; the 656 presets import without a failure (instruments 4.6 GB as 24-bit WAV in the audit directory).

**UI.** `OscWaveView::paintMultisample`: the **zone map** (keys across, velocity up; the zone of the last note lights), the envelope thumbnail (a tap switches OVERRIDE, the shape of the keys is drawn when it is on), **Load SFZ** (button; `ContentIO::loadInstrumentFile`: any sample format the host decodes, stored as WAV) and the menu entries **Switch to Single Sample** (`switchToSingleSample`: the sample of the last note's zone becomes `content.samples[osc]` with root, tune, start / end and loop of the zone, the type becomes Sample) and Load SFZ... `OscPanel` dims the envelope keys until OVERRIDE and the velocity tracking until its override. Kick page `oscN_multi_1` (3 pages, 155 in total).

| ID | Assumption (default) | Question |
|----|----------------------|---------:|
| A427 | The SFZ subset above; a double slash inside a value ("Samples//file.flac", written by libSFZ) is part of the path, a comment starts at the start of a line or after a blank | 90 |
| A428 | The instrument is stored in the user library with its samples as 24-bit WAV (the engine decodes WAV only); at most 24 million decoded frames per instrument (about 100 MB with the mip levels), later zones are left silent and counted (`numSkipped`) | 91 |
| A429 | All zones that match a note play, at most two (velocity layers that overlap); the round robin counter, the key switch and the choke groups are the state of the instrument shared by the voices | 90 |
| A430 | The envelope is delay / attack / hold / decay (linear segments) / sustain / release; without any `ampeg_*` and without OVERRIDE a zone plays at once and releases in 3 ms; the release is at least 2 ms | 89 |
| A431 | Velocity: gain = 1 - t + t x curve (velocity), t = the SFZ's `amp_veltrack` or, with `vel_override`, the key `vel_track`; the curve is `amp_velcurve_N` (linear between the points) or velocity / 127 | 89 |
| A432 | TIMBRE: the zone is looked up with the key k + t (127 - 2 k), t = timbre / 100 (1: the mapping is inverted - higher notes play zones mapped lower; -1: stretched, 3 k - 127); the pitch is still the played note | 89 |
| A433 | Serum block units: the envelope times are seconds, `kParamEnvSustain` a fraction (x 100 = %), `kParamTimbreShift` -1 .. 1 (x 100 = %), `kParamVelTrack` percent | 89 |
| A434 | Switch to Single Sample takes the zone of the last note (read-out) and copies its sample into the library as a sample; the oscillator tune adds the zone's tune (cents, +- 100) | 91 |

## 5. State that is not in the key list (additive `content` section, state version 3)

Required by Q8, Q17, Q23, Q6, Q26. These are not `ParamInfo` keys (not automatable, not modulatable) and are written to both the `.sbpreset` and the host state document (ASSUMPTION A25: the plugin state IS the preset; otherwise custom shapes would be lost when a DAW project reloads). **[WP0 fix]** The section sits INSIDE the state object (`{"format":"synthyboy-state","version":3,"params":{..},"content":{..}}`), so the host blob and the `.sbpreset` (whose `state` member is that object) share one writer and one parser; 11 C3 / `serializeImportedPreset` must write it there, not as a sibling of `state`. `loadState ()` stays allocation-free and skips the section (`LoadResult::content = ignored`); `loadStateWithContent ()` / `loadPreset (.., StateContent&)` parse it into `StateContent` (`content_state.h`, allocates, not for the audio thread). A value outside the caps or ranges drops the whole section (`LoadResult::content = rejected`, parameters still apply); only malformed JSON invalidates the document. Maximum nesting is 7 inside the section (`kMaxDepth` = 8 holds). Extra rules fixed by WP0: strings <= 255 bytes without control characters, pack / file are relative paths (no `..`, no `\`, no `:`, not absolute), sha256 empty or 64 lower case hex digits, LFO shape y in 0..1, point `power` in -20..20, point modulation `amount` in -1..1 and `source` a key of §2, tuning text may carry `\n \r \t` only, every list ascending in x.

| Section | Entries (fields) | Size bound | Referenced by |
|---------|------------------|-----------:|---------------|
| `content.wavetables[]` | `{osc, pack, file, sha256, name, author, frames}` (11 §1.3.1 step 5); never the table data (Q8) | 3 entries | `oscN.wavetable = Preset table` |
| `content.samples[]` | `{osc 1..3, pack, file, sha256, name, sample_rate, frames, channels, slices:[0..1,...]}` (**ASSUMPTION A26**: samples are referenced like tables, never embedded: audio would break the 1 MiB limit; "embed in preset" G04 is not decided) | 3 entries, <= 128 slices | `oscN.type = Sample` |
| `content.lfo_shapes[]` | `{lfo 1..10, name, smooth, loop_point?, points:[{x, y, power, mod_x?:{source, amount}, mod_y?:{source, amount}}]}`; `source` is a mod-source key of §2 / the table; `mod_*` is the point modulation of G65 | <= 128 points per shape (Vital line generator: 100) -> ~4 KB per shape, 40 KB worst case | `lfoN.shape = Custom` |
| `content.curves` | `{velocity:{points}, note:{points}}`, points `{x, y, power}`, default identity `[(0,0),(1,1)]` | <= 64 points each | sources `velocity`, `note` |
| `content.mod_curves[]` | `{slot: 1..64, points:[{x, y, power}]}` | <= 64 entries, <= 100 points per curve | mod connection curve (`line_mapping` in Vital / Serum per-slot curve) |
| `content.tuning` | `{scl: text, kbm: text or null}` (a `.tun` is converted to the same pair at load, **ASSUMPTION A27**) | scl <= 64 KiB, kbm <= 4 KiB | `master.tuning.scale = User file` |
| `content.impulses[]` | `{pack, file, sha256, name}` | 1 entry | `convolve.ir = Preset IR` |
| `content.remaps[]` | `{osc 1..3, slot 1..4, name, points:[{x, y, power}]}` (B16, A120): the drawn wavetable position remap of an oscillator slot; no point modulation | <= 12 entries, <= 64 points each | `oscN.remap.slot` |
| `content.spectral_filters[]` | `{osc 1..3, name, points:[{x, y, power}]}` (GROUPC WP-12, A420): the drawn gain mask of the spectral filter of an oscillator; x = position on the log frequency axis 0..1, y = 0..1 with 0.5 = unity | <= 3 entries, <= 64 points each | `oscN.type = Spectral` |
| `content.instruments[]` | `{osc 1..3, pack, file, sha256, name, zones}` (GROUPC WP-13, A428): the SFZ file of a multisample instrument; the samples are files of the same pack next to it | 3 entries | `oscN.type = Multisample` |
| `content.filter_curves[]` | `{filter: "filter1"/"filter2"/"fxfilter", points:[{x: log frequency 0..1, y: dB}]}` (**ASSUMPTION A28**: the drawable PZ SVF stores a magnitude curve; the DSP fits pole / zero pairs) | <= 16 points | `filterN.model = PZ SVF` |

Missing content at load: the existing fallback and `missing_content` flag (ARCHITECTURE 5.2): `Preset table` -> built-in Saw, `Sample` -> silent oscillator, `Custom` LFO -> `Sine`, `User file` tuning -> 12-TET, `Preset IR` -> bypass, `PZ SVF` -> flat.

**B17 addition (4.17):** `content.curves` may also hold `shaper_a` and `shaper_b`, each `{"points":[{"x","y","power"}, ...]}` with the rules of the velocity curve (2 to 64 points, ascending x in 0..1, y in 0..1, |power| <= 20; absent or empty = the identity). They are the transfer curves of `distortion.type` X-Shaper and X-Shaper (Asym). The section stays part of state version 3 (additive, no version bump); an older reader ignores the two members.

## 6. Contract changes without keys

| Change | Where | Needed by |
|--------|-------|-----------|
| `Event::Type::sustainPedal { bool down }` | `events.h`, shells translate CC64 | Q22 |
| `Event::Type::noteExpression { noteId, channel, key, kind (tuning, pressure, brightness), value }` | `events.h`, `shell-clap` (`CLAP_EVENT_NOTE_EXPRESSION`), `shell-juce` (MPE zones) | Q24, `slide`, `poly_aftertouch`, per-note bend |
| Note-off velocity carried on the note-off event | `events.h` | `lift` |
| `IEngine`: load a user wavetable (C1), a sample, a tuning (scl / kbm), an impulse, an LFO shape / curve point list, an MTS-ESP client attach / status | `engine.h` | §5 |
| `EngineCaps` bits for every new capability (the UI and the importer fall back like 11 does when a bit is false) | `engine.h` | R5 |

## 7. Key-count summary

| Block | Content | Keys | Positions | Mod | Dest. choices |
|-------|---------|-----:|-----------|----:|---------------|
| B01 | macro.5..8 | 4 | 880..883 | 4 | 270-273 |
| B02 | lfo9, lfo10 | 26 | 884..909 | 16 | 274-289 |
| B03 | lfoN.mono / direction / host (N=1..10) | 30 | 910..939 | 0 | - |
| B04 | env units, legato inverted, beat times (x6) | 42 | 940..981 | 30 | 290-319 |
| B05 | curve legato | 2 | 982..983 | 0 | - |
| B06 | modNN.aux_* (x64) | 256 | 984..1239 | 0 | - |
| B07 | scale, tuning, MPE | 9 | 1240..1248 | 0 | - |
| B08 | osc mapping (x3) | 18 | 1249..1266 | 0 | - |
| B09 | osc type, sample, warp 2 (x3) | 93 | 1267..1359 | 18 | 320-337 |
| B10 | mixer, buses, direct | 26 | 1360..1385 | 19 | 338-356 |
| B11 | filter var / pan / level (x3) | 9 | 1386..1394 | 9 | 357-365 |
| B12 | fx order, racks, new FX, reverb force | 89 | 1395..1483 | 38 | 366-403 |
| B16 | wavetable remap x3, FX levels x8, Clean Mode x3 (WTREMAP) | 23 | 1484..1506 | 8 | 404-411 |
| B14 | pitch modes, CRS, snap, unison, phase legato (PITCHUNI) | 37 | 1507..1543 | 12 | 412-423 |
| B17 | compressor limit / crossovers, delay hq / offset, distortion key track, phaser depth 2, reverb type / room / width / ER / diffusion | 14 | 1544..1557 | 8 | 424-431 |
| B18 | sub osc, noise colours & extras (OSCEXTRA) | 18 | 1558..1575 | 10 | 432-441 |
| B15 | voice steal, poly legato, bend down, stereo spread, lfo swing (x10), quality 4x / no smoothing (VOICEGLB) | 18 | 1576..1593 | 11 | 442-452 |
| B20 | stereo routing, voice override (MODAMT); plus 64 mod amounts, portamento time and polyphony as appended destinations | 2 | 1594..1595 | 1 (+66 appended) | 453 (+454-519) |
| B21 | ENV 1 buttons of the Main / Direct channels: sub, osc 1-3, noise, filter 1-2 (WARPENV); plus 41 appended entries of the warp lists | 7 | 1596..1602 | 0 | - |
| **Total (B01-B21)** | | **723** | 880..1602 | **184** | 270..519 |
| B22 | the pool of 16 effect instances (FXINST) | 448 | 1603..2050 | 384 | 520-903 |
| B23 | sub phase, noise stereo / filter / type / bounce (GROUPA1 WP-04) | 6 | 2051..2056 | 3 | 904-906 |
| B24 | MPE: XYZ / YZ to macros, Y to mod wheel, bend also to X, Y bi-directional (GROUPA1 WP-09) | 4 | 2057..2060 | 0 | - |
| B25 | delay offset left, reverb feedback (GROUPA1 WP-10) | 2 | 2061..2062 | 2 | 907-908 |
| B26 | voice control: length, 8 x 6 step values, random, scaling, mask; sources Voice Index / Voice Mod 1 / 2 (GROUPA2 WP-07) | 62 | 2063..2124 | 0 | - |

Resulting table of B01-B21: 880 + 723 = **1603 keys**, 269 + 184 = **453 modulatable** keys, destination choice size **520** (453 modulatable keys + 66 appended destinations + none), mod sources **39**, positions 0..1602. The sustain pedal adds 0 keys. Choice-list appends (§3): 9 lists, 0 renumbered. **After B25: 2063 keys, 842 modulatable keys, destination choice size 909, positions 0..2062, `kMaxParams` 4096.**

## 8. Decided items deliberately NOT specified as keys, and undecided GAPs left out

Left out because no decision names them (not silently dropped; the owner can add them as another block): per-slot remap curve and RISE / FALL of the mod matrix slots (G69 matrix part; the wavetable position part is B16), warp modes G34-G37, ENV 1 toggle on channels (G39), per-voice control (G17, Q25 proposed), Multisample / Granular / Spectral (G19, G21, G22), dynamic effect instancing (G71), compressor / delay / distortion / reverb / phaser extras (G73-G77), Save as Default (G02), lock (G08), MIDI learn (G07: shell table, not a key). Q3, Q4 (A/B snapshots), Q7, Q31, Q32, Q33, Q14 need no key. G02, G07 and G08 are built key-free, see 15-shell-features.md. Voice steal / same-note G12, poly legato G13, separate bend up / down G14, stereo spread G15, LFO swing G16, 4x / no smoothing G18 are specified in block B15 (4.20).

## 9. Kick pages (09 §5 has 61 pages)

New pages: 42 -> **103 pages** (B16 of WTREMAP adds 4 more: 107, B14 adds 3 more: 110, B17 adds 5: 115, B18 adds 1: 116, B15 adds 2: 118, B21 adds 1: `mix_env1` (119); B20 adds 0: its two keys sit on the page `master_expression`) (one `clap.remote-controls` page = at most 8 keys; keys on a `more:` sheet add no page).

| Page id(s) | Section | Keys (<= 8) | New pages |
|------------|---------|-------------|----------:|
| `macros_2` | Perf | `macro.5` .. `macro.8` (+4 free knobs for the category wiring of Q29) | 1 |
| `lfo9`, `lfo10` | LFO | as `lfoN` | 2 |
| `master_scale_tuning` | Master | `master.scale.key/type/mode`, `master.tuning.a4/scale/mts_esp/mts_note_on_only` (7 + 1 free) | 1 |
| `master_expression` | Master | `master.mpe.on`, `master.mpe.bend_range`, `curve.velocity.legato`, `curve.note.legato` (+4 free) | 1 |
| `osc1_type_map`, `osc2_type_map`, `osc3_type_map` | Osc | `oscN.type`, six `oscN.map.*` (+1 free) | 3 |
| `oscN_sample_1`, `oscN_sample_2` (N = 1..3) | Osc | §4.9; the 10 keys marked `more:` sit on the More sheet of `oscN_sample_2` | 6 |
| `oscN_warp2` | Osc | `oscN.warp2.*` (+4 free) | 3 |
| `sub_main` | Osc | `sub.on/waveform/octave/tune/level/pan/destination/mono` (more: `filter_blend`, `bus1_send`, `bus2_send`) | 1 |
| `mix_routing`, `mix_bus`, `mix_sends_osc`, `mix_sends_filter` | Osc | §4.10 | 4 |
| `filter1_extra`, `filter2_extra`, `fx_filter_extra` | Filter / FX | `filterN.var/pan/level` | 3 |
| `fx_order_2` | FX | `fx.order.10` .. `fx.order.16` | 1 |
| `fx_rack_1`, `fx_rack_2` | FX | the 16 `<fx>.rack` | 2 |
| `fx_hyperdim_1/2`, `fx_utility_1/2`, `fx_bode_1/2`, `fx_convolve_1/2`, `fx_splitter_lh`, `fx_splitter_lmh`, `fx_splitter_ms` | FX | §4.12 | 11 |
| `mod_slot_1_aux` .. `mod_slot_4_aux` | Matrix | `modNN.aux_*` (4 keys) | 4 |
| `osc1_remap`, `osc2_remap`, `osc3_remap`, `fx_levels` (B16, WTREMAP; the Clean Mode keys join `filter1_extra`, `filter2_extra`, `fx_filter_extra`) | Osc / FX | `oscN.remap.on/slot/rise/fall`; the eight `<fx>.level` | 4 |
| More sheets (no page) | - | `lfoN` (3 keys x10), `envN` (7 keys x6), `fx_reverb_1` (`reverb.force_enable`), Matrix view (`mod05`..`mod64` aux, 240 keys), `noise` extras (7 keys), `sub_main` sends (3 keys) | 0 |
| `sub_main` (B18, OSCEXTRA) | Osc | `sub.on/waveform/octave/tune/level/pan/destination/mono` (+ 3 sends on the more sheet) | 1 |
| **Total** | | | **43** |

Because `kick-pages.json` is generated by `synthyboy_paramdump` (Q28, proposed), the page table above is data, not code. The existing duplicate knobs (09 §4) are unchanged.

## 10. Totals, id ranges, state migration, risks

**Totals.** 604 new keys, 1484 total, 0 renumbered, 0 renamed; ids = `hashKey(key)`, table positions 880..1483 by block (§7).

**Collision check (reproducible).** The ids were verified with a script over the 880 existing ids and the 604 new keys (no key or id collision). To re-run after the table is written, generate `docs/params.json` with `synthyboy_paramdump` and check:

```
python3 -I -c "import json,collections as c;p=json.load(open('docs/params.json'))['params'];\
print(len(p),[k for k,v in c.Counter(x['id'] for x in p).items() if v>1],[k for k,v in c.Counter(x['key'] for x in p).items() if v>1])"
```

(`core/tests/test_params.cpp:109` already asserts unique ids, keys and display names; add `kCount == 1484` and the position of the first new key `macro.5` == 880 as a golden so the append rule R2 cannot regress.)

**State version bump: yes, 2 -> 3** (`kStateVersion`, `params.json` `stateVersion`). Rationale and migration (`kMigrations` gets `{ 2, migrateV2ToV3 }`):

| # | Point |
|---|-------|
| M1 | No rescale or rename is needed (R5, append-only); a version 2 document loads with every new key at its neutral default. `migrateV2ToV3` writes the 7 `fx.order.10..16` explicitly (9..15) so the 16-permutation is complete; it touches nothing else. |
| M2 | Why bump anyway (Q18): version 3 adds the `content` section (§5) and the 16-module chain semantics. An older reader sees `version > kStateVersion` -> `LoadStatus::newer_version` and applies only the known parameters (choice values beyond its range are clamped, for example `filterN.model` 12 -> 7; this loss is expected and documented). |
| M3 | The loader must keep: unknown keys ignored, missing keys keep the inOut value, stepped clamping to the widened ranges (the ranges of the old stepped keys only grow). |
| M4 | `loadPreset` / `savePreset`: the `content` section travels inside the state object (see §5, **[WP0 fix]**); the `import` section of 11 C3 is not part of WP0; `kMaxPresetFileBytes` check includes the content. |
| M5 | Save deterministic order = table order, so the new keys are written after `reverb.chorus_rate`. A v3 file therefore differs from a v2 file only by appended entries plus `content`. |
| M6 | `hashKey` ids make the CLAP / VST3 parameter ids of the new keys stable; the host sees 604 additional parameters at the end of the list (existing host automation lanes are unaffected). |
| M7 | Update the golden state files and the `params.json` / `PARAMETERS.md` generation (11 C5 said "state version stays 2" - superseded here by Q18). |

**Risks.**

| Risk | Detail | Mitigation |
|------|--------|-----------|
| `kMaxParams` = 1024 | 1484 > 1024: `static_assert(kCount <= kMaxParams)` fails; arrays in `state.h` (`ParamValues`), `state.cpp` (`Parsed`), `modulation.cpp`, `params.cpp` (`uint16_t index`), `vital_engine.cpp`, `own_engine.*`, `PluginProcessor.h` are sized by it | Raise to **2048** (headroom 564 for the undecided GAPs of §8). It is a compile-time constant and not part of any file format, so a later raise is free. Cost: `ParamValues` 8 KB (undo history, A/B snapshots), `Parsed` ~18 KB on the loader stack. `ParamInfo::order` is `uint16_t` (fine to 65535) |
| `kNumModSources` / `kNumModDestinations` | 28 -> 38 and 270 -> 404; both are constants checked against the table | Update with `static_assert` against the generated counts |
| `ModSlotParams` | has 7 indices; aux needs 4 more (key lookup by hash, not by position, because the aux block is appended) | extend struct + `modSlotParams()` |
| 1 MiB limit | State text: 1484 keys, **[WP0 fix]** 29 KB for the default patch (about 45 KB with every value a full-precision float). Content: the largest legal section measured by `the_largest_legal_state_stays_far_below_the_limit` (10 shapes x 128 points with both point modulations, 255-byte names, 3 sample references with 128 slices, 64 KiB scl, 4 KiB kbm) is 402 KB in total, not < 200 KB. Samples and tables are references only, so the limit holds; embedding audio would not (G04 undecided) | bounded parser limits per section (§5) |
| Engine capacity | `engine-vital` upstream fixes `kNumMacros` = 4, `kNumLfos` = 8, 3 oscillators with one warp + one morph, one `sample`, 64 connections, a monolithic 9-effect chain ordered by one float code; macro 5-8, LFO 9-10, aux source, warp 2, sample oscillator, racks and 7 new effects exceed it. Changes to the imported GPL code must be listed in `docs/UPSTREAM.md` | do the extensions in `adapter/` (own modules around the Vital core) where possible; edit upstream only where unavoidable and document it |
| FX chain split | Putting own effects (Hyper, Utility, Bode, Convolve, splitters) between Vital effects needs the Vital chain run per effect or in segments | WP5 decides; keep the permutation contract of §4.12 |
| CPU / tiers | 16 effects, 3 racks, 37 more filter types, 3 sample oscillators with unison, up to 10 LFOs. `quality.h` `fxMask` has to grow bits for the new modules; `Tier::low` needs a mask decision (default: new heavy modules Convolve, Bode, Hyper off on Low) | tier table in WP9; `force_enable` only for reverb (Q27) |
| Kick page growth | 61 -> 103 pages; the MPC page cycler (Q30 proposed) should skip disabled modules or the cycle becomes unusable | skip rule; More sheets keep 103 keys off the pages and the Matrix view holds the 240 aux keys of slots 5-64 |
| Host parameter list | 1484 parameters in VST3 / CLAP lists: some hosts' parameter UIs become slow; the 880 already exceed typical plugins | keep the group strings (hosts show them as folders), document the count in the release notes |
| Hash ids | 604 new ids collision-free today; every later key must be re-checked | the uniqueness test in `test_params.cpp` plus the snippet above |
| Unspecified DSP semantics | all `ASSUMPTION` rows below; the largest are A6 (aux maths), A16 (mixer graph), A19 (racks), A13 (sample scan / loop), A22 / A23 (Bode, Convolve ranges), A24 (splitter crossover) | owner / DSP review before WP start |
| Names unique | `ParamInfo::name` must be unique and choice names unique per key (tests) | the names above were chosen with a family prefix |

## 11. Ordered implementation plan (engine and DSP work packages)

Keys land in ONE step (WP0) so tables, state, `params.json` and the UI/Kick pages are final once; DSP packages then make each key audible, and `EngineCaps` bits tell the UI which are live (R5: a key never changes the old sound).

| WP | Content | Depends on |
|----|---------|-----------|
| WP0 | **Table and state infrastructure**: `kMaxParams` 2048; constants (`kNumModSources` 38, `kNumModDestinations` 404, `kNumFxSlots` 16, `kNumWavetableChoices` 14, `kLfoShapeCustom` 12); all 604 keys appended in the block order B01..B12 with the choice appends of §3; `ModSlotParams` aux indices; `kStateVersion` 3 + `migrateV2ToV3`; `content` section parser/writer (§5) and `LoadResult`; regenerate `docs/params.json`, `PARAMETERS.md` (`kick-pages.json` is generated on the Kick pages branch, not in WP0); tests (counts, uniqueness, golden v2 -> v3 load equals v2 audio hash, append-position golden) | none |
| WP1 | **Event contract**: `sustainPedal`, `noteExpression`, note-off velocity; `shell-clap` / `shell-juce` translation; engine-vital sustain wiring | WP0 |
| WP2 | **Modulation core**: macros 5-8, LFO 9-10 (engine capacity), new sources (`lift`, `slide`, `oct_note`, `poly_aftertouch`), aux-source wiring (second multiplier in each connection, `aux_output`), `mono` / `direction` / `host`, Rossler style, env beat units + `legato_inverted`, velocity / note curves + legato; custom LFO shapes with point modulation (`content.lfo_shapes`) | WP0, WP1 |
| WP3 | **Voice / global**: Key/Scale note filter, tuning (`sbvital::Tuning` exposure, `.scl/.kbm/.tun` parsers, A4, MTS-ESP client, licence check in `THIRD_PARTY.md`), MPE (per-note bend range, pressure, slide), oscillator mapping (voice gating, fold, warp flag) | WP0, WP1 |
| WP4 | **Content store + sample oscillator + warp 2**: `ContentStore` samples (C4), user wavetable `Preset table` (C1), sample playback (loop modes, slices, scan, unison start / span, fades, normalize), the second warp slot, the decision on warp-on-sample (§4.9) | WP0, C3 / C4 content tools |
| WP5 | **Mixer graph and racks**: `filter_blend`, `None`, sends, `filterN.destination`, buses, DIRECT, `<fx>.rack`, the 16-slot chain with per-rack ordering, per-effect segment processing of the Vital effects, `fx.order` repair for 16 | WP0 |
| WP6 | **Filters**: `var` / `pan` / `level`, then the 14 models / 37 types in A/B rounds (one model family per round, names frozen after listening, 04 §7.2), PZ SVF curve editor data | WP0 |
| WP7 | **New FX DSP**: Utility, Hyper / Dimension, Bode, Convolve (needs WP4 content for IRs), splitters (needs WP5 racks) | WP5 (and WP4 for Convolve) |
| WP8 | **Reverb force-enable** (`quality.h` mask override) | WP0 |
| WP9 | **UI, Kick and import**: page generation, `EngineCaps` dimming, update 11 (Vital sample import to `oscN.type = Sample`, `settings.tuning`, mapping tables `vital_bindings`), tier table, docs | all above |

Dependencies in one line: WP0 -> {WP1, WP4, WP5, WP6, WP8}; WP1 -> {WP2, WP3}; WP5 -> WP7; WP4 -> WP7 (Convolve); WP9 last.

## 12. Assumptions (all marked ASSUMPTION above)

| ID | Assumption |
|----|-----------|
| A1 | Lift, Slide, Oct Note, Poly Aftertouch are all included (Q19 proposed, Q24 decided) |
| A2 | Other Serum sources (G67 rest) stay GAP |
| A3 | Engine cap of 8 LFOs is lifted for LFO 9-10 |
| A4 | `lfoN.host` in Free (Hz) mode anchors to seconds since transport start |
| A5 | Envelope beat times are continuous floats, UI snaps to divisions |
| A6 | Aux maths: `src * amount * aux * output`, bipolar aux mapped to 0..1, `aux_output` 0..200 % |
| A7 | Scale conform tie goes to the lower note |
| A8 | The scale list (20 entries) is ours; Serum's list is not enumerated in the manual |
| A9 | `master.tuning.mts_note_on_only` included |
| A10 | MPE Y-bipolar and Pitch-Bend-to-X are not keys; per-note bend goes straight to pitch |
| A11 | Osc mapping (Q2) included although not in the delegation list |
| A12 | Vital custom sample imports into the first `on = 0` oscillator as type `Sample`; `noise.*` stays |
| A13 | Sample scan / loop / unison numbers (speed law, ranges, 120 BPM lock reference, crossfade in ms) |
| A14 | Slice selection as a modulation target deferred |
| A15 | Warp order: warp, warp 2, morph; warp 2 uses the same type list as warp |
| A16 | Mixer graph: send taps, blend law (equal power), Direct skips racks and `master.volume`, bus loop guard |
| A17 | Filter pan = +-12 semitones per channel at +-100 % |
| A18 | Only the 37 missing types are implemented, not the Var functions of types mapped to existing models |
| A19 | One instance per effect type; racks are an assignment key `<fx>.rack`; splitters only in Main / Bus racks |
| A20 | Hyper rate / defaults |
| A21 | Utility LPF / HPF have no switch; extremes bypass |
| A22 | Bode ranges and defaults |
| A23 | Convolve IR content contract (None / Preset IR) and ranges |
| A24 | Splitters use Linkwitz-Riley 24 dB crossovers |
| A25 | `content` is part of the host state as well as the preset |
| A26 | Samples are references, never embedded |
| A27 | `.tun` converted to scl + kbm text at load |
| A28 | PZ SVF stores a drawable magnitude curve, up to 16 points |
| A38 | Warp on a sample voice: Quantize, FM and RM act, the phase domain warps are inert (4.9) |
| A39 | Sample `scan` is varispeed (pitch follows speed); Normalize uses the peak of the whole file; `loop_link` is an editor action without DSP effect; `loop_relative` makes the loop points fractions of the playback region |
| A40 | Auto slices: onset detection of the file (2 ms RMS envelope, rise against the previous 20 ms, minimum distance 20 ms) filtered by `slice_threshold`; in slice mode the key picks the slice (`key - slice_root`) and the pitch is the recorded one |
| A41 | DIRECT passes `master.volume` as in version 2 (R5 wins over the A16 wording "skips master.volume"); it skips every rack and is scaled by `direct.level` |
| A42 | A bypassed splitter band is removed from the sum (the package's meaning) |
| A43 | `PZ SVF` without curve data runs the package's pole / zero SVF (the "flat" fallback of 5 is for a missing reference); with a curve the voice filter is a cascade of bells fitted to it, the cutoff shifts the curve (1 kHz = as drawn) |
| A44 | Convolve re-partitions its impulse on a builder thread when size, minimum phase, attack, decay, damping or gain change (an audible change follows after the build); tier low bypasses Hyper / Dimension, Bode and Convolve |
| A45 | `lfoN.mono` in the envelope and loop trigger types (Envelope, Sustain envelope, Loop at point, Loop and hold) is Serum's MONO: ONE LFO instance serves all voices. Notes are grouped: a note on while no key is held and no sustain pedal is down starts a new group, and only that restarts the LFO (from its start, delay and fade included); further notes of the group join the running instance at its current position and never restart or release it; the instance is "held" (Sustain envelope waits at the phase marker, Loop and hold loops) while any key of the group or the pedal is down, and runs on when the last one is released, also for the voices still in their release. The instance is advanced once per engine chunk with the rate / phase / delay / fade of the first voice processed, every voice reads it with its own rate (key follow) for that chunk. Retrigger and Sync types keep the A33 behaviour (Sync mode on the engine clock). Point modulation of a custom shape (Q17 / G65) reads the source of each voice: the points of the shape move per voice by `amount * source` (y clamped to 0..1, x between the neighbours, the end points keep x), every voice reads its own table; the sources are the outputs of the modulation sources as the matrix sees them (envelopes 0..1, LFOs -1..1, velocity, note, random, lift, slide, oct note, poly aftertouch per voice; macros, mod wheel, pitch wheel, channel pressure globally). A source that depends on the LFO itself (its own output, or an LFO that is modulated by it) would close a loop and is ignored, as for a matrix slot. The values are quantised to 1/4096 before a table is baked; the bake cost per chunk is bounded by the tier (low 4 tables of 512 points, medium 8 and high 16 of 2048; a table that has waited 4 chunks is baked anyway) |
| A120 | Wavetable remap domain: normalised frame position through the baked curve (256 segments), identity when off or the slot is empty |
| A121 | Remap slot / content change applies at the next block (the 7 ms wave buffer fade smooths it); inert on a sample oscillator |
| A122 | RISE / FALL: the slew limiter of the remapped position (one pole glide, separate rise and fall time) per voice, per wave buffer, note on jumps, 0 = off; in Serum RISE / FALL belong to the mod matrix curve (manual p.211), see 4.14 |
| A123 | The unison frame spread is added after the remap |
| A124 | FX LEVEL -24..12 dB on the module output after its dry / wet, modulatable, ramped per block |
| A125 | Clean Mode (manual p.142): the filter input -24 dB, the filter output +24 dB, the drive keeps acting inside the models; Comb / Formant ignore it |
| A126 | Capability bits 26..28 for the three B16 areas |
| A127 | B16 positions 1484..1506, destinations 404..411 on this branch; the lead renumbers at merge |
| A80 | (Names verified against Serum 2 manual p.30 by MANUALCHECK; the mode applies to the CRS slot here, 4.16) `oscN.pitch_mode` (G26): 4 choices: Semitones (0), Harmonics (1), Ratio (2), Step (3). Semitones mode performs standard semitonic pitch transposition. Harmonics mode multiplies pitch by an integer harmonic index (1 + floor(coarse)). Ratio mode multiplies frequency by ratio 1 + coarse (or 1/(1-coarse) when negative). Step mode steps through the scale degrees of the active tuning / MTS-ESP dynamic tuning seam. Default Semitones (0). |
| A81 | `oscN.pitch_coarse` (G25): coarse pitch, key range -48 .. 48, modulatable, default 0. Semitones: semitones. Harmonics: multiplier 1 + floor(coarse) (values below 0 give 1). Ratio: multiplier 1 + coarse (1 / (1 - coarse) when negative). Step: key steps of the active tuning (the tuned note table, so 24-EDO moves 50 cents a step). |
| A82 | `oscN.pitch_track` & `oscN.fixed_pitch` (G27): `pitch_track` toggle (default on / true). When off, a wavetable oscillator runs at the fixed pitch `oscN.fixed_pitch` (default 0 = MIDI note 0 = C-2 as in Serum 2 manual p.35; was 48) and a sample oscillator at its recorded pitch. `oscN.pitch_bend_track` (default on / true) controls whether pitch bend affects a sample oscillator whose pitch tracking is off (a wavetable oscillator always follows the wheel, manual p.36). |
| A83 | `oscN.transpose_snap` (Row 02-10): Transpose snap keyboard quantizing transpose to selected pitch classes. 13-bit value (0..8191, default 0 = Off): bits 0..11 select pitch classes C through B; bit 12 (0x1000) selects Global Snap. Uses Vital's built-in `snapTranspose` engine algorithm. |
| A84 | `oscN.unison.mode` (G32): 5 detune laws: Linear (0), Super (1), Exp (2), Inv (3), Random (4). Linear matches standard Vital detune. Super applies a cubic distribution clustered near the center. Exp applies an exponential distribution. Inv scoops the center voices outward. Random applies a deterministic per-voice pseudo-random detune. Default Linear (0). |
| A85 | `oscN.unison.range` & `oscN.unison.blend_shape` & `oscN.unison.stack_repeat` (G32): `unison.range` sets maximum detune span (0..48 semitones, default 2.0 st, `kMod`). `unison.blend_shape` controls detuned vs center voice level distribution (-100..100%, default 0.0%, `kMod`). `unison.stack_repeat` is the 1-3x of the Serum stacks `12` and `12+7` (Octaves and Power Chord here): the number of octaves above the original pitch the voices are spread over (1x, 2x, 3x, default 1x = 0; MANUALCHECK 4.16). |
| A86 | `oscN.phase_legato`, `oscN.phase_memory`, `voice.phase_legato` (G31): either switch on and another key held when a note starts: the new voice takes the phases of the voice that rendered last (state shared by all voice pairs of the oscillator; it is one block old). `phase_memory`: All Voices (0) every unison phase follows, Contiguous (1) only the centre phase follows (the detuned ones restart), Per Voice (2) the voice keeps its own free running phases. Defaults off / All Voices. |
| A87 | B14 sits behind B16: positions 1507..1543, destinations 412..423, capability bit `kCapPitchUnison` = 1 << 29 (A126 lists 26..28 for B16) |
| A140 | G73-G77 were defined by this file (B17) and were then checked against Serum 2 manual p.162-177 (MANUALCHECK, 4.20); the corrections are A147, A150, A151, A153, A155; the five reverb algorithms stay our own designs |
| A141 | Limit mode: peak limiter, 1.5 ms look-ahead, stereo linked, ceiling = the band's upper threshold, estimated inter-sample peak; the upward stage is kept |
| A142 | `limit_latency_comp` On reports the look-ahead as chain latency and delays the other paths; Off does not; the dry signal of the compressor mix is always aligned |
| A143 | Crossover defaults 120 Hz / 2500 Hz (what the engine does), not the 150 Hz / 3 kHz written in 08 |
| A144 | Modulated crossovers update the filter coefficients once per block |
| A145 | Delay HQ: 8-point Lagrange read |
| A146 | `delay.offset` multiplies the right delay time (all styles), range 0.25 .. 4 |
| A147 | Tap->Delay is the delay mode `delay.style` 4 (manual p.166): a tap on the left time without feedback into the right delay with feedback, mono; the TAP button of the panel is a tap tempo helper only |
| A148 | Distortion types 6-11 as defined in 4.17; asymmetric types have a 10 Hz DC blocker |
| A149 | Distortion filter key track uses the last-key note like the FX filter |
| A150 | X-Shaper: two graphs A and B morphed by DRIVE (0 % = A, 100 % = B; the input is not scaled); X-Shaper is symmetric (magnitude graph), X-Shaper (Asym) reads a signed graph (x -1 .. 1, y -1 .. 1, the centre is silence); stored in `content.curves.shaper_a / shaper_b`, 513-point tables |
| A151 | Phaser DEPTH 2 is the offset between the 12 all-pass stages, spread over `depth2` semitones around the swept cutoff, both channels (manual p.174) |
| A152 | The five reverb algorithms are our own (Dattorro-type plate, Hadamard FDN, Freeverb-type combs, dense bright FDN, room-mode FDN) |
| A153 | `reverb.room` is the MODE of Nitrous (manual p.177) and acts on Nitrous only; Basin is the dense bright network |
| A154 | ER SIZE scales six early reflections per channel (level fixed per type) |
| A155 | DIFF A = input diffusion, DIFF B = diffusion in the loops (Vintage has both, Nitrous' DIFFUSION is DIFF A; Nitrous' and Basin's FEEDBACK is the decay) |
| A156 | `reverb.width` is mid / side width of the wet signal for all types |
| A157 | Five cap bits 30-34 for B17, apart from the WP bits |
| A158 | UI of B17 as listed in 4.17 |
| A60 | The sub waveform list (Sine, Rounded Rect, Triangle, Saw, Square, Pulse) is the one of Serum 2 manual p.125-126 (verified by MANUALCHECK); the pulse width (25 %) is ours |
| A61 | `sub.octave` -4 .. 4 whole octaves (default -1), `sub.tune` = CRS, +-48 semitones (MANUALCHECK; was +-100 cents), `sub.level` default 50 % (the oscillator default), `sub.on` default Off so every existing preset is unchanged |
| A62 | `sub.destination` uses the oscillator list `kOscDestinations` (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) with the mixer graph of 4.10 (`sub.filter_blend`, two bus sends); default `Effects only` as the manual says (p.145: SUB, NOISE, OSC B and C route to MAIN by default; the older keys `oscN.destination` / `noise.destination` keep Filter 1 for every factory preset) |
| A63 | `sub.mono` (an extension, the manual has no mono sub; default Off): when On only the voice with the lowest sounding note carries the sub; the hand-over between voices is ramped over one block (no click) |
| A64 | Noise samples are built into the engine (9 procedural sets: White (the stock Vital noise, bit identical), Pink, Brown, Geiger = the four colours of manual p.130; Tape Hiss, Vinyl Clicks, Digital Hash, Velvet Noise, Atmosphere are ours); no files |
| A65 | `noise.pitch` +-48 semitones, `noise.fine` +-100 cents, `noise.keytrack` (default Off) makes the playback rate follow the key relative to middle C, `noise.phase` is the start position in % of the sample, `noise.random_phase` picks it per note |
| A66 | `noise.loop` (default On); off = one shot, silent after the end of the sample (the stock Vital held the last value) |
| A67 | B18 is placed after B17: positions 1558..1575, destination choices 432..441 (renumbered from the draft "B13" at 404..413) |
| A68 | Capability bits 35 (`kCapSubOsc`) and 36 (`kCapNoiseExtras`); each is set because `test_oscextra.cpp` renders every key off and on |
| A69 | UI: Sub panel (picker row of six >= 44 pt segments, level / pan pad, four knobs, rest behind More) and Noise panel (`<` name `>` row, nine item menu, level / pan / pitch / fine knobs, rest behind More), both on the oscillator page |
| A100 | `master.voice.same_note` Steal (default, ends the sounding voice of the key and starts a new one) / Stack (both sound, a note off releases all) / Ignore (the second note on of a held key is dropped = Serum 2 "Limit Same Note Poly to 1") |
| A101 | `master.voice.steal_priority` = Serum 2 Voice Steal Priority (Newest, Oldest, Highest, Lowest, Velocity: the kind of voice that is kept); Newest = Vital kRoundRobin (the version 2 behaviour), Oldest = kOldest, Highest = kHighest, Lowest = kLowest, Velocity = our kVelocity |
| A102 | Poly legato = Serum 2 paraphonic legato (envelopes do not retrigger on a note played while another is held): the new voice continues from the stage / position / value of the same envelope of a held voice; one bus per envelope and engine, state of the end of the previous block, nothing held = start from zero; the voices keep their own pitch |
| A103 | `master.bend_down_range` 0 .. 24 st (default 2) with `master.bend_down_link` On (default) = the wheel uses `master.bend_range` both ways; Off = a negative wheel uses the down range |
| A104 | `master.stereo_spread` 0 .. 100 % (default 100): mid / side width of the finished mix, 0 = exactly mono, modulatable |
| A105 | `lfoN.swing` 0 .. 100 % (default 0): per LFO amount (Serum 2: global swing amount + division applied via the RATE menu of a synced LFO); fixed grid of four pairs of slots per cycle, the second slot of every pair delayed by swing / 4 of the pair, pair boundaries fixed, modulatable |
| A106 | `quality.oversample_4x` = Serum 2 Ultra (4x) on any tier, applied by prepare () (needsPrepare ()); `quality.no_smoothing` = Serum 2 Disable Smoothing; both default Off |
| A107 | Capability bits 37 .. 42 for the six B15 areas; block placed after B18: positions 1576 .. 1593, destination choices 442 .. 452, `kNumModDestinations` 453 |
| A108 | UI: `GlbVoicePanel` (new), `GlbQualityPanel` (options primary), `LfoPanel` (swing behind More); Kick pages `voice_global`, `quality_options` |
| A109 | Poly legato does not touch LFOs, oscillator phases or the mono `master.legato` machinery; it is envelopes only |
| A110 | `same_note` Ignore tests the key as the adapter sees it (a key whose note off has been seen is free again, also while its release rings) |
| A111 | `lfoN.swing` and `master.stereo_spread` are read once per block (swing from the first voice lane), like `stereo_routing`; fast modulation of them is block-rate |
| A112 | The presets are regenerated only for the 18 new keys at their defaults; no preset sounds different |
| A197 | Modulating another connection's amount (G1): `modNN.amount` is a destination (choices 454 .. 517). The engine uses Vital's own `modulation_N_amount` input; a loop (a slot on its own amount, or a chain A -> amount of B -> ... -> amount of A) is not wired (decided before wiring, no allocation), the amount stays at its base value, the slot is retried when the matrix changes. The result of the modulated amount is clamped to -1 .. 1 (Vital). The UI marks the row (loop badge) and greys the entries of the destination menu that would close a loop |
| A198 | Per-connection curve (G2): `content.mod_curves[]` `{slot 1..64, points}` (<= 100 points, x ascending 0 .. 1, y 0 .. 1, power -20 .. 20), at most one per slot, no key. It is Vital's `line_mapping`: applied to the source value (unipolar, 0 .. 1) before the power curve and the amount; a slot without an entry is the straight line. It replaces the velocity / note curve of a slot whose source is velocity / note |
| A199 | Destination numbering: choices 1 .. 453 are the modulatable keys in table order (B20 adds `master.stereo_routing` = 453), 454 .. 517 `mod01.amount` .. `mod64.amount`, 518 `master.portamento.time`, 519 `master.polyphony`. The appended group sits behind the last key of B20; later blocks continue at 520, so stored presets stay valid. `kNumModDestinations` = 520 |
| A200 | Source `stereo` (G4, choice 39, bipolar): Vital's `stereo` modulation source; its per-channel sign makes a connection move the left and the right channel in opposite directions (test: cutoff driven by it gives different channels). Needs `kCapModAmount` |
| A201 | `master.stereo_routing` 0 .. 100 % (default 100, modulatable): Vital's `stereo_routing`, the stereo decoder at the end of the chain (before the master volume): 100 % = the signal as it is, 0 % = both channels carry the same (centre) signal; in between an equal-power blend. It is not `master.stereo_spread` (A104: mid / side width of the finished mix, per block); both exist, they multiply |
| A202 | `master.voice_override` (Kill / Steal, default Kill = Vital's default): what happens to the voice that has to give way when the polyphony is reached: Kill lets it fade out quickly while the new voice starts, Steal gives the voice to the new note at once. Audible with a polyphony of 2 and three notes |
| A203 | Modulation of `master.portamento.time` (518) and `master.polyphony` (519): the engine's own destinations `portamento_time` / `polyphony`. The engine reads the polyphony at every note: modulated down (test: a macro takes 8 voices to 1) the next notes steal as at a lower polyphony setting (`master.voice.*`, A100). `modNN.amount` is not flagged modulatable in the table (version 2 region), the three groups are listed by `DestinationLists` |
| A204 | `osc_N_transpose_quantize` (G6) maps onto the existing `oscN.transpose_snap` (B14: 0 .. 8191 = the 12 pitch-class bits plus bit 12 global snap, the same bitmask Vital stores); no new key and no new capability bit |
| A205 | Capability bit 43 `kCapModAmount`, set because every item has an off / on render in `test_modamt.cpp` (amount destination, per-slot curve, stereo source, stereo routing, voice override, portamento time, polyphony); a mod source 39 or a destination >= 454 needs it, the keys `master.stereo_routing` / `master.voice_override` too |
| A270 | Warp list order (B21): the 41 new modes are appended in the order of the manual's table (Sync Var, Bend, Asym, Flip, Mirror, Remap, Odd/Even, Filter, Distortion, FM, PD, AM); the modulator modes come as a first-other / second-other pair (X, Y) like the Vital FM / RM entries; the list has 52 entries for both slots and all three oscillators, `kNumWarpTypes`. The modulator sources Noise, Sub, Filter 1 and 2 of the manual (p.54-56) are not built here (backlog WP-06, G29) |
| A271 | Bend / Asym laws (own, the manual gives none): a bend of strength s in 0 .. 1 is the power `1 + 3 s` of the position inside each half (Bend) or the whole cycle (Asym), the minus variants use the reciprocal power; the +/- variants map the knob 0 .. 100 % to s = -1 .. +1 (neutral at 50 %). Vital's own `Bend` (entry 4) stays as it is |
| A272 | Flip: the polarity flips from the cycle position `1 - amount` on; Mirror: the second half plays the first half backwards (position 1 - u) and the amount bends both halves like Asym +/-; Odd/Even: the even / odd harmonics are the half sums / half differences of the wave and itself half a cycle later, the knob scales them (50 % original) |
| A273 | Remap 1-4 read the table of `oscN.remap.slot` (block B16, the curve drawn in the Remap panel): the feature "wavetable position remap" (RISE / FALL) and the warp modes share the curve; Remap 1 mixes curve and diagonal by the amount, 2 does it per half, 3 is a fixed sine remap (position - amount x sin (2 pi position) / 2 pi), 4 repeats the curve four times |
| A274 | Sync Var is a separate list entry whose VAR fader is the **phase knob of the slot** (no new key; 0 % = hard sync, 100 % = a raised cosine fade over half a cycle each side of the reset). The Serum control is a fader that appears with Sync; a key per slot (6 keys) was not worth the table space. Morning question 35 |
| A275 | Evaluation: the phase domain modes of both slots run before the wavetable read, the value domain modes (Odd/Even, Filter, Distortion) after it, so slot 1 = Distortion with slot 2 = Bend runs the Bend first. Slot 2 (like the original slot 2) is not part of the band limit choice of the wavetable read that slot 1 Sync / Formant / Sync Var get |
| A276 | Warp on a sample voice: AM, the 14 distortions and the three FM laws act, everything else of the new modes is inert (a sample has no cycle to bend, filter, split or remap) |
| A277 | A channel whose ENV 1 button is off skips the envelope but keeps the velocity tracking and the voice amplitude (the same factor as the enveloped path, applied to the free output), so the level follows the velocity |
| A278 | A voice with a sounding free channel ends when ENV 1 and every enabled ENV 2 .. 4 are silent (an envelope counts as enabled when the mod matrix uses it); with every ENV 1 button on the voice killer is the amplitude as before (bit identical) |
| A279 | `filter1.env1` and `filter2.env1` are built although the MANUALCHECK proposal named only the sources: the manual shows the button on the filter channels too (p.149); the table has 7 keys instead of 5 |
| A280 | Capability bits 44 .. 49, one per area, each set because `core/engine-vital/tests/test_warpenv.cpp` renders every mode / key off and on: 44 `kCapWarpAlt` (entries 11 .. 24), 45 `kCapWarpFilter` (25, 26), 46 `kCapWarpDist` (27 .. 40), 47 `kCapWarpFm` (41 .. 46), 48 `kCapWarpPdAm` (47 .. 51), 49 `kCapEnv1Toggle` (the seven keys). The five warp bits are needed by a value of a choice list (`requiredCapsForValue`), not by a key |
| A281 | Swap Warps is an action at the end of the grouped list of both warp type controls (not a key): it exchanges type, amount, phase and spread of the two slots, each as a host gesture |
| A282 | While a slot's type is `Sync Var` its phase knob is labelled `Var` |
| A283 | Numbers the manual does not give: filter order 12 dB; distortion drive 0 .. 30 dB over the amount; FM index 8 x amount^2 (Thru-Zero, Linear), exponent 4 x amount^2 (Exp); PD up to 0.9 cycle; the four shapers without a counterpart in `distortion.type` (Sine Shaper, Stomp Box, Tape Sat., Soft Sat.) are our own curves |
| A284 | The second warp slot now starts a note at its amount (it ramped up from zero over the first block, slot 1 has snapped since Vital); the sound of a patch with a warp 2 changes in the first 5 ms of each note, no other sound changes |
| A290 | Parameters per slot P = 24, not 16: the compressor has 24 keys, the reverb 19 and the FX filter 18 (with 16, 8 + 3 + 2 would not fit). Key cost 16 x 28 = 448 |
| A291 | The slot parameter is the NORMALISED value of the source key (its own skew); stepped keys are stored as index / max and snapped by the engine, so a host sees a generic knob. The conversion is the one of `ParamInfo` (`fromNormalised`), so instance 0 and a slot given the same numbers render bit-identically (tested for every key) |
| A292 | The order scale: a module of instance 0 at position q of `fx.order` sorts as q * 64 + 32, a slot by `fxiNN.order` (0 .. 1023); a module of instance 0 before a slot on a tie, slots by number. The edits (core/fx_rack.h) renumber so that no tie is needed |
| A293 | Pool slots live in Main, Bus 1 or Bus 2 only (no band racks), the splitters stay single instance 0 modules (design 7); a module of instance 0 in a band rack is duplicated into Main |
| A294 | Parameters of an instance are applied through the bindings of the source key (same conversion, same limits); `reverb.force_enable` and the tier mask are engine wide: the mask bit of the module's type bypasses a slot as it bypasses instance 0 |
| A295 | The 10 ms crossfade also covers switching a slot on / off and a type change; until the module is built the slot passes the signal dry (realtime), offline (`Quality::realtime` false) the audio thread waits for the builder and takes the module over in the same block. The builder settles the module with 100 silent chunks (as `reset ()` does for instance 0); a free running LFO starts at the phase the settle left, so a slot equals instance 0 only for equal histories (the tests give both the same) |
| A296 | Content (X-Shaper curves, FX filter curve, impulse response) is shared: every distortion / FX filter / convolve instance uses the one the content holds; the EQ analyser and the compressor meters show instance 0 only |
| A297 | Modulation of a slot parameter is wired to the destination of the module behind it: a handover unwires and rewires the matrix slots that name it; a parameter whose source key is not modulatable has no destination (the UI refuses to assign it) |
| A298 | A removed module sends its slot back to the key defaults (type None, parameters 0); modulation slots that named it keep their destination and reconnect when a module of that slot returns |
| A299 | The 43 types are grouped by the DSP family into five new models, not by the manual's categories: the manual's `Multi` category holds the 12 dual SVF types (model 8, full at 12 styles) and the 4 morphing types (model 23 `Multi Morph`). The UI menu shows the manual's categories, the list stays what the engine needs (up to 12 styles per model) |
| A300 | Style counts 12 / 4 / 9 / 12 / 6; `filterN.style` max stays 11; an out of range style clamps to the last style of the model (engine and `serumFilterTypeOf`) |
| A301 | One shared table (model, style) -> `FilterType` in `core/filters-serum`, used by the engine and the UI; no second copy |
| A302 | `kCapFilterModels2` = bit 51 (the first free bit above the fx-instances bit 50); `kCapAllKnown` = 2^52 - 1. An older engine or a host without the bit gets the model value refused by the cap check, never silent audio of another type |
| A303 | The 43 types do not use Blend (the manual lists none; the DSP ignores `blend` for them): the Blend label is the plain "Blend", a menu or picker pick leaves the Blend knob alone (`defaultBlend < 0`). The pick of a model 8 .. 21 entry keeps setting the Blend default of the WP6 catalog |
| A304 | The response of the models 22 .. 26 is the DFT of the impulse response of the real DSP (`FilterMeasure.h`: impulse 0.01 through a private `Filter`, 8192 samples at 48 kHz, the last quarter faded out, 257 log spaced points 20 Hz .. 20 kHz, magnitude and phase; measured again only when model, style, cutoff, resonance, blend or Var change). Drive is 0 in the display. The models 0 .. 21 keep their closed form sketches (question 46) |
| A305 | PZ SVF editor: drag a point; a double tap (double click) on empty space adds a point (at most `kMaxFilterCurvePoints` = 16), a double tap on a point deletes it (at least 2 stay), right click on a point deletes it, Reset = three neutral points (0 dB); y is -36 .. +18 dB, x is 20 Hz .. 20 kHz log; every change is written to `content.filter_curves[filterN]` (so it is saved, undone and loaded with the preset); a double tap does not switch the display mode of a PZ SVF view. Moving a point past its neighbour keeps the points ordered and the dragged point selected |
| A306 | To make the 43 types differ from each other and from the 37 earlier types the character of four was given its own signal path (the manual says "circuit model", the formulas are ours): German LP = resonance in the first 12 dB stage, second stage critically damped; New Wsp = OTA input saturation (3x) in front of the loop; Exp MM / Exp BPF = SEM core with input saturation (1.8x), resonance range R 1 .. 0.3 and some feedback saturation. This changes the sound of `New Wsp` (model 17) against the WP6 state (no factory preset uses it) |
| A307 | Var labels per type are the ones of the manual (p.135-139) and of `getFilterVarName ()` in core/filters-serum; a type with "N/A" shows the Var knob dimmed as "Var (N/A)". Ring Mod x2 only: SPREAD |
| A308 | The menu lists the eight classic Vital models (Analog .. Phaser) under `Classic` after the five Serum categories; a classic model keeps its own name on the type button (the style button shows its variant) |
| A309 | The user content library lives in `<userDir>/content/{wavetables,samples,impulses,embedded}/<sha12>/<name>.wav` with the pack ids `user.wavetables`, `user.samples`, `user.impulses`, `user.embedded` (the backlog named the three directories; the pack id carries the `user.` prefix of `user.imported-vital`); installed packs are searched in `<userDir>/packs/<pack>/<file>` after the user directory |
| A310 | "Embed in Preset" stores the file in the embedded pack (content addressed, hidden from the library list). The state still holds a reference, not the bytes (Q8: tables are not embedded in the document); a preset that is moved to another machine takes the `embedded` directory with it. Writing the bytes into the document needs a schema change and is a morning question |
| A311 | Samples that are not WAV are decoded by the JUCE shell and stored as 24-bit WAV (the engine and the native CLAP shell read WAV only, the core never depends on JUCE); a WAV the engine reads is stored byte for byte so that `smpl` / `inst` chunks survive |
| A312 | The root note priority is the file name, then the WAV chunk (`readWav` already uses this order); a file without either leaves `oscN.sample.root` as it is. The file name keeps the note suffix in the library (`<sha12>/Piano Eb3.wav`) |
| A313 | Trim is destructive for the oscillator, not for the library: the trimmed region is stored as a new file and the old one stays in the library |
| A314 | Snap Beats uses `(END - START) / length_beats` as the beat grid whatever `scan_to_bpm` says; Snap Loop snaps START / END / slices to the loop markers and the loop markers to START / END (tolerance 2 % of the file); Snap Zero moves a marker to the nearest positive going zero crossing of the mono mix within 2048 frames. Snap is a view setting (not saved, not a key) |
| A315 | Show Marker Animation draws the modulation range of UNI START (a band from the base value to base +- |amount| for every assigned slot) with a 20 Hz pulse; the live position of a marker needs the engine to UI modulation read-out (WP-17, 14 row 49). The START / END / loop markers are not modulation destinations |
| A316 | Auto slice markers are computed shell side from the same `SampleData::detectOnsets` rule the engine applies (threshold in percent, `onset.strength >= 1 - threshold / 100`, first slice 0, at most 128); a test proves equality with `SampleData::detectSlices` |
| A317 | The Missing Files dialog opens when the content revision changes (a whole document was loaded) and some reference is unresolved; Remove drops the reference from the content (the sound falls back), Locate adds the chosen file to the library and rewrites the reference; a file with another SHA-256 than the reference is refused unless the caller accepts it (the dialog does not offer that: the preset would sound different) |
| A318 | The Vital import in the plugin: `.vital` replaces the patch (`UiHost::loadPatch`, respecting the module locks), its tables and samples go to `user.imported-vital` (so the references resolve at once), a `.vitalbank` writes every preset to `<userDir>/presets/<bank>/` and loads none (presets are browsed in Kick), `.vitaltable` becomes the table of the oscillator of the open tab, `.vitallfo` the shape of the LFO of the open tab (the slot is switched to Custom). What the engine cannot keep (capability bits) follows the importer's `ImportCaps` of the engine |
| A319 | The import report sheet lists the items of `ImportReport` (imported counts, approximated / dropped / ignored lines with Vital name, our key, values and the stable detail id, at most 200 lines) |
| A320 | FX rack presets are presets of the whole FX chain (all module parameters, powers, racks, `fx.order.N`, the pool slots), not of one bus: a bus is not a unit of the state because the modules of instance 0 have fixed identities. Applying one writes only the keys that differ |
| A321 | The user library file name is the sanitised item name (`sanitizeFileStem`: letters, digits, space, `- _ # ( ) .`); favourites are one name per line in `favourites.txt`; items are limited to 256 KiB and 4096 per category |
| A322 | The CLAP extension `synthyboy.content` is main thread only, WAV only (the native shell links no audio file library; Kick converts other formats), `attach` sets `oscN.sample.root` but not `oscN.type` / `oscN.wavetable` (the host sets parameters itself), and tells the host the state is dirty |
| A323 | The wavetable editor of an oscillator other than osc 1 is opened from the oscillator view (Edit button next to the view mode); the Wavetable panel stays the osc 1 panel of design 03 |
| A324 | The Noise sample load UI (S07-06) is not built: the noise source cannot play a content sample before WP-04 (`noise.type`), and a button without a sound behind it would be a facade; `loadSampleFile` is written for any target and WP-04 only has to call it |
