# Implementation Plan: Own Engine Memory Footprint & Tier-Scaled Allocation

**Scope:** SynthyBoy's own synth engine (`core/engine-own` and the clean-room DSP libraries `core/filters-serum`, `core/fx-serum-a`, `core/fx-serum-b`, `core/sample-osc`, `core/granular`, `core/spectral`, `core/multisample`).
**Targets:** Akai MPC Key 61 (ARMv7 Cortex-A17, NEON, 4 GB RAM (measured MemTotal 4,113,388 kB, no swap, 2026-11-21), 32 KB L1D / ~1 MB L2, quality tier `low`, 8 voices usable, 44.1 kHz, 128-frame blocks, 2 cores isolated for audio), MPC XL (ARM64 Cortex-A55/A76, `high,voices=16`), iOS, and desktop (`high`, 32 voices).
**Findings covered (verified against source + verification reports + `TODO.md`):**
- Part 3 Rank 1 (`CONFIRMED`): `OwnEngine::prepare()` unconditionally allocates 36 voices and all 13 FX modules across 3 racks on every tier.
- Part 3 Rank 6 (`CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`): `sizeof(Voice) = 33,128 B` interleaving hot/cold state, per-oscillator `MorphScratch` (`2.66 MB`), and `~4,968` small heap allocations per instance.
- Part 3 Rank 17 (`CONFIRMED`): Built-in wavetable bank is `33.10 MB` (`31.57 MiB`), while `core/engine-own/README.md:13` claims `20.5 MB`.
- Part 1 Rank 10 (`PARTLY WRONG`): Per-voice `FilterImpl` delay lines (16 vectors, not 14; already sample-rate-scaled) and `LibOsc` pre-allocated sub-engines (`64` multisample layers, not 256).
- Part 2 Rank 5 — memory half (`CONFIRMED` on memory, `PARTLY WRONG` on Nitrous damping/Plate counts): `Reverb::prepare()` allocates all 6 mutually exclusive reverb topologies simultaneously (`4.87 MB`, 93 delay vectors per instance).

---

## 1. The Problem, as Verified

### 1.1 What the code does today (`file:line` verified in this worktree)

1. **Unconditional 36-voice pool on every tier** (`core/engine-own/own_engine.h:90`, `core/engine-own/own_engine.cpp:426-432`):
   - `kPoolVoices = kMaxVoices + 4 = 36` (`core/include/synthyboy/quality.h:11` defines `kMaxVoices = 32`).
   - `OwnEngine::prepare()` always allocates and prepares all `36` `Voice` objects even when `quality.maxVoices == 8` (Key 61 tier `low`, `core/include/synthyboy/quality.h:112`, `shell-mpc/device_profile.h:41`) or `16` (MPC XL).
   - `OwnEngine::setQuality()` (`core/engine-own/own_engine.cpp:314-323`) stores `voiceCap`, `unisonCap`, `fxMask` into atomics, which `prepare()` never uses to size the voice pool or FX racks.

2. **Three redundant copies of all 13 instance-0 FX modules, ignoring `fxMask`** (`core/engine-own/own_engine.h:196`, `core/engine-own/own_engine.cpp:452-458`, `core/engine-own/fx/fx_chain.cpp:172-189`):
   - `OwnEngine` instantiates `FxChain fx[3]` (Main, Bus 1, Bus 2). Each `FxChain::prepare()` calls `makeFxModule(m)` and `inst[m]->prepare(sr, maxBlock)` for all `kNumFxModules = 13` modules (`0..12`, Chorus through Convolve), regardless of `quality.fxMask`.
   - On tier `low`, `Quality::forTier(Tier::low).fxMask` (`core/include/synthyboy/quality.h:112`) masks out 5 heavy effects: `kFxReverb | kFxFlanger | kFxHyperDim | kFxBode | kFxConvolve`. Only `reverb.force_enable` (`quality.h:55-58`, `own_engine.cpp:1474`) can unmask Reverb on tier `low`.
   - **Critical structural redundancy (`core/engine-own/fx/fx_chain.cpp:412-429`):** Every instance-0 effect module `m ∈ [0..12]` has a single global `<fx>.rack` parameter (`moduleRack[m]`) assigning it to Main (`0`), Bus 1 (`1`), Bus 2 (`2`), or a band of a single splitter (`3..9`, where each splitter `13..15` also sits in only one rack `0..2`). Therefore `mine` is true in **at most ONE** of `fx[0..2]` at any time. Allocating 3 full copies of `inst[0..12]` wastes **2× the entire instance-0 FX memory** on every tier (additional instances use `FxPool` slots `fxi01..fxi16`, not `fx[1..2].inst[]`).
   - Note that `FxChain::update()` (`fx_chain.cpp:429,445`) still calls `inst[m]->hasMakeup()` and `inst[m]->makeup(...)` when a module is masked out by `fxMask` and `Quality::fxMakeup == true`. However, `makeup()` reads only parameters and lightweight caches (`MakeupCache` in `fx_modules.cpp:436-445,531-544,592-601,698-709,810-860`) — it never touches delay buffers.

3. **Per-voice `FilterImpl` allocates 16 separate delay vectors for mutually exclusive filter families** (`core/filters-serum/src/filter.cpp:34-63,833-837`, `core/filters-serum/src/filter_dsp.h:344-363,446-470`, `core/engine-own/voice/filter_unit.h:31-37`):
   - Every `FilterUnit` owns 2 `FilterImpl` instances (`left`, `right`) and 2 `ClassicFilter` instances (`cl[0..1]`). Each `Voice` owns `2 × FilterUnit` (`voice.h:379`), plus `FilterModule` owns `1 × FilterUnit` (`fx_modules.cpp:405`).
   - Each `FilterImpl::prepare(sr)` allocates **16** independent `std::vector<float>` delay lines in 3 **mutually exclusive** filter-type groups (`filter.cpp:1098-1624`):
     - Group A (`Cmb*/Flg*/Phs*`, `DistComb2LP/BP`, `Comb2`): `combDelay1_` + `combDelay2_`.
     - Group B (`Allpasses`, `Diffusor`): `diffuserDelays_[8]`.
     - Group C (`Combs`, `Reverb`): `reverbCombs_[4]` + `reverbAP_[2]`.
   - At `44.1 kHz`: Group A = `2 × (floor(44100 × 0.06) + 64 + 4) = 2 × 2,714 = 5,428` floats (`21,712 B`); Group B = `72+130+200+315+430+597+791+1047 = 3,582` floats (`14,328 B`); Group C = `(1329+1656+1841+1947) + (244+580) = 6,773 + 824 = 7,597` floats (`30,388 B`). Total = `16,607` floats = **`66,428 B`** across **16 heap allocations** per `FilterImpl` (vs `30,388 B` and 1 allocation if pooled).
   - Each `ClassicFilter::prepare(sr)` (`core/engine-own/voice/classic_filters.h:74`, `core/engine-own/fx/fx_dsp.h:20-28`) also allocates `comb` of `next_pow2(int(44100 * 0.1) + 4) = 8,192` floats = **`32,768 B`**.

4. **`LibOsc` unconditionally pre-allocates all 4 sample-based sub-engines at full `kMaxU = 16` unison** (`core/engine-own/osc/lib_osc.h:93-95,193-226`, `core/engine-own/osc/lib_osc.cpp:123-147`):
   - Every `Voice` owns `3 × LibOsc` (`voice.h:353`). `LibOsc::prepare()` unconditionally allocates:
     - `sampleVoices`: `16 × SampleVoice` (`core/sample-osc/include/synthyboy/sampleosc/sample_voice.h:138-174`, `~192 B` each → `~3,072 B`).
     - `grainVoices`: `16 × GranularVoice` (`core/granular/include/synthyboy/granular/granular.h:22,119-163`, `48 × 80 B Grain + ~56 B = ~3,896 B` each → `~62,336 B`).
     - `specVoices`: `4 × SpectralVoice` (`core/spectral/include/synthyboy/spectral/spectral.h:23-25,205-237`, `2 × (1025+1025+2048+512) × 4 B + ~104 B = 36,984 B` each → `147,936 B`).
     - `layers`: `kSlots × kMaxU = 4 × 16 = 64` `Layer` structs (`core/multisample/include/synthyboy/multisample/multisample.h:24-25`: `kMaxLayers = 2`, `kMaxLayerSlots = 4`; `Layer = LayerVoice (~304 B) + int` → `~19,712 B`).
     - Work vectors (`lib_osc.cpp:140-145`): `filterGain` (`1025 × 4 = 4,100 B`), `6 × 32`-float scratch vectors (`tmpL/R`, `mixL/R`, `modBuf`, `rateBuf` = `768 B`, 6 heap allocations), `ring` (`3 × 2048 × 4 = 24,576 B`), `extMag` (`3 × 1025 × 4 = 12,300 B`) → `41,744 B`.
   - Total per `LibOsc`: **`~274,800 B` (`274.8 KB`)** across **13 heap allocations** (`3 × 274.8 KB = 824.4 KB` per voice; **`29.68 MB`** across 36 voices), even when all 3 oscillators are Wavetable (`oscN.type == 0`).

5. **`Voice` object layout (`sizeof(Voice) = 33,128 B`), redundant `MorphScratch` (`2.66 MB`), and allocation scatter** (`core/engine-own/voice/voice.h:213-397`, `core/engine-own/osc/wt_osc.h:515-524,1223`, `core/engine-own/osc/morph.h:22-26`, `docs/release/CPU-REVIEW-MEMORY.md:25,326-346`):
   - `sizeof(Voice) = 33,128 B` (`0x8168` in ARM32 disassembly, `[measured: docs/release/CPU-REVIEW-MEMORY.md:25,326]`) interleaves cold setup/cache structs (`LfoSettings[10]`, `RlfoSettings[4]`, `OscPart[3]` containing `libV[98]`, `EnvSettings[6]`, `shapes::ShapeData scratchShape`, `LibOsc::fltX/Y[257]`) with hot per-sample/per-period state (`env`, `lfoCore`, `src`, `osc`, `filt`, `oscMono`, `extMono`).
   - Every `WtOsc::prepareMorph()` (`wt_osc.h:520`) allocates a private `std::unique_ptr<MorphScratch>` (`(2048 + 2048 + 1026 + 1026) × 4 B = 24,592 B`), used only synchronously inside `buildMorphCycle()` (`wt_osc.h:612`) on the single audio thread. Across `36 × 3 = 108` oscillators this wastes `108 × 24,592 B = 2,655,936 B` (**`2.66 MB`**) instead of 1 engine-shared scratch (`24,592 B`, like `spectralScratch` at `own_engine.h:172`).
   - `WtOsc::history` (`wt_osc.h:523`) always allocates `kMaxVoices (16) × (kTaps - 1 = 127) × 4 B = 8,128 B` per oscillator even when tier `low` caps unison to `3..4` (`2,032 B` needed at `maxUnison = 4`).

