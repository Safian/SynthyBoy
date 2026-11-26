# Test overlap report (2026-11-26)

Nothing in this report was deleted or changed. It lists tests that appear to check the same behaviour twice, what each of them asserts, what it costs, and a recommendation
per candidate with the evidence. The owner decides about deletions.

Times are seconds alone on the x86 VM of "Test suite timing" in [testing.md](testing.md) (full coverage, `SB_TEST_FULL=1` where the binary has the switch).

## Method: one line mutations

The evidence is a mutation check: one line of the engine is broken on purpose, only the targets that could notice it are rebuilt, and the quick run of each is started (the quick run
is the default; where the mutation sits in a part that the quick run reduces, the full run of that case is named). A test that fails "catches" the mutation. The driver is a 40 line
Python script that applies a `(file, line, old, new)` edit, runs `ninja <targets>` and the executables, and restores the file with `git checkout`; the table gives the file, the line and
the edit, so each row can be repeated by hand.

| id | mutation | what it models | caught by | passes (not caught) |
|---|---|---|---|---|
| A | `core/engine-own/voice/classic_filters.h` line 1052, `in3 = lo * s2;` -> `in3 = lo * s1;` (the live, optimised Diode loop only; the references of the tests are copies) | a wrong term in an optimised filter loop | `own_filter_savings` (`diode_dead_terms_are_equal_as_floats`), `own_engine` (`own_filter_responses_match_document_05`) | `own_voice_opt`, `own_filter_styles`, `own_diode_probe`, `own_filter_response`, `own_filter_response_review`, `own_voice`, `own_osc_types` |
| B | same file line 232, the shared saturator `sat`: `0.9f` -> `0.91f` | a change of a shared constant that the reference copies of the tests share | nothing | all nine targets of A |
| C | `core/engine-own/osc/wt_osc.h` line 1475, `L[k] += y * lerp (pg, cgl, t)` times `1.0000002f` (the scalar tail of `renderPlain`) | a one ulp gain error in an optimised oscillator loop | `own_wt_osc_savings` (2 cases), `own_osc_options` (`defaults_keep_the_render_bit_identical`, a golden checksum) | `own_wt_osc_shared_decim`, `own_osc_types`, `own_voice_opt`, `own_content_opt`, `own_osc_displays`, `own_engine`, `own_voice`, `own_lib_quantize_levels` |
| D | `core/engine-own/matrix/mod_matrix.cpp` line 354, `zeroOut` loses `&& amL == amL && amR == amR` (a NaN amount is skipped as zero) | a wrong zero skip condition | `own_voice_opt` (`matrix_zero_skip_gives_the_same_deltas_for_every_special_value`, also in the quick run with every 29th combination) | `own_engine`, `own_param_snapshot`, `own_voice`, `own_engine_fixes`, `own_osc_types` |
| E | `core/engine-own/dsp/util.h` line 30, `flushDenormal` threshold `0x219392ef` -> `0x219392f0` (one bit pattern) | an off by one in a bit pattern compare | `own_filter_savings` `..._on_all_bit_patterns` (the full 2^32 sweep in 65 s; the quick run with the neighbourhood sweep in 3.6 s) | `own_voice_opt` (quick), `own_engine`; the plain every 257th pattern quick sweep also missed it, which is why the neighbourhood sweep was added |
| F | same file line 63, `ffloorBits`: `a < 0x3f800000u` -> `a < 0x3f800001u` (1.0 floors to 0) | an off by one at a binade boundary | `own_voice_opt` `ffloor_on_the_bits_*` (both, in the quick run 0.2 s, because the quick sweep visits the binade boundaries) | `own_engine`, `own_voice` |
| G | `core/engine-own/fx/fx_adaa.h` line 47, `kLncoshSaturated` `8.8f` -> `7.0f` (a claimed bit identical shortcut) | a speed shortcut that changes the last bit | `fxopt_cpu_distortion` (3 cases) | the 11 `own_alias_*` targets that were run (tanh, clip, diode, fold, os2, shaper, latency, lib_warp, warp_adaa, warp_tanh, baseline), `own_fx`, `own_engine` |
| H | same file line 77, `foldIntegral`: `(1.0f - 2.0f * w)` -> `(1.0f - 2.1f * w)` (a wrong antiderivative of the Linear Fold) | a functional error of an ADAA kernel | `fxopt_cpu_distortion` (2 cases), `own_alias_dist_fold` (`..._matches_a_double_precision_reference`) | `own_alias_dist_tanh`, `_clip`, `_diode`, `_os2`, `_shaper`, `_latency`, `own_alias_lib_warp`, `own_alias_warp_adaa`, `own_alias_baseline`, `own_fx`, `own_engine` |
| I | `core/engine-own/osc/lib_osc.cpp` line 969, the cached Quantize level count `14.0f` -> `14.1f` | a wrong cache of a per call value | `own_lib_quantize_levels` | `own_content_opt`, `own_voice_opt`, `own_osc_types`, `own_osc_options`, `own_alias_lib_warp`, `own_wt_osc_savings`, `own_engine`, `own_voice` |

