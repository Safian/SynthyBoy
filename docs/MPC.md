# SynthyBoy MPC Build Guide

## Build

The MPC build is a 32-bit ARMv7 (hard-float, NEON) VST2 module for the Akai MPC Key 61. The build script is `scripts/build-armv7-mpc.sh`; the host needs Docker. The environment variable `MPC_SDK` must point at a checkout of the MPC plugin SDK (it is not part of this repository and must not be copied into it). The environment variable `ARMV7_IMAGE` selects the Docker image (default: the pinned Debian 12 image in the script); an image that already has the cross toolchain installed is faster because the script skips the `apt-get install` step inside the container.

The exact command line is `scripts/build-armv7-mpc.sh [--out DIR] [--jobs N] [--build-dir DIR] [--skins DIR]`. The script does the following, step by step:

1. It runs a `docker run` container mounting the source tree read-only, the build directory, the output directory, and the SDK.
2. Inside the container it installs missing packages (the cross compiler, `binutils-arm-linux-gnueabihf`, `cmake`, `file`, `git`, `ninja` or `make`).
3. It runs `cmake` with the toolchain file `cmake/toolchains/armv7-linux-gnueabihf.cmake`, with `-DSYNTHYBOY_BUILD_PLUGIN=OFF`, `-DSYNTHYBOY_BUILD_TESTS=OFF`, `-DSYNTHYBOY_BUILD_MPC_SHELL=ON`, `-DSYNTHYBOY_WITH_VITAL=OFF`, and `-DSYNTHYBOY_SHELL_ENGINE=own`. The package is the own engine; Vital is not built.
4. It builds the `synthyboy_mpc` target.
5. It checks that the resulting `.so` is an ARMv7 hard-float ELF file (using `file` and `readelf`).
6. It checks that the `.so` needs only `libc`, `libm`, `libdl`, `libpthread`, and `ld-linux-armhf` (no other shared libraries).
7. It checks that `VSTPluginMain` is the only exported symbol (using `nm -D`).
8. It produces a stripped copy (`synthyboy_mpc.stripped.so`) for the package; the unstripped module stays in the build tree for profiling.
9. On the host it assembles the plugin folder `Safian - VST - SynthyBoy` with `VST/synthyboy-arm32.so` (the stripped copy) and `Content/presets` (a copy of `content/presets`).
10. It generates the Plugin Skins from `scripts/mpc-ui-pages.json` (using `scripts/gen-mpc-ui.py` and the SDK's `tools/gen_ui.py`), unless `--skins DIR` is given.
11. It generates `plugin.json` and `version.xml` via `scripts/gen-mpc-manifest.py`.
12. It runs the SDK's `tools/validate.py` on the finished folder. The validator's schema lists `file64` as required, while the SDK's `plugin-folder-format.md` says a 32-bit MPC uses only `file32`; the build script accepts exactly that one finding and fails on any other error.

The module must be built with GCC, not with Zig: `--exclude-libs,ALL` is not understood by Zig's linker and a Zig-built module crashed at host exit on the device.

The native acceptance is `sh shell-mpc/accept.sh` with `MPC_SDK` set. It builds the module natively on the development machine, runs the SDK host simulator with tests, checks the parameter table against `docs/params.json`, verifies the exported symbols, tests the factory presets as programs, runs the stub host for state round-trips, and builds and runs with ThreadSanitizer to check for data races.

## Package layout

The output folder is named `Safian - VST - SynthyBoy` and contains:

- `plugin.json` — the MPC manifest. It has only `file32` (pointing to `VST/synthyboy-arm32.so`); there is no 64-bit build, so `file64` is absent. The SDK validator reports the missing `file64` and the build script accepts exactly that finding.
- `version.xml` — the content version of the package (the root manifest version with a fourth part) and, when `Plugin Skins` is present, the `uiversion` of the skins.
- `VST/synthyboy-arm32.so` — the stripped 32-bit ARMv7 shared object that exports only `VSTPluginMain`.
- `Content/presets` — the factory presets from `content/presets`, shown as programs in the MPC's preset browser.
- `Plugin Skins` — the touch-screen UI and Q-Links knob mapping, generated from `scripts/mpc-ui-pages.json` via `scripts/gen-mpc-ui.py` and the SDK's `tools/gen_ui.py`.

The plugin finds its own folder by walking up from the binary's path (obtained via `dladdr` on `VSTPluginMain`) to the first directory that contains a file named `plugin.json`. This logic is in `shell-mpc/plugin_paths.h`.

## Install on the MPC

Copy the whole folder `Safian - VST - SynthyBoy` into a folder named exactly `VST2 Plugins` at the root of a USB drive, SD card, or the MPC's internal drive. The MPC must be running MPC OS 3.10.1 or later. In **Preferences → Enhancements (Beta)** turn on both **Enable Enhancements** and **External Plugin Support**; the plugin stays hidden until both switches are on. After inserting the drive, open the plugin browser (instrument track → plugin program → browse, or an insert slot). The plugin is listed under **External Plugins**.

Replacing a plugin: copy the new folder over the old one, then remove and re-add the plugin on the track (or restart the MPC if the skins changed). Uninstalling: delete the folder from `VST2 Plugins`. Projects that used it will load without the plugin.

## Known host behaviour

### Observed on the device

Seen on an MPC Key 61 with firmware 3.10.1:

- The host runs plugins at 44.1 kHz with 128 frame blocks.
- There is no plugin GUI. The screen pages come from Plugin Skins (`TUI.json`), and the hardware knobs are mapped from `Q-Links.json`.
- A newly copied plugin appears only after the MPC application is restarted.
- The plugin runs inside the MPC's process. For that reason the shell wraps the instance creation and every dispatch call in `catch (...)` (no instance, or a result of 0), so no exception leaves the plugin.
- A module built with Zig crashed when the host exited (see Build): the module is built with GCC.
- The packaged plugin loads in the stock MPC app; stepping through the presets works; the generated skin with its 30 pages works; the owner checked the sound by ear (it plays without crackling).

Measured on 2026-11-13 with the bench on the device (MPC Key 61, 44.1 kHz, 128 frame blocks, so one block lasts 2.902 ms; tier low, own engine):

- Basic patch, 8 notes held: block time average 0.535 ms, 99th percentile 0.789 ms, worst 1.405 ms.
- Idle (no notes): 0.022 ms per block. Each additional note costs about 0.070 ms per block.
- Note-on of 8 notes: average 0.97 ms, worst 1.34 ms.
- Preparing the engine (prepare) takes 542 ms.
- Heavy patch (3 oscillators, 2 filters, effects, 8 notes; the patch asks for 7 unison voices per oscillator, tier low ran 4): average 5.41 ms, 99th percentile 8.09 ms, worst 9.20 ms, which is longer than one block.
- The bench option `--ftz` makes no difference to these times.

Measured on 2026-11-13 in the afternoon with a unison cap in the engine (engine commit `c3a2065`), the same heavy patch with 8 notes, block time average / 99th percentile / worst:

- Cap 1: 3.73 / 5.46 / 6.04 ms.
- Cap 2: 4.16 / 5.43 / 5.67 ms.
- Cap 3: 5.36 / 8.27 / 8.98 ms.
- Cap 4: 5.97 / 9.00 / 9.62 ms.
- After one optimisation round (engine commit `4cd77ce`): cap 3: 4.64 / 5.52 / 5.66 ms; cap 1: 3.64 / 4.85 / 5.40 ms; the basic patch with 8 notes: 0.559 / 0.781 / 1.016 ms.

What follows from these numbers:

- The heavy patch with 8 notes does not fit into the 2.902 ms block even without unison (cap 1).
- The default of the unison cap is 3; that there is a cap is the owner's decision. No decision has been made about a cap on the number of notes.
- A cap of 3 affects 14 of the 112 factory presets (`scripts/preset-unison-report.py` lists every preset): Fat Reese, Cosmic Drift Lead, Hyper Saw Lead, Diffusor Cloud Pad, Ethereal Glass, Frozen Aurora, Harmonic Cloud, Infinite Horizon, Lush Analog Pad, Space Odyssey Pad, Tri-Band Split Texture, Velvet Strings, Vintage Brass Pad, Pizzicato Strings. The heaviest factory preset has 2 oscillators with 7 unison voices each (Hyper Saw Lead).

Saving a project and loading it again, tried on 2026-11-13 in the afternoon:

- After the MPC application was restarted, the project (Sbtest) loaded with the preset name "Acid Saw Reso *" and the edited values: Osc 1 Level 79.4, Filter 1 Cutoff 450, Resonance 78. Nobody listened to the sound after loading.
- With the plugin loaded and idle, the MPC process uses 4 to 7 % of the CPU.

### Per the SDK documentation / not yet verified on the device

These statements come from the SDK's documents (host-contract.md, install-and-test.md). A project was saved and loaded once (see above), but in which order the host restores the program and the chunk was not observed, so the host side of the program handling below is unconfirmed. Still not verified on the device: the CPU load during playback in the MPC app.

- The sample rate is fixed, and the user can choose other block sizes (32, 64, 96, 192, 256, 512) in Preferences; the announced block size is a maximum. The shell handles any smaller count.
- The host has no crash protection for plugins: a crash in the plugin takes the MPC application down.
- The host selects program 0 after loading and reads every program name by index.
- The host saves the program number and the plugin's chunk in the project and restores both on load. In which order it does so, and whether it selects the saved program again after restoring the chunk, is not known.

What the shell does with programs and the chunk is its own behaviour and is covered by the acceptance tests, whatever the host's order turns out to be. `effGetChunk` returns a JSON text starting with `{"mpcProgram":N,"mpcProgramName":"<escaped name>",` followed by the engine state. On `effSetChunk` the shell reads the program number and name and calls `resolveProgram` to map them to the current preset list. The name decides: the stored index is used only if the program at that index has that name (or to choose between programs of the same name), otherwise the first program of that name is taken, so a project still finds its program when a later package has a different preset list. A chunk from an older build that has only the number still loads. If the name is not in the list, `resolveProgram` returns -1 and the shell enters a "no program" state: the sound is the one stored in the chunk, the host is given index 0, the saved name is shown as the program name and written into the next chunk again. In case the host selects the saved program number again right after restoring the chunk, that one selection is ignored (the chunk wins); any other program selection or a parameter edit ends this protection, and it is never armed in the "no program" state.

## Sound differences at tier low

The MPC build runs the engine at `Tier::low` (`core/include/synthyboy/quality.h`). The following things sound or behave differently from the desktop tiers:

- **Oversampling**: the tier sets the engine's oversampling to 1 (the other tiers use 2). Warped oscillators run at 2× oversampling with a 32-tap decimator instead of 4× with 128 taps (commit `419b4d9`). The spectral morph is rebuilt every 512 samples instead of every control period. The consequence is less aliasing protection on warped oscillators and a coarser spectral morph movement.
- **Classic filters**: the `tanh` saturation in the classic filter models (Diode Ladder, etc.) is replaced by a [7/6] Pade rational approximation with absolute error below 1e-4 (commit `068f03f`). The Ladder L/R pair is processed in a single loop for better throughput. The saturation character is very close to the original but computationally cheaper.
- **Economy LFOs**: at tier low, LFOs that no modulation slot reads are updated less often (every fourth control period instead of every period), using a tick counter to phase the decimation (commit `4a04871`). Unread LFO outputs keep their last values.
- **Effect racks on silence**: effect racks whose input has been exactly zero and whose output has stayed below a very low threshold for longer than the longest gap between echoes (4 seconds for delay or pool slots, 1 second for everything else) are not processed — their audio path is skipped entirely. Their LFOs keep their pace through `idleAdvance` calls so that when sound returns the effect is in the correct phase (commit `930a2d6`).
- **Fast math**: per-sample calls to `tanh`, `sin`, `cos`, `exp`, `exp2`, and `pow` in the chorus, phaser, distortion, and warp functions are replaced by cheaper approximations from `core/engine-own/dsp/fastmath.h` (commit `26234bd`). The chorus and phaser LFO phase updates use the fast `sin` and `cos`. The warp phase and sample functions use the fast `tanh` and `sin`. Each approximation states its error bound in that header.
- **Bypassed effects**: `Tier::low` bypasses reverb, flanger, Hyper/Dimension, Bode, and Convolve (the bit mask in `quality.h`). A preset that uses them plays without them. The remaining effects (chorus, compressor, delay, distortion, EQ, the effect filter, phaser) are still active. The tier also caps polyphony at 8 voices and unison at 4 voices per oscillator; stored parameter values are never changed, so a preset keeps its shape.
- **A plain table read loop**: for oscillators without warps and without a cross signal, the engine uses a straight sample-at loop without the branch overhead of the general path (commit `419b4d9`).
- **Filter stale handling**: an unconfigured filter that was turned off is not re-configured until it is switched on again, saving the filter setup work (commit `4a04871`).