6. **`Reverb` allocates all 6 mutually exclusive reverb topologies simultaneously** (`core/engine-own/fx/fx_reverb.h:76-117,515-535`):
   - Exact buffer counts at `44.1 kHz` (`DelayLine::prepare` rounds `maxSamples + 4` to next power of two, `core/engine-own/fx/fx_dsp.h:20-28`):
     - Always used: `pre[2] = 2 × pow2(14112 + 4) = 2 × 16,384 = 32,768` floats (`131,072 B`, 2 allocs).
     - Early reflections (types `1..5` only): `erLine = pow2(7497 + 64 + 4) = 8,192` floats (`32,768 B`, 1 alloc).
     - Type `0` (FDN16): `line[16]` (`16 × pow2(22800 + 4096 + 4) = 16 × 32,768 = 524,288`) + `apLine[16]` (`16 × pow2(1100 + 4) = 16 × 2,048 = 32,768`) = `557,056` floats (**`2,228,224 B`**, 32 allocs). *(At `48 kHz`: `scale = 2`, so `apLine[16]` doubles to `65,536` floats → `589,824` floats = `2,359,296 B`.)*
     - Types `2, 4, 5` (Hall, Nitrous, Basin — 8-line FDN): `net[8]` (`8 × pow2(22050 + 64 + 4) = 8 × 32,768 = 262,144`) + `netAp[8]` (`8 × pow2(882 + 64 + 4) = 8 × 1,024 = 8,192`) + `inAp[4]` (`4 × 1,024 = 4,096`) = `274,432` floats (**`1,097,728 B`**, 20 allocs).
     - Type `1` (Plate, `pr = (44100/29761) × 2.83 × 1.2 = 5.0322`): `plModAp[2]` (`2 × 8,192 = 16,384`) + `plDel1[2]` (`2 × 32,768 = 65,536`) + `plAp2[2]` (`2 × 16,384 = 32,768`) + `plDel2[2]` (`2 × 32,768 = 65,536`) + `plIn[4]` (`4 × 4,096 = 16,384`) = `196,608` floats (**`786,432 B`**, 12 allocs).
     - Type `3` (Vintage, `vmax = 5,901 → 8,192`): `vComb[2][8]` (`16 × 8,192 = 131,072`) + `vInAp[2]` (`2 × 1,024 = 2,048`) + `vAp[2][4]` (`8 × 2,048 = 16,384`) = `149,504` floats (**`598,016 B`**, 26 allocs).
   - **Total per `Reverb` today at 44.1 kHz:** `1,218,560` floats = **`4,874,240 B` (`4.87 MB` / `4.65 MiB`)** across **93 heap allocations** (× 3 racks = **`14.62 MB`** and **279 allocations**). At 48 kHz: `1,271,808` floats = `5.09 MB`.

7. **Built-in wavetable bank size vs stale documentation** (`core/engine-own/osc/wavetable_bank.h:18-29,50`, `core/engine-own/osc/wavetable_bank.cpp:137-217,229-235,274-281`, `core/engine-own/README.md:13`, `core/tests/test_own_osc_types.cpp:637-642`):
   - `Wavetable::kNumLevels = 18` with `size[m] = max(256, next_pow2(8 * harmonics[m]))` capped at `2048`, plus 4 guard samples per level:
     - `m = 0..3` (`1024, 512, 256, 181`): `4 × 2,052 = 8,208` floats
     - `m = 4..5` (`128, 91`): `2 × 1,028 = 2,056` floats
     - `m = 6..7` (`64, 45`): `2 × 516 = 1,032` floats
     - `m = 8..17` (`32..1`): `10 × 260 = 2,600` floats
     - `frameStride = 13,896` floats (`55,584 B`) + `spec = 2 × 1,024 = 2,048` floats (`8,192 B`) = **`63,776 B/frame`**.
   - Across 8 multi-frame tables (`8 × 64 = 512` frames) + 7 single-frame tables (`7` frames) = `519` frames:
     - Multi-frame tables: `512 × 63,776 B = 32,653,312 B` (**`32.65 MB` / `31.14 MiB`**).
     - All 15 built-in tables (`builtinBankBytes()`): `519 × 63,776 B = 33,099,744 B` (**`33.10 MB` / `31.57 MiB`**), shared **once per process** (and lazy-loaded on MPC via `Quality::lazyTables = true`, `shell-mpc/synthyboy_mpc.cpp:2412`, so startup only builds ~7 single-cycle tables + active preset table = `~0.45–4.53 MB`).
   - `core/engine-own/README.md:13` erroneously states `21 half-octave mip levels stored at 2x their band: the eight 64 frame tables take 20.5 MB`, and `wavetable_bank.h:18-21` erroneously states `at least 16` samples and `29 MB` (time data alone, omitting `4.19 MB` of `spec`). `test_own_osc_types.cpp:641` checks `< 32 MiB` (`33,554,432 B`), passing by only `454 KB`.

### 1.2 Baseline memory & startup totals (computed from code @ 44.1 kHz, 128-frame blocks, 32-bit ARM)

| Component | Per-instance bytes today | Heap allocs | Code location |
|---|---|---|---|
| `36 × Voice` objects (`33,128 B` each) | `1,192,608 B` (`1.19 MB`) | `36` | `own_engine.cpp:426-431`, `CPU-REVIEW-MEMORY.md:326` |
| `36 × (mod + modR)` (`2 × 2,637 × 4 B`) | `759,456 B` (`0.76 MB`) | `72` | `voice.cpp:154-155` |
| `36 × 3 × WtOsc` (`morphBuf 32.8 KB + MorphScratch 24.6 KB + history 8.1 KB`) | `7,079,616 B` (`7.08 MB`) | `324` | `voice.cpp:162`, `wt_osc.h:515-524` |
| `36 × 2 × FilterUnit` (`2 × FilterImpl 66.4 KB + 2 × ClassicFilter::comb 32.8 KB + ~2.5 KB impls`) | `14,464,224 B` (`14.46 MB`) | `2,592` | `voice.cpp:158-159`, `filter.cpp:40-60`, `classic_filters.h:74` |
| `36 × 3 × LibOsc` (`Sample 3.1 KB + Gran 62.3 KB + Spec 147.9 KB + 64 Layers 19.7 KB + scratch/ring 41.7 KB`) | `29,678,400 B` (`29.68 MB`) | `1,404` | `voice.cpp:163`, `lib_osc.cpp:127-146` |
| **Voice Pool Subtotal** | **`53.17 MB`** (`50.71 MiB`) | **`4,428`** | |
| `3 × FxChain` unmasked on `low` (`Chorus 262 KB + Comp 4.6 KB + Delay 2.10 MB + Filter 200 KB + Splitters 7.2 KB + vbase 10.5 KB`) | `7,743,564 B` (`7.74 MB`) | `204` | `fx_chain.cpp:176-188`, `fx_effects.h:68,172`, `fx_compressor.h:39-43` |
| `3 × FxChain` masked on `low` (`Flanger 524 KB + Reverb 4.87 MB + HyperDim 180 KB + Bode 884 KB`; `Convolve` is `0 B` at prepare) | `19,386,888 B` (`19.39 MB`) | `336` | `fx_chain.cpp:176-181`, `fx_reverb.h:76-116`, `fx_effects.h:519`, `hyper_dimension.cpp:51-62`, `bode.cpp:68-79`, `fx_modules.cpp:744-762` |
| **3 FX Racks Subtotal** | **`27.13 MB`** (`25.87 MiB`) | **`540`** | |
| `OwnEngine` fixed buffers (`base/gdelta/pdelta`, `morphCache 131 KB`, `spectralScratch 76 KB`, `scratchRight 16 KB`, struct) | `~370,000 B` (`0.37 MB`) | `~10` | `own_engine.cpp:146,393,433-438,461` |
| **Total per `OwnEngine` instance (`prepare()` baseline)** | **`80.67 MB`** (`76.93 MiB`) | **`~4,978`** | (`+0.61 MB` after first `setContent()`, `own_content.h:47-52`) |
| **1 instance + shared WT bank (full `33.10 MB` / lazy startup `~4.5 MB`)** | **`113.77 MB` / `85.17 MB`** | — | `wavetable_bank.cpp:229-281` |
| **4 instances + shared WT bank (full `33.10 MB`)** | **`355.78 MB`** (`4 × 80.67 + 33.10`) | **`~19,912`** | On Key 61 (4 GB RAM), MPC host + 1 instance + 422 MB filmstrips already uses `~945 MB VmRSS` (`[measured: docs/MPC.md:117,148,159]`). |
| **Key 61 `engine_prepare` startup time (`[measured: docs/MPC.md:327]`)** | **`186.8 ms`** (`prepare_voices 95.6 ms` + `prepare_fx 73.6 ms` + `wavetables 8.7 ms` + `overlay 10.7 ms`) | — | |

### 1.3 Audit claims verified wrong, corrected, or already fixed

