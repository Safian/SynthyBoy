# 01 Voice management

Status: final (2026-11-12). Covers: what a voice is, note on and note off, polyphony, stealing, the lifetime of a voice, sustain and
sostenuto, legato and retrigger, portamento, pitch composition (bend, tune, transpose, per-note expression, MPE), velocity,
aftertouch and the other per-voice controllers, the level laws from the voice to the master output, unison (stacking, detune, blend,
stereo spread), parameter smoothing, and the "voice control" extension. Oscillator internals (wavetable reading, warps) are in
document 2; envelope and LFO shapes in 3; the modulation matrix in 4.

Parameter keys are ours (`docs/PARAMETERS.md`). Where a key is not named, it is not part of the baseline.

## 1. The voice and the signal path

A **voice** is one sounding note: three wavetable-class oscillators (each with its own unison stack), a noise/sample source, a sub
oscillator, two filters, six envelopes, ten LFOs, four random LFOs and the 64 modulation slots (their per-voice sources). Voices run
in parallel; their stereo outputs are summed into the effect chain (document 6). All per-voice values are evaluated per voice; the
global (mono) modulators (macros, global LFOs in mono mode) exist once.

The voice output, per sample and channel, is

```
voice = ( sum of the sources routed to the "effects" destination      -- see 1.1
        + filter outputs )                                            -- filters: document 5
        * A(t)^2
A(t)   = E1(t) * V * G                                                -- E1: envelope 1 (value 0..1), V: velocity law, G: voice amplitude
V      = 1 + (v - 1) * T                                              -- v: velocity 0..1, T = master.velocity_track / 100, may be negative
```

