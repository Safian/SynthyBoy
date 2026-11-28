# SynthyBoy

SynthyBoy is a wavetable synthesizer for standalone Akai MPC hardware and desktop hosts. Its own C++ engine powers the MPC VST2, VST3, CLAP and macOS Audio Unit builds.

> Version 0.1.0 is in development. The MPC overlay has been exercised on the Key 61 and XL. Desktop DAW embedding is unverified.

| Phase (docs/ARCHITECTURE.md section 9) | State |
|-------|-------|
| 0 Spike (8-voice synth, VST3 + CLAP from one code base) | done |
| 1 Foundation (core / shells split, `IEngine`, parameter table, state, CI matrix) | done |
| 2 Vital take-over (DSP import, JUCE removal, ARMv7 NEON) | done, then removed again: the own engine is the only engine (docs/plans/vital-removal.md) |
| 3 Engine completion | Oscillators, filters, modulation, buses, effect racks, tuning and MPE. See the [engine reference](core/engine-own/README.md). |
| 4 Content and presets | Factory content packs and 184 presets across six categories. |
| 5 UI | in progress: UI kit and all panels with a control for every key, wavetable editor, headless GUI tests; MPC Key 61 & XL overlay UI verified on hardware; desktop DAW embedding not verified |
| 6 Native CLAP shell and static build | done (no build holds Vital-derived code) |
| 7 Release (signing, notarisation, packs as assets, manual) | See the [release procedure](docs/RELEASE.md) and [outstanding work][tasks]. |

Quality gates beyond the unit tests: the core and the GUI tests run under ASan + UBSan, the threaded tests under ThreadSanitizer, `synthyboy_fuzz_smoke`
feeds garbage and mutated documents to every parser, and `synthyboy_preset_sanity` renders the factory presets at every tier
(`docs/ui-design/SWEEP-REPORT.md`).

## Factory content

The [factory sound catalogue](docs/user-guide/factory-sounds.md) covers 184 CC0 presets. The factory files use state version 4 with 2,637 parameters. The engine writes state version 6, which also stores the LFO editor grid and loop-link choices. [State definition](core/include/synthyboy/state.h)

The factory packs contain 72 wavetables, 12 standalone samples, three multisample instruments and three impulse responses. Preset output levels are stored in the sounds themselves. The default output stage is at unity gain with the limiter off. [Content catalogue](docs/user-guide/factory-content.md) · [Output stage](core/include/synthyboy/output_stage.h)

| Category | Presets | Of which showcase presets (version 3 engine features) |
|----------|--------:|--------------------------------------------------------|
| Bass | 34 | Scream Filter Bass (Scream filter), EMS Squelch Bass (EMS ladder), Custom Shape Wobble (custom LFO shape in `content.lfo_shapes`) |
| Lead | 37 | Twin Warp Lead (second warp stage), MPE Pressure Lead (poly aftertouch and slide as sources, MPE on) |
| Pad | 40 | Diffusor Cloud Pad (Diffusor filter), Hyper Dimension Pad (Hyper/Dimension), Tri-Band Split Texture (L/M/H splitter with racks) |
| Pluck | 23 | Aux Keyscale Pluck (aux source of a mod route) |
| Keys | 29 | Utility Width Keys (Utility effect), Just Intonation Keys (tuning, A4 = 440 Hz) |
| FX | 21 | Bode Shifter Drone (Bode frequency shifter) |

## Plugin

The desktop editor supports touch input. Set `SYNTHYBOY_GENERIC_UI=1` to use JUCE's generic parameter editor.

- VST3 category: `Instrument|Synth`; CLAP id `com.safian.synthyboy`, features `instrument`, `synthesizer`

## Formats

| Format | Output (after build) |
|--------|----------------------|
| VST3   | `build/SynthyBoy_artefacts/Release/VST3/SynthyBoy.vst3` |
| CLAP   | `build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap` |
| AU (macOS only, Audio Unit v2 instrument `aumu Syby Safn`) | `build/SynthyBoy_artefacts/Release/AU/SynthyBoy.component` |

