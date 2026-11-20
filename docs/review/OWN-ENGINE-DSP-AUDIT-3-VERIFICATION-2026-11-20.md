# Independent Verification of DSP Audit Part 3: Core, Performance, Threading, Memory, Build & MPC Targets

**Date:** 2026-11-20  
**Audit Verified:** `docs/review/OWN-ENGINE-DSP-AUDIT-3-CORE-PERFORMANCE-2026-11-20.md`  
**Method:** Clean-room, adversarial code reading and arithmetic verification against the repository source and recorded benchmark documents (no build, test, or benchmark executed).

---

## Summary Table

| Rank | Finding in six words | Verdict | Corrected `file:line` if different | The measurement still needed (or "none") |
|---:|---|---|---|---|
| 1 | Unconditional 36-voice and three-rack FX allocation | `CONFIRMED` | `core/engine-own/own_engine.cpp:426-431,452-458` | none (`engine_prepare` 169.4 ms already `[measured: docs/MPC.md:148,327]`) |
| 2 | ContentHub lock-free publish race and UAF | `PARTLY WRONG` | `core/engine-own/own_engine.cpp:230-236,1351`, `core/engine-own/voice/own_content.h:101-106,151-155` | TSan stress test calling `setContent()` >8 times concurrently with `process()` |
| 3 | Audio-thread WakeSignal notify and Impulse free | `PARTLY WRONG` | `core/engine-own/dsp/helper_thread.h:36-47,71`, `core/engine-own/fx/fx_modules.cpp:786-787,820-825,986-995,1019-1020` | Non-Linux `WakeSignal::notify()` contention test and >64-job queue saturation test |
| 4 | Suboptimal ARMv7/ARM64 compiler and linker flags | `PARTLY WRONG` | `cmake/toolchains/armv7-linux-gnueabihf.cmake:16`, `core/CMakeLists.txt:59`, `shell-mpc/CMakeLists.txt:18-20,230` | Interleaved A/B `tier_bench` on Key 61 and XL with `-mcpu=cortex-a17`, hidden visibility, and LTO |
| 5 | Kill-mode voice overflow before killExcess runs | `CONFIRMED` | `core/engine-own/own_engine.cpp:678-682,852-856,1193,1574,1670,1760` | none (Spectral chord-strike stall already `[measured: docs/MPC.md:353-354]`) |
| 6 | Voice struct layout and MorphScratch bloat | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/voice/voice.h:213-397`, `core/engine-own/osc/wt_osc.h:520,1223`, `core/engine-own/osc/morph.h:22-26` | Cortex-A17 PMU `L1-dcache-load-misses` / `L2D_CACHE_REFILL` + `tier_bench` before/after layout changes |
| 7 | Single setParam triggers full blockDirty rebuild | `PARTLY WRONG` | `core/engine-own/own_engine.cpp:260-261,1350-1365,1392-1394`, `core/engine-own/voice/note_tuning.h:95-125` | `tier_bench` with 1-param/block Q-Link sweep vs static parameters on Key 61 |
| 8 | Apple ARM64 NEON disabled, missing CLAP FTZ | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/dsp/simd.h:12-28`, `core/filters-serum/src/filter_lanes.h:17-23`, `shell-clap/synthyboy_clap.cpp:225-393` | Apple ARM64 (iOS/macOS) `synthyboy_bench` with `SB_NEON=1`, CLAP tail decay timing, and XL FMA A/B bench |
| 9 | tier_bench lacks SCHED_FIFO, PMU, thermal logs | `CONFIRMED` | `bench/tier_bench.cpp:11-12,557,603-776`, `core/engine-own/dsp/section_timing.cpp:25,57-73` | none (`+20–25%` p99 noise, `18%` drift, `1.3 µs` clock cost already `[measured: docs/MPC.md:321,350,540]`) |
| 10 | Wavetable builder thread affinity and polling | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/osc/wavetable_bank.cpp:362,372,472-497` | `ps -eLo pid,tid,psr,cls,rtprio,ni,comm` on Key 61 during live plugin load and idle operation |
| 11 | Low tier ignores oversample_4x; masked FX | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/own_engine.cpp:1449`, `core/serum-import/serum_import.cpp:636`, `bench/tier_bench.cpp:162` | Key 61 `tier_bench` on imported Serum presets with `quality.oversample_4x = 1` |
| 12 | Redundant output zeroing and silent-bus passes | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/own_engine.cpp:1324,1335-1339,1672-1737,1799` | Key 61 `tier_bench --sections` measuring `engine tail` before/after removing redundant zeroing and silent-bus loops |
| 13 | MPC shell spinlock, applyState, unconditional meters | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `shell-mpc/event_inbox.h:22,35-42`, `shell-mpc/output_peak.h:136`, `shell-mpc/synthyboy_mpc.cpp:1897-1917,2456,3298-3301` | Audio glitch test during live preset switch (`applyState`) while playing notes on Key 61 |
| 14 | Silent sustained voices and writeReadout overhead | `PARTLY WRONG` | `core/engine-own/voice/voice.cpp:1108-1118`, `core/engine-own/own_engine.cpp:1190-1192,1901-2034` | `tier_bench` on `Ring Mod Gamelan` after 1.5 s decay with silent-sustain voice skip |
| 15 | Missing mlock, pinned contentInUse, FxPool leak | `PARTLY WRONG` | `core/engine-own/own_engine.cpp:202-210,1417`, `core/engine-own/fx/fx_chain.cpp:113-149`, `shell-mpc/callback_stats.h:20-70` | Key 61 `getrusage` `minflt`/`majflt` logging during cold preset switches |
| 16 | Missing alignas, fastmath bounds, cached std::round | `PARTLY WRONG` | `core/engine-own/dsp/fastmath.h:37-82`, `core/engine-own/voice/voice.cpp:1360-1376`, `core/engine-own/fx/fx_modules.cpp:914-945` | Key 61 `tier_bench` with `alignas(16)` on hot voice/oscillator buffers |
| 17 | Outdated README wavetable bank memory figure | `CONFIRMED` | `core/engine-own/README.md:13`, `core/engine-own/osc/wavetable_bank.h:20,28-30`, `core/engine-own/osc/wavetable_bank.cpp:229-235,274-281` | none (arithmetic from declarations is exact and deterministic) |
| 18 | Static tier_bench glibc mismatch and RTTI | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/own_engine.cpp:32-92`, `core/CMakeLists.txt:59`, `bench/CMakeLists.txt:30-32` | Key 61 `tier_bench` linked dynamically against device glibc 2.39 `libm.so.6` vs `-static` glibc 2.36 |

---

## Counts per Verdict

| Verdict | Count | Ranks |
|---|---:|---|
| `CONFIRMED` | 4 | 1, 5, 9, 17 |
| `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | 7 | 6, 8, 10, 11, 12, 13, 18 |
| `PARTLY WRONG` | 7 | 2, 3, 4, 7, 14, 15, 16 |
| `REFUTED` | 0 | — (note: specific sub-claims inside Ranks 3, 7, 14, 15, and 16 are refuted; see sections below) |
| `COULD NOT VERIFY` | 0 | — |
| **Total** | **18** | |

---

## Finding-by-Finding Verification

### Rank 1 — Unconditional 36-voice and three-rack FX allocation in `OwnEngine::prepare()`

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:426-431,452-454`, `docs/MPC.md:148,327`.
  - Corrected: `core/engine-own/own_engine.cpp:426-431,452-458` (the 3-rack FX preparation loop spans lines 452–458; `kPoolVoices = kMaxVoices + 4 = 36` is declared at `core/engine-own/own_engine.h:88-89`, and per-rack module instantiation is at `core/engine-own/fx/fx_chain.cpp:173-180`).
- **(b) Verbatim decisive code (`core/engine-own/own_engine.cpp:426-431,452-458`):**
  ```cpp
      voices.resize (static_cast<size_t> (kPoolVoices));
      for (int i = 0; i < kPoolVoices; ++i)
      {
          voices[static_cast<size_t> (i)] = std::make_unique<Voice>();
          voices[static_cast<size_t> (i)]->prepare (sampleRate);
      }
      for (int i = 0; i < 3; ++i)
      {
          fx[i].setRackId (i);
          fx[i].setPool (&fxPool);
          fx[i].prepare (sampleRate, maxBlockFrames);
      }
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - `OwnEngine::setQuality()` (`core/engine-own/own_engine.cpp:239-243`) only stores `qualityTier`, `qualityMaxVoices`, `qualityMaxUnison`, `qualityFxMask`, `qualityRealtime`, `qualityEconomy`, and `qualityFxMakeup` into atomic members; `OwnEngine::prepare()` never reads `qualityMaxVoices` or `qualityFxMask` when sizing `voices` (`kPoolVoices = 36`) or when calling `fx[0..2].prepare()`.
  - Inside `FxChain::prepare()` (`core/engine-own/fx/fx_chain.cpp:173-180`), every rack unconditionally calls `makeFxModule(m)` and `inst[m]->prepare(sampleRate, maxBlock)` for all `kNumFxModules = 13` modules (`0..12`, Chorus through Convolve) plus `splitLh`, `splitLmh`, and `splitMs`, even on tier `low` where `kFxReverb | kFxFlanger | kFxHyperDim | kFxBode | kFxConvolve` are masked out by `Quality::forTier(QualityTier::low)` (`core/include/synthyboy/quality.h:112`).
  - Note on `FxPool`: `fxPool.prepare(sampleRate, maxBlockFrames, types)` (`own_engine.cpp:448`, `fx_chain.cpp:80-95`) only allocates pool slots whose `fxi01.type..fxi16.type` parameter is non-zero (`0 B` on default state).
  - Existing tests: `core/tests/test_own_engine.cpp`, `core/tests/test_own_profile.cpp`, `core/tests/test_rt_alloc.cpp`.
- **(d) Redone arithmetic and EXTRA (2) complete memory recomputation (at `sampleRate = 44100`, `maxBlockFrames = 128`, 32-bit ARM):**
  - **Voice pool (`36` voices):** `36 * [sizeof(Voice) 33,128 B + mod/modR (2 * 2,637 * 4 = 21,096 B) + 3 * WtOsc (morphBuf 32,832 B + MorphScratch 24,592 B + history 8,128 B = 65,552 B) + 2 * FilterUnit (2 * FilterImpl 2 * 67,292 B @ 96 kHz floor + 2 * ClassicFilter::comb 2 * 32,768 B = 200,120 B) + 3 * LibOsc (16 * SampleVoice 2,176 B + 16 * GranularVoice 68,480 B + 4 * SpectralVoice 147,872 B + 64 * Layer 16,384 B + filterGain/scratch/ring/extMag 41,744 B = 276,656 B)] = 36 * 1,481,088 B = 53,319,168 B (53.32 MB / 50.85 MiB)`.
  - **Three FX racks (`3 * FxChain`):** `3 * [Chorus 262,144 B + Compressor 7,168 B + Delay 2,097,152 B + FilterModule 200,120 B + Flanger 524,288 B + Reverb (predelay 131,072 + plate 98,304 + hall 524,288 + vintage 1,081,344 + nitrous 618,496 + basin 1,441,792 + fdn16 1,014,784 = 4,910,080 B) + HyperDimension 180,224 B + Bode 883,544 B + Convolve (live + staging = 728,064 B)] = 3 * 9,792,784 B = 29,378,352 B (29.38 MB / 28.02 MiB)`.
  - **Total per `OwnEngine` instance after `prepare()`:** `53.32 MB + 29.38 MB = 82.70 MB (78.87 MiB)` before `ContentHub` (`+0.61 MB` after 1st `setContent()`, up to `+4.90 MB` after 8 `setContent()` calls; or `72.7 MiB` excluding `ClassicFilter::comb` and `Convolve::staging`, matching `[measured: docs/release/CPU-REVIEW-MEMORY.md:326-341]`), and `engine_prepare` takes `95.6 ms (voices) + 73.6 ms (fx) = 169.4 ms` (`[measured: docs/MPC.md:148,327]`).
- **(e) Verdict:** `CONFIRMED` — Both the unconditional allocation in `OwnEngine::prepare()` and its measured 169.4 ms startup time and ~73–83 MB per-instance RAM footprint on MPC Key 61 hold.

---

### Rank 2 — `ContentHub::publish()` lock-free ring race, 8-slot overwrite UAF hazard, and per-instance footprint

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:230,1351`, `core/engine-own/voice/own_content.h:59-106,151-155`.
  - Corrected: `core/engine-own/own_engine.cpp:230-236,1351`, `core/engine-own/voice/own_content.h:101-106,133,151-155`.
- **(b) Verbatim decisive code (`core/engine-own/own_engine.cpp:227-236` and `core/engine-own/voice/own_content.h:101-106,151-155`):**
  ```cpp
  void OwnEngine::setContent (const SynthContent& content)
  {
      ownContent.publish (content);
      std::lock_guard<std::mutex> lock (contentMutex);
      lastContent = content;
  ```
  ```cpp
          const size_t slot = next++ % kRing;
          ring[slot] = std::move (o);
          current.store (ring[slot].get(), std::memory_order_release);
      }
  private:
      static constexpr size_t kRing = 8;
      std::unique_ptr<OwnContent> ring[kRing];
      size_t next = 0;
  ```
