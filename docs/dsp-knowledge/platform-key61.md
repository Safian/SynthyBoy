# Platform: MPC Key 61 and MPC XL

## Facts

- **Key 61:** ARMv7 Cortex-A17, 4 cores at 1.608 GHz, NEON, 4 GB RAM (MemTotal 4,113,388 kB),
  **no swap** (the OOM killer is the only limit), cores 2-3 isolated by the kernel, the host's
  audio threads on all four cores, 44.1 kHz, 128-frame block = 2.902 ms, quality tier `low`.
  `measured: coordinator, /proc on the device, 2026-11-21; docs/MPC.md`
- **XL:** ARM64 RK3588 (4x A55 + 4x A76), audio threads pinned to cores 4-7, tier `high,voices=16`.
- **Memory per SynthyBoy instance (Key 61, tier low):** engine heap 72.4 MB (peak 105.6 MB),
  4761 allocations; host RSS grows 80-90 MB per added SynthyBoy track; 3 tracks leave 2.54 GB
  available. The 33.1 MB wavetable bank is per process. `prepare()` does not scale with the
  tier yet (xl/high allocate the same). `measured: docs/MPC.md "Memory on the Key 61"`
- **Division is not the bottleneck of the classic Diode loop on the A17:** replacing four
  independent `sat()` divisions by one (batch inversion, C1) made the filter section 5.5 %
  SLOWER (0.7249 -> 0.7648 ms/block, A/B/A, every A slower than every B). The loop is bound by
  the latency of the sample-to-sample recurrence, not by divider throughput.
  `measured: .hw-local/mpc-perf/noteon-burst/c1-abba-results.md; reverted 39f8323c`
- **Independent recurrences overlap:** two channels in one loop (SVF pair loops, NEON 2 lanes)
  is the lever for latency-bound filters. `source/test: classic_filters.h svfPairLoop`; diode pair
  loop on the device: pending.
- **Bit identity across builds needs `-ffp-contract=off`** when comparing standalone compiles:
  FMA contraction alone produced "differences" (Bode FIR, 2026-11-20).

## Open

- Diode Saturated Bass filter section is 0.72 ms/block (about 4x the gate). Saturator curve
  change = sound change (owner decision); estimated gain small because the chain, not the
  divide, dominates (HYPOTHESIS).
