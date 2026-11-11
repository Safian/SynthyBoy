# SynthyBoy – architecture

Status: design, written 2026-11-04 on top of the verified spike (`403ea61`, VST3 + CLAP from one JUCE code base).
Items marked **(unverified)** are assumptions that must be checked before they are relied on;
items marked **(owner)** are decisions that belong to the project owner.

SynthyBoy is a Serum-class wavetable synthesizer built on the source of [Vital](https://github.com/mtytel/vital)
(`mtytel/vital`, GPL-3.0), released under **AGPL-3.0-or-later** under its own name. It is the first
own plugin of the Kick ecosystem (Kick DAW/sampler, Akai MPC Key 61, mobile app), but it is developed,
built, tested and released **without any Kick source**.

## 1. Requirements that shape everything

| # | Requirement | Consequence |
|---|-------------|-------------|
| R1 | One code base → **VST3 and CLAP** | JUCE shell + clap-juce-extensions (spike-proven) and a native CLAP shell (§4) |
| R2 | Runs on **Akai MPC Key 61** (Linux ARMv7 hard-float, 2 isolated cores), **iOS/Android** (inside the Kick app), **macOS, Windows, Linux x86_64/AArch64** | DSP core is a platform- and UI-independent static C++17 library; NEON + SSE2 only, no x86-only code; quality tiers (§3.6) |
| R3 | No global state, no allocation or locks on the audio thread | Engine is an object; allocation audited in tests (§3.7) |
| R4 | Parameter description and preset format must map onto **Kick's own UI**, not only a native plugin GUI | One parameter table (`ParamInfo`) in the core is the single source for plugin params, preset I/O, Kick UI and docs (§5) |
| R5 | Large content (wavetables, presets) is a separate **ContentPack**: downloadable at install, deletable, restorable; the plugin never crashes with content missing | §6 |
| R6 | "Download-ready" model: built-in and downloaded plugins behave identically (`PluginManifest`, `StaticPluginLoader`/`DynamicPluginLoader` on the Kick side) | Same source builds as a static library **and** a dynamic `.clap`; manifest ships in this repo (§7) |
| R7 | Never a half-solution (owner rule) | Every phase in §9 ends in a tested, validated, shippable state; no "for now" branches |

## 2. Building on Vital

### 2.1 How the source is taken over

Options considered:

| Option | Verdict |
|--------|---------|
| GitHub fork tracking upstream | No. The public repo has one author and one commit line, is "updated on a delay after binary releases" and takes no pull requests (README). Last public commit is `636ca0e` (2022-04-20). There is nothing to track, and a fork would keep Vital's JUCE-6/Projucer build structure, UI and web-service code. |
| Git submodule of `mtytel/vital` | No. We need to change the files (JUCE removal, namespace, ARMv7 fixes), and the repo ships `third_party/JUCE`, `firebase_cpp_sdk` and `VST_SDK` we must not pull in. |
| **One-time source take-over into this repo** | **Chosen.** Selected directories are copied in a single, clearly labelled commit (`Import Vital 636ca0e (pristine)`, byte-identical to upstream), recorded in `docs/UPSTREAM.md` (upstream commit, file list, what was dropped and why). Later changes are our own commits. If upstream publishes something worth having we cherry-pick by diff, with the same record. |

What is taken (first import, from the `636ca0e` checkout, ~25 k lines under `src/synthesis` plus the
wavetable model):

- `src/synthesis/**` (framework, producers, filters, modulators, effects, modules, synth_engine, lookups, utilities)
- `src/common/wavetable/**`, `src/common/line_generator.*`, `src/common/synth_parameters.*`, `synth_constants.h`, `synth_types.*`, `tuning.*`, `fourier_transform.h`
- `third_party/kissfft` (BSD-3), `third_party/json` (MIT) – licences verified from the file headers at import time and recorded in `THIRD_PARTY.md` and `UPSTREAM.md`
- `src/interface/look_and_feel/synth_strings.h` – the one exception to "nothing from `src/interface`": a table of choice names (`std::string` constants, no GUI code) that `synth_parameters.cpp` includes (see `UPSTREAM.md`)

What is **not** taken, and must not be:

- `src/interface/**` (Vital's OpenGL GUI: tied to Vital's look and to JUCE OpenGL; we write our own UI, §8)
- `src/common/authentication.h`, `startup.*`, anything that talks to `vital.audio`, `account.vital.audio`, `store.vital.audio`, `firebase_cpp_sdk`
- `third_party/JUCE`, `third_party/VST_SDK` (we use our own pinned JUCE and the SDK bundled with it)
- **Presets, wavetables, skins and tunings shipped with Vital** (`default.vitalskin`, `tunings/`, anything from the free/paid preset banks): separate licence, no redistribution

Each imported file keeps its original copyright/licence header (Matt Tytel, GPL-3.0). Our modifications and new
files carry `Copyright (C) 2026 Safian Szabolcs` + `SPDX-License-Identifier: AGPL-3.0-or-later`. The repo has
a top-level `NOTICE` that names Vital and its author.

### 2.2 Licence hygiene

- **GPLv3 → AGPLv3:** GPLv3 §13 explicitly permits linking/combining GPLv3 code with AGPLv3 code; the combined
  work is then governed by AGPLv3 for the combined parts, and Vital's GPLv3 text applies to its original files.
  The repo's `LICENSE` is AGPL-3.0; `NOTICE` and `THIRD_PARTY.md` state which files are GPL-3.0-derived.
  The header of every imported Vital file reads "GNU General Public License ... either version 3 of the License, or (at your
  option) any later version" (checked for all of them at import), so they are **GPL-3.0-or-later**; the combined work is
  AGPL-3.0-or-later. Imported files keep their header unchanged; files we changed add one line
  (`// Modified for SynthyBoy (AGPL-3.0-or-later combined work), 2026`), new files carry the SPDX line.
- **JUCE** is used under its AGPLv3 option (pinned `8.0.17-security`). The commercial JUCE licence cannot be mixed in.
- **Source offer:** AGPL §13 – anyone who gets the binary gets the source. Every release links to the matching tag;
  Kick (also AGPL) ships the same link in its About/Plugin-manager screen.