Two things follow before any single candidate:

1. The tests of the "bit for bit against the reference" kind (`own_voice_opt`, `own_filter_savings`, `own_wt_osc_savings`, `own_content_opt`, `own_lib_quantize_levels`, `fxopt_cpu_distortion`)
   are not duplicates of each other. Each guards its own optimisation and every one of them was the only catcher of at least one mutation (A, C, D/E/F, I, G). The overlap is in their
   topic, not in what they can detect.
2. Mutation B (a constant that both the code and the test references share) is caught by no test of the quick set. That is a gap, not an overlap; see "Gaps".

## Candidates

### 1. `own_diode_probe` - candidate for deletion (or turn it into an assertion)

- 3.1 s, 1 check, and that check is `CHECK (true)` (`core/tests/test_own_diode_probe.cpp` line 142). Its header says it prints checksums "BEFORE any change" so that a build worker can compare
  the printed `DIODECL-CHECKSUM` lines of two builds; it asserts nothing itself.
- Evidence: it passed mutation A and B, both of which change the Diode output.
- `docs/plans/diode-ladder.md` names it as a task Z3 probe. If the build worker no longer compares its lines, it checks nothing and costs 3 s of every full run.
- Recommendation: **candidate for deletion** after the owner confirms that no worker still diffs the `DIODECL-CHECKSUM` lines; otherwise keep it, label it, and do not count it as a test.
  The same applies to `own_alias_baseline` (3.4 s, "asserts only that the meter works", it records baseline numbers for `docs/plans/distortion-antialiasing-baseline.txt`; it does have 1100
  checks of the meter being finite and a hard clip reading above -90 dB, so it is a weak test and a recorder, not an empty one): **keep, low priority**.

### 2. The filter family: `own_voice_opt` / `own_filter_savings` / `own_filter_styles` / `own_filter_response*` / `own_engine`

| test | alone | what it asserts |
|---|---|---|
| `own_filter_savings` | 72 s full, 3.8 s quick | optimised classic filter loops against copies of the code they replaced (bit for bit or a stated bound): twin channels, Diode dead terms, SVF reciprocal, Serum pair kernels, 2^32 sweep of the integer compare forms, the Diode ramp within 0.5 dB |
| `own_voice_opt` | 328 s full, 20 s quick | the voice, matrix, envelope, display-skip and `ffloor` optimisations: whole engines with and without the saving, bit for bit, on random patches and every factory preset |
| `own_filter_styles` | 0.05 s | styles above the style count of a model repeat another style bit for bit; delay arena memory |
| `own_filter_response`, `_rates`, `_review` | 6.2, 14.9 (5.5 quick), 0.3 | the `FilterResponse` curve of the real filter code (UI display), rates, validation |
| `own_engine` | 21 s full, 5.8 quick | engine finite and bounded under random patches, `own_filter_responses_match_document_05` (the filter curves against the spec) |

- Overlap: `own_voice_opt` and `own_filter_savings` both compare optimised and reference code, but of different layers: `own_voice_opt` compares the engine as a whole and never reached the
  filter internals (mutation A, E: not caught); `own_filter_savings` compares the filter units (A, E: caught). Neither contains the other.
- `own_filter_response_review` (0.3 s) and `_rates` overlap in setup only; `_rates` has the timing ratio, `_review` the argument validation.
- Recommendation: **keep all**. `own_filter_styles` and `_review` are almost free.

### 3. The oscillator family: `own_osc_types` / `own_osc_options` / `own_wt_osc_savings` / `own_wt_osc_shared_decim` / `own_content_opt` / `own_lib_quantize_levels`

- `own_wt_osc_savings` (3.6 s) and `own_osc_options` (0.8 s, `defaults_keep_the_render_bit_identical`) both caught mutation C by different means (reference path against a golden checksum of the
  render). They overlap on "the default render did not change"; `own_osc_types` (36 s full, 10.8 s quick, 43 cases) caught nothing of C, I or A.
- `own_lib_quantize_levels` (10.6 s, one case, 2306 checks) and `own_content_opt` (0.6 s, 420 checks) both test the "cached per call value equals the per sample value" idea of the library
  oscillators. Mutation I was caught only by `own_lib_quantize_levels`. **Keep**; but its 10 s for one case is large for what `own_content_opt` does in 0.6 s: the case could run fewer
  patches in the quick set (not done here; it already carries the label `slow`).
- `own_osc_types` is the largest test file (2181 lines, 43 cases) and is mostly not a duplicate of anything: sample, granular, spectral, multisample types, read-outs, spectral start ledger
  and checksums. Its heavy cases (`own_render_osc_warp_bake_matches_the_live_warp` 11 s, `own_osc_warps_odd_even_and_filter` 7.3 s, the three 2 to 4 s cases) are left to the full run.
  **Keep**; the warp checks `own_osc_warps_odd_even_and_filter` (levels of the odd-even and filter warps against the vectors O11 / O12) and `own_alias_warp_*` (aliasing of the warps) look at
  different properties of the same warps.