All three come from the same sources: JUCE builds the VST3 and the AU (the AU wrapper is part of JUCE; target `SynthyBoy_AU`, added only on Apple desktop builds), and
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) wraps the same
`juce::AudioProcessor` into a CLAP (`SynthyBoy_CLAP` CMake target, created by `clap_juce_extensions_plugin(...)`).
The MPC VST2 build (`scripts/build-mpc.sh`, [docs/MPC.md](docs/MPC.md); end-user instructions: [MPC Installation Guide](docs/user-guide/INSTALL.md) and [MPC User Guide](docs/user-guide/SynthyBoy-MPC-User-Guide.md)) is a separate build and does not depend on the AU.

## Documentation map

| Topic | Start here | Evidence and history |
|---|---|---|
| Using the MPC instrument | [Installation](docs/user-guide/INSTALL.md), [playing guide](docs/user-guide/SynthyBoy-MPC-User-Guide.md), [magyar kézikönyv](docs/user-guide/hu/SynthyBoy-MPC-felhasznaloi-kezikonyv.md) | [MPC hardware observations](docs/MPC.md) identify the model, build and limits of individual checks. |
| Controls and screen changes | [MPC screen map](docs/ui-map/README.md), [native component catalogue](docs/ui-components/README.md) | The screen map links before/after captures. [UI design](docs/ui-design/README.md) records earlier proposals, coverage decisions and implementation reports. |
| Engine, presets and content | [Engine](core/engine-own/README.md), [parameter reference](docs/PARAMETERS.md), [content packs](docs/CONTENT-PACKS.md) | [Engine specification and test vectors](docs/engine-spec/README.md), [quality gates](docs/QUALITY.md), [factory sound catalogue](docs/user-guide/factory-sounds.md) |
| Performance and device limits | [Measurement protocol](docs/perf/README.md), [DSP findings by topic](docs/dsp-knowledge/README.md) | Each finding identifies its evidence. Raw hardware captures are local to the owner under the shared `.hw-local/mpc-perf/`. |
| Building and distributing | [Release procedure](docs/RELEASE.md), [MPC build guide](docs/MPC.md) | [Development history](docs/release/HISTORY-by-week.md) and linked reports record the versions they describe. |
| Project rules and outstanding work | [Project instructions](AGENTS.md), [current tasks and acceptance criteria][tasks] | Dated plans, review reports and local handoffs describe their original sessions. Check the current task and source before reusing a procedure or a device result. |

## Repository layout

