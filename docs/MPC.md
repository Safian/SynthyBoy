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

Measured on 2026-11-13 in the evening, after the later optimisation rounds (the final state: engine commit `91f2d75`, shell branch commit `17d6e11`), with the bench option `--preset-dir` on all 112 factory presets, 8 notes, tier low, 2 s per preset, notes retriggered after 1 s. Average of the 112 block-time averages, and how many presets have a 99th percentile above 2.0 ms or a worst block above 2.902 ms, per optimisation round (engine commit):

| Engine commit | Average of averages | 99th percentile above 2.9 ms | 99th percentile above 2.0 ms | Worst block above 2.902 ms |
|---|---|---|---|---|
| `bc0f3ce` | 1.20 ms | 9 | 67 | 55 |
| `e3abe08` | 1.06 ms | 6 | 44 | 57 |
| `178316e` | 1.00 ms | 4 | 29 | 29 |
| `e596f94` | 0.98 ms | 0 | 6 | 35 |
| `91f2d75` | 0.87 ms | 0 | 0 | 0 |

In the final state, 8 notes: no preset has a 99th percentile above 2.0 ms and none has a worst block above 2.902 ms; six presets have a worst block above 2.0 ms, listed as average / 99th percentile / worst: Diffusor Cloud Pad 1.75 / 1.96 / 2.74 ms, Scream Filter Bass 1.63 / 1.86 / 2.36 ms, EMS Squelch Bass 1.50 / 1.71 / 2.24 ms, Diode Saturated Bass 1.44 / 1.66 / 2.13 ms, Twin Warp Lead 1.23 / 1.52 / 2.05 ms, Formant Vowel Bass 1.34 / 1.60 / 2.01 ms. With 4 notes the average of the averages is 0.666 ms and two presets have a worst block above 2.0 ms. The per-preset results of every round are kept as CSV files next to the measurement notes of the project.

Final state, other bench results (2026-11-13): the note-on of 8 notes takes 0.715 ms on average and 1.018 ms in the worst case (before the last round: 0.97 / 1.34 ms), 1 note 0.122 / 0.371 ms. The chorus section of a block went from 0.359 to 0.201 ms. In the slowest 1 % of the blocks of Moog Ladder Bass the number of voice render calls per block is 33.3 (all blocks: 32.0), so a note-on block no longer runs more voices than a held block.

What follows from these numbers:

- The synthetic heavy patch with 8 notes does not fit into the 2.902 ms block even without unison (cap 1). The last measurement of it was made at engine commit `878326f` (cap 3: average 4.42 ms, 99th percentile 6.35 ms, worst 6.83 ms); there is no new measurement of the heavy patch for the final state.
- The 112 factory presets do fit with 8 notes (see the table above), so no cap on the number of notes is needed. No decision about such a cap is pending.
- The default of the unison cap is 3; that there is a cap is the owner's decision. A cap of 3 affects 14 of the 112 factory presets (`scripts/preset-unison-report.py` lists every preset): Fat Reese, Cosmic Drift Lead, Hyper Saw Lead, Diffusor Cloud Pad, Ethereal Glass, Frozen Aurora, Harmonic Cloud, Infinite Horizon, Lush Analog Pad, Space Odyssey Pad, Tri-Band Split Texture, Velvet Strings, Vintage Brass Pad, Pizzicato Strings. The heaviest factory preset has 2 oscillators with 7 unison voices each (Hyper Saw Lead).

Saving a project and loading it again, tried on 2026-11-13 in the afternoon:

- After the MPC application was restarted, the project (Sbtest) loaded with the preset name "Acid Saw Reso *" and the edited values: Osc 1 Level 79.4, Filter 1 Cutoff 450, Resonance 78. Nobody listened to the sound after loading.
- With the plugin loaded and idle, the MPC process uses 4 to 7 % of the CPU.

### Per the SDK documentation / not yet verified on the device