* **Envelope 1 acts squared.** A(t) is smoothed by a linear ramp across each chunk (the control-rate part V*G is ramped from the
  previous chunk's value to the new one over the chunk; E1 itself is audio rate) and then **squared**. A note with velocity v and
  tracking 100 % therefore has gain v^2; half velocity is -12.04 dB. The amplitude envelope's shape you hear is the square of the
  drawn curve (the drawn release curve at half position is -12 dB, not -6 dB).
* `T < 0` extrapolates: velocity 0.5 with T = -100 % gives V = 1.5, gain 2.25 (clipped at the output, see 8).
* G is the target of the "Voice Amplitude" modulation destination (default 1). It is only reachable through the matrix.
* Sources and filters keep their own level laws (section 8) before this multiplication.

### 1.1 Source routing

Each of the three oscillators, the noise and the sub has a **destination** (`oscN.destination`, `noise.destination`,
`sub.destination`): Filter 1, Filter 2, both filters, Effects (the filters are bypassed), Direct out, Filter blend, None. A source
routed to a filter whose `filterN.on` is 0 is added to the effects path instead (the switched-off filter is transparent). "Filter
blend" splits the source between the two filters with an equal-power law, `g1 = cos(b pi/2)`, `g2 = sin(b pi/2)`, `b` = the source's
`filter_blend` (0..1), ramped per chunk; a source whose blended filter is off passes its share to Effects. The bus sends
(`bus1_send`, `bus2_send`, linear gain 0..1 ramped per chunk) tap the source after its level and pan whatever its destination is.
Direct out bypasses the whole effect rack except what the rack puts behind (document 6). The `env1` button of a source (state
version 3) set to Off removes the amplitude multiplication for that source and routes it through a separate path that is multiplied
by V*G squared only (no envelope), and keeps the voice alive until the longest enabled envelope has ended; with every button on
(default) the baseline above holds exactly.

## 2. Note on: what happens before the voice allocator sees the note

1. Block splitting: the host block is cut at every event frame and at 128-frame boundaries, events are applied at the first sample of
   a chunk, so a note starts exactly at its sample. (Alternatives such as sub-block offsets are fine as long as the note begins at
   the same sample, +-0.)
2. `master.scale.*` (scale conform) moves the incoming key to the nearest key in the scale before anything else; the *moved* key is
   the note of the voice, the note-off finds the voice by the key that was started.
3. `master.voice.same_note`: **Steal (0)** sends a note-off for the same key and channel first, then the note-on (the old voice is
   released and a new one starts; two voices sound for the release time); **Stack (1)** starts a second voice on the same key (a
   later note-off releases all of them); **Ignore (2)** drops a note-on whose key and channel is already sounding.
4. Tuning: the key is mapped through the active tuning table to a fractional MIDI note (`13-new-keys.md` 4.7; the 12-TET
   table is `note = key`); a key the tuning does not map does not sound. The tuned note is fixed at note-on (live retuning adds a
   per-voice difference later).
5. Velocity is clamped to 0..1 (NaN = 0). A note-on with velocity 0 is the host's business (the engine treats it as a note-on).

## 3. Allocation and stealing

State per voice: key state in {triggering, held, sustained, released, dead}. A voice is *active* until it dies (section 4).

Parameters: `master.polyphony` P (1..32, modulatable per note; effectively clamped to 1..32 and by the tier cap), `master.voice_override`
(Kill 0 / Steal 1), `master.voice.steal_priority`, `master.legato`.

### 3.1 Picking the voice for a new note

1. If fewer than P voices are active, **or** the override mode is Kill and legato is off: take a free voice. A free voice that lives
   next to an active one in the same stereo pair is preferred (cache locality only; not audible). The pool has one spare voice so
   that Kill mode always finds a free voice.
2. Otherwise (pool exhausted) take an active voice by key state, in this order: first one in the released state, else first in
   the sustained state, else first held, else first triggering. "First" means first in the **priority order** (3.3).
3. Kill mode with P reached: the new voice starts at once from the free voice, and the overflow is resolved at the start of the next chunk
   (3.2). Steal mode: the picked voice is **reassigned immediately**: the same voice index gets the new note, its
   envelopes restart from zero (the amplitude envelope goes 0 within the chunk: a hard cut, can click; use Kill for clean voice
   stealing). The voice is not "dead", so its oscillator phases, filter state and smoothers continue (section 5): only the envelopes, LFOs
   and random sources retrigger (unless legato applies). The voice does not pass through a fade.

### 3.2 Over-polyphony (Kill mode, and after P is lowered)

At the start of every chunk: `excess = active voices - P`, minus the voices already in the kill fade. For each excess voice one is
chosen: first the first released voice, else the first sustained, else the first held (in priority order), and **killed**: its envelopes
jump to their kill stage, which fades the envelope value to zero **linearly in 50 ms** (the audible fade is the square of that
because of section 1: `gain = (1 - t/50ms)^2` if the voice was at full level). A killed voice dies when the fade ends. Lowering P
while playing therefore fades the excess voices out in 50 ms rather than cutting.

### 3.3 Priority order (`master.voice.steal_priority`)

The setting names the kind of voice that is **kept** (Serum 2 convention); the order is of the *stolen-first* list:

| Setting | Stolen first | Rule |
|---------|--------------|------|
| 0 Newest | the voice that started earliest | insertion order (no sort) |
| 1 Oldest | the voice that started last | descending note count |
| 2 Highest | the lowest note | ascending note number gets stolen first |
| 3 Lowest | the highest note | descending note number |
| 4 Velocity | the lowest velocity | velocity ascending (resolution 1/1024) |

The list is re-sorted after every note on and note off. The order of voices with an equal key is not defined; do not depend on it.

### 3.4 Note off, and recall of pressed notes

The engine keeps the list of **pressed** notes (key+channel, in the order pressed; pressing a key again moves it to the end).
On note-off the key leaves the list, then every voice with that key and channel is handled:

* sustain pedal of that channel down: the voice goes to *sustained* (stays in its sustain/decay stage; the lift velocity is stored);
* otherwise, if the number of *still pressed* notes is >= P (all voices were busy and a pressed note is without a voice), the
  releasing voice is **reused** for the next pressed note that has no voice: Kill mode first fades the released voice (kill) and takes a
  fresh voice; Steal mode retakes the same voice. Which note is recalled depends on the priority setting: Newest recalls the most
  recently pressed unplayed note; Highest the highest, Lowest the lowest; the others the earliest. The recalled note keeps the
  velocity of the note that released and its own channel. The voice is placed at the front of the priority list for Newest,
  else at the end.
* otherwise: the voice is released (envelopes go to their release stage), the lift velocity is stored.

This gives the usual mono behaviour with P = 1: hold 60, play 64, release 64 -> 60 sounds again (with portamento if set).

### 3.5 Test vectors (section 12): V1 .. V4.

## 4. Lifetime of a voice

* **Release**: a note-off sends every envelope of the voice to the release stage. Envelope 1 reaches exactly zero at the end of the
  release time (the curve is built to end at 0); the voice is **dead** when, over a whole chunk, the amplitude path is exactly zero
  (threshold 1e-16 in float) *and* the voice is in the released or kill state. So a voice lives for exactly the longest release it
  has (envelope 1; with an `env1`-Off source also the longest of envelopes 2..4). A voice whose release is 0 ms dies in the first
  chunk after the off.
* A dead voice returns to the free list; its filter and oscillator state are reset when it is reused (the `reset` trigger, section 5).
* **Sustain pedal** (per channel, `SustainPedal` event, value >= 0.5 = down): a note-off while the pedal is down marks the voice
  sustained; pedal-up releases every sustained voice of the channel (those not held by sostenuto). `channel < 0` addresses all
  channels. The pedal state is *per channel*; notes started while the pedal is down are held normally and become sustained at their note-off.
* **Sostenuto** (CC 66): pressing it marks the voices that are active at that moment (of the channel); only those are kept at note-off
  until it is released. Not exposed as an event in the baseline interface.
* **All notes off** releases normally (no sustain exception); **all sounds off / reset** kills every voice at once, clears the
  pressed list, the last-note memory, and the note counter of the voice control; filters/oscillators are cleared.
* `activeVoices()` counts voices not yet dead, including releasing and killed ones.

## 5. Retrigger, legato, phase reset

Every note-on creates the events *voice on*, *note*, *last note*, *velocity*, *channel* for the voice and increments counters (note
count since start, number of pressed notes at that moment). Which consumers restart depends on the setting:

* **reset** (restart oscillator phases, clear filter state, jump smoothers to their targets): fired when the chosen voice was **dead**
  (a brand-new voice from the free pool, which in Kill mode is the normal case). A voice taken over while still sounding (Steal mode,
  the recall of 3.4) is not reset in any mode, so its phases and filter state continue.
* **retrigger** (restart envelopes, LFOs set to "Retrigger", random LFOs/sample&hold restart, the noise phase, per-note random sources):
  fired on every note-on **except** when `master.legato` is on and the voice is taken over from the *held* state (a note played while
  a key is down in mono/steal situations). With legato on and a free voice available, each note still gets its own voice and
  retriggers normally. Legato only changes what happens to a voice that is *stolen from a held note*: with P = 1 and legato on, the
  second note continues the same envelope (no attack), the oscillator keeps running and glides if portamento is on.
* Per envelope, `envN.legato_inverted` = On makes that one envelope retrigger even on legato note-ons (it listens to the unconditional
  stream).
* `master.poly_legato` (state version 3): a new voice's envelopes do not start from zero but **continue from the state of a held voice**
  (stage, position, value) as recorded in the previous chunk; it makes chords played over a held note continue instead of re-attack. The held
  voice used is the first voice (in voice order) in attack, hold or decay.
* `voice.phase_legato` and `oscN.phase_legato` / `phase_memory` (document 2): on a legato note-on the unison phases follow the voice
  that rendered last.
* A mono "group" restart (mono LFOs restart when the first of a group of overlapping keys goes down) is kept by the shell, document 3.

Retrigger styles of LFOs (free, retrigger, sync to playhead) are in document 3.

## 6. Pitch

### 6.1 Composition

The pitch of a voice for oscillator N, in MIDI notes, is the sum (all inputs fractional, updated once per chunk and **linearly interpolated
per sample inside the chunk**):

```
note_voice = glide(last_note -> note)                 -- 6.2, the tuned note of the key
           + wheel_global * pitch_bend_range         -- the zoned wheel; fixed to 0 by the shell (it uses the per channel path below)
           + local_bend                              -- 6.3: channel bend in semitones + per-note bend + live-tuning delta
           + voice_tune                              -- master.tune / 100 semitones (+-1), modulatable
           + voice_transpose                         -- master.transpose, integer semitones, modulatable
note_osc   = note_voice + fold(osc mapping) + oscN.transpose + oscN.tune/100 + coarse term of the pitch mode
```

and the frequency is `8.1757989156 * 2^(note_osc/12)` Hz. Tuning (`master.tuning.*`, `master.scale.*`) changes the *tuned note* at note-on only. A4 (`master.tuning.a4`) rescales the reference. Transposition and tune are summed in the
note domain, so they compose exactly (+12 and +1200 ct are identical). `oscN.pitch_track` Off replaces the note by `fixed_pitch` (default
note 48, bend still added). Transpose snap and the pitch modes are in document 2.

### 6.2 Portamento (glide)

Keys: `master.portamento.time` (ms, 0..8000; internally seconds on a log2 axis, minimum 2^-10 s), `.slope` (-8..8), `.always` (force),
`.scaled`.

* If the time is <= 1 ms the glide is off (bypass): the note is the new note immediately.
* At every note-on (event "voice on") the glide position `x` is reset to 0 and the glide runs from `source` = the *previous played
  note* (of any voice; the engine remembers the last tuned note, initial value: the new note itself) to `target` = the new note. `x`
  advances by `chunk_length / (time * sr)` per chunk and is clamped to 1.
* Unless `.always` is on, the glide only runs when the note is played while at least one other note is pressed: if the note is the
  only pressed note (pressed count == 1 at its activation) `x` is forced to 1 (no glide). With `.always` the glide runs for
  every note, from the last note played even if released.
* With `.scaled` the time is multiplied by `|target - source| / 12` (the time is for an octave).
* The pitch is `note = source + (target - source) * P(x, -slope)` where `P` is the power scale of the conventions: slope > 0 gives a
  fast start and slow end (the glide "lands softly"), slope < 0 the opposite. Slope 0 is linear in semitones (so linear in log-frequency,
  constant "speed" in pitch).
* Because the position advances per chunk, the glide is a staircase of at most 128-sample steps; the oscillator linearly
  interpolates the note across the chunk so it is continuous.
* The glide does not restart at note-off; a voice's release keeps its final pitch.
* `master.portamento.time` can be modulated (per voice); the position increment uses the value at each chunk, so changing the
  time mid-glide changes the speed from then on (no jump).

### 6.3 Pitch bend, MPE, per-note expression

* `PitchBend` event: value -1..1, **per channel**, scaled to semitones: `bend * range` where `range` = `master.bend_range`
  (0..24 st) for positive values and, if `master.bend_down_link` is off, `master.bend_down_range` for negative values. The bend
  is stored per channel and **copied into every voice of that channel that is in the held state** (not into releasing or sustained voices:
  a note whose key was released keeps the bend it had, a quirk; do the same unless you choose to let sustained voices follow) and into
  every new note on that channel from the stored channel value.
* The mod wheel (CC 1) is stored per channel and read by every voice of that channel at every chunk (live). Aftertouch (channel
  pressure) is stored per channel, copied into the *held* voices of the channel when it changes, and a new note starts with the stored
  value of its channel.
* MPE (`master.mpe.on`): the manager channel (0) bend moves every note by `bend * master.bend_range`; a member channel adds
  `bend * master.mpe.bend_range` (default 48 st) to its own notes. Pressure per channel becomes "Poly Aftertouch"; CC 74 / the
  `Brightness` expression becomes "Slide".
* Per-note expressions (`NoteExpression`): `Tuning` = semitones added to that note's pitch (relative to the key), `Pressure` 0..1 =
  poly aftertouch source, `Brightness` 0..1 = slide, `Pan` 0..1 (0.5 centre) = the X axis of the MPE macros. `key < 0` addresses
  every note of the channel.
* Live retuning (MTS-ESP) adds `target_note - tuned_note_at_start` to the pitch of the sounding voices.
* The voice pitch is limited only by the oscillator's frequency clamp (document 2); there is no hard clamp on bend.

## 7. Velocity, aftertouch and the other per-voice values

* `velocity` source: the note-on velocity 0..1 held for the voice's life. `lift` (release velocity): 0.5 until the note-off supplies one
  (the default when the controller sends none); it is only readable from the release on.
* `aftertouch` source: 0..1 channel pressure, per voice (see 6.3); `poly_aftertouch`: per note pressure; `slide`: per note or channel Y.
* `mod_wheel` (0..1) and `pitch_wheel` (`0.5 + 0.5 * bend`, bend = the raw -1..1 wheel value of the voice's channel, independent of the bend
  range): the controller of the voice's channel, read every chunk.
* `note` source = `note_voice / 127` (including bend and portamento); `note_in_octave` = fractional octave position of the note, 0..1;
  `note_count`, `last_note`, `channel`, `random` (per voice, new at every retrigger), `stereo` (±1 alternating between the two channels).
  Details and ranges of every source: document 4.
* A note-on's initial aftertouch/slide are the stored channel values; per-note ones start at 0.

## 8. Level laws from the oscillator to the master

All gains are applied to the sample values; "dB" is 20 log10.

| Stage | Law | Smoothing |
|-------|-----|-----------|
| Oscillator level | gain = `oscN.level / 100` (linear amplitude fraction; the knob's 50 % is -6.02 dB); the DSP actually multiplies the *square* of a control that is the square root of the level (a historical detail: the net law is linear), and the modulation adds in the square-root domain (document 4) | audio rate one-pole 5 Hz on the control |
| Oscillator pan | constant power, `L = sqrt2 sin(pi/4 (1 - p))`, `R = sqrt2 sin(pi/4 (1 + p))`, p in [-1,1]; p = 0 gives 1.0 in both channels (0 dB), p = ±1 gives +3.01 dB in one channel and silence in the other | ramped across each chunk |
| Noise, sub levels | same as oscillator level (`noise.level`, `sub.level`), pan as above | same |
| Amplitude A^2 | section 1 | ramp per chunk |
| Master volume `master.volume` | dB in -60..6; **-60 dB and below is silence** (not -inf approach: -59 dB is a real -59 dB); linear gain `10^(dB/20)` | per-chunk ramp of the gain |
| Master stereo spread | mid/side width of the finished stereo mix `L' = M + w S, R' = M - w S` with `M = (L+R)/2`, `S = (L-R)/2`, w in [0,1] (default 1 = untouched) | none |
| Stereo routing | `master.stereo_routing` r in 0..1 (default 1): `phi = (1 - r) pi/4`, `L' = L cos(phi) + R sin(phi)`, `R' = R cos(phi) + L sin(phi)`; r = 1 untouched, r = 0 both channels = 0.7071 (L + R) (mono sum at -3 dB). Applied after the effects and the decimator, before the master volume | per-chunk linear ramp of cos and sin |
| Output limiter | hard clamp of each sample to ±2.1 (+6.4 dBFS); no soft limiter | none |

The master volume sits **after** the effects. Measured: Sine, filter off, level 100, velocity 1, volume 0 dB -> peak amplitude 1.000
(0.01 dB).

## 9. Unison (a property of an oscillator, evaluated per voice)

Keys: `oscN.unison.voices` U (1..16; capped by the tier `maxUnison`), `.detune` D (0..100), `.range` R (0..48 st, default 2),
`.detune_curve` c (-5..5, default 1.5), `.mode` (Linear, Super, Exp, Inv, Random), `.blend` b (0..1, default 0.8), `.blend_shape` s
(-1..1), `.spread` w (0..1, default 1), `.stack` (+ `.stack_repeat`), `.law` (Vital 0 / Serum 1), `oscN.frame_spread`,
`oscN.warp.spread`, `oscN.morph.spread`, the per-unison phases.

### 9.1 Layout of the voices

The stack always runs in **pairs**: the number of internal voices is `U` rounded **up** to an even number. Pair `i` (i = 0 ..
ceil(U/2)-1) has a left-channel voice and a right-channel voice. For odd U the first pair is a **single centre voice**: the same phase
and detune ratio in both channels (so it comes out identical in L and R; its phase is the right voice's start phase on both). For even
U every pair is detuned.

### 9.2 Detune law

`cents_max = R * D` (cents, e.g. R = 2 and D = 25 % -> 50 ct; D = 100 % with R = 2 -> 200 ct = 2 semitones: measured).

For pair i, with `bump = 1 if U is even else 0` and `divisor = max(1, U - 1)`:

```
t = (2 i + bump) / divisor          t in [0, 1]; the outermost pair has t = 1
```

then the **mode** reshapes t: Linear: t; Super: t^3; Exp: `(e^(2t) - 1)/(e^2 - 1)`; Inv: `1 - (1-t)^3`; Random: a fixed pseudo-random value in [0,1]
that depends on i only (the same every note). Then the curve: `u = P(t, c)` (power scale). `cents_i = u * cents_max`. The pair's
ratios are `r = 2^(cents_i/1200)` (up) and `1/r` (down). Sharpness alternates between pairs: **in pair 0 the left voice is flat (1/r) and
the right voice sharp (r); in pair 1 the left is sharp and the right flat, pair 2 as pair 0, ...** (a centre voice has ratio 1).
Measured (Sine, D = 100, R = 2, c = 0, `unison.blend` 100):

| U | left channel (ct) | right channel (ct) |
|---|-------------------|--------------------|
| 2 | -200 | +200 |
| 3 | 0, +200 | -200, 0 |
| 4 | -67, +200 | -200, +67 |
| 5 | -200, 0, +100 | -100, 0, +200 |
| 7 | -133, +67, +200, ... | see vector V8 |

With c = 1.5 and U = 5: the middle pair is at 64.2 ct instead of 100 (`P(0.5, 1.5) = 0.3208`).

### 9.3 Stack styles (`oscN.unison.stack`)

The stack multiplies the pair's ratio by a style table indexed by the **pair index** (0-based, repeated cyclically where the table is
shorter, 8 entries) *in addition* to the detune ratio:

| Style | multipliers by pair index |
|-------|---------------------------|
| Unison (0) | 1 1 1 1 1 1 1 1 |
| Center Drop 12 | 0.5 1 1 1 ... (first pair an octave down) |
| Center Drop 24 | 0.25 1 1 1 ... |
| Octaves | 1 2 1 2 1 2 1 2 |
| 2 Octaves | 1 2 3 1 2 3 1 2 (as ratios 1x, 2x, 3x) |
| Power Chord | 1 1.49831 2 1 1.49831 2 1 1.49831 |
| Power Chord x2 | 1 1.49831 2 2.99661 4 1 1.49831 2 |
| Major | 1 1.25992 1.49831 2 1 1.25992 1.49831 2 |
| Minor | 1 1.18921 1.49831 2 1 1.18921 1.49831 2 |
| Harmonic series | 1 2 3 4 5 6 7 8 |
| Odd harmonics | 1 3 5 7 9 11 13 15 |

(The ratios are fixed numbers: 1.49830707688 = 2^(7/12) rounded, 1.25992104989 = 2^(4/12), 1.189207115 = 2^(3/12).) Both voices of a
pair share the multiplier. `stack_repeat` (1x/2x/3x) applies to Octaves and Power Chord only: Octaves -> `2^(i mod (n+1))` with
n = repeat + 1 octaves, Power Chord -> step `s = i mod (2n+1)`, `2^(s/2 floor) * (1.4983 if s odd else 1)`.

### 9.4 Blend law (Vital law, `oscN.unison.law` = 0) and amplitudes

Amplitudes are set per pair class: *centre pair* (index 0; the single centre voice for odd U) with amplitude `a_c` and *all other pairs*
with `a_d`. For U <= 2: `a_c = 1`, `a_d = 0` (no blend; a pair of two is one pair). For U >= 3:

```
c0  = 1 + (0.4 - 1) * b            = 1 - 0.6 b
d0  = 0.6 * (1 - (1-b)^2)
c1  = c0 * (1 - s/2),  d1 = d0 * (1 + s/2)         -- s = blend_shape
n   = ceil(U/2)                                    -- number of pairs
norm = 1 / sqrt(c1^2 + (n-1) d1^2)
a_c = c1 * norm,  a_d = d1 * norm
```

so the sum of squares over the pairs is 1 whatever U and b are (**power normalisation**); b = 0 leaves only the centre voice (a_c = 1), b = 1 gives
`c0 = 0.4`, `d0 = 0.6`. Measured (Sine, U = 3, spread 0, b = 100 %: centre -8.09 dB, outer -10.59 dB, single voice -5.99 dB: ratios
0.7845 and 0.5884 = the pair amplitudes 0.5547 / 0.8321 times the stereo law of 9.5 at spread 0).

The amplitudes are smoothed linearly across each chunk when the knobs move.

### 9.5 Stereo spread

After the unison sum, the raw stereo pair `(L, R)` of the oscillator is blended with equal power:

```
a = sin((pi/2) * (0.5 + 0.5 w))       -- own channel
b = sin((pi/2) * (1.5 + 0.5 w))       -- other channel; a^2 + b^2 = 1
L' = a L + b R,  R' = a R + b L
```

w = 1: a = 1, b = 0 (full spread: the left channel carries the left voices only). w = 0: a = b = 0.7071 (both channels identical). Then oscillator
pan (section 8) multiplies. A centre voice (identical in L and R) therefore has gain `a + b` after the blend (1 at w = 1, 1.414 at w = 0),
which is why a unison stack whose centre voice dominates (and a single voice, U = 1, which is a centre voice) gets louder by 3 dB when
w goes from 1 to 0 (measured U = 1: -5.99 dB at w = 100 %, -2.97 dB at w = 0). **Quirk: keep it**, a preset that sets the spread would
otherwise change level. Measured U = 3, w = 50 %: centre -8.78 dB, near outer voice -8.27 dB, far outer -15.94 dB
(= -5.99 + 20 log(0.5547 * (0.9239 + 0.3827)), 0.8321 * 0.9239, 0.8321 * 0.3827).

### 9.6 The "Serum" unison law (`oscN.unison.law` = 1, our extension)

A measured table replaces 9.4 and 9.5 (fitted to the real Serum 2.1.5; `tools/ref-compare/fit_unison.py` has the fit, the numbers are
in the engine source and can be read back black-box): per voice count a gain `g(U)` (U = 2: 0.3200, 3: 0.4384, 4: 0.4260, 5: 0.4139,
6: 0.4021, 7: 0.3907, 8: 0.3796, 9: 0.3689, 10: 0.3584, 11: 0.3482, 12: 0.3384, 13: 0.3288, 14: 0.3194, 15: 0.3104, 16: 0.3016;
U = 1: 1), centre-voice and outer-voice gain tables on the blend axis 0, 10, ..., 100 %:

| blend % | 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 | 100 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| centre | 1.0000 | 0.9945 | 0.9778 | 0.9501 | 0.9112 | 0.8613 | 0.8003 | 0.7281 | 0.6449 | 0.5506 | 0.4451 |
| outer | 0.0445 | 0.0573 | 0.0948 | 0.1543 | 0.2314 | 0.3200 | 0.4119 | 0.4975 | 0.5653 | 0.6018 | 0.5921 |

Linear interpolation on the blend axis. `a_c = g(U) * centre(b) * (1 if U odd else T)` and `a_d = g(U) * outer(b) * T`, with `T` the "total"
of the pan table (own + other); for U = 2: `a_c = g(2) T`. Pan table (spread 0, 0.1, 0.25, 0.5, 0.75, 0.9, 1): own channel
1.0000 1.1286 1.3315 1.6955 2.0925 2.3461 2.5217, other channel 1.0000 0.8766 0.7010 0.4346 0.2011 0.0764 0.0000 (linear between);
the left/right blend uses `own/(own+other)` and `other/(own+other)` in place of `a`, `b` of 9.5.

## 10. Smoothing summary

Everything that is a "knob" is smoothed in one of three ways; implementers must not make it stepped (zipper noise is audible):

| Class | Method | Time |
|-------|--------|------|
| Smoothed audio-rate controls: `oscN.level`, `oscN.phase`, `noise.level`, `filterN.cutoff`, the formant x/y/transpose, `eq.*.cutoff`, `distortion.drive`, `distortion.filter_cutoff`, `phaser.center` | one-pole, 5 Hz cutoff on the target, per sample, from the value of the previous sample (the smoother is skipped when the value is steady) | time constant 32 ms |
| Every other control (transpose, tune, pan, unison and warp parameters, envelope times, LFO rates, effect parameters, ...) | evaluated once per chunk, then linearly interpolated across the chunk by the module that uses it (pitch, amplitudes, pan, warp amount, filter coefficients): no extra one-pole | <= 128 samples |
| Master volume | the linear gain is interpolated across each chunk | <= 128 samples |
| Switches | a stepped value, no smoothing (e.g. `oscN.on`, destinations) | - |
| `quality.no_smoothing` (our key) | disables all of the above (values jump at the start of the chunk); for offline renders and tests | - |

When a note is triggered, per-voice smoothers jump to their targets (no ramp from the previous note).

## 11. Extensions that live in the voice (our design)

* **Oscillator mapping** (`oscN.map.*`): per oscillator a key range and a velocity range (a note outside is muted; the oscillator
  keeps running if "warp" so that other oscillators can use it as FM source); optional octave **fold**: a note below the range is
  moved up by whole octaves (`shift = ceil((low - note)/12)` octaves) into the range and above it moved down, only if the range is at
  least 11 semitones wide; the fold in semitones is added to the oscillator's pitch.
* **Voice control** (`voicectl.*`): a sequence of up to 8 steps indexed by the note counter (`(count-1) mod length`), each step carries
  values for pan, detune (cents), filter cutoff offset (percent of ±36 semitones), envelope time factor `2^(2 * x)` for x in ±1,
  and two free sources (Voice Mod 1/2); random parts are a hash of (seed, note counter, stream) so the same notes give the same values and
  offline renders repeat. `Voice Index` is `step/(length-1)`. The values feed Pan and Detune of each source, the filter cutoff, the
  envelope time multiplier and the matrix sources.
* **Per-note random sources** (Note-On Alt 1/2 = parity of the note counter, Note-On Rand 2, 64 discrete randoms): hashes of (seed,
  counter, stream) mapped to 0..1.

## 12. Gotchas and "better"

* Steal mode is a click risk (envelope restarts at 0 within a chunk). A better engine can crossfade 1-2 ms: that is within the spec
  as long as the new note begins at the same sample.
* Kill mode's 50 ms fade has no overlap limit: with fast repeated notes at P reached, up to P + 1 voices (plus the fading ones)
  sound; the pool size P + 1 bounds this. A better engine can use a larger spare pool.
* The bend freezes for released voices (6.3). The portamento position advances once per chunk (a staircase).
* Voice death at 1e-16 of amplitude can keep a voice with an extremely long exponential tail alive; the envelope shapes always reach 0
  exactly, but a modulation that adds a constant to level or release might keep it alive. Use a threshold (e.g. -140 dB for 1 chunk)
  to free voices earlier: allowed.
* Per-sample cost: the engine processes two voices per SIMD register (four floats: voice A L/R, voice B L/R). The own engine is free to use
  any layout.
* Sample rate above 44.1 kHz: the oversampling factor is halved per doubling of the rate (88.2 kHz runs at 1x if the setting was 2x), so
  voices always run near 88-96 kHz internally unless quality 4x is on.

## 13. Test vectors (black box, `IEngine`, 48 kHz, Sine, filter off, `master.volume` 0 dB unless stated)

| # | Script | Expected |
|---|--------|----------|
| V1 | `master.polyphony 2`, `master.voice_override 0` (Kill), `env1.attack 1 ms`, `release 200 ms`, sustain 100; notes 60 (vel .5) at 0, 64 (.9) at 0.3, 67 (.7) at 0.6; all off at 1.8; look at 0.9 .. 1.4 s | sounding pitches by `steal_priority`: 0 Newest {64, 67}; 1 Oldest {60, 64}; 2 Highest {64, 67}; 3 Lowest {60, 64}; 4 Velocity {64, 67}; `activeVoices` = 2 |
| V2 | as V1 with `master.voice_override 1` (Steal) | 0 {64, 67}; 1 {60, 67}; 2 {64, 67}; 3 {60, 67}; 4 {64, 67} |
| V3 | `osc1.level 100`, vel 1, note 69 held: peak amplitude of the 440 Hz line | 1.00 (+-0.1 dB); `osc1.level 50` -6.02 dB, 25 -12.03, 10 -19.99; velocity 0.75 -4.99 dB, 0.5 -12.03, 0.25 -24.07; `velocity_track 50` at v 0.5: -4.99 dB; `master.volume -6/-20`: -6/-20 dB; `-59` -59 dB; `-60` silence; pan -100 left channel +3.01 dB; pan -50 left +2.33 dB |
| V4 | `master.portamento.time 500`, slope 0, `env1.release 1000`; note 60 at 0, note 67 at 1.0 (overlapping), 60 off at 1.5, 67 off at 2.0; track the pitch every 100 ms from 0.9 s | pitch rises linearly from 60.0 at 1.0 s to 67.0 at 1.5 s (61.03 at 1.02, 62.0 at 1.12, 63.3 at 1.22, 64.7 at 1.32, 65.9 at 1.42, 67.0 at 1.52); stays 67.0 through the release (no re-glide) |
| V5 | unison: Sine, `osc1.unison.detune 100`, `detune_curve 0`, `blend 100`, spread 100; U = 2 | left 392.01 Hz (-200 ct), right 493.89 Hz (+200 ct), each -12.01 dB relative to full scale at -6 dB volume |
| V6 | U = 3, same | left {440.00, 493.89}, right {392.01, 440.00}; levels 0.5547 x (centre) and 0.8321 x (outer) of a single voice |
| V7 | U = 5, `detune_curve 1.5` | left {392.01, 440.00, 456.60}, right {423.96, 440.00, 493.89} |
| V8 | U = 7, curve 0 | left {407.36, 440, 457.29, 493.89}, right {392.01, 423.40, 440, 475.21} |
| V9 | U = 3, blend 100, spread 0 | both channels {392.01 -10.59 dB, 440 -8.09 dB, 493.89 -10.59 dB} with the single voice at -5.99 dB |
| V10 | U = 3, blend 100, spread 50 | left {392 -15.94, 440 -8.78, 494 -8.27}, right mirrored |
| V11 | U = 3, blend 0, spread 100 | only 440 Hz at -5.99 dB in both channels |
| V12 | U = 6, D = 50, c = 0 | left {415.28, 434.93, 455.49}, right {425.00, 445.14, 466.18} |
| V13 | velocity_track -100, v 0.5, level 100 | gain 2.25 (+7.04 dB), output clipped at ±2.1: harmonics appear |

Tolerances: frequencies +-0.5 ct, levels +-0.15 dB for laws (V3, V5 .. V13); V1/V2 the set of sounding pitches exactly; V4 +-0.25 st.
