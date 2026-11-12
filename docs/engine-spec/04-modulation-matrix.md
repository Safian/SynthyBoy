# 04 Modulation matrix

Status: final (2026-11-12). Covers the 64 modulation slots: the signal law of one slot, the destination domains (the "engine's own scale" that
`docs/PARAMETERS.md` refers to), summation, mono / voice evaluation, rate, order, macros, aux source, rise / fall / delay, curves, loops.
The vocabulary (the list of sources and the list of destinations = every key flagged `mod`, in table order) is `core/include/synthyboy/modulation.h` and
`core/src/modulation.cpp` (ours). Sources are produced by documents 1 and 3.

## 1. Model

* 64 slots, each with keys `modNN.source` (choice, 0 = none), `.destination` (choice, 0 = none; value k = the k-th key flagged `mod`, in table order, plus the appended
  destinations `modNN.amount` x 64, `master.portamento.time`, `master.polyphony`), `.amount` (-100 .. 100 %), `.power` (-10 .. 10), `.bipolar`, `.stereo`, `.bypass`,
  and the version 3 keys (`aux_source`, `aux_inv`, `aux_power`, `aux_output`, `rise`, `fall`, `delay`, `delay_beats`, `delay_sync`).
* A slot with source or destination "none", or `bypass` On, contributes exactly 0 (and costs nothing).
* Any number of slots can share a destination; their contributions add. Any number can share a source.

## 2. The law of one slot

Let `x` be the value of the source (0 .. 1, see 5), `m = clamp(amount, -1, 1)` the amount as a fraction (percent / 100, itself a modulatable destination),
`B` = 1 if `bipolar` else 0, `p = -power`, and `D` the **destination width** of section 3. The contribution to the destination **control value** is

```
y      = x * (B + 1) - B                    -- unipolar: x in 0..1 ; bipolar: 2x - 1 in -1..1
c_slot = D * m * aux * sign(y) * P(|y|, p) * (1 - B/2)
```

* `P` is the power scale of the README (so `power = 0` is linear). The bracket `(1 - B/2)` makes **bipolar swing +-0.5 of the full range** for amount 100 % (the
  unipolar swing is 0 .. 1): a bipolar slot with amount 100 % moves the destination by half its width each way.
* `stereo` On multiplies the **right channel lane** of the result by -1 (the left lane is untouched): the contribution is `+c` on the left and `-c` on the right
  (used for pan, detune, filter cutoff differences between the channels). Destinations that have one value per voice (no lanes) receive the left lane.
* `aux` = 1 unless an aux source is set (section 7).
* The audio-rate path with `power = 0` and no curve is `(x - B/2) * m * D * aux * stereo_sign` (identical, evaluated per sample).
* The amount is interpolated linearly across the chunk when it changes; on a note start the interpolation starts at the new value (no ramp from the
  last note). The power is interpolated likewise.
* Source value handling at control rate: `x` is clamped to [0, 1] before the curve and the bipolar mapping (a source that is outside 0..1 is clipped;
  audio-rate sources are not clipped).

## 3. Destinations: the control domain