- **Upstream's restrictions (README, quoted as rules for this project):**
  1. Do not use the names "Vital", "Vital Audio", "Tytel", "Matt Tytel" to name or market our builds. We may state factually, in `NOTICE`/About, that the engine is derived from Vital.
  2. Do not connect to `vital.audio`, `account.vital.audio`, `store.vital.audio` from our builds. The plugin has **no network code at all**; downloads are done by the host (Kick) or the installer, never by the plugin.
  3. Do not distribute Vital's bundled presets.
  4. **Do not distribute on the iOS App Store without paying for a GPLv3 exception** (`licensing@vital.audio`).
     **Owner decision (2026-11-04): own iOS engine.** No Vital-derived code goes into any iOS build (and into Android builds where
     that is practical). iOS gets an engine written from scratch behind the same `Engine` interface (§3.1). To make that real and
     auditable:
     - Vital-derived code lives **only** in `core/engine-vital/` (+ its `third_party/`), selected by the CMake option
       `SYNTHYBOY_WITH_VITAL` (default ON; **forced OFF** by the iOS toolchain/CI target; a CI job greps the iOS link line and the
       static library's symbols for Vital-derived objects and fails if any is present).
     - Everything outside `core/engine-vital/` (the `Engine` interface, `ParamInfo`, state/preset formats, content-pack code,
       shells, UI, tools that ship in binaries) is **our own code with no Vital-derived content**. In particular the `ParamInfo`
       table is authored by us and is *not* generated from Vital's parameter table; `engine-vital` contains an adapter that maps
       our parameters onto Vital's internals.
     - The own engine (`core/engine-own/`, always built) starts as the spike's engine, which is original code, and grows by
       clean-room discipline: no copying from `engine-vital/`; the sound contract is the `ParamInfo` semantics plus golden/measurement
       tests, not code similarity. It will not sound identical to the Vital engine, and presets must say which parameters are
       engine-specific (a flag in `ParamInfo`).
     - Content is data authored by us (§2.3). Offline authoring tools may use Vital-derived code on a developer machine; the data
       they produce is ours, and no Vital-derived code ships in the iOS binary.
     Android: the same switch applies; whether it is used there is decided when the Android build exists.
- **Trademark / name:** the product is called **SynthyBoy** (owner decision 2026-11-04; "VST" is Steinberg's trademark and not part of the name, only the compatibility statement "VST is a registered trademark of Steinberg Media Technologies GmbH" is used). Name screening and the open clearance steps are in [NAMING.md](NAMING.md) **(owner: official registers not yet searched)**. Steinberg's VST3 SDK 3.8 is MIT; no separate VST3 licence agreement is needed, the VST3 logo is not used.
- **Trade dress:** the UI is original; it does not copy Vital's layout, colours or graphics.

### 2.3 Own factory content, free licence

- Factory presets and wavetables are **authored by us from scratch** (or generated by scripts in `tools/`, committed, reproducible) and released under **CC0-1.0** (alternatively CC-BY-4.0 where a third-party work is a base; each pack states its licence in the manifest, §6).
- No preset is created by loading, editing and re-saving a Vital preset. No Vital wavetable is used as a starting point.
- Each preset file has `license` and `author` fields; CI rejects a factory preset without them.
- Wavetables are produced with the take-over's own wavetable model (`WavetableCreator` components: wave source, line source, warp, fold, phase, slew, window, frequency filter, Shepard tone, file import) from original sources (generated waveforms, spectra, our own recordings).

## 3. Sound engine (Serum-class)

### 3.1 Layering and the `Engine` interface

```
core/                  synthyboy_core   static lib, C++17, no JUCE, no OS API, no globals
  include/synthyboy/   Engine interface, ParamInfo, StateIO, Quality, events, EngineCaps, ContentStore (public API)
  src/                 params (1484 keys), state (JSON, versioned, v3), content section, content store, engine glue
  engine-own/          our engine (spike engine; always built; the only engine on iOS)
  engine-vital/        Vital-derived DSP + adapter (optional: SYNTHYBOY_WITH_VITAL, OFF for iOS)
  tuning/              .scl / .kbm / .tun parsers, 128-key table, the MTS-ESP seam (ITuningSource; the library is not linked)
  formula/             wavetable formula compiler (bytecode) and frame renderer (grammar: formula/GRAMMAR.md)
  shapes/              custom LFO shapes and the velocity / note curves (point lists, baked tables)
  wavetable-edit/      wavetable data model, drawing / spectral tools, undo, audio / image import, WAV export (UI-free)
  wavetable-render/    clean-room renderer for the ten Vital component types (used by the importer)
  vital-import/        .vital preset / .vitaltable / .vitallfo importer (730-row control table)
  sample-osc/          sample oscillator DSP and the WAV reader (loop modes, slices, scan, unison)
  filters-serum/       the ~35 additional filter types (real DSP, names final after listening)
  fx-serum-a/, fx-serum-b/   Hyper / Dimension, Utility, splitters; Bode (frequency shifter), Convolve (partitioned FFT)
  tests/               unit/RT/thread/fuzz tests (own tiny runner, no framework dependency)
shell-juce/            JUCE AudioProcessor -> VST3 + CLAP (clap-juce-extensions); ui/ = the editor (section 8)
shell-clap/            native CLAP entry on core only (headless; for MPC/mobile/static)
bench/, tools/         synthyboy_bench; tools/paramdump (docs/params.json); tools/preset-render (factory preset gate)
cmake/toolchains/      armv7-linux-gnueabihf, aarch64-linux-gnu, (ios, android later)
```

`Engine` is the replacement seam: both the Vital-derived DSP (`engine-vital`) and our own engine (`engine-own`) sit behind it. The shells and UI never include Vital headers.

Take-over adaptations (all in the import's follow-up commits, each with a test):

1. **JUCE removal from the core (done).** `src/synthesis` used JUCE only for `JuceHeader.h` (standard headers), `JUCE_LEAK_DETECTOR` (112 uses, now a no-op macro in `compat/vital_compat.h`), `FloatVectorOperations::disableDenormalisedNumberSupport` (removed; the adapter sets flush-to-zero around `process()`), `Base64` for the JSON blobs, and `String`/`File`/`StringArray` in `Tuning` (file loading dropped, the line parsers kept on `std::string`). Its random numbers were `std::mt19937` already (no `juce::Random`). No file access remains in the core.
2. **Namespace** `vital` → `sbvital` (mechanical, done), to avoid any confusion of the binary's symbols with Vital's own; the name differs from our own `synthyboy` namespace so that Vital-derived symbols are recognisable (`scripts/check-no-vital.sh` greps for `sbvital`, `Vital`, `vital::`).
3. **ARMv7 NEON (done in code, GCC 12 `armv7-a -mfpu=neon` cross build and QEMU run; real ARMv7 hardware not yet).** `poly_values.h` has SSE2 and NEON paths (no scalar path: none was invented). The NEON path used one A64-only intrinsic, `vdivq_f32`; on ARMv7 it is `vrecpeq_f32` + two Newton–Raphson `vrecpsq_f32` steps (upstream already had that branch behind an undefined macro; the condition is now `!defined(__aarch64__)`). MSVC x64 is accepted as SSE2 (MSVC does not define `__SSE2__`). Compile flags for ARMv7 hard-float: `-mfpu=neon-vfpv4 -mfloat-abi=hard -mcpu=cortex-a?` (exact CPU of the MPC Key 61 to be read from the device, not guessed **(unverified)**); the toolchain file uses generic `armv7-a -mfpu=neon`.
4. **Real-time audit:** Vital's constructor-time `new` is fine; wavetable loading/replacement, sample loading and preset load must hand the audio thread a pre-built object via lock-free swap (single-slot atomic pointer + deferred free on the message/worker thread). §3.7 describes the test.
5. **Build** as plain CMake (the repo's), no Projucer.

### 3.2 Oscillators

Baseline (Vital): 3 wavetable oscillators + sample oscillator + noise, per-oscillator unison (up to 16 voices), band-limited
wavetable playback (mip-mapped per-note-frequency), frame interpolation (none/linear/spectral), 16 distortion/warp phase
modes ("spectral warp": spectral morph, FM, ring-mod, sync, formant, bend, quantize, fold, …), stack/random phase, spread, detune, level, pan, transpose, tune.

Serum-class additions (each a phase item in §9, none a stopgap):
- Wavetable length 2048 samples × up to 256 frames (as Serum); import of single-cycle wav / multi-cycle wav with automatic cycle detection (existing `pitch_detector`) and slice by size.
- **Per-oscillator spectral processing at playback** (existing spectral morph) with a published list of modes and ranges, tested for aliasing (below).
- Unison with selectable **blend law** and **stereo spread law**, per-voice random phase seeded deterministically (reproducible offline render).
- **Alias budget test:** a sweep (20 Hz–20 kHz) through each oscillator/warp mode must stay under a documented alias level (e.g. > 80 dB below the fundamental at 44.1 kHz, 1× oversampling off) in the test suite; thresholds are set from measurements of the imported code and then guarded.

### 3.3 Wavetable editor

- **Data model** (core, UI-free, `core/wavetable-edit`): `Wavetable` of up to 256 frames x 2048 samples with the drawing and spectral tools, morph between keyframes, undo (a stack of states), audio import (by frame size, pitch detection, FFT 256 .. 2048), image import, WAV export; the formula renderer (`core/formula`) feeds it. The renderer of the ten Vital component types (`core/wavetable-render`, clean room) builds the tables of imported presets, so editor, importer and offline generator share one code path.
- **Editor UI** (`shell-juce/ui`, §8): wavetable view and editor sheet with the tool strip, 44 pt touch targets, undo / redo through the core stack; frame list, waveform and spectrum views, draw tools, per-frame spectral edit, import and preview.
- **Mobile/MPC: the full editor is available there too** (owner decision Q7, `docs/ui-design/12-decisions.md`; this resolves the former "no editor on mobile" rule). The editor is part of the shared UI kit (touch-first, §8); the core package has no UI and no platform API, so every shell can host it. Custom wavetables are user content in the content store (below and §6), not embedded in the state.
- Wavetables are **content**, stored in ContentPacks / the user store (§6) and referenced by content id + SHA-256 from the state's `content` section (Q8). The file format today is **WAV** (a length that is a multiple of 2048 is that many frames, anything else is one cycle resampled to 2048); the `.sbwt` container planned earlier does not exist. The engine also synthesises the 13 built-in tables (Sine .. Bell Partials) so it always makes sound with zero content installed; a missing reference plays the saw.

### 3.4 Filters

Baseline from Vital: analog (Sallen-Key-style), dirty, ladder, digital SVF, diode, formant, comb, phaser filter, with blend and keytrack, 2 filters in series/parallel/split. These are already coded (`src/synthesis/filters`). Serum-class additions: oversampled drive stage (tier-dependent), filter-model specific resonance compensation tests (cutoff sweep, self-oscillation threshold documented and guarded), cutoff modulation at audio rate within the mod matrix, optional **filter-FM**.

### 3.5 Modulation and effects

- **Mod matrix:** Vital's connection model (source → destination, amount, bipolar, stereo, power/curve, bypass, 64 slots), addressed through `ParamInfo`: each slot is seven parameters (`modNN.source` / `destination` / `amount` / `power` / `bipolar` / `stereo` / `bypass`), the source a stepped choice over core's `ModSource` table (28 sources: 6 envelopes, 8 LFOs with a built-in list of 12 shapes, 4 random LFOs, 4 macros, velocity, note, aftertouch, pitch wheel, mod wheel, random per note), the destination a stepped choice over every parameter flagged `kParamModulatable` (269), so Kick UI, CLAP and the state format see the same ids. Free-form LFO shapes, "lift" and "slide" sources are not exposed (docs/PARAMETERS.md). Wiring a slot on the audio thread allocates in the imported engine in three cases that the adapter removes (docs/UPSTREAM.md); a slot that would close a loop stays unwired.
- **FX chain:** reorderable chain from Vital (chorus, compressor, delay, distortion, EQ, filter FX, flanger, phaser, reverb), with a per-effect CPU class so a tier can drop heavy effects (§3.6).
- **Unison/voice:** voice manager with legato/portamento/voice stealing (oldest or lowest), polyphony 1–32 (tier-capped), note-id tracking for CLAP (§5).

### 3.6 Quality tiers (platform scaling)

A `Quality` struct (not a global) is passed to `Engine::prepare`/`setQuality`:

| Parameter | High (desktop) | Medium (phone) | Low (MPC ARMv7) |
|-----------|----------------|----------------|------------------|
| Max voices | 32 | 16 | measured on device, start 8 |
| Max unison voices per osc | 16 | 8 | 4 |
| Oversampling | 2× (4× offline render) | 2×/1× | 1× |
| Wavetable interpolation | spectral | linear | linear |
| Heavy FX (reverb, flanger) | all | all | reduced reverb quality |
| Control rate | 1/16 sample ~ as Vital | same | 1/32 |

Numbers in the Low/Medium columns are **targets, not claims**: nothing is promised until measured on an MPC Key 61 and a mid-range phone (benchmark harness: `bench/` renders a fixed set of presets/notes for N seconds and prints real-time factor and worst block time). Presets store the *requested* settings; the tier only caps them, so a preset sounds the same shape on every device, just cheaper.

### 3.7 Real-time rules and tests

- No `new/delete/malloc`, no `std::mutex`, no file or syslog calls in `Engine::process`. Unit test links a `malloc` hook and fails on any allocation during `process`, for each tier and for state/preset changes during playback.
- ThreadSanitizer and AddressSanitizer/UBSan builds of the core tests in CI (Linux x86_64).
- Determinism test: same preset + same notes → bit-identical output on repeated runs, and within a tolerance across SIMD/scalar paths.
- Denormals: handled in the shell (`ScopedNoDenormals` / MXCSR / FPSCR flags) and by the core's own flush where recursion exists.

### 3.8 Engines and capability bits

Two engines sit behind `IEngine` (`createEngine (EngineKind::own | vital)`). Since state version 3 the parameter table is larger than what an engine can play, so an engine reports what it implements: `IEngine::capabilities ()` returns an `EngineCapsMask` (`core/include/synthyboy/engine_caps.h`, 26 bits, one per area of the work packages: event contract, macros 5 .. 8, LFO 9 / 10, extra mod sources, aux source, LFO options, envelope beat units, curves, custom LFO shapes, key / scale, tuning, MPE, oscillator mapping, sample oscillator, user wavetable, second warp slot, mixer graph (buses, DIRECT), FX racks, filter extras, filter models, the new effects, reverb force). `requiredCaps (paramIndex)` / `requiredCapsForValue` say which bits a key or a choice entry needs; `supportsParam` is false and the UI dims the control while a bit is missing. **A bit is set only together with a test that renders the key off and on and asserts a measurable difference.** The Vital-derived engine sets all bits; `engine-own` (the only engine on iOS) sets the event contract only, accepts and stores every other key and ignores it. Per-bit tests: `docs/ui-design/WP123-REPORT.md`, `WP48-REPORT.md`.

- **Caps of a tier** (`Quality`): voices, unison, oversampling, interpolation, `fxMask` (bits 0 .. 15, one per effect module; tier low bypasses reverb, flanger, Hyper / Dimension, Bode, Convolve), and `realtime` (default false = offline, deterministic: the audio block that posts a Convolve rebuild waits for the builder thread and takes the result over in that very block; true = live, the audio thread never waits). The shells set it from the host (live playback true, an offline bounce false). Tier numbers: section 3.6, `docs/QUALITY.md`.
- **Content store** (`content_store.h`): `IContentStore::readFile (ContentFileRef, bytes)` checks the SHA-256 of a reference; `MemoryContentStore` (tests, static builds) and `DirectoryContentStore` (`<root>/<pack>/<file>`). `IEngine::setContent (StateContent)` / `setContentStore ()` run on the message thread: the files are decoded there into a snapshot that the audio thread takes over through an atomic pointer without a lock; retired snapshots are freed by the message thread only (tested under the counting `operator new` and ThreadSanitizer).
- **State version 3** (section 5.2): the `content` section is additive and bounded (caps in `content_state.h`, `validateContent`); a section outside the caps is dropped as a whole (`LoadResult::content = rejected`) while the parameters still apply; `loadState ()` stays allocation-free and skips the section, `loadStateWithContent ()` allocates for it.
- **State version 4** (REFCOMPARE round 3): the ranges of the EQ gains and of the compressor thresholds and gains grew and `compressor.limit` has three values; the pool of effect instances (`fxiNN.pKK`) stores normalised values, so a version 3 document is rescaled on load (`migrateV3ToV4`, `fxPoolMigrateFromV3`) and sounds as before; rack presets of version 1 likewise.

## 4. Plugin shells and formats

| Shell | Used by | Formats | Notes |
|-------|---------|---------|-------|
| **JUCE shell** (`shell-juce/`) | DAW users on macOS/Windows/Linux | VST3 + CLAP (via clap-juce-extensions, pinned commit) | Proven in the spike: clean build ~40 s on macOS arm64. Hosts the JUCE GUI. |
| **Native CLAP shell** (`shell-clap/`) | Kick on MPC (ARMv7), Kick mobile (static), Kick desktop | CLAP only, `clap.h` + `core` | No JUCE, no X11/ALSA/OpenGL dependencies: the plugin is loadable in a headless host. Exposes `clap_entry` (dynamic `.clap`) **and** `synthyboy_clap_get_entry()` (static link, §7). |

Why two shells: JUCE's Linux build pulls in X11/ALSA/freetype/fontconfig and its CLAP wrapper supports X11 only (not Wayland); on the MPC and in a mobile app the host provides audio and UI, so a JUCE-free shell is both smaller and safer. Both shells expose the same `ParamInfo` table and read/write the same state format, which a **parity test** checks (parameter ids, ranges, defaults, state round-trip in both shells). The JUCE shell can later be replaced by the native shell plus a thin GUI layer if the parity holds; that is not required for this plan and not decided.

### 4.1 CLAP extensions

| Extension | Plan |
|-----------|------|
| `clap.audio-ports` | 1 stereo output (+ mono option); optional second output pair (per-oscillator or FX-bus out) reserved in the port table |
| `clap.note-ports` | 1 input port, dialects `CLAP` and `MIDI` (MPE-ready); no output |
| `clap.params` | Every `ParamInfo` entry, **stable numeric ids** (never reused or reordered; new ids only appended), flags: automatable, modulatable, `IS_MODULATABLE_PER_NOTE_ID` for pitch/level/pan/filter cutoff, stepped for enums, hidden for internal |
| `clap.state` | Versioned binary/JSON blob from `StateIO` (§5.2); `clap.state-context` (preset vs. project) honoured |
| `clap.preset-discovery` | Factory-preset provider reading ContentPack presets from the installed location; empty provider (not a failure) when the pack is missing |
| `clap.preset-load` | Load by location/`load_key` |
| `clap.voice-info` | Reports `voice_count`/`voice_capacity` of the active tier; overlapping notes supported |
| `clap.gui` | Native shell: **floating or embedded** via host parent window (Cocoa, Win32, X11); JUCE shell: JUCE editor wrapped by clap-juce-extensions (X11 only on Linux, Wayland unsupported – documented). Not used on MPC/mobile: those use Kick's own UI |
| `clap.latency` | 0 samples unless a linear-phase oversampler is enabled (then reported) |
| `clap.tail` | Computed from release times and effect tails |
| `clap.thread-check`, `clap.timer-support`, `clap.posix-fd-support` | Required for the native GUI on Linux |
| `clap.note-name`, `clap.remote-controls` | 8 macro pages: name = page of 8 `ParamInfo` entries chosen per section; this is also what Kick's hardware surface uses |
| `clap.render` | Offline mode switches to the High tier + 4× oversampling |
| `clap.context-menu`, `clap.track-info` | Later, not required |

Which of these `clap-juce-extensions` already implements at the pinned commit is **(unverified)**; the clap-validator run (§10) is the arbiter, and anything missing is provided by the native shell.

### 4.2 The bitmap GUI of the native module

The native module (`shell-clap`, `synthyboy.clap`) can render the editor of `shell-juce/ui` itself, with JUCE's software renderer into a pixel buffer, and offer it through the generic CLAP extension
`com.safian.bitmap-gui/1` (`shell-clap/bitmap_gui_ext.h`, spec `BITMAP-GUI-EXTENSION.md`, design record `ui-design/13-new-keys.md` 4.45, report `ui-design/BITMAPGUI-REPORT.md`). A host that cannot or must
not run the plug-in's toolkit (Kick on the Akai MPC, a framebuffer; a host that links another version of JUCE) copies the dirty rectangles and forwards touch / mouse / key input. The module links its own JUCE 8.0.17
statically with hidden symbols and exports `clap_entry` and `synthyboy_clap_get_entry` only (checked by `scripts/check-clap-exports.sh`). The GUI never touches the audio thread: edits travel through the lock free
`UiLink` (`shell-clap/synthyboy_ui_link.h`) and are handled like host automation in `process ()` / `params.flush ()`. It is a CMake option, `SYNTHYBOY_BITMAP_GUI` (default ON with the JUCE plug-in, OFF for the static
library: the iOS rule of 2.2 is unchanged; linking this module's JUCE 8 into an iOS binary that also contains Kick's JUCE 7 needs `JUCE_NAMESPACE`, an open point, `14-morning-questions.md` 110).

## 5. Parameters, state, presets (UI-independent)

### 5.1 `ParamInfo`

One constexpr table in the core:

```
struct ParamInfo {
  uint32_t id;          // == hashKey (key); stable, appended only
  const char* key;      // lowercase dotted key, e.g. "osc1.unison.detune"; used in files, never renamed
  const char* name;     // display name, unique in the table ("Osc 1 Unison Detune")
  const char* group;    // hierarchy "Osc 1 / Unison" (-> CLAP module path, Kick page, JUCE parameter group)
  float min, max, def, skew, interval;
  Scale scale;          // linear | exp_skew | stepped(choice)
  Unit unit;            // none | dB | Hz | ms | percent | semitone | cents | beats | degrees | voices
  uint32_t flags;       // automatable | modulatable | per_note | hidden | stepped | engine_specific
  const char* const* choices; uint16_t numChoices;  // names of a stepped parameter ...
  ChoiceSet choiceSet;  // ... or: none | list | modSource | modDestination (names generated from core's tables)
  uint8_t decimals;     // digits of the display text
  uint16_t order;       // display order (table position)
};
```

The table is our own design (core/src/param_table.cpp, 1484 entries since state version 3, 880 of them in state version 2 and unchanged in id, position and sound; constexpr, generated per group with macros; docs/PARAMETERS.md lists it by section). From it the build
generates: the JUCE parameter layout (grouped), the CLAP `params` info, the Kick UI parameter list (`docs/params.json`, written by `synthyboy_paramdump`; CI checks that it is
current), and the parameter chapter (docs/PARAMETERS.md, `scripts/gen-params-doc.py`); the preset schema follows in Phase 4. Display-name/value text functions live in the core, so a hardware knob
and the plugin GUI show the same string.

### 5.2 State and preset format

- **Versioned.** `{"format":"synthyboy-state","version":N, ...}` (N = 3 since WP0 of `docs/ui-design/13-new-keys.md`: all 1484 parameters by key, deterministic text of about 29 KB, limit 1 MiB, plus the additive bounded `content` section for what is not a parameter: references to wavetables / samples / impulses, custom LFO shapes, curves, a tuning file; N = 2 had 880 parameters); loader accepts all older versions through explicit migrations (version 1: gain, waveform, cutoff -> `master.volume`, `osc1.wavetable`, `filter1.cutoff` + `filter1.on`, tested with the literal documents of the old code; version 2 -> 3: nothing renamed, the new keys keep their value, `fx.order.10..16` are set to the identity); unknown *future* versions load what they can and warn (never crash).
- Parameters stored by **key** (string), not index; a renamed parameter has a migration alias.
- Mod matrix stored as a list of `{source, destination key, amount, bipolar, stereo, power, bypass}`.
- Wavetables referenced by **content id + SHA-256** (from a ContentPack) or **embedded** (custom, user-made; compressed). A reference to a missing wavetable falls back to the built-in sine/saw table and sets a `missing_content` flag the host can show ("Restore"); sound is produced either way.
- Two containers, one schema: `.sbpreset` (single preset, JSON + optional embedded wavetables, `license`/`author`/`tags`/`category`) and the plugin state blob (same JSON, binary-wrapped when large). Kick stores a **plugin reference + version + state** in projects, as in the platform plan.
- Preset discovery (CLAP) and the Kick preset browser read the same metadata without loading the audio engine.

## 6. Content packs

A **ContentPack** is a directory/zip with `contentpack.json` + data files, described in the plugin manifest (§7):

```
{ "id": "com.safian.synthyboy.factory-wavetables", "version": "1.0.0",
  "schemaVersion": 1, "license": "CC0-1.0", "attribution": "...",
  "size": 12345678, "sha256": "…", "files": [{ "path": "...", "sha256": "..." }],
  "kind": "wavetables" | "presets" | "samples", "requires_plugin": ">=0.2.0" }
```

Rules: the plugin never downloads anything; it **looks up** installed packs (location from the host via the CLAP preset-discovery/`state` context or a standard per-platform folder; Kick passes the path explicitly in the static case) and **tolerates absence** (built-in minimal wavetables, built-in "Init" preset, `missing_content` flag). Delete / restore is a file operation owned by the host. Packs are versioned independently from the plugin; the loader checks `requires_plugin` and `schemaVersion` and ignores incompatible packs with a log message rather than failing. A pack is verified by SHA-256 before use (the host verifies the signature; the plugin only checks hashes of what it loads). Factory content is built from `content/` sources in this repo by `tools/`, in CI, into release assets (GitHub assets are limited to 2 GiB each; packs are separate from binaries).

### 6.1 Content store and user content

Content that is not a parameter is addressed by **pack id + file + SHA-256** (`ContentFileRef`), never embedded in the state (Q8): wavetables (WAV), samples, impulse responses for Convolve, filter curves, a tuning file; short things (custom LFO shapes as point lists, curves, a scale up to 64 KiB) live inline in the `content` section of the state. A preset shared with a user is packaged together with the tables it references (a ContentPack). The factory pack `content/presets` (100 presets, `contentpack.json` with SHA-256 per file) is verified by `verifyContentPack`. Tuning: the `.scl` / `.kbm` text is stored inline; MTS-ESP is a seam only (`ITuningSource`, `master.tuning.mts_esp`): no MTS-ESP code is linked (THIRD_PARTY.md).

## 7. Manifest and loading ("download-ready")

`plugin.json` in the repo root (schema in `schema/plugin-manifest.schema.json`, validated in CI by `scripts/check-manifest.py`), consumed by the Kick-side `PluginManifest` (`Kick/clap_host`):

```
{ "schemaVersion": 1, "id": "com.safian.synthyboy", "name": "SynthyBoy", "version": "0.1.0",
  "kind": "instrument", "author": "…", "description": "…", "license": "AGPL-3.0-or-later", "homepage": "…", "min_host_version": "…",
  "clap_id": "com.safian.synthyboy",   // must equal CLAP_ID in CMakeLists.txt (checked by scripts/check-manifest.py)
  "builds": { "linux-armv7": {"status":"unbuilt","url":"","sha256":"","size":0}, "linux-aarch64": {…}, "linux-x86_64": {…},
              "macos-universal": {…}, "windows-x64": {…} },
  "static": { "ios": {"status":"planned","library":"libsynthyboy_static.a"}, "android": {…},
              "entry": "synthyboy_clap_get_entry" },
  "contentPacks": [ {"id","name","version","kind","license","required":false,"recommended":true,"path":"content/presets",
                     "status":"unbuilt","url":"","sha256":"","size":0} ] }
```

The checked-in file keeps the placeholders (`status` "unbuilt", empty url / sha256, size 0). The release workflow (`.github/workflows/release.yml`, docs/RELEASE.md)
builds and packs each platform as `<id>-<version>-<platform>.zip` and each pack as `<packid>-<packversion>.zip` and runs `scripts/fill-manifest.py`, which
writes a COPY, `plugin.json.release`: every `builds[*]` and `contentPacks[*]` entry gets `status` "released", the GitHub asset url, the SHA-256 and the size computed
from the file (builds also `entry_point`, the `.clap` inside the zip), and the fields Kick reads besides are derived: `assets[]` (id, name, version, url, sha256, size,
required, type, license, from the content packs). `scripts/check-manifest.py --released` checks the result (https urls, asset names, the files' hashes and sizes,
`assets` mirrors the packs); `--local` is the variant for `scripts/make-local-release.sh` (relative or `file://` urls, only the platforms that were built). Each pack
zip is `contentpack.json` + the files at its root, verified by `tools/contentpack/contentpack.py` (every file, SHA-256, size, nothing unlisted) and below 2 GB
(GitHub's limit per asset is 2 GiB; a larger pack is split per instrument with `contentpack.py split` and every part is declared). `key_id` and `signature`
(128 hex characters, Ed25519) are produced by the Kick-side signing of the manifest (official plugins are signed with the project key; not this repo's concern
beyond leaving the fields out of the checked-in file). The manifest holds no secret.

The repo builds the same source into **both** CMake targets:

- `synthyboy_static` (static library, exports `const clap_plugin_entry_t* synthyboy_clap_get_entry(void)`), linked by `StaticPluginLoader` in the Kick app (mobile; also desktop builds).
- `synthyboy_clap` (shared module `SynthyBoy.clap`, standard `clap_entry`), loaded by `DynamicPluginLoader` (MPC, desktop).

Both go through the same CLAP entry, so Kick's Plugin-manager, state, content packs and project references take one code path.

## 8. GUI framework decision

**Experience from the spike:** JUCE 8 + clap-juce-extensions gave a working VST3 + CLAP from one `AudioProcessor` in one configure/build (≈40 s clean, macOS arm64), with the generic parameter GUI. Not yet tested: a custom GUI, GUI inside a DAW window for either format, Linux/Windows runs.

**Decision:** desktop GUI is a **separate layer on JUCE (AGPL)** `Component`s, using the JUCE shell. Reasons: it works today; JUCE's text/graphics/OpenGL stack is portable; wavetable and spectrum views are plain 2D drawing; the editor needs no DSP code (it reads/writes `ParamInfo` parameters and the wavetable model). Alternatives considered: DPF (ISC; thinner, produces VST3/CLAP/LV2; smaller community and weaker text handling), iPlug2 (zlib-style licence; no CLAP in the main line **(unverified)**), writing on Vital's own GUI (rejected, §2.1). Constraint kept: the UI never touches core internals, so replacing JUCE later (or giving the native CLAP shell a GUI) costs only the `ui/` directory. The Kick-side UI on MPC/mobile is not this GUI; it is generated from `ParamInfo` + `remote-controls` + macro pages.

Open GUI risk to verify in Phase 2: embedded-GUI behaviour in Reaper/Bitwig (CLAP), Reaper/Ableton (VST3), plus Linux Wayland (documented as unsupported by the wrapper).

### 8.1 UI kit (`shell-juce/ui`, state 2026-11-08)

The editor replaces JUCE's generic editor (`SYNTHYBOY_GENERIC_UI=1` brings it back). It is one touch-first layout for phone, tablet and desktop; guide for panel authors: `shell-juce/ui/README.md`; reports `docs/ui-design/UI-KIT-REPORT.md`, `UI-PANELS-REPORT.md`.

| Layer | Content |
|-------|---------|
| View-model (no JUCE, unit tested) | key resolution, display text with units, cap dimming, drag zones, double-tap / long-press rules, layout modes, pages; the assign-by-touch state machine on the `modNN` slots, A/B snapshots |
| Theme | one LookAndFeel, dark / light, 44 pt minimum for every interactive control at every UI scale; **no literal colours outside `Theme` (a test greps for them)** |
| Controls | knob, slider, choice, toggle, long-press sheet; mod ring (purple depth ring, bipolar, bypass dashed), value bubble with the fine-zone ladder; parameters bound by key, one drag = one host gesture |
| Panels | self-registering (`SYNTHYBOY_REGISTER_PANEL`), laid out in 1 / 2 / 3 columns by width; at most four primary controls, the rest behind "More"; **every parameter key has a control** (`synthyboy_test_ui_panels`) |
| Strip | preset prev / next + name, A/B, MOD, menu, 8 macros, mini keyboard + octave |

Decisions: double-tap resets, long-press (400 ms) or right-click opens the sheet with the exact value (Q3); A/B = two `ParamValues` snapshots in the processor (Q4); 8 macros (Q2); a control whose capability bit is missing dims itself with a tooltip. Not in the kit yet (listed in the reports, nothing hidden): undo / redo of parameter edits, pitch / mod wheel strips, MAIN meter, oscilloscope, help mode, LFO tab chips and shape editing tools, persistence of UI size / theme. Tests: `synthyboy_test_ui_model`, `synthyboy_test_ui_render`, `synthyboy_test_ui_panels`, `synthyboy_test_glb`, `synthyboy_test_fx` (headless Components on the real processor; phone 390x844 and tablet 1024x768 screenshots go to `$SYNTHYBOY_UI_SCREENSHOT_DIR`).

## 9. Phases

Each phase ends with: green CI on all platforms in the matrix that exists at that point, pluginval strictness 5 and clap-validator passing, the artifacts installable, `docs/` updated. Phases are not shortcuts of one another; later phases add scope.

| Phase | Content | Exit criteria |
|-------|---------|---------------|
| **0 (done)** | Spike: 8-voice synth, VST3 + CLAP from one code base | merged to `main` after independent build/codesign check |
| **1 Foundation** | Repo restructure into `core/` (`engine-own` = the spike engine ported off JUCE, `Engine` interface, `ParamInfo`, `StateIO`, `Quality`) and `shell-juce/` (thin adapter); manifest + schema; `SYNTHYBOY_WITH_VITAL` switch + empty `engine-vital/`; **CI for all targets** (§10): core tests on x86_64 with ASan/UBSan and TSan, Linux ARMv7 (cross + QEMU), Linux AArch64 (native arm runner), real-time allocation test, plugin builds as before; clap_smoke + validator stay green | all-platform CI green, JUCE-shell/core parity test, ARMv7/AArch64 core tests pass, state round trip incl. future-version and garbage input |
| **2 Vital take-over** | Import (§2.1), JUCE removal, namespace, build on all targets, ARMv7 NEON fix, `Engine` over the imported DSP, generic UI | audio-equivalence golden tests vs. the imported code's own tests; benchmark numbers on x86_64/arm64; first measurement on MPC. **State:** import, JUCE removal, `sbvital`, NEON fix, `IEngine` adapter (`createEngine (EngineKind::vital)`, selectable in the JUCE shell with `SYNTHYBOY_SHELL_ENGINE=vital`, the own engine stays the default), the 35 ported Vital unit test classes, feature "golden" measurements, allocation/TSan/ASan/UBSan tests, `bench/` with numbers for macOS arm64 (`PERFORMANCE.md`) and the `ios-vital-free` CI job exist. **Open:** CI results of the new jobs (Windows/MSVC and Linux x86_64 not run locally), numbers on x86_64 and ARMv7/MPC hardware, first measurement on a phone. |
| **3 Engine completion (done)** | Oscillators + spectral warp, filters, mod matrix, FX, unison, quality tiers, voice management, state migration | alias/filter/tuning tests; sanitizers clean; tiers measured on device. **State (2026-11-04):** our own parameter table of 880 entries (docs/PARAMETERS.md, `docs/params.json`) with state format version 2 and migration of version 1; the Vital-derived engine implements all of it (3 wavetable oscillators with unison, spectral morph and warp, noise, 2 filters + FX filter with 8 models, 6 envelopes, 8 LFOs, 4 random LFOs, 4 macros, a 64-slot modulation matrix with source / destination / amount / curve / bipolar / stereo / bypass, 9 effects in a reorderable chain, portamento, legato, pitch bend range, mod wheel and aftertouch sources, 13 built-in wavetables and 12 LFO shapes generated by code); it is the default engine of the JUCE plugin, the own engine is the fallback and iOS engine (it implements a documented subset); quality tiers cap voices, unison, oversampling and interpolation and bypass heavy effects without touching parameters; audio quality gates (parameter sweep, tuning, alias budget, filters, matrix, unison, effects, voices, tiers, state) in docs/QUALITY.md, allocation audit of the matrix in docs/UPSTREAM.md, benchmark of a default and a heavy patch in docs/PERFORMANCE.md, multithread stability fixes in Vital DSP (futils tanh clamp, lookup_table Catmull-Rom clamp, ladder filter style fallback). |
| **4 Content & presets (done)** | ContentPack format + tools, own factory wavetables and ≥100 own presets (CC0), preset discovery, missing-content behaviour tests | pack delete/restore test; licence lint passing. **State (2026-11-04):** `schema/contentpack.schema.json`, `core/include/synthyboy/preset.h` (.sbpreset format version 1 with metadata and version 2 state, fast metadata parser without DSP/engine overhead, directory scanner), `core/include/synthyboy/content_pack.h` (manifest parser, pack verification with self-contained SHA-256, ContentRegistry with missing-content tracking), 100 factory presets generated under CC0-1.0 across Bass, Lead, Pad, Pluck, Keys, FX with verified SHA-256 hashes in `content/presets/contentpack.json`, manifest check and full unit tests in `synthyboy_test_preset`. |
| **5 UI (in progress)** | Own JUCE GUI, wavetable editor, spectrum, mod UI; `clap.gui`; remote-controls pages | DAW embedding checks (§8), UI screenshot tests. **State (2026-11-08):** UI kit, all panels (osc, mix, filters, envelopes, LFOs, modulation, effects, global, wavetable editor sheet) with a control for every one of the 1484 keys, headless GUI tests with phone / tablet screenshots; DAW embedding, real touch hardware and Windows / Linux editor runs not verified |
| **6 Native CLAP shell + static build (done)** | Complete `shell-clap/`, `synthyboy_static` built with `SYNTHYBOY_WITH_VITAL=OFF` for iOS (own engine only; CI check that no Vital-derived object is linked), parity test green, iOS/Android build checks (library only) | static entry loads in a test host; manifest generated by CI; iOS Vital-free check green. **State (2026-11-04):** `shell-clap/` implementation (zero JUCE dependencies, pure C++17 + CLAP 1.2.7 headers), exports `synthyboy_clap_get_entry` and `clap_entry`, full parameter mapping, stereo audio-ports, note-ports, state save/load, voice info, sample-accurate event handling; `synthyboy_static` target; comprehensive unit and parity tests in `synthyboy_test_shell_clap` (11,998 checks passing); iOS Vital-free static library check verified clean for both `libsynthyboy_core.a` and `libsynthyboy_static.a`. |
| **7 Release** | Signed macOS (Developer ID + notarisation), Windows signing option, Linux/ARM tarballs, GitHub Releases, source tags, packs as separate assets, user manual | reproducible release job; checksums published |

Kick-side integration (installer, Plugin-manager, signing/trust levels) comes from the Kick plan; this repo exposes only the manifest, packs and entry points.

## 10. CI and validation

Current workflow (`.github/workflows/build.yml`): Linux/macOS/Windows build + core tests (CTest, including the GUI tests with real Components and no display), artifacts, pluginval 1.0.4 (SHA-256 pinned) on VST3, clap-validator 0.4.1 built from source (blocking); further jobs: core tests under ASan/UBSan and TSan (including `synthyboy_fuzz_smoke` and, under ASan, the factory preset gate `synthyboy_preset_sanity`), the GUI tests under ASan/UBSan (`ui-sanitizers`), core ARMv7 hard-float cross build + tests under QEMU, native Linux AArch64 build, manifest check. Findings and numbers of the sanitizer / fuzz / preset sweep: `docs/ui-design/SWEEP-REPORT.md`.

Target matrix, added in Phase 1 unless noted:

| Target | How | Checks |
|--------|-----|--------|
| Linux x86_64 | native (`ubuntu-latest`) | full build, pluginval under xvfb, clap-validator, core tests with ASan/UBSan/TSan, alloc-hook test |
| Linux AArch64 | `ubuntu-24.04-arm` runner if available, else cross + QEMU user **(unverified: runner availability)** | core build + tests, JUCE-free native shell build |
| Linux ARMv7 hard-float | cross toolchain (`arm-linux-gnueabihf`) + `qemu-arm` for core/native-shell tests; real timing only on the MPC | core build + tests with NEON, `.clap` symbol check (`clap_entry`), `file`/`readelf` ABI check |
| macOS | `macos-latest` arm64 (+ universal build job) | build, `codesign --verify`, pluginval, clap-validator |
| Windows | `windows-latest` x64 | build, pluginval, clap-validator |
| iOS | Xcode toolchain, static library of core + native shell only | compile check, no plugin run |
| Android | NDK (arm64-v8a, armeabi-v7a), static library | compile check, no plugin run |

Vital-derived code in CI: all jobs except `ios-vital-free` build and test it (`SYNTHYBOY_WITH_VITAL=ON` is the default): the shared engine tests run for both engines, the allocation test and the TSan thread test cover the Vital engine, the ported Vital unit tests and the adapter's measurements run on every target (ARMv7 under QEMU), and a Linux job builds the plugin with `-DSYNTHYBOY_SHELL_ENGINE=vital` and runs the CLAP smoke host on it. `ios-vital-free` configures `-DSYNTHYBOY_WITH_VITAL=OFF`, builds and tests the core and runs `scripts/check-no-vital.sh` on `libsynthyboy_core.a` (fails on any `sbvital`/`Vital`/`vital::` object, symbol or string; the script tests itself with `--self-test`).

clap-validator: pin to a tag, built once and cached; it is blocking since its first green run (2026-11-04). Known validator-relevant requirements are designed in (stable ids, note-id/NOTE_END handling, state round-trip, `init` without GUI/IO).

## 11. Known open points

- **(owner)** Trademark/name clearance for "SynthyBoy" (docs/NAMING.md). iOS is decided: own engine (§2.2).
- Exact CPU of the MPC Key 61 and its available toolchain/sysroot (cross-compile target) – read from the device.
- Whether `clap-juce-extensions` implements the extensions needed in §4.1 at the pinned commit.
- Vital engine in the shipped plugin: since Phase 3 it is the JUCE shell's default (own engine: `-DSYNTHYBOY_SHELL_ENGINE=own`, and when `SYNTHYBOY_WITH_VITAL=OFF`). The owner's listening tests (level, velocity curve and release time differ, `core/engine-vital/README.md`) and the MPC measurement are still open **(owner)**.
- Upstream quirks recorded but not changed (`WaveFrame::removedDc`, the non-atomic seed counter): docs/UPSTREAM.md.
- Spike gaps carried over: GUI never opened in a DAW; `-G Xcode` configure fails (Ninja is the supported generator); Linux/Windows builds untested until the first CI run is recorded; CLAP wrapper on Linux supports X11 only.
