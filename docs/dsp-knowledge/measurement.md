# Measuring performance

## Facts

- **A single device run cannot be compared with an older run.** On the MPC Key 61 the same
  binary gave 0.6 and 1.6 blocks for the same worst note-on block within one session
  (2026-11-21, 5ef901c8 binary, Acid Saw Reso; MPC process CPU share 17 % -> 8 %, load 2.1 -> 1.3,
  clock 1608 MHz and temperature unchanged). `measured: .hw-local/mpc-perf/noteon-burst/control-rerun.md`
- **The rule: interleaved A/B/A** (A1 B1 A2 B2 A3) in one session, the device quiet (owner not
  playing, no install running). A difference counts only when every A run is on one side of
  every B run and the gap is several times the spread. `measured: abba-results.md, c1-abba-results.md`
- **Worst-block values are noisy:** about +-1 ms (+-0.3 blocks of 2.9 ms) between two runs of
  the same binary; p99 moves at most about 0.2 ms. Decisions on worst blocks need 3-5 runs per
  variant. `measured: budget-sweep-results.md (2026-11-21)`
- **Section means are stable:** the filter section mean per block spreads 0.002-0.005 ms over
  runs, so a 0.04 ms (5 %) change is clear. `measured: c1-abba-results.md`
- **Mac timing does not predict the Cortex-A17:** a change that is neutral on an M-series core
  (wide out-of-order) can matter on the A17, and the reverse (diode pair loop 2026-11-21: Mac
  2.29 -> 2.27 %, device pending). Use the Mac only to check that nothing got grossly slower.
- **Bench setup on the Key 61:** `synthyboy_tier_bench` armv7 static, core 3 (kernel-isolated),
  config `low`, governor performance at 1608 MHz. Scripts: `scripts/perf-device.sh`,
  `run-noteon-burst.sh`; docs: `docs/perf/README.md`.

## Decisions

- Device timing claims in plans, commit messages and TODO carry the method (A/B/A, runs) and
  the spread. Single-run numbers are labelled as such.

## Thermal limits and the run rule (XL, read-only, 2026-11-21)

- XL (RK3588): zones package, bigcore0, bigcore2, littlecore, center, gpu, npu (step_wise). Passive trips at 85 C on bigcore0, bigcore2, littlecore and gpu; the center zone has two ACTIVE trips (75 C and 85 C: the pwm-fan, cooling device0, max state 3); every zone has a critical trip at 115 C. Cooling devices: pwm-fan, cpufreq cpu0 / cpu4 / cpu6 (max 4 / 6 / 6), devfreq gpu (max 7). Idle-ish reading 62.8-64.7 C (load average 3), fan state 0.
- GPU devfreq (`fb000000.gpu`): simple_ondemand, 300..1000 MHz in 100 MHz steps.
- Rule for every device run (owner, 2026-11-21): record the start and end temperature of all zones; stop the run and cool down if any zone comes within 10 C of its first passive trip point: XL 75 C (the fan's first active trip is also 75 C), Key 61 85 C (our own limit, see platform-key61.md).