These statements come from the SDK's documents (host-contract.md, install-and-test.md). A project was saved and loaded once (see above), but in which order the host restores the program and the chunk was not observed, so the host side of the program handling below is unconfirmed. Still not verified on the device: the CPU load during playback in the MPC app, and the sound of the final build (shell commit `17d6e11`): it loads in the stock MPC app, but nobody has listened to it.

- The sample rate is fixed, and the user can choose other block sizes (32, 64, 96, 192, 256, 512) in Preferences; the announced block size is a maximum. The shell handles any smaller count.
- The host has no crash protection for plugins: a crash in the plugin takes the MPC application down.
- The host selects program 0 after loading and reads every program name by index.
- The host saves the program number and the plugin's chunk in the project and restores both on load. In which order it does so, and whether it selects the saved program again after restoring the chunk, is not known.

What the shell does with programs and the chunk is its own behaviour and is covered by the acceptance tests, whatever the host's order turns out to be. `effGetChunk` returns a JSON text starting with `{"mpcProgram":N,"mpcProgramName":"<escaped name>",` followed by the engine state. On `effSetChunk` the shell reads the program number and name and calls `resolveProgram` to map them to the current preset list. The name decides: the stored index is used only if the program at that index has that name (or to choose between programs of the same name), otherwise the first program of that name is taken, so a project still finds its program when a later package has a different preset list. A chunk from an older build that has only the number still loads. If the name is not in the list, `resolveProgram` returns -1 and the shell enters a "no program" state: the sound is the one stored in the chunk, the host is given index 0, the saved name is shown as the program name and written into the next chunk again. In case the host selects the saved program number again right after restoring the chunk, that one selection is ignored (the chunk wins); any other program selection or a parameter edit ends this protection, and it is never armed in the "no program" state.

## Sound differences at tier low

The MPC build runs the engine at `Tier::low` (`core/include/synthyboy/quality.h`). Everything below applies only at that tier; the other tiers and the offline render (which runs on an engine at the high tier) are not changed by it, and the stored parameter values and the saved state are never changed, so a preset keeps its shape and a project saved on the MPC sounds in full on a desktop. Error bounds are absolute for a signal whose full scale is 1, unless they are called relative.

