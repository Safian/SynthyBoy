# Anti-aliasing

Plan: `docs/plans/distortion-antialiasing.md` (owner decisions D1-D5, 2026-11-21).

## Facts

- First-order ADAA on the distortion: Zero-Square (type 9) gains **5.91 dB** alias reduction at
  drive 0 where the plan's model said 10.1 dB; the kernel matches a double-precision reference,
  so the model was optimistic. Acceptance threshold for every type: >= 3.0 dB.
  `test: core/tests/test_own_alias_dist_tanh.cpp`
- ADAA lowers the wet-path treble (about -2.4 dB at 10 kHz, derived); the remedy is 2x
  oversampling (stage C, D1: on XL/desktop only, 15 samples latency), not a compensation filter.
- Quantize ADAA: for a segment shorter than 1/1024 use the exact staircase average, not the
  midpoint level (error 0.0276 -> 1.7e-4). `test: own_alias_warp_* (fix 6085f61f)`

## Open

- Oscillator warps O2-O4 (wt_osc.h), library oscillators L1, X-Shaper X2-X3, oversampling
  E7-E9: in progress.
