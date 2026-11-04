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
| `src/synthesis/**` | 147 | 24 952 | framework, producers, filters, modulators, effects, effects_engine, modules, synth_engine, lookups, utilities |
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
| `#include "JuceHeader.h"` -> `#include "vital_compat.h"` (21 files) | JuceHeader.h only provided standard headers and the leak-detector macros. `compat/vital_compat.h` provides exactly those (`JUCE_LEAK_DETECTOR(x)` = nothing, `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x)` = deleted copy operations). The macro uses themselves (112 + 3) are unchanged. |
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
| `src/synthesis/framework/poly_values.h` | (a) SSE2 detection also accepts MSVC (`_M_X64`, `_M_IX86_FP >= 2`; MSVC does not define `__SSE2__`); (b) the NEON `div()` uses the existing Newton-Raphson branch (`vrecpeq_f32` + 2x `vrecpsq_f32`) on every target except AArch64 (condition was `NEON_ARM32`, which no build defined) | `vdivq_f32` exists on AArch64 only. This was the only A64-only intrinsic of the whole tree (all other NEON intrinsics used are ARMv7). **Unverified on real ARMv7 hardware**; compiled with GCC 12 for `armv7-a -mfpu=neon` and run under QEMU user mode (see tests). |
| `src/common/wavetable/wave_line_source.h` (+`line_generator.h`), `src/synthesis/filters/phaser_filter.h` (+`futils.h`), `src/synthesis/modulators/synth_lfo.cpp` (+`futils.h`), `src/synthesis/modules/envelope_module.h` (+`envelope.h`), `equalizer_module.h` (+`memory.h`), `src/synthesis/effects/reverb.h` (+`memory.h`), `src/common/synth_parameters.cpp` (+`operators.h`) | added the `#include` the file needs itself | upstream compiles these as unity builds in a fixed order and never needed them; compiling each `.cpp` on its own does |

### Dropped functions, summary

* `Tuning`: file based loading (see above). `WavetableCreator::updateJson`: legacy migrations. `SoundEngine::process`: the
  denormal call. Nothing else was removed; no other behaviour was changed.

### Imported but not compiled

* `third_party/kissfft/{COPYING,LICENSES}`, `LICENSE-GPL-3.0`: licence texts.
* Intel IPP branch of `fourier_transform.h` and the Accelerate branch (needs `-DSBVITAL_USE_ACCELERATE`): dead by default.
