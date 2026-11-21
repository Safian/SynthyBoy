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
- **WtOsc shared MorphScratch and tier-sized history (T5):** `WtOsc::prepareMorph (shared, maxUnison)` takes an engine-shared `MorphScratch` (24,592 B) and sizes the oversampling history to `maxUnison * 127` floats; per oscillator `morphOwnedBytes()` 65,552 B (private scratch, 16 voices) -> 34,864 B (shared, unison 4), 30,688 B less, 2.63 MB over 108 oscillators; bit-identical sound (WTOSC-SHARED-SCRATCH-CHECKSUM, 4 morph types). **Not wired into Voice / OwnEngine yet** (T8/T9): `synthyboy_test_own_memory` on the Mac, develop -> merged tree: 66,933,896 -> 66,938,528 B per instance (+4,632 B, the new members), 2485 allocations both, so no instance saving exists until the wiring lands. `measured: 2026-11-21, Mac arm64, Release; test: own_wt_osc_external_shared_morph_scratch_and_tier_sized_history, synthyboy_test_own_memory (MEM-BASELINE)`
- **Division is not the bottleneck of the classic Diode loop on the A17:** replacing four
  independent `sat()` divisions by one (batch inversion, C1) made the filter section 5.5 %
  SLOWER (0.7249 -> 0.7648 ms/block, A/B/A, every A slower than every B). The loop is bound by
  the latency of the sample-to-sample recurrence, not by divider throughput.
  `measured: .hw-local/mpc-perf/noteon-burst/c1-abba-results.md; reverted 39f8323c`
- **Interleaving two channels did NOT help the classic Diode on the A17:** a bit-identical
  two-channel pair loop (`9ab6e01b`, not merged) was 4.8 % SLOWER (0.7234 -> 0.7579 ms/block, A/B/A,
  every A above every B; Acid control equal). With C1 (+5.5 %) that is two failed levers: the
  "latency-bound recurrence" explanation is NOT confirmed either (HYPOTHESIS now: register
  pressure on ARMv7 VFP/NEON; the plain per-channel code at `25aaa77b` is the best measured).
  Next step must start from a measurement of where the time goes (perf counters / sections),
  not from another guess. `measured: noteon-burst/pair-abba-results.md, 2026-11-21`
- SVF pair loops (`classic_filters.h svfPairLoop`, NEON 2 lanes for model 3) remain; whether they
  help on the A17 was not measured with A/B/A (HYPOTHESIS).
- **Bit identity across builds needs `-ffp-contract=off`** when comparing standalone compiles:
  FMA contraction alone produced "differences" (Bode FIR, 2026-11-20).

## Open

- Diode Saturated Bass filter section is 0.72 ms/block (about 4x the gate). Saturator curve
  change = sound change (owner decision); estimated gain small because the chain, not the
  divide, dominates (HYPOTHESIS).
