# 02 Oscillator

Status: final (2026-11-12). Covers the wavetable oscillator (`oscN.*`, N = 1..3), its warp slots, spectral morph, phase and pitch laws,
unison inside the oscillator (the laws are in document 1 section 9, only the oscillator-side parts are here), the sub oscillator and
the noise source. The sample / granular / spectral / multisample oscillator types are document 7. Level, pan and the stereo blend are
document 1 sections 8 and 9.5.

Conventions: a **cycle** is phase 0..1; the "table" is a periodic function `T(x)` of the phase `x` (period 1); `fract(x)` is the
fractional part; "signed phase" `s` means the phase taken into [-0.5, 0.5) (0.5 and -0.5 are the same point of the cycle).

## 1. Wavetables

* A wavetable is `F` frames of 2048 samples, one period each. `F` is 1 for the single-cycle shapes (Sine, Saw, Square, Triangle, Pulse 25 %,
  Saw Down), 64 for the morphing built-in tables, 1..256 for user tables (`oscN.wavetable` = Preset table). The frame **position**
  `oscN.frame` (0..100 %) selects `frame = pct * (F - 1) / 100`, a real number. A table with one frame ignores it.
* Every frame is stored as its **spectrum** (amplitude and phase of harmonics 0..1024, the DFT of the 2048 samples) as well as the time
  samples; the oscillator builds its band-limited waveform from the spectrum (section 3). Frames are peak-normalised to |x| <= 1 by
  the builder (the time signal; DC removed), so a band-limited sawtooth is slightly below 1 in amplitude: the fundamental of the Saw table
  measures 0.541 (-5.3 dB) at level 100 %.
* **Built-in tables** (all generated from formulas, nothing is read from a file; `b_k` = amplitude of `sin(2 pi k x)` in the series
  `T(x) = sum b_k sin(2 pi k x) + a_k cos(2 pi k x)`, then DC removed and the peak normalised to 1; harmonics 1..1023):

| Entry | Name | Frames | Series |
|-------|------|--------|--------|
| 0 | Sine | 1 | `b_1 = 1` (so `T(x) = sin(2 pi x)`) |
| 1 | Saw | 1 | `b_k = -2/(pi k)` (rising ramp: -1 at x=0+, 0 at x=0.5, 1 at x=1-) |
| 2 | Square | 1 | `b_k = 4/(pi k)` for odd k |
| 3 | Triangle | 1 | `b_k = 8/(pi^2 k^2) * (-1)^((k-1)/2)` for odd k |
| 4 | Pulse 25 % | 1 | pulse of duty 0.25: `a_k = 2 sin(2 pi k d)/(pi k)`, `b_k = 2 (1 - cos(2 pi k d))/(pi k)`, d = 0.25 |
| 5 | Saw to Square | 64 | `b_k = 2/(pi k) * (1 + t)` for odd k, `* (1 - t)` for even k, t = frame/63 |
| 6 | Sine to Saw | 64 | `b_1 = (1 - t) + t * 2/pi`, `b_k = t * 2/(pi k)` |
| 7 | Harmonic Series | 64 | `b_k = clamp(count - k + 1, 0, 1)/k` for k <= 64, `count = 1 + 63 t` |
| 8 | Odd Harmonics | 64 | as 7, odd k only |
| 9 | Formant Sweep | 64 | `b_k = (g1 + g2)/sqrt(k) + 0.15 [k=1]`, `g1 = exp(-((k-c1)/(1+0.4 c1))^2)`, `g2 = 0.6 exp(-((k-c2)/(1+0.4 c2))^2)`, `c1 = 2 + 10 t`, `c2 = 6 + 26 t` |
| 10 | FM Sweep | 64 | time domain `sin(2 pi x + 6 t sin(4 pi x))` |
| 11 | PWM Sweep | 64 | pulse of duty `0.05 + 0.45 t` |
| 12 | Bell Partials | 64 | partials k in {1,2,3,4,6,8,11,15,19,26,34}: `w = exp(-k/(4+40t)) + 0.3 exp(-((k-3-20t)/4)^2)`, `b_k = w * (0.8 if k mod 3 = 0 else 1)`, `a_k = 0.4 w (+1 if k even else -1)` |
| 13 | Preset table | 1..256 | user data |
| 14 | Saw Down | 1 | `b_k = 2 (-1)^k/(pi k)` (falling ramp) |

  (The sign convention above is verified: with `oscN.phase = 0` the Sine plays `-sin(2 pi t)`, section 4.)

### 1.1 Exact frame definitions (answer to implementer question 1)

The recipes of the table in section 1 define every frame **exactly**; they were verified numerically against the engine's frames (all 581 frames of the built-in set, maximum deviation below 1.1e-6). The construction of one frame:

1. Frame `f` of a table with 64 frames has the parameter `t = f/63` (0 .. 1); single-frame tables use `t = 0`. (So frame 0 of "Saw to Square" is the pure Saw, frame 63 the pure square; the "first frames agree" with the single-cycle tables.)
2. Form the unnormalised periodic signal over 2048 samples, `n = 0 .. 2047`, phase `n/2048`: `x[n] = sum_k a_k cos(2 pi k n/2048) + b_k sin(2 pi k n/2048)` for the harmonics `k = 1 .. 1023` listed in the recipe (the 1023-harmonic band limit is part of the definition: the Saw and Square carry their Gibbs ripple). Harmonic 1024 and DC are zero.
   Entry 10 (FM Sweep) is defined in the time domain instead: `x[n] = sin(2 pi n/2048 + 6 t sin(4 pi n/2048))`.
3. Remove the mean: `x[n] -= mean(x)`.
4. Normalise the peak: `x[n] /= max |x[n]|` (so the largest absolute sample of every frame is exactly 1; frames are never normalised against each other, e.g. the sweep tables change loudness with `t` only through their spectra).
5. The spectrum of the oscillator (section 1) is the DFT of this final frame.

Entries and `t` per frame: 5 and 6: `t = f/63`; 7 and 8: harmonic count `1 + 63 t` (the last partial fades in linearly: `clamp(count - k + 1, 0, 1)`), amplitude `1/k`, `k <= 64`; 9: formant centres `c1 = 2 + 10 t`, `c2 = 6 + 26 t` (harmonic numbers), Gaussian widths `1 + 0.4 c`; 11: duty `0.05 + 0.45 t`; 12: the partial list with weights moving with `t` (formulas in the table). The sign convention: `b_k > 0` is `+ sin`, i.e. the Sine entry is `+sin(2 pi n/2048)` starting at 0 and rising; the engine's read adds the half-cycle offset of section 4.

Checkpoint samples (final, normalised frames; `x[i]` at the sample index `i`; tolerance 2e-4) for a regression test:

