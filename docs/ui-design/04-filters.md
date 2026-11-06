# Filters – mobile UI design

Scope: Filter 1, Filter 2, the FX filter, formant, all Serum filter types and their Var parameters, filter display, routing / blend. The effect chain (where the FX filter sits, `fx.order.*`) is 08.
Cites: `[S p.n]` Serum 2 manual, `[V p.n]` Vital unofficial manual, `[V src]` imported Vital source `core/engine-vital/upstream/src/synthesis/filters/`. Key family: `filterN` = filter1, filter2 (and `fxfilter`, which has the same keys under that prefix).
Status: `HAVE` = engine and params support it; `GAP` = missing, UI still designed here. "Nearest" mapping of a Serum type to a Vital model + style is an ear / structure judgement from the manuals, marked `[unverified]` until A/B-tested.

Our filter structure (docs/params.json): `filterN.model` (8: Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser) x `filterN.style` (6: A 12 dB, B 24 dB, C notch swap, D dual notch, E band peak notch, F shelf) x `filterN.blend` (0..100 %). Style meaning by model from `[V src]`: Analog / Dirty / Ladder / Digital use A-E (F = shelving only in Digital); Diode: A vs others; Comb: style = 3 feedback laws (Comb, Positive Flange, Negative Flange) x 2 filter styles (Low-High blend, Band spread); Formant: 0 = A-O-I-E, 1 = A-I-U-O, 2 = vocal tract; Phaser: A vs others `[unverified]`.

## 1. Function inventory

### 1.1 Filter module

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Power button; panel dims when off [S p.28, p.134] [V p.13] | both | `filterN.on` | HAVE |
| Filter 1, Filter 2 in one preset [S p.134] [V p.13] | both | `filter1.*`, `filter2.*` | HAVE |
| Third filter as an effect (FX filter, same models) | Vital | `fxfilter.*` | HAVE |
| Filter type menu (hierarchical), < > arrows, wheel [S p.135] | Serum | `filterN.model`, `filterN.style` | HAVE (8 models x 6 styles, picker below) |
| Filter type chooser [V p.13] | Vital | `filterN.model`, `filterN.style` | HAVE |
| Blend slider (type morph, e.g. LP -> BP -> HP) [V p.13-14] | Vital | `filterN.blend` | HAVE |
| Cutoff [S p.141] [V p.13] | both | `filterN.cutoff` (20..20000 Hz, log) | HAVE |
| Resonance [S p.142] [V p.13] | both | `filterN.resonance` | HAVE |
| Drive [S p.142] [V p.13] | both | `filterN.drive` (0..20 dB) | HAVE |
| Drive Clean Mode (-24 dB pre, +24 dB post) [S p.142] | Serum | none | GAP |
| Keytrack: Serum switch (first pitch-tracking oscillator, else MIDI note); Vital knob -100..100 % [S p.141] [V p.14] | both | `filterN.keytrack` | HAVE (knob); GAP (follow oscillator pitch incl. portamento) |
| Mix (dry / wet), no effect on Comb types [S p.143, p.149] [V p.13] | both | `filterN.mix` | HAVE |
| Pan: cutoff offset left vs. right [S p.143] | Serum | none | GAP |
| Level slider (dB) [S p.143] | Serum | none | GAP |
| Var parameter (the 4th knob, changes per filter type; label below) [S p.142] | Serum | per type, see 1.3 | per type |
| Filter display: Frequency Response [S p.140] [V p.13] | both | `filterN.cutoff`, `filterN.resonance`, `filterN.blend` | HAVE (UI draws the response) |
| Filter display: Frequency Response & FFT (live spectrum) [S p.140] | Serum | none | HAVE (UI: shell FFT of the filter input / output tap) |
| Filter display: Phase Response & FFT [S p.141] | Serum | none | HAVE (UI computes phase from the model) |
| Cycle display modes with Alt-click [S p.141, p.350] | Serum | none | HAVE (UI) |
| Drag in the display = cutoff (x) + resonance (y) [S p.142] [V p.13] | both | `filterN.cutoff`, `filterN.resonance` | HAVE |
| Graphically edited filter (PZ SVF "drawable filters") [S p.139] | Serum | none | GAP |
| Filter routing: S, A, B, C, N switches on the filter (which sources feed it) [S p.134] | Serum | `oscN.destination`, `noise.destination` | HAVE (written through the oscillator; Sub: GAP) |
| Filter routing: oscillator / sampler / other filter into a filter [V p.13] | Vital | `oscN.destination`, `filter.routing` | HAVE (sampler: GAP, 02) |
| Filter 1 / Filter 2 order: parallel, series 1 to 2, series 2 to 1 | Vital | `filter.routing` | HAVE |
| Filter output routing: other filter, Main, Direct, None; buses [S p.148-149] | Serum | `filter.routing` (series only) | GAP (Main / Direct / None / buses, see 02 mixer) |
| Routing knob blending an oscillator between Filter 1 and 2 [S p.31] | Serum | none (`oscN.destination` discrete incl. Filters 1+2) | GAP (continuous blend) |
| Filter as modulation source (FM / PD / AM / RM source) [S p.54-56, p.208] | Serum | none | GAP |
| Unrouted filter dimmed in the mixer [S p.148] | Serum | none | HAVE (UI) |
| Copy / paste / init / lock module [S p.33-34] | Serum | all `filterN.*` | see 01 |