A destination key has a **control value** `c` (the engine's internal number) that the knob sets through a fixed map, and a DSP value derived from `c` after modulation:

```
knob plain  --(map K)-->  c0          the base
c = c0 + sum over slots of c_slot       the modulated control; summed in the control domain, BEFORE the final transform
DSP value = T(c)                        T per destination
```

Maps `K` (plain -> `c0`) and transforms `T`:

| Kind | `K` | `T` | Used by |
|------|-----|-----|---------|
| lin | `c = plain * a` | identity (some controls clamp at use) | most keys (percent keys: `a = 0.01`; degrees: `a = 1/360`) |
| sqrt | `c = sqrt(plain * a)` | `c^2` (negative `c` -> 0) for the control-rate keys; the audio-rate level keys use `c` directly and the oscillator squares it (document 1 sec 8) | levels, unison detune, EQ resonances, reverb chorus amount |
| quart | seconds -> `c = seconds^(1/4)` | `c^4` seconds (negative `c` -> 0) | envelope times |
| log2s | `c = log2(seconds)`, floored at `a` | `2^c` seconds | portamento time, LFO smooth time, chorus delays, reverb decay |
| log2f | `c = log2(Hz)` (delay times: `log2(1000/ms)`, a frequency) | `2^c` Hz (delay: period `1/2^c`) | LFO / effect rates, delay time |
| note | `c = 12 log2(f / 8.1758)` (a MIDI note), f limited to 20 Hz .. 0.45 sr | identity (a note number), converted to Hz by the consumer | cutoffs, EQ and effect filter frequencies |
| vol | `c = (dB + 80)^2` (dB <= -60 -> c = 0), dB capped at 6 | `dB = sqrt(max(c, 0)) - 80` -> gain `10^(dB/20)`, zero when dB <= -80 | `master.volume` |

* The width `D` of a destination is `max - min` of the **control range** in the table of section 9. A slot's amount of 100 % therefore moves the *control* by the whole range:
  the filter cutoff by the whole keyboard range of 128 semitones; an envelope time by 1.4142 or 2.3784 in the fourth-root domain (very non-linear in seconds); the unison detune by 10 in
  the square-root domain (`(sqrt(plain) + 10 m x)^2` percent); `master.volume` by 7399 in the squared-dB domain (a small amount moves the volume a lot at the quiet end and
  almost nothing at the loud end).
* The oscillator frame position and its spread use the width of the **frame count of the built-in tables**, 63 frames (126 for the spread), independent of the loaded table (so a 100 % slot sweeps 63
  frames; on a table with fewer frames the position clamps at the last).
* Nothing clamps `c` to the control's range **except the consumer** (section 4): filters clamp the cutoff note to the audible range, the oscillators round and clamp the unison voice count to 1 .. 16, the frame position
  is clamped to the table, times are non-negative, mixes clamp to 0..1 where it matters. A modulated `osc.level` can exceed 1 (gain `c^2` with `c > 1`: measured +3.3 dB over unity for level 50 % +
  macro 100 % x amount 50 %).

### 3.1 Mono and voice destinations, mono and voice sources

A destination exists in two forms where the parameter lives in the voice (oscillators, filters, envelopes, per-voice LFO parameters, pan, master tune, portamento): a **per-voice** control and a global one (the
knob value). A source is per-voice (envelopes, LFOs, random LFOs, velocity, note, aftertouch, ...) or global (macros, the sources of the global modulators).

* voice source -> voice destination: each voice's own value drives its own control (this is the normal case).
* global source -> voice destination: the same value adds to every voice.
* voice source -> global destination (all effect parameters, `master.volume`, `master.stereo_spread`, `master.stereo_routing`, `master.polyphony`, bus and direct levels): the value of the **most recently started active voice**
  is used (both channels of its value copied), at control rate. With no active voice the connection is not evaluated and keeps its last value (so an envelope that modulates a reverb mix stays at its last value after the
  voices die, until the next note). Sources of the global kind are evaluated even with no voices.
* The `stereo` flag of such a slot still produces a left/right difference.

## 4. Rate

A slot is evaluated at **audio rate** if (and only if) its destination is an audio-rate control and its source is able to run at audio rate; otherwise at control rate (once per chunk of at most 128 samples at the host rate).
Audio-rate destinations: `oscN.transpose`, `oscN.tune`, `oscN.level`, `oscN.phase`, `noise.level`, `filterN.cutoff`, `filterN.formant.x`, `.y`, `.transpose`, `eq.*.cutoff` (low, mid, mid2, high), `distortion.drive`,
`distortion.filter_cutoff`, `phaser.center`. When such a slot is connected, its source (envelope, LFO) is switched to audio rate for as long as the connection exists. Sources that are control rate by nature
(velocity, note, aftertouch, macro, the per-note randoms) stay control rate and the slot is then also control rate (the result is a ramp across the chunk). The levels, phases and cutoffs that are audio-rate destinations are smoothed with the 5 Hz one-pole of document 1 section 10 after the sum.

At control rate a value is held for a chunk; consumers that are sensitive (pitch, level, pan, warp amount, filter coefficients) interpolate linearly across the chunk.

**Better:** give every slot audio rate where the destination can use it; the original's control-rate chunks (up to 128 samples = 2.7 ms) produce audible zipper noise on fast LFOs on cutoff in some destinations (resolved there by audio-rate paths).

## 5. Sources (values given to a slot)

All in 0 .. 1 unless noted. "Per voice" unless noted.

| Source | Value |
|--------|-------|
| Env 1 .. 6 | the envelope value (doc 3) |
| LFO 1 .. 10 | the LFO value 0 .. 1 (doc 3); bipolar through the slot flag |
| Random LFO 1 .. 4 | 0 .. 1 |
| Macro 1 .. 8 | the macro knob (global) |
| Velocity | note-on velocity |
| Release Velocity / Lift | 0.5 until the note off, then the release velocity (0.5 when the controller sends none) |
| Note | `(note incl. glide and bend) / 127` |
| Oct Note (Note in octave) | `fract(note/12)` |
| Aftertouch | channel pressure, per voice (doc 1 sec 6.3) |
| Poly Aftertouch | per-note pressure (MPE, poly key pressure) |
| Slide | per-note / channel Y (CC 74) |
| Pitch Wheel | `0.5 + 0.5 * bend` of the channel |
| Mod Wheel | CC 1 of the channel |
| Random per Note | doc 3 sec 5 |
| Stereo | left channel 1, right channel 0 (with the bipolar flag: +0.5 / -0.5 -> a stereo difference; with `stereo` On it doubles) |
| Voice Index / Voice Mod 1, 2 | doc 1 sec 11 |
| Oscillator A, B, C / Noise / Sub / Filter 1 / Filter 2 | the last sample of the output of the block before; audio-rate (the value in -1 .. 1 is clipped to 0 .. 1 at control rate: use the bipolar flag |
| Active Voices | `min(1, active/polyphony)` (global) |
| Note-On Alt 1 / 2, Rand 2, Rand (Discrete), Expression X | doc 1 sec 11 |
| Fixed | 1 |

## 6. Evaluation order and loops

* Within a chunk the engine evaluates in dependency order: global modulators (macros, global randoms, tempo and transport values), then per voice the note/performance sources, envelopes, LFOs, random LFOs, then the slots, then
  the processors that read the modulated controls (oscillators, filters, sub, noise, then voice sum, the effect chain, the output stage). A slot that modulates a modulator (e.g. `lfo2.rate` driven by
  `env2`) is evaluated before that modulator in the same chunk, as long as the dependency chain has no cycle.
* A slot whose connection would close a **cycle** (the source depends on the destination, possibly through other slots) is **not wired** (it contributes 0); a later change that removes the cycle wires it.
  The check is per connection and runs on the slot change, not per chunk.
* Exceptions that are one chunk late by design: the oscillator warps that read Filter 1 / Filter 2 (document 2 sec 6.4) and the oscillator-to-oscillator FM/RM when two oscillators modulate each other (the second one gets the previous block).
* The matrix is evaluated for the active voices only; a note start resets the per-slot interpolation, rise/fall state and delay timer of its voice.

## 7. Extensions of a slot (our design, Serum 2 behaviour)

* **Aux source** (`aux_source`, `aux_inv`, `aux_power`, `aux_output`): `aux = a * aux_output/100` with `a = clamp(aux_x, 0, 1)` (a bipolar aux source first mapped `(x + 1)/2`), inverted `1 - a` if `aux_inv`,
  and if `aux_power != 0` `a = P(a, -aux_power)`. The slot amount is multiplied by `aux`. Without an aux source `aux = 1`.
* **Curve** (velocity and note curves, per-slot maps): before the bipolar mapping the source value `x` is replaced by `C(x)` where `C` is the drawn curve (2048 points, Catmull-Rom, clamped to
  -1 .. 1); for the velocity and note sources the curve (`curve.velocity.*`, `curve.note.*`) is applied this way. With `curve.*.legato` On a legato note does not jump: the curved value glides from the old to the new one with the
  one-pole time constant `master.portamento.time` (coefficient `1 - exp(-chunk/time)`).
* **Rise / Fall**: a slew limiter on the source in front of the curve: a one-pole towards the input whose time `T` (ms) is the span in which the source covers 90 % of a step (`coefficient = 1 - exp(-dt * ln10 / T)`, `dt` = chunk or sample),
  `rise` when the input is above the held value and `fall` when below; both 0 = off. A fresh voice starts on the source's value. Times are modulatable (0 .. 5000 ms).
* **Delay** (`delay` ms 0 .. 4000, or `delay_beats` 0 .. 8 with `delay_sync`): the slot's amount is multiplied by 0 until the voice has played for the delay (per voice, from the note on), then by 1; the gate is applied through the
  amount's per-chunk interpolation (a ramp of one chunk).

## 8. Notes for the implementation ("better")

* Sum in the control domain exactly as specified; this is the part of the sound that makes the matrix feel the same (a cutoff moves in semitones, a time in the fourth-root domain).
* Audio-rate LFOs on cutoff are the most audible; give the filter a per-sample modulation input (document 5).
* The "last active voice" rule for global destinations is a sound feature (mono effects follow the newest note); an alternative (average or max of the voices) changes presets.
* 64 slots x 16 voices is cheap if only wired slots are evaluated (as above).

## 9. Test vectors (M1 .. M9)

Setup for all: Sine, filter off, `master.volume 0`, `env1.attack 0`, `env1.release 50`, note 69 held 1 s, level read as the peak dB of the 440 Hz line (0.3 .. 0.8 s) unless stated. Slot 1 = (macro 1 -> destination).

| # | Setup | Expected |
|---|-------|----------|
| M1 | `osc1.level 50`, `macro.1 100`, slot `macro1 -> osc1.level` amount +50 % | +3.28 dB (level control `sqrt(0.5) + 0.5 = 1.207`, gain `1.457`) |
| M2 | M1 with `mod01.bipolar 1` | -0.75 dB (control `0.7071 + 0.5 * 0.5`, gain `0.9482`) |
| M3 | `osc1.level 50`, `macro.1 50`, amount +100 % | +3.28 dB (same control) |
| M4 | `osc1.level 100`, `macro.1 50`, amount -50 % | -4.99 dB (control `1 - 0.25 = 0.75`, gain `0.5625`) |
| M5 | `osc1.level 100`, `macro.1 50`, amount -100 %, `mod01.power -5` / `+5` | -1.36 dB / -44.79 dB (`1 - P(0.5, +-5)` squared) |
| M6 | `macro.1 50`, `macro1 -> osc1.transpose` +25 % | 880.00 Hz (`+0.5 * 0.25 * 96 = 12` st) |
| M7 | `macro.1 100`, `bipolar 1`, `macro1 -> osc1.transpose` +50 % | 1760.0 Hz (`+0.5 * 0.5 * 96 = 24` st) |
| M8 | `macro.1 100`, `macro1 -> osc1.pan` +100 % | left channel silent, right channel -3.01 dB (pan control +1 -> pan +1) |
| M9 | M8 with `mod01.stereo 1` | both channels silent (the right lane gets -1: pan lanes +1 / -1) |
| M10 | LFO to pan: see document 3 L1 .. L4 | |

Tolerance +-0.1 dB; frequencies +-0.2 Hz.

## 10. Appendix: destination domains

`Domain` as in section 3 (the map `K`), `min` / `max` the range of the control `c`, `width` = `D`. Percent keys have `c = plain/100`. The 24 parameters of each effect instance
(`fxiNN.p01 .. p24`) are modulated in their own normalised 0 .. 1 range (width 1). `oscN` entries for the frame use the 63 / 126 widths of section 3.

| Key | Domain `c` | min | max | width |
|-----|------------|-----|-----|-------|
| `master.volume` | vol | 0 | 7399.44 | 7399.44 |
| `master.tune` | lin | -1 | 1 | 2 |
| `oscN.level` | sqrt | 0 | 1 | 1 |
| `oscN.pan` | lin | -1 | 1 | 2 |
| `oscN.transpose` | lin | -48 | 48 | 96 |
| `oscN.tune` | lin | -1 | 1 | 2 |
| `oscN.frame` | lin | 0 | 63 | 63 |
| `oscN.frame_spread` | lin | -63 | 63 | 126 |
| `oscN.phase` | lin | 0 | 1 | 1 |
| `oscN.random_phase` | lin | 0 | 1 | 1 |
| `oscN.unison.voices` | lin | 1 | 16 | 15 |
| `oscN.unison.detune` | sqrt | 0 | 10 | 10 |
| `oscN.unison.detune_curve` | lin | -5 | 5 | 10 |
| `oscN.unison.spread` | lin | 0 | 1 | 1 |
| `oscN.unison.blend` | lin | 0 | 1 | 1 |
| `oscN.morph.amount` | lin | 0 | 1 | 1 |
| `oscN.morph.spread` | lin | -0.5 | 0.5 | 1 |
| `oscN.warp.amount` | lin | 0 | 1 | 1 |
| `oscN.warp.phase` | lin | 0 | 1 | 1 |
| `oscN.warp.spread` | lin | -0.5 | 0.5 | 1 |
| `noise.level` | sqrt | 0 | 1 | 1 |
| `noise.pan` | lin | -1 | 1 | 2 |
| `filterN.cutoff` | note | 8 | 136 | 128 |
| `filterN.resonance` | lin | 0 | 1 | 1 |
| `filterN.drive` | lin | 0 | 20 | 20 |
| `filterN.blend` | lin | 0 | 2 | 2 |
| `filterN.mix` | lin | 0 | 1 | 1 |
| `filterN.keytrack` | lin | -1 | 1 | 2 |
| `filterN.comb_offset` | lin | 0 | 84 | 84 |
| `filterN.formant.x` | lin | 0 | 1 | 1 |
| `filterN.formant.y` | lin | 0 | 1 | 1 |
| `filterN.formant.resonance` | lin | 0.3 | 1 | 0.7 |
| `filterN.formant.spread` | lin | -1 | 1 | 2 |
| `filterN.formant.transpose` | lin | -12 | 12 | 24 |
| `fxfilter.cutoff` | note | 8 | 136 | 128 |
| `fxfilter.resonance` | lin | 0 | 1 | 1 |
| `fxfilter.drive` | lin | 0 | 20 | 20 |
| `fxfilter.blend` | lin | 0 | 2 | 2 |
| `fxfilter.mix` | lin | 0 | 1 | 1 |
| `fxfilter.keytrack` | lin | -1 | 1 | 2 |
| `fxfilter.comb_offset` | lin | 0 | 84 | 84 |
| `fxfilter.formant.x` | lin | 0 | 1 | 1 |
| `fxfilter.formant.y` | lin | 0 | 1 | 1 |
| `fxfilter.formant.resonance` | lin | 0.3 | 1 | 0.7 |
| `fxfilter.formant.spread` | lin | -1 | 1 | 2 |
| `fxfilter.formant.transpose` | lin | -12 | 12 | 24 |
| `envN.delay` | quart | 0 | 1.41421 | 1.41421 |
| `envN.attack` | quart | 0 | 2.37842 | 2.37842 |
| `envN.hold` | quart | 0 | 1.41421 | 1.41421 |
| `envN.decay` | quart | 0 | 2.37842 | 2.37842 |
| `envN.sustain` | lin | 0 | 1 | 1 |
| `envN.release` | quart | 0 | 2.37842 | 2.37842 |
| `lfoN.rate` | log2f | -7 | 9 | 16 |
| `lfoN.phase` | lin | 0 | 1 | 1 |
| `lfoN.fade_in` | lin | 0 | 8 | 8 |
| `lfoN.delay` | lin | 0 | 4 | 4 |
| `lfoN.smooth_time` | log2s | -10 | 4 | 14 |
| `lfoN.stereo` | lin | -0.5 | 0.5 | 1 |
| `lfoN.keytrack_transpose` | lin | -60 | 36 | 96 |
| `lfoN.keytrack_tune` | lin | -1 | 1 | 2 |
| `rlfoN.rate` | log2f | -7 | 9 | 16 |
| `rlfoN.keytrack_transpose` | lin | -60 | 36 | 96 |
| `rlfoN.keytrack_tune` | lin | -1 | 1 | 2 |
| `macro.N` | lin | 0 | 1 | 1 |
| `chorus.mix` | lin | 0 | 1 | 1 |
| `chorus.rate` | log2f | -6 | 3 | 9 |
| `chorus.depth` | lin | 0 | 1 | 1 |
| `chorus.feedback` | lin | -0.95 | 0.95 | 1.9 |
| `chorus.delay1` | log2s | -10 | -5.64386 | 4.35614 |
| `chorus.delay2` | log2s | -10 | -5.64386 | 4.35614 |
| `chorus.filter_cutoff` | note | 8 | 136 | 128 |
| `chorus.filter_spread` | lin | 0 | 1 | 1 |
| `compressor.mix` | lin | 0 | 1 | 1 |
| `compressor.attack` | lin | 0 | 1 | 1 |
| `compressor.release` | lin | 0 | 1 | 1 |
| `compressor.low.gain` | lin | -36 | 36 | 72 |
| `compressor.mid.gain` | lin | -36 | 36 | 72 |
| `compressor.high.gain` | lin | -36 | 36 | 72 |
| `delay.mix` | lin | 0 | 1 | 1 |
| `delay.feedback` | lin | -1 | 1 | 2 |
| `delay.time` | log2f | -2 | 9 | 11 |
| `delay.time2` | log2f | -2 | 9 | 11 |
| `delay.filter_cutoff` | note | 8 | 136 | 128 |
| `delay.filter_spread` | lin | 0 | 1 | 1 |
| `distortion.drive` | lin | -30 | 30 | 60 |
| `distortion.mix` | lin | 0 | 1 | 1 |
| `distortion.filter_cutoff` | note | 8 | 136 | 128 |
| `distortion.filter_resonance` | lin | 0 | 1 | 1 |
| `distortion.filter_blend` | lin | 0 | 2 | 2 |
| `eq.low.cutoff` | note | 8 | 136 | 128 |
| `eq.low.gain` | lin | -24 | 24 | 48 |
| `eq.low.resonance` | sqrt | 0 | 1 | 1 |
| `eq.mid.cutoff` | note | 8 | 136 | 128 |
| `eq.mid.gain` | lin | -24 | 24 | 48 |
| `eq.mid.resonance` | sqrt | 0 | 1 | 1 |
| `eq.high.cutoff` | note | 8 | 136 | 128 |
| `eq.high.gain` | lin | -24 | 24 | 48 |
| `eq.high.resonance` | sqrt | 0 | 1 | 1 |
| `flanger.mix` | lin | 0 | 0.5 | 0.5 |
| `flanger.feedback` | lin | -1 | 1 | 2 |
| `flanger.rate` | log2f | -5 | 2 | 7 |
| `flanger.depth` | lin | 0 | 1 | 1 |
| `flanger.center` | note | 8 | 136 | 128 |
| `flanger.phase_offset` | lin | 0 | 1 | 1 |
| `phaser.mix` | lin | 0 | 1 | 1 |
| `phaser.feedback` | lin | 0 | 1 | 1 |
| `phaser.rate` | log2f | -5 | 2 | 7 |
| `phaser.depth` | lin | 0 | 48 | 48 |
| `phaser.center` | note | 8 | 136 | 128 |
| `phaser.blend` | lin | 0 | 2 | 2 |
| `phaser.phase_offset` | lin | 0 | 1 | 1 |
| `reverb.mix` | lin | 0 | 1 | 1 |
| `reverb.size` | lin | 0 | 1 | 1 |
| `reverb.decay` | log2s | -6 | 6 | 12 |
| `reverb.predelay` | lin | 0 | 0.3 | 0.3 |
| `reverb.pre_low_cut` | note | 0 | 128 | 128 |
| `reverb.pre_high_cut` | note | 0 | 128 | 128 |
| `reverb.low_shelf_cutoff` | note | 0 | 128 | 128 |
| `reverb.low_shelf_gain` | lin | -6 | 0 | 6 |
| `reverb.high_shelf_cutoff` | note | 0 | 128 | 128 |
| `reverb.high_shelf_gain` | lin | -6 | 0 | 6 |
| `reverb.chorus_amount` | sqrt | 0 | 1 | 1 |
| `reverb.chorus_rate` | log2f | -8 | 3 | 11 |
| `envN.delay_beats` | quart | 0 | 1.41421 | 1.41421 |
| `envN.attack_beats` | quart | 0 | 2.37842 | 2.37842 |
| `envN.hold_beats` | quart | 0 | 1.41421 | 1.41421 |
| `envN.decay_beats` | quart | 0 | 2.37842 | 2.37842 |
| `envN.release_beats` | quart | 0 | 2.37842 | 2.37842 |
| `oscN.sample.scan` | lin | -1 | 1 | 2 |
| `oscN.sample.uni_start` | lin | 0 | 1 | 1 |
| `oscN.sample.uni_span` | lin | 0 | 1 | 1 |
| `oscN.warp2.amount` | lin | 0 | 1 | 1 |
| `oscN.warp2.phase` | lin | 0 | 1 | 1 |
| `oscN.warp2.spread` | lin | -0.5 | 0.5 | 1 |
| `oscN.filter_blend` | lin | 0 | 1 | 1 |
| `noise.filter_blend` | lin | 0 | 1 | 1 |
| `oscN.bus1_send` | lin | 0 | 1 | 1 |
| `oscN.bus2_send` | lin | 0 | 1 | 1 |
| `noise.bus1_send` | lin | 0 | 1 | 1 |
| `noise.bus2_send` | lin | 0 | 1 | 1 |
| `filterN.bus1_send` | lin | 0 | 1 | 1 |
| `filterN.bus2_send` | lin | 0 | 1 | 1 |
| `bus1.level` | lin | -60 | 6 | 66 |
| `bus2.level` | lin | -60 | 6 | 66 |
| `direct.level` | lin | -60 | 6 | 66 |
| `filterN.var` | lin | 0 | 1 | 1 |
| `filterN.pan` | lin | -1 | 1 | 2 |
| `filterN.level` | lin | -60 | 12 | 72 |
| `fxfilter.var` | lin | 0 | 1 | 1 |
| `fxfilter.pan` | lin | -1 | 1 | 2 |
| `fxfilter.level` | lin | -60 | 12 | 72 |
| `hyperdim.rate` | lin | 0.05 | 10 | 9.95 |
| `hyperdim.detune` | lin | 0 | 100 | 100 |
| `hyperdim.mix` | lin | 0 | 100 | 100 |
| `hyperdim.dim_size` | lin | 0 | 100 | 100 |
| `hyperdim.dim_mix` | lin | 0 | 100 | 100 |
| `hyperdim.level` | lin | -24 | 12 | 36 |
| `utility.lpf` | lin | 20 | 20000 | 19980 |
| `utility.hpf` | lin | 20 | 20000 | 19980 |
| `utility.mono_freq` | lin | 20 | 500 | 480 |
| `utility.width` | lin | 0 | 200 | 200 |
| `utility.pan` | lin | -100 | 100 | 200 |
| `utility.mix` | lin | 0 | 100 | 100 |
| `utility.level` | lin | -24 | 12 | 36 |
| `bode.shift` | lin | 0 | 100 | 100 |
| `bode.range` | lin | 1 | 5000 | 4999 |
| `bode.dir` | lin | -100 | 100 | 200 |
| `bode.width` | lin | 0 | 100 | 100 |
| `bode.delay` | lin | 1 | 2000 | 1999 |
| `bode.feed` | lin | 0 | 100 | 100 |
| `bode.balance` | lin | -100 | 100 | 200 |
| `bode.blur` | lin | 0 | 100 | 100 |
| `bode.mix` | lin | 0 | 100 | 100 |
| `bode.level` | lin | -24 | 12 | 36 |
| `convolve.size` | lin | 0 | 100 | 100 |
| `convolve.tone` | lin | -100 | 100 | 200 |
| `convolve.predelay` | lin | 0 | 500 | 500 |
| `convolve.attack` | lin | 0 | 2000 | 2000 |
| `convolve.decay` | lin | 0 | 100 | 100 |
| `convolve.damp` | lin | 0 | 100 | 100 |
| `convolve.ir_gain` | lin | -40 | 24 | 64 |
| `convolve.mix` | lin | 0 | 100 | 100 |
| `convolve.level` | lin | -24 | 12 | 36 |
| `splitter_lh.freq` | lin | 20 | 20000 | 19980 |
| `splitter_lh.level` | lin | -24 | 12 | 36 |
| `splitter_lmh.freq_low` | lin | 20 | 20000 | 19980 |
| `splitter_lmh.freq_high` | lin | 20 | 20000 | 19980 |
| `splitter_lmh.level` | lin | -24 | 12 | 36 |
| `splitter_ms.level` | lin | -24 | 12 | 36 |
| `chorus.level` | lin | -24 | 12 | 36 |
| `compressor.level` | lin | -24 | 12 | 36 |
| `delay.level` | lin | -24 | 12 | 36 |
| `distortion.level` | lin | -24 | 12 | 36 |
| `eq.level` | lin | -24 | 12 | 36 |
| `flanger.level` | lin | -24 | 12 | 36 |
| `phaser.level` | lin | -24 | 12 | 36 |
| `reverb.level` | lin | -24 | 12 | 36 |
| `oscN.pitch_coarse` | lin | -48 | 48 | 96 |
| `oscN.fixed_pitch` | lin | 0 | 127 | 127 |
| `oscN.unison.range` | lin | 0 | 48 | 48 |
| `oscN.unison.blend_shape` | lin | -1 | 1 | 2 |
| `compressor.crossover_low` | lin | 20 | 1000 | 980 |
| `compressor.crossover_high` | lin | 200 | 16000 | 15800 |
| `delay.offset` | lin | 0.25 | 4 | 3.75 |
| `distortion.filter_keytrack` | lin | -1 | 1 | 2 |
| `phaser.depth2` | lin | -48 | 48 | 96 |
| `reverb.width` | lin | 0 | 2 | 2 |
| `reverb.diff_a` | lin | 0 | 1 | 1 |
| `reverb.diff_b` | lin | 0 | 1 | 1 |
| `sub.octave` | lin | -4 | 4 | 8 |
| `sub.tune` | lin | -48 | 48 | 96 |
| `sub.level` | sqrt | 0 | 1 | 1 |
| `sub.pan` | lin | -1 | 1 | 2 |
| `sub.filter_blend` | lin | 0 | 1 | 1 |
| `sub.bus1_send` | lin | 0 | 1 | 1 |
| `sub.bus2_send` | lin | 0 | 1 | 1 |
| `noise.pitch` | lin | -48 | 48 | 96 |
| `noise.fine` | lin | -1 | 1 | 2 |
| `noise.phase` | lin | 0 | 1 | 1 |
| `master.stereo_spread` | lin | 0 | 1 | 1 |
| `lfoN.swing` | lin | 0 | 1 | 1 |
| `master.stereo_routing` | lin | 0 | 1 | 1 |
| `sub.phase` | lin | 0 | 1 | 1 |
| `noise.stereo` | lin | 0 | 1 | 1 |
| `noise.filter` | lin | -1 | 1 | 2 |
| `delay.offset_left` | lin | 0.25 | 4 | 3.75 |
| `reverb.feedback` | lin | 0 | 1 | 1 |
| `oscN.sample.slice` | lin | -1 | 1 | 2 |
| `oscN.gran.window_amount` | lin | 0 | 1 | 1 |
| `oscN.gran.window_skew` | lin | -1 | 1 | 2 |
| `oscN.gran.density` | lin | 0.1 | 1000 | 999.9 |
| `oscN.gran.length` | lin | 1 | 4000 | 3999 |
| `oscN.gran.length_percent` | lin | 5 | 1000 | 995 |
| `oscN.gran.position` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_offset` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_dir` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_pitch` | lin | 0 | 24 | 24 |
| `oscN.gran.rand_length` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_pan` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_level` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_window` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_skew` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_warp` | lin | 0 | 1 | 1 |
| `oscN.gran.rand_warp2` | lin | 0 | 1 | 1 |
| `oscN.gran.xy_y` | lin | 0 | 1 | 1 |
| `oscN.spec.freq_lo` | lin | 20 | 20000 | 19980 |
| `oscN.spec.freq_hi` | lin | 20 | 20000 | 19980 |
| `oscN.spec.cut` | lin | -1 | 1 | 2 |
| `oscN.spec.mix` | lin | 0 | 1 | 1 |
| `oscN.spec.freq_shift` | lin | -2000 | 2000 | 4000 |
| `oscN.spec.position` | lin | 0 | 1 | 1 |
| `oscN.spec.xy_y` | lin | 0 | 1 | 1 |
| `oscN.multi.timbre` | lin | -1 | 1 | 2 |
| `modNN.rise` | lin | 0 | 5000 | 5000 |
| `modNN.fall` | lin | 0 | 5000 | 5000 |
| `modNN.delay` | lin | 0 | 4000 | 4000 |
| `modNN.delay_beats` | lin | 0 | 8 | 8 |
| `oscN.spec.flt_frame` | lin | 0 | 1 | 1 |
| `eq.mid2.cutoff` | note | 8 | 136 | 128 |
| `eq.mid2.gain` | lin | -24 | 24 | 48 |
| `eq.mid2.resonance` | sqrt | 0 | 1 | 1 |
