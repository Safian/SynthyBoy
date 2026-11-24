# GROUPC - WP-11 Granular, WP-12 Spectral, WP-13 Multisample oscillators

Branch `work-groupC` (from `develop` 3a0c2ec). Spec: `13-new-keys.md` 4.37 .. 4.39 with assumptions A412 .. A434; morning questions 84 .. 91. Keys 2134 -> **2299** (blocks B28 93 keys, B29 42 keys, B30 30 keys), modulation destinations 912 -> **987**, warp list entries 76 -> **99** (23 spectral warps, 76..98), `oscN.type` list Wavetable / Sample / Granular / Spectral / Multisample, Kick pages 155, `kMaxParams` 4096 untouched. The 64-bit capability mask was full after GROUPB, so a **second capability word** was added (`kCap2Granular`, `kCap2Spectral`, `kCap2SpectralWarps`, `kCap2Multisample`). State version unchanged (additive content sections `content.spectral_filters[]`, `content.instruments[]`). `gen-factory-presets.py --check` passes (112 presets reproducible).

## 1. What was built

### Second capability word (commit 232cf73)

| Part | Where |
|------|-------|
| `EngineCaps2Mask`, `requiredCaps2`, `requiredCapsForValue2`, `capName2`, `capsCover2` | `core/include/synthyboy/engine_caps.h:105`, `core/src/engine_caps.cpp:293` |
| `IEngine::capabilities2()`, `UiHost::engineCaps2()` (default `kCap2AllKnown`, overridden by `PluginProcessor`) | engine / UI host interfaces |
| Tests | `core/tests/test_engine_caps.cpp` (word 2 independent of word 1, `oscN.type` values, `oscN.warp.type` not confused with `oscN.type`) |

### WP-11 Granular (`core/granular`)

| Part | Where |
|------|-------|
| DSP: cloud per unison voice, 48-grain pool, Hermite read with mip level, 10 windows, spawn patterns, jump start, loop grains, per-grain randomisation, pointwise warps per grain | `core/granular/src/granular.cpp:175` (spawn), `:263` (process) |
| Keys `oscN.gran.*` (93) | `core/src/param_table.cpp:417` (`SB_OSC_GRAN`) |
| Engine: kind 2 of `SampleOscillator`, `buildGrainParams` | `sample_oscillator.cpp:453`, `startKind` `:191`, `process` `:337`, `renderSlot` `:666` |
| UI: window thumbnail, X\|Y dot, grain marks, manual position | `OscWaveView.cpp:1822` (`paintGranular`), `OscPanel.cpp` |
| Importer: `kGranular[]` rules, density modes -> rates | `serum_map.cpp`, `serum_voice.cpp:51` (`granularRates`) |
| Audit | `scripts/audit-serum-import.py:756` (`audit_granular`) |
| Tests | `core/granular/tests/test_granular.cpp`, `engine-vital/tests/test_granular_osc.cpp`, UI `uig_wp11`, importer test |

### WP-12 Spectral (`core/spectral`)

| Part | Where |
|------|-------|
| DSP: FFT 2048, hop 512, Hann OLA, instantaneous-frequency phase vocoder, peak-region pitch shift, phase lock, transient reset, 3-frame priming, 23 warps, unison cap 8 | `spectral.cpp:111` (`Analysis::build`), `:251` (`applyWarp`), `:590` (`renderChannel`) |
| Keys `oscN.spec.*` (42), warp names 76..98 | `param_table.cpp:451` (`SB_OSC_SPEC`) |
| Engine: kind 3, `buildSpectralParams`, spectral filter baked to a 257-point table off-thread | `sample_oscillator.cpp:590`, `vital_engine.cpp:373` |
| UI: spectrogram (`SamplePreview::spectrogram`), frequency strip, X\|Y, filter graph in `RemapEditor` SpectralFilterMode | `OscWaveView.cpp:1494` (`paintSpectral`), `RemapEditor.cpp:90`, `user_content.cpp:259` |
| Importer: `kSpectral[]`, flex curve -> spectral filter (screen y flipped) | `serum_map.cpp`, `serum_content.cpp` |
| Audit | `audit-serum-import.py:793` (`audit_spectral`) |
| Tests | `core/spectral/tests/test_spectral.cpp`, `test_spectral_osc.cpp`, UI `uig_wp12`, importer test |

