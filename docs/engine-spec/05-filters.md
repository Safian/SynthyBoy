# 05 Filters, drive, keytrack, routing, oversampling

Status: final (2026-11-12). Covers the two voice filters and the effect-rack filter (`filter1.*`, `filter2.*`, `fxfilter.*`): common frame (cutoff
composition, keytrack, drive, blend, mix, level, clean mode, routing, series/parallel), the eight models of the Vital engine (`*.model` 0 .. 7:
Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser) and the oversampling / decimation of the whole engine.
The models 8 and up (`Multi SVF`, `Slope`, ..., the Serum 2 families) are **not** in this document: `core/filters-serum` implements them (see its `CATALOGUE.md`) and the own engine links it.
Which of the Serum families the product offers is a `filters-serum` matter. The `*.style` choices mean different things per model (section 2.1 for the SVF-based models, the model sections for the others).

Conventions: cutoff is a MIDI note number `n` (fractional); frequency `f = 8.1757989 * 2^(n/12)` Hz; `P` power scale, README. `sr_i` = the internal sample rate (document 6, section 1).

## 1. The frame around every model

### 1.1 Signal path of one filter (per voice, per channel)

```
x_in --> [clean: x * 10^(-24/20)] --> model --> [clean: * 10^(24/20)] --> mix with dry --> * level --> routing/sends
```

* **Clean mode** (`filterN.clean_mode`, Serum 2 behaviour): the model sees its input at -24 dB and the +24 dB follow after it (so the drive saturates less); not for Comb and Formant (no drive stage). The gains are ramped across the block.
* **Mix** (`filterN.mix`, 0 .. 1): `out = lerp(dry, wet, mix)` where `dry` is the filter's input (before the clean gain) and `wet` the model's output including the clean +24 dB. Ramped across the block.
* **Level** (`filterN.level`, -60 .. 12 dB): linear gain `10^(dB/20)`, `-59.9 dB` and below = 0; ramped across the block. It comes after the mix.
* `filterN.on` Off: the filter outputs 0, and sources routed to it go to the effects instead (doc 1 sec 1.1).
* The effect-rack filter (`fxfilter.*`) is the same model bank, one instance per stereo bus (no voices) with the cutoff in note units, no keytrack from the voice (its keytrack uses the current "last note" of the engine), and its own mix/level.

### 1.2 Cutoff composition

```
n_cut = base + keytrack * (n_voice - 60) + voice_control_cutoff + pan_offset + modulation
base       = 12 log2(cutoff_Hz / 8.1758)     -- the knob, 20 .. 20 000 Hz, clamped to the audible range
keytrack   = filterN.keytrack in [-1, 1] (percent / 100); n_voice includes glide and bend (the pitch before the oscillator's transpose and tune)
pan_offset = filterN.pan * 12 semitones, with opposite signs in the two channels: left -12 pan, right +12 pan  (so +100 % puts the left channel 12 st lower and the right 12 st higher)
```

* The sum is formed at audio rate (the cutoff is an audio-rate destination: modulations are summed per sample) and then smoothed with the 5 Hz one-pole of doc 1 sec 10.
* Each model derives `f = 8.1757989 * 2^(n/12)`, clamps it to `[1 Hz, 0.4535 sr_i]` (SVF, Analog, Dirty, Diode...: 1 Hz .. just below Nyquist; the Ladder to 20 kHz; the comb filter ranges differ, 4.7) and maps it to the model's own
  coefficient via a 2048-point lookup with cubic interpolation: the SVF/Digital uses `g = tan(pi min(f/sr_i, 0.499))`; the one-pole based models (Analog, Dirty, Ladder, Diode, Phaser, Comb) use `g = tan(min(0.499 pi, x/(1 + x)))` with `x = pi f/sr_i`
  (a warping that is close to `x` for small `x` and compresses toward Nyquist); an exact function is fine as long as the measured responses of section 10 hold.
* Within a block the cutoff is evaluated **per sample** (interpolated base + the per-sample modulation).

### 1.3 Drive

`filterN.drive` 0 .. 20 dB (the control accepts 0 .. 36): `d = 10^(dB/20)` linear. Each model uses it differently (below); the common idea: drive is input gain before the nonlinearity,
partly compensated (post gain `~1/sqrt(d)`) so that higher drive does not blow the level up. Drive is also a *resonance boost* in several models.

### 1.4 Blend and styles

`filterN.blend` (0 .. 100 %) is `b_raw = percent * 0.02` in 0 .. 2, `b = clamp(b_raw - 1, -1, 1)`. For the multimode models in their normal style (styles 0 and 1; the others in section 2.1):
`lowpass amount = -b` for `b < 0`, `highpass amount = b` for `b > 0`, `bandpass amount = sqrt(1 - b^2)`; output `= lp * LP + bp * BP + hp * HP`. So blend 0 = low-pass, 50 % = band-pass, 100 % = high-pass, the transitions
keep the power. The other styles use the blend differently (section 2.1).

### 1.4a Routing

`filter.routing`: Parallel (the two filters process their own inputs and add), Series 1 -> 2 (filter 2's input = filter 1's output + the sources routed to filter 2), Series 2 -> 1.
In series only the second filter's output (and its `destination`) goes out; the first filter's own `destination` is ignored (but its bus sends and tap stay).
A filter that is Off in series is bypassed in the chain: its input goes forward.
`filterN.destination` (Effects only, Direct out, None) routes the filter's output: the `env1` button off moves it to the "free" path (doc 1 sec 1.1). The bus sends are `bus_send * output` taps of each filter's output (after mix and level).

### 1.5 Oversampling and the decimator

* Everything from the oscillators to the end of the rack runs at `sr_i = N * sr` with `N = 2` (default of the `medium` and `high` tiers), `1` (`low` tier) or `4` (`quality.oversample_4x`). For host rates >= 88.2 kHz the factor
  is halved once per doubling of the rate (88.2 kHz and 96 kHz run at 1x for N = 2, 2x for N = 4; 176.4 / 192 kHz at 1x). So the filters, the effects and the oscillators all see the high rate.
