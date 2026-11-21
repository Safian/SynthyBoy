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

## Open

- Classic Diode: C1 batch inversion +5.5 % and the two-channel pair loop +4.8 % on the Key 61 (both rejected, see platform-key61.md). The section stays 0.72 ms/block; next: profile it (PMU counters, `tier_bench --pmu --sections`) before any change.
