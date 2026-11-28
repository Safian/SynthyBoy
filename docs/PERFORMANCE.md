# Performance

For MPC measurements, use the [device protocol](perf/README.md) and the [measured DSP findings](dsp-knowledge/README.md). The protocol records the workload, device, sampling limits and spread. The desktop benchmark below measures a different workload and serves as a development check.

The original Key 61 benchmark used the CI artifact `synthyboy-bench-armv7` at 48 kHz with the two patches below. Follow the [sampling rules](dsp-knowledge/measurement.md) when collecting device results.

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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bench/synthyboy_bench --engine own   --seconds 10          # both patches (--patch default | heavy | both)
```

The desktop defaults are 44.1 kHz, 128 frames per block and flush-to-zero on supported architectures.