* The result is decimated to the host rate by cascaded **IIR half-band** stages (polyphonic all-pass pair, Regalia structure): for N = 2 one stage; N = 4 two stages; the last stage is the "sharp" one.
  A stage takes two input samples `x_a` (earlier), `x_b` (later); each goes through its own cascade of first-order all-passes `y = z^-1 x + a (x - y_prev)` ... in the decimated domain (`y[n] = x[n-1] + a (x[n] - y[n-1])`), and the output is `0.5 (A(x_a) + B(x_b))`.
  Check of the branch assignment: the stage must have gain 1 at DC and 0 at the Nyquist of its input rate (swapping the branches gives the opposite). Coefficients (branch A, branch B): the soft stage (used for the stages before the last) **(0.167135, 0.742130) / (0.041355, 0.387893)**; the sharp stage **(0.093022, 0.312318, 0.548379, 0.737199, 0.872235, 0.975498) / (0.024388, 0.194030, 0.433856, 0.650125, 0.810419, 0.925980)**.
  The sharp stage has ~-60 dB at the half-band edge and a 0.45 passband; it adds a few samples of group delay that vary with frequency (a stage is not linear phase). **Better:** the own engine may use any alias-rejecting resampler with >= 80 dB rejection above 0.55 sr and
  <= 0.1 dB ripple below 0.45 sr; mono-sum compatibility with a linear-phase FIR is fine.
* Filters that are driven hard produce harmonics above the audible band; the oversampling is what keeps them from aliasing. At 1x (low tier) heavy drive aliases.

## 2. Digital (model 3, default) -- the clean TPT state variable filter

Topology: a Cytomic/Zavalishin-style zero-delay-feedback **state variable filter**, two integrators, with a hard-knee tanh limiter on the output.

```
g  = tan(pi min(f/sr_i, 0.499));      R = 1/Q;  Q = 0.5 + 15.5 r^3  (r = resonance in 0..1; Q from 0.5 to 16)
per sample:  v3 = u - ic2;  v1 = (ic1 + g v3)/(1 + g R + g^2);  v2 = ic2 + g v1;   ic1 = 2 v1 - ic1;  ic2 = 2 v2 - ic2      (trapezoidal integrators)
LP = v2,  BP = v1 (peak gain Q at the corner),  HP = u - R v1 - v2;   output = hp_a * HP + bp_a * BP + lp_a * LP    (the weights of 1.4)
u = d' * x,   d' = d/(2 r^3 + 1)   (drive compensated by resonance)
y = hardtanh(output) * gain/sqrt(d)
hardtanh(v) = v within +-0.66, beyond: 0.66 sgn + 0.34 tanh((|v| - 0.66)/0.34)   (a soft knee, never exceeds 1)
```

Styles (`filterN.style`): 0 = 12 dB (one SVF section); 1 = 24 dB (two sections in series; the first has the fixed Q = 1 (R = 1) in its integrator loop and its output goes through hardtanh before the second; the drive acts on the first);
2 = Notch/Pass swap; 3 = Dual notch/band; 4 = Band/Peak/Notch; 5 = Shelving. Styles 2 to 5 are defined in section 2.1.

The cutoff coefficients and the resonance are interpolated linearly across the block. The model is stable at all settings.

### 2.1 Styles 2 to 5 of the SVF-based models (Digital, Analog, Dirty, Ladder)

This section is the complete definition of what `filterN.style` and `filterN.blend` do for the styles 2 .. 5 of the four SVF-family models (the Diode, Formant, Comb, Phaser models have their own meaning, sections 6 to 9).
Everything below is measured black-box and derived from the structure; there is no further hidden term.

**The weight model.** One filter *section* delivers three outputs of the same input `u`: `LP`, `BP` and `HP = u - R BP - LP` (R = 1/Q of the true resonance, section 2). The section output is
`a_LP LP + a_BP BP + a_HP HP`, so the styles differ only in the three weights (and the drive, and the number of sections). With `b = clamp(blend_raw - 1, -1, 1)` (1.4), `t = (b + 1)/2` (0 .. 1) and `m(b) = sqrt(0.5 max(b,0)^2 + 0.5)`:

| style | name | `a_LP` | `a_BP` | `a_HP` | sections |
|-------|------|--------|--------|--------|----------|
| 0, 1 | 12 dB, 24 dB | `max(-b, 0)` | `sqrt(1 - b^2)` | `max(b, 0)` | 1 (style 0), 2 (style 1) |
| 2 | Notch / Pass swap | `min(1 - b, 1)` | 0 | `min(1 + b, 1)` | 2, both with these weights |
| 3 | Dual notch / band | section A: `t`; section B: 1 | 0 | section A: 1; section B: `t` | 2 different ones, see below |
| 4 | Band / Peak / Notch | `m (1 + p)` | `2 m (1 + p - b)` | `m (1 + p)` | 2, both with these weights; `p = min(b, 0)` |
| 5 | Shelving | `lerp(A, 1, clamp(b + 1, 0, 1))` | `R sqrt(A) lerp(1, sqrt(A), 1 - b^2)` | `lerp(1, A, clamp(b, 0, 1))` | 1 |

Reading the table: style 2 sweeps **low-pass (blend 0) -> notch (blend 50 %) -> high-pass (blend 100 %)** (`LP + HP = u - R BP` is the notch); style 3 sweeps **band-pass (blend 0) -> a mixed state (50 %) -> notch (100 %)**
(section A is a high-pass with a low-pass share `t`, section B the mirror image: a low-pass with a high-pass share `t`; at `t = 0` HP followed by LP is a band-pass, at `t = 1` the two notches in series are one deep notch at the cutoff); style 4 sweeps **band-pass (blend 0, weight 1.414) ->
peak (50 %: `LP + HP + 1.414 BP`, flat -6 dB with an up-bump of the resonance) -> notch (100 %)**. The weights are interpolated linearly across the block like the other filter settings. At the blend ends the weights are exactly those of the neighbouring plain shapes, so a modulated blend is click free.

