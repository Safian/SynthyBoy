# Upstream take-over record: Vital

This file records what was taken from upstream, how, what was left out and why, and every local change to imported
files. See [ARCHITECTURE.md](ARCHITECTURE.md) section 2 for the policy.

## Source

| | |
|---|---|
| Upstream | https://github.com/mtytel/vital (author Matt Tytel) |
| Commit | `636ca0e` (2022-04-20 16:53:39 +0100), the last public commit; shallow clone |
| Method | one-time copy of selected files (`cp`, upstream relative paths kept below `core/engine-vital/upstream/`, so later upstream diffs apply by path). Commit "Import Vital 636ca0e (pristine)" contains the files byte-identical (verified with `cmp` against the clone before committing). All changes are in later commits and listed below. |
| Licence of the files | every imported Vital file carries the header "Copyright 2013-2019 Matt Tytel ... GNU General Public License ... version 3 ... or (at your option) any later version" (checked for all imported files), so they are **GPL-3.0-or-later**. The text is in `core/engine-vital/upstream/LICENSE-GPL-3.0` (upstream `LICENSE`, byte-identical). |
| Combined work | the repository is **AGPL-3.0-or-later**; GPLv3 section 13 allows combining GPLv3 code with AGPLv3 code, the combined work is governed by the AGPL for the combined parts. Original Vital files keep their GPL header; our own files carry `SPDX-License-Identifier: AGPL-3.0-or-later`. |

## What was imported (summary)

All below `core/engine-vital/upstream/`:

| Path | Files | Lines | Content |
|------|-------|-------|---------|
| `src/synthesis/**` | 147 | 24 952 | framework, producers, filters, modulators, effects, effects_engine (removed again, see below), modules, synth_engine, lookups, utilities |
| `src/common/wavetable/**` | 32 | 3 764 | wavetable data model (`WavetableCreator`, components, keyframes, modifiers) |
| `src/common/{synth_constants.h, synth_types.*, synth_parameters.*, line_generator.*, tuning.*, fourier_transform.h}` | 10 | 1 773 | shared types, Vital's parameter definitions (used by the imported modules via `getDetails`) |
| `src/interface/look_and_feel/synth_strings.h` | 1 | 332 | **single exception**, see below |
| `third_party/kissfft/{kissfft.h, COPYING, LICENSES/}` | 4 | 359 (header) | KISS FFT, BSD-3-Clause (the FFT used by `fourier_transform.h`) |
| `third_party/json/json.h` | 1 | 17 300 | nlohmann/json 3.1.2, MIT (included by `sample_source.h`, `line_generator.h`, `tuning.h`, the wavetable components) |
| `LICENSE-GPL-3.0` | 1 | 674 | Vital's `LICENSE` |

Include closure: every `#include "..."` of the imported files was resolved transitively against the clone; the only
files outside the requested list that the retained code truly includes are:

* `src/interface/look_and_feel/synth_strings.h` (included by `src/common/synth_parameters.cpp` and
  `src/synthesis/modules/reorderable_effect_chain.cpp`). It lives under `src/interface` upstream but is a plain table of
  `std::string` constants (menu/choice names) without any GUI or JUCE code. It is the **only** file taken from
  `src/interface`; nothing else from there is taken or may be taken.
* `src/common/load_save.h` is included by `wavetable/wavetable_creator.cpp` (see "local modifications": it is not imported,
  the include is removed).
* `ipps.h` (Intel IPP) is included only in an `#if INTEL_IPP` branch that is never compiled; the header is not imported.

## What was NOT imported, and why

