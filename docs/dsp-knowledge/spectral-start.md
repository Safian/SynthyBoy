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

## Decisions

- Budget 1700 us (2026-11-21, merged). The 4x2 chord (1604 us) still starts whole; in an 8x2 chord 8 voices start at once, 4 wait 2.9 ms and 4 wait 5.8 ms (`test: own_spectral_chord_start`).

## Open

- Gong Pad 8 Kill and Chord Morph 8 Steal stay around 1.6 blocks: more runs, or another lever
  (HYPOTHESIS: not budget-limited). Worker-thread start preparation was considered and
  deferred: no free core on the Key 61 (host audio threads on all four), and it makes the start
  non-deterministic.

## Budget 1700 vs 1400 us, interleaved confirmation (Key 61, 2026-11-21, develop f37e79cf)

- Method: tier_bench-style note-on-burst, 1700 / 1400 / 1700 / 1400 / 1700 / 1400 us interleaved, 12 Spectral rows (3 presets x 8/16 notes x kill/steal) plus the Acid Saw control, core 3, 1608 MHz, 66-70 C. Files: `.hw-local/mpc-perf/key61-night-2026-11-21/job2-results.md`.
- Mean worst block over the 12 rows: 1400 us = 1.25 blocks, 1700 us = 1.42 blocks; 1400 is lower on 9 of 12 rows, but 11 of 12 differences are inside the noise (rule: more than max(0.3 blocks, run spread)). Only Chord Morph 8 Steal clears it (1.30 -> 0.98). Gong Pad 8 Kill: 1.28 vs 1.34 (noise). Rows under 1.5 blocks: 9/12 at 1700, 10-11/12 at 1400. Steady p99 unchanged (0.66 vs 0.67). Control 0.69-0.79 at both.
- The earlier 1.59 / 1.62 readings did not reproduce at 1700 on this HEAD (1.28 / 1.30): noise or the newer code.
- Decision (coordinator, 2026-11-21): the budget stays 1700 us (1400 is only weakly better and costs more voice delay). A start-frame cache (memoised start frames for repeated notes, bit-identical) is being analysed and may remove the rest.

## Start cache (2026-11-21, branch worktree-agent-aadd1287d0f23c8e2, off by default)

- Bit-identical memo of the start frames (process-wide, 3.2 MB, lock-free). Key 61 A/B/A (OFF/ON x5, budget 1700): Chord Morph strike 1.18-1.43 -> 0.77-0.88 blocks (repeated chord); Vowel Freeze and Gong Pad unchanged (no hits: random start or modulated inputs at the note-on, per the plan's caveat, hit counters not printed); a 16-note chord transposed every strike (all misses) costs about +0.1 block, inside the noise. Output bit-identical in all 72 comparisons. `measured: key61-night-2026-11-21/start-cache/job2-results.md`
