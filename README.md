# SynthyBoy

A minimal 8-voice polyphonic synthesizer, built as **VST3** and **CLAP** from a single JUCE code base.

> Status: spike / proof of concept. Version 0.1.0.

## Plugin

- 8-voice polyphony, MIDI note on/off (+ pitch wheel, +-2 semitones)
- Oscillator: sine or band-limited (PolyBLEP) saw
- Simple fixed ADSR amplitude envelope (5 ms / 150 ms / 0.7 / 250 ms)
- 12 dB/oct low-pass filter

| Parameter | Range | Default |
|-----------|-------|---------|
| Gain      | -60 .. +6 dB | -12 dB |
| Waveform  | Sine / Saw   | Saw |
| Cutoff    | 20 .. 20000 Hz | 8000 Hz |

The editor is JUCE's generic parameter GUI (no custom UI yet).

- VST3 category: `Instrument|Synth`; CLAP id `com.safian.synthyboy`, features `instrument`, `synthesizer`

## Formats

| Format | Output (after build) |
|--------|----------------------|
| VST3   | `build/SynthyBoy_artefacts/Release/VST3/SynthyBoy.vst3` |
| CLAP   | `build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap` |

Both come from the same sources: JUCE builds the VST3, and
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) wraps the same
`juce::AudioProcessor` into a CLAP (`SynthyBoy_CLAP` CMake target, created by `clap_juce_extensions_plugin(...)`).

## Repository layout

| Path | Content |
|------|---------|
| `core/` | `synthyboy_core`: JUCE-free C++17 static library. `include/synthyboy/` public API (`IEngine`, `ParamInfo` table, state I/O, `Quality`, events), `src/` params/state/number formatting, `engine-own/` the engine (the spike's 8-voice synth ported off JUCE), `engine-vital/` reserved for Vital-derived code (empty), `tests/` unit, real-time (no allocation) and thread tests with an own tiny runner |
| `shell-juce/` | thin `juce::AudioProcessor`: parameter layout generated from the core table, MIDI/params to core events, state blob from the core |
| `tests/clap-smoke/` | `clap_smoke`: loads the built `.clap`, checks parameter parity with the core table, audio, state round trip |
| `cmake/toolchains/` | `armv7-linux-gnueabihf`, `aarch64-linux-gnu` cross toolchain files |
| `plugin.json`, `schema/` | plugin manifest and its JSON schema (`scripts/check-manifest.py`) |
| `docs/ARCHITECTURE.md` | design and phase plan |

CMake options: `SYNTHYBOY_BUILD_PLUGIN` (ON; OFF builds only the core, no JUCE download), `SYNTHYBOY_BUILD_TESTS` (ON),
`SYNTHYBOY_WITH_VITAL` (ON; no effect yet).

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
cmake -S . -B build-asan -G Ninja -DSYNTHYBOY_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S . -B build-armv7 -G Ninja -DSYNTHYBOY_BUILD_PLUGIN=OFF -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armv7-linux-gnueabihf.cmake \
  -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-arm;-L;/usr/arm-linux-gnueabihf"
```

### Installing

Copy the bundles to the usual plugin folders:

| OS | VST3 | CLAP |
|----|------|------|
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/CLAP/` |
| Linux | `~/.vst3/` | `~/.clap/` |
| Windows | `C:\Program Files\Common Files\VST3\` | `C:\Program Files\Common Files\CLAP\` |

## Continuous integration

`.github/workflows/build.yml`:

- `build` (`ubuntu-latest`, `macos-latest`, `windows-latest`): VST3 + CLAP, core tests (CTest), `clap_smoke` (Linux/macOS),
  artifacts, [pluginval](https://github.com/Tracktion/pluginval) (release v1.0.4, SHA-256 checked) on the VST3 and
  [clap-validator](https://github.com/free-audio/clap-validator) (built from source, 0.4.1; blocking) on the CLAP.
- `core-sanitizers`: the core without JUCE under ASan + UBSan and under TSan.
- `core-armv7`: the core cross compiled for Linux ARMv7 hard-float, tests run under QEMU user mode (ABI/NEON attributes are printed).
- `linux-aarch64`: full build, tests and `clap_smoke` on GitHub's native arm runner.
- `manifest`: `plugin.json` against `schema/plugin-manifest.schema.json`.

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
- **clap-juce-extensions**, the CLAP SDK (`clap`, `clap-helpers`) and the bundled Steinberg VST3 SDK 3.8.0 are
  **MIT** licensed and compatible with the AGPL.

Versions and licences of all dependencies: [THIRD_PARTY.md](THIRD_PARTY.md), [NOTICE](NOTICE).

VST is a registered trademark of Steinberg Media Technologies GmbH.