| Entry | Frame | x[0] x[128] x[256] x[384] x[512] x[640] x[768] x[896] x[1024] x[1280] x[1536] x[1792] |
|---|---|---|
| 0 Sine | 0 | 0.0000 0.3827 0.7071 0.9239 1.0000 0.9239 0.7071 0.3827 0.0000 -0.7071 -1.0000 -0.7071 |
| 1 Saw | 0 | 0.0000 -0.7415 -0.6360 -0.5302 -0.4242 -0.3182 -0.2121 -0.1061 0.0000 0.2121 0.4242 0.6360 |
| 2 Square | 0 | 0.0000 0.8468 0.8474 0.8476 0.8477 0.8476 0.8474 0.8468 0.0000 -0.8474 -0.8477 -0.8474 |
| 3 Triangle | 0 | 0.0000 0.2501 0.5002 0.7503 1.0000 0.7503 0.5002 0.2501 0.0000 -0.5002 -1.0000 -0.5002 |
| 4 Pulse 25 % | 0 | 0.2976 0.8920 0.8923 0.8920 0.2976 -0.2969 -0.2974 -0.2975 -0.2976 -0.2976 -0.2976 -0.2974 |
| 14 Saw Down | 0 | -0.0000 -0.1061 -0.2121 -0.3182 -0.4242 -0.5302 -0.6360 -0.7415 -0.0000 0.6360 0.4242 0.2121 |
| 5 Saw to Square | 0 | -0.0000 0.7415 0.6360 0.5302 0.4242 0.3182 0.2121 0.1061 -0.0000 -0.2121 -0.4242 -0.6360 |
| 5 Saw to Square | 21 | -0.0000 0.7766 0.7065 0.6360 0.5654 0.4947 0.4240 0.3531 -0.0000 -0.4240 -0.5654 -0.7065 |
| 5 Saw to Square | 42 | -0.0000 0.8117 0.7770 0.7419 0.7066 0.6712 0.6358 0.6000 -0.0000 -0.6358 -0.7066 -0.7770 |
| 5 Saw to Square | 63 | 0.0000 0.8468 0.8474 0.8476 0.8477 0.8476 0.8474 0.8468 0.0000 -0.8474 -0.8477 -0.8474 |
| 6 Sine to Saw | 0 | 0.0000 0.3827 0.7071 0.9239 1.0000 0.9239 0.7071 0.3827 0.0000 -0.7071 -1.0000 -0.7071 |
| 6 Sine to Saw | 21 | -0.0000 0.6488 0.8566 0.9788 0.9897 0.8800 0.6588 0.3525 -0.0000 -0.6588 -0.9897 -0.8566 |
| 6 Sine to Saw | 42 | -0.0000 0.9027 0.9349 0.9211 0.8475 0.7094 0.5116 0.2681 -0.0000 -0.5116 -0.8475 -0.9349 |
| 6 Sine to Saw | 63 | -0.0000 0.7415 0.6360 0.5302 0.4242 0.3182 0.2121 0.1061 -0.0000 -0.2121 -0.4242 -0.6360 |
| 7 Harmonic Series | 0 | 0.0000 0.3827 0.7071 0.9239 1.0000 0.9239 0.7071 0.3827 0.0000 -0.7071 -1.0000 -0.7071 |
| 7 Harmonic Series | 21 | 0.0000 0.8191 0.6499 0.5455 0.4532 0.3156 0.2327 0.1031 0.0000 -0.2327 -0.4532 -0.6499 |
| 7 Harmonic Series | 42 | 0.0000 0.7650 0.6639 0.5342 0.4263 0.3307 0.2188 0.1018 0.0000 -0.2188 -0.4263 -0.6639 |
| 7 Harmonic Series | 63 | 0.0000 0.7307 0.6344 0.5309 0.4255 0.3195 0.2131 0.1066 0.0000 -0.2131 -0.4255 -0.6344 |
| 8 Odd Harmonics | 0 | 0.0000 0.3827 0.7071 0.9239 1.0000 0.9239 0.7071 0.3827 0.0000 -0.7071 -1.0000 -0.7071 |
| 8 Odd Harmonics | 21 | 0.0000 0.8874 0.8493 0.8287 0.8722 0.8287 0.8493 0.8874 0.0000 -0.8493 -0.8722 -0.8493 |
| 8 Odd Harmonics | 42 | 0.0000 0.8499 0.8655 0.8480 0.8359 0.8480 0.8655 0.8499 0.0000 -0.8655 -0.8359 -0.8655 |
| 8 Odd Harmonics | 63 | 0.0000 0.8262 0.8362 0.8390 0.8397 0.8390 0.8362 0.8262 0.0000 -0.8362 -0.8397 -0.8362 |
| 9 Formant Sweep | 0 | 0.0000 0.9493 0.6069 0.4377 0.2153 0.1280 0.0892 0.0478 0.0000 -0.0892 -0.2153 -0.6069 |
| 9 Formant Sweep | 21 | -0.0000 0.4967 -0.0397 0.0568 0.0661 0.0566 0.0420 0.0224 -0.0000 -0.0420 -0.0661 0.0397 |
| 9 Formant Sweep | 42 | 0.0000 0.0306 0.0433 0.0460 0.0474 0.0425 0.0320 0.0172 0.0000 -0.0320 -0.0474 -0.0433 |
| 9 Formant Sweep | 63 | -0.0000 -0.1591 0.0331 0.0382 0.0398 0.0361 0.0274 0.0147 -0.0000 -0.0274 -0.0398 -0.0331 |
| 10 FM Sweep | 0 | 0.0000 0.3827 0.7071 0.9239 1.0000 0.9239 0.7071 0.3827 0.0000 -0.7071 -1.0000 -0.7071 |
| 10 FM Sweep | 21 | 0.0000 0.9723 0.3487 0.5221 1.0000 0.5221 0.3487 0.9723 0.0000 -0.3487 -1.0000 -0.3487 |
| 10 FM Sweep | 42 | 0.0000 -0.0794 -0.9973 -0.7611 1.0000 -0.7611 -0.9973 -0.0794 0.0000 0.9973 -1.0000 0.9973 |
| 10 FM Sweep | 63 | 0.0000 -0.9970 0.4814 -0.7594 1.0000 -0.7594 0.4814 -0.9970 0.0000 -0.4814 -1.0000 -0.4814 |
| 11 PWM Sweep | 0 | 0.4323 -0.0478 -0.0484 -0.0483 -0.0483 -0.0483 -0.0483 -0.0482 -0.0482 -0.0482 -0.0481 -0.0479 |
| 11 PWM Sweep | 21 | 0.3374 0.8986 0.8990 0.8981 -0.2245 -0.2247 -0.2247 -0.2247 -0.2247 -0.2246 -0.2245 -0.2243 |
| 11 PWM Sweep | 42 | 0.2028 0.8779 0.8785 0.8788 0.8791 0.8801 -0.4757 -0.4741 -0.4737 -0.4735 -0.4733 -0.4729 |
| 11 PWM Sweep | 63 | -0.0000 0.8468 0.8474 0.8476 0.8477 0.8476 0.8474 0.8468 -0.0000 -0.8474 -0.8477 -0.8474 |
| 12 Bell Partials | 0 | 0.0343 0.6858 0.5367 0.2763 0.0430 0.0633 0.1718 -0.0185 0.5615 -0.2613 -0.1649 -0.6996 |
| 12 Bell Partials | 21 | 0.0402 0.3403 0.6141 0.0594 -0.3500 -0.0131 0.2377 -0.3072 0.7200 -0.3092 0.2738 -0.5234 |
| 12 Bell Partials | 42 | -0.0064 0.3813 0.7055 -0.0285 -0.5582 -0.1382 0.1253 -0.1629 0.8199 -0.2195 0.3891 -0.6516 |
| 12 Bell Partials | 63 | 0.0552 0.3649 0.8197 -0.0090 -0.5539 -0.0781 0.0478 -0.1594 0.8282 -0.1646 0.3187 -0.7323 |

