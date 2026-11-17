# SynthyBoy

SynthyBoy is a wavetable synthesizer designed for standalone music production hardware and desktop DAWs.

The Akai MPC hardware platform is the primary product target, featuring a full-screen, plugin-drawn touch UI on both generation 1 (Akai MPC Key 61, 32-bit ARMv7 hard-float with NEON) and generation 2 (Akai MPC XL, 64-bit AArch64). A single package (`Safian - VST - SynthyBoy`) contains both architectures and runs natively within MPC OS 3.10.1+.

Desktop formats (CLAP, VST3, and macOS Audio Unit) are built from the same shared core DSP engine.

- **User Guide:** [SynthyBoy MPC User Guide](docs/user-guide/SynthyBoy-MPC-User-Guide.md) (in [docs/user-guide](docs/user-guide/))
- **Changelog Draft:** [Changelog Draft](docs/release/CHANGELOG-draft.md) (in [docs/release](docs/release/))

---

## Current Facts and Specifications

All numbers and specifications below are taken directly from the repository source files:

### Parameters and State Format
- **Total parameters:** 2637 <!-- docs/params.json --> distinct parameters defined in `docs/params.json` (and `core/src/param_table.cpp`), exposing full synthesis control with machine-readable metadata.
- **Engine state format version:** 5 <!-- core/include/synthyboy/state.h:16 --> (`constexpr int kStateVersion = 5;` in `core/include/synthyboy/state.h`, supporting LFO shape editor grid configuration `grid_x` and `grid_y`).
- **Factory preset state format version:** 4 <!-- content/presets/Bass/scream_filter_bass.sbpreset --> (all bundled presets store explicit state format version 4 with 2637 <!-- content/presets/Bass/scream_filter_bass.sbpreset --> parameters, backward-compatible with version 5 loaders).

### Factory Content
`content/presets/` provides 112 <!-- content/presets --> factory presets under CC0-1.0 (`contentpack.json`, author Safian Szabolcs), categorized across 6 <!-- content/presets --> sound banks:
- **Bass:** 23 <!-- content/presets/Bass --> presets
- **Lead:** 22 <!-- content/presets/Lead --> presets
- **Pad:** 23 <!-- content/presets/Pad --> presets
- **Pluck:** 16 <!-- content/presets/Pluck --> presets
- **Keys:** 17 <!-- content/presets/Keys --> presets
- **FX:** 11 <!-- content/presets/FX --> presets
Total: 112 <!-- content/presets --> presets.

### MPC Touch UI: Pages and Tabs
On Akai MPC hardware, the user interface is organised into touch pages and tabs:
- **Standard touch skin layout:** 34 <!-- docs/MPC.md:606 --> tabs grouped under 10 <!-- docs/MPC.md:606 --> function keys (`scripts/mpc-ui-pages.json`):
  1. `Macros` (Macros: 1 <!-- docs/MPC.md:606 --> tab)
  2. `Osc` (Osc 1, Osc1 Shp, Osc 2, Osc2 Shp, Osc 3, Osc3 Shp: 6 <!-- docs/MPC.md:606 --> sub-tabs)
  3. `Mix` (Sub/Nois, Mixer: 2 <!-- docs/MPC.md:606 --> sub-tabs)
  4. `Filter` (Filter 1, Filter 2: 2 <!-- docs/MPC.md:606 --> sub-tabs)
  5. `Env` (Env 1 to 4: 4 <!-- docs/MPC.md:606 --> sub-tabs)
  6. `LFO` (LFO 1 to 4: 4 <!-- docs/MPC.md:606 --> sub-tabs)
  7. `Mod` (Mod 1-2, Mod 3-4, Mod Amt: 3 <!-- docs/MPC.md:606 --> sub-tabs)
  8. `FX` (FX Chain, FX Filt, Distort, Comp 1, Comp 2, EQ: 6 <!-- docs/MPC.md:606 --> sub-tabs)
  9. `FX 2` (Chorus, Delay, Phaser, FX Level: 4 <!-- docs/MPC.md:606 --> sub-tabs)
  10. `Master` (Master, Voice: 2 <!-- docs/MPC.md:606 --> sub-tabs)
  Total: 34 <!-- docs/MPC.md:606 --> tabs (fallback layout: 32 <!-- docs/MPC.md:609 --> tabs in `scripts/mpc-ui-pages-v32.json`).
