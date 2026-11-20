# Expert DSP Audit — Own Synth Engine, Part 3: Core, Performance, and Memory

**Date:** 2026-11-20  
**Scope:** Engine scheduling and `process()` path (`core/engine-own/own_engine.*`), voice pool and layout (`core/engine-own/voice/`), cross-thread hand-over (`core/engine-own/dsp/helper_thread.h`, `core/engine-own/voice/own_content.h`, `core/engine-own/osc/wavetable_bank.*`, `core/engine-own/osc/content_set.*`, `core/engine-own/fx/fx_chain.*`, `core/engine-own/fx/fx_modules.cpp`), memory and cache hierarchy, SIMD and numerics policy (`core/engine-own/dsp/simd.h`, `fastmath.h`, `neon_cmp.h`, `util.h`), quality tiers (`core/include/synthyboy/quality.h`, `shell-mpc/device_profile.h`), build and toolchain (`CMakeLists.txt`, `core/CMakeLists.txt`, `cmake/toolchains/*.cmake`, `shell-mpc/CMakeLists.txt`), benchmark methodology (`bench/tier_bench.cpp`, `bench/noteon_bench.cpp`, `bench/bench_engine.cpp`, `docs/PERFORMANCE.md`, `docs/QUALITY.md`, `docs/MPC.md`), and the MPC plugin shell audio boundary (`shell-mpc/synthyboy_mpc.cpp`, `event_inbox.h`, `callback_stats.h`, `play_log.h`, `output_peak.h`, `spectrum.h`, `overlay/gpu/scene.h`).  
**Targets:** Akai MPC Key 61 (ARMv7 Cortex-A17, 4 cores @ 1.608 GHz, 32 KB L1D / 1 MB L2, 4 GB RAM, NEON, 2 isolated audio cores `isolcpus=2-3`, 44.1 kHz, 128-frame block = 2.902 ms budget, profile `low`), MPC XL (ARM64 RK3588, 4× Cortex-A55 + 4× Cortex-A76 @ 2.4 GHz, 64 KB L1D / 512 KB L2 / 3 MB L3, profile `high,voices=16,control=economy`), iOS (Apple ARM64), desktop (macOS/x86_64/Windows).  
**Evidence tags:** `[code file:line]` = verified directly in source; `[measured: doc:line]` = existing measurement recorded in repository docs; `[hypothesis: measurement]` = analytical/engineering prediction paired with the measurement that confirms it.  
**Cross-references:** Part 1 (`docs/review/OWN-ENGINE-DSP-AUDIT-1-VOICE-PATH-2026-11-20.md`, Ranks 1–27) and Part 2 (`docs/review/OWN-ENGINE-DSP-AUDIT-2-EFFECTS-OUTPUT-2026-11-20.md`, Ranks 1–19) are referenced by `Part 1 #N` and `Part 2 #N` and are not repeated here.

---

## Verdict

The own engine's core is built on several genuinely strong real-time principles — a block-size-independent 32-sample absolute control grid, planar float buffers, Dekker-style hazard-pointer swaps for `ContentSet`, a lock-free `OutputStage`, clean separation of the GPU overlay's `SharedBlock` onto non-audio cores, and disciplined ARMv7 awareness (`activeMask` bitset iteration, `ffloor` bit-level helpers, `neon_cmp.h`, `kEconomyControl` ghost-kill and event-grid snapping) — yet it still carries critical system-level flaws in memory scaling, thread safety, compiler configuration, and measurement discipline that directly threaten drop-out-free operation on the 4 GB Cortex-A17 MPC Key 61. Specifically, `prepare()` unconditionally allocates 36 full voices (~43.6 MB) and all 13 FX modules across 3 racks (~26–27 MB) even on tier `low` where only 8 voices and 8 FX are usable (~70–75 MB per plugin instance, risking page reclaim and OOM with 3–4 instances); `ContentHub` has an unsynchronised 8-slot overwrite ring with a real use-after-free hazard; `WakeSignal::notify()` locks a mutex on macOS/iOS/Windows and issues `SYS_futex` on Linux directly from the audio thread while `ConvolveModule` can drop a `shared_ptr` (`free()`) on queue overflow; 16 simultaneous note-ons bypass `maxVoices` during the strike block and run unbounded work (amplifying the measured 2.6–3.1-block Spectral cold-cache spike); ARM32 builds still compile with `-march=armv7-a` instead of `-mcpu=cortex-a17`, lack `-fno-math-errno`, `-fvisibility=hidden` on `synthyboy_core`, and LTO; `SB_NEON` is disabled on iOS and `shell-clap`/iOS have no FTZ/DAZ guard; and `tier_bench` runs `SCHED_OTHER` on a shared audio core with no PMU cache counters, introducing ±20% noise that obscures real optimisations. Fixing the toolchain flags, shrinking tier-scaled memory pools and hot/cold voice layout, closing the real-time thread-safety holes, capping strike bursts, and making on-device measurements RT-isolated with PMU counters will recover more headroom and reliability than any isolated micro-optimisation.

---

## NOT COVERED

None within Part 3 (engine core, scheduling, voice management, threading/hand-over, memory/cache, SIMD/numerics policy, quality tiers, build/toolchain, measurement method, and MPC shell audio callback boundary). All hot audio paths and cross-thread hand-over boundaries were read completely.

Out of scope for Part 3:
- Voice-path DSP algorithms and math (already audited in Part 1, `docs/review/OWN-ENGINE-DSP-AUDIT-1-VOICE-PATH-2026-11-20.md`).
- Effect module DSP algorithms, gain staging math, and master/output stage DSP (already audited in Part 2, `docs/review/OWN-ENGINE-DSP-AUDIT-2-EFFECTS-OUTPUT-2026-11-20.md`).
- Overlay GPU rendering internals (`shell-mpc/overlay/gpu/` outside `SharedBlock` hand-over) and non-audio UI widget layout.
- Forbidden clean-room directories (`core/engine-vital/`, `core/vital-import/`, `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/`).

---

## Ranked Findings Table

