# Engine specification for the own (iOS) engine

Written 2026-11-12 on branch `engine-spec`. This directory is the **only** channel from the people (or agents) who may read the
Vital-derived engine (`core/engine-vital/upstream/`, GPLv3) to the people who write the own engine (`core/engine-own/`, original code,
the only engine of iOS builds, `docs/ARCHITECTURE.md` 2.2). It describes the **behaviour and the algorithms** of the Vital-derived
engine in prose and mathematics so that a competent DSP engineer can build an engine that is as good or better without ever opening
that source.

## Process rules (clean room)

1. **Two roles.** The *spec author* reads `core/engine-vital/upstream/` and writes `docs/engine-spec/*.md` and nothing else. The
   *implementer* works on branch `engine-own-impl`, writes only `core/engine-own/` (and tests), and **must never open
   `core/engine-vital/upstream/`, `core/engine-vital/adapter/` or any file of the `engine-vital` build products.** The implementer
   may use: these documents, `docs/PARAMETERS.md` / `docs/params.json` (our own parameter table), `docs/ui-design/*` (our own design
   documents, including `13-new-keys.md`), the public manuals under `docs/ui-design/_manuals`, the Vital-free libraries in
   `core/` (`filters-serum`, `fx-serum-a`, `fx-serum-b`, `sample-osc`, `granular`, `spectral`, `multisample`, `shapes`, `formula`,
   `wavetable-edit`, `wavetable-render`, `tuning`), and **black-box measurements** of engine-vital through `synthyboy::IEngine`
   (render a script of parameter values and note events, look at the audio). Reading the *output* of engine-vital is allowed and
   encouraged; reading its *code* is not.
2. **What the documents contain.** Prose, mathematics, numeric constants and tables where the behaviour depends on them (functional
   facts), signal flow and order of operations, parameter-to-DSP maps in terms of **our** parameter keys, edge cases, numerical notes,
   quality knobs, and a "better" paragraph per topic. **What they never contain:** code, pseudo-code that follows the source line by
   line, identifiers (class, function, variable, enum names) of the upstream source, copied comments. Where a name is needed the
   document uses our parameter keys (`osc1.unison.detune`), our file names, or a plain description.
3. **Acceptance tests.** Each document ends with test vectors: a script (parameter values, notes) and the expected measurement. They
   were produced by running engine-vital black box with the probe described below; the implementer reproduces the same script
   against the own engine through `IEngine` and compares with the stated tolerance. A vector that the document marks "law" must
   match; a vector marked "character" is a target for the sound (the own engine may do better, but must not be worse in the
   stated sense).
4. **Equal or better.** "Equal" is measured at the vectors. "Better" is wanted where a document says so (aliasing, cost, zipper
   noise, determinism); the implementer may deviate from a *quirk* only where the document lists it as a quirk and says what the
   deviation must preserve.
5. **Order and commits.** The documents are committed one by one, in the order of the table; the implementer picks them up as they
   appear. A document that is marked `draft` can still change; `final` is stable (later corrections are listed at its end).
6. **Question channel.** The implementer writes questions into `docs/engine-spec/QUESTIONS.md` (a table: number, document, question).
   The spec author answers in the document (never in chat) and marks the row answered.

## The probe (how the test vectors were made)

A 90-line program (not committed: it is a throw-away driver) creates `createEngine(EngineKind::vital)`, calls `prepare(48000, 512,
Quality{realtime=false})`, then executes a text script: `set KEY VALUE` (plain value of one of **our** parameters), `on NOTE VEL TIME`,
`off NOTE TIME`, `bend/mod/at/pedal TIME VALUE`, `render SECONDS OUT.raw` (32-bit float stereo, blocks of 512). Analysis is plain
FFT/zero-crossing arithmetic in Python. Everything is deterministic and offline (`realtime=false`). The factory defaults of the
parameter table apply to every key a script does not set, so a vector says which keys it sets. "Sine" means `osc1.wavetable = 0`
(entry 0 of the built-in list is the sine), "filter off" means `filter1.on = 0`. Sample rate 48 kHz unless stated. The default tier
of the probe is the one of `Quality{}` (2x oversampling, linear frame interpolation).

The implementer builds the same driver (it is ten lines against `IEngine`; `tools/preset-render` and `tools/ref-compare` show how)
and runs the vectors against `EngineKind::own`.

## Conventions used in all documents

* **Block**: the engine renders in chunks of at most 128 frames at the host rate, cut at every note event so that events happen at the
  first sample of a chunk. "Control rate" below means once per chunk; "audio rate" means every sample.
* **dB** is 20 log10 of amplitude. **st** semitones, **ct** cents (1/100 st). Pitch is expressed in MIDI note numbers (A4 = 69 = 440 Hz,
  `f = 8.1757989156 * 2^(note/12)`, fractional notes allowed); `master.tuning.a4` rescales this.
* **Power scale** `P(x, p)` for x in [0,1]: `(exp(p x) - 1) / (exp(p) - 1)`, and `x` itself when |p| < 0.01. It is the curve law of
  envelopes, portamento and unison detune. p > 0 bends down (slow start, fast end), p < 0 bends up.
* **One-pole smoothing** at cutoff `fc`: per step `y += (x - y) * (1 - exp(-2 pi fc dt))`.
* The engine has two audio channels throughout; every voice produces a stereo pair.
* The word **quirk** marks behaviour that is a side effect of the original design and not something a player would ask for.

## Status

| # | Document | Status | Contents |
|---|----------|--------|----------|
| 1 | [01-voice-management.md](01-voice-management.md) | final | polyphony, stealing, portamento, legato, unison laws, note on/off, retrigger, velocity, aftertouch, MPE, smoothing, level laws |
| 2 | 02-oscillator.md | pending | wavetable reading, band-limiting, frame interpolation, phase, pitch laws, every warp mode, spectral morph, sub, noise |
| 3 | 03-envelopes-lfos.md | pending | envelope stages and curves, timing, retrigger/loop, LFO shapes, sync, fade, delay, random LFOs |
| 4 | 04-modulation-matrix.md | pending | sources, destinations, amount/power/bipolar/stereo law, ramp, macros, evaluation order |
| 5 | 05-filters.md | pending | filter models not in `core/filters-serum`, oversampling, drive, keytrack, blend, formant/comb/phaser |
| 6 | 06-effects.md | pending | chorus, delay, reverb, distortion, EQ, compressor, phaser, flanger, filter FX, chain order and mix laws |
| 7 | 07-sample-granular-spectral.md | pending | how the sample/granular/spectral/multisample oscillators plug into the voice |

## Scope note: what "the Vital engine" means here

engine-vital is the Vital engine plus SynthyBoy's own additions (the keys added after state version 2: second warp slot, Serum 2
warp modes, voice control, oscillator mapping, buses, rack, ...). The documents describe the Vital baseline completely. The additions
are our own design (`docs/ui-design/13-new-keys.md` is the authoritative description of what they must do, with the Serum 2 manual
as the public reference); a document covers an addition only where the Vital code path is what defines its sound, or where the
addition changes the behaviour of the baseline. The own-engine gap list is `docs/ui-design/OWN-ENGINE-GAP.md`.