- **(c) Surrounding code, EXTRA (1) audio-thread call path and blocking analysis, and existing tests:**
  - **EXTRA (1) Audio-thread call path:**
    1. `pluginProcessReplacing()` (`shell-mpc/synthyboy_mpc.cpp:3184`) → `inst->engine->process()` (`shell-mpc/synthyboy_mpc.cpp:3271`)
    2. `OwnEngine::process()` (`core/engine-own/own_engine.cpp:1319`) → `const OwnContent* contentNow = ownContent.get();` (`core/engine-own/own_engine.cpp:1351`) → `ContentHub::get()` (`core/engine-own/voice/own_content.h:133`, `current.load(std::memory_order_acquire)`).
    3. `OwnEngine::process()` stores `contentNow` into `matrix.setContent(contentNow)` (`own_engine.cpp:1357`) and `globals.lfoContent = contentNow` (`own_engine.cpp:1369`).
    4. `OwnEngine::process()` (`own_engine.cpp:1470`) → `OwnEngine::controlTick()` (`own_engine.cpp:1138`) → `Voice::tick()` (`own_engine.cpp:1201`, `core/engine-own/voice/voice.cpp:349`) caches `&g.lfoContent->lfo[l]` (`voice.cpp:401`), `&g.lfoContent->velocityCurve` (`voice.cpp:789`), and `&g.lfoContent->noteCurve` (`voice.cpp:790`); `ModMatrix::compile()` (`core/engine-own/matrix/mod_matrix.cpp:301`) caches `&content->modCurve[s]`, dereferenced in `ModMatrix::evaluate()` (`mod_matrix.cpp:364`).
  - **EXTRA (1) Can the audio-thread call BLOCK?** **No.** `ContentHub::get()` is a single `std::atomic<const OwnContent*>::load(std::memory_order_acquire)` (`own_content.h:133`).
  - **Surrounding context & what the audit missed:**
    1. `ownContent.publish(content)` at `own_engine.cpp:230` is indeed placed *above* `std::lock_guard<std::mutex> lock(contentMutex)` at `line 233`, and `ContentHub::next` (`own_content.h:153`) is a non-atomic `size_t`. However, `IEngine::setContent` is documented as message-thread-only (`core/include/synthyboy/engine.h:104`), and `shell-mpc` holds `inst->contentMutex` around `setContent` in `applyState` (`shell-mpc/synthyboy_mpc.cpp:1908,1917`).
    2. The 8-slot overwrite UAF hazard is real in code: unlike `ContentSet` (which uses the `contentInUse` atomic hazard pointer at `own_engine.cpp:202-210,1415-1418`), `ContentHub::publish()` blindly overwrites `ring[next++ % 8]`. On the 9th call to `setContent()`, slot 0's `std::unique_ptr<OwnContent>` is destroyed even if a preempted audio thread is still mid-`process()` holding `contentNow` from slot 0.
    3. **Where the audit is partly wrong:** The audit claims `ContentHub` wastes `~4.3 MB/instance` as part of every instance's baseline footprint (`Rank 1` and `Rank 2`). In reality, `ring[0..7]` are `std::unique_ptr<OwnContent>` initialized to `nullptr`, and `OwnEngine::OwnEngine()` (`own_engine.cpp:147`) only calls `publishContent(ContentSet::build(...))`, *not* `ownContent.publish(...)`. Thus `ContentHub` allocates **`0 B`** at engine construction/`prepare()`, and **`613,040 B` (`0.61 MB`)** after the first preset load (`setContent()`), reaching all 8 slots (`4.90 MB`) only after `setContent()` has been called 8 times on that instance.
  - Existing tests: `core/tests/test_threads.cpp`, `core/tests/test_own_engine.cpp`.
- **(d) Redone arithmetic (64-bit / 32-bit):**
  - `sizeof(Point) = 16 B` (`core/shapes/include/synthyboy/shapes/shape.h:25-32`); `sizeof(ShapeData) = 128 * 16 + 8 = 2,056 B` (64-bit) or `2,052 B` (32-bit); `sizeof(CompiledPoint) = 16 + 3 * 8 = 40 B`; `sizeof(CompiledShape) = 8 + 128 * 40 + 2,056 = 7,184 B` (64-bit) / `7,180 B` (32-bit); `sizeof(CompiledModCurve) = 8 + 2048 * 4 = 8,200 B`; `sizeof(OwnContent) = 10 * 7,184 + 66 * 8,200 = 71,840 + 541,200 = 613,040 B (0.613 MB)` per populated slot, or `8 * 613,040 B = 4,904,320 B (4.90 MB / 4.68 MiB)` when all 8 ring slots are filled.
- **(e) Verdict:** `PARTLY WRONG` — Moving `ownContent.publish()` outside `contentMutex` and overwriting `ring[next++ % 8]` without an `inUse` hazard check are confirmed defects in `own_engine.cpp:230` and `own_content.h:103-105`, **but** `ContentHub` does not allocate 4.3–4.9 MB upfront per instance (`ring[8]` starts as 8 null `unique_ptr`s, allocating `0 B` at `prepare()` and `613 KB` on the first `setContent()`).

---

