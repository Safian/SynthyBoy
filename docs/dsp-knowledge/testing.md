# Test recipes

- **Test first, and prove it fails:** every change lands as a pair: the test that fails on the
  old code (compile-fail counts, say so), then the change. A test that passes before the fix
  proves nothing.
- **Signals must not hide the defect:** no signal whose zero crossings or whole periods line up
  with the defect (a chirp's zero crossings fooled the granular loop test twice); the defect's
  effect must be at least 10x the threshold.
- **Ratios are not proofs:** "RMS ratio < 0.97" depended on phases; prove silence with exact
  zeros. (own_spectral_a_waiting_voice_is_silent, 2026-11-21)
- **Read inside the render:** a test read 64 frames past its render end and was flaky with heap
  garbage; run suspicious tests with `MallocScribble=1`.
- **Checksums:** `*-CHECKSUM` lines are compared before/after by the build worker; a change that
  must not change sound must leave every line identical; intended changes list their lines.
- **Bit identity across compiles:** `-ffp-contract=off` (see platform file).
- **Batch builds:** workers write, one build worker builds the merged tree once; phase A (old
  code, new tests must fail) and phase B (all pass).
- **Never memcmp a struct:** padding bytes hold heap garbage (voice_opt compared `VoiceCtl` with its 3 padding bytes and was red only after an unrelated layout change); compare the fields.
- **A test that has never been built is not a test:** the agy branches of 2026-11-21 had a wrong include (`synthyboy/param_ids.h`) and a doubly declared member (`fx_oversampler.h`); both showed only in the first build.
- **A TSan report is a real race:** the FX builder thread read the audio thread's slot module pointer (`FxPool::typeChanged` vs `FxChain::configureSlot`); it showed in 2 of 20 `stub_host` runs but in 5 of 5 of a test that re-sets the same FX type key while the audio thread processes. Hammer the control path in the test, do not wait for the flaky run. (test: `synthyboy_test_own_fx_worker_race`, 2026-11-21)

- **LFO loop migration:** To check continuity, compare the authored values immediately before and after the engine's jump. In the own engine the phase parameter sets the return position for Loop at point and the end of the held interval for Loop and hold. The stored shape editor loop marker is independent. Envelopes have no repeated interval. Source: `core/engine-own/env_lfo/lfo.h` (`LfoCore::advance` and `position`). Test: `core/tests/test_lfo_seam_migration.cpp`, including the first engine wrap after loading a legacy state. With both editor links enabled, verify the full-cycle endpoints and the internal return ordinate after editing either an overlapping or an independent endpoint. Test: `shell-mpc/overlay/tests/lfo_ui_host.cpp`, `testEditorMarker`.

- **Chorus phase-table bounds** (tested: `core/tests/test_own_chorus_lanes.cpp`, Apple arm64).
  Include large negative phases and non-finite values in the four-lane comparison.
  The scalar and vector sine helpers return zero for integral phases of magnitude
  at least 2²³ and for infinity, and propagate NaN before converting a table index.
- **Fast exp2 non-finite input** (tested: `fast_exp2_preserves_nan_and_saturates_infinities`).
  Run with `-fsanitize=float-cast-overflow` to catch a NaN converted to an integer
  exponent even when the numerical result happens to propagate NaN.

## Compiled content publication

`ContentHub` hands custom LFO shapes and modulation curves to one audio consumer per engine.
An empty hub returns an empty snapshot. After publication the renderer keeps its slot until
the next acquisition that advances it. The reader does one relaxed load and, when an update
is pending, one acquire-release exchange. The publisher reclaims returned slots outside the
audio callback. A generation travels with its own data and invalidates the instance's matrix
and voice caches. Source: `core/engine-own/voice/own_content.h` and `core/engine-own/own_engine.cpp`.

The old eight-slot ring freed a live snapshot under rapid updates. On Mac arm64, ASan
reported heap-use-after-free when a reader kept a snapshot through 16 publications. The
regressions in `core/tests/test_threads.cpp` retain a snapshot through 64 updates, check coherent
data, monotonic generations and the final publication count with two writers, and render
two independent instances while each receives alternating LFO and slot curves. The last
test checks both the live LFO source and the modulated cutoff against the same snapshot.
Restoring a process-static generation cache in the instrumented binary caused a TSan
data race between the instances' audio callbacks.

## Test suite timing

Measured on 2026-11-26, x86-64 Linux VM (4 vCPU Xeon 2.1 GHz, GCC 13.3, Release, `-DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BITMAP_GUI=OFF`, 172 tests), other
sessions share the machine, so every number has a few tens of percent of noise. Three runs:

- **before -j2**: `ctest -j2` on the tree before the quick / full split: 1171 s real (sum of the test times 2172 s). That run includes `param_audit_scan` (545 s, fails here)
  and `release_scripts` (208 s, fails here).
- **alone**: `ctest -j1 -E param_audit_scan`, one test at a time (1099 s sum, 171 tests; of which 372 s `output_stage_presets`, 146 s `release_scripts`, 119 s `preset_sanity`,
  98 s `own_voice_count_gain`).
- Seven binaries were run again by hand alone with `SB_TEST_FULL=1` to get the time of the full coverage.

Tests of 2 s or more (seconds; "alone, full" is the time of the coverage as it was before the split, "alone, quick" the time of the entry of `ctest -LE slow`):

| test | before -j2 | alone, full | alone, quick | result |
|---|---|---|---|---|
| synthyboy_param_audit_scan | 545.4 | not run (about 9 min, TODO.md) | same | Failed |
| synthyboy_test_output_stage_presets | 362.9 | 372.8 | same | Passed |
| synthyboy_test_own_voice_opt | 340.9 | 327.6 | 20.0 | Passed |
| synthyboy_test_release_scripts | 207.7 | 145.7 | same | Failed |
| synthyboy_preset_sanity | 150.2 | 118.6 | same | Passed |
| synthyboy_test_own_voice_count_gain | 96.3 | 98.5 | same | Passed |
| synthyboy_test_own_filter_savings | 68.8 | 71.9 | 3.8 | Passed |
| synthyboy_param_audit_selftest | 38.3 | 30.5 | same | Passed |
| synthyboy_test_own_osc_types | 37.0 | 36.3 | 10.8 | Passed |
| synthyboy_test_own_fx | 25.4 | 25.1 | 15.0 | Passed |
| synthyboy_tier_bench_evict_flag | 24.8 | 23.8 | same | Passed |
| synthyboy_test_own_engine | 22.7 | 21.4 | 5.8 | Passed |
| synthyboy_test_filter_pair | 21.5 | 20.6 | same | Passed |
| synthyboy_test_fxopt_reverb_vintage | 16.1 | 15.3 | same | Failed |
| synthyboy_test_own_filter_response_rates | 15.2 | 14.9 | 5.8 | Passed |
| synthyboy_test_own_param_snapshot | 13.9 | 12.9 | 2.2 | Passed |
| synthyboy_test_param_semantics | 11.6 | 13.6 | same | Passed |
| synthyboy_test_own_lib_quantize_levels | 10.7 | 10.6 | same | Passed |
| synthyboy_test_rt | 8.8 | 10.1 | same | Passed |
| synthyboy_factory_content_reproducible | 8.6 | 6.3 | same | Failed |
| synthyboy_test_spectral | 7.3 | 7.2 | same | Passed |
| synthyboy_test_clap_content | 6.8 | 6.8 | same | Passed |
| synthyboy_test_own_filter_response | 6.6 | 6.2 | same | Failed |
| synthyboy_test_filters_serum | 6.4 | 6.5 | same | Passed |
| synthyboy_test_wavetable_render | 4.8 | 5.7 | same | Passed |
| synthyboy_test_own_note_storm | 5.0 | 5.3 | same | Passed |
| synthyboy_test_own_voice | 4.9 | 4.9 | same | Passed |
| synthyboy_test_fxopt_reverb_ref | 4.8 | 4.5 | same | Passed |
| synthyboy_test_fxopt_hyper | 3.9 | 4.0 | same | Passed |
| synthyboy_test_own_wt_osc_savings | 4.0 | 3.6 | same | Passed |
| synthyboy_test_state | 4.0 | 4.0 | same | Passed |
| synthyboy_test_own_fx_makeup | 3.6 | 3.5 | same | Passed |
| synthyboy_test_fxopt_cpu_distortion | 3.5 | 3.4 | same | Passed |
| synthyboy_test_threads | 3.5 | 3.1 | same | Passed |
| synthyboy_fuzz_smoke | 3.4 | 3.0 | same | Passed |
| synthyboy_test_own_alias_baseline | 3.3 | 3.4 | same | Passed |
| synthyboy_test_own_diode_probe | 3.2 | 3.1 | same | Passed |
| synthyboy_test_own_profile | 3.0 | 3.1 | same | Passed |
| synthyboy_test_own_wt_osc_shared_decim | 2.7 | 2.9 | same | Passed |
| synthyboy_test_spectral_add_harmonics | 2.8 | 2.8 | same | Passed |
| synthyboy_local_release_script | 2.7 | 2.0 | same | Passed |
| synthyboy_test_state_v3 | 2.7 | 2.4 | same | Passed |
| synthyboy_test_own_fx_worker_race | 2.4 | 2.4 | same | Passed |
| synthyboy_test_vital_import | 2.1 | 2.2 | same | Passed |
| synthyboy_test_own_compressor_opt | 2.1 | 2.1 | same | Passed |
| synthyboy_test_own_comb_pitch | 2.0 | 2.1 | same | Passed |
| synthyboy_test_own_unison_cap | 2.1 | 1.7 | same | Passed |

18 tests were over 10 s under -j2 and made 93 % of the sum (the owner counted 26 in a -j4 run, which inflates every time 2 to 3 times); the 125 tests under 2 s add up to 41 s.

### Tests that fail on this machine (not caused by the load, not changed by the split)

- `synthyboy_param_audit_scan` (545 s then fails), `synthyboy_factory_content_reproducible`, `synthyboy_test_release_scripts`, `synthyboy_ref_compare_metrics` (skipped): host reasons, see TODO.md.
- `synthyboy_test_fxopt_reverb_vintage` fails 3 of 3 runs alone on this VM: `vintage cpu ratio live / reference 0.875 .. 0.880 (model 0.65 ARM, 0.72 x86; limit 0.85)`.
  It is not a flaky test on this machine but a ratio the Xeon does not reach; the limit was not touched. It passed in the owner's runs on other x86 machines.

- `synthyboy_test_own_filter_response` is borderline on this machine even alone: `worstMs < 5.0` (thread CPU time, best of 5) measured worst 4.79, 5.34 and 4.71 ms in three runs in a row,
  and failed once in the `ctest -j2` run of the full set (the quick set passed 3 of 3). It has `RUN_SERIAL`, so load from other ctest jobs is not the cause; the limit was not touched.

### Where the time of the heavy binaries goes (seconds per case, alone, `SB_TEST_FULL=1`)

| binary | case | s |
|---|---|---|
| own_voice_opt (328 s) | `matrix_zero_skip_gives_the_same_deltas_for_every_special_value` (full grid, 2 820 checks of the binary in total) | 219 |
| | `ffloor_on_the_bits_is_std_floor_for_every_pattern` (2^32 patterns) | 28 |
| | `ffloor_on_the_bits_is_the_form_before_for_every_pattern_with_flush_to_zero` (2^32) | 20 |
| | `voice_opt_engine_factory_presets_are_bit_identical` | 17 |
| | `voice_opt_engine_random_patches_are_bit_identical` | 10 |
| | `voice_param_read_zero_test_on_the_bits_is_the_float_compare_for_every_pattern` (2^32) | 5.5 |
| own_filter_savings (72 s) | `..._integer_compares_match_the_float_forms_on_all_bit_patterns` (2^32 sweep) | 65 |
| | `..._diode_ramp_stays_within_half_a_db_of_the_piece_loop` | 6.7 |
| own_osc_types (36 s) | `own_render_osc_warp_bake_matches_the_live_warp` | 11 |
| | `own_osc_warps_odd_even_and_filter` | 7.3 |
| | `own_osc_spectral_morph_vectors`, `own_osc_xy_control_warp_axes`, `own_osc_xy_control_every_granular_axis` | 3.9, 2.1, 1.9 |
| own_fx (25 s) | `fx_reverb_levels_and_tails_of_the_six_types`, `fx_svf_styles_2_to_5_vectors`, `fx_reverb_vital_rt60_table_r1` | 6.8, 6.1, 3.5 |
| own_engine (21 s) | `own_engine_is_finite_and_bounded_under_random_patches`, `own_engine_rt_sequence_is_finite`, `own_filters_stay_bounded` | 8.8, 6.8, 3.9 |
| own_filter_response_rates (15 s) | `own_filter_response_every_model_at_96000_is_finite_and_fast` | 14.8 |
| own_param_snapshot (13 s) | `own_param_snapshot_every_parameter_alone_high`, `..._low` | 8.2, 3.9 |

Check counts of these binaries before the split (the printed `N check(s)`): own_voice_opt 2820, own_engine 3337, own_fx 9263, own_osc_types 4389, own_filter_savings 361827,
own_param_snapshot 81, own_filter_response_rates 5359. With `SB_TEST_FULL=1` they print the same numbers after the split (see "Quick set and full set" below).

## Build time (report, 2026-11-26, same VM, 4 jobs)

- **Full build** of everything `-DSYNTHYBOY_BUILD_TESTS=ON -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BITMAP_GUI=OFF` (560 steps, Ninja `-j4`): 142 s. The compile steps add up to 552 s of CPU, the
  link steps to 11 s; linking is not the problem. A change of `core/tests/test_runner.h` (every test includes it) rebuilds 383 steps: 87 s.
- **ccache**: `SYNTHYBOY_USE_CCACHE` (top level `CMakeLists.txt`; default ON only when ccache is found, a missing ccache is no error, a launcher the caller set is kept) makes it the compiler
  launcher. Measured with ccache 4.9.1: first build in an empty cache 136 s (33 % of the 402 compiles were hits already inside that build: the same source with the same flags is compiled
  for several targets), the same build again in a new, empty build directory 3.6 s. Use it when switching branches or build directories; it does not shorten the edit-compile-run loop of a
  single changed test file.
- **The 6 slowest translation units** (compiled alone, `-ftime-report`; none is over 11 s, so there is no single file to split): `core/serum-import/tests/test_serum_import.cpp` 11.0 s,
  `core/engine-own/fx/fx_modules.cpp` 10.1 s, `core/tests/test_own_fx.cpp` 9.7 s, `bench/tier_bench.cpp` 8.6 s, `core/engine-own/voice/voice.cpp` 8.3 s,
  `tools/param-audit/contexts.cpp` 7.5 s. In the three that were profiled, parsing (headers and templates) is 11 to 18 % of the time, optimisation and code generation 79 to 86 %.
- **Unity build or precompiled headers would gain at most the parsing share** (10 to 18 % of the compile time of a file), so they are not worth the loss of isolation. Two cheaper facts:
  `core/tests/test_main.cpp` is compiled once per test executable, 128 times, 28 s of CPU in total (5 % of the compile time; ccache removes it); building it once as an object library would
  remove it without ccache. Not done: the task was a report.

## Quick set and full set (workflow)

The development loop runs the quick set; the full set is one `ctest` away and runs everything that ran before the split.

```sh
# once: a development build (core and tests only; ccache is used when it is installed, SYNTHYBOY_USE_CCACHE=OFF turns it off)
cmake --preset dev && cmake --build --preset dev          # build-dev/ ; or: cmake -S . -B build -G Ninja -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BITMAP_GUI=OFF

# quick set: everything without the label slow, the heavy cases of seven binaries reduced (85 s wall at -j2 on the x86 VM; 153 tests)
ctest --preset quick                                       # or: ctest --test-dir build -j2 -LE slow --output-on-failure

# full set: every test, the heavy cases in full (the entries <name>_full, label slow), the audits
ctest --preset full                                        # or: ctest --test-dir build -j2 --output-on-failure
```

- **`slow`** is on every test that takes more than about 5 s alone (26 tests, listed by `ctest -N -L slow`). `ctest -L slow` runs only those.
- **`SB_TEST_FULL`** (environment, read by `sbtest::full ()` in `core/tests/test_runner.h`): unset, empty or `0` is the quick run, `1` the full run, for the seven binaries
  `own_voice_opt`, `own_filter_savings`, `own_osc_types`, `own_fx`, `own_engine`, `own_param_snapshot`, `own_filter_response_rates`. A heavy case that runs reduced or not at all prints one line
  per reduction, never silently, and the summary counts them:
  ```
  [ SKIP-QUICK ] voice_opt random patches: reduced from 80 to 10 (140 check(s) not run), run with SB_TEST_FULL=1
  quick run: 14 heavy case part(s) reduced or skipped, 866 check(s) not run; SB_TEST_FULL=1 runs them all
  ```
  Each of the seven has a ctest entry `<name>_full` (labels `slow;full`) that runs the same executable with `SB_TEST_FULL=1`; `ctest` runs both, the quick entry first.
  Check counts (`N check(s)` of the last line), identical in full mode to the ones before the split, and quick plus "not run" equals full:

  | binary | before the split | `SB_TEST_FULL=1` | quick run | "not run" in the quick run | alone, full / quick (s) |
  |---|---|---|---|---|---|
  | own_voice_opt | 2820 | 2820 | 1954 | 866 | 328 / 20 |
  | own_engine | 3337 | 3337 | 3337 | 0 (the blocks per case are fewer) | 21 / 5.8 |
  | own_fx | 9263 | 9263 | 8695 | 568 | 25 / 15 |
  | own_osc_types | 4389 | 4389 | 3536 | 853 | 36 / 10.8 |
  | own_filter_savings | 361827 | 361827 | 361827 | 0 (a strided 2^32 sweep) | 72 / 3.8 |
  | own_param_snapshot | 81 | 81 | 81 | 0 (every 3rd parameter) | 13 / 2.2 |
  | own_filter_response_rates | 5359 | 5359 | 1579 | 3780 | 15 / 5.8 |

  What the quick run does: a strided sample of the exhaustive 2^32 float sweeps (every 257th pattern and, as no stride may skip an off by one, the neighbourhood of the constants of the code
  under test and of every binade boundary; the one pattern mutation of `flushDenormal` and the one pattern mutation of `ffloorBits` of the overlap report are caught in the quick run), every
  29th combination of the matrix grid, fewer random patches / presets / blocks, every Nth row of the reference tables, and five whole cases of `own_osc_types` left out. The quick run is a
  development check, not a release gate: before a commit that changes DSP, run the area's full entries (below); a release runs `ctest` completely.
- **A new heavy case**: use `sbtest::quickScaled (what, fullValue, quickValue, skippedChecks)` for an amount of work (it returns `fullValue` with `SB_TEST_FULL=1`, otherwise `quickValue`, and
  prints the line) or `if (sbtest::skipInQuick (what, skippedChecks, reason)) return;` at the top of a case; register the binary with `synthyboy_add_full_run (<target>)` in
  `core/tests/CMakeLists.txt`. `skippedChecks` is `full - quick` measured with `./<binary> <case>` with and without `SB_TEST_FULL=1`; the two sums must stay equal to the full count.
  Under a sanitizer or `SBTEST_INSTRUMENTED=1` the existing `sbtest::instrumented ()` reduction is still used first.
- **Only the tests of a changed area**: `ctest --test-dir build -R <regex>`, with `-j2`. The quick entry only: anchor it, `-R '^synthyboy_test_own_voice_opt$'`; both entries: `-R own_voice_opt`.
  Examples: filters `-R 'own_filter|own_diode|filters_serum|filter_pair'`, oscillators `-R 'own_osc|wt_osc|own_content_opt|own_lib_'`, effects `-R 'own_fx|fxopt|own_alias|own_compressor'`,
  voice and matrix `-R 'own_voice|own_param_snapshot|own_engine'`. One case of one binary: `build/core/tests/synthyboy_test_own_voice_opt <part of the case name>` (add `SB_TEST_FULL=1` for the
  full case).
- **Timing-sensitive tests** (`own_filter_response`, `own_osc_displays`, `own_helper_thread`, `fxopt_hyper`, `fxopt_reverb_vintage`, `tier_bench_evict_flag`) have `RUN_SERIAL`: ctest never
  runs them next to another test, so `-j N` cannot make them fail; no limit was loosened. `tier_bench_evict_flag` has TIMEOUT 240 s (single run 24 s; it timed out at 120 s under `-j4`).
  `-j2` is the right width on the 4 vCPU development VM; `-j4` is what made tests of 5 to 25 s take 2 to 3 times longer.
- **ASan / UBSan, separately** (the CI job `core-sanitizers` does the same; the instrumented binaries are 5 to 20 times slower and set `sbtest::instrumented ()`, which reduces the work of the
  thread and engine tests by itself):
  ```sh
  cmake -S . -B build-asan -G Ninja -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BITMAP_GUI=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
  cmake --build build-asan && ctest --test-dir build-asan -j2 -LE slow --output-on-failure
  ```
  Run it on the changed area (`-R`), not on the whole set, while developing; the `_full` entries of the split binaries are the ones to run there before a release (they carry the label slow).
  TSan: the same with `-fsanitize=thread`, `-R 'threads|own_fx_worker_race|own_helper_thread'`. On a Linux host with a high ASLR entropy the runtime needs `sudo sysctl vm.mmap_rnd_bits=28`.

### Result after the split (2026-11-26, same VM, `ctest -j2`)

- Quick set (`ctest -j2 -LE slow`, 153 tests): 85.4, 86.0 and 84.2 s wall, all 153 pass; the sum of the test times is 165 s. The same selection of tests before the split (166 tests,
  the 6 that carried the label then left out) added up to 967 s (under -j2, so about 480 s wall at the best).
- Full set (`ctest -j2`, 179 tests, the 7 `_full` entries included): 1160 s wall against 1171 s before the split; the full set does the same work as before plus the quick entries of the
  seven binaries (about 70 s of test time), so it is not shorter; what changed is that most of it no longer has to run in the development loop. 174 of 179 pass; the 5 failures are the known
  machine reasons above (`fxopt_reverb_vintage`, `own_filter_response` once, `param_audit_scan`, `factory_content_reproducible`, `release_scripts`), `ref_compare_metrics` is skipped.
  The `_full` entries print exactly the check counts of before the split (2820, 3337, 9263, 4389, 361827, 81, 5359).
- `ctest -LE audit` (what the CI build matrix runs) leaves out the 9 minute `param_audit_scan` and the audit tests.
