# Implementation Plan: Real-Time Thread Safety of the Own Engine and MPC Shell

**Scope:** Part 3 Audit Ranks **2**, **3**, **10**, **12**, **13**, and **15** (`docs/review/OWN-ENGINE-DSP-AUDIT-3-CORE-PERFORMANCE-2026-11-20.md` and `docs/review/OWN-ENGINE-DSP-AUDIT-3-VERIFICATION-2026-11-20.md`, plus open items in `TODO.md`).  
**Targets:** Akai MPC Key 61 (ARMv7 Cortex-A17, NEON, 4 GB RAM (measured MemTotal 4,113,388 kB, no swap, 2026-11-21), tier `low`, 8 voices, 44.1 kHz, 128-frame blocks = 2.902 ms budget, cores 2–3 isolated for audio), MPC XL (ARM64 Cortex-A76), iOS (Apple ARM64), and desktop (macOS / Linux / Windows).  
**Execution model:** Small Gemini Flash workers (no local compilation); each task is one commit pair (**commit 1:** test that fails on current `develop`; **commit 2:** minimal fix that makes it pass), editing at most 3 files total, batched so no two tasks in a batch touch the same file. One build worker compiles and runs everything once per batch or at the end.

---

## 1. The Problem, as Verified

Every call path below was traced directly from the audio callback (`OwnEngine::process`, `pluginProcessReplacing`, `effProcessEvents`, or `clap_process`) to the exact line in the current worktree. Where the audit (`AUDIT-3`) and verification (`AUDIT-3-VERIFICATION`) disagreed, the verification and direct source inspection govern.

### 1.1 Complete Audio-Thread Call-Path Trace and Hazard Classification

Rules used for classification: a **mutex lock**, **heap allocation (`new`/`malloc`)**, **heap free (`delete`/`free`)**, **spinlock against a lower-priority thread**, or **C++11 magic-static guard (`__cxa_guard_acquire`)** on the audio thread **can block**. A Linux **`FUTEX_WAKE_PRIVATE`** or an **uncontended atomic load/store/RMW** **does not block**. Only verified real hazards (or diagnostic measurement gaps explicitly required to verify page-fault freedom) receive tasks.

