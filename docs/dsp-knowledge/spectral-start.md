# Spectral note start

## Facts

- The worst note-on block on the Key 61 came from the Spectral oscillator's start work (the
  first FFT frames of every voice), up to 3.5-4.4 blocks for 16-note chords.
  `measured: noteon-burst/before-*.md, abba-results.md`
- Start cost model (calibration S0): about 89 us per frame and 67 us per shaping on the A17,
  about 0.19-0.20 ms per voice. `measured: S0, docs/plans/spectral-start.md`
- The start ledger (tier low only) spreads the start work so that a block takes at most the
  budget; a single note never waits; deterministic. 16-note strike 3.5-4.4 -> 1.6-1.75 blocks
  at 2400 us. `measured: abba-results.md (A/B/A)`
- **Budget sweep** (interleaved, one session): rows under 1.5 blocks out of 12 Spectral rows:
  2400 us 0, 2000 us 4, 1700 us 10, 1400 us 8; single-run noise +-0.3 blocks, so 1700 and 1400
  are not separable. Steady-state p99 does not depend on the budget. `measured: budget-sweep-results.md`
- **Start cache** (switch `Quality::spectralStartCache`, default by tier: on at low only (owner decision 2026-11-21), off at medium / high; `tier_bench --start-cache` / `--no-start-cache`; `test: device_profile_host` (the low profile enables it, medium and high do not); plan `docs/plans/spectral-start-cache.md`): the start frames are memoised per (inputs, channel, frame), bit-identical to the cache off. `test: own_spectral_start_cache_repeated_chord_is_bit_identical_and_second_strike_hits` (a 4x2 chord struck twice: strike 1 computes 24 frames, strike 2 hits 24 and computes none; the two renders have the same FNV hash), `spectral_the_start_cache_is_bit_identical_and_the_second_strike_makes_no_frame` (other start offsets, phase lock, filter, stereo, Smear bypass). Mac, indicative only (Spectral Gong Pad, low, retrigger 0.5 s, 20 s, two runs per variant, A/B/A/B): longest block 4.29 / 2.58 % off against 2.39 / 1.86 % on, p99 1.45 / 1.23 against 1.20 / 1.21; the A17 is not measured. `measured: tier_bench on the Mac, 2026-11-21`

## Decisions

- Budget 1700 us (2026-11-21, merged). The 4x2 chord (1604 us) still starts whole; in an 8x2 chord 8 voices start at once, 4 wait 2.9 ms and 4 wait 5.8 ms (`test: own_spectral_chord_start`).

## Open

- Start cache on the Key 61: A/B/A on the device (measurement.md), then the default.
- Gong Pad 8 Kill and Chord Morph 8 Steal stay around 1.6 blocks: more runs, or another lever
  (HYPOTHESIS: not budget-limited). Worker-thread start preparation was considered and
  deferred: no free core on the Key 61 (host audio threads on all four), and it makes the start
  non-deterministic.

## Budget 1700 vs 1400 us, interleaved confirmation (Key 61, 2026-11-21, develop f37e79cf)

- Method: tier_bench-style note-on-burst, 1700 / 1400 / 1700 / 1400 / 1700 / 1400 us interleaved, 12 Spectral rows (3 presets x 8/16 notes x kill/steal) plus the Acid Saw control, core 3, 1608 MHz, 66-70 C. Files: `.hw-local/mpc-perf/key61-night-2026-11-21/job2-results.md`.
- Mean worst block over the 12 rows: 1400 us = 1.25 blocks, 1700 us = 1.42 blocks; 1400 is lower on 9 of 12 rows, but 11 of 12 differences are inside the noise (rule: more than max(0.3 blocks, run spread)). Only Chord Morph 8 Steal clears it (1.30 -> 0.98). Gong Pad 8 Kill: 1.28 vs 1.34 (noise). Rows under 1.5 blocks: 9/12 at 1700, 10-11/12 at 1400. Steady p99 unchanged (0.66 vs 0.67). Control 0.69-0.79 at both.
- The earlier 1.59 / 1.62 readings did not reproduce at 1700 on this HEAD (1.28 / 1.30): noise or the newer code.
- Decision (coordinator, 2026-11-21): the budget stays 1700 us (1400 is only weakly better and costs more voice delay). A start-frame cache (memoised start frames for repeated notes, bit-identical) is being analysed and may remove the rest.

## Start cache (2026-11-21, branch worktree-agent-aadd1287d0f23c8e2; on at tier low by the owner's decision, off at medium / high)