## 2. Reading a table: the ideal oscillator

For one unison voice and channel, per output sample `i` at the internal (oversampled) rate `sr_i`:

```
x_i       = x_{i-1} + f_i / sr_i                         -- phase accumulator, f_i = frequency incl. detune, see 5 and doc 1 sec 9
read_i    = W1( W2( x_i + o ) )                          -- o: start phase offset (4); W1, W2: warp slots (6), identity when None
y_i       = window_1 * window_2 * T_band(read_i)         -- T_band: band-limited frame (3), read by cubic interpolation (3.3)
```

Warp slot 1 acts on the accumulated phase first; slot 2 works on the phase slot 1 produced; both amplitude windows multiply; the
value stages (6.6) run on the sampled value in the same order.

### 2.1 Order of work inside a block

1. unison amplitudes and detune ratios are updated (doc 1 sec 9), warp amount / spread / morph values are computed per pair of voices (6.1);
2. the phase increments are computed per sample (5), the start phase offsets (4);
3. every **7 ms** (`0.007 * sr_i` samples, ~308 at 44.1 kHz, ~672 at 96 kHz) a new set of band-limited waveform buffers is built, one per
   unison voice and channel, from the frame position, the pitch and the morph values *of that moment* (3.1). The signal between two
   rebuilds is the **linear cross-fade** from the previous buffer to the new one over exactly those 7 ms (per-sample weight
   `(n+1)/N`). A note start copies the new buffers into the old ones (no fade-in).
4. samples are produced chunk by chunk between rebuilds, the unison stack is summed (with the centre amplitude `a_c` for pair 0
   and `a_d` for the others, doc 1 sec 9.4, linearly interpolated within the block when the amplitudes change), then the stereo
   spread blend (doc 1 sec 9.5), then level and pan (doc 1 sec 8). The **raw** output (before level/pan, after the spread blend) is what other oscillators use
   as FM/RM/PD/AM modulator and what the "Osc out" modulation source sees at the end of the block.

## 3. Band-limiting, frame choice, interpolation

### 3.1 Band limit

For a buffer rebuild with phase increment `d = f/sr_i` (cycles per sample, including unison detune, the stack multiplier and, for
Sync/Formant, the sync ratio) the buffer contains **only the harmonics `k <= floor(0.5 / (d * c))`**, `c` = 1 for `sr_i < 88.2 kHz`
and 2 for `88.2 <= sr_i < 176.4 kHz`, 4 above (so the cutoff is always about 22 .. 24 kHz regardless of the internal rate), and at most 1024. The cut is
a **brickwall** in the spectrum (the inverse DFT of the truncated spectrum, no taper); with `d > 0.5/c` the buffer is silent. The result
has no aliasing from the table itself up to the cutoff; the Gibbs ringing of a brickwalled saw is accepted (it is part of the sound).
Harmonics beyond 1024 do not exist (the table resolution).

The band limit follows the pitch *at the rebuild time only*: pitch glides move it in 7 ms steps (a quirk; fine).

### 3.2 Which frame

Without smooth interpolation (`oscN.smooth_interp` Off, tier `Interpolation::linear`): the spectrum of frame `floor(position)` (clamped to the
table) is used, **no interpolation between frames at all**; the 7 ms cross-fade between rebuilds makes a moving frame position a
piecewise-linear morph between frames sampled every 7 ms (a position that is steady between two integer frames sounds as the lower frame). With smooth interpolation On (and
for the `spectral` tier) the waveform is `(1-u) B(frame_lo) + u B(frame_lo+1)`, `u` = fractional part, a linear mix of two band-limited
frames (identical to mixing their spectra). **Better:** always interpolate; it costs one more inverse DFT (or one mip read) per rebuild
and removes the stair noise of fast sweeps.

The position is clamped to `[0, F-1]`. Unison frame spread (`oscN.frame_spread`, in frames over the whole stack): the pair `v` of `P`
pairs (P >= 2; for one pair P := 2) uses `position + spread * v/(P-1)` (clamped), but only if the "spectral unison" switch is on (default
on) and morph or spread is active; otherwise all pairs share one waveform buffer (a big saving: one rebuild for the stack).

### 3.3 Interpolation

The 2048-sample buffer is read with **cubic Catmull-Rom** interpolation (four points, tension 0.5): with `t` the fraction between
samples `p1, p2` and neighbours `p0, p3`:
`y = p1 + 0.5 t (p2 - p0) + t^2 (p0 - 2.5 p1 + 2 p2 - 0.5 p3) + 0.5 t^3 (p3 - p0 + 3 (p1 - p2))`. The index is the top 11 bits of the 32-bit phase, the fraction the
lower 21 bits. The buffers carry guard samples so the wrap needs no modulo. (Linear interpolation, mentioned for some tiers, is
audibly dull: the Catmull-Rom path is the reference.)

The phase accumulator is a 32-bit **integer** (a cycle = 2^32), which makes the phase exactly periodic and makes warp arithmetic wrap
naturally; a floating point implementation must keep the phase in double precision or wrap carefully.

## 4. Start phase, random phase, phase legato

* `oscN.phase` (degrees, 0..360): the table is read at `x + knob/360 + 0.5` (internally the offset is `knob/360 - 0.5` cycles, the accumulator
  starts at 0 and the signed phase of section 6 is `s = x + knob/360 - 0.5`). With the knob at 0 deg and no randomness a note starts at table position
  0.5. Measured (Sine entry, filter off): the fundamental
  starts at phase 0 deg of `-sin(2 pi t)` (zero crossing, going negative); the Saw starts at its zero crossing rising; raising the knob
  by 90 deg shifts the waveform 90 deg earlier (knob 90 starts the Sine at its minimum).
* Smoothing of the knob: audio-rate one-pole at 5 Hz, skipped at the note start (jumps to the target).
* `oscN.random_phase` (0..100 %): at a note start (reset) the accumulator of **every unison voice and channel** is set to
  `r * amount * 0.5` cycles, `r` uniform in [-1, 1) (a pseudo-random sequence; any good PRNG will do, determinism is per instance and note),
  independently for the left and right voice. For odd unison counts the centre pair's left phase is set equal to its right phase
  (the centre voice is mono). At amount 100 % the phase is uniform over the whole cycle; at 0 % all voices start together (they are then
  in phase, which is audible as a "comb-free" attack).
* `oscN.phase_legato`, `voice.phase_legato`, `oscN.phase_memory`: on a legato note start (the note count > 1 at the voice) the phase of all
  unison voices (`All voices`), only the centre voice (`Contiguous`) or nothing (`Per voice`: a stolen voice just continues) is taken from
  the voice that was rendered last (the phases of its pair at the end of its previous block) instead of being set to the random
  start. The voice that wrote last is voice 1 of the pair if both are active, else the active one.
* The phase of sources that need a start phase and restart (sub, noise) is in sections 9 and 10.

## 5. Pitch

Inputs (document 1 sec 6.1): the voice note `n_v` (glide, bend, master tune and transpose included), then