- **Part 1 Rank 10 (`PARTLY WRONG` — Audit 1 Verification lines 322-327 + direct code inspection):**
  1. `FilterImpl` already sizes all delay buffers to the active `sampleRate_` (`filter.cpp:36-60`), NOT a fixed 96 kHz (`docs/QUALITY.md:127`'s `180 KB` figure was for a 96 kHz prepare rate; at 44.1 kHz it is `66.4 KB`).
  2. `FilterImpl` allocates **16** delay lines (`2` comb + `8` diffuser + `4` reverbComb + `2` reverbAP), not 14.
  3. **Correction to Audit 1 Verification line 324:** Direct inspection of `core/multisample/include/synthyboy/multisample/multisample.h:24-25` shows `kMaxLayers = 2` and `kMaxLayerSlots = 4` (NOT `16`!), so `LibOsc::layers` allocates `4 × 16 = 64` `Layer` objects (`~19.7 KB`, confirming the audit's `64`, not Verification's `256`).
  4. Unused filter delay lines and idle `LibOsc` sub-engines are NOT cleared on note-on (`filter_dsp.h:356-363` guards on `written_`, `classic_filters.h:84-88` guards on `combUsed`, `lib_osc.cpp:170` guards on `playersUsed`). The ~3× cold-cache spike in `docs/MPC.md:354` is on **Spectral priming**, not unused filter lines.
- **Part 2 Rank 5 (`PARTLY WRONG` on DSP half, `CONFIRMED` on memory half — Audit 2 Verification lines 169-180):**
  1. Nitrous Space cannot reach negative HF damping (`ah < 0`) via engine parameters because `core/src/param_table.cpp:699` clamps `reverb.high_shelf_gain` to `[-6, 0]` dB (`ah >= +0.2767`).
  2. Plate executes `38` `readCubic` calls/sample (not 33), and fractional all-pass taps cannot simply become integer reads.
  3. The **memory half (`4.87 MB` / 93 vectors at 44.1 kHz)** is `CONFIRMED` (noting Verification line 179 swapped `netAp`/`inAp` lengths in its text, though both are `1,024` floats at 44.1 kHz so the `1,218,560 floats = 4,874,240 B` total is exact).
- **Part 3 Rank 1 (`CONFIRMED` — Audit 3 Verification lines 49-78 + direct code inspection):**
  - Correction to Audit 3 Verification line 76: `ConvolveModule::prepare()` (`fx_modules.cpp:744-762`) leaves `active = nullptr` (`0 B` at prepare time, not `728 KB`), so 3 FX racks total `27.13 MB` rather than `29.38 MB`.
- **Part 3 Rank 6 (`CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` — Audit 3 Verification lines 241-270):**
  - `sizeof(Voice) = 33,128 B`, `2.66 MB` of redundant `MorphScratch`, and `>4,400` per-voice heap allocations are confirmed, but **L1D/L2 cache-miss CPU savings are UNPROVEN (`HYPOTHESIS`)** because no PMU miss counter has ever been measured on the Key 61 (`docs/release/CPU-REVIEW-MEMORY.md:25,344,449`).
- **Part 3 Rank 17 (`CONFIRMED`) & related already-fixed item:**
  - Mipmap Gibbs cosine taper (`wavetable_bank.cpp:300-302`) is already fixed in `develop` (`TODO.md:57`). Only the stale `README.md:13` (`20.5 MB`) and `wavetable_bank.h:18-21` comments remain, plus exact memory accounting in `own_wavetable_bank_memory`.

---

## 2. Baseline Measurement Before Any Change

Existing tools (`bench/tier_bench.cpp`, `bench/bench_engine.cpp`, `core/tests/test_rt_alloc.cpp`, `core/tests/test_own_osc_types.cpp`) measure CPU block times, zero-allocation RT safety, and wavetable bank size (`builtinBankBytes()`), **but no existing test or tool measures per-instance and 4-instance heap bytes, allocation counts, `VmRSS`, and `prepareDetailMs(0/1)` across tiers.**

Therefore, **Task T1 (the first task of the plan)** builds `core/tests/test_own_memory_footprint.cpp` (`synthyboy_test_own_memory`), using a byte-and-count-tracking `operator new` hook (modeled on `core/tests/alloc_hook.h:23-90`) plus `IEngine::prepareDetailMs(0/1)` (`core/include/synthyboy/engine.h:151`) and OS RSS (`VmRSS` from `/proc/self/status` on Linux; `mach_task_basic_info::resident_size` on macOS).

### Commands to run BEFORE any code change (baseline capture)

1. **Memory & prepare-time baseline (host + ARMv7 cross build on Key 61):**
   ```sh
   ./build/core/tests/synthyboy_test_own_memory
   ```
   **Inputs & numbers to record** (all at `44100 Hz`, `128`-frame blocks, `realtime = false` & `realtime = true` with `lazyTables = true`):
   - `1x low` (Key 61 profile: `8` voices, `3` unison, 5 FX masked), default Wavetable patch: `heap_bytes`, `alloc_count`, `prepare_voices_ms`, `prepare_fx_ms`, `rss_delta_kb`. (Expected baseline: `~80.7 MB`, `~4,978` allocs.)
   - `1x low` with `osc1.type = Spectral` + `reverb.force_enable = 1`: `heap_bytes`, `alloc_count`.
   - `4x low` (4 simultaneous `OwnEngine` instances): total `heap_bytes`, `alloc_count`, `rss_delta_kb`. (Expected baseline: `~322.7 MB` engine heap + `33.1 MB` WT bank.)
   - `1x xl` (`high,voices=16`, `fx=all`) and `1x high` (`32` voices, `fx=all`): `heap_bytes`, `alloc_count`, `prepare_voices_ms`, `prepare_fx_ms`.
   - `builtinBankBytes()`: expected `33,099,744 B` (`33.10 MB` / `31.57 MiB`).

2. **Exact bit-identity audio baseline (all 160 factory presets, float32 + FNV-1a hash per `docs/MPC.md:342`):**
   ```sh
   ./build/tools/preset-render/synthyboy_preset_render \
     --presets content/presets --engine own --tier all \
     --float32 --hash --wav /tmp/sb-mem-base/single --chord-wav /tmp/sb-mem-base/chord \
     --content-dir build/core/tests/output-stage-content-store
   ```
   Record all 960 `.hash` files (`160 presets × 3 tiers × single/chord`); after the plan lands, `scripts/compare-renders.sh /tmp/sb-mem-base /tmp/sb-mem-after` must report `0` differences.

3. **CPU & cache-miss baseline on Key 61 (interleaved A/B + optional PMU per Audit 3 Rank 9 / `CPU-REVIEW-MEMORY.md:344`):**
   ```sh
   # On Key 61 (isolated core 3, SCHED_FIFO 20 if rtrun available):
   ./synthyboy_tier_bench --presets content/presets --core 3 --config low --budget 70 \
     --load "Lorenz Garden" --load "Stretched Glass Choir" --load "Shepard Ascent" \
     --load "Spectral Vowel Freeze" --load "Frozen Pluck Drone" --load "Bowed MPE Strings" \
     --load "Diffusor Cloud Pad" --load "Mid Side Halo" --seconds 10 --repeat 3 --csv k61-mem-base.csv
   ```
   Also run `perf stat -e L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses` (if kernel `CONFIG_PERF_EVENTS` permits) around the same command to test the **HYPOTHESIS** that shared `MorphScratch` + compact `Voice` layout reduces L1D/L2 refills on `Stretched Glass Choir` and `Lorenz Garden`.

4. **Real-time allocation & thread-safety baseline:**
   ```sh
   ctest --test-dir build -R 'synthyboy_test_(rt|threads|own_engine|own_fx|own_voice_opt|own_wt_osc_opt|own_osc_types|own_content_opt|own_fx_makeup|own_filter_styles)' --output-on-failure
   ```

---

## 3. Design Options & Chosen Design

### 3.1 Specification constraints to preserve

- **`docs/engine-spec/01-voice-management.md:68,73-75,86-90`:**
  > Parameters: `master.polyphony` P (1..32, modulatable per note; effectively clamped to 1..32 and by the tier cap)... The pool has one spare voice so that Kill mode always finds a free voice... Lowering P while playing therefore fades the excess voices out in 50 ms rather than cutting.
  *(Own engine uses `kKillSpares = 4`, `own_engine.h:90`, so `poolSize = maxVoices + 4`.)*
- **`docs/ARCHITECTURE.md:167,178,182,191`:**
  > A `Quality` struct (not a global) is passed to `Engine::prepare`/`setQuality`... Presets store the *requested* settings; the tier only caps them, so a preset sounds the same shape on every device, just cheaper... No `new/delete/malloc`, no `std::mutex`, no file or syslog calls in `Engine::process`.
- **`core/include/synthyboy/engine.h:58-63`:**
  > `setQuality`: Changes the quality while playing: a lock-free hand-off (any thread, no allocation), the audio thread takes it over at the start of its next block. `maxVoices`, `maxUnison`, `interpolation` and `fxMask` take effect then (a voice that is above a lowered cap is released, not cut).
- **`core/include/synthyboy/quality.h:53-58`:**
  > `reverb.force_enable`: the user trades polyphony for the reverb, so a tier that masks the reverb out does not remove it while `reverb.force_enable` is on.
- **`docs/engine-spec/06-effects.md:185,248`:**
  > Reverb types `0..5` (Vital FDN16, Plate, Hall, Vintage, Nitrous, Basin) are mutually exclusive cores sharing the pre-filter and pre-delay pipeline.

### 3.2 Answering the key architectural question: Pre-allocated vs. Scaled vs. Lock-free Off-thread Hand-over

| Resource | Strategy | Why |
|---|---|---|
| Initial tier voice pool (`maxVoices + 4`: 12 on Key 61, 20 on XL, 36 on desktop) | **Pre-allocated at `prepare()`** | Note-on must never wait or allocate; 4 spares cover Kill-mode fade (`doc 01 §3.1-3.2`). |
| Unmasked instance-0 FX (`Chorus, Comp, Delay, Dist, EQ, Filter, Phaser, Utility` on `low`) — **ONE shared set across all 3 racks** | **Pre-allocated at `prepare()`** | Instant click-free FX enable/rack move; `fx_chain.cpp:412-429` proves at most 1 rack owns each instance-0 module. |
| `FilterImpl` unified delay arena (`30.4 KB`/ch @ 44.1 kHz) & `ClassicFilter::comb` (`32.8 KB`/ch) | **Pre-allocated per prepared voice** | `filter1/2.model` can switch at any time; pooling the 3 mutually exclusive `FilterImpl` families saves 54% RAM & 15 allocs/filter with 0 latency on switch. |
| `Reverb` unified delay arena (`2.36 MB` @ 44.1 kHz) | **Pre-allocated when `kFxReverb` is unmasked; lazy off-thread when masked** | Pooling the 4 mutually exclusive reverb topologies saves 52% RAM & 92 allocs; on tier `low` it costs `0 B` unless `reverb.force_enable` or `setQuality` unmasks it. |
| Engine-shared `MorphScratch` (`24.6 KB`) & `spectralScratch` (`76 KB`) | **Pre-allocated once per `OwnEngine`** | Single audio thread renders voices sequentially; zero risk, saves `2.63 MB`. |
| `fxMakeupShell[0..12]` (lightweight `FxModule` shells without `prepare()`) | **Pre-allocated at `prepare()` (~15 KB total)** | `Quality::fxMakeup` (`fx_chain.cpp:429,445`) needs `hasMakeup()`/`makeup()` for bypassed FX immediately with `0 B` of delay buffers. |
| `LibOsc` sub-engines (`Sample`, `Granular`, `Spectral`, `Multisample`), sized to `maxUnison` | **Pre-allocated at `prepare()` ONLY for active `osc1..3.type`; lazy off-thread per `(osc, type)` on type switch** | Saves `29.68 MB` on Wavetable patches (`>75%` of presets) and `90%+` on single-type content patches; `oscN.type` is non-modulatable (`param_table.cpp`). |
| Voice pool growth (`setQuality` raising `maxVoices` / `maxUnison`) & masked FX unmasking (`reverb.force_enable` / `setQuality`) | **Prepared off-thread by existing `sb-fx-build` worker (`OwnEngine::fxWorker`), published via lock-free atomic pointers** | Zero allocation & zero locks on the audio thread (`realtime == true`); deterministic synchronous build inside `setQuality`/`setParam` when `realtime == false` (tests & offline bounce). |
| Built-in wavetable bank (`33.10 MB` process-shared) | **Keep bit-identical (lazy per-table on MPC via `Quality::lazyTables`)** | Already shared across all instances (`1×` cost even for 4 instances); lowering mip floor below 256 would increase high-pitch interpolation aliasing (`wavetable_bank.cpp:231`). |

**Exact lock-free off-thread resize hand-over protocol:**
1. **Fixed-size atomic slot tables (zero vector reallocation on the audio thread):**
   - `std::array<std::atomic<Voice*>, kPoolVoices> voiceSlots {}` (`kPoolVoices = 36`, `is_always_lock_free`).
   - `std::array<std::atomic<FxModule*>, kNumFxModules> fxLive {}` inside `FxInst0Table`.
   - `std::atomic<SampleSub*>`, `std::atomic<GranularSub*>`, `std::atomic<SpectralSub*>`, `std::atomic<MultiSub*>` inside each `LibOsc`.
2. **Writer ownership & retirement (TSan-clean, zero audio-thread `delete`):**
   - Non-RT allocations are pushed into mutex-guarded `std::vector<std::unique_ptr<...>>` owned vectors (e.g. `voiceOwned`, `fxOwned`, `libOwned`) that are ONLY cleared in `OwnEngine::prepare()` (while `process()` is not running per `engine.h:31`) or `~OwnEngine()`. Because audio-thread pointers are never invalidated until `prepare()` or destruction, **no ABA or use-after-free can occur** even if `setParam`/`setQuality` hammers concurrently (`test_threads.cpp`).
3. **Publication & consumption:**
   - **Live mode (`realtimeFlag == true`):** `setQuality()` / `setParam()` sets atomic request targets (`wantPoolVoices`, `wantUnison`, `wantFxMask`, `wantLibTypeMask[3]`), sets `fxWorkDue = true`, and notifies `fxWake` (existing `WakeSignal` of `core/engine-own/dsp/helper_thread.h:32-83`, waking `sb-fx-build` off the isolated audio cores). The worker allocates, calls `prepare()`, and publishes each pointer with `std::memory_order_release` (and bumps `preparedVoices` / `preparedUnison` with `release`).
   - **Offline / test mode (`realtimeFlag == false`):** `setQuality()` / `setParam()` runs the same mutex-guarded `servicePoolWork()` helper synchronously inside `try { ... } catch (...) {}` (since both are `noexcept`), publishing via the exact same `release` stores so offline renders and tests see the resources on the very next `process()` call with 0 allocations inside `process()` and full TSan cleanliness.
   - **Audio thread (`process()`):** Reads `voiceSlots[i].load(acquire)`, `fxLive[m].load(acquire)`, and `LibOsc` sub-engine pointers with `acquire`. If a lazily-requested resource is still `nullptr` (for ~1–15 ms in live mode), the audio thread safely skips/bypasses it and fades it in cleanly (`fadeIn[m] = true` on FX) once the atomic becomes non-null. `Voice::attachPool(&activeMask, i)` only records `poolMask` and `poolBit` without touching `*poolMask` when `!active`, avoiding any data race with the audio thread's `activeMask`.

### 3.3 Comparison of Design Options

| Dimension | Option 1: Static Tier-Sized Pools + Arenas Only | Option 2 (**CHOSEN**): Tier-Scaled Initial + Unified Arenas + Shared `fxInst0` + Lock-Free Off-Thread Growth | Option 3: Global Cross-Instance Slab / Voice Pool |
|---|---|---|---|
| **How it works** | Size `voices` to `maxVoices + 4` in `prepare()`; pool `FilterImpl`, `Reverb`, `MorphScratch`; keep all 3 FX racks & eager `LibOsc`. No runtime growth. | Option 1 **plus**: single shared `fxInst0[13]` across 3 racks; skip preparing masked FX; lazy per-`(osc,type)` `LibOsc` sized to `maxUnison`; hot/cold `Voice` split; lock-free off-thread growth via `sb-fx-build`. | Global singleton memory slab allocating voices/FX dynamically across all plugin instances. |
| **Sound change** | None (bit-identical; clears stale reverb/filter tail on topology switch). | **None** (100% bit-identical on all 160 presets & offline tests; in live mode, first-ever unmask of a masked FX or switch to an unbuilt `oscN.type` fades in ~1–15 ms later, matching existing `FxPool` and `lazyTables` UX). | None, unless global slab exhausts under multi-instance load (steals across tracks!). |
| **Key 61 RAM (1 inst / 4 inst + 33 MB WT)** | `~26 MB` / `~137 MB` (still pays 3× FX and eager `LibOsc`). | **`7.90 MB` (WT) – `10.17 MB` (1 Spectral)** / **`64.7–73.8 MB`** (**−90% per instance**, saves **~285 MB** at 4 instances!). | Similar RAM to Option 2, slightly lower if instances never peak together. |
| **Desktop / XL RAM (1 inst, WT)** | `~62 MB` (high) / `~38 MB` (XL). | **`22.38 MB` (high)** / **`15.50 MB` (XL)** (down from `80.67 MB`). | Similar. |
| **Startup `prepare()` (Key 61, HYPOTHESIS)** | `169 ms → ~65–80 ms`. | **`169 ms → ~18–35 ms`** (`−80%`). | `~20 ms`, higher lock contention on multi-instance open. |
| **CPU effect** | Neutral; small L2 win from shared `MorphScratch` (HYPOTHESIS). | Neutral to positive: shared `MorphScratch` + compact hot `Voice` (`33.1 KB → ~19.8 KB`, fits 32 KB L1D) + `96%` fewer heap pages (HYPOTHESIS: verify with PMU). | Extra indirection & cross-instance cache-line ping-pong. |
| **RT safety & contract compliance** | **FAILS** `setQuality` upward voice/FX growth (`engine.h:58-63`, `test_rt_alloc.cpp:150-152`, `test_engine.cpp:597`) and `reverb.force_enable` (`test_own_fx.cpp:1048`). | **100% RT-safe & contract-compliant** (0 audio-thread allocs/locks, TSan-clean, full `setQuality` and `reverb.force_enable` support). | Complex lock-free slab, high ABA/TSan risk, violates per-instance isolation (`quality.h:73`). |
| **Risk** | Low code risk, breaks existing tests/contracts. | **Low–Medium**: reuses the proven `OwnEngine::fxWorker` + `WakeSignal` + atomic publication pattern already tested in `test_own_helper_thread` and `FxPool`. | **High**: hard-to-debug cross-instance lifetime bugs inside MPC host process (`docs/MPC.md:361`). |

**Chosen design: Option 2.** It delivers the largest per-instance and 4-instance memory reduction (`80.67 MB → 7.90–10.17 MB` on Key 61; `355.8 MB → 64.7–73.8 MB` for 4 instances), preserves 100% bit-identical audio, satisfies every `IEngine` / `setQuality` / `reverb.force_enable` / TSan / RT-alloc contract, and decomposes cleanly into small, independent Flash-worker tasks.

---

## 4. Task Breakdown (Flash-Worker Sized, Zero File Overlap per Batch)

Rules respected by every task below:
- One commit pair per task: Commit 1 adds the failing test; Commit 2 implements the fix and makes the test pass.
- At most 3 editable files per task; **no two tasks in the same batch edit the same file** (including `core/tests/CMakeLists.txt`).
- Every intermediate batch compiles cleanly and keeps all existing tests passing.
- Bit-identical tasks print `<NAME>-CHECKSUM` (64-bit FNV-1a of output float bits) so the build worker can verify zero audio regression.

### Dependency Graph

```text
Batch 1 (parallel):  T1 (mem probe)   T2 (FilterImpl arena)   T3 (Reverb arena)   T4 (WT bank docs/test)
                                                │                    │
Batch 2 (parallel):              T5 (WtOsc shared scratch)   T6 (LibOsc lazy)   T7 (Shared FxInst0)
                                          │                        │                  │
Batch 3 (serial):                         └──────────► T8 (Voice hot/cold & wiring)   │
                                                                   │                  │
Batch 4 (serial):                                                  └──────► T9 (OwnEngine tier pool & off-thread resize)
                                                                                      │
Batch 5 (serial):                                                                     ▼
                                                                           T10 (Strict memory acceptance gates)
```

---

### Batch 1 (4 parallel tasks — zero overlapping files)

#### Task T1 — Memory Footprint & Prepare-Time Measurement Probe (Prerequisite)
- **Depends on:** None.
- **Editable files (2):** `core/tests/test_own_memory_footprint.cpp` (new), `core/tests/CMakeLists.txt`.
- **What to change:**
  1. Create `core/tests/test_own_memory_footprint.cpp` with a thread-local counting `operator new` / `delete` hook (modeled on `core/tests/alloc_hook.h:23-90`) that tracks `g_allocCount` AND `g_allocBytes` (total bytes requested) plus peak live bytes (`g_liveBytes`, `g_peakBytes`, storing size header or using an atomic counter on `operator new(size_t)` + `operator delete(void*, size_t)`). Also read OS RSS (`VmRSS` from `/proc/self/status` on Linux, `mach_task_basic_info` on macOS).
  2. Add tests measuring at `44100 Hz`, `128`-frame blocks:
     - `own_memory_report_single_tiers`: prepares 1 `OwnEngine` at Key 61 `low` (`maxVoices=8, maxUnison=3, low fxMask`), XL (`maxVoices=16, maxUnison=16, kFxAll`), and `high` (`32, 16, kFxAll`) with `lazyTables = false` (after warming the shared WT bank once beforehand so engine heap is isolated from the shared 33.1 MB bank). Prints `MEM-BASELINE tier=<name> allocs=<N> bytes=<B> prepare_voices_ms=<V> prepare_fx_ms=<F> rss_kb=<R>`.
     - `own_memory_report_four_instances_low`: prepares 4 `OwnEngine` instances simultaneously at `low`, prints total `bytes`, `allocs`, and `rss_kb`.
     - `own_memory_report_content_and_reverb`: prepares 1 `OwnEngine` at `low` with `osc1.type = Spectral` and `reverb.force_enable = 1`, prints `bytes` and `allocs`.
  3. Register `synthyboy_test_own_memory` in `core/tests/CMakeLists.txt` (including `core/engine-own` include dir).
- **Test (Commit 1 → Commit 2):**
  - Signal / input: `OwnEngine::prepare(44100.0, 128, quality)` on default state, Spectral+Reverb state, and 4 instances.
  - Measured quantity: `allocs`, `bytes`, `prepareDetailMs(0)`, `prepareDetailMs(1)`, `builtinBankBytes()`.
  - Threshold: In T1, asserts the measurement hook observes `> 0` bytes/allocs and `builtinBankBytes() == 33099744u` (before-state sanity: `1x low` reports `~80.7 MB` / `~4,978` allocs; `4x low` reports `~322.7 MB`; strict post-fix upper bounds are locked in T10).
- **Sound bit-identical:** N/A (measurement harness only).

#### Task T2 — `FilterImpl` Unified Delay Arena (Part 1 Rank 10 + Part 3 Rank 6)
- **Depends on:** None.
- **Editable files (3):** `core/filters-serum/src/filter_dsp.h`, `core/filters-serum/src/filter.cpp`, `core/tests/test_own_filter_styles.cpp`.
- **What to change:**
  1. In `core/filters-serum/src/filter_dsp.h:344-470`: add a non-owning `DelaySlice` (or extend `DelayLine` with `void bind (float* externalBuf, size_t len)` alongside owning `prepare()` so standalone callers keep working): stores `float* data_`, `size_t size_`, `size_t writeIndex_`, `bool written_`, keeping exact `read()`, `tapFor()`, `readTap()`, `write()`, `writeSanitized()`, and `reset()` (`if (!written_) return;`) logic bit-for-bit identical. Update `AllpassDiffuser` to forward `bind(float*, size_t, float g)`.
  2. In `core/filters-serum/src/filter.cpp:34-110,833-837`:
     - Add `std::vector<float> delayArena_;` and `int delayGroup_ = -1;` (`0` = Comb family: `Cmb*/Flg*/Phs*`, `DistComb2LP/BP`, `Comb2`; `1` = Diffusor family: `Allpasses`, `Diffusor`; `2` = Reverb family: `Combs`, `Reverb`; `-1` = non-delay filter).
     - In `FilterImpl::prepare(double sampleRate)`: compute exact lengths for Group 0 (`2 × (maxDelaySamples + 4)`), Group 1 (`sum_{0..7}(diffLen_i + 4)`), Group 2 (`sum_{0..3}(combLen_i + 4) + (ap0Len + 4) + (ap1Len + 4)`). Allocate `delayArena_.assign(std::max({len0, len1, len2}), 0.0f)` (**1 heap allocation**, `7,597` floats = `30,388 B` at 44.1 kHz vs `16,607` floats = `66,428 B` / 16 allocs today). Bind `combDelay1_/2_` starting at offset `0`, `diffuserDelays_[0..7]` starting at offset `0`, and `reverbCombs_[0..3]` + `reverbAP_[0..1]` starting at offset `0`.
     - In `FilterImpl::setType(int type)`: compute `newGroup = delayGroupFor(type)`. If `newGroup >= 0 && newGroup != delayGroup_`, zero `delayArena_.data()[0 .. groupLen[newGroup])`, reset write indices and `written_` flags for that group, and set `delayGroup_ = newGroup`.
     - Expose free test helpers `namespace synthyboy::filters { size_t filterDelayBytes (const Filter&) noexcept; size_t filterDelayAllocs (const Filter&) noexcept; }` defined in `filter.cpp` and forward-declared in `test_own_filter_styles.cpp` (staying within 3 editable files without touching `filter.h`).
- **Test (in `core/tests/test_own_filter_styles.cpp`):**
  - Signal: 4096-sample deterministic saw+noise burst at `44100 Hz` through `Filter` for all 6 delay-using types (`Comb2`, `CmbL`, `Allpasses`, `Diffusor`, `Combs`, `Reverb`) plus cross-family switch `Reverb → Diffusor → Comb2` on silence checking zero residual bleed.
  - Measured quantity: `filterDelayBytes(f)` and `filterDelayAllocs(f)` at `44100 Hz` and `96000 Hz`, plus `FILTER-ARENA-CHECKSUM`.
  - Threshold: `filterDelayBytes(f) <= 32768` at `44100 Hz` (before: `66,428 B`, after: `30,388 B`), `filterDelayAllocs(f) == 1` (before: `16`).
- **Sound bit-identical:** YES (`FILTER-ARENA-CHECKSUM` matches before vs after for every single filter type).

#### Task T3 — `Reverb` Unified Delay Arena (Part 2 Rank 5 Memory Half)
- **Depends on:** None.
- **Editable files (3):** `core/engine-own/fx/fx_dsp.h`, `core/engine-own/fx/fx_reverb.h`, `core/tests/test_own_fx.cpp`.
- **What to change:**
  1. In `core/engine-own/fx/fx_dsp.h:17-86`: add `void bind (float* externalBuf, int pow2Size) noexcept` and `size_t allocatedBytes() const noexcept` to `DelayLine` (when bound, `ptr = externalBuf`, `mask = pow2Size - 1`, `pos = 0`, `buf` remains empty; `push/read/readCubic/readLagrange4/readLagrange8/reset` operate on `ptr` with `mask`, 0 extra branches in sample loops!). Update `AllPassLine` in `fx_reverb.h:37-49` with `void bind (float* buf, int pow2Size) noexcept`.
  2. In `core/engine-own/fx/fx_reverb.h:76-180,515-535`:
     - Replace the 93 individual `DelayLine::prepare()` allocations with a single `std::vector<float> arena;` allocated once in `Reverb::prepare(double sr)`.
     - Compute exact power-of-two sizes for every line using a helper `pow2Size(int maxSamples)` matching `fx_dsp.h:22-24`:
       - Permanent prefix: `pre[0]` + `pre[1]` (`32,768` floats at 44.1 kHz).
       - Overlay core union starting at `coreOffset = preSize * 2`:
         - Group 0 (`type == 0`, FDN16): `line[16]` + `apLine[16]` = `557,056` floats @ 44.1 kHz (`589,824` @ 48 kHz).
         - Group 1 (`type == 1`, Plate): `erLine` + `plModAp[2]` + `plDel1[2]` + `plAp2[2]` + `plDel2[2]` + `plIn[4]` = `204,800` floats @ 44.1 kHz.
         - Group 2 (`type == 2, 4, 5`, Net8): `erLine` + `net[8]` + `netAp[8]` + `inAp[4]` = `282,624` floats @ 44.1 kHz.
         - Group 3 (`type == 3`, Vintage): `erLine` + `vInAp[2]` + `vComb[2][8]` + `vAp[2][4]` = `157,696` floats @ 44.1 kHz.
       - Allocate `arena.assign(coreOffset + maxCoreFloats, 0.0f)` (`589,824` floats = **`2,359,296 B`** at 44.1 kHz, **`2,490,368 B`** at 48 kHz; **1 allocation** instead of **93**). Bind all lines into `arena.data()`.
     - Track `int activeGroup = -1;` (`groupOf(type)`). In `Reverb::process()`, if `groupOf(type) != activeGroup`, zero `arena[coreOffset .. coreOffset + groupFloats[newGroup])`, reset write indices (`pos = 0`) and scalar states (`hsLp`, `lsLp`, `netDamp`, `netLen`, `plDamp`, `plFeed`, `vDamp`, `vLp`) of `newGroup`, and set `activeGroup = newGroup`. In `Reverb::reset()`, zero `pre[0..1]` plus only the active group slice (or full arena if `activeGroup < 0`).
     - Expose `size_t memoryBytes() const noexcept { return arena.size() * sizeof(float); }` and `int bufferAllocCount() const noexcept` (returns `arena.empty() ? 0 : 1`).
- **Test (in `core/tests/test_own_fx.cpp`):**
  - Signal: 8192-sample impulse + stereo noise burst at `44100 Hz` through `Reverb` for every `reverb.type` `0..5` (all rooms for Nitrous), plus live topology switching `0 → 1 → 2 → 3 → 4 → 5 → 0` followed by silence check (no stale bleed).
  - Measured quantity: `r.memoryBytes()` at `44100 Hz` and `48000 Hz`, `r.bufferAllocCount()`, and `REVERB-ARENA-CHECKSUM` per type `0..5`.
  - Threshold: `memoryBytes() <= 2,400,000` at `44100 Hz` (before: `4,874,240 B`, after: `2,359,296 B`), `memoryBytes() <= 2,550,000` at `48000 Hz` (before: `5,087,232 B`, after: `2,490,368 B`), `bufferAllocCount() == 1` (before: `93`).
- **Sound bit-identical:** YES for each `reverb.type` `0..5` (`REVERB-ARENA-CHECKSUM`).

#### Task T4 — Wavetable Bank Documentation & Exact Byte Accounting (Part 3 Rank 17)
- **Depends on:** None.
- **Editable files (3):** `core/engine-own/README.md`, `core/engine-own/osc/wavetable_bank.h`, `core/tests/test_own_osc_types.cpp`.
- **What to change:**
  1. Update `core/engine-own/README.md:13`: replace `"21 half-octave mip levels stored at 2x their band: the eight 64 frame tables take 20.5 MB"` with `"18 mip levels (1024..256 octave, 181..1 half-octave) stored at >=8x their band with a 256-sample floor: 55.6 KB time + 8.2 KB spectrum = 63.8 KB/frame; 32.65 MB (31.14 MiB) for the eight 64-frame tables and 33.10 MB (31.57 MiB) for all 15 built-in tables, shared once per process"`.
  2. Update `core/engine-own/osc/wavetable_bank.h:18-21`: correct `"at least 16"` → `"at least 256"` and `"6.0 times ... 29 MB"` → `"6.79 times one 2048-sample cycle (13,896 time floats + 2,048 spectrum floats = 63,776 B/frame; 32.65 MB / 31.14 MiB for the eight 64-frame tables, 33.10 MB / 31.57 MiB for all 15 built-in tables)"`.
  3. Update `core/tests/test_own_osc_types.cpp:637-642` (`own_wavetable_bank_memory`): assert exact formula `519u * (13896u + 2048u) * sizeof(float) == 33099744u` (`CHECK_EQ (bytes, 33099744u)`), print both decimal MB (`33.10 MB`) and binary MiB (`31.57 MiB`), and keep `< 32 MiB` ceiling guard.
- **Test (in `core/tests/test_own_osc_types.cpp`):**
  - Signal / input: `own::builtinBankBytes()`.
  - Measured quantity: exact byte count + level size check (`Wavetable::size(17) == 256`, `Wavetable::kNumLevels == 18`).
  - Threshold: `CHECK_EQ (bytes, 33099744u)` (fails before if we check `README.md`/exact accounting assertion; specifically commit 1 checks `builtinBankBytes() == 33099744u` AND `Wavetable::kBankDocumentedBytes == 33099744u` added as `constexpr` in `wavetable_bank.h`, which fails to compile/pass before commit 2 updates `wavetable_bank.h` and `README.md`).
- **Sound bit-identical:** YES (zero DSP change).

---

### Batch 2 (3 parallel tasks — depends on Batch 1; zero overlapping files)

#### Task T5 — `WtOsc` External Shared `MorphScratch` & Tier-Sized `history` (Part 3 Rank 6)
- **Depends on:** Batch 1.
- **Editable files (2):** `core/engine-own/osc/wt_osc.h`, `core/tests/test_own_wt_osc_opt.cpp`.
- **What to change:**
  1. In `core/engine-own/osc/wt_osc.h:494-525,612,1223-1231`:
     - Replace `std::unique_ptr<MorphScratch> morphScratch;` with `MorphScratch* morphScratchPtr = nullptr;` + `std::unique_ptr<MorphScratch> ownedMorphScratch;` and `int historyVoices = 0;`.
     - Update `clearState()` (`wt_osc.h:494-512`) to preserve `morphScratchPtr`, `ownedMorphScratch`, `history`, `historyVoices` across in-place placement-new reset.
     - Update `prepareMorph (MorphScratch* sharedScratch = nullptr, int maxUnison = kMaxVoices)`:
       - If `sharedScratch != nullptr`: set `morphScratchPtr = sharedScratch` and do NOT allocate `ownedMorphScratch`.
       - Else if `morphScratchPtr == nullptr`: allocate `ownedMorphScratch = std::make_unique<MorphScratch>()` and set `morphScratchPtr = ownedMorphScratch.get()` (preserves 100% compatibility with `OscCycle` and standalone tests).
       - Allocate `history` to `historyVoices = std::clamp(maxUnison, 1, kMaxVoices)` × `(kTaps - 1)` floats (`2,032 B` at `maxUnison = 4` vs `8,128 B` at `16`).
     - In `updateMorph()` (`wt_osc.h:612`): pass `*morphScratchPtr` to `buildMorphCycle(...)` (guarded by `morphScratchPtr != nullptr`).
     - In `render()` (`wt_osc.h`): clamp active oversampled voice count to `std::min(curCount, historyVoices)` when indexing `history` (safety guard if `setQuality` raises `maxUnison` before off-thread resize completes). Add `size_t morphOwnedBytes() const noexcept` (`(morphBuf.size() + history.size()) * 4 + (ownedMorphScratch ? sizeof(MorphScratch) : 0)`).
- **Test (in `core/tests/test_own_wt_osc_opt.cpp`):**
  - Signal: 4096-sample chord with 2 `WtOsc` instances sharing one `MorphScratch` (`Vocode`, `HarmonicStretch`, `Smear`, `PhaseDisperse` morphs + `Sync` warp at `maxUnison = 4`), compared against legacy private-scratch `WtOsc`.
  - Measured quantity: `osc.morphOwnedBytes()` and `WTOSC-SHARED-SCRATCH-CHECKSUM`.
  - Threshold: `morphOwnedBytes() <= 35,000 B` (before: `65,552 B`, after: `32,832 + 2,032 = 34,864 B` — saving `30,688 B/osc`, or `2.63 MB` across 108 oscs for `MorphScratch` alone); `memcmp` output bit-identical (`0` diff).
- **Sound bit-identical:** YES (`WTOSC-SHARED-SCRATCH-CHECKSUM`).

#### Task T6 — `LibOsc` Stack Scratch & On-Demand Per-Type Sub-Engines (Part 1 Rank 10 + Part 3 Rank 6)
- **Depends on:** Batch 1.
- **Editable files (3):** `core/engine-own/osc/lib_osc.h`, `core/engine-own/osc/lib_osc.cpp`, `core/tests/test_own_content_opt.cpp`.
- **What to change:**
  1. In `core/engine-own/osc/lib_osc.h:192-227` and `lib_osc.cpp:123-147,900-1015`:
     - Remove heap vectors `tmpL, tmpR, mixL, mixR, modBuf, rateBuf` (`6 × std::vector<float>`) from `LibOsc` and declare them as stack `float[kControlPeriod]` arrays inside `LibOsc::render()` / `renderType()` (eliminates 6 heap allocations per `LibOsc` = 648 allocs on 36 voices).
     - Extract the 4 heavy sub-engine payloads into 4 structs:
       - `struct SampleSub { int capU; std::vector<sampleosc::SampleVoice> voices; sampleosc::SampleOscParams params; };`
       - `struct GranularSub { int capU; std::vector<granular::GranularVoice> voices; granular::GrainParams params; };`
       - `struct SpectralSub { int capU; std::vector<spectral::SpectralVoice> voices; spectral::SpectralParams params; std::vector<float> filterGain, ring, extMag; float fltX[257], fltY[257]; };`
       - `struct MultiSub { int capU; std::vector<Layer> layers; };`
     - Store 4 atomic pointers in `LibOsc`: `std::atomic<SampleSub*> sampleSub { nullptr }`, `std::atomic<GranularSub*> granSub { nullptr }`, `std::atomic<SpectralSub*> specSub { nullptr }`, `std::atomic<MultiSub*> multiSub { nullptr }` (and `std::vector<std::unique_ptr<...>>` or non-RT owned storage freed only in `~LibOsc()` / `prepare()` so audio-thread loads are never invalidated!). Note: moving `fltX[257], fltY[257]` (`2,056 B`) + `*Params` (`~400 B`) into the lazy structs also shrinks `sizeof(LibOsc)` by `~2.5 KB` (`~7.5 KB` saved per `Voice`!).
     - Update `void prepare (double sampleRate, int maxUnison = kMaxU, bool eagerTypes = true)`:
       - Stores `sr`, `spectralGain_`, `fadeTotal`, `capU = std::clamp(maxUnison, 1, kMaxU)`. Clears/frees any previous sub-engines (non-RT).
       - If `eagerTypes == true` (default: preserves standalone callers), calls `prepareType(t, capU)` for `t = 1..4`. If `eagerTypes == false`, allocates **0 B** of sub-engine buffers!
     - Add `void prepareType (int type, int maxUnison)` (non-RT / worker thread, idempotent if already `>= maxUnison`): allocates & prepares the sub-engine for `type ∈ {1..4}` sized to `std::clamp(maxUnison, 1, kMaxU)` (for Spectral: `std::clamp(maxUnison, 1, kSpecU)`), pushes old/new ownership into non-RT owned vector, and publishes the pointer with `std::memory_order_release`.
     - Add `bool hasTypeReady (int type, int minUnison) const noexcept` (`acquire` load) and `size_t subEngineBytes() const noexcept`.
     - In audio-thread methods (`update`, `beginType`, `renderType`, `noteOff`, `stopAll`, `readPosition`, `readGrains`): load the needed atomic pointer with `memory_order_acquire`; if `nullptr`, treat as silent / no-op (zero allocation, zero crash); clamp active unison loop count to `std::min(U, sub->capU)`.
- **Test (in `core/tests/test_own_content_opt.cpp`):**
  - Signal: Render 4096 samples at `44100 Hz` for each of the 4 library types (`Sample`, `Granular`, `Spectral`, `Multisample`) at `maxUnison = 4` comparing `eagerTypes = true` vs `eagerTypes = false` + `prepareType(type, 4)`, plus render with `eagerTypes = false` before `prepareType` (asserts exact silence, finite, 0 crash).
  - Measured quantity: `lib.subEngineBytes()` before vs after `prepareType`, `sizeof(LibOsc)`, and `LIBOSC-LAZY-CHECKSUM` across all 4 types.
  - Threshold: `eagerTypes = false` allocates `0 B` (`subEngineBytes() == 0`, before: `~274,800 B`); `prepareType(Granular, 4)` allocates `<= 20,000 B` (computed: `~15,900 B`); `sizeof(LibOsc) <= 2,200 B` (before: `~4,000 B`).
- **Sound bit-identical:** YES (`LIBOSC-LAZY-CHECKSUM` matches bit-for-bit on all 4 types).

#### Task T7 — Shared Instance-0 FX Table (`FxInst0Table`) & Deferred Masked FX (Part 3 Rank 1)
- **Depends on:** Batch 1 (especially T3 for Reverb arena sizing).
- **Editable files (3):** `core/engine-own/fx/fx_chain.h`, `core/engine-own/fx/fx_chain.cpp`, `core/tests/test_own_fx_makeup.cpp`.
- **What to change:**
  1. In `core/engine-own/fx/fx_chain.h` & `fx_chain.cpp`:
     - Add `class FxInst0Table` (owned once by `OwnEngine`, shared across `fx[0..2]` via `FxChain::setSharedInst0(FxInst0Table*)`, parallel to `FxPool`):
       - `std::unique_ptr<FxModule> makeupShell[kNumFxModules]`: constructed via `makeFxModule(m)` **without** calling `prepare()` (0 B delay buffers; immutable after `prepare()`, used strictly for `hasMakeup()` and `makeup(...)` when a module is bypassed by `fxMask`).
       - `std::array<std::atomic<FxModule*>, kNumFxModules> live {}`: prepared modules published via `memory_order_release`.
       - `std::mutex buildMutex;` + `std::vector<std::unique_ptr<FxModule>> owned;` (freed only in `prepare()` / `~FxInst0Table()`).
       - `void prepare (double sr, int maxBlock, uint32_t initialFxMask)`: resets `live`, clears `owned`, builds `makeupShell[0..12]` once, and calls `ensurePrepared(m)` ONLY for `m` where `(initialFxMask & (1u << m)) != 0`.
       - `void ensurePrepared (int m)` (thread-safe via `buildMutex`): if `live[m].load(acquire) != nullptr`, returns; else constructs `makeFxModule(m)`, calls `prepare(sr, maxBlock)` + `setSampleRate(sr)`, stores ownership in `owned`, and publishes `live[m].store(raw, memory_order_release)`.
       - `bool isPrepared (int m) const noexcept { return live[m].load(std::memory_order_acquire) != nullptr; }`
       - `int preparedCount() const noexcept` and `void resetAll() noexcept`.
     - In `FxChain`:
       - Add `void setSharedInst0 (FxInst0Table* t) noexcept { sharedInst0 = t; }`.
       - In `FxChain::prepare(sr, maxBlock)`: if `sharedInst0 != nullptr`, skip allocating local `inst[0..12]` (0 B!); keep local `inst[0..12]` fallback ONLY when `sharedInst0 == nullptr`.
       - Add inline helpers `FxModule* liveModule(int m) const noexcept` (loads `sharedInst0->live[m].load(acquire)`) and `FxModule* makeupModule(int m) const noexcept` (`sharedInst0->makeupShell[m].get()`).
       - In `FxChain::update()` (`fx_chain.cpp:428-475`): `on[m]` requires `liveModule(m) != nullptr` (if a newly unmasked module is still being built on `sb-fx-build`, `on[m]` stays `false` until `liveModule(m)` becomes non-null, at which point `!prevOn[m]` triggers `reset()` + `fadeIn[m] = true` cleanly!). `bypassed[m]` uses `makeupModule(m)->hasMakeup()` and `makeupModule(m)->makeup(...)`.
       - In `FxChain::reset()`: only `rackId == 0` calls `sharedInst0->resetAll()` (identical to `pool->resetAll()` at `fx_chain.cpp:219-220`).
- **Test (in `core/tests/test_own_fx_makeup.cpp`):**
  - Signal: Test `FxInst0Table` + 3 `FxChain` racks (`Main`, `Bus 1`, `Bus 2`) prepared at `44100 Hz` with `low.fxMask`:
    1. Verify `sharedInst0.preparedCount() == 8` (before: `3 × 13 = 39` modules prepared across 3 racks) and `isPrepared(8 /*Reverb*/) == false`.
    2. Verify bypassed loudness makeup for Reverb, Flanger, HyperDimension, Bode matches existing makeup values bit-for-bit (`FX-SHARED-INST0-CHECKSUM`).
    3. Verify moving Delay from Main (`rack 0`) → Bus 1 (`rack 1`) → Bus 2 (`rack 2`) produces bit-identical output to 3 independent racks.
    4. Call `sharedInst0.ensurePrepared(8)` and verify Reverb activates cleanly on the next block.
  - Threshold: `preparedCount() == 8` on `low.fxMask` (before: `39` module instances across 3 racks); `FX-SHARED-INST0-CHECKSUM` bit-identical.
- **Sound bit-identical:** YES (`FX-SHARED-INST0-CHECKSUM`).

---

### Batch 3 (1 task — depends on Batch 2)

#### Task T8 — `Voice` Hot/Cold Layout Separation & Wiring Shared `MorphScratch` / `LibOsc` (Part 3 Rank 6 + Part 1 Rank 10)
- **Depends on:** T5, T6.
- **Editable files (3):** `core/engine-own/voice/voice.h`, `core/engine-own/voice/voice.cpp`, `core/tests/test_own_voice_opt.cpp`.
- **What to change:**
  1. In `core/engine-own/voice/voice.h:106,135-140,213-397`:
     - Update `Voice::attachPool(uint64_t* mask, int index)` so it only writes `*poolMask` if `active == true` (safe for off-thread `sb-fx-build` to attach a freshly constructed inactive voice without racing the audio thread's `activeMask`!).
     - Update `void prepare (double sampleRate, MorphScratch* sharedMorphScratch = nullptr, int maxUnison = kMaxUnisonVoices, bool eagerLibOsc = true);` and add `void prepareLibType (int oscIndex, int type, int maxUnison)` + `bool hasLibTypeReady (int oscIndex, int type, int minUnison) const noexcept`.
     - Extract cold setup/cache structs into `struct VoiceCold` held via `std::unique_ptr<VoiceCold> cold` (allocated once in `Voice::Voice()` / `Voice::prepare()` so standalone `Voice` tests work without null checks):
       - `LfoSettings lfoSet[10]` (`~1,120 B`)
       - `RlfoSettings rlfoSet[4]` (`~224 B`)
       - `EnvSettings envCache[6]` (`~384 B`)
       - `OscPart oscPart[3]` (including `libV[98]` × 3, `~1,620 B`)
       - `SubPart subPart` (`~52 B`)
       - `shapes::ShapeData scratchShape` (`~1,040 B`)
       - `float vcRand[4]`, `uint32_t envSetEpoch[6]`, `double envSetBpm[6]`, `float envSetFactor[6]`, `bool envSetOk[6]`, `envCacheEpoch`, `vcCacheEpoch`, `vcCacheSequence`, `envCacheBpm`, `envCacheValid`, `vcCacheValid`.
       *(Keep fields directly read by `VoiceOptTest::diff` at `core/tests/test_own_voice_opt.cpp:44-84` — `envLen`, `legatoInv`, `lfoSmoothL/R`, `lfoAge`, `lfoReadPos`, `mod`, `modR`, `vc`, etc. — on `Voice` so `VoiceOptTest::diff` stays intact, placing readout-only fields like `envLen` at the bottom of `Voice`.)*
     - Order remaining `Voice` members with `alignas(64)` hot header first (`active`, `released`, `killFading`, `poolMask`, `poolBit`, `pidx`, `laws`, `infos`, `groupOf`, `modP`, `modRP`, `ampPrev/ampCur/velGain`, `env[6]`, `envOut[6]`, `lfoCore[10]`, `src[58]`, `oscMono`, `extMono`, `osc[0..2]`, `sub`, `filt[0..1]`), followed by infrequent/readout fields (`lib[0..2]`, `noiseSv`, `envLen`, `cold`).
  2. In `core/engine-own/voice/voice.cpp:144-166` and cold member sites:
     - Forward `sharedMorphScratch` and `maxUnison` to `osc[o].prepareMorph(sharedMorphScratch, maxUnison)` and `(sampleRate, maxUnison, eagerLibOsc)` to `lib[o].prepare(...)`.
     - Access moved cold fields via `cold->...`.
- **Test (in `core/tests/test_own_voice_opt.cpp`):**
  - Signal: Existing `VoiceOptTest` random-patch lockstep test (old vs new voice state) + new `own_voice_layout_and_footprint` test checking `sizeof(Voice)` and `Voice::prepare(44100.0, &sharedScratch, 4, false)`.
  - Measured quantity: `sizeof(Voice)`, `Voice::osc[0].morphOwnedBytes()`, `Voice::lib[0].subEngineBytes()`, and `VOICE-LAYOUT-CHECKSUM`.
  - Threshold: `sizeof(Voice) <= 24,576 B` (64-bit) / `<= 22,528 B` (32-bit; before: `33,128 B` on 32-bit, `~34,500 B` on 64-bit; after: `~19,800 B` on 32-bit / `~21,500 B` on 64-bit); `lib[0].subEngineBytes() == 0` when `eagerLibOsc == false`; `VoiceOptTest::diff == 0` on all steps.
- **Sound bit-identical:** YES (`VOICE-LAYOUT-CHECKSUM`).

---

### Batch 4 (1 task — depends on Batch 3)

#### Task T9 — `OwnEngine` Tier-Scaled Voice Pool, Shared `MorphScratch`/`FxInst0Table`, and Lock-Free Off-Thread Resize (Part 3 Rank 1)
- **Depends on:** T7, T8.
- **Editable files (3):** `core/engine-own/own_engine.h`, `core/engine-own/own_engine.cpp`, `core/tests/test_rt_alloc.cpp`.
- **What to change:**
  1. In `core/engine-own/own_engine.h`:
     - Add `std::unique_ptr<MorphScratch> morphScratch;` (allocated once in `OwnEngine::OwnEngine()` next to `spectralScratch` at `own_engine.cpp:146`).
     - Add `FxInst0Table fxInst0;` (shared across `fx[0..2]` via `fx[i].setSharedInst0(&fxInst0)`).
     - Replace `std::vector<std::unique_ptr<Voice>> voices;` and `std::vector<uint8_t> sustainPending;` with:
       - `std::array<std::atomic<Voice*>, kPoolVoices> voiceSlots {};` (`static_assert(std::atomic<Voice*>::is_always_lock_free)`)
       - `std::array<uint8_t, kPoolVoices> sustainPending {};`
       - `std::atomic<int> preparedVoices { 0 };`, `std::atomic<int> preparedUnison { 0 };`
       - `std::mutex poolBuildMutex;` + `std::vector<std::unique_ptr<Voice>> voiceOwned;` (freed only in `prepare()` / `~OwnEngine()`).
       - Atomic resize/lazy requests serviced by existing `fxWorker` (`sb-fx-build`): `std::atomic<int> wantPoolVoices { 0 }`, `std::atomic<int> wantPoolUnison { 0 }`, `std::atomic<uint32_t> wantFxMask { 0 }`, `std::atomic<uint32_t> wantLibTypes[3]` (bit `t-1` for `oscN.type == t`), `int oscTypeIndex[3] = { -1, -1, -1 }`.
       - Helper `void servicePoolWork() noexcept;` (acquires `poolBuildMutex`, grows `voiceSlots` up to `wantPoolVoices`, calls `fxInst0.ensurePrepared(m)` for unmasked bits in `wantFxMask`, calls `prepareLibType(o, t, capU)` across `0 .. preparedVoices-1` for set bits in `wantLibTypes[o]`; wrapped in `try/catch`).
  2. In `core/engine-own/own_engine.cpp`:
     - Constructor: allocate `morphScratch = std::make_unique<MorphScratch>()`; look up `oscTypeIndex[0..2]` (`osc1.type`, `osc2.type`, `osc3.type`).
     - `OwnEngine::prepare(sr, maxBlock, quality)`:
       - Call `stopFxWorker()` BEFORE clearing/rebuilding `voiceOwned` and `fxInst0`.
       - Compute `initPool = std::clamp(quality.maxVoices, 1, kMaxVoices) + 4` and `initUnison = std::clamp(quality.maxUnison, 1, kMaxUnisonVoices)`.
       - Allocate ONLY `initPool` voices via `v->prepare(sampleRate, morphScratch.get(), initUnison, /*eagerLibOsc=*/false)`, `v->attachPool(&activeMask, i)`, publish into `voiceSlots[0 .. initPool-1]`, set `voiceSlots[initPool .. kPoolVoices-1] = nullptr`, store `preparedVoices = initPool`, `preparedUnison = initUnison`.
       - Compute `initFxMask = effectiveFxMask(quality.fxMask, readParam(paramTable()[fxReverbForceIndex].id) >= 0.5f)` and call `fxInst0.prepare(sampleRate, maxBlockFrames, initFxMask)`.
       - Pass `fx[i].setSharedInst0(&fxInst0)` before `fx[i].prepare(sampleRate, maxBlockFrames)`.
       - Synchronously prepare any `osc1..3.type` already `> 0` (`servicePoolWork()`).
       - Start `fxWorker` when `realtimeFlag == true`; inside `fxWorker` loop (`own_engine.cpp:175-182`), call `servicePoolWork()` right alongside `fxPool.typeChanged(...)`.
     - `OwnEngine::setQuality(q)`:
       - Store existing atomics. Compute `needPool = std::clamp(q.maxVoices, 1, kMaxVoices) + 4`, `needUnison = std::clamp(q.maxUnison, 1, kMaxUnisonVoices)`.
       - Atomically raise `wantPoolVoices` (monotonic max), `wantPoolUnison` (monotonic max), `wantFxMask |= q.fxMask`.
       - If any target exceeds prepared state: in `realtimeFlag` mode set `fxWorkDue = true; fxWake.notify();`; in offline mode (`!realtimeFlag`) call `servicePoolWork()` synchronously!
     - `OwnEngine::setParam(id, plain)`:
       - If `i == fxReverbForceIndex && v >= 0.5f`: set `wantFxMask |= kFxReverb`; dispatch to `fxWorker` (if live) or `servicePoolWork()` (if offline).
       - If `i == oscTypeIndex[o]`: `int t = int(v + 0.5f)`; if `t >= 1 && t <= 4`, set `wantLibTypes[o] |= (1u << (t - 1))`; dispatch to `fxWorker` (if live) or `servicePoolWork()` (if offline).
     - Audio thread (`freeVoice`, `pickVictim`, `allocateVoice`, `forEachActive`, `hasVoice`, `noteOff`, `setSustain`, `controlTick`, `process`, `writeReadout`, `publishStats`, `reset`, `silence`):
       - Iterate `const int poolN = preparedVoices.load(std::memory_order_acquire);` and load `Voice* v = voiceSlots[i].load(std::memory_order_acquire); if (v == nullptr) continue;` — zero vector access, zero lock, zero allocation.
- **Test (in `core/tests/test_rt_alloc.cpp`):**
  - Signal: Add `TEST(process_does_not_allocate_during_tier_and_fx_growth)`:
    1. Prepare `OwnEngine` at `Tier::low` (`44100 Hz`, `128` frames, both `realtime = false` and `realtime = true`), play 8 notes.
    2. Raise quality dynamically via `setQuality(qualityForTier(Tier::high))` (8 → 32 voices, unmasking all FX), toggle `reverb.force_enable = 1`, cycle `osc1.type` across `0..4`, strike 24 notes, render 200 blocks.
    3. Measure `g_allocations` during every `process()` call AND verify finite audio, `activeVoices() > 8` once grown (in offline mode immediately on the next block; in live mode within 50 ms wait), and `OWN-ENGINE-TIER-POOL-CHECKSUM` matches a high-tier-prepared engine when run offline.
  - Threshold: `0` allocations in `process()` (must fail before if checking `preparedVoices == 12` after `low` prepare and `== 36` after `high` `setQuality`; passes after!).
- **Sound bit-identical:** YES for all offline renders (`OWN-ENGINE-TIER-POOL-CHECKSUM`).

---

### Batch 5 (1 task — depends on Batch 4)

#### Task T10 — Strict Memory Footprint Acceptance Gates
- **Depends on:** T1..T9.
- **Editable files (1):** `core/tests/test_own_memory_footprint.cpp`.
- **What to change:**
  1. Tighten `synthyboy_test_own_memory` assertions from informational reporting (T1) to hard acceptance thresholds computed from the verified architecture (with ~15–25% headroom for 64-bit pointer sizes & allocator metadata):
     - `1x low` (Key 61 profile, `44100 Hz`, WT default): `bytes <= 12,000,000` (computed 32-bit: `7.90 MB`, before: `80.67 MB`), `allocs <= 450` (computed: `~200`, before: `~4,978`).
     - `1x low` with `osc1.type = Spectral` + `reverb.force_enable = 1`: `bytes <= 16,500,000` (computed 32-bit: `12.53 MB`, before: `80.67 MB`).
     - `4x low` (4 instances, `44100 Hz`, WT default): total engine `bytes <= 44,000,000` (computed 32-bit: `31.60 MB`, before: `322.68 MB`).
     - `1x xl` (`high,voices=16`, `fx=all`, WT): `bytes <= 20,000,000` (computed 32-bit: `15.50 MB`, before: `80.67 MB`).
     - `1x high` (`32` voices, `fx=all`, WT): `bytes <= 28,000,000` (computed 32-bit: `22.38 MB`, before: `80.67 MB`).
  2. Add a multi-feature checksum test (`MEM-PLAN-FINAL-CHECKSUM`) rendering Wavetable+Morph, Granular, Spectral, Multisample, Serum Diffusor/Comb/Reverb filter, and Reverb type `0..5` to lock audio determinism.
- **Test:** Fails on today's code (`80.67 MB > 12 MB`, `4,978 > 450` allocs); passes after T1..T9.
- **Sound bit-identical:** YES (`MEM-PLAN-FINAL-CHECKSUM`).

---

## 5. Whole-Item Acceptance Criteria

### 5.1 Computed & Measured Targets (44.1 kHz, 128-frame blocks)

| Metric | Baseline (today) | Target (after T1..T10) | Verified by |
|---|---|---|---|
| **Key 61 (`low`, 1 inst, WT) engine heap** | `80.67 MB` (`76.93 MiB`) | **`<= 12.0 MB`** (computed 32-bit: `7.90 MB`, **−90%**) | `synthyboy_test_own_memory` |
| **Key 61 (`low`, 1 inst, Spectral + forced Reverb)** | `80.67 MB` | **`<= 16.5 MB`** (computed 32-bit: `12.53 MB`, **−84%**) | `synthyboy_test_own_memory` |
| **Key 61 (`low`, 4 inst + full `33.1 MB` WT bank)** | `355.8 MB` | **`<= 77.1 MB`** (computed 32-bit: `64.7–73.8 MB`, **−79–82%**) | `synthyboy_test_own_memory` + Key 61 `VmRSS` |
| **MPC XL (`high,voices=16`, 1 inst, WT)** | `80.67 MB` | **`<= 20.0 MB`** (computed 32-bit: `15.50 MB`, **−81%**) | `synthyboy_test_own_memory` |
| **Desktop (`high`, 1 inst, WT)** | `80.67 MB` | **`<= 28.0 MB`** (computed 32-bit: `22.38 MB`, **−72%**) | `synthyboy_test_own_memory` |
| **Heap allocations per `prepare()` (`low`)** | `~4,978` | **`<= 450`** (computed: `~200`, **−96%**) | `synthyboy_test_own_memory` |
| **Built-in wavetable bank (shared once/process)** | `33,099,744 B` (`33.10 MB` / `31.57 MiB`) | Exact `33,099,744 B` documented & asserted | `synthyboy_test_own_osc_types` |
| **Key 61 `engine_prepare` (`voices + fx`)** | `169.2 ms` (`95.6 + 73.6`, `docs/MPC.md:327`) | **`<= 45 ms`** (`HYPOTHESIS`: computed `~18–35 ms`) | MPC startup log + `prepareDetailMs(0/1)` |
| **Key 61 CPU per block (`tier_bench --config low`)** | Round 13 baseline (`docs/MPC.md:350`: mean median `23.45%`, mean p99 `48.96%`, SCHED_FIFO p99 `39.30%`) | **No regression** (`<= +0.5%` noise band on every preset; `HYPOTHESIS`: −0.5 to −3.0% on morph/heavy presets from shared `MorphScratch` + compact `Voice`) | Interleaved A/B `synthyboy_tier_bench` on Key 61 core 3 |
| **Audio bit-identity & aliasing** | 960 baseline `.hash` renders (`docs/MPC.md:342`); alias bounds (`docs/QUALITY.md:33-47`) | **0 of 960 renders differ** (`scripts/compare-renders.sh`); aliasing **unchanged** (`0 dB` delta) | `synthyboy_preset_render --float32 --hash` + `scripts/compare-renders.sh` |
| **RT safety & concurrency** | 0 allocs in `process()`; TSan & ASan/UBSan clean | **0 allocs** in `process()` (including live tier/FX/osc-type growth); **0 TSan/ASan/UBSan findings**; `shell-mpc/accept.sh` `ACCEPT OK` | `synthyboy_test_rt`, `synthyboy_test_threads`, ASan/UBSan/TSan CI |

### 5.2 Device Checks (MPC Key 61 — armv7 only during dev per `AGENTS.md:8`)

1. Build arm32 package (`scripts/build-mpc.sh --arch arm32`) and static arm32 `synthyboy_tier_bench` (`scripts/build-tier-bench.sh --arch arm32`).
2. **Startup time & single-instance RSS:** Load SynthyBoy on Track 1 of `Sbtest` on Key 61; read startup status lines (`prepare_voices`, `prepare_fx`, `open_ms_total`) and `/proc/<mpc-pid>/status` `VmRSS` before vs after loading the plugin (`VmRSS` delta must drop from `~85–115 MB` to `<= 25 MB` with `lazyTables = true`).
3. **Multi-instance check (2–4 tracks):** Load 4 SynthyBoy instances on 4 tracks (1 Wavetable, 1 Granular, 1 Spectral, 1 Multisample + `reverb.force_enable = 1` on one track); confirm `VmRSS` increase across instances 2–4 is `<= 15 MB/instance` (no OOM, no audio crackle).
4. **Live transitions without drop-outs:** While holding a 4-note chord on Key 61 with `SYNTHYBOY_MPC_CALLBACK_STATS`:
   - Toggle `reverb.force_enable` `0 → 1 → 0` (verify Reverb fades in cleanly within ~20 ms, callback gap `< 1.5×` block time).
   - Switch `osc1.type` `Wavetable → Granular → Spectral → Multisample → Wavetable` (verify no drop-out, no crash).
   - Switch `filter1.model` across `Diffusor`, `Combs`, `Reverb`, `Comb2` and `reverb.type` `0..5` (verify no stale tail bleed and no audio glitches).
   - Check `ps -eLo tid,psr,cls,rtprio,comm | grep sb-fx` confirms `sb-fx-build` runs in `TS` class off isolated audio cores 2–3.
5. **Interleaved A/B `tier_bench` on Key 61 core 3** (baseline binary vs new binary) + `tier_bench --verify --tolerance 3e-5` (`0 of 15 loads differ`) + PMU L1D/L2D miss check if kernel supports `perf_event_open`.

---

## 6. Risks & Fallback / Mitigation Plan

| Risk | Likelihood | Impact | Mitigation / What to do if it fails |
|---|---|---|---|
| **R1: ThreadSanitizer race during concurrent `setParam`/`setQuality` + `process()` (`test_threads.cpp`)** | Medium | High (CI failure or UB) | Strictly obey single-direction atomic publication: writer populates object completely before `store(ptr, memory_order_release)`; audio thread uses `load(memory_order_acquire)` and never frees; `voiceOwned`/`fxOwned`/`libOwned` are cleared only in `prepare()`/`~OwnEngine()`. If TSan flags `activeMask` during `attachPool()`, defer `attachPool()` to the audio thread on first claim of `voiceSlots[i]`. |
| **R2: Reverb core-slice zeroing (`2.23 MB` for FDN16) causes a single-block spike when user clicks `reverb.type`** | Low | Low (one-shot UI click, already half of today's `4.87 MB` `Reverb::reset()`) | Verify on Key 61 callback stats while toggling `reverb.type`. If a spike exceeds `1.5×` block time, switch `DelayLine` to a watermarked lazy-zero counter (`validSamples = 0`, returning `0` when `d > validSamples`) or clear off-thread on `sb-fx-build` and swap a fresh `Reverb` via `FxInst0Table`. |
| **R3: Flash worker breaks `VoiceOptTest::diff` or standalone `Voice`/`WtOsc`/`OscCycle` callers** | Medium | Medium (compile fail at build worker) | All new `prepare()`/`prepareMorph()` parameters have backwards-compatible defaults (`nullptr`, `kMaxVoices`, `true`), and T8 keeps every field inspected by `VoiceOptTest::diff` (`test_own_voice_opt.cpp:44-84`) directly on `Voice`. If a batch fails to compile, revert only the failing task's commit pair; earlier batches remain green. |
| **R4: Live `oscN.type` switch or `reverb.force_enable` sounds silent for ~1–15 ms while `sb-fx-build` allocates** | Low | Low (same UX as existing `FxPool` at `own_engine.cpp:275` and `lazyTables` at `docs/MPC.md:327`) | In offline mode (`realtime == false`), allocation is synchronous so tests and renders are 100% deterministic. In live mode, `MPC` preset load can optionally call `IEngine::readyContentFor()` (already called on program change!) extended to pre-warm `osc1..3.type` and `reverb.force_enable` before parameters apply. |
| **R5: `Voice` hot/cold split (`T8`) shows zero or slightly negative CPU delta on Key 61** | Medium (`HYPOTHESIS`) | Low (RAM still drops 90%) | Keep the `VoiceCold` split ONLY if A/B `tier_bench` is neutral or faster (within noise). If pointer indirection `cold->` regresses median CPU by `> 0.5%` on `Lorenz Garden`, keep `VoiceCold` inline at the tail of `Voice` (no extra dereference) while retaining T2, T3, T5, T6, T7, T9 (which deliver >99% of the memory reduction). |
| **R6: `setQuality` raises `maxUnison` from `3..4` → `16` after `WtOsc::history` was sized to `4`** | Low (tests only; MPC profile is fixed) | Medium (OOB if unclamped) | `WtOsc::render()` clamps oversampled voices to `historyVoices`, and `servicePoolWork()` can grow `history` only via atomic pointer swap (or keep `WtOsc::history` at `kMaxVoices` = `8.1 KB` if dynamic `maxUnison` increase without swap is hit — `12 × 3 × 6 KB = 216 KB` total difference on Key 61, negligible!). Prefer fixed `kMaxVoices` or atomic history pointer if `test_own_unison_cap` raises unison live on a low-prepared engine. |

---

## 7. Public References Used

1. J.-M. Jot and A. Chaigne, *"Digital Delay Networks for Designing Artificial Reverberators"*, 90th Audio Engineering Society (AES) Convention, Preprint 3030, 1991.
2. J. Dattorro, *"Effect Design, Part 1: Reverberator and Other Filters"*, Journal of the Audio Engineering Society (JAES), Vol. 45, No. 9, pp. 660–684, 1997.
3. M. R. Schroeder, *"Natural Sounding Artificial Reverberation"*, Journal of the Audio Engineering Society (JAES), Vol. 10, No. 3, pp. 219–223, 1962.
4. R. Bencina, *"Real-time Audio Programming 101: Time Waits for Nothing"*, Ross Bencina's Audio & Music Engineering Notes, 2011 (wait-free audio-thread rules and off-thread allocation hand-over).
5. M. M. Michael, *"Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects"*, IEEE Transactions on Parallel and Distributed Systems, Vol. 15, No. 6, pp. 491–504, 2004 (lock-free pointer publication and safe non-RT reclamation).
6. U. Drepper, *"What Every Programmer Should Know About Memory"*, Red Hat, 2007 (L1D/L2 cache line packing, hot/cold struct splitting, and TLB working-set effects).

---

Clean-room attestation: Nothing under `core/engine-vital/`, `core/vital-import/`, or `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/` was opened, read, grepped, or listed while preparing this plan.