### 1.2 Formant, comb, phaser specifics (Vital)

| Function | Origin | Our params (keys) | Engine status |
|----------|--------|-------------------|---------------|
| Formant: X / Y sliders replace cutoff / resonance, set vowel [V p.15] | Vital | `filterN.formant.x`, `filterN.formant.y` | HAVE |
| Formant: transpose slider replaces blend (shifts all peaks exponentially) [V p.15] | Vital | `filterN.formant.transpose` (-12..12 st) | HAVE |
| Formant: Peak knob replaces drive (resonance of the peaks) [V p.15] | Vital | `filterN.formant.resonance` (30..100 %) | HAVE |
| Formant: Spread knob replaces keytrack (spread out / converge to band-pass) [V p.15] | Vital | `filterN.formant.spread` | HAVE |
| Formant style (3 vowel sets incl. vocal tract) | Vital | `filterN.style` | HAVE |
| Comb: Cut knob replaces drive (low / band / high after the comb, or comb dry-wet from a start frequency) [V p.15] | Vital | `filterN.drive` (relabelled "Cut" for Comb) `[unverified]` | HAVE |
| Comb: blend transpose / offset between comb and second comb (Band spread) | Vital | `filterN.comb_offset` (0..84 st) | HAVE |
| Comb: positive vs. negative (flanger-like) feedback; positive = even harmonics, negative = odd [V p.15] | Vital | `filterN.style` | HAVE |
| Phaser: blend = number of notches (poles); positive vs. negative [V p.15] | Vital | `filterN.blend`, `filterN.style` | HAVE |

### 1.3 Serum filter types and Var functions [S p.135-139]

"Nearest" = model + style in our engine. Blend replaces Serum's "MORPH" style variables where noted.