```
n_osc = n_v                                        (oscN.pitch_track On)   or   fixed_pitch + (n_v - n_raw)   (Off)   -- n_raw: note without bend
      + fold                                       -- oscillator mapping fold (doc 1 sec 11)
n_tot = n_osc + oscN.transpose' + oscN.tune/100 + coarse
f     = 8.1757989156 * 2^(n_tot / 12)
```

* The wavetable oscillators **always follow the pitch bend** (also with `pitch_bend_track` Off: that switch only exists for the sample-type
  oscillators; the `fixed_pitch` mode adds the bend as a separate offset).
* `transpose'` is the semitone transpose after the optional snap: `oscN.transpose_snap` is a bit mask of the 12 pitch classes (bit k = allowed
  offset k semitones above the octave floor) with bit 12 = global. Local mode: the transpose offset alone is snapped to the nearest allowed
  step within its octave (octave part kept); global mode: `note + transpose` is snapped. With an all-zero mask no snapping. The snapping
  cell is evaluated per sample (it follows glides and bends in whole-semitone steps for the transposed part; no smoothing).
* Pitch mode (`oscN.pitch_mode`) decides `coarse`: Semitones: `oscN.pitch_coarse` in semitones; Harmonics: `12 log2(max(1, 1 + floor(coarse)))`;
  Ratio: `12 log2(1 + coarse)` for coarse >= 0, `12 log2(1/(1 - coarse))` for coarse < 0 (so -1 is 1:2); Step: `coarse` steps of the tuning table
  (`note_table[k + step] - note_table[k]`, falls back to semitones without a table). In Ratio mode with a Ratio source (`oscN.ratio_src` =
  another oscillator or the sub) the pitch of that source (its `n_tot` of the previous block) replaces `n_v` entirely.
* `n_tot` is interpolated linearly across the block, per sample. There is **no frequency clamp**: above Nyquist/c the band-limited buffer
  is silent (3.1); the phase increment is computed in integer arithmetic and a frequency above the sample rate wraps.
* The unison pair's frequency is `f * ratio_pair` (doc 1 sec 9.2/9.3).
* **Tuning law check:** Sine, filter off: note 69 -> 440.00 Hz, `master.tune +100 ct` -> +1 st, `osc1.transpose +12` -> 880 Hz.

## 6. Warps

Two slots: `oscN.warp.*` (type, amount, phase, spread) and `oscN.warp2.*`. Amount `a` in [0,1] is the knob (percent / 100) plus the unison
spread: for the pair `v` of `P` pairs (P >= 2) `a_v = clamp(a + spread * 2 v/(P-1), 0, 1)` where `spread` is the knob (-100..100 %) / 200
(so +100 % moves the outermost pair by +1.0). The warp **phase** knob `p` (0..360 deg -> 0..1) gives the offset `delta = p - 0.5`
cycles for the modes that use it (marked "p" below); default 180 deg -> delta = 0. All values move linearly across the block.
The modes (list index in `oscN.warp.type`, the other oscillator named by the choice list: for osc 1 "Osc 2" is the *first* modulator and "Osc 3" the *second*, for osc 2 they are osc 1 and
osc 3, for osc 3 osc 1 and osc 2):

### 6.1 The Vital warps

`s` = the signed phase that reaches the warp (accumulator + start offset), `u = s + 0.5`.

