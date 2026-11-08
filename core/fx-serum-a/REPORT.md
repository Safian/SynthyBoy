# Report: core/fx-serum-a (Serum FX Modules)

## 1. Elkészült feladatok (Completed)

1. **Hyper / Dimension (`synthyboy::fx::HyperDimension`)**:
   - `Hyper`: 0–7 voice micro-delay unison chorus with configurable rate (0.05–10 Hz), detune depth, retrigger capability, voice stereo panning spread, cubic Hermite interpolation, and dry/wet mix.
   - `Dimension`: 4-tap out-of-phase pseudo-stereo expander with size control (5–35 ms delays), subtle slow amplitude modulation, and dry/wet mix.
   - Master level in dB, sample-accurate parameter smoothing across audio blocks, real-time safe (zero allocation in `process()`), bypass-null when mix is 0 and level is 0 dB.

2. **Utility (`synthyboy::fx::Utility`)**:
   - Polarity inversion per channel (`invertL`, `invertR`).
   - 2nd-order Butterworth low-pass (`lpfEnabled`, `lpfFreq`) and high-pass (`hpfEnabled`, `hpfFreq`) filters.
   - Mono Bass (`monoBassEnabled`, `monoBassFreq` 20–1000 Hz) using 4th-order Linkwitz-Riley (LR4) crossover with centered mono low band and transparent summing.
   - Stereo width adjustment (0.0 mono to 2.0 200% expanded) via M/S matrix.
   - Stereo pan balance (-1.0 to +1.0).
   - 1st-order DC removal filter (`dcFilter`).
   - Dry/wet mix and output level with linear block smoothing and bypass-null.

3. **Splitter L/H (`synthyboy::fx::SplitterLH`)**:
   - 2-band Linkwitz-Riley 4th-order (LR4) crossover (`splitFreq` 20–20000 Hz) with zero magnitude ripple.
   - Per-band bypass (`lowBypass`, `highBypass`) and gain (`lowGainDb`, `highGainDb` -60..+12 dB).
   - Standalone stereo processor (`process`) + routing helper methods (`split`, `merge`).

4. **Splitter L/M/H (`synthyboy::fx::SplitterLMH`)**:
   - 3-band multiband crossover tree using LR4 crossovers (`splitFreq1`, `splitFreq2`).
   - All-pass phase alignment on Low band to preserve flat magnitude reconstruction across the spectrum.
   - Per-band bypass (`lowBypass`, `midBypass`, `highBypass`) and gains (`lowGainDb`, `midGainDb`, `highGainDb`).
   - Standalone stereo processor (`process`) + routing helper methods (`split`, `merge`).

5. **Splitter M/S (`synthyboy::fx::SplitterMS`)**:
   - Mid/Side encoder/decoder matrix for independent Mid and Side sub-rack processing.
   - Per-band bypass and gain adjustments.
   - Standalone stereo processor (`process`) + routing helper methods (`split`, `merge`).

6. **Stereo Splitter (`synthyboy::fx::StereoSplitter`)**:
   - Left/Right discrete channel routing helper and recombiner.
   - Per-channel bypass, gain, and stereo pan positioning.
   - Standalone stereo processor (`process`) + routing helper methods (`split`, `merge`).

7. **DSP Primitives (`synthyboy::fx::dsp_utils.h`)**:
   - Denormal flushing to prevent subnormal floating point CPU stalls.
   - Transposed direct-form II biquad, Butterworth LP/HP/AP, LR4 LP/HP/AP filters.
   - 1st-order DC blocking filter.
   - DelayLine with cubic Hermite fractional delay interpolation.
   - Parameter smoothers.

8. **Package Build & Test Architecture**:
   - Standalone `core/fx-serum-a/CMakeLists.txt` defining `synthyboy_fx_serum_a` static library and `synthyboy_test_fx_serum_a` test executable.
   - Exactly ONE `add_subdirectory(fx-serum-a)` added to `core/CMakeLists.txt`.
   - `-Wall -Wextra -Wpedantic` clean compilation with zero warnings.
   - Comprehensive unit test suite in `core/fx-serum-a/tests/test_fx_serum_a.cpp`: 10 test cases, 26,093 checks covering bypass-null, level/energy sanity, no NaN/inf on extremes and denormals, determinism, click-free parameter sweeps, sample rate (22050–192000 Hz) and block size (1–4096) scalability, and routing helper methods.

## 2. Mi maradt (What remains)

- Engine parameter registration: Mapping these standalone C++ parameter structs to global parameter IDs in `docs/params.json` when the parameter specification package merges.

## 3. Érintett fájlok (Affected files)