| Not imported | Reason |
|--------------|--------|
| `src/interface/**` (except `synth_strings.h`), `src/plugin`, `src/standalone`, `src/headless`, `plugin/`, `standalone/`, `headless/` | Vital's GUI, plugin wrappers and applications: JUCE/OpenGL, own UI (ARCHITECTURE 8) |
| `src/common/synth_base.*`, `load_save.*`, `midi_manager.*`, `synth_gui_interface.*`, `border_bounds_constrainer.*` | JUCE-heavy application layer; our adapter replaces `SynthBase` |
| `src/common/authentication.h`, `startup.*` | network / account code (`vital.audio`), forbidden by upstream's terms and by ARCHITECTURE 2.2 |
| `third_party/JUCE`, `VST_SDK`, `firebase_cpp_sdk`, `concurrentqueue` | not needed / must not be pulled in |
| `default.vitalskin`, `fonts/`, `icons/`, `tunings/`, `debian/`, `Makefile` | skin, fonts, icons: trade dress and separate licences; tunings are content |
| presets and wavetables of any kind | separate licence, no redistribution (ARCHITECTURE 2.1, 2.3) |
| `tests/` | JUCE UnitTest based; the DSP tests in `tests/synthesis` are ported (translated) to `core/engine-vital/tests/`, see "Ported tests" below |

## Local modifications to imported files

Every changed file carries `// Modified for SynthyBoy (AGPL-3.0-or-later combined work), 2026` directly below the original
header, which is otherwise untouched. The diff against the pristine import commit (`git diff 26daf58 -- core/engine-vital/upstream`)
is the complete list; the table groups it.

### Mechanical, all imported files under `upstream/src` (185 files)

| Change | Reason |
|--------|--------|
| `#include "JuceHeader.h"` -> `#include "vital_compat.h"` (21 files) | JuceHeader.h only provided standard headers and the leak-detector macros. `compat/vital_compat.h` provides exactly those (`JUCE_LEAK_DETECTOR(x)` = nothing, `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x)` = deleted copy operations). The macro uses themselves are unchanged. |
| `namespace vital` -> `namespace sbvital` and every `vital::` -> `sbvital::` (all files with the namespace, incl. `synth_strings.h`) | binary symbols must not be confused with Vital's own (ARCHITECTURE 3.1); also what `scripts/check-no-vital.sh` greps for. Global names (`LineGenerator`, `Tuning`, `WavetableCreator`, `FileSource`, `strings`, ...) did not clash with anything and were not renamed. `VITAL_ASSERT` (macro) is unchanged; it is active only when `DEBUG` is defined (CMake Debug). |

### Functional changes

