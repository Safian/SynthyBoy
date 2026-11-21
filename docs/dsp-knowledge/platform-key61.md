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

- **Third failed lever, the precomputed 4x4 inverse** (tier-low fast path, branch worktree-agent-af82a810d4a3e202d, not merged): Mac 29 % faster, Key 61 13 % SLOWER (plain median 0.978 -> 1.127 ms, filter section 0.705 -> 0.824 ms, +254 k instructions per block, IPC 0.73 -> 0.81: the extra instructions outweigh the gained overlap). `measured: key61-night-2026-11-21/diode-inverse/job1-results.md (A/B/A x3)`. Rule learned: on the A17 the diode loop is best left as the plain elimination; the Mac is not a predictor for this loop.

## Open

- Diode Saturated Bass filter section is 0.72 ms/block (about 4x the gate). Saturator curve
  change = sound change (owner decision); estimated gain small because the chain, not the
  divide, dominates (HYPOTHESIS).

## Thermal limits (read-only, 2026-11-21)

- Key 61 (RK3288): zone0 `cpu-thermal` and zone1 `gpu-thermal`, governor step_wise, ONE trip point each: 113 C critical. NO passive (throttle) trip point: the cooling devices `cpufreq-cpu0` (max state 11) and `devfreq-ffa30000.gpu` (max 4) exist but nothing steps them before 113 C. Reading while a benchmark ran: cpu 68.8 C, gpu 66.5 C (tonight's benches peaked at 69-72 C). No fan.
- Rule for every device run (owner, 2026-11-21): record the start and end temperature of every zone; the Key 61 has no passive trip, so the limit is set by us: stop and cool down at 85 C (28 C under critical, still 15 C above the highest value seen tonight).
- GPU devfreq (`/sys/class/devfreq/ffa30000.gpu`): governor simple_ondemand, 100..600 MHz, steps 100/200/300/400/600 MHz.

## GPU (Key 61 Mali-T760 and XL Mali-G610; EGL probe 2026-11-21)

- Both: Mesa Panfrost through a surfaceless EGL context on the render node (probe: `.hw-local/mpc-tools/gpu-info/`, outputs `k61.txt`, `xl.txt`). OpenGL ES 3.1 context works, 3.2 fails. COMPUTE SHADERS compile and link (`#version 310 es`). No OpenCL, rusticl or ICD file on either device.
- Key 61: Mesa 24.0.7, EGL 1.4, compute work group size 128 and 128 invocations, GL_EXT_color_buffer_float, EXT_shader_framebuffer_fetch, OES_texture_float present; missing against the XL: EXT_gpu_shader5, OES_sample_shading, EXT_disjoint_timer_query, EXT_multi_draw_indirect, EXT_texture_storage. devfreq 100-600 MHz (100/200/300/400/600), simple_ondemand.
- XL: Mesa 24.3.4, EGL 1.5, work group 256 x 3 / 256 invocations, shared memory 32 KiB, 8 compute SSBO blocks, 192 image units, devfreq 300-1000 MHz.
- A GPU dispatch plus read-back through shared memory suits non-realtime parallel work; whether a GPU FFT pays off is unmeasured (open item in TODO.md: the overlay thread time split decides).


## SynthyBoy vs a stock Akai synth (Key 61, 2026-11-21)

- 8-note chord (MIDI 48 52 55 59 60 64 67 71, vel 100), 30 s, 3 interleaved rounds, spread < 0.2 points, MPC process % of one core, net of each plugin's idle: AIR TubeSynth "Rich Swell" 18.8; SynthyBoy "Acid Saw Reso" 22.8 (overlay hidden) / 30.0 (visible); "Lorenz Garden" (unison 3, 3 wavetables, 2 filters, chorus + delay, 6 voices) 52.7 / 60.3. Heavy preset about 3x TubeSynth, light preset 1.2-1.6x. Our overlay costs about 8 points idle and 15 during the chord (overlay thread 9-10 %). The claim "costs about as much as Akai's own synths" is NOT supported; not to be written in the user guide. Not compared: Akai wavetable synths (Hype, Mini D, Jura). `measured: key61-night-2026-11-21/cpu-compare/results.md`
