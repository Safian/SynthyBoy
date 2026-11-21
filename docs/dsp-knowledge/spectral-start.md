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

- Budget 1700 us (2026-11-21). The 4x2 chord (1604 us) still starts whole.

## Open

- Gong Pad 8 Kill and Chord Morph 8 Steal stay around 1.6 blocks: more runs, or another lever
  (HYPOTHESIS: not budget-limited). Worker-thread start preparation was considered and
  deferred: no free core on the Key 61 (host audio threads on all four), and it makes the start
  non-deterministic.
