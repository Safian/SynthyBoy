# New parameter keys for the owner-decided engine extensions – specification

Status: SPEC (2026-11-08); WP0 (table, state, content section, EngineCaps) implemented on the same day, see `WP0-REPORT.md` (corrections found while implementing are marked **[WP0 fix]**); WP1, WP2, WP3 implemented, see `WP123-REPORT.md` (assumptions A29-A37 there); WP4 to WP8 implemented, see `WP48-REPORT.md` (assumptions A38-A45 below; A45 by LFOVOICE-REPORT.md). Source decisions: `12-decisions.md` (Q2, Q6, Q8, Q9, Q13, Q14, Q15, Q16, Q17, Q18, Q19, Q22, Q23, Q24, Q26, Q27), gaps G01-G84 of `10-coverage-matrix.md` §3, design files 01-09 and 11. Where a design file is silent the text says **ASSUMPTION** (collected in §12).

Verified baseline (read from the repo on 2026-11-08):

| Fact | Value | Source |
|------|-------|--------|
| Keys in the table | 880 (table positions 0..879) | `docs/params.json` `count` |
| Modulatable keys / destination choices | 269 / `kNumModDestinations` = 270 (0 = none) | `param_table.cpp`, `params.json` |
| Mod sources | 28 (`kNumModSources`, choice 1..28) | `modulation.h` |
| Highest id value | 4293057762 = `hashKey("mod22.destination")` (table position 475). Ids are hashes, not a counter: see R1 | `params.json` |
| `kMaxParams` | 1024 | `params.h:77` |
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
| `filterN.model` (filter1, filter2, fxfilter) | 8 | 8 `Multi SVF`, 9 `Slope`, 10 `EQ 6 dB`, 11 `Ring Mod`, 12 `Sample Hold`, 13 `Smear`, 14 `French LP`, 15 `Add Bass`, 16 `Scream`, 17 `New Wsp`, 18 `DJ Mixer`, 19 `Diffusor`, 20 `EMS Ladder`, 21 `PZ SVF` (names provisional until the A/B listening of Q13; the index meaning in §4.11 is what is frozen; a display name may change) | 22 |
| `filterN.style` (filter1, filter2, fxfilter) | 6 | 6 `Style G`, 7 `Style H`, 8 `Style I`, 9 `Style J`, 10 `Style K`, 11 `Style L` (generic; the UI shows the per-model label of §4.11) | 12 |
| `fx.order.1` .. `fx.order.9` (+ new `fx.order.10..16`) via `kFxNames` | 9 | 9 `Hyper/Dimension`, 10 `Utility`, 11 `Bode`, 12 `Convolve`, 13 `Splitter L/H`, 14 `Splitter L/M/H`, 15 `Splitter M/S`. `kNumFxSlots` 9 → 16 | 16 |
| `modNN.source` (mod01..mod64) | 28 | 29..38 of §2 (also the list of the new `modNN.aux_source`) | 38 |
| `modNN.destination` | 270 | 270..403 of §2 | 404 |

Lists that stay unchanged: `kLfoSync`, `kDivisions`, `kFxSync`, `kWarpTypes1/2/3` (the new `oscN.warp2.type` uses the same three lists, so a later new warp mode is appended once for both slots), `kOscDestinations` of the existing keys, `kFilterRouting`, `kLfoSyncTypes`, `kMorphTypes`.

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

Group `Osc N / Warp 2`. Order of evaluation: `warp`, then `warp2`, then the morph family (**ASSUMPTION A15**; "Swap Warps" is a UI action exchanging the two value sets). New warp modes of G34-G37 are not decided; when added they are appended to the shared list.

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

Total: 4 + 12 + 2 + 2 + 2 + 2 + 1 + 1 = 26. Groups `Osc N`, `Noise`, `Filter N`, `Mixer / Bus N`, `Mixer`. Semantics (**ASSUMPTION A16**, 02 §1.9 / 08 §1.1 name the controls, not the graph): `filter_blend` is used only when `destination = Filter blend` (equal-power crossfade between Filter 1 and 2: 50 % = -3 dB each; `Filters 1+2` keeps both at unity as in v2). A send taps the source after its level / pan (filters: after `mix`, `pan`, `level`), independent of its `destination`, so a source can feed a filter and a bus at the same time. `filterN.destination` applies to the filter's output unless the filter feeds the other filter (`filter.routing` series). A bus sums its sends, runs through the effects whose `rack` is that bus unless `fx_bypass`, then `level`, then `destination` (`Other bus` = the other bus; if both buses choose `Other bus`, bus 2 is treated as `Main`, no feedback). `Main` = sum of sources with destination `Effects only`, filters to Main and buses to Main, run through the `Main` rack unless `main.fx_bypass`, then `master.volume` (= Serum MAIN level). `Direct` = sources with `Direct out`, buses to Direct, filters to Direct; it skips every rack and `master.volume` and is scaled by `direct.level`, then added at the output. The ENV 1 toggle on Main / Direct channels (G39) and the Sub oscillator (G23) are not decided and not specified.

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
| `oscN.remap.slot` | `Osc N Remap Slot` | choice | 0 `Remap 1`, 1 `Remap 2`, 2 `Remap 3`, 3 `Remap 4` | 0 | - | - | V, X | no | `oscN_remap` |
| `oscN.remap.rise` | `Osc N Remap Rise` | float | 0 .. 2000 | 0 | ms | 0.3 | V, S | no | `oscN_remap` |
| `oscN.remap.fall` | `Osc N Remap Fall` | float | 0 .. 2000 | 0 | ms | 0.3 | V, S | no | `oscN_remap` |

