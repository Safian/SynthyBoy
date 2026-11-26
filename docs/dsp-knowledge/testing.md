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
