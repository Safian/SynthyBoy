# REPORT: Serum FX Modules (core/fx-serum-a)

## 1. Pontonként mi készült el (Completed items)

- **Hyper / Dimension (`synthyboy::fx::HyperDimension`)**:
  - `Hyper` 1–7 voice micro-delay unison chorus (`hyperRate`, `hyperUnison`, `hyperDetune`, `hyperRetrig`, `hyperMix`), voice stereo spread, cubic Hermite interpolation, retrigger support.
  - `Dimension` 4-tap out-of-phase pseudo-stereo expander (`dimSize`, `dimMix`) with subtle slow amplitude modulation.
  - Master output gain (`levelDb`), real-time safe (zero allocations in process), click-free parameter smoothing, bypass-null bit-for-bit when dry.
- **Utility (`synthyboy::fx::Utility`)**:
  - Polarity inversion per channel (`invertL`, `invertR`).
  - 2nd-order Butterworth low-pass and high-pass filters (`lpfEnabled`, `lpfFreq`, `hpfEnabled`, `hpfFreq`).
  - Mono Bass (`monoBassEnabled`, `monoBassFreq`) using 4th-order Linkwitz-Riley crossover with centered mono low band and flat magnitude summing.
  - Stereo Width (`width` 0.0 mono .. 1.0 normal .. 2.0 expanded) via Mid/Side matrix.
  - Stereo Pan balance (`pan` -1.0 L .. +1.0 R).
  - Sub-audible 1st-order DC removal filter (`dcFilter`).
  - Dry/Wet mix and output level with linear block smoothing and bypass-null.
- **Splitter L/H (`synthyboy::fx::SplitterLH`)**:
  - 2-band Linkwitz-Riley 4th-order (LR4) crossover (`splitFreq` 20–20000 Hz) with zero magnitude ripple.
  - Per-band bypass and gain adjustments (-60..+12 dB).
  - Standalone processor (`process`) + routing helpers (`split`, `merge`).
- **Splitter L/M/H (`synthyboy::fx::SplitterLMH`)**:
  - 3-band multiband crossover tree using LR4 crossovers (`splitFreq1`, `splitFreq2`).
  - All-pass phase alignment on Low band to preserve flat magnitude reconstruction across the spectrum.
  - Per-band bypass and gains.
  - Standalone processor (`process`) + routing helpers (`split`, `merge`).
- **Splitter M/S (`synthyboy::fx::SplitterMS`)**:
  - Mid/Side encoder/decoder matrix for independent Mid and Side sub-rack processing.
  - Per-band bypass and gains.
  - Standalone processor (`process`) + routing helpers (`split`, `merge`).
- **Stereo Splitter (`synthyboy::fx::StereoSplitter`)**:
  - Left/Right discrete channel routing helper and recombiner.
  - Per-channel bypass, gain, and stereo pan positioning.
  - Standalone processor (`process`) + routing helpers (`split`, `merge`).
- **DSP Primitives (`synthyboy::fx::dsp_utils.h`)**:
  - Denormal flushing, Biquad Direct Form II Transposed, Butterworth LP/HP/AP, LR4 LP/HP/AP, 1st-order DC blocker, DelayLine with cubic Hermite interpolation, parameter smoother.
- **Standalone Package & Unit Tests**:
  - `core/fx-serum-a/CMakeLists.txt` and exactly one `add_subdirectory(fx-serum-a)` in `core/CMakeLists.txt`.
  - -Wall -Wextra -Wpedantic clean (0 warnings).
  - 10 unit test cases (26,093 checks) in `core/fx-serum-a/tests/test_fx_serum_a.cpp`: bypass-null, level/energy sanity, no NaN/inf, determinism, click-free parameter sweeps, sample rates 22050–192000 Hz, block sizes 1–4096.

## 2. Mi maradt (What remains)

- Engine parameter registration: Mapping standalone DSP C++ parameter structs to global keys in `docs/params.json` once the parameter specification package merges.

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
- `REPORT.md` (root, untracked)

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

## 5. Élőben ellenőrizendők (Live Verification)

- A/B listening and phase alignment tests with host automation in a DAW environment once high-level parameter bindings are linked.
- Preset import mapping against Serum patches using Hyper/Dimension and Utility modules.
