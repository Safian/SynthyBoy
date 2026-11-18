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
* **Spectral voices prime their overlap buffer.** The analysis (FFT 2048, hop 512) of every sample is made once when the content is loaded, off the audio thread (`ContentSet::build`), and is shared by all voices. At the note start a Spectral voice synthesises four frames at the start position (the head does not move yet): three before the first output hop and the frame of the hop itself, so that the first output hop is the sum of four overlapping frames and the note starts at full level (`SpectralVoice::noteOn`, `primeFast`; the first block then only reads the hop). The four frames have the same shape (the same analysis frame, the same settings: filter, band, warps, pitch), so the shaped bins are made once per channel and only the phases (advanced by the partials' frequencies), the sine and cosine, the inverse transform and the overlap add are made for each; a note with the Smear warp (the only warp with a memory that every call changes) shapes every frame. This is the work of a strike on the audio thread, 4 frames per voice and channel (docs/MPC.md "Content oscillators on the MPCs"). The voices of a note and of a chord start at different offsets inside their hop (section 4a), so that their later frames do not fall into the same block.

## 4a. The start of a Spectral voice (round 10)

Facts of the code the maths below stands on (`core/spectral`): FFT 2048 (N), hop 512 (H), periodic Hann `w` for the analysis and again for the synthesis, output scale 1/1.5 (the sum of the squared windows). The analysis frame `t` is centred on file sample `t H`; its `frequency` of bin `b` is `nominal_b + wrapPi (phase_t - phase_(t-1) - nominal_b H) / H` (`Analysis::build`), i.e. the instantaneous frequency of the bin, in radians per sample. A voice frame is `x_j[n] = Re IFFT (A_b e^(i phi_(j,b)))`, added to the ring at `ola + n` with the weight `w[n] / 1.5`, `phi_(j+1,b) = phi_(j,b) + of_b H` (wrapped). Frame 0 of a note takes the analysis phase, the frames 1 .. 3 advance it; the first output hop (output samples 0 .. 511) is `sum_(m=0..3) w[(m) H + q] x_(3-m)[m H + q] / 1.5` for `q` in `0 .. H - 1`; frame 3 is the first frame the voice makes "for real" and the phases it leaves behind (`synthPhase`, the smear memory) are the ones the voice goes on from.

**The shifted copy does not stand in for the three earlier frames (design considered and rejected).** If every bin sat at its centre frequency (`of_b = 2 pi b / N`, so `of_b H = b pi / 2`), the frame `m` hops before frame 3 would be `x_(3-m)[n] = x_3[(n - m H) mod N]` (a circular shift, not `n + m H`: an earlier frame is the later one delayed), and the ring of the first four hops would be `x_3[q] * sum_(m: q + m H < N) w[q + m H] / 1.5` from one inverse transform. That identity was verified numerically (synthetic spectrum, `of_b` at the bin centres: largest difference 1.8e-6 for a peak of 0.31). It does not hold for the spectrum of any analysed sound: the bins of a partial's main lobe all carry the partial's frequency (`frequency` is the instantaneous frequency of the *bin*, which is the partial's), not their own centre, so the earlier frame differs from the shifted copy by the phase `m (omega_b - of_b) H` per bin, which is `m (pi / 2) (b - f / binwidth)`: up to +- pi / 2 per hop for the neighbours of a peak bin. In the time domain: the unwindowed frame carries the analysis window as its envelope; the phase advance of a real frame keeps that envelope in place (only the carrier moves), the shifted copy slides the envelope with the shift, so the first hop of the note would rise with the analysis window's edge (measured on an analysed 1 kHz sine: the first 512 samples 9.95 dB below the following hops, the largest difference of the two first hops equal to the peak; that is the fade in over 2048 samples the priming exists to avoid). An exact earlier frame needs the quadrature of every partial separately (`Re (e^(-i m theta_r) z_r)` per region), which is one inverse transform per frame; pruning the transform to the part of the frame that the hop needs (a quarter to three quarters) does not pay, because the real transform already runs at half length. So the four frames stay, what changed is what they share.

**What is shared now.** The frame of the first hop is made by `noteOn` with the other three: one shaping per channel for all four (before: two, the fourth was shaped again in the first block). The ring and the voice state after the note start are those of the three priming hops and the first `synthesizeHop` of the old code (the same frames in the same order, `--scalar-content` renders agree with the block kernels to the same rounding as before, `spectral_block_kernels_agree_with_the_reference_voice_to_rounding`). Only if the settings change between `noteOn` and the first block (the engine starts the note and renders the same control period, so they do not) would the fourth frame use the settings of `noteOn`.

**Start offsets.** `noteOn (..., hopOffset)` with `hopOffset` 0, 128, 256 or 384 (`kStartStep = kHop / 4`, one block of the engine; the engine does not announce the host's block size to the oscillator, a host block of 256 or 512 samples then sees two or four quarters in one block): the four frames are made as above, the output starts `hopOffset` samples into the first hop, the next frame is due after `kHop - hopOffset` samples. The note is *the same rendering shifted*: sample `t` of a voice with offset `d` is sample `t + d` of the voice without offset, bit for bit, for a frozen and for a scanning head, with Smear, Phase Lock, a phase twist, transients and a stereo sample (`spectral_a_start_offset_renders_the_same_note_shifted_by_the_offset`, 60 runs, difference 0). So level, pitch and the sound of the voice are the old ones; the first `d` samples of the old note are not played (up to 384 samples, 8.7 ms at 44.1 kHz: the first hop is the full sum for every `d`, its first 512 samples are within 0.1 dB of the level of the rest, `spectral_every_start_offset_begins_at_the_level_of_the_note_and_keeps_pitch`) and the play head is ahead by `d` samples of the scan, less than one hop (`spectral_a_start_offset_keeps_the_scan_speed_and_the_scan_position`). The offset of the voice `u` of `U` of the note with serial `s` on oscillator `o` is `((s U + u + 2 o) mod 4) * 128` (`LibOsc::beginType`): the note serial counts the notes of the engine (so a render repeats), and consecutive notes and the unison voices of a chord fall on all four quarters. What differs from before is therefore only the relative timing of the voices: the voices of a note and the notes of a chord no longer start at the same instant, so partials that the voices share (the octave of a chord, unison voices of equal pitch) add with other relative phases. Measured on the Mac (chord of four notes, `synthyboy_preset_render --chord-wav`, tier high; the three presets with a Spectral oscillator, all other 157 presets of the 160 are bit-identical in the single note and in the chord render): Spectral Chord Morph +0.54 dB level of the loudest 300 ms (whole render +0.31 dB, peak +1.02 dB, the largest third octave band +2.7 dB, mean 0.68 dB); Spectral Gong Pad +0.02 dB (peak +0.73 dB, bands 0.68 dB at most); Spectral Vowel Freeze -0.54 dB (peak -1.55 dB, bands 3.6 dB at most, mean 0.37 dB); the first 50 ms alone: +0.32 / +0.45 / -0.22 dB. With the offset by note only (the unison voices together) the three differ by +0.63 / -0.10 / +0.09 dB of level.

**What the strike still costs.** Frames per voice and channel at a strike: 4 (the factory samples of the three presets are mono). Per frame on the Key 61 (Cortex-A17, 1608 MHz, `--micro`): phase advance and wrap 33 us, sine and cosine 17.5 us, inverse transform 33.8 us, overlap add 4.7 us (sum 89 us); the shaping that precedes a strike is 67 us (peaks 18.7, placing, the lerp, the copies and fills); a voice start (`noteOn`) 422 us warm, before this change 322 us plus 156 us in the first block. XL (Cortex-A76): 16 us per frame, 14 us shaping, `noteOn` 79 us (before 61 + 30). Eight voices at a strike are about 3.4 ms warm on the Key 61 (one block is 2.9 ms), measured 4.2 to 4.6 blocks in the benchmark's strike block (cold caches, the rest of the voices), 1.4 to 1.7 on the XL. The steady state is flat now: every block carries about a quarter of the frames of the voices.

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
* **The block kernels (core/sample-osc, core/granular, core/spectral, core/multisample).** What does not change inside one call is made once per call and the per sample (per frame) loop does only the arithmetic that is the sound:
  * the sample voice (`SampleVoice::renderBlock`) takes the region and loop bounds, the pan gains, the mip level and its blend and the table of the tier out of the sample loop and, where nothing but the plain read happens (no release, no fade, no rate modulation, no loop event, no mip blend), reads four (two groups of four) consecutive samples at a time with the four lanes of a NEON register; the output is the same bits as the per sample reference (`renderFrame`) except in the loop crossfade, where the equal power weights are computed in float lanes (within 3e-6 of the library's double cos and sin);
  * the granular voice (`GranularVoice::renderGrains`) makes the constants of a grain (window peak and slopes, 1 / length, mip level) at its spawn, finds the spawns of a placed head from the timer, and does the window and the Hermite read of eight samples of a grain at a time; the window multiplies by a reciprocal where the reference divides (about 1e-7 of the grain's level, at most 3e-5 of a block's peak at the tail of a grain);
  * the spectral voice (`SpectralVoice::renderChannelFast`, core/spectral/include/synthyboy/spectral/kernels.h) transforms the real signal of a frame with one complex transform of half the length (NEON, radix 2 squared), computes sine and cosine with a polynomial (2e-7), updates and wraps the bin phases with the exact value of the old `fmod` wrap, and does the interpolation, the band and spectral filter gains (cached with their settings), the peaks and the placing of the peak regions with vector loops; the frames agree with the reference code to rounding (-120 dB), and the Spectral Gong Pad (Add Subharmonics) to -98.6 dB of the RMS of the preset's output, which is not the oscillator (see the next bullet);
  * **Why the Spectral Gong Pad differs by -98.6 dB while the other presets differ by -120 dB or less.** Measured by switching the pieces of the Spectral voice one by one to the reference code on that preset (shaping, phase update, sine / cosine, transform, priming): with sine / cosine and the transform both at the reference the render is identical to `--scalar-content`, and each of them alone leaves -99 dB; the voice has no other difference (the phase update, the peak and placing loops and the priming are bit-identical to the reference, and a unit-level comparison of the voice on the same sample over a range of pitch ratios, host rates, scan speeds, Phase Lock and the band gives 2e-7 of the peak). The oscillator alone differs by -127 dB there (the patch with Utility, EQ and reverb off: -127.4 dB, filter off too: -129.2 dB). The amplifier is the effect chain: the same patch with only `utility.mono_bass` off gives -124.5 dB, with every other Utility setting off or on it stays at -98.6 dB. Mono Bass is a 4th order Linkwitz-Riley crossover at 200 Hz (core/fx-serum-a/include/synthyboy/fx/utility.h) on a signal with almost nothing below 200 Hz (-122 dB of the spectrum there): an input that differs in the last bits makes its recursive low frequency sections round differently, and that round-off noise, which the crossover's low frequency gain lifts, is independent of the signal. It is inherent (a different float rounding of the oscillator, which no kernel can avoid), it is not a divergence of the kernels. It does not grow: a note of this preset held 60 s (`synthyboy_preset_render --hold 60`, kernels against `--scalar-content`) differs by -96.6 to -96.9 dB of the signal's RMS in every 6 s window from the first to the last (RMS difference 4.2e-7 .. 4.9e-7, largest sample difference 2.3e-6 .. 3.7e-6).
  * every vector form does, lane by lane, the operations of its scalar form in the same order (no fused multiply-add), so the two give the same bits (a test per kernel), and `synthyboy_tier_bench --scalar-content` switches the oscillators to their reference code (the code of before, per sample / per frame) for a before / after with one binary.
* A sample change (loading a file) must not happen on the audio thread: the content store hands the new view over between blocks (document 13-new-keys.md 5).

## 7a. Details added after implementer questions 4 and 6

### 7a.1 The spectral output gain: what 5.3703 is relative to (question 4)

The factor `5.3703` multiplies the output gain of the `core/spectral` voice, and the voice's raw output level **depends on the host rate it is prepared with** (the rate of the engine's internal processing, `sr_i`): for the same 0.8-amplitude 440 Hz sine sample the voice at gain 1 gives an RMS of **0.2601 at 48 000 Hz** and **0.0948 at 96 000 Hz**
(ratio 0.364, i.e. -8.8 dB; the level is not simply proportional to the rate). engine-vital runs the voices at `sr_i = 96 000` (2x oversampling at 48 kHz): `0.0948 * 5.3703 = 0.509`, which is the measured engine RMS 0.5106 (S4). **`5.3703` is therefore a calibration for `sr_i = 96 kHz`.** An engine that prepares the voice at 48 kHz needs
`5.3703 * 0.364 = 1.95` for the same level: exactly the implementer's factor. Consequences, measured in engine-vital:
* with the `low` tier (1x oversampling, `sr_i = 48 kHz`) the spectral oscillator is **2.74 times (8.8 dB) louder** than with 2x (sine sample: RMS 1.4005 against 0.5106) because the calibration is not rate-aware. This is a quirk; the **level target is the 2x value**, whatever the own engine's internal rate is.
* the level of the spectral type relative to the Sample type depends on the **content**: RMS ratio spectral/sample for a sine 1.27, for a sawtooth (220 Hz, amplitude 0.6) 2.33, for white noise 4.01 (granular/sample: 1.49, 1.47, 1.43 for the same contents). The resynthesis has no fixed gain relative to the input; implement the library as it is and calibrate only the constant. RMS values at 2x oversampling, note 69, root 69, Forward loop, level 100:
  sine 0.8: sample 0.401, granular 0.596, spectral 0.511; sine 0.4: 0.2005 / 0.2981 / 0.2553; saw 220 Hz 0.6: 0.2453 / 0.3613 / 0.5718; white noise (uniform +-0.8): 0.3031 / 0.4328 / 1.2142.

### 7a.2 The X|Y control: `y_axis` and `xy_y` (granular and spectral)

The oscillator has a two-dimensional pad: **X is the play head** (`gran.position` / `spec.position`, 0 .. 100 %, used while `manual` is On) and **Y is `xy_y`** (0 .. 100 % = 0 .. 1, modulatable, clamped to 0 .. 1). `y_axis` picks which parameter the Y value takes over; while an axis is assigned the normal knob of that parameter is **ignored** and the value below is used instead (axis 0 = None: the knobs act).

| Granular `y_axis` | Value taken from Y (`y` = 0 .. 1) |
|---|---|
| 1 Level | output gain of the type = `y` (0 mutes) |
| 2 Warp / 3 Warp 2 | warp 1 / warp 2 amount = `y` |
| 4 Density | `0.1 * 10^(4 y)` grains per second (0.1 .. 1000 Hz) (free-rate mode; the tempo and grain-count modes keep their own rules) |
| 5 Grain Length | `4000^y` ms (1 ms .. 4 s) |
| 6 Window Amt / 7 Window Skew | window amount = `y`; skew = `2 y - 1` |
| 8 Rand Offset, 9 Rand Dir, 11 Rand Length, 12 Rand Pan, 13 Rand Gain, 14 Rand Window, 15 Rand Skew, 16 Rand Warp, 17 Rand Warp 2 | that randomisation depth = `y` |
| 10 Rand Pitch | `24 y` semitones |

| Spectral `y_axis` | Value taken from Y |
|---|---|
| 1 Level | output gain = `y` (times the trim of 7a.1) |
| 2 Warp / 3 Warp 2 | warp amount = `y` |
| 4 Spec Flt Cutoff | the spectral filter's shift (`cut`) = `2 y - 1` |
| 5 Spec Flt Wet/Dry | the spectral filter's mix = `y` |
| 6 Freq Lo / 7 Freq Hi | the low / high band edge = `20 * 1000^y` Hz (20 Hz .. 20 kHz) |

### 7a.3 Mask and Vocode warps from the Noise and the other oscillators (spectral type)

The warps `Mask from Osc 2 / Osc 3 / Noise` and `Vocode from Osc 2 / Osc 3 / Noise` (list order: first other oscillator, second other oscillator, noise) use the **magnitude spectrum of the external source's current signal**:
* every block (only when a warp needs it) the engine appends the block of the source's **raw output (the left lane of the voice)** to a ring of the last 2048 samples **at the engine's internal rate**, applies a Hann window `0.5 - 0.5 cos(2 pi n/2048)`, takes a 2048-point FFT and keeps the 1025 magnitudes. No scaling, no rate conversion: **bin `k` of the external spectrum is paired with bin `k` of the sample's analysis** (the sample's bins are `file_rate/2048` apart, the external ones `sr_i/2048`: they only coincide when the rates are equal; engine-vital pairs bin indices regardless). The other oscillators must have been rendered first (as for FM).
* **Mask**: `mx = max(external)`; every magnitude `m[k] *= (1 - a) + a * min(1, 6 external[k] / mx)` (`a` = warp amount): a soft gate that passes the bins where the source is within -15.6 dB of its peak, and fully attenuates a silent source at `a = 1`.
* **Vocode**: `level = sum(m)/sum(external)` (loudness match); per bin the envelopes over `k-4 .. k+4` are compared: `ratio = min(10, sum(external * level)/sum(m))`, `m[k] *= (1 - a) + a * ratio`: the source's spectral envelope is imposed on the sample's partials (gain limited to +20 dB).
Both are scale-free in the external level (normalised by its maximum or its sum). These are exactly the formulas of `core/spectral`; what the engine adds is the external magnitudes above.

### 7a.4 Choke groups (question 6)

Layers (zones of a multisample instrument) carry a group number (`group`) and a choke target (`off_by`). **Scope: all voices of the same oscillator slot** (every voice and every unison voice of that oscillator; not across `osc1` / `osc2` / `osc3`): the oscillator has one counter of started notes and one table `latest[group]`. At a note start with zones: for every zone with `0 < group < max`, `latest[group] = counter + 1`;
then `counter += 1`, and that value is the note's sequence number, stored by all its layers (all unison voices). In every block, for each active layer with `off_by > 0`: if `latest[off_by] > layer.sequence` (a **later** note started a zone of the group the layer is choked by) the layer receives its note off, **fast** (10 ms) if its `off_mode` is fast, else the normal release.
A note therefore never chokes its own layers (equal sequence) but chokes the layers of earlier notes of any voice. The test happens when the layer's voice is next rendered (at most one chunk later). Release-trigger zones (started at a note off) get a new sequence but do not update `latest`.

## 8. Test vectors

The libraries carry their own acceptance tests; the glue is measured black box with a generated content pack (a mono 16-bit WAV, 440 Hz sine of amplitude 0.8, 44.1 kHz, 2 s, loaded as the sample of osc 1 through the content store; `osc1.sample.root 69`, `sample.loop_mode` Forward loop, `osc1.level 100`, filter off, `master.volume 0`, `env1.attack 0`, `random_phase 0`):

| # | Setup | Measured |
|---|-------|----------|
| S1 | `osc1.type 1` (Sample), unison 1, note 69 / 81 | 440.0 Hz / 880.0 Hz; `osc1.transpose 12` at note 69: 880.0 Hz; `sample.keytrack` Off at note 81: 440.0 Hz. The level of the 440 Hz line is **-4.93 dB** (waveform peak 0.567 for a sample peak of 0.8: the sample voice's centre pan law costs a mono sample 3.0 dB; a stereo sample is not attenuated by it) |
| S2 | as S1, `unison.voices 2`, `detune 100`, `range 2`, `detune_curve 0`, `spread 100`, `blend 100` | left channel only 392.03 Hz (-200 ct), right channel only 493.91 Hz (+200 ct), each at -4.94 dB: the same level as the single voice (weights `detuned_w` equal, `norm = 1/sqrt2`, hard-pan gain `sqrt2`) |
| S3 | `osc1.type` changed while a note sounds | 10 ms crossfade (derived from the engine rules; the offline probe cannot change a parameter in the middle of a note, so this one is **not measured**) |
| S4 | `osc1.type` 1 / 2 (Granular) / 3 (Spectral) with the same sample, unison 1 | level of the 440 Hz line: **-4.93 / -1.49 / -2.90 dB**, waveform peaks 0.567 / 0.851 / 0.871. The spectral voice additionally shows weak sidebands at 252.5 / 353.9 / 526.1 / 627.5 Hz (-24 .. -29 dB: the resynthesis). The internal gain factor 5.3703 (+14.6 dB) of the spectral type is a calibration inside the glue; the net level relative to the Sample type is +2.0 dB for this sample |
| S5 | `osc1.type 1` without a loaded sample | silence (exact zero) |
