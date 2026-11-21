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

- Classic Diode two-channel pair loop: bit-identical, device A/B/A pending.