| Rank | Module | Finding | Evidence | Recommended Change | Gain Class | Risk | Proving Measurement |
|---:|---|---|---|---|---|---|---|
| 1 | 4 (Memory / Tier Scaling) | `OwnEngine::prepare()` unconditionally allocates all 36 `Voice` objects (~43.6 MB) and all 13 FX × 3 racks (~26–27 MB) even on tier `low` (`maxVoices = 8`, 5 FX masked), costing ~70–75 MB/instance (300 MB for 4 instances on 4 GB Key 61) and `186.8 ms` startup. | `[code core/engine-own/own_engine.cpp:426-431,452-454]`, `[measured: docs/MPC.md:148,327]` | Size voice pool to `profile.maxVoices + 4` (12 on Key 61, 20 on XL), skip allocating delay buffers for FX masked out by `fxMask` (or lazy-prepare off-thread), and pool `LibOsc`/`FilterImpl`/`Reverb` buffers (extending Part 1 #10 & Part 2 #5). | Large (RAM ~70% + startup >60% + L2/TLB) | Medium (pool resize if tier changes at runtime; currently static per process) | `VmRSS` before/after loading 1 and 4 instances on Key 61, `engine_prepare` startup breakdown (`prepare_voices` 95.6 ms, `prepare_fx` 73.6 ms), and `tier_bench --config low` strike-block latency. |
| 2 | 3 (Threading / `ContentHub`) | `ContentHub::publish()` is called outside `contentMutex` (data race on `ring[]`/`next`) and frees `ring[slot]` after 8 updates without verifying the audio thread is done reading `contentNow` / `lfoSet[l].custom` (use-after-free on rapid UI LFO/curve drags), while wasting ~4.3 MB/instance. | `[code core/engine-own/own_engine.cpp:230,1351]`, `[code core/engine-own/voice/own_content.h:59-106,151-155]` | Fold `OwnContent` into `ContentSet` under the existing Dekker hazard pointer (`contentCurrent`/`contentInUse`), and store only present `modCurve` tables (sparse pointer/vector instead of 64 × 8 KB fixed arrays). | Large (RT crash safety + ~4 MB RAM) | Low | TSan stress test hammering `setContent()` 100×/s from two threads while `process()` runs at 128 frames; verify zero UAF/race and `OwnContent` footprint < 100 KB on typical presets. |
| 3 | 3 (Threading / `WakeSignal` & `Convolve`) | `WakeSignal::notify()` locks `std::mutex` + `cv.notify_one()` on macOS/iOS/Windows and invokes `syscall(SYS_futex)` on Linux directly from `OwnEngine::process()` (via `FxWorker::submit`) and `setParam()`; `ConvolveModule::startBuild`/`makeupPower` can drop the last `shared_ptr<Impulse>` (`free()`) on the audio thread when the job queue is full. | `[code core/engine-own/dsp/helper_thread.h:36-47,71]`, `[code core/engine-own/fx/fx_modules.cpp:52,820,989-994]`, `[code core/engine-own/own_engine.cpp:278]` | Replace audio-thread `WakeSignal::notify()` with a pure lock-free atomic counter polled by a lightweight non-RT thread (or `dispatch_semaphore`/Mach/pipe non-blocking wake only from non-RT threads, or 1–2 ms backoff wait on worker); replace `shared_ptr<Impulse>` in `ConvolveModule::Job` with a raw `const ContentSet::Impulse*` kept alive by an explicit off-thread retirement queue (matching `FxPool::grave`). | Large (RT drop-out / priority inversion safety) | Low–Medium | Audio-thread syscall/lock audit (`strace -e futex` / macOS `dtrace` / `malloc` hook) during rapid `fxi01.type` and `convolve.size` automation; verify zero mutex, futex, or `free()` calls on audio thread. |
| 4 | 7 (Build / Toolchain) | ARM32 MPC builds use `-march=armv7-a` (forfeiting hardware `sdiv`/`udiv` on 137 `__aeabi_idiv` sites and Cortex-A17 scheduling), omit `-fno-math-errno -fno-trapping-math` (forcing `libm` branches on every `sqrtf`), omit `-fvisibility=hidden`/`-fno-semantic-interposition` on `synthyboy_core` (blocking intra-TU inlining of `Voice::modulatedSlow` etc.), omit LTO on MPC, and pass `--gc-sections` without `-ffunction-sections -fdata-sections`. | `[code cmake/toolchains/armv7-linux-gnueabihf.cmake:14-15]`, `[code core/CMakeLists.txt:59]`, `[code shell-mpc/CMakeLists.txt:18-24]`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:32,104,119,352-358]` | Switch ARM32 to `-mcpu=cortex-a17 -mfpu=neon` and ARM64 to `-march=armv8-a -mtune=cortex-a76`; add `-fno-math-errno -fno-trapping-math -ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden -fno-semantic-interposition` to `synthyboy_core` and DSP sub-libs; enable Thin/Full LTO (`-flto=4`) for release MPC builds. | Large (CPU 5–15% zero-DSP-change) | Low (bit-identical with `-ffp-contract=off` kept; verify against RK3288 hardware) | Interleaved `rtrun` A/B `tier_bench --config low --all-presets --verify` on Key 61 and XL comparing current vs tuned+LTO build, plus `objdump` verifying 0 `__aeabi_idiv` and 0 `vsqrt`+branch pairs in hot kernels. |
| 5 | 2 (Voice System / Burst Cap) | `allocateVoice()` in Kill mode returns any free slot from the 36-voice pool before `killExcess()` runs at block end, so 16 simultaneous note-ons on Key 61 (`maxVoices = 8`) execute `start()` + `tick()` + Spectral priming for all 16 voices in one strike block (compounding the measured 2.6–3.1-block 8-note Spectral strike overrun). | `[code core/engine-own/own_engine.cpp:672-682,852-857,1760]`, `[measured: docs/MPC.md:353-354]` | Cap simultaneous newly-allocated active voices inside `noteOn()`/`startNote()` to `polyphonyLimit()` (selecting the top-$P$ notes of a burst before calling `start()`/`tick()`), and stagger Spectral priming (e.g., max 2–4 voices primed per 128-frame block on tier `low`). | Large (worst-case strike p99 / drop-out prevention) | Low–Medium (staggering adds 2.9–5.8 ms latency only to voices 5–8 of a Spectral chord strike) | `synthyboy_noteon_bench` extended to 16-note simultaneous burst on Key 61 (`Spectral Vowel Freeze`, `Twin Warp Lead`, `Grand Saw Pad`) measuring worst-block ms (target `< 2.90 ms`). |
| 6 | 2 & 4 (Voice Layout & `MEM1`) | `sizeof(Voice) = 33,128 B` interleaves cold setup/cache structs with hot per-sample state, every `WtOsc` owns a private 24.6 KB `MorphScratch` (2.66 MB across 36×3 oscs) instead of one engine-shared scratch, and ~3,500+ small heap allocations per instance scatter hot voice memory across pages, causing the 30–50 KB per-voice working set to overflow Key 61's 32 KB L1D every period. | `[code core/engine-own/voice/voice.h:229-397]`, `[code core/engine-own/osc/wt_osc.h:520,1223]`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:25,326-346]` | Move `MorphScratch` into `TickGlobals` (1 shared instance = 24.6 KB vs 2.66 MB, matching `spectralScratch`); split `Voice` into `VoiceHot` (aligned, contiguous ~4–6 KB per voice in a single arena) and `VoiceCold` (caches, setup, rare types); allocate `mod`/`modR` contiguously per active voice slot. | Medium–Large (CPU on L1D/L2 misses + 2.6 MB RAM) | Low (data layout only, bit-identical) | Key 61 `perf_event_open` `L1D_CACHE_REFILL` / `L2D_CACHE_REFILL` + `tier_bench --config low --sections` on `Stretched Glass Choir`, `Twin Warp Lead`, `Diffusor Cloud Pad`. |
| 7 | 1 (Process Path / `blockDirty`) | Any single parameter tweak (e.g., Q-Link knob sweep or DAW automation) bumps `paramGen`, forcing `process()` to copy all 2,637 atomics, recompile the 64-slot `ModMatrix` (with DFS cycle checks), recompute all 128 `tunedNotes[]` via `std::log2`, invalidate all 3 FX racks, and bump `baseEpoch` invalidating every active voice's parameter caches. | `[code core/engine-own/own_engine.cpp:1350-1365,1392-1394,1473]`, `[code core/engine-own/matrix/mod_matrix.cpp:240-326]`, `[measured: docs/release/CPU-REVIEW-PROFILE.md:129]` | Split `paramGen` into domain dirty bitmasks (`matrixGen`, `tuningGen`, `fxGen[3]`, `oscGen[3]`, `envGen`, `lfoGen`, `filterGen[2]`) updated in `setParam()` by parameter-index range, plus a lock-free dirty-index ring for single-param writes (falling back to full copy only on preset load). | Medium (CPU during live Q-Link / automation) | Low (bit-identical) | `tier_bench --config low` extended with a 1-parameter-per-block Q-Link cutoff sweep on 8-voice `Twin Warp Lead` (measuring block mean and p99 before vs after). |
| 8 | 5 (SIMD / FTZ & Apple ARM64) | `SB_NEON` is forced to `0` on Apple ARM64 (leaving iOS and Apple Silicon macOS on pure scalar loops); `shell-clap` (including iOS `synthyboy_static`) and `OwnEngine::process()` have no FTZ/DAZ guard (denormal stall hazard); and `fp-contract=off` is enforced inconsistently across ARM64 TUs while blocking FMA and 2-step NR `vrecpe` gains. | `[code core/engine-own/dsp/simd.h:13-35]`, `[code shell-clap/synthyboy_clap.cpp:320-415]`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:36]` | Add a scoped FTZ/DAZ guard at the top of `OwnEngine::process()` (skipping `vmsr` when FZ is already set) so CLAP, iOS, and offline renderers match MPC; enable `SB_NEON` on Apple ARM64; evaluate a relaxed `-115 dB` parity policy to allow FMA on ARM64 / `neon-vfpv4` and 2-step NR `vrecpe`. | Medium–Large (iOS CPU + RT denormal safety + XL CPU) | Low (FTZ & iOS NEON) / Medium (FMA breaks strict bit-identity) | iOS/macOS `synthyboy_bench --engine own` scalar vs NEON; x86/ARM64 CLAP resonant-filter decay-tail CPU with host FTZ off; XL A/B `tier_bench` with FMA enabled and `--verify --tolerance 1e-5`. |
| 9 | 8 (Measurement Method) | `tier_bench` runs `SCHED_OTHER` on core 3 concurrently with the MPC host's `SCHED_FIFO 20` audio thread (+20–25% p99 inflation and 18% day-to-day drift), lacks PMU cache/branch counters, distorts fine sections with `1.3 µs` `steady_clock` scopes, and lacks per-module cost tables, Q-Link/burst gestures, thermal logging, and automated regression gates. | `[measured: docs/MPC.md:321,336,350,540]`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:344,449]`, `[code core/engine-own/dsp/section_timing.cpp:25]` | Standardise on interleaved A/B runs under `rtrun` (`SCHED_FIFO 20` on isolated core 2 or stopped host), log thermal zones, add optional `perf_event_open` counters (`CYCLES`, `INSTRUCTIONS`, `L1D_REFILL`, `L2D_REFILL`, `BR_MIS_PRED`), add per-module `--micro` (every filter/warp/FX), and add `minflt`/`majflt` + CI/device p99/VmRSS regression thresholds. | Large (engineering leverage — prevents wasted work & false regressions) | Low | Run 3 back-to-back interleaved `rtrun` benchmarks on Key 61 verifying run-to-run p99 variance `< 2%` (down from 18–25% noise). |
| 10 | 3 (Threading / Wavetable Bank) | The built-in `Wavetable` background builder thread (`Bank::run`) never calls `runHelperThreadStartHook()` (so if started from an RT/pinned thread it inherits RT scheduling and isolated audio-core affinity; `nice +10` is ignored under `SCHED_FIFO`), polls with `sleep_for(10ms)` forever (100 wakeups/s after completion), and shares one global `latest[6]` across plugin instances. | `[code core/engine-own/osc/wavetable_bank.cpp:372,472-497]` | Call `runHelperThreadStartHook()` at the top of `Bank::run()`, replace the `10 ms` polling loop with `WakeSignal` (or exit the thread once all `kBuiltInCount = 15` tables are built), and store per-table priority flags (`uint32_t wantedMask`) instead of a shared 6-slot overwrite array. | Medium (audio-core isolation + idle power/jitter) | Low | `ps -eLo tid,cls,rtprio,psr,comm` on Key 61 verifying `sb-wt-build` is `SCHED_OTHER` on cores `0-1` and exits (or 0 wakeups/s in `/proc/<tid>/sched`) after all 15 tables finish. |
| 11 | 6 (Quality Tiers / Cap Holes) | `quality.oversample_4x` (set by `serum_import.cpp:636` on imported presets) is NOT capped on tier `low`, forcing every wavetable osc onto 2× oversampling even without warp; tier `low` also drops 5 character FX completely on 31+ factory presets without offering an economy fallback (e.g., single-FDN low-cost reverb). | `[code core/engine-own/own_engine.cpp:1449]`, `[code core/src/serum_import.cpp:636]`, `[measured: docs/release/CPU-REVIEW-PROFILE.md:56]`, `[measured: docs/MPC.md:453-457]` | Ignore `quality.oversample_4x` when `EconomyFlags::kEconomyWarp` is active (`low` tier); combine with Part 1 #21 (`noiseSample` tier pass-through); evaluate a cheap 4-line stereo FDN or single-pass reverb option once memory/toolchain headroom is recovered. | Medium (CPU on imported Serum presets + preset fidelity) | Low (OS cap) / Medium (economy reverb) | `tier_bench --config low` on an imported Serum preset with `oversample_4x = 1` comparing before vs after; preset A/B loudness and listening check. |
| 12 | 1 (Process Path / Passes & RT Hazards) | `OwnEngine::process()` zeros `left`/`rightOut` then immediately overwrites them (`1324, 1339, 1736`), multiplies `voiceCountGain` and runs `runBus()` across silent buses (`1672-1732`), leaves `scratchRight.assign()` (`1336`) able to allocate on oversized mono blocks, and runs `static const int convolveOn` (`1799`, `__cxa_guard_acquire`) on the audio thread. | `[code core/engine-own/own_engine.cpp:1324,1335-1339,1672-1737,1747-1749,1799]`, `[measured: docs/release/CPU-REVIEW-PROFILE.md:215-224]` | Remove the redundant initial `std::fill` of `left`/`rightOut`; track per-period `busFedMask` to skip `voiceCountGain` and `runBus` on silent buses; fuse `mixL/R[0]` copy + `master::stage` + master peak into one pass; clamp mono `numFrames` chunks to `scratchRight.size()` in a loop (zero RT alloc); hoist `convolveOn` into `ParamIndex`. | Medium (CPU ~1–2% + RT alloc/guard elimination) | Low (bit-identical) | `tier_bench --config low --sections` (`engine rest` / `fx_chain`) on single-bus presets + mono oversized-block RT-malloc test (`numFrames = 8192`). |
| 13 | 9 (MPC Shell Boundary) | `EventInbox::push()` uses an `atomic_flag` spinlock (priority inversion risk under multi-producer hosts); `noteDiagnostics()` uses a lazy function-local static guard on the first MIDI note in `effProcessEvents`; `applyState()` writes 2,637 params one by one without batching (torn preset audio + synchronous STFT build on UI thread); `outputPeak` and `spectrumRing` run unconditionally every block. | `[code shell-mpc/event_inbox.h:22]`, `[code shell-mpc/synthyboy_mpc.cpp:1891-1925,2456,3292-3298]`, `[code shell-mpc/output_peak.h:136]` | Replace `EventInbox` spinlock with Vyukov lock-free MPSC ring (already used in `play_log.h`); pre-initialise `noteDiagnostics()` in `VSTPluginMain`; add `beginParamBatch()`/`endParamBatch()` (or double-buffered parameter snapshot) to `OwnEngine` for atomic preset swaps and offload `ContentSet::build` to a worker thread; gate `spectrumRing.push` on an `overlayActive` atomic. | Medium (RT safety + glitch-free preset loads + shell CPU) | Low–Medium | `SYNTHYBOY_MPC_CALLBACK_STATS=1` gap stats during rapid preset switching while holding an 8-note chord on Key 61; TSan check on `EventInbox` and `applyState()`. |
| 14 | 2 & 1 (Idle / Silent Voices & Readout) | Zero-amplitude sustained/released voices (e.g., `Ring Mod Gamelan` after 1.5 s decay) stay active running full oscillators and filters; `controlTick()` scans all 36 voices for `sustainPending` every 32 samples; `writeReadout()` runs `std::exp`, up to 10 `std::log10`, and 300–700 atomic stores every block even when idle or when no UI reads it. | `[code core/engine-own/own_engine.cpp:1190-1192,1901-2034]`, `[measured: docs/release/CPU-REVIEW-PROFILE.md:204-213]`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:139]` | Skip oscillator/filter rendering on voices whose amp envelope is in sustain with `ampCur == 0 && ampTarget == 0` (and auto-retire voices silent below `-96 dBFS` in release); guard the `sustainPending` loop with `sustainPendingMask`; throttle `writeReadout()` to ~30 Hz (every 10 blocks) or gate by `readoutRequested` atomic, converting dB on the UI thread. | Medium (CPU 2–5% on decaying chords + lower idle/UI overhead) | Low (bit-identical audio; UI readout stays 30–60 Hz) | `tier_bench --config low` on `Ring Mod Gamelan` (held 3 s chord) and `noteon_bench` idle block time (`0.022 ms` `[measured: docs/MPC.md:71]`). |
| 15 | 4 (Memory / Page Faults & Leaks) | Neither engine nor shell calls `mlock()`/`mlockall()` or tracks `minflt`/`majflt` in `callback_stats.h` (leaving hot code and wavetables vulnerable to reclaim under memory pressure (4 GB, no swap)); `contentInUse` is never cleared at end of `process()`, pinning one dead multi-MB `ContentSet` indefinitely; `FxPool::retire()` leaks a module if >4 rapid type switches occur before collection. | `[code core/engine-own/own_engine.cpp:202-209,1417]`, `[code core/engine-own/fx/fx_chain.cpp:113-125]`, `[code shell-mpc/callback_stats.h:20-70]` | Clear `contentInUse.store(nullptr, release)` at the end of `OwnEngine::process()` so dead `ContentSet`s reclaim immediately on the next publish (or on a periodic maintenance tick); replace `FxPool::grave[4]` with a fixed lock-free intrusive list or larger ring collected on every `fxWorker` wake; log `getrusage` `minflt`/`majflt` in `callback_stats.h` and `mlock` hot voice/audio buffers where `RLIMIT_MEMLOCK` permits. | Medium (RAM hygiene + drop-out diagnostics) | Low | Load 5 consecutive sample/IR presets on Key 61 and check `VmRSS` (verify dead `ContentSet` freed); verify `majflt == 0` during 30-min MPC soak under `SYNTHYBOY_MPC_CALLBACK_STATS=1`. |
| 16 | 1 & 5 (Alignment & `fastmath` Edge Cases) | No `alignas(16)`/`alignas(64)` exists anywhere in `core/engine-own`, yielding 2,808 unaligned vs 634 aligned `vld1/vst1` on 8-byte-aligned arm32 glibc `malloc`; `fastmath::log2` has no zero/denormal guard; `fastmath::sinCycles` overflows `int` at `|c| >= 2^31`; `std::round` still hits `libm` on ARMv7 (`voice.cpp:1370`, `fx_modules.cpp:941`). | `[code core/engine-own/dsp/fastmath.h:37-82]`, `[code core/engine-own/voice/voice.cpp:1370]`, `[code core/engine-own/fx/fx_modules.cpp:941]`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:37]` | Align scratch/bus/voice buffers to 64 bytes (`alignas(64)`) and pass `__builtin_assume_aligned(..., 16)` in `simd.h` (plus `__restrict__` on `float*` params); fix `fastmath.h` union UB (Part 1 #27), clamp `log2` input `>= 1.175494e-38f`, wrap `sinCycles` cleanly, and replace `std::round` with `ffloor(x + 0.5f)`. | Small–Medium (CPU + undefined-behaviour hardening) | Low | ARMv7 disassembly check on `simd.h` (`vld1.32 {d}, [r,:128]`), UBSan run on `fastmath.h`, and `tier_bench` A/B on Key 61. |
| 17 | 4 (Docs / Wavetable Bank Size) | `core/engine-own/README.md:13` documents the built-in wavetable bank as "20.5 MB" (21 levels at 2×), whereas the live 18-level `>= 8×` mip builder actually occupies **~33.1 MB** (`63.78 KB/frame` × 519 frames across 15 tables) when fully built. | `[code core/engine-own/README.md:13]` vs `[code core/engine-own/osc/wavetable_bank.h:20,29]`, `[code core/engine-own/osc/wavetable_bank.cpp:229-235,274-281]` | Update memory budget docs to reflect the true 33.1 MB shared bank, and consider dropping the 2,048-float `spec[]` array (~4.2 MB across 8 tables) from built-in tables that are never used as Spectral Morph targets (or rebuilding `spec[]` lazily on first morph request). | Small (4.2 MB shared RAM + accurate budgeting) | Low | Report exact `sizeof(Table)` / total `bank` allocation in `synthyboy_tier_bench --memory`. |
| 18 | 7 (Build / Bench Drift & RTTI) | Static `tier_bench` links container glibc 2.36 `libm` while `synthyboy_mpc.so` calls device glibc 2.39 `libm` via PLT; C++ exceptions and RTTI remain enabled across all DSP libraries solely for 7 `dynamic_cast<own::OwnEngine*>` helpers (`own_engine.cpp:32-92`). | `[code core/engine-own/own_engine.cpp:32-92]`, `[code core/CMakeLists.txt:59]`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:359]` | Replace the 7 `dynamic_cast` helpers with virtual methods on `IEngine` so DSP libraries can compile with `-fno-rtti` (and hot non-allocating DSP TUs with `-fno-exceptions`); link `tier_bench` dynamically against target sysroot glibc (or measure a thin harness that `dlopen`s `synthyboy_mpc.so`) so benchmark `libm`/PLT costs match the real plugin. | Small (binary size, icache, measurement fidelity) | Low | Compare `tier_bench` vs `dlopen(synthyboy_mpc.so)` on a `libm`-heavy preset (`Bode Barber Riser` / `Hall` Reverb) on Key 61; inspect `.ARM.exidx` and `.rodata` size delta. |

---

## Topic-by-Topic Deep Dive

### Topic 1: The `process()` Path

#### (a) What the code does
- **Entry, zeroing, and mono fallback (`core/engine-own/own_engine.cpp:1319-1347`):** `OwnEngine::process(events, numEvents, left, right, numFrames)` validates `left != nullptr && numFrames > 0`, immediately fills `left[0..numFrames-1]` with `0.0f` (`line 1324`), falls back to member `scratchRight` if `right == nullptr` — calling `scratchRight.assign(numFrames, 0.0f)` if `numFrames > scratchRight.size()` (`line 1335-1336`) — and fills `rightOut[0..numFrames-1]` with `0.0f` (`line 1339`). It then zeros `stripBlockPeak[16]` (`line 1340`) and decrements `meterHold` via CAS (`line 1343-1346`).
- **Block snapshot and coarse invalidation (`own_engine.cpp:1349-1455`):** Loads `paramGen` (`acquire`) and `ownContent.get()` (`acquire`). If either changed (`blockDirty == true`, `line 1352`), it copies all `base.size()` (`2,637`) relaxed parameter atomics (`lines 1355-1356`), calls `matrix.compile(base.data())` (which scans all 64 slots, resolves source/target tables, and runs a topological DFS cycle check, `[code core/engine-own/matrix/mod_matrix.cpp:240-326]`), clears `morphCache` if `contentNow` changed, and increments `baseEpoch` (`line 1364`). Further down, `blockDirty` also forces `noteTuning.compute()` over all 128 MIDI notes using `std::log2` (`own_engine.cpp:1392-1394`, `[code core/engine-own/voice/note_tuning.h:95-125]`), invalidates `inputsSame` in all three `FxChain` racks (`own_engine.cpp:1473`), and invalidates every active voice's cached envelope/LFO/RLFO/oscillator parameter blocks via `baseEpoch` (`[code core/engine-own/voice/voice.cpp:959,1051,1112,1268]`).
- **Event handling, block splitting, and the 32-sample control grid (`own_engine.cpp:1457-1765`, `dsp/util.h:21`):** `kControlPeriod = 32` is tracked continuously across host blocks via persistent member `controlPos` (`0..31`), guaranteeing block-size invariance when host buffer sizes vary. For each chunk starting at `pos`:
  - `end = std::min(numFrames, pos + (kControlPeriod - controlPos))` (`line 1581`).
  - When `eventGrid == true` (`EconomyFlags::kEconomyControl`, active on both MPC Key 61 and MPC XL `[code shell-mpc/device_profile.h:50,159]`), **all** events with `events[ei].frame < end` are delivered together at the start of the 32-sample period (`lines 1572-1575`), so the block is never split inside a 32-sample period (at most 0.726 ms timing quantisation at 44.1 kHz).
  - When `eventGrid == false` (`medium`/`high` without economy control), events at `frame == pos` are consumed, and if the next event falls strictly inside `(pos, end)`, `end` is truncated to `events[ei].frame` (`lines 1577-1583`), splitting the control period into sub-blocks `n < 32`. Note the cross-referenced split-invariance bugs already documented in Part 1 #8 (`HOST` LFO `songSeconds`), Part 1 #25 (`oscMono`/`extMono` indexing), Part 2 #9 (`voiceCountGain` updating only at `controlPos == 0`), and Part 2 #18 (`makeupOfBus` smoothing over `n`).
- **Buffer passes per 32-sample period (`own_engine.cpp:1656-1755`):**
  1. Zero all 8 mix bus buffers `mixL[0..3][0..n-1]` and `mixR[0..3][0..n-1]` (`lines 1657-1661`).
  2. Each active voice renders into stack-local planar buffers (`oscL/R[3][32]`, `tE/tN_L/R[4][32]`, `fin/fout_L/R[2][32]` — ~5.6 KB of stack `[code core/engine-own/voice/voice.cpp:1590-1599]`) and accumulates into `mixL/R[0..3]`.
  3. `voiceCountGain` builds `gRamp[0..n-1]` and scales **all 4 stereo buses** (8 buffers) unconditionally even when Bus 1, Bus 2, or Direct received zero audio (`own_engine.cpp:1672-1683`).
  4. `peakAbsPair` scans Bus 1 and Bus 2 (`lines 1687-1691`) for strip metering.
  5. `runBus(1)` and `runBus(2)` run `fxBus1`/`fxBus2`, compute `makeupOfBus`, and linearly ramp-add Bus 1/2 into Main/Bus 2/Direct (`lines 1696-1732`) even when nothing routed to those buses (`[measured: docs/release/CPU-REVIEW-PROFILE.md:215-224]` S7).
  6. `fxMain.process(mixL[0], mixR[0], n)` (`line 1734`), followed by `std::copy(mixL[0], mixL[0] + n, left + pos)` and `rightOut + pos` (`lines 1736-1737`), completely overwriting the initial zeroing from `lines 1324, 1339`.
  7. `master::stage(left + pos, rightOut + pos, mixL[3], mixR[3], ...)` reads and writes `left`/`rightOut` and adds Direct (`line 1747`), followed by `peakAbsPair` reading `left`/`rightOut` a second time (`line 1749`).
- **End-of-block work (`own_engine.cpp:1759-1805, 1901-2034`):** Calls `killExcess(0)`, `publishStats()` (which contains a function-local `static const int convolveOn = findParamIndex(...)` at `line 1799`!), and `writeReadout()` (which computes `std::exp`, up to 10 `std::log10`, and ~300–700 relaxed atomic stores every block `[measured: docs/release/CPU-REVIEW-MEMORY.md:139]`).

#### (b) Hardware / software principle
On an in-order/limited-out-of-order ARMv7 core like the Cortex-A17 (32 KB L1D, 64-byte cache lines, 8-byte `malloc` alignment under glibc), every redundant pass over `left`/`rightOut` or empty mix buses burns load/store issue slots and evicts hot voice/wavetable lines from L1D. Conversely, hoisting invariant control work onto a fixed 32-sample absolute grid amortises expensive transcendental calculations by 32× while guaranteeing deterministic audio independent of host buffer fragmentation. Any hidden heap growth (`std::vector::assign`) or C++11 thread-safe static local initialisation (`__cxa_guard_acquire`) inside `process()` introduces an unbounded mutex/syscall path on the real-time thread.

#### (c) Best practice and literature
- **Best practice followed:** Fixed control-period scheduling independent of host block size (`kControlPeriod = 32`), planar (non-interleaved) stereo buffers for contiguous NEON vector loads, and fixed-size member scratch buffers (`mixL[4][32]`) follow standard real-time DSP engine design (Bencina, *"Real-time audio programming 101"*, 2011; Pirkle, *Designing Software Synthesizer Plugins in C++*, 2nd ed., 2021).
- **Where it falls short:**
  - Coarse single-counter `paramGen` invalidation violates fine-grained dirty propagation: touching a single cutoff knob triggers full matrix recompilation and 128-note retuning, whereas a per-domain dirty bitmask + small dirty-index ring updates only the affected parameter(s) in $O(1)$.
  - Unfused output passes (`fill 0` → `copy` → `master::stage` → `peakAbsPair`) perform 4 memory passes where 1 fused pass suffices (`master::stage` can read `mixL[0]`/`mixL[3]` directly, write `left`/`rightOut`, and accumulate peak in registers).
  - Zero `alignas(16)`/`alignas(64)` alignment on `mixL`, `mixR`, `base`, or `scratchRight` forfeits 128-bit alignment hints (`[:128]`) on ARMv7 `vld1`/`vst1` (`[measured: docs/release/CPU-REVIEW-MEMORY.md:37]`: 2,808 unaligned vs 634 aligned vector loads/stores).

#### (d) Risks
- **Heap allocation on audio thread (`[code core/engine-own/own_engine.cpp:1335-1336]`):** If a mono host ever delivers `numFrames > max(maxBlockFrames, 4096)`, `scratchRight.assign()` invokes `operator new` inside `process()`.
- **Static guard lock on audio thread (`[code core/engine-own/own_engine.cpp:1799]`):** `static const int convolveOn = findParamIndex(...)` emits `__cxa_guard_acquire` / `__cxa_guard_release` (a futex/mutex guard in libstdc++) on the first audio callback.
- **Automation / Q-Link CPU spikes (`[code core/engine-own/own_engine.cpp:1353-1365,1392-1394]`):** Sweeping any Q-Link knob continuously sets `blockDirty = true` every block, paying ~10–20k cycles (`0.2–0.4%` of a Key 61 block `[measured: docs/release/CPU-REVIEW-PROFILE.md:129]`) for `base` copy + `matrix.compile` + `noteTuning.compute` PLUS invalidating every active voice's `baseEpoch` caches (`envSetOk`, `lfoSet`, `rlfoSet`, `oscPart`, `subPart`) on every single block.
- **Unnecessary idle CPU (`[measured: docs/MPC.md:71,122]`):** With zero voices sounding, the engine still spends `0.022 ms/block` (0.76% of budget) in `noteon_bench` and `63–65 µs/block` inside the MPC shell because `controlTick()` advances 10 global LFOs + 4 global chaotic RLFOs + 2 global `ModMatrix::evaluate` calls every 32 samples (`own_engine.cpp:1159-1316`) and `writeReadout()` executes transcendental dB conversions and hundreds of atomic writes every block (`own_engine.cpp:1901-2034`).

#### (e) Cost breakdown
- **Per block (128 frames = 4 control periods @ 44.1 kHz):**
  - Clean block: 1 redundant zero-fill of `left`/`rightOut` (1 KB), 4 control periods, `killExcess`, `publishStats`, `writeReadout` (300–700 atomic stores + `exp` + up to 10 `log10`).
  - Dirty block (any param change): +2,637 atomic loads (10.5 KB read), 64-slot `ModMatrix::compile` + DFS, 128× `std::log2` tuning recompute, full voice cache rebuild on all active voices.
- **Per control period (32 samples):** 8× 128 B bus zeros (1 KB), `voiceCountGain` across all 4 stereo buses (8× 128 B), 2× `peakAbsPair` on Bus 1/2, `runBus(1)`/`runBus(2)`, `fxMain`, copy to `left`/`rightOut`, `master::stage`, master `peakAbsPair`.
- **Stack footprint:** `Voice::render` stack buffers (~5.6 KB) + `OwnEngine::process` locals (~1.5 KB) ≈ ~7–8 KB stack depth (safe on Linux 8 MB / MPC RT stacks, worth keeping bounded).

#### (f) Concrete recommendations
1. **Split `blockDirty` into domain dirty bitmasks + dirty-index ring** (Rank 7): Gain: Medium (removes Q-Link/automation spikes and voice cache thrash); Risk: Low (bit-identical); Proof: `tier_bench --config low` extended with 1-param/block Q-Link cutoff sweep on 8-voice `Twin Warp Lead`.
2. **Remove initial `left`/`rightOut` zeroing, gate silent buses via `busFedMask`, fuse copy + `master::stage` + master peak, chunk mono `numFrames` to `scratchRight.size()`, and move `convolveOn` into `ParamIndex`** (Rank 12): Gain: Medium (~1–2% CPU + eliminates RT alloc and `__cxa_guard_acquire`); Risk: Low (bit-identical); Proof: `tier_bench --config low --sections` on single-bus presets + mono `numFrames = 8192` malloc-hook test.
3. **Align bus/scratch buffers to 64 bytes (`alignas(64)`)** (Rank 16): Gain: Small–Medium; Risk: Low; Proof: ARMv7 `objdump` verifying `[:128]` hints in `simd.h` and Key 61 A/B `tier_bench`.

---

### Topic 2: Voice Management as a System

#### (a) What the code does
- **Pool structure and active bitset (`own_engine.h:89,131`, `own_engine.cpp:426-431,602-614`):** `kPoolVoices = kMaxVoices + 4 = 36` `Voice` objects are individually heap-allocated via `std::make_unique<Voice>()` in `prepare()`. Round 13 introduced `uint64_t activeMask` + `forEachActive()` (`__builtin_ctzll`), eliminating full 36-slot scans in `tick()`, `render()`, `killExcess()`, `updateLowestNote()`, `publishStats()`, and `writeReadout()`.
- **Remaining linear 36-voice scans (`own_engine.cpp`):** `freeVoice()` (`622-628`), `pickVictim()` (`637`), `allocateVoice()` (`672`), `hasVoice()` (`777`), `startNote()` (`802, 824, 830, 842, 856`), `noteOn()` (`939`), `noteOff()` (`968`), `setSustain()` (`1029`), and — critically on the periodic hot path — `controlTick()`'s `sustainPending` loop (`1190-1192`), which scans all 36 `Voice` pointers every 32 samples (even when no voice has `sustainPending`).
- **Allocation, stealing, and strike path (`own_engine.cpp:635-692, 850-857, 1760`):**
  - When `voiceOverride < 0.5` (Kill mode, the default), `allocateVoice()` returns `freeVoice()` **whenever any of the 36 pool slots is free** (`lines 674-677`), without checking `polyphonyLimit()` (`8` on Key 61). Only when all 36 slots are busy does it call `pickVictim()`.
  - `startNote()` immediately executes `chosen->start(...)` (which runs placement-new `clearState()` on 3 `WtOsc` + sub, `filt[0..1].reset()` clearing 14 delay lines each, and `lib[0..2].resetForNote()`) **and** immediately calls `chosen->tick(globals, ...)` (`line 856`) — executing morph table builds and `LibOsc::beginType()` (which primes Spectral STFT frames!).
  - Excess voices beyond `polyphonyLimit()` are only killed later by `killExcess()` at the next `controlPos == 0` or at the end of the block (`line 1760`), using either a 3 ms DC-ramp `ghostKill` (`kEconomyControl`) or a 50 ms soft kill.
- **Per-voice state size and layout (`voice.h:229-397`, `docs/release/CPU-REVIEW-MEMORY.md:25,326-340`):**
  - `sizeof(Voice) = 33,128 B` on arm32 (`operator new(0x8168)` `[measured: docs/release/CPU-REVIEW-MEMORY.md:25]`), plus ~480–950 KB of per-voice heap allocations: `mod`/`modR` (21.1 KB), `WtOsc` (`morphBuf` 3×32.8 KB + `MorphScratch` 3×24.6 KB + `history` 3×8.1 KB = 196 KB), `FilterUnit` (4 `FilterImpl` delay sets ≈ 266 KB @ 44.1 kHz `[measured: docs/release/CPU-REVIEW-MEMORY.md:330]`), and `LibOsc` (3× [16 Sample + 16 Granular + 4 Spectral (~148 KB) + 64 LayerVoice] ≈ 690 KB/voice).
  - Inside `Voice`, hot fields touched every 32 samples (`active`, `ampCur`, `osc[3]`, `filt[2]`) are interleaved with cold fields (`envCache`, `lfoSet[10]` with `shapes::ShapeData`, `oscPart[3]`, `LibOsc` cold members, `FilterUnit` unused models).
- **Silent voices (`CPU-REVIEW-PROFILE.md:204-213` S6):** A held note whose amp envelope decays to `0.0` (e.g., `Ring Mod Gamelan` after 1.5 s) remains `active == true`, continuing to execute all oscillators, warps, and filters until note-off.

#### (b) Hardware / software principle
On the Cortex-A17 (32 KB 4-way L1D, 64-byte lines, small L1 DTLB), a 33 KB `Voice` struct scattered across individual `new` allocations plus ~100 child heap vectors per voice guarantees L1D capacity thrashing and DTLB misses on every voice transition within a 32-sample period (Drepper, *"What Every Programmer Should Know About Memory"*, 2007). Meanwhile, Cortex-A17's NEON execution datapath is 64-bit wide (`[measured: docs/release/CPU-REVIEW-MEMORY.md:135]`): processing 2 lanes (stereo L/R pair) already fills the 64-bit NEON pipeline without suffering the branch divergence of heterogeneous cross-voice SIMD, whereas ARM64 Cortex-A76 has dual 128-bit NEON pipes that can exploit 4-voice AoSoA (`VoiceGroup4`).

#### (c) Best practice and literature
- **Best practice followed:** `activeMask` bitset iteration, SoA unison lanes inside `WtOsc` (`prevInc[16]`, `curGainL[16]`, `phase[16]`, `[code core/engine-own/osc/wt_osc.h:1229-1233]`), and 3 ms `ghostKill` (halving chord-steal spikes `[measured: docs/MPC.md:337]`) follow sound embedded synth practice (Farnell, *Designing Sound*, 2010; Pirkle, 2021).
- **Where it falls short:**
  - **Hot/cold splitting & arena allocation:** Best practice splits `Voice` into a compact, 64-byte-aligned `VoiceHot` array (~4–6 KB/voice) and a separate `VoiceCold` struct, sharing one engine-level `MorphScratch` (as already done for `spectralScratch`).
  - **AoS vs SoA / cross-voice SIMD:** On Cortex-A17 (64-bit NEON), stereo-pair (2-lane) SIMD inside each voice (fixing Part 1 #26 for `Normal`/`AcidLadder`/`MGDirty` filters) gives nearly full hardware utilisation without cross-voice divergence. On ARM64 Cortex-A76 (128-bit NEON), grouping 4 active voices into AoSoA lanes (`VoiceGroup4`) for identical osc/filter topologies would give up to 2× further throughput on voice filters and plain wavetables, but carries high architectural complexity and should only be considered after fixing Parts 1–2 algorithmic costs.
  - **Burst limiting:** Best practice caps per-block note-on admissions to `polyphonyLimit()` *before* running expensive voice initialisation (`start()`/`tick()`/`beginType()`), and spreads heavy FFT/STFT priming over multiple blocks when a full chord lands simultaneously.

#### (d) Risks
- **Unbounded 16-note strike burst (`[code core/engine-own/own_engine.cpp:674-677,852-857]`):** If 16 MIDI Note-Ons arrive in one 128-frame block (e.g., dense MIDI clip or sustain chord), `allocateVoice()` hands out 16 slots from the 36-voice pool, runs `start()` + `tick()` + morph/Spectral priming on **all 16** (on Key 61 where `maxVoices = 8`!), renders all 16 for the first control period if arriving mid-block, and only then ghost-kills 8 of them. On an 8-note Spectral chord, Key 61 ALREADY overruns **2.6–3.1 blocks** (`8.9 ms` `[measured: docs/MPC.md:353-354]`, and MPC XL hits `1.19` blocks on `Spectral Vowel Freeze`); a 16-note burst would stall for **>15 ms** (5+ dropped audio blocks!).
- **Preset change while notes sound (`[code shell-mpc/synthyboy_mpc.cpp:1891-1925]`):** Changing preset writes 2,637 parameters individually while voices are sounding, potentially switching oscillator types from Wavetable to Spectral mid-block, triggering simultaneous `beginType()` priming across all sounding voices alongside torn half-A/half-B parameter frames.
- **Cold-cache 3× strike penalty (`[measured: docs/MPC.md:354]`):** Because 36 voices + 14 filter lines/filter + `LibOsc` arrays scatter over ~43 MB, waking cold voice slots on a chord strike triggers DRAM misses (`5.4 ms` cold vs `1.6–1.74 ms` warm on Spectral).

#### (e) Cost breakdown
- **Note-on ("strike block") measured on Key 61 (`[measured: docs/MPC.md:97,353-354]`):**
  - 1 note (default saw): `0.122 ms` mean / `0.371 ms` worst (12.8% of 2.902 ms).
  - 8-note chord (default saw): `0.715 ms` mean / `1.018 ms` worst (35.1% of budget).
  - 8-note Spectral chord (`Spectral Vowel Freeze` / `Spectral Dark Lead`): **2.6–3.1 blocks** (`7.5–8.9 ms` = 257–307% of budget!).
- **Working set vs caches:**
  - Active per-voice period working set: **~30–50 KB** (`Voice` hot portion 8–15 KB + `mod` 10.5 KB + wavetable/morph reads 6–20 KB + filter states 2–4 KB), **exceeding Key 61's 32 KB L1D on every voice** (`[measured: docs/release/CPU-REVIEW-MEMORY.md:336]`).
  - 8 active voices: **250–450 KB**, fitting in Key 61's 1 MB L2 *unless* wavetable frame sweeps (3.5 MB frame-major stride) or Spectral analysis (4.35 MB) evict L2 into DRAM.
  - On MPC XL (Cortex-A76: 64 KB L1D / 512 KB private L2 / 3 MB L3): 1 voice fits inside 64 KB L1D once `VoiceHot` is compacted, and 16 voices fit inside 3 MB L3.

#### (f) Concrete recommendations
1. **Enforce `polyphonyLimit()` eagerly in `noteOn()`/`startNote()` and stagger Spectral priming (max 2–4 voices/block on tier `low`)** (Rank 5): Gain: Large (eliminates multi-block drop-outs on 8–16-note chord strikes); Risk: Low–Medium; Proof: `synthyboy_noteon_bench` 8-note and 16-note burst on Key 61 (`Spectral Vowel Freeze`, `Twin Warp Lead`, target `< 2.90 ms`).
2. **Move `MorphScratch` to `TickGlobals` (`MEM1`) and split `Voice` into contiguous 64-byte-aligned `VoiceHot` + `VoiceCold`** (Rank 6): Gain: Medium–Large (L1D/L2 refill reduction + 2.66 MB RAM); Risk: Low (bit-identical); Proof: Key 61 `perf_event_open` `L1D_CACHE_REFILL` + `tier_bench --config low --sections` on `Stretched Glass Choir` and `Twin Warp Lead`.
3. **Skip rendering zero-amplitude sustained voices (`S6`) and replace `sustainPending` 36-voice scan with a bitmask** (Rank 14): Gain: Medium (2–5% CPU on decaying held chords); Risk: Low; Proof: `tier_bench --config low` on `Ring Mod Gamelan` held 3 s.

---

### Topic 3: Threading and Hand-Over

#### (a) What the code does (complete inventory of every cross-thread hand-over)
| Path | Writer → Reader | Mechanism & Location | RT Safety Verdict |
|---|---|---|---|
| Parameters (`params[4096]`, `paramGen`) | Host/UI/RT → Audio | Relaxed `atomic<float>` + `paramGen` release/acquire (`own_engine.h:116`, `own_engine.cpp:254-288,1350-1365`) | Lock-free; **not transactional** across 2,637 params on preset load; `setParam()` calls `fxWorker.wake()` (`line 278`). |
| `ContentSet` (user WT, samples, IRs, SFZ) | UI/Host → Audio | Dekker seq_cst hazard pointer (`contentCurrent` + `contentInUse`, `own_engine.cpp:196-246,1411-1420`) | Audio side lock-free & UAF-safe; **`contentInUse` never cleared at block end**, pinning 1 dead set until a subsequent publish. |
| `ContentHub` / `OwnContent` (LFO shapes, mod/vel/note curves) | UI/Host → Audio | Fixed 8-slot `ring[8]` + atomic raw pointer (`own_content.h:56-155`, `own_engine.cpp:230,1351`) | **DATA RACE** (called outside `contentMutex`) + **USE-AFTER-FREE** (overwrites `ring[slot]` after 8 updates without checking audio thread!). |
| `NoteTuning` (`userNote[128]`) | UI/Host → Audio | Relaxed `atomic<double>` array + `userPresent` (`note_tuning.h:62-125`) | Lock-free; **torn reads** if a second user tuning loads while `userPresent == true`. |
| Built-in `Wavetable` Bank (`g_bank`) | Bg thread → Audio | `atomic<const Table*> ready[15]` acquire/release (`wavetable_bank.cpp:367-532`); immortal pointers | Audio side lock-free & safe; **bg thread misses `runHelperThreadStartHook()`**, polls every `10 ms` forever, shares global `latest[6]` across instances. |
| `FxPool` (`sb-fx-build`) | `setParam()` → Bg thread → Audio | `WakeSignal` + `Slot::pending` atomic + `grave[4]` graveyard (`fx_chain.h:43-87`, `fx_chain.cpp:67-163`) | Audio side lock-free; **`grave[4]` leaks** on >4 rapid switches; **`WakeSignal::notify()` locks mutex on non-Linux & calls `SYS_futex` on Linux**. |
| `FxWorker` (`sb-fx-job`, Convolve IR rebuild) | Audio → Bg thread → Audio | SPSC ring + `WakeSignal::notify()` (`fx_modules.cpp:25-70,820,986-1021`) | **RT VIOLATION:** `WakeSignal::notify()` called from `process()` (mutex on Apple/Win, futex syscall on Linux); **`shared_ptr<Impulse>` can `delete` on audio thread** if ring full; **data race on `live.tone`** (Part 2 #4). |
| Live Readout (`LiveValues`) & Strip Peaks | Audio → UI/Overlay | ~300+ relaxed atomics (`own_engine.cpp:1839-2034`) | Lock-free; torn cross-field snapshots (benign for meters, slight glitch for grain dots). |
| `EventInbox` (MIDI events) | Host → Audio | `atomic_flag` spinlock on producer + lock-free consumer (`shell-mpc/event_inbox.h:17-52`) | Consumer lock-free; **producer spinlock risks priority inversion** if multi-threaded host calls `effProcessEvents`. |
| Overlay `SharedBlock` | Overlay thread → GPU helper | `memfd_create` + `MAP_SHARED` + atomic seqlock (`shell-mpc/overlay/gpu/scene.h:642-724`, `helper_transport_linux.h:181-201`) | **Completely isolated from audio thread** (runs on non-audio cores `0-1`, `cpu_plan_linux.h`). Sound design. |
| `SpectrumRing` & `OutputPeak` | Audio → Overlay | 8192 atomic floats + relaxed peak atomics (`shell-mpc/spectrum.h`, `output_peak.h`) | Lock-free; `noteDiagnostics()` has lazy static guard (`output_peak.h:136`). |

#### (b) Hardware / software principle
On a real-time audio thread scheduled under `SCHED_FIFO` (MPC Linux) or CoreAudio Mach real-time constraints (macOS/iOS), any lock acquisition, `malloc`/`free`, or blocking kernel transition can invert priority against a lower-priority thread holding the lock or allocator mutex, causing unbounded latency beyond the 2.902 ms block deadline (Bencina, 2011; Sha, Rajkumar & Lehoczky, *"Priority Inheritance Protocols"*, IEEE Trans. Computers, 1990). Lock-free memory reclamation requires explicit epoch or hazard-pointer tracking (Michael, *"Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects"*, IEEE TPDS, 2004) — a fixed circular overwrite buffer (`kRing = 8`) is NOT safe reclamation because real-time preemption or rapid UI gestures can lap the ring arbitrarily fast.

#### (c) Best practice and literature
- **Best practice followed:** `ContentSet` uses a textbook Dekker-fenced single-slot hazard pointer (Michael, 2004), `Overlay::SharedBlock` uses a clean word-wise atomic seqlock (Boehm, *"Can Seqlocks Get Along with Programming Language Memory Models?"*, MSPC 2012), and `sb-fx-build`/`sb-fx-job` call `runHelperThreadStartHook()` to drop RT priority and pin to non-audio cores `0-1` (`[code shell-mpc/thread_policy.h]`).
- **Where it falls short:**
  1. `ContentHub` (`own_content.h:103-105`) relies on the comment *"a block never lasts that long"* for its 8-slot overwrite ring — a classic concurrency bug whenever UI drags (e.g., dragging an LFO breakpoint at 60 Hz while audio is paused/preempted or during a burst) call `setContent()` 8 times before the audio thread finishes reading `contentNow` or `Voice::lfoSet[l].custom`.
  2. `WakeSignal::notify()` (`helper_thread.h:36-47`) takes `std::mutex` on macOS/iOS/Windows and invokes `SYS_futex` on Linux directly from `OwnEngine::process()` (`ConvolveModule::startBuild` → `FxWorker::submit`) and `OwnEngine::setParam()`. True RT-safe notification avoids both mutexes and syscalls on the audio thread (either atomic-flag polling at 1–5 ms on the low-priority worker thread, or a dedicated non-RT watcher).
  3. `Bank::run()` (`wavetable_bank.cpp:472-497`) missed the Round 13 T1 thread-policy fix: `nice(+10)` is a no-op under `SCHED_FIFO`, so if the host invokes `prepare()` from an RT thread, the wavetable builder inherits `SCHED_FIFO` and isolated audio-core affinity!

#### (d) Risks
- **Use-after-free crash (`[code core/engine-own/voice/own_content.h:103-105]`):** Dragging an LFO point or switching presets rapidly 8+ times while the audio thread holds `contentNow` / `globals.lfoContent` frees live `OwnContent` under the audio thread.
- **Audio drop-out / priority inversion (`[code core/engine-own/dsp/helper_thread.h:43,71]`):** Automating `convolve.size` or `fxi01.type` triggers `std::mutex` on macOS/iOS/Windows audio threads and `SYS_futex` kernel entry on MPC Linux audio threads; if `FxWorker::submit` returns `false`, overwriting `job.impulse` can run `free()` on the audio thread (`[code core/engine-own/fx/fx_modules.cpp:820,989]`).
- **Audio-core pollution (`[code core/engine-own/osc/wavetable_bank.cpp:472-497]`):** `sb-wt-build` can run on isolated audio cores `2-3` under `SCHED_FIFO` and wakes up 100 times/second forever after finishing all 15 tables.
- **Reachable audio-thread hazards summary (every site found in `OwnEngine::process`):**
  1. `scratchRight.assign(...)` (`own_engine.cpp:1336`) — heap allocation if mono `numFrames > 4096`.
  2. `static const int convolveOn` (`own_engine.cpp:1799`) — `__cxa_guard_acquire` lock on block 1.
  3. `WakeSignal::notify()` (`helper_thread.h:43,71`) via `FxWorker::submit()` (`fx_modules.cpp:52`) and `setParam()` (`own_engine.cpp:278`) — mutex on non-Linux, `SYS_futex` on Linux.
  4. `job.impulse = f.content->impulse` (`fx_modules.cpp:820,989`) — `shared_ptr` refcount drop / `free()` on audio thread if queue full + content replaced.

#### (e) Cost breakdown
- `ContentSet` hazard pointer adds 2 `seq_cst` operations (`dmb ish` on ARM) once per 128-frame block — negligible (~20–40 ns).
- `WakeSignal` `SYS_futex` syscall costs ~1–5 µs uncontended and unbounded under kernel lock contention; `Bank::run` 10 ms polling causes 100 timer interrupts/sec per process indefinitely.
- `ContentHub`'s `ring[8]` wastes `8 × ~540 KB = 4.3 MB` per engine instance for mostly empty `modCurve[64]` tables.

#### (f) Concrete recommendations
1. **Merge `OwnContent` into `ContentSet` under the existing Dekker hazard pointer and store `modCurve` sparsely** (Rank 2): Gain: Large (eliminates UAF + data race + ~4 MB RAM); Risk: Low; Proof: TSan 2-writer `setContent()` stress test under live `process()`.
2. **Eliminate audio-thread `WakeSignal` mutex/futex and `ConvolveModule` `shared_ptr` drops** (Rank 3): Gain: Large (RT safety on all platforms); Risk: Low–Medium; Proof: `strace -e futex` (Linux) and `malloc`/mutex hook (macOS) under rapid `convolve.size` and `fxi01.type` automation.
3. **Add `runHelperThreadStartHook()` to `Bank::run()`, exit or sleep on `WakeSignal` once all 15 tables finish, and use an atomic bitmask `wantedMask`** (Rank 10): Gain: Medium (audio-core isolation + 0 idle wakeups); Risk: Low; Proof: `ps -eLo tid,cls,rtprio,psr,comm` on Key 61.

---

### Topic 4: Memory & Cache Working Set

#### (a) What the code does (exact working-set inventory)
- **Process-wide shared built-in wavetable bank (`osc/wavetable_bank.h:20,29`, `wavetable_bank.cpp:229-235,274-281`):**
  - **Documentation mismatch:** `core/engine-own/README.md:13` claims "20.5 MB" (21 levels at 2×), but the code builds **18 levels at `>= 8×` highest harmonic (min 256 samples)**: `13,896` time floats + `2,048` spectrum floats = `63.78 KB/frame`.
  - 8 multi-frame tables (64 frames each) = **32.65 MB** + 7 single-cycle tables = **0.45 MB** → **~33.1 MB total** when all 15 are built. Shared across plugin instances; built lazily when `Quality::lazyTables = true` (`own_engine.cpp:397-406`, `wavetables 8.7 ms` at startup `[measured: docs/MPC.md:327]`).
- **Per-instance `prepare()` allocations (`own_engine.cpp:426-465`) — IDENTICAL across all quality tiers!**
  - **Voice pool (`36` voices unconditionally, even on Key 61 where `maxVoices = 8`):**
    - `36 × Voice` (`sizeof = 33,128 B` `[measured: docs/release/CPU-REVIEW-MEMORY.md:25]`): **1.19 MB**
    - `36 × (mod + modR)` (`2 × 2,637 × 4 B`): **0.76 MB**
    - `36 × 3 × WtOsc` (`morphBuf` 32.8 KB + `MorphScratch` 24.6 KB + `history` 8.1 KB): **7.07 MB** (2.66 MB redundant `MorphScratch`)
    - `36 × 2 × FilterUnit` (4 `FilterImpl` × 14 delay lines ≈ 266 KB/voice @ 44.1 kHz `[measured: docs/release/CPU-REVIEW-MEMORY.md:330]`): **~9.6 MB @ 44.1 kHz** (~18–25 MB @ 96 kHz `[measured: docs/QUALITY.md:127]`)
    - `36 × 3 × LibOsc` (16 Sample + 16 Granular + 4 Spectral [~148 KB] + 64 LayerVoice + rings): **~25 MB** (`[code core/engine-own/README.md:45]`)
    - **Voice pool subtotal:** **~43.6 MB @ 44.1 kHz** (`prepare_voices 95.6 ms` `[measured: docs/MPC.md:327]`) — **78% (28 of 36 voices) is unreachable on Key 61!**
  - **FX racks (`3 × FxChain`, all 13 modules prepared even when masked by `fxMask`):**
    - `3 × Reverb` (6 algorithms simultaneously, ~4.7 MB each, Part 2 #5): **~14.1 MB** (**100% masked on Key 61!**)
    - `3 × Delay` (~2.0 MB each): **~6.0 MB**
    - `3 × Bode` (~0.87 MB each): **~2.6 MB** (**100% masked on Key 61!**)
    - `3 × Flanger` (512 KB each, Part 2 #11): **~1.5 MB** (**100% masked on Key 61!**)
    - `3 × Filter` (~1.1 MB), `3 × Chorus` (~0.77 MB), `3 × HyperDim` (~0.53 MB, **masked**), `3 × Comp/EQ/Util/Splitters` (~0.5 MB): **~2.9 MB**
    - **FX subtotal:** **~27.1 MB** (`prepare_fx 73.6 ms` `[measured: docs/MPC.md:327]`).
  - **`ContentHub` ring:** up to **4.3 MB** (`8 × ~540 KB`).
  - **Total fixed per-instance footprint @ 44.1 kHz:** **~70–75 MB** (excluding shared 33.1 MB bank and user `ContentSet`).
- **Lazy allocations:** `Wavetable` bank tables (background thread), `FxPool` extra slots (`sb-fx-build`), `Convolve` partitions (up to **33.5 MB** for a 4 s IR, Part 2 #4), and `ContentSet` (user samples + 4.35 MB spectral analysis per sample + SFZ multisamples + IRs, plus 1 retired copy pinned by `contentInUse`).
- **Page locking / pre-touch:** `grep` across the entire non-forbidden codebase confirms **zero** calls to `mlock`, `mlockall`, `madvise`, or `getrusage` page-fault counters. `std::vector::assign(N, 0.0f)` touches pages during `prepare()`, preventing initial demand-zero faults on `prepare()` buffers, but provides no protection against later page reclaim.

#### (b) Hardware / software principle
On the 4 GB MPC Key 61 (no swap), the MPC host application already consumes `~946 MB VmRSS` (`[measured: docs/MPC.md:148,159]`) and knob filmstrips take `~422 MB` (`[measured: docs/MPC.md:117]`), leaving very tight headroom before the Linux kernel starts reclaiming clean file-backed pages (plugin `.so` text, wavetables) or triggering OOM/swap stalls. Every minor/major page fault on a `SCHED_FIFO` audio thread enters the kernel page-fault handler and can stall for hundreds of microseconds to tens of milliseconds. Furthermore, allocating ~3,500+ small `std::vector`s per instance scatters hot DSP state across hundreds of 4 KB pages, thrashing the Cortex-A17's small L1 DTLB alongside its 32 KB L1D / 1 MB L2 cache hierarchy (ARM Cortex-A17 TRM; Drepper, 2007).

#### (c) Best practice and literature
- **Best practice followed:** Pre-allocating voice and FX buffers in `prepare()` so `process()` does not allocate, and sharing the read-only built-in wavetable bank process-wide across instances, are standard real-time audio practices.
- **Where it falls short:**
  - Embedded targets with fixed memory ceilings must scale pool sizes to the active tier (`maxVoices + 4`, active `fxMask`, and active `sampleRate`), and pool mutually exclusive sub-engines (`Reverb`'s 6 algorithms, `FilterImpl`'s delay models, `LibOsc`'s 4 osc types) from a shared per-engine arena rather than eagerly allocating the Cartesian product (`36 voices × 3 oscs × 4 engines` and `3 racks × 6 reverbs`).
  - On embedded Linux RT audio appliances, critical hot audio arenas should be contiguous (minimising TLB entries) and locked (`mlock`) where `RLIMIT_MEMLOCK` permits, with `minflt`/`majflt` monitored in release telemetry (Bencina, 2011; Gregg, *Systems Performance*, 2nd ed., 2020).

#### (d) Risks
- **Multi-instance OOM / page reclaim on Key 61 (`[measured: docs/MPC.md:117,148,159]`):** 4 instances of SynthyBoy consume `4 × 75 MB = 300 MB` + `33 MB` shared bank + user samples/IRs — nearly **350–450 MB** on top of the host's ~946 MB (+ 422 MB filmstrips), pushing a 4 GB device straight into kswapd reclaim and audio drop-outs.
- **Cold-cache DRAM stalls (`[measured: docs/MPC.md:354]`):** 43.6 MB voice pool + 33.1 MB frame-major wavetables + 4.35 MB spectral analysis easily evict the 1 MB shared L2, directly producing the measured **3× cold-cache strike penalty** (`5.4 ms` vs `1.6–1.74 ms`).
- **Retired `ContentSet` memory pin (`[code core/engine-own/own_engine.cpp:202-209,1417]`):** Because `contentInUse` is never cleared when `process()` exits, loading a 50 MB sample/IR preset and then switching to a tiny preset keeps the 50 MB `ContentSet` alive until a *third* `publishContent()` runs.
- **`FxPool::retire()` leak (`[code core/engine-own/fx/fx_chain.cpp:113-125]`):** More than 4 rapid pool type switches before `typeChanged()` drains `grave[4]` drops the pointer and permanently leaks up to 4.7 MB per Reverb.

#### (e) Cost breakdown (cache vs working set)
| Buffer / Pool | Key 61 Current (tier `low`) | Key 61 Target (tier-scaled + pooled) | Fits L1D (32 KB A17 / 64 KB A76)? | Fits L2/L3 (1 MB A17 / 3 MB A76)? |
|---|---:|---:|---|---|
| Shared built-in wavetable bank | 33.1 MB | 28.9 MB (lazy `spec[]`) | No (1 active mip 1–8 KB fits L1D) | Only active frames fit L2 |
| Voice pool (36 vs 12 voices) | ~43.6 MB | ~8–10 MB | Hot 1 voice: 30–50 KB (spills A17 L1D) → target `< 16 KB` | 8 voices: 250–450 KB (fits L2 warm) |
| FX racks (3× all 13 vs unmasked + pooled) | ~27.1 MB | ~6–8 MB | No (active Chorus 256 KB / Delay 2 MB) | Chorus fits L2; Delay 2 MB spills A17 L2 |
| `ContentHub` ring (8× full vs sparse) | ~4.3 MB | `< 0.1 MB` | N/A | Fits L2 |
| **Total per instance (excl. shared bank)** | **~75 MB** | **~15–18 MB** | — | **~75% RAM reduction** |

#### (f) Concrete recommendations
1. **Scale voice pool to `maxVoices + 4`, skip masked FX buffers on tier `low`, and pool `LibOsc`/`FilterImpl`/`Reverb` arenas** (Rank 1, synergising with Part 1 #10 & Part 2 #5): Gain: Large (~70% RAM cut: 75 MB → ~18 MB/instance; `engine_prepare` cut from 186.8 ms to <70 ms); Risk: Medium; Proof: Key 61 `VmRSS` (1 and 4 instances), `startup_timing` (`prepare_voices`/`prepare_fx`), and `tier_bench` strike-block latency.
2. **Clear `contentInUse` at end of `process()`, fix `FxPool::grave[4]` leak, and track `minflt`/`majflt` in `callback_stats.h` (plus optional `mlock` for hot voice arena)** (Rank 15): Gain: Medium; Risk: Low; Proof: 5-preset sample/IR switch `VmRSS` check + 30-min MPC soak verifying `majflt == 0`.
3. **Update `core/engine-own/README.md:13` from 20.5 MB to 33.1 MB and build `spec[]` lazily for morphing** (Rank 17): Gain: Small (4.2 MB shared RAM); Risk: Low; Proof: `synthyboy_tier_bench --memory`.

---

### Topic 5: SIMD and Numerics Policy

#### (a) What the code does
- **Bit-exact scalar = NEON contract (`core/engine-own/dsp/simd.h:1-35`, `core/filters-serum/src/filter.cpp:5-11`, `docs/MPC.md:24`):**
  - On ARM32 and Linux ARM64 (`SB_ARM_LINUX_OR_32 == 1`, `simd.h:13`), `simd.h` emits `#pragma GCC optimize("fp-contract=off")` (`line 34`; repeated in `filter.cpp:10`, `sample_voice.cpp:23`, `granular.cpp:23`, `spectral.cpp:18`, `kernels.cpp:21`) and manually writes separate `vmulq_f32` + `vaddq_f32` (no `vfma`/`vmla` fused ops, no `vrecpe`/`vrsqrte` reciprocal estimates, exact scalar accumulation order such as `0.0f + a*g` in `firstScaled`) so NEON matches scalar bit-for-bit (`--verify` in `tier_bench`).
  - **Apple ARM64 omission (`simd.h:13, 23-25`):** On macOS and iOS (`__aarch64__ && !__linux__`), `SB_ARM_LINUX_OR_32` is `0`, which forces `SB_NEON = 0` — disabling all NEON intrinsics in `simd.h`, `wt_osc.h`, `fx_effects.h`, and `core/filters-serum` on iOS and Apple Silicon!
  - **Inconsistent `fp-contract=off` on ARM64:** Translation units that do not include `simd.h` or the pragma (`mod_matrix.cpp`, `fx_makeup.cpp`, `fx-serum-a` `Utility`/`HyperDimension`, `fx_chain.cpp`) DO emit `fmadd` (`[measured: docs/release/CPU-REVIEW-MEMORY.md:36]`: 53 `fmadd` in `Utility`, 54 in `HyperDimension`), so ARM64 is already not globally unfused.