- `core/CMakeLists.txt`
- `core/fx-serum-a/CMakeLists.txt`
- `core/fx-serum-a/include/synthyboy/fx/dsp_utils.h`
- `core/fx-serum-a/include/synthyboy/fx/hyper_dimension.h`
- `core/fx-serum-a/include/synthyboy/fx/utility.h`
- `core/fx-serum-a/include/synthyboy/fx/splitter_lh.h`
- `core/fx-serum-a/include/synthyboy/fx/splitter_lmh.h`
- `core/fx-serum-a/include/synthyboy/fx/splitter_ms.h`
- `core/fx-serum-a/include/synthyboy/fx/stereo_splitter.h`
- `core/fx-serum-a/src/hyper_dimension.cpp`
- `core/fx-serum-a/src/utility.cpp`
- `core/fx-serum-a/src/splitter_lh.cpp`
- `core/fx-serum-a/src/splitter_lmh.cpp`
- `core/fx-serum-a/src/splitter_ms.cpp`
- `core/fx-serum-a/src/stereo_splitter.cpp`
- `core/fx-serum-a/tests/test_fx_serum_a.cpp`
- `core/fx-serum-a/REPORT.md`

## 4. Kapuk kimenete szó szerint (Verbatim Gate Output)

```
Test project /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-fxA/build
      Start  1: synthyboy_test_params
 1/13 Test  #1: synthyboy_test_params ............   Passed    0.50 sec
      Start  2: synthyboy_test_state
 2/13 Test  #2: synthyboy_test_state .............   Passed    2.47 sec
      Start  3: synthyboy_test_preset
 3/13 Test  #3: synthyboy_test_preset ............   Passed    0.19 sec
      Start  4: synthyboy_test_shell_clap
 4/13 Test  #4: synthyboy_test_shell_clap ........   Passed    0.25 sec
      Start  5: synthyboy_test_engine
 5/13 Test  #5: synthyboy_test_engine ............   Passed    3.67 sec
      Start  6: synthyboy_test_rt
 6/13 Test  #6: synthyboy_test_rt ................   Passed    3.58 sec
      Start  7: synthyboy_test_threads
 7/13 Test  #7: synthyboy_test_threads ...........   Passed   13.29 sec
      Start  8: synthyboy_test_vital_engine
 8/13 Test  #8: synthyboy_test_vital_engine ......   Passed    2.35 sec
      Start  9: synthyboy_test_vital_params
 9/13 Test  #9: synthyboy_test_vital_params ......   Passed    1.77 sec
      Start 10: synthyboy_test_vital_audio
10/13 Test #10: synthyboy_test_vital_audio .......   Passed   26.22 sec
      Start 11: synthyboy_test_vital_dsp
11/13 Test #11: synthyboy_test_vital_dsp .........   Passed    3.67 sec
      Start 12: synthyboy_test_fx_serum_a
12/13 Test #12: synthyboy_test_fx_serum_a ........   Passed    0.01 sec
      Start 13: synthyboy_paramdump
13/13 Test #13: synthyboy_paramdump ..............   Passed    0.16 sec

100% tests passed out of 13

Label Time Summary:
slow    =  26.22 sec*proc (1 test)

Total Test time (real) =  58.13 sec
```

Direct test runner execution:
```
[ RUN  ] HyperDimension_BypassNull
[  OK  ] HyperDimension_BypassNull
[ RUN  ] Utility_BypassNull
[  OK  ] Utility_BypassNull
[ RUN  ] Splitters_BypassNull
[  OK  ] Splitters_BypassNull
[ RUN  ] HyperDimension_EnergySanity
[  OK  ] HyperDimension_EnergySanity
[ RUN  ] Splitters_ReconstructionFlatMagnitude
[  OK  ] Splitters_ReconstructionFlatMagnitude
[ RUN  ] NoNaN_ExtremesAndDenormals
[  OK  ] NoNaN_ExtremesAndDenormals
[ RUN  ] Determinism_IdenticalRuns
[  OK  ] Determinism_IdenticalRuns
[ RUN  ] ParameterSweeps_ClickFree
[  OK  ] ParameterSweeps_ClickFree
[ RUN  ] Scalability_SampleRatesAndBlockSizes
[  OK  ] Scalability_SampleRatesAndBlockSizes
[ RUN  ] RoutingHelpers_SplitAndMerge
[  OK  ] RoutingHelpers_SplitAndMerge
10 test case(s), 26093 check(s), 0 failed case(s), 0.00 s
```

## 5. Élőben ellenőrizendők (Live Verification)

- A/B listening and phase alignment tests with host automation in a DAW environment once high-level parameter bindings are linked.
- Preset import mapping against Serum patches using Hyper/Dimension and Utility modules.
