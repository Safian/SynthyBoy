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

- **ADAA has a half-sample delay and a Nyquist zero** in the linear regime (`(x[n]+x[n-1])/2`); Vicanek (2023/24) moves the kernel by half a sample: one sample delay, only 6 dB loss at Nyquist, and a one-pole pair `a = 1/(3+sqrt 8)` makes it exactly transparent. `source: sources.md (Vicanek, Parker et al. Sec. 4)`
- **ADAA alone is weak at high-frequency aliases; ADAA + 2x-4x oversampling is the robust combination** (Vicanek Sec. 4; Zavalishin 6.13: 44.1 kHz is "usually insufficient"). This supports D1 (2x on XL/desktop). HYPOTHESIS to test: Vicanek's half-sample-shifted kernel instead of plain first order on tier low (no oversampling there).
- **Inside zero-delay feedback loops** the half-sample delay must be absorbed into the following trapezoidal integrator (Parker et al. Sec. 6, Zavalishin 6.13); relevant if a filter saturator gets ADAA.

- **X-Shaper (types 12, 13) with ADAA (X2, X3):** steep curve, alias -36.83 dB naive to -42.96 dB with ADAA (-6.14 dB), harmonics unchanged (2.62 / 2.60 dB). `measured: 2026-11-21, Mac, Release, batch 4 merged tree; test: core/tests/test_own_alias_dist_shaper.cpp (DIST-AA type=12), test_own_alias_shaper_engine.cpp`. Types 0-11 keep their checksums bit for bit (DIST-AA-CHECKSUM lines of the five alias tests differ only at types 12 and 13).
- **Library oscillators L1 (host rate):** Sample type, note 84: Hard Clip -5.24 dB alias (-42.05 to -47.30), Quantize -8.50 dB (-22.45 to -30.95), harmonics within 0.03 dB. `measured: same run; test: core/tests/test_own_alias_lib_warp.cpp (LIBWARP-AA)`. Without warp nothing changes (bits).
- **Oscillator warps O2 (sample-domain warps in WtOsc, ADAA):** note 84, 4x: Zero Square 29 -9.73 dB, Quantize 3 -12.39 dB, 2x (lowWarp): -15 to -20 dB; harmonics within 0.1 dB. `test: core/tests/test_own_alias_warp_adaa.cpp (WARP-AA)`. `WARP-CHECKSUM aa=off` of all 52 types is bit-identical to the code before O2; `aa=on` differs from `aa=off` for 20 types: the 15 sample-domain ones (3, 27-40) and the five with events (Sync 1, Pulse 6, Sync Var 11, Flip 18, Mirror 19) that O3/O4 change on purpose.
- **O3 PolyBLEP at the master wrap (Sync 35 %):** alias drops 14.5 to 26.3 dB (notes 60 to 96, 2x and 4x), harmonics within 0.05 dB; the 2x-against-4x difference at note 60 is -14.23 dB off and -14.25 dB on, so the residual does not close the gap between the two rates. **O4 PolyBLAMP:** Mirror 80 % -19.9 dB, Flip 50 % -17 to -18.6 dB, Sync 35 % with BLEP+BLAMP a further 7 to 17 dB below BLEP alone. **Pulse 50 %** was -2.93 / -2.97 dB (notes 72 / 84) while `warpEvents` had only the clamp slope jump; with the second slope jump at phase 0 (the slope rises from 0 again at the wrap; the value is continuous) it is -16.82 / -18.52 dB (measured: 2026-11-21, Mac, Release). `test: test_own_alias_warp_blep.cpp, test_own_alias_warp_blamp.cpp`. Block splits (32, 7+25, 1+31) give identical bits.
- **Half-band 2x oversampler (E7, `fx_oversampler.h`):** unit-tested only (taps, gain and latency, symmetry and DC, passband ripple, stopband, round trip); not wired into the distortion yet. `test: core/tests/test_own_alias_oversampler.cpp`.
- **The drive of the distortion is ramped across the control period** (gain, morph and divisor, linear), not stepped: a 30 dB jump made a step 15 times the steady sample step before, 1.00 times now. The ADAA state needs nothing extra: `adaa::step` and `stepShaper` re-make the stored integral at the last input when the gain (or morph) changes, so a gain that changes every sample stays a consistent difference quotient (cost: one more antiderivative per sample during the ramp). `test: own_distortion_drive_does_not_step (test_own_engine_fixes.cpp), fx_distortion_drive_ramps_across_the_period (test_own_fx.cpp)`. A still drive is bit-identical (all DIST checksums unchanged).

## Open

- Oversampling E8-E9 (the half-band is not used yet).