### Rank 3 — `WakeSignal::notify()` from audio thread and `ConvolveModule` `shared_ptr<Impulse>` overwrite

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/dsp/helper_thread.h:36-47`, `core/engine-own/fx/fx_modules.cpp:52,820,989,993`, `core/engine-own/own_engine.cpp:278,1514`.
  - Corrected: `core/engine-own/dsp/helper_thread.h:36-47,71`, `core/engine-own/fx/fx_modules.cpp:52,786-787,817-826,986-995,1019-1020,1056-1058`, `core/engine-own/own_engine.cpp:278,1514`.
- **(b) Verbatim decisive code (`core/engine-own/dsp/helper_thread.h:36-47` and `core/engine-own/fx/fx_modules.cpp:986-995`):**
  ```cpp
      void notify() noexcept
      {
  #if defined(__linux__)
          if (word.exchange (1u, std::memory_order_acq_rel) == 0u)
              futexWake();
  #else
          std::lock_guard<std::mutex> lock (mutex);
          flag = true;
          cv.notify_one();
  #endif
      }
  ```
  ```cpp
      void startBuild (const FxFrame& f, const Key& k) noexcept
      {
          job.self = this;
          job.impulse = f.content->impulse;
          job.key = k;
          pendingKey = k;
          jobBusy.store (1, std::memory_order_release);
          if (!FxWorker::instance().submit (&ConvolveModule::runJob, &job))
              jobBusy.store (0, std::memory_order_release);
      }
  ```
- **(c) Surrounding code, EXTRA (1) audio-thread call paths and blocking analysis, and existing tests:**
  - **EXTRA (1) Audio-thread call paths to `WakeSignal::notify()` and `job.impulse = ...`:**
    - **Path A1 (Active Convolve):** `OwnEngine::process()` (`core/engine-own/own_engine.cpp:1319`) → `FxChain::update()` (`own_engine.cpp:1514`, `core/engine-own/fx/fx_chain.cpp:369`) → `FxModule::update()` (`fx_chain.cpp:462`, `core/engine-own/fx/fx_module.h:125`) → `ConvolveModule::configure()` (`fx_module.h:137`, `core/engine-own/fx/fx_modules.cpp:768`) → `ConvolveModule::startBuild()` (`fx_modules.cpp:787`, `986`) [assigns `job.impulse = f.content->impulse` at `989`] → `FxWorker::submit()` (`fx_modules.cpp:993`, `42`) → `WakeSignal::notify()` (`fx_modules.cpp:52`, `core/engine-own/dsp/helper_thread.h:36`).
    - **Path A2 (Bypassed Convolve on tier `low` with `fxMakeup = true`):** `OwnEngine::process()` (`own_engine.cpp:1319`) → `FxChain::update()` (`own_engine.cpp:1514`, `fx_chain.cpp:369`) → `FxModule::makeup()` (`fx_chain.cpp:437` or `290`, `fx_module.h:107`) → `ConvolveModule::makeupPower()` (`fx_module.h:110`, `fx_modules.cpp:810`) [assigns `specJob.impulse = f.content->impulse` at `820`] → `FxWorker::submit()` (`fx_modules.cpp:824`, `42`) → `WakeSignal::notify()` (`fx_modules.cpp:52`, `helper_thread.h:36`).
    - **Path B (FX pool slot type change via `setParam`):**In CLAP (`clap_process` → `applyHostValue` → `OwnEngine::setParam` on the audio thread at `shell-clap/synthyboy_clap.cpp:319`; in `shell-mpc`, `pluginSetParameter` is host-thread at `shell-mpc/synthyboy_mpc.cpp:3039`), `OwnEngine::setParam()` (`own_engine.cpp:254`) → `fxWake.notify()` (`own_engine.cpp:278`, `helper_thread.h:36`) when `id` matches `fxi01.type..fxi16.type`.
  - **EXTRA (1) Can the audio-thread calls BLOCK?**
    - **`WakeSignal::notify()` on Linux (MPC Key 61 & MPC XL):** **Does NOT block.** It performs an atomic `word.exchange(1u, acq_rel)` and, only on a `0 -> 1` transition, calls `syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1, ...)` (`helper_thread.h:39-40,71`). Per the verification rule (*"a futex wake or an uncontended atomic does not block; a mutex lock, an allocation, a free can"*), a futex wake does not block (though it does incur a kernel syscall transition on 0→1).
    - **`WakeSignal::notify()` on non-Linux (`!defined(__linux__)`, macOS/iOS/Windows):** **CAN BLOCK.** Line 43 executes `std::lock_guard<std::mutex> lock(mutex);` (`helper_thread.h:43`), which blocks if the helper thread holds `mutex` inside `WakeSignal::wait()`.
    - **`job.impulse = f.content->impulse` (`fx_modules.cpp:820,989`):** **CAN BLOCK (`free()`) only in an edge case.** When `FxWorker::submit()` succeeds (the normal case, `kJobs = 64` at `fx_module.h:58`), the worker thread executes `j->impulse.reset()` *before* `m->jobBusy.store(0, release)` (`fx_modules.cpp:1019-1020` and `1056-1058`), so `job.impulse` is already `nullptr` whenever the audio thread sees `jobBusy == 0`. However, if all 64 slots of `FxWorker::jobs` are full and `submit()` returns `false`, `startBuild()` resets `jobBusy` to `0` (`line 994`) while leaving `job.impulse` non-null; if the old `ContentSet` is subsequently retired on the message thread before the next `startBuild()`, `job.impulse` becomes the sole owner (`use_count() == 1`) and the next `job.impulse = f.content->impulse` on the audio thread frees the `Impulse` buffer.
  - Existing tests: `core/tests/test_own_helper_thread.cpp`, `core/tests/test_own_convolve_bypass.cpp`, `core/tests/test_rt_alloc.cpp`.
- **(d) Redone arithmetic:**
  - `FxWorker::kJobs = 64` slots (`core/engine-own/fx/fx_module.h:58`); `WakeSignal::notify()` on Linux makes `0` syscalls when `word` is already `1` and `1` `SYS_futex(FUTEX_WAKE_PRIVATE)` syscall on a `0 -> 1` transition (`helper_thread.h:39-40`).
- **(e) Verdict:** `PARTLY WRONG` — The audio-thread call paths exist (even on tier `low` via bypassed `ConvolveModule::makeupPower()`), the non-Linux `std::mutex` lock in `WakeSignal::notify()` can block, and a failed `FxWorker::submit()` leaves `job.impulse` populated without resetting it on line 994/825; **however**, on Linux (MPC Key 61 and MPC XL) `WakeSignal::notify()` is an atomic exchange + `FUTEX_WAKE_PRIVATE` that does not block, and when `submit()` succeeds `runJob`/`runSpectrumJob` already resets `j->impulse` on the worker thread before clearing `jobBusy`.

---

### Rank 4 — ARM32/ARM64 compiler, visibility, `--gc-sections`, and LTO flags

- **(a) Cited and verified `file:line`:**
  - Cited: `cmake/toolchains/armv7-linux-gnueabihf.cmake:14-15`, `core/CMakeLists.txt:59`, `shell-mpc/CMakeLists.txt:18-24`, `docs/release/CPU-REVIEW-MEMORY.md:32,104,119,352-358`.
  - Corrected: `cmake/toolchains/armv7-linux-gnueabihf.cmake:16`, `core/CMakeLists.txt:59`, `shell-mpc/CMakeLists.txt:18-20,230`, `CMakeLists.txt:359`, `docs/release/CPU-REVIEW-MEMORY.md:32-35,104,119,352-371,462`.
- **(b) Verbatim decisive code (`cmake/toolchains/armv7-linux-gnueabihf.cmake:16`, `core/CMakeLists.txt:59`, `shell-mpc/CMakeLists.txt:18-20,230`):**
  ```cmake
  set(SYNTHYBOY_ARM_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard")
  ```
  ```cmake
  set_target_properties(synthyboy_core PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
  ```
  ```cmake
      CXX_VISIBILITY_PRESET hidden
      C_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON
  ```
  ```cmake
          target_link_options(synthyboy_mpc PRIVATE -static-libstdc++ -static-libgcc -Wl,--gc-sections -Wl,-s)
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - The CMake and toolchain observations are accurate:
    1. `armv7-linux-gnueabihf.cmake:16` uses `-march=armv7-a -mfpu=neon -mfloat-abi=hard` without `-mcpu=cortex-a17` (or `-mtune=cortex-a17`).
    2. `core/CMakeLists.txt:59` (and sibling static libraries `core/filters-serum`, `core/sample-osc`, `core/granular`, `core/spectral`, `core/multisample`, `core/fx-serum-a`, `core/fx-serum-b`) do not set `CXX_VISIBILITY_PRESET hidden`, `-fno-semantic-interposition`, `-fno-math-errno`, `-fno-trapping-math`, or `-ffunction-sections -fdata-sections`.
    3. `shell-mpc/CMakeLists.txt:230` links with `-Wl,--gc-sections` without compiling with `-ffunction-sections -fdata-sections`, and LTO is only enabled on the desktop JUCE target (`CMakeLists.txt:359`).
  - **What the audit missed (why the motivation is partly wrong):**
    1. The audit cites "`137` `__aeabi_idiv`/`__udivsi3` software-division sites" (`[measured: docs/release/CPU-REVIEW-MEMORY.md:33]`) to support a predicted `~5–15%` CPU reduction. However, `CPU-REVIEW-MEMORY.md:33,221-244,462` and `docs/MPC.md:338` explicitly record that the one hot audio-path integer division site — **136 `__aeabi_idivmod` calls per sample in `Compressor::Limiter` (item M1)** — was **already eliminated in Round 13** (`core/engine-own/fx/fx_compressor.h:301`, verified by `core/tests/test_own_compressor_opt.cpp`)! The remaining sites in the binary are cold setup/UI/loader paths or `Bode::process` (`% 129`, masked off on Key 61 tier `low`).
    2. Likewise, the static guard calls inside `Voice::modulatedSlow` (`paramTable()`, `destLawOf()`) noted in Round 12 (`CPU-REVIEW-MEMORY.md:104`) were already hoisted into `Voice::prepare()` pointers (`pidx`, `libk`, `laws`, `infos` at `core/engine-own/voice/voice.cpp:148-152`) in Round 13 (`work/opt-voice`).
    3. `CPU-REVIEW-MEMORY.md:365-371` estimates the remaining gain of `-mcpu=cortex-a17`, `-fno-math-errno`, visibility, and LTO at `0–3%` (flags) and `1–5%` (LTO), and notes they are unmeasured on the device.
  - Existing tests: `core/tests/test_own_compressor_opt.cpp`, `core/tests/test_own_voice_opt.cpp`.
- **(d) Redone arithmetic:**
  - Round 12 integer-divide call sites in `synthyboy_mpc.so`: `29 (__aeabi_idivmod) + 56 (__aeabi_uidivmod) + 38 (__udivsi3) + 14 (__divsi3) = 137 sites` (`[measured: docs/release/CPU-REVIEW-MEMORY.md:33]`), of which the hot `2 * 67 + 2 = 136 calls/sample/band` in `Compressor::Limiter` (`fx_compressor.h:260,267,272,274`) are now `0 calls/sample` (`fx_compressor.h:301-336`).
- **(e) Verdict:** `PARTLY WRONG` — All cited toolchain/CMake flag omissions exist in code (at the corrected line numbers), **but** the audit uses pre-Round-13 hot software-division and `modulatedSlow` figures that were already fixed in `fx_compressor.h:301` and `voice.cpp:148-152` to justify an inflated 5–15% gain claim; the actual remaining flag/LTO gain is unproven and needs an interleaved A/B device benchmark.

---

### Rank 5 — `allocateVoice()` in Kill mode returns any free slot from the 36-voice pool before `killExcess()` runs

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:672-682,852-857,1760`, `docs/MPC.md:353-354`.
  - Corrected: `core/engine-own/own_engine.cpp:667-682,852-856,1193,1470,1574,1670,1760`, `docs/MPC.md:353-354`.
- **(b) Verbatim decisive code (`core/engine-own/own_engine.cpp:672-682,852-856`):**
  ```cpp
      const int P = effectivePolyphony();
      int n = 0;
      forEachActive ([&] (int i) { if (!voices[static_cast<size_t> (i)]->killing()) ++n; });
      Voice* free = freeVoice();
      if (free != nullptr && (killMode || n < P))
          return free; // Kill mode: the overflow is resolved at the next control tick (killExcess)
  ```
  ```cpp
      chosen->start (ns, chosen->lastL(), chosen->lastR());
      activateVoice (static_cast<int> (chosen - voices[0].get()), chosen);
      lastPlayedVoice = chosen;
      // Primed so the samples before the next control boundary already use the new note's settings
      chosen->tick (globals, base.data(), gdelta.data(), matrix, rand);
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - In `OwnEngine::allocateVoice()` (`own_engine.cpp:667-682`), `killMode` is `true` whenever `voice.override < 0.5f && !legato` (the default `"Kill"` mode). Because `if (free != nullptr && (killMode || n < P))` short-circuits on `killMode`, `allocateVoice()` ignores `n < P` (`P = 8` on Key 61 tier `low`) whenever any of the 36 pool slots is free!
  - Tracing `OwnEngine::process()` (`own_engine.cpp:1461-1760`) reveals that the actual execution order is even worse than the audit's mid-block note:
    1. At `controlPos == 0` (`line 1461`), `controlTick()` runs at `line 1470` (calling `killExcess()` at `line 1193` *before* any events for that control period are processed).
    2. At `lines 1570-1575`, `handleEvent()` processes all NoteOn events in the 32-sample period (at `frame == 0` or on the `eventGrid`). Each NoteOn calls `startNote()` → `allocateVoice()` → `chosen->start()` → `chosen->tick()` (`line 856`) → `LibOsc::update()` → `LibOsc::beginType()` → `SpectralVoice::noteOn()` (`core/engine-own/osc/lib_osc.cpp:643,744`), priming all 16 voices immediately.
    3. At `line 1670`, `forEachActive` calls `voices[i]->render(mo, n, controlPos)` for **all 16 active voices** across the first 32-sample control period (`n = 32`), even when all 16 notes arrived at `frame == 0`!
    4. Only at the start of the second control period (`pos = 32`, `controlTick()` → `killExcess()` at `line 1193`) or at block end (`line 1760`) are the 8 oldest voices ghost-killed.
  - Existing tests: `core/tests/test_own_note_storm.cpp`, `core/tests/test_own_engine.cpp`.
- **(d) Redone arithmetic:**
  - With `kPoolVoices = 36` (`own_engine.h:89`) and `effectivePolyphony() = 8` on Key 61 (`own_engine.cpp:657-664`), a 16-note strike from silence allocates `16` free pool voices (`16 <= 36`), runs `16 * (start + tick + 32-sample render)` in period 0 (2× the 8-voice cap), and on `Spectral Vowel Freeze` even an 8-note strike costs `2.6 to 3.1 blocks = 7.5 to 9.0 ms` (`8.9 ms` in `noteon_bench`, `[measured: docs/MPC.md:353-354]`).
- **(e) Verdict:** `CONFIRMED` — Both the code path (which in fact renders all 16 voices for the first 32-sample control period even for `frame == 0` chord strikes because `killExcess()` in `controlTick()` runs before `handleEvent()`) and the measured Spectral strike stall hold.

---

### Rank 6 — `sizeof(Voice) = 33,128 B` hot/cold layout, `2.66 MB` private `MorphScratch`, and ~3,500+ small heap allocations

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/voice/voice.h:229-397`, `core/engine-own/osc/wt_osc.h:520,1223`, `docs/release/CPU-REVIEW-MEMORY.md:25,326-346`.
  - Corrected: `core/engine-own/voice/voice.h:213-397`, `core/engine-own/osc/wt_osc.h:520,1223`, `core/engine-own/osc/morph.h:22-26`, `docs/release/CPU-REVIEW-MEMORY.md:25,126-131,326-346,449`.
- **(b) Verbatim decisive code (`core/engine-own/osc/wt_osc.h:515-524,1222-1224` and `core/engine-own/osc/morph.h:22-26`):**
  ```cpp
      void prepareMorph()
      {
          morphBuf.assign (2 * 2 * (Wavetable::kCycle + 4), 0.0f);
          morphScratch = std::make_unique<MorphScratch>();
          history.assign (static_cast<size_t> (kMaxUnison * kFirDelay), 0.0f);
  ```
  ```cpp
  struct MorphScratch
  {
      float re[Wavetable::kCycle] = {}, im[Wavetable::kCycle] = {};
      float c[Wavetable::kHarmonics + 2] = {}, s[Wavetable::kHarmonics + 2] = {};
  };
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - Inside `Voice` (`core/engine-own/voice/voice.h:213-397`), hot state read in `Voice::tick()` and `Voice::render()` is interleaved with large fixed-size arrays: `oscOutL/R[3][32]`, `subOutL/R[32]`, `noiseOutL/R[32]`, `filtInL/R[2][32]`, `filtOutL/R[2][32]`, `oscMono[3][32]`, `extMono[2][32]`, `extPrev[2][32]`, `extNext[2][32]` (`20 * 32 * 4 = 2,560 B` at `voice.h:295-305`), `oscPart[3]` (`3 * 124 * 4 = 1,488 B` at `voice.h:381`), and `osc[0..2]` + `sub` (`4 * sizeof(WtOsc)`, where each `WtOsc` embeds `OscTick tick` + `OscTick lastT` + unison arrays at `wt_osc.h:1221-1269`).
  - `WtOsc::prepareMorph()` (`wt_osc.h:520`) allocates a private `std::unique_ptr<MorphScratch>` per wavetable oscillator (`voice.cpp:160-162` calls `osc[o].prepareMorph()` for `o = 0..2`; `sub` does not call `prepareMorph()`). Since `buildMorphCycle()` (`wt_osc.h:578`) runs synchronously inside `WtOsc::render()` on a single audio thread, only one oscillator in the entire engine can use `MorphScratch` at any instant.
  - However, `CPU-REVIEW-MEMORY.md:25,128,344,449` explicitly records that **no L1D/L2 cache-miss counter has ever been measured on the device** (*"No miss counter was ever measured; this is the largest unknown of the review"*).
  - Existing tests: `core/tests/test_own_voice_opt.cpp`, `core/tests/test_own_wt_osc_opt.cpp`, `core/tests/test_own_morph_transform.cpp`.
- **(d) Redone arithmetic and EXTRA (2) memory & allocation count recomputation:**
  - **`sizeof(Voice)`:** `33,128 B` on 32-bit ARM (`0x8168`, `[measured: docs/release/CPU-REVIEW-MEMORY.md:25,326]`); `8 active voices * 33,128 B = 265,024 B (258.8 KiB = 8.09 * 32 KiB L1D)`, and `36 pool voices * 33,128 B = 1,192,608 B (1.19 MB)`.
  - **`MorphScratch` total:** `sizeof(MorphScratch) = (2048 + 2048 + 1026 + 1026) * 4 B = 6,148 * 4 B = 24,592 B (24.02 KiB)` per oscillator × `3 oscs/voice * 36 voices = 108 * 24,592 B = 2,655,936 B (2.66 MB / 2.53 MiB)`.
  - **Heap allocation count per engine instance:** Per `Voice`: `1 (Voice) + 2 (mod, modR) + 3 * 3 (WtOsc: morphBuf, morphScratch, history = 9) + 2 * 36 (FilterUnit: 2 FilterImpl + 2 * 16 DelayLines + 2 ClassicFilter::comb = 72) + 3 * 13 (LibOsc: 13 std::vectors = 39) = 123 heap allocations/voice`; across 36 voices = `36 * 123 = 4,428 voice heap allocations` (plus `~240` across the 3 `FxChain` racks = **`~4,668 heap allocations/instance`**, confirming `>3,500`).
- **(e) Verdict:** `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` — All memory sizes (`sizeof(Voice) = 33,128 B`, `2.66 MB` of redundant `MorphScratch`, and `~4,428` per-voice heap allocations) are verified from the declarations, while the L1D/L2 cache-miss and CPU impact is unmeasured and requires the named PMU + `tier_bench` measurement.

---

### Rank 7 — Single `setParam()` bumps `paramGen`, triggering full `blockDirty` work in `OwnEngine::process()`

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:1350-1365,1392-1394,1473`, `core/engine-own/matrix/mod_matrix.cpp:240-326`, `docs/release/CPU-REVIEW-PROFILE.md:129`.
  - Corrected: `core/engine-own/own_engine.cpp:260-261,1350-1365,1392-1394,1473`, `core/engine-own/matrix/mod_matrix.cpp:240-326`, `core/engine-own/voice/note_tuning.h:95-125`, `docs/release/CPU-REVIEW-PROFILE.md:129`.
- **(b) Verbatim decisive code (`core/engine-own/own_engine.cpp:1352-1365,1392-1394` and `core/engine-own/voice/note_tuning.h:95-106`):**
  ```cpp
      if (gen != snapGen || contentNow != snapContent)
      {
          blockDirty = true;
          for (size_t i = 0; i < base.size(); ++i)
              base[i] = params[i].load (std::memory_order_relaxed);
          matrix.setContent (contentNow);
          matrix.compile (base.data());
          snapGen = gen;
          snapContent = contentNow;
          ++baseEpoch;
      }
  ```
  ```cpp
      void compute (float a4Hz, int scaleChoice, int rootNote, const ContentSet* content, bool useMts) noexcept
      {
          const float shift = static_cast<float> (12.0 * std::log2 (std::max (a4Hz, 1.0f) / 440.0f));
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - Confirmed in code: `OwnEngine::setParam()` (`own_engine.cpp:260-261`) increments a single global `paramGen`. In `OwnEngine::process()` (`1352-1365`), any change to `paramGen` sets `blockDirty = true`, loads all `2,637` `params[i]` atomics into `base[i]`, runs `matrix.compile(base.data())` (which scans 64 slots and runs DFS cycle reachability `adj.reaches()` at `mod_matrix.cpp:249-314`), increments `baseEpoch`, calls `noteTuning.compute(...)` (`1392-1394`), and sets `inputsSame = false` for `fx[0..2].update(...)` (`1473,1514`).
  - **What the audit got wrong:**
    1. The audit claims `blockDirty` forces `OwnEngine::process()` to *"recompute all 128 `tunedNotes[]` via `std::log2`"*. Opening `NoteTuning::compute()` (`core/engine-own/voice/note_tuning.h:95-125`) refutes this: line 97 computes `std::log2` **once** (`12.0 * std::log2(max(a4Hz, 1.0f) / 440.0f)`), and lines 104–115 populate `tunedNotes[0..127]` with **128 float additions** (`static_cast<float>(k) + shift` or `(*tab)[k] + shift`). The loop calling `std::log2` 128 times at `note_tuning.h:117-124` is gated by `if (useMts && tuning::hasMtsMaster())`, which is false unless `master.tuning.mts_esp >= 0.5f` AND an external MTS-ESP master library is active (never linked in `core` on MPC: `core/include/synthyboy/engine.h:115`).
    2. While incrementing `baseEpoch` invalidates `Voice`'s `baseEpoch`-keyed caches (`envAllOk`, `oscPart[o].epoch`, `subPart.epoch` at `voice.cpp:847,1194,1360`), downstream heavy setups still short-circuit by value comparison: `WtOsc::update()` (`wt_osc.h:225`) checks `std::memcmp(&t, &lastT, sizeof(OscTick)) == 0`, `ClassicFilter::remember()` (`classic_filters.h:161`) checks filter arguments, and `FxModule::levelGainOf()` (`fx_module.h:222`) caches `dbToGain(db)` by value.
    3. The audit's own cited document `[measured: docs/release/CPU-REVIEW-PROFILE.md:129]` estimates the entire dirty-block overhead at only `10 to 20 k cycles, 0.2 to 0.4 % of that one block`.
  - Existing tests: `core/tests/test_own_voice_opt.cpp`, `core/tests/test_own_engine.cpp`.
- **(d) Redone arithmetic:**
  - `base.size() = 2,637` atomic loads (`10,548 B`); `ModMatrix::kSlots = 64`; `NoteTuning::compute()` without active MTS-ESP = `1` `std::log2` call + `128` float additions (`note_tuning.h:97,104-115`); estimated dirty-block cost = `10,000..20,000 cycles / 4,666,667 cycles/block = 0.21%..0.43%` (`[measured: docs/release/CPU-REVIEW-PROFILE.md:129]`).
- **(e) Verdict:** `PARTLY WRONG` — `blockDirty` does copy 2,637 atomics, recompile `ModMatrix`, bump `baseEpoch`, and set `inputsSame = false`, **but** `NoteTuning::compute()` runs `1` `std::log2` + 128 additions (not 128 `std::log2` calls), oscillator/filter value caches still short-circuit unchanged modules, and the consequence during Q-Link sweeps is unproven beyond the 0.2–0.4% estimate in `CPU-REVIEW-PROFILE.md:129`.

---

### Rank 8 — `SB_NEON` disabled on Apple ARM64, missing FTZ/DAZ in `shell-clap` / `OwnEngine::process()`, and inconsistent `fp-contract=off` on Linux ARM64

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/dsp/simd.h:13-35`, `shell-clap/synthyboy_clap.cpp:320-415`, `docs/release/CPU-REVIEW-MEMORY.md:36`.
  - Corrected: `core/engine-own/dsp/simd.h:12-28`, `core/filters-serum/src/filter_lanes.h:17-23`, `shell-clap/synthyboy_clap.cpp:225-393`, `docs/release/CPU-REVIEW-MEMORY.md:36,309-318,357`.
- **(b) Verbatim decisive code (`core/engine-own/dsp/simd.h:12-26`):**
  ```cpp
  // 32 bit ARM, and AArch64 on Linux (the MPC XL). Not Apple's arm64 (macOS, iOS): there the scalar code and its compiler contraction stay as they were.
  #if defined(__arm__) || (defined(__aarch64__) && defined(__linux__))
   #define SB_ARM_LINUX_OR_32 1
  #else
   #define SB_ARM_LINUX_OR_32 0
  #endif

  #if ((SB_ARM_LINUX_OR_32 && (defined(__ARM_NEON) || defined(__ARM_NEON__))) || defined(SB_TEST_NEON)) && !defined(SB_NO_NEON)
   #include <arm_neon.h>
   #define SB_NEON 1
  #else
   #define SB_NEON 0
  #endif
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - **Apple ARM64 NEON gate:** `core/engine-own/dsp/simd.h:12-26` (and `core/filters-serum/src/filter_lanes.h:17-23` for `SYNTHYBOY_FILTER_NEON`) explicitly exclude Apple ARM64 (`!defined(__linux__)`). As the comment on `simd.h:12` explains, this was a deliberate choice so macOS reference tests kept their existing scalar + FMA-contracted bit patterns while unit tests (`synthyboy_test_own_warp_lanes`, `synthyboy_test_neon_cmp`, `synthyboy_test_own_filter_unit_lanes`, etc. in `core/tests/CMakeLists.txt:73-152`) test NEON on macOS by defining `SB_TEST_NEON=1` and `-ffp-contract=off`. Note that `core/sample-osc/src/kernels.cpp:7`, `core/granular/src/kernels.cpp:7`, and `core/spectral/src/kernels.cpp:7` check `defined(__ARM_NEON)` directly, so content-oscillator NEON kernels *are* enabled on Apple ARM64, whereas `core/engine-own` and `core/filters-serum` NEON paths are disabled on iOS/macOS.
  - **FTZ/DAZ in `shell-clap` and `OwnEngine::process()`:** Confirmed that `clap_process` (`shell-clap/synthyboy_clap.cpp:225-393`) and `OwnEngine::process()` (`core/engine-own/own_engine.cpp:1319-1767`) do not set `FPSCR`/`FPCR`/`MXCSR` flush-to-zero bits, whereas `shell-mpc/synthyboy_mpc.cpp:3217-3218`, `shell-juce/PluginProcessor.cpp:346`, and `bench/tier_bench.cpp:150-157` do (`[measured: docs/release/CPU-REVIEW-MEMORY.md:309-318]`). Note, however, that the own engine includes explicit software denormal guards (`flushDenormal(v)` at `core/engine-own/dsp/util.h:24-27` and bit-pattern `sanitize(x)` at `core/filters-serum/src/filter_dsp.h:25-32`) in its filter, reverb, and delay feedback states, which bounds software denormal exposure in `shell-clap`.
  - **Inconsistent `fp-contract=off` on Linux ARM64:** Confirmed by `[measured: docs/release/CPU-REVIEW-MEMORY.md:36,357]`: `#pragma GCC optimize("fp-contract=off")` is applied in `simd.h`, `wt_osc.h`, `morph.cpp`, `classic_filters.h`, `fx_effects.h`, `filter.cpp`, and `kernels.cpp`, while other TUs (`utility.cpp` with 53 `fmadd`, `hyper_dimension.cpp` with 54 `fmadd`, `mod_matrix.cpp`, `envelope.h`, `fx_makeup.cpp`) compile with default FMA contraction on ARM64.
  - Existing tests: `core/tests/CMakeLists.txt:73-152` (`synthyboy_test_own_warp_lanes`, `synthyboy_test_neon_cmp`, `synthyboy_test_own_peak`, `synthyboy_test_own_master_stage`, `synthyboy_test_own_filter_unit_lanes`, `synthyboy_test_own_mix_lanes`, `synthyboy_test_own_morph_transform`, `synthyboy_test_own_wt_osc_opt`).
- **(d) Redone arithmetic:**
  - `SB_NEON = 0` and `SYNTHYBOY_FILTER_NEON = 0` on Apple ARM64 (`simd.h:25`, `filter_lanes.h:22`), disabling 4-wide NEON wavetable/warp/classic-filter/chorus/master loops and 2-wide `NeonLanes` Serum filter pair kernels; on Linux ARM64, `53` (`Utility::process`) + `54` (`HyperDimension::process`) = `107` fused `fmadd` instructions remain in the top two uncontracted functions (`[measured: docs/release/CPU-REVIEW-MEMORY.md:357]`).
- **(e) Verdict:** `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` — All three code facts hold (with `synthyboy_clap.cpp:225-393` corrected and noting that content oscillators already use NEON on Apple ARM64 and software `flushDenormal`/`sanitize` protects feedback loops), while the iOS speedup, CLAP denormal cost, and XL FMA gain require the named benchmarks.

---

### Rank 9 — `tier_bench` measurement methodology (`SCHED_OTHER`, `steady_clock` overhead, no PMU or thermal logging)

- **(a) Cited and verified `file:line`:**
  - Cited: `docs/MPC.md:321,336,350,540`, `docs/release/CPU-REVIEW-MEMORY.md:344,449`, `core/engine-own/dsp/section_timing.cpp:25`.
  - Corrected: `bench/tier_bench.cpp:11-12,435,557,582-776`, `core/engine-own/dsp/section_timing.cpp:25,57-73`, `docs/MPC.md:321,336,350,540`, `docs/release/CPU-REVIEW-MEMORY.md:344,449`.
- **(b) Verbatim decisive code (`bench/tier_bench.cpp:11-13` and `core/engine-own/dsp/section_timing.cpp:23-26`):**
  ```cpp
  //   --core N              Pin the worker thread to CPU N (Linux sched_setaffinity). Nothing else of the machine is touched:
  //                         no scheduling class, no priority, no frequency governor.
  ```
  ```cpp
  uint64_t ownSectionNowNs() noexcept
  {
      return static_cast<uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - Confirmed in `bench/tier_bench.cpp` and `core/engine-own/dsp/section_timing.cpp`:
    1. `tier_bench.cpp` sets CPU affinity (`sched_setaffinity`) when `--core N` is passed (`lines 11-12, 134-147`) but never calls `sched_setscheduler(SCHED_FIFO)`. `docs/MPC.md:321` documents that running under `SCHED_OTHER` on core 3 alongside the MPC application inflates p99 by `+20 to +25 points`, and `docs/MPC.md:350` documents `18%` day-to-day drift on Key 61 between Round 12 and Round 13. (Note: `docs/MPC.md:336,350` shows the team already mitigated this during Round 12/13 device sessions via an external `/tmp/rtrun` `SCHED_FIFO 20` wrapper on core 2 and interleaved A/B runs, though neither is built into `tier_bench.cpp`.)
    2. `section_timing.cpp:25` calls `std::chrono::steady_clock::now()`, which costs `~1.3 µs` per scope on Key 61 (`[measured: docs/MPC.md:540]`). Note that `section_timing.cpp:57-73` (`ownSectionClockCostNs()`) and `tier_bench.cpp:435,582-601` already measure the median clock read cost and subtract `childCalls * clockNs` from parent sections to prevent nested scopes from distorting parent totals (`docs/MPC.md:540`), though the actual runtime overhead while `--sections` is active remains ~20–25 µs/block.
    3. `tier_bench.cpp` logs `/sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq` (`lines 557, 567`), but does not read `/sys/class/thermal/thermal_zone*/temp` or `perf_event_open` PMU counters (`[measured: docs/release/CPU-REVIEW-MEMORY.md:344,449]`), and `--micro` (`tier_bench.cpp:603-776`) covers only Spectral, Granular, and SampleVoice kernels.
  - Existing tests: `core/tests/test_own_profile.cpp`.
- **(d) Redone arithmetic:**
  - At `~1.3 µs` per `OwnSectionScope` (`[measured: docs/MPC.md:540]`), ~16–20 section scopes per 128-sample block add `16..20 * 1.3 µs = 20.8..26.0 µs/block` (`0.72%..0.90%` of the `2,902.5 µs` block budget); `SCHED_OTHER` interference on core 3 adds `+20..+25%` of the block to p99 (`[measured: docs/MPC.md:321]`), and day-to-day drift on Key 61 reached `(294.75 - 249.49) / 249.49 = +18.14%` (`[measured: docs/MPC.md:350]`).
- **(e) Verdict:** `CONFIRMED` — Both the code in `tier_bench.cpp` / `section_timing.cpp` and the measured `+20–25%` p99 noise, `18%` drift, and `1.3 µs/scope` clock cost in `docs/MPC.md:321,350,540` hold.

---

### Rank 10 — Wavetable background builder thread (`Bank::run`) misses `runHelperThreadStartHook()`, polls at 100 Hz forever, and shares `latest[6]` across instances

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/osc/wavetable_bank.cpp:372,472-497`.
  - Corrected: `core/engine-own/osc/wavetable_bank.cpp:362,372,472-497,552-570`.
- **(b) Verbatim decisive code (`core/engine-own/osc/wavetable_bank.cpp:372,472-497`):**
  ```cpp
      std::atomic<int> latest[kRequestSlots] {};
  ```
  ```cpp
      void run() noexcept
      {
  #if defined(__linux__)
          ::setpriority (PRIO_PROCESS, static_cast<id_t> (::syscall (SYS_gettid)), 10);
  #endif
          static constexpr int kOrder[kNumChoices] = { 0, 5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 14, 15, 13 };
          int nextDefault = 0;
          while (!stop.load (std::memory_order_relaxed))
          {
              const int req = takeRequested();
              if (req >= 0) { buildOne (req); continue; }
              while (nextDefault < kNumChoices && state[kOrder[nextDefault]].load (std::memory_order_acquire) == 2)
                  ++nextDefault;
              if (nextDefault < kNumChoices) { buildOne (kOrder[nextDefault]); continue; }
              std::this_thread::sleep_for (std::chrono::milliseconds (10));
          }
      }
  ```
- **(c) Surrounding code, EXTRA (1) audio-thread call path and blocking analysis, and existing tests:**
  - **EXTRA (1) Audio-thread call paths into `wavetable_bank.cpp`:**
    1. `OwnEngine::process()` (`core/engine-own/own_engine.cpp:1319`) → `builtinWavetableIfReady(choice)` (`own_engine.cpp:1434`, `core/engine-own/osc/wavetable_bank.cpp:552-557`) → `g_bank.ready[choice].load(std::memory_order_acquire)`.
    2. `OwnEngine::setParam()` (`core/engine-own/own_engine.cpp:254`; called on the audio thread in CLAP `synthyboy_clap.cpp:319`, or host thread in MPC `synthyboy_mpc.cpp:3039`) → `requestBuiltinWavetable(k, ...)` (`own_engine.cpp:265`, `wavetable_bank.cpp:566-570`) → `g_bank.latest[slot].store(choice, std::memory_order_relaxed)`.
    3. Note: `startBuiltinBankBackground()` (`own_engine.cpp:403` → `wavetable_bank.cpp:592`) and `stopBuiltinBankBackground()` (`own_engine.cpp:154` → `wavetable_bank.cpp:597`) are called only in `OwnEngine::prepare()` and `OwnEngine::~OwnEngine()`, **not** on the audio thread.
  - **EXTRA (1) Can the audio-thread calls BLOCK?** **No.** `builtinWavetableIfReady()` is a single `memory_order_acquire` atomic load, and `requestBuiltinWavetable()` is a single `memory_order_relaxed` atomic store.
  - **Verification of the three code claims on `Bank::run()` (`wavetable_bank.cpp:472-497`):**
    1. Unlike `OwnEngine::startFxWorker()` (`own_engine.cpp:167`) and `FxWorker::loop()` (`fx_modules.cpp:61`), which call `runHelperThreadStartHook("synthyboy-fx-pool")` / `runHelperThreadStartHook("synthyboy-convolve")` to demote inherited `SCHED_FIFO` policy and pin helper threads to non-audio cores `0-1`, `Bank::run()` only calls `::setpriority(..., 10)` (`line 475`) and **never calls `runHelperThreadStartHook()`**! (And `::setpriority` has no effect on a thread that inherited `SCHED_FIFO` from its parent.)
    2. Even after all 15 built-in tables reach `state[c] == 2` (`nextDefault == kNumChoices = 16`), the `while (!stop.load())` loop never exits or waits on a `WakeSignal`; it executes `std::this_thread::sleep_for(std::chrono::milliseconds(10))` (`line 495`, 100 wakeups/second) for the entire lifetime of the engine instance.
    3. `g_bank` is a process-wide singleton (`wavetable_bank.cpp:544`) with a single 6-element array `std::atomic<int> latest[kRequestSlots]` (`kRequestSlots = 6`, `lines 362, 372`), so multiple `OwnEngine` instances overwrite each other's `latest[0..5]` slots.
  - Existing tests: `core/tests/test_own_helper_thread.cpp` (tests `FxWorker` and `fxWorker`, which is why it did not catch `Bank::run` omitting `runHelperThreadStartHook`!).
- **(d) Redone arithmetic:**
  - `1000 ms / 10 ms = 100 wakeups/s` (`wavetable_bank.cpp:495`) indefinitely after the `15` built-in wavetables finish building; `kRequestSlots = 6` shared across all instances (`wavetable_bank.cpp:362,372`).
- **(e) Verdict:** `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` — All three code defects in `wavetable_bank.cpp:372,472-497` are verified verbatim; whether `Bank::run()` inherits an audio core or `SCHED_FIFO` on MPC Key 61 and causes measurable jitter requires the named `ps -eLo` check on the device.

---

### Rank 11 — `quality.oversample_4x` is not capped on tier `low`, and tier `low` masks 5 character FX on 31 factory presets without an economy DSP fallback

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:1449`, `core/src/serum_import.cpp:636`, `docs/release/CPU-REVIEW-PROFILE.md:56`, `docs/MPC.md:453-457`.
  - Corrected: `core/engine-own/own_engine.cpp:1449`, `core/serum-import/serum_import.cpp:636` (**the audit cited a non-existent path `core/src/serum_import.cpp:636`**), `core/engine-own/osc/wt_osc.h:640,654`, `bench/tier_bench.cpp:162`, `docs/release/CPU-REVIEW-PROFILE.md:56`, `docs/MPC.md:453-474`.
- **(b) Verbatim decisive code (`core/engine-own/own_engine.cpp:1449` and `core/serum-import/serum_import.cpp:634-637`):**
  ```cpp
      globals.oversample = ultraRender.load (std::memory_order_relaxed) || (fxOversampleIndex >= 0 && base[static_cast<size_t> (fxOversampleIndex)] >= 0.5f);
  ```
  ```cpp
          else if (u.name == "kParamOversampling")
          {
              store ("quality.oversample_4x", v >= 2.0 ? 1 : 0);
          }
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - In `OwnEngine::process()` (`own_engine.cpp:1449`), `globals.oversample` is set to `true` whenever `quality.oversample_4x >= 0.5f`, regardless of `qualityTier` or `globals.economy & kEconomyWarp`.
  - In `Voice::tick()` (`core/engine-own/voice/voice.cpp:1293`), `t.oversample = g.oversample; t.lowWarp = (g.economy & kEconomyWarp) != 0;`.
  - In `WtOsc::render()` (`core/engine-own/osc/wt_osc.h:640,654`), `const bool warped = tick.oversample || warpNeedsOversampling(tick.warp[0]) || warpNeedsOversampling(tick.warp[1]);` and `const int os = tick.lowWarp ? kOversampleLow : kOversample;`. Thus, on tier `low` (`lowWarp == true`), `quality.oversample_4x = 1` does not run at 4×, but it **does** force `warped = true` on every active wavetable oscillator (even unwarped oscillators that normally run at 1× in `renderPlain`), forcing them through the 2× oversampled path (`renderWarped`) with a 32-tap FIR decimator per unison voice!
  - `CPU-REVIEW-PROFILE.md:56` notes that `0` of the 160 factory presets set `quality.oversample_4x = 1`, while `core/serum-import/serum_import.cpp:636` sets `quality.oversample_4x = 1` on any imported Serum patch with `kParamOversampling >= 2.0`.
  - For the 5 masked effects (`kFxReverb | kFxFlanger | kFxHyperDim | kFxBode | kFxConvolve` in `core/include/synthyboy/quality.h:112`), `bench/tier_bench.cpp:162` confirms that 31 factory presets use at least one of them, and `docs/MPC.md:468-474` confirms that `Quality::fxMakeup` replaces their loudness with a broadband gain while leaving their spatial/modulation character absent (unless `reverb.force_enable` is set at `own_engine.cpp:1472`).
  - Existing tests: `core/tests/test_own_profile.cpp`, `core/tests/test_own_fx_makeup.cpp`, `core/tests/test_own_convolve_bypass.cpp`.
- **(d) Redone arithmetic:**
  - On tier `low` (`kOversampleLow = 2`, `kTapsLow = 32` at `wt_osc.h:158-159`), setting `quality.oversample_4x = 1` on an unwarped oscillator multiplies per-sample oscillator evaluations by `2x` and adds `32` FIR multiply-accumulates per output sample per unison voice (`3 * 32 = 96 MACs/sample` at 3 unison voices).
- **(e) Verdict:** `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` — Both code paths hold (with `core/serum-import/serum_import.cpp:636` corrected from `core/src/serum_import.cpp:636`, and noting that tier `low` runs forced-oversampled oscillators at `2x` rather than `4x`), while the CPU impact on imported Serum patches requires the named `tier_bench` measurement.

---

### Rank 12 — Redundant `left`/`rightOut` zeroing, silent-bus `voiceCountGain` and `runBus()` passes, `scratchRight.assign()` allocation hazard, and `static const int convolveOn` on audio thread

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:1324,1335-1339,1672-1737,1747-1749,1799`, `docs/release/CPU-REVIEW-PROFILE.md:215-224`.
  - Corrected: `core/engine-own/own_engine.cpp:1324-1339,1672-1737,1747-1749,1799`, `docs/release/CPU-REVIEW-PROFILE.md:215-224,242-246`.
- **(b) Verbatim decisive code (`core/engine-own/own_engine.cpp:1324-1339,1736-1737,1799`):**
  ```cpp
      std::fill (left, left + numFrames, 0.0f);
      if (!prepared) { ... return; }
      float* rightOut = right;
      if (rightOut == nullptr)
      {
          if (static_cast<int> (scratchRight.size()) < numFrames)
              scratchRight.assign (static_cast<size_t> (numFrames), 0.0f);
          rightOut = scratchRight.data();
      }
      std::fill (rightOut, rightOut + numFrames, 0.0f);
  ```
  ```cpp
          std::copy (mixL[0], mixL[0] + n, left + pos);
          std::copy (mixR[0], mixR[0] + n, rightOut + pos);
  ```
  ```cpp
      static const int convolveOn = findParamIndex (hashKey ("convolve.on"));
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - All five code observations in `core/engine-own/own_engine.cpp` are verified verbatim:
    1. Lines `1324` and `1339` zero `left[0..numFrames-1]` and `rightOut[0..numFrames-1]` at the start of `OwnEngine::process()`, and then lines `1736-1737` (`std::copy(mixL[0], mixL[0] + n, left + pos)`) overwrite every frame of `left` and `rightOut` across the `while (pos < numFrames)` loop. Moving `std::fill` inside `if (!prepared)` eliminates the redundant writes on all prepared blocks.
    2. Lines `1672-1683` apply `simd::mulRamp(mixL[t], ...)` and `simd::mulRamp(mixR[t], ...)` across all 4 stereo buses `t = 0..3` whenever `voiceCountGainPrev != 1.0f || voiceCountGainCur != 1.0f`, even when buses 1, 2, and 3 received no voice output.
    3. Lines `1696-1732` call `runBus(1)` and `runBus(2)` every sub-block `n`, executing the `for (int i = 0; i < n; ++i)` bus-gain lerp into `outL`/`outR`, `dlBusOut[b-1].process`, and the `for (int i = 0; i < n; ++i)` accumulation into `mixL[to]`/`mixR[to]` even when Bus 1 and Bus 2 are completely silent and their FX chains are inactive.
    4. Lines `1335-1336` call `scratchRight.assign(numFrames, 0.0f)` on the audio thread if a mono host passes `numFrames > static_cast<int>(scratchRight.size())` (where `prepare()` pre-allocated `std::max(maxBlockFrames, 4096)` at `own_engine.cpp:393`).
    5. Line `1799` inside `OwnEngine::publishStats()` (called from `OwnEngine::process()` at `line 1764`) declares `static const int convolveOn = findParamIndex(hashKey("convolve.on"));`, which uses a C++ thread-safe static initialization guard (`__cxa_guard_acquire`) on the audio thread instead of a pre-resolved member index like `fxReverbForceIndex` (`own_engine.cpp:96`).
  - Note on CPU magnitude: `[measured: docs/release/CPU-REVIEW-PROFILE.md:215-224,242-246]` (items S7 and S11) estimated the silent-bus and buffer-fill work at `0.06 to 0.07%` + `~0.1%` (`~0.15–0.2%` of the block total), smaller than the audit's `~1–2%` estimate.
  - Existing tests: `core/tests/test_own_master_stage.cpp`, `core/tests/test_rt_alloc.cpp`.
- **(d) Redone arithmetic:**
  - Per 128-frame stereo block, redundant `std::fill` writes `2 * 128 * 4 B = 1,024 B`; silent `voiceCountGain` on buses 1–3 reads/writes `3 * 2 * 128 * 4 B = 3,072 B`; silent `runBus(1)` and `runBus(2)` touch `2 * 4 * 128 * 4 B = 4,096 B` (`8,192 B/block` of redundant buffer traffic total).
- **(e) Verdict:** `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` — All five code observations at the cited lines in `own_engine.cpp` are exact, while the CPU gain on Key 61 (estimated at ~0.15–0.2% in `CPU-REVIEW-PROFILE.md:217,246` vs the audit's 1–2%) requires the named `tier_bench --sections` measurement.

---

### Rank 13 — MPC shell boundary: `EventInbox::push` spinlock, lazy static `noteDiagnostics()`, unbatched `applyState()`, and unconditional `outputPeak` / `spectrumRing.push`

- **(a) Cited and verified `file:line`:**
  - Cited: `shell-mpc/event_inbox.h:22`, `shell-mpc/synthyboy_mpc.cpp:1891-1925,2456,3292-3298`, `shell-mpc/output_peak.h:136`.
  - Corrected: `shell-mpc/event_inbox.h:22,35-42`, `shell-mpc/output_peak.h:72-80,134-138`, `shell-mpc/spectrum.h:30-39`, `shell-mpc/synthyboy_mpc.cpp:1891-1925,2425-2471,3243,3297-3302`.
- **(b) Verbatim decisive code (`shell-mpc/event_inbox.h:21-24`, `shell-mpc/output_peak.h:134-138`, `shell-mpc/synthyboy_mpc.cpp:3297-3302`):**
  ```cpp
      void push (const synthyboy::Event& e) noexcept
      {
          while (producers.test_and_set (std::memory_order_acquire)) {}
  ```
  ```cpp
  inline NoteDiagnostics& noteDiagnostics() noexcept
  {
      static NoteDiagnostics nd;
      return nd;
  }
  ```
  ```cpp
      if (out0 && out1 && sampleFrames > 0)
          synthyboy_mpc::outputPeak().note (out0, out1, sampleFrames);
      if (out0 && out1 && sampleFrames > 0)
          inst->spectrumRing.push (out0, out1, sampleFrames);
  ```
- **(c) Surrounding code, EXTRA (1) audio-thread call paths and blocking analysis, and existing tests:**
  - **EXTRA (1) Call paths and blocking analysis for each of the four sub-items:**
    1. **`EventInbox::push()` vs `EventInbox::pop()`:**
       - **Audio callback path:** `pluginProcessReplacing()` (`shell-mpc/synthyboy_mpc.cpp:3184`) → `inst->eventInbox.pop(taken)` (`synthyboy_mpc.cpp:3243`, `shell-mpc/event_inbox.h:35-42`). `EventInbox::pop()` only reads/writes `tail` and `head` atomics and **never touches `producers`**, so the audio callback **cannot block** on `EventInbox`.
       - **MIDI dispatcher path:** `dispatch()` → `case effProcessEvents:` (`shell-mpc/synthyboy_mpc.cpp:2425`) → `inst->eventInbox.push(ev)` (`synthyboy_mpc.cpp:2471`, `shell-mpc/event_inbox.h:21-33`). `EventInbox::push()` spins on `while (producers.test_and_set(std::memory_order_acquire)) {}` (`line 22`), which **can block/spin** if the host invokes `effProcessEvents` concurrently from two threads of different RT priorities.
    2. **`noteDiagnostics().noteOn()`:**
       - **Call path:** `dispatch()` → `case effProcessEvents:` (`shell-mpc/synthyboy_mpc.cpp:2425`) → `synthyboy_mpc::noteDiagnostics().noteOn(hostVel)` (`synthyboy_mpc.cpp:2456`, `shell-mpc/output_peak.h:134-138`).
       - **Can it BLOCK?** On the very first call in the process, `static NoteDiagnostics nd;` (`output_peak.h:136`) acquires the C++ static initialization guard (`__cxa_guard_acquire`, which can block if contended, though `NoteDiagnostics` has a trivial atomic constructor); subsequent calls execute 2 relaxed atomic stores (`lastNoteOnVelocity`, `noteOnCount` at `output_peak.h:112-116`) and cannot block.
    3. **`applyState()` (`shell-mpc/synthyboy_mpc.cpp:1891-1925`):**
       - **Call path:** Runs on the host UI/state thread (`effSetChunk` / preset browser load), **not** on the audio thread (`pluginProcessReplacing`).
       - It loops `for (size_t i = 0; i < count; ++i)` (`lines 1897-1905`) calling `inst->engine->setParam(table[i].id, values.plain[i])` `2,637` times (incrementing `paramGen` 2,637 times), then locks `inst->contentMutex` (`line 1908`) and calls `inst->engine->setContent(inst->content)` (`line 1917`). Because the 2,637 `setParam` writes are not published atomically with `setContent()`, an audio block (`OwnEngine::process()`) firing mid-`applyState()` snapshots a torn parameter array (half old preset, half new preset).
    4. **`outputPeak().note()` and `inst->spectrumRing.push()`:**
       - **Audio callback path:** `pluginProcessReplacing()` (`shell-mpc/synthyboy_mpc.cpp:3184`) → `synthyboy_mpc::outputPeak().note(out0, out1, sampleFrames)` (`line 3298` → `shell-mpc/output_peak.h:72-80`) and `inst->spectrumRing.push(out0, out1, sampleFrames)` (`line 3301` → `shell-mpc/spectrum.h:33-39`).
       - **Can they BLOCK?** **No** (both perform NEON/scalar peak math and relaxed/release atomic stores).
       - Note on cache lines: `SpectrumRing::kSize = 8192` floats (`32,768 B = 32 KB`, `spectrum.h:30,62`), so each 128-frame block writes `128 * 4 B = 512 B` (8 cache lines), wrapping the 32 KB buffer over 64 blocks (186 ms), rather than touching all 32 KB in a single block.
  - Existing tests: `shell-mpc/tests/output_stage_lanes_host.cpp`, `core/tests/test_events.cpp`.
- **(d) Redone arithmetic:**
  - `applyState()` executes `2,637` `atomic::store(relaxed)` + `2,637` `paramGen.fetch_add(1, release)` atomic RMW operations per preset load (`synthyboy_mpc.cpp:1897-1905`, `own_engine.cpp:260-261`); `spectrumRing.push` writes `128 floats * 4 B = 512 B/block` (`8` 64-byte cache lines/block) into a `8,192 * 4 B = 32,768 B (32 KiB)` ring (`spectrum.h:30-39`).
- **(e) Verdict:** `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` — All four shell code patterns exist at the cited locations (noting that `EventInbox::pop` on the audio callback is lock-free and `spectrumRing.push` touches 512 B/block of the 32 KB ring); whether mid-load parameter tearing in `applyState` or unconditional meter/spectrum writes cause measurable audio-thread cost requires the named device test.

---

### Rank 14 — Silent sustained voices stay active, `controlTick()` scans 36 voices for `sustainPending`, and `writeReadout()` runs `std::exp`, `std::log10`, and 300–700 atomic stores per block

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:1190-1192,1901-2034`, `docs/release/CPU-REVIEW-PROFILE.md:204-213`, `docs/release/CPU-REVIEW-MEMORY.md:139`.
  - Corrected: `core/engine-own/voice/voice.cpp:1108-1118`, `core/engine-own/own_engine.cpp:1190-1192,1901-2034`, `docs/release/CPU-REVIEW-PROFILE.md:198-213`, `docs/release/CPU-REVIEW-MEMORY.md:139`.
- **(b) Verbatim decisive code (`core/engine-own/own_engine.cpp:1190-1192,1906-1917`):**
  ```cpp
      for (size_t i = 0; i < voices.size(); ++i)
          if (sustainPending[i] != 0 && (!voices[i]->isActive() || voices[i]->killing()))
              sustainPending[i] = 0;
  ```
  ```cpp
      const float release = sampleRate > 0.0 && lastBlockFrames > 0 ? std::exp (-static_cast<float> (lastBlockFrames) / (0.3f * static_cast<float> (sampleRate))) : 0.0f;
      for (int s = 0; s < LiveValues::kMaxStrips; ++s)
      {
          if (s < 7 && ! sourceMeters)
          {
              stripShown[s] = 0.0f;
              liveStripPeak[s].store (0.0f, std::memory_order_relaxed);
              continue;
          }
          const float pk = stripBlockPeak[s];
          const float level = pk > 1.0e-3f ? std::clamp (1.0f + 20.0f * std::log10 (pk) / 60.0f, 0.0f, 1.0f) : 0.0f;
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - **Part 1 (Zero-amplitude sustained voices):** Confirmed in `Voice::tick()` (`core/engine-own/voice/voice.cpp:1108-1118`): a voice only deactivates when `env[0].finished() || killFading`. While a note is held in the sustain stage (`env[0].finished() == false`), even if `env1.sustain == 0` and `ampCur == 0.0f && ampPrev == 0.0f` after decay finishes (documented on `Ring Mod Gamelan` after 1.5 s in `[measured: docs/release/CPU-REVIEW-PROFILE.md:208-210]`), the voice stays active and renders all oscillators and filters into a zero-gain output.
  - **Part 2 (`sustainPending` scan — audit missed the short-circuit):** In `OwnEngine::controlTick()` (`own_engine.cpp:1190-1192`), `sustainPending` is a contiguous `std::vector<uint8_t>` of **36 bytes** (allocated at `own_engine.cpp:432`), and `if (sustainPending[i] != 0 && ...)` tests `sustainPending[i] != 0` *first*! When no voice is sustain-held (the common case), the loop **never dereferences `voices[i]`** — it scans 36 contiguous bytes inside a single L1 cache line (~10–20 cycles per 32 samples).
  - **Part 3 (`writeReadout()` — audit missed the `sourceMeters` and `pk > 1e-3f` guards):**
    1. `writeReadout()` (`own_engine.cpp:1901-2034`) does compute `1` `std::exp` per block (`line 1906`) and executes `334` relaxed atomic stores when idle / no targets (`334 + activeTargetCount + 3 * sum(numGrains)` when active).
    2. **However**, `std::log10` at `line 1916` is guarded by **two** checks that the audit ignored: first, `if (s < 7 && !sourceMeters) continue;` (`lines 1909-1914`, added in Round 13 S3, commit `eb5e24e8`) skips source strips `0..6` entirely unless a UI has called `requestMeters()` within the last 256 blocks; second, `pk > 1.0e-3f ? ... std::log10(pk) : 0.0f` (`line 1916`) skips `std::log10` whenever a strip is silent (`pk <= -60 dBFS`). Consequently, `std::log10` is called **0 times when idle**, at most **3 times per block** (Bus 1, Bus 2, Master) during playback when the Mixer UI is closed, and up to 10 times only when the Mixer UI is actively open and all 10 strips exceed `-60 dBFS`.
    3. Moreover, `[measured: docs/release/CPU-REVIEW-MEMORY.md:139]` and `CPU-REVIEW-PROFILE.md:198-202` record that the entire `writeReadout()` function costs only `0.1 to 0.2%` of the block and that throttling it to every 4th block was explicitly rejected because it breaks `stripShown` ballistics by up to 6% and breaks `core/tests/test_own_displays.cpp`.
  - Existing tests: `core/tests/test_own_displays.cpp`, `core/tests/test_own_osc_displays.cpp`.
- **(d) Redone arithmetic:**
  - `sustainPending` vector size = `36 * sizeof(uint8_t) = 36 B` (< 1 64-byte cache line); atomic stores in `writeReadout()` (`own_engine.cpp:1907-2032`, `core/include/synthyboy/readout.h:17-71`): `kMaxStrips (10) + kMaxSources (64) + sampleHead (3) + kReadVoices (8) * (3 + 1 + 1 + 10 + 12 = 27) + liveNumLfoVoices (1) + liveNumGrains (3) + liveNoisePos (1) + liveLfo (10) + liveEnv (24) + liveNumTargets (1) + liveBlock (1) = 334 relaxed atomic stores` baseline (up to `334 + 96 + 96 = 526` with all 96 targets and 3×32 grains active).
- **(e) Verdict:** `PARTLY WRONG` — Silent sustained voices (`env1.sustain == 0` after decay) do keep rendering full voices, and `writeReadout()` executes 1 `std::exp` + 334–526 atomic stores per block, **but** the `sustainPending` loop short-circuits on a 36-byte `uint8_t` array without dereferencing `voices[i]`, and `std::log10` in `writeReadout()` runs 0 times when idle and at most 3 times when the Mixer page is closed.

---

### Rank 15 — Missing `mlock`/`minflt`/`majflt` tracking, `contentInUse` never cleared at block end, and claimed `FxPool::retire()` leak

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:202-209,1417`, `core/engine-own/fx/fx_chain.cpp:113-125`, `shell-mpc/callback_stats.h:20-70`.
  - Corrected: `core/engine-own/own_engine.cpp:196-213,1413-1420,1760-1767`, `core/engine-own/fx/fx_chain.cpp:113-149,242-260,702`, `shell-mpc/callback_stats.h:20-70`.
- **(b) Verbatim decisive code (`core/engine-own/own_engine.cpp:202-210,1414-1419` and `core/engine-own/fx/fx_chain.cpp:127-149`):**
  ```cpp
      const ContentSet* inUse = contentInUse.load();
      contentOwned.erase (std::remove_if (contentOwned.begin(), contentOwned.end(),
                                          [raw, inUse] (const std::unique_ptr<ContentSet>& p)
                                          { return p.get() != raw && p.get() != inUse; }),
                          contentOwned.end());
  ```
  ```cpp
  void FxPool::typeChanged (int slot, int type)
  {
      if (slot < 0 || slot >= kNumFxSlots) return;
      collect (slot);
      ...
      FxModule* old = s.pending.exchange (m.release(), std::memory_order_acq_rel);
      delete old;
  }
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - **Part 1 (No `mlock`/`mlockall` and no `minflt`/`majflt` in `callback_stats.h`):** Confirmed. `shell-mpc/callback_stats.h:20-70` records wall-clock durations and xruns via `clock_gettime(CLOCK_MONOTONIC)` without reading `getrusage(RUSAGE_THREAD)` (`ru_minflt`/`ru_majflt`), and no `mlock`/`mlockall` call exists in `core/` or `shell-mpc/`.
  - **Part 2 (`contentInUse` never cleared at end of `OwnEngine::process()`):** Confirmed. At `own_engine.cpp:1414-1419`, `process()` stores `cs` (`contentCurrent.load()`) into `contentInUse`, and never stores `nullptr` into `contentInUse` before returning at `line 1767`. Consequently, when the message thread later calls `publishContent(C_2)` (`own_engine.cpp:196-213`), `contentInUse.load()` still points to the old `ContentSet` `C_1` even if `process()` is not running, so `contentOwned.erase(...)` retains both `C_2` (`raw`) and `C_1` (`inUse`). `C_1` remains pinned in memory until a *third* `publishContent(C_3)` call occurs after `process()` has advanced `contentInUse` to `C_2`.
  - **Part 3 (Claimed `FxPool::retire()` leak on >4 rapid `fxi*.type` switches — REFUTED):**
    - The audit read the fallback comment in `FxPool::retire()` (`core/engine-own/fx/fx_chain.cpp:113-125`, where `s.grave` has 4 slots: `std::atomic<FxModule*> grave[4]` at `fx_chain.h:60`) and claimed that if a host automates `fxi*.type` rapidly (>4 times before collection), `retire()` overflows `s.grave[4]` and leaks `~4.7 MB` `FxModule` instances.
    - Reading the producer side in `FxPool::typeChanged(int slot, int type)` (`fx_chain.cpp:127-149`) **completely refutes this**:
      1. `FxPool::typeChanged()` is the **only** function that allocates and publishes a new `FxModule*` into `s.pending` (after `prepare()`).
      2. Line `132` — the very first statement of `typeChanged()` before allocating or publishing a module — calls `collect(slot);`, which deletes every non-null entry in `s.grave[0..3]` and resets all 4 graveyard slots to `nullptr` (`fx_chain.cpp:97-111`).
      3. Line `146-148` exchanges `s.pending` with the newly created module and immediately calls `delete old;` on the helper thread if the audio thread had not yet consumed the previous `s.pending`.
      4. On the audio thread, `Slot s` holds at most **2** live module pointers (`s.cur` and `s.from`, `fx_chain.h:55-58`), and can only obtain a new pointer to retire an old one by consuming `s.pending.exchange(nullptr)` (`fx_chain.cpp:242`). Because at most **1** `s.pending` module can exist between two consecutive calls to `typeChanged()` — and every call to `typeChanged()` empties `s.grave[0..3]` via `collect(slot)` — the audio thread can retire at most **2** modules (`s.cur` and `s.from`) into `s.grave[4]` between collections. Since `2 <= 4`, **`s.grave[4]` can never overflow and `FxPool::retire()` can never leak.**
  - Existing tests: `core/tests/test_fxinstance_keys.cpp`, `core/tests/test_threads.cpp`, `core/tests/test_own_content_policy.cpp`.
- **(d) Redone arithmetic:**
  - Maximum `FxModule*` pointers retired into `Slot::grave[4]` between two `FxPool::collect(slot)` calls = `2` (`sl.cur` + `sl.from`) `<= 4` (`std::size(s.grave) = 4` at `fx_chain.h:60`), so leaked modules = `0`.
- **(e) Verdict:** `PARTLY WRONG` — Missing `mlock`/`minflt`/`majflt` tracking and `contentInUse` pinning one retired `ContentSet` after `process()` are confirmed, **but** the claimed `FxPool::retire()` memory leak is **refuted** because `FxPool::typeChanged()` always calls `collect(slot)` (`fx_chain.cpp:132`) and deletes unconsumed `s.pending` (`fx_chain.cpp:146-148`), bounding `s.grave[4]` occupancy to at most 2 pointers.

---

### Rank 16 — No `alignas` in `core/engine-own`, `fastmath` domain/union caveats, and cached `std::round` sites

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/dsp/fastmath.h:37-82`, `core/engine-own/voice/voice.cpp:1370`, `core/engine-own/fx/fx_modules.cpp:941`, `docs/release/CPU-REVIEW-MEMORY.md:37`.
  - Corrected: `core/engine-own/dsp/fastmath.h:37-82`, `core/engine-own/voice/voice.cpp:1360-1376`, `core/engine-own/fx/fx_modules.cpp:914-945`, `docs/release/CPU-REVIEW-MEMORY.md:37,203`.
- **(b) Verbatim decisive code (`core/engine-own/dsp/fastmath.h:43-51,71-75` and `core/engine-own/voice/voice.cpp:1360-1372`):**
  ```cpp
  inline float log2 (float x) noexcept
  {
      union { uint32_t u; float v; } s { 0 };
      s.v = x;
      const float e = static_cast<float> (static_cast<int32_t> (s.u >> 23) - 127);
      s.u = (s.u & 0x007fffffu) | 0x3f800000u;
      const float m = s.v;
      return e - 2.6129108f + m * (4.1636205f + m * (-2.1231438f + m * (0.6484418f - 0.0760072f * m)));
  }
  ```
  ```cpp
      SubPart& sp = subPart;
      if (!(sp.ok && subUntouched && g.baseEpoch != 0 && sp.epoch == g.baseEpoch))
      {
          ...
          sp.octave12 = std::round (P (si.octave)) * 12.0f;
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - **Confirmed code facts:**
    1. No `alignas(16)` or `alignas(64)` annotations appear in `core/engine-own/`, and `[measured: docs/release/CPU-REVIEW-MEMORY.md:37]` counts `634` alignment-qualified `vld1`/`vst1` instructions vs `2,808` unaligned ones in `synthyboy_mpc.so` (while noting that Cortex-A17 handles unaligned NEON accesses within a 64-byte cache line with zero penalty and saves ~1 cycle only on 16-byte aligned hints).
    2. `fastmath::exp2` and `fastmath::log2` (`core/engine-own/dsp/fastmath.h:37,44`) use `union { uint32_t u; float v; }` type-punning instead of `std::memcpy`, `fastmath::log2(x)` assumes normal `x > 0` as stated in its doc comment on `line 41` (all existing callers in `mod_matrix.cpp:151,156,161` and `fx_effects.h` clamp `x >= 1e-6f`), and `fastmath::sinCycles(float c)` (`fastmath.h:73`) casts `static_cast<int>(c)` (callers pass wrapped `[0, 1)` phases or clamp `|warpPhase| < 1000.0f` at `wt_osc.h:876`).
  - **What the audit got wrong on `std::round` (`voice.cpp:1370` and `fx_modules.cpp:941-945`):**
    1. Both `std::round` sites were **already placed behind parameter caches in Round 13**: `voice.cpp:1370` (`sp.octave12 = std::round(P(si.octave)) * 12.0f;`) sits inside `if (!(sp.ok && subUntouched && g.baseEpoch != 0 && sp.epoch == g.baseEpoch))` (`voice.cpp:1360`, Round 13 V7), so it runs only when `baseEpoch` changes or `sub.octave` is modulated; and `fx_modules.cpp:941-945` (`ConvolveModule::makeKeyUncached`) sits behind `if (keyCache.valid && keyCache.in == in && !makeup::legacyCaches()) return keyCache.key;` (`fx_modules.cpp:914-919`, Round 13 MK1), so it runs only when Convolve parameters change. Neither is a hot per-period call!
    2. The audit's recommendation to *"replace `std::round` with `ffloor(x + 0.5f)`"* is **mathematically wrong for negative half-integers**: for `x = -0.5f` (`sub.octave` has range `-2..+2`, and `convolve.tone` has range `-100..+100`), `std::round(-0.5f) == -1.0f` (round half away from zero), whereas `ffloor(-0.5f + 0.5f) == ffloor(0.0f) == 0.0f` (round half toward `+inf`), a trap explicitly documented in `CPU-REVIEW-MEMORY.md:203`!
  - Existing tests: `core/tests/test_own_voice_opt.cpp`, `core/tests/test_own_content_opt.cpp`.
- **(d) Redone arithmetic:**
  - Alignment ratio in `synthyboy_mpc.so`: `634 / (634 + 2808) = 18.4%` aligned vs `81.6%` unaligned `vld1`/`vst1` (`[measured: docs/release/CPU-REVIEW-MEMORY.md:37]`); for `x = -0.5f`, `std::round(-0.5f) = -1.0f != ffloor(-0.5f + 0.5f) = 0.0f` (`1.0f` error = `12 semitones` error if applied to `sub.octave12`).
- **(e) Verdict:** `PARTLY WRONG` — The missing `alignas` qualifiers and `fastmath.h` union/domain details hold in code, **but** both cited `std::round` sites (`voice.cpp:1370` and `fx_modules.cpp:941-945`) are already cached behind `subPart` and `keyCache`, and replacing `std::round(x)` with `ffloor(x + 0.5f)` produces wrong results for negative `.5` inputs.

---

### Rank 17 — `core/engine-own/README.md:13` states built-in wavetable bank is "20.5 MB" (21 levels at 2×), whereas the 18-level `>= 8x` bank occupies `33.10 MB`

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/README.md:13` vs `core/engine-own/osc/wavetable_bank.h:20,29`, `core/engine-own/osc/wavetable_bank.cpp:229-235,274-281`.
  - Corrected: `core/engine-own/README.md:13`, `core/engine-own/osc/wavetable_bank.h:20,28-30,50`, `core/engine-own/osc/wavetable_bank.cpp:137-217,229-235,274-281`.
- **(b) Verbatim decisive code (`core/engine-own/README.md:13` vs `core/engine-own/osc/wavetable_bank.cpp:229-235,274-281`):**
  ```markdown
  - **Oscillators 1, 2, 3** and the **sub oscillator**: `osc/wavetable_bank.h` (the fifteen built-in tables, each frame pre-integrated into 21 half-octave mip levels stored at 2x their band: the eight 64 frame tables take 20.5 MB, `own_wavetable_bank_memory`), `osc/wt_osc.h`, `osc/warp.h` and `osc/morph.h`
  ```
  ```cpp
      static constexpr int kHarmonics[Wavetable::kNumLevels] = { 1024, 512, 256, 181, 128, 91, 64, 45, 32, 23, 16, 11, 8, 6, 4, 3, 2, 1 };
      for (int m = 0; m < Wavetable::kNumLevels; ++m)
      {
          int n = 256;
          while (n < 8 * harmonics[m] && n < Wavetable::kCycle)
              n <<= 1;
          size[m] = m == 0 ? Wavetable::kCycle : n;
      }
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - `Wavetable::kNumLevels` is `18` (`core/engine-own/osc/wavetable_bank.h:29`), and `LevelTable` (`core/engine-own/osc/wavetable_bank.cpp:229-235`) sizes each level `m` to `max(256, next_pow2(8 * harmonics[m]))` capped at `kCycle = 2048`, plus 4 guard samples per level (`wavetable_bank.cpp:277`).
  - Furthermore, `Wavetable::build()` (`wavetable_bank.cpp:281`) allocates `spec` with `numFrames * 2 * kHarmonics` floats (`2,048 floats = 8,192 B` per frame) across all 15 built-in tables (8 multi-frame tables with `kMorph = 64` frames at choices `5..12`, plus 7 single-frame tables at choices `0..4, 14, 15`; choice `13` is `kWavetablePresetTable = nullptr`, `wavetable_bank.cpp:137-217`).
  - `core/engine-own/README.md:13` was never updated when the mip table was changed from 21 levels at 2× (`20.5 MB`) to 18 levels at ≥8× (`33.10 MB`). (Note that `wavetable_bank.h:20` mentions `"29 MB instead of 46 MB"`, where `29 MB` refers to the 8 multi-frame tables' time-domain data alone: `512 * 13,896 * 4 B = 28.46 MB`, or `31.14 MiB` with `spec`).
  - Existing tests: `core/tests/test_own_engine.cpp` (`own_wavetable_bank_memory`).
- **(d) Redone arithmetic and EXTRA (2) exact memory recomputation:**
  - **Per frame (`Wavetable::memoryBytes()`, `wavetable_bank.h:50`):**
    - Time levels `m = 0..3` (`harmonics = 1024, 512, 256, 181`): `4 * (2048 + 4) = 8,208 floats`.
    - Time levels `m = 4..5` (`harmonics = 128, 91`): `2 * (1024 + 4) = 2,056 floats`.
    - Time levels `m = 6..7` (`harmonics = 64, 45`): `2 * (512 + 4) = 1,032 floats`.
    - Time levels `m = 8..17` (10 levels, `harmonics = 32..1`): `10 * (256 + 4) = 2,600 floats`.
    - `frameStride = 8,208 + 2,056 + 1,032 + 2,600 = 13,896 floats (55,584 B)`.
    - Spectrum `spec` per frame = `2 * 1024 = 2,048 floats (8,192 B)`.
    - **Total per frame:** `(13,896 + 2,048) * 4 B = 15,944 * 4 B = 63,776 B (63.776 KB / 62.28 KiB)`.
  - **Across all 15 built-in wavetables (`519` frames total):**
    - 8 multi-frame tables (`8 * 64 = 512 frames`): `512 * 63,776 B = 32,653,312 B (32.65 MB / 31.14 MiB)`, of which `spec` is `512 * 8,192 B = 4,194,304 B (4.19 MB / 4.00 MiB)` and time data is `512 * 55,584 B = 28,459,008 B (28.46 MB / 27.14 MiB)`.
    - 7 single-frame tables (`7 * 1 = 7 frames`): `7 * 63,776 B = 446,432 B (0.45 MB / 0.43 MiB)`.
    - **Grand total for built-in wavetable bank:** `519 * 63,776 B = 33,099,744 B (33.10 MB / 31.57 MiB)`.
- **(e) Verdict:** `CONFIRMED` — Every figure in the audit's wavetable bank calculation (`18` levels, `13,896` time floats + `2,048` spectrum floats = `63,776 B/frame`, `32.65 MB` for the 8 multi-frame tables, `33.10 MB` for all 15 tables, and `4.19 MB` for `spec[]`) is exact, and `core/engine-own/README.md:13` is stale.

---

### Rank 18 — Static `tier_bench` links container glibc 2.36 `libm` while `synthyboy_mpc.so` calls device glibc 2.39 `libm` via PLT; RTTI and exceptions enabled across DSP libraries for 7 `dynamic_cast` helpers

- **(a) Cited and verified `file:line`:**
  - Cited: `core/engine-own/own_engine.cpp:32-92`, `core/CMakeLists.txt:59`, `docs/release/CPU-REVIEW-MEMORY.md:359`.
  - Corrected: `core/engine-own/own_engine.cpp:32-92`, `core/CMakeLists.txt:59`, `bench/CMakeLists.txt:30-32`, `docs/release/CPU-REVIEW-MEMORY.md:352-359`.
- **(b) Verbatim decisive code (`bench/CMakeLists.txt:30-33` and `core/engine-own/own_engine.cpp:32-36`):**
  ```cmake
  option(SYNTHYBOY_TIER_BENCH_STATIC "Link synthyboy_tier_bench fully static (Linux cross builds for the MPC)" OFF)
  if(SYNTHYBOY_TIER_BENCH_STATIC)
      target_link_options(synthyboy_tier_bench PRIVATE -static)
  endif()
  ```
  ```cpp
  void setOwnLowTierKillFade (IEngine& engine, float seconds) noexcept
  {
      if (auto* o = dynamic_cast<own::OwnEngine*> (&engine))
          o->setLowTierKillFade (seconds);
  }
  ```
- **(c) Surrounding code, guards, and existing tests:**
  - Confirmed in `bench/CMakeLists.txt:30-32` and `[measured: docs/release/CPU-REVIEW-MEMORY.md:352-359]`: `scripts/build-tier-bench.sh` builds `synthyboy_tier_bench` with `-DSYNTHYBOY_TIER_BENCH_STATIC=ON` (`-static`), embedding the Debian 12 cross-container's glibc 2.36 `libm.a` directly into the benchmark binary (direct PC-relative calls), whereas `synthyboy_mpc.so` links `libm.so.6` dynamically and resolves `expf`, `log2f`, `powf`, `sinf`, `cosf`, `tanf` through the PLT into the MPC device's glibc 2.39 `libm.so.6`.
  - Confirmed in `core/engine-own/own_engine.cpp:32-92`: the seven test/measurement helpers (`setOwnLowTierKillFade`, `setOwnSavingsOff`, `setOwnVoiceCountAlpha`, `ownVoiceCountGain`, `setOwnLowTierMorphSpread`, `setOwnLowTierUnisonCap`, `setOwnPadPressureToCutoff`) use `dynamic_cast<own::OwnEngine*>(&engine)` (and `const own::OwnEngine*`), and neither `-fno-rtti` nor `-fno-exceptions` is set on `synthyboy_core` or the DSP sub-libraries (`core/CMakeLists.txt:59`).
  - Note that `IEngine` (`core/include/synthyboy/engine.h`) already has virtual methods for engine capabilities and parameters; adding virtual hooks or a tag enum on `IEngine` would eliminate the 7 `dynamic_cast` calls if `-fno-rtti` is desired, though `core/src/preset.cpp` and `core/multisample/src/multisample.cpp` (off-audio-thread loaders inside `synthyboy_core`) would need to be checked before enabling `-fno-exceptions` on `synthyboy_core` as a whole.
  - Existing tests: `core/tests/test_own_voice_count_gain.cpp`, `core/tests/test_own_unison_cap.cpp`, `core/tests/test_own_pad_pressure.cpp`.
- **(d) Redone arithmetic:**
  - `7` `dynamic_cast<own::OwnEngine*>` sites in `core/engine-own/own_engine.cpp:34,47,55,61,72,82,89`; `synthyboy_tier_bench` links static glibc `2.36` `libm.a` (`0` PLT stubs) vs `synthyboy_mpc.so` dynamic glibc `2.39` `libm.so.6` via PLT (`[measured: docs/release/CPU-REVIEW-MEMORY.md:354,359]`).
- **(e) Verdict:** `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` — Both the static-vs-dynamic `libm` difference between `tier_bench` and `synthyboy_mpc.so` and the 7 `dynamic_cast` helpers in `own_engine.cpp:32-92` are confirmed; whether PLT indirection vs glibc 2.39 `libm` or RTTI/eh_frame metadata measurably affects audio-thread CPU requires the named device measurement.

---

## Seen in Passing

1. **`core/tests/test_own_helper_thread.cpp` does not cover `wavetable_bank.cpp`'s helper thread (`Bank::run`):** `test_own_helper_thread.cpp` tests `OwnEngine::fxWorker` and `FxWorker::instance()` via `setHelperThreadStartHookForTest()`, which is why `Bank::run()` (`core/engine-own/osc/wavetable_bank.cpp:472`) omitting `runHelperThreadStartHook()` went undetected by the helper-thread test suite.
2. **`ConvolveModule::startBuild` and `makeupPower` do not release `job.impulse` / `specJob.impulse` when `FxWorker::submit()` fails (`core/engine-own/fx/fx_modules.cpp:824-825,993-994`):** Adding `job.impulse.reset();` inside the `if (!FxWorker::instance().submit(...))` failure branch (and `specJob.impulse.reset();` at line 825) guarantees that `job.impulse` is always `nullptr` whenever `jobBusy == 0`.
3. **`OwnEngine::setContent()` calls `ownContent.publish(content)` before acquiring `contentMutex` (`core/engine-own/own_engine.cpp:230-233`):** Moving `std::lock_guard<std::mutex> lock(contentMutex);` two lines up (before `ownContent.publish(content);`) serializes `ContentHub::next++` against any concurrent `setContent()` caller at zero cost.

---

## Work List Check

Verification of the 15 items in the audit's **"Ordered Work List Across Parts 1 to 3"** (`docs/review/OWN-ENGINE-DSP-AUDIT-3-CORE-PERFORMANCE-2026-11-20.md:524-540`):

1. **Item 1 (Part 3 Rank 9 — Harden `tier_bench` with `SCHED_FIFO`, interleaved A/B, PMU, thermal, dynamic `libm`):** Position #1 still makes sense because Rank 9 is `CONFIRMED` (`+20–25%` p99 noise and `18%` drift in `docs/MPC.md:321,350` are larger than most individual DSP optimizations), though `section_timing.cpp:57-73` already subtracts estimated clock read cost.
2. **Item 2 (Part 3 Ranks 2, 3, 12, 13, 15 — Eliminate audio-thread RT-safety hazards):** Position #2 still makes sense as a low-risk hygiene pass, except that the `FxPool::retire()` leak sub-item is `REFUTED` (so `grave[4]` needs no change) and `WakeSignal::notify()` does not block on Linux (only its non-Linux `std::mutex` fallback and failed-submit `job.impulse.reset()` need fixing alongside `ContentHub` hazard protection, `contentInUse` reset, `convolveOn` hoisting, and `applyState` batching).
3. **Item 3 (Part 3 Rank 5 — Enforce `effectivePolyphony()` inside `allocateVoice()` in Kill mode):** Position #3 is strongly justified because Rank 5 is `CONFIRMED` and our trace showed all 16 voices in a 16-note strike even run a full 32-sample `Voice::render()` period before `killExcess()` fires.
4. **Item 4 (Part 3 Ranks 1, 6, 17 & Part 1 Rank 10 — Shrink memory footprint and pool `LibOsc` / `FilterImpl` / `Reverb` / `MorphScratch`):** Position #4 still makes sense because Ranks 1 and 17 are `CONFIRMED` (`82.7 MB` per instance and `169.4 ms` `prepare()` time) and Rank 6's `2.66 MB` of duplicate `MorphScratch` buffers is `CONFIRMED-CODE`, even though the cache-miss CPU savings remain to be measured with PMU counters from Item 1.
5. **Item 5 (Part 3 Ranks 4, 8, 18 — Toolchain & build flags: `-mcpu=cortex-a17`, hidden visibility, `--ffunction-sections`, LTO, Apple ARM64 `SB_NEON`, CLAP FTZ):** Position #5 should be demoted below the confirmed Part 1 and Part 2 algorithmic bugs (Items 6–10) or treated strictly as an A/B experiment, because Rank 4 is `PARTLY WRONG` (the 136 integer-divides/sample in `Compressor::Limiter` were already fixed in Round 13) and Ranks 8 and 18 are `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`.
6. **Item 6 (Part 1 Ranks 11, 9, 26 & Part 2 Ranks 6, 7, 12, 14, 15 — Eliminate remaining ARMv7 NEON VFP compare spills and per-sample `libm`/divisions):** Moving this ahead of Item 5 makes sense because the remaining raw `vcltq_f32` sites in `wt_osc.h:975,982` and `core/spectral/src/kernels.cpp` (`[measured: docs/MPC.md:341,352]`) and per-sample divisions/transcendentals in `DelayLine::readLagrange8`, `Bode`, `Reverb`, and `HyperDimension` are direct per-sample hot-loop costs.
7. **Item 7 (Part 1 Ranks 2, 3, 12 — Move warp FIR decimation after unison sum, throttle Spectral Ext Mask/Vocode FFT to hop boundaries, half-size real morph IFFT):** Position #7 makes strong sense right after or alongside Item 6 because running decimation once per stereo oscillator output instead of `U` times per unison voice and running the 2048-pt external FFT every 512 samples instead of every 32 samples are large, low-risk algorithmic CPU wins.
8. **Item 8 (Part 1 Ranks 1, 8, 13, 14, 15, 17, 20, 24, 25 — Fix deterministic voice-path math bugs):** Position #8 makes sense (and the trivial 1-line bugfixes like `gSafe <= 0.95f` capping ladders at 10.67 kHz, `subTick.panRight`, `combFeedbackLP_[4]`, Gibbs taper `80%` vs `85%`, and `HOST`-locked LFO `songSeconds` ordering can even be landed earlier alongside Item 3).
9. **Item 9 (Part 2 Ranks 2, 3, 5, 8, 9, 11, 13 — Fix deterministic effects & shell bugs):** Position #9 makes sense as the direct Part 2 counterpart to Item 8 (`SplitterLMH` `AP2` vs `AP4`, splitter band latency compensation, `Nitrous` negative HF damping `ah`, multiband compressor parallel dry phase alignment, `Flanger` ±1.0 feedback clip, and `shell-clap` mono/tail sleep bugs).
10. **Item 10 (Part 1 Ranks 4, 5, 7 & Part 2 Rank 1 — Close major aliasing gaps in warps, sample/granular mip selection, and 1× distortion):** Position #10 makes sense after the CPU headroom items (Items 3, 4, 6, 7) and correctness bugfixes (Items 8, 9), since 2×/4× distortion oversampling or ADAA spends a small slice of the recovered CPU budget to remove audible aliasing.
11. **Item 11 (Part 1 Ranks 6, 16, 23 & Part 2 Ranks 10, 16 — Eliminate control-rate zipper noise and click hazards):** Position #11 makes sense here as a cohesive smoothing and crossfading pass across wavetable frame scanning, FX parameter ramps, and 10 ms instance-0 bypass fades.
12. **Item 12 (Part 2 Ranks 4, 5 — Upgrade Convolve to non-uniform partitioning / real FFT and vectorize Reverb FDN in NEON):** Position #12 makes sense as a medium-complexity XL / `fx=all` performance and quality upgrade once core Key 61 voice and tier-`low` FX work is complete.
13. **Item 13 (Part 3 Ranks 7, 12, 14 & Part 1 Rank 18 — Trim per-block control & silent-path overhead):** Position #13 appropriately sits near the bottom of the list because our verification showed Ranks 7 and 14 are `PARTLY WRONG` (`NoteTuning::compute()` only calls `std::log2` once and `writeReadout()` already gates `std::log10` by `sourceMeters` and `pk > 1e-3f`, costing only 0.1–0.4% of a block), though skipping silent sustained voices (`Ring Mod Gamelan`) and unused filters (`!filtOn[f] && !usedFin[f]`) remains worthwhile.
14. **Item 14 (Part 3 Ranks 10, 11 — Fix wavetable builder thread affinity/wake and cap `quality.oversample_4x` on tier `low` + lightweight character FX):** Position #14 makes sense for the lightweight Reverb/Flanger/HyperDimension tier-`low` modes after Item 12, while the two 5-line fixes (`runHelperThreadStartHook()` + `WakeSignal` in `Bank::run` and capping `globals.oversample` on tier `low`) can be folded into Item 2.
15. **Item 15 (Part 1 Ranks 19, 21, 22 & Part 2 Ranks 17, 18, 19 — Secondary polish: RK4 attractors, noise SR scaling, Chorus `1/sqrt(V)` normalization, `muteSkip`, True-Peak detector):** Position #15 at the end of the work list makes sense as lower-priority quality and completeness polish after all safety, headroom, correctness, and anti-aliasing work is verified.

---

Clean-room attestation: Nothing under `core/engine-vital/`, `core/vital-import/`, or `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/` was opened, read, grepped, or listed during this verification.