- **FTZ / DAZ settings:**
  - `shell-mpc/synthyboy_mpc.cpp:191-243, 3217-3218`: saves, enables, and restores `FPSCR.FZ` (arm32), `FPCR.FZ` (arm64), or `MXCSR` FTZ+DAZ (x86) on every block — issuing 3 serialising `vmrs`/`vmsr` instructions unconditionally even when FZ is already set.
  - `shell-juce/PluginProcessor.cpp:346`: uses `juce::ScopedNoDenormals`.
  - **`shell-clap/synthyboy_clap.cpp` (CLAP + iOS `synthyboy_static`), `OwnEngine::process()`, and `synthyboy_preset_render`:** **Zero FTZ/DAZ guard!** (`[measured: docs/release/CPU-REVIEW-MEMORY.md:318]`).
- **Float vs double:** Steady-state per-sample kernels are float32 (`__fixdfdi`/`__floatdidf` eliminated in Round 10 `[measured: docs/release/CPU-REVIEW-MEMORY.md:34]`), while double persists in control-rate `envelope.h` divisions, `voice.cpp` pitch/rate divides, `OwnEngine::makeupOfBus` (3 double MACs/sample), `note_tuning.h` (`std::log2`), and `multisample` envelopes.
- **`fastmath.h` helpers (`core/engine-own/dsp/fastmath.h:15-82`):**
  - `tanh`: `[7/6]` Padé, `< 1e-4` abs error (`lines 15-24`), scalar division.
  - `exp2`: `< 3e-9` rel error (`lines 26-40`), uses UB `union { uint32_t u; float v; }` (`line 37`).
  - `log2`: `< 6e-8` abs error (`lines 41-51`), uses UB `union` (`line 44`) and has **no guard for `x <= 0` or subnormals** (returns garbage on `0.0f` or denormal input).
  - `sinCycles`: 1024-entry 4 KB LUT + linear interp, `< 5e-6` abs error (`lines 60-82`), casts `static_cast<int>(c)` (signed overflow UB if `|c| >= 2^31`).
