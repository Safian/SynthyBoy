# SynthyBoy

SynthyBoy is a wavetable synthesizer for standalone Akai MPC hardware (MPC Key 61, MPC XL and the other standalone MPCs). Its own C++ engine powers the MPC VST2 plugin.

> Version 0.1.0 is in development. The plugin has been exercised on the Key 61 and XL.

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

## MPC plugin

The MPC VST2 build (`scripts/build-mpc.sh`, [docs/MPC.md](docs/MPC.md); end-user instructions: [MPC Installation Guide](docs/user-guide/INSTALL.md) and [MPC User Guide](docs/user-guide/SynthyBoy-MPC-User-Guide.md)) is the product. A release is two archives, armv7 (Key 61 and the other 32-bit devices) and aarch64 (MPC XL), see [docs/RELEASE.md](docs/RELEASE.md). To install: copy the `Safian - VST - SynthyBoy` directory to a folder named `VST2 Plugins` on the MPC's drive (see [INSTALL.md](docs/user-guide/INSTALL.md)).

## Documentation map

| Topic | Start here | Evidence and history |
|---|---|---|
| Using the MPC instrument | [Installation](docs/user-guide/INSTALL.md), [playing guide](docs/user-guide/SynthyBoy-MPC-User-Guide.md), [magyar kézikönyv](docs/user-guide/hu/SynthyBoy-MPC-felhasznaloi-kezikonyv.md) | [MPC hardware observations](docs/MPC.md) identify the model, build and limits of individual checks. |
| Controls and screen changes | [MPC screen map](docs/ui-map/README.md), [native component catalogue](docs/ui-components/README.md) | The screen map links before/after captures. [UI design](docs/ui-design/README.md) records earlier proposals, coverage decisions and implementation reports. |
| Engine, presets and content | [Engine](core/engine-own/README.md), [parameter reference](docs/PARAMETERS.md), [content packs](docs/CONTENT-PACKS.md) | [Engine specification and test vectors](docs/engine-spec/README.md), [factory sound catalogue](docs/user-guide/factory-sounds.md) |
| Performance and device limits | [Measurement protocol](docs/perf/README.md), [DSP findings by topic](docs/dsp-knowledge/README.md) | Each finding identifies its evidence. Raw hardware captures are local to the owner under the shared `.hw-local/mpc-perf/`. |
| Building and distributing | [Release procedure](docs/RELEASE.md), [MPC build guide](docs/MPC.md) | [Development history](docs/release/HISTORY-by-week.md) and linked reports record the versions they describe. |
| Project rules and outstanding work | [Project instructions](AGENTS.md), [current tasks and acceptance criteria][tasks] | Dated plans, review reports and local handoffs describe their original sessions. Check the current task and source before reusing a procedure or a device result. |

## Repository layout

| Path | Content |
|------|---------|
| `core/` | `synthyboy_core`: C++17 static library. `include/synthyboy/` public API (`IEngine`, `ParamInfo` table, state I/O, `Quality`, events), `src/` the parameter table and versioned state with its `content` section, content store, engine capability bits, number formatting, `engine-own/` the engine (see [Engine](#engine)), `tests/` unit, real-time (no allocation), thread and fuzz tests with an own tiny runner; the Serum-class packages are `tuning/`, `formula/`, `shapes/`, `wavetable-edit/`, `wavetable-render/`, `vital-import/`, `sample-osc/`, `filters-serum/`, `fx-serum-a/`, `fx-serum-b/` (each with a REPORT, overview: docs/ARCHITECTURE.md 3.1) |
| `cmake/toolchains/` | `armv7-linux-gnueabihf`, `aarch64-linux-gnu` cross toolchain files |
| `tools/contentpack/` | `contentpack.py`: builds, verifies and splits ContentPacks (zip + `contentpack.json`), see docs/RELEASE.md |
| `bench/`, `tools/paramdump/`, `tools/preset-render/` | `synthyboy_bench`: real-time factor and block times of a default and a heavy patch per tier; `synthyboy_paramdump`: the parameter table as JSON (`docs/params.json`); `synthyboy_preset_render`: renders the factory presets at every tier, checks them and prints the level spread and the velocity curve (`--wav DIR` writes the renders for listening) |
| `scripts/` | `build-mpc.sh` (the MPC package), `package-mpc-release.py` ([docs/RELEASE.md](docs/RELEASE.md)) |
| `docs/ARCHITECTURE.md`, `PARAMETERS.md`, `PERFORMANCE.md` | design, the parameter reference (generated), benchmark numbers |
| `shell-mpc/` | the MPC VST2 shell, the display layer (`overlay/`) and its GPU helper; build guide [docs/MPC.md](docs/MPC.md) |
| `docs/ui-components/` | the native component catalogue and the notes of the display layer (`shell-mpc/ui/`); entry page [docs/ui-components/README.md](docs/ui-components/README.md), instructions for AI agents [shell-mpc/ui/AGENTS.md](shell-mpc/ui/AGENTS.md) |

CMake options: `SYNTHYBOY_BUILD_TESTS` (ON), `SYNTHYBOY_BUILD_MPC_SHELL` (OFF; the MPC shell, needs `MPC_SDK_DIR`), `SYNTHYBOY_DEFAULT_TIER` (`high` | `medium` | `low`: the quality tier of the plugin).

## Engine

One engine sits behind `synthyboy::IEngine` (`createEngine (EngineKind::own)`): `core/engine-own`, original code, which acts on 2634 of the 2637 parameters of the table ([core/engine-own/README.md](core/engine-own/README.md)). The parameters are our own table (keys like `osc1.unison.detune`, our units): [docs/PARAMETERS.md](docs/PARAMETERS.md), machine readable `docs/params.json`. `build/bench/synthyboy_bench` prints the real-time factor ([docs/PERFORMANCE.md](docs/PERFORMANCE.md)). The importer of the `.vital` file format, `core/vital-import`, reads that format only; the source of `core/engine-own` holds no code derived from any other synthesizer.

## Building

Requirements: CMake >= 3.22, a C++17 compiler, git. Ninja is used when installed. The MPC plugin is built with `scripts/build-mpc.sh` ([docs/MPC.md](docs/MPC.md)).

### Core and tests

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Core only with sanitizers, or cross compiled for ARMv7 (needs `g++-arm-linux-gnueabihf` and `qemu-user`):

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S . -B build-armv7 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armv7-linux-gnueabihf.cmake \
  -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-arm;-L;/usr/arm-linux-gnueabihf"
```

## License

Copyright (C) 2026 Safian Szabolcs.

This program is free software: you can redistribute it and/or modify it under the terms of the
**GNU Affero General Public License, version 3 or (at your option) any later version**
(`AGPL-3.0-or-later`, see [LICENSE](LICENSE)).

Third-party components and their licences: [THIRD_PARTY.md](THIRD_PARTY.md), [NOTICE](NOTICE).

VST is a registered trademark of Steinberg Media Technologies GmbH.

[tasks]: TODO.md
