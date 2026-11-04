# SynthyBoyVST

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

- VST3 category: `Instrument|Synth`; CLAP id `com.safian.synthyboyvst`, features `instrument`, `synthesizer`

## Formats

| Format | Output (after build) |
|--------|----------------------|
| VST3   | `build/SynthyBoyVST_artefacts/Release/VST3/SynthyBoyVST.vst3` |
| CLAP   | `build/SynthyBoyVST_artefacts/Release/CLAP/SynthyBoyVST.clap` |

Both come from the same sources: JUCE builds the VST3, and
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) wraps the same
`juce::AudioProcessor` into a CLAP (`SynthyBoyVST_CLAP` CMake target, created by `clap_juce_extensions_plugin(...)`).

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

### Installing

Copy the bundles to the usual plugin folders:

| OS | VST3 | CLAP |
|----|------|------|
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/CLAP/` |
| Linux | `~/.vst3/` | `~/.clap/` |
| Windows | `C:\Program Files\Common Files\VST3\` | `C:\Program Files\Common Files\CLAP\` |

## Continuous integration

`.github/workflows/build.yml` builds VST3 + CLAP on `ubuntu-latest`, `macos-latest` and `windows-latest`,
uploads the plugins as artifacts, and runs [pluginval](https://github.com/Tracktion/pluginval)
(release v1.0.4, SHA-256 checked) on the VST3 and
[clap-validator](https://github.com/free-audio/clap-validator) (built from source, 0.4.1; blocking) on the CLAP.
First GitHub run 2026-11-04: Linux and macOS green; Windows failed only in the pluginval SHA-256 check (fixed).
clap-validator found three real issues in the spike (Cutoff text round trip; parameter values changed on state load
without `params.rescan`), fixed in `Source/PluginProcessor.cpp` and guarded by `tests/clap-smoke` (built as `clap_smoke`,
run in CI on Linux/macOS).

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