| Serum category / type | Var function [S] | Nearest in our engine | Our params (keys) | Engine status |
|----------------------|------------------|-----------------------|-------------------|---------------|
| Normal: MG Low 6 / 12 / 18 / 24 (ladder LP) | FAT (saturation in resonance path) | Ladder model, Style A / B, blend 0 `[unverified]` | `filterN.model`, `filterN.style`, `filterN.blend` | HAVE for 12 / 24 dB; GAP for 6 / 18 dB slopes; GAP (FAT) |
| Normal: Low 6 / 12 / 18 / 24 (SVF LP) | FAT | Digital model, Style A / B, blend 0 | same | HAVE (12 / 24); GAP (6 / 18, FAT) |
| Normal: High 6 / 12 / 18 / 24 (SVF HP) | FAT | Digital model, Style A / B, blend 100 % | same | HAVE (12 / 24); GAP (6 / 18, FAT) |
| Normal: Band / Peak / Notch 12 / 24 | FAT | Digital / Analog, Style A / B blend 50 %, Style E (band-peak-notch) | same | HAVE (12 / 24, Style E); GAP (FAT) |
| Multi: LH, LB, LP, LN, HB, HP, HN, BP, BN, PP, PN, NN (dual SVF; first letter primary, second secondary) | FREQ (cutoff of the second SVF) | none (Style D "dual notch" is one fixed pair) | none | GAP |
| Multi: LBH, LPH, LNH, BPN (morphing SVF) | MORPH | Digital: LBH = Style A blend sweep, LNH = Style C, BPN = Style D / E | `filterN.blend`, `filterN.style` | HAVE (nearest) |
| Flanges: Cmb L, Flg L, Phs L (feedback LP) | LP FREQ | Comb model (Comb / +Flange / -Flange) / Phaser model | `filterN.model`, `filterN.style`, `filterN.blend` | HAVE (types); GAP (LP FREQ) |
| Flanges: Cmb H, Flg H, Phs H (feedback HP) | HP FREQ | same | same | HAVE (types); GAP (HP FREQ) |
| Flanges: Cmb HL, Flg HL, Phs HL (feedback HP + LP) | HL WID | same | same | HAVE (types); GAP (HL WID) |
| Misc: Low EQ 6 / 12, Band EQ 6 / 12, High EQ 6 / 12 | DB +/- (pass band gain) | Digital Style F (shelf) `[unverified]` | `filterN.style`, `filterN.blend` | HAVE (12 dB shelf shapes); GAP (6 dB, DB var) |
| Misc: Ring Mod, Ring Mod x2 | SPREAD (x2 only) | none | none | GAP |
| Misc: SampHold, SampHold- | none | none | none | GAP |
| Misc: Combs, Allpasses, Reverb (phase smear) | DAMP | none | none | GAP |
| Misc: French LP | BOEUF (second resonance) | Dirty model `[unverified]` | `filterN.model` | GAP (type and var) |
| Misc: German LP (zero-delay feedback LP) | none | Digital / Analog Style A blend 0 `[unverified]` | `filterN.model` | HAVE (nearest) |
| Misc: Add Bass | THRU (phase-rotated dry) | none | none | GAP |
| Misc: Formant-I, Formant-II, Formant-III (CUTOFF morphs vowels) | FORMNT (formant shift) | Formant model, Style 0 / 1 / 2 | `filterN.model`, `filterN.style`, `filterN.formant.x`, `.y`, `.transpose` | HAVE (3 vowel sets, nearest); FORMNT = `formant.transpose` HAVE |
| Misc: Bandreject | WIDTH (notch width) | Analog / Digital Style C, D | `filterN.style`, `filterN.resonance` | HAVE (nearest); GAP (WIDTH var) |
| Misc: Dist.Comb 1 / 2 LP / BP | COMBFRQ | Comb model, Style by feedback + filter style | `filterN.comb_offset` | HAVE (nearest, `comb_offset`) |
| Misc: Scream LP, Scream BP | SCREAM (feedback cutoff; DRIVE > 50 % = scream) | none (Dirty model with drive is the nearest) | `filterN.drive` | GAP |
| New: New Wsp | MORPH (LPF / Notch / HPF) | none | none | GAP |
| New: DJ Mixer Xfer | none | none | none | GAP |
| New: Diffusor | STAGES (all-pass stages) | Phaser model `[unverified]` | `filterN.model` | GAP (STAGES) |
| New: MG Ladder | SMOOTH | Ladder model | `filterN.model` | HAVE (type); GAP (SMOOTH) |
| New: Acid Ladder | SMOOTH | Diode model | `filterN.model` | HAVE (type); GAP (SMOOTH) |
| New: EMS Ladder | SMOOTH | none | none | GAP |
| New: MG Dirty | PAIN (overdrive) | Dirty model | `filterN.model`, `filterN.drive` | HAVE (type, drive as PAIN `[unverified]`) |
| New: PZ SVF (drawable) | SMOOTH | none | none | GAP |
| New: Comb 2 | FRQ2 | Comb model | `filterN.comb_offset` | HAVE (nearest, `comb_offset` as FRQ2 `[unverified]`) |
| New: Exp MM (LPF / Notch / HPF) | MIX (blend) | Digital Style C | `filterN.blend` | HAVE (nearest) |
| New: Exp BPF | none | Digital Style A blend 50 % | `filterN.style`, `filterN.blend` | HAVE (nearest) |

## 2. Screen design

### 2.1 FLT page, phone portrait (Filter 1 selected)

```
+------------------------------------------+
| [F1*][F2][FX]      (power)   [PAR|1>2|2>1] |  filter chips; routing chip = filter.routing (F1/F2 only)
| Model [Digital v]  Style [A 12 dB v]  [type list]|  type chips (filterN.model/style)
+------------------------------------------+
|  +--------------------------------------+|
|  |  response curve (+FFT / phase)       ||  drag: x = cutoff, y = resonance
|  |        ____                          ||  tap display mode chip: RESP | +FFT | PHASE
|  |  _____/    \____                     ||
|  +--------------------------------------+|
|  BLEND  LP ====o======== HP    (slider)   |  filterN.blend (label per model)
+------------------------------------------+
| (CUTOFF) (RES) (DRIVE) (MIX)              |  4 main knobs
+------------------------------------------+
| KEY TRACK | INPUTS S A B C N | MODEL SETTINGS |  "more" row
+------------------------------------------+
```

* Main knobs: `filterN.cutoff`, `filterN.resonance`, `filterN.drive`, `filterN.mix`.
* KEY TRACK sheet: `filterN.keytrack` knob (-100..100 %), "follow oscillator pitch" switch (GAP).
* INPUTS sheet: chips `S` (Sub, GAP), `A`, `B`, `C`, `N` toggle the feed of that source into this filter; each chip writes the source's `destination` (rule in section 3). A hint shows when the filter is off or nothing is routed.
* MODEL SETTINGS sheet: what is shown depends on the model (2.2).