**Structure per model** (what decides the number of sections):

* **Digital.** Style 0 and style 5: one section. Style 1, 2, 4: two sections: the first one (the "pre" section) has R = 1 in its integrator loop but the *weights above* (with the true R inside `HP`); its output is hardtanh-limited and is the input of the second section, which has the true R and its own hardtanh at the output. Style 3: the same two sections, but with the two different weight sets of the table (A first, then B); the drive acts at the input of section A, the limiter sits after A and after B.
  The consequence of "weights with the true R but a pre section with R = 1" is visible in the vectors: the deep stopband of the style 2 high-pass at resonance 0 is **not** a 24 dB/oct slope (it flattens at about -30 dB, table below): quirk, reproduce it by using exactly this structure.
* **Analog (Sallen-Key) and Dirty.** Style 0: one section; style 3: the dual pair; **every other style (1, 2, 4 and 5) runs the two-section 24 dB structure with the weights of its row** (so style 5, Shelving, is *not* a shelf in these models: it is the 24 dB structure with the plain `LP/BP/HP` weights of style 1; the vectors show it).
* **Ladder.** The weights are applied to the stage taps (below); style 0: 12 dB tap set, style 1: 24 dB tap set, styles 2 and 3 have their own tap sets, style 4 weights the 12 dB tap set, **style 5 is the same as style 0**.

**Drive law of the styles** (`d` = linear drive, `d_c` = the resonance-compensated drive of the model: Digital `d/(2 r^3 + 1)`, Analog `d/(2 r + 1)`, Dirty and Ladder: their own compensation, sections 4 and 5; `r` = the resonance knob 0 .. 1). The drive is blended between the raw drive and the compensated one with a weight `w` that follows the blend:
`d_eff = lerp(d, d_c, w)` with `w = 1` (styles 0, 1), `w = |b|` (style 2: no compensation at the notch), `w = min(1 - b, 1)` (style 4), and for style 3 `d_eff = lerp(d, d_c (2 - t), min(1 - b, 1))`. The post gain is `gain/sqrt(d)` (Digital, using the raw `d`); in the Analog model `1/sqrt((2r + 1) d_eff)`, which equals `1/sqrt(d)` in the normal styles and is lower by `sqrt(2r + 1)` where `d_eff` is the raw drive.
With drive 0 dB and resonance 0 (all the vectors below) the compensation is 1 and these laws do not show.

**Style 5, Shelving (Digital).** A single section without drive and without post gain (`d = 1`, gain 1). With the shelf gain `G` (dB, clamped to +-24; **in every voice filter and in the FX filter the gain is 0 dB**, so `A = 10^(G/20) = 1`):
the weights above with `A = 1` are `a_LP = a_HP = 1`, `a_BP = R`: the output is `LP + HP + R BP = u` exactly -- **style 5 is a bit-exact identity at 0 dB whatever the blend, cutoff and resonance** (measured: 0.0 dB at all eight frequencies, resonance 0 and 50 %).
For a gain `G != 0` (the Serum equalizer use; not reachable from a voice filter) the weights above give: at `b = -1` a **low shelf** (`a_LP = A`, `a_HP = 1`, `a_BP = R sqrt(A)`), at `b = 0` a **bell** (`a_LP = a_HP = 1`, `a_BP = R A`, i.e. `u + (A - 1) R BP`: gain `A` at the cutoff, the width set by the resonance),
at `b = +1` a **high shelf** (`a_LP = 1`, `a_HP = A`, `a_BP = R sqrt(A)`), crossfading in between; the cutoff is moved by `3 b log2(A)` semitones (down for a low shelf boost, up for a high shelf boost) so that the shelf corner stays near the audible edge of the gain.

**Ladder tap sets.** The ladder has the (drive-scaled, saturated) input `x` and the four stage outputs `s1 .. s4`; the output is `c_0 x + c_1 s1 + c_2 s2 + c_3 s3 + c_4 s4` times the post gain. Let `Lw = max(-b, 0)`, `Bw = sqrt(1 - b^2)`, `Hw = max(b, 0)`, `H = min(1 + b, 1)`, `L = min(1 - b, 1)`,
and `q = m(b)(1 + p)`, `band = 2 m(b)(1 + p - b)` with `m`, `p` of the table above:

| style | `c_0` | `c_1` | `c_2` | `c_3` | `c_4` |
|-------|-------|-------|-------|-------|-------|
| 0 (12 dB) | `Hw` | `Bw - 2 Hw` | `Lw - Bw + Hw` | 0 | 0 |
| 1 (24 dB; band weight `1 - |b|`, not `Bw`) | `Hw` | `-4 Hw` | `6 Hw - (1 - |b|)` | `-4 Hw + 2 (1 - |b|)` | `Lw + Hw - (1 - |b|)` |
| 2 | `H^2` | `-4 H^2` | `6 H^2 + 2 H L` | `-4 H^2 - 4 H L` | `H^2 + L^2 + 2 H L` |
| 3 | `H` | `-4 H` | `8 H + 4 L` | `-8 H - 8 L` | `4 H + 4 L` |
| 4 | `q` | `band - 2 q` | `2 q - band` | 0 | 0 |
| 5 | as style 0 | | | | |