- **Unison cap**: at most 3 unison voices per oscillator (the tier's own cap is 4). A patch that asks for 7 voices sounds like the same patch with 3: the same detune width, stereo spread, blend and level law. `setLowTierUnisonCap` changes the number (0 = no cap).
- **Warps**: a warped oscillator runs 2x oversampled with a 32-tap Kaiser decimator instead of 4x with 128 taps, so extreme warps alias a little more and the warped oscillator is about 8 instead of 16 samples late. The table band limit of a warped oscillator is 1.0 times the host rate instead of 3.2 times. The decimator sums its taps in four partial sums (relative difference about 1e-7).
- **Spectral morph**: the morph cycle is rebuilt every 512 samples instead of every 256, so a modulated morph lags by up to about 12 milliseconds instead of 6. The cycle comes from a transform of half the size (difference to the full transform 4e-7). By default the unison voices keep the morph spread (two cycles); `setLowTierMorphSpread(false)` shares one cycle between them. A new note builds its morph cycle at once (this also fixed a bug that left the first period of a morphing note stale).
- **tanh** is a [7/6] Pade rational function with an absolute error below 1e-4 (clamped to plus or minus 1 beyond 4.972) in the classic filter models (the limiter of the Digital model, Ladder, Diode, Dirty), the phaser feedback, the distortion shapes and the warps.
- **sin and cos** come from a 1024-entry table with linear interpolation (error below 5e-6): the chorus LFO, the sine shaper of the distortion, the bend, sine fold and sine shaper warps.
- **exp, exp2, pow, 10 to the power x and log2** are polynomial forms (exp2 relative error about 3e-9, exp, 10^x and pow about 1e-6, log2 absolute 6e-8) in the phaser, the distortion drive and shapes, the compressor's power terms and the modulated parameter values.
- **Moving filters**: for the Ladder and the state variable models (Analog, Dirty, Digital) with a moving cutoff the setup (tan, powers, square roots) is computed at both ends of the 32-sample control period and interpolated linearly over four pieces of 8 samples (relative difference about 1e-4); both channels run in one loop with one setup. The Diode divides by constants of the block through reciprocals (a few units in the last place), and the Ladder divides by `1 + k G^4` through a reciprocal (1 unit in the last place).
- **Serum filters**: the smoothed cutoff and resonance that the filter runs on are held for 4 samples (the coefficient lags the smoothed cutoff by at most 3 samples of a 30 Hz one-pole filter).
- **Economy modulators**: LFOs, random LFOs and envelopes 2 to 6 that no modulation slot or custom shape reads are stepped every fourth control period instead of every period; their displayed values are up to 1.4 milliseconds old, and a route added during a note finds them at the phase they have.
- **Chorus and phaser**: the chorus delay times are held for 4 samples (a change below 0.004 samples), and the phaser stage and band coefficients are updated every 8 samples (a few thousandths of an octave).
- **Noise**: the six uniform numbers of the Gaussian noise come from three draws of the generator (16 bits from each half); the variance and the colour are the same, the random sequence is a different one.
- **Effect racks on silence**: a rack whose input has been exactly zero and whose output has stayed below minus 120 decibels full scale for longer than the longest gap between echoes (20 seconds if a delay or a pool slot is in the rack, 1 second otherwise) is not processed; its LFOs keep their pace through `idleAdvance` calls, and the rack wakes with the first non-zero input.
- **Stolen voices**: when a note is stolen (Kill mode) the voice stops at once and its last output sample on each bus is faded to zero in 3 milliseconds by the engine (`setLowTierKillFade`; the medium and high tiers let the voice itself fade in 50 milliseconds). The ramp is a held value, like a short thump, not the voice's own sound.
- **Events**: note and controller events take effect at the start of the 32-sample control period that contains their frame, up to 0.73 milliseconds early, instead of sample accurately; a block is no longer cut into pieces by events.
- **Bypassed effects and polyphony**: `Tier::low` bypasses reverb, flanger, Hyper/Dimension, Bode, and Convolve (the bit mask in `quality.h`). A preset that uses them plays without them. The remaining effects (chorus, compressor, delay, distortion, EQ, the effect filter, phaser) are still active. The tier caps polyphony at 8 voices.

Changes that do not alter the sound at any tier (they compute the same samples with less work) are not listed here: caches of filter and morph results, coefficients computed once per block, no work for sources that are switched off, NEON loops that equal the scalar code bit for bit, and the exact floor.

## Benchmark switches

The benchmark `synthyboy_bench` (`bench/bench_engine.cpp`) and `synthyboy_noteon_bench` run on the device as they are (built by `cmake` for the armv7 toolchain with `-DSYNTHYBOY_BUILD_TESTS=ON`). The switches used for the measurements above:

- `--preset FILE` and `--preset-dir DIR` play `.sbpreset` files through the same loading path as the shell (parameters by `setParam`, then the content); the directory mode prints one line per preset, the presets over the block budget in the worst block and the top 15 by average. `--csv FILE` writes name, average, 99th percentile and worst block of every preset.
- `--voices K` plays K notes (default: the voices of the tier); `--tier low|medium|high`, `--seconds N`, `--sr 44100`, `--retrigger S`.
- `--sections` prints the exclusive time of each part of a block, and the same for the slowest 1 % of the blocks with the number of voice ticks and render calls. It needs a build with `-DSYNTHYBOY_SECTION_TIMING=ON`; the clock of a scope costs about one microsecond on the device, so small sections are inflated.
- `--unison-cap N` (0 = none), `--morph-spread 0|1`, `--kill-fade MS` set the tier low options of the engine described above. `--ftz` sets flush-to-zero like the plugin; `--idle` plays no notes.