### 2.2 Model-dependent layouts

```
Formant                          Comb                         Phaser
+---------------------+          +--------------------+       +--------------------+
|  XY pad: x=vowel x  |          | comb teeth display  |       | notch display       |
|           y=vowel y |          | cutoff line         |       |                     |
|  (dot, drag)        |          +--------------------+       +--------------------+
+---------------------+          | (CUTOFF)(RES)(CUT)(MIX)    | (CUTOFF)(RES)(DRIVE)(MIX)
| (TRANSP)(PEAK)(SPREAD)(MIX)    | OFFSET (comb_offset)       | BLEND = notches (+/-)
+---------------------+          | STYLE: comb|+flange|-flange | STYLE
                                 |        low-high | band spread
```

Formant main knobs: `filterN.formant.transpose`, `filterN.formant.resonance` (PEAK), `filterN.formant.spread`, `filterN.mix`; the XY pad edits `filterN.formant.x` / `.y`. Comb: `filterN.drive` shown as CUT `[unverified]`; the extra control is `filterN.comb_offset`. Models other than Formant, Comb hide the model settings chip.

### 2.3 Type picker sheet (Serum-style hierarchy on our model x style)

```
+------------------------------------------+
| FILTER TYPE      (search)                |
| Normal   > Ladder 12 | Ladder 24 | Low 12 | Low 24 | High 12 | High 24 | Band/Peak/Notch 12 | 24
| Multi    > LBH | LPH | LNH | BPN   (others greyed: not available)
| Flanges  > Comb | +Flange | -Flange | Phaser ...
| Misc     > Formant I/II/III | Bandreject | Dist.Comb | German LP | EQ shelf 12 ...
| New      > MG Ladder | Acid Ladder | MG Dirty | Comb 2 | Exp MM | Exp BPF | Diffusor ...
| --- raw: Model [8]  Style [6] ---         |
+------------------------------------------+
```

Each entry sets `filterN.model` + `filterN.style` (+ `filterN.blend` where the entry implies a position). Entries without engine support are listed greyed with the badge "not available" so the whole Serum list is visible. The "raw" row always shows the true Vital pair.

### 2.4 Tablet / landscape / MPC

```
+--------------------------------------------------------------------------+
| FILTER 1 [on] model style [type]  | route chip  | FILTER 2 [on] model style |
| response display + FFT           |  PAR | 1>2 | 2>1 | response display + FFT|
| BLEND slider                     |  inputs grid| BLEND slider              |
| CUTOFF RES DRIVE MIX KEY         |  S A B C N x 2| CUTOFF RES DRIVE MIX KEY|
+--------------------------------------------------------------------------+
| FX FILTER tile (collapsed; expands to the same layout)                    |
+--------------------------------------------------------------------------+
```

The inputs grid is a 5 x 2 matrix (sources x Filter 1 / Filter 2); each cell toggle follows the rule in section 3. Phone landscape: one filter at a time, the routing chip on the top bar.

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| Response display | One-finger drag | x = `filterN.cutoff` (log), y = `filterN.resonance` [S p.142] [V p.13] | Knobs below |
| Response display | Tap the mode chip (or double-tap the display) | Cycles Response, Response + FFT, Phase + FFT [S p.140-141] | Chip menu |
| Response display (Formant) | Drag the dot | `filterN.formant.x`, `.y` | XY numeric fields |
| Blend slider | Drag | `filterN.blend`; label changes with model (LP-BP-HP, notches, shelf, comb mix, phaser poles) [V p.14-15] | Numeric |
| Model / Style chips | Tap, `<` `>` | `filterN.model`, `filterN.style` | Type picker |
| Type picker entry | Tap | Sets model + style (+ blend) | Raw row |
| Power | Tap | `filterN.on` | Module sheet |
| INPUTS chip (source X to Filter n) | Tap | Source X `destination` rule below | Source's ROUTE sheet (02) |
| Routing chip | Tap segment | `filter.routing` Parallel, Series 1 to 2, Series 2 to 1 | none |
| Key track knob | Drag | `filterN.keytrack` | Numeric |
| Main knobs | As in 01 | Parameters | Long-press sheet |
| Var knob (GAP types) | Drag | disabled with badge | none |
| Filter header | Long-press | Module sheet (copy, paste, init, lock) [S p.33] | Menu |