(Rows 0, 1, 4 are the 12 dB (`LP = s2`, `BP = s1 - s2`, `HP = x - 2 s1 + s2`) or 24 dB (`LP = s4`, `BP = -s2 + 2 s3 - s4`, `HP = x - 4 s1 + 6 s2 - 4 s3 + s4`) tap sets weighted by the weights of the table; rows 2 and 3 are written out because their products of shares are not a weighted sum of the plain shapes.
Check: row 2 at `b = -1` is the 24 dB low-pass (only `c_4`), at `b = 1` the 24 dB high-pass; row 3 at `b = 1` is `(1, -4, 8, -8, 4)`: a deep notch made of the stage taps.)
Drive of the ladder: `d_L = d (1 + r')` for the normal blend, faded to `d` as the blend goes to high-pass: `d_L = d lerp(1 + r', 1, Hw)` with `r' = r` (style 0) or `sin(r)` (r in radians, styles 1 .. 5); style 3 uses the raw `d`; style 4 uses `lerp(d, d_L, min(1 - b, 1))`.
Post gain `1/sqrt((d - 1)/2 + 1)`; for style 1 `1/sqrt((d - 1)/4 + 1)`. The ladder's resonance law is `k = lerp(0.001, 4.1, r'')` with `r'' = r` (style 0) or `sin(pi/2 r)` (every other style): styles 1 .. 5 have a stronger low half of the resonance knob than style 0.

**Vectors** (noise source, cutoff 1 kHz, keytrack 0, drive 0, level 0 dB, mix 100 %; same method and columns as section 10: 125 Hz .. 16 kHz; narrow notches are smeared by the +-6 bin averaging, so a measured notch depth of -32 dB is the bin-limited value; tolerance +-1.5 dB, +-3 dB near a resonance peak):

| Model, style, resonance, blend | 125 | 250 | 500 | 1k | 2k | 4k | 8k | 16k |
|---|---|---|---|---|---|---|---|---|
| Digital, 2, r 0, 0 % | -0.1 | -0.3 | -1.0 | -6.0 | -25.1 | -48.6 | -73.1 | -99.7 |
| Digital, 2, r 0, 50 % | -0.7 | -1.7 | -5.1 | -31.9 | -5.0 | -1.5 | -0.8 | -0.4 |
| Digital, 2, r 0, 100 % | -30.3 | -30.6 | -19.0 | -4.1 | -0.9 | -0.6 | -0.6 | -0.4 |
| Digital, 2, r 50, 0 % | -1.8 | -1.1 | 1.2 | 5.7 | -22.9 | -49.5 | -74.8 | -101.5 |
| Digital, 2, r 50, 50 % | -0.3 | -0.6 | -1.6 | -23.8 | -1.6 | -0.5 | -0.3 | -0.3 |
| Digital, 2, r 50, 100 % | -28.9 | -29.8 | -19.1 | 6.6 | 1.2 | -1.3 | -2.0 | -2.1 |
| Digital, 3, r 0, 0 % | -9.4 | -9.1 | -4.3 | -1.4 | -9.6 | -21.2 | -33.7 | -46.6 |
| Digital, 3, r 0, 50 % | -3.5 | -4.6 | -5.8 | -10.3 | -11.3 | -5.4 | -4.5 | -3.8 |
| Digital, 3, r 0, 100 % | -0.7 | -1.7 | -5.1 | -31.9 | -5.0 | -1.5 | -0.8 | -0.4 |
| Digital, 3, r 50, 0 % | -12.3 | -10.0 | -3.2 | 10.8 | -6.4 | -20.9 | -33.8 | -47.4 |
| Digital, 3, r 50, 50 % | -4.5 | -4.6 | -4.7 | 0.5 | -8.7 | -5.5 | -5.1 | -5.0 |
| Digital, 3, r 50, 100 % | -0.3 | -0.6 | -1.6 | -23.8 | -1.6 | -0.5 | -0.3 | -0.3 |
| Digital, 4, r 0, 0 % | -30.5 | -17.9 | -7.0 | -0.0 | -7.0 | -18.4 | -30.6 | -43.8 |
| Digital, 4, r 0, 50 % | -6.0 | -6.0 | -6.0 | -6.0 | -6.0 | -6.0 | -6.0 | -6.0 |
| Digital, 4, r 0, 100 % | -0.7 | -1.7 | -5.1 | -31.9 | -5.0 | -1.5 | -0.8 | -0.4 |
| Digital, 4, r 50, 0 % | -30.3 | -18.6 | -4.7 | 11.8 | -4.9 | -19.2 | -32.2 | -45.8 |
| Digital, 4, r 50, 50 % | -7.7 | -5.7 | 0.4 | 13.7 | 0.3 | -5.7 | -7.6 | -8.1 |
| Digital, 4, r 50, 100 % | -0.3 | -0.6 | -1.6 | -23.8 | -1.6 | -0.5 | -0.3 | -0.3 |
| Digital, 5, r 0 and 50, any blend | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| Analog, 2, r 0, 0 % | -0.2 | -0.4 | -1.2 | -6.2 | -25.2 | -48.7 | -73.2 | -99.9 |
| Analog, 2, r 0, 50 % | -1.6 | -2.7 | -7.3 | -54.8 | -7.3 | -2.6 | -1.6 | -1.3 |
| Analog, 2, r 0, 100 % | -59.7 | -48.2 | -27.3 | -7.8 | -2.6 | -1.7 | -1.5 | -1.4 |
| Analog, 3, r 0, 0 % | -15.8 | -17.6 | -15.1 | -7.8 | -14.2 | -25.3 | -37.4 | -50.6 |
| Analog, 3, r 0, 50 % | -6.4 | -7.7 | -13.3 | -19.4 | -15.1 | -8.4 | -7.1 | -6.7 |
| Analog, 3, r 0, 100 % | -1.6 | -2.7 | -7.3 | -54.8 | -7.3 | -2.6 | -1.6 | -1.3 |
| Analog, 4, r 0, 0 % | -30.8 | -18.3 | -7.3 | -0.3 | -7.3 | -18.7 | -30.8 | -44.1 |
| Analog, 4, r 0, 50 % | -6.7 | -6.1 | -4.1 | -1.0 | -4.1 | -6.1 | -6.6 | -6.8 |
| Analog, 4, r 0, 100 % | -1.6 | -2.7 | -7.3 | -54.8 | -7.3 | -2.6 | -1.6 | -1.3 |
| Analog, 5, r 0, 50 % (= the 24 dB band-pass) | -36.7 | -24.1 | -13.2 | -6.2 | -13.2 | -24.5 | -36.7 | -50.0 |
| Dirty, 2, r 0, 0 % | 0.6 | -0.2 | -2.8 | -10.8 | -26.8 | -48.5 | -72.4 | -98.9 |
| Dirty, 2, r 0, 50 % | -2.1 | -4.0 | -10.6 | -19.9 | -10.3 | -3.6 | -2.0 | -1.4 |
| Dirty, 2, r 0, 100 % | -19.3 | -20.6 | -20.0 | -14.2 | -5.7 | -2.7 | -2.0 | -1.5 |
| Dirty, 3, r 0, 0 % | -30.1 | -18.2 | -9.1 | -5.1 | -9.1 | -18.6 | -30.3 | -43.4 |
| Dirty, 3, r 0, 50 % | -3.9 | -6.0 | -14.4 | -18.6 | -14.3 | -5.7 | -3.9 | -3.3 |
| Dirty, 3, r 0, 100 % | -2.1 | -4.0 | -10.6 | -19.9 | -10.3 | -3.6 | -2.0 | -1.4 |
| Dirty, 4, r 0, 0 % | -29.7 | -17.8 | -8.7 | -4.7 | -8.7 | -18.2 | -29.9 | -43.0 |
| Dirty, 4, r 0, 50 % | -6.0 | -5.8 | -5.7 | -5.5 | -5.7 | -5.9 | -5.9 | -5.9 |
| Dirty, 4, r 0, 100 % | -2.1 | -4.0 | -10.6 | -19.9 | -10.3 | -3.6 | -2.0 | -1.4 |
| Dirty, 5, r 0, 50 % (= the 24 dB band-pass) | -35.7 | -23.7 | -14.7 | -10.6 | -14.7 | -24.2 | -35.8 | -48.9 |
| Ladder, 2, r 0, 0 % | -1.6 | -2.4 | -5.3 | -13.4 | -29.4 | -50.9 | -74.8 | -101.3 |
| Ladder, 2, r 0, 50 % | -1.9 | -3.6 | -10.3 | -48.1 | -10.1 | -3.5 | -1.8 | -1.4 |
| Ladder, 2, r 0, 100 % | -69.5 | -51.2 | -29.3 | -13.3 | -5.2 | -2.4 | -1.5 | -1.3 |
| Ladder, 3, r 0, 0 % | -26.0 | -14.0 | -5.1 | -1.3 | -5.3 | -14.6 | -26.2 | -39.3 |
| Ladder, 3, r 0, 50 % | -2.4 | -6.6 | -11.8 | -1.3 | -12.6 | -6.3 | -2.3 | -1.5 |
| Ladder, 3, r 0, 100 % | -1.9 | -3.6 | -10.3 | -48.1 | -10.1 | -3.5 | -1.8 | -1.4 |
| Ladder, 4, r 0, 0 % | -16.9 | -10.7 | -6.2 | -4.3 | -6.3 | -11.0 | -16.8 | -23.3 |
| Ladder, 4, r 0, 50 % | -4.4 | -4.3 | -4.3 | -4.3 | -4.3 | -4.3 | -4.3 | -4.3 |
| Ladder, 4, r 0, 100 % | -1.6 | -2.4 | -5.9 | -33.4 | -5.7 | -2.4 | -1.5 | -1.4 |
| Ladder, 5, r 0, 50 % (= style 0, the 12 dB band-pass) | -20.0 | -13.7 | -9.2 | -7.3 | -9.3 | -14.0 | -19.8 | -26.4 |

