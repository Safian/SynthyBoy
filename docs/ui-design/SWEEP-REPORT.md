# Sweep report - sanitizers, fuzz, factory presets, docs / CI (2026-11-08)

Branch `worktree-agent-aeeeac1f3f5aa7a12`, fast-forwarded to develop `f355eec` first. Host: Apple M-series, arm64 macOS, clang (Apple), 18 cores.

## 1. Sanitizers

| Build | What ran | Result |
|-------|----------|--------|
| ASan + UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=all`, RelWithDebInfo, plugin OFF) | all 33 core tests incl. the `slow` audio gates (`synthyboy_test_vital_audio`, 107 s), the thread tests, 5 Vital suites, tuning, formula, shapes, filters, fx-a/b, sample-osc, wtedit, import | 2 findings, both fixed, then 33/33 |
| ASan + UBSan, plugin ON (JUCE reused from the main checkout) | `synthyboy_test_ui_model`, `_ui_render`, `_ui_panels`, `_glb`, `_fx` (real Components, headless) | clean (5/5) |
| ThreadSanitizer (RelWithDebInfo, plugin OFF) | everything except the `slow`-labelled gates (the same exclusion as the CI job): 32 tests incl. `synthyboy_test_threads`, `synthyboy_test_vital_wp7` (Convolve builder thread, `Quality::realtime` on and off), `synthyboy_test_vital_wp123` (content hand-over from another thread), `synthyboy_test_shell_clap`, `synthyboy_test_rt` | clean, no report |
| Leaks | macOS has no LeakSanitizer on arm64; `leaks --atExit` on the release binaries of the fuzz smoke (both engines, all parsers), wp4 (content store, samples), state v3, shell-clap, vital-import, fx-serum-b | 0 leaks in each |

Findings and root-cause fixes (no suppressions):

1. `ShapeData::evaluate` (`core/shapes/src/shape.cpp`) trusted `numPoints` and read past the 128-point array when it was called on data that was never sanitized (`shape_fuzz_point_lists`, ASan stack-buffer-overflow, a read). `evaluate` is also reachable from the engine with a shape taken from a state, so the fix is in the function: it now clamps the count to `kMaxPoints` itself.
2. `convolve_ir_rate_conversion` (`core/fx-serum-b/tests/test_convolve.cpp`): a **test** bug, 600 frames processed in blocks of 64 read 40 frames past the buffers (ASan heap-buffer-overflow in `Convolve::process`, the reads were the test's). Buffer length is now a whole number of blocks; `Convolve::process` itself was correct.

Not run / limits: no Linux x86_64 here (the CI jobs `core-sanitizers` and the new `ui-sanitizers` are the arbiters, they have not run on GitHub yet); LeakSanitizer only through the macOS `leaks` tool; the GUI tests are not run under ThreadSanitizer (JUCE; the UI is one thread); `synthyboy_test_vital_audio` and `synthyboy_preset_sanity` are not run under TSan (single-threaded gates, minutes under TSan); UBSan's `-fsanitize=float-divide-by-zero` / `implicit-conversion` families are not part of `undefined` and were not enabled.

## 2. Fuzz: `synthyboy_fuzz_smoke` (`core/tests/fuzz_smoke.cpp`)

Fixed seeds, bounded iterations. 5 s under ASan + UBSan (about 2 s native); `SYNTHYBOY_FUZZ_SCALE=N` multiplies every count. Targets and invariants:

* `loadState` / `loadStateWithContent` / `loadPreset` / `readPresetMeta` (900 documents: garbage, mutated factory presets, mutated states of those presets): plain and content loader agree, a failed load leaves the target untouched, an accepted one is inside the ranges and passes `validateContent` (about 100 accepted, 800 rejected).
* Content pack manifest (600), Vital import of presets, `.vitaltable`, `.vitallfo` (150, 3 s deadline each), SCL / KBM / TUN (1500; every table entry finite and >= 0, 0 = unmapped key), formula compile + render (1500, all seeds compile, rendered frames finite), the three WAV readers (fx-serum-b, sample-osc, `importAudioWav` in four modes; 900 files), garbled parameter sets rendered by both engines (finite output).
* Result: no finding at scale 1 and none at **scale 20** (93 s under ASan + UBSan). The earlier per-package fuzz tests are unchanged.

## 3. Factory presets: `synthyboy_preset_render` and `synthyboy_preset_sanity`

`tools/preset-render` (`--presets DIR [--engine] [--tier] [--velocity] [--wav DIR] [--summary-only]`) renders each preset with a fresh Vital engine per tier (state loaded as the shell does: all parameters, then the content): single note (C1 for category Bass, else C3) at velocity 0.8 held 1.5 s then released, and a four-note chord at velocity 1.0 held 1 s. Gate (ctest `synthyboy_preset_sanity`, label `slow`, 51 s native, 162 s under ASan + UBSan): finite; peak <= +3 dBFS; single note peak >= -60 dBFS; the tail ends (< -90 dBFS for 250 ms within 12 s of the note off).

**Defects found and fixed**

* **FM Digital Lead**: silent (peak -146 dBFS at every tier). It used Spectral Morph "Vocode" (`osc1.morph.type` 1, labelled FM in the preset text) on the FM Sweep table; a matrix over all 14 tables x 12 morph types shows that Vocode, Random Amplitudes and High Pass silence the sine-like tables at 40 % (engine behaviour inherited from Vital, see the alias table in QUALITY.md; not an adapter bug). The preset now uses the FM Sweep table at frame 40 and no morph (peak -22.1 dBFS).
* **Pulse Width Bass**: -48 dBFS, 20 dB below the median. Table "PWM Sweep" at frame 0 (hairline pulse) through Sync warp is nearly empty; frame 75 gives -22.9 dBFS.
  Both edits are in `scripts/gen-factory-presets.py` and in the two shipped `.sbpreset` files plus `contentpack.json` (hash and size of those two entries only). **Not regenerated with the script**: it reads the current `docs/params.json` (1484 keys) and would rewrite all 100 files with the new keys inside a version 2 state header; the shipped files are state version 2 on purpose (a test loads them as such). Decision needed from the owner (below).

**Numbers** (single note velocity 0.8, level = RMS of the loudest 300 ms of the held part; peak over single and chord)

| | low | medium | high |
|---|---|---|---|
| Level median / min / max (dBFS) | -29.0 / -45.2 / -19.7 | -29.0 / -45.2 / -19.7 | -29.0 / -45.2 / -19.7 |
| Level spread (sd, dB) | 5.9 | 5.9 | 5.9 |
| Highest single-note peak | -14.1 (sub_drop_boom) | -14.1 | -14.1 |
| Highest chord peak (4 notes, vel 1.0) | +0.9 (sub_drop_boom) | +0.9 | +0.9 |
| Longest tail | 2.62 s (infinite_horizon) | 2.62 s | 2.62 s |
| Largest DC offset | 0.041 (dirty_distortion_bass) | 0.042 | 0.042 |

Low, medium and high agree within 2.2 dB in the peak of any preset: none of the 100 presets asks for more than the low tier allows (one oscillator, one filter, no heavy effects).

Level by category (median / min / max RMS dBFS, high tier): Bass -27.0 / -42.7 / -19.7, FX -28.5 / -45.2 / -21.7, Keys -29.1 / -41.8 / -23.3, Lead -28.2 / -37.3 / -23.5, Pad -28.7 / -35.2 / -20.9, Pluck -39.1 / -43.3 / -34.4 (plucks decay inside the window, so their RMS is low by design; their peaks are -17 .. -24 dBFS like the rest).

Level outliers beyond 8 dB from the median (for the listening session): louder: `dirty_distortion_bass`, `solid_sub` (+9.3 dB), `golden_dawn_pad` (+8.1); quieter: `noise_riser` (-16.2), `fast_arp_bass` (-13.7), `dyno_electric_piano` (-12.8), `sync_growl_bass` (-8.7), `harpsichord_bar` (-8.7), `staccato_bleep_lead` (-8.3) and the plucks `formant_pop_pluck`, `electric_mute_pluck`, `trance_short_pluck`, `acoustic_style_pluck`, `digital_glass_tap`, `pizzicato_strings` (-10 .. -14 dB). Absolute level: single notes peak at -14 .. -26 dBFS (master volume -12 dB in every preset), a four-note chord at velocity 1.0 reaches -6 .. +0.9 dBFS; the factory set is quiet, not clipping.

**Velocity curve** (peak of the single note, median over the 100 presets, all tiers): velocity 0.25 / 0.5 / 0.75 / 1.0 = -42.5 / -30.4 / -23.4 / -18.4 dBFS, i.e. +12.0 / +7.0 / +5.0 dB per step; velocity 0.25 -> 1.0 is **24.1 dB for every preset** (min = max): peak ~ velocity^2 (40 log10 v), all presets carry `master.velocity_track` 100 and no velocity routing. This matches the engine's measured exponent (`velocityExponent` 2.0 in `core/tests/test_engines.h`) and is the "velocity curve differs from the own engine (exponent 1)" item already listed for the owner's listening test.

## 4. Docs and CI

* `docs/ARCHITECTURE.md`: layout block with all packages (3.1), 3.3 wavetable editor on mobile = yes (Q7; format is WAV, `.sbwt` does not exist), new 3.8 (engines, capability bits, caps of a tier, content store hand-over, state v3), 5.1 count 1484, new 6.1 content store and user content, new 8.1 UI kit, phase 5 state, CI paragraph.
* `docs/QUALITY.md`: tier table for the state-v3 modules and the sanitizer / fuzz / preset gates. `docs/PARAMETERS.md` and `docs/params.json`: regenerated check says up to date (no key changed in this task, `scripts/check-params-doc.sh`).
* `README.md`: status table of the phases (the "spike" status line was stale), packages, engine row (1484 keys), CI jobs. `docs/UPSTREAM.md`: file counts reconciled with `git diff 26daf58 HEAD` (190 imported, 4 removed, 6 own files, 186 modified, 192 today; 149 synthesis files). `THIRD_PARTY.md` / `NOTICE`: own-code packages listed, MTS-ESP not linked, KISS FFT note.
* `.github/workflows/build.yml`: new job `ui-sanitizers` (GUI tests under ASan + UBSan, leak detection off because JUCE singletons are reported by LSan; not run on GitHub yet). The existing `core-sanitizers` job picks up the fuzz smoke automatically (the preset gate runs there under ASan + UBSan only: label `slow`). pluginval / clap-validator steps, matrix and secrets are untouched.

## 5. Gates

`cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=ON && cmake --build build -j8 && ctest --test-dir build` (JUCE and clap-juce-extensions taken from the main checkout's `_deps` with `FETCHCONTENT_SOURCE_DIR_*`, the same revisions): no compiler warning, **40/40 tests passed** (170 s), `clap_smoke` all checks passed, `check-params-doc.sh` up to date. pluginval and clap-validator were not run in this task (CI only).

## 6. Not fixed / open

* **Factory preset generator vs. shipped files** (owner decision): `scripts/gen-factory-presets.py` produces different files than the ones in the repo (it follows the current key table). Either regenerate all 100 (state version 3 inside, sound unchanged because the new keys sit at defaults, new hashes) or pin the generator to the v2 key list. Until then, edit the shipped files and the script together, as done for the two fixes.
* **Spectral morphs that silence a sine-like table** (Vocode, Random Amplitudes, High Pass; ~22 of 168 table x morph combinations at 40 %): Vital behaviour, kept; the UI could warn. Not a defect of the adapter.
* **Quiet factory set**: peaks -14 .. -26 dBFS for one note. A level pass over 100 presets is a sound-design decision (owner listening).
* **DC offset** 4 % in `dirty_distortion_bass` (and about 1 % in a few Lead / Bass presets): from the distortion stage; inaudible but visible on a meter. Not changed.
* CI jobs `ui-sanitizers` and the fuzz / preset steps of `core-sanitizers` are new and unverified on GitHub.
