# Vital preset import (`.vital`) – mobile UI and import pipeline design

Scope: importing user-supplied Vital preset files into SynthyBoy: the user-facing flow (entry points, progress, report sheet, save, batch, duplicates) and the technical pipeline (parser, mapping, embedded wavetables / LFO shapes, report, API, tests). The preset BROWSER is excluded: Kick. We provide only the "import" action.
Cites: `[V ...]` Vital unofficial manual (thin; the preset menu is the only relevant part), `[src file]` = file in this repo. Key families: `oscN` = osc1..osc3, `modNN` = mod01..mod64.
Status: `HAVE` = exists in core/engine today; `GAP` = missing, designed here and listed in Annex A.4 ("core changes required").
Facts about the file format are taken from the imported Vital source under `core/engine-vital/upstream/src/` (read for interoperability facts only, see Annex B) and marked `[verified: file]`; facts from memory of Vital's `SynthBase` save code (not imported into this repo) are marked `[unverified]` and each has a fixture/real-preset check in Annex C.

## 1. Function inventory = Vital preset content -> our model

### 1.0 Function inventory (what the feature does)

| Function | Origin | Where specified | Engine / core status |
|----------|--------|-----------------|----------------------|
| Parse `.vital` (JSON), tolerant, bounded, never crash / hang | Vital format | 1.1, Annex A | GAP (new code `core/src/vital_import/`) |
| Map 730 one-to-one settings + 150 special parameters (mod slots, wavetable, LFO shape, FX order, routing, bend range) | both | 1.2 | HAVE (all target keys exist) |
| Embedded wavetables (3 per preset) -> user ContentPack entry | Vital | 1.3.1 | GAP: engine has no user-wavetable path (`oscN.wavetable` = 13 built-ins; 03-wavetable.md Q2) |
| Embedded custom LFO shapes (8 per preset) -> `lfoN` shape data | Vital | 1.3.2 | GAP: `lfoN.shape` = 12 built-ins (PARAMETERS "Not exposed") |
| Sample oscillator content | Vital | 1.3.3 | GAP (noise is the engine's built-in sample only) |
| Import report (imported / approximated / dropped) + sheet + log | new | 1.4, 2 | GAP |
| Save as `.sbpreset` with imported metadata | new | 2.4, A.3 | HAVE (`savePreset`), extended by an additive `import` / `content` section |
| Entry points: file picker, desktop drag-drop, Open-in / share sheet, Kick browser action | new | 2.1, 5 | GAP |
| Batch import of a folder, duplicate handling | new | 2.6 | GAP |
| Stand-alone `.vitaltable` / `.vitallfo` files (same JSON objects as inside a preset) | Vital | 1.3, A.2 | GAP (shares the parser) |
| Export as `.vital` | - | decided: NOT a product feature (Annex C.3) | - |

### 1.1 File format (as found in the source)

A `.vital` file is one UTF-8 JSON object (written by nlohmann/json 3.1.2 [verified: `third_party/json`], extension `vital` [verified: `kPresetExtension` in synth_constants.h]). Related extensions in the same code: `vitaltable` (wavetable), `vitallfo` (LFO shape), `vitalbank`, `vitalskin` [verified: synth_constants.h].

```
{
  "synth_version": "1.5.5",          string "major.minor.patch"                   [unverified]
  "preset_name":  "My Bass",         string                                        [unverified]
  "author":       "Someone",         string                                        [unverified]
  "comments":     "free text",       string                                        [unverified]
  "preset_style": "Bass",            string, one of Vital's styles or ""           [unverified]
  "macro1".."macro4": "Cutoff",      macro display names                           [unverified]
  "settings": {
     "<control>": <float>,           730+ named controls, RAW engine value (not display value) [verified: control names/ranges in synth_parameters.cpp]
     "modulations": [ {"source":"lfo_1","destination":"filter_1_cutoff"}, ... ],   [source/destination strings: verified by adapter vitalSourceName(); array shape: unverified]
     "wavetables": [ <wavetable>, <wavetable>, <wavetable> ],                      osc 1..3     [verified: wavetable object; array: unverified]
     "sample": <sample>,                                                           [verified: object]
     "lfos": [ <lfo-shape> x 8 ],                                                  lfo 1..8     [verified: object; array: unverified]
     "tuning": { ... }                                                             optional, Vital >= 1.0 [unverified]
  }
}
```

Key points:
* Settings hold the engine's raw control values. The adapter's conversions (`vital_bindings.cpp`, `Conv`) go "our unit -> control"; the importer applies their exact inverses (1.2.0). Display units of Vital's UI are irrelevant.
* Modulation: connection `i` (0-based position in `settings.modulations`) is slot `modulation_{i+1}_*`; its five values live in `settings` as `modulation_N_amount`, `_power`, `_bipolar`, `_stereo`, `_bypass` [verified: names in mod_parameter_list]. Slots beyond the array length are unused; their values are ignored. At most 64 [verified: `kMaxModulationConnections`]. Source strings = the engine control names: `env_1..6`, `lfo_1..8`, `random_1..4`, `macro_control_1..4`, `velocity`, `note`, `aftertouch`, `pitch_wheel`, `mod_wheel`, `random` [verified: adapter `vitalSourceName`], plus sources we do not have (`lift`, `slide`, `stereo`, `note_in_octave` [unverified]). Destination strings = any control name from 1.2.
* Version: `synth_version` -> integer `major<<16 | minor<<8 | patch`; the same encoding is used by the "version added" column of the parameter table (`0x000500` = 0.5.0) [verified]. Known versions up to 1.5.5 (last public commit `636ca0e`, UPSTREAM.md).
* Wavetable object [verified: wavetable_creator.cpp, wavetable_group.cpp, wavetable_component.cpp]:
  `{ "name", "author", "version", "remove_all_dc": bool, "full_normalize": bool, "groups": [ { "components": [ <component> ... ] } ... ] }`.
  A wavetable file may instead be a bare LFO-line object (`num_points`, `points`, `powers`): then the wavetable is built from that line (accepted, 1.3.1).
  Component: `{ "type": <name>, "keyframes": [ {"position": 0..256, ...fields} ... ], "interpolation_style": 0 none | 1 linear | 2 cubic, ...component fields }`.
  Component `type` strings [verified]: `Wave Source`, `Line Source`, `Audio File Source`, `Shepard Tone Source`, `Phase Shift`, `Wave Window`, `Frequency Filter`, `Slew Limiter`, `Wave Folder`, `Wave Warp`.
  Fields (all [verified]): Wave Source `interpolation` (0 time, 1 frequency), keyframe `wave_data` = base64 of 2048 little-endian float32. Line Source `num_points`, keyframe `pull_power`, `line` (LFO-line object). Audio File Source `normalize_gain`, `normalize_mult`, `window_size`, `fade_style` (0 wave blend, 1 none, 2 time, 3 freq), `phase_style` (0 none, 1 clear, 2 vocode), `random_seed`, `audio_sample_rate`, `audio_file` = base64 int16 mono PCM, keyframe `start_position`, `window_fade`, `window_size`. Phase Shift `phase`, `mix`, keyframe style `style`. Frequency Filter `cutoff`, `shape`, `style`, `normalize`. Slew Limiter `up_run_rise`, `down_run_rise`. Wave Window `left_position`, `right_position`, `window_shape`. Wave Folder `fold_boost`. Wave Warp `horizontal_power`, `vertical_power`, `horizontal_asymmetric`, `vertical_asymmetric`. Shepard Tone Source: only the common fields.
  Render model [verified: WavetableCreator::render]: frames = last keyframe position over all groups + 1 (<= 257); per frame every group runs its components in order, groups are summed; then post-process per `remove_all_dc` / `full_normalize`.
* LFO-shape object [verified: line_generator.cpp]: `{ "name": str, "num_points": n (<=100), "points": [x0,y0,x1,y1,...] (2n numbers), "powers": [p0..p(n-1)], "smooth": bool }`. `lfos[i]` belongs to `lfo_{i+1}`.
* Sample object [verified: sample_source.cpp]: `{ "name", "length", "sample_rate", "samples": base64 int16 mono, "samples_stereo": base64 int16 (only if stereo) }`. The engine default is the name `"White Noise"` [verified].
* Effect order is ONE number, `settings.effect_chain_order`, a permutation index of the 9 effects (0 .. 9!-1) [verified: control range `factorial(kNumEffects)-1`]; effect ids 0..8 = Chorus, Compressor, Delay, Distortion, EQ, FX Filter, Flanger, Phaser, Reverb [verified: `enum Effect`] = our `fx.order.N` choice list. Decode (clean statement of the engine's encoding [verified: utils.cpp]): `order[0..8] = 0..8; code = (int) value; for i in 0..8 { r = 9-i; inv = code % r; code /= r; take element at index (r-1-inv), shift the elements above it down by one, put it at index r-1 }`; result `order[position] = effect`. A cross-test decodes all 362 880 codes with the engine's function (Annex C.1).
* Filter routing is two switches `filter_1_filter_input`, `filter_2_filter_input` [verified: adapter]: (1 on filter 1 = series 2->1), (1 on filter 2 = series 1->2).

Versions handled and migrations (only what the source proves):

| `synth_version` | Handling |
|-----------------|----------|
| missing / unparsable | treated as 1.0.0, log `versionUnknown`; import continues |
| < 0.5.0 | Only 2 oscillators existed: missing `osc_3_*` -> `osc3.on = 0`. `osc_N_destination` (added 0.5.0) is absent: derive from `filter_1_oscN_input` / `filter_2_oscN_input` (1,0)->Filter 1, (0,1)->Filter 2, (1,1)->Filters 1+2, (0,0)->Effects only (`(0,0)` is `[unverified]`, fixture `filter_routing`). Reported `legacyMigration`. |
| < 0.6.1 | max 32 modulation slots; nothing to do |
| 0.5.0 .. 1.5.5 | parameter set as 1.2; settings added later than the file's version are absent -> rule "missing setting" below |
| > 1.5.5 | parse as 1.5.5, unknown settings go to `ignored`, log `versionNewer` |

Rule "missing setting": a key absent from `settings` takes our default (`ParamValues::defaults()`), except on/off switches `*_on`, which take Vital's default (`osc_1_on` = 1, every other switch = 0), and `osc_N_destination` (Vital defaults: osc 2 -> 1, osc 3 -> 3). Logged as `defaulted`, not shown in the sheet (counted only). Reason: a real Vital file lists every setting, so absence means an old or hand-made file; we do not embed Vital's default table (Annex B).

Tolerant parsing rules:
1. Skip a UTF-8 BOM and leading / trailing whitespace; reject if anything but whitespace follows the root value.
2. Root must be an object. A root without `settings` but with `groups` / `num_points` is a stand-alone wavetable / LFO file (A.2). Anything else: `notVital`.
3. Unknown keys are ignored (counted, names of the first 32 go to the log). Duplicate keys: last wins (as nlohmann).
4. Numbers: accept int, float, exponent; a JSON string that holds a number, `true` / `false` are accepted (value 1 / 0) with `typeMismatch` noted; `null`, objects, arrays for a numeric setting = missing. Non-finite after parsing (`1e999`) = missing.
5. Values are clamped into OUR range then snapped by `constrainValue`; a clamp that changes the value is `approximated / clamped` (items whose change is below 1e-4 of the range are not reported). Stepped values: round half up.
6. Strings: invalid UTF-8 -> U+FFFD, control characters removed, truncated at a code point boundary to 255 bytes (the `.sbpreset` meta parser DROPS any value that does not fit 255 bytes [verified: `preset.cpp`, `char val[256]`]).
7. A sub-structure that is malformed (a wavetable, an LFO shape, one modulation entry) is dropped alone, never the whole file; the file fails only when the root is not a JSON object or the limits below are hit.

Limits (constants in `vital_import.h`, tunable per call):

| Limit | Value | On exceed |
|-------|-------|-----------|
| file size | 32 MiB | `tooLarge`, nothing parsed |
| JSON nesting depth | 16 (real files: 9) | `malformed` |
| any string (non base64) | 4096 bytes | truncate |
| base64 string | decoded size <= 16 MiB per field, <= 48 MiB per file | that field dropped, `limitExceeded` |
| arrays: `modulations` 64, `wavetables` 3, `lfos` 8, groups per wavetable 8, components per group 32, keyframes per component 257, line points 100 | entries beyond are ignored, `limitExceeded` |
| unknown keys in `settings` | tracked in a bounded set (first 4096 distinct) | the rest only counted |
| working memory | `ImportOptions.maxWorkingBytes` = 192 MiB, one arena; all allocation goes through it | `budgetExceeded` (status), partial result NOT returned |
| work | op counter bound for wavetable rendering (frames x components x 2048 x constant) and `deadlineMs`, `cancel()` polled between components | `cancelled` / `budgetExceeded` |
| report items | 2000 | `report.truncated = true` |

Malformed input behaviour: `importVitalPreset` is `noexcept`, never reads outside `[data, data+size)`, never recurses beyond the depth limit (iterative parser), never allocates outside the arena, returns a status instead of throwing (a `std::bad_alloc` escape is caught and mapped to `budgetExceeded`). Output is deterministic (same bytes -> same result bytes, floats written `%.9g`). The fuzz list is Annex C.6.

### 1.2 Mapping tables

#### 1.2.0 Notation and conversion inverses

`x` = value read from `settings`. Class column: **L** lossless (inside our range; float rounding only), **C** lossless inside our range, clamped outside (reported `approximated/clamped` only when a clamp changes the value), **A** approximated (always or under the stated condition), **U** unmappable (dropped, reported), **I** ignored (host / UI / tier owned or fixed by the engine; log only, not in the sheet).

| Adapter `Conv` (ours -> control) | Importer inverse (control `x` -> ours) |
|----------------------------------|-----------------------------------------|
| `linear` a,b: `p*a+b` | `(x-b)/a` |
| `sqrtScaled` a: `sqrt(p*a)` | `x*x/a` |
| `quarticMs`: `(ms/1000)^(1/4)` | `x^4*1000` ms |
| `log2Ms` a: `max(a, log2(ms/1000))` | `x <= a+1e-4 ? 0 (portamento only; else our minimum) : 2^x*1000` ms |
| `log2Hz` | `2^x` Hz |
| `log2PeriodMs` | `1000 / 2^x` ms |
| `noteFromHz`: `12*log2(Hz/8.1757989)` | `8.1757989156 * 2^(x/12)` Hz |
| `index` b | `round(x) - b` |
| `indexMap` (warp type) | inverse of `{0,1,2,3,4,5,6,7,8,10,11}`; engine 9 and 12 have no inverse (A) |
| `volumeDb`: `(dB+80)^2` | `sqrt(x) - 80`, `<= -60` -> -60 |

The same inverse table is the importer's data: 730 rows (key, Vital control, conv, a, b), authored in `core/src/vital_import/vital_table.cpp` (Annex A.1). Verified now (python over `docs/params.json` and the adapter): 730 bindings, 0 missing keys, 0 duplicate controls, all 269 modulatable keys are bound (so every modulation destination can be resolved), 150 parameters are special (22 below + 128 mod `source` / `destination`), 730 + 150 = 880. Of Vital's 794 named controls, 730 are bound one-to-one, 64 are not (listed in 1.2.11).

Amount semantics: `modNN.amount` is a fraction of the destination's range in the engine's own scale [PARAMETERS.md line 48], the same scale Vital uses, so amounts import 1:1 (L). This holds for `engine-vital`; the own iOS engine must keep the same rule (Q6).

#### 1.2.1 Top level (metadata and content)

| Vital | Our target | Conversion | Class |
|-------|------------|------------|-------|
| `preset_name` | `PresetMeta.name` | UTF-8 clean, 255 B; empty -> file name without extension | L |
| `author` | `PresetMeta.author` | verbatim (original author is always kept); empty -> `""` | L |
| `comments` | `PresetMeta.description` | newlines -> spaces, truncated to 250 B + ellipsis (full text in `import.source.comments`, A.3) | C |
| `preset_style` | `PresetMeta.category` + tag `vital-style:<orig>` | Bass->Bass, Lead->Lead, Pad->Pad, Keys->Keys, Percussion->Pluck, Sequence->Lead, Experimental->FX, SFX->FX; `Template`, empty, unknown -> user chooses in the save sheet (pre-selected Lead) | A |
| `synth_version` | `import.source.vital_version` | stored text | I |
| `macro1..4` names | `import.source.macro_names` (UI may use them as macro labels) | text, 32 B each | A |
| `settings.wavetables[i]` | `content.wavetables` + `oscN.wavetable` | 1.3.1 | A (see there) |
| `settings.lfos[i]` | `content.lfo_shapes` + `lfoN.shape` | 1.3.2 | A (see there) |
| `settings.sample` | `noise.*` or dropped | 1.3.3 | U when custom, L when `"White Noise"` |
| `settings.tuning` | none (no tuning table; PARAMETERS "Not exposed") | reported with scale name | U |

#### 1.2.2 Oscillators (N = 1..3)

| Vital setting | Our key | Vital range | File -> ours | Class |
|---------------|---------|-------------|--------------|-------|
| `osc_N_on` | `oscN.on` | 0..1 | round | L |
| `osc_N_level` | `oscN.level` | 0..1 | `x*x*100` % | L |
| `osc_N_pan` | `oscN.pan` | -1..1 | `x*100` % | L |
| `osc_N_transpose` | `oscN.transpose` | -48..48 | round | L |
| `osc_N_tune` | `oscN.tune` | -1..1 | `x*100` ct | L |
| `osc_N_wave_frame` | `oscN.frame` | 0..256 | `x / max(1, F-1) * 100` %, F = frames of that oscillator's imported table (1.3.1); built-in fallback keeps the fraction | L (A/B check for F < 257, Q4) |
| `osc_N_frame_spread` | `oscN.frame_spread` | -128..128 | `x / max(1, F-1) * 100` % | L (same check) |
| `osc_N_phase` | `oscN.phase` | 0..1 | `x*360` deg | L |
| `osc_N_random_phase` | `oscN.random_phase` | 0..1 | `x*100` % | L |
| `osc_N_destination` | `oscN.destination` | 0..14 | 0..4 identical lists (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out); 5..14 (send to one effect) -> 3 | L for 0..4, A for 5..14 |
| `osc_N_unison_voices` | `oscN.unison.voices` | 1..16 | round | L |
| `osc_N_unison_detune` | `oscN.unison.detune` | 0..10 | `x*x` | L |
| `osc_N_detune_power` | `oscN.unison.detune_curve` | -5..5 | `x` | L |
| `osc_N_stereo_spread` | `oscN.unison.spread` | 0..1 | `x*100` | L |
| `osc_N_unison_blend` | `oscN.unison.blend` | 0..1 | `x*100` | L |
| `osc_N_stack_style` | `oscN.unison.stack` | 0..10 | round, same 11 names | L |
| `osc_N_spectral_morph_type` | `oscN.morph.type` | 0..11 | round, same 12 names | L |
| `osc_N_spectral_morph_amount` | `oscN.morph.amount` | 0..1 | `x*100` | L |
| `osc_N_spectral_morph_spread` | `oscN.morph.spread` | -0.5..0.5 | `x/0.005` | L |
| `osc_N_distortion_type` | `oscN.warp.type` | 0..12 | engine index -> ours via inverse of `{0..8,10,11}`; 9 (FM from sample) and 12 (RM from sample) -> 0 None | L for 0..8,10,11; A for 9, 12 |
| `osc_N_distortion_amount` | `oscN.warp.amount` | 0..1 | `x*100` | L |
| `osc_N_distortion_phase` | `oscN.warp.phase` | 0..1 | `x*360` | L |
| `osc_N_distortion_spread` | `oscN.warp.spread` | -0.5..0.5 | `x/0.005` | L |
| `osc_N_detune_range` | none (engine default 2 semitones) | 0..48 | if `x != 2` report | U |
| `osc_N_midi_track` | none (fixed 1) | 0..1 | if 0 (fixed-pitch oscillator) report | U |
| `osc_N_spectral_unison` | none (fixed 1) | 0..1 | if 0 report | U |
| `osc_N_transpose_quantize` | none (fixed 0) | 0..8191 | if != 0 report | U |
| `osc_N_smooth_interpolation`, `osc_N_view_2d` | none (quality tier / UI) | | | I |
| `wavetables[N-1]` | `oscN.wavetable` | | 1.3.1 | see 1.3.1 |

#### 1.2.3 Noise (= Vital "sample" oscillator, engine built-in white noise) and Sub

| Vital setting | Our key | Vital range | File -> ours | Class |
|---------------|---------|-------------|--------------|-------|
| `sample_on` | `noise.on` | 0..1 | if `sample.name == "White Noise"`: round; if a custom sample is stored: 0 (a noise burst would be wrong) and report | L / A |
| `sample_level` | `noise.level` | 0..1 | `x*x*100` | L |
| `sample_pan` | `noise.pan` | -1..1 | `x*100` | L |
| `sample_destination` | `noise.destination` | 0..14 | as `osc_N_destination` | L / A |
| `sample` object (custom audio) | none | | 1.3.3 | U |
| `sample_random_phase`, `sample_keytrack`, `sample_loop`, `sample_bounce`, `sample_transpose`, `sample_tune`, `sample_transpose_quantize` | none | | reported only if a custom sample is used, else ignored | U / I |
| `sub_on`, `sub_direct_out`, `sub_transpose`, `sub_transpose_quantize`, `sub_tune`, `sub_level`, `sub_pan`, `sub_waveform` | none (no sub oscillator) | | if `sub_on = 1`: ONE report item "Sub oscillator" with level, waveform, transpose; no nearest fit (all 3 oscillators carry their own content) | U |

#### 1.2.4 Filters (F: `filter_1_`, `filter_2_`, `filter_fx_` -> `filter1.`, `filter2.`, `fxfilter.`)

| Vital setting | Our key | Vital range | File -> ours | Class |
|---------------|---------|-------------|--------------|-------|
| `F_on` | `F.on` | 0..1 | round | L |
| `F_model` | `F.model` | 0..7 | round; same 8 names (Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser) | L |
| `F_style` | `F.style` | 0..9 | round, clamp 0..5 | C (A when > 5) |
| `F_cutoff` | `F.cutoff` | 8..136 (note) | Hz via inverse `noteFromHz`, clamp 20..20000 | C |
| `F_resonance` | `F.resonance` | 0..1 | `x*100` | L |
| `F_drive` | `F.drive` | 0..20 | dB, `x` | L |
| `F_blend` | `F.blend` | 0..2 | `x/0.02` | L |
| `F_mix` | `F.mix` | 0..1 | `x*100` | L |
| `F_keytrack` | `F.keytrack` | -1..1 | `x*100` | L |
| `F_blend_transpose` | `F.comb_offset` | 0..84 | `x` | L |
| `F_formant_x`, `F_formant_y` | `F.formant.x`, `.y` | 0..1 | `x*100` | L |
| `F_formant_resonance` | `F.formant.resonance` | 0.3..1 | `x*100` | L |
| `F_formant_spread` | `F.formant.spread` | -1..1 | `x*100` | L |
| `F_formant_transpose` | `F.formant.transpose` | -12..12 | `x` | L |
| `filter_1_filter_input`, `filter_2_filter_input` | `filter.routing` | 0..1 each | (0,0)->0 Parallel; (0,1)->1 Series 1 to 2; (1,0)->2 Series 2 to 1; (1,1)->0 and report (loop) | L / A for (1,1) |
| `filter_N_osc1_input` ... `_sample_input` (pre-0.5.0 routing) | `oscN.destination` | | migration in 1.1 | L |
| `filter_fx_*_input` | none | | | I |

#### 1.2.5 Envelopes (N = 1..6)

| Vital setting | Our key | Vital range | File -> ours | Class |
|---------------|---------|-------------|--------------|-------|
| `env_N_delay`, `env_N_hold` | `envN.delay`, `envN.hold` | 0..1.4142 | `x^4*1000` ms (max 4000) | L |
| `env_N_attack`, `env_N_decay`, `env_N_release` | `envN.attack`, `.decay`, `.release` | 0..2.37842 (32 s) | `x^4*1000` ms, clamp 10000 | C |
| `env_N_sustain` | `envN.sustain` | 0..1 | `x*100` | L |
| `env_N_attack_power`, `env_N_decay_power`, `env_N_release_power` | `envN.attack_curve`, `.decay_curve`, `.release_curve` | -20..20 | `x` | L |

#### 1.2.6 LFOs (N = 1..8) and random LFOs (M = 1..4)

| Vital setting | Our key | Vital range | File -> ours | Class |
|---------------|---------|-------------|--------------|-------|
| `lfo_N_frequency` | `lfoN.rate` | -7..9 (log2 Hz) | `2^x` Hz, clamp 0.01..200 | C |
| `lfo_N_sync` | `lfoN.sync` | 0..4 | round; same 5 names | L |
| `lfo_N_tempo` | `lfoN.division` | 0..12 | round; same 13 names | L |
| `lfo_N_sync_type` | `lfoN.sync_type` | 0..5 | round; same 6 names | L |
| `lfo_N_phase` | `lfoN.phase` | 0..1 | `x*360` | L |
| `lfo_N_fade_time` | `lfoN.fade_in` | 0..8 s | `x*1000` ms | L |
| `lfo_N_delay_time` | `lfoN.delay` | 0..4 s | `x*1000` ms | L |
| `lfo_N_smooth_mode` | `lfoN.smooth` | 0..1 | round | L |
| `lfo_N_smooth_time` | `lfoN.smooth_time` | -10..4 (log2 s) | `2^x*1000` ms, clamp 1..1000 | C |
| `lfo_N_stereo` | `lfoN.stereo` | -0.5..0.5 | `x*360` deg | L |
| `lfo_N_keytrack_transpose` | `lfoN.keytrack_transpose` | -60..36 | round | L |
| `lfo_N_keytrack_tune` | `lfoN.keytrack_tune` | -1..1 | `x*100` | L |
| `lfos[N-1]` shape | `lfoN.shape` (+ `content.lfo_shapes`) | | 1.3.2 | see 1.3.2 |
| `random_M_style` | `rlfoM.style` | 0..3 | round; same 4 names (Perlin, Sample and hold, Sine interpolate, Lorenz attractor) | L |
| `random_M_frequency` | `rlfoM.rate` | -7..9 | `2^x` Hz, clamp 0.01..200 | C |
| `random_M_sync`, `_tempo`, `_sync_type`, `_stereo` | `rlfoM.sync`, `.division`, `.sync_type`, `.stereo` | 0..4, 0..12, 0..1, 0..1 | round | L |
| `random_M_keytrack_transpose`, `_keytrack_tune` | `rlfoM.keytrack_transpose`, `.keytrack_tune` | as LFO | as LFO | L |

#### 1.2.7 Macros and modulation matrix

| Vital | Our key | Vital range | File -> ours | Class |
|-------|---------|-------------|--------------|-------|
| `macro_control_M` | `macro.M` | 0..1 | `x*100` | L |
| `modulations[i].source` | `modNN.source` (NN = i+1) | strings | `env_k`->env k, `lfo_k`->lfo k, `random_k`->rlfo k, `macro_control_k`->macro k, `velocity`, `note`, `aftertouch`, `pitch_wheel`, `mod_wheel`, `random` -> same-named choices (28 sources, `modSourceTable`) | L |
| `modulations[i].source` = `lift`, `slide`, `stereo`, `note_in_octave`, unknown | none | | whole connection dropped; item shows source, destination, amount | U |
| `modulations[i].destination` | `modNN.destination` | control name | inverse lookup in the 730-row table; target must be flagged modulatable (all 269 are); choice = `modDestinationChoiceOf` | L |
| `modulations[i].destination` = a control we have no key for, or one that is not modulatable in ours | none | | whole connection dropped, reported (e.g. `sub_level`, `voice_amplitude`, `osc_N_detune_range`) | U |
| `modulation_N_amount` | `modNN.amount` | -1..1 | `x*100` % | L |
| `modulation_N_power` | `modNN.power` | -10..10 | `x` (the curve) | L |
| `modulation_N_bipolar`, `_stereo`, `_bypass` | `modNN.bipolar`, `.stereo`, `.bypass` | 0..1 | round | L |
| line-map curve of a connection (custom curve in newer Vital) | none (PARAMETERS "Line-map curve") | | if a connection carries one: kept linear, item `approximated` | A |

Rules: connection order = slot order; a slot whose source / destination is dropped stays empty (`source = 0`) and the following connections keep their slot numbers (so the relative order is unchanged). Two connections with identical source and destination are kept (Vital forbids them; a hand-made file may contain them).

#### 1.2.8 Voice, portamento, legato, bend, tuning, global

| Vital setting | Our key | Vital range | File -> ours | Class |
|---------------|---------|-------------|--------------|-------|
| `volume` | `master.volume` | 0..7399.44 | `sqrt(x)-80` dB, `<= -60` -> -60, clamp 6 | C |
| `polyphony` | `master.polyphony` | 1..32 | round | L |
| `velocity_track` | `master.velocity_track` | -1..1 | `x*100` | L |
| `voice_transpose` | `master.transpose` | -48..48 | round | L |
| `voice_tune` | `master.tune` | -1..1 | `x*100` ct | L |
| `legato` | `master.legato` | 0..1 | round | L |
| `portamento_time` | `master.portamento.time` | -10..4 (log2 s) | `x <= -9.9999 ? 0 : 2^x*1000` ms, clamp 8000 | C |
| `portamento_slope` | `master.portamento.slope` | -8..8 | `x` | L |
| `portamento_force` | `master.portamento.always` | 0..1 | round | L |
| `portamento_scale` | `master.portamento.scaled` | 0..1 | round | L |
| `pitch_bend_range` | `master.bend_range` | 0..48 | semitones, clamp 24 | C |
| `voice_amplitude` | folded into `master.volume` | 0..1 | if `x != 1`: `master.volume += 20*log10(x)` (floor -60) | A |
| `voice_priority`, `voice_override` | none (voice stealing is "oldest", README) | | log only | I |
| `oversampling`, `stereo_mode`, `stereo_routing`, `mpe_enabled`, `bypass`, `beats_per_minute`, `view_spectrogram`, `pitch_wheel`, `mod_wheel` | none (tier / host / UI / live state) | | log only | I |
| `settings.tuning` | none | | scale name + size reported | U |

#### 1.2.9 Effects (each has `<fx>_on` -> `<fx>.on`, round, class L)

| Vital setting | Our key | Vital range | File -> ours | Class |
|---------------|---------|-------------|--------------|-------|
| `effect_chain_order` | `fx.order.1` .. `fx.order.9` | 0..362879 | permutation decode (1.1), `fx.order.(p+1) = order[p]` | L (out of range / non-integer: identity order + report -> A) |
| `chorus_dry_wet`, `_mod_depth`, `_spread` | `chorus.mix`, `.depth`, `.filter_spread` | 0..1 | `x*100` | L |
| `chorus_feedback` | `chorus.feedback` | -0.95..0.95 | `x*100` | L |
| `chorus_frequency` | `chorus.rate` | -6..3 | `2^x` Hz, clamp 0.02..8 | C |
| `chorus_sync`, `chorus_tempo`, `chorus_voices` | `chorus.sync`, `.division`, `.voices` | 0..3, 0..10, 1..4 | round | L |
| `chorus_delay_1`, `chorus_delay_2` | `chorus.delay1`, `.delay2` | -10..-5.64 | `2^x*1000` ms, clamp 1..20 | C |
| `chorus_cutoff` | `chorus.filter_cutoff` | 8..136 | Hz (inverse note), clamp 20..20000 | C |
| `compressor_mix`, `_attack`, `_release` | `compressor.mix`, `.attack`, `.release` | 0..1 | `x*100` | L |
| `compressor_enabled_bands` | `compressor.bands` | 0..3 | round | L |
| `compressor_{low,band,high}_lower_threshold`, `_upper_threshold`, `_gain` | `compressor.{low,mid,high}.lower_threshold`, `.upper_threshold`, `.gain` | -80..0, -30..30 | `x` dB | L |
| `compressor_{low,band,high}_lower_ratio` | `compressor.{low,mid,high}.lower_ratio` | -1..1 | `x*100` | L |
| `compressor_{low,band,high}_upper_ratio` | `compressor.{low,mid,high}.upper_ratio` | 0..1 | `x*100` | L |
| `compressor_low_band_unused` | none | | | I |
| `delay_dry_wet`, `delay_feedback`, `delay_filter_spread` | `delay.mix`, `.feedback`, `.filter_spread` | 0..1, -1..1 | `x*100` | L |
| `delay_frequency`, `delay_aux_frequency` | `delay.time`, `delay.time2` | -2..9 | `1000/2^x` ms, clamp 2..4000 | C |
| `delay_sync` | `delay.sync` | 0..3 | round | L |
| `delay_tempo`, `delay_aux_tempo` | `delay.division`, `delay.division2` | 4..12 | `round(x)-4` (0..8, "4 bars" ... "1/64") | L |
| `delay_aux_sync` | none (`delay.sync` governs both) | 0..3 | if it differs from `delay_sync`: report | A |
| `delay_style` | `delay.style` | 0..3 | round | L |
| `delay_filter_cutoff` | `delay.filter_cutoff` | 8..136 | Hz, clamp 20..20000 | C |
| `distortion_type`, `_filter_order` | `distortion.type`, `.filter_order` | 0..5, 0..2 | round | L |
| `distortion_drive` | `distortion.drive` | -30..30 | `x` dB | L |
| `distortion_mix`, `_filter_resonance` | `distortion.mix`, `.filter_resonance` | 0..1 | `x*100` | L |
| `distortion_filter_cutoff` | `distortion.filter_cutoff` | 8..136 | Hz, clamp | C |
| `distortion_filter_blend` | `distortion.filter_blend` | 0..2 | `x/0.02` | L |
| `eq_{low,band,high}_mode` | `eq.{low,mid,high}.mode` | 0..1 | round | L |
| `eq_{low,band,high}_cutoff` | `eq.{low,mid,high}.cutoff` | 8..136 | Hz, clamp 20..20000 | C |
| `eq_{low,band,high}_gain` | `eq.{low,mid,high}.gain` | -15..15 | `x` dB | L |
| `eq_{low,band,high}_resonance` | `eq.{low,mid,high}.resonance` | 0..1 | `x*x*100` | L |
| `flanger_dry_wet` | `flanger.mix` | 0..0.5 | `x/0.005` | L |
| `flanger_feedback`, `_mod_depth` | `flanger.feedback`, `.depth` | -1..1, 0..1 | `x*100` | L |
| `flanger_frequency` | `flanger.rate` | -5..2 | `2^x` Hz, clamp 0.04..4 | C |
| `flanger_sync`, `_tempo` | `flanger.sync`, `.division` | 0..3, 0..10 | round | L |
| `flanger_center` | `flanger.center` | 8..136 | Hz, clamp 20..16000 | C |
| `flanger_phase_offset` | `flanger.phase_offset` | 0..1 | `x*360` | L |
| `phaser_dry_wet`, `_feedback` | `phaser.mix`, `.feedback` | 0..1 | `x*100` | L |
| `phaser_frequency` | `phaser.rate` | -5..2 | `2^x` Hz, clamp 0.04..4 | C |
| `phaser_sync`, `_tempo` | `phaser.sync`, `.division` | 0..3, 0..10 | round | L |
| `phaser_mod_depth` | `phaser.depth` | 0..48 | `x` semitone | L |
| `phaser_center` | `phaser.center` | 8..136 | Hz, clamp 20..16000 | C |
| `phaser_blend` | `phaser.blend` | 0..2 | `x/0.02` | L |
| `phaser_phase_offset` | `phaser.phase_offset` | 0..1 | `x*360` | L |
| `reverb_dry_wet`, `_size` | `reverb.mix`, `.size` | 0..1 | `x*100` | L |
| `reverb_decay_time` | `reverb.decay` | -6..6 | `2^x*1000` ms, clamp 20..30000 | C |
| `reverb_delay` | `reverb.predelay` | 0..0.3 s | `x*1000` ms | L |
| `reverb_pre_low_cutoff`, `_pre_high_cutoff`, `_low_shelf_cutoff`, `_high_shelf_cutoff` | `reverb.pre_low_cut`, `.pre_high_cut`, `.low_shelf_cutoff`, `.high_shelf_cutoff` | 0..128 | Hz (inverse note), clamp 20..12000 | C |
| `reverb_low_shelf_gain`, `_high_shelf_gain` | `reverb.low_shelf_gain`, `.high_shelf_gain` | -6..0 | `x` dB | L |
| `reverb_chorus_amount` | `reverb.chorus_amount` | 0..1 | `x*x*100` | L |
| `reverb_chorus_frequency` | `reverb.chorus_rate` | -8..3 | `2^x` Hz, clamp 0.01..8 | C |

#### 1.2.10 Quality-tier and tier-cap notes

Import never applies tier caps (unison, voices, FX bypass): the stored values are the preset's. The report sheet adds one info line when this device's tier (`Quality`) plays less than the preset asks (voices, unison, bypassed effects), computed from `Quality::maxVoices`, `maxUnison`, FX bypass; it is not an item in the lists.

#### 1.2.11 Vital controls without a one-to-one key (64) and their fate

| Controls | Fate |
|----------|------|
| `effect_chain_order`, `pitch_bend_range`, `filter_1_filter_input`, `filter_2_filter_input` (4) | special parameters (1.2.9, 1.2.8, 1.2.4) |
| `filter_{1,2,fx}_osc1_input`, `_osc2_input`, `_osc3_input`, `_sample_input`, `filter_fx_filter_input` (13) | legacy routing migration (1.1) or I |
| `sub_*` (8), `sample_{random_phase,keytrack,loop,bounce,transpose,tune,transpose_quantize}` (7) | U / I (1.2.3) |
| `osc_N_{detune_range,midi_track,spectral_unison,transpose_quantize,smooth_interpolation,view_2d}` (18) | U / I (1.2.2) |
| `voice_amplitude`, `delay_aux_sync` (2) | A (1.2.8, 1.2.9) |
| `voice_priority`, `voice_override`, `oversampling`, `stereo_mode`, `stereo_routing`, `mpe_enabled`, `bypass`, `beats_per_minute`, `view_spectrogram`, `pitch_wheel`, `mod_wheel`, `compressor_low_band_unused` (12) | I |

### 1.3 Embedded content

#### 1.3.1 Wavetables (osc 1..3)

A `.vital` file contains only the SOURCE description of each wavetable (components), never the rendered frames [verified: `stateToJson` writes groups only]. To get audio the importer must render it, so the renderer is part of the import:

| Step | Rule |
|------|------|
| 1 Parse | `WavetableDesc { name, author, removeAllDc, fullNormalize, groups[] -> components[] -> keyframes[] }`, limits of 1.1; unknown `type` -> that component skipped, `unsupportedComponent` |
| 2 Built-in shortcut | If the render is a single frame (or all frames equal) AND matches a built-in single-cycle table (Sine, Saw, Square, Triangle, Pulse 25%) with normalised RMS error <= 0.01: `oscN.wavetable` = that built-in, nothing stored (L, saves content) |
| 3 Render | Own renderer `renderVitalWavetable(desc) -> frames[F][2048]` (F = last keyframe position + 1, <= 257) following the render model of 1.1: Wave Source (keyframe interpolation none / linear / cubic, time / frequency mode), Line Source (own power-curve line renderer, shared with 1.3.2), Audio File Source (windowing, fade styles, phase styles, normalisation), Shepard Tone Source and the six modifiers (Phase Shift, Wave Window, Frequency Filter, Slew Limiter, Wave Folder, Wave Warp), then `removeAllDc` / `fullNormalize`. Clean-room: the implementer writes from this document and black-box reference frames (Annex C.2), not from `core/engine-vital/upstream/src/common/wavetable/` (Annex B). Every component type is required; the golden test per component is the definition of done. |
| 4 Store | Frames as 16-bit (Annex A.4: `.sbwt` bytes are defined by the Phase 4 content tools; the importer calls `WavetableStore::add`), SHA-256 of the rendered frames = identity (dedupe: the same table in 100 presets is stored once). Written into the per-user ContentPack `user.imported-vital` (`kind: "wavetables"`, `license: "user-supplied"`, `attribution` = original author; `contentpack.json` rewritten atomically; pack is deletable / restorable by the host like any pack, ARCHITECTURE section 6). |
| 5 Reference | `.sbpreset` gets `content.wavetables[]: {osc, pack, file, sha256, name, author, frames}`; `oscN.wavetable` = the new choice "Preset table" (A.4, core change C1). Missing pack at load: built-in Saw + `missing_content` flag (ARCHITECTURE 5.2); the report is unaffected. |
| 6 Engine cannot represent it | `ImportOptions.caps.customWavetables == false` (older engine, or a tier that cannot hold the table), or the render fails: nearest built-in by spectral distance (below), class `approximated`, reason `nearestWavetable`, item shows Vital table name + chosen built-in + distance. Frame position keeps its fraction. |
| Tier | Frame count is NOT reduced at import (full F stored); the tier caps frames at load time. |

Nearest built-in: for p in {0, 0.25, 0.5, 0.75, 1} of the table, magnitudes of the first 64 harmonics in dB (floor -90), mean absolute difference vs the same positions of each of the 13 built-ins; the smallest wins; ties -> lower index. Deterministic.

Stand-alone `.vitaltable`: same parser and renderer, result is a wavetable pack entry only (no preset).

#### 1.3.2 Custom LFO shapes (lfo 1..8)

| Step | Rule |
|------|------|
| 1 Parse | `LfoShape { name, points[<=100]{x,y}, powers[], smooth }`; sanitising: `num_points` clamped to `[2, min(100, powers.size(), points.size()/2)]`, x clamped to [0,1] and made non-decreasing, y clamped to [0,1], power clamped to +-50 |
| 2 Built-in shortcut | Render 256 samples with the line renderer (power curve per segment, optional smoothing between points; semantics fixed by black-box golden, C.2); if normalised RMS error <= 0.01 against one of the 12 built-ins: `lfoN.shape` = that built-in, nothing stored (L) |
| 3 Store | `content.lfo_shapes[]: {lfo, name, smooth, points:[x,y,power]...}` inside the `.sbpreset` (<= 100 points -> about 3 KB; 8 shapes fit the 1 MiB limit trivially); `lfoN.shape` = new choice "Custom" (A.4, core change C2) |
| 4 Engine cannot represent it | `caps.customLfoShapes == false`: nearest built-in by L2 error over 256 samples, `approximated / nearestLfoShape` with the error value; the LFO keeps rate, phase, sync |
| Note | An LFO that is not used (no connection with `lfo_N` as source) and has the default shape is not reported |

Stand-alone `.vitallfo`: same code, result = a shape for the user (no engine slot; used by the LFO editor's shape library, 05-envelopes-lfos.md, GAP there).

#### 1.3.3 Sample

`settings.sample` with `name == "White Noise"` (engine default) = our noise oscillator (L). A custom sample (any other name or length) cannot be played (no sample oscillator exposed): `noise.on = 0`, one `dropped / customSample` item with name, length, sample rate. The decoded audio is NOT kept (could be offered later with a sample oscillator; not a hidden branch: the content is simply not part of the model today, PARAMETERS "Not exposed").

### 1.4 Import report

```cpp
enum class ItemKind : uint8_t { imported, approximated, dropped, ignored };
enum class Reason : uint8_t {
  exact, clamped, rounded, enumRemapped, legacyMigration, defaulted, typeMismatch, truncated,
  nearestWavetable, nearestLfoShape, unsupportedComponent, renderFallback,
  noEngineEquivalent, customSample, subOscillator, tuningUnsupported,
  modSourceUnsupported, modDestinationUnsupported, effectOrderInvalid, limitExceeded, tierNote };
enum class Fidelity : uint8_t { exact, close, degraded };   // sheet colour

struct ReportItem {                 // approximated / dropped / ignored: full strings
  ItemKind    kind;
  Reason      reason;
  std::string area;                 // "osc1", "filter2", "env3", "lfo4", "mod07", "fx.reverb", "voice", "content", "meta"
  std::string vitalName;            // "osc_1_distortion_type" or "modulations[6]"
  std::string vitalValue;           // as read, text ("12", "lift -> filter_1_cutoff amount 0.4")
  std::string ourKey;               // "osc1.warp.type" or ""
  std::string ourValue;             // value after import, display text ("None") or ""
  std::string detail;               // short English sentence, stable id for localisation: "warp_from_sample_unsupported"
};
struct ImportedEntry { uint16_t row; float vitalValue; float ourValue; };   // compact, 730 x 12 B; text built on demand

struct ImportReport {
  std::vector<ImportedEntry> imported;
  std::vector<ReportItem>    items;                // approximated + dropped + ignored, in file order
  int numImported = 0, numApproximated = 0, numDropped = 0, numIgnored = 0, numDefaulted = 0;
  int vitalSettingsSeen = 0, vitalUnknownKeys = 0;
  Fidelity fidelity = Fidelity::exact;             // degraded: any dropped item with audible effect (sub on, custom sample, modulation dropped, unsupported component, warp from sample on an audible oscillator); close: only approximated / clamped; exact: neither
  bool truncated = false;                           // > 2000 items
  std::string vitalVersion;
  std::string toJson() const;                      // schema "synthyboy-import-report" v1, deterministic
};
```

* Sheet = the three lists (2.3); `ignored` is log-only.
* Persisted: `<preset>.import-report.json` next to the `.sbpreset` (extension differs, so `scanPresetDirectory` ignores it), and an append-only `vital-import.log` (JSON lines, 1 MiB x 2 files rotation) in the host-provided log directory: file name (not path), SHA-256 of the source file, version, counts, items. Local files only; the plugin has no network code (Annex B).
* Flagging in the loaded preset: `.sbpreset` `import.approximated` lists up to 64 of our keys that were approximated / clamped; the UI draws an amber ring on those knobs until the user edits them, and an amber "approx." badge next to the preset name opens the report.

## 2. Screen design

All sheets are modal bottom sheets (phone portrait), one primary action each, targets >= 44 px. The Kick browser (list, search, folders) is excluded; the sheets below are owned by the plugin UI and also hosted by Kick, which supplies files.

### 2.1 Entry points

| Entry | Platform | What happens |
|-------|----------|--------------|
| Plugin menu `...` > `Import Vital preset...` | all | OS file picker (`.vital`, multiple selection, folders on desktop / Android); iOS: document picker with the imported UTI for `vital` |
| Drag one or more files / a folder onto the plugin window | desktop | drop target outlines the window; same flow as the picker |
| Open-in / share sheet ("Open in SynthyBoy") | iOS, Android | Kick (the app) registers the `.vital` document type and forwards file URL + bytes via the plugin API (section 5) |
| Kick preset browser action `Import Vital preset(s)` | Kick | Kick shows its own picker and calls the plugin API; the result is a list of imported `.sbpreset` files Kick adds to its browser |

### 2.2 Phone portrait: progress

```
+------------------------------+
| Import Vital preset          |
|                              |
|  My Bass.vital               |
|  [=========>        ] 55 %   |
|  Rendering wavetable 2/3     |
|                              |
|                    [ Cancel ]|
+------------------------------+
```

Steps shown: Reading, Mapping, Rendering wavetables, Finishing. Typical import < 1 s without wavetables; the bar is determinate (steps weighted by frames x components). Cancel stops at the next component boundary; nothing is written.

### 2.3 Phone portrait: report sheet

```
+------------------------------+
| My Bass          by Someone  |
| Vital 1.5.5 -> SynthyBoy     |
| (o) 702 ok  (~) 14  (x) 6    |  chips = filter: Imported / Approximated / Dropped
|------------------------------|
| APPROXIMATED (14)       [v]  |
|  Osc 2 Warp type             |
|   Vital: FM from sample (9)  |
|   Now:   None                |
|   Reason: not available      |
|  Filter 1 Cutoff             |
|   Vital: 29.2 kHz  Now: 20 k |
|   Reason: above our maximum  |
|  ...                         |
| DROPPED (6)             [v]  |
|  Sub oscillator  level 0.8   |
|   Reason: no sub oscillator  |
|  Mod 07 lift -> Filter 1 Cut |
|   Reason: source not exposed |
| IMPORTED (702)          [>]  |  collapsed; tap = list by area
|------------------------------|
| This device plays 16 of 24   |  tier note (info line)
| voices.                      |
|------------------------------|
| [Audition]   [Copy report]   |
| [ Save as... ]  [ Load only ]|
+------------------------------+
```

* Header colour by `fidelity`: green exact, amber close, red degraded. Row shows the human name of our parameter (`ParamInfo.name`), Vital's original value and name (small), reason text.
* `Audition`: loads the result as a temporary patch (lock-free swap, ARCHITECTURE 3.1) and plays the on-screen keyboard; leaving the sheet restores the previous patch unless `Load only` / `Save as` was used.
* `Load only`: patch becomes the current patch, not saved (marked modified).
* `Copy report`: report text to the clipboard (plain text) / share sheet.
* Content items (wavetable, LFO shape) have their own section with a small preview (wavetable 2D, LFO line) and the decision: "Stored as user wavetable" or "Replaced by Saw (nearest)".

### 2.4 Save sheet (`Save as...`)

```
+------------------------------+
| Save imported preset         |
| Name      [My Bass         ] |
| Author    [Someone         ] |  original author, editable, never removed silently
| Category  (Bass)(Lead)(Pad)  |
|           (Pluck)(Keys)(FX)  |  pre-selected from Vital style
| Style note: Vital "Percussion"|
|   mapped to Pluck            |
| Tags      imported, vital    |
| Licence   Imported (user-    |
|           supplied)          |
| [ ] Keep the import report   |  on by default
| Save to: User / Imported     |
|                  [Save]      |
+------------------------------+
```

* Fields are limited to 255 bytes (counter turns amber at 240). File name = sanitised name (`/ \ : * ? " < >` and control characters removed, leading dots removed, <= 100 bytes, reserved device names suffixed `_`), folder = user preset directory given by the host, subfolder `Imported/<Category>/`.
* Metadata written: `name`, `author` (original), `license` = "Imported, user-supplied; original terms apply", `category` (mapped), `tags` = `imported,vital,vital-style:<orig>,src:<first 12 hex of source SHA-256>`, `description` (comments, 250 B).
* A preset saved from here is never written into the repository's `content/` tree (Annex B lint).

### 2.5 Failure and empty sheets

```
+------------------------------+
| Could not import             |
| "x.vital" is not a Vital     |
| preset (not valid JSON).     |
| [ Close ]                    |
+------------------------------+
```

Messages per status: see section 4.

### 2.6 Batch import (folder, or several files)

```
+------------------------------+
| Import 128 presets           |
| [=========>          ] 61/128|
| ok 58  approx 17  failed 2   |
|------------------------------|
| Duplicates:                  |
| (o) Skip same file           |  default
| ( ) Replace                  |
| ( ) Keep both                |
| ( ) Ask each time            |
| Folder -> category           |
| [x] Use Vital style          |
| Unknown style -> [Lead v]    |
|           [Cancel]           |
+------------------------------+
```

* Files are processed one at a time on a worker thread (UI stays live); after the run a summary sheet lists rows `name | status icon | counts`; tap a row opens its report sheet; `Save all` saves every row with `Save as` defaults (no per-file prompts); failed files are listed with the reason and stay untouched.
* Duplicates: identical source (SHA-256 tag `src:`) -> "already imported"; same name, different content -> policy above (Keep both appends ` (2)`, ` (3)`); a wavetable already present (same SHA-256) is never stored twice regardless of policy.
* Subfolders: recursive, depth <= 8, at most 5000 files per batch (a counter, not a silent cut: the sheet says "5000 of N, import the rest next"); symbolic links are not followed; files over 32 MiB are listed as `tooLarge`.

### 2.7 Landscape / tablet / MPC

```
+-------------------------------+--------------------------------+
| My Bass  by Someone           | Save                           |
| (o)702  (~)14  (x)6           | Name [..........]              |
| Approximated | Dropped | Imp. | Author [........]               |
|  Osc 2 Warp type  FM from ... | Category (Bass)(Lead)...        |
|  Filter 1 Cutoff  29.2k > 20k | Tags ...                        |
|  ...  (scrolls)               | [Audition] [Load] [Save]        |
|                               | mini keyboard                   |
+-------------------------------+--------------------------------+
```

Left = report with three tabs (the lists are tabs, not sections), right = save form + audition. MPC: the same two columns, controlled by the touch screen; the file picker is the system's or Kick's.

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| Menu `Import Vital preset...` | Tap | File picker | Kick browser action (section 5) |
| Window / sheet drop area | Drop file(s) / folder (desktop) | Starts import | Menu item |
| Progress `Cancel` | Tap | Stops at next component boundary, nothing written | Back / Escape |
| Report chip (Imported / Approximated / Dropped) | Tap | Filters / jumps to the list | Segmented tabs (landscape) |
| Report row | Tap | Expands: full Vital name, raw value, our key, our value, reason | Row chevron |
| Report row (approximated parameter) | Long press | Jumps to the parameter's page after `Load only` (opens its knob) | `Show in synth` button in the expanded row |
| `Audition` | Tap (toggle), then play keyboard | Temporary load; second tap or leaving the sheet restores | `Stop audition` button |
| `Copy report` | Tap | Plain text to clipboard / share sheet | - |
| `Save as...` | Tap | Save sheet (2.4) | - |
| Category chips | Tap | Selects category | Picker list |
| `Load only` | Tap | Patch becomes current, unsaved | - |
| Batch duplicate policy | Radio | Applies to the run | - |
| Batch summary row | Tap | Opens that report | - |
| Preset name amber badge `approx.` | Tap | Reopens the stored report | Preset menu `Import report...` |
| Amber knob ring | Edit knob | Ring disappears for that parameter | Long-press menu `Clear flag` |

## 4. States and edge cases

| Situation | Behaviour |
|-----------|-----------|
| `notVital` (valid JSON without `settings`, or not JSON) | Failure sheet "not a Vital preset"; nothing stored |
| `malformed` (truncated JSON, depth limit) | "File damaged: could not read it" + byte offset in the log |
| `tooLarge` (> 32 MiB) | "File too large" |
| `budgetExceeded` | "Preset too complex for this device"; the memory limit may be lowered by the host on low-memory devices |
| `cancelled` | Silent close |
| Partly readable (e.g. one wavetable broken) | Imports the rest; the broken part is a `dropped` item; fidelity `degraded` |
| File has 0 modulations / default patch | Imports; header "702 ok" with nothing else |
| Preset from Vital < 0.5.0 | Migration items shown as `imported` with a small "converted from older format" note |
| Preset needs more than the device's tier plays | Tier info line; no change to values |
| Wavetable pack cannot be written (disk full, iOS sandbox) | The preset still saves; wavetable falls back to nearest built-in (`approximated`), message "could not store the wavetable" |
| Wavetable already stored | Reused (SHA-256), no duplicate file |
| Name collision | Policy of 2.6; single import asks once (Replace / Keep both / Cancel) |
| Names with emoji / non-Latin | Kept (UTF-8); file name sanitised; invalid UTF-8 replaced |
| Author empty | `author = ""`; sheet shows "unknown author" and does not invent one |
| Original author in `.sbpreset` | Always `author` = Vital's author; the licence line says the original terms apply |
| Import while playing | Import runs off the audio thread; audition swaps lock-free; no audio dropout allowed (tested with the real-time allocation audit, Annex C) |
| Same file imported twice | "Already imported" (SHA-256 tag) unless the user picks Replace |
| `Template` / unknown style | Save sheet asks for a category |
| iOS file access | Security-scoped URL opened by the host, bytes passed in; the plugin never touches paths outside the directories the host gives |
| Report > 2000 items | Truncated flag, sheet says "list shortened, see log" |

## 5. Kick remote pages (just the import action)

Import is an action, not a parameter: it has no key in `docs/params.json` and does not appear on a remote-control page. Kick's preset browser gets one command and calls the plugin (or links `synthyboy_core` and calls the same functions):

| Action | Kick -> plugin | Plugin -> Kick |
|--------|----------------|----------------|
| `Import Vital preset(s)` | file bytes (or file path + security-scoped access granted by Kick), display name, `ImportOptions` subset (duplicate policy, target category rule) | per file: status, `Fidelity`, counts, `.sbpreset` bytes (and wavetable pack entries added), report JSON |
| `Show import report` | preset path | report JSON (`<preset>.import-report.json`) for Kick's UI |

Sketch of the extension (CLAP custom extension id `com.safian.synthyboy.vital-import/1`, equivalent C functions for the VST3 / Kick-linked case):

```c
typedef struct synthyboy_vital_import_v1 {
  /* returns a handle to a result; free with release(). thread: main or worker, never audio. */
  void* (*import_bytes)(const clap_plugin_t*, const uint8_t* data, size_t size,
                        const char* display_name_utf8, const synthyboy_import_options_t* options);
  int         (*status)(void* result);             /* ImportStatus */
  int         (*fidelity)(void* result);           /* Fidelity */
  const char* (*report_json)(void* result);
  const char* (*preset_json)(void* result);        /* .sbpreset text, ready to write */
  int         (*commit_content)(void* result);     /* writes wavetable pack entries via the host-provided store */
  void        (*release)(void* result);
} synthyboy_vital_import_v1;
```

## 6. Coverage check

All 880 keys of `docs/params.json` are import targets or are deliberately left at the default:

| Key group (count) | Source in the file | Section |
|-------------------|--------------------|---------|
| `master.*` (11): volume, polyphony, bend_range, velocity_track, transpose, tune, legato, portamento x4 | 1.2.8 | 1.2.8 |
| `oscN.*` (72, 69 one-to-one + 3 `wavetable`) | 1.2.2, 1.3.1 | 1.2.2, 1.3.1 |
| `noise.*` (4) | 1.2.3 | 1.2.3 |
| `filter1.*`, `filter2.*`, `fxfilter.*` (45), `filter.routing` (1) | 1.2.4 | 1.2.4 |
| `envN.*` (54) | 1.2.5 | 1.2.5 |
| `lfoN.*` (104, 96 one-to-one + 8 `shape`) | 1.2.6, 1.3.2 | 1.2.6, 1.3.2 |
| `rlfoN.*` (32) | 1.2.6 | 1.2.6 |
| `macro.N` (4) | 1.2.7 | 1.2.7 |
| `modNN.*` (448: 320 one-to-one + 128 `source` / `destination`) | 1.2.7 | 1.2.7 |
| `fx.order.N` (9) | 1.2.9 | 1.2.9 |
| `chorus`, `compressor`, `delay`, `distortion`, `eq`, `flanger`, `phaser`, `reverb` (12 + 20 + 11 + 8 + 13 + 9 + 10 + 13 = 96) | 1.2.9 | 1.2.9 |

Check performed now: 730 bindings + 22 special (3 + 8 + 9 + 1 + 1) + 128 slot source / destination = 880; no key without a source in the file. Vital controls without a key: 64, all classified in 1.2.11. The import table test (Annex C.1) re-checks both directions on every build.

## 7. Open questions (owner)

1. **Own wavetable renderer.** Because `.vital` files hold only wavetable sources, importing custom wavetables on iOS (no Vital code) needs our own clean-room renderer for all 10 component types (1.3.1). This is the largest work item of the feature. Recommendation: build all 10, each with a golden test (C.2). Confirm, and confirm the clean-room procedure (A.5).
2. **Core changes C1 to C4 (A.4)**: new choice "Preset table" in `oscN.wavetable`, new choice "Custom" in `lfoN.shape`, additive `content` / `import` sections in `.sbpreset`, adapter frame-scale rule. They touch `param_table.cpp` and regenerate `params.json` / `PARAMETERS.md` (appended choices, keys and ids unchanged). Approve.
3. **Licence sign-off** for shipping the 730-row table of Vital control names (file-format interoperability data, authored by us from `vital_bindings.cpp`, not generated from `synth_parameters.cpp`) in the iOS binary (Annex B).
4. **Frame position semantics for tables with fewer than 257 frames** in real Vital (is `wave_frame` an absolute frame index or scaled to the table?). Settled by fixture `custom_wavetable` rendered in real Vital (A/B, C.4); until then the rule in 1.2.2 is the working assumption.
5. **`.vitalbank` (zip) support**: not in this design (zip reader with bomb limits is a separate parser). Add it for batch import? The same pipeline would apply per entry.
6. **Own iOS engine modulation scale.** `modNN.amount` imports 1:1 only if engine-own defines the amount as a fraction of the destination range in the same scale as Vital (PARAMETERS line 48). Confirm that this is the contract for engine-own, or the importer needs a per-destination rescale table.
7. **Sample oscillator**: a custom Vital sample is dropped (1.3.3). Keep, or add the sample oscillator (needs ContentPack samples, PARAMETERS "Not exposed") so samples can be imported? Out of this file's scope unless the engine gains it.
8. **Tuning blocks** (`settings.tuning`) are dropped; the engine has no tuning table exposed. Confirm that is acceptable, or expose tuning in 07.
9. **Style mapping** of Vital `Percussion` and `Sequence` (Pluck and Lead) is my proposal; Vital's style list itself is `[unverified]`, see C.5 (real presets).
10. **Export as `.vital`**: recommendation NOT a product feature (C.3). Confirm.
11. Kick integration: CLAP custom extension (section 5) vs. Kick linking `synthyboy_core` directly. Which does Kick use on mobile?
12. `tests/vital-presets/` (owner's real presets) must stay out of git (add to `.gitignore`); confirm that CI never receives them.

---

## Annex A – Import pipeline and API

### A.1 Pipeline and repository layout

```
bytes -> sniff (looksLikeVitalPreset) -> bounded JSON reader (arena, iterative, views into the buffer)
      -> VitalDoc { meta, settings map, modulations[], wavetables[3], sample, lfos[8], tuning }
      -> version + migrations -> table-driven mapping (730 rows) + special mappers
         (modulations, fx order, routing, osc destination, bend range, wavetable choice, LFO shape)
      -> content: renderVitalWavetable / buildLfoShape / built-in shortcut / nearest fit
      -> ParamValues (constrainValue) + PresetMeta + content + ImportReport
      -> ImportResult (pure memory, no I/O)
host layer (shell / Kick): ContentStore + PresetStore write files, batch loop, UI
```

| Path | Content |
|------|---------|
| `core/include/synthyboy/vital_import.h` | public API (A.2), JUCE-free, Vital-free |
| `core/src/vital_import/json_reader.cpp` | bounded tolerant JSON reader + arena |
| `core/src/vital_import/vital_table.cpp` | the 730-row table (key, control, conv, a, b, class flags) |
| `core/src/vital_import/mapper.cpp` | values, modulations, FX order, routing, migrations |
| `core/src/vital_import/wavetable_render.cpp` | the 10 component renderers + post-process |
| `core/src/vital_import/lfo_shape.cpp` | line renderer, built-in matching |
| `core/src/vital_import/report.cpp` | report, JSON, log line |
| `core/src/vital_import/sbpreset_ext.cpp` | `content` / `import` sections of `.sbpreset` |
| `tests/vital-import/` | fixtures, goldens, fuzz targets, fixture writer |
| `core/engine-vital/tests/test_vital_import_crosscheck.cpp` | table vs adapter and Vital cross-checks (needs Vital; built only with `SYNTHYBOY_WITH_VITAL`) |
| `tools/vital-ref/` | dev-machine reference renderer using the real Vital sources (never shipped) |

Placement decision: the importer is built into `synthyboy_core` for ALL targets including the Vital-free iOS build. Reason: iOS users import presets too; the parser, the table (names are format facts), the renderers and the report are our own code with no Vital-derived content, so the iOS rule (ARCHITECTURE 2.2: Vital-derived code only in `core/engine-vital/`) holds and the CI symbol grep stays clean. Everything that needs Vital code (cross-check tests, `tools/vital-ref`) lives under `core/engine-vital/` or `tools/` and is excluded from iOS.

### A.2 API sketch

```cpp
// core/include/synthyboy/vital_import.h  (namespace synthyboy; JUCE-free, Vital-free)
struct VitalImportLimits {
    size_t maxFileBytes = 32u << 20;
    size_t maxWorkingBytes = 192u << 20;
    int    maxDepth = 16;
    int    maxReportItems = 2000;
    uint32_t deadlineMs = 20000;            // 0 = none
};
struct EngineCaps {                          // what the running engine can represent
    bool customWavetables = false;           // choice "Preset table" + user wavetable path
    bool customLfoShapes  = false;           // choice "Custom" + free-form LFO line
    int  maxWavetableFrames = 257;
};
struct ImportOptions {
    VitalImportLimits limits;
    EngineCaps caps;
    bool        renderWavetables = true;
    std::function<bool()> cancel;            // polled between components; true = cancel
    std::function<void(int percent, const char* step)> progress;  // optional
    const char* fallbackName = nullptr;      // file stem when preset_name is empty
};
enum class ImportStatus { ok, notVital, malformed, tooLarge, cancelled, budgetExceeded };

struct ImportedWavetable {
    int osc;                                 // 1..3
    std::string name, author, sha256;        // sha256 of the rendered frames
    int numFrames;                           // <= 257
    std::vector<int16_t> frames;             // numFrames * 2048
};
struct ImportedLfoShape {
    int lfo; std::string name; bool smooth;
    std::vector<std::array<float, 3>> points; // x, y, power
};
struct ImportResult {
    ImportStatus status = ImportStatus::malformed;
    ParamValues values;                      // defaults() unless status == ok
    PresetMeta meta;
    std::vector<ImportedWavetable> wavetables;   // empty if built-in shortcut / not representable
    std::vector<ImportedLfoShape>  lfoShapes;
    ImportReport report;
    std::string vitalMacroNames[4];
    std::string error;                       // short, English, for the log
};

bool looksLikeVitalPreset (const void* data, size_t size) noexcept;               // cheap sniff, first 64 KiB
ImportResult importVitalPreset (const void* data, size_t size, const ImportOptions&) noexcept;
ImportResult importVitalWavetableFile (const void* data, size_t size, const ImportOptions&) noexcept;   // .vitaltable
ImportResult importVitalLfoFile (const void* data, size_t size, const ImportOptions&) noexcept;         // .vitallfo

/** .sbpreset text for a result: savePreset() content plus additive sections
    "content": {"wavetables":[{osc,pack,file,sha256,name,author,frames}],"lfo_shapes":[{lfo,name,smooth,points}]},
    "import":  {"source":{"format":"vital","version","sha256","preset_name","comments","macro_names"},"approximated":[keys<=64]}.
    contentRefs fills pack/file for the wavetables the host stored. */
std::string serializeImportedPreset (const ImportResult&, const PresetMeta& finalMeta, const ContentRefs& contentRefs);

/** Host-side interfaces (implemented by the shell / Kick; the core never touches the file system here). */
struct ContentStore { virtual bool hasWavetable (const std::string& sha256) = 0;
                      virtual bool addWavetable (const ImportedWavetable&, std::string& packOut, std::string& fileOut) = 0; };
struct PresetStore  { virtual bool exists (const std::string& name, const std::string& srcTag) = 0;
                      virtual bool write (const std::string& relPath, const std::string& sbpresetText, const std::string& reportJson) = 0; };
struct BatchOptions { enum Duplicate { skipSame, replace, keepBoth, ask } duplicate = skipSame; bool useVitalStyle = true; std::string unknownStyleCategory = "Lead"; };
```

Properties: no globals, no I/O, no threads created; thread-safe for independent calls; never called from the audio thread; a result is plain data (movable to the message thread). The `.sbpreset` additions are additive keys; an older loader skips unknown keys (the meta parser's `skipValue` path, test in C.1: load an imported file with the current `loadPreset`).

### A.3 Metadata fields written

| `PresetMeta` | Value |
|--------------|-------|
| `name` | `preset_name` (or file stem) |
| `author` | `author` verbatim |
| `license` | `Imported, user-supplied; original terms apply` |
| `category` | mapped style or user choice |
| `tags` | `imported,vital,vital-style:<orig>,src:<12 hex>` (<= 255 B) |
| `description` | comments, <= 250 B |

### A.4 Core changes required (dependencies, listed once)

| # | Change | Why |
|---|--------|-----|
| C1 | `oscN.wavetable`: append choice 13 "Preset table"; engine `setUserWavetable(osc, data)` through the lock-free single-slot swap (ARCHITECTURE 3.1 point 4); the adapter's frame scale uses the loaded table's frame count (today a hard-coded `kMorphFrames - 1`) | custom wavetables |
| C2 | `lfoN.shape`: append choice 12 "Custom"; engine takes a point list (the Vital line generator is already the LFO source in `engine-vital`; the own engine needs an equivalent) | custom LFO shapes |
| C3 | `.sbpreset`: additive `content` and `import` sections; `savePreset` overload; size check against the 1 MiB preset limit (wavetables are never embedded here, they live in the pack) | references |
| C4 | `WavetableStore` / `.sbwt` bytes: defined by the Phase 4 content tools (not in the repo yet: `grep sbwt` finds only ARCHITECTURE text); the importer only calls `ContentStore::addWavetable` | storage |
| C5 | Regenerate `docs/params.json`, `docs/PARAMETERS.md` (choices appended; keys and ids unchanged; state version stays 2) | docs |

Without C1 / C2 the importer still works: `EngineCaps` false -> nearest-built-in fallbacks (1.3.1 step 6, 1.3.2 step 4).

### A.5 Clean-room procedure (wavetable renderer and line renderer)

Author A writes `docs/vital-wavetable-behaviour.md` from this file, the Vital manual and black-box reference output (C.2); implementer B writes `wavetable_render.cpp` / `lfo_shape.cpp` from that behaviour document and the goldens without opening `core/engine-vital/upstream/src/common/wavetable/` or `line_generator.*`. If one person does both, the commit message records that the upstream files were not consulted for the implementation. (Parsing of the JSON fields in 1.1 is format knowledge, not implementation.)

## Annex B – Licence and policy

| Rule | How it is kept |
|------|----------------|
| User-supplied files only | Nothing is bundled, downloaded or fetched. The importer reads bytes handed over by the user / host. No Vital preset, wavetable or LFO file is in the repo: all test fixtures are hand-made by scripts (C.1). `tests/vital-presets/` (owner's real files) is git-ignored. |
| Vital's bundled presets must not be redistributed (ARCHITECTURE 2.2 rule 3) | Imported presets are saved only into the user's own preset directory; CI lint fails if any file under `content/` has the tag `imported` or `vital-style:`; the factory tools never call the importer (ARCHITECTURE 2.3: no preset is created by loading and re-saving a Vital preset). |
| No network (ARCHITECTURE 2.2 rule 2) | The core API takes bytes; there is no URL type, no socket include in `vital_import*`; CI greps the core and shell sources for network headers. Download of presets from vital.audio is the user's job in a browser; Kick's share sheet hands the file over. |
| No telemetry | The import log is a local file; nothing is sent; the report is shown, stored next to the preset, and copyable by the user only. |
| Original author | `author` is the Vital author verbatim; the preset licence field says that original terms apply; the sheet never claims authorship. |
| Engine differences | Same engine family, our parameter ranges: sound may differ. The report marks each clamp / approximation, the preset carries `import.approximated`, the UI shows the amber badge and rings (1.4); the sheet text says "Imported from Vital: may sound different" when fidelity is not `exact`. |
| Trademark | UI text says "Vital preset", factual compatibility only; not "Vital" as our product name (ARCHITECTURE 2.2 rule 1). |
| Vital-derived code | None in the importer. The control-name table is authored from `vital_bindings.cpp` (our code) and format knowledge; it is not generated from `synth_parameters.cpp` (ARCHITECTURE 2.2: the ParamInfo table is not generated from Vital's table; the same spirit applies). Cross-check tests that include Vital code live under `core/engine-vital/`. Open question 3 asks for the owner's sign-off. |
| Name | The file extension `.vital` is handled as a document type only. |

## Annex C – Test plan

### C.1 Unit and golden tests (`tests/vital-import/`, ctest label `vital-import`)

Fixtures are generated by `tests/vital-import/make_fixtures.py` (deterministic, committed output) from scratch-authored data; expected outputs `*.expected.sbpreset` and `*.expected.report.json` are committed goldens.

| # | Fixture | Content | Checks |
|---|---------|---------|--------|
| 1 | `min_empty` | `{"settings":{}}` only | status ok, all defaults, `*_on` rule, `numDefaulted` large |
| 2 | `osc_all` | osc 1-3: every setting at a non-default in-range value, all stack / morph / warp indices (incl. engine 9 and 12), destinations 0..14, built-in-like Sine wavetable | each of the 69 oscillator keys exact; warp 9 / 12 -> approximated; destination 5..14 -> approximated; Sine shortcut -> `oscN.wavetable = 0` |
| 3 | `filters_routing` | filter 1 / 2 / fx: models 0..7, styles 0..9, extremes of cutoff; routing in all four input combinations; a pre-0.5.0 file with `filter_N_oscM_input` only | clamps reported, style > 5 reported, routing values, legacy migration |
| 4 | `env_lfo_ranges` | envelopes at 0, mid, max (32 s), LFO frequency -7 / 9, sync types, tempo 0..12, random LFO styles | C-class clamps, inverse conversions equal the adapter's forward (round trip) |
| 5 | `mod_matrix` | 64 connections: bipolar / stereo / bypass / power, source `lift`, destination `sub_level`, duplicate pair, destination in each group, source = every one of the 28 | dropped connections reported with slot numbers kept; all 269 destinations resolve |
| 6 | `effects_all` | all 9 effects on with non-defaults; `effect_chain_order` = 0, 362879, 12345, 1.5, -1, 1e9 | order decode, invalid -> identity + report |
| 7 | `voice_global` | polyphony 1 / 32, legato, portamento -10 / 4, bend 48, `voice_amplitude` 0.5, velocity track, tuning block, ignored settings | `master.*` values, volume fold-in (`voice_amplitude`), tuning dropped |
| 8 | `custom_wavetable` | osc 1: Wave Source 3 keyframes (time, cubic), 2 groups; osc 2: Line Source + Wave Folder + Frequency Filter; osc 3: Audio File Source (tiny PCM, window 512) | renders, stored once, SHA-256 stable, `content.wavetables`, F < 257 frame fraction |
| 9 | `custom_lfo` | 8 `lfos`: default saw, a built-in-like sine (shortcut), 7-point custom with powers and smooth, 100-point shape, invalid (1 point, short arrays) | shortcut, custom stored, sanitising, invalid dropped alone |
| 10 | `sample_sub_metadata` | custom sample, `sub_on = 1`, non-"White Noise" name; metadata with emoji, invalid UTF-8, 1 KB comments, names `../../x`, each `preset_style` | `noise.on = 0` + dropped item, sub item, truncation to 255 B, file-name sanitising, category mapping |

Additional unit tests: (a) conversion table: for every row, `forward(inverse(x)) == x` for 1000 samples across the Vital range, and for every row the stored class matches whether our range is narrower; (b) `effect_chain_order` decoder equals the engine's `decodeFloatToOrder` for all 362 880 codes (`core/engine-vital/tests`); (c) every `vitalBindings()` entry has an importer row with the same control, key, conv, a, b, and vice versa; (d) all 269 modulatable keys resolve as destinations; (e) `loadPreset(serializeImportedPreset(...))` returns `ok`, same values; the current `readPresetMeta` reads the imported meta; (f) determinism: two imports, byte-identical output; (g) no network: link check that `synthyboy_core` imports no socket symbols.

### C.2 Reference data for the renderers

`tools/vital-ref/` (dev machine only, links the real Vital sources) renders a library of generated `.vitaltable` / `.vitallfo` inputs (one family per component type, parameter sweeps, 1 to 257 keyframes) to raw frames; the outputs are committed as goldens (data derived from our own inputs). Per-component pass criterion: max per-frame residual after best-gain alignment <= -90 dB for the pure-arithmetic components (Wave Source, Line Source, Phase Shift, Wave Folder, Slew Limiter, Wave Window, Wave Warp, LFO line), <= -60 dB for Frequency Filter, Shepard Tone Source and Audio File Source. A component that misses its criterion is a defect to fix, not a downgrade.

### C.3 Round trip and export decision

Recommendation: no export as `.vital` in the product. Reasons: (1) no user need was stated (the goal is bringing Vital presets in); (2) our model is a subset in places (no sub, no samples, no tuning) and a superset in none, so an export could not be lossless for new presets and would promise compatibility we cannot test without Vital; (3) writing Vital's wavetable format would need the inverse renderer (frames -> Wave Source keyframes: feasible but a second feature); (4) the same value falls out of a test-only writer. Round-trip coverage therefore uses `tests/vital-import/fixture_writer.cpp`: it writes a `.vital` from our `ParamValues` (forward conversions = the adapter's) and the test checks `import(write(values)) == values` for all 730 keys on 1000 random in-range patches and the default patch, and that an out-of-Vital-range value clamps. The writer is test code, not shipped.

### C.4 A/B render against real Vital

* Setup: for each fixture and each real preset, render the same `.vital` in (a) the real Vital (via `tools/vital-ref`, built from the official source at `636ca0e`, or the installed Vital plugin driven by a host script that loads the JSON as state `[unverified]`), (b) SynthyBoy `engine-vital` at quality tier High after import. 48 kHz, stereo float, 120 BPM, no host effects. Notes: C2, C3 and C4 at velocity 100, held 2.0 s plus 1.0 s release tail; a chord and a 4-note arpeggio pattern for presets with LFO sync. Random elements: both engines are run with fixed seeds where the engine allows; otherwise the statistical metrics below are used.
* Metrics per rendered note: peak (dBFS), RMS (dBFS, 0.25 to 2.0 s), spectral centroid (Hz; 4096-pt Hann, mean over 0.25 to 2.0 s), 1/3-octave band levels (20 Hz to 16 kHz, dB), normalised residual `RMS(a-b)/RMS(a)` (deterministic presets only).
* Classes and thresholds:

| Class | When | Pass |
|-------|------|------|
| deterministic | fidelity `exact`, no random LFO / noise / `random_phase` > 0 | residual <= -40 dB, peak within 0.5 dB, RMS within 0.5 dB, centroid within 2 % |
| statistical | `exact` or `close` with random elements | RMS within 1.5 dB, centroid within 10 %, mean band-level difference <= 3 dB, no band > 8 dB |
| flagged | `degraded` | no threshold; the difference must be explained by report items (listed in the A/B output) |

A failure in `deterministic` / `statistical` is a bug in the mapping, the renderer or the report (an unreported difference), never a threshold change. Fixture 8 additionally settles open question 4.
* Outputs: `tests/vital-import/_out/ab-<fixture>.json` (metrics) and WAV files for listening; run on demand (`ctest -L vital-ab`) and nightly on the dev machine that has the reference renderer.

### C.5 Real-preset acceptance (owner's folder `tests/vital-presets/`, not in git, does not exist yet)

`ctest -L vital-real` skips with a message when the folder is absent. When present: (1) every file parses without crash, hang (5 s limit each) or sanitizer finding; (2) `status == ok` for every file that real Vital loads (a file that Vital itself rejects may be `malformed`); (3) summary `tests/vital-presets/_out/summary.json`: counts per class, the ranked list of dropped / approximated names across all files, unknown top-level keys, unknown settings; (4) any unknown top-level key or setting name is a task to extend the table (not a "later" item); (5) A/B (C.4) over all files: zero unexplained failures; (6) the `preset_style` values actually seen replace the `[unverified]` list in 1.2.1, and `[unverified]` keys in 1.1 are confirmed or corrected in this document.

### C.6 Fuzz and malformed-input tests

libFuzzer target `fuzz_vital_import` (clang; also an AFL-compatible stdin driver), corpus = the 10 fixtures + mutations; options `-timeout=5 -rss_limit_mb=512 -max_len=4194304`; nightly 1 M executions; every crash / hang / slow input becomes a committed regression file under `tests/vital-import/regress/`. Also a deterministic test that replays this list:

1. every fixture truncated at every byte offset (stride 1 below 4 KiB, stride 97 above); 2. random single-byte flips and bit flips; 3. nesting `[[[[...` and `{"a":{"a":...` 1 000 000 deep; 4. `modulations` with 1 000 000 tiny entries (limit, time); 5. strings with every escape, `\u0000`, lone surrogates, 4 MB strings; 6. numbers `1e999`, `-1e999`, `1e-999`, `-0`, `0x10`, `01`, `NaN`, `Infinity`, 400-digit numbers; 7. base64 with invalid characters, bad padding, lengths not multiple of 4, 1 byte, 16 MiB decoded, 17 MiB decoded; 8. keyframe `position` negative, > 256, NaN, duplicates, unsorted; 0 and 258 keyframes; 9. 0 groups, 0 components, unknown `type`, 10 000 components; 10. LFO `num_points` 0 / 1 / 101 / -5 / `INT_MAX`, `points` shorter than `2*num_points`, x decreasing or outside [0,1], `powers` +-1e30; 11. `modulations` entries that are not objects, missing keys, 1 MB source / destination names, 100 000 duplicates; 12. `synth_version` as number, empty, `"9.9.9"`, 1 MB of digits, `"1.5"`; 13. `settings` with 100 000 unknown keys; 14. invalid UTF-8 and overlong sequences in names, `../../x`, `CON`, very long names; 15. Audio File Source with `window_size` 0 / -1 / 2^31-1, `audio_sample_rate` 0 / -1 / 1e9, empty `audio_file`, PCM of odd byte length; 16. wavetable with maximum allowed content in all three oscillators (budget and time); 17. duplicate keys in objects; 18. trailing garbage, two root objects, empty file, whitespace only, binary data, a gzip stream, a UTF-16 file; 19. `effect_chain_order` NaN / negative / fractional / 2^31; 20. a `.sbpreset` or a `.vitaltable` given to the preset importer (and the reverse).

Pass criteria: no crash, no out-of-bounds, no allocation above `maxWorkingBytes`, completes within the deadline, deterministic status, outputs always in range (`constrainValue` idempotent over every produced value), report counts consistent (`imported + approximated + dropped + ignored + defaulted` = settings processed).

### C.7 Sanitizers and real-time checks

* ASan + UBSan build of `test_vital_import`, the fuzz target in replay mode over all fixtures and `regress/`, and the cross-check tests (CI job `sanitize`); TSan for the batch worker; an MSan or valgrind run of the golden tests on Linux.
* Real-time audit: the import runs entirely off the audio thread; the existing allocation audit (ARCHITECTURE 3.7) runs a test that audits the audio thread while an import and a preview swap happen on another thread (no allocation / lock on the audio thread, no dropouts).
* iOS link check (existing CI job): symbol grep shows no Vital-derived object when the importer is linked.