Notes on the vectors: (1) At resonance 0 the notch of style 2 at 50 %, of style 3 at 100 % and of style 4 at 100 % is the same curve in the Digital, Analog, Dirty and Ladder (styles 2, 3) models (identical rows). (2) The Ladder 3 / 50 % row is a band-pass-like peak at 1 kHz (-1.3 dB) between two shoulders (-11.8 / -12.6 dB at 500 Hz / 2 kHz) that rise again outwards (-2.4 dB at 125 Hz): the dual tap set is a notch-with-bandpass mixture, not a band-pass.
(3) The style 4 / 50 % row of Digital is flat -6.0 dB at resonance 0 (a peak filter whose bump needs resonance): at resonance 50 % the bump is +13.7 dB at the cutoff.

## 3. Analog (model 0) -- the Sallen-Key style 12 / 24 dB

Two cascaded one-pole low-pass stages with a feedback from both stage states to the input (Sallen-Key), a tanh at the loop input; the multimode outputs are taken from the stage differences.
Parameters: resonance `rr = sqrt(r)` -> feedback `k = 2.15 rr + 1.1 * drive_fraction * r` (drive_fraction = drive_dB/36), tuned against frequency (`k / max(1, 2g * 0.09 + 0.97)`); input gain `d/(2 r + 1)`;
post gain `1/sqrt(d)` in the 12 / 24 dB styles (the compensation of the input gain cancels). The tanh sits inside the loop so the resonance saturates instead of growing. Styles: 12 dB (one section), 24 dB (an additional resonance-free pre-section with feedback `2g - g^2 - 1`), notch/pass swap, dual notch/band, band/peak/notch with the same blend maps as 2.
Self-oscillation at high resonance: yes (sine at the cutoff); the amplitude is limited by the tanh to about 1.

## 4. Dirty (model 1) -- the same family with more saturation

The Analog topology with: resonance range 0.1 .. 2.15 (`res = 0.1 + 2.05 * r_t`, `r_t = sqrt(r)` tuned `r_t / max(1, 2g*0.25 + 0.97)`), a stronger input drive `d_in = 2 (d - 1) + 1` (min 0.1), a loop normaliser `1.4/(res^2 + 1)`
and an extra stage pair (stages 3 and 4, one-pole stages with the algebraic saturator `v - 0.9 v^3/(v^2 + 3)`) in the feedback path; output `loop_input/1.4`. Character: gritty, grows louder and thicker with drive and resonance, resonance peak
never gets clean. The measured responses below are the reference.

