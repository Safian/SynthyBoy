# Performance

**History note (2026-11-21):** the Vital-derived engine is removed (docs/plans/vital-removal.md) and `synthyboy_bench` has the own engine only (`--engine own`, or no option). The `vital` columns and the `--engine vital` commands below are the last numbers of that engine, kept as a record; they cannot be reproduced on the current tree.

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

## Device Measurements and Protocol

The informal 2026-11-04 laptop benchmark table was removed. All device performance measurements on target hardware (Akai MPC Key 61 and Akai MPC XL), standard load sets (SET12, SET6), hardware performance counter protocols (PMU), differential module cost breakdowns, and automated regression thresholds are documented in [docs/perf/README.md](perf/README.md).

For quick desktop sanity checks, `synthyboy_bench` runs with defaults of 44.1 kHz sample rate, 128 frames per block, and flush-to-zero (FTZ / DAZ) enabled on supported architectures.
