# 03 Envelopes and LFOs

Status: final (2026-11-12). Covers the six envelopes (`envN.*`), the ten LFOs (`lfoN.*`), the four random LFOs (`rlfoN.*`), the
per-note random source, the tempo-sync tables and the LFO shape tables. How their outputs are used (amount, power, bipolar, stereo, rise,
fall, delay of a slot) is document 4. Conventions as in the README (power scale `P`).

All modulator outputs live in **0 .. 1** (envelopes, LFOs, random LFOs, macros, velocity, ...). "Bipolar" behaviour is a property of the
modulation slot (document 4), never of the source. Every modulator exists **per voice** (an envelope, an LFO, a random LFO is instantiated
for each voice) except the macros; the LFOs and random LFOs can be switched to a shared "mono" instance (`lfoN.mono`, 3.7).

## 1. Envelopes

### 1.1 Parameters and units

| Key | Meaning | Control law |
|-----|---------|-------------|
| `envN.delay`, `.attack`, `.hold`, `.decay`, `.release` | stage times, ms (or beats with `envN.units` = Beats: `*_beats`) | a time is stored as `c` with `seconds = c^4`; ranges of `c`: delay and hold 0 .. 1.4142 (4 s), attack, decay, release 0 .. 2.3784 (32 s at the top of the control, the key's range 0 .. 10 000 ms stops at 10 s) |
| `envN.sustain` | level 0 .. 1 | linear |
| `envN.attack_curve`, `.decay_curve`, `.release_curve` | curve power -20 .. 20 (defaults 0, -2, -2) | enters `P(x, p)` |

* **A modulation of a time acts on `c`** (document 4): an amount `a` adds `a * 1.4142` or `a * 2.3784` to `c` before the fourth power, i.e. a
  modulation is perceptually logarithmic, and a time can be pushed beyond the knob's range by a modulator: the sum `c` is **not** clamped to
  the range of the control, only a negative `c` is treated as 0. Times below 1e-9 s are 1e-9 s.
* In Beats units the time in seconds is `(beats * 60 / bpm)` with the host tempo at the moment the value is written (the key stores beats;
  document 4 of `13-new-keys.md` 4.x). The envelope times are also multiplied per voice by the **voice control time factor** (doc 1 sec 11), default 1.

### 1.2 Stages and the value

State per voice: `idle (delay)`, `attack`, `hold`, `decay`, `release`, `kill`; a **position** `x` in [0,1] inside the stage and the output `v`.
Per chunk (control rate) or per sample (audio rate, below): `x += dt / T_stage` (clamped to 1); when `x` reaches exactly 1 the stage changes and `x` is reset to 0.

```
delay :  v = 0                                                       (only if envN.delay != 0 at the note on, else the note starts in attack)
attack:  v = P(x, -attack_curve)                                     (from 0, whatever v was: a retrigger restarts from 0, no continuation)
hold  :  v = 1                                                       (skipped when hold = 0)
decay :  v = 1 - (1 - S) * P(x, decay_curve)                         (S = sustain, read live: moving sustain moves v at once)
        x == 1 -> v = S and the envelope stays in decay forever (the sustain stage)
release: v = v0 * (1 - P(x, release_curve))                          (v0 = v at the instant of the note off, wherever the envelope was)
kill  :  v = v0 * (1 - x),  x += dt / 0.050                          (linear, 50 ms; the stage voices enter when they are stolen or the polyphony is lowered)
```

* A note-on sets the stage to attack (or delay) and `x = 0`, remembering the current `v` as `v0`. If `master.poly_legato` is on, the stage, `x`, `v`
  and `v0` are instead copied from the first held voice of the previous chunk (doc 1 sec 5).
* A note-off sets the stage to release, `x = 0`, `v0 = v`. A note-off during delay or attack therefore releases from the current value (no
  jump to sustain).
* The curve law: `P(x, p) = (e^{p x} - 1)/(e^p - 1)` with `p = -attack_curve` for the attack, `decay_curve` for the decay and `release_curve` for the release. Defaults:
  attack 0 (linear), decay -2 and release -2 (both start fast and flatten: the exponential-looking default of the product). A positive knob
  value in the attack gives a log-like (fast start) attack; negative an exponential (slow start) attack; in decay/release the sign convention
  is the opposite of the attack's, i.e. positive curve = slow start, fast end.
* All stage times are **linear in position**: a time of `T` means `x` advances at `1/T` per second whatever the curve; the curve only bends the output.
* Envelope 1 is also the **amplitude** of the voice (doc 1 sec 1), where its value is squared; envelopes 2..6 are free mod sources.
* Values are clamped to [0, 1].

### 1.3 Rate: audio vs control

Envelope 1 runs at audio rate. Envelopes 2..6 run at control rate (once per chunk of at most 128 samples at the host rate; stage transitions
land on chunk boundaries, so a very short stage is quantised to the chunk, up to 128 samples = 2.7 ms at 48 kHz) **unless a slot connects them to a destination
that is audio rate** (oscillator level, phase, transpose and tune; noise level; filter cutoff and the formant knobs; EQ cutoffs; distortion drive and cutoff;
phaser center), then they too run at audio rate. At audio rate the stage transitions are sample accurate, the sustain level and the curve powers
are interpolated linearly across the chunk, and the note-on position inside the chunk is honoured. **Better:** run every envelope sample accurately
(cost is small), or at least at 32-sample chunks.

### 1.4 `envN.legato_inverted`

Normally an envelope does not retrigger on a legato note-on (doc 1 sec 5); with this switch On it listens to the unconditional note stream
and retriggers on every note. It is per envelope.

### 1.5 Test vectors (E1, E2)

E1: Sine, `osc1.level 100`, `osc1.phase 90`, `osc1.random_phase 0`, filter off, `master.volume 0`, `env1.delay 0`, `attack 100`, `hold 50`, `decay 200`, `sustain 50`,
`release 300`, curves 0 / -2 / -2; note on at 0.05 s, note off at 0.8 s. The amplitude of the 440 Hz sine (peak of the waveform in a +-1.25 ms window) is the **square** of the
envelope: at +10 / 50 / 90 / 120 / 160 / 200 / 300 / 350 / 500 ms after the note on: 0.0097 / 0.256 / 0.821 / 0.998 / 0.930 / 0.619 / 0.309 / 0.253 / 0.250
(model `v^2` with the formulas above: 0.010 / 0.250 / 0.810 / 1.0 / 0.893 / 0.597 / 0.303 / 0.250 / 0.250; the difference is the +-1.25 ms window); in the release at +10 / 50 / 100 / 200 / 290 ms after the note off:
0.217 / 0.115 / 0.049 / 0.0057 / 0 (model 0.214 / 0.113 / 0.048 / 0.0055 / 0). Tolerance 6 % of the value plus 0.005.

E2: the voice is freed exactly at the end of the release: `activeVoices` goes 1 -> 0 between 0.8 + 0.3 s and one chunk later.

## 2. Tempo sync, rate and division tables (LFOs, random LFOs; also the effects)

The frequency of an LFO is chosen by `lfoN.sync` (the same for `rlfoN.sync`, with the effect LFOs using the first four):

| `sync` | Frequency |
|--------|-----------|
| 0 Free (Hz) | `lfoN.rate` Hz: stored as `log2(Hz)`, range `-7 .. 9` (0.0078 Hz .. 512 Hz) |
| 1 Tempo | `r(d) * bps` Hz, `bps = bpm/60` (beats per second), `r` from the table below |
| 2 Tempo dotted | `(2/3) r(d) bps` (a dotted note is 1.5 times longer, so the frequency is 2/3) |
| 3 Tempo triplet | `(3/2) r(d) bps` |
| 4 Key follow | the frequency of the note `n_voice + keytrack_transpose + keytrack_tune/100` (`keytrack_transpose` -60 .. 36 st, default -12), i.e. an audio-rate LFO that follows the keyboard (the note includes bend and glide) |

The division `d` (13 entries) gives `r = (0, 1/128, 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8, 16)` cycles per beat: Freeze, 32 bars, 16 bars, 8 bars, 4 bars,
2 bars, 1 bar, 1/2 note, 1/4 note (default for LFOs: 1 cycle per quarter note), 1/8, 1/16, 1/32, 1/64 (16 cycles per beat). "Freeze" gives frequency 0.
(`bps` is clamped to >= 0.) The tempo is the host tempo at the start of the chunk (the shell passes it; default 120 BPM).

The voice control may scale the rate of free and synced LFOs alike (`voicectl.scale.lfo`).

## 3. LFOs

### 3.1 Parameters

`lfoN.shape` (13 shapes, 3.2), `rate` / `sync` / `division` (section 2), `sync_type` (3.4), `phase` (0 .. 360 deg), `stereo` (-180 .. 180 deg), `fade_in` (0 .. 8 s),
`delay` (0 .. 4 s), `smooth` (Off/On) and `smooth_time` (1 .. 1000 ms, stored as `log2(seconds)`, -10 .. 4), `direction` (Forward, Reverse, Ping pong),
`swing` (0 .. 100 %), `mono`, `host` (host lock). Rate, phase, fade, delay, stereo, smooth time, swing are modulatable (per voice).

### 3.2 Shape tables

Each shape is a function `S(u)`, `u` in [0,1] (one cycle), value in [0, 1], stored in a table of 2048 points (plus guard points) and read with the
same **Catmull-Rom** cubic interpolation as the oscillator (position `u * 2048`, index clamped to 0..2047). (Resolution 2048 means a very steep edge ramps over 1/2048 of a cycle.)

| # | Name | `S(u)` (breakpoints; "smooth" = every segment uses the raised-cosine transition `s(t) = 0.5 - 0.5 cos(pi t)`) |
|---|------|------------------------------|
| 0 | Sine | 0 -> 1 at u = 0.5 -> 0, smooth: `S = 0.5 - 0.5 cos(2 pi u)` |
| 1 | Triangle | linear 0 -> 1 (u = 0.5) -> 0 |
| 2 | Saw Up | ramp 0 -> 1 over the cycle, then jump to 0 |
| 3 | Saw Down | ramp 1 -> 0 |
| 4 | Square | 1 for u < 0.5, 0 for u >= 0.5 |
| 5 | Pulse 25 % | 1 for u < 0.25, else 0 |
| 6 | Steps Up | 0 for u < 0.25, 1/3, 2/3, 1 (four steps of 1/4 cycle) |
| 7 | Steps Down | 1, 2/3, 1/3, 0 |
| 8 | Random Steps | eight fixed levels over eighths of a cycle: 0.2, 0.9, 0.5, 1.0, 0.1, 0.7, 0.35, 0.8 |
| 9 | Exp Up | `u^3` |
| 10 | Exp Down | `(1 - u)^3` |
| 11 | Half Sine | `sin(pi u)` |
| 12 | Custom | the user shape (`core/shapes` renders it: points with powers, smooth flag, loop flag; per-segment curve `P(t, power)` after the optional raised cosine) |

The general line shape: a polyline of up to 100 points `(x, y)`, each segment has a power; within a segment, `t = (u - x0)/(x1 - x0)`, optionally
`t = s(t)`, then `t = P(t, power)`, value `= y0 + t (y1 - y0)`; with `loop` the segment from the last point to the first wraps.

### 3.3 Phase, direction, swing

The running phase `u` of a voice is `offset + phase_knob/360 + lane` where `lane = +stereo/720` (left) or `-stereo/720` (right) (so `stereo = 180 deg`
puts the two channels half a cycle apart: +-0.25 cycle). Then:

1. **swing** (`sw` in [0,1], 0 = identity): the cycle is 4 pairs of eighths; inside every pair the boundary between the two halves moves from 1/2 to `1/2 + sw/4` of the pair
   (`sw = 2/3` gives triplet swing); at the pair boundaries the warped phase meets the straight one, so the cycle length is unchanged;
2. **direction**: Forward `u`; Reverse `1 - u`; Ping pong `1 - |2u - 1|` (the shape is read forth and back within one cycle, so the effective cycle is the same length);
3. the table is read at the result. (Direction and swing apply to the **shape position**, never to the running phase, so changing them does not jump the phase.)

### 3.4 Sync type (the retrigger / loop mode), `lfoN.sync_type`

`offset` (cycles) advances by `f * dt` each chunk (control rate) or sample (audio rate), starting from 0 after the delay; `f` = the frequency of section 2.

| # | Mode | Behaviour |
|---|------|-----------|
| 0 | Retrigger | at every note on (retrigger, not legato) `offset = 0`; runs and wraps (mod 1) |
| 1 | Free-run (sync) | at the note on `offset = fract(f * T)` where `T` is the engine's global time in seconds (the host song position when the transport is synced, otherwise the free running clock the shell supplies): all voices and notes share one phase reference. With `lfoN.host` On the offset is re-anchored to that clock **every chunk**, not just at the note on |
| 2 | Envelope | `offset` runs 0 -> 1 once and stays at 1 (a one-shot envelope: the end value holds). The shape position is `min(offset + phase_knob, 1)` (the knob is a start offset) |
| 3 | Sustain envelope | `offset` runs 0 -> 1 but while the key (or pedal) is held it stops at `phase_knob` (the sustain point, 0 .. 1) and continues to 1 after the release; the position is `offset` |
| 4 | Loop at point | runs; when `offset >= 1` it jumps back to `offset - 1 + phase_knob` (the loop start is the phase knob); the position is `min(offset, 1)` |
| 5 | Loop and hold | while the key is held, whenever `offset >= phase_knob` it subtracts `phase_knob` (the position loops over `[0, phase_knob)`, so the loop length is the knob); after the release it plays on to 1 and holds there |

* The **delay**: the offset does not advance (and the fade does not start) until `delay` seconds have elapsed after the note on; during it the LFO sits at its start value.
* **Fade-in**: `fade_amplitude` rises linearly from 0 to 1 over `fade_in` seconds (starting after the delay); `fade_in = 0` is off.
  Output (control rate path, smoothing off): `lerp(S(start phase), S(running phase), fade)`: the LFO **fades from its start value into the movement** (not from 0).
  At audio rate the output is `fade * S(...)` (from 0). (Hence an LFO connected to an audio-rate destination fades from zero, otherwise from its start value; keep both.)
* **Smooth** (`lfoN.smooth` On): a one-pole low-pass on the output, with half-life `0.2 * smooth_time` seconds: per step `y += (x - y)(1 - 2^{-dt/h})`, `h = 0.2 * smooth_time`
  (no smoothing when `h < 0.0002 s`); with smoothing on, the fade is not applied. Default: On, 2^-7.5 s = 5.5 ms (so the default LFO has its sharp edges rounded by a ~1 ms half life).
* Output = clamp(value, -1, 1) (the values are already 0..1).
* **Rate**: LFOs run at control rate unless connected to an audio-rate destination (then audio rate), like the envelopes (1.3).

### 3.5 Per-voice, stereo and the notes that restart them

An LFO is per voice. Left and right channel run the same oscillator with the `stereo` phase difference (the two channels of a voice are the two lanes; for destinations that have
only one value per voice the left lane is used). A retrigger LFO restarts on the voice's note-on (and on a retrigger). Free-run LFOs do not restart.

### 3.6 Host lock and the sync clock

The engine keeps a clock `T` in seconds; the shell sets it from the host transport (song position * 60/bpm) when the transport plays and `lfoN.host` is on; otherwise it counts
the audio time since the engine was prepared. Only the sync-type 1 LFOs and the synced random LFOs use it.

### 3.7 Mono LFOs (`lfoN.mono`)

With `mono` On and the sync type one of Envelope / Sustain envelope / Loop at point / Loop and hold, **one** instance serves all voices: it restarts only when the first of a group of
overlapping keys goes down (a note-on while no key and no pedal is down starts a new group), it is "held" while any key or the pedal is down, and individual voices'
note-ons and note-offs neither restart nor release it. In other modes `mono` has no effect.

### 3.8 Point modulation of a custom shape

A custom shape's points may be modulated by up to 64 sources (`core/shapes`); the shape is re-baked per voice (2048 points) when a source moved by more than 1/4096, at most a few shapes per
chunk (a budget; a table that waited 4 chunks is baked regardless). Voices without a baked table read the shared one. Not part of the baseline.

## 4. Random LFOs (`rlfoN.*`)

Style (`rlfoN.style`): Perlin, Sample and hold, Sine interpolate, Lorenz attractor, Rossler attractor. `rate`, `sync`, `division`, `sync_type` (Retrigger / Free-run), `stereo` (Off/On), keytrack
as in section 2. Output 0 .. 1.

* Perlin, Sine interpolate: two uniform random values `a, b` in [-1, 1) (`a` = last, `b` = next); every cycle (`offset` wraps at the rate) `a := b`, `b := new random`. Between them, with `t = offset`:
  Perlin: `y = 2 * lerp(a t, b (t - 1), t^2 (3 - 2 t))` (a gradient-noise segment: zero crossings at the segment ends); Sine interpolate: `y = a + (b - a)(0.5 - 0.5 cos(pi t))`.
  Output `0.5 y + 0.5`.
* Sample and hold: the output is `0.5 a + 0.5`, held for a whole cycle; at the wrap `a := b`, `b := new random` and the output steps to the new value (sample accurate at audio rate).
* Lorenz attractor: the classic system with sigma = 10, rho = 28, beta = 8/3, explicit Euler with step `dt = min(0.01, f * 0.5/sr)` per sample (so `rate` scales the speed; at `f >= 0.01*2*sr` it saturates), state initialised at a note start (retrigger mode) as
  `(r1, r2, 37.6 + r3)` with `r_i` uniform in [-1,1); output `x/40 + 0.5`.
* Rossler attractor (our addition): a = b = 0.2, c = 5.7, RK4 with `h = clamp(f * 6/sr, 1e-5, 0.05)`, start `x = -6 + 0.5 r`, `y = z = 0`; a state that leaves `|x|,|y| > 50`, `z < -10` or `z > 100` restarts; output `clamp(x/24 + 0.5, 0, 1)`.
* Mono vs stereo: with `stereo` Off both channels of a voice get the same random numbers (and the attractors the left lane's state); with On they are independent.
* Retrigger mode: at the note-on the phase restarts (offset = -rate*(sample offset inside the chunk), i.e. exactly at the note), and `a`, `b` get fresh values; Free-run (sync) mode follows the global clock `T` like the LFO (new randoms
  when `fract(f T)` passes from >= 0.5 to < 0.5 across a chunk: once per cycle).
* Smooth interpolation of the output within a chunk (audio rate): linear from the previous chunk's value to the new one.

## 5. Per-note random source ("Random per Note")

Uniform in [0, 1) drawn at each note-on, identical for both channels of the voice, constant for the voice's life. Seeded deterministically per engine (a render repeats).

## 6. Macros

Eight macros `macro.N` 0 .. 100 %, global, modulatable (they are destinations too: a macro can be modulated by another source; they are control rate; the knob value is used directly, the
engine applies no smoothing to the knob (the shell smooths automation)). Output 0 .. 1.

## 7. Test vectors (L1 .. L4)

All: Sine osc, level 100, filter off, `master.volume 0`, `env1.attack 0`, `env1.release 50`, `osc1.pan` base 0; slot 1: source `lfo1`, destination `osc1.pan`, amount +25 %
(so the pan control moves by `0.25 * 2 * S`, `S` the LFO value: the left channel gain is `sqrt2 sin(pi/4 (1 - 0.5 S))`, measured as the peak of the left channel in a +-2 ms window).

| # | Setup | Expectation (time since the note on: left channel peak) |
|---|-------|-----------------------------------------------------------|
| L1 | `lfo1.rate 4`, `lfo1.shape 0` (Sine), sync 0 | 0.12 s: 0.540, 0.17: 0.672, 0.22: 0.938, 0.27: 0.986, 0.32: 0.768, 0.37: 0.548, 0.42: 0.674, 0.47: 0.939 (period 0.25 s; the model `S = 0.5 - 0.5 cos(2 pi 4 t)` gives 0.543, 0.685, 0.945, 0.975, 0.742, 0.543, 0.685, 0.945); tolerance 0.03 |
| L2 | as L1 with `lfo1.phase 90` | 0.12: 0.735, 0.17: 0.975, 0.22: 0.952, 0.27: 0.695, 0.32: 0.543, 0.37: 0.745 (a quarter cycle ahead) |
| L3 | `lfo1.sync 1`, `lfo1.division 8` (1/4 note), host tempo 120 BPM, shape Sine | frequency 2 Hz: 0.12: 0.802, 0.22: 0.564, 0.32: 0.628, 0.42: 0.901, 0.52: 0.997 (model 0.800, 0.559, 0.633, 0.905, 0.994) |
| L4 | `lfo1.rate 4`, `lfo1.shape 4` (Square), `lfo1.smooth 0` | the LFO is 1 for 0 .. 125 ms (pan control +0.5, left peak 0.541), 0 for 125 .. 250 ms (left peak 1.000), repeating every 250 ms; the edges are within 2 ms of the multiples of 125 ms |

(Early readings below ~60 ms after a note on include the attack of the output filter; start comparing at 0.1 s.)