## 5. Ladder (model 2) -- the four-pole saturating transistor ladder

Four cascaded one-pole low-pass stages, each with the algebraic saturator `sat(v) = v - 0.9 v^3/(v^2 + 3)` in the state, global feedback of the fourth stage back to the input with the usual half-sample correction
(`g1 = 1.66 g` weighting of the stage states: `fb = s4 + g1 s3 + g1^2 s2 + g1^3 s1`), input `tanh(d' x - k fb)`.
`k = 0.001 + 4.099 r_s + 5 * drive_fraction * r` with `r_s = r` (style 12 dB) or `sin(pi r/2)` (the other styles); pre gain `d' = d (1 + r)` (12 dB) or `d (1 + sin r)` (other styles), faded toward `d` as the blend goes to high-pass; post gain `1/sqrt((d-1)/2 + 1)` (12 dB) or `1/sqrt((d-1)/4 + 1)` (24 dB).
Outputs are linear combinations of the stage outputs: 24 dB low-pass = stage 4; 12 dB low-pass = stage 2; band-pass 12 = s1 - s2; band-pass 24 = -s2 + 2 s3 - s4; high-pass 24 = x - 4 s1 + 6 s2 - 4 s3 + s4; high-pass 12 = x - 2 s1 + s2; the notch / swap styles are
combinations with the weights of the code table (rows of the form `[1, -4, 6, -4, 1]`). The max cutoff is 20 kHz. Self-oscillates at high resonance.

## 6. Diode (model 4) -- the diode ladder with a high-pass in front

Four stages of the TB-303-style diode ladder (each stage averages with its neighbours: `s2 <- (s1 + s3)/2` etc., the algebraic saturator in each), feedback `k` from stage 4, input `tanh(0.5 (d' hp - k s4))` where `hp` is the input through a 2-pole high-pass
(the high-pass cutoff = `cutoff * 2^(-1 .. -9)` by blend in the 24 dB style, a fixed 20 Hz feedback high-pass in the loop). Resonance `k = 0.7 + 16.3 r^3`; drive `d' = (k/2 + 1) d`; post `1/sqrt(d)`. Style 12 dB: the high-pass ratio is fixed (2^-1) and `blend^2/4` is its amount;
otherwise the blend moves the high-pass cutoff. The output is stage 4: a 24 dB/oct low-pass whose resonance does not self-oscillate cleanly (it saturates).

## 7. Phaser (model 7) -- an all-pass-chain filter

Twelve first-order all-pass stages (`y = x - 2 lp(x)`, one-pole `lp` at the cutoff coefficient) in a chain of 4 + 4 + 4, with taps after stage 4 ("peak 1"), 8 ("peak 3") and 12 ("peak 5"):
`out_ap = p1 T4 + p3 T8 + p5 T12` with `p1 = clamp(1 - b_raw)`, `p5 = clamp(b_raw - 1)`, `p3 = 1 - p1 - p5` (b_raw = blend percent * 0.02: 0 -> 4 stages, 1 -> 8, 2 -> 12 stages: more stages = more notches).
Feedback: `fb = k * (lp_a(out_ap) - lp_b(lp_a(out_ap)))` (a band-pass of the all-pass output with corners at `cutoff/20` and `20 cutoff`; `k = r` in 0..1), not saturated; the stage input is `hardtanh(d (1 + r/2) x + inv * fb)`; `inv = +1` (style 0) or `-1` (style 1);
the filter output is `0.5 (x + inv * out_ap)`: notches appear where the all-pass chain has turned the phase by an odd multiple of 180 degrees (style 0). Measured with the phaser *effect* at its centre 1000 Hz, depth 0, feedback 0: blend 0 (4 stages) notches at 451 Hz and 2625 Hz; blend 50 % (8 stages) at 217, 727, 1629, 5420 Hz (and a weak one at 2631 Hz); blend 100 % (12 stages) at 141, 451, 838, 1418, 2625, 8080 Hz, each -25 .. -60 dB deep (measured on white noise; the first notch is therefore at roughly 0.45 times the centre). The spread (`stage_spread`, used by the Phaser *effect*, document 6) spreads the stage cutoffs over `+-spread/2` semitones.

## 8. Comb (model 6) -- the feedback comb / flanger filter

A delay line with a fractional delay (Catmull-Rom read) of `T = sr_i/f` samples (f from the cutoff; clamp `2 <= T <= buffer`), a filter in the loop and three feedback styles
(`style = feedback_style + 3 * filter_style`, `feedback_style` 0 comb, 1 positive flange, 2 negative flange; `filter_style` 0 low/high blend, 1 band spread):

* Comb (0): `fb = lerp(-1, 1, r)`, `fb := fb/sqrt(|fb| + 1e-5)`, input scale `1 - 0.5 fb^2`; `v = scale x + fb * delay(T)`; the filtered `v` (see below) is written (`hardtanh`) to the delay line and also the output.
* Positive flange (1): `fb = r`; the delay line holds `x/sqrt2 + hardtanh(fb * filtered(delay(T)))`; output `x/sqrt2 * scale + filtered`; scale `1/(1 + fb)`.
* Negative flange (2): as 1 with the sign of the feedback and of the output term inverted and the read at `T/2` (notches at odd multiples).
* The loop filter: `filter_style` 0: a low/high **blend** around the corner `n_cut + comb_offset` (`comb_offset` 0 .. 84 st, default 42): `blend` raw 0..2 -> low gain `clamp(2 - b_raw, 0, 1)`, high gain `clamp(b_raw, 0, 1)` into a one-pole split, then a second one-pole high-pass at `n_cut - 48 st`;
  `filter_style` 1: a **band** around `n_cut + comb_offset` whose width is `blend * 4` octaves (`+-` that around the centre), the two corner frequencies clamped to `[1 Hz, sr_i/2.1]`.