| # | Mode | Phase map (what the table is read at) and window | p? |
|---|------|--------------------------------------------------|----|
| 1 | Sync | `read = 0.5 + R (s) mod 1`, `R = 2^(8a - 4)` (1/16 .. 16, a = 0.5 -> R = 1). The slave **restarts at the master's wrap**; the buffer is built for the frequency `f R`. Window 1. | p |
| 2 | Formant | the Sync map, plus the window `w = cos(pi s)` (a half sine over the master cycle, zero at the master's wrap); buffer for `f R` | p |
| 3 | Quantize | `q = 2^(27.2 (1 - a)^3 + 1)` steps per cycle; `read = (trunc(q s + p') - p')/q` with `p' = delta`, trunc toward zero (the dead zone around s = 0 is twice as wide) | p |
| 4 | Bend | cubic Hermite from (0,0) to (1,1) on `u`: start slope `a + 2 a^2`, end slope `3 - 5 a + 2 a^2`; a = 0.5 is the identity; read `= hermite(u - delta) - 0.5 + delta` | p |
| 5 | Squeeze | `k = 0.05 + 1.9 a`; with `x = |s - delta|`: `g = x/k` for `x <= k/4`, else `0.5 - (0.5 - x)/(2 - k)`; `read = sign(s - delta) g + delta` (k = 1, a = 0.5, is the identity) | p |
| 6 | Pulse | `v = 1/max(1 - a, 2.3e-10)`; `read = clamp(s v, -0.5, 0.5)` (as signed phase); window 0 where the negative clamp is reached, else 1 | no |
| 7, 8 | FM from the first/second modulator oscillator | `read = x + 6 a^2 m` (cycles; m = the raw output of the modulator, +-1; **phase** modulation; stereo: left channel by the modulator's left, right by right) | no |
| 9, 10 | RM from the first/second | window `(1 - a) + a m` | no |

(Verified numerically against the engine: Bend, Squeeze, Pulse, Sync and Quantize reproduce the measured harmonic amplitudes of the Saw table within
0.2 dB, FM the Bessel combination with 6 cycles, RM the product `(1-a) + a m`.) For Sync/Formant the reset point `s = -0.5/+0.5` means the slave
starts mid-table (x = 0.5): for waveforms that are zero at 0.5 (Saw, Sine, Triangle) the sync restart is click free.
The noise-sourced FM/RM of the original is not offered.

Amount smoothing: the amount (and `delta`) is interpolated linearly per sample across the block; at a note start it starts at its target.

### 6.2 Phase-domain warps added by SynthyBoy (Serum 2 names)

`u = fract(s)`, `a` the amount. Result `read = f(u)`:

| Mode | Law |
|------|-----|
| Sync Var | Sync (6.1) with a window that fades the slave over half a cycle each side of the reset: `edge = min(u', 1 - u')` of the *slave* cycle, `width = 0.5 * var` (var = the warp phase knob 0..1), `w = 0.5 - 0.5 cos(pi edge/width)` for `edge < width` else 1 |
| Bend +, Bend -, Bend +/- | `h = 2u` for `u < 0.5` else `2(1-u)`; `r = 0.5 h^e`; `read = r` (first half) or `1 - r` (second); exponent `e = 1 + 3b` for `b >= 0` and `e = 1/(1 + 3|b|)` for `b < 0`, with `b = a` (Bend +), `-a` (Bend -), `2a - 1` (Bend +/-) |
| Asym +, Asym -, Asym +/- | `read = u^(1 + 3b)` for b >= 0, `1 - (1 - u)^(1 + 3(-b))` for b < 0; b = a, -a, `2a - 1` |
| Flip | window `-1` for `u >= 1 - a`, else +1 (the last fraction `a` of the cycle is inverted) |
| Mirror | `h = 2u` / `2(1-u)` as above; `read = 0.5 * asym(h, 2a - 1)` (the second half plays the first backwards) |
| Remap 1 | `read = u + a (C(u) - u)`, `C` = **the curve of the oscillator's remap slot** (`oscN.remap.slot`: the same drawn curve the frame-position remap of section 6.7 uses, 257 points over 0 .. 1, linear interpolation; the identity when the slot holds no drawn curve). The curve is read **whether or not `oscN.remap.on` is set** (`on` only gates the frame-position remap); both warp slots read the same curve |
| Remap 2 | the same on each half cycle |
| Remap 3 | `read = u - a sin(2 pi u)/(2 pi)` |
| Remap 4 | the same as Remap 1 on each quarter cycle |

### 6.3 Value-domain warps (after the table read)

| Mode | Law |
|------|-----|
| Odd/Even | `E = (T(x) + T(x + 0.5))/2`, `O = (T(x) - T(x + 0.5))/2`; `y = e E + o O` with `e = min(1, 2a)`, `o = min(1, 2(1 - a))` (a = 0.5 is the identity) |
| Filter LPF / HPF | two cascaded one-pole filters on the sample stream with cutoff `ratio * f0` where `ratio = 2^(7a)` (LPF) or `0.5 * 2^(7a)` (HPF) (`g = x/(1+x)`, `x = min(2 pi ratio f0/sr_i, 8)`); the HPF is `input - lowpass` twice |
| Dist ... (Tube, Soft Clip, Hard Clip, Diode 1/2, Linear Fold, Sine Fold, Zero-Square, Asym, Rectify) | the waveshaper of the same name in the distortion effect (document 6 section on the distortion), at drive `10^(30 a/20)` (0 .. 30 dB) with the sample clamped to +-40/drive first; no DC removal |
| Dist Sine Shaper | `sin(1.5708 d y)` |
| Dist Stomp Box | `0.7 tanh(2 d y) + 0.3 clamp(d y, -1, 1)` |
| Dist Tape Sat. | `0.6366 atan(1.4137 d y)` |
| Dist Soft Sat. | `d y/(1 + |d y|)` |

(`d` = the drive, `y` the table sample.)

### 6.4 Modulator warps (FM Thru-Zero, FM Exp, FM Linear, PD, AM, RM from Osc 2/3, Noise, Sub, Filter 1/2)

Modulator `m` in [-1, 1] (clamped to +-4 for the FM modes) is the raw output of the named source in the same block (the filters and
the sub are read from the previous block for the filters: a one-block delay, because the filters run after the oscillators). `side` handling: if only one of the two
voice halves is active the modulator of that half is used for both.

| Mode | Law |
|------|-----|
| FM Thru-Zero | the phase accumulator gets a second integrator: `acc += d (8 a^2 m)` each sample (d = the phase increment, cycles/sample); `read = x + acc` (frequency modulation by `8 a^2 m` times the carrier frequency; negative values run the wave backwards) |
| FM Exp | `acc += d (2^(4 a^2 m) - 1)`; `read = x + acc` |
| FM Linear | like Thru-Zero but the factor is clamped to >= -1 (no backwards running) |
| PD | `read = x + 0.9 a m` (phase distortion; `m` of -1 .. 1) |
| PD Self | the modulator is the table read at the unmodified phase: `read = x + 0.9 a T(x)` |
| AM | window `(1 - a) + a (0.5 + 0.5 m)` |
| RM from Noise/Sub/Filters | window `(1 - a) + a m` |

(The accumulated `acc` is reset to 0 at every note start.)

### 6.5 Spectral warps

The 23 modes after the modulator group (Spectral Comb ... Vocode from Noise) act in the frequency domain on the **spectral oscillator
type only**; a wavetable oscillator ignores them. They are specified in `core/spectral` (Vital-free) and document 7.

### 6.6 Order of the stages in one sample

1. slot 1 phase map (6.1 or 6.2), also computes its window; for the modulator and PD families the modulation is added to the phase;
2. slot 2 phase map on the result of slot 1 (the same table of modes), its window multiplied in;
3. the table read at the final phase (Catmull-Rom);
4. value stages (6.3) of slot 1 then slot 2.

A mode of the value family in slot 1 leaves the phase untouched in step 1.

### 6.7 Frame-position remap with RISE / FALL (`oscN.remap.*`)

`oscN.remap.on` Off: nothing. On: the frame position of each unison pair is passed through the oscillator's drawn curve `C` (slot `remap.slot` 0 .. 3, 257 points over x = 0 .. 1, linear interpolation, identity when no curve is stored) and then slewed:
```
x      = clamp(frame_position / max_frame, 0, 1)            -- max_frame = number of frames - 1 (63 for the built-in morphing tables; the user table's frame count - 1)
target = C(x) * max_frame
every wave-buffer rebuild (every 7 ms, per lane):  time = rise_ms if target > current else fall_ms
   if the lane was off, or the note just started (reset / retrigger), or time <= 0:  current = target           -- a jump
   else  current += (target - current) * (1 - exp(-7 / time_ms))                                            -- one pole, time constant = time_ms
frame_used = current
```
`remap.rise` / `remap.fall`: 0 .. 2000 ms. The slewed position replaces the frame position before unison frame spread, smooth interpolation and clamping. Measured (Saw to Square, osc frame modulated by a 0.5 Hz square LFO between 0 and 100 %, second-harmonic level re the fundamental): without the remap it switches at once (-6.0 dB -> -46 dB); with `remap.on`, `rise = fall = 500 ms`
it glides: -9.5, -12.2, -14.6, -16.9, -19.0, -21.0, -23.0 ... -28.5 dB at 0.1 s steps after the switch to square, and rises from -32.4 dB to -8.5 dB over the first 0.9 s of the note (the note starts from the position the first block saw, here the previous end value of the retriggered LFO, then glides to the real target: a first-block quirk; time constant about 0.5 s).

## 7. Spectral morph (`oscN.morph.*`)

Applies to the spectrum of the frame before the inverse transform (so it is part of the band-limited buffer, rebuilt every 7 ms).
Notation: harmonic `i` (0 = DC, up to 1024) of the frame has amplitude `A_i` and unit phasor `P_i` (so the complex coefficient is `A_i P_i`);
`K` is the band limit of 3.1; the result `out_i` is the coefficient that goes into the inverse transform (everything above `K` is zero);
`m` in [0,1] is the amount plus `morph.spread * 2 v/(P-1)` for the unison pair `v` (clamped to 0..1; Shepard Tone wraps instead, see below).
Interpolation "of coefficients" means complex linear interpolation of `A P`.

| Type | Control law | Operation |
|------|-------------|-----------|
| None | - | `out_i = A_i P_i` for `i <= K` |
| Vocode | `s0 = 2^(1 - 2m)`; `s = s0 * 2048 * f_v / sr_table * ratio_table`, with `f_v` the frequency of the voice (incl. detune), `sr_table` the sample rate the table was recorded at (44100 for the built-in tables, 88200 if unknown) and `ratio_table` its frequency ratio (1) | the table is treated as a fixed spectrum on an absolute frequency axis; harmonic `i` of the note reads the source at `x = max(1, i s)`: `i0 = floor(x)`, moved down by one if `i0` and `i` have different parity, `t = (x - i0)/2`, `out_i = s * ((1 - t) A_{i0} P_{i0} + t A_{i0+2} P_{i0+2})` for `1 <= i <= min(K, 1024/s)`; `out_0 = A_0 P_0`. The factor `s` keeps the spectral density (a `1/k` spectrum is unchanged: the Saw table is invariant). |
| Formant Scale | `s = 2^(2 - 4m)` (a range of +-2 octaves), no pitch coupling | the same operation |
| Harmonic Stretch | `s = 2^(8m - 4)` | each source harmonic `i` (1 .. `min(1024, (K-1)/s + 1)`) is moved to the position `x = max(1, (i-1) s + 1)`, split linearly over the bins `floor(x)` and `floor(x)+1` (weights `1-t`, `t`), the coefficients are **added**; DC kept |
| Inharmonic Stretch | `s = 2^(24 m - 12)`, per harmonic `shift_i = s^(log2(max(i,1))/10)` | as Harmonic Stretch with `x = max(1, shift_i (i - 1) + 1)`; a partial that lands at or above `K` ends the loop |
| Smear | `m' = 1 - (1 - m)^3` | `a = A_0 (1 - m')`, `out_0 = a P_0`; for `i = 1 .. K`: `a = A_i + (a - A_i) m'`, `out_i = a P_i`, then `a = a (i + 0.25)/i` (a low-pass-like smear of the amplitudes that lets partials ring into the next harmonics) |
| Random Amplitudes | `q = 15 m` (the stage) | a fixed table of uniform random numbers `r[stage][i]` in [-1,1) (any fixed table: seed it deterministically) with 16 stages x 1025 harmonics; `j = min(floor(q), 14)`, `t = q - j`; per harmonic `g_j = (1 + q) * max(1 - q - q r[j][i], 0)`, `g = lerp(g_j, g_{j+1}, t)`; `out_i = min(g A_i, 1024) P_i`. At small `q` all gains are ~1 (the gain is `1 + q (1 - r)`-like); at large `q` most harmonics are removed (character: "random harmonic deletion that is stable per note"). |
| Low Pass | cutoff `c = 2^(10 m) + 1` harmonics | brickwall at `c` with the last partial faded linearly (the original fades in pairs of harmonics) |
| High Pass | cutoff `c = 2^(10 m) * 1025/1024` | complement |
| Phase Disperse | `p = -(2m - 1) * 0.05` | every harmonic's phasor is rotated by `theta_i = p ((i - 24)^2 - 23^2)` radians (a quadratic group delay around harmonic 24); amplitudes unchanged |
| Shepard Tone | `m = fract(0.99 m)/0.99` (wraps), then `m' = 1 - m` | each coefficient is mixed with the coefficient at **twice** its index: `out_i = (1 - m) A_i P_i` for the regular series plus the Shepard series `lerp(A_i, A_{i/2}, m)` for even `i` with the phases interpolated the short way; the endless sweep is made by wrapping the phase increment by a factor of 2 (and the phase by the matching shift) at a buffer boundary when the morph value wraps, cross-fading the two buffers. Character: a harmonic series that keeps rising/falling in pitch as `m` is swept. Match by ear and the vectors. |
| Time Skew | `m' = 16 m^2` | needs >= 2 frames; the frame read for harmonic `i` is `tri(frame/256 + m' log2(i)/11) * 256` with the triangle `tri(x) = 1 - |1 - 2 fract(x/2)|`, interpolated between frames `floor` and `floor+1` (clamped to `F-2`): `out_i = lerp(A) * lerp(P)` (phasors interpolated component-wise, not renormalised); DC from the current frame. So low harmonics follow the frame position and high ones lead/lag it |

For all types, harmonic numbers above `K` are zero and the result goes through the usual inverse transform and 7 ms cross-fade.

**Better:** the morph types are the expensive part of this engine because they need an inverse DFT per rebuild per unison voice; with
morph off the own engine can use precomputed mips.

## 8. Unison inside the oscillator (summary)

* Voice count `U`, pair layout, detune, stack, blend, spread: document 1 section 9.
* Each pair has its own wave buffer only if spectral unison needs different waveforms; otherwise the pairs share one buffer (identical
  waveform, different phase increments).
* The pairs share the warp type but get warp amounts `a_v` (6) and morph values (7) and frame positions (3.2) spread over the stack.
* Unison voices > 2 per pair: not possible; the maximum is 16 voices = 8 pairs. The tier cap (`Quality::maxUnison`) reduces `U`.

## 9. Sub oscillator (`sub.*`)

Not part of the Vital baseline (SynthyBoy addition, original design). Per voice and channel:

* `f = 8.1757989156 * 2^(n/12)`, `n = clamp(n_v + 12 sub.octave + sub.tune/100 + voice-control detune, -24, 136)`; `d = min(f/sr_i, 0.499)`. Note
  `sub.mono` On: only the voice with the lowest note of the active voices sounds (within 0.5 st), the gate is ramped across the block.
* Phase `p` in [0,1) advances `p += d`, wrapped. At a note start `p = sub.phase / 100`; with `sub.phase_contiguous` On the new note starts at the phase the newest voice had at the start of the block.
* Waveforms: Sine `sin(2 pi p)`; Rounded Rect `tanh(3 sin(2 pi p))/0.99505`; Triangle `1 - |4 fract(p + 0.25) - 2|`; Saw `2p - 1 - blep(p)`;
  Square +1 for `p < 0.5` with polyBLEP corrections at both edges; Pulse 25 %: duty 0.25 square, with the DC subtracted and the peak
  normalised (`(sq + 0.5)/1.5` for duty 0.25 -> peak 1). polyBLEP residual at an edge at `p = 0` with increment `d`: for `p < d`, `t = p/d`:
  `2t - t^2 - 1`; for `p > 1 - d`, `t = (p - 1)/d`: `t^2 + 2t + 1`.
* Raw output = waveform (times the mono gate); levelled = raw * `sub.level/100` (linear, clamped 0..1) * pan (doc 1 sec 8).
* Routing like the oscillators (destination, bus sends, filter blend, `env1`), the tap `sub_out` of the modulation matrix is the last sample of the block.

## 10. Noise source (`noise.*`)

* A looped (or one-shot, `noise.loop`) buffer of 1 s at 44.1 kHz played back with a phase increment
  `2^(transpose/12 + tune... ) * sr_buffer / sr_i`, Catmull-Rom interpolated, from a chain of **half-band downsampled copies**: the copy
  used is the one at `floor(log2(increment))` (so transposing up never aliases; 2:1 steps with a 31/33-tap FIR) and one upsampled copy for
  increments below 1. `noise.keytrack` On adds the note relative to middle C (note - 60) to the transpose. `noise.pitch` -48..48 st, `noise.fine`
  ±100 ct (-1..1 st). Transposition snap as for the oscillators (`transpose_quantize`).
* The generators (selected by `noise.sample` choice, all deterministic, the seed depends on the type): White (uniform ±0.9, a linear congruential
  generator), Pink (Paul Kellet's 7-stage filter of white, scaled 0.18), Brown (integrated white, leaky), Geiger (sparse clicks), Tape Hiss, Vinyl Clicks,
  Digital Hash, Velvet Noise, Atmosphere. The precise recipe is free; the contract is the **spectral tilt and the level**: White flat, Pink -3 dB/oct,
  Brown -6 dB/oct, all with an RMS of about -15 dBFS at unit level (peak <= 0.9), and the colour types are two-channel (`noise.stereo` mixes the
  right channel with the left one: `r' = (1 - c) l + sqrt(1 - (1 - c)^2) r`... i.e. `c = 1 - stereo`, `r' = c l + sqrt(1 - c^2) r`; `noise.filter` -1..1 = a 2-pole low-pass (negative,
  `20000 * 0.002^|f|` Hz) or high-pass (positive, `20 * 800^f` Hz)).
* `noise.phase` (0..1) the start position, `noise.random_phase` On random start per voice (both channels), `noise.bounce` ping-pongs.
* Level: audio-rate smoothed (5 Hz) like the oscillators, gain `noise.level/100` (clamped to 2), pan as in doc 1. The raw output is also the
  FM/RM source "Noise".
* **`noise.filter`** (colour generators only: White, Pink, Brown, Geiger; the other generators and user samples are untouched): two cascaded one-pole low-pass filters (`y1 += c (x - y1)`, `y2 += c (y1 - y2)`, `c = min(0.99, 1 - exp(-2 pi f/sr_i))`, `f` limited to 0.45 sr_i) on the raw noise after resampling and the stereo stage: for `a = noise.filter/100 < 0` a **low-pass** (output `y2`) at `f = 20000 * 0.002^|a|` Hz (-100 % = 40 Hz); for `a > 0` the complement
  **`x - y2`** at `f = 20 * 800^a` Hz (+100 % = 16 kHz); `|a| < 1e-4`: off (the states follow the signal while off, so engaging is click free). Measured on white noise (re 500 Hz): -100 %: -24 dB at 2 kHz, -40 at 5k, -54 at 11k, -65 at 21k, RMS 0.020; -50 %: -13 / -28 / -42 / -52, RMS 0.093; +50 %: flat within 1.7 dB, RMS 0.506; +100 %: +11.7 / +19 / +23 / +25 dB, RMS 0.27.
* **`noise.stereo`**: the colour generators have independent left and right noise; `c = 1 - stereo/100`, `right = c * left + sqrt(1 - c^2) * right_buffer` (left unchanged): measured correlation 1.000 / 0.499 / -0.002 at 0 / 50 / 100 %, both channels RMS 0.510.
* **Generator noise pitch** (`noise.pitch`, `noise.fine`, `noise.keytrack`): the generator buffer (1 s at 44.1 kHz, band-limited to 22.05 kHz) is played at the rate `2^(semitones/12) * 44100/sr_i` through a chain of 2:1 half-band down-sampled copies (rate > 1) and one 2x up-sampled copy (rate < 1), so
  **transposing down low-passes the noise**: white noise at -12 st has its energy up to about 12 kHz (48 kHz host; 14 kHz is -61 dB), at -24 st up to about 6 kHz; **transposing up stays white to the top but loses level**: RMS 0.5025 at 0, 0.3803 at +12 st (-2.4 dB), 0.3156 at +24 st (-4.0 dB). Pink / brown at pitch 0: -5.4 / -9.6 / -12.1 / -17.4 dB (pink) and -10.5 / -19.2 / -24.9 / -29.1 dB (brown) at 2 / 5 / 11 / 21 kHz re 500 Hz; RMS pink 0.310, brown 0.194, white 0.5025.
* When off, outputs are cleared once.

## 11. Oscillator type switch

`oscN.type` changes between the wavetable oscillator and the sample-type oscillators (document 7). While the oscillator is sounding a change
**cross-fades over 10 ms** (both run; raw and levelled mixed linearly), otherwise it switches at once.

### 11.1 Why the Filter HPF/LPF warps stay clean in engine-vital

The filters are linear, so they create no new components; the noise floor of the warped oscillator is the floor of the plain oscillator, and **the high-pass only removes harmonic energy**. Measured on the Saw table, note 57 (220 Hz), `osc1.level 100`, a Blackman-windowed 1 s analysis,
energy outside +-1 % of the harmonics relative to the total power: no warp -98.9 dB (an absolute floor of about -105 dB re an amplitude-1 sine, RMS 0.489); Filter HPF 0 / 25 / 50 / 100 %: -97.7 / -93.5 / -88.6 / -84.9 dB with the RMS falling to 0.423 / 0.254 / 0.130 / 0.018 (the absolute floor stays at about -105 dB; the relative floor rises only because the signal falls); Filter LPF 0 / 25 / 50 / 100 %: -123.6 / -122.8 / -112.5 / -100.3 dB.
What keeps the floor this low (there is no special interpolation-image treatment beyond this): (1) the wave buffer holds only harmonics up to the 22 .. 24 kHz band limit of section 3.1; (2) the table is read with **cubic (Catmull-Rom) interpolation** at 2048 points per cycle and at 2x the host rate; (3) the output decimator attenuates everything above 0.45 sr by about 60 dB; (4) the warp filter runs per sample at that internal rate with the corner at `ratio * f0` (`ratio = 0.5 * 2^(7 a)` for the HPF: 1.68 for a = 25 %, 370 Hz at 220 Hz), `g = x/(1 + x)`, `x = 2 pi ratio f0/sr_i`, two cascaded one-pole high-pass stages.
An implementation that reads with linear interpolation, at the host rate, or from a table band-limited only at the host Nyquist has a higher absolute floor, which the high-pass then makes 5 .. 9 dB more audible against the quieter signal. The acceptance criterion is an **absolute inharmonic floor of -100 dB or lower** (power re 1) for this setup.

## 12. Cost and quality notes ("better")

* The brickwall inverse-DFT rebuild per unison voice per 7 ms is the dominant cost: at 16 voices x 3 oscillators x 16 polyphony it is ~4 800
  2048-point real transforms per 7 ms. A better engine builds **mip levels** (one band-limited copy per half octave per frame, built once at table
  load) and cross-fades between the two nearest copies per sample or per buffer; the sound is the same to within the brickwall's Gibbs ripple.
  The morph types need the transform; limit them to what is on.
* Warps are not band-limited (Sync, Quantize, Pulse, Squeeze, FM, PD and the distortions alias). The engine runs at 2x oversampling by default
  so aliasing above 24 kHz is removed by the decimator; at 1x (`low` tier) they alias audibly. A better engine can use polyBLEP / BLAMP at the discontinuities of the
  phase maps (Sync, Quantize, Pulse, Flip) or 4x for those modes.
* The phase accumulator as a 32-bit integer is exact; if you use floats, keep one double per voice.
* Dense unison with many voices at the same pitch ratio (U = 16, detune 0) is identical voices: sum them as one when `detune = 0` and the phases are equal.

## 13. Test vectors

All: filter off, `master.volume 0`, `osc1.level 100`, `env1.attack 0`, `osc1.random_phase 0`, note 57 (220 Hz), velocity 1, held 0.9 s; measured on the steady part (0.2 .. 0.7 s).
`h1..h8` = amplitudes of harmonics 1..8 in dB (Hann window, zero-padded FFT, peak within +-10 % of the harmonic). Tolerance +-0.3 dB where > -45 dB, otherwise "below -40 dB".

| # | Setup | h1 .. h8 (dB) |
|---|-------|----------------|
| O1 | Saw table (`osc1.wavetable 1`), no warp | -5.3 -11.4 -14.9 -17.4 -19.3 -20.9 -22.2 -23.4 |
| O2 | Saw, Sync (`warp.type 1`) amount 25 / 50 / 75 / 100 % | 25: -17.4 -23.4 -26.9 -29.4 -31.4 -32.9 -34.3 -35.4; 50: as O1; 75: -140 .. only h4 -5.3, h8 -11.4 (R = 4); 100: R = 16 so energy at h16 |
| O3 | Saw, Formant 25 / 50 / 75 | 25: -22.3 -44.3 -55.9 -63.7 ..; 50: -10.3 -32.3 -43.8 -51.7 ..; 75: -44.3 -33.9 -19.0 -9.2 -19.2 -38.8 -26.5 -15.0 |
| O4 | Saw, Quantize 25 / 50 / 75 | 25: as O1; 50: -4.9 -13.0 -17.6 -26.5 -35.8 -34.0 -30.2 -26.4; 75: -27.5 -23.0 -21.9 -23.2 -26.9 -33.7 -47.4 -75.2 |
| O5 | Saw, Bend 25 / 50 / 75 | 25 and 75: -7.1 -10.5 -14.8 -17.9 -19.0 -20.8 -22.5 -23.2; 50: as O1 |
| O6 | Saw, Squeeze 25 / 50 / 75 | 25: -8.3 -13.2 -15.6 -17.2 -18.8 -20.4 -22.0 -23.5; 75: -3.1 -13.2 -14.2 -17.2 -19.9 -20.4 -22.5 -23.5 |
| O7 | Saw, Pulse 25 / 50 / 75 | 25: -8.4 -9.7 -15.8 -17.4 -18.8 -21.5 -21.9 -23.4; 50: -14.1 -11.4 -13.2 -17.4 -20.5 -20.9 -21.5 -23.4; 75: -25.4 -20.1 -18.0 -17.4 -17.9 -19.2 -21.2 -23.4 |
| O8 | Sine tables on osc 1 and osc 2 (`osc2.on 1`, `osc2.wavetable 0`, `osc2.destination 6`, `osc2.level 100`, `osc2.random_phase 0`), osc 1 `warp.type 7`, osc 2 `transpose +12`, amount 25 / 50 / 100 | h1, h3, h5, h7 (the others < -120): 25: -5.9 -0.4 -12.6 -12.0; 50: -8.9 -8.0 -10.4 -9.0; 100: -14.8 -14.5 -15.0 -14.4 (a model `sin(2 pi (x + 6 a^2 sin(2 pi (2 x + 0.5))))` reproduces these) |
| O9 | same with `warp.type 9` (RM), osc 2 `transpose 0`, amount 25 / 50 / 100 | h1: -2.5 / -6.0 / < -120; h2: -18.1 / -12.0 / -6.0 (the product `(1 - a) + a sin`) |
| O10 | Saw, Flip (`warp.type 18`) amount 25 / 75 | h1..h8 -6.5 -16.0 -22.3 -23.4 -24.7 -26.8 -28.3 -29.4 (both); at 50 %: h1 -9.3, h2 -50, h3 -29.1, h4 -50 (a symmetric wave) |
| O11 | Saw, Odd/Even (`warp.type 24`) amount 25 / 50 / 75 | 25: -5.3 -17.4 -14.9 -23.4 -19.3 -26.9 -22.2 -29.4; 50: as O1; 75: -11.4 -11.4 -20.9 -17.4 -25.3 -20.9 -28.3 -23.4 |
| O12 | Saw, Filter LPF (`warp.type 25`) 25 / 50 / 75 | 25: -6.1 -14.1 -20.1 -25.3 ..; 50: -5.4 -11.7 -15.6 -18.6 ..; 75: -5.3 -11.4 -15.0 -17.5 .. |
| O13 | Saw, morph Low Pass (`morph.type 7`) amount 25 % | h1..h5 as O1, h6 -24.5, h7 and up < -150 (cutoff 2^2.5 + 1 = 6.7 harmonics) |
| O14 | Saw, morph High Pass (`morph.type 8`) 25 % | h1..h4 < -150, h5 -28.7, h6 on as O1 |
| O15 | Saw, morph Formant Scale (`morph.type 2`) 25 / 75 % | 25: h1 -2.8 (+2.5 dB), rest ≈ O1; 75: h1 -11.4, h2 -23.4, h3 -12.9 |
| O15b | Saw, morph Harmonic Stretch (`morph.type 3`) 25 / 75 % | 25: -1.2 -6.2 -12.1 -15.4 -17.8 -19.7 -21.2 -22.5; 75 (s = 4): -5.3 at h1, h5 -11.4, h9 -14.9, all other harmonics < -120 |
| O15c | Saw, Inharmonic Stretch (`morph.type 4`) 25 / 75 % | 25: -4.3 -5.8 -6.6 -8.8 -11.0 -13.0 -14.7 -16.3; 75: 2.3 -15.9 -9.0 -16.7 -15.0 -16.8 -18.9 -21.4 |
| O15d | Saw, Smear (`morph.type 5`) 25 / 75 % | 25: -12.8 -18.9 -13.6 -17.1 -14.2 -17.8 -15.9 -19.2; 75: -41.5 -47.5 -39.1 -42.7 -36.2 -40.4 -34.8 -39.0 |
| O15e | Saw, Random Amplitudes (`morph.type 6`) 25 / 75 % | 25: only harmonic 6 survives (-20.8); 75: only harmonic 5 (-2.7) (which harmonics survive depends on the random table: the contract is "sparse, a few harmonics, loud") |
| O15f | Saw, Shepard Tone (`morph.type 10`) 25 / 75 % | 25: -77.7 -104.6 -31.4 -96.5 -101.5 -20.5 -70.7 -110.5 -40.9 -48.4 -107.1 -32.6; 75: -67.6 -102.8 -113.5 -96.2 -19.0 -44.2 -108.8 -125.9 -112.3 -25.0 -30.1 -106.0 (character: the fundamental is gone, energy on a few harmonics at multiples of 3 and 5; do not spend effort on matching the exact bins) |
| O15g | Formant Sweep table (`osc1.wavetable 9`, `osc1.frame 50`), no morph; morph Formant Scale 25 / 75 %; Vocode 25 / 75 % | none: -22.8 -27.7 -24.1 -21.2 -19.2 -18.2 -18.2 -19.1 -20.9 -23.3 -25.8 -27.6; Formant Scale 25: -17.4 -15.2 -12.7 -13.1 -17.0 -21.6 -22.3 -21.8 ..; 75: -28.8 -39.7 -29.1 -33.7 -29.8 -29.9 -28.6 -27.3 ..; Vocode 25: -5.1 -18.3 -79.4 then < -140; 75: -1.3 -11.2 -12.1 -24.3 -48.3 -84.4 then < -130 |
| O16 | Phase: Sine, `osc1.phase` 0 / 90 / 180 / 270; fit of the fundamental at the note start | the signal equals `-sin(2 pi t + phase)` (+-3 deg, the output stage delays by ~1.4 samples): phases of `cos` reference -87.7, -177.7, 92.4, 2.4 |
| O17 | Pitch: `osc1.transpose 12`, note 69 | 880.00 Hz (+-0.1 Hz) |
| O18 | Level: `osc1.level 50` | -6.02 dB |

Tolerances for O10 .. O15: +-0.4 dB for entries above -40 dB.
