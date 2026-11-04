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

Filled in by the follow-up commits (JUCE removal, namespace, ARMv7).

| File | Change | Reason |
|------|--------|--------|