- **Overlay skin layout:** 24 <!-- docs/MPC.md:1132 --> tabs (`scripts/overlay-skin/mpc-ui-pages-overlay.json`).
- **Plugin-drawn UI (overlay build):** Full-screen 1280 <!-- docs/MPC.md:1114 --> x 800 <!-- docs/MPC.md:1114 --> pixel vector-rendered views drawn directly by the plugin:
  - Play (Play, Play Env, Play LFO) <!-- docs/MPC.md:1118 -->
  - Oscillators: 5 <!-- docs/MPC.md:1112,1128 --> dedicated full-screen views (Osc A, Osc B, Osc C, Sub, Noise) under the OSC function key
  - Filters: 2 <!-- docs/MPC.md:1119 --> views (Filter 1, Filter 2) with response curve visualization
  - Mixer: 1 <!-- docs/MPC.md:1120 --> view with strip meters and faders
  - FX: 1 <!-- docs/MPC.md:1121 --> view with chain bar and slot editor
  - Mod Matrix: 1 <!-- docs/MPC.md:1122 --> view with 57 <!-- docs/MPC.md:1138 -->-source quick-add board
  - Macros: 1 <!-- docs/MPC.md:1123 --> view with 8 <!-- docs/MPC.md:1136 --> editable macro cards
  - Master: 1 <!-- docs/MPC.md:1124 --> view (Output, Pitch, Voices, Glide, Scale, Tuning, MPE)
  - Voice: 1 <!-- docs/MPC.md:1125 --> view (Voice control grid and modulation parameters)
  - Envelopes: 4 <!-- docs/MPC.md:1126 --> views (Env 1 through Env 4)
  - LFOs: 4 <!-- docs/MPC.md:1127 --> views (LFO 1 through LFO 4, plus random LFOs)

### Engine Synthesis Architecture: Filters and Effects
- **Filter Models:** 27 <!-- docs/params.json --> filter models (`filter1.model`, `filter2.model`, `fxfilter.model` in `docs/params.json`, `docs/engine-spec/05-filters.md`, `core/filters-serum/CATALOGUE.md`):
  - 8 <!-- docs/engine-spec/05-filters.md:3 --> base models: Analog, Dirty, Ladder, Digital (Cytomic/Zavalishin ZDF SVF), Diode, Formant, Comb, Phaser.
  - 19 <!-- core/filters-serum/CATALOGUE.md --> Serum-class models: Multi SVF, Slope, EQ 6 dB, Ring Mod, Sample Hold, Smear, French LP, Add Bass, Scream, New Wsp, DJ Mixer, Diffusor, EMS Ladder, PZ SVF, Normal, Multi Morph, Flanges, Misc, New.
- **Filter Styles:** 12 <!-- docs/params.json --> selectable styles (`filter1.style`, Style A through Style L in `docs/params.json`).
- **Effect Slots and Types:**
  - 16 <!-- core/include/synthyboy/quality.h:50 --> effect slots in the main chain (`docs/engine-spec/06-effects.md:12`, `core/include/synthyboy/quality.h:32-50`):
    - 9 <!-- docs/engine-spec/06-effects.md:3 --> baseline effects: Chorus, Compressor, Delay, Distortion, EQ, Filter, Flanger, Phaser, Reverb.
    - 4 <!-- docs/engine-spec/06-effects.md:5, core/include/synthyboy/quality.h:42-45 --> advanced modules: Hyper/Dimension, Utility, Bode frequency shifter, Convolve.
    - 3 <!-- core/include/synthyboy/quality.h:46-48 --> band splitters: Splitter L/H (Low/High), Splitter L/M/H (Low/Mid/High), Splitter M/S (Mid/Side).
  - Effect Instance Pool: 16 <!-- core/include/synthyboy/fx_types.h:19 --> generic instance slots (`fxi01` .. `fxi16`) supporting 13 <!-- core/include/synthyboy/fx_types.h:41 --> parameterised effect types with 24 <!-- core/include/synthyboy/fx_types.h:21 --> parameters per slot across 10 <!-- docs/MPC.md:1150 --> racks (Main, Bus 1, Bus 2, and 7 <!-- docs/MPC.md:1150 --> band racks).