* The delay time follows the cutoff per sample (pitch-synchronous resonator: set the cutoff to a note and the comb rings at that pitch); keytrack applies.
* Latency: the comb's first echo arrives after T samples; there is no look-ahead.

## 9. Formant (model 5)

Four parallel band-pass SVFs (the Digital SVF in band-pass mode, blend 1) whose centre frequencies (MIDI notes), resonances and gains (dB) are the bilinear interpolation, over the XY pad `formant.x`, `formant.y`, of four vowels:
`(x,y)`: (0,0) = vowel 1, (1,0) = vowel 2, (0,1) = vowel 3, (1,1) = vowel 4. Style 0 uses the vowels A, I, U, O (corners in the order bottom-left, bottom-right, top-left, top-right); style 1 uses A, O, I, E.
The per-vowel data (formant 1 .. 4: gain dB, resonance share, centre note):

| Vowel | F1 | F2 | F3 | F4 |
|-------|----|----|----|----|
| A | -2, 0.66, 75.755 | -8, 0.75, 84.545 | -9, 1.0, 100.085 | -10, 1.0, 101.646 |
| E | 0, 0.66, 67.350 | -14, 0.75, 92.400 | -4, 1.0, 99.755 | -14, 1.0, 103.350 |
| I | 0, 0.80, 61.783 | -15, 0.75, 94.050 | -17, 1.0, 101.038 | -20, 1.0, 103.618 |
| O | -2, 0.70, 67.350 | -6, 0.75, 79.350 | -14, 1.0, 99.755 | -14, 1.0, 101.038 |
| U | 0, 0.70, 65.038 | -20, 0.75, 74.370 | -17, 1.0, 100.409 | -14, 1.0, 101.646 |

