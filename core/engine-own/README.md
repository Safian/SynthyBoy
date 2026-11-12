# engine-own

The engine written for this project (original code, no Vital-derived content, clean-room process of `docs/ARCHITECTURE.md` 2.2): a Serum-class
subtractive wavetable synth behind `synthyboy::IEngine`. It is always built and the only engine of iOS builds. Its sources are the public
documents (`docs/engine-spec/*.md` of branch `engine-spec`, `docs/ui-design/*`, the manuals), DSP literature and black-box measurements of
engine-vital through `IEngine` (`tools/own-oracle`); it never reads `core/engine-vital`.

## Layout

| Directory | Contents |
|-----------|----------|
| `dsp/` | helpers shared by all modules (control period, power curve, pan law, RNG, smoother) |
| `osc/` | band-limited built-in wavetables (additive spectra, inverse FFT, half-octave mip levels), the unison wavetable oscillator, the warp modes (4x oversampled with a decimator when a warp is active) |
| `env_lfo/` | DAHDSR envelope (output squared as the amplitude, power curves), the LFO shape table, the random LFOs |
| `matrix/` | the 64 slot modulation matrix: slot law, aux source, destination control domains (`DestLaw`: lin / sqrt / fourth root / log2 / note / volume) |
| `voice/` | the voice (3 oscillators, sub, noise, 2 filters on `core/filters-serum`, 6 envelopes, 10 LFOs, 4 random LFOs, sources, voice control, the mixer outputs Main / Bus 1 / Bus 2 / Direct), the classic filter models, the tuning (`note_tuning.h`: A4, scales, user file, MTS-ESP seam, key scale), the content the voice reads (`own_content.h`: custom LFO shapes with point modulation, per slot curves), the table positions of the keys |
| `fx/` | the effect racks (Main, Bus 1, Bus 2 and the bands of the three splitters, one `FxChain` per bus) in `fx.order` order: chorus, compressor, delay, distortion, EQ, FX filter, flanger, phaser, reverb (written here), Hyper/Dimension, Utility and the splitters (`core/fx-serum-a`), Bode (`core/fx-serum-b`) |
| `own_engine.*` | events, voice allocation (polyphony, same-note rule, steal priority, legato, portamento), the control grid, the master stage |
| `own_support.*` | which keys the engine acts on (`supportsParam`) and the capability bits it reports |

## Timing

Envelopes, LFOs and the matrix advance in control periods of 32 samples **on the absolute sample count** (never on the host block), audio rate values
(pitch, level, pan, cutoff) are interpolated linearly across the period: the output does not depend on how the host splits the stream into blocks
(`own_output_does_not_depend_on_the_block_size`). Parameters are read once per `process()` call. No allocation in `process()` (the wavetable bank is
built in `prepare()`).

## The voice side of this branch (engine-own-c)

Tuning (A4, Pythagorean / Just / user .scl + .kbm, MTS-ESP seam, key scale), MPE (member channel bend, axes to macros / mod wheel), Kill mode with the 50 ms fade, note recall,
poly legato, voice control, LFO sync types 2 .. 5, swing, stereo offset, HOST lock, mono LFOs, custom LFO shapes with point modulation, rise / fall / delay of a slot, the amount of a
slot as a destination, the stereo source, the cycle check, the mixer graph (sends, filter blend, buses, Direct, bypasses), filter pan / clean mode / level, the Diode ladder with the
high pass in front, the delay with the tape glide, the chorus and flanger of document 06, the phaser of document 06 section 8, the Serum unison law.
Tests: `core/tests/test_own_voice.cpp` (vectors M1 .., L1 .., D1 / D2, F1 .. F7, P1 and an off / on probe for every capability bit this branch reports).

## What is not there yet (see docs/ui-design/OWN-ENGINE-GAP.md, generated)

Sample / granular / spectral / multisample oscillators, user wavetables (Preset table), spectral morph (branch engine-own-b), the 16 slot effect instance pool (`fxiNN.*`), Convolve
(needs the impulse content of the store), the audio rate evaluation of the matrix (the control period is 32 samples), the Serum EQ / compressor / convolve laws, latency compensation of Bode
across the buses, the SVF styles 2 .. 4 exactly (question 4 of `docs/engine-spec/QUESTIONS.md`).

## Known differences to engine-vital

Wavetable names with several frames (Saw to Square, Sine to Saw, Harmonic Series, Odd Harmonics, Formant Sweep, FM Sweep, PWM Sweep, Bell Partials) are defined
here, not matched frame by frame. The warps are original definitions; they alias less than engine-vital (measured, see the report of the work).
The filters-serum types can run away at resonance and drive 100 %: `voice/filter_unit.h` guards the output.
