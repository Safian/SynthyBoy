# Kick UI & Remote-Control Integration – mobile UI design

Defines how SynthyBoy integrates into **Kick** (Kick mobile/tablet host and Akai MPC Key 61 hardware surface) via `ParamInfo` (`core/include/synthyboy/params.h`), `docs/params.json` (880 parameters), `clap.remote-controls` (`docs/ARCHITECTURE.md` §4.1, §5.1, §8), and a declarative JSON page definition format. Includes the concrete JSON schema, all 61 8-knob remote-control pages across every synth area, default 4-macro assignments per preset category (`content/presets/contentpack.json`), and a Python verification report accounting for all 880 keys.

* **Out-of-scope exclusions (`00-BRIEF.md`):** Preset browser (`[S p.327]`): excluded: Kick. Clips (`[S p.219]`): excluded: Kick. Arpeggiator (`[S p.244]`): excluded: Kick.

---

## 1. Function inventory

| Function | Origin (Vital / Serum / Kick Architecture) | Our params / API | Engine status (HAVE / GAP) |
|----------|--------------------------------------------|------------------|----------------------------|
| **Single-Source Parameter Table (`ParamInfo` -> `docs/params.json`)** (stable `uint32_t id = hashKey(key)`, dotted `key`, `name`, `group`, `min`, `max`, `def`, `scale`, `skew`, `interval`, `unit`, `decimals`, `choices`, `modulatable`, `ownEngine`) | SynthyBoy `ARCHITECTURE.md` §5.1, `core/include/synthyboy/params.h` | All 880 keys in `docs/params.json` (`modSources` 1..28, `modDestinations` 1..269) | **HAVE** |
| **Locale-Independent Value Formatting & Parsing** (`valueToText` / `textToValue` in core so hardware Q-Link displays and touch knobs show identical strings) | SynthyBoy `params.h` l.116-120, `[S p.25]`, `[V p.4]` | `synthyboy::valueToText`, `synthyboy::textToValue`, `synthyboy::choiceName` | **HAVE** |
| **`clap.remote-controls` 8-Knob Page Extension** (exposes named 8-parameter pages to CLAP hosts and Kick's MPC / mobile hardware surface) | SynthyBoy `ARCHITECTURE.md` §4.1, §8 | `clap_plugin_remote_controls_t` in `shell-clap/` & `shell-juce/` reading the page table in §5 | **GAP** (`shell-clap` exposes `clap.params`; `clap.remote-controls` extension struct is Phase 5 work) |
| **Declarative JSON Page Definition (`kick-pages.json`)** (machine-readable mapping of section tabs, 8-knob pages, "More" sheet overflow keys, and category macro templates) | SynthyBoy `00-BRIEF.md` #7, `ARCHITECTURE.md` §5.1, §8 | Consumes `docs/params.json` keys (schema + example in §2.1) | **HAVE** (all keys exist in `docs/params.json`) |
| **4 Performance Macros & Category Templates (`macro.1..4`)** (always-visible 4 macro knobs with per-category target conventions across Bass, Lead, Pad, Pluck, Keys, FX) | Both `[S p.206-208]`, `[V p.23]`, `content/presets/contentpack.json` | `macro.1`, `macro.2`, `macro.3`, `macro.4` (`0..100%`) + `modNN.*` | **HAVE** |
| **Kick "More" Overflow Sheet & Matrix Bridge** (reaches secondary parameters not placed on primary 8-knob hardware pages, plus all 64 Mod Matrix slots) | `00-BRIEF.md` #1, #7 | 46 secondary modulator keys ("More" sheets) + 408 matrix slot keys (Kick Matrix view) | **HAVE** |
| **Missing-Content & Quality-Tier Host Status Bridge** (`missing_content` restore badge and `Tier::low/medium/high` voice/FX cap badges in Kick header) | `ARCHITECTURE.md` §3.6, §5.2, §6, `quality.h` | `ContentRegistry` + `IEngine::setQuality` / `clap.voice-info` | **HAVE** |

---

## 2. Screen design

### 2.1 Concrete JSON Page Definition Format (Schema Sketch + Example)

Kick reads `kick-pages.json` alongside `docs/params.json`. Each page has a stable `id`, human `title`, `section` tab group, up to 8 `knobs` (each referencing a `key` in `docs/params.json`, an optional short `label`<=10 chars for hardware OLEDs, and a `control` hint), and an optional `moreKeys` array listing secondary parameters opened by the page's `[More...]` sheet.

#### JSON Schema Sketch (`schema/kick-pages.schema.json`)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://safian.com/schemas/synthyboy-kick-pages.schema.json",
  "title": "SynthyBoy Kick Remote Control Pages",
  "type": "object",
  "required": ["format", "version", "pluginId", "macroDefaults", "pages"],
  "properties": {
    "format": { "const": "synthyboy-kick-pages" },
    "version": { "type": "integer", "const": 1 },
    "pluginId": { "const": "com.safian.synthyboy" },
    "macroDefaults": {
      "type": "object",
      "additionalProperties": {
        "type": "array",
        "minItems": 4,
        "maxItems": 4,
        "items": {
          "type": "object",
          "required": ["macroKey", "label", "targets"],
          "properties": {
            "macroKey": { "enum": ["macro.1", "macro.2", "macro.3", "macro.4"] },
            "label": { "type": "string", "maxLength": 12 },
            "targets": {
              "type": "array",
              "items": {
                "type": "object",
                "required": ["destination", "amount", "bipolar"],
                "properties": {
                  "destination": { "type": "string" },
                  "amount": { "type": "number", "minimum": -100, "maximum": 100 },
                  "bipolar": { "type": "boolean" }
                }
              }
            }
          }
        }
      }
    },
    "pages": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "required": ["id", "section", "title", "knobs"],
        "properties": {
          "id": { "type": "string", "pattern": "^[a-z0-9_]+$" },
          "section": {
            "enum": ["Performance", "Master", "Osc", "Filter", "Env", "LFO", "FX", "Matrix"]
          },
          "title": { "type": "string", "maxLength": 24 },
          "knobs": {
            "type": "array",
            "minItems": 1,
            "maxItems": 8,
            "items": {
              "type": ["object", "null"],
              "required": ["key", "shortLabel"],
              "properties": {
                "key": { "type": "string" },
                "shortLabel": { "type": "string", "maxLength": 10 },
                "widget": { "enum": ["knob", "stepped", "toggle", "bipolar_knob"] }
              }
            }
          },
          "moreKeys": {
            "type": "array",
            "items": { "type": "string" }
          }
        }
      }
    }
  }
}
```

#### Concrete JSON Instance Example (`kick-pages.json` excerpt)

```json
{
  "format": "synthyboy-kick-pages",
  "version": 1,
  "pluginId": "com.safian.synthyboy",
  "macroDefaults": {
    "Bass": [
      { "macroKey": "macro.1", "label": "Cutoff", "targets": [{ "destination": "filter1.cutoff", "amount": 45.0, "bipolar": false }] },
      { "macroKey": "macro.2", "label": "Sub/Drive", "targets": [{ "destination": "filter1.drive", "amount": 50.0, "bipolar": false }, { "destination": "distortion.drive", "amount": 35.0, "bipolar": false }] },
      { "macroKey": "macro.3", "label": "Env/Wobble", "targets": [{ "destination": "env2.decay", "amount": 40.0, "bipolar": false }, { "destination": "lfo1.rate", "amount": 30.0, "bipolar": false }] },
      { "macroKey": "macro.4", "label": "Width/FX", "targets": [{ "destination": "osc1.unison.detune", "amount": 30.0, "bipolar": false }, { "destination": "chorus.mix", "amount": 35.0, "bipolar": false }] }
    ]
  },
  "pages": [
    {
      "id": "lfo1",
      "section": "LFO",
      "title": "LFO 1",
      "knobs": [
        { "key": "lfo1.shape", "shortLabel": "Shape", "widget": "stepped" },
        { "key": "lfo1.sync", "shortLabel": "Sync", "widget": "stepped" },
        { "key": "lfo1.rate", "shortLabel": "Rate", "widget": "knob" },
        { "key": "lfo1.division", "shortLabel": "Division", "widget": "stepped" },
        { "key": "lfo1.sync_type", "shortLabel": "Trigger", "widget": "stepped" },
        { "key": "lfo1.phase", "shortLabel": "Phase", "widget": "knob" },
        { "key": "lfo1.fade_in", "shortLabel": "Fade In", "widget": "knob" },
        { "key": "lfo1.delay", "shortLabel": "Delay", "widget": "knob" }
      ],
      "moreKeys": [
        "lfo1.smooth",
        "lfo1.smooth_time",
        "lfo1.stereo",
        "lfo1.keytrack_transpose",
        "lfo1.keytrack_tune"
      ]
    }
  ]
}
```

---

### 2.2 Kick Remote UI Layout (Phone Portrait vs MPC Key 61 / Tablet Landscape)

#### Phone Portrait (Kick Host Remote View — 2x4 Knob Grid + Section Pills + "More" Sheet)

```
+-------------------------------------------------------------------+
| KICK REMOTE: SynthyBoy   [<] Acid Saw Reso [>]   [Full GUI] [...] |
+-------------------------------------------------------------------+
| SECTION: [Perf] [Master] [Osc*] [Filt] [Env] [LFO] [FX] [Matrix]  |
| PAGE:    [<]  Osc 1 Main (1 / 3)  [>]         [o][o][o]  [More..]|
+-------------------------------------------------------------------+
|  ( Q1: ON )      ( Q2: WAVE )    ( Q3: FRAME )   ( Q4: LEVEL )    |
|     On               Saw            24.5 %          50.0 %        |
|  `osc1.on`     `osc1.wavetable`  `osc1.frame`    `osc1.level`     |
|                                                                   |
|  ( Q5: PAN )     ( Q6: TRANS )   ( Q7: TUNE )    ( Q8: DEST )     |
|    0.0 %            0 st            0.0 ct         Filter 1       |
|  `osc1.pan`    `osc1.transpose`  `osc1.tune`   `osc1.destination` |
+-------------------------------------------------------------------+
| PINNED MACROS: (M1: 35%)   (M2: 0%)    (M3: 20%)   (M4: 15%)      |
+-------------------------------------------------------------------+
```

#### MPC Key 61 / Tablet Landscape (2x4 Hardware Q-Link OLED Cells + Touch Screen)

```
+---------------------------------------------------------------------------------------+
| KICK INSTRUMENT: SynthyBoy | Tier: LOW (8v Cap) | Page 4/61: Osc 1 Main  [More Sheet] |
+--------------------+--------------------+--------------------+------------------------+
| Q-LINK 1 [osc1.on] | Q-LINK 2 [wavetbl] | Q-LINK 3 [frame]   | Q-LINK 4 [osc1.level]  |
|   ((   ON   ))     |   ((   Saw   ))    |   ((  24.5 %  ))   |   ((   50.0 %   ))     |
|   [Toggle 0/1]     |   [Choice 1/13]    |   [Mod: LFO 1 +30%]|   [Mod: Env 1 +100%]   |
+--------------------+--------------------+--------------------+------------------------+
| Q-LINK 5 [osc1.pan]| Q-LINK 6 [transp]  | Q-LINK 7 [tune]    | Q-LINK 8 [destination] |
|   ((  0.0 %  ))    |   ((   0 st   ))   |   ((  0.0 ct  ))   |   ((  Filter 1  ))     |
|   [Bipolar -100..] |   [-48 .. +48 st]  |   [-100 .. +100 ct]|   [Choice 0/5]         |
+--------------------+--------------------+--------------------+------------------------+
| [Perf] [Master (2)] [Osc 1-3 (10)] [Filter (4)] [Env (6)] [LFO (12)] [FX (20)] [Mod]  |
+---------------------------------------------------------------------------------------+
```

---

### 2.3 Default 4 Macro Assignments per Preset Category (`macro.1` .. `macro.4`)

To make hardware Q-Links and the mobile Macro Bar predictable across all 100 factory presets (`content/presets/contentpack.json` categories: `Bass`, `Lead`, `Pad`, `Pluck`, `Keys`, `FX`), every preset category follows a standardized 4-Macro assignment layout wired via `mod01..mod08`:

| Preset Category | `macro.1` (`Macro 1`) | `macro.2` (`Macro 2`) | `macro.3` (`Macro 3`) | `macro.4` (`Macro 4`) |
|-----------------|-----------------------|-----------------------|-----------------------|-----------------------|
| **1. Bass** *(20 factory presets)* | **Filter Cutoff** (`filter1.cutoff` `+45%`, `filter2.cutoff` `+45%`) | **Drive & Bite** (`filter1.drive` `+50%`, `distortion.drive` `+35%`, `osc1.warp.amount` `+25%`) | **Envelope / Wobble** (`env2.decay` `+40%`, `lfo1.rate` `+30%`) | **Width & Chorus** (`osc1.unison.detune` `+25%`, `chorus.mix` `+35%`) |
| **2. Lead** *(20 factory presets)* | **Brightness** (`filter1.cutoff` `+50%`, `osc1.morph.amount` `+30%`) | **Timbre / Sync / FM** (`osc1.warp.amount` `+45%`, `osc1.frame` `+50%`) | **Detune & Glide** (`osc1.unison.detune` `+30%`, `osc2.unison.detune` `+30%`) | **Space (Delay + Rev)** (`delay.mix` `+35%`, `reverb.mix` `+30%`) |
| **3. Pad** *(20 factory presets)* | **Warmth / Cutoff** (`filter1.cutoff` `+55%`, `filter1.resonance` `+15%`) | **Wavetable Morph** (`osc1.frame` `+60%`, `osc2.frame` `+60%`) | **Slow Motion** (`lfo1.rate` `+25%`, `env1.attack` `-30%`, `env1.release` `+35%`) | **Lush Space** (`reverb.mix` `+40%`, `reverb.decay` `+30%`, `chorus.mix` `+30%`) |
| **4. Pluck** *(15 factory presets)* | **Pluck Open** (`filter1.cutoff` `+50%`, `env2.decay` `+45%`) | **Length / Sustain** (`env1.decay` `+50%`, `env1.release` `+40%`) | **Harmonics / Transient** (`osc1.morph.amount` `+40%`, `noise.level` `+25%`) | **Echo & Hall** (`delay.mix` `+40%`, `reverb.mix` `+30%`) |
| **5. Keys** *(15 factory presets)* | **Tone / Bell** (`filter1.cutoff` `+40%`, `osc1.warp.amount` `+30%`) | **Tremolo / Phaser** (`lfo1.rate` `+35%`, `phaser.mix` `+40%`) | **Release / Envelope** (`env1.release` `+45%`, `env1.decay` `+35%`) | **Room / Chorus** (`chorus.mix` `+35%`, `reverb.mix` `+25%`) |
| **6. FX** *(10 factory presets)* | **Sweep / Freq** (`filter1.cutoff` `+70%`, `fxfilter.cutoff` `+70%`) | **Chaos / Rate** (`rlfo1.rate` `+60%`, `lfo1.rate` `+60%`) | **Crush & Fold** (`distortion.drive` `+55%`, `osc1.warp.amount` `+50%`) | **Wash / Feedback** (`reverb.mix` `+50%`, `delay.feedback` `+40%`) |
| **7. Init / Default** | **Cutoff** (`filter1.cutoff` `+50%`) | **Resonance** (`filter1.resonance` `+40%`) | **Wavetable Pos** (`osc1.frame` `+100%`) | **Space** (`reverb.mix` `+35%`) |

---

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| **Section Pill (`[Perf]..[Matrix]`)** | Single tap | Jumps to the first remote page of that section | Hardware `BANK < / >` buttons on MPC Key 61 |
| **Page Header (`[<] Page (m/n) [>]`)** | Horizontal swipe left/right | Advances to previous/next 8-knob remote page within section | Tap `[<]` / `[>]` arrows |
| **Remote Knob (`Q1..Q8`)** | Vertical drag (touch) / turn Q-Link (hardware) | Adjusts parameter using `fromNormalised` / `toNormalised` (`ParamInfo::scale`, `skew`, `interval`) and formats text via `valueToText` | Double-tap to reset to `ParamInfo::def`; long-press for exact numeric entry |
| **Stepped Choice Knob (`scale == stepped`)** | Tap cell or turn encoder | Opens choice popover (on touch) or steps through `0..numChoices-1` (`choiceName`) | Tap `< / >` stepper inside popover |
| **`[More..]` Button (on pages with `moreKeys`)** | Single tap | Opens the bottom sheet exposing `moreKeys` for that module (e.g., `envN.release_curve`, `lfoN.smooth/stereo/keytrack`) | Switch to `[Full GUI]` view |
| **Pinned Macro Strip (`M1..M4`)** | Drag `M1..M4` at bottom of any remote page | Adjusts `macro.1..4` without leaving the current deep-edit page | Select `Page 1: Macros & Performance` |

---

## 4. States and edge cases

1. **Dynamic Sync / Model Label Adaptation on 8-Knob Pages:**
   - Pages for tempo-syncable modules (`lfo1..8`, `rlfo1..4`, `chorus`, `delay`, `flanger`, `phaser`) expose **both** `<mod>.sync`, `<mod>.rate` (`time`), and `<mod>.division` across their 8 knobs so hardware Q-Link users never lose access when toggling `sync` between `Free (Hz)` and `Tempo`. Whichever of `rate` vs `division` is currently inactive dims to `50%` opacity on screen.
2. **Quality Tier Capping Indicators on Remote Knobs:**
   - When `master.polyphony` or `oscN.unison.voices` exceeds the active `Quality` tier limit (`8v / 4u` on Low, `16v / 8u` on Medium), the Q-Link cell displays `Cap: 8 (16)` and when `reverb.on` or `flanger.on` is bypassed by `Tier::low`, the `Q1` toggle displays `ON (LOW-BYP)`.
3. **Own Engine (`core/engine-own`, iOS build) Flag (`ownEngine: false`):**
   - `docs/params.json` includes `"ownEngine": true/false` on every parameter. When running on `engine-own`, Kick dims parameters with `ownEngine: false` and shows an `(Vital DSP)` subtitle while still saving/loading their values in presets.
4. **Empty Knob Slots (`null` / `—`):**
   - `mod_slot_1..4` have 7 parameters per slot (`source`, `destination`, `amount`, `power`, `bipolar`, `stereo`, `bypass`); Knob 8 is `null` (rendered as an unassigned disabled cell `—`).

---

## 5. Kick remote pages (All Areas — 61 Pages, <= 8 Params per Page)

Every key in the table below is a verified key from `docs/params.json`:

| # | Page ID | Section | Page Title | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Knob 5 | Knob 6 | Knob 7 | Knob 8 |
|---|---------|---------|------------|--------|--------|--------|--------|--------|--------|--------|--------|
| 1 | `macros` | Perf | **Macros & Performance** | `macro.1` | `macro.2` | `macro.3` | `macro.4` | `master.volume` | `filter1.cutoff` | `filter1.resonance` | `env1.release` |
| 2 | `master_voicing` | Master | **Master & Voicing** | `master.volume` | `master.transpose` | `master.tune` | `master.polyphony` | `master.legato` | `master.bend_range` | `master.velocity_track` | `master.portamento.time` |
| 3 | `master_porta` | Master | **Portamento & Glide** | `master.portamento.time` | `master.portamento.slope` | `master.portamento.always` | `master.portamento.scaled` | `master.legato` | `master.polyphony` | `master.bend_range` | `master.volume` |
| 4 | `osc1_main` | Osc | **Osc 1 Main** | `osc1.on` | `osc1.wavetable` | `osc1.frame` | `osc1.level` | `osc1.pan` | `osc1.transpose` | `osc1.tune` | `osc1.destination` |
| 5 | `osc1_unison` | Osc | **Osc 1 Unison & Phase** | `osc1.unison.voices` | `osc1.unison.detune` | `osc1.unison.spread` | `osc1.unison.blend` | `osc1.unison.stack` | `osc1.unison.detune_curve` | `osc1.phase` | `osc1.random_phase` |
| 6 | `osc1_shaping` | Osc | **Osc 1 Morph & Warp** | `osc1.morph.type` | `osc1.morph.amount` | `osc1.morph.spread` | `osc1.warp.type` | `osc1.warp.amount` | `osc1.warp.phase` | `osc1.warp.spread` | `osc1.frame_spread` |
| 7 | `osc2_main` | Osc | **Osc 2 Main** | `osc2.on` | `osc2.wavetable` | `osc2.frame` | `osc2.level` | `osc2.pan` | `osc2.transpose` | `osc2.tune` | `osc2.destination` |
| 8 | `osc2_unison` | Osc | **Osc 2 Unison & Phase** | `osc2.unison.voices` | `osc2.unison.detune` | `osc2.unison.spread` | `osc2.unison.blend` | `osc2.unison.stack` | `osc2.unison.detune_curve` | `osc2.phase` | `osc2.random_phase` |
| 9 | `osc2_shaping` | Osc | **Osc 2 Morph & Warp** | `osc2.morph.type` | `osc2.morph.amount` | `osc2.morph.spread` | `osc2.warp.type` | `osc2.warp.amount` | `osc2.warp.phase` | `osc2.warp.spread` | `osc2.frame_spread` |
| 10 | `osc3_main` | Osc | **Osc 3 Main** | `osc3.on` | `osc3.wavetable` | `osc3.frame` | `osc3.level` | `osc3.pan` | `osc3.transpose` | `osc3.tune` | `osc3.destination` |
| 11 | `osc3_unison` | Osc | **Osc 3 Unison & Phase** | `osc3.unison.voices` | `osc3.unison.detune` | `osc3.unison.spread` | `osc3.unison.blend` | `osc3.unison.stack` | `osc3.unison.detune_curve` | `osc3.phase` | `osc3.random_phase` |
| 12 | `osc3_shaping` | Osc | **Osc 3 Morph & Warp** | `osc3.morph.type` | `osc3.morph.amount` | `osc3.morph.spread` | `osc3.warp.type` | `osc3.warp.amount` | `osc3.warp.phase` | `osc3.warp.spread` | `osc3.frame_spread` |
| 13 | `noise` | Osc | **Noise & Osc Mix** | `noise.on` | `noise.level` | `noise.pan` | `noise.destination` | `filter.routing` | `osc1.level` | `osc2.level` | `osc3.level` |
| 14 | `filter1_main` | Filter | **Filter 1 Main** | `filter1.on` | `filter1.model` | `filter1.style` | `filter1.cutoff` | `filter1.resonance` | `filter1.drive` | `filter1.blend` | `filter1.mix` |
| 15 | `filter1_model` | Filter | **Filter 1 Key & Formant** | `filter1.keytrack` | `filter1.comb_offset` | `filter1.formant.x` | `filter1.formant.y` | `filter1.formant.resonance` | `filter1.formant.spread` | `filter1.formant.transpose` | `filter.routing` |
| 16 | `filter2_main` | Filter | **Filter 2 Main** | `filter2.on` | `filter2.model` | `filter2.style` | `filter2.cutoff` | `filter2.resonance` | `filter2.drive` | `filter2.blend` | `filter2.mix` |
| 17 | `filter2_model` | Filter | **Filter 2 Key & Formant** | `filter2.keytrack` | `filter2.comb_offset` | `filter2.formant.x` | `filter2.formant.y` | `filter2.formant.resonance` | `filter2.formant.spread` | `filter2.formant.transpose` | `filter.routing` |
| 18 | `env1` | Env | **Envelope 1 (Amp)** | `env1.attack` | `env1.hold` | `env1.decay` | `env1.sustain` | `env1.release` | `env1.delay` | `env1.attack_curve` | `env1.decay_curve` |
| 19 | `env2` | Env | **Envelope 2** | `env2.attack` | `env2.hold` | `env2.decay` | `env2.sustain` | `env2.release` | `env2.delay` | `env2.attack_curve` | `env2.decay_curve` |
| 20 | `env3` | Env | **Envelope 3** | `env3.attack` | `env3.hold` | `env3.decay` | `env3.sustain` | `env3.release` | `env3.delay` | `env3.attack_curve` | `env3.decay_curve` |
| 21 | `env4` | Env | **Envelope 4** | `env4.attack` | `env4.hold` | `env4.decay` | `env4.sustain` | `env4.release` | `env4.delay` | `env4.attack_curve` | `env4.decay_curve` |
| 22 | `env5` | Env | **Envelope 5** | `env5.attack` | `env5.hold` | `env5.decay` | `env5.sustain` | `env5.release` | `env5.delay` | `env5.attack_curve` | `env5.decay_curve` |
| 23 | `env6` | Env | **Envelope 6** | `env6.attack` | `env6.hold` | `env6.decay` | `env6.sustain` | `env6.release` | `env6.delay` | `env6.attack_curve` | `env6.decay_curve` |
| 24 | `lfo1` | LFO | **LFO 1** | `lfo1.shape` | `lfo1.sync` | `lfo1.rate` | `lfo1.division` | `lfo1.sync_type` | `lfo1.phase` | `lfo1.fade_in` | `lfo1.delay` |
| 25 | `lfo2` | LFO | **LFO 2** | `lfo2.shape` | `lfo2.sync` | `lfo2.rate` | `lfo2.division` | `lfo2.sync_type` | `lfo2.phase` | `lfo2.fade_in` | `lfo2.delay` |
| 26 | `lfo3` | LFO | **LFO 3** | `lfo3.shape` | `lfo3.sync` | `lfo3.rate` | `lfo3.division` | `lfo3.sync_type` | `lfo3.phase` | `lfo3.fade_in` | `lfo3.delay` |
| 27 | `lfo4` | LFO | **LFO 4** | `lfo4.shape` | `lfo4.sync` | `lfo4.rate` | `lfo4.division` | `lfo4.sync_type` | `lfo4.phase` | `lfo4.fade_in` | `lfo4.delay` |
| 28 | `lfo5` | LFO | **LFO 5** | `lfo5.shape` | `lfo5.sync` | `lfo5.rate` | `lfo5.division` | `lfo5.sync_type` | `lfo5.phase` | `lfo5.fade_in` | `lfo5.delay` |
| 29 | `lfo6` | LFO | **LFO 6** | `lfo6.shape` | `lfo6.sync` | `lfo6.rate` | `lfo6.division` | `lfo6.sync_type` | `lfo6.phase` | `lfo6.fade_in` | `lfo6.delay` |
| 30 | `lfo7` | LFO | **LFO 7** | `lfo7.shape` | `lfo7.sync` | `lfo7.rate` | `lfo7.division` | `lfo7.sync_type` | `lfo7.phase` | `lfo7.fade_in` | `lfo7.delay` |
| 31 | `lfo8` | LFO | **LFO 8** | `lfo8.shape` | `lfo8.sync` | `lfo8.rate` | `lfo8.division` | `lfo8.sync_type` | `lfo8.phase` | `lfo8.fade_in` | `lfo8.delay` |
| 32 | `rlfo1` | LFO | **Random LFO 1** | `rlfo1.style` | `rlfo1.sync` | `rlfo1.rate` | `rlfo1.division` | `rlfo1.sync_type` | `rlfo1.stereo` | `rlfo1.keytrack_transpose` | `rlfo1.keytrack_tune` |
| 33 | `rlfo2` | LFO | **Random LFO 2** | `rlfo2.style` | `rlfo2.sync` | `rlfo2.rate` | `rlfo2.division` | `rlfo2.sync_type` | `rlfo2.stereo` | `rlfo2.keytrack_transpose` | `rlfo2.keytrack_tune` |
| 34 | `rlfo3` | LFO | **Random LFO 3** | `rlfo3.style` | `rlfo3.sync` | `rlfo3.rate` | `rlfo3.division` | `rlfo3.sync_type` | `rlfo3.stereo` | `rlfo3.keytrack_transpose` | `rlfo3.keytrack_tune` |
| 35 | `rlfo4` | LFO | **Random LFO 4** | `rlfo4.style` | `rlfo4.sync` | `rlfo4.rate` | `rlfo4.division` | `rlfo4.sync_type` | `rlfo4.stereo` | `rlfo4.keytrack_transpose` | `rlfo4.keytrack_tune` |
| 36 | `fx_order_1` | FX | **FX Chain Order 1–8** | `fx.order.1` | `fx.order.2` | `fx.order.3` | `fx.order.4` | `fx.order.5` | `fx.order.6` | `fx.order.7` | `fx.order.8` |
| 37 | `fx_switches` | FX | **FX Order 9 & Switches** | `fx.order.9` | `fxfilter.on` | `distortion.on` | `compressor.on` | `eq.on` | `chorus.on` | `delay.on` | `reverb.on` |
| 38 | `fx_chorus_1` | FX | **Chorus Main** | `chorus.on` | `chorus.mix` | `chorus.rate` | `chorus.sync` | `chorus.division` | `chorus.depth` | `chorus.feedback` | `chorus.voices` |
| 39 | `fx_chorus_2` | FX | **Chorus Delays & Filter** | `chorus.delay1` | `chorus.delay2` | `chorus.filter_cutoff` | `chorus.filter_spread` | `chorus.mix` | `chorus.depth` | `chorus.feedback` | `chorus.on` |
| 40 | `fx_comp_main` | FX | **Compressor Main & Gain** | `compressor.on` | `compressor.mix` | `compressor.bands` | `compressor.attack` | `compressor.release` | `compressor.low.gain` | `compressor.mid.gain` | `compressor.high.gain` |
| 41 | `fx_comp_low` | FX | **Compressor Low Band** | `compressor.low.lower_threshold` | `compressor.low.upper_threshold` | `compressor.low.lower_ratio` | `compressor.low.upper_ratio` | `compressor.low.gain` | `compressor.attack` | `compressor.release` | `compressor.mix` |
| 42 | `fx_comp_mid` | FX | **Compressor Mid Band** | `compressor.mid.lower_threshold` | `compressor.mid.upper_threshold` | `compressor.mid.lower_ratio` | `compressor.mid.upper_ratio` | `compressor.mid.gain` | `compressor.attack` | `compressor.release` | `compressor.mix` |
| 43 | `fx_comp_high` | FX | **Compressor High Band** | `compressor.high.lower_threshold` | `compressor.high.upper_threshold` | `compressor.high.lower_ratio` | `compressor.high.upper_ratio` | `compressor.high.gain` | `compressor.attack` | `compressor.release` | `compressor.mix` |
| 44 | `fx_delay_1` | FX | **Delay Main** | `delay.on` | `delay.mix` | `delay.style` | `delay.feedback` | `delay.sync` | `delay.time` | `delay.time2` | `delay.division` |
| 45 | `fx_delay_2` | FX | **Delay Filter & Div 2** | `delay.division2` | `delay.filter_cutoff` | `delay.filter_spread` | `delay.feedback` | `delay.mix` | `delay.style` | `delay.sync` | `delay.on` |
| 46 | `fx_distortion` | FX | **Distortion** | `distortion.on` | `distortion.type` | `distortion.drive` | `distortion.mix` | `distortion.filter_order` | `distortion.filter_cutoff` | `distortion.filter_resonance` | `distortion.filter_blend` |
| 47 | `fx_eq_low_mid` | FX | **EQ Low & Mid** | `eq.on` | `eq.low.mode` | `eq.low.cutoff` | `eq.low.gain` | `eq.low.resonance` | `eq.mid.mode` | `eq.mid.cutoff` | `eq.mid.gain` |
| 48 | `fx_eq_high` | FX | **EQ Mid Q & High** | `eq.mid.resonance` | `eq.high.mode` | `eq.high.cutoff` | `eq.high.gain` | `eq.high.resonance` | `eq.low.gain` | `eq.mid.gain` | `eq.on` |
| 49 | `fx_filter_1` | FX | **FX Filter Main** | `fxfilter.on` | `fxfilter.model` | `fxfilter.style` | `fxfilter.cutoff` | `fxfilter.resonance` | `fxfilter.drive` | `fxfilter.blend` | `fxfilter.mix` |
| 50 | `fx_filter_2` | FX | **FX Filter Formant/Comb** | `fxfilter.keytrack` | `fxfilter.comb_offset` | `fxfilter.formant.x` | `fxfilter.formant.y` | `fxfilter.formant.resonance` | `fxfilter.formant.spread` | `fxfilter.formant.transpose` | `fxfilter.mix` |
| 51 | `fx_flanger` | FX | **Flanger** | `flanger.on` | `flanger.mix` | `flanger.rate` | `flanger.sync` | `flanger.division` | `flanger.depth` | `flanger.feedback` | `flanger.center` |
| 52 | `fx_phaser_1` | FX | **Phaser & Flanger Phase** | `phaser.on` | `phaser.mix` | `phaser.rate` | `phaser.sync` | `phaser.division` | `phaser.depth` | `phaser.feedback` | `flanger.phase_offset` |
| 53 | `fx_phaser_2` | FX | **Phaser Tone & Stereo** | `phaser.center` | `phaser.blend` | `phaser.phase_offset` | `phaser.depth` | `phaser.feedback` | `phaser.mix` | `phaser.rate` | `phaser.on` |
| 54 | `fx_reverb_1` | FX | **Reverb Space & Cuts** | `reverb.on` | `reverb.mix` | `reverb.size` | `reverb.decay` | `reverb.predelay` | `reverb.pre_low_cut` | `reverb.pre_high_cut` | `reverb.chorus_amount` |
| 55 | `fx_reverb_2` | FX | **Reverb Damping & Chorus** | `reverb.low_shelf_cutoff` | `reverb.low_shelf_gain` | `reverb.high_shelf_cutoff` | `reverb.high_shelf_gain` | `reverb.chorus_amount` | `reverb.chorus_rate` | `reverb.decay` | `reverb.mix` |
| 56 | `mod_slot_1` | Matrix | **Mod Matrix Slot 1** | `mod01.source` | `mod01.destination` | `mod01.amount` | `mod01.power` | `mod01.bipolar` | `mod01.stereo` | `mod01.bypass` | — |
| 57 | `mod_slot_2` | Matrix | **Mod Matrix Slot 2** | `mod02.source` | `mod02.destination` | `mod02.amount` | `mod02.power` | `mod02.bipolar` | `mod02.stereo` | `mod02.bypass` | — |
| 58 | `mod_slot_3` | Matrix | **Mod Matrix Slot 3** | `mod03.source` | `mod03.destination` | `mod03.amount` | `mod03.power` | `mod03.bipolar` | `mod03.stereo` | `mod03.bypass` | — |
| 59 | `mod_slot_4` | Matrix | **Mod Matrix Slot 4** | `mod04.source` | `mod04.destination` | `mod04.amount` | `mod04.power` | `mod04.bipolar` | `mod04.stereo` | `mod04.bypass` | — |
| 60 | `mod_amounts_1_8` | Matrix | **Mod Amounts 1–8** | `mod01.amount` | `mod02.amount` | `mod03.amount` | `mod04.amount` | `mod05.amount` | `mod06.amount` | `mod07.amount` | `mod08.amount` |
| 61 | `mod_amounts_9_16` | Matrix | **Mod Amounts 9–16** | `mod09.amount` | `mod10.amount` | `mod11.amount` | `mod12.amount` | `mod13.amount` | `mod14.amount` | `mod15.amount` | `mod16.amount` |

---

## 6. Coverage check & Python Verification Report (All 880 Keys)

### 6.1 Python Verification Snippet

Running the following snippet from the repository root verifies that all 61 pages contain at most 8 parameters, that 100% of referenced keys exist in `docs/params.json`, and prints the exact breakdown of reached vs. overflow ("More" sheet / Matrix) keys:

```python
import json, re

