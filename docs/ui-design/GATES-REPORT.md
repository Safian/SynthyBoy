# GATES-REPORT: WP-20 gates before the push

Branch `work-gates`, started at develop `4bddcd5`, 2026-11-09, macOS arm64 (Apple clang, Ninja). Everything below was run on this tree; what could not be run is listed in section 8.

## 1. Summary

| Gate | Result |
|------|--------|
| Clean build, zero compiler warnings (14 row 7) | PASS (also with `-Wvla -Wunused-const-variable -Wmissing-field-initializers`, no VLA / GNU-only construct) |
| Full ctest -j8 on the clean build | PASS, 85 / 85 (before and after the fixes below) |
| ASan + UBSan (core, all 73 tests incl. real-file importer tests) | PASS, 73 / 73 (full run before the fix of section 4, the 5 suites it touches again after) |
| TSan (core, 71 tests, label `slow` excluded as in CI) | 1 real data race found and FIXED, 1 test wall-time false positive FIXED; the 7 affected suites re-run: 7 / 7 pass |
| clap-validator (14 row 6) | NOT RUN: no binary on this machine (section 8) |
| pluginval / auval | NOT RUN (pluginval not installed; `/usr/bin/auval` exists but there is no AU target) |
| gen-factory-presets --check, check-params-doc, check-manifest | PASS |
| check-no-vital | was FAILING (importer names), script fixed, PASS |
| Vital audit (35 presets) | PASS, ERRORS: 0 |
| Serum audit (656 real files) | PASS, 0 errors |

## 2. Clean build (TODO 1)

```
cmake -S . -B build-clean -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=<main>/build/_deps/juce-src \
  -DFETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS=<main>/build/_deps/clap-juce-extensions-src \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DSYNTHYBOY_CODESIGN_IDENTITY=-
ninja -C build-clean -j10 -k 0        # 1566 steps, exit 0
```

