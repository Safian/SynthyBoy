# 06 Effects

Status: final (2026-11-12). Covers the nine effects of the Vital engine and the frame they sit in: chain order, on/off, the dry/wet law, the output trim,
the rate they run at, then chorus, compressor, delay, distortion, EQ, flanger, phaser, reverb (the FX filter is the filter bank of document 5 with its own page).
**Not in this document** (they exist as Vital-free libraries the own engine links): Hyper/Dimension, Utility, the three splitters (`core/fx-serum-a`), Bode, Convolve (`core/fx-serum-b`).
The rack/bus/pool structure (Main, Bus 1, Bus 2, the splitter band racks, the effect instance pool) is our design, described in `docs/ui-design/08-effects.md` and `18-FX-INSTANCES.md`; the parts of it that define the sound or the timing are here: the chain law (section 1), the latency compensation across the buses (section 11) and the behaviour of the instance pool (section 12).

## 1. The frame

* **Rate.** The whole effect chain runs at the internal (oversampled) rate `sr_i` of document 5 section 1.5 (2x by default), after the voices are summed. All delay times, filter corners and LFO rates are in real
  time, so the rate does not change them, but the aliasing of the distortions and the cost do. **Better:** run the linear effects (chorus, delay, reverb, EQ, phaser, flanger) at the host rate and only the nonlinear ones (distortion, compressor limiter) oversampled.
* **Input and signal format.** Stereo (L, R). The voices' sum arrives on the Main bus (document 1); the effects of a rack see the stereo bus in the order of `fx.order.1 .. 16` (the **default order**: FX Filter, Distortion, Compressor, EQ, Chorus, Flanger,
  Phaser, Delay, Reverb, then the Serum modules in their own order); a position holds one module; an effect with `*.on` = 0 is skipped (its state is cleared when it is switched on).
* **Wet/dry law** (every effect with a `mix`): equal power, `wet = sin(pi/2 m)`, `dry = cos(pi/2 m)` for `m` in [0,1] (so 50 % is -3 dB each, and the sum of the powers is constant), ramped linearly across the chunk. The exceptions, which use a linear crossfade
  `out = lerp(dry, wet, m)`: **Compressor**, **Distortion**, **Phaser**, the filters (document 5). The **Flanger** mix knob runs 0 .. 100 % over `m` in [0, 0.5] (so its wet is at most `sin(pi/4)` = -3 dB and its dry at least -3 dB).
