# 07 The sample, granular, spectral and multisample oscillator types in the voice

Status: final (2026-11-12). The four non-wavetable oscillator types (`oscN.type` = Sample, Granular, Spectral, Multisample) are implemented by Vital-free libraries (`core/sample-osc`, `core/granular`, `core/spectral`, `core/multisample`, namespaces
`synthyboy::sampleosc`, `granular`, `spectral`, `multisample`) that the own engine links. This document is the **glue**: how a voice of the engine drives one of those library voices, what it feeds them, how they share the oscillator's controls, and how their output
joins the rest of the voice. What each library does inside (windowed-sinc resampling, grain scheduling, spectral analysis/resynthesis, zone selection) is in its headers and tests.

## 1. One oscillator slot, one type at a time

* Each of the three oscillator slots (`osc1..3`) of a voice owns **both** a wavetable oscillator and a sample-type oscillator; the controls `type` (0 Wavetable, 1 Sample, 2 Granular, 3 Spectral, 4 Multisample) decides which one produces the slot's sound (the wavetable one is
  disabled, which also stops its CPU use). Outputs are the same two as for the wavetable oscillator (document 2 sec 2.1): **raw** (after unison mix, before level and pan; used as FM/RM source by other oscillators) and **levelled** (`raw * pan * level^2` in the same law as document 1 sec 8:
  the level control is audio-rate smoothed square root of the level, the pan is the constant-power pan).
* **Changing the type while the oscillator sounds** crossfades over **10 ms**: the new type starts at once from the voice's current pitch/phase state and the old one fades out (linear gain ramps `1 - r`, `r`); with a new note the type simply starts.
* A type needs its content: a sample (Sample/Granular/Spectral; the "Sample" of the oscillator, loaded from the content store, document 13-new-keys.md 5) or an instrument (Multisample). **Without content the slot is silent** (no fallback tone). A content change under a running voice
  stops that voice's players (no reading of freed memory).
* The mute of the oscillator mapping (key/velocity range, doc 1 sec 11) applies to the levelled output of every type (and to the raw output unless "warp" is on).
* The oscillator's `on`, `destination`, `filter_blend`, `env1`, bus sends: unchanged (document 1 sec 1.1).

## 2. Which oscillator controls the sample types use

| Control | Sample | Granular | Spectral | Multisample |
|---------|--------|----------|----------|-------------|
| `level`, `pan`, `destination`, bus sends | yes | yes | yes | yes |
| `transpose`, `tune` | yes | yes | yes | yes |
| `unison.voices` (also capped by the tier), `.detune`, `.range`, `.detune_curve`, `.spread`, `.blend` | yes | yes | yes (at most 4 voices) | yes |
| `phase`, `random_phase`, `sample.uni_start`, `sample.uni_span` | start position | start position | - | - |
| `sample.scan` | read speed | - | - | - |
| `pitch_track` / `pitch_bend_track` | yes | yes | yes | yes |
| `warp.*`, `warp2.*` | FM/RM/AM/quantize/shaper (4) | grain warps (4) | spectral warps (5) | - (not applied) |
| `frame`, `frame_spread`, `morph.*`, `pitch_mode`, `pitch_coarse`, `fixed_pitch`, `unison.mode`, `unison.stack`, `transpose_snap`, the Serum unison law | ignored | ignored | ignored | ignored |

(The wavetable-only controls keep their values; the UI dims them by type.)

## 3. The pitch of a library voice

```
semitones = oscN.transpose + oscN.tune/100 [+ the master and bend terms below]
if pitch tracking is on (and the sample is not "sliced"):   semitones += n_voice + fold - root_key        -- n_voice: doc 1 sec 6.1 (glide, bend, master tune and transpose included); root_key: the root note of the sample / of the zone
else if pitch_bend_track is on (and not sliced):            semitones += n_voice - n_raw                      -- the bend (and glide offset) still moves it
ratio(u)  = 2^( (semitones + shaped_u * range * detune/100 * ... ) / 12 )
```

