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
| `voice/` | the voice (3 oscillators, sub, noise, 2 filters on `core/filters-serum`, 6 envelopes, 10 LFOs, 4 random LFOs, sources), the table positions of the keys |
| `fx/` | the master bus effect chain in `fx.order` order: chorus, compressor, delay, distortion, EQ, FX filter, flanger, phaser, reverb (written here), Hyper/Dimension and Utility (`core/fx-serum-a`) |
| `own_engine.*` | events, voice allocation (polyphony, same-note rule, steal priority, legato, portamento), the control grid, the master stage |
| `own_support.*` | which keys the engine acts on (`supportsParam`) and the capability bits it reports |

## Timing

Envelopes, LFOs and the matrix advance in control periods of 32 samples **on the absolute sample count** (never on the host block), audio rate values
(pitch, level, pan, cutoff) are interpolated linearly across the period: the output does not depend on how the host splits the stream into blocks
(`own_output_does_not_depend_on_the_block_size`). Parameters are read once per `process()` call. No allocation in `process()` (the wavetable bank is
built in `prepare()`).

## What is not there yet (see docs/ui-design/OWN-ENGINE-GAP.md, generated)

Sample / granular / spectral / multisample oscillators (step 5), user wavetables (Preset table), spectral morph, the per slot stereo lane, rise / fall / delay of a
slot, tuning / scale / MPE (step 6), the mixer graph (buses, sends), voice control, the effect racks, Bode, Convolve and the splitters.

## Known differences to engine-vital

Wavetable names with several frames (Saw to Square, Sine to Saw, Harmonic Series, Odd Harmonics, Formant Sweep, FM Sweep, PWM Sweep, Bell Partials) are defined
here, not matched frame by frame. The warps are original definitions; they alias less than engine-vital (measured, see the report of the work).
The filters-serum types can run away at resonance and drive 100 %: `voice/filter_unit.h` guards the output.