| File | Change | Reason |
|------|--------|--------|
| `src/synthesis/effects_engine/**` (4 files) | **removed** | Vital's separate effects-only product: it defines a second `vital::SoundEngine` (ODR clash with `synth_engine/sound_engine.*`), uses `FloatVectorOperations`, and is not part of the synthesizer. Never built. |
| `src/synthesis/synth_engine/sound_engine.cpp` | removed `FloatVectorOperations::disableDenormalisedNumberSupport()` in `process()`; added `#include "chorus_module.h"` | JUCE call that set FTZ/DAZ on the host's audio thread for good. Denormal handling is a shell/adapter duty (ARCHITECTURE 3.7); the adapter sets and restores FTZ/DAZ around `process()`. The include was missing (upstream built as a unity build). |
| `src/common/fourier_transform.h` | removed the `juce_dsp` branch; the Accelerate (vDSP) branch is now opt-in (`#elif __APPLE__ && defined(SBVITAL_USE_ACCELERATE)`); the kissfft branch is used everywhere by default, the Intel IPP branch stays compiled out | JUCE dependency; one FFT implementation on all platforms (same numbers on macOS and Linux) |
| `src/common/tuning.h`, `tuning.cpp` | `juce::String`/`File`/`StringArray` replaced by `std::string` / `std::vector<std::string>`. **Removed:** `Tuning::getTuningForFile(File)`, `Tuning::allFileExtensions()`, `Tuning(File)`, `loadFile(File)` and the three `File` overloads of `loadScalaFile/loadKeyboardMapFile/loadTunFile` (they only read the file and then parsed). **Kept, now taking the text lines:** `loadScalaFile(const std::vector<std::string>&)`, `loadKeyboardMapFile(...)`, `loadTunFile(...)` (public), `noteToMidiKey(const std::string&)`. Small `std::string` helpers (trim, tokens, number parsing with the classic locale) replace the `juce::String` calls. The tuning name is no longer taken from the file name (`setName()`). | no file access in the core; loading goes through the content API (ARCHITECTURE 3.1). Nothing else used these functions. |
| `src/common/wavetable/wave_source.cpp`, `file_source.cpp`, `src/synthesis/producers/sample_source.cpp` | `juce::Base64` / `MemoryOutputStream` / `String` in `stateToJson/jsonToState` replaced by `sbcompat::toBase64/fromBase64` (compat) and `std::string` / `std::vector`; decoded buffers are resized to the expected length before the `memcpy` (the original copied the full length from whatever was decoded) | JUCE dependency. **Format note:** standard RFC 4648 base64; JUCE's `Base64` uses a different alphabet, so Vital's own wavetable/preset JSON blobs are not readable (intended, ARCHITECTURE 2.3). |
| `src/common/wavetable/wavetable_creator.cpp` | removed `#include "load_save.h"`; `updateJson()` (migrations of Vital's pre-1.0 wavetable JSON, using `LoadSave::compareVersionStrings/convertBufferToPcm/convertPcmToFloatBuffer`) reduced to `return data;` | `LoadSave` is JUCE-heavy and not imported; only files written by this code are read. `ProjectInfo::versionString` (still written into the JSON) is provided by `vital_compat.h`. |
| `src/synthesis/framework/poly_values.h` | (a) SSE2 detection also accepts MSVC (`_M_X64`, `_M_IX86_FP >= 2`; MSVC does not define `__SSE2__`); (b) the NEON `div()` uses the existing Newton-Raphson branch (`vrecpeq_f32` + 2x `vrecpsq_f32`) on every target except AArch64 (condition was `NEON_ARM32`, which no build defined) | `vdivq_f32` exists on AArch64 only. This was the only A64-only intrinsic of the whole tree (all other NEON intrinsics used are ARMv7). Built with GCC 12.2 for `armv7-a -mfpu=neon -mfloat-abi=hard` (the repository's toolchain file) and all test executables pass under QEMU user mode (`readelf -A`: Tag_CPU_arch v7, NEONv1, VFP registers); **not yet run on real ARMv7 hardware** (QEMU does not model the NEON/VFP corner cases or timing). |
| `src/common/wavetable/wave_line_source.h` (+`line_generator.h`), `src/synthesis/filters/phaser_filter.h` (+`futils.h`), `src/synthesis/modulators/synth_lfo.cpp` (+`futils.h`), `src/synthesis/modules/envelope_module.h` (+`envelope.h`), `equalizer_module.h` (+`memory.h`), `src/synthesis/effects/reverb.h` (+`memory.h`), `src/common/synth_parameters.cpp` (+`operators.h`) | added the `#include` the file needs itself | upstream compiles these as unity builds in a fixed order and never needed them; compiling each `.cpp` on its own does |
| `src/common/fourier_transform.h` (kissfft branch) | forward: keeps the Nyquist bin (`data[size_]`; the vDSP layout conversion this branch copied overwrote it with the imaginary part of DC), clears everything above it; inverse: DC and Nyquist get half weight (`data[size_] *= 0.5f` instead of `data[1] = data[size_]`) | **bug fix**: upstream selects IPP, JUCE `dsp::FFT` or Accelerate before it falls back to this kissfft branch, so the branch is not what upstream's builds run (as far as the code shows); it lost the Nyquist component; found by the ported `WaveFrameTest` (2048 of 2048 samples out of tolerance with random input) and covered by `vital_fft_matches_a_direct_dft` |
| `third_party/kissfft/kissfft.h` (`kf_bfly_generic`) | `cpx_t scratchbuf[p]` (variable length array) → `std::vector<cpx_t> scratchbuf(p)` | MSVC does not support VLAs (GitHub Windows CI: C2131). Only the generic-radix butterfly allocates; the transforms used by the engine have power-of-two sizes (radix 2/4 only), so no allocation happens on the audio thread |
| `src/synthesis/synth_engine/sound_engine.cpp` | `allNotesOff (int sample, int channel)` called `voice_handler_->allNotesOff (channel)`, i.e. the overload for *all* channels with the channel number as sample offset; now `allNotesOff (sample, channel)` | **bug fix**: a channel specific all-notes-off released every voice (found by the shared engine test `engine_all_notes_off_releases`) |
| `src/synthesis/framework/voice_handler.cpp` | `allSoundsOff()` also resets `last_played_note_` to -1 | after all-sounds-off no previous note exists; without it the first note after a reset glides from the last note of the previous run (portamento minimum is 1 ms), so `reset()` could not reproduce a fresh engine |
| `src/synthesis/producers/synth_oscillator.cpp` | (a) `modulation_buffer + start` only if the buffer is not null (two places); (b) the random start phases `float * INT_MAX` -> `uint32_t` go through a clamped `int32_t` | UBSan: null pointer plus offset; negative float to unsigned is undefined (the oscillator's random phase) |
| `src/synthesis/effects/delay.cpp` | the `Style` enum is built from the control value clamped to `0 .. kNumStyles - 1` | UBSan: a value outside the enumerators is undefined (hit by the ported input-bounds test with a control value of 100000) |
| `src/synthesis/producers/spectral_morph.h` (`inharmonicScaleMorph`) | (a) the loop over the stretched partials stops at `dest_index >= last_harmonic` (was `> 2 * last_harmonic`); (b) `log2` of the harmonic index is taken of `max (index, 1)` | **bug fixes (Phase 3, found by the parameter sweep with random patches: NaN output at "Inharmonic Stretch" >= about 92 %)**. (a) partials stretched past the last harmonic were written up to `4 * last_harmonic` floats behind the start of the spectrum, over the table of shifted indices that lives behind it in the same buffer; (b) harmonic 0 gave `log2 (0) = -127`, `pow (mult, -12.7)` = -inf for large `mult`, an inf in that table, and the inverse transform returned NaN for some values of `mult`. Covered by `morph_never_gives_nan` in `synthyboy_test_vital_params`. |
| `src/synthesis/lookups/wavetable.h` | added `Wavetable::useExternalData (WavetableData*)` | lets the audio thread point an oscillator at a pre-built table (the adapter's 13 built-in tables) between two blocks without allocating or waiting for a lock: the oscillator already re-reads `current_data_` and its version number at the start of every block |
| `src/synthesis/framework/processor.h/.cpp` | added `Processor::reserveInputs (int count)` | appends `count` unconnected inputs, so that `plugNext` (which allocates when it finds no free input) never allocates when the modulation matrix wires a slot on the audio thread. The adapter reserves 64 per modulation destination |
| `src/synthesis/framework/processor_router.h` | added `ProcessorRouter::reserveWorkspace (int capacity)` | grows the three work queues of the dependency search (`getDependencies`, called by every `connect`) once, so that wiring a slot does not have to grow them |

| `src/synthesis/framework/utils.h/.cpp` (`RandomGenerator::next_seed_`) | `static int` -> `static thread_local int` | the counter gave every random generator of every engine its seed (`next_seed_++`, not atomic): a data race between engines built on different threads, and seeds that depended on how many generators earlier engines had made. The adapter sets the counter to 0 on the building thread before it constructs an engine (after the one process-wide table that also draws a number), so two engines of the same patch are identical, including the noise oscillator, random phase and random LFOs (`state_random_walk_saves_loads_and_renders_the_same`) |
| `src/synthesis/framework/futils.h` (`tanh`) | input clamped to `[-10.0f, 10.0f]` in both `poly_float` and `mono_float` overloads | **bug fix**: rational polynomial approximation overflowed float32 ($x^{40} \to \infty/\infty = \text{NaN}$) when extreme values entered during multithreaded modulation / filter feedback; covered by `synthyboy_test_threads` |
| `src/synthesis/lookups/lookup_table.h` (`cubicLookup`) | `boost` clamped to `[0.0f, resolution]`, `indices` clamped to `[0, resolution - 1]` | **bug fix**: Catmull-Rom parameter $t$ exited $[0.0f, 1.0f]$ on negative or out-of-range frequencies, leading to cubic extrapolation explosion; covered by `synthyboy_test_threads` |
| `src/synthesis/filters/ladder_filter.cpp` | `stage_scales_` initialized in `hardReset()` and default fallback added to `setStageScales()` | **bug fix**: unhandled filter styles (e.g. `kShelving` = 5) left stage scale coefficients uninitialized / undefined; covered by `synthyboy_test_threads` |
| `src/synthesis/modules/producers_module.cpp` | process remaining unprocessed oscillators when mutual FM/RM creates a dependency cycle | **bug fix**: mutually modulating oscillators (A modulates B and B modulates A) never became "ready" in the dependency loop and were skipped entirely, leaving uninitialized buffer contents; now processed using previous block's output |
| `src/common/synth_constants.h`, `synth_parameters.cpp`, `interface/look_and_feel/synth_strings.h` | `kNumLfos` 8 -> 10, `kNumMacros` 4 -> 8; parameters `macro_control_5..8`, `lfo_N_direction`, `lfo_N_host_lock`, `env_N_legato_inverted`, `osc_N_map_warp`; the style name "Rossler Attractor" | WP2/WP3 of `docs/ui-design/13-new-keys.md`: macros 5-8, LFO 9 and 10, LFO direction / host lock, envelope legato inverted, oscillator mapping |
| `src/synthesis/framework/voice_handler.h/.cpp` | `Voice`: per-note bend (`note_bend`), live retune (`tuning_delta`), per-note pressure; `VoiceHandler`: outputs `poly_aftertouch`, `retrigger_always`, `osc_mute(i)` / `osc_fold(i)` (oscillator mapping from key and velocity ranges), `setNoteTable` (tuned note of each key, read at note on, replaces the `Tuning` object), `retuneVoices`, `setNotePitchBend / Pressure / Slide`, `setChannelPitchBend` (semitones per MPE channel); **bug fix**: `Voice::clearSlideEvent` cleared the aftertouch event instead of the slide event | per-note expression (CLAP note expressions, MPE), the sources `poly_aftertouch`, the key/velocity mapping and the tuning table of the adapter |
| `src/synthesis/modules/synth_voice_handler.h/.cpp`, `synth_engine/sound_engine.h/.cpp` | registers `poly_aftertouch` as a modulation source, plugs `retrigger_always` into the envelopes and the mute / fold outputs into the producers, `correctLfoToTime (index, seconds)`, forwarding methods of the new voice handler calls | as above; the LFO sync clock per LFO (host mode) |
| `src/synthesis/modulators/synth_lfo.h/.cpp`, `modules/lfo_module.cpp` | inputs `kDirection` (forward, reverse, ping pong: applied to the position in the shape) and `kHostLock` (the phase of a synced LFO is re-anchored to the sync clock on every block) | `lfoN.direction`, `lfoN.host` |
| `src/synthesis/modulators/random_lfo.h/.cpp` | style `kRosslerAttractor`: Rossler system (a = b = 0.2, c = 5.7) by 4th order Runge-Kutta per voice and stereo lane, same constants, time scale and normalisation as `synthyboy::shapes::RosslerAttractor` | `rlfoN.style = Rossler attractor` |
| `src/synthesis/modulators/envelope.h/.cpp`, `modules/envelope_module.*` | inputs `kTriggerAlways` and `kLegatoInverted`: an envelope with the switch on retriggers on legato note ons | `envN.legato_inverted` |
| `src/synthesis/modules/modulation_connection_processor.h/.cpp` | input `kAuxInput` and the aux factor (`curve (aux) * aux_output`, multiplied into the amount of the connection), the legato glide of the source (velocity / note curves); the settings live in a `shared_ptr` shared by the per-voice clones of the processor | `modNN.aux_*`, `curve.*.legato` |
| `src/synthesis/modules/producers_module.h/.cpp`, `oscillator_module.*`, `producers/synth_oscillator.*` | per voice mute (levelled output always, raw output unless `osc_N_map_warp`) and fold (semitones added to the note in `SynthOscillator`) | `oscN.map.*` |
| `src/synthesis/producers/sample_oscillator.h/.cpp` (**new files**, our code in the fork, not in Vital) | the sample oscillator type of an oscillator slot: a `Processor` with the inputs of `SynthOscillator` (note, transpose, tune, unison, level, pan, phase, active voices, reset) and its two outputs; renders with the standalone package `core/sample-osc` (`SampleVoice`, one per unison voice of each of the two voices of a poly_float), `SampleOscShared` (settings and the sample view of one slot, shared by the per-voice clones like the wavetable) | `oscN.type`, `oscN.sample.*` (WP4) |
| `src/synthesis/modules/oscillator_module.h/.cpp` | creates the sample oscillator next to the wavetable one; `osc_N_type` enables exactly one of the two per block (both write the module's outputs); controls `osc_N_type`, `osc_N_sample_scan`, `osc_N_sample_uni_start`, `osc_N_sample_uni_span`, `osc_N_warp2_*`; new input `kNotePressed` | WP4 |
| `src/synthesis/modules/producers_module.h/.cpp`, `synth_voice_handler.*`, `synth_engine/sound_engine.*` | input `kNotePressed` (the voice's `note_pressed`, for the loop exit of the sample oscillator); the FM / RM sources are also given to the sample oscillator; an oscillator waits for the sources of BOTH warp slots before it runs; `getSampleShared (index)` down the chain | WP4 |
| `src/synthesis/producers/synth_oscillator.h/.cpp` | the second warp slot: inputs `kWarp2Type/Amount/Phase/Spread`; `VoiceBlock` and the per-unison arrays `distortion2_values_`; in `processDetuned` / `processCenter` the read phase of slot 1 goes through `warp2Phase ()` / `warp2Window ()` (a run-time switch over all 13 warp types, skipped entirely when slot 2 is None, so a patch with slot 2 off renders bit-identically) | `oscN.warp2.*` (WP4) |
| `src/common/synth_parameters.cpp` | osc parameters `type`, `sample_scan`, `sample_uni_start`, `sample_uni_span`, `warp2_type`, `warp2_amount`, `warp2_phase`, `warp2_spread` | WP4 |
| `CMakeLists.txt` of engine-vital | links `synthyboy_sampleosc` (the fork's lib no longer depends on nothing of ours: the standalone sample package) | WP4 |

### What the modulation matrix does on the audio thread (Phase 3 audit)

Vital wires a slot with `SoundEngine::connectModulation` / `disconnectModulation`, called by its (not imported) `SynthBase` on the
audio thread between blocks. Checked with a counting `operator new` (random sources, destinations and amounts, 3000 blocks, 10 seeds, then
`process_does_not_allocate_while_patches_change`): the only allocations were (1) `Processor::plugNext` creating an input when the
destination has no free one (fixed by `reserveInputs`), (2) the growth of the dependency queues in `ProcessorRouter::getDependencies`
(fixed by `reserveWorkspace`), (3) `ProcessorRouter::connect` creating a `Feedback` node when the connection closes a loop (a source that
depends on the destination). (3) cannot be pre-allocated (map nodes, vectors in several routers), so the adapter makes the engine's own
loop check (`isDownstream`) first and leaves such a slot unwired; it is tried again whenever the matrix changes. With these three measures
the matrix is allocation free in the tests. This is not a proof: the rule is "no allocation observed", not "none possible".


### Dropped functions, summary

* `Tuning`: file based loading (see above). `WavetableCreator::updateJson`: legacy migrations. `SoundEngine::process`: the
  denormal call. `effects_engine/`: removed. Nothing else was removed.
* Behaviour changed on purpose: the FFT, `allNotesOff (sample, channel)`, `allSoundsOff()` (rows above), base64 format of the JSON
  blobs. Everything else in the DSP is as imported.

### Upstream quirks noticed, not changed

* `WaveFrame::removedDc()` reads the DC offset from `frequency_domain[0].imag()`; every FFT layout used here stores DC in the real
  part, so the function subtracts zero and `WavetableCreator`'s `remove_all_dc_` has no effect. Not touched in this phase (no
  imported wavetable has a DC offset: the sine and saw used by the adapter are DC free); to be settled with the wavetable tools
  in Phase 3/4.
* (Phase 3: fixed, see the table: the seed counter is thread local and reset by the adapter.)
* The first note an engine plays differs slightly from all later ones (attack of the first note). The adapter plays two notes in
  `prepare()` so that the first note of the user is like every other.

### Imported but not compiled

* `third_party/kissfft/{COPYING,LICENSES}`, `LICENSE-GPL-3.0`: licence texts.
* Intel IPP branch of `fourier_transform.h` and the Accelerate branch (needs `-DSBVITAL_USE_ACCELERATE`): dead by default.

## Ported tests

`core/engine-vital/tests/synthesis/**` are upstream's `tests/synthesis/**` (72 files), translated mechanically for our runner:
`#include "JuceHeader.h"` -> `unit_test_shim.h` (a 60-line stand-in for `juce::UnitTest`: `beginTest`, `expect`,
`expectWithinAbsoluteError`), `vital::` -> `sbvital::`, `String` -> `std::string`, `static XTest x;` -> `SBVITAL_REGISTER_UNIT_TEST (XTest, x)`.
The test code itself is unchanged. They run in `synthyboy_test_vital_dsp` (35 test classes, 54 370 checks): note handler,
processor input-bounds tests of every filter / effect / modulator / producer, poly values and utils, matrix, circular queue,
wave frame (FFT round trip). Every upstream test under `tests/synthesis` is ported; classes whose body upstream left empty
(Fir/Iir Halfband Decimator, Synth Oscillator, Portamento Slope) run with 0 checks, as upstream.

Not ported (not in `tests/synthesis`, or dependent on removed/imported-out code):

| Upstream file | Reason |
|---------------|--------|
| `tests/main.cpp`, `tests/interface_tests.cpp`, `tests/interface/**` | JUCE application and GUI tests (`src/interface` is not imported) |
| `tests/stress_tests.cpp`, `tests/stress/**` | need `SynthBase`, `LoadSave`, presets and the GUI thread (not imported) |
| `tests/synthesis_tests.cpp` | unity include list of the ported files |
| `tests/JuceLibraryCode/**`, `tests/builds/**`, `tests/vital.jucer` | Projucer project |

## Licences of the third-party files in `upstream/third_party`

| Files | Licence | Source |
|-------|---------|--------|
| `third_party/kissfft/kissfft.h`, `COPYING`, `LICENSES/` | BSD-3-Clause (`SPDX-License-Identifier: BSD-3-Clause` in the header; `LICENSES/` also carries the `Unlicense` text used by KISS FFT's REUSE layout) | https://github.com/mborgerding/kissfft, as shipped in the Vital tree at 636ca0e |
| `third_party/json/json.h` | MIT, nlohmann/json 3.1.2, Copyright (c) 2013-2018 Niels Lohmann (header: "Licensed under the MIT License") | https://github.com/nlohmann/json, as shipped in the Vital tree |
