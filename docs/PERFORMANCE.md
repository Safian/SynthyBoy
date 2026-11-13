# Performance

**These are NOT numbers for the Akai MPC Key 61 (Linux ARMv7, two isolated cores) or for any phone.** They were taken on a
development laptop, in a release build, with other work running on the machine. They show how the two engines compare and
give a rough scale; the architecture's tier numbers (docs/ARCHITECTURE.md 3.6) stay targets until measured on the devices.

## How

`bench/bench_engine.cpp` (`synthyboy_bench`, built with `SYNTHYBOY_BUILD_TESTS`): renders `--seconds` of audio (after one
second of warm-up) in blocks of `--block` frames through an engine, with `K` held notes (keys spread over five octaves,
velocities 0.6 .. 1.0) that are released and retriggered every second, and measures the time spent inside `IEngine::process()`
for every block (`std::chrono::steady_clock`). Real-time factor = audio seconds / seconds in `process()`. It prints the requested
configuration (K voices, everything else at full quality) and then one run per quality tier with that tier's quality (voices
8 / 16 / 32, unison cap 4 / 8 / 16, oversampling 1x / 2x / 2x, interpolation, bypassed effects: core/include/synthyboy/quality.h),
for two patches. Nothing is asserted.

* **default patch**: the table's defaults: one saw oscillator, one voice, filter 1 (Digital, 12 kHz), master -12 dB, no effects.
* **heavy patch**: three oscillators (saw, square, saw-to-square with the Vocode morph) with 7 unison voices each, two filters in series
  (Ladder + Analog), three LFOs and one envelope routed through the modulation matrix (5 slots), all nine effects on. On the low tier
  the unison cap (4) and the bypassed reverb and flanger make it cheaper than the same patch on the others: that is the point of the tiers.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_PLUGIN=OFF
cmake --build build
build/bench/synthyboy_bench --engine own   --seconds 10          # both patches (--patch default | heavy | both)
build/bench/synthyboy_bench --engine vital --seconds 10
```

## Machine

| | |
|---|---|
| Date | 2026-11-04 (Phase 3 numbers; the Phase 2 numbers of the same machine were in the earlier version of this file) |
| Machine | MacBook Pro, Apple M5 Pro, 18 cores, 64 GB (`Darwin 27.2.0 arm64`, macOS 27.2) |
| Compiler | Apple clang 21.0.0, `-O3 -DNDEBUG` (CMake Release), NEON on the SIMD path of Vital |
| Load | the machine was not idle: load average 3 .. 4 during the runs (other builds and sessions), the benchmark thread is single threaded |
| Block | 128 frames at 48 kHz (budget 2.667 ms per block) |

**These are laptop numbers, NOT numbers of the Akai MPC Key 61 or of a phone.**

## Results (macOS arm64, 10 s per run)

Time per block of 128 frames in ms: mean / 99th percentile / worst. The "requested" rows (8 voices at full quality) are in
the raw outputs; the tier rows are the ones that matter.

Default patch:

| Tier | Voices | own: real-time factor | own: mean / p99 / worst | vital: real-time factor | vital: mean / p99 / worst |
|------|--------|-----------------------|-------------------------|-------------------------|---------------------------|
| low | 8 | 298x | 0.0090 / 0.0092 / 0.0392 | 44.5x | 0.0600 / 0.2042 / 0.2903 |
| medium | 16 | 154x | 0.0174 / 0.0187 / 0.0433 | 17.0x | 0.1571 / 0.4798 / 0.7856 |
| high | 32 | 80x | 0.0332 / 0.0385 / 0.0590 | 9.2x | 0.2894 / 0.5089 / 1.2249 |

Heavy patch (the own engine implements none of it and plays its default patch):

| Tier | Voices | own: real-time factor | own: mean / p99 / worst | vital: real-time factor | vital: mean / p99 / worst |
|------|--------|-----------------------|-------------------------|-------------------------|---------------------------|
| low | 8 | 321x | 0.0083 / 0.0089 / 0.0288 | 10.6x | 0.2525 / 0.8297 / 1.1093 |
| medium | 16 | 161x | 0.0166 / 0.0180 / 0.0330 | 3.2x | 0.8335 / 2.6773 / 3.4692 |
| high | 32 | 81x | 0.0329 / 0.0385 / 0.0675 | 1.8x | 1.5135 / 2.8713 / 6.1405 |

Linux aarch64 (earlier numbers, Phase 2, Docker VM on the same Mac, one 2 s run, saw, 8 voices): vital 28x, mean 0.0946 ms per block.
Not repeated in Phase 3.

## What the numbers say, and what they do not

* The Vital engine costs about 7 to 9 times the own engine per voice on the default patch (about 9 us per voice and block of 128
  frames here, own about 1 us), as expected for a wavetable engine with 2x oversampling, a stereo signal path and a per-voice filter
  model. The own engine is a minimal one-oscillator, one-pole design, so the ratio is a statement about these two designs, not about
  quality of implementation.
* The heavy patch (21 unison voices per note, two filters, nine effects) costs about 5 times the default patch per voice: 47 us per
  voice and block on the high tier. **At 32 voices the heavy patch does not run in real time on one core of this machine**: the 99th
  percentile is 2.87 ms against a budget of 2.667 ms, the worst block 6.1 ms (the blocks in which the held notes are retriggered are the expensive ones).
  The low tier (8 voices, unison cap 4, no reverb / flanger) runs it at 10.6x real time here. Whether the numbers hold on an ARMv7
  core of the MPC is exactly what the benchmark request (docs/MPC-BENCH-REQUEST.md) is for.
* The parameter transfer of the shell costs 0.4 us per block when nothing changes (880 atomic loads and compares) and 13 us when every
  parameter changes in one block; the adapter's own per-block work (change detection over 730 bindings, the specials, the 64 slots) is
  inside the numbers above.
* The worst block times are 2 to 9 times the mean: the blocks in which the held notes are retriggered (voice start, stolen voices
  fading) are the expensive ones. A device budget must be met by the worst block, not by the mean.
* Transferring to the MPC: an ARMv7 core of that class is several times slower than one performance core of this machine; no
  factor is assumed here. The tier voice limits for the Low tier (8 at the moment) must be decided from a measurement on the
  device (benchmark harness: `synthyboy_bench --engine vital --voices 8`); iOS gets the own engine by the owner's decision
  (ARCHITECTURE 2.2). The measurements on an MPC are in docs/MPC.md (own engine, tier low); the first measurement on a mid-range phone is still open (ARCHITECTURE section 9, Phase 2).
* The CI job `linux-aarch64` prints the same benchmark for the Vital engine on GitHub's arm runner (informational, shared
  cloud machine).