* The centre note of each formant is shifted by `formant.transpose` (-12 .. 12 st) and pulled toward the middle note 80 by `formant.spread` (`lerp(note, 80, spread)`, spread -1 .. 1: negative moves away).
* The resonance of each band: `r_band = formant.resonance (0.3 .. 1, default 0.85) * vowel resonance share`, mapped to Q with the SVF bounds **4 .. 30** (`Q = 4 + 26 r^3`); the band gain is the vowel gain in dB, the sum of the four bands is the output.
* The filter has no cutoff knob, no drive and no resonance knob of the filter (`filterN.cutoff`, `resonance` do nothing in this model; the measured responses are identical for resonance 0 and 50 %); `blend` is unused.
* Style 2 (the vocal-tract style) is **not implemented** in the Vital engine: the block does not write its output, the model is silent (measured: the filtered noise is 105 .. 143 dB below the unfiltered one at 125 Hz .. 16 kHz; what remains audible in the engine is only the other, unfiltered sources). There is therefore no reference behaviour for style 2.
  **Decision (answers the implementer's question on style 2): the own engine's vocal-tract style (vowels E, U, O, A at the corners, narrower resonators, a glottal tilt) is accepted as the definition of style 2.** Constraints so that it fits the family: the same XY pad (`formant.x`, `formant.y`: bilinear between the four corner vowels, in the corner order of style 0), the same `formant.transpose` / `formant.spread` / `formant.resonance` semantics as styles 0 and 1,
  no dependence on `cutoff`, `resonance`, `drive`, `blend` (as styles 0 and 1), the output level at the pad centre within +-3 dB of style 0 at the same setting (noise source), and the states cleared at a note start. The glottal tilt is a fixed spectral tilt of the source (a low-pass of about -6 dB/oct above 1 kHz is right for a tract model) and must not be steered by a key.

## 10. Filter responses (reference measurements)

Noise source (white, level 100), `filter1.cutoff 1000 Hz`, `keytrack 0`, `drive 0`, `blend 0` (low-pass), mix 100 %, level 0 dB; gain of the filtered noise relative to the same noise with the filter off, 8192-point Welch averages, bins +-6 around:

| Model, style, resonance | 125 Hz | 250 | 500 | 1 kHz | 2 kHz | 4 kHz | 8 kHz | 16 kHz |
|-------------------------|--------|-----|-----|-------|-------|-------|-------|--------|
| Analog, 12 dB, r 0 | -1.5 | -1.8 | -3.3 | -7.3 | -15.3 | -26.0 | -37.9 | -51.2 |
| Analog, 12 dB, r 50 | -6.5 | -6.0 | -4.6 | -2.1 | -16.7 | -30.2 | -42.9 | -56.4 |
| Analog, 24 dB, r 0 | -0.2 | -0.4 | -1.2 | -6.2 | -25.2 | -48.7 | -73.2 | -99.9 |
| Analog, 24 dB, r 50 | -6.1 | -5.4 | -3.3 | -0.5 | -27.2 | -53.8 | -79.1 | -105.9 |
| Dirty, 12 dB, r 0 | 0.4 | 0.0 | -1.2 | -5.1 | -13.2 | -24.0 | -36.1 | -49.4 |
| Dirty, 12 dB, r 50 | -4.8 | -4.3 | -2.7 | 1.5 | -14.8 | -28.6 | -41.3 | -54.8 |
| Dirty, 24 dB, r 0 | 0.6 | -0.2 | -2.8 | -10.8 | -26.8 | -48.5 | -72.4 | -98.9 |
| Dirty, 24 dB, r 50 | -4.5 | -4.5 | -4.3 | -3.8 | -28.2 | -52.7 | -77.5 | -104.2 |
| Ladder, 12 dB, r 0 | -1.5 | -1.9 | -3.3 | -7.4 | -15.4 | -26.1 | -38.1 | -51.4 |
| Ladder, 12 dB, r 50 | -6.6 | -5.5 | -2.2 | -1.4 | -13.1 | -24.0 | -35.9 | -49.2 |
| Ladder, 24 dB, r 0 | -1.6 | -2.4 | -5.3 | -13.4 | -29.4 | -50.9 | -74.8 | -101.3 |
| Ladder, 24 dB, r 50 | -8.8 | -8.1 | -5.9 | -5.4 | -27.2 | -49.0 | -72.8 | -99.4 |
| Digital, 12 dB, r 0 | -0.1 | -0.5 | -1.9 | -6.0 | -14.0 | -24.7 | -36.6 | -49.9 |
| Digital, 12 dB, r 50 | -1.8 | -1.4 | 0.3 | 5.7 | -11.8 | -25.6 | -38.3 | -51.8 |
| Digital, 24 dB, r 0 | -0.1 | -0.3 | -1.0 | -6.0 | -25.1 | -48.6 | -73.1 | -99.7 |
| Digital, 24 dB, r 50 | -1.8 | -1.1 | 1.2 | 5.7 | -22.9 | -49.5 | -74.8 | -101.5 |
| Diode (both styles), r 0 | 0.3 | -7.4 | -16.8 | -29.2 | -45.5 | -66.1 | -89.6 | -116.0 |
| Diode (both styles), r 50 | -1.4 | -2.6 | -11.5 | -25.2 | -41.8 | -62.5 | -85.9 | -112.3 |
| Formant, style 0 (r irrelevant) | -12.9 | -2.8 | 2.6 | 4.2 | -27.6 | -9.5 | -18.4 | -26.0 |
| Formant, style 1 | -13.1 | -3.3 | 4.5 | -15.7 | -22.1 | -7.6 | -16.5 | -24.2 |
| Comb, style 0, r 0 | -12.4 | -9.6 | 16.7 | -11.9 | -11.8 | -11.8 | -11.3 | -10.1 |
| Comb, style 0, r 50 | -1.2 | -0.3 | -0.1 | -0.0 | -0.1 | -0.5 | -1.7 | -5.1 |
| Phaser, style 0, r 0 | -1.1 | -5.3 | -10.8 | -0.1 | -11.1 | -5.1 | -1.1 | -0.3 |
| Phaser, style 1, r 0 | -7.0 | -1.6 | -0.5 | -26.7 | -0.5 | -1.7 | -6.7 | -13.0 |

(Noise measurement: the standard error of a bin average is about 0.3 dB; the Comb table has the delay line's own comb structure and is only meaningful for the notches/peaks it shows. The Diode's response is that of a 4-pole
low-pass at a **lower** corner than the knob (-29 dB at 1 kHz): the diode ladder's cutoff is on a different scale -- a quirk to reproduce: set the corner so that the numbers hold.)
The numbers above come from the default `filterN.style` choices 0 and 1 of each model and `filterN.resonance` 0 / 50 %; model 5 ignores resonance.

Tolerances for the acceptance test: +-1.5 dB for the models 0, 2, 3 (Analog, Ladder, Digital) at |gain| < 30 dB, +-3 dB for Dirty, Diode, Formant, Phaser, Comb and everywhere the resonance peak is within 6 dB of the cutoff, and "below -40 dB" for the deep stopband.

### 10.1 Further measured vectors (Digital model, 12 dB, cutoff 1 kHz, resonance 0, drive 0, noise source)

Same method and columns as the table above (125 Hz .. 16 kHz). Tolerance +-1 dB.

| # | Setup | Gain (dB) |
|---|-------|-----------|
| F1 | `blend` 0 / 50 / 100 % (LP / BP / HP) | LP: -0.1 -0.5 -1.9 -6.0 -14.0 -24.7 -36.6 -49.9; BP: -18.6 -12.4 -8.0 -6.0 -8.0 -12.6 -18.4 -25.0; HP: -27.8 -23.9 -14.5 -6.5 -2.2 -0.7 -0.4 -0.2 |
| F2 | `mix` 50 % | -0.1 -0.5 -1.9 -5.1 -7.0 -6.5 -6.1 -6.0 (the dry signal comes through at -6 dB) |
| F3 | `level` -6 dB | the F1-LP row minus 6 dB |
| F4 | `keytrack` 100 % at note 72 (12 st above the reference note 60) | -0.0 -0.1 -0.5 -1.9 -6.0 -14.0 -25.0 -37.9 (the corner doubled to 2 kHz); keytrack 0 at note 72: the unmodified row |
| F5 | `drive` 12 dB | 5.6 5.3 3.8 -0.2 -8.0 -18.7 -30.7 -44.2 (about +5.6 dB in the passband: the post gain only partly compensates); with `clean_mode` On: 5.9 5.5 4.1 -0.0 -8.0 -18.7 -30.6 -43.9 |
| F6 | `pan` +100 % | left (corner 12 st down, 500 Hz): -0.5 -2.0 -6.0 -14.0 -24.6 -36.3 -48.6 -61.9; right (12 st up, 2 kHz): -0.0 -0.1 -0.5 -1.9 -6.0 -14.0 -25.0 -37.9 |
| F7 | both filters low-pass 1 kHz, source routed to filter 1 only, `filter.routing` Series 1 to 2 | -0.2 -1.1 -3.9 -12.1 -27.9 -49.4 -73.3 -99.8 (two 12 dB stages); Series 2 to 1 or Parallel with the source only in filter 1: the single-filter row |

## 11. Self-oscillation and stability

* Digital: no self-oscillation (Q <= 16); stable for all g.
* Analog, Dirty, Ladder, Diode: the tanh/algebraic saturators in the loop limit the amplitude; at maximum resonance with a signal they ring at the cutoff with an amplitude ~1; a note-off does not stop them (the voice ends with the amplitude envelope).
* All states are cleared at a note start (a dead voice's filters) and when the model changes.
* A cutoff step is smoothed (5 Hz one-pole), so there are no clicks from the knob; modulation at audio rate is supported.

## 12. "Better"

* The original uses a table-lookup warped coefficient and a one-sample-delay approximation of the ladder feedback; a zero-delay-feedback solver (one Newton iteration) would be more accurate at high cutoffs and resonances and is allowed where the measured responses stay within tolerance.
* The decimator is not linear phase and the Digital filter's cutoff is clamped to ~0.4535 sr_i; both are fine.
* The effect-rack filter runs at the oversampled rate; a base-rate instance is enough.