Group `Osc N / Remap`. The curves are not keys: `content.remaps[]` (§5), `{osc, slot, name, points:[{x, y, power}]}`, at most 12 entries (3 oscillators x 4 slots), <= 64 points each, x ascending in 0..1, y in 0..1. A slot without an entry is the identity (the four slots are the "preset" choice of the Kick page; the graph itself is UI + content). Semantics (ASSUMPTIONS A120-A123):

* **A120** Domain: the position fed to the oscillator (the frame knob plus all frame modulation, per voice) is normalised to `u = frame / (frames - 1)` (63 for the built-in tables, the frame count - 1 of a `Preset table`), mapped through the baked curve (256 segments, linear between the baked values, input clamped to 0..1) and scaled back. `Remap On = Off` is bit-identical to the version 2 sound.
* **A121** Slot change and content change take effect at the next block; the oscillator's own 7 ms wave buffer fade prevents clicks. A curve for an oscillator with `type = Sample` is inert.
* **A122** RISE / FALL (the slew of Serum's matrix slot, here on the remapped position): a one pole glide, time constant = the key in ms (63 % of a step), RISE when the target position is above the current one, FALL when below, evaluated every wave buffer (7 ms) per voice; a note on jumps to its position (no glide from the previous note); 0 = no glide. The glide is on the remapped position, so it also smooths a drawn jump of the curve.
* **A123** The unison frame spread (`oscN.frame_spread`) is added after the remap, in frame units: the spread voices are not remapped individually.

The per-slot remap curve and RISE / FALL of the mod matrix slots (`modNN`) remain GAP (G69 matrix part): the task that defined this block names the wavetable position; see `14-morning-questions.md` row 18.

**FX LEVEL (G72).** `<fx>.level` for the eight Vital effects that have none: `chorus`, `compressor`, `delay`, `distortion`, `eq`, `flanger`, `phaser`, `reverb` (the FX filter has `fxfilter.level` from B11; Hyper / Dimension, Utility, Bode, Convolve and the splitters have their own):

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `<fx>.level` (8) | `<Module> Level` | float | -24 .. 12 | 0 | dB | lin | G, S | y | `fx_levels` |

Group `FX / <Module>`. **A124**: the trim is applied to the output of the module after its own dry / wet, ramped over the block, 0 dB leaves the signal untouched (bit-identical), the range matches the Serum modules (no "silence at the bottom" as the mixer faders); a module that is off has no level. Modulation destinations 404..411 in table order.

### 4.15 B16 – Clean Mode (G54) – positions 1504..1506 [3] – area 04 / 08

| Key | Name | Type | Range | Default | Unit | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|----|-----|------|
| `filter1.clean_mode`, `filter2.clean_mode`, `fxfilter.clean_mode` (3) | `Filter N Clean Mode` | bool | Off / On | Off | - | V (fxfilter: G), X | no | `filterN_extra` / `fx_filter_extra` |

**A125** (the manual text of p.142 is not in the repository; 04 §1.1 gives "Drive Clean Mode (-24 dB pre, +24 dB post)", the lead's task called it a quality toggle, which it is not in the design): Off = version 2 (the drive pushes the signal into the filter, with the model's own level compensation). On = the filter runs without drive and `filterN.drive` (0 .. 20 dB, still modulatable) becomes a plain linear gain on the wet signal after the filter and before the mix, so it can neither saturate nor be compensated away. The models Comb and Formant have no drive stage and ignore the key. Proven by `test_wtremap.cpp` (on / off differ by a relative RMS error of 2.07 at 12 dB drive; on = exactly the drive-0 render times 10^(12/20)).

**A126** The capability bits are the next free ones of develop `629805b`: `kCapWtRemap` (26), `kCapFxLevel` (27), `kCapCleanMode` (28); the lead renumbers when the other blocks merge. **A127** the new blocks share the table end with other workers' blocks; the positions above are those of this branch (1484..1506), `kNumModDestinations` 412.

### 4.16 B14 – Pitch modes, CRS coarse, pitch tracking, transpose snap, unison laws & range, phase legato (G25-G27, G31, G32) – positions 1507..1543 [37] – area 02 / 07

12 keys per oscillator (N = 1..3) + 1 global voice key = 37 keys.

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|---------|------|-------|----|----:|------|
| `oscN.pitch_mode` (x3) | `Osc N Pitch Mode` | choice | Semitones / Harmonics / Ratio / Step | Semitones | - | - | V | no | `oscN_pitch_uni` |
| `oscN.pitch_coarse` (x3) | `Osc N Pitch Coarse` | float | -48 .. 48 | 0 | st | lin | V | y | same |
| `oscN.pitch_track` (x3) | `Osc N Pitch Tracking` | bool | Off/On | On | - | - | V | no | same |
| `oscN.pitch_bend_track` (x3) | `Osc N Pitch Bend Tracking` | bool | Off/On | On | - | - | V | no | `more:` |
| `oscN.fixed_pitch` (x3) | `Osc N Fixed Pitch` | float | 0 .. 127 | 48 | - | lin | V | y | `oscN_pitch_uni` |
| `oscN.transpose_snap` (x3) | `Osc N Transpose Snap` | int | 0 .. 8191 | 0 | - | lin | V | no | `more:` |
| `oscN.unison.mode` (x3) | `Osc N Unison Mode` | choice | Linear / Super / Exp / Inv / Random | Linear | - | - | V | no | `oscN_pitch_uni` |
| `oscN.unison.range` (x3) | `Osc N Unison Range` | float | 0 .. 48 | 2 | st | lin | V | y | same |
| `oscN.unison.blend_shape` (x3) | `Osc N Unison Blend Shape` | float | -100 .. 100 | 0 | % | lin | V | y | same |
| `oscN.unison.stack_repeat` (x3) | `Osc N Unison Stack Repeat` | choice | 1x / 2x / 3x | 1x | - | - | V | no | same |
| `oscN.phase_legato` (x3) | `Osc N Phase Legato` | bool | Off/On | Off | - | - | V | no | `more:` |
| `oscN.phase_memory` (x3) | `Osc N Phase Memory` | choice | All Voices / Contiguous / Per Voice | All Voices | - | - | V | no | `more:` |
| `voice.phase_legato` | `Voice Phase Legato` | bool | Off/On | Off | - | - | G | no | `master_expression` |

Order inside B14: `osc1` (12 keys), `osc2` (12 keys), `osc3` (12 keys), `voice.phase_legato` (1 key).

Block B14 follows block B16 in the table (merge order of the lead): the 12 modulatable keys are destination choices 412..423 (`oscN.pitch_coarse`, `oscN.fixed_pitch`, `oscN.unison.range`, `oscN.unison.blend_shape`, N = 1..3, in table order).

### 4.13 Choices made by WP0 where this file was silent

* **Order inside a block** (the destination numbers of §2 only depend on the counts, the order fixes the table positions): B03 is LFO-major (`lfo1.mono`, `lfo1.direction`, `lfo1.host`, `lfo2.mono` ...); B10 lists `osc1..3.filter_blend`, `noise.filter_blend`, then the sends source by source (`osc1.bus1_send`, `osc1.bus2_send`, `osc2.bus1_send` ... `noise.*`, `filter1.*`, `filter2.*`), `filter1.destination`, `filter2.destination`, `bus1.level`, `bus2.level`, `bus1.destination`, `bus2.destination`, `bus1.fx_bypass`, `bus2.fx_bypass`, `direct.level`, `main.fx_bypass`; B11 is filter-major (`filter1.var/pan/level`, `filter2.*`, `fxfilter.*`); B12 follows the row order of §4.12 (the 16 `<fx>.rack` keys in the order of the module list). `tests/test_new_keys.cpp` freezes the positions.
* **Display names not given above:** `Utility Polarity Left/Right`, `Utility Low Pass`, `Utility High Pass`, `Utility Mono Bass`, `Utility Mono Bass Frequency`, `Utility Width/Pan/Mix/Level`; the Bode, Convolve and Splitter names are the ones of the tables. Rack keys: `<Module> Rack` (`Hyper Dimension Rack`, `Splitter L/M/H Rack`, ...).
* **Groups not given above:** `Master / Curves` (B05), `Master / Scale`, `Master / Tuning`, `Master / MPE` (B07), `FX Filter` for the `fxfilter.*` additions of B11 (the existing group of that filter), `Osc N`, `Noise`, `Filter N` for the mixer keys of those sources.
* **Constants added to `params.h` / `modulation.h`:** `kNumKeysV2` = 880, `kNumLegacyFxSlots` = 9 (the effect slots the engines implement; `kNumFxSlots` = 16 is the table), `kNumModSourcesV2` = 28.
* **Engines:** an engine implements the version 2 keys only until `IEngine::capabilities ()` (engine_caps.h) says more; a stored value beyond what the engine implements acts like the last implemented one (filter model / style, `rlfoN.style`, oscillator destination), `Preset table` plays the saw and `Custom` the sine (§5 fallbacks).

### 4.17 B17 – Compressor, delay, distortion, phaser and reverb extras (G73-G77) – positions 1507..1520 [14] – area 08

Added by the fx-extras work package (FXEXTRA-REPORT.md), after block B16, as its own trailing block (R2: appended at the end of `kRaw[]`). The rows G73-G77 of `10-coverage-matrix.md` name the features only ("limit mode, adjustable crossovers", "Tap->Delay, HQ, scalar offset", "X-Shaper, Tube / Diode / Rectify, filter key track", "DEPTH 2", "Plate / Hall / Vintage / Nitrous + room modes"); the Serum manual is not in the repository, so the definitions below are this file's own (assumptions A140-A159). The per-module `LEVEL` trim (G72) is not part of B17.

**Compressor** (`compressor.*`, group `FX / Compressor`, G73) [4]:

| Key | Name | Type | Range | Default | Unit | Curve | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|-----|------|
| `compressor.limit` | `Compressor Limit` | bool | Off/On | Off | - | - | no | `fx_comp_limit` |
| `compressor.limit_latency_comp` | `Compressor Limiter Latency Comp` | bool | Off/On | On | - | - | no | `fx_comp_limit` |
| `compressor.crossover_low` | `Compressor Low Crossover` | float | 20 .. 1000 | 120 | Hz | 0.4, 0 dec | y | `fx_comp_limit` |
| `compressor.crossover_high` | `Compressor High Crossover` | float | 200 .. 16000 | 2500 | Hz | 0.4, 0 dec | y | `fx_comp_limit` |

`limit` replaces the downward stage of every enabled band by a look-ahead limiter whose ceiling is the band's `upper_threshold` (the ratio is ignored, the upward stage stays). The limiter is a peak limiter of 1.5 ms look-ahead, stereo linked, with an estimate of the peak between two samples (A141); `release` sets how fast the gain returns. `limit_latency_comp` On adds the look-ahead to the latency of the chain (`IEngine::latencySamples ()`, the other signal paths of the chain are delayed by it, A142); Off leaves the chain alone, the module still delays its own dry signal so that `mix` never combs. The crossovers are the two Linkwitz-Riley 24 dB crossovers of the multiband compressor; the defaults 120 Hz and 2500 Hz are the fixed values of the engine (the design file 08 says 150 Hz and 3 kHz, which is not what the engine does, A143). The high one is kept 1.25 times above the low one. Modulation moves them once per block (A144).

**Delay** (`delay.*`, group `FX / Delay`, G74) [2]:

| Key | Name | Type | Range | Default | Unit | Curve | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|-----|------|
| `delay.hq` | `Delay High Quality` | bool | Off/On | Off | - | - | no | `fx_delay_3` |
| `delay.offset` | `Delay Offset` | float | 0.25 .. 4 | 1 | x | 0.5, 3 dec | y | `fx_delay_3` |

`hq` reads the delay with an 8-point Lagrange interpolator instead of the 4-point Catmull-Rom of the Vital delay (A145). `offset` multiplies the delay time of the right channel (A146); at 1 the two channels are as before. The UI snaps to 1.333 (triplet) and 1.5 (dotted). **Tap->Delay** is not a key: it is the tap-tempo button of the delay panel, which measures the interval of the last taps and writes `delay.sync` = Free and `delay.time` / `delay.time2` (A147).

**Distortion** (`distortion.*`, group `FX / Distortion`, G75) [1 key, 8 appended choices, 2 content curves]:

| Key | Name | Type | Range | Default | Unit | Mod | Kick |
|-----|------|------|-------|--------:|------|-----|------|
| `distortion.filter_keytrack` | `Distortion Filter Key Track` | float | -100 .. 100 | 0 | % | y | `fx_distortion_2` |

`distortion.type` gets the entries 6 `Tube`, 7 `Diode 1`, 8 `Diode 2`, 9 `Zero square`, 10 `Asym`, 11 `Rectify`, 12 `X-Shaper`, 13 `X-Shaper (Asym)` (R3, section 3). Definitions (v = input times the drive gain): Tube `tanh (v + 0.3) - tanh (0.3)`; Diode 1 `(1 - e^-v+) - 0.5 (1 - e^2v-)` (v+ and v- the positive and the negative part); Diode 2 `e^v- - e^-v+`; Zero square `tanh (32 v)`; Asym `tanh (v+) + max (v-, -0.5)`; Rectify `|clamp (v)|`. The types with an asymmetric curve (Tube, Diode 1, Asym, Rectify, X-Shaper (Asym)) pass a 10 Hz DC blocker (A148). `filter_keytrack` moves the cutoff of the filter by the note of the last key relative to C4 times the amount, like `fxfilter.keytrack` (A149). **X-Shaper**: the two transfer curves are drawn by the user and live in the `content` section as `content.curves.shaper_a` and `shaper_b` (same point list as the velocity curve: x = input magnitude 0..1, y = output magnitude 0..1, power per segment; empty = identity; section 5). X-Shaper uses A for both polarities (odd symmetric), X-Shaper (Asym) uses A for the positive and B for the negative half (A150). The curves are baked into 513-point tables on the message thread and copied to the effect between blocks.

**Phaser** (`phaser.*`, group `FX / Phaser`, G76) [1]:

| Key | Name | Type | Range | Default | Unit | Mod | Kick |
|-----|------|------|-------|--------:|------|-----|------|
| `phaser.depth2` | `Phaser Depth 2` | float | -48 .. 48 | 0 | semitones | y | `fx_phaser_3` |

`depth2` is added to `phaser.depth` for the right channel only, so that the two channels sweep over different ranges; at 0 the effect is as before (A151).

**Reverb** (`reverb.*`, group `FX / Reverb`, G77) [6]:

| Key | Name | Type | Range | Default | Unit | Mod | Kick |
|-----|------|------|-------|--------:|------|-----|------|
| `reverb.type` | `Reverb Type` | choice | 0 `Vital`, 1 `Plate`, 2 `Hall`, 3 `Vintage`, 4 `Nitrous`, 5 `Basin` | 0 | - | no | `fx_reverb_3` |
| `reverb.room` | `Reverb Room` | choice | 0 `Space`, 1 `Marble`, 2 `Rectangle`, 3 `Hexagon`, 4 `Box` | 2 | - | no | `fx_reverb_3` |
| `reverb.width` | `Reverb Width` | float | 0 .. 200 | 100 | % | y | `fx_reverb_3` |
| `reverb.er_size` | `Reverb ER Size` | float | 0 .. 100 | 50 | % | no | `fx_reverb_3` |
| `reverb.diff_a` | `Reverb Diffusion A` | float | 0 .. 100 | 70 | % | y | `fx_reverb_3` |
| `reverb.diff_b` | `Reverb Diffusion B` | float | 0 .. 100 | 60 | % | y | `fx_reverb_3` |

`type` 0 is the feedback delay network of the Vital reverb, bit for bit as before (R5). The other five are our own algorithms (`reverb_algorithms.h`, A152): Plate (after Dattorro's plate: four input all-passes, a figure-of-eight tank with a modulated all-pass, damping and 14 output taps), Hall (eight-line feedback delay network with a Hadamard matrix, an all-pass in every line, long lines), Vintage (eight feedback combs and four series all-passes per channel, darker output), Nitrous (the network with short dense bright lines and strong modulation), Basin (the network with line lengths from the modes of a room). `room` chooses the room of Basin (dimensions in metres: Space 24x32x18, Marble 6.3x4.1x3, Rectangle 9x5.5x3.5, Hexagon 7.5x6.5x3.8, Box 5x5x5; the eight lines are the axial, tangential and oblique path lengths and their sum divided by the speed of sound) and is inert for the other types (A153). All types share `size` (scales the delay lines, 0.35x .. 2.8x), `decay` (T60, the loop gain of every line follows from its delay), the damping shelves, the pre cuts, the pre-delay, `chorus_amount` / `chorus_rate` (modulation of the lines) and `mix`. `er_size` scales the six early reflections per channel (A154); `diff_a` is the input diffusion, `diff_b` the diffusion inside the loops (A155). `width` is mid / side width of the wet signal for every type including Vital (A156); `er_size`, `diff_a`, `diff_b` and `room` are inert for the Vital type and the UI dims them there. Switching `type` or `room` clears the tail (stepped change, R7).

**Choices appended** (section 3): `distortion.type` 6 -> 14 entries (`kNumLegacyDistortionTypes` = 6). **Destinations**: the 8 modulatable keys take the destination choices 412-419 (after the B16 levels 404-411) in table order (`compressor.crossover_low` 412, `compressor.crossover_high` 413, `delay.offset` 414, `distortion.filter_keytrack` 415, `phaser.depth2` 416, `reverb.width` 417, `reverb.diff_a` 418, `reverb.diff_b` 419); `kNumModDestinations` = 420. **Caps** (`engine_caps.h`, bits 29-33 after the B16 bits 26-28, A157): `kCapFxCompressorLimit` (the four compressor keys), `kCapFxDelayExtras`, `kCapFxDistortionExtras` (the keytrack, `distortion.type` >= 6, the two curves), `kCapFxPhaserDepth2`, `kCapFxReverbAlgos` (all six reverb keys); each is set by the Vital adapter because `test_fxextra.cpp` renders the keys off and on. **Kick** pages 108-112: `fx_comp_limit`, `fx_delay_3`, `fx_distortion_2`, `fx_phaser_3`, `fx_reverb_3`. **UI** (A158): the FX panels of the five modules get the new controls (limit toggle and crossover handles on the band view, HQ toggle / offset knob / TAP button, the X-Shaper curve editor, depth 2 knob, type / room chips and the four reverb knobs).

### 4.18 B18 – Sub oscillator and Noise extras (G23, G38) – positions 1558..1575 [18] – area 02

Added by the osc-extras work package (OSCEXTRA-REPORT.md) as the last trailing block (R2). It was drafted as "B13" on a branch cut before B16/B14/B17 and renumbered at the merge: positions 1558..1575, destination choices 432..441, capability bits 35 (`kCapSubOsc`) and 36 (`kCapNoiseExtras`), Kick page 116 (`sub_main`), `noise` extras on the `more:` sheet of the `noise` page.

**Sub oscillator (`sub.*`, group `Sub`, Serum p.123-127) [11]:**

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `sub.on` | `Sub On` | bool | Off / On | Off | - | - | V, X | no | `sub_main` |
| `sub.waveform` | `Sub Waveform` | choice (`kSubWaveforms`, 6) | 0..5 (Sine, Rounded Rect, Triangle, Saw, Square, Pulse) | 0 | - | - | V, X | no | `sub_main` |
| `sub.octave` | `Sub Octave` | whole | -4 .. 4 | -1 | - | lin | V, S | y | `sub_main` |
| `sub.tune` | `Sub Tune` | cont | -100 .. 100 | 0 | cents | lin | V, S | y | `sub_main` |
| `sub.level` | `Sub Level` | cont | 0 .. 100 | 50 | % | lin | V, S | y | `sub_main` |
| `sub.pan` | `Sub Pan` | cont | -100 .. 100 | 0 | % | lin | V, S | y | `sub_main` |
| `sub.destination` | `Sub Destination` | choice (`kOscDestinations`, 7) | 0..6 | 0 | - | - | V, X | no | `sub_main` |
| `sub.mono` | `Sub Mono` | bool | Off / On | On | - | - | V, X | no | `sub_main` |
| `sub.filter_blend` | `Sub Filter Blend` | cont | 0 .. 100 | 0 | % | lin | V, S | y | `more:sub_main` |
| `sub.bus1_send` | `Sub Bus 1 Send` | cont | 0 .. 100 | 0 | % | lin | V, S | y | `more:sub_main` |
| `sub.bus2_send` | `Sub Bus 2 Send` | cont | 0 .. 100 | 0 | % | lin | V, S | y | `more:sub_main` |

Semantics (**ASSUMPTIONS A60..A63, A67, A68**): `sub.waveform` selects one of 6 shapes (saw, square and pulse are PolyBLEP band-limited, sine / rounded rect / triangle are alias free by construction; pulse is 25 % duty, DC free). `sub.octave` transposes by octaves (-1 = 12 semitones down) and `sub.tune` by cents. `sub.destination` matches the oscillator destinations (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None). When `sub.mono` is On, the sub plays monophonically on the lowest active voice note to prevent phase cancellation; when Off, it follows polyphonic voice allocation. `sub.filter_blend` crossfades between Filter 1 and Filter 2 when `sub.destination = Filter blend`. `sub.bus1_send` and `sub.bus2_send` tap post-level/pan into Bus 1 and Bus 2.

**Noise colours and sample extras (`noise.*`, group `Noise`, Serum p.128-132) [7]:**

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

### 4.19 B20 – Mod amount destinations, mod curves, stereo source, stereo routing & voice override (G1-G6) – positions 1576..1577 [2] – area 05 / 06 / 01

Added by the mod-amount work package (MODAMT-REPORT.md) as trailing block B20 (R2: appended at the end of `kRaw[]`).

**Master Voicing Extras (`master.*`, group `Master / Voicing`, G5) [2]:**

| Key | Name | Type | Range | Default | Unit | Curve | Sc | Mod | Kick |
|-----|------|------|-------|--------:|------|-------|----|-----|------|
| `master.stereo_routing` | `Master Stereo Routing` | cont | 0 .. 100 | 100 | % | lin | V, S | y | `master_voicing` |
| `master.voice_override` | `Master Voice Override` | choice (`kVoiceOverrides`, 2) | Kill / Steal | Kill | - | - | V, X | no | `master_voicing` |

Semantics (**ASSUMPTIONS A240..A248**):
- `master.stereo_routing` (position 1576, destination choice 442): controls stereo spread / stereo routing width (0..100%). Modulatable.
- `master.voice_override` (position 1577): selects voice stealing policy when voice limit is reached (Kill = 0, Steal = 1).
- **Modulation of modulation amounts (G1)**: modulation slots 1..64 amount parameters are added as modulation destinations `mod01.amount` .. `mod64.amount` (choices 443..506). Direct self-modulation and cyclic modulation loops are detected and rejected to ensure deterministic, feedback-free evaluation.
- **Per-connection modulation curve (G2)**: custom curves per matrix connection stored in additive state `content.mod_curves[]` (up to 64 curves, up to 100 points `{x, y, power}` matching LFO shape format). Applied to source value before amount.
- **Modulation of portamento time and polyphony (G3)**: destination choice 507 modulates `master.portamento.time`; choice 508 modulates `master.polyphony` (dynamic voice count modulation).
- **Modulation source `stereo` (G4)**: choice 39 (0-based index 38), bipolar per-voice left/right channel sign source (+1 left, -1 right).
- **Oscillator transpose quantize (G6)**: Vital's `osc_N_transpose_quantize` imported into `oscN.transpose_snap` (Block B14, 0..8191 scale bitmask).
- **Engine capability bit**: `kCapModAmount` = `1ull << 37`.

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
| **Total** | | **696** | 880..1575 | **172** | 270..441 |

Resulting table: 880 + 696 = **1576 keys**, 269 + 172 = **441 modulatable** keys, destination choice size **442**, mod sources **38**, positions 0..1575. The sustain pedal adds 0 keys. Choice-list appends (§3): 9 lists, 0 renumbered.

## 8. Decided items deliberately NOT specified as keys, and undecided GAPs left out

Left out because no decision names them (not silently dropped; the owner can add them as another block): per-slot remap curve and RISE / FALL of the mod matrix slots (G69 matrix part; the wavetable position part is B16), warp modes G34-G37, ENV 1 toggle on channels (G39), voice steal / same-note (G12), poly legato (G13), separate bend up / down (G14), stereo spread (G15), LFO swing (G16), per-voice control (G17, Q25 proposed), 4x / no smoothing (G18), Multisample / Granular / Spectral (G19, G21, G22), dynamic effect instancing (G71), compressor / delay / distortion / reverb / phaser extras (G73-G77), Save as Default (G02), lock (G08), MIDI learn (G07: shell table, not a key). Q3, Q4 (A/B snapshots), Q7, Q31, Q32, Q33, Q14 need no key. G02, G07 and G08 are built key-free, see 15-shell-features.md.

## 9. Kick pages (09 §5 has 61 pages)

New pages: 42 -> **103 pages** (B16 of WTREMAP adds 4 more: 107, B14 adds 3 more: 110, B17 adds 5: 115, B18 adds 1: 116) (one `clap.remote-controls` page = at most 8 keys; keys on a `more:` sheet add no page).

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
| A122 | RISE / FALL: one pole glide of the remapped position per voice, per wave buffer, note on jumps, 0 = off |
| A123 | The unison frame spread is added after the remap |
| A124 | FX LEVEL -24..12 dB on the module output after its dry / wet, modulatable, ramped per block |
| A125 | Clean Mode: drive becomes a linear post-filter gain, the filter runs without drive; Comb / Formant ignore it |
| A126 | Capability bits 26..28 for the three B16 areas |
| A127 | B16 positions 1484..1506, destinations 404..411 on this branch; the lead renumbers at merge |
| A80 | `oscN.pitch_mode` (G26): 4 choices: Semitones (0), Harmonics (1), Ratio (2), Step (3). Semitones mode performs standard semitonic pitch transposition. Harmonics mode multiplies pitch by an integer harmonic index (1 + floor(coarse)). Ratio mode multiplies frequency by ratio 1 + coarse (or 1/(1-coarse) when negative). Step mode steps through the scale degrees of the active tuning / MTS-ESP dynamic tuning seam. Default Semitones (0). |
| A81 | `oscN.pitch_coarse` (G25): coarse pitch, key range -48 .. 48, modulatable, default 0. Semitones: semitones. Harmonics: multiplier 1 + floor(coarse) (values below 0 give 1). Ratio: multiplier 1 + coarse (1 / (1 - coarse) when negative). Step: key steps of the active tuning (the tuned note table, so 24-EDO moves 50 cents a step). |
| A82 | `oscN.pitch_track` & `oscN.fixed_pitch` (G27): `pitch_track` toggle (default on / true). When off, oscillator runs at fixed pitch `oscN.fixed_pitch` (default 48 = C3, matching Vital's `kNoMidiTrackDefault`). `oscN.pitch_bend_track` (default on / true) controls whether pitch bend affects fixed-pitch oscillators. |
| A83 | `oscN.transpose_snap` (Row 02-10): Transpose snap keyboard quantizing transpose to selected pitch classes. 13-bit value (0..8191, default 0 = Off): bits 0..11 select pitch classes C through B; bit 12 (0x1000) selects Global Snap. Uses Vital's built-in `snapTranspose` engine algorithm. |
| A84 | `oscN.unison.mode` (G32): 5 detune laws: Linear (0), Super (1), Exp (2), Inv (3), Random (4). Linear matches standard Vital detune. Super applies a cubic distribution clustered near the center. Exp applies an exponential distribution. Inv scoops the center voices outward. Random applies a deterministic per-voice pseudo-random detune. Default Linear (0). |
| A85 | `oscN.unison.range` & `oscN.unison.blend_shape` & `oscN.unison.stack_repeat` (G32): `unison.range` sets maximum detune span (0..48 semitones, default 2.0 st, `kMod`). `unison.blend_shape` controls detuned vs center voice level distribution (-100..100%, default 0.0%, `kMod`). `unison.stack_repeat` sets interval repetition for stack styles (1x, 2x, 3x, default 1x = 0). |
| A86 | `oscN.phase_legato`, `oscN.phase_memory`, `voice.phase_legato` (G31): either switch on and another key held when a note starts: the new voice takes the phases of the voice that rendered last (state shared by all voice pairs of the oscillator; it is one block old). `phase_memory`: All Voices (0) every unison phase follows, Contiguous (1) only the centre phase follows (the detuned ones restart), Per Voice (2) the voice keeps its own free running phases. Defaults off / All Voices. |
| A87 | B14 sits behind B16: positions 1507..1543, destinations 412..423, capability bit `kCapPitchUnison` = 1 << 29 (A126 lists 26..28 for B16) |
| A140 | G73-G77 are defined by this file (B17); the Serum manual is not in the repository, so every definition below is ours |
| A141 | Limit mode: peak limiter, 1.5 ms look-ahead, stereo linked, ceiling = the band's upper threshold, estimated inter-sample peak; the upward stage is kept |
| A142 | `limit_latency_comp` On reports the look-ahead as chain latency and delays the other paths; Off does not; the dry signal of the compressor mix is always aligned |
| A143 | Crossover defaults 120 Hz / 2500 Hz (what the engine does), not the 150 Hz / 3 kHz written in 08 |
| A144 | Modulated crossovers update the filter coefficients once per block |
| A145 | Delay HQ: 8-point Lagrange read |
| A146 | `delay.offset` multiplies the right delay time (all styles), range 0.25 .. 4 |
| A147 | Tap->Delay is a UI tap-tempo action (writes Free sync and the two times), not a key |
| A148 | Distortion types 6-11 as defined in 4.17; asymmetric types have a 10 Hz DC blocker |
| A149 | Distortion filter key track uses the last-key note like the FX filter |
| A150 | X-Shaper curves: odd symmetric from A; Asym: A positive, B negative; stored in `content.curves.shaper_a / shaper_b`, 513-point tables |
| A151 | Phaser DEPTH 2 is a right-channel offset of the depth in semitones |
| A152 | The five reverb algorithms are our own (Dattorro-type plate, Hadamard FDN, Freeverb-type combs, dense bright FDN, room-mode FDN) |
| A153 | `reverb.room` acts on Basin only |
| A154 | ER SIZE scales six early reflections per channel (level fixed per type) |
| A155 | DIFF A = input diffusion, DIFF B = diffusion in the loops (Serum's DIFF A/B and DIFFUSION are mapped onto these two; FEEDBACK is the decay) |
| A156 | `reverb.width` is mid / side width of the wet signal for all types |
| A157 | Five cap bits 30-34 for B17, apart from the WP bits |
| A158 | UI of B17 as listed in 4.17 |
| A60 | The Serum manual is not in the repository: the sub waveform list (Sine, Rounded Rect, Triangle, Saw, Square, Pulse) is this file's definition of "sine / triangle / saw / square / pulse + a few more" |
| A61 | `sub.octave` -4 .. 4 whole octaves (default -1), `sub.tune` +-100 cents, `sub.level` default 50 % (the oscillator default), `sub.on` default Off so every existing preset is unchanged |
| A62 | `sub.destination` uses the oscillator list `kOscDestinations` (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) with the mixer graph of 4.10 (`sub.filter_blend`, two bus sends) |
| A63 | `sub.mono` (default On): only the voice with the lowest sounding note carries the sub; the hand-over between voices is ramped over one block (no click) |
| A64 | Noise samples are built into the engine (9 procedural sets: White (the stock Vital noise, bit identical), Pink, Brown, Geiger, Tape Hiss, Vinyl Clicks, Digital Hash, Velvet Noise, Atmosphere); no files |
| A65 | `noise.pitch` +-48 semitones, `noise.fine` +-100 cents, `noise.keytrack` (default Off) makes the playback rate follow the key relative to middle C, `noise.phase` is the start position in % of the sample, `noise.random_phase` picks it per note |
| A66 | `noise.loop` (default On); off = one shot, silent after the end of the sample (the stock Vital held the last value) |
| A67 | B18 is placed after B17: positions 1558..1575, destination choices 432..441 (renumbered from the draft "B13" at 404..413) |
| A68 | Capability bits 35 (`kCapSubOsc`) and 36 (`kCapNoiseExtras`); each is set because `test_oscextra.cpp` renders every key off and on |
| A69 | UI: Sub panel (picker row of six >= 44 pt segments, level / pan pad, four knobs, rest behind More) and Noise panel (`<` name `>` row, nine item menu, level / pan / pitch / fine knobs, rest behind More), both on the oscillator page |
| A240 | Modulating another modulation's amount (`modNN.amount` as destination, choices 443..506). Self-modulation (`modK` modulating `modK.amount`) and cyclic dependency chains (`modA` -> `modB.amount` -> `modA.amount`) are detected before evaluation and treated as neutral (amount modulation ignored, connection evaluated with base amount) to prevent infinite recursion and undefined feedback behavior. |
| A241 | Modulation curves (`line_mapping` in Vital / Serum per-slot curve) are stored in additive `content.mod_curves[]` (up to 64 curves, up to 100 points per curve, format matching LFO shape points `{x, y, power}` with power in -20..20, x in 0..1 ascending, y in 0..1). A curve is applied to the source value before scaling by the modulation amount. When no custom curve is defined for a slot or the curve is empty/flat, standard linear response is used. |
| A242 | Destination list indexing: To maintain 100% backward compatibility and prevent shifting of destination indices for the first 880 keys and existing blocks B01-B18, non-parameter modulatable destinations (`modNN.amount`, `master.portamento.time`, `master.polyphony`) are appended in `DestinationLists` after all table-modulatable parameters. `master.stereo_routing` is placed in `kRaw[]` with `kMod` at position 1576, giving destination choice 442. Choices 443..506 are `mod01.amount` .. `mod64.amount`, choice 507 is `master.portamento.time`, and choice 508 is `master.polyphony`. Total destinations = 508 (choiceCount = 509). |
| A243 | `stereo` modulation source (choice 39, 0-based index 38): per-voice left/right channel sign source (+1.0 for left channel / left ear, -1.0 for right channel / right ear). In mono voice evaluation it evaluates to 0.0. |
| A244 | `master.stereo_routing` (key `master.stereo_routing`, group `Master / Voicing`, G5): 0..100%, default 100%, continuous, `kMod`. Controls stereo spread / stereo panning width across the voice processing chain. |
| A245 | `master.voice_override` (key `master.voice_override`, group `Master / Voicing`, G5): choice Kill (0) / Steal (1), default Kill (0), `kAuto`. Sets voice stealing policy when voice limit is reached. |
| A246 | `master.portamento.time` (destination 507) and `master.polyphony` (destination 508) modulation: bound in engine adapter to Vital's `"portamento_time"` and `"polyphony"` modulation destinations. Polyphony modulation dynamically modulates the active polyphony count (e.g. macro turning a polyphonic preset into mono). |
| A247 | `osc_N_transpose_quantize` (G6): Vital's integer transpose quantize bitmask (12 bits for 12 semitones in scale) is imported directly into SynthyBoy's `oscN.transpose_snap` (Block B14, positions 1512, 1524, 1536), which uses the exact same 0..8191 bitmask representation. |
| A248 | Capability bit `kCapModAmount` = `1ull << 37`: set on `sbvital` engine adapter to indicate support for mod amount modulation, mod curves, stereo source, stereo routing / voice override, and portamento/polyphony modulation. |