* **Output trim.** Every module (also the Serum ones) ends in `*.level` (dB, -24 .. 12; `fxfilter.level` -60 .. 12): linear gain `10^(dB/20)`, ramped across the chunk, 0 dB is bit-exact identity.
* **State.** Switching an effect on clears its delay lines and filters and starts the mix at 0 (a one-chunk fade-in); a module that is off costs nothing. `reset()` / "all sounds off" clear every module.
* **Parameter smoothing** (nothing is stepped): the delay-type effects glide (3.3), the others interpolate linearly across the chunk; the cutoffs listed in doc 1 sec 10 are smoothed with the 5 Hz one-pole.
* **Tempo.** Effects with `sync` use the host tempo like the LFOs (doc 3 sec 2): chorus, delay, flanger, phaser (`*.division`: the same table; the delay's `division` has 9 entries: 4 bars .. 1/64 note = indices 4 .. 12 of the table).
* **Latency.** Bode and the compressor's limit mode add latency (reported to the host); everything else is zero latency. Every other signal path is delayed to match: section 11.

## 2. Chorus (`chorus.*`)

Keys: `rate` (Hz or sync), `mix`, `depth` (0 .. 1), `feedback` (-0.95 .. 0.95), `delay1`, `delay2` (1 .. 20 ms; stored as log2 seconds), `voices` (1 .. 4 = number of voice pairs), `filter_cutoff` (note), `filter_spread` (0 .. 1).

The input (L, R) is processed by **`voices` pairs of two delay lines**, each stereo (a line has an L and an R side). Pair `i` (0-based), voice A and voice B:

```
phase(i, voice, channel) = lfo + 0.25 [channel = R] + 0.5 [voice = B] + 0.25 i / voices          -- cycles; lfo = global phase, rate in Hz, free running
mod        = 1 + 0.5 sin(2 pi phase)                                                              -- 0.5 .. 1.5
base(voice)= delay1 for A, delay2 for B (seconds);  avg = (delay1 + delay2)/2
t_i        = i/(voices - 1) for i > 0, 0 for i = 0
delay(i)   = mod * 0.03 depth + lerp(base(voice), avg, t_i)                                       -- seconds; the delay line is read at 1/delay Hz (period = delay * sr)
```

Each line: a feedback delay with the loop filter of section 3 (a band-pass: low-pass at `cutoff + radius`, minus a low-pass at `cutoff - radius`, `radius = spread * 96` semitones, gain compensated by `f_lo/f_hi + 1`), the saturation `hardtanh(v/8)*8`
in the loop, feedback `chorus.feedback`, wet 1, dry 0 (the dry signal is added at the end). The loop filter gain is `f_lo/f_hi + 1` (see section 3). Reading is Catmull-Rom interpolated, the period clamped to [3, 0.08 sr] samples. The pair outputs are summed with a factor 0.5; **A and B are summed into both channels per side**
(`out = 0.5 sum_i (A_i + B_i)` per channel). Final: `mix` equal power. The LFO phase is set from the host song position when the transport is synced.

## 3. Delay (`delay.*`)

Keys: `time` (left), `time2` (right) -- stored as frequency `2^c` Hz; `sync`/`division`/`division2`, `feedback` (-1 .. 1), `mix`, `style` (Mono, Stereo, Ping pong, Mid ping pong, Tap->Delay), `filter_cutoff` (note), `filter_spread`, `hq` (our flag),
`offset`, `offset_left` (scalars 0.25 .. 4 on the right / left time, our addition).

* **Time.** `T_L, T_R` seconds (Mono uses the left time for both channels; Stereo/Ping pong/Tap use `time2` for the right). The delay time **glides**: the target frequency is smoothed with a half-life of 20 ms
  (`f_last = lerp(f_target, f_last, 0.5^(n/(0.02 sr)))` per chunk, n = chunk samples), the period (samples) is `sr/f_last` clamped to [3, 4 s * sr], and moves half way to its new value each chunk, interpolated linearly across the chunk. So changes of the time bend the
  pitch of the echoes (tape-like). Mid ping pong adds the swapped left period to the left side (`T_L' = T_L + T_R`).
* **Core (Mono, Stereo).** per channel: `read = delay(period)` (Catmull-Rom, or 8-point if `hq`); `write = L(x + fb * read)` with `L(v) = 8 hardtanh(v/8)` (a soft limiter at 8x full scale); the written signal passes the loop filter and is stored; `out = dry x + wet read`.
* **Loop filter** (not applied for the clamped/unfiltered variants): `lp = onepole_lowpass(write * g, f_hi)`, `stored = lp - onepole_lowpass(lp, f_lo)` where `f_hi = freq(cutoff + radius)`, `f_lo = freq(cutoff - radius)`, `radius = spread * 96 st`
  (clamped to [1 Hz, 0.4535 sr]) and `g = f_lo/f_hi + 1` (between 1 and 2): a band-pass in the feedback path whose width grows with the spread (spread 100 % at the default cutoff is wide open; a narrow spread rolls off lows and highs of the repeats).
* **Ping pong** (right-lane feedback forced to 1, left to the knob): the written value is channel-swapped, `stored = swap(filter(L(x + fb read)))`: an echo alternates between the channels. **Mid ping pong**: mono-sum input on the left, same swap. **Tap->Delay**
  (our addition, Serum 2): the left side is a tap (mono input `(L + R)/sqrt2` at the left time, no feedback), the right side is a delay fed by the tap and by itself; the output is the sum of both on both channels.
* `mix` equal power (wet = `sin`, dry = `cos`). The first echo has amplitude `wet`, then `fb` per repeat.

Test vector D1: Sine bursts (osc1 level 100, env1 attack 0, decay 5, sustain 0, release 5, `osc1.phase 90`, note 69 at 0.1 s for 20 ms), `delay.on 1`, `style 0` (Mono), `sync 0`, `time 250`, `feedback 50`, `mix 50`, `filter_spread 100`:
peaks of the windowed (20 ms) amplitude at 0.1 / 0.34 / 0.6 / 0.84 / 1.1 / 1.34 / 1.6 s: 0.812 / 0.755 / 0.407 / 0.197 / 0.096 / 0.050 / 0.024 (successive echoes halve; spacing 250 ms; the dry burst is `cos(pi/4)`).
D2: `style 1` (Stereo), `time 250`, `time2 400`: left channel echoes at 0.1 + k 0.25 s, right channel at 0.1 + k 0.4 s (0.5, 0.9, 1.3, 1.7), amplitudes as D1.

## 4. Distortion (`distortion.*`)

Keys: `type`, `drive` (-30 .. 30 dB), `mix` (linear crossfade), `filter_order` (None, Pre, Post), `filter_cutoff` (note), `filter_resonance`, `filter_blend`, `filter_keytrack` (our addition).
Drive gain `d = 10^(dB/20)` (dB clamped to +-30). The types (`x` = input sample, each channel on its own, no cross terms):

| Type | Law |
|------|-----|
| Soft clip | `tanh(d x)` |
| Hard clip | `clamp(d x, -1, 1)` |
| Linear fold | `|(-4 fract(0.25 d x + 0.75) + 2)| - 1` (a triangle folding of period 4/d in x: 0 for x = 0, +-1 turning points) |
| Sine fold | `sin(2 pi fract(0.5 - 0.25 d x))` |
| Bit crush | step `q = clamp(((dB + 30)/60)^2, 1.5e-8, 1)`; `y = round(x / q) * q` |
| Down sample | sample-and-hold per channel; the hold period in samples is `sr_i * max(0.99/(1 - u)^2, 1) / 88200`, `u = (dB + 30)/60` (so the effective rate is 88 200 (1 - u)^2 Hz) |
| Tube | `tanh(d x + 0.3) - tanh(0.3)` |
| Diode 1 | `(1 - e^{-v^+}) - 0.5 (1 - e^{2 v^-})` with `v = d x`, `v^+ = max(v, 0)`, `v^- = min(v, 0)` |
| Diode 2 | `e^{v^-} - e^{-v^+}` |
| Zero-Square | `tanh(32 d x)` |
| Asym | `tanh(v^+) + max(v^-, -0.5)` |
| Rectify | `|clamp(d x, -1, 1)|` |
| X-Shaper, X-Shaper (Asym) | two drawn transfer curves A and B (513 points, 0..1), drive (-30 .. 30 dB mapped to 0 .. 1) morphs between them: symmetric type reads the **magnitude** `|x|` (clamped to 1) and keeps the sign; asym type reads `x` over -1 .. 1 (as 0 .. 1) and returns -1 .. 1 |

The asymmetric types (Tube, Diode 1, Asym, Rectify, X-Shaper Asym) are followed by a 10 Hz first-order DC blocker. Switching the type clears the state. With `filter_order` Pre the signal goes through the filter then the shaper, Post shaper then filter, None: no filter.
The filter is a 12 dB state variable filter (the Digital model of document 5 in its simple form: no soft limiter, no drive compensation) with `Q = 0.5 + 15.5 r^3`, blend 0 .. 100 % = LP .. BP .. HP, cutoff in note units (`+ keytrack * (last note - 60)`), audio-rate smoothed.
Final: `lerp(dry, wet, mix)`. Measured (Sine, level 50 % -> input peak 0.5006, filter None, mix 100): Soft clip drive 0 / 12 / 24 dB: peak 0.462 / 0.963 / 1.000 (= `tanh(0.5 d)`); Hard clip 0.501 / 1.014 / 1.077 (the excess over 1 is the ringing of the output decimator);
Linear fold 0.501 / 0.971 / 0.946; Sine fold 0.709 / 1.000 / 1.005.

**Bit crush** (Sine, input peak 0.5, RMS 0.354; filter None, mix 100; q = ((drive_dB + 30)/60)^2): drive -30 dB (q 0): output = input (error RMS 0.0000); -12 dB (q 0.09): RMS 0.355, error RMS 0.026; 0 dB (q 0.25): RMS 0.372, error RMS 0.063, peak 0.565; +12 dB (q 0.49): RMS 0.401, error RMS 0.119, peak 0.607;
+30 dB (q 1): RMS 0.173, peak 1.07 (the sine of peak 0.5 rounds to 0 and +-1 steps: a square-ish wave of reduced RMS). **Down sample** (Sine 110 Hz): drive -30 / 0 / +12 dB (effective rate 88200 / 22050 / 7938 Hz): fundamental -6.0 dB and no visible sidebands; +24 dB (882 Hz): fundamental -6.9 dB with images at 335 / 555 / 781 / 1001 Hz at -17 / -21 / -26 / -28 dB.

## 5. Equalizer (`eq.*`)

Three bands in series: low, mid, high. Keys per band: `mode`, `cutoff` (note), `gain` (dB, -24 .. 24), `resonance` (0 .. 1, stored as the square root); optional `eq.mid2.*` (our fourth band, Off/Peak/Notch). `eq.law` chooses the filter family:

* **Vital law** (`eq.law` 0): each band is the Digital SVF of document 5 section 2 (simple form: no soft limiter, no drive compensation, drive 1) in a special mode; `r` = the band's resonance (0 .. 1, stored as the square root), `Q = 0.5 + 15.5 r^3`, `R = 1/Q`:
  * low: Shelf = the shelving mode with blend -1 (a low shelf); High pass = the 12 dB mode with blend +1 (high-pass);
  * mid: Peak = the shelving mode with blend 0 (a bell); Notch = the notch/pass-swap mode with blend 0 (`LP + HP`);
  * high: Shelf = the shelving mode with blend +1 (a high shelf); Low pass = the 12 dB mode with blend -1.
  The shelving mode: `A = 10^(gain/20)`; blend `b` in {-1, 0, +1}; `low_amount = lerp(A, 1, clamp(b + 1, 0, 1))`, `high_amount = lerp(1, A, clamp(b, 0, 1))`, `band_amount = R sqrt(A) lerp(1, sqrt(A), 1 - b^2)`; the corner is moved by `3 b log2(A)` semitones;
  output `= high_amount HP + band_amount BP + low_amount LP` (HP, BP, LP of the SVF of document 5). The measured responses below are the reference.
* **RBJ law** (`eq.law` 1, our extension, the Serum 2.1.5 law): the bands are the RBJ cookbook biquads (`10^(dB/40)` amplitude factor `A`): peaking, low shelf, high shelf with `alpha = sin(w0)/(2Q)`, high-pass, low-pass and notch as in the cookbook; the cutoff is clamped to [10 Hz, 0.499 sr];
  `Q = 0.4 + 1.7417 r^3` for the shelves, `Q = 0.1 + 18.9053 r^4` for the others (r = resonance 0..1); coefficients are interpolated linearly across the chunk; transposed direct form II.
* The EQ operates in the order low, mid, (mid2), high.

Measured gain of white noise (filter off), `eq.on 1`, relative to the dry noise, at 63 / 125 / 250 / 500 / 1k / 2k / 4k / 8k / 16k Hz (E1 .. E7):

| # | Setup | Gain (dB) |
|---|-------|-----------|
| E1 | mid Peak, cutoff 1 kHz, gain +12, default resonance | 0.4 1.0 3.4 7.9 **12.0** 7.9 3.3 1.0 0.3 |
| E2 | as E1 with resonance 100 % | 0.0 0.0 0.0 0.1 **10.8** 0.1 0.0 0.0 0.0 |
| E3 | low Shelf, cutoff 250, gain +12 | 12.2 12.4 10.5 1.4 -0.4 -0.2 -0.0 -0.0 -0.0 |
| E4 | high Shelf, cutoff 4 kHz, gain -12 | 0.0 0.0 0.0 0.0 0.2 0.4 -1.4 -10.8 -12.4 |
| E5 | low mode High pass, cutoff 250 | -20.3 -11.7 -0.7 0.7 0.2 0.1 0.0 0.0 0.0 |
| E6 | high mode Low pass, cutoff 4 kHz | 0.0 0.0 0.0 0.1 0.2 0.7 -0.7 -11.7 -25.5 |
| E7 | mid Notch, cutoff 1 kHz | -0.0 -0.1 -0.4 -1.9 **-28.3** -1.8 -0.3 -0.1 -0.0 |
| E8 | RBJ law, mid Peak 1 kHz +12 | 1.1 2.5 6.3 10.1 **11.9** 10.1 6.2 2.6 0.7 |

Tolerance +-1 dB.

## 6. Compressor (`compressor.*`)

A multiband compressor with **upward and downward** stages per band, three bands (or one), power-domain detector.

* **Bands** (`compressor.bands`): Multiband = three bands (low, mid, high) with their own parameters; Low band = two bands split at the low crossover (below: low parameters, above: mid parameters); High band = two bands split at the high crossover (below: mid parameters, above: high parameters);
  Single band = the whole signal with the mid-band parameters. The split uses 4th-order Linkwitz-Riley crossovers at **120 Hz and 2500 Hz** (`compressor.crossover_low/high` are our adjustable crossovers, 20 .. 1000 / 200 .. 16 000 Hz). The three outputs are summed (flat when no band is changed).
* **Detector and gain, per band and per channel pair** (all in power; `thr_u`, `thr_l` thresholds in dB, converted to power `10^(dB/10)`; `rho_u`, `rho_l` ratio knobs):

```
x2          = x^2
env_hi     = max( one-pole(env_hi, x2) , thr_u_power )      -- the upper (downward) envelope; attack when x2 > env_hi, else release
g_hi       = (thr_u_power / env_hi) ^ (rho_u / 2)           -- rho_u in 0..1: 0 = no compression, 1 = full limiting (the output stays at the threshold)
env_lo     = min( one-pole(env_lo, x2) , thr_l_power )      -- the lower (upward) envelope
g_lo       = (thr_l_power / env_lo) ^ (rho_l / 2)           -- rho_l in -1..1: > 0 raises quiet signals toward the threshold (upward compression); < 0 expands downward
gain       = clamp(g_hi g_lo, 0, 32)                        ;   y = gain x
```

  The one-pole of the envelope: `env = (x2 + env * N) / (N + 1)` with `N` samples: `N = base_ms * (sr_i/1000) * exp(8 c - 4)` where `c` = the attack or release knob (0 .. 1) and `base_ms` = attack 2.8 / 1.4 / 0.7 ms (low / mid / high band), release 40 / 28 / 15 ms; `N >= 5`.
  The rate `N` uses the attack value when `x2 > env` and the release value otherwise. The band-split compressors share the attack/release knob, thresholds and ratios are per band.
  Thresholds are clamped to [-100, 12] dB (-120 for the peak mode).
* **After the stage**: output gain `gain_dB` (-36 .. 36) per band (ramped), then the band sum; finally the **mix** `lerp(dry, wet, m)`. Measured (single band, mid band: upper threshold -20 dB, lower ratio 0, gain 0 dB, attack 0, release 50 %, mix 100):
  Sine input peak 0 dB: upper ratio 0 / 50 % / 100 % -> output peak 0 / -10.0 / -19.9 dB; input -10.4 dB: -10.4 / -15.2 / -19.9; input -20 dB and below: untouched. (Law: `out = in + rho (thr - in)` in dB for a steady level above the threshold; the sine peak is what is compared.)
* `compressor.limit`: Compress (above), Limit (our addition): the upper stage becomes a look-ahead brickwall limiter (1.5 ms look-ahead, ceiling = the upper threshold, makeup after the limiter, latency reported), Serum (peak detector, hard knee, smoothed gain reduction in dB with time constants 0.1 .. 1000 ms on
  the log scale of the knob, upper ratio as `1 - 1/R`). They are our additions defined by `docs/ui-design/13-new-keys.md` 4.17 / 4.48.

### 6.1 Further compressor vectors (C1 .. C2)

Single band, mid band parameters, upper threshold +12 dB (inactive), attack 0, release 50 %, mix 100, gain 0, a Sine at input peak levels -50.4 / -40 / -30.4 / -20 / -10.4 / 0 dB:

* **Upward compression** (C1), lower threshold -30 dB: lower ratio 0: output = input; 50 %: -40.2 / -35.0 / -30.2 / -20 / -10.4 / 0 (below the threshold `out = in + rho (thr - in)`: -50.4 -> -40.2); 100 %: -29.9 / -29.9 / -29.9 / -20 / -10.4 / 0 (everything below the threshold is lifted to it);
  lower ratio -50 % (**downward expansion**, C2): -60.7 / -45.0 / -30.7 / -20 / -10.4 / 0 (`-50.4 -> -60.6`). Above the lower threshold nothing changes.

## 7. Flanger (`flanger.*`)

A stereo feedback delay with a modulated short delay time.

```
phase_L = lfo - offset/2,   phase_R = lfo - offset/2 + offset         (offset = flanger.phase_offset in cycles, 0 .. 1)
tri(t)  = | 2 fract(t + 0.75) - 1 |                                    -- triangle, 0 .. 1 (period 1)
mod     = depth (2 tri(phase) - 1) + 1                                 -- depth = flanger.depth 0..1
d_c     = 1 / f_center,  f_center = frequency of the note flanger.center
delay   = (d_c - 0.0005) mod + 0.0005                                  -- seconds; read at 1/max(delay, 1/20000) Hz
y       = dry x + wet read,  stored = hardtanh(x + feedback read)      -- feedback -1 .. 1; no loop filter; wet/dry equal power with m = mix knob * 0.5
```

The delay line is the stereo delay of section 3 in its clamped, unfiltered mode (Catmull-Rom, up to 40 000 samples); the delay time is **not** glided (the modulation is per chunk, interpolated across it). `rate` Hz or sync (the key's range 0.04 .. 4 Hz).

## 8. Phaser (`phaser.*`)

The phaser filter of document 5 section 7 in its "clean" variant (tanh on the resonance feedback instead of the input hard-tanh), 12 all-pass stages, fed with a modulated cutoff:

```
tri_int(t)  = |2 fract(t) - 1|-style triangle in the integer phase domain of period 1, in [-1, 1] after the map v = 2 tri - 1
cutoff      = center_note + depth_semitones * v(phase_L / phase_R)       -- depth 0..48 st; the right channel's LFO phase is offset by +- phase_offset (cycles) in opposite directions; phase_offset smoothed across the chunk
feedback    = phaser.feedback 0..1; blend (poles) = phaser.blend: 0 -> 4 stages, 50 % -> 8, 100 % -> 12 (cross-fading the taps, document 5 sec 7)
depth2      = our "Depth 2" (-48 .. 48 semitones): the cutoffs of the 12 all-pass stages are spread evenly around the cutoff above (law below)
y           = lerp(dry, filter output, mix)                               -- linear crossfade
```

**Depth 2 (`phaser.depth2`).** Stage `s` (s = 0 .. 11, the first stage of the chain is 0) runs at the cutoff of the LFO-modulated centre multiplied by `2^(depth2 (s/11 - 0.5) / 12)`: the stages are spread **linearly in semitones over `[-depth2/2, +depth2/2]`** around the centre, stage 0 lowest for a positive value
(a negative value reverses the order). `depth2 = 0` puts every stage at the same cutoff (the behaviour of the plain phaser filter); the spread is applied on top of the `depth` LFO movement and of the filter's key/modulation offsets, per sample, and a stage frequency is limited to the Nyquist.
`blend` takes the output after the 4th, 8th or 12th stage, so with fewer active stages only the **lowest** stages of the spread take part (with a positive `depth2`). Because all-pass stages commute, the notch pattern of the full 12-stage chain does not depend on the sign of `depth2`.
Measured with the vectors of 8.1 (P2): the notches spread apart as the stage cutoffs separate; at `depth2 = 24` the lowest notches move down by about 12 % and the highest up by about 9 %.

Rate Hz or sync (`phaser.division`). Reset at the host position when synced. The phaser filter output is `(x + y_ap)/2` (so the mix of 100 % still contains the dry half: notches, not a pure all-pass).

## 9. Reverb (`reverb.*`)

Keys (`type` 0 "Vital" is sections 9.1 .. 9.3; types 1 .. 5 (Plate, Hall, Vintage, Nitrous, Basin) are the algorithms of 9.4, with `room`, `diff_a`, `diff_b`, `er_size` and `feedback`): `size` (0 .. 1), `decay` (stored as log2 seconds, 0.0156 .. 64), `predelay` (0 .. 0.3 s), `pre_low_cut`, `pre_high_cut` (notes), `low_shelf_cutoff/gain` (notes, -6 .. 0 dB), `high_shelf_cutoff/gain`, `chorus_amount` (0 .. 1, squared),
`chorus_rate` (log2 Hz, 0.0039 .. 8), `mix`, `width` (our addition, mid/side width of the wet).

### 9.1 Structure (a 16-line feedback delay network)

* **Input.** The stereo input (L, R) is band-limited (a one-pole low-pass at `pre_high_cut` subtracted from a one-pole low-pass at `pre_low_cut`, i.e. a **polarity-inverted** band-pass between the two notes) and scaled by 0.25, then added to all 16 lines, with the L input to the lines
  of the left half and the R input to the right half.
* **Lines.** Sixteen parallel loops, arranged as 4 groups of 4. Each loop: a **feedback delay** of `L_k = size_mult * D_k * (sr_i/44100)` samples (`size_mult = 2^(4 size - 3)`, i.e. 0.125 .. 2; `D_k` in samples at 44.1 kHz):

  | group | D (44.1 kHz samples) |
  |-------|----------------------|
  | 1 | 6753.2, 9278.4, 7704.5, 11328.5 |
  | 2 | 9701.12, 5512.5, 8480.45, 5638.65 |
  | 3 | 3120.73, 3429.5, 3626.37, 7713.52 |
  | 4 | 4521.54, 6518.97, 5265.56, 5630.25 |

  read with a cubic Lagrange interpolation, and **in series an all-pass diffuser** with the delays (44.1 kHz samples `A_k`, scaled by the buffer scale = the power of two >= sr_i/44100) and feedback gain 0.6:

  | group | A (samples) |
  |-------|-------------|
  | 1 | 1001, 799, 933, 876 |
  | 2 | 895, 807, 907, 853 |
  | 3 | 957, 1019, 711, 567 |
  | 4 | 833, 779, 663, 997 |

  The all-pass is the Schroeder form: input to the store `u = in + (fb_read - 0.6 allpass_read)`... precisely `w = line_read - 0.6 a_read`, stored `= scaled_input + w`, the all-pass output is `a_read + 0.6 w` (an all-pass of order A_k with coefficient 0.6).
* **Mixing.** The 16 all-pass outputs `v[g][j]` (group g, lane j) are mixed by an orthogonal feedback matrix: `y[g][j] = v[g][j] + 0.25 S - 0.5 C_j - 0.5 R_g` with `S` the total sum, `C_j` the sum over the groups of lane `j`, `R_g` the sum over the lanes of group `g` (eigenvalues +-1: lossless). Any 16 x 16 orthogonal matrix of similar density is acceptable.
* **Damping.** After the matrix, per line: a **high shelf** `y_hs = hp + A_h (y - hp)` with `hp = onepole_lowpass(y, high_cutoff)` and `A_h = 10^(high_gain/20)`, then a **low shelf** `y -= A_l' lp(y, low_cutoff)` with `A_l' = 1 - 10^(low_gain/20)` (gains clamped to [-24, 0] dB: they only attenuate).
* **Decay.** The signal of line k is multiplied by `g_k = 0.001^(L_k / (T 44100 ...))`: more exactly `g_k = 0.001 ^ (size_mult D_k / (T * 44100))` where `T` = `decay` seconds (clamped 0.1 .. 100): each pass through a line of length `l` seconds loses `60 l/T` dB. (The all-pass delay adds to the round trip without losing gain, so
  the effective RT60 is longer than `T`, most for small sizes; the measured table below is the reference.)
* **Output.** The stored lines (after the decay) are summed per side with the mixed outputs: `out = sum(write_k) + 0.125 sum(feed_forward_k g_k)` where `feed_forward` is the same mixing matrix applied to the stored values; left and right sides are the lane pairs. **The output taps the lines before the decay gain** (`write_k`, the mixed and shelved values, not the stored `g_k write_k`): the direct, diffused first pass of the input is always in the output at full level, so the **wet level does not depend on `decay` or `size`**
  (only on how long the tail then lasts; see 9.5 for the measured peak: -8.5 dB at decay 500 ms / size 100 %, -8.6 dB at 500 ms / 50 %, -8.8 dB at 2000 ms / 50 %). An implementation that applies the decay gain to the signal it sends to the output is quieter by the whole per-pass loss (for the long lines of size 100 % and decay 500 ms: `g_k` is -17 .. -62 dB per line) -- that is the level difference to avoid.
  The result goes into a stereo memory and is read back
  at the **pre-delay** (`predelay` seconds; the read position moves slowly toward its target, a few percent of the error per chunk with a smooth acceleration, so a change of the pre-delay bends the tail's pitch briefly) -- i.e. the pre-delay delays the whole wet signal. Final `out = wet * mem + dry * input`, `wet = sin(pi/2 m)`, `dry = cos(pi/2 m)`.
* **Chorus** (`chorus_amount`, `chorus_rate`): the read position of the lines is modulated by a complex rotating oscillator of rate `chorus_rate` (max 16 Hz): group 1 reads at `L + cos(phi) m`, group 2 at `L - cos(phi) m`, group 3 `L + sin(phi) m`, group 4 `L - sin(phi) m`, with `phi = 2 pi (rate t + j/16)` for lane `j` (0 .. 3) of the group,
  depth `m = chorus_amount (the stored value, squared knob) * 2500 samples (44.1k)`, limited to `L - 32` samples: it breaks up the metallic ringing of the loops.

### 9.2 Reference measurements (R1)

White-ish burst through the reverb (osc1 Saw, env short, mix 100 %, `high_shelf_gain 0`, `low_shelf_gain 0`, `pre_high_cut` 128, `pre_low_cut` 20, `chorus_amount 0`, `predelay 0`), RT60 from the backward integrated energy between -5 and -35 dB (seconds):

| decay param | size 20 % | size 50 % | size 100 % |
|-------------|-----------|-----------|------------|
| 500 ms | 1.81 | 0.90 | 0.47 |
| 1000 ms | 3.21 | 1.59 | 1.06 |
| 2000 ms | 6.87 | 2.80 | 2.14 |
| 4000 ms | 12.3 | 7.24 | 4.97 |

(tolerance +-25 %; the shape: size 100 % -> RT60 ~ `decay`, smaller sizes decay *longer* than set, see 9.1 decay). The first reflection builds up over ~20 ms; there is no discrete early reflection pattern (diffuse from the start).
Tail loudness: the wet level of a loud note at `mix 100`, size 50 %: about -29 dB (energy) relative to the dry burst. The default stereo width is the full width of the lanes (L-in feeds L lines).

**Better:** a 16-line FDN with a modulated delay network is standard; the own engine can use 8 or 12 lines at the 44.1k delay values scaled by 16/N for cost, if RT60 and density hold; run it at the host rate.

### 9.3 Further reverb vectors (R2)

Saw burst (20 ms, loud), `reverb.on 1`, `decay 2000`, `size 50`, `chorus_amount 0`, shelves 0 dB, mix 100; 10 ms energy windows, absolute dB (the dry burst peaks at -19.8 dB):
* **Pre-delay 0**: the wet signal rises with the burst at 0.12 s and peaks at **-38.1 dB** (about 18 dB below the dry burst); the tail at 0.2 / 0.5 / 1 / 2 / 3 / 4 s after the note on: -47.9 / -51.5 / -58.1 / -74.6 / -96.3 / -113.1 dB (about -17 dB per second between 1 and 3 s, RT60 about 3.5 s for the energy slope).
* **Pre-delay 100 ms**: the onset moves to 0.22 s (the silence before it is exact), the peak is -37.3 dB, the tail at 0.5 / 1 / 2 / 3 / 4 s: -49.6 / -64.0 / -77.5 / -93.7 / -110.6 dB.
* **High shelf -6 dB at note 90** (instead of 0 dB): the tail at 0.5 / 1 / 2 s: -59.1 / -71.2 / -96.6 dB (lower and faster decaying than the flat case: the high frequencies are cut each pass).
* `mix 50`: the dry burst reaches 0.812 of full scale (equal power -3 dB).

### 9.4 The other five reverbs (`reverb.type` 1 .. 5)

All five share the pipeline of the Vital reverb around them and differ only in the core:

* **Input.** The stereo input is band-limited by the same pre-filter pair (`pre_low_cut`, `pre_high_cut`: the polarity-inverted band-pass of 9.1) and summed to **mono: `0.5 (L + R)`**. The core is mono in, stereo out.
* **After the core.** The stereo result goes into the same wet memory (pre-delay glide of 9.1), then the optional `width` (mid/side, only when `reverb.width` is not 100 %), then the equal-power mix of the module (`wet = sin(pi/2 m)`, `dry = cos(pi/2 m)`). The shelves, size, decay, chorus controls are shared in meaning; there is no chorus law as in 9.1.
* **Controls used.** `size` 0 .. 1 -> `size_scale = 2^((size - 0.5) 3)` (0.354 .. 2.83; **not** the `2^(4 size - 3)` of type 0); `decay` seconds `T` (clamped 0.1 .. 100, and never below 0.05 inside the cores); `diff_a`, `diff_b` 0 .. 1 (input diffusion, loop diffusion); `er_size` 0 .. 1 -> `er_scale = 2^((er_size - 0.5) 3)`;
  `chorus_amount` (the stored 0 .. 1 value) is the modulation depth and `chorus_rate` (0 .. 16 Hz) the modulation rate; the damping shelves (below); `room` (Nitrous only); `feedback` (Nitrous and Basin only).
* **`reverb.feedback` (0 .. 100 %, default 50 %).** For **Nitrous and Basin only** the decay time used for the loop gains is `T' = T 2^((feedback - 0.5) 4)`: a quarter of `T` at 0 %, `T` at 50 % (so a stored patch is unchanged), four times `T` at 100 %. The other types, including type 0, ignore it.
  Measured at `decay` 2000 ms, `size` 50 %: RT60 0.51 / 2.01 / 8.06 s for feedback 0 / 50 / 100 % (Nitrous), 0.50 / 2.05 / 8.24 s (Basin); Tolerance +-15 % on the RT60.
* **Loop gain law (all cores).** A recirculating path whose total round-trip time is `l` seconds gets the gain `g = min(exp(-ln(1000) l / T'), 0.9996)`, i.e. exactly -60 dB after `T'` seconds of round trips, and a hard ceiling 0.9996 (so `T'` of 100 s cannot run away). `l` includes the all-pass delays in the loop.
* **Damping (all cores except the plate's variant noted).** A stage in each recirculating path: a trapezoidal (zero-delay) one-pole low-pass `lp` of the stage input (coefficient from `high_shelf_cutoff`) gives the high shelf `y = lp + A_h (x - lp)` with `A_h = 10^(high_shelf_gain/20)` (gain clamped to -24 .. 0 dB), followed by the low shelf `y - lpl (1 - 10^(low_shelf_gain/20))` with `lpl` the same kind of one-pole low-pass of `y` at `low_shelf_cutoff`. Nitrous and Basin brighten the shelf: Basin uses `A_h' = 1 - (1 - A_h) 0.35`; Nitrous `A_h' = 1 - (1 - A_h) k_room` with `k_room` Space 1.45, Marble 0.40, Rectangle 1.0, Hexagon 1.0, Box 0.80.
* **Early reflections (every type).** Six taps from the mono input: left at 7.3, 13.1, 19.7, 29.3, 37.9, 47.1 ms, right at 9.1, 15.7, 23.3, 31.9, 41.3, 53.3 ms (times multiplied by `er_scale`), gains `0.8, -0.7, 0.6, -0.5, 0.4, -0.35` on the left and the negated values on the right; the sum is multiplied by `0.5 er_level` and added to the core's output. `er_level`: Plate 0.30, Hall 0.45, Vintage 0.25, Basin 0.25, Nitrous by room Space 0.15, Marble 0.60, Rectangle 0.40, Hexagon 0.40, Box 0.45.

**Network cores: Hall, Nitrous, Basin (8 lines).**
Input: the mono input goes through four series all-passes with delays `i1 .. i4` ms and the coefficient `0.30 + 0.40 diff_a` (Hall, Nitrous) or `0.55 + 0.40 diff_a` (Basin) (all-pass: `z = read(delay)`, `v = x - c z` stored, output `z + c v`). Eight lines `k = 0 .. 7`: line `k` holds a delay of `D_k = clamp(base_k size_scale, 4 samples, 0.5 s)` seconds, read with a cubic (Catmull-Rom) interpolation at `D_k + depth sin(2 pi (ph + k/8))` samples,
`ph` a free-running phase at `chorus_rate`; the delay itself glides toward its target by 0.2 % of the error per sample (a size change bends the tail). Per sample: `y_k` = the line outputs; the left output `l = 0.5 sum_k a_k y_k`, the right `r = 0.5 sum_k b_k y_k` with the +-1 sign patterns
`a = (+ + - - + + - -)`, `b = (+ - + - - + - +)`; the feedback `m = H y / sqrt(8)` with `H` the 8 x 8 Walsh-Hadamard (Sylvester) matrix (orthonormal, lossless); the line input `v_k = m_k + c_k x` with `x` the diffused input and `c_k = sigma_k (-1)^k / sqrt(8)`, `sigma = (+ + + + - - - -)`;
then the line's all-pass (delay `ap_k`, coefficient `0.2 + 0.5 diff_b`), the damping stage, and the store `g_k` times the value, `g_k` by the loop gain law with `l_k = D_k + ap_k` seconds. Output `(l, r)` multiplied by `output_scale` (Hall 1.6, Nitrous 1.5, Basin 1.1), then the early reflections.
`depth` (samples) = `chorus_amount` times 1.2 ms (Hall), 0.6 ms (Nitrous), 2.4 ms (Basin).

| Core | line delays at size_scale 1 (ms) | line all-pass delays (ms) | input all-pass delays (ms) |
|------|----------------------------------|---------------------------|---------------------------|
| Hall | 41.7, 53.9, 61.3, 71.1, 79.7, 89.9, 101.3, 113.9 | 4.3, 5.1, 6.7, 7.9, 9.1, 10.9, 12.7, 14.3 | 5.3, 7.9, 11.3, 13.7 |
| Basin | 17.3, 21.1, 23.9, 27.7, 31.3, 34.9, 39.1, 43.7 | 2.3, 3.1, 3.7, 4.3, 5.3, 6.1, 7.1, 8.3 | 2.9, 4.1, 5.7, 7.3 |
| Nitrous | from the room, below | half the Hall values (2.15, 2.55, 3.35, 3.95, 4.55, 5.45, 6.35, 7.15) | the Hall values |

Nitrous derives the eight line lengths from a room of the dimensions `(a, b, c)` metres (`reverb.room`): the path lengths `2a, 2b, 2c, 2 sqrt(a^2+b^2), 2 sqrt(a^2+c^2), 2 sqrt(b^2+c^2), 2 sqrt(a^2+b^2+c^2), a+b+c` divided by 343 m/s, each multiplied by `1 + 0.0113 k` (so that a cube does not give equal lines).
Rooms: Space (24, 32, 18), Marble (6.3, 4.1, 3.0), Rectangle (9.0, 5.5, 3.5) (default), Hexagon (7.5, 6.5, 3.8), Box (5, 5, 5).

**Plate (Dattorro-style).** Reference rate 29 761 Hz, `scale = sr/29761 size_scale` (all delays below are in samples at the reference rate, multiplied by `scale`, at least 2): the input `0.5 x` through four all-passes of 142, 107, 379, 277 with the coefficients `0.45 + 0.40 diff_a` (first two), `0.35 + 0.40 diff_a` (last two). Two branches `b = 0, 1`, each fed by the diffused input plus the output of the **other** branch (one sample old):
a modulated all-pass (delay 672 / 908 plus `depth sin(2 pi (ph + b/2))` with `depth = chorus_amount 16 sr/29761`, coefficient `-(0.4 + 0.45 diff_b)`), a delay of 4453 / 4217, the damping stage times the decay gain, an all-pass of 1800 / 2656 (coefficient `0.75 (0.4 + 0.45 diff_b)`), a delay of 3720 / 3163 (its output is the branch output).
The decay gain per pass is `min(exp(-ln(1000) p / T), 0.9996)` with `p` = the mean over the two branches of the branch's round trip in seconds, `(672 + 4453 + 1800 + 3720)/29761 size_scale` (branch 0) and `(908 + 4217 + 2656 + 3163)/29761 size_scale` (branch 1) -- about 0.36 s at `size_scale` 1.
The stereo output is the sum of seven taps per side (positions in reference samples, times `scale`; the sign is the sign in the sum), left: +266 and +2974 of branch 0's first delay, -606.5 of its modulated all-pass, +1996 of its second delay, -1990 of branch 1's first delay, -187 of branch 1's second all-pass, -1066 of branch 1's second delay;
right: +353 and +3627 of branch 1's first delay, -1228 of its second all-pass, +2673 of its second delay, -2111 of branch 0's first delay, -335 of branch 0's second all-pass, -121 of branch 0's second delay. Multiplied by 0.75. (Dattorro's published output taps; any equivalent plate tank within the vectors of 9.5 is acceptable.)

**Vintage (Freeverb-like).** Per channel `c` (0 left, 1 right; the right channel's delays are +23 samples at 44.1 kHz), all delays scaled by `sr/44100 size_scale`: an input all-pass of 142 samples (coefficient `0.3 + 0.4 diff_a`), eight parallel feedback combs of 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 samples with the read position modulated by `chorus_amount 0.8 ms sin(2 pi (ph + k/8 + c/2))`;
each comb: `store = input + damping(read) g_k`, with `g_k` by the loop gain law with `l_k` = the comb delay (the sum of the comb outputs `read` -- the undamped values -- is what leaves); then four series all-passes of 556, 441, 341, 225 samples (coefficient `0.3 + 0.4 diff_b`), then a one-pole low-pass at 7 kHz, multiplied by 0.14. The output is darker than the other types by the 7 kHz low-pass.

### 9.5 Level and tail vectors of all six types (R3)

Setup as 9.3: Saw burst (osc1 level 100, env attack 0 / decay 20 ms / sustain 0, one note on at 0.1 s, released at 0.12 s), `reverb.on 1`, `mix 100`, shelves 0 dB, pre cuts 128 / 20, `chorus_amount 0`, `predelay 0`, `room` default, `diff_a` 70 %, `diff_b` 60 %, `er_size` 50 %, `feedback` 50 %. Values are the sum of squares of the left output in 10 ms windows,
in dB; the dry burst peaks at **+11.4 dB** (reverb off). Columns: the highest window (and its time), then the window 0.1 / 0.25 / 0.5 / 1 / 2 s after the note on, the RT60 from the backward-integrated energy between -5 and -35 dB (seconds; tolerance +-25 %).

| decay / size | type | peak (time) | +0.1 s | +0.25 s | +0.5 s | +1 s | +2 s | RT60 |
|--------------|------|-------------|--------|---------|--------|------|------|------|
| 500 ms / 100 % | 0 Vital | -8.5 (0.13 s) | -20.2 | -36.9 | -59.9 | -103 | -182 | 0.61 |
| | 1 Plate | -11.6 (0.27) | -17.9 | -18.7 | -19.6 | -43.2 | -84.3 | 1.28 |
| | 2 Hall | -11.8 (0.11) | -89.6 | -38.4 | -69.1 | -128 | -200 | 0.75 |
| | 3 Vintage | -13.4 (0.28) | -29.0 | -21.8 | -45.5 | -110 | -200 | 0.58 |
| | 4 Nitrous | -12.9 (0.11) | -21.7 | -39.3 | -65.5 | -132 | -200 | 0.59 |
| | 5 Basin | -16.9 (0.11) | -22.3 | -44.2 | -68.5 | -130 | -200 | 0.55 |
| 500 ms / 50 % | 0 | -8.6 (0.13) | -20.2 | -26.4 | -43.4 | -77.2 | -130 | 0.93 |
| | 1 | -8.7 (0.14) | -13.0 | -26.9 | -58.8 | -86.9 | -156 | 0.44 |
| | 2 | -11.8 (0.11) | -16.9 | -31.1 | -64.7 | -122 | -200 | 0.53 |
| | 3 | -11.6 (0.19) | -14.9 | -34.1 | -66.7 | -120 | -200 | 0.52 |
| | 4 | -12.8 (0.19) | -21.1 | -29.5 | -62.6 | -124 | -200 | 0.51 |
| | 5 | -12.6 (0.18) | -18.4 | -33.9 | -63.4 | -119 | -200 | 0.50 |
| 1000 ms / 100 % | 0 | -8.5 (0.13) | -20.2 | -32.9 | -43.8 | -64.3 | -115 | 1.19 |
| | 1 | -11.6 (0.27) | -17.9 | -18.7 | -19.6 | -43.2 | -82.6 | 1.28 |
| | 2 | -11.8 (0.11) | -89.6 | -26.5 | -42.5 | -72.3 | -131 | 1.05 |
| | 3 | -13.6 (0.28) | -29.0 | -22.0 | -31.2 | -65.0 | -126 | 0.99 |
| | 4 | -12.9 (0.11) | -17.8 | -27.4 | -38.1 | -75.0 | -131 | 1.00 |
| | 5 | -15.5 (0.18) | -17.5 | -29.5 | -41.2 | -72.0 | -130 | 1.00 |
| 2000 ms / 50 % | 0 | -8.8 (0.13) | -17.5 | -15.6 | -21.9 | -34.8 | -48.7 | 3.19 |
| | 1 | -8.7 (0.14) | -13.0 | -23.9 | -29.4 | -40.2 | -63.1 | 2.17 |
| | 2 | -10.5 (0.24) | -11.5 | -12.3 | -24.8 | -37.1 | -71.0 | 2.01 |
| | 3 | -9.9 (0.19) | -12.8 | -17.0 | -30.3 | -37.1 | -69.0 | 1.94 |
| | 4 | -7.6 (0.19) | -15.2 | -10.8 | -21.0 | -40.1 | -67.1 | 1.99 |
| | 5 | -7.2 (0.18) | -12.3 | -14.5 | -20.8 | -37.6 | -69.6 | 2.01 |

Reading the table for the two things that were reported as different in the own engine (decay 500 ms, size 100 %):

1. **Level.** In the Vital reverb (type 0) the highest 10 ms window is **-8.5 dB** at 500 ms / 100 %, the same as at 500 ms / 50 % (-8.6) and 2000 ms / 50 % (-8.8): it is the direct first pass (about 20 dB below the dry burst) and does not depend on decay or size, because the output is taken before the decay gain (9.1). A core that attenuates its output by the line gain is
   tens of dB too quiet at a short decay and a large size (the per-line gain at 500 ms / size 100 % is `0.001^(2 D_k/(0.5 * 44100))`, between -17 dB and -62 dB per pass). The six types differ in absolute level at equal settings (the peaks of types 1 .. 5 lie between 1.2 dB above and 8.4 dB below type 0); the numbers of the table are the reference, tolerance +-2 dB on the peak and +-4 dB on the later windows.
2. **Tail.** At 500 ms / 100 % the Vital reverb's tail falls from -20 dB (0.1 s after the note on) to -60 dB at 0.5 s and -103 dB at 1 s: **RT60 0.47 s (9.2) .. 0.61 s (this table: a different burst)**, about `T`. At size 50 % the same decay gives 0.93 s, at 20 % 1.8 s (9.2): the effective RT60 is `T` times a factor that grows as the lines get shorter, because the all-pass delays in the loop add round-trip time without loss (loop gain law of 9.1 with the all-pass included).
   For types 1 .. 5 the RT60 follows the loop gain law with the loop times of 9.4 and is 0.5 .. 0.75 s at 500 ms (types 2 .. 5) and 1.28 s for the plate, identical at 500 and 1000 ms: at size 100 % (`size_scale` 2.83) one pass through the tank takes about 1.0 s, longer than `T`, so the loop gain is practically zero and the tail is the single pass of the tank (the same two rows).
   If the own tail decays faster than these numbers, the usual cause is a loop gain computed from the line delay alone (without the all-pass delay in `l`), or a gain that is applied twice per round trip.

### 8.1 Phaser, chorus and flanger vectors (P1, CH1, F1)

* **P1 (phaser)**, white noise, `phaser.mix 100`, `rate 0.04`, `depth 0`, `center 1000 Hz`, `feedback 0`: notches at 416 Hz and 2414 Hz for `blend` 0 (4 stages; they are `0.4142 fc` and `2.4142 fc`: the pole of the stages sits at the centre); 199, 668, 1500, 4992 Hz for 50 % (8 stages); 135, 416, 768, 1301, 2414, 7447 Hz for 100 % (12 stages).
* **P2 (phaser, `depth2`)**, the P1 setup with `blend` 100 % (12 stages), white noise, `phaser.mix 100`, `feedback 0`, `depth 0`, `center 1000 Hz`; notch frequencies (Hz; the lowest notch is only resolved to +-6 Hz by the 5.9 Hz bins, the measurement is a 5-bin smoothed noise spectrum, notch depths -20 .. -55 dB):
  `depth2` 0: 135 / 416 / 768 / 1301 / 2414 / 7447; `depth2` +12 and -12 (identical): 129 / 410 / 762 / 1312 / 2455 / 7617; `depth2` +24: 123 / 387 / 750 / 1336 / 2572 / 8121. Tolerance +-3 % (+-8 Hz for the lowest; the own engine, which runs the chain at the host rate, is 5 .. 6 % low at the top notch: `synthyboy_fx_oracle phasernotches`).
  History (P3): engine-vital once measured 451 / 2625 Hz (4 stages), 141 / 451 / 838 / 1418 / 2625 / 8080 Hz (12 stages) here, every pole 8.8 % high: the idle phaser filter of the phaser effect was not reached by `ProcessorRouter::setSampleRate` and kept the default 44.1 kHz (48000 / 44100 = 1.0884). Fixed (`Phaser::setSampleRate`); the own engine had reproduced the 1.0884 as a "pole scale" and dropped it with the fix (QUESTIONS 10 is answered).
* **CH1 (chorus)**, Sine burst (osc1 level 100, env attack 0 / decay 5 ms / sustain 0, note 69 at 0.1 s), `chorus.depth 0`, `feedback 0`, `voices 1`: `mix 100`, `delay1 = delay2 = 5 ms`: the burst appears delayed by 5 ms (first window above 0.05 at 4.5 .. 5 ms) with the same peak as the dry burst (1.11 against 1.15); with 12 ms the delay is 11.5 .. 12 ms; `mix 50`: dry 0.81 and the delayed copy at -3 dB;
  `voices 2`, `delay1 5`, `delay2 15` (mix 100): two copies at 5 ms and 15 ms, each at half the single-voice level (0.556 / 0.277 .. 0.128 in the windows). White noise through the chorus at `mix 100` and depth 0 is spectrally flat (no comb: the dry is not in the output).
  History (CH2): engine-vital once showed the burst at 4.6 ms (5 ms setting) and 11.0 ms (12 ms setting): the idle delay lines of the chorus module were not reached by `ProcessorRouter::setSampleRate` and kept the default 44.1 kHz (every delay time 8 % short at 48 kHz, the loop filters 8 % high). Fixed (`ChorusModule::setSampleRate`); both engines meet the vectors (`synthyboy_fx_oracle chorus`, `test_fxextra.cpp` chorus_vectors_ch1). The own engine's chorus is 0.3 .. 0.5 dB louder than engine-vital at equal settings (its wet factor 0.53 against the 0.5 of the design was trimmed to the old engine-vital).
  Rate invariance of the whole effect section (P3 and CH2 were found by it): `test_fxextra.cpp` fx_rate_invariance_of_the_effect_modules renders a held saw (note 45) through every effect module at 44.1 and at 48 kHz and compares the gain of every harmonic of 110 Hz up to 6.9 kHz. Worst disagreement now: chorus 1.5 dB (24.8 dB before the fix; the loop with feedback 40 % sharpens the interpolation loss at the notches), flanger 0.30, delay 0.18, phaser 0.40 (13.6 before), EQ 0.01, distortion 0.01, compressor 0.05, FX filter 0.05, convolve 0.28, voice filter model Phaser 0.00 dB; the reverb forwards the rate but its RT60 differs by 18 % between the rates (upstream design, see the test), not changed. Frozen golden changed with these two fixes: the bit-exact hash of the busy Vital patch in `test_new_keys.cpp` / `test_state_v3.cpp` (0x199ba135a1fd977f -> 0x0ecaf6456c563d02; the segment RMS is the same to 9 digits).
* **F1 (flanger)**, Sine burst (osc1 level 100, phase 90, env attack 0 / decay 5 ms / sustain 0, note 69 at 0.1 s) and a held saw at note 45, `flanger.mix 100`, `depth 0`, `center 329.63`: `feedback 0`: the dry burst at -3 dB and one copy 3.03 ms later (146 samples at 48 kHz; peak of the copy 0.78 in engine-vital and 0.73 in the own engine, against 0.83 and 0.69 for the dry window), nothing at 6.06 ms; `feedback +-50 %`: a second copy at 6.06 ms at half the first (0.39 / 0.37, the sign follows the feedback); the RMS of the saw against the dry saw is 0.81 at feedback 0 and +50 %, 1.08 at -50 %.
  Both engines meet these vectors (`synthyboy_fx_oracle flanger`, `test_fxextra.cpp` flanger_vectors_f1). History: engine-vital once had two defects here, the delay style of the flanger (the internal `kClampedUnfiltered`) was clamped onto `kTapDelay` (no feedback, almost no wet part: RMS of the saw 0.321 at mix 100 against 0.490 dry) and the idle delay of the flanger module kept the default 44.1 kHz (every delay time 8 % short at 48 kHz). Both are fixed; the presets that use the flanger sound different from before.

## 10. Chain order law (one more time)

The order of modules in a rack is the permutation `fx.order`; two modules never share a position (a repeated choice is repaired: the lowest free module takes the position). Modules whose `*.rack` is not Main belong to the other racks of the structure of `08-effects.md`.
The **Direct out** destination bypasses the racks, the buses and their effects (it is added after the racks). `main.fx_bypass` / `busN.fx_bypass` mute the effects of a rack (the dry bus passes). Level trims come last in each module.

## 11. Latency and its compensation across the buses

**Who has latency.** Bode: the Hilbert filter delays by 64 frames of the chain rate (Bode's own dry path is delayed by the same amount, so its mix does not comb); at the default 2x oversampling that is **32 samples at the host rate**, at 1x 64. The compressor in Limit mode: the look-ahead of 1.5 ms,
**72 samples at 48 kHz** (measured, the same at 1x, 2x, 4x); with `compressor.limit_latency_comp` Off the **reported** latency is 0, **but the look-ahead delay is still in the signal** (a note's onset is 72 samples late): the key only decides whether the host is told (and so compensates). Everything else (Hyper/Dimension, Utility, Convolve, the three splitters -- their crossovers are phase compensated -- and all nine Vital effects) has zero latency.
The latency reported to the host is the chain's longest path, rounded **up** to host samples: `ceil(frames / oversample)`.

**How it is counted (configuration, not signal).** The latency of a rack is the **sum of the latencies of the modules that are On and in that rack** (instance 0 and pool slots that are on), in series; a splitter adds the **slowest of its band racks** (the bands wait for it, so they stay phase aligned when summed); a rack whose `fx_bypass` is set (Main, Bus 1, Bus 2) contributes 0. The number is computed from the switches and does **not** depend on whether signal is flowing,
so it does not jump with the music; switching a latency module on or off changes the reported latency (and the delays below) at once.

**Where the paths come together.** The paths are: the voices into Main; Bus 1 and Bus 2 (their sends, then their rack, then the bus level); the **Direct** output (no rack); and the destination choices `busN.destination`: Main (the bus is added to the Main bus before the Main rack), Direct (added at the end, after the Main rack, with the direct level), Other bus (the bus feeds the other bus's input; if both say "other", bus 2 goes to Main).
Let `LB1`, `LB2` be the latency of the output of each bus (a bus that feeds the other bus adds its latency to the other's: the first bus runs first), `LM_in` the largest `LB` of the buses that go to Main, `LM = LM_in + latency(Main rack)`, and `Ltot = max(LM, LB of the buses that go to Direct)`. The compensation delays, integer samples at the chain rate, are exactly:

* the voices into Main wait `LM_in` before the Main rack (they meet the late buses there);
* a bus that goes to Main waits `LM_in - LB`; a bus that goes to Direct waits `Ltot - LB`;
* after the Main rack the Main bus waits `Ltot - LM`;
* the Direct input waits `Ltot`;
* the second bus waits for what the first one sends into it (the first bus's latency) before the sum;
* every band of a splitter waits `slowest - own`.

The result is that **every path reaches the output delayed by the same `Ltot`**: the dry/direct signal and the processed signal stay in phase. A compensation delay is at most 255 samples at the chain rate (a longer one is clamped to 255 and that path is then early by the difference: e.g. two Bode modules and a limiter at 2x are 128 + 144 = 272 frames, and the Direct input, which waits the full `Ltot`, would be 17 frames short), and **a change of a delay clears its history**: switching a latency module on or off while sounding makes the aligned paths glitch once.

**Measured** (the offline probe; onset of a Sine note with attack 0, filter off, samples after the note on; "latency" is what the engine reports; plain onset = 2 samples):

| Setup | reported latency (host samples) | onset delay vs plain |
|-------|---------------------------------|----------------------|
| none | 0 | 0 |
| Bode on in Main | 32 | +32 |
| Bode on in Bus 1 (nothing sent), destination Main | 32 | +32 (the voices into Main are delayed to meet the bus) |
| Bode in Bus 1, destination Direct | 32 | +32 (the Main path waits) |
| Bode in Bus 1 with `bus1.fx_bypass`; or Bode in Main with `main.fx_bypass` | 0 | 0 |
| a filter routed to Direct out, Bode in Main / in Bus 1 | 32 | +32 (the Direct path waits) |
| Compressor limit mode | 72 | +72 |
| Compressor limit mode, `limit_latency_comp` Off | 0 | +72 |
| Bode in Main + limiter | 104 | |
| Bode in Bus 1 + limiter in Main (destination Main) | 104 | |
| Bode in Bus 1 (destination Other bus), limiter in Bus 2 | 104 | |
| 1x oversampling: Bode in Main | 64 | |

Tolerance: the reported number exactly; the onsets within 1 sample.

## 12. The effect instance pool (`fxi01` .. `fxi16`)

On top of the 16 modules of the chain (instance 0: `chorus.*`, `delay.*`, ... the keys of sections 2 to 9 and of the Serum modules) there are **16 pool slots**, each holding one more instance of an effect. A slot is a generic container: a type, an on switch, a rack, an order and 24 normalised parameters.

* **Keys per slot `NN` = 01 .. 16.** `fxiNN.type`: 0 None, then 1 Chorus, 2 Compressor, 3 Delay, 4 Distortion, 5 EQ, 6 FX Filter, 7 Flanger, 8 Phaser, 9 Reverb, 10 Hyper/Dimension, 11 Utility, 12 Bode, 13 Convolve (type `t` is module `t - 1` of the chain; **the splitters are not in the pool**).
  `fxiNN.on` (default Off), `fxiNN.rack` (Main, Bus 1, Bus 2; **never a splitter band rack**), `fxiNN.order` (0 .. 1023, default 1023), and `fxiNN.p01 .. p24`, each 0 .. 1.
* **Parameter map.** Parameter `p_k` (k = 1 .. 24) is the `k`-th key of the type's own key list in table order **without** the type's `.on` and `.rack` keys (the compressor has 24 such keys, the FX filter 20, the reverb 19). Its range, curve, unit, labels and default are those of that source key (so `fxi03.p01` of a Delay slot is the first Delay key after `delay.on`), the plain value is
  `plain = fromNormalised(source, norm)` with the source key's own mapping (including its curve and choice snapping), then clamped to the range the module accepts. Entries beyond the type's key count do nothing. **Choosing a type writes the type's normalised defaults into `p01 .. p24` (the unused entries 0); the engine itself does not reset the values on a type change**, and a slot whose values are all 0 sounds at the minimum of every key (including the level trim at -24 dB); measured: a Delay slot with all 24 values 0 reduces a note's peak from 0.125 to 0.049.
* **The module.** A slot of a type renders **exactly what instance 0 of that module renders** for the same key values (same code, same laws, sections 1 .. 9 and the Serum modules): stereo in, stereo out, the module's own `*.level` output trim applied (the FX filter has its own level), the non-finite protection (a NaN/Inf output is replaced by 0 and the module is cleared). Each instance has its own state (delay lines, LFO phases) and follows the host tempo and key track of the engine like instance 0;
  a note that sounded **before** the module was created does not retrigger it (the retrigger of Hyper and Bode counts only the notes from the module's first block on). Every instance of a Bode or limiter adds its latency (section 11): two Bode slots in Main report 64.
* **Order within a rack.** The items of a rack are processed in ascending **sort key**: a module of instance 0 at position `q` (0 .. 15) of `fx.order` has the key `64 q + 32`, a pool slot its `order` value (0 .. 1023). On a tie the module of instance 0 comes first, then the slots by number. So `order` 0 .. 31 puts a slot before the first module, `64 q + 33 .. 64 q + 95` between the modules at positions `q` and `q + 1`, and `order` 992 (the tie with the last module: the module first) up to the default 1023 after the last module; slots with the same order run in slot-number order.
  The rack of a slot is Main, Bus 1 or Bus 2 only (`rack` is clamped to 0 .. 2).
* **Bypass and on/off.** An Off slot is skipped (cost zero; the module is idle and its state is cleared the next time it is switched on). `main.fx_bypass` / `busN.fx_bypass` skip every item of that rack, the slots included. A type that the quality tier switches off (the tier's effect mask) is treated as Off.
  **Crossfade.** When the module of a slot changes (type change), or the slot is switched on or off, the output is a **linear 10 ms crossfade** (`sr_i x 0.01` samples, `t` rising linearly from 0 to 1): `out = from (1 - t) + to t`, where `from` is the old module's output (or the dry input when the slot was Off) and `to` the new one (or the dry input when it goes Off); both run on the same input during the fade.
  A change within 10 ms of the previous one drops the earlier outgoing module at once. Parameter changes inside a running module are not crossfaded (they are smoothed like instance 0's, sections 1 .. 9).
* **Modulation.** The 24 values are modulation destinations like any other `mod` key (the mod matrix adds in the normalised 0 .. 1 domain of `p_k` and the result is converted through the source key's mapping and clamped); the connections to a slot's destinations are dropped when its module is replaced and re-made for the new type.
* **Cost/scope.** A slot costs nothing while Off; the engine does not limit the number of slots of one type.

Vectors: a slot of a type, with the defaults of the type, switched on at the start and played with the same note as instance 0 (Saw note 60 at 0.1 s, 0.3 .. 0.7 s compared), renders **the same samples** for the deterministic effects: Bode and Distortion differ by at most 4e-8; Delay and Flanger by 1e-3 .. 2e-3 (their tempo/LFO state starts with the slot);
for the effects with a free-running modulation (Chorus: difference up to 0.31 against a 0.24 peak; Phaser 0.044; Reverb 0.053, all with the same peak level within 1 dB) the sample values differ because the LFO phase of a slot starts when the slot is created, not with the engine -- match slot against instance 0 by level and spectrum, not by sample, for these.
Latency vectors: Bode slot in Main: 32; two Bode slots in Main: 64; Bode slot + instance 0 Bode in Main: 64; a Bode slot in Bus 2: 32; a Compressor slot (defaults, no limiter): 0.