| # | Audit Rank | Hazard / Sub-Item | Traced Call Path (`file:line` per step) | Can Block? | Data Race? | Use-After-Free? | Verdict & Disposition |
|---|---|---|---|---|---|---|---|
| H1 | **Rank 2** | `ContentHub::publish()` outside `contentMutex` & 8-slot overwrite ring | **Audio read:** `pluginProcessReplacing` (`shell-mpc/synthyboy_mpc.cpp:3192`) → `inst->engine->process` (`3298`) → `OwnEngine::process` (`core/engine-own/own_engine.cpp:1320`) → `ownContent.get()` (`1352`, `core/engine-own/voice/own_content.h:133`) → `matrix.setContent`/`compile` (`own_engine.cpp:1358-1359`, `core/engine-own/matrix/mod_matrix.cpp:301`, read in `evaluate` `:364`) & `globals.lfoContent` (`own_engine.cpp:1370`) → `Voice::tick` (`core/engine-own/voice/voice.cpp:401,414,789-790,801-802,999`).<br>**Writer:** `OwnEngine::setContent` (`own_engine.cpp:227`) → `ownContent.publish` (`230`) **before** `std::lock_guard lock(contentMutex)` (`233`) → `next++ % 8` / `ring[slot] = std::move(o)` (`own_content.h:103-105,151-154`). | **No** (`ContentHub::get()` is a single acquire load). | **YES** (writer–writer on non-atomic `next` and `ring[]`; also `noteTuning.setContent` at `own_engine.cpp:229`). | **YES** (9th `setContent()` destroys slot 0 while audio thread still reads `contentNow` / `ModMatrix` / `Voice::lfoSet`). | **REAL HAZARD** → Tasks **T8**, **T10**. |
| H2a | **Rank 3** | `WakeSignal::notify()` on **Linux** (MPC Key 61 / XL) | **Path A1:** `OwnEngine::process` (`own_engine.cpp:1320`) → `FxChain::update` (`1516`, `core/engine-own/fx/fx_chain.cpp:377`) → `FxModule::update` (`472`, `core/engine-own/fx/fx_module.h:130`) → `ConvolveModule::configure` (`144`, `core/engine-own/fx/fx_modules.cpp:768`) → `startBuild` (`787,986`) → `FxWorker::submit` (`993,42`) → `wake.notify()` (`52`, `core/engine-own/dsp/helper_thread.h:36-40,71`).<br>**Path A2 (tier `low`, bypassed Convolve):** `OwnEngine::process` (`1320`) → `FxChain::update` (`1516`, `fx_chain.cpp:377`) → `FxModule::makeup` (`445`, `fx_module.h:112`) → `ConvolveModule::makeupPower` (`115`, `fx_modules.cpp:810`) → `FxWorker::submit` (`824,42`) → `wake.notify()` (`52`, `helper_thread.h:36-40,71`).<br>**Path B (CLAP `setParam`):** `clap_process` (`shell-clap/synthyboy_clap.cpp:244,338`) → `applyHostValue` (`shell-clap/synthyboy_plugin.cpp:87,90`) → `OwnEngine::setParam` (`own_engine.cpp:254`) → `fxWake.notify()` (`278`, `helper_thread.h:36-40,71`). | **No** (`word.exchange(1)` + `FUTEX_WAKE_PRIVATE` does not block). | No. | No. | **NON-BLOCKING ON LINUX** (Audit 3 partly wrong; 3-state futex in T1 still removes redundant wakes when worker is awake). |
| H2b | **Rank 3** | `WakeSignal::notify()` on **Apple (iOS/macOS) & Windows** | Exact same Paths A1, A2, B above → `core/engine-own/dsp/helper_thread.h:42-46`: `std::lock_guard<std::mutex> lock(mutex); flag = true; cv.notify_one();`. | **YES** (`std::mutex::lock` blocks if helper thread holds `mutex` in `wait()` at `helper_thread.h:60-62`). | No. | No. | **REAL HAZARD** (iOS + desktop) → Task **T1**. |
| H2c | **Rank 3** | `ConvolveModule` `shared_ptr<Impulse>` overwrite on audio thread (+ adjacent Part 2 #4 `live.tone` race) | Paths A1 (`fx_modules.cpp:989-994`) & A2 (`fx_modules.cpp:820-825`). When `FxWorker::submit()` succeeds, `runJob`/`runSpectrumJob` call `j->impulse.reset()` (`1019,1056`) before clearing `jobBusy`/`specBusy` (`1020,1058`). When `FxWorker::jobs[64]` (`fx_module.h:59`) is full, `submit()` returns `false` and clears `jobBusy`/`specBusy` (`994,825`) while leaving `job.impulse`/`specJob.impulse` non-null; once the old `ContentSet` is freed, the next `startBuild`/`makeupPower` drops the last strong reference on the audio thread. Also `configure()` writes plain `live.tone` (`778`) while `runJob()` reads `m->live.tone` (`1014`). | **YES** (`delete`/`free()` of `Impulse` vector on audio thread after failed `submit()`). | **YES** (`live.tone` plain `float` race between `fx_modules.cpp:778` and `:1014`). | No. | **REAL HAZARD** (failed-submit edge case + `live.tone` TSan race) → Task **T2**. |
| H3 | **Rank 10** | Wavetable bank builder (`Bank::run`): missing start hook, 10 ms polling, shared `latest[6]` | **Audio read:** `OwnEngine::process` (`own_engine.cpp:1320`) → `builtinWavetableIfReady` (`1435`, `core/engine-own/osc/wavetable_bank.cpp:553-558`, acquire load).<br>**Audio/CLAP write:** `OwnEngine::setParam` (`own_engine.cpp:254,265`) → `requestBuiltinWavetable` (`wavetable_bank.cpp:567-571`, relaxed store).<br>**Builder thread:** `OwnEngine::prepare` (`own_engine.cpp:403`) → `startBuiltinBankBackground` (`wavetable_bank.cpp:592,500-514`) → `Bank::run` (`473-498`): omits `runHelperThreadStartHook()`, calls `::setpriority(..., 10)` (`476`, ignored under `SCHED_FIFO`), loops `sleep_for(10ms)` forever (`495-496`), shares `latest[6]` (`363,373`) across instances. | **Audio calls: No.**<br>**Builder thread:** Can inherit `SCHED_FIFO` + isolated audio-core affinity and preempt audio core; 100 wakeups/s indefinitely. | No. | No. | **REAL HAZARD** (audio-core isolation + multi-instance request loss) → Task **T6**. |
| H4a | **Rank 12** | `OwnEngine::process()` oversized mono `scratchRight.assign()` | `OwnEngine::process` (`own_engine.cpp:1320`) with `right == nullptr` and `numFrames > max(maxBlockFrames, 4096)` (`prepare` allocates `std::max(maxBlockFrames, 4096)` at `own_engine.cpp:393`) → `scratchRight.assign(numFrames, 0.0f)` (`own_engine.cpp:1336-1337`). | **YES** (`operator new` + `operator delete` on audio thread). | No. | No. | **REAL HAZARD** (violates `IEngine::process` contract `core/include/synthyboy/engine.h:53-54,124-126`) → Task **T7**. |
| H4b | **Rank 12** | `static const int convolveOn` in `publishStats()` | `OwnEngine::process` (`own_engine.cpp:1320`) → `publishStats()` (`1769,1775`) → `static const int convolveOn = findParamIndex(hashKey("convolve.on"));` (`1804`). | **YES** (first call acquires C++11 static init guard `__cxa_guard_acquire` mutex/futex). | No. | No. | **REAL HAZARD** → Task **T7**. |
| H4c | **Rank 12** | Redundant `left`/`rightOut` `std::fill`, silent-bus `voiceCountGain` & `runBus()` | `own_engine.cpp:1325,1340,1680-1684,1698-1734`. | No. | No. | No. | **NOT AN RT HAZARD** (pure CPU opt ~0.15–0.2%, `docs/release/CPU-REVIEW-PROFILE.md:217,246`; no task in this plan). |
| H5a | **Rank 13** | `EventInbox::push()` spinlock | **Audio callback:** `pluginProcessReplacing` (`shell-mpc/synthyboy_mpc.cpp:3192`) → `eventInbox.pop` (`3251`, `shell-mpc/event_inbox.h:35-42`, lock-free).<br>**MIDI callback:** `pluginDispatcher` → `effProcessEvents` (`synthyboy_mpc.cpp:2433`) → `eventInbox.push` (`2479`, `event_inbox.h:21-33`) → `while (producers.test_and_set(acquire)) {}` (`22`). | **Consumer (`pop`): No.**<br>**Producer (`push`): YES** (priority-inversion livelock if RT MIDI thread spins behind a preempted non-RT producer). | No. | No. | **REAL HAZARD** → Task **T3**. |
| H5b | **Rank 13** | Function-local static guards in `noteDiagnostics()` & `outputPeak()` | **MIDI callback:** `effProcessEvents` (`synthyboy_mpc.cpp:2433`) → `noteDiagnostics().noteOn(hostVel)` (`2464`, `shell-mpc/output_peak.h:134-138`: `static NoteDiagnostics nd;`).<br>**Audio callback:** `pluginProcessReplacing` (`3192`) → `outputPeak().note(...)` (`3306`, `output_peak.h:85-89`: `static OutputPeak p;`). | **YES** (first call invokes `__cxa_guard_acquire` on MIDI/audio thread). | No. | No. | **REAL HAZARD** → Task **T4**. |
| H5c | **Rank 13** | `applyState()` writing 2,637 parameters one by one | Host state thread (`effSetProgram` `synthyboy_mpc.cpp:2123,2145`; `effSetChunk` `2546`) → `applyState` (`1899-1933`) loops 2,637× calling `engine->setParam` (`1905-1913`), bumping `paramGen` 2,637× (`own_engine.cpp:261`) while `OwnEngine::process()` (`1351-1357`) reads `params[0..2636]` without batching. | **No.** | **No** (individual atomics). | **No.** | **REAL CONCURRENCY HAZARD** (torn half-A / half-B preset audio frames & transient spikes during live preset switches) → Tasks **T11**, **T12**, **T13**. |
| H5d | **Rank 13** | Unconditional `outputPeak().note()` & `spectrumRing.push()` | `pluginProcessReplacing` (`synthyboy_mpc.cpp:3306,3309`) → `output_peak.h:72-80`, `shell-mpc/spectrum.h:33-39`. | No (once H5b static guard is removed). | No. | No. | **NOT AN RT HAZARD** (lock-free relaxed/release atomics; `512 B/block` of `32 KiB` ring; gating is a CPU opt outside this plan). |
| H6a | **Rank 15** | `contentInUse` never cleared at block end + mid-period `controlPos != 0` UAF | `OwnEngine::process` (`own_engine.cpp:1414-1420`) sets `contentInUse.store(cs)` and never clears it before returning (`1772`), pinning 1 dead `ContentSet` indefinitely in `contentOwned` (`202-210`). **Additional defect found by direct code trace:** if a host delivers a non-multiple-of-32 block while `activeMask != 0` (`controlPos != 0` at entry), line `1418` overwrites `contentInUse` with `C2` BEFORE `controlTick()` (`1463`) runs, while `Voice::render()` (`1672`) still reads `C1` via `LibOsc::boundSet` (`lib_osc.cpp:236`), `Voice::noiseBound` (`voice.cpp:1435`), and `WtOsc` (`wt_osc.h:225`) — allowing a concurrent `publishContent(C3)` to free `C1` mid-render! | No (audio thread). | No. | **YES** (mid-period UAF on non-multiple-of-32 blocks; plus indefinite retention of multi-MB retired `ContentSet`s on 4 GB Key 61). | **REAL HAZARD** → Task **T9**. |
| H6b | **Rank 15** | Claimed `FxPool::retire()` leak on >4 switches | `core/engine-own/fx/fx_chain.cpp:106-152,245-265`, `core/engine-own/fx/fx_chain.h:53-60`. | No. | No. | No. | **REFUTED** (no task; see §1.3). |
| H6c | **Fixed 2026-11-21** | TSan data race: the FX builder thread read the audio thread's `FxPool::Slot::cur` in `FxPool::typeChanged` (`fx_chain.cpp:134`) while `FxChain::configureSlot` wrote it (`:254`, via `FxChain::update` `:486`, `OwnEngine::process`) | Builder-side `Slot::handedOver` (guarded by `FxPool::typeMutex`, never taken on the audio thread) replaces `pending != nullptr \|\| cur != nullptr`. | No (the audio thread does not touch the new fields or the mutex). | No. | No. | **FIXED**; test `synthyboy_test_own_fx_worker_race` (TSan report 5/5 before, 0 after); sounds bit-identical (CHECKSUM lines of the FX/engine tests unchanged). |
| H6c | **Rank 15** | Missing `minflt`/`majflt` tracking & `mlock()` | `shell-mpc/callback_stats.h:20-97` tracks only monotonic wall-clock gaps/durations. No `mlock`/`mlockall` exists. | Page fault (`majflt`) **can block** audio thread for ms; `mlockall` inside host process risks OOM. | No. | No. | **DIAGNOSTIC GAP** (`minflt`/`majflt` on writer thread) → Task **T5**. `mlock` deferred pending device `RLIMIT_MEMLOCK` measurement (`HYPOTHESIS`). |

### 1.2 Costs, Magnitudes, and Exact Arithmetic

- **`ContentHub` memory (`core/shapes/include/synthyboy/shapes/shape.h:25-44`, `core/engine-own/voice/own_content.h:19-52,151-155`):**
  - `sizeof(shapes::Point) = 3 * 4 B + 2 * 1 B + 2 B pad = 16 B`
  - `sizeof(shapes::ShapeData) = 128 * 16 B + 8 B = 2,056 B` (64-bit) / `2,052 B` (32-bit)
  - `sizeof(PointMod) = 4 + 4 = 8 B`; `sizeof(CompiledPoint) = 16 + 3 * 8 = 40 B`
  - `sizeof(CompiledShape) = 2 + 2 pad + 4 + 128 * 40 + 2,056 = 7,184 B` (64-bit) / `7,180 B` (32-bit)
  - `sizeof(CompiledModCurve) = 1 + 3 pad + 4 + 2048 * 4 = 8,200 B`
  - `sizeof(OwnContent) = 10 * 7,184 + 66 * 8,200 = 71,840 + 541,200 = 613,040 B` (`0.613 MB`) on 64-bit (`613,000 B` on 32-bit).
  - Baseline after `OwnEngine::prepare()`: **`0 B`** (`ring[8]` starts as 8 null `unique_ptr`s; `OwnEngine::OwnEngine()` at `own_engine.cpp:147` only calls `publishContent`, not `ownContent.publish`). After 8 calls to `setContent()`: **`8 * 613,040 = 4,904,320 B` (`4.90 MB` / `4.68 MiB`)** retained per instance.
  - With `ContentHub` Dekker hazard pointer (at most 1 set idle) + sparse `modCurve[64]` (`std::array<std::unique_ptr<CompiledModCurve>, 64>`): `sizeof(OwnContent) = 10 * 7,184 + 64 * 8 + 2 * 8,200 = 88,752 B` (`86.7 KiB`) on 64-bit (`88,456 B` on 32-bit) — a **55× reduction** vs the 8-slot ring (`4.90 MB → 88.8 KB`) on presets without custom mod curves.
- **`ContentSet` pinned retention (`core/engine-own/osc/content_set.cpp:76-138`, `core/engine-own/own_engine.cpp:196-213,1414-1420`):**
  - Each sample/spectral/IR preset builds a `ContentSet` holding decoded stereo samples (`frames * 2 * 4 B * 2`), STFT analysis (`numFrames * channels * 3 * 1025 * 4 B`), user wavetables, or stereo IR (`frames * 2 * 4 B`). Because `contentInUse` is never cleared, switching from a heavy sample/spectral preset `C1` to a synth preset `C2` keeps `C1` alive in `contentOwned` until a third preset `C3` is loaded after audio has run — wasting `1` full `ContentSet` (measured `HYPOTHESIS`: 5–30 MB for Spectral/Sample/IR presets; decided by `VmRSS` before/after switching presets on Key 61).
- **`Wavetable` builder thread idle wakeups (`core/engine-own/osc/wavetable_bank.cpp:495-496`):**
  - `1000 ms / 10 ms = 100 wakeups/s` indefinitely for the plugin's lifetime when idle (`0 wakeups/s` after `WakeSignal`).
- **`scratchRight` waste (`core/engine-own/own_engine.cpp:393,1336-1337`):**
  - Pre-allocated `std::max(maxBlockFrames, 4096) * 4 B = 16,384 B` (`16 KiB`) per instance even in stereo; reallocates (`numFrames * 4 B`, e.g., `32 KiB` at `numFrames = 8192`) on the audio thread in mono. Replacing it with stack `float monoRight[kControlPeriod]` costs `32 * 4 = 128 B` on the stack and `0 B` on the heap.
- **`applyState()` per-preset load overhead (`shell-mpc/synthyboy_mpc.cpp:1905-1913`, `core/engine-own/own_engine.cpp:254-288`):**
  - `2,637` hash lookups (`findParamIndex`), `2,637` `atomic<float>::store(relaxed)`, `2,637` `paramGen.fetch_add(1, release)` RMWs, and up to `16` `fxWake.notify()` calls per preset switch; with `setParamSnapshot()`, `0` hash lookups, `1` `paramGen` bump, `<= 1` `fxWake.notify()`, and `10,548 B` (`2,637 * 4 B`) pre-allocated `baseStaging` per `OwnEngine`.
- **MPC Key 61 baseline CPU and callback gaps (from repository measurements):**
  - Key 61 block budget at 44.1 kHz / 128 frames: **`2.902 ms`** (`docs/MPC.md:68`).
  - Bare `OwnEngine` idle: **`0.022 ms/block`** (`docs/MPC.md:71`); full MPC shell idle: **`63–65 µs/block`** average, max gap `3.237–3.525 ms` (`1.12–1.21×` nominal) (`docs/MPC.md:122`); 4 held notes with overlay active: `576–617 µs` average, max gap `3,472 µs` (`1.20×`), 0 callbacks `>= 1.5×` over 338,085 callbacks (`docs/MPC.md:308-314`).
  - Round 13 Key 61 `tier_bench --config low` (160 presets, 8 notes): mean of medians **`23.45 %`** (`0.681 ms`), mean p99 **`48.96 %`** (`1.421 ms`); under `SCHED_FIFO 20` (`rtrun`, 24 heaviest presets): mean median **`34.26 %`**, mean p99 **`39.30 %`**, 0 presets over 70 % (`docs/MPC.md:350`).
  - MPC XL (`high,voices=16,control=economy`): mean median **`14.27 %`**, dearest preset p99 **`39.7 %`** (all 160 under 50 %, `docs/MPC.md:350`).
  - Desktop/laptop reference (`docs/PERFORMANCE.md:50-60`, Apple M5 Pro, 48 kHz, 128 frames = 2.667 ms): `synthyboy_bench --engine own` default patch low/med/high = `0.0090 / 0.0174 / 0.0332 ms` mean.

### 1.3 Audit Claims Corrected, Refuted, or Already Fixed

1. **Part 3 Rank 2 (`PARTLY WRONG`):** Audit claimed `ContentHub` wastes `~4.3 MB` upfront per instance. Verified: `ring[8]` starts as 8 null `unique_ptr`s (`0 B` at `prepare()`), growing `613 KB` per `setContent()` call up to `4.90 MB` after 8 calls (`core/engine-own/voice/own_content.h:151-155`, `own_engine.cpp:147`).
2. **Part 3 Rank 3 (`PARTLY WRONG`):**
   - Audit treated Linux `WakeSignal::notify()` (`SYS_futex(FUTEX_WAKE_PRIVATE)`) as a blocking RT violation. Verified: `FUTEX_WAKE_PRIVATE` is non-blocking; only the `#else` `std::mutex` path (`helper_thread.h:42-46`) on Apple (iOS/macOS) and Windows can block.
   - Audit implied `ConvolveModule` routinely frees `shared_ptr<Impulse>` on the audio thread. Verified: on successful `FxWorker::submit()`, `runJob`/`runSpectrumJob` already call `j->impulse.reset()` on the worker thread before clearing `jobBusy`/`specBusy` (`fx_modules.cpp:1019-1020,1056-1058`). The RT `free()` occurs **only** when the 64-slot `FxWorker::jobs` queue is full and `submit()` returns `false` (`fx_modules.cpp:824-825,993-994`).
3. **Part 3 Rank 10 (correction to Verification 3's snippet):** Verification 3 (`OWN-ENGINE-DSP-AUDIT-3-VERIFICATION-2026-11-20.md:386-396`) quoted an older eager `kOrder[kNumChoices]` loop. Direct inspection of `core/engine-own/osc/wavetable_bank.cpp:473-498` shows `Bank::run()` is **strictly on-demand** (it only builds tables requested via `latest[k]`, never eagerly builds all 15). Therefore the thread **must not exit** after a first pass; it must sleep on `WakeSignal` until a new request or stop arrives.
4. **Part 3 Rank 12 (`CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`):** Only `scratchRight.assign()` (`own_engine.cpp:1337`) and `static const int convolveOn` (`own_engine.cpp:1804`) are RT-threading hazards; redundant `std::fill` and silent-bus loops (`~0.15–0.2%`, `CPU-REVIEW-PROFILE.md:217,246`) are pure CPU optimizations, not RT hazards.
5. **Part 3 Rank 13 (`CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`):** `EventInbox::pop()` on the audio callback (`event_inbox.h:35-42`) is already lock-free; only `EventInbox::push()` (`event_inbox.h:22`) spins. `spectrumRing.push()` (`spectrum.h:33-39`) writes `512 B/block` (8 cache lines, wrapping `32 KiB` over 64 blocks) via lock-free atomics and is not an RT hazard. Note that `outputPeak()` (`output_peak.h:87`) has the **same** function-local static guard hazard as `noteDiagnostics()` (`output_peak.h:136`).
6. **Part 3 Rank 15 (`PARTLY WRONG` — `FxPool::retire()` leak `REFUTED`):** `FxPool::typeChanged()` (`core/engine-own/fx/fx_chain.cpp:127-152`) is the sole post-`prepare()` producer, calls `collect(slot)` (`line 132`) before allocating, and deletes unconsumed `s.pending` (`lines 149-151`). The audio thread holds at most 2 pointers (`sl.cur`, `sl.from`, `fx_chain.h:55-58`), so `grave[4]` occupancy is bounded by `2 <= 4` and **can never leak** (no task). Also, **Convolve::reset()** (`core/fx-serum-b/convolve.cpp:103-122`, noted in Verification 2 "Seen in Passing") already uses `std::fill` on `m_tailFftHistL/R` on `develop` (no allocation after `prepare()`).

---

## 2. Baseline Measurements to Make BEFORE Any Change

Run these commands on the current `develop` tree to confirm baseline behavior, record CPU/render references, and expose which hazards are currently untested.

### 2.1 Existing Host & Sanitizer Suite (macOS / Linux x86_64)

1. **Release build & existing RT/helper/event tests:**
   ```sh
   cmake -S . -B build-base -G Ninja -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_TESTS=ON
   cmake --build build-base --target synthyboy_test_rt synthyboy_test_threads synthyboy_test_own_helper_thread synthyboy_test_own_engine synthyboy_test_own_content_policy synthyboy_test_own_osc_types synthyboy_bench synthyboy_tier_bench synthyboy_preset_render
   ./build-base/core/tests/synthyboy_test_rt
   ./build-base/core/tests/synthyboy_test_own_helper_thread
   ./build-base/core/tests/synthyboy_test_own_content_policy
   ./build-base/core/tests/synthyboy_test_own_osc_types
   sh shell-mpc/tests/run_event_inbox_test.sh
   ```
   - **Record:** Pass/fail status (all pass today because none currently test oversized mono blocks, 64-slot `FxWorker` saturation, concurrent `setContent()` > 8×, `sb-wt-build` start hook, spinlock progress under stall, or `applyState` tearing).

2. **ThreadSanitizer baseline (matching `.github/workflows/build.yml:209-239`):**
   ```sh
   cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_TESTS=ON \
     -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
   cmake --build build-tsan --target synthyboy_test_threads synthyboy_test_own_helper_thread synthyboy_test_own_engine
   TSAN_OPTIONS=halt_on_error=1 ./build-tsan/core/tests/synthyboy_test_threads
   TSAN_OPTIONS=halt_on_error=1 ./build-tsan/core/tests/synthyboy_test_own_helper_thread
   ```
   - **Record:** Zero TSan reports on existing tests (baseline before new stress tests are added).

3. **Exact bit-identity render baseline (`docs/MPC.md:342` K6 rule):**
   ```sh
   ./build-base/tools/preset-render/synthyboy_preset_render --engine own --tier all --float32 --hash --out /tmp/rt-base-renders
   ```
   - **Record:** All 960 `.hash` files (`160 presets × 3 tiers × single + chord`) for a comparison of the `.hash` files of the two render folders after all tasks land.

4. **Desktop benchmark baseline:**
   ```sh
   ./build-base/bench/synthyboy_bench --engine own --patch both --seconds 5
   ```
   - **Record:** Mean / p99 / worst ms per 128-frame block across `low`, `medium`, `high`.

### 2.2 Device Baseline Checks (MPC Key 61 — ARMv7)

1. **Wavetable builder thread scheduling, core affinity, and idle wakeups (`Rank 10`):**
   - Load SynthyBoy on Key 61 (overlay build, `lazyTables = true`), leave idle for 10 s, then run:
     ```sh
     ps -eLo pid,tid,psr,cls,rtprio,ni,comm | grep -E 'sb-|MPC'
     # Record nr_switches over a 10 s idle window for the wavetable builder thread:
     grep nr_switches /proc/<mpc_pid>/task/<wt_tid>/sched; sleep 10; grep nr_switches /proc/<mpc_pid>/task/<wt_tid>/sched
     ```
   - **Record:** `cls` (expected `TS` vs `FF` if RT-inherited), `psr` (core `0-1` vs `2-3`), `comm` (unnamed host thread name today vs `sb-wt-build` after T6), and `delta(nr_switches) / 10 s` (expected `~100 / s` today from `sleep_for(10ms)` at `wavetable_bank.cpp:496`, `0 / s` after T6).

2. **`RLIMIT_MEMLOCK` and `VmRSS` across preset switches (`Rank 15`):**
   - On Key 61, inspect `/proc/<mpc_pid>/limits` (`Max locked memory`) to decide the `mlock` `HYPOTHESIS`.
   - Switch between 5 consecutive sample/spectral/IR presets (`Spectral Vowel Freeze` → `Acid Saw Reso` → `Spectral Gong Pad` → `Init` → `Bowed MPE Strings`) and record `/proc/<mpc_pid>/status` `VmRSS` after each switch (confirming whether 1 dead `ContentSet` stays pinned until the 2nd subsequent switch).

3. **Key 61 CPU & callback-gap baseline (`tier_bench` + `SYNTHYBOY_MPC_CALLBACK_STATS=1`):**
   - Record `rtrun` `tier_bench --core 3 --config low --seconds 4 --repeat 3` on the 12 heaviest presets (`Diffusor Cloud Pad`, `Stretched Glass Choir`, `Lorenz Garden`, `Mid Side Halo`, `Frozen Pluck Drone`, `Bowed MPE Strings`, `Twin Warp Lead`, `Spectral Vowel Freeze`, etc.) and `callback_stats` during live preset switching while holding an 8-note chord (max gap µs, `>= 1.5x` count, and `minflt`/`majflt` once T5 lands).

### 2.3 Missing Automated Failure Proofs (Built by Commit 1 of Each Task)

No existing test triggers H1 (concurrent `setContent()` > 8×), H2b (Apple/Windows `WakeSignal` mutex contention), H2c (64-slot `FxWorker` saturation + `g_threadFrees`), H3 (`sb-wt-build` start hook & cross-instance request loss), H4a (mono `numFrames = 8192` allocation), H5a (`EventInbox` producer stall livelock), H5b (`__cxa_guard_acquire` in `outputPeak`/`noteDiagnostics`), H5c (live torn preset frames), or H6a (idle `contentInUse` retention + 48-frame mid-period UAF). Per the commit-pair workflow, **commit 1 of each task (T1–T13)** builds that exact failing test first, proving the hazard is real before commit 2 fixes it.

---

## 3. Design Options and Chosen Design

### Authoritative Specification Constraints

- **`docs/ARCHITECTURE.md` §3.7 (lines 180–186):** *"No `new/delete/malloc`, no `std::mutex`, no file or syslog calls in `Engine::process`. Unit test links a `malloc` hook and fails on any allocation during `process`, for each tier and for state/preset changes during playback. ThreadSanitizer and AddressSanitizer/UBSan builds of the core tests in CI (Linux x86_64). Determinism test: same preset + same notes → bit-identical output on repeated runs."*
- **`docs/ARCHITECTURE.md` §3.8 (lines 191–192):** *"`realtime` (default false = offline, deterministic: the audio block that posts a Convolve rebuild waits for the builder thread and takes the result over in that very block; true = live, the audio thread never waits)... `IEngine::setContent (StateContent)` / `setContentStore ()` run on the message thread: the files are decoded there into a snapshot that the audio thread takes over through an atomic pointer without a lock; retired snapshots are freed by the message thread only."*
- **`core/include/synthyboy/engine.h:53-54,124-128`:** *"`maxBlockFrames` is a hint; `process()` accepts any block size... Renders `numFrames` of audio, overwriting `left` and `right` (`right` may be null for a mono output; before `prepare()` the output is silence)... No allocation, no locks, no exceptions, no I/O."*
- **`docs/engine-spec/README.md:53-60`:** Offline renders (`realtime = false`) are deterministic and bit-identical across runs; control rate runs on the fixed 32-sample grid (`kControlPeriod = 32`).

---

### 3.1 Group A: `ContentHub` UAF/Race & `ContentSet` `contentInUse` Lifecycle (Ranks 2 & 15a)

| Option | How it works | Sound change | CPU & Memory | RT safety | Risk |
|---|---|---|---|---|---|
| **A1. Keep `ContentHub` ring, enlarge `kRing` to 64 + lock `contentMutex`** | Serialize `publish()` under `contentMutex`, bump `kRing = 8 → 64`. | None (bit-identical). | **Bad:** `64 * 613 KB = 39.2 MB`/instance! Still theoretically unbounded UAF if audio pauses. | **Unsafe** (ring can still wrap). | High (OOM on 4 GB Key 61 + UAF remains). |
| **A2. Fold `OwnContent` directly inside `ContentSet` under one hazard pointer** | Move `OwnContent` into `ContentSet`; `setContent()` builds one combined object; remove `ContentHub`. | None (bit-identical). | Good: single Dekker acquire/release; saves `4.9 MB` ring. Touches 8+ files across `voice.cpp`, `mod_matrix.cpp`, `content_set.*`, `own_engine.*`. | **Safe** (provided `controlPos != 0` pin & ABA guard are added). | Medium (larger multi-file refactor; `setContent` with only LFO edits would rebuild heavy sample/STFT `ContentSet` unless split). |
| **A3. (CHOSEN) Upgrade `ContentHub` to Dekker hazard pointer + sparse `modCurve[64]` + safe `contentInUse` idle release & mid-period pin** | Give `ContentHub` its own `publishMutex` + Dekker hazard pointer (`current`/`inUse` + `owned` vector + monotonic `generation`), move `ownContent.publish()` and `noteTuning.setContent()` under `contentMutex`, make `OwnContent::modCurve[64]` `std::array<std::unique_ptr<CompiledModCurve>, 64>`, and for **both** `ContentHub` and `ContentSet`: keep `inUse` pinned across `controlPos != 0 && activeMask != 0` (eliminating the mid-period UAF on non-multiple-of-32 blocks), update at `controlPos == 0` or `activeMask == 0`, and release `inUse = nullptr` at block end whenever `activeMask == 0` (and in `reset()`/`silence()`). | **None (bit-identical).** | **CPU:** +1 atomic `seq_cst` store/load on `activeMask == 0` / block start (~10–20 ns/block = 0.0007% of 2.902 ms).<br>**RAM:** `OwnContent` drops from `4.90 MB` (8-slot ring) → **`88.8 KB`** (`1` live set idle, sparse `modCurve[64]`, saving **4.81 MB/instance**); dead `ContentSet` freed immediately on next publish when idle (saving 5–30 MB on sample/spectral presets). | **Provably lock-free, UAF-free, ABA-free** (when `activeMask == 0`, `Voice::start()` clears all voice caches `voice.cpp:292-312` & `lib_osc.cpp:180`, and monotonic `generation` prevents `ModMatrix`/`morphCache` ABA; when `activeMask != 0`, old set stays pinned until `controlPos == 0` rebinds every active voice, guaranteeing `C_new != C_old`). | **Low** — cleanly splits into 3 tasks of <= 3 files each (T8, T9, T10), keeps `ContentHub::compileLfoShape` intact for `core/src/lfo_display.cpp:66`, and avoids rebuilding samples/STFT on LFO curve drags. |

**Why A3 wins over A2:** In `shell-mpc/synthyboy_mpc.cpp:1472`, dragging a custom LFO point calls `setContent()` frequently; keeping `ContentHub` separate from sample/STFT decoding while giving it the exact same Dekker hazard-pointer reclamation as `ContentSet` preserves fast LFO editing, cleanly fits the `<= 3 files/task` worker constraint, and eliminates both UAF and ABA.

---

### 3.2 Group B: Cross-Platform `WakeSignal` & `ConvolveModule` Job Safety (Rank 3)

| Option | How it works | Sound change | CPU & Memory | RT safety | Risk |
|---|---|---|---|---|---|
| **B1. Atomic-flag polling (1–2 ms `sleep_for`) on helper threads** | Remove OS wait primitives; helper threads wake every 1–2 ms to check atomic flags. | None (slight 1–2 ms delay on FX build). | **Worse:** reintroduces 500–1000 idle wakeups/s that Round 13 T1 (`docs/MPC.md:340`) explicitly eliminated. | Lock-free on audio thread. | Low, but regresses idle power/jitter and fails `test_own_helper_thread.cpp` ("never waits on a timeout"). |
| **B2. Dedicated watcher thread polling an atomic flag on non-Linux only** | Keep Linux futex; on Apple/Windows add a 2 ms polling thread that forwards to `cv`. | None. | Extra thread + 500 wakeups/s on iOS (drains battery). | Lock-free on audio thread. | Medium (extra thread lifecycle/join complexity). |
| **B3. (CHOSEN) 3-state lock-free `WakeSignal` (`0=idle, 1=signaled, 2=sleeping`) with native non-blocking wake per OS + `ConvolveModule` failed-submit `impulse.reset()` & `job.tone`** | **Linux:** `FUTEX_WAIT_PRIVATE(..., 2u)` / `FUTEX_WAKE_PRIVATE` only on `2 → 1`.<br>**Apple (macOS/iOS):** `<dispatch/dispatch.h>` `dispatch_semaphore_t` (`dispatch_semaphore_signal` only on `2 → 1` — lock-free atomic inc + non-blocking Mach trap, 1:1 signal/wait pairing so count stays in `{-1,0,+1}` and `dispatch_release` never traps).<br>**Windows:** Win32 `CreateSemaphoreW` / `ReleaseSemaphore` / `WaitForSingleObject` with the same 3-state `2 → 1` guard (non-blocking wake, no extra link lib).<br>**Convolve:** Add `job.impulse.reset()` (`fx_modules.cpp:994`) and `specJob.impulse.reset()` (`825`) on `!submit()` (safe because `f.content` is pinned by `contentInUse`, so `use_count` goes `>= 2 → >= 1`, never `0`!), and copy `job.tone = live.tone` into `Job` so `runJob()` reads `j->tone` instead of racing on `m->live.tone`. | **None (bit-identical).** | **CPU:** Fewer syscalls on Linux too (`0` `SYS_futex` calls when worker is already awake vs 1 per `0 → 1` today); `0` idle wakeups/s on all OSes.<br>**RAM:** Replaces `std::mutex` + `std::condition_variable` (~112 B on macOS) with `std::atomic<uint32_t>` + `dispatch_semaphore_t` (16 B). | **100% lock-free / non-blocking** on audio thread on Linux, iOS, macOS, and Windows; zero RT `free()`; zero TSan race on `live.tone`. | **Low** — exact 1:1 `signal`/`wait` pairing verified by 200,000-cycle ping-pong + multi-producer TSan stress test in `test_own_helper_thread.cpp`. |

---

### 3.3 Group C: Wavetable Bank Builder Thread (Rank 10)

| Option | How it works | Sound change | CPU & Memory | RT safety | Risk |
|---|---|---|---|---|---|
| **C1. Add `runHelperThreadStartHook` only, keep `sleep_for(10ms)`** | Only fixes scheduling/affinity; leaves 100 wakeups/s and `latest[6]` cross-instance overwrite. | None. | Keeps 100 idle wakeups/s forever. | Fixes core isolation, keeps wakeup jitter. | Low, incomplete. |
| **C2. Per-instance 6-slot table + `WakeSignal`** | Allocate 6 request slots per `OwnEngine` instance (requires registering/unregistering instance slot tables under a mutex or fixed `kMaxInstances` array). | None. | Extra bookkeeping (`16 * 6 * 4 = 384 B`). Preserves skipping swept-past tables. | Lock-free on `requestBuiltinWavetable`. | Medium (changes `requestBuiltinWavetable(int slot, int choice)` API and breaks existing tests at `test_own_osc_types.cpp:657-659`). |
| **C3. (CHOSEN) `runHelperThreadStartHook("sb-wt-build")` + `WakeSignal` + `std::atomic<uint32_t> wantedMask`** | Call `runHelperThreadStartHook("sb-wt-build")` first in `Bank::run()` (before `::setpriority(..., 10)`), replace `latest[6]` with `std::atomic<uint32_t> wantedMask { 0u }` (16 bits cover all `kBankSlots = 16` choices), wake via `WakeSignal` on unbuilt request / unpause / stop, and sleep on `wake.wait()` when idle or paused. | **None (bit-identical).** | **CPU:** `100 → 0` idle wakeups/s; `stopBackground()` joins immediately instead of up to 10 ms.<br>**RAM:** `-20 B` (`1` atomic `uint32_t` vs `6` atomic `int`s). *(Trade-off: rapid knob sweep past unbuilt morph tables queues them on nice +10 cores 0–1 at most once per process; max 8 morph tables total.)* | **Safe:** Audio-thread `requestBuiltinWavetable` is 1 acquire load + 1 `fetch_or` + lock-free `WakeSignal::notify()` (after T1). Builder runs `SCHED_OTHER` nice +10 on non-audio cores `0–1`. | **Low** — keeps `requestBuiltinWavetable(int slot, int choice)` signature unchanged (`test_own_osc_types.cpp:657-659` keeps passing). |

---

### 3.4 Group D: Oversized Mono Block Allocation & `convolveOn` Static Guard (Rank 12)

| Option | How it works | Sound change | CPU & Memory | RT safety | Risk |
|---|---|---|---|---|---|
| **D1. Chunk mono loop to `scratchRight.size()` (4096 frames)** | Keep `scratchRight` (`16 KiB` heap), wrap `process()` in an outer `4096`-frame loop when `right == nullptr`. | None. | Still keeps `16 KiB` heap per instance and redundant `std::fill` on `scratchRight`. | Zero RT alloc. | Medium (outer event-offset slicing bug risk). |
| **D2. (CHOSEN) Stack `float monoRight[kControlPeriod]` inside the existing `while (pos < numFrames)` loop + hoist `convolveOnIndex` to `OwnEngine` member** | Inside the existing 32-frame chunk loop (`own_engine.cpp:1461-1755`), use stack `float monoRight[kControlPeriod]` (`128 B`) when `right == nullptr`, pass `chunkRight` to `std::copy`, `master::stage`, and `peakAbsPair`, and immediately fold `left[pos + i] = 0.5f * (left[pos + i] + monoRight[i])`. Remove `scratchRight` completely. Hoist `const int convolveOnIndex` into `OwnEngine::OwnEngine()` (`own_engine.cpp:96`), matching `fxReverbForceIndex`. | **None (bit-identical)** — nothing between `own_engine.cpp:1752` and `1761` reads `left` or `rightOut`. | **CPU:** Slightly faster in mono (hot 128 B stack buffer stays in L1D, drops redundant `std::fill(rightOut)`).<br>**RAM:** **`-16,384 B` (`-16 KiB`)** per `OwnEngine` instance (`0 B` heap). | **100% RT-safe** for any `numFrames` (`1 .. INT_MAX`) and eliminates `__cxa_guard_acquire` in `publishStats()`. | **Very low.** |

---

### 3.5 Group E: MPC Shell Boundary (`EventInbox`, Static Guards, Transactional `applyState`) (Rank 13)

| Sub-item | Option 1 | Option 2 (CHOSEN) | Rationale |
|---|---|---|---|
| **13a. `EventInbox`** | Keep spinlock, add `yield()`/`pause` backoff. Still risks priority inversion if RT producer spins behind preempted non-RT producer. | **Vyukov bounded lock-free MPSC ring** (exact pattern of `shell-mpc/play_log.h:190-215`) with per-slot `std::atomic<size_t> seq`, wrap-safe `ptrdiff_t(seq - h)`, and `!essential` check `h - tail >= N - kReserve` before `head.compare_exchange_weak`. | Truly lock-free `push()` and wait-free `pop()`, preserves exact 256-slot note-off reserve and FIFO order, +`4096 * 4 B = 16 KiB` (32-bit) / `32 KiB` (64-bit) per instance for sequence words, **bit-identical audio**. |
| **13b. `outputPeak()` & `noteDiagnostics()`** | Only call both once in `VSTPluginMain` (still leaves `__cxa_guard_acquire` check branch on every call). | **Namespace-scope constant-initialized `inline OutputPeak gOutputPeak;` and `inline NoteDiagnostics gNoteDiagnostics;`** (C++17 `constexpr` atomic init = zero `__cxa_guard_acquire` symbol!) **plus** belt-and-suspenders warm call in `VSTPluginMain`. | Eliminates guard symbols completely from `effProcessEvents` and `pluginProcessReplacing`; zero RAM/CPU cost; **bit-identical audio**. |
| **13c. `applyState()` batching** | **Option 1:** Double-buffered heap-allocated `ParamSnapshot` swapped by atomic pointer + hazard reclamation (allocates `10.5 KB` per preset load; complicates wait-free single `setParam()`).<br>**Option 2:** Lock `std::mutex` in `process()` (FORBIDDEN by RT rules). | **Option 3 (CHOSEN): Non-blocking Try-Seqlock `IEngine::setParamSnapshot(const float* plain, size_t count)`** over existing `std::atomic<float> params[]` + pre-allocated `baseStaging` (`10.5 KB`). `setParamSnapshot()` locks `paramBatchMutex` (message threads only), increments `paramBatchSeq` odd → writes all `2,637` relaxed atomics → updates wavetable/FX requests with `<= 1` `fxWake.notify()` → increments `paramBatchSeq` even → bumps `paramGen` once. `OwnEngine::process()` checks `s1 = paramBatchSeq.load(acquire)`: if odd (and `snapValid`), skips snapshot for 1 block (2.9 ms); else copies `params[]` into `baseStaging`, re-checks `s2 == s1`, and `O(1)`-swaps `base.swap(baseStaging)`. | **Wait-free** on audio thread (0 retries, 0 locks, 0 allocations), **TSan-clean** (all `params[i]` are `std::atomic<float>`, avoiding non-atomic seqlock UB per Boehm 2012), single `paramGen` bump instead of 2,637, **0 torn half-A/half-B frames**. Sound: **bit-identical** in steady state and offline renders; live preset switches eliminate transient torn-parameter glitches. |

*(Note on Rank 15c `CallbackStats` page-fault tracking: Option 1 calling `getrusage(RUSAGE_THREAD)` inside `CallbackStats::begin()` adds a syscall to every audio block and is rejected. **Chosen Option 2:** Capture `audioTid` once on the first callback via `syscall(SYS_gettid)` — diagnostic build `SYNTHYBOY_MPC_CALLBACK_STATS=1` only, compiled out in shipping releases — and read `/proc/self/task/<audioTid>/stat` fields 10 & 12 (`minflt`, `majflt`) on the low-priority `sb-cb-stats` writer thread `callback_stats.h:437-450`. Zero syscalls on steady-state audio callbacks!)*

---

## 4. Task Breakdown and Batch Schedule

Every task is **one commit pair**:
1. **Commit 1 (`test(...): ...`):** adds the failing test reproducing the exact hazard on current code.
2. **Commit 2 (`fix(...): ...`):** applies the minimal fix so the test passes.

Every task touches **at most 3 files total** (production + test combined). **No two tasks in the same batch edit the same file.**

### 4.1 Batch & Dependency Matrix

| Batch | Task | Title | Editable Files (count <= 3) | Depends On |
|---|---|---|---|---|
| **Batch 1** | **T1** | Cross-platform lock-free 3-state `WakeSignal` | `core/engine-own/dsp/helper_thread.h`, `core/tests/test_own_helper_thread.cpp` (2) | — |
| **Batch 1** | **T2** | `ConvolveModule` failed-submit `impulse.reset()`, `job.tone` & `g_threadFrees` hook | `core/engine-own/fx/fx_modules.cpp`, `core/tests/alloc_hook.h`, `core/tests/test_rt_alloc.cpp` (3) | — |
| **Batch 1** | **T3** | Lock-free Vyukov MPSC `EventInbox` with 256-slot note-off reserve | `shell-mpc/event_inbox.h`, `shell-mpc/tests/event_inbox_host.cpp` (2) | — |
| **Batch 1** | **T4** | Constant-initialized `outputPeak()` & `noteDiagnostics()` + `VSTPluginMain` warm | `shell-mpc/output_peak.h`, `shell-mpc/synthyboy_mpc.cpp`, `shell-mpc/tests/output_stage_lanes_host.cpp` (3) | — |
| **Batch 1** | **T5** | Diagnostic `minflt`/`majflt` tracking in `CallbackStats` (writer thread) | `shell-mpc/callback_stats.h`, `shell-mpc/tests/callback_stats_host.cpp` (2) | — |
| **Batch 2** | **T6** | Wavetable bank builder: start hook, `WakeSignal`, multi-instance `wantedMask` | `core/engine-own/osc/wavetable_bank.cpp`, `core/tests/test_own_helper_thread.cpp` (2) | T1 |
| **Batch 2** | **T7** | Stack-chunked mono output (`scratchRight.assign` removal) & `convolveOnIndex` hoist | `core/engine-own/own_engine.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_rt_alloc.cpp` (3) | T2 |
| **Batch 3** | **T8** | `ContentHub` Dekker hazard pointer, mutex serialization & idle release | `core/engine-own/voice/own_content.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_threads.cpp` (3) | T7 |
| **Batch 4** | **T9** | `ContentSet` `contentInUse` safe idle clear & mid-period `controlPos != 0` pin | `core/engine-own/own_engine.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_own_content_policy.cpp` (3) | T8 |
| **Batch 4** | **T10** | Sparse `OwnContent::modCurve[64]` (`613 KB → 88.8 KB`) | `core/engine-own/voice/own_content.h`, `core/engine-own/matrix/mod_matrix.cpp`, `core/tests/test_own_engine.cpp` (3) | T8 |
| **Batch 5** | **T11** | `OwnEngine` wait-free try-seqlock `setParamSnapshotImpl()` | `core/engine-own/own_engine.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_own_engine.cpp` (3) | T9, T10 |
| **Batch 6** | **T12** | Promote `IEngine::setParamSnapshot()` virtual seam & TSan torn-preset test | `core/include/synthyboy/engine.h`, `core/engine-own/own_engine.h`, `core/tests/test_threads.cpp` (3) | T11 |
| **Batch 7** | **T13** | Wire `shell-mpc` `applyState()` to `setParamSnapshot()` & live `effSetChunk` test | `shell-mpc/synthyboy_mpc.cpp`, `shell-mpc/tests/plugin_review_host.cpp` (2) | T4, T12 |

**Collision audit:** Batch 1 touches 12 distinct files; Batch 2 touches 5 distinct files; Batch 3 touches 3; Batch 4 touches 6 distinct files; Batches 5–7 each have 1 task (2–3 files). Zero file overlaps within any batch.

---

### 4.2 Detailed Task Specifications

#### T1 — Cross-platform lock-free 3-state `WakeSignal` *(Part 3 Rank 3a)*
- **Editable files:** `core/engine-own/dsp/helper_thread.h`, `core/tests/test_own_helper_thread.cpp`.
- **What to change:**
  1. In `core/engine-own/dsp/helper_thread.h`:
     - Replace the `#if defined(__linux__) ... #else std::mutex/std::condition_variable` implementation of `WakeSignal` (`lines 32-83`) with a unified 3-state atomic state machine (`std::atomic<uint32_t> word { 0u }`; `0 = idle`, `1 = signaled`, `2 = sleeping`, with `static_assert(std::atomic<uint32_t>::is_always_lock_free)`):
       - `notify() noexcept`: `if (word.exchange(1u, std::memory_order_acq_rel) == 2u) osWake();`
       - `wait() noexcept`: loop: `uint32_t expected = 1u; if (word.compare_exchange_strong(expected, 0u, std::memory_order_acq_rel)) return; expected = 0u; if (word.compare_exchange_strong(expected, 2u, std::memory_order_acq_rel)) osWait();`
       - **Linux (`#if defined(__linux__)`):** `osWake()` calls `::syscall(SYS_futex, reinterpret_cast<uint32_t*>(&word), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0)`; `osWait()` calls `::syscall(SYS_futex, reinterpret_cast<uint32_t*>(&word), FUTEX_WAIT_PRIVATE, 2u, nullptr, nullptr, 0)`.
       - **Apple (`#elif defined(__APPLE__)`):** include `<dispatch/dispatch.h>`; member `dispatch_semaphore_t sem = dispatch_semaphore_create(0);`, destructor `dispatch_release(sem);` (delete copy/move constructors); `osWake()` calls `dispatch_semaphore_signal(sem);`; `osWait()` calls `dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);`.
       - **Windows (`#elif defined(_WIN32)`):** include `<windows.h>` (with `NOMINMAX`); member `HANDLE sem = CreateSemaphoreW(nullptr, 0, 1, nullptr);`, destructor `CloseHandle(sem);`; `osWake()` calls `ReleaseSemaphore(sem, 1, nullptr);`; `osWait()` calls `WaitForSingleObject(sem, INFINITE);`.
       - Remove `<mutex>` and `<condition_variable>` includes from `helper_thread.h`.
       - Expose `static constexpr bool kNotifyLockFree = true;` on `WakeSignal` (only enabled when no `std::mutex` is used; or keep it unconditionally on the 3-state implementation).
- **Test (fails before, passes after) — in `core/tests/test_own_helper_thread.cpp`:**
  - **Signal / input:** (a) Compile-time/size check: `sizeof(own::WakeSignal) <= 16` (`std::mutex + std::condition_variable + bool` on macOS/libc++ is `64 + 48 + 8 = 120 B`, on Linux libstdc++ `sizeof(uint32_t) = 4 B`, on Apple `atomic<uint32_t> + dispatch_semaphore_t = 16 B`, on Windows `16 B` — **FAILS on Apple/Windows before fix (`120 B > 16 B`), PASSES after (`16 B <= 16 B`)!**); (b) Multi-producer TSan stress test: 4 producer threads × 50,000 `notify()` calls concurrently with 1 consumer `wait()` loop (`200,000` total items, scaled under TSan via `sbtest::scaled(50000, 5000)`), verifying `taken == total` with zero lost wake-ups and clean destructor teardown (no `dispatch_release` trap on Apple!).
  - **Threshold:** `sizeof(own::WakeSignal) <= 16` (before on macOS/iOS: `120 B` → FAIL; after: `16 B` → PASS) and `taken == total` under TSan.
  - **Bit-identical sound:** Yes. Print `WAKE-SIGNAL-CHECKSUM` (FNV-1a 64-bit hash of a 256-frame offline `OwnEngine` render with Distortion on `fxi01`).

#### T2 — `ConvolveModule` failed-submit `shared_ptr<Impulse>` reset, `job.tone` snapshot & audio-thread free hook *(Part 3 Rank 3b + adjacent Part 2 #4 `live.tone` race)*
- **Editable files:** `core/engine-own/fx/fx_modules.cpp`, `core/tests/alloc_hook.h`, `core/tests/test_rt_alloc.cpp`.
- **What to change:**
  1. In `core/tests/alloc_hook.h`: add `std::atomic<unsigned long> g_threadFrees { 0 };` next to `g_frees` (`line 24`), and in `countedFree` (`lines 57-62`) and `countedAlignedFree` (`lines 64-73`) add `if (t_countThisThread) g_threadFrees.fetch_add(1, std::memory_order_relaxed);` when `p != nullptr`.
  2. In `core/engine-own/fx/fx_modules.cpp`:
     - In `ConvolveModule::Job` (`lines 966-971`): add `float tone = 0.0f;`.
     - In `ConvolveModule::startBuild()` (`lines 986-995`): set `job.tone = live.tone;` before `jobBusy.store(1, release);`, and in the `!FxWorker::instance().submit(&ConvolveModule::runJob, &job)` failure branch (`lines 993-994`) call `job.impulse.reset();` **before** `jobBusy.store(0, release);`. (Because `f.content` is pinned by `contentInUse` for the entire `process()` call, `f.content->impulse` holds a live reference so `use_count` drops from `>= 2` to `>= 1`, never `0`.)
     - In `ConvolveModule::runJob()` (`line 1014`): change `c->params.tone = m->live.tone;` to `c->params.tone = j->tone;`.
     - In `ConvolveModule::makeupPower()` (`lines 824-825`): in the `!FxWorker::instance().submit(&ConvolveModule::runSpectrumJob, &specJob)` failure branch call `specJob.impulse.reset();` **before** `specBusy.store(0, release);`.
- **Test (fails before, passes after) — in `core/tests/test_rt_alloc.cpp` (`synthyboy_test_rt`):**
  - **Signal / input:** Deterministic 64-slot `FxWorker` saturation test (needs `#include "fx/fx_module.h"` — note `core/tests/CMakeLists.txt:182` does not add `core/engine-own` to `synthyboy_test_rt`, so include `"../engine-own/fx/fx_module.h"` via `PROJECT_SOURCE_DIR/core/src`? Wait: `core/tests/CMakeLists.txt:13` adds `${PROJECT_SOURCE_DIR}/core/src`, so `"../engine-own/fx/fx_module.h"` resolves cleanly without editing `CMakeLists.txt`!).
    1. Submit 64 barrier jobs (`FxWorker::instance().submit(waitOnBarrier, &barrier)`) that block until `releaseBarrier` is set (occupying all `kJobs = 64` slots of `FxWorker`, `fx_module.h:59`).
    2. Create `OwnEngine`, enable `convolve.on = 1`, `convolve.ir = 1`, `convolve.size = 50`, `Quality q = qualityForTier(Tier::high); q.realtime = true;`, `prepare(48000, 128, q)`.
    3. Load `MemoryContentStore` with `ir1.wav` (`512` stereo frames), call `setContent(c1)` and run `process()` once → `startBuild()` sets `job.impulse = c1.impulse`, `submit()` returns `false`.
    4. Load `ir2.wav`, call `setContent(c2)`, run `process()` once with `convolve.on = 0` (so `contentInUse` advances to `c2` without overwriting `job.impulse`), then call `setContent(c3)` (forcing `publishContent` to destroy `c1`, leaving `job.impulse` as the **sole owner** `use_count == 1` of `ir1`'s `Impulse`!).
    5. Re-enable `convolve.on = 1`, snapshot `beforeFrees = g_threadFrees.load()` and `beforeAllocs = g_allocations.load()`, call `process()` once, then release `releaseBarrier = true` and join/drain `FxWorker`.
    6. Repeat the same test on `Tier::low` (`q.fxMakeup = true`) to exercise `makeupPower()` / `specJob.impulse`!
  - **Measured quantity & threshold:** `g_threadFrees - beforeFrees == 0` and `g_allocations - beforeAllocs == 0`.
  - **Before vs after (computed from code):** Before: `job.impulse = f.content->impulse` drops `use_count` from `1 → 0`, freeing `Impulse` + `std::vector<float> data` + `shared_ptr` control block = **`>= 2` audio-thread frees (`g_threadFrees >= 2`) → FAIL**. After: `job.impulse` was already `nullptr`, so **`g_threadFrees == 0` → PASS**.
  - **Bit-identical sound:** Yes. Print `CONVOLVE-JOB-CHECKSUM` on an offline (`realtime = false`) Convolve render with `ir1.wav` (`convolve.tone = 0.5`).

#### T3 — Lock-free Vyukov MPSC `EventInbox` preserving 256-slot note-off reserve *(Part 3 Rank 13a)*
- **Editable files:** `shell-mpc/event_inbox.h`, `shell-mpc/tests/event_inbox_host.cpp`.
- **What to change:**
  1. In `shell-mpc/event_inbox.h`:
     - Remove `std::atomic_flag producers = ATOMIC_FLAG_INIT;` (`line 51`).
     - Add `static_assert(std::atomic<size_t>::is_always_lock_free, "lock-free indices");`.
     - Replace `E slots[N];` with `struct Slot { std::atomic<size_t> seq { 0 }; E data {}; }; Slot slots[N];` and initialize `slots[i].seq.store(i, std::memory_order_relaxed)` in `EventInbox()` constructor (`i = 0..N-1`).
     - In `push(const E& e, bool essential) noexcept`:
       - Loop reading `h = head.load(relaxed)`, `Slot& s = slots[h & (N - 1)]`, `seq = s.seq.load(acquire)`, `diff = static_cast<std::ptrdiff_t>(seq - h)`.
       - If `diff == 0`: if `!essential && (h - tail.load(acquire)) >= N - kReserve`, increment `dropped` and return `false`. Otherwise try `head.compare_exchange_weak(h, h + 1, relaxed)`: on success write `s.data = e; s.seq.store(h + 1, release); return true;`.
       - Else if `diff < 0`: increment `dropped` and return `false` (ring full).
       - Else: reload `h = head.load(relaxed)`.
     - In `pop(E& out) noexcept`:
       - Read `t = tail.load(relaxed)`, `Slot& s = slots[t & (N - 1)]`; if `s.seq.load(acquire) != t + 1` return `false`.
       - Copy `out = s.data;`, store `tail.store(t + 1, release);`, then `s.seq.store(t + N, release);`, return `true`.
- **Test (fails before, passes after) — in `shell-mpc/tests/event_inbox_host.cpp` (run via `shell-mpc/tests/run_event_inbox_test.sh`, both `-O2` and `-fsanitize=thread`):**
  - **Signal / input:** Deterministic `StallingEv` lock-freedom proof test:
    - Define `struct StallingEv { int seq = 0; bool stall = false; StallingEv& operator=(const StallingEv& o) { ... if (o.stall) { p1InCopy.store(true, release); waitUntil(p2Done, 200ms); } seq = o.seq; return *this; } };`.
    - Producer 1 pushes `StallingEv{1, true}`; once `p1InCopy` is true, Producer 2 pushes `StallingEv{2, false}` and sets `p2Done = true`.
    - Also add a 4-producer × 25,000-event + 1-consumer TSan stress test verifying exact count `100,000`, zero duplicates, zero losses.
  - **Measured quantity & threshold:** `p2Done.load()` becomes `true` before Producer 1's 200 ms timeout (`p2Done == true` and elapsed `< 50 ms`).
  - **Before vs after (computed from code):** Before (`producers.test_and_set()` spinlock at `event_inbox.h:22`): Producer 2 spins behind Producer 1 for the entire 200 ms timeout → **`p2Done == false` → FAIL**. After (Vyukov MPSC): Producer 2 claims slot 1 independently in `< 1 ms` → **`p2Done == true` → PASS**.
  - **Bit-identical sound:** Yes (event transport only, FIFO order and frame offsets preserved; existing tests in `event_inbox_host.cpp:20-58` assert exact ordering and 256-slot note-off reserve).

#### T4 — Constant-initialized `outputPeak()` & `noteDiagnostics()` + `VSTPluginMain` warm *(Part 3 Rank 13b)*
- **Editable files:** `shell-mpc/output_peak.h`, `shell-mpc/synthyboy_mpc.cpp`, `shell-mpc/tests/output_stage_lanes_host.cpp`.
- **What to change:**
  1. In `shell-mpc/output_peak.h`:
     - Add `constexpr OutputPeak() noexcept = default;` and `constexpr NoteDiagnostics() noexcept = default;` (all members are `std::atomic` with constant initializers; add `static_assert(std::atomic<float>::is_always_lock_free)` and `static_assert(std::atomic<uint32_t>::is_always_lock_free)`).
     - Replace function-local `static OutputPeak p;` (`lines 85-89`) and `static NoteDiagnostics nd;` (`lines 134-138`) with namespace-scope C++17 inline variables:
       ```cpp
       inline OutputPeak gOutputPeak {};
       inline OutputPeak& outputPeak() noexcept { return gOutputPeak; }
       inline NoteDiagnostics gNoteDiagnostics {};
       inline NoteDiagnostics& noteDiagnostics() noexcept { return gNoteDiagnostics; }
       ```
  2. In `shell-mpc/synthyboy_mpc.cpp` (`VSTPluginMain`, right after line `3360`): touch `(void) synthyboy_mpc::outputPeak(); (void) synthyboy_mpc::noteDiagnostics();` explicitly before any instance is created.
- **Test (fails before, passes after) — in `shell-mpc/tests/output_stage_lanes_host.cpp`:**
  - **Signal / input:** Static compile-time test `constexpr synthyboy_mpc::OutputPeak kConstPeak{}; constexpr synthyboy_mpc::NoteDiagnostics kConstNd{};` (verifying constant-initializability without dynamic init guards) plus verifying `&synthyboy_mpc::outputPeak() == &synthyboy_mpc::gOutputPeak` and `&synthyboy_mpc::noteDiagnostics() == &synthyboy_mpc::gNoteDiagnostics` (and build-worker verification via `nm -u synthyboy_mpc.so` / `objdump` confirming neither `outputPeak()` nor `noteDiagnostics()` emits `__cxa_guard_acquire`).
  - **Before vs after:** Before: `OutputPeak`/`NoteDiagnostics` have no `constexpr` constructor and use function-local statics (`output_peak.h:87,136`) emitting `__cxa_guard_acquire` → **FAIL**. After: constant-initialized inline globals with `0` guard calls → **PASS**.
  - **Bit-identical sound:** Yes (diagnostic metering only; existing NEON vs scalar peak tests in `output_stage_lanes_host.cpp` remain bit-identical).

#### T5 — Diagnostic `minflt`/`majflt` tracking in `CallbackStats` on writer thread *(Part 3 Rank 15c)*
- **Editable files:** `shell-mpc/callback_stats.h`, `shell-mpc/tests/callback_stats_host.cpp`.
- **What to change:**
  1. In `shell-mpc/callback_stats.h`:
     - Under `#if defined(__linux__)`, add `std::atomic<long> audioTid { 0 };` to `CallbackStats`.
     - In `CallbackStats::begin()` (`line 62`): capture TID **once** on the very first callback: `if (audioTid.load(std::memory_order_relaxed) == 0) audioTid.store(static_cast<long>(::syscall(SYS_gettid)), std::memory_order_relaxed);` (0 syscalls on every callback after block 1!).
     - In `CallbackStats::format()` (called only on the low-priority `sb-cb-stats` writer thread, `lines 402-430`): read `/proc/self/task/<audioTid>/stat` (if `audioTid > 0`, parsing fields 10 = `minflt` and 12 = `majflt` after the closing `')'` of `comm`) and also process totals via `getrusage(RUSAGE_SELF, &ru)` (`ru_minflt`, `ru_majflt`), emitting:
       `page_faults: audio_minflt=<N> audio_majflt=<M> proc_minflt=<P> proc_majflt=<Q>\n` (on non-Linux, emit `page_faults: proc_minflt=<P> proc_majflt=<Q>\n` via `getrusage(RUSAGE_SELF)`).
- **Test (fails before, passes after) — in `shell-mpc/tests/callback_stats_host.cpp`:**
  - **Signal / input:** Existing `callback_stats_host` run; parse the generated stats file and assert the `page_faults:` line is present with non-negative integer `proc_minflt`/`proc_majflt` (and on Linux, `audio_minflt`/`audio_majflt`).
  - **Before vs after:** Before: `page_faults:` absent from `CallbackStats::format()` → **FAIL**. After: line present with exact kernel counters → **PASS**.
  - **Bit-identical sound:** Yes (diagnostic build `SYNTHYBOY_MPC_CALLBACK_STATS=ON` only; compiled out in release).

#### T6 — Wavetable bank builder: start hook, `WakeSignal`, and multi-instance `wantedMask` *(Part 3 Rank 10; depends on T1)*
- **Editable files:** `core/engine-own/osc/wavetable_bank.cpp`, `core/tests/test_own_helper_thread.cpp`.
- **What to change:**
  1. In `core/engine-own/osc/wavetable_bank.cpp`:
     - Add `#include "../dsp/helper_thread.h"`.
     - In `struct Bank` (`lines 368-531`):
       - Replace `std::atomic<int> latest[kRequestSlots];` (`line 373`) with `std::atomic<uint32_t> wantedMask { 0u };` and add `WakeSignal wake;`.
       - In `Bank::run()` (`lines 473-498`):
         - First statement: `runHelperThreadStartHook ("sb-wt-build");`
         - Keep `#if defined(__linux__) ::setpriority (PRIO_PROCESS, static_cast<id_t> (::syscall (SYS_gettid)), 10); #endif` immediately after (so `SCHED_OTHER` + non-audio cores from the hook stay in force while nice becomes `+10`).
         - Loop `while (! stop.load (std::memory_order_acquire))`:
           - If `paused.load (std::memory_order_acquire)`: `wake.wait(); continue;`.
           - Atomically take `uint32_t mask = wantedMask.exchange (0u, std::memory_order_acq_rel);`.
           - For each set bit `c` (`0 <= c < kBankSlots`): if `stop.load(acquire)` break; if `paused.load(acquire)` `{ wantedMask.fetch_or(mask, release); break; }` (preserving remaining bits); clear bit `c` from `mask`; if `buildable(c) && state[c].load(acquire) == 0`, call `ensure(c, &stop);`.
           - If no more bits pending (`wantedMask.load(acquire) == 0u && !stop.load(acquire)`): `wake.wait();` (0 wakeups/s at rest!).
       - In `requestBuiltinWavetable(int slot, int choice)` (`lines 567-571`): validate `slot >= 0 && slot < kRequestSlots && buildable(choice)`; if `g_bank.state[choice].load(std::memory_order_acquire) == 0`, do `g_bank.wantedMask.fetch_or(1u << choice, std::memory_order_release); g_bank.wake.notify();`.
       - In `setBuiltinBuilderPausedForTests(bool paused)` (`lines 602-605`): store with `memory_order_release`, and if `!paused`, call `g_bank.wake.notify();`.
       - In `stopBackground()` (`lines 516-530`) and `~Bank()` (`lines 391-396`): store `stop.store(true, std::memory_order_release);` and call `wake.notify();` before `t.join()`.
       - In `resetBuiltinBankForTests()` (`line 607`): also reset `g_bank.wantedMask.store(0u, std::memory_order_relaxed);`.
- **Test (fails before, passes after) — in `core/tests/test_own_helper_thread.cpp`:**
  - **Signal / input:**
    1. *Start-hook check:* Reset bank, install a hook that records all thread names seen into an atomic bitmask (`sb-fx-build`, `sb-wt-build`), create `OwnEngine` with `q.realtime = true; q.lazyTables = true;`, call `prepare(48000, 128, q)`, destroy engine. Assert `"sb-wt-build"` was observed on a non-creator thread ID. (**FAILS before:** `Bank::run()` never calls `runHelperThreadStartHook()` → `sawWtBuild == false`; **PASSES after:** `sawWtBuild == true`.)
    2. *Multi-instance slot-0 overwrite check:* Reset bank, pause builder, start background builder, call `requestBuiltinWavetable(0, 5)` (instance A) then `requestBuiltinWavetable(0, 6)` (instance B, same slot 0!), unpause builder, wait bounded (up to 5 s). Assert **both** `builtinWavetableIfReady(5) != nullptr` AND `builtinWavetableIfReady(6) != nullptr`. (**FAILS before:** `latest[0]` overwrote `5` with `6`, so table `5` stays `nullptr` forever; **PASSES after:** both built!)
    3. *Bit-identical sound:* Print `WT-BANK-CHECKSUM` rendering 256 frames on table 5 and table 6 against offline `ensureAllBuiltinWavetables()`.

#### T7 — Stack-chunked mono output (`scratchRight.assign()` removal) & `convolveOnIndex` hoist *(Part 3 Rank 12; depends on T2)*
- **Editable files:** `core/engine-own/own_engine.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_rt_alloc.cpp`.
- **What to change:**
  1. In `core/engine-own/own_engine.h`:
     - Add `const int convolveOnIndex;` next to `noSmoothingIndex` (`line 279`).
     - Delete `std::vector<float> scratchRight;` (`line 280`).
  2. In `core/engine-own/own_engine.cpp`:
     - In `OwnEngine::OwnEngine()` (`line 96`): initialize `convolveOnIndex (findParamIndex (hashKey ("convolve.on")))`.
     - In `OwnEngine::prepare()` (`line 393`): delete `scratchRight.assign (...)`.
     - In `OwnEngine::process()` (`lines 1326-1340, 1738-1764`):
       - Remove `float* rightOut = right;` and the `scratchRight.assign` / `std::fill(rightOut, ...)` block; if `right != nullptr` and `!prepared`, keep `std::fill(right, right + numFrames, 0.0f);`.
       - Inside `while (pos < numFrames)` (`lines 1738-1754`):
         ```cpp
         float monoRight[kControlPeriod];
         float* chunkRight = right != nullptr ? right + pos : monoRight;
         std::copy (mixL[0], mixL[0] + n, left + pos);
         std::copy (mixR[0], mixR[0] + n, chunkRight);
         {
             master::Params mp;
             ...
             master::stage (left + pos, chunkRight, mixL[3], mixR[3], n, mp, smoothedGain);
         }
         stripBlockPeak[9] = std::max (stripBlockPeak[9], peak::peakAbsPair (left + pos, chunkRight, n));
         if (right == nullptr)
             for (int i = 0; i < n; ++i)
                 left[pos + i] = 0.5f * (left[pos + i] + monoRight[i]);
         ```
       - Remove the post-loop mono fold (`lines 1761-1763`).
     - In `OwnEngine::publishStats()` (`lines 1804-1806`): delete `static const int convolveOn = ...;` and use `convolveOnIndex`.
- **Test (fails before, passes after) — in `core/tests/test_rt_alloc.cpp` (`synthyboy_test_rt`):**
  - **Signal / input:** Prepare `OwnEngine` with `sampleRate = 48000`, `maxBlockFrames = 128`, `qualityForTier(Tier::low)`. Play a chord (keys 48, 55, 60) and call `process(ev, 3, left.data(), nullptr, 8192)` (mono, `8192 > 4096`).
  - **Measured quantity & threshold:** `g_allocations - before == 0` AND `g_threadFrees - beforeFrees == 0`; compare `left[0..8191]` bit-for-bit (`std::memcmp == 0`) against a second identical engine rendered in 64 × `128`-frame mono blocks.
  - **Before vs after (computed from code):** Before: `scratchRight.assign(8192, 0.0f)` (`own_engine.cpp:1337`) reallocates `4096 → 8192` floats (`1` alloc + `1` free) → **FAIL**. After: **`0` allocs, `0` frees** → **PASS**.
  - **Bit-identical sound:** Yes. Print `MONO-OVERSIZED-CHECKSUM` (FNV-1a 64-bit hash of `left[0..8191]`).

#### T8 — `ContentHub` Dekker hazard pointer, mutex serialization & idle release *(Part 3 Rank 2; depends on T7)*
- **Editable files:** `core/engine-own/voice/own_content.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_threads.cpp`.
- **What to change:**
  1. In `core/engine-own/voice/own_content.h`:
     - Add `#include <mutex>` and `#include <vector>`.
     - Replace `kRing = 8`, `ring`, `next` (`lines 151-155`) in `ContentHub` with:
       - `std::mutex publishMutex;`
       - `std::vector<std::unique_ptr<OwnContent>> owned;`
       - `std::atomic<const OwnContent*> current { nullptr }, inUse { nullptr };`
       - `std::atomic<uint64_t> gen { 0 };`
     - In `ContentHub::publish(const StateContent& c)`: build `o` outside the lock, then lock `publishMutex`, push `o` into `owned`, `current.store(raw)` (`seq_cst`), `gen.fetch_add(1, release)`, read `const OwnContent* pinned = inUse.load()` (`seq_cst`), and erase every entry `p` in `owned` where `p != raw && p != pinned`.
     - Replace `get()` with `acquire() noexcept` (Dekker loop: `do { p = current.load(); inUse.store(p); } while (p != current.load()); return p;`), `release() noexcept` (`inUse.store(nullptr, std::memory_order_release);`), `generation() const noexcept`, and `ownedCount() const noexcept` (locks `publishMutex`, returns `owned.size()`, for tests). Keep `get()` as alias or `acquire()` and keep `static void compileLfoShape(...)` unchanged (`core/src/lfo_display.cpp:66`).
  2. In `core/engine-own/own_engine.cpp`:
     - In `OwnEngine::setContent()` (`lines 227-237`): move `noteTuning.setContent(content.tuning);` and `ownContent.publish(content);` **inside** `std::lock_guard<std::mutex> lock(contentMutex);` (before `contentState = content;`).
     - In `OwnEngine::process()` (`line 1352`): use `contentNow = ownContent.acquire();` (when `activeMask == 0 || controlPos == 0`; if `activeMask != 0 && controlPos != 0`, keep existing `contentNow` pinned until `controlPos == 0` inside the loop — preventing mid-period UAF!), and at the end of `process()` (`line 1771`) plus `reset()` and `silence()`, if `activeMask == 0`, call `ownContent.release();`. Also trigger `blockDirty` if `ownContent.generation()` changed (preventing ABA when `activeMask == 0`).
- **Test (fails before, passes after) — in `core/tests/test_threads.cpp` (`synthyboy_test_threads`, run under TSan & ASan):**
  - **Signal / input:**
    1. *Deterministic 20-publish UAF test:* Two writer threads each call `engine->setContent(makeCustomLfoAndCurveContent(i))` 200 times (`sbtest::scaled(200, 40)`) concurrently while the audio thread runs `process()` with held notes using custom LFO 1 (`lfo1.shape = 12`) routed to `filter1.cutoff` through mod slot 1 custom curve (`modCurve[0]`) plus velocity/note curves.
    2. *Convolve `live.tone` TSan check (verifies T2 under TSan):* Automate `convolve.size` and `convolve.tone` while an impulse is loaded and `process()` runs.
  - **Measured quantity & threshold:** Zero TSan data-race reports, zero ASan heap-use-after-free reports, all output samples finite (`|x| < 20.0f`).
  - **Before vs after:** Before: TSan immediately flags data race on `ContentHub::next` (`own_content.h:103`) and `ring[slot]` (`own_content.h:104`), and ASan flags heap-use-after-free after the 9th `publish()` frees slot 0 under `Voice::tick`/`ModMatrix::evaluate` → **FAIL**. After: **0 races, 0 UAF** → **PASS**.
  - **Bit-identical sound:** Yes. Print `CONTENT-HUB-CHECKSUM` for a deterministic single-threaded render with custom LFO 1 + mod curve 1 + velocity/note curves.

#### T9 — `ContentSet` `contentInUse` safe idle clear & mid-period `controlPos != 0` pin *(Part 3 Rank 15a; depends on T8)*
- **Editable files:** `core/engine-own/own_engine.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_own_content_policy.cpp`.
- **What to change:**
  1. In `core/engine-own/own_engine.h`:
     - Add audio-thread-only `const ContentSet* activeContent = nullptr;` and `uint64_t snapOwnContentGen = 0;` (moving the generation tracking cleanly into a member if T8 used a local or refining it).
     - Add `size_t ownedContentSetsForTest() const noexcept;` (locks `contentMutex`, returns `contentOwned.size()`).
  2. In `core/engine-own/own_engine.cpp`:
     - Define a helper `acquireContentSet() noexcept` implementing the Dekker loop (`do { cs = contentCurrent.load(); contentInUse.store(cs); } while (cs != contentCurrent.load()); activeContent = cs;`).
     - At `OwnEngine::process()` entry (`lines 1413-1421`): only call `acquireContentSet()` if `activeMask == 0 || controlPos == 0 || activeContent == nullptr`; otherwise keep `globals.content = activeContent` (still pinned in `contentInUse` from the previous block!).
     - Inside `while (pos < numFrames)` when `controlPos == 0` (`line 1463`): if `contentCurrent.load(std::memory_order_acquire) != activeContent`, call `acquireContentSet()` and update `globals.content = activeContent` **before** `controlTick()` (so every active voice rebinds `LibOsc::boundSet`, `Voice::noiseBound`, and `WtOsc::tick.wt` to the new `ContentSet`, while `C_old` stayed pinned until right before this switch — guaranteeing `C_new != C_old`, zero ABA!).
     - At `OwnEngine::process()` exit (`line 1771`) AND inside `OwnEngine::reset()` and `OwnEngine::silence()`: if `activeMask == 0`, clear `contentInUse.store(nullptr, std::memory_order_release); activeContent = nullptr;`.
- **Test (fails before, passes after) — in `core/tests/test_own_content_policy.cpp`:**
  - **Signal / input:**
    1. *Idle reclamation test:* Create `OwnEngine` (cast via `dynamic_cast<own::OwnEngine*>` or header include), prepare with `MemoryContentStore`, call `setContent(c1)`, run 1 silent/idle block (`activeMask == 0`), then call `setContent(c2)`. Query `ownedContentSetsForTest()`.
    2. *48-frame non-multiple-of-32 mid-period stress test:* Start a Sample oscillator note (`osc1.type = 1`, `sample.wav`), render alternating `48`-frame blocks (leaving `controlPos == 16 != 0` between blocks!) while another thread rapidly calls `setContent(c_i)` 50 times. Verify zero ASan UAF / TSan race and finite output.
  - **Measured quantity & threshold:** `ownedContentSetsForTest() == 1` immediately after `setContent(c2)` when idle.
  - **Before vs after (computed from code):** Before (`contentInUse` never cleared, `own_engine.cpp:1418`): `contentOwned` retains both `c2` (`raw`) and `c1` (`inUse`) → **`ownedContentSetsForTest() == 2` → FAIL**. After: `contentInUse == nullptr` at idle block exit, so `publishContent(c2)` erases `c1` immediately → **`ownedContentSetsForTest() == 1` → PASS**.
  - **Bit-identical sound:** Yes. Print `CONTENT-INUSE-CHECKSUM` on a 48-frame vs 128-frame render with a loaded sample.

#### T10 — Sparse `OwnContent::modCurve[64]` (`613 KB → 88.8 KB`) *(Part 3 Rank 2 memory hygiene; depends on T8)*
- **Editable files:** `core/engine-own/voice/own_content.h`, `core/engine-own/matrix/mod_matrix.cpp`, `core/tests/test_own_engine.cpp`.
- **What to change:**
  1. In `core/engine-own/voice/own_content.h`:
     - Change `std::array<CompiledModCurve, 64> modCurve {};` (`line 50`) to `std::array<std::unique_ptr<CompiledModCurve>, 64> modCurve {};`.
     - In `ContentHub::publish()` (`lines 68-84`): allocate `o->modCurve[slot - 1] = std::make_unique<CompiledModCurve>()` only for present curves in `c.modCurves`.
  2. In `core/engine-own/matrix/mod_matrix.cpp` (`line 301`):
     - Change `(content != nullptr && content->modCurve[s].present) ? &content->modCurve[s] : nullptr` to `(content != nullptr && content->modCurve[s] != nullptr && content->modCurve[s]->present) ? content->modCurve[s].get() : nullptr`.
- **Test (fails before, passes after) — in `core/tests/test_own_engine.cpp`:**
  - **Signal / input:** Assert `sizeof(own::OwnContent) < 100000` (computed: `613,040 B` before → **FAIL**; `88,752 B` on 64-bit / `88,456 B` on 32-bit after → **PASS**), plus render a patch with custom mod curve on slot 1 and verify bit-identical output via `SPARSE-MODCURVE-CHECKSUM`.

#### T11 — `OwnEngine` wait-free try-seqlock `setParamSnapshotImpl()` *(Part 3 Rank 13c — engine core; depends on T9, T10)*
- **Editable files:** `core/engine-own/own_engine.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_own_engine.cpp`.
- **What to change:**
  1. In `core/engine-own/own_engine.h`:
     - Add `void setParamSnapshotImpl (const float* plain, size_t count) noexcept;` (public on `OwnEngine`).
     - Add members: `std::mutex paramBatchMutex;`, `std::atomic<uint32_t> paramBatchSeq { 0 };`, `std::vector<float> baseStaging;`.
  2. In `core/engine-own/own_engine.cpp`:
     - In `OwnEngine::prepare()` (`line 433`): allocate `baseStaging.assign (paramCount(), 0.0f);` right next to `base.assign(...)`.
     - Implement `OwnEngine::setParamSnapshotImpl(const float* plain, size_t count) noexcept`:
       - Lock `paramBatchMutex`; `paramBatchSeq.fetch_add(1u, std::memory_order_acq_rel);` (now odd); `std::atomic_thread_fence(std::memory_order_release);`.
       - For `size_t i = 0; i < n; ++i`: `if (!std::isnan(plain[i])) params[i].store(constrainValue(table[i], plain[i]), std::memory_order_relaxed);`.
       - Update the 6 wavetable requests (`wtParamIdx[0..5]`) and 16 FX instance types (`fxiTypeIndex[0..15]`) — calling `fxWake.notify()` **at most once** if any `fxWant[s]` changed (or `fxPool.typeChanged` offline when `!fxWorker.joinable()`).
       - `paramBatchSeq.fetch_add(1u, std::memory_order_release);` (now even); `paramGen.fetch_add(1u, std::memory_order_release);` (single bump!).
     - In `OwnEngine::process()` (`lines 1350-1366`):
       - Read `const uint32_t s1 = paramBatchSeq.load(std::memory_order_acquire);` and `const uint32_t gen = paramGen.load(std::memory_order_acquire);`.
       - When `gen != snapGen || !snapValid`:
         - If `(s1 & 1u) != 0 && snapValid`: keep current consistent `base` for this 1 block (do not update `snapGen`, so next block retries!).
         - Else: copy `params[0..N-1]` into `baseStaging[0..N-1]`, `std::atomic_thread_fence(std::memory_order_acquire)`, `const uint32_t s2 = paramBatchSeq.load(std::memory_order_acquire);`.
         - If `s1 == s2 || !snapValid`: `base.swap(baseStaging);` (`O(1)` pointer swap, 0 alloc), compile `matrix`, update `snapGen = gen`, `snapValid = true`, `++baseEpoch`.
         - Else: discard `baseStaging`, keep current `base` for this 1 block (do not update `snapGen`).
- **Test (fails before, passes after) — in `core/tests/test_own_engine.cpp`:**
  - **Signal / input (deterministic silence-vs-silence torn-preset detector):**
    - Preset A: `osc1.on = 1, osc1.level = 100, osc2.on = 0, filter1.on = 0, master.volume = -60.0` (exact silence: `peak == 0.0f`!).
    - Preset B: `osc1.on = 1, osc1.level = 0, osc2.on = 0, filter1.on = 0, master.volume = 0.0` (exact silence: `peak == 0.0f`!).
    - Hold note 60 (`quality.no_smoothing = 1` so gain changes take effect instantaneously in the torn block!). Writer thread rapidly toggles A ↔ B (`2,000` iterations, `sbtest::scaled(2000, 200)`) while audio thread renders 128-frame blocks and records `maxAbsPeak`.
    - Commit 1 calls the 2,637-`setParam` loop (proving tearing produces `maxAbsPeak > 0.1f`); commit 2 switches to `setParamSnapshotImpl()`.
  - **Measured quantity & threshold:** `maxAbsPeak < 1.0e-6f` (exact `0.0f`).
  - **Before vs after (computed from code):** In `paramTable()`, `master.volume` and `osc1.level` are at distinct indices; whenever `process()` runs between the two stores during an A → B or B → A sweep, `osc1.level == 100` combines with `master.volume == 0 dB` → full-scale saw wave **`maxAbsPeak >= 0.3` → FAIL**. After try-seqlock: every block sees pure A or pure B → **`maxAbsPeak == 0.0f` → PASS**.
  - **Bit-identical sound:** Yes in steady state; print `SNAPSHOT-CHECKSUM` comparing a preset loaded via `setParamSnapshotImpl()` vs `setParam()` loop (`std::memcmp == 0`).

#### T12 — Expose `IEngine::setParamSnapshot()` virtual seam & TSan torn-preset test *(Part 3 Rank 13c — interface; depends on T11)*
- **Editable files:** `core/include/synthyboy/engine.h`, `core/engine-own/own_engine.h`, `core/tests/test_threads.cpp`.
- **What to change:**
  1. In `core/include/synthyboy/engine.h` (right after `setParam` at `line 89`): add:
     ```cpp
     /** Atomically replaces plain[0 .. min(count, paramCount())) for the next audio block (not real-time; message thread). */
     virtual void setParamSnapshot (const float* plain, size_t count) noexcept
     {
         const auto* table = paramTable();
         const size_t n = std::min (count, paramCount());
         for (size_t i = 0; i < n; ++i)
             setParam (table[i].id, plain[i]);
     }
     ```
  2. In `core/engine-own/own_engine.h`: add `void setParamSnapshot (const float* plain, size_t count) noexcept override { setParamSnapshotImpl (plain, count); }`.
- **Test (fails before, passes after) — in `core/tests/test_threads.cpp` (`synthyboy_test_threads`, run under TSan):**
  - **Signal / input:** Same silence-vs-silence A ↔ B preset toggle through `IEngine::setParamSnapshot()` concurrently with a second thread calling single `IEngine::setParam(kParamFilter1Cutoff, ...)` and the audio thread calling `process()` (`sbtest::scaled(2000, 150)` blocks).
  - **Measured quantity & threshold:** `maxAbsPeak < 1.0e-6f` and 0 TSan race reports.
  - **Bit-identical sound:** Yes (verified via `IEngine` steady-state equivalence).

#### T13 — Wire `shell-mpc` `applyState()` to `engine->setParamSnapshot()` & live `effSetChunk` test *(Part 3 Rank 13c — MPC shell; depends on T4, T12)*
- **Editable files:** `shell-mpc/synthyboy_mpc.cpp`, `shell-mpc/tests/plugin_review_host.cpp`.
- **What to change:**
  1. In `shell-mpc/synthyboy_mpc.cpp` (`applyState`, `lines 1905-1913`):
     - Keep the loop updating `inst->paramMirror[i]` and `inst->paramUndo[i]`, remove the per-parameter `inst->engine->setParam(table[i].id, values.plain[i]);` inside the loop, and call `if (inst->engine) inst->engine->setParamSnapshot(values.plain, count);` **once** immediately after the mirror loop!
- **Test (fails before, passes after) — in `shell-mpc/tests/plugin_review_host.cpp`:**
  - **Signal / input:** Build two valid state chunks (Preset A: `osc1.level = 100, master.volume = -60, quality.no_smoothing = 1`; Preset B: `osc1.level = 0, master.volume = 0, quality.no_smoothing = 1`), hold MIDI note 60 via `effProcessEvents`, run an audio thread calling `processReplacing(128)` for 1,000 blocks while the main thread toggles `effSetChunk(A)` / `effSetChunk(B)` 100 times.
  - **Measured quantity & threshold:** `maxAbsPeak < 1.0e-6f` across all 1,000 audio blocks (and `effClose` joins cleanly within 2 s).
  - **Before vs after:** Before (`2,637` sequential `setParam` calls in `applyState`): torn frame fires `osc1.level = 100` with `master.volume = 0 dB` → **`maxAbsPeak > 0.1` → FAIL**. After (`setParamSnapshot`): **`maxAbsPeak == 0.0f` → PASS**.
  - **Bit-identical sound:** Yes for every loaded preset once applied (`effGetChunk` and rendered steady-state audio match bit-for-bit).

---

## 5. Acceptance Criteria for the Whole Item

All of the following must hold after Batches 1–7 are built and verified by the build worker:

1. **Zero RT-safety violations (host tests + sanitizers):**
   - `synthyboy_test_rt`: **0 allocations AND 0 audio-thread frees (`g_allocations == 0`, `g_threadFrees == 0`)** across all existing cases PLUS oversized mono (`numFrames = 8192`) PLUS 64-slot `FxWorker` saturation (active + bypassed Convolve).
   - `ctest -LE slow` under **ThreadSanitizer** (`-fsanitize=thread`, `TSAN_OPTIONS=halt_on_error=1`) and **AddressSanitizer + UBSan** (`-fsanitize=address,undefined`): **0 data races, 0 use-after-free, 0 leaks** across `synthyboy_test_threads`, `synthyboy_test_own_helper_thread`, `synthyboy_test_own_engine`, `synthyboy_test_own_content_policy`, `synthyboy_test_own_osc_types`.
   - `shell-mpc/tests/run_event_inbox_test.sh`: **PASS** native and under TSan (including deterministic `StallingEv` lock-freedom proof and 4P1C stress test).
   - `shell-mpc` host tests: `output_stage_lanes_host`, `callback_stats_host` (with `page_faults:` output), `plugin_review_host` (including live `effSetChunk` torn-preset test), and `shell-mpc/accept.sh`: **100% PASS**.
   - Symbol hygiene (`nm -u` / `objdump` on `synthyboy_mpc.so` and `synthyboy_core`): zero `__cxa_guard_acquire` inside `OwnEngine::publishStats()`, `outputPeak()`, and `noteDiagnostics()`; `sizeof(own::WakeSignal) <= 16` on Apple/Windows/Linux.

2. **Bit-identical steady-state sound & unchanged aliasing:**
   - `synthyboy_preset_render --engine own --tier all --float32 --hash` compared with §2.1 baseline via a comparison of the `.hash` files of the two render folders: **0 of 960 renders differ** (bit-identical across all 160 presets × 3 tiers × single + chord).
   - Every task's `<NAME>-CHECKSUM` matches baseline bit-for-bit.
   - Aliasing level: **unchanged** from baseline (`docs/QUALITY.md:33-47`), as zero DSP math changes.

3. **CPU and Memory budgets (Key 61 & XL):**
   - **Key 61 CPU (`rtrun` `tier_bench --core 3 --config low`, 128 frames @ 44.1 kHz, 2.902 ms budget):** no regression vs Round 13 baseline (mean median `<= 24 %` all-presets / `<= 35 %` `rtrun` top-24; `0` presets `> 70 %` p99 under `rtrun`, `docs/MPC.md:350`).
   - **Memory:** `OwnContent` drops from `4.90 MB` (8-slot ring) → **`88.8 KB`** (`< 100 KB`); `scratchRight` drops `16 KB → 0 B`; `ownedContentSetsForTest() == 1` when idle (0 dead `ContentSet` pinned after preset switch).

4. **Device checks (MPC Key 61 — ARMv7 only during development per `AGENTS.md`):**
   - `ps -eLo pid,tid,psr,cls,rtprio,ni,comm | grep sb-` shows `sb-fx-build`, `sb-fx-job`, and **`sb-wt-build`** all running **`SCHED_OTHER` (`TS`)** on non-audio cores **`0-1`** (`sb-wt-build` at `ni = 10`).
   - `/proc/<mpc_pid>/task/<wt_tid>/sched` `nr_switches` delta over 10 s idle = **`0`** (down from `~1,000`).
   - `SYNTHYBOY_MPC_CALLBACK_STATS=1` during 5 consecutive preset switches while holding an 8-note chord: **0 gaps `>= 1.5×`** nominal (`2.902 ms`), `audio_majflt == 0`, no audible torn-preset click/spike, and `VmRSS` drops after switching from a heavy sample/spectral preset to an empty synth preset.

---

## 6. Risks and Mitigation / Rollback Plan

| Risk | Likelihood | Impact | Mitigation & What to Do If It Fails |
|---|---|---|---|
| **Apple `dispatch_release` crash if `WakeSignal` is destroyed while `sem` count != 0 (T1)** | Low | High (abort on shutdown) | The 3-state machine (`0/1/2`) enforces strict 1:1 pairing: `signal` runs only on `2 → 1` and `wait` only on `0 → 2`. Always join the helper thread before `~WakeSignal()`. If a macOS test hits a libdispatch trap, verify every `stop()` sets `stop = true` before `notify()` and joins before destructor; fallback on Apple is `os_sync_wait_on_address` (macOS 14.4+/iOS 17.4+) or `pthread_cond` only in non-RT destructor. |
| **Mid-period `ContentSet` / `OwnContent` UAF if `controlPos != 0` on non-32-multiple host blocks (T8, T9)** | Medium if naively cleared | High (crash) | Explicitly guarded by the `activeMask == 0` idle-release rule and `controlPos != 0` pin rule in T8 & T9, verified by the 48-frame ASan/TSan stress test in `test_own_content_policy.cpp`. If ASan trips on any voice pointer, check `Voice::render()` for any unguarded `globals.content` read outside `LibOsc`/`noiseSv`/`WtOsc`. |
| **Try-seqlock `setParamSnapshotImpl` drops a live `setParam()` tweak during preset load (T11–T13)** | Low | Low (1 knob tick overwritten by preset load) | Intentional: a full preset load overwrites all parameters, and any subsequent `setParam()` increments `paramGen` independently. If T11's silence-vs-silence test flakes under TSan, verify `std::atomic_thread_fence(release)` precedes the stores and `std::atomic_thread_fence(acquire)` precedes the second `paramBatchSeq.load(acquire)`. |
| **Rapid wavetable knob sweep builds passed-over tables under `wantedMask` (T6)** | Medium | Low (background work on cores 0–1, nice +10, max 8 tables once per process) | `sb-wt-build` is pinned to non-audio cores `0-1` at `SCHED_OTHER` nice `+10` and checks `stop` between frames. If device profiling shows unwanted CPU on cores 0–1 during rapid scrolling, switch from `wantedMask` to a 16-instance × 6-slot per-instance atomic array while keeping `runHelperThreadStartHook` + `WakeSignal`. |
| **Cross-build (`arm32`) failure on `std::atomic<size_t>` or `SYS_gettid` (T3, T5)** | Low | Medium (compile fail) | `std::atomic<size_t>` is 32-bit lock-free on ARMv7 (`ldrex`/`strex`); wrap `SYS_gettid` in `#if defined(__linux__)` with `<sys/syscall.h>` and `<unistd.h>` (already used in `shell-mpc/thread_policy.h:21-22`). If any task fails to compile in the build worker, revert that single task's commit pair while keeping all earlier passing batches intact. |

---

## 7. Public References

1. **Bencina, R.** — *"Real-time audio programming 101: time waits for nothing"*, 2011.
2. **Boehm, H.-J.** — *"Can Seqlocks Get Along with Programming Language Memory Models?"*, ACM SIGPLAN Workshop on Memory Systems Performance and Correctness (MSPC), 2012.
3. **Drepper, U.** — *"Futexes Are Tricky"*, Red Hat, 2011.
4. **Herlihy, M. & Shavit, N.** — *The Art of Multiprocessor Programming*, 2nd ed., Morgan Kaufmann, 2020.
5. **Michael, M. M.** — *"Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects"*, IEEE Transactions on Parallel and Distributed Systems, 15(6):491–504, 2004.
6. **Sha, L., Rajkumar, R., & Lehoczky, J. P.** — *"Priority Inheritance Protocols: An Approach to Real-Time Synchronization"*, IEEE Transactions on Computers, 39(9):1175–1185, 1990.
7. **Vyukov, D.** — *"Bounded MPMC / MPSC Queue"*, 1024cores.net, 2010.

---

Clean-room attestation: Nothing under `core/engine-vital/`, `core/vital-import/`, or `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/` was opened, read, grepped, or listed at any point while producing this plan.