- **Documented ARMv7 pitfalls:**
  - GCC on ARMv7 never emits `vcgt/vcge/vclt/vcle/vceq.f32` without `-funsafe-math-optimizations` because ARMv7 NEON always flushes subnormals (`[code core/engine-own/dsp/neon_cmp.h:5-8]`), causing 4× scalar VFP compares + `vmrs APSR_nzcv` + stack spills (`0` NEON float compares in Round 12 binary `[measured: docs/release/CPU-REVIEW-MEMORY.md:21,35]`). Round 13 N1 fixed `sample-osc` and `granular`, but **8 sites remain** in `core/spectral/src/kernels.cpp` (`267, 298, 310-313, 540, 677, 716`) and `wt_osc.h` (`975, 982`) (Part 1 #11).
  - `floorf`/`roundf` have no ARMv7 hardware instruction (`vrint` requires ARMv8) and call `libm`; `ffloor` in `util.h:49-79` fixed `floorf`, but `std::round` still hits `libm` in `voice.cpp:1370` and `fx_modules.cpp:941-945`.
  - GCC 12 emits `-Waggressive-loop-optimizations` on `simd::clampTo` (`simd.h:173`) on arm32 (`[measured: docs/MPC.md:347]`).

#### (b) Hardware / software principle
IEEE-754 floating-point contraction (`a*b + c` fused into `fmadd`/`vfma`) eliminates one intermediate rounding, improving accuracy and halving multiply-add latency/issue count (1 instruction vs 2 on ARM64 Cortex-A76 and ARMv7 `neon-vfpv4`), at the cost of a 0.5 ULP (~`-144 dB`) difference versus unfused scalar code (Goldberg, *"What Every Computer Scientist Should Know About Floating-Point Arithmetic"*, 1991). On ARMv7-A, NEON unconditionally flushes subnormals to zero whereas VFP obeys `FPSCR.FZ`; without `FPSCR.FZ = 1` on x86/ARMv7-VFP/ARM64, decaying IIR feedback tails enter subnormal microcode traps costing 10–100× per instruction.

#### (c) Best practice and literature
- **Best practice followed:** Explicit `neon_cmp.h` IEEE-754 bit-pattern comparisons for ARMv7 NEON (tested by `synthyboy_test_neon_cmp`), integer-bit `ffloor` avoiding `libm`, and per-block `FPSCR.FZ` in `shell-mpc` show deep ARMv7-specific craftsmanship (ARMv7-A/R Architecture Reference Manual; ARM NEON Programmer's Guide).
- **Where it falls short:**
  - Enforcing strict 0-ULP bit-exactness (`fp-contract=off`, banning FMA and 2-step Newton-Raphson `vrecpeq_f32` which achieves full 23-bit IEEE precision in ~6 cycles across 4 lanes vs 4× 14-cycle scalar `vdiv`) trades away real arithmetic headroom for test convenience, when a `-115 dB` (`1e-5`) parity test (`--tolerance`) catches every genuine DSP bug while permitting FMA and NR reciprocals.
  - Disabling `SB_NEON` on Apple ARM64 leaves iOS (a primary battery- and thermal-constrained target) running scalar C++.
  - Putting FTZ/DAZ only in `shell-mpc` and `shell-juce` leaves `shell-clap`, iOS `synthyboy_static`, and offline renderers exposed to denormal stalls and subnormal divergence.

#### (d) Risks
- **Denormal CPU spikes on CLAP / iOS (`[code shell-clap/synthyboy_clap.cpp:320-415]`):** `[hypothesis: rendering a decaying resonant filter or phaser tail in shell-clap on x86_64 or arm64 with host FTZ off will show >2× tail CPU vs FTZ on]`.
- **iOS battery/CPU penalty (`[code core/engine-own/dsp/simd.h:13,23-25]`):** `[hypothesis: enabling SB_NEON on Apple arm64 will cut WtOsc, FilterUnit, and Chorus CPU by 25–45% on iOS devices, measurable via synthyboy_bench --engine own]`.
- **Undefined behaviour in `fastmath.h` (`[code core/engine-own/dsp/fastmath.h:37,44,69]`):** Strict-aliasing union punning + unguarded `log2(0)` + `int` overflow in `sinCycles`.

#### (e) Cost breakdown
- `fp-contract=off` + banning NR `vrecpe`: `[hypothesis: lifting fp-contract=off on ARM64 XL and adding 2-step NR vrecpe in division-heavy loops (ClassicFilter/EQ/Delay) will recover 5–12% CPU on XL and 3–8% on Key 61 neon-vfpv4, verifiable with A/B tier_bench --verify --tolerance 1e-5]`.
- `vmrs`/`vmsr` FTZ save/restore in `shell-mpc`: 3 serialising system-register instructions every block even when `FPSCR.FZ` is already `1` (checking `if (!(saved & kFzBit))` avoids both `vmsr` writes on hosts that already enable FZ).
- `fastmath::sinCycles` LUT: 4 KB (12.5% of Key 61's 32 KB L1D); a 5th-order minimax odd polynomial (`~5e-7` error, 3 FMAs, zero L1D footprint, 4-wide NEON-vectorisable) is both more accurate and L1D-free.

#### (f) Concrete recommendations
1. **Add a branch-guarded FTZ/DAZ RAII scope to `OwnEngine::process()`, enable `SB_NEON` on Apple ARM64, and evaluate `-115 dB` parity tolerance for FMA + 2-step NR `vrecpe`** (Rank 8, extending Part 1 #27): Gain: Medium–Large; Risk: Low (FTZ/iOS NEON) / Medium (FMA tolerance); Proof: iOS `synthyboy_bench`, CLAP decay-tail test with FTZ off, XL A/B `tier_bench --verify --tolerance 1e-5`.
2. **Fix `fastmath.h` union UB (`std::memcpy`), guard `log2(x)` for `x <= 0`, fix `sinCycles` range (or replace 4 KB LUT with minimax polynomial), and replace `std::round` with `ffloor(x + 0.5f)`** (Rank 16): Gain: Small–Medium; Risk: Low; Proof: UBSan + `fastmath` accuracy test + ARMv7 `objdump`.

---

### Topic 6: Quality Tiers

#### (a) What the code does
- **Tier definitions (`core/include/synthyboy/quality.h:63-117`, `shell-mpc/device_profile.h:39-195`):**

| Knob | `low` (MPC Key 61 actual `Profile{}`) | `medium` | `high` (MPC XL actual `Profile{}`) |
|---|---|---|---|
| `maxVoices` | 8 | 16 | 32 (**16** on MPC XL) |
| `maxUnison` | 4 in `quality.h:112`, overridden to **3** on Key 61 (`device_profile.h:42`) | 8 | 16 (16 on XL) |
| `oversampling` / `interpolation` | `0` (Sample-osc 4-tap nearest mip) | `1` (8-tap) | `2` (16-tap + mip blend) |
| `fxMask` | Drops Reverb, Flanger, Hyper/Dim, Bode, Convolve (unless `reverb.force_enable`) | All 13 FX | All 13 FX |
| `fxMakeup` | ON (5-band K-weighted `ProgramMeter` + `makeupOfBus`, 0.3–0.65% CPU `[measured: docs/release/CPU-REVIEW-PROFILE.md:48]`) | OFF | OFF |
| `EconomyFlags` | `Warp` (2×/32-tap) + `Morph` (512-smp / real IFFT) + `Math` (`fastmath`, 4-smp Serum filter hold, 8-smp classic filter interp, 4-smp Chorus / 8-smp Phaser hold, 3-draw noise) + `Control` (32-smp event grid, 3 ms ghost kill, 1/4-rate unread mods, `muteSkip`, silent rack skip) | None | None (`Control` **ON** on MPC XL!) |
| `lazyTables` | ON (built-in wavetables built lazily) | OFF | ON on MPC XL |
| Control period | **32 samples** (fixed across all tiers, `util.h:21`) | 32 samples | 32 samples |
| Memory allocation (`prepare()`) | **All 36 voices + all 13 FX × 3 racks (~75 MB)** | Same (~75 MB) | Same (~75 MB) |

- **Uncapped holes on tier `low`:**
  1. `quality.oversample_4x` (`[code core/engine-own/own_engine.cpp:1449]`, set by `serum_import.cpp:636`): NOT capped by tier `low`, forcing imported Serum presets onto 2× oversampled wavetable rendering even with zero warp (`[measured: docs/release/CPU-REVIEW-PROFILE.md:56]` D7).
  2. `noiseSample` (`[code core/engine-own/voice/voice.cpp:1433]`): hardcodes `sampleosc::QualityTier::high` (16-tap sinc + mip blend) even on tier `low` (Part 1 #21).
  3. Spectral priming (`lib_osc.cpp`): unthrottled on tier `low`, causing 2.6–3.1-block strikes (`[measured: docs/MPC.md:353-354]`).
  4. `Quality::oversampling` does NOT switch wavetable frame interpolation (own engine has no spectral frame interpolation `[code core/engine-own/own_engine.cpp:318]`).

#### (b) Hardware / software principle
An effective quality-tier system targets the dominant cost centres on the constrained hardware (voice count, unison multiplier, memory footprint, and heavy O(N)-per-sample FX) while keeping acoustic degradation below perceptual masking thresholds. Cutting a 0.3% module while leaving a 40% bottleneck uncapped (or allocating 78% unused voice RAM) wastes both quality and memory headroom.

#### (c) Best practice and literature
- **Best practice followed:** Capping polyphony (8) and unison (3) on Key 61, enabling `kEconomyControl` on both Key 61 and XL (halving chord-steal spikes), and compensating loudness via K-weighted `fxMakeup` (ITU-R BS.1770-4) when effects are masked are strong embedded product choices.
- **Where it falls short:**
  - `low` still spends **12–19% of the Key 61 block** in control-rate `Voice::tick` + FX updates (`[measured: docs/release/CPU-REVIEW-PROFILE.md:300]` D1) because `kControlPeriod = 32` is invariant, and allocates 100% of `high`-tier RAM.
  - `kEconomyMath`'s 4-sample Chorus hold and 8-sample Phaser hold introduce 11 kHz / 5.5 kHz zipper noise (Part 2 #10) where linear interpolation across the 4/8 samples costs only 1 add/sample and eliminates the artefacts.
  - Completely masking Reverb/Bode/Flanger/HyperDim/Convolve preserves loudness (`fxMakeup`), but removes the defining spatial/timbral identity of 31+ factory presets (e.g., `Bode Barber Riser` becomes dry!).

#### (d) Risks (preset breakage & dynamic adaptation)
- **Measured tier `low` sound differences (`[measured: docs/MPC.md:453-471,538]`):**
  - `Math` alone: `< 0.01 dB` level error, `-26 dB` waveform diff on heavily modulated/distorted patches.
  - `Warp` (2×/32-tap vs 4×/128-tap): `-25 dB` aliasing difference + 8-sample latency shift; severe aliasing on Sync/Fold/HardClip (Part 1 #5).
  - `unison=3`: within `0.5 dB` on plain saw, `3 dB` on 14 unison patches (and 40 dB lower Digital limiter harmonics on 3 sine pads due to lower crest factor).
  - `ghostKill`: 3 ms DC-ramp produces a subtle low-frequency thump on polyphonic voice steals (`[measured: docs/MPC.md:466]`).
  - `fxMask`: breaks spatial/timbral intent on 31+ factory presets where Reverb, Bode, Flanger, or Convolve is the core sound.
- **Safe dynamic adaptation (currently absent — profile is process-static `[code shell-mpc/device_profile.h:22]`):**
  1. **Load-shedding silent/dying voices (`D8`):** When trailing block CPU > 75% of budget, tighten release-tail auto-retire threshold from `-96 dBFS` to `-60 dBFS` and retire oldest releasing voice first (inaudible under dense polyphony).
  2. **Adaptive morph interval & staggered Spectral priming:** On chord strikes > 4 voices, stagger Spectral priming across 2–3 blocks and widen morph rebuild from 512 to 1024 samples until voice count drops.
  3. **Tiered unison step-down:** For voices 7–8 under > 80% load, step unison cap `3 → 2` on newly struck voices only (never mid-note).

#### (e) Cost breakdown
- Tier `low` measured savings on Key 61: keeps typical 8-voice factory presets under ~60–75% of the 2.902 ms budget (`[measured: docs/MPC.md:332-345]`), except Spectral strikes (`257–307%`), heavy double-warped unison (`Twin Warp Lead`, `Triple Fold Reese`), and imported `oversample_4x` presets.
- Doubling `kControlPeriod` to `64` on tier `low` would save `[hypothesis: 5–10% of the Key 61 block, verifiable via tier_bench --config low --sections]` (`[measured: docs/release/CPU-REVIEW-PROFILE.md:300]`), but degrades <1.5 ms percussive envelopes and requires careful audit of all 32-sample assumptions — lower priority than toolchain, memory, and Part 1/2 algorithmic fixes.

#### (f) Concrete recommendations
1. **Cap `quality.oversample_4x` under `kEconomyWarp` (and fix `noiseSample` tier pass-through, Part 1 #21)** (Rank 11): Gain: Medium (removes accidental 2× OS cost on imported Serum presets); Risk: Low; Proof: `tier_bench --config low` on an imported Serum preset with `oversample_4x = 1`.
2. **Replace tier `low` Chorus/Phaser step-hold with linear ramp (Part 2 #10), fix Warp aliasing with ADAA (Part 1 #5), and re-evaluate a lightweight Reverb option on Key 61 once toolchain/memory headroom is banked** (Rank 11): Gain: Medium (preset fidelity without sacrificing budget); Risk: Low–Medium; Proof: aliasing/zipper FFT + Key 61 `tier_bench --config low` budget check.

---

### Topic 7: Build and Toolchain

#### (a) What the code does
- **Toolchains (`cmake/toolchains/armv7-linux-gnueabihf.cmake`, `cmake/toolchains/aarch64-linux-gnu.cmake`, `scripts/mpc-container-build.sh`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:352-359]`):**
  - Compiler: GCC 12.2 inside Debian 12 cross-build container.
  - ARM32 (Key 61): `-march=armv7-a -mfpu=neon -mfloat-abi=hard -O3 -DNDEBUG -std=c++17 -fPIC`.
  - ARM64 (MPC XL): `-march=armv8-a -O3 -DNDEBUG -std=c++17 -fPIC`.
- **Target flags (`core/CMakeLists.txt:59`, `shell-mpc/CMakeLists.txt:18-24`, `CMakeLists.txt:359`):**
  - `synthyboy_core` and all DSP sub-libraries (`filters-serum`, `sample-osc`, `granular`, `spectral`, `multisample`, `fx-serum-a`, `fx-serum-b`) set ONLY `POSITION_INDEPENDENT_CODE ON`. They do **NOT** set `CXX_VISIBILITY_PRESET hidden`, `-fno-semantic-interposition`, `-fno-math-errno`, `-fno-trapping-math`, `-ffunction-sections`, `-fdata-sections`, `-fno-rtti`, or LTO.
  - `shell-mpc` sets `CXX_VISIBILITY_PRESET hidden` and `VISIBILITY_INLINES_HIDDEN ON` only on `synthyboy_mpc` (`shell-mpc/CMakeLists.txt:18-20`), and passes `-Wl,--gc-sections` (`line 24`) — which is **largely ineffective without `-ffunction-sections -fdata-sections`** on the compiled `.o` files!
  - LTO is enabled ONLY on desktop JUCE (`juce::juce_recommended_lto_flags`, `CMakeLists.txt:359`), **OFF** for MPC builds.
  - PGO (`-fprofile-generate`/`-fprofile-use`) is unused.
  - Exceptions and RTTI are enabled everywhere; `dynamic_cast<own::OwnEngine*>` is used only in 7 helper functions (`[code core/engine-own/own_engine.cpp:32-92]`).
  - `tier_bench` is statically linked against container glibc 2.36 `libm`, whereas `synthyboy_mpc.so` calls the device's glibc 2.39 `libm` via PLT (`[measured: docs/release/CPU-REVIEW-MEMORY.md:359]`).

#### (b) Hardware / software principle
On ELF shared libraries compiled with `-fPIC` and default symbol visibility, GCC must assume every non-inline function and global could be interposed at load time via the PLT/GOT, which **forbids intra-TU inlining and interprocedural constant propagation** unless `-fvisibility=hidden` or `-fno-semantic-interposition` is set (GCC manual; Drepper, *"How To Write Shared Libraries"*, 2011). Similarly, without `-fno-math-errno`, GCC treats `std::sqrt`/`expf`/`sinf` as mutating global `errno`, emitting a hardware `vsqrt` followed by a NaN/negative check and a conditional branch to `libm` on every square root (`[measured: docs/release/CPU-REVIEW-MEMORY.md:32,118]`). On Cortex-A17 (ARMv7ve), `-march=armv7-a` targets the 2005 base ISA lacking hardware integer division (`sdiv`/`udiv`), forcing 137 calls to `__aeabi_idiv` / `__aeabi_idivmod` (`[measured: docs/release/CPU-REVIEW-MEMORY.md:355]`) and mis-scheduling instructions for Cortex-A8/A9 instead of Cortex-A17's out-of-order dual-issue pipeline.

#### (c) Best practice and literature
- **Best practice for embedded C++ DSP shared libraries:** Target exact hardware microarchitecture (`-mcpu=cortex-a17` on RK3288 MPCs — all 32-bit Akai MPCs use RK3288; `-march=armv8-a -mtune=cortex-a76` on RK3588 XL so code still runs on A55 if migrated); enable `-fno-math-errno -fno-trapping-math` (IEEE-exact on valid audio inputs, zero precision loss); compile static DSP archives with `-fvisibility=hidden -fvisibility-inlines-hidden -fno-semantic-interposition -ffunction-sections -fdata-sections`; enable LTO (`-flto=4`) to inline hot cross-TU boundaries (`Voice::tick` → `paramTable()`/`destLawOf()`, `Filter::processPair()`, `LibOsc::update()`); and disable RTTI (`-fno-rtti`) in DSP cores.
- **What the compiler CANNOT auto-vectorise (and why):**
  1. **Every ARMv7 float loop without `-funsafe-math-optimizations`:** GCC refuses to auto-vectorise IEEE float32 into ARMv7 NEON because ARMv7 NEON always flushes subnormals (`[code core/engine-own/dsp/neon_cmp.h:5-8]`), so manual NEON intrinsics are mandatory on Key 61.
  2. **Recursive IIR/filter loops (`SVF`, `Ladder`, `Diode`, `Biquad`, `Phaser`, `Compressor` envelopes):** Sample-to-sample feedback prevents time-axis vectorisation; vectorisation must happen across stereo L/R pairs (2-lane, saturating Cortex-A17's 64-bit NEON) or 4-voice AoSoA (ARM64).
  3. **Aliased pointer arguments lacking `__restrict__`:** Functions like `simd::addScaled(float* d, const float* a, ...)` and `Phaser`'s loop (`[measured: docs/release/CPU-REVIEW-MEMORY.md:243]`) force reload/alias guards around loads and stores.
  4. **Loops containing `libm` calls:** `-fmath-errno` and the absence of `libmvec` on glibc armhf completely inhibit vectorisation and loop-invariant code motion.

#### (d) Risks
- **ISA portability guard:** `-mcpu=cortex-a17` emits `sdiv`/`udiv` (ARMv7ve, present on all RK3288 MPCs: Key 61, Live, Live II, One, One+, X, Force); it would SIGILL only on pre-ARMv7ve Cortex-A8/A9 devices, none of which are Akai MPC targets. On ARM64, use `-mtune=cortex-a76` (not `-mcpu=cortex-a76`) so instructions remain ARMv8.0-A compatible with the RK3588's Cortex-A55 cores.
- **LTO compile memory/time:** Full LTO on arm32 cross-builds increases link RAM; use `-flto=4` (within the workspace's `-j4` discipline) and build release artefacts via `buildslot.sh`.
- **PGO maintenance:** Requires capturing `.gcda` profiles on device from a representative preset suite (`tier_bench --all-presets`) and feeding them back into the container build; valuable as a release-engineering pass after higher-leverage fixes, `[hypothesis: 3–8% further gain on branch-heavy Voice::tick and FilterImpl::processInternal]`.

#### (e) Cost breakdown
- Measured in Round 12 arm32 disassembly (`[measured: docs/release/CPU-REVIEW-MEMORY.md:32,104,118-119,355]`):
  - `Voice::modulatedSlow`: **81 out-of-line calls** inside `Voice::tick` (prevented from inlining by visibility/interposition + cross-TU boundaries).
  - `137` software `__aeabi_idiv`/`__aeabi_idivmod` call sites replaceable by 1-instruction `sdiv`/`udiv` under `-mcpu=cortex-a17`.
  - Per-`sqrtf` compare + branch to `libm` across filters, compressors, and `peakAbsPair` eliminated by `-fno-math-errno`.
  - `[hypothesis: combining -mcpu=cortex-a17, -fno-math-errno -fno-trapping-math, -fvisibility=hidden -fno-semantic-interposition, and -flto will yield 5–15% CPU reduction across all presets on Key 61 and 4–10% on XL with zero DSP changes, verifiable by interleaved rtrun A/B tier_bench --all-presets --verify]`.

#### (f) Concrete recommendations
1. **Update ARM32/ARM64 toolchain and `synthyboy_core` flags (`-mcpu=cortex-a17`, `-mtune=cortex-a76`, `-fno-math-errno -fno-trapping-math`, `-fvisibility=hidden -fvisibility-inlines-hidden -fno-semantic-interposition`, `-ffunction-sections -fdata-sections`, `-flto=4`)** (Rank 4): Gain: Large (5–15% system-wide); Risk: Low (bit-identical); Proof: interleaved `rtrun` A/B `tier_bench --config low --all-presets --verify` on Key 61 and XL + `objdump` check.
2. **Replace 7 `dynamic_cast<own::OwnEngine*>` helpers with `IEngine` virtuals so DSP libs compile `-fno-rtti`, and align `tier_bench` glibc linkage with `synthyboy_mpc.so`** (Rank 18): Gain: Small; Risk: Low; Proof: `.ARM.exidx`/binary size comparison + `tier_bench` vs `dlopen(synthyboy_mpc.so)` check on `Bode Barber Riser`.

---

### Topic 8: The Measurement Method Itself

#### (a) What the code does
- **`synthyboy_tier_bench` (`bench/tier_bench.cpp`):** Runs realistic MPC parameters (44.1 kHz, 128 frames, FTZ enabled), supports `--core N`, `--config`, `--all-presets`, `--repeat 3`, `--budget`, `--paced`, `--sections` (`section_timing.cpp`), `--micro`, `--profile` (250 µs `SIGPROF` PC sampler), and `--verify` (bit-exact comparison). Prints `scaling_cur_freq` (`tier_bench.cpp:567`).
- **`synthyboy_noteon_bench` (`bench/noteon_bench.cpp`):** Measures idle block, 1-note strike vs steady, and 8-note chord strike vs steady (`[measured: docs/MPC.md:67-97]`).
- **`SYNTHYBOY_MPC_CALLBACK_STATS` (`shell-mpc/callback_stats.h`):** Real in-host callback gap histogram (`<1.5×`, `<2×`, `<4×`, `<8×`, `>=8×` of 2.902 ms), top-10 gaps, mean and max processing time (`[measured: docs/MPC.md:122,312]`).
- **`synthyboy_preset_render --float32 --hash` + `scripts/compare-renders.sh`:** Deterministic float32 regression checks (Round 13 K6).
- **`synthyboy_bench` (`bench/bench_engine.cpp:144,149`):** Still defaults to 48 kHz and `--ftz` OFF; `docs/PERFORMANCE.md:54` still publishes outdated Nov 4 Phase-3 laptop numbers of an unimplemented stub engine.

#### (b) Hardware / software principle
Performance measurements without RT scheduling isolation, interleaved A/B execution, and hardware PMU counters suffer from severe confounding (OS preemption, thermal DVFS throttling, memory layout bias, and clock-instrumentation observer effect) that routinely exceeds the 3–10% size of real DSP optimisations (Mytkowicz et al., *"Producing Wrong Data Without Doing Anything Obviously Wrong!"*, ASPLOS 2009; Gregg, *Systems Performance*, 2020). Furthermore, mean latency hides the tail spikes (strike blocks, cold-cache refills, Q-Link dirty blocks, page faults) that actually cause real-time audio drop-outs: p99 and max block time are the primary correctness metrics.

#### (c) Best practice and literature
- **Best practice followed:** In-host callback gap histograms (`callback_stats.h`), `--paced` mode, 250 µs `SIGPROF` sampling, `--verify` determinism checks, and separating strike-block vs steady-block benchmarks (`noteon_bench`) are well above industry average for plugin development.
- **Where it falls short (critical blind spots):**
  1. **Non-RT core contention (`[measured: docs/MPC.md:321,336,350]`):** Running `tier_bench` as `SCHED_OTHER` on core 3 while the MPC host's `SCHED_FIFO 20` audio thread is also on core 3 inflates p99 by **+20 to +25 percentage points** even on idle presets and caused an **18% day-to-day drift** between Round 12 and Round 13.
  2. **Zero PMU hardware counters (`[measured: docs/release/CPU-REVIEW-MEMORY.md:344,449]`):** Neither `tier_bench` nor any script reads `perf_event_open` (`PERF_COUNT_HW_CPU_CYCLES`, `INSTRUCTIONS`, `L1D_READ_MISSES`, `LL_READ_MISSES`, `BRANCH_MISSES`), leaving every memory/cache optimisation (`MEM1`, `VoiceHot`, wavetable stride) unprovable at the hardware level.
  3. **Clock observer effect in `--sections` (`[measured: docs/MPC.md:540]`, `[code core/engine-own/dsp/section_timing.cpp:25]`):** Each `steady_clock::now()` call costs `~1.3 µs` on Key 61 (~2.6 µs/scope). Inside `Voice::tick` (10 nested scopes × 8 voices × 4 periods = 320 scopes/block), instrumentation alone adds **~400–800 µs (14–28% of the block!)**, distorting fine-grained rankings and only recording mean + single `slowest` block rather than per-section p99.
  4. **No thermal zone logging:** `tier_bench` logs CPU frequency but not `/sys/class/thermal/thermal_zone*/temp` (Key 61 reaches `65–69°C` under load `[measured: docs/MPC.md:156,159]`).
  5. **Missing realistic workloads & shell boundary:** No Q-Link 1-param/block automation sweep, no preset-change-while-playing test, no 16-note burst test, no 3 s silent-sustain held-note test, no per-module isolated cost table (all filters, all warps, all 13 FX on Key 61 & XL), no `minflt`/`majflt` page-fault tracking, and `tier_bench` bypasses `OutputStage`/`OutputPeak`/`SpectrumRing`.
  6. **No automated regression threshold:** No machine-checked budget table (`p99 < 80%`, `max_strike < 95%`, `VmRSS < limit`, `majflt == 0`) gating releases.

#### (d) Risks
- **Mis-ranked engineering work:** Measuring a 5% optimisation inside ±20% `SCHED_OTHER` noise leads to shipping regressions or abandoning real wins.
- **Unobserved production regressions:** Changes to `Voice` size, `prepare()` RAM, Q-Link `blockDirty`, or `shell-mpc` overhead pass `tier_bench` invisibly because those paths are never exercised or gated.

#### (e) Cost breakdown (the smallest measurement set to rank Parts 1–3)
Before starting any optimisation from Parts 1–3, run this **5-step, ~10-minute on-device baseline** on Key 61 (and steps 1–3 on MPC XL):
1. **RT-isolated A/B harness check:** Run `rtrun` (`SCHED_FIFO 20` on isolated core 2, or with host audio idle) `synthyboy_tier_bench --config low --paced --repeat 3` logging `/sys/class/thermal/thermal_zone0/temp` on 8 anchor presets (`Basic Saw`, `Twin Warp Lead`, `Triple Fold Reese`, `Stretched Glass Choir`, `Spectral Vowel Freeze`, `Diffusor Cloud Pad`, `Ring Mod Gamelan`, `Pentatonic Step Pluck`) — confirm run-to-run p99 spread `< 2%`.
2. **Per-module isolated cost table (`--micro` extended):** Measure 1-voice and 8-voice cost of each oscillator type, each warp (with 3-voice unison), each filter model (`SVF`, `Classic Ladder`, `MGLow24`, `AcidLadder`, `Diode`, `EQ12`, `Formant`), and each of the 13 FX modules (including `Delay HQ`, `Reverb` 6 algos, `Bode`, `HyperDim`, `Distortion`, `Compressor` multiband/limit) using 1 coarse outer timer per module (zero nested `steady_clock` distortion).
3. **PMU cache & branch profile:** Wrap the 8 anchor presets (warm steady AND cold 8-note / 16-note strike) with `perf_event_open` reading `CYCLES`, `INSTRUCTIONS`, `L1D_CACHE_REFILL`, `L2D_CACHE_REFILL`, and `BR_MIS_PRED` to quantify the exact memory-bound vs compute-bound split.
4. **Live stress gestures:** Run `noteon_bench` (16-note burst + 8-note Spectral strike), a 1-param/block Q-Link cutoff sweep, and a 3 s held `Ring Mod Gamelan` test.
5. **In-host shell & RAM audit:** On Key 61 inside the real MPC host with `SYNTHYBOY_MPC_CALLBACK_STATS=1`, load 1 then 4 instances, play an 8-note chord while sweeping Q-Links and switching presets, recording `VmRSS`, `prepare` ms, `minflt`/`majflt`, and gap histogram (`max_gap`, `>=2×` count).

#### (f) Concrete recommendations
1. **Build the 5-step RT-isolated + PMU + per-module benchmark & regression suite** (Rank 9): Gain: Large (turns all Parts 1–3 hypotheses into ground truth with `< 2%` noise); Risk: Low; Proof: 3 consecutive `rtrun` Key 61 runs showing `< 2%` p99 variance and a complete populated per-module ms/block table.
2. **Replace `docs/PERFORMANCE.md`'s obsolete Phase-3 laptop table with the Key 61 / XL baseline and set `synthyboy_bench` defaults to 44.1 kHz + `--ftz` ON** (Rank 9): Gain: Medium (clarity); Risk: Low; Proof: updated `docs/PERFORMANCE.md` matching device numbers.

---

### Topic 9: The Plugin Shell Boundary on the MPC (`shell-mpc`)

#### (a) What the code does
- **Audio callback (`shell-mpc/synthyboy_mpc.cpp:3184-3312`):**
  1. `CallbackStats::begin` (`line 3187`, compiled out in release unless `SYNTHYBOY_MPC_CALLBACK_STATS=1`).
  2. `startupTimes().reached(kFirstAudio)` (`line 3190`, single relaxed atomic load after block 1).
  3. `flushDenormalsSave()` + `flushDenormalsOn()` (`lines 3217-3218`, 3 unconditional `vmrs`/`vmsr` per block).
  4. `audioMaster(audioMasterGetTime)` through `HostGate` (`lines 3222-3240`) → `engine->setTransport(...)`.
  5. Drain `eventInbox.pop(ev)` (`lines 3244-3252`, lock-free consumer) into pre-allocated `events` buffer, clamp frames to `[0, sampleFrames - 1]`, insertion-sort by frame (`lines 3253-3267`), prepend `soundOffPending` if requested (`lines 3268-3274`).
  6. `engine->process(events.data(), count, outL, outR, sampleFrames)` (`line 3278`).
  7. `outputStage.process(outL, outR, sampleFrames)` (`line 3281`, NEON fast-path when quiet/bypassed).
  8. Latency change check (`lines 3284-3288`): updates atomic `latencyDirty` (host `ioChanged` deferred to UI/idle thread — RT-safe!).
  9. `outputPeak().note(outL, outR, sampleFrames)` (`line 3292`, NEON peak scan — runs **every block** even when overlay/status is closed).
  10. `spectrumRing.push(outL, outR, sampleFrames)` (`line 3297`, 128 scalar atomic stores into an 8,192-float = **32 KB** ring — runs **every block** even when overlay is closed!).
  11. `events.clear()`, `startupTimes().mark(kFirstAudio)`, `flushDenormalsRestore()` (`lines 3300-3310`).
- **Event hook (`effProcessEvents`, `synthyboy_mpc.cpp:2425-2475`):** Translates VST MIDI events, calls `noteDiagnostics().noteOn(hostVel)` unconditionally in release (`line 2456` — function-local static at `[code shell-mpc/output_peak.h:136]`!), calls `PlayLog::note` (compiled out unless `SYNTHYBOY_MPC_DEBUG_HOOKS=1`; uses lock-free Vyukov MPSC ring with `vsnprintf`), and pushes into `eventInbox` (which acquires `while (producers.test_and_set(acquire)) {}` spinlock `[code shell-mpc/event_inbox.h:22]`).
- **Parameter hook (`pluginSetParameter`, `synthyboy_mpc.cpp:3039-3131`):** Echo-filters against `paramMirror`, then calls `engine->setParam()` (which can hit `WakeSignal::notify` `SYS_futex` on `fxiNN.type`).
- **Preset / chunk load (`applyState`, `synthyboy_mpc.cpp:1891-1925`):** Writes all 2,637 parameters sequentially via `setParam()` with zero transactional batching, then synchronously calls `engine->setContent()` (triggering `ContentSet::build` STFT analysis on the host UI thread).
- **Overlay's `SharedBlock` (`shell-mpc/overlay/gpu/scene.h:642-724`, `helper_transport_linux.h:181-201`):** Lives in `memfd_create` + `MAP_SHARED` updated by the plugin's non-RT overlay thread (pinned to non-audio cores `0-1` via `cpu_plan_linux.h`) using a word-wise atomic seqlock, read by the external `synthyboy-overlay-gpu` helper process. **The audio thread never touches `SharedBlock`, `memfd`, `contentMutex`, `programMutex`, `chunkMutex`, or `lfoShapeMutex`.**

#### (b) Hardware / software principle
Even when the DSP core is strictly lock-free, a single spinlock, lazy C++11 static local guard, or cache-thrashing diagnostic ring in the host-callback wrapper can inject priority inversion or evict an entire 32 KB L1D cache (the exact size of `SpectrumRing`!) on every pass through the callback.

#### (c) Best practice and literature
- **Best practice followed:** Isolating the overlay `SharedBlock` on non-audio cores `0-1`, deferring `ioChanged` latency notifications via an atomic `latencyDirty` flag (unlike `shell-juce`, Part 2 #13), compiling out `PlayLog`/`CallbackStats`/`MPCKIT_LOG` in release builds, and reserving 256 slots in `EventInbox` exclusively for note-offs (preventing stuck notes under MIDI floods) are exemplary embedded engineering.
- **Where it falls short:**
  - `EventInbox::push` uses a spinlock (`event_inbox.h:22`) when `play_log.h` in the exact same directory already implements a lock-free Vyukov MPSC ring.
  - `noteDiagnostics()` (`output_peak.h:136`) uses a lazy function-local static inside `effProcessEvents` (`synthyboy_mpc.cpp:2456`), triggering `__cxa_guard_acquire` on the first live MIDI note.
  - `applyState` lacks atomic parameter-batching, producing torn preset audio and 2,637 `paramGen` bumps during preset changes.
  - `spectrumRing.push` touches 512 B/block cycling through a 32 KB ring (100% of Key 61's L1D) via 128 scalar atomic stores even when no overlay spectrum is visible.

#### (d) Risks
- **Producer spinlock priority inversion (`[code shell-mpc/event_inbox.h:22]`):** If a host UI/MIDI thread (`SCHED_OTHER`) is preempted inside `EventInbox::push()` while a `SCHED_FIFO` host MIDI thread spins on `producers.test_and_set()`, the RT thread livelocks until the scheduler preempts it.
- **First-note static guard (`[code shell-mpc/output_peak.h:136]`, `[code shell-mpc/synthyboy_mpc.cpp:2456]`):** Unwarmed lazy static guard in `effProcessEvents`.
- **Torn preset audio & UI hitch (`[code shell-mpc/synthyboy_mpc.cpp:1891-1925]`):** Switching presets during playback produces transient half-A/half-B frames (potential loud filter/level spikes) and blocks the MPC UI thread during synchronous STFT/wavetable building.
- **Host `audioMasterGetTime` (`[code shell-mpc/synthyboy_mpc.cpp:3224]`):** Unavoidable host call every block; observed healthy in real host soaks (`1.12–1.20×` max gap `[measured: docs/MPC.md:122,312]`), must stay monitored under `callback_stats.h`.
- **Debug-build `vsnprintf` on RT thread (`[code shell-mpc/play_log.h:89]`, `[code shell-mpc/kit/log.h:174]`):** Safe on glibc C locale and compiled out in release (`SYNTHYBOY_MPC_DEBUG_HOOKS=0`, `SYNTHYBOY_MPC_DEV_LOG=0`), but must never be enabled in shipping builds.

#### (e) Cost breakdown
- Measured full-shell idle callback on Key 61: `63–65 µs/block` (`~2.2%` of 2.902 ms `[measured: docs/MPC.md:122]`) vs `22 µs/block` for bare `OwnEngine` in `noteon_bench` (`[measured: docs/MPC.md:71]`) — meaning the MPC shell wrapper (`audioMasterGetTime` + `OutputStage` + `outputPeak` + `spectrumRing` + `vmrs`/`vmsr`) costs **~41–43 µs/block (`~1.4–1.5%` of budget)** continuously on every single block, invisible to `tier_bench`!

#### (f) Concrete recommendations
1. **Replace `EventInbox` spinlock with Vyukov lock-free MPSC ring, warm `noteDiagnostics()` in `VSTPluginMain`, gate `spectrumRing.push` on `overlayActive` atomic, and add transactional `beginParamBatch`/`endParamBatch` for `applyState`** (Rank 13): Gain: Medium (RT safety + glitch-free preset swaps + lower L1D churn); Risk: Low–Medium; Proof: `SYNTHYBOY_MPC_CALLBACK_STATS=1` gap stats during preset switching under 8-note chord on Key 61 + idle callback µs before/after gating `spectrumRing`.

---

## Ordered Work List Across Parts 1 to 3

1. **Part 3 #9 — RT-isolated `rtrun` + PMU + per-module benchmark suite:** First because ±20% `SCHED_OTHER` noise invalidates every downstream ranking; *pre-check:* 3 consecutive `rtrun` Key 61 runs confirming `< 2%` p99 variance and populated per-module table.
2. **Part 3 #2 & #3 — Fix `ContentHub` UAF/race, `WakeSignal` audio-thread mutex/futex, and `Convolve` RT `shared_ptr` drop:** Top reliability risk (live crashes & priority inversion) for low effort; *pre-check:* TSan `setContent()` stress + `strace -e futex` / malloc hook during `fxi01.type`/`convolve.size` automation.
3. **Part 3 #4 — ARM32 `-mcpu=cortex-a17`, `-fno-math-errno`, `-fvisibility=hidden` on `synthyboy_core`, and LTO:** Largest system-wide CPU win (5–15%) with zero DSP risk and tiny effort; *pre-check:* interleaved `rtrun` A/B `tier_bench --all-presets --verify` + `objdump` checking `__aeabi_idiv` and `vsqrt` branches.
4. **Part 1 #1 — Fix `gSafe <= 0.95f` 10.67 kHz cutoff ceiling in `core/filters-serum`:** Highest sound-quality bug (every ladder filter loses the top octave) for a 3-line fix; *pre-check:* `synthyboy_filter_response` sweep at 16–20 kHz on `MGLow24`/`EMSLadder`.
5. **Part 3 #1 + Part 1 #10 + Part 2 #5 (memory half) — Tier-scale voice pool (`maxVoices + 4`), skip masked FX buffers, pool `LibOsc`/`FilterImpl`/`Reverb` arenas:** Cuts ~75 MB → ~18 MB/instance (eliminating multi-instance OOM on 4 GB Key 61) and >60% startup time; *pre-check:* Key 61 `VmRSS` (1 & 4 instances), `prepare_voices`/`prepare_fx` ms, and PMU `L2D_CACHE_REFILL`.
6. **Part 3 #5 — Cap burst note-ons to `polyphonyLimit()` before `start()`/`tick()` and stagger Spectral priming:** Eliminates the measured 2.6–3.1-block (and >15 ms 16-note) drop-out spike on Key 61; *pre-check:* `synthyboy_noteon_bench` 8- and 16-note Spectral chord worst-block ms.
7. **Part 1 #2 — Move warp decimation FIR outside the per-unison-voice loop:** Huge CPU win (up to 8× less FIR work on warped unison) with low risk and bit-near-identical output; *pre-check:* `rtrun` `tier_bench --sections` on `Twin Warp Lead` and `Triple Fold Reese` + null test.
8. **Part 1 #3, #8, #14, #15 + Part 2 #2, #3, #9, #13 — Fast correctness/bugfix bundle:** External Spectral 16× FFT rate, `HOST` LFO `songSeconds`, Combs shared LP state, Sub `panRight`, `SplitterLMH` `AP2`, splitter latency alignment, `voiceCountGain` mid-period update, CLAP mono/sleep & JUCE RT `setLatencySamples`; *pre-check:* targeted unit/invariance tests for each defect.
9. **Part 1 #4, #6, #7 — Core voice-path anti-aliasing & zipper fixes:** Clamp pre-warp band limit below `0.5*os*fs`, ramp wavetable `wB` across 32 samples, fix Sample/Granular mip selection (`ceil(log2(|pitch*speed|))`); *pre-check:* 65,536-pt FFT aliasing & 1378 Hz zipper sweeps.
10. **Part 1 #9 & #11 — Eliminate per-sample filter `libm`/divs and finish ARMv7 `neon_cmp.h` in `spectral`/`wt_osc`:** High deterministic Key 61 CPU wins with zero sound change; *pre-check:* per-module `--micro` on `MGLow24`/`Acid`/`Diode`/`EQ12`/`Spectral` + `objdump` for `vcmpe`/`vmrs`.
11. **Part 3 #6 & #7 — Split `VoiceHot`/`VoiceCold` + shared `MorphScratch` (`MEM1`) and domain-split `blockDirty`:** Cuts L1D misses and live Q-Link spikes; *pre-check:* PMU `L1D_CACHE_REFILL` + Q-Link 1-param/block `tier_bench` on `Twin Warp Lead`.
12. **Part 3 #8, #10, #12, #13 — Platform & shell RT hardening:** Add FTZ/DAZ to `OwnEngine::process()`, enable `SB_NEON` on Apple ARM64, fix `Bank::run()` thread hook/polling, fuse output passes & fix `scratchRight`/`convolveOn`, lock-free `EventInbox` & atomic preset batching; *pre-check:* iOS `synthyboy_bench`, CLAP FTZ-off tail test, `ps -eLo` on Key 61, host preset-switch gap stats.
13. **Part 1 #5 + Part 2 #1 — ADAA / oversampling for nonlinear Warps and Distortion (plus X-Shaper curves):** Transforms aggressive synth tones from aliased to reference-grade; *pre-check:* 65,536-pt FFT aliasing sweeps + Key 61/XL CPU budget margin after items 3–11.
14. **Part 2 #5 (CPU half), #6, #7, #8, #12 — Heavy FX algorithmic rewrites (`Reverb` FWHT/hoisting, `Delay HQ` barycentric, `Bode` ring/Hilbert, `Compressor` multiband dry + $O(1)$ limiter, `HyperDim` 1-ring):** Unlocks XL headroom and opens the door to enabling lightweight FX on Key 61; *pre-check:* per-module `--micro` on all 6 Reverbs, Delay HQ, Bode, Multiband Comp, HyperDim.
15. **Part 2 #4 & #10 + Part 1 #12 & #26 — Gardner non-uniform `Convolve`, FX/Chorus/Phaser smoothing, real-packed sized Morph IFFT, and NEON pair kernels for `Normal`/`Acid`/`MGDirty`:** High-polish final tier for studio/XL parity; *pre-check:* 2 s/4 s IR `tier_bench`, 5.5/11 kHz zipper FFTs, and filter microbenchmarks.

---

## What to Watch at the Finest Level

- [ ] Never call `std::vector::assign`/`push_back`/`resize` inside `process()` (`own_engine.cpp:1336` `scratchRight` still can on oversized mono blocks).
- [ ] Never put function-local `static const` initialisers inside `process()` or `effProcessEvents()` (`own_engine.cpp:1799`, `output_peak.h:136` emit `__cxa_guard_acquire`).
- [ ] Never call `WakeSignal::notify()` (mutex on Apple/Win, `SYS_futex` on Linux) or overwrite a `std::shared_ptr` on the audio thread (`helper_thread.h:43,71`, `fx_modules.cpp:820,989`).
- [ ] Never reclaim audio-read pointers with a fixed-size overwrite ring (`own_content.h:103-105`); always use a Dekker hazard pointer and clear `contentInUse` at block end (`own_engine.cpp:1417`).
- [ ] Every background thread spawned by the engine must call `runHelperThreadStartHook()` first (`wavetable_bank.cpp:472` missed it) and never busy-poll with `sleep_for(10ms)`.
- [ ] Never let a single parameter write trigger full 2,637-param copy, 64-slot DFS matrix compile, 128-note `log2` retune, and full voice cache invalidation (`own_engine.cpp:1352-1365`).
- [ ] Cap burst note-ons to `polyphonyLimit()` *before* running `start()`/`tick()`/`beginType()`, or 16 simultaneous notes blow past the 2.902 ms budget (`own_engine.cpp:674-677`).
- [ ] Keep every active `Voice` hot state compact and 64-byte aligned (`< 16 KB`), sharing engine scratch (`MorphScratch`, `spectralScratch`) so one voice stays inside Cortex-A17's 32 KB L1D.
- [ ] Scale voice pool size, `FilterImpl` delay rates, and FX buffer allocation to active `Quality` tier and `sampleRate` — never allocate 75 MB/instance on a 4 GB device.
- [ ] Always apply FTZ/DAZ inside `OwnEngine::process()` (branch-skipping `vmsr` when already set) so CLAP, iOS, and offline renderers match MPC without denormal traps.
- [ ] Never use raw `vcltq_f32`/`vcgtq_f32`/`vcgeq_f32` in ARMv7 NEON loops — GCC spills them to 4× scalar VFP compares; always use `neon_cmp.h` bit-pattern helpers (`kernels.cpp`, `wt_osc.h`).
- [ ] Never call `std::floor`/`std::round` in ARMv7 hot loops (no `vrint` on ARMv7 → `libm` call); use `ffloor` (`util.h:49-79`) and `ffloor(x + 0.5f)`.
- [ ] Never compile `synthyboy_core` with `-fPIC` + default visibility, missing `-fno-math-errno`, or `-march=armv7-a` on Cortex-A17 — verify disassembly for 0 `__aeabi_idiv` and 0 `vsqrt` branches.
- [ ] Enable `SB_NEON` on Apple ARM64 (`simd.h:13` currently forces scalar on iOS/macOS) and keep `alignas(64)` + `__restrict__` on planar audio buffers.
- [ ] Replace UB `union` float/int punning in `fastmath.h:37,44` with `std::memcpy`, guard `fastmath::log2` against `<= 0`, and bound `sinCycles` integer cast.
- [ ] Gate `quality.oversample_4x` (`own_engine.cpp:1449`) and `noiseSample` quality (`voice.cpp:1433`) by the active tier so imported/noise presets cannot bypass Key 61 caps.
- [ ] Never hold a `std::atomic_flag` spinlock on a producer that can race an RT thread (`event_inbox.h:22`); use Vyukov's lock-free MPSC ring.
- [ ] Never write 2,637 preset parameters non-atomically while audio runs (`synthyboy_mpc.cpp:1891-1925`); batch or double-buffer preset swaps.
- [ ] Skip `voiceCountGain`, `runBus`, and `Voice::render` on silent buses and zero-amplitude sustained voices, and fuse `mix[0]` copy + `master::stage` + master peak into one pass.
- [ ] Gate `spectrumRing.push` (32 KB L1D sweep) and `writeReadout()` (`exp`/`log10` + 300–700 atomics/block) when no UI/overlay is actively consuming them.
- [ ] Always check block-split invariance (`block=32` vs `block=512` + mid-period event splits) for `HOST` LFOs, `voiceCountGain`, `makeupOfBus`, `oscMono`, and `fx-serum-a` ramps.
- [ ] Never trust `SCHED_OTHER` `tier_bench` runs on a shared audio core; always run interleaved A/B under `rtrun` (`SCHED_FIFO 20`), log SoC temperature, and verify `< 2%` variance.
- [ ] Never rely on `steady_clock::now()` inside per-voice micro-scopes on Key 61 (`~1.3 µs`/call distorts results); measure modules with coarse outer timers and `perf_event_open` PMU counters.
- [ ] Track `getrusage` `minflt`/`majflt`, `VmRSS`, and max strike-block ms alongside steady p99 in every release gate — mean CPU alone hides drop-outs.
- [ ] Keep `docs/PERFORMANCE.md` and `core/engine-own/README.md` synchronised with measured device numbers and actual buffer sizes (e.g., 33.1 MB wavetable bank, not 20.5 MB).

---

## Public References

1. **ARM Ltd.** — *ARM Cortex-A17 MPCore Processor Technical Reference Manual* (r1p1, 2014) and *ARM Architecture Reference Manual: ARMv7-A and ARMv7-R Edition* (DDI 0406C, 2014).
2. **ARM Ltd.** — *ARM Cortex-A76 Core Technical Reference Manual* (2018) and *ARM NEON Programmer's Guide* (DEN0018A, 2013).
3. **Bencina, R.** — *"Real-time audio programming 101: time waits for nothing"*, 2011.
4. **Boehm, H.-J.** — *"Can Seqlocks Get Along with Programming Language Memory Models?"*, ACM SIGPLAN Workshop on Memory Systems Performance and Correctness (MSPC), 2012.
5. **Drepper, U.** — *"What Every Programmer Should Know About Memory"*, Red Hat, 2007.
6. **Drepper, U.** — *"How To Write Shared Libraries"*, 2011.
7. **Goldberg, D.** — *"What Every Computer Scientist Should Know About Floating-Point Arithmetic"*, ACM Computing Surveys, 23(1):5–48, 1991.
8. **Gregg, B.** — *Systems Performance: Enterprise and the Cloud*, 2nd ed., Addison-Wesley, 2020.
9. **Herlihy, M. & Shavit, N.** — *The Art of Multiprocessor Programming*, 2nd ed., Morgan Kaufmann, 2020.
10. **ITU-R** — *Recommendation BS.1770-4: Algorithms to measure audio programme loudness and true-peak audio level*, 2015.
11. **Michael, M. M.** — *"Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects"*, IEEE Transactions on Parallel and Distributed Systems, 15(6):491–504, 2004.
12. **Mytkowicz, T., Diwan, A., Hauswirth, M., & Sweeney, P. F.** — *"Producing Wrong Data Without Doing Anything Obviously Wrong!"*, ASPLOS, 2009.
13. **Pirkle, W.** — *Designing Software Synthesizer Plugins in C++*, 2nd ed., Focal Press, 2021.
14. **Sha, L., Rajkumar, R., & Lehoczky, J. P.** — *"Priority Inheritance Protocols: An Approach to Real-Time Synchronization"*, IEEE Transactions on Computers, 39(9):1175–1185, 1990.
15. **Vyukov, D.** — *"Intrusive / Bounded MPSC Queue"*, 1024cores.net, 2010.

---

Clean-room attestation: Nothing under `core/engine-vital/`, `core/vital-import/`, or `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/` was opened, read, grepped, or listed at any point during this audit.