with open("docs/params.json") as f:
    all_params = [p["key"] for p in json.load(f)["params"]]
valid_set = set(all_params)

with open("docs/ui-design/09-kick-integration.md") as f:
    md = f.read()

# Parse the 61 rows of the Section 5 table
rows = re.findall(r"^\|\s*\d+\s*\|\s*`([a-z0-9_]+)`\s*\|([^|]+)\|([^|]+)\|(.+)\|$", md, re.M)
assert len(rows) == 61, f"Expected 61 pages, got {len(rows)}"

reached = set()
for page_id, sec, title, cells in rows:
    knobs = [c.strip().strip("`") for c in cells.split("|") if c.strip() != "—"]
    assert 1 <= len(knobs) <= 8, f"Page {page_id} has {len(knobs)} knobs"
    for k in knobs:
        assert k in valid_set, f"Unknown key {k} on page {page_id}"
        reached.add(k)

unreached = [k for k in all_params if k not in reached]
print(f"Total params in docs/params.json: {len(all_params)}")
print(f"Pages verified: {len(rows)} (all <= 8 knobs, 0 invalid keys)")
print(f"Unique keys reached directly on 8-knob pages: {len(reached)}")
print(f"Keys not on a primary 8-knob page: {len(unreached)}")
```

### 6.2 Report of Keys of the 880 Not Reached by a Primary 8-Knob Page (454 Keys)

Out of **880** total parameters in `docs/params.json`:
- **426 parameters** are reached directly on the **61 primary 8-knob remote pages** in §5 (including **100%** of `Master` (11), `Macros` (4), `Osc 1` (24), `Osc 2` (24), `Osc 3` (24), `Noise` (4), `Filter 1` (15), `Filter 2` (15), `Filters` (1), `FX Filter` (15), `Random LFO 1..4` (32), and `FX` (105)).
- **454 parameters** are intentionally kept off the primary 8-knob hardware bank to avoid scrolling through 110+ pages, and are reached via a module **"More" sheet** (`moreKeys` in `kick-pages.json`) or the **Modulation Matrix UI**:

| Unreached Key Group | Exact Keys (`docs/params.json`) | Count | Where Reached (`More` Sheet or `Matrix`) |
|---------------------|---------------------------------|-------|------------------------------------------|
| **Envelope Release Curves (Env 1–6)** | `env1.release_curve`, `env2.release_curve`, `env3.release_curve`, `env4.release_curve`, `env5.release_curve`, `env6.release_curve` | **6** | **`env1`..`env6` "More / Curves" Sheet** (`moreKeys` on pages `env1..env6`; also direct-dragged on the Release segment curve in `05-envelopes-lfos.md` §2) |
| **LFO Smoothing, Stereo & Key Follow (LFO 1–8)** | For `N = 1..8`: `lfoN.smooth`, `lfoN.smooth_time`, `lfoN.stereo`, `lfoN.keytrack_transpose`, `lfoN.keytrack_tune` | **40** *(8 x 5)* | **`lfo1`..`lfo8` "More / Smooth & Key Follow" Sheet** (`moreKeys` on pages `lfo1..lfo8`; also in the LFO "More" drawer in `05-envelopes-lfos.md` §2) |
| **Mod Matrix Slots 05–16 Non-Amount Fields** *(note: `mod05..16.amount` ARE on pages 60–61)* | For `NN = 05..16`: `modNN.source`, `modNN.destination`, `modNN.power`, `modNN.bipolar`, `modNN.stereo`, `modNN.bypass` | **72** *(12 x 6)* | **Modulation Matrix Page & Long-Press Knob Context Sheet** (`06-modulation.md` §2.2, §2.3) |
| **Mod Matrix Slots 17–64 All Fields** | For `NN = 17..64`: `modNN.source`, `modNN.destination`, `modNN.amount`, `modNN.power`, `modNN.bipolar`, `modNN.stereo`, `modNN.bypass` | **336** *(48 x 7)* | **Modulation Matrix Page & Long-Press Knob Context Sheet** (`06-modulation.md` §2.2, §2.3) |
| **Total Unreached on Primary 8-Knob Pages** | `6 + 40 + 72 + 336` | **454** | **100% reachable via `env1..6` / `lfo1..8` "More" sheets (46) or Modulation Matrix (408)** |
| **Grand Total Verified** | `426 (on 61 pages) + 454 (More/Matrix)` | **880 / 880** | **Every single key in `docs/params.json` accounted for** |

---

## 7. Open questions (owner)

1. **Sequencer exclusions (`00-BRIEF.md`):** Serum 2 **Clips** (`[S p.219]`) and **Arpeggiator** (`[S p.244]`) are excluded because Kick provides them.
2. **Where `kick-pages.json` lives in the repo:** Should `kick-pages.json` be generated alongside `docs/params.json` by `tools/paramdump/synthyboy_paramdump` (from C++ `constexpr` page tables in `core/` so `shell-clap`'s `clap.remote-controls` and Kick read the exact same compiled table), or committed as a standalone JSON file in `docs/kick-pages.json`?
3. **Factory Preset Macro Wiring (`scripts/gen-factory-presets.py`):** Currently the 100 generated factory presets in `scripts/gen-factory-presets.py` leave `mod01..mod04` unwired on most patches. Should we update `scripts/gen-factory-presets.py` in Phase 5 so every factory preset comes pre-wired with the 4 category Macro assignments from §2.3?
4. **Hardware Q-Link Bank Filtering on MPC Key 61:** Should the MPC Q-Link page cycler skip pages for disabled modules (`osc2.on == 0`, `filter2.on == 0`, `<fx>.on == 0`) by default with a `[Show Disabled Pages]` toggle, or always cycle through all 61 pages in fixed order?