Input rule (`oscN.destination` choices: Filter 1, Filter 2, Filters 1+2, Effects only, Direct out; same for `noise.destination`): toggling "on" for Filter 1 changes Filter 2 -> Filters 1+2, Effects only / Direct out -> Filter 1; toggling "off" for Filter 1 changes Filters 1+2 -> Filter 2, Filter 1 -> Effects only; Filter 2 symmetrically [V p.13: unrouting sends to effects].

## 4. States and edge cases

* Filter off: panel dims; the display shows a flat line; the sources routed to it pass through to the effects unfiltered [V p.7].
* Nothing routed: display dimmed, banner "no input routed" [S p.148].
* Resonance high: self-oscillation is possible [V p.13]; the display marks the peak in amber above 90 %.
* Cutoff: 20..20000 Hz log scale, shown in Hz and as note name when keytrack != 0; engine limits the cutoff to 0.45 x sample rate (README).
* Mix: for Comb types it has no audible effect [S p.149]; the knob is shown disabled for the Comb model.
* Blend label by model: Analog / Dirty / Ladder / Digital by style (LP-BP-HP, LP-Notch-HP, BP-Notch, BP-Peak-Notch, shelves); Diode LP-BP; Phaser notch count; Comb offset blend; Formant: slider replaced by transpose.
* Style availability: styles with no effect for the current model are shown dimmed `[unverified: per-model audit pending]`; README documents a style fallback for Ladder.
* Formant resonance minimum is 30 % (range 30..100).
* Drive 0..20 dB; filter mix 0..100 %; defaults: model Digital, style A, cutoff 12 kHz, resonance 20 %.
* Quality tiers: oversampled drive stage is tier dependent (ARCHITECTURE 3.4); on Low the drive knob is unchanged but the chip "tier" in the tooltip explains the reduced quality.
* FX filter on = effect in the chain; `fxfilter.on` default Off. Its position is set in 08 (`fx.order.1` .. `fx.order.9` choose which effect sits at each position; default: FX Filter at position 1); the FLT page shows the position as a read-only chip.
* Cutoff modulated by audio-rate sources (ARCHITECTURE 3.4) is a matrix feature (06).

## 5. Kick remote pages

Pages repeat for `filter1`, `filter2`, `fxfilter` ("Filter 1 Main", ...):

| Page | Params (<= 8) |
|------|---------------|
| Filter N Main | `filterN.on`, `filterN.model`, `filterN.style`, `filterN.cutoff`, `filterN.resonance`, `filterN.drive`, `filterN.blend`, `filterN.mix` |
| Filter N Model | `filterN.keytrack`, `filterN.comb_offset`, `filterN.formant.x`, `filterN.formant.y`, `filterN.formant.resonance`, `filterN.formant.spread`, `filterN.formant.transpose`, `filter.routing` |

For `fxfilter` the entry `filter.routing` is omitted (7 params): it only concerns Filter 1 / 2.

## 6. Coverage check

| Key group (params.json) | Where in section 2 |
|-------------------------|--------------------|
| `filterN.on`, `fxfilter.on` | 2.1 power, 2.4 |
| `filterN.model`, `filterN.style` | 2.1 chips, 2.3 picker |
| `filterN.cutoff`, `filterN.resonance` | 2.1 display + main knobs |
| `filterN.drive`, `filterN.mix` | 2.1 main knobs |
| `filterN.blend` | 2.1 blend slider |
| `filterN.keytrack` | 2.1 KEY TRACK sheet |
| `filterN.comb_offset` | 2.2 Comb layout |
| `filterN.formant.x`, `.y`, `.resonance`, `.spread`, `.transpose` | 2.2 Formant layout |
| `filter.routing` | 2.1 routing chip, 2.4 |
| `fxfilter.*` | 2.1 chip FX, 2.4 tile (effect position: 08) |
| `oscN.destination`, `noise.destination` | 2.1 INPUTS sheet, section 3 rule |

## 7. Open questions (owner)

1. Roughly 35 of the 60 Serum filter types are `GAP`. Which DSP is added (Serum-like types are not in Vital)? Until then the picker shows them greyed.
2. The "nearest" mappings in 1.3 are judgement calls `[unverified]`; they need an A/B listening pass before the type names in the picker are final.
3. Cutoff keytrack following the oscillator pitch (Serum) versus MIDI note (Vital): which one is the engine's? Needs a check of `filter_N_keytrack` behaviour with portamento.
4. Per-filter Pan (L / R cutoff offset) and Level (dB) exist in Serum only; wanted?
5. Continuous oscillator blend between Filter 1 and Filter 2 (Serum routing knob) vs. discrete `destination`.
