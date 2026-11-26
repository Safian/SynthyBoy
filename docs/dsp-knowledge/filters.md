# Filters

## Facts

- Serum-type DiodeLadder (`core/filters-serum/src/filter_dsp.h`): zero-delay feedback with a
  linear start value and Newton steps on `f(u) = u - sc(D - k sc(p + c u))`, `f' >= 1` (unique
  root, denominator never below 1). One Newton step leaves 2.8e-3 relative RMS error at fc
  21.6 kHz, res 1, drive 1; two steps 2.7e-6. `test: serum_diode_d7 (fix ea3c38a5)`
- Classic Diode (model 4) is a separate implementation (`core/engine-own/voice/classic_filters.h`,
  tridiagonal trapezoidal solve, saturators on the old states); its cost and the failed C1 are
  in [platform-key61.md](platform-key61.md).
- Ladder clamp: `std::min(g, 50.0f)` for the MG/EMS ladders (diode excluded).

- The preset format stores a PZ SVF drawing as up to 16 ordered log-frequency/dB points in `StateContent::filterCurves`. The JUCE and MPC editors use that content. The current own-engine PZ SVF uses the blend between the low-pass and notch outputs. Its audio path does not evaluate the stored curve. The MPC editor preview draws the saved points, and the ordinary response plot measures the current filter implementation. `source` `core/include/synthyboy/content_state.h`, `core/engine-own/own_engine.cpp`, `core/filters-serum/src/filter.cpp` (PZSVF case), `shell-mpc/ui/pz_curve_editor.h`; `test` `filter_ui_host`.

## Open

- Classic Diode: C1 batch inversion +5.5 % and the two-channel pair loop +4.8 % on the Key 61 (both rejected, see platform-key61.md). The section stays 0.72 ms/block; next: profile it (PMU counters, `tier_bench --pmu --sections`) before any change.

## Savings of 2026-11-26 (x86 host evidence; the Cortex-A17 effect is HYPOTHESIS until measured, TODO.md)

Review source: callgrind of Diode Saturated Bass (ClassicFilter::block 256x per block, tanf 704k, powf 415k, exp2f 91k per 688 blocks). One commit per item; every bit-identical item has a test against the code it replaced (`test: core/tests/test_own_filter_savings.cpp`).

- **Twin channels (bit identical).** The right channel of a filter with equal parameters adopts the memo the left one just computed (`ClassicFilter::adoptMemo`, `processTwin`): Diode, Formant and the exact SVF / Ladder pieces compute their setup once per piece for both channels. Comb and Phaser keep no memo of their setup, so they do not share (open, TODO.md).
- **sameParams once (bit identical).** `FilterUnit::set` keeps the answers as booleans; the clean gain's `dbToGain` is remembered for its last argument (`GainMemo`), so it runs once per block, not per sample.
- **Integer compares (bit identical).** `flushDenormal`, the `fastmath::tanh` clamp and the `hardtanh` knee compare the magnitude bits. `test: own_filter_savings_integer_compares_*`: all 2^32 patterns, 0 differences (NaN payload included), plus the denormal range with flush-to-zero on. armv7 listing (clang, snippet): `vcmp` + `vmrs` gone (1 / 2 / 1 flag transfers to 0).
- **Diode dead terms (equal as floats).** With `kDiodeW = 1` the `(1 - w)` terms and `up * 0.0f` were still computed. Removed; differences only in the sign of an exact zero or for a non-finite state (0 inf = NaN): 0 differences in 245,760 samples, none finite-to-non-finite. The unused tangent `gp` (extra pole off) is no longer computed. **Not done:** skipping the input high pass when `amount == 0`: its two one-poles (corner down to 1 Hz) hold the whole input history, so a blend switch 0 -> non-zero cannot be warmed up without changing the output.
- **Diode ramp, tier low (not bit identical).** Setup at both ends of the control period, interpolated per 8 sample piece. Bound against the generic piece loop: peak within 0.083 dB, bins within 30 dB of the peak within 0.171 dB for up to 4 semitones of cutoff per period (criterion 0.5 dB); shipped 12 dB presets within 0.02 dB up to 12 semitones; 24 dB style with cutoff and blend both moving 12 semitones per period 4.8 dB. `test: own_filter_savings_diode_ramp_*`. Documented in docs/MPC.md.
- **SVF reciprocal, tier low (not bit identical).** `tpt` multiplies by the per-call reciprocal of `1 + g R + g g`. At most 4.5e-7 for an input peak 0.05, RMS -103.6 dB re the input RMS over 120 settings. `test: own_filter_savings_svf_reciprocal_*`. The NEON Digital pair loop still divides (no ARM toolchain / qemu here; TODO.md).
- **Serum filters (bit identical).** Pair kernel: lane 1 takes lane 0's tan / pow2 for bit-equal arguments; scalar path: `Filter::processBoth` shares the memo for the call. `test: own_filter_savings_serum_*`, `test_filter_pair`, `test_filters_serum`.
- **Measured on the x86 host (not the device).** callgrind, instructions per `FilterUnit::process` call, tier low: Diode Saturated Bass 20,815 -> 13,966 (-32.9 %; tanf 51.4 M -> 2.3 M, powf 23.1 M -> 1.3 M per run), Diode Bite Lead -11.7 %, Fat Reese (Digital) -5.5 %, Analog Sweep Bass -8.3 %, Dirty Distortion Bass -3.6 %, Acid Saw Reso -2.4 %, Subterranean Duo -3.9 %, Wah Pressure Clav -3.1 %. Interleaved A B A B A runs of tier_bench (median block %, 4 s): Diode Saturated Bass A 4.69 4.86 4.28, B 4.01 3.97; Fat Reese A 2.93 3.04 2.95, B 2.87 2.55 (shared VM, noisy).