### 4. The 21 `own_alias_*` tests

- 21 executables, 9.8 s in total, 8 of them under 0.1 s. No time problem; the question is only whether they duplicate each other. On mutation H only `own_alias_dist_fold` (and
  `fxopt_cpu_distortion`) failed; on G only `fxopt_cpu_distortion`. That says the files are split by law (tanh, clip, diode, fold, os2, shaper, ...) and each guards its own law, which is the
  expected structure, not duplication.
- `fxopt_cpu_distortion` (3.4 s, 1.2 million checks) is the bit identity of the distortion against the code before and overlaps the per law `own_alias_dist_*` only on "the law still produces
  its output"; it is the only catcher of the bit level mutation G, and the `own_alias_dist_fold` double precision reference caught the functional mutation H next to it. **Keep both.**
- `own_alias_switch` (1 case), `own_alias_shaper_tables` (2 cases), `own_alias_warp_events`, `own_alias_oversampler` run in under 0.05 s each: **keep**; merging them into fewer executables would
  save link time only (see "Build time" in testing.md), not test time.

### 5. The three tests that render every factory preset: `output_stage_presets` (373 s), `preset_sanity` (119 s), `own_voice_count_gain` (98 s)

- `output_stage_presets`: 640 renders (every preset, chord and single note, Key 61 and XL profile) through the shells' output stage at six make-up gains: ceiling, finite, safety clip never
  acts, loudness never falls as the gain rises. `preset_sanity`: `synthyboy_preset_render --summary-only` on every preset with the content store: silence, peak, NaN and level gates of the
  raw render. `own_voice_count_gain`: acceptance tests of the voice count gain (N^-alpha), partly on factory presets.
- They share the expensive part (rendering every preset through the own engine with the content packs) but assert different things on top of it. No mutation was run on them: each run is
  2 to 6 minutes. Mutations that change the level of every preset would be caught by `output_stage_presets` (the stock level test) and `preset_sanity` alike; that is the overlap.
- Recommendation: **merge candidate**, not deletion: let `output_stage_presets` and `preset_sanity` share one render of each preset (one pass would give both sets of gates), which removes
  roughly the time of the smaller one (about 2 minutes of the full run). Needs the owner's choice because the two are in different directories and one runs the raw engine, the other the
  shell's stage.

### 6. The parameter audits: `param_audit_scan` / `param_audit_selftest` / `param_semantics` / `test_params`

- `param_audit_scan` (`--quick`: representative values of every key plus the modulation sweep against the committed baseline) is 9 minutes here and fails on this host (TODO.md);
  `param_audit_selftest` (30.5 s) injects seven faults and proves the audit catches them; `param_semantics` (13.6 s) asserts expected directions of measurements over full sweeps of a key;
  `test_params` (1.7 s) the parameter table itself. They answer different questions (no effect at all / the effect is wrong / the table is consistent), and the selftest is a mutation test of the
  scan, which is the kind of evidence this report is made of.
- No mutation run here (the scan cannot complete on this machine). Recommendation: **keep all**; `param_audit_scan` stays outside the quick set (already labelled `slow;audit`).

### 7. Effect tests: `own_fx` / `fxopt_reverb_ref` / `fxopt_reverb_vintage` / `own_fx_mute` / `own_compressor_opt`

- `own_fx` (25 s full) asserts the effect laws against the spec vectors (reverb levels and RT60, EQ, compressor, phaser, SVF styles); `fxopt_reverb_*` assert bit identity of the optimised
  reverb against the reference and a CPU ratio; `own_fx_mute`, `own_compressor_opt`, `own_fx_makeup` are about the economy and the limiter. They target different properties; mutation G and H
  were not caught by `own_fx` at all (the effect laws are tolerance based).
- `fxopt_reverb_vintage` fails on this machine for a machine reason (CPU ratio 0.88 against a limit of 0.85) in 3 of 3 runs. Recommendation: **keep**; the owner may want a per machine limit
  or a model ratio from `tier_bench` instead of a fixed one (not changed here, no limit loosened).

## Gaps found on the way (what no test of the quick set catches)

- Mutation B: a changed constant of the shared saturator `ClassicFilter::sat` is caught by nothing, because `own_filter_savings` compares against copies that call the same `sat`, and the filter
  response tests are tolerance based. A golden checksum of the Diode (like `own_osc_options` has for the oscillators) would close it; `own_diode_probe` already prints one
  (`DIODECL-CHECKSUM`), so turning its printed numbers into `CHECK_EQ` against stored values would give that test a purpose instead of deleting it.
- The 2^32 sweeps are the only catchers of one bit pattern off by one mutations (E, F). They are no candidate for deletion; the quick set samples them (every 257th pattern plus the
  neighbourhood of the constants and of every binade boundary) and the full set runs them completely.
