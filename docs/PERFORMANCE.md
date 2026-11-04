# Performance

**These are NOT numbers for the Akai MPC Key 61 (Linux ARMv7, two isolated cores) or for any phone.** They were taken on a
development laptop, in a release build, with other work running on the machine. They show how the two engines compare and
give a rough scale; the architecture's tier numbers (docs/ARCHITECTURE.md 3.6) stay targets until measured on the devices.

## How

`bench/bench_engine.cpp` (`synthyboy_bench`, built with `SYNTHYBOY_BUILD_TESTS`): renders `--seconds` of audio (after one
second of warm-up) in blocks of `--block` frames through an engine, with `K` held notes (keys spread over five octaves,
velocities 0.6 .. 1.0) that are released and retriggered every second, and measures the time spent inside `IEngine::process()`
for every block (`std::chrono::steady_clock`). Real-time factor = audio seconds / seconds in `process()`. It prints the requested
configuration and then one run per quality tier with that tier's voice limit (8 / 16 / 32). Saw and sine, default patch
(cutoff 8 kHz, gain -12 dB). Nothing is asserted.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_PLUGIN=OFF
cmake --build build
build/bench/synthyboy_bench --engine own   --seconds 10
build/bench/synthyboy_bench --engine vital --seconds 10
```

## Machine

| | |
|---|---|
| Date | 2026-11-04 |
| Machine | MacBook Pro, Apple M5 Pro, 18 cores, 64 GB (`Darwin 27.2.0 arm64`, macOS 27.2) |
| Compiler | Apple clang 21.0.0, `-O3 -DNDEBUG` (CMake Release), NEON on the SIMD path of Vital |
| Load | the machine was not idle: load average 11 .. 13 during the runs (other builds and sessions), the benchmark thread is single threaded |
| Block | 128 frames at 48 kHz (budget 2.667 ms per block) |

## Results (macOS arm64, 10 s per run)

Time per block of 128 frames in ms: mean / 99th percentile / worst. "Vital / own" is the ratio of the means.

Saw:

| Tier | Voices | own: real-time factor | own: mean / p99 / worst | vital: real-time factor | vital: mean / p99 / worst | vital / own |
|------|--------|-----------------------|-------------------------|-------------------------|---------------------------|-------------|
| low | 8 | 307x | 0.0087 / 0.0094 / 0.0213 | 32x | 0.0831 / 0.2479 / 0.7066 | 9.6x |
| medium | 16 | 149x | 0.0179 / 0.0360 / 0.1071 | 17x | 0.1569 / 0.4835 / 0.9574 | 8.8x |
| high | 32 | 74x | 0.0362 / 0.0724 / 0.1470 | 9.0x | 0.2974 / 0.6251 / 1.2003 | 8.2x |

Sine:

| Tier | Voices | own: real-time factor | own: mean / p99 / worst | vital: real-time factor | vital: mean / p99 / worst | vital / own |
|------|--------|-----------------------|-------------------------|-------------------------|---------------------------|-------------|
| low | 8 | 293x | 0.0091 / 0.0099 / 0.0482 | 32x | 0.0834 / 0.2464 / 0.6051 | 9.2x |
| medium | 16 | 146x | 0.0183 / 0.0287 / 0.0872 | 17x | 0.1573 / 0.4858 / 0.7597 | 8.6x |
| high | 32 | 73x | 0.0364 / 0.0572 / 0.1308 | 9.0x | 0.2968 / 0.6701 / 1.5311 | 8.2x |

Linux aarch64 (GCC 12.2, Release, in a Docker VM on the same Mac, single run of 2 s, saw, 8 voices; the VM is native aarch64, not
emulated): vital 28x, mean 0.0946 ms / p99 0.286 ms / worst 0.390 ms per block. (Run once, only to see that the Linux/NEON build
is in the same range as the macOS build; it is not a repeated measurement.)

## What the numbers say, and what they do not

* The Vital engine costs about 8 to 10 times the own engine per voice (about 9 us per voice and block of 128 frames here, own
  about 1 us), as expected for a wavetable engine with 2x oversampling, a stereo signal path and a per-voice filter model.
  The own engine is a minimal one-oscillator, one-pole design, so the ratio is a statement about these two designs, not about
  quality of implementation.
* The worst block times are 2 to 9 times the mean: the blocks in which the held notes are retriggered (voice start, stolen voices
  fading) are the expensive ones. A device budget must be met by the worst block, not by the mean.
* Transferring to the MPC: an ARMv7 core of that class is several times slower than one performance core of this machine; no
  factor is assumed here. The tier voice limits for the Low tier (8 at the moment) must be decided from a measurement on the
  device (benchmark harness: `synthyboy_bench --engine vital --voices 8`); iOS gets the own engine by the owner's decision
  (ARCHITECTURE 2.2). First measurement on an MPC and on a mid-range phone is still open (ARCHITECTURE section 9, Phase 2).
* The CI job `linux-aarch64` prints the same benchmark for the Vital engine on GitHub's arm runner (informational, shared
  cloud machine).
