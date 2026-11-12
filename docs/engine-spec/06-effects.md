# 06 Effects

Status: final (2026-11-12). Covers the nine effects of the Vital engine and the frame they sit in: chain order, on/off, the dry/wet law, the output trim,
the rate they run at, then chorus, compressor, delay, distortion, EQ, flanger, phaser, reverb (the FX filter is the filter bank of document 5 with its own page).
**Not in this document** (they exist as Vital-free libraries the own engine links): Hyper/Dimension, Utility, the three splitters (`core/fx-serum-a`), Bode, Convolve (`core/fx-serum-b`).
The rack/bus/pool structure (Main, Bus 1, Bus 2, the splitter band racks, the effect instance pool) is our design, described in `docs/ui-design/08-effects.md` and `18-FX-INSTANCES.md`; only the chain law that defines the sound is here.

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
* **Latency.** Bode and the compressor's limit mode add latency (reported to the host); everything else is zero latency.

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
depth2      = our "Depth 2": semitones of stage-to-stage cutoff spread
y           = lerp(dry, filter output, mix)                               -- linear crossfade
```

Rate Hz or sync (`phaser.division`). Reset at the host position when synced. The phaser filter output is `(x + y_ap)/2` (so the mix of 100 % still contains the dry half: notches, not a pure all-pass).

## 9. Reverb (`reverb.*`)

Keys (`type` 0 "Vital" is this section; types 1 .. 5 are our Serum-style algorithms): `size` (0 .. 1), `decay` (stored as log2 seconds, 0.0156 .. 64), `predelay` (0 .. 0.3 s), `pre_low_cut`, `pre_high_cut` (notes), `low_shelf_cutoff/gain` (notes, -6 .. 0 dB), `high_shelf_cutoff/gain`, `chorus_amount` (0 .. 1, squared),
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
* **Output.** The stored lines (after the decay) are summed per side with the mixed outputs: `out = sum(write_k) + 0.125 sum(feed_forward_k g_k)` where `feed_forward` is the same mixing matrix applied to the stored values; left and right sides are the lane pairs. The result goes into a stereo memory and is read back
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

## 10. Chain order law (one more time)

The order of modules in a rack is the permutation `fx.order`; two modules never share a position (a repeated choice is repaired: the lowest free module takes the position). Modules whose `*.rack` is not Main belong to the other racks of the structure of `08-effects.md`.
The **Direct out** destination bypasses the racks, the buses and their effects (it is added after the racks). `main.fx_bypass` / `busN.fx_bypass` mute the effects of a rack (the dry bus passes). Level trims come last in each module.
