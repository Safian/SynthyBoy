# engine-own

The engine written for this project (original code, no Vital-derived content, clean-room process of `docs/ARCHITECTURE.md` 2.2): a Serum-class
subtractive wavetable synth behind `synthyboy::IEngine`. It is always built and the only engine of iOS builds. Its sources are the public
documents (`docs/engine-spec/*.md` of branch `engine-spec`, `docs/ui-design/*`, the manuals), DSP literature and black-box measurements of
engine-vital through `IEngine` (`tools/own-oracle`); it never reads `core/engine-vital`.

## Layout

| Directory | Contents |
|-----------|----------|
| `dsp/` | helpers shared by all modules (control period, power curve, pan law, RNG, smoother) |
| `osc/` | band-limited built-in wavetables (additive spectra, inverse FFT, 21 half-octave mip levels stored at 2x their band: the eight 64 frame tables take 20.5 MB, `own_wavetable_bank_memory`), the unison wavetable oscillator, the warp modes (4x oversampled with a decimator when a warp is active), the spectral morph (`morph.*`: an inverse FFT of the morphed spectrum every 256 samples, cross-faded), the content set (`content_set.*`: user tables, samples with their analysis, multisample instruments, drawn curves, decoded off the audio thread and handed over without a lock), and the sample / granular / spectral / multisample oscillator types of the voice (`lib_osc.*`, the glue of docs/engine-spec/07 around core/sample-osc, granular, spectral, multisample) |
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

## Content and the sample-type oscillators

`setContent` / `setContentStore` (message thread) decode the content section into one immutable `ContentSet` (user wavetables = Preset table, samples with their spectral analysis,
instruments, remap curves, spectral filter masks) and publish it with an atomic pointer; the audio thread announces the set it reads (hazard pointer) and the message thread frees a
set only when it is neither current nor announced. A voice whose set changed stops its players. `oscN.type` 1 .. 4 runs `LibOsc` (one per slot and voice): unison as a line of voices
(doc 07 section 5), pitch, start position, scan, loops and slices, the FM / RM / AM / quantize / distortion warps (sample, granular) and the spectral warps, a 10 ms crossfade on a
type change. Memory of the library voices (granular clouds, spectral voices, multisample layers) is allocated in `prepare ()` for all 32 voices (about 25 MB); the analysis of a sample
(`spectral::Analysis`, up to 12 KB per frame and channel, 16 s at most) is built when the content is set.

## What is not there yet (see docs/ui-design/OWN-ENGINE-GAP.md, generated)

The per slot stereo lane is there, but not: the frame position remap with RISE / FALL (`remap.on`), the X|Y control of the granular and spectral oscillators (`y_axis`), the Mask /
Vocode spectral warps from the Noise (Osc A / B are fed), choke groups across voices, tuning / scale / MPE (step 6), the mixer graph (buses, sends), voice control, the effect racks,
Bode, Convolve and the splitters.

## Known differences to engine-vital

Wavetable names with several frames (Saw to Square, Sine to Saw, Harmonic Series, Odd Harmonics, Formant Sweep, FM Sweep, PWM Sweep, Bell Partials) are defined
here, not matched frame by frame. The warps are original definitions; they alias less than engine-vital (measured, see the report of the work).
The filters-serum types can run away at resonance and drive 100 %: `voice/filter_unit.h` guards the output.