### Hardware Quality Profiles
On MPC hardware, CPU capabilities differ between devices. The shell selects a device profile at startup (`shell-mpc/device_profile.h`):
- **Akai Professional MPC XL (64-bit AArch64):** `high,voices=16,control=economy` <!-- shell-mpc/device_profile.h:186 -->
  Plays 16 <!-- shell-mpc/device_profile.h:186 --> voices with up to 16 <!-- docs/MPC.md:420 --> unison voices, all effects enabled (`fx=all`), 4x <!-- docs/MPC.md:420 --> oversampled warps, full-rate morph, exact math, with economy control rate to stay within 50 <!-- docs/MPC.md:422 -->% of the audio block budget (2.902 <!-- docs/MPC.md:422 --> ms).
- **Akai Professional MPC Key 61 (32-bit ARMv7):** `low` <!-- shell-mpc/device_profile.h:187 -->
  Runs base quality `low`: 8 <!-- shell-mpc/device_profile.h:40 --> voices, 3 <!-- shell-mpc/device_profile.h:41 --> unison voices cap per oscillator, all four economy flags active (warp=2x, morph=half, math=fast, control=economy), heavy effects bypassed (reverb, flanger, Hyper/Dimension, Bode, Convolve) to fit the single-core CPU budget.
- **Unknown Device / Fallback:** `low` <!-- shell-mpc/device_profile.h:190,192,193 --> (`kProfileUnknownDevice`, `kProfileNoNameArm64`, `kProfileNoNameArm32`).
- **Profile grammar:** `low | medium | high [, voices=1..32] [, unison=1..16] [, warp=2x|4x] [, morph=half|full] [, math=fast|exact] [, control=economy|full] [, fx=low|all] [, samples=low|medium|high]` <!-- shell-mpc/device_profile.h:8 -->.

---

## Formats and Package Structure

| Target Platform | Package / Artefact | Notes |
|-----------------|---------------------|-------|
| **Akai MPC (Hardware)** | `Safian - VST - SynthyBoy/` | Single folder containing 32-bit (`VST/synthyboy-arm32.so`) and 64-bit (`VST/synthyboy-arm64.so`) binaries, `Plugin Skins`, `Content/presets`, and `plugin.json` / `version.xml` |
| **CLAP** | `build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap` | Statically linked native CLAP module with bitmap GUI extension |
| **VST3** | `build/SynthyBoy_artefacts/Release/VST3/SynthyBoy.vst3` | Desktop VST3 instrument |
| **AU (macOS only)** | `build/SynthyBoy_artefacts/Release/AU/SynthyBoy.component` | Audio Unit v2 instrument (`aumu Syby Safn`) |

---

## How to Build

Documented build commands from [docs/MPC.md](docs/MPC.md) and [docs/RELEASE.md](docs/RELEASE.md):

### 1. Akai MPC Package Build
Building the MPC package requires Docker and the MPC plugin SDK (`MPC_SDK` environment variable):

```sh
# Build both 32-bit (ARMv7) and 64-bit (AArch64) binaries into Safian - VST - SynthyBoy:
scripts/build-mpc.sh

# Build a single architecture:
scripts/build-mpc.sh --arch arm32     # or scripts/build-armv7-mpc.sh (for Key 61)
scripts/build-mpc.sh --arch arm64     # (for MPC XL)

# Package the finished MPC release zip (SynthyBoy-MPC-<version>.zip):
python3 scripts/package-mpc-release.py
```

To install on an MPC device: copy `Safian - VST - SynthyBoy` into `VST2 Plugins/` at the root of a USB stick, SD card, or internal storage (MPC OS 3.10.1+; enable Enhancements and External Plugin Support in Preferences).