- Bit-identical memo of the start frames (process-wide, 3.2 MB, lock-free). Key 61 A/B/A (OFF/ON x5, budget 1700): Chord Morph strike 1.18-1.43 -> 0.77-0.88 blocks (repeated chord); Vowel Freeze and Gong Pad unchanged (no hits: random start or modulated inputs at the note-on, per the plan's caveat, hit counters not printed); a 16-note chord transposed every strike (all misses) costs about +0.1 block, inside the noise. Output bit-identical in all 72 comparisons. `measured: key61-night-2026-11-21/start-cache/job2-results.md`

## Real host (Key 61, 2026-11-21): budget 900 us on the device

- The offline bench misses host load. In the real host (callback-stats build, 16-note Vowel Freeze / Gong Pad / Chord Morph, 6 strikes, 3 interleaved rounds) the max time in our callback was 3371-3541 us at 1700, 2918-3056 at 1200 and at most 2780 at 900 (block 2902 us); gaps >= 2 blocks only at 1700. Only 900 meets "max callback < 1 block, no gap >= 2x" in every round. The Key 61 table profile therefore sets `startbudget=900` (`shell-mpc/device_profile.h`); the engine default stays 1700 (offline/desktop tier low). Remaining ~1.7-block gaps at every budget look like host scheduling (our callback is under the block there). Cost: the voices of big chords start later (an 8-voice start books about 1604 us, so it spreads over more control periods): audible effect not yet judged by ear. `measured: key61-night-2026-11-21/startbudget/results.md`

## Deferred start bounds at Key 61 900 us (16-note chord)

- **Units**: budget is modeled warm us per quad (`kStartFrameUs = 89`, `kStartShapeUs = 67`, times `channels` in `{1, 2}` from `SpectralVoice::startPattern`, S0 cost model, not wall time). Delay is measured in control periods ("ticks") of `kControlPeriod = 32` samples (`kStartTicksPerQuad = 4` ticks = 128 samples = 1 quad); sample-time equivalent is `ticks * 32 / sampleRate` (at Key 61's 44.1 kHz: 0.7256 ms/tick, 2.9025 ms/quad; at 48 kHz: 0.6667 ms/tick).
- **Assumptions & non-guarantee**: simultaneous 16-note strike on an initially empty `StartLedger` under Key 61's profile (`"low,startbudget=900"`, `maxVoices = 8`, `unison = 3`, `control = economy`, `start = stagger`), worst-case over all 4 note-serial offset residues (`first mod 4`) and all 4 strike phases (`phi` in `0..3`; `phi = 0` is worst). Default overflow policy (`master.voice.steal_priority = 0`, oldest killed/stolen first with `cancelSerial`) is worst-case for surviving voices because voices granted in tick 0 are discarded while their quad-0 work stays booked and every survivor waits for quad 1+. Start cache does not change ledger bookings or grant ticks. **Not a wall-time guarantee**: these bounds cover deterministic scheduler ticks / sample offsets only; hardware wall-clock latency also depends on cold caches, host scheduling, CPU clock, and concurrent load.
- **Pinned worst-case bounds at 900 us** (`test: core/spectral/tests/test_start_ledger.cpp start_ledger_key61_900us_16_note_chord_worst_case_bound`, `test: core/tests/test_own_osc_types.cpp own_spectral_key61_900us_16_note_chord_start_bound`):
  - Factory presets (16-note burst, Kill/Steal, 1 osc, U=2, mono): Chord Morph (poly 3) **8 ticks** (256 smp, 5.80 ms @ 44.1 kHz); Vowel Freeze / Gong Pad (poly 4) **12 ticks** (384 smp, 8.71 ms).
  - Key 61 profile cap (16-note burst, Kill/Steal to 8 notes, `unison <= 3`): 1 osc mono U=1 **16 ticks** (11.61 ms), U=2 **32 ticks** (23.22 ms), U=3 **48 ticks** (34.83 ms); 1 osc stereo U=3 **140 ticks** (101.59 ms); 1 osc Smear (mono or stereo) U=3 **96 ticks** (69.66 ms; stereo Smear books 1070 > 900 via the empty-ledger progress rule); 3 oscs mono U=3 **160 ticks** (116.10 ms); 3 oscs stereo U=3 **392 ticks** (284.44 ms, Key 61 legal worst case); 3 oscs stereo Smear U=3 **288 ticks** (208.98 ms).
  - Uncapped 16 notes (all 16 survive, 1 osc): mono U=1 **28 ticks** (20.32 ms), U=2 **60 ticks** (43.54 ms), U=3 **92 ticks** (66.76 ms), U=4 **124 ticks** (89.98 ms); stereo U=2 **184 ticks** (133.51 ms), U=3 **280 ticks** (203.17 ms).
- **Test commands**:
  - `ctest --test-dir <build> --output-on-failure -R "^(synthyboy_test_start_ledger|synthyboy_test_own_osc_types)$"`
  - `<build>/core/spectral/synthyboy_test_start_ledger start_ledger_key61_900us_16_note_chord_worst_case_bound`
  - `<build>/core/tests/synthyboy_test_own_osc_types own_spectral_key61_900us_16_note_chord_start_bound`

## Physical Key 61, 2026-11-23

At commit 966f9980 the ARMv7 engine test passed all 176 checks of the 16-note scheduler bounds on the Key 61. An independent static process tested the three factory Spectral presets, 8 and 16 notes, Kill and Steal, core 3, normal scheduling alongside the host, 4 s per cell. Five interleaved passes (900/1700/900/1700/900 µs) yielded 0/36 Spectral maxima at or above 1.5 blocks at 900 µs (worst 1.114), and 3/24 at 1700 µs (worst 1.575). Both maxima were 16-note Gong Pad / Steal. Peak sampled temperature 62.1 °C. The engine checks concern scheduler grant offsets; the benchmark times process blocks. Physical audio-output latency and owner listening preference were not measured. Raw CSVs, binary identity, package hash and runner: measured: .hw-local/mpc-perf/key61-spectral-20261123-XGOS/results.md.