| Path | Content |
|------|---------|
| `core/` | `synthyboy_core`: JUCE-free C++17 static library. `include/synthyboy/` public API (`IEngine`, `ParamInfo` table, state I/O, `Quality`, events), `src/` the parameter table and versioned state with its `content` section, content store, engine capability bits, number formatting, `engine-own/` the engine (see [Engine](#engine)), `tests/` unit, real-time (no allocation), thread and fuzz tests with an own tiny runner; the Serum-class packages are `tuning/`, `formula/`, `shapes/`, `wavetable-edit/`, `wavetable-render/`, `vital-import/`, `sample-osc/`, `filters-serum/`, `fx-serum-a/`, `fx-serum-b/` (each with a REPORT, overview: docs/ARCHITECTURE.md 3.1) |
| `tests/clap-smoke/` | `clap_smoke`: loads the built `.clap`, checks parameter parity with the core table, audio, state round trip |
| `cmake/toolchains/` | `armv7-linux-gnueabihf`, `aarch64-linux-gnu` cross toolchain files |
| `plugin.json`, `schema/` | plugin manifest and its JSON schema (`scripts/check-manifest.py`) |
| `tools/contentpack/` | `contentpack.py`: builds, verifies and splits ContentPacks (zip + `contentpack.json`), see docs/RELEASE.md |
| `bench/`, `tools/paramdump/`, `tools/preset-render/` | `synthyboy_bench`: real-time factor and block times of a default and a heavy patch per tier; `synthyboy_paramdump`: the parameter table as JSON (`docs/params.json`); `synthyboy_preset_render`: renders the factory presets at every tier, checks them and prints the level spread and the velocity curve (`--wav DIR` writes the renders for listening) |
| `scripts/` | `build.sh`, `check-manifest.py`, release: `package-release.py`, `fill-manifest.py`, `build-armv7-clap.sh`, `make-local-release.sh` ([docs/RELEASE.md](docs/RELEASE.md)), `check-no-vital.sh` (no Vital-derived code in the core library, none in the source of the own engine), `gen-params-doc.py` / `check-params-doc.sh` (docs/PARAMETERS.md and params.json) |
| `docs/ARCHITECTURE.md`, `PARAMETERS.md`, `QUALITY.md`, `UPSTREAM.md`, `PERFORMANCE.md` | design and phase plan, the parameter reference (generated), audio quality gates and their measurements, history of the removed Vital take-over, benchmark numbers |
| `docs/ui-components/` | the native component catalogue and the notes of the display layer (`shell-mpc/ui/`); entry page [docs/ui-components/README.md](docs/ui-components/README.md), instructions for AI agents [shell-mpc/ui/AGENTS.md](shell-mpc/ui/AGENTS.md) |

CMake options: `SYNTHYBOY_BUILD_TESTS` (ON),
`SYNTHYBOY_DEFAULT_TIER` (`high` | `medium` | `low`: the quality tier of the plugin).

## Engine

One engine sits behind `synthyboy::IEngine` (`createEngine (EngineKind::own)`): `core/engine-own`, original code, which acts on 2634 of the 2637 parameters of the table ([core/engine-own/README.md](core/engine-own/README.md), the list of the keys it does not act on: [docs/ui-design/OWN-ENGINE-GAP.md](docs/ui-design/OWN-ENGINE-GAP.md)). It is the engine of every build: the JUCE plugin, the native CLAP module, the MPC package and iOS. An engine derived from [Vital](https://github.com/mtytel/vital) was part of earlier versions; it was removed (plan and decisions: [docs/plans/vital-removal.md](docs/plans/vital-removal.md), record of the take-over: [docs/UPSTREAM.md](docs/UPSTREAM.md)). `EngineKind::vital` remains only as another name of the own engine, so that old settings and callers still work.

The parameters are our own table (keys like `osc1.unison.detune`, our units): [docs/PARAMETERS.md](docs/PARAMETERS.md), machine readable `docs/params.json`; what the engine does
and what it is measured to do: [docs/QUALITY.md](docs/QUALITY.md). `build/bench/synthyboy_bench` prints the real-time factor
([docs/PERFORMANCE.md](docs/PERFORMANCE.md)). The CI job `no-vital` builds the core and checks the static
library with `scripts/check-no-vital.sh` (the importer of the `.vital` file format, `core/vital-import`, is not Vital-derived code and is allowed; the source of `core/engine-own` may not get a Vital identifier or a GPL header).

## Building

Requirements: CMake >= 3.22, a C++17 compiler, git, internet access on the first configure
(JUCE and clap-juce-extensions are fetched with CMake `FetchContent` at pinned versions, see
[THIRD_PARTY.md](THIRD_PARTY.md)). Ninja is used when installed.

### macOS / Linux

```sh
scripts/build.sh            # Release build into ./build
scripts/build.sh --clean    # start from scratch
scripts/build.sh --debug
```

or by hand:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

macOS: the build targets the host architecture (arm64 on Apple Silicon), deployment target 11.0.
For a universal binary add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. Both bundles are ad-hoc
signed; pass `-DSYNTHYBOY_CODESIGN_IDENTITY="Developer ID Application: ..."` to sign with a real identity.
The `-G Xcode` generator has not been verified.

Linux packages (Debian/Ubuntu):

```sh
sudo apt-get install ninja-build pkg-config libasound2-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libfreetype-dev \
  libfontconfig1-dev libgl1-mesa-dev
```

### Windows (Visual Studio 2022)

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

### Tests

```sh
ctest --test-dir build --output-on-failure                       # core tests (Windows: add -C Release)
build/clap_smoke build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap
python3 -I scripts/check-manifest.py                             # plugin.json vs. schema and CMakeLists.txt
```

Core only (no JUCE), e.g. with sanitizers or cross compiled for ARMv7 (needs `g++-arm-linux-gnueabihf` and `qemu-user`):

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S . -B build-armv7 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armv7-linux-gnueabihf.cmake \
  -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-arm;-L;/usr/arm-linux-gnueabihf"
```

### Installing

Copy the bundles to the usual plugin folders:

| OS | VST3 | CLAP |
|----|------|------|
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/CLAP/` |
| Linux | `~/.vst3/` | `~/.clap/` |
| Windows | `C:\Program Files\Common Files\VST3\` | `C:\Program Files\Common Files\CLAP\` |
| MPC Standalone OS | (see [INSTALL.md](docs/user-guide/INSTALL.md)) `<Drive>/VST2 Plugins/Safian - VST - SynthyBoy/` | — |

macOS only, the Audio Unit (`SynthyBoy.component`) goes to `~/Library/Audio/Plug-Ins/Components/`; validate it with `auval -v aumu Syby Safn`.

For **Akai MPC Standalone OS** (MPC Key 61, MPC XL, etc. running MPC OS 3.10.1+), copy the `Safian - VST - SynthyBoy` directory to a folder named `VST2 Plugins` on your internal drive, SD card, or USB drive, enable **External Plugin Support** under Preferences > Enhancements, and restart the unit. See the full [MPC Installation Guide](docs/user-guide/INSTALL.md) and [MPC User Guide](docs/user-guide/SynthyBoy-MPC-User-Guide.md).

## Continuous integration

`.github/workflows/build.yml`:

- `build` (`ubuntu-latest`, `macos-latest`, `windows-latest`): VST3 + CLAP, core tests (CTest), `clap_smoke` (Linux/macOS),
  artifacts, [pluginval](https://github.com/Tracktion/pluginval) (release v1.0.4, SHA-256 checked) on the VST3 and
  [clap-validator](https://github.com/free-audio/clap-validator) (built from source, 0.4.1; blocking) on the CLAP.
- `core-sanitizers`: the core without JUCE under ASan + UBSan and under TSan (includes `synthyboy_fuzz_smoke`; the slow audio gates, among them the 100 factory presets x 3 tiers, run under ASan + UBSan).
- `ui-sanitizers`: the GUI tests (real Components, no display) under ASan + UBSan.
- `core-armv7`: the core cross compiled for Linux ARMv7 hard-float, tests run under QEMU user mode (ABI/NEON attributes are printed).
- `linux-aarch64`: full build, tests and `clap_smoke` on GitHub's native arm runner.
- `manifest`: `plugin.json` against `schema/plugin-manifest.schema.json`.

`.github/workflows/release.yml` runs only for a pushed tag `v*.*.*` (or by hand) and creates a **draft** release with the zips of all five platforms, the content packs and `plugin.json.release`: [docs/RELEASE.md](docs/RELEASE.md); `scripts/make-local-release.sh` does the same locally (Docker for linux-armv7) into `dist/`.
- `no-vital`: core build + tests, `scripts/check-no-vital.sh` on `libsynthyboy_core.a` and `libsynthyboy_static.a`, on the source of `core/engine-own` (`--source`) and its `--self-test`; the `build` job runs it on the core library too.

Every job builds and tests the own engine; `build` (Linux) runs `clap_smoke` on the plugin, checks that `docs/params.json` and `docs/PARAMETERS.md` are current, `linux-aarch64` prints benchmark numbers.

First GitHub run 2026-11-04: Linux and macOS green; Windows failed only in the pluginval SHA-256 check (fixed).
clap-validator found three real issues in the spike (Cutoff text round trip; parameter values changed on state load
without `params.rescan`), fixed in `shell-juce/PluginProcessor.cpp` and guarded by `tests/clap-smoke`. The jobs added with
the core/shell restructuring have not run on GitHub yet.

## License

Copyright (C) 2026 Safian Szabolcs.

This program is free software: you can redistribute it and/or modify it under the terms of the
**GNU Affero General Public License, version 3 or (at your option) any later version**
(`AGPL-3.0-or-later`, see [LICENSE](LICENSE)).

- **JUCE** is used under its **AGPLv3** option (JUCE is dual-licensed AGPLv3 / commercial). Because
  the plugin links JUCE, the combined work is distributed under the AGPL: anyone who receives the
  binaries (or interacts with them over a network) must be able to get the corresponding source code.
- The **Audio Unit** wrapper (macOS) is JUCE code (`juce_audio_plugin_client`, AGPLv3 option) and links the system `AudioUnit`/`CoreAudioKit` frameworks; no Apple AudioUnit SDK source is in this repository.
- **clap-juce-extensions**, the CLAP SDK (`clap`, `clap-helpers`) and the bundled Steinberg VST3 SDK 3.8.0 are
  **MIT** licensed and compatible with the AGPL.

Versions and licences of all dependencies: [THIRD_PARTY.md](THIRD_PARTY.md), [NOTICE](NOTICE).

VST is a registered trademark of Steinberg Media Technologies GmbH.

[tasks]: TODO.md