### 2. Desktop Builds (macOS / Linux / Windows)
Prerequisites: CMake >= 3.22, C++17 compiler, Ninja (recommended).

```sh
# Convenience wrapper script (Release build into ./build):
scripts/build.sh
scripts/build.sh --clean
scripts/build.sh --debug

# Or using CMake directly:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### 3. Local Release Flow (Multi-Platform)
```sh
scripts/make-local-release.sh              # linux-armv7 (Docker) + host platform + packs -> dist/
scripts/make-local-release.sh --with-juce  # includes host JUCE CLAP / VST3
scripts/make-local-release.sh --no-armv7   # without Docker
```

### 4. Verification and Tests
```sh
pytest scripts/tests/test_params_doc.py       # verifies docs/PARAMETERS.md matches docs/params.json
python3 -I scripts/check-manifest.py          # validates plugin.json against schema
```

---

## Repository Layout

| Directory / File | Description |
|------------------|-------------|
| `core/` | `synthyboy_core` JUCE-free C++17 static library: public headers in `include/synthyboy/` (`IEngine`, parameter definitions, state format version 5 <!-- core/include/synthyboy/state.h:16 -->, quality types, events, content store). `src/`: 2637 <!-- docs/params.json -->-parameter table, state serialization. `engine-own/`: own engine with ARM NEON SIMD optimizations (used on MPC and iOS). `engine-vital/`: Vital-derived DSP adapter. Modular DSP libraries: `filters-serum/`, `fx-serum-a/`, `fx-serum-b/`, `sample-osc/`, `tuning/`, `formula/`, `shapes/`, `wavetable-edit/`, `wavetable-render/`. |
| `shell-mpc/` | Standalone MPC VST2 plugin shell (`synthyboy_mpc`), device profiles (`device_profile.h`), and overlay touch UI (`shell-mpc/overlay/`). |
| `shell-juce/` | JUCE AudioProcessor plugin wrapper for desktop VST3 and Audio Unit. |
| `shell-clap/` | Native CLAP plugin implementation with `com.safian.bitmap-gui/1` extension support. |
| `content/presets/` | 112 <!-- content/presets --> factory presets in 6 categories (`contentpack.json`, CC0-1.0). |
| `docs/` | Architecture specs, MPC build guide ([docs/MPC.md](docs/MPC.md)), release guide ([docs/RELEASE.md](docs/RELEASE.md)), parameter reference ([docs/PARAMETERS.md](docs/PARAMETERS.md)), parameter table JSON (`docs/params.json`), user guide ([docs/user-guide/SynthyBoy-MPC-User-Guide.md](docs/user-guide/SynthyBoy-MPC-User-Guide.md)), changelog draft ([docs/release/CHANGELOG-draft.md](docs/release/CHANGELOG-draft.md)), release tracking ([docs/release/OPEN-QUESTIONS.md](docs/release/OPEN-QUESTIONS.md)). |
| `scripts/` | MPC build scripts (`build-mpc.sh`, `package-mpc-release.py`), release automation (`make-local-release.sh`), UI generators (`gen-mpc-ui.py`), parameter doc generator (`gen-params-doc.py`), test suite (`scripts/tests/`). |
| `tools/` | Standalone utilities: `synthyboy_paramdump`, `contentpack.py`, `preset-render`. |
| `bench/` | Benchmarking utilities: `synthyboy_bench`, `synthyboy_tier_bench`, `profile_compare`. |
| `tests/` | Integration and smoke tests (`clap-smoke`, etc.). |

---

## License

Copyright (C) 2026 Safian Szabolcs.

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU Affero General Public License, version 3 or (at your option) any later version** (`AGPL-3.0-or-later`, see [LICENSE](LICENSE)).

- The factory presets in `content/presets/` are released under the **Creative Commons Zero 1.0 Universal** public domain dedication (`CC0-1.0`, see `content/presets/contentpack.json`).
- Dependencies, bundled components, and licensing details: [THIRD_PARTY.md](THIRD_PARTY.md) and [NOTICE](NOTICE).

VST is a registered trademark of Steinberg Media Technologies GmbH.