* Targets: the whole default graph (core libraries, VST3 `SynthyBoy.vst3`, CLAP `SynthyBoy.clap` through clap-juce-extensions, the native CLAP shell `shell-clap/synthyboy.clap` + `libsynthyboy_static.a`, all tools, bench, 85 test executables). There is no standalone target (`FORMATS VST3` + CLAP only).
* Compiler warnings: 0 (`grep -c "warning:"` finds only the linker's `ld: warning: ignoring duplicate libraries`, which is out of scope). The warning flags are `-Wall -Wextra -Wpedantic` on every core / shell-clap / tools target and `juce_recommended_warning_flags` on the JUCE shell targets.
* Row 7 (`kNumEnv`, `test_wp123.cpp` field initialisers): already gone on the develop tip; the stricter second configuration below confirms it.
* Stricter pass: the two sanitizer trees (section 3 / 4) were configured with `-Wall -Wextra -Wpedantic -Wvla -Wunused-const-variable -Wmissing-field-initializers` (456 compile commands each; the flags are in all 456 of them): 0 compiler warnings.
* MSVC-sensitive patterns: `-Wvla` over all non-JUCE code (above) and a `-fsyntax-only -Wvla -Wgnu` pass over the 84 distinct translation units of `shell-juce/` with their real compile commands: 0 findings. A grep of the diff `b5b6826..HEAD` for variable-length arrays found only `bool sustainDown[numChannels]` in `core/engine-own/own_engine.h:95`, a class constant, not a VLA.
* Preprocessor / CMake: nothing else GNU-only found in the new code (no `__attribute__`, `typeof`, `?:` extension, `alloca`, `__int128` in the added lines).

## 3. ctest (TODO 2)

`ctest -j8 --output-on-failure` in `build-clean`: `100% tests passed out of 85`, 277 s (first run) and 199 s (run after the fixes). `synthyboy_test_threads` passed in the full run (105 s), no rerun was needed. The real-file tests ran with the real presets: the Serum ones through the main checkout fallback (`SB_SERUM_PRESETS_MAIN`), the Vital ones (`vi_real_presets_if_present`: 10 files, 10 imported; `vital_batch_real_bank_optional`) after copying `tests/vital-presets/` (gitignored, 17 MB) into the worktree, because a worktree has no copy of it and the Vital tests otherwise print `SKIP`.

## 4. Sanitizers (TODO 3)

Core only (`SYNTHYBOY_BUILD_PLUGIN=OFF`, the CI `core-sanitizers` configuration), RelWithDebInfo:

```
cmake -S . -B build-asan -G Ninja -DSYNTHYBOY_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer <warning flags>"
cmake -S . -B build-tsan ... -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer <warning flags>"
UBSAN_OPTIONS=print_stacktrace=1 ctest -j6 --output-on-failure                 (build-asan)
TSAN_OPTIONS=halt_on_error=1 ctest -j6 -LE slow --output-on-failure            (build-tsan)
```

ASan + UBSan, `100% tests passed out of 73`, 746 s (macOS has no LeakSanitizer on arm64, so no leak check). Covered by the 73 suites (all of the core):

| Package | Suites |
|---------|--------|
| WP-03 filters | `synthyboy_test_filters_serum`, `synthyboy_test_vital_wp6` (filter outputs), `synthyboy_test_vital_audio` |
| WP-06 mod sources / warps | `synthyboy_test_vital_modsources`, `synthyboy_test_vital_warpenv`, `synthyboy_test_vital_wp6`, `synthyboy_test_vital_modamt`, `synthyboy_test_vital_modslew` |
| WP-07 voice control | `synthyboy_test_vital_voicecontrol`, `synthyboy_test_vital_wp7`, `synthyboy_test_vital_wp_voiceglb`, `synthyboy_test_vital_lfovoice` |
| WP-11 / 12 / 13 | `synthyboy_test_granular`, `synthyboy_test_vital_granular`, `synthyboy_test_spectral`, `synthyboy_test_vital_spectral`, `synthyboy_test_multisample`, `synthyboy_test_vital_multisample`, `synthyboy_test_vital_wp123`, `synthyboy_test_vital_groupc_rt` |
| WP-15 offline render, WP-14 / 16 | `synthyboy_test_vital_groupd`, `synthyboy_test_wave_convert` |
| importers | `synthyboy_test_serum_import` (all 656 real presets, 420 s), `synthyboy_test_serum_container`, `synthyboy_test_vital_import`, `..._import_crosscheck`, `synthyboy_test_vital_bank` (real bank; these two Vital ones were re-run separately in `build-asan` with the 35 real presets present: pass), `synthyboy_fuzz_smoke`, `synthyboy_preset_sanity` (all 112 factory presets) |
| rest | engine, rt (allocation hook), state, threads, events, keys, params, shell_clap, clap_content, user_content, fx_*, noisesample, oscextra*, pitchuni, readout, silence, wtremap, ... |

TSan, 71 tests (label `slow` excluded as in CI): 69 passed, 2 failed:

1. `synthyboy_test_vital_fxinstances_rt` (`fxi_handover_while_the_audio_thread_runs_and_parameters_change_from_another_thread`): a REAL DATA RACE. `ReorderableEffectChain::noteTriggered()` (audio thread, `reorderable_effect_chain.h:154`) writes `note_counter_` while the FX pool's builder thread reads it, together with the engine's tempo and key-track outputs, in `FxInstanceModule::run()` (called from `FxPool::build`, `vital_fx_pool.cpp:290`, the settle blocks). Fix: `FxInstanceModule::run(..., bool follow_engine = true)`; the builder passes `false`, so it uses the copies set by `prime()` and never reads what the audio thread writes (`reorderable_effect_chain.{h,cpp}`, `vital_fx_pool.cpp:290`); the audio thread keeps the old behaviour (first audio block takes the note counter offset). Regression test: the same test, which is the TSan detector of this path (it is part of the CI `core-sanitizers` tsan job); after the fix it passes under TSan in 39 s and no `ThreadSanitizer` line appears in the run. There is no non-TSan observable of a benign-looking race, so no separate functional test was added.
2. `synthyboy_test_serum_import`: not a race. All 656 real presets imported with 0 failures and 0 unaccounted leaves; only `CHECK (slowest < 20.0)` failed because the slowest preset took 52 s under TSan. Fixed in `core/serum-import/tests/test_serum_import.cpp`: the wall time check is skipped when built under a sanitizer (the same `SBTEST_INSTRUMENTED` pattern as `test_fxinstances_rt.cpp`). Re-run under TSan: pass (1801 s total for the batch).

Thread-handover paths under TSan (all passed): FX instance builder handover (`synthyboy_test_vital_fxinstances`, `..._fxinstances_rt` incl. the 16 slot handover with parameter changes from another thread), content store attach / user content writer thread (`synthyboy_test_vital_wp123`, writer thread at line 996; `synthyboy_test_user_content`, `synthyboy_test_clap_content`), LiveBus / read-out ring (`synthyboy_test_vital_lfovoice` writer thread, `synthyboy_test_vital_readout`), offline render during live processing (`synthyboy_test_vital_groupd`: `offline_render_does_not_disturb_the_live_engine`, a render on another thread while the audio goes on, bit-identical live output), parameter thread vs audio (`synthyboy_test_threads`, 1159 s).

After the fix: full ctest on `build-clean` 85 / 85; ASan + UBSan `-R "fxinstances|groupd|wp123|lfovoice"`: 5 / 5; TSan `-R "fxinstances|serum_import$|groupd|wp123|lfovoice|threads"`: 7 / 7. The other TSan suites were run before the fix only (the change is confined to `FxInstanceModule::run` and its one caller).

The UI (JUCE) tests were not run under a sanitizer here: the CI `ui-sanitizers` job does that on Linux, and no WP-20 package changes an audio buffer in the shell. They ran in the normal clean ctest (all `synthyboy_test_ui_*`, `glb`, `fx`).

## 5. CLAP / plug-in validation (TODO 4)

* `clap-validator`: NOT RUN. `which clap-validator` empty; Spotlight, `/tmp`, `~/.claude`, `~/Downloads`, `~/Library`, `/opt`, `/usr/local`, `/Applications`, the repo and `$HOME` to depth 7 were searched: the only traces are two empty temp directories of the earlier run (`$TMPDIR/clap-validator/temp_*`); the binary itself is gone. Nothing was downloaded or installed (not allowed here). CI builds clap-validator 0.4.1 from source and it is blocking there (`.github/workflows/build.yml`). Morning question 6 therefore stays open.
* Run instead (not a substitute for the validator): `build-clean/clap_smoke build-clean/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap`: `all checks passed` (parameter text round trip, state reproducibility, core parity, audio: default patch A4 peak 0.1233). The native CLAP shell is covered by `synthyboy_test_shell_clap` and `synthyboy_test_clap_content` (pass).
* pluginval is not installed (only an empty cache dir of the earlier run). `auval` exists but SynthyBoy has no AudioUnit target.

## 6. Scripts and audits (TODO 5)

| Command | Result |
|---------|--------|
| `python3 scripts/gen-factory-presets.py --check` | `factory presets up to date (112 presets)` |
| `scripts/check-params-doc.sh build-clean/tools/paramdump/synthyboy_paramdump` | `docs/params.json and docs/PARAMETERS.md are up to date` |
| `python3 -I scripts/check-manifest.py` | `plugin.json: OK (schemaVersion 1, version 0.1.0, clap_id com.safian.synthyboy)` |
| `scripts/check-no-vital.sh build-novital/core/libsynthyboy_core.a build-novital/shell-clap/libsynthyboy_static.a` (a core-only `SYNTHYBOY_WITH_VITAL=OFF` build, the CI recipe) | `clean` for both; `--self-test` all three cases ok; the Vital-engine core is still rejected |
| Vital: `unzip` the bank, `synthyboy_vital_import --in vp --out imp --report`, `synthyboy_vital_roundtrip`, `python3 -I scripts/audit-vital-import.py --vital vp --imported imp --roundtrip rt.json` (35 presets = 10 loose + 25 bank) | 35 presets imported, 0 failed; `value rows checked: 18249, mismatches: 0`; `35 presets, 0 mismatch(es)` (round trip); `ERRORS: 0` |
| Serum: `synthyboy_serum_import --in tests/serum-presets/Presets --out simp`, `python3 -I scripts/audit-serum-import.py --serum tests/serum-presets --imported simp` | `656 files: 656 ok, 0 failed ... unaccounted=0; stored=12389 missing=0`; `audited 656 files ... 0 errors` |

`check-no-vital.sh` failed on the importer: since WP-01 the core ships the importer of the Vital FILE format (`core/vital-import`: `synthyboy::vitalimport`, `importVital*`, `looksLikeVitalPreset`, ... and its error texts), and the script rejected every identifier with the word "Vital" (23 symbol lines, 16 string lines in `libsynthyboy_core.a`; the CI step in `.github/workflows/build.yml:342` would have gone red). That importer is our own code, not the Vital-derived engine, so the script now allows exactly these identifiers / texts (an explicit allow-list in the script header) and a new self-test case proves that they pass while `sbvital` is still rejected. The user visible reverb type name "Vital" (`kReverbTypes`) is allowed for the same reason.

## 7. What was fixed (commits)

1. `check-no-vital.sh` allow-list for the Vital file-format importer + self-test (`scripts/check-no-vital.sh`).
2. TSan data race in the FX instance builder (`core/engine-vital/upstream/src/synthesis/modules/reorderable_effect_chain.{h,cpp}`, `core/engine-vital/adapter/vital_fx_pool.cpp`) and the sanitizer-safe wall time check in `core/serum-import/tests/test_serum_import.cpp`.
3. This report and morning questions rows 6 / 7.

## 8. Not run / open

* clap-validator (row 6) and pluginval: no binaries on this machine, installing is out of bounds here. Remaining: run `clap-validator validate <SynthyBoy.clap>` (and pluginval strictness 5 on the VST3) on the final tree, or let the CI do it.
* The listening passes (14 rows 13, 15, 16, 21, 27, 31, filter / reverb names) are the owner's.
* TSan and ASan were run on the core (no JUCE) on macOS arm64; the GUI sanitizer job and the Linux / Windows builds (MSVC itself) were not run here, only the `-Wvla` / `-Wgnu` source checks above.
* The TSan sweep of the 64 suites not touched by the fix ran before the fix (section 4).