### WP-13 Multisample (`core/multisample`)

| Part | Where |
|------|-------|
| SFZ subset parser / writer (`//` inside a path is not a comment), zone choice (max 2 layers), round robin, key switch, choke groups, `LayerVoice` with DAHDSR (SFZ or OVERRIDE) | `multisample.cpp:186` (`parseSfz`), `:474` (`selectZones`), `:516` (`LayerVoice::noteOn`) |
| Keys `oscN.multi.*` (30) | `param_table.cpp:469` (`SB_OSC_MULTI`) |
| Engine: kind 4, velocity plumbed to the oscillator (`synth_voice_handler.cpp`), timbre lookup `k + t(127-2k)`, instrument built off-thread (24M frame cap) | `sample_oscillator.cpp`, `vital_engine.cpp` |
| User library: `kPackInstruments`, `addInstrument`, `attachInstrument` / `detachInstrument`, `storedSamplePath` | `user_content.cpp` |
| UI: zone map, envelope thumbnail, Load SFZ, Switch to Single Sample | `OscWaveView.cpp:1686`, `ContentIO.cpp:193` / `:264` |
| Importer: the whole instrument is kept (`applyMultisample`) | `serum_content.cpp:368` |
| Audit | `audit-serum-import.py:829` (`audit_multisample`) |
| Tests | `core/multisample/tests/test_multisample.cpp`, `test_multisample_osc.cpp`, UI `uig_wp13`, importer test |

### Cross-package

`core/engine-vital/tests/test_groupc_rt.cpp`: extreme settings of every kind stay finite and allocate nothing (alloc hook) while switching kinds under a running note. Helpers in `groupc_util.h`.

## 2. Verification

- Full `ctest -j8` (once, at the end): `100% tests passed, 0 tests failed out of 82`, total 292.84 s. Zero compiler warnings in the incremental builds.
- ASan + UBSan (`build-asan`): granular, spectral, multisample, groupc_rt and modsources vital suites all clean (0 failed cases).
- Serum audit on the real files (`tests/serum-presets/`, 656 files): `0 errors`; granular oscillators 66, instrument regions 10287, instrument samples missing 0; selftest mutations all found.
- `gen-factory-presets.py --check`: passes.

## 3. Findings

- UBSan found a pre-existing out-of-range index in `Feedback::refreshOutput` (`feedback.cpp`): GROUPB WP-06 uses the node at the oversampled rate, but the ring held only one `kMaxBufferSize` block, so the read index went negative. It reproduced in GROUPB's own `synthyboy_test_vital_modsources`. Fixed by sizing the ring `kMaxBufferSize * kMaxOversample` (`feedback.h`, `feedback.cpp`); modsources is clean afterwards.
- Pitch bugs found by the tests and fixed: grains and priming frames spawned at ratio 1; the spectral filter used the host rate instead of the file rate; layer voices were not prepared (pitch ~2.18x).

## 4. Not built / open (exact remainder)

- "Use a wavetable as the spectral filter" (manual p.120): not built, curve filter only (question 86).
- Switch to wavetable (S05-07, S06-07) belongs to WP-14.
- The Missing Files dialog does not list instruments.
- SFZ opcodes that are ignored: `trigger=release`, `xfin` / `xfout`, `fil_*` and others (question 90).
- Serum-name guesses in the importer enums: window shapes beyond the observed five, Y-axis names, the third density mode, `kParamLoHiPostWarp` (questions 84, 85).
- The factory instruments are 4.6 GB of WAV in the user library (question 91).
- Cost of spectral / granular voices at high unison (question 88).

## 5. Docs touched

`13-new-keys.md` (4.37 .. 4.39, content-table rows), `14-morning-questions.md` (rows 84 .. 91), `UI-GESTURE-AUDIT.md`, `16-COVERAGE-FINAL.md` (closed rows), `17-BACKLOG.md`, regenerated `docs/params.json`, `PARAMETERS.md`, `kick-pages.json`, factory presets.