* `unison` voice `u` = 0 .. U-1 gets `t_u = 2u/(U-1) - 1` in [-1, 1] (0 for U = 1), `shaped_u = sign(t_u) * P(|t_u|, detune_curve)` (power scale, README) and detunes by `shaped_u * (range * detune) / 100` semitones (`range * detune` = cents at the extreme, as in
  document 1 sec 9.2). Note the **different layout** from the wavetable oscillator: a linear line of voices from flat to sharp, no pairs; for odd U there is a centre voice at ratio 1 (t = 0).
* The note's pitch is read **per chunk** and applied by the library voice with its own smoothing; a glide/bend moves the playback rate continuously.
* The FM warps modulate the **read speed**: `rate = 1 + deviation`, `deviation = 4 a^2 m` (Linear, clamped to >= -1: the sample cannot run backwards), `2^(4 a^2 m) - 1` (Exp), where `a` is the warp amount and `m` the modulator (-1 .. 1). The spectral type has no per-sample read speed: it uses
  the block mean of the rate. RM: `y *= 1 + a (m - 1)` (a = 1: pure ring modulation). AM: `y *= (1 - a) + a (0.5 + 0.5 m)`. Quantize: `levels = 2^(1 + 14 (1-a)^3)`, `y = round(y * levels)/levels`. The distortion-type warps (Dist Tube ... Soft Sat.) apply the shaper of document 2 sec 6.3
  to the sample at drive `a * 30 dB`. The modulator sources are as for the wavetable oscillator (the other oscillators' raw outputs, the Noise, the Sub, the filters of the previous block).

## 4. Starting a note

At the voice's reset (a new voice) the engine calls the library voice's note-on with: the sample view, the loop/region settings of the oscillator (loop mode None/Forward/Ping-pong/Reverse/Tailed, start/end/loop points relative to the region, loop cross-fade in ms, reverse, edge fade ms, quality tier = the tier's
`quality` 0/1/2 -> 4/8/16-point windowed sinc), root key 60 for the sample oscillator itself (the real root in the pitch term), velocity 1 and level 1/pan 0 (the engine does level and pan itself).

* **Start position** of unison voice `u`: `fraction = phase + random_phase * r_0 + uni_start * r_u + uni_span * t'_u` (mod 1), with `phase` in cycles (the knob / 360), `r_0` one random number per note (shared), `r_u` one per unison voice, `t'_u = u/(U-1)`.
  `fraction` is the position inside the playback region.
* **Key release** (the voice's note off, doc 1 sec 4): voices that are in a loop leave it (`releaseLoop`) if the oscillator's "loop exit" is on or the loop mode is Tailed (plays on past the loop end); Multisample layers get their note off (release stage of the zone); zones of trigger type "release" start now.
  A voice with a one-shot or released sample ends at the end of the region; the voice's amplitude envelope decides when the engine voice dies.
* Random numbers: a per-oscillator deterministic generator (a render repeats).

## 5. Mixing the unison and positioning in the stereo field

```
centre_w = 1 - 0.5 blend;   detuned_w = max(0.25, 1 - (1 - blend)^2)          -- blend 0..1
w_u      = centre_w if (U odd and t_u = 0) or U = 1, else detuned_w
norm     = 1 / sqrt(sum_u w_u^2)                                                -- equal power
pos_u    = pan_law(t_u * spread)                                                -- constant power pan, spread = unison.spread 0..1: voice 0 (t = -1) at the far left, the last at the far right; document 1 sec 8 pan law
raw_L += sl_u * pos_u,L * norm * w_u * g ; raw_R += sr_u * pos_u,R * norm * w_u * g       -- sl, sr: the library voice's stereo output (a mono sample gives sl = sr)
g = 1 (or the normalisation gain of the sample when the sample's "normalize" is on)
```

This is **not** the Vital unison law of document 1 section 9 (no pair layout, no `blend_shape`, no Serum table). The stereo field of a stereo sample is kept (each side through its own pan position).

## 6. Per-type notes

* **Sample**: the library's `SampleVoice` with `setSemitoneOffset` (per chunk), `setSpeedMultiplier` (the scan: `sample.scan` * range {1, 2, 4, 8} for the "scan range" key, sign flipped by Reverse, times `bpm/120` with tempo lock, times
  region/target length ratio with length-to-BPM) and `processModulated` (the FM rate modulation). Mono samples play as dual mono.
* **Granular**: the library's `GranularVoice`; grain parameters from the `oscN.gran.*` keys per chunk (position, density, length, randomisations, window, warps applied per grain); the spawn pattern across unison voices staggers the grain clocks (none / u/U / `2^(u/U) - 1`); the
  grains of the voice are capped by `max_grains / U`. Output gain: the granular level (a function of density and window so that the level stays steady) and the unison weights above.
* **Spectral**: the library's `SpectralVoice` (analysis of the sample once, resynthesis per note), at most 4 unison voices; output gain `spec_level * 5.3703` (**+14.6 dB**, a calibration against Serum 2: the spectral oscillator is that much louder than the raw engine level at the same volume).
  The spectral warps (Spectral Comb ... Vocode from Noise) are parameters of the library voice; a few use external spectra (the analysis of Osc 2/3 or the Noise sample, refreshed per block).
* **Multisample**: an **instrument** (zones with key/velocity ranges, round-robin and switch rules, release triggers, choke groups, per-zone envelopes) is the content; the engine asks the instrument for the zones for `(key, velocity, random, counter, switch)` at the note on, and
  starts one layer per zone and unison voice. The pitch of each layer is `semitones + detune + (root_key - zone_center)` (tracking: every zone plays at the note relative to its own centre key); `multi.timbre` (-1 .. 1) shifts the key used for the zone lookup (`timbreKey`); the
  choke groups (`off_by`) release layers when the group triggers; the instrument envelope can be overridden (`multi.*` keys). Zones with `trigger = release` start on the note off.

## 7. Cost and "better"

* The library voices are block based, allocation free in `process`; the engine keeps up to 16 unison voices x 3 oscillators x the polyphony: the sample type costs one windowed-sinc voice per unison voice (the `low` tier uses the 4-point sinc).
* The spectral oscillator is the most expensive; the engine caps its unison at 4.
* A sample change (loading a file) must not happen on the audio thread: the content store hands the new view over between blocks (document 13-new-keys.md 5).

## 8. Test vectors

The libraries carry their own acceptance tests (`core/sample-osc/tests`, `core/granular/tests`, `core/spectral/tests`, `core/multisample/tests`); the glue rules are tested black box:

| # | Setup | Expectation |
|---|-------|-------------|
| S1 | `osc1.type 1` with a sine sample of root key 69 (440 Hz), note 69, unison 1 | 440 Hz; note 81: 880 Hz; `osc1.transpose 12`: 880 Hz; `pitch_track` Off: 440 Hz whatever the key |
| S2 | as S1, `unison.voices 2`, `detune 100`, `range 2`, `detune_curve 0`, spread 100, blend 100 | voice 0 at -200 ct hard left (pan t = -1), voice 1 at +200 ct hard right; each at `1/sqrt2` of the single voice (equal weights `detuned_w`, `norm = 1/sqrt2`) times the pan factor `sqrt2` of a hard-panned voice -> 1.0 in its channel (0 dB) |
| S3 | `osc1.type` changed from Wavetable to Sample while a note sounds | no click; the old sound fades out and the new fades in over 10 ms |
| S4 | `osc1.type 3` (Spectral) vs `osc1.type 1` with the same sample at the same level | the spectral one is 14.6 dB louder (+-1 dB) |
| S5 | `type 1` with no sample loaded | silence from that oscillator; other oscillators unaffected |
