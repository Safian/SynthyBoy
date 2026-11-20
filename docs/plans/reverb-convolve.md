# Plan: reverb (six algorithms) and Convolve - CPU, memory and quality (and Hyper / Dimension)

Status of this document: NOT FINISHED - sections 1 and 2 are written and committed; sections 3 to 8 follow (each is committed when it is complete).

Written on branch `worktree-agent-ad6ebad4a87a4235d`, base `develop` 6069a30f (2026-11-20). Reading and reasoning only: no build, no test, no benchmark was run. Arithmetic that is not in a repository
document was done with throw-away Python scripts (not committed), only to check sums and flop counts.

Covers: `docs/review/OWN-ENGINE-DSP-AUDIT-2-EFFECTS-OUTPUT-2026-11-20.md` part 2 rank 5 (Reverb), rank 4 (Convolve), rank 12 (Hyper / Dimension), each corrected by the matching section of
`docs/review/OWN-ENGINE-DSP-AUDIT-2-VERIFICATION-2026-11-20.md` (the verification wins where they differ; where I re-read the code myself and the verification is wrong in a detail, section 1.5 says so).
Open items: `TODO.md:58` (this plan is the one listed as missing: "zengeto es konvolucio"), `TODO.md:85` (rank 4), `TODO.md:86` (rank 5), `TODO.md:91` (rank 12). Re-check rule after sound changes: `TODO.md:60`.

Conventions. `file:line` = a line I opened at 6069a30f. "(verif.)" = taken from the verification document, not re-opened by me. "HYPOTHESIS" = not measured and not derivable from the code; the measurement that decides it is named.
Cycle weights used in cost models are HYPOTHESIS until section 2's numbers exist.

## 0. Scope, neighbours, ground rules

* In scope: `core/engine-own/fx/fx_reverb.h` (+ `fx_dsp.h` helpers), the Convolve library `core/fx-serum-b/{convolve,fft}.{h,cpp}` and its module `ConvolveModule` in `core/engine-own/fx/fx_modules.cpp`, and (rank 12) `core/fx-serum-a/src/hyper_dimension.cpp` + header.
* Other plans that already exist and own part of the same code (`docs/plans/`). This plan does not repeat them; it orders itself after them where they touch the same file:
  * `memory-footprint.md` task T3 = "Reverb unified delay arena" (`fx_dsp.h`, `fx_reverb.h`, `test_own_fx.cpp`): the MEMORY half of rank 5 (4,874,240 B -> 2,359,296 B at 44.1 kHz, 93 allocations -> 1). **This plan does not allocate or bind reverb buffers.** Its reverb tasks start after T3 is merged (same file).
  * `rt-threading.md` task T2 = `ConvolveModule` failed-submit reset and the `live.tone` snapshot (`job.tone`) in `fx_modules.cpp`. The data race on `live.tone` is fixed there, first; section 3.5 shows why the race disappears for good in this plan's design, and task C6/C8 remove the now dead snapshot.
  * `perf-measurement.md` T17 (`tier_bench --modules`) is an optional second source for module costs; this plan builds its own small module bench (task T01) because it must time `Reverb` and `Convolve` in isolation and against a frozen copy of the old code.
* Audio-thread rules for everything planned: no allocation, no lock, no system call, output independent of the block split (the effects are called with at most one control period of 32 samples at a time - `ConvolveModule::run` copies `n` samples into `float wl[kControlPeriod]`, `fx_modules.cpp:877-879`; every planned change keeps its state advance per sample or per chunk exactly as now).
* The specified behaviour is kept (`docs/engine-spec/06-effects.md` sections 9.1 to 9.5: structure, delay tables, gains, `output_scale`, the loop gain law, the six types' RT60 and level vectors). Where a change moves the output by rounding only, the bound is computed (section 3.6).

## 1. The problem, as verified

### 1.1 Who runs these effects, and where (decides what matters)

* Tier `low` (Akai MPC Key 61) masks Reverb, Flanger, Hyper / Dimension, Bode and Convolve: `core/include/synthyboy/quality.h:112` (`kFxAll & ~(kFxReverb | kFxFlanger | kFxHyperDim | kFxBode | kFxConvolve)`); `reverb.force_enable` unmasks the reverb only (`docs/QUALITY.md:125`, `docs/MPC.md:468`; verif. for the code path `own_engine.cpp:1474`). `docs/MPC.md:495`: the XL profile runs all of them. So **on the Key 61 the CPU of these effects is zero today**; their CPU matters on the XL (ARM64), iOS and desktop, and becomes a Key 61 question only if the owner un-masks the reverb. What does matter on the Key 61 today is memory (they are still allocated, `docs/plans/memory-footprint.md` item 6) and the prepare time (`prepare_fx = 73.6 ms`, `docs/MPC.md:327`).
* Use in the 160 factory presets (`grep -o` over `content/presets`, counted by me): `reverb.on` = 1 in 120, 0 in 40. `reverb.type` (stored in all 160, also where the reverb is off): 0 in 131, 1 in 11, 2 in 11, 3 in 1, 4 in 4, 5 in 2. `reverb.chorus_amount` = 20 in all 160; `reverb.predelay` = 0 in all 160; `reverb.size` = 50 in 150. `convolve.on` = 1 in 9 presets, all with `convolve.ir` 1, `convolve.size` 50, `convolve.decay` 100, `phi_min` 0 (e.g. "Reverse Gong Swell", "Frozen Pluck Drone"). `hyperdim.on` = 1 in 2 presets (`hyperdim.unison` 4 in 159, 5 in 1; `dim_mix` 0 in 158). Consequences: type 0 (the 16-line network) is the one that sounds in production, so its tasks come with the most weight; `chorus_amount` 0 never occurs, so "skip the LFO when depth is 0" would save nothing in the shipped content; the pre-delay branch (`fx_reverb.h:205`) is never taken in the content.
* The factory impulse responses (`content/factory-samples/impulses/*.wav`, header bytes 20..35 read: 2 channels, 44,100 Hz, block align 6 = 24 bit): `cabinet_colour.wav` 23,858 B, `plate.wav` 383,714 B, `small_room.wav` 185,264 B. With the canonical 44-byte header (HYPOTHESIS: the loader reads the real value) that is 3,969 / 63,945 / 30,870 frames = 0.09 / 1.45 / 0.70 s. So the real working point of Convolve is 0.1 to 1.5 s; the 524,288-frame cap (`convolve.h:36`, 11.9 s at 44.1 kHz) is the stress case of the audit, not the typical one.

### 1.2 Reverb (rank 5)

What the code does, per `Reverb::process` call (at most 32 samples; `fx_reverb.h:161-209`):

* Per call (setup, `fx_reverb.h:163-178`): `MixGains` (cos + sin), `TrapLp::coefficient` x2 (`std::tan`, `fx_dsp.h:151-155`), `OnePole::coefficient` x2 (`std::exp`, `fx_dsp.h:171`), then `prepareFdn()` (type 0, `:214-231`: 16 `std::pow` + `exp2` + 2 `pow` + 2 `exp`) or `prepareCore()` (types 1..5, `:285-328` and `coreNetTargets` `:330-364`: `exp2`, `pow`, and 8 `gainFor` = 8 `exp` for the 8-line types). About 25 libm calls per 32 samples = 0.8 per sample, recomputed even when no parameter moved.
* Per sample, by type (counted from the lines named):

| type | algorithm | libm per sample | divisions per sample | interpolated reads per sample | other |
|------|-----------|-----------------|----------------------|-------------------------------|-------|
| 0 | 16-line network `fdnSample` `:232-275` | 16 trig (`:243`: 8 `cos` + 8 `sin`; group 1 = -group 0, group 3 = -group 2) + `floor` (`:237`) | 1 (`:236`) | 16 `readLagrange4` (`:245`) + 16 `readCubic` on `kA[k]*scale - 1`, an exact integer (`:246`) | 16 lines x 2 shelf one-poles, Householder-type mix `:251-263` |
| 1 | plate `plateSample` `:435-471` | 1 `exp` (`gainFor`, `:446`) + 2 `sin` (`:452`) | 6 (`sc` `:438`, `p0`, `p1` `:445`, `gainFor` `:283`, phase `:380`; `S()` multiplies) | 26 (`plIn` 4, `plModAp` 2, `plDel1` 2, `plAp2` 2, `plDel2` 2, 14 output taps `:465-468`) + 12 early reflections = **38** (verif. rank 5 (c).2; the audit said 33) | 2 `DampStage` |
| 2, 4, 5 | 8-line network `netSample` `:393-433` | 8 `sin` (`:404`) | 1 (phase) | 8 lines + 8 `netAp` + 4 `inAp` = 20, + 12 ER = **32** | the 8x8 Walsh-Hadamard as a 64-iteration double loop with bit tricks `:415-421` (64 `& >> +` chains, 64 float adds); 8 `DampStage` |
| 3 | Freeverb-style `vintageSample` `:473-502` | **16 `sin`** (`:487`) + **16 `exp`** (`gainFor`, `:489`) + **2 `exp`** (`OnePole::coefficient(7000, sr)`, `:496`) = 34 | **36**: 16 (`len / sr`, `:489`) + 16 (`/ T` inside `gainFor`, `:283`) + 2 (inside `coefficient`) + 1 (`ratio`, `:478`) + 1 (phase) | 26 (2 x (1 + 8 + 4)) + 12 ER = **38** | 16 `DampStage` |

  ER = the six-tap early reflections of every core type (`coreSample` `:370-378`: 12 `readCubic` with positions recomputed per sample from `kL[t] * 0.001f * sr * cfg.erScale`). `gainFor` (`:283`) depends only on the loop time and `T`, so its 16 calls per sample in `vintageSample` return the same 16 values every sample of a chunk.
* Memory (`prepare`, `:76-117`; floats; every `DelayLine` is rounded up to a power of two with `maxSamples + 4`, `fx_dsp.h:19-27`; I recomputed each line with the same rounding, 44.1 kHz / 48 kHz): type 0 lines + all-passes 557,056 / 589,824; types 2/4/5 network + all-passes + input all-passes 274,432 / 286,720; plate 196,608 / 196,608; vintage 149,504 / 149,504; the early-reflection line shared by types 1..5 8,192 / 16,384; the pre-delay pair shared by all 32,768 / 32,768. Sum **1,218,560 floats = 4,874,240 B at 44.1 kHz and 1,271,808 floats = 5,087,232 B at 48 kHz** - identical to the verification (rank 5 (d)). The largest single algorithm (type 0 plus the pre-delay pair) is 589,824 floats = 2,359,296 B at 44.1 kHz (2,490,368 B at 48 kHz), so the arena of `memory-footprint.md` T3 saves 52 %. Each of the three racks owns one such object, and each Reverb slot of the effect pool adds another when built (`docs/review/OWN-ENGINE-DSP-AUDIT-2-EFFECTS-OUTPUT-2026-11-20.md:86`).
* Nitrous "Space" damping: `fx_reverb.h:294-300`: `ahRaw = 10^(clamp(highShelfDb, -24, 0)/20)`, `ah = 1 - (1 - ahRaw) * k_room`, `k_room[Space] = 1.45` (`:295`). `ah < 0` needs `ahRaw < 1 - 1/1.45 = 0.310345`, i.e. `highShelfDb < 20 log10(0.310345) = -10.16 dB` (the audit said -9.9). Values: -6 dB -> `ahRaw` 0.501187 -> `ah` +0.276721; -10.16 dB -> 0; -12 dB -> 0.251189 -> -0.085777; -24 dB -> 0.063096 -> **-0.358511**. The engine key `reverb.high_shelf_gain` has the range -6 .. 0 dB (`core/src/param_table.cpp:699`, my read; the verification says `modulatedValue` clamps to it), so **through the engine the negative gain cannot occur** (lowest value +0.2767). It exists only for a direct `Reverb::set` (tests, tools): a latent defect, not a sound defect. The spec (`06-effects.md:257`) requires the gain clamp -24 .. 0 dB for the shelf and says nothing about `ah`.

### 1.3 Convolve (rank 4)

Structure (`core/fx-serum-b/convolve.cpp`, `convolve.h`): a 128-tap time-domain head (`:603-607`) plus uniform partitions of 128 samples, FFT size 256 (`convolve.h:34-36`), `P = ceil((F - 128) / 128)` tail partitions for an impulse of `F` frames (`:444-445`); every 128 samples `computeTailFftBlock` (`:478-527`) multiplies the input-spectrum history with all `P` partition spectra for both channels (`:487-494`, `:510-517`, complex `Complex * Complex` on 256 bins, `histIdx` with `% m_numTailPartitions` per partition) and runs a full 256-point complex IFFT. The real input is zero-padded into a complex buffer (`Radix2FFT::forwardReal`, `fft.cpp` forwardReal, my read of `:53-120`): a real transform with 256 complex bins, of which only 129 are independent. `ConvolveModule` (`fx_modules.cpp:725-1094`) builds a **complete new `Convolve` object on the helper thread** for every change of a key that shapes the impulse (`runJob` `:996-1021`) and takes it over in `takeOver` (`:973-985`): `active = fresh`; the old convolver is dropped, the new one starts with zeroed history (`Convolve::loadIr` calls `rebuildPartitions` + `reset`, `convolve.cpp:132-148`).

Costs, computed from the code:

* Complex multiply-adds per sample = `2 channels x P x 256 / 128 = 4P`. `F = 63,945` (plate.wav): `P = ceil(63,817 / 128) = 499` -> **1,996 CMAC/sample** = 15,968 real flops (a CMAC = 4 mul + 4 add), plus the 256-point FFT+IFFT pair per channel per hop (`2 x 2 x 5 x 256 x 8 / 128 = 320` flops/sample) and the 128-tap head (256 MAC/sample = 512 flops): **about 16,800 flops per sample = 7.4e8 flops/s at 44.1 kHz** (HYPOTHESIS: roughly half a Cortex-A17 core at about 1 flop/cycle scalar; the XL figure is unknown - measured by task T01). `F = 524,288`: `P = 4,095`, **16,380 CMAC/sample = 131,040 flops/sample** (audit: ~24,000 float MACs for a 4 s IR at 48 kHz: `P = 1,499`, `4P = 5,996` CMAC = 23,984 real MAC: consistent).
* Memory: tail spectra + input-spectrum history = 4 tables x `P` x 256 bins x 8 B: `F = 524,288` -> **33,546,240 B** (verif. rank 4 (d), recomputed: 4 x 4,095 x 256 x 8); `F = 63,945` -> 4,087,808 B. During a rebuild the helper thread holds a second complete convolver (verif.: 67.09 MB at the cap); `prepare` also allocates 2 x ceil(2.5 sr) floats = 882,000 B of pre-delay (`convolve.cpp:93-97`).
* Swap (`fx_modules.cpp:973-985`): the output switches from the old convolver (state: all history, tail still ringing) to a fresh one (history all zero) in one sample: a click proportional to the wet signal at that instant and a complete loss of the tail (the new convolver answers only to input that comes after the swap, so the wet level re-builds over the IR length). The keys that shape the impulse are modulation destinations (`docs/engine-spec/04-modulation-matrix.md:364-370`: size, attack, decay, damp, ir_gain) and are quantised only so that a smooth modulation "does not rebuild at every period" (`fx_modules.cpp:940-945`): with modulation the module rebuilds again and again, and each rebuild wipes the tail. That is the audible defect (verif. rank 4 (c): `takeOver` "no audio crossfade"; audit module 8 (d): "hard click and tail cut-off").
* Race: the audio thread writes `live.tone` (`fx_modules.cpp:778`, plain float, every control period) and the helper thread reads it in `runJob` (`:1014`): unsynchronised. Fixed first by `rt-threading.md` T2; see 3.5.
* Head loop: 128 taps x 2 channels per sample with descending index `pL[kPartitionSize - 1 - k]` and one sequential accumulator per channel (`:603-607`); a sequential float sum is not vectorised by the compiler, because that would reassociate the additions (not allowed without `-ffast-math` / `-fassociative-math`); so the loop is scalar.
* Minor: `% m_preDelayCapacity` twice per sample when the pre-delay is on (`:660`, `:665`); `reset()` (`:103-130`) uses `assign` on the head/block/overlap vectors (`:105-116`; the verification names the tail history `assign`, but `:118-121` use `std::fill` there) - with unchanged sizes no `assign` reallocates in the three standard libraries in use (HYPOTHESIS: the standard does not promise it); the rewrite uses `std::fill`.

### 1.4 Hyper / Dimension (rank 12)

`hyper_dimension.cpp`: `prepare` allocates 7 + 4 buffers of `next_pow2(int(0.070 sr) + 32)` = 4,096 floats = 16 KiB each at 44.1 and 48 kHz (`:51-61`; `int(0.07 x 44100) + 32 = 3,119`, `x 48000 = 3,392`) = 176 KiB per instance (528 KiB over three racks), all written with the same `monoIn = 0.5 (inL + inR)` (`:144`, `:164`, `:204`). Per sample and per voice `v < unison`: `std::sin(phase)` (`:171`), the pan division `-1 + 2 v / (unison-1)` (`:178`), `std::cos` and `std::sin` of an angle that depends only on `(v, unison)` (`:179-181`), plus the delay constants `vCenter`, `baseDelaySamples`, `maxExcursion` (`:168-170`) and the LFO increment `kTwoPi * lfoFreq * invFs` (`:185`); then, unconditionally, 4 Dimension reads (`:208-211`) and `std::sin` + `std::cos` of `dimLfoPhase_` (`:214-215`) **even when `dimMix` is 0**. At `unison = 4` (159 of 160 presets): `4 x 3 + 2 = 14` libm and 4 divisions per sample; at 7: 23 and 7 (verif. rank 12 (d)). Measured value of the hoist (not of the whole): `docs/release/CPU-REVIEW-FILTER-FX.md:399` (item 15 / F17): 0 % at `low` (masked), 1.8 to 4.4 % with `fx=all`, about 0.4 % on the XL. Only 2 of 160 presets use it. The module's own bypass (`:116-119`) already skips the whole effect when both mixes are 0 and the level is 0 dB.

### 1.5 What in the audit was wrong, already fixed, or only partly true

| claim (audit part 2) | finding | evidence |
|----------------------|---------|----------|
| Nitrous Space gives a negative HF damping gain | true only for `highShelfDb < -10.16 dB`, unreachable through the engine key (range -6..0); minimum reachable `ah` = +0.2767. A latent defect of the class itself. | `fx_reverb.h:294-300`, `param_table.cpp:699`, arithmetic 1.2 |
| 16 `sin`/`cos` per sample in type 0 but "only 4 unique phases" | 16 calls, 8 distinct values (cos and sin of 4 lane phases; group 1 = -group 0 and group 3 = -group 2 in the same argument) | `fx_reverb.h:242-243` |
| Plate: 21 + 12 = 33 `readCubic` | 26 + 12 = 38 | verif. rank 5 (c).2, recount in 1.2 |
| `readCubic` on integer-constant delays in all all-passes and taps; replace by direct ring reads | only `apLine` in type 0 is an exact integer (`kA[k] * scale - 1`, `:246`). The others are float products with `sr` or `sizeScale` (`:342-344`, `:438-439`, `:478`) - constant for a chunk, **fractional**. The right replacement is a per-chunk precomputed fixed-fraction tap (task R6), not a direct read | `fx_reverb.h:246, 342-344, 438, 478`; verif. rank 5 (c).2 |
| "skip the early reflections when `erLevel == 0`" | `erLevel` is never 0: `kEr[4] = 0` is overridden for type 4 by `kErRoom` (min 0.15, `:306-308`), all others >= 0.25; there is no key. Nothing to skip | `fx_reverb.h:306-308` |
| 0 trig "when `depth == 0`" | correct but empty in the content: `chorus_amount` is 20 in all 160 presets | content scan |
| reverb on Key 61 "100 % masked", memory "saves 7.5-12 MB" | 14.6 MB over 3 racks is real RAM on a 2 GB device; the CPU of the effect on `low` is zero today (masked) | `quality.h:112`, `memory-footprint.md` item 6 |
| Convolve memory "33.5 MB" | true only at the 524,288-frame cap; the factory impulses need 0.2 to 4.1 MB | 1.3, 1.1 |
| Convolve `reset()` allocates | `assign` of equal size, not shown to allocate; `m_tailFftHist*` use `std::fill` | `convolve.cpp:103-130` |
| Convolve `live.tone` race | real, fixed in `rt-threading.md` T2 (interim) and structurally by this plan | `fx_modules.cpp:778, 1014` |
| Hyper hoisting "1.8-4.4 %" | on Key 61 with `fx=all` (an unmasked profile); 0.4 % on the XL; 2 of 160 presets use the effect | `CPU-REVIEW-FILTER-FX.md:399` |
| (new, from my reading) Pre-delay in the reverb "zipper", type 0 `size` clicks, 48 kHz RT60 shift of 18 % | these are specified behaviours: pre-delay glide "a few percent of the error per chunk" (`06-effects.md:218`), type 0 has no length glide (only the network cores do, `:262`), the 18 % is documented as "upstream design, not changed" (`:334`). **Not part of this plan; an owner decision** (list in 6.4) | spec lines named |

Not part of this plan, found while reading (recorded so they are not lost): the non-Serum `convertToMinimumPhase` branch uses an FFT of `next_pow2(F)` capped at 65,536 without 2x zero padding (`convolve.cpp:390-392`, verif. extras 2), `m_toneState*` have no denormal flush (`:631-647`), the pre-delay is an integer delay that clicks when modulated (`:581, :660`). The IR build itself (17-tap double-precision windowed-sinc resampling with one `sin` and one `cos` per tap, `convolve.cpp:26-66`: about `34 x F` libm calls, about 2.2 million for plate.wav stretched, 18 million at the cap) runs on the helper thread; its duration is measured by T01 and decides whether a table-driven resampler is worth a task (6.4).

## 2. The measurement to make BEFORE any change

### 2.1 What exists, and what it cannot do

* `synthyboy_tier_bench` (`bench/tier_bench.cpp`): whole engine, 44.1 kHz, 128-frame blocks, per-block time as % of 2.902 ms; options `--presets --core --config --load --seconds --repeat --csv --sections --verify` (`bench/tier_bench.cpp:5-45`). It can load a preset (so "Lush Analog Pad" = type 0 reverb on, "Reverse Gong Swell" = type 1 + convolve on 'plate', "Spectral Chord Morph" = type 2, "Parallel Dusk Pad" = type 3, "Bode Barber Riser" = type 4, "Shepard Ascent" = type 5; checked in the preset files) under `--config "low,fx=all"` (profile grammar `shell-mpc/device_profile.h:8`). It cannot isolate an effect: the voices dominate and the differences of 0.1 to 1 percentage point are below its noise (`docs/plans/perf-measurement.md` sets the noise floor method). Good as the end-to-end confirmation, not as the baseline.
* `synthyboy_bench --engine own` (`bench/bench_engine.cpp`, `docs/PERFORMANCE.md:22-27`): real-time factor of the default patch; no reverb on the Key 61 profile (`PERFORMANCE.md:20`, `:74`). Not useful here.
* `synthyboy_fx_makeup_calibrate` (`bench/fx_makeup_calibrate.cpp`): runs one module (reverb, not convolve, `:5-8`) in control periods on pink noise; no timing.
* Correctness tests that already guard the sound: `core/tests/test_own_fx.cpp` (`fx_reverb_levels_and_tails_of_the_six_types` `:149`, `fx_reverb_vital_rt60_table_r1` `:181`, `fx_reverb_predelay_and_mix_r2` `:198`, `fx_reverb_feedback_scales_the_decay_of_nitrous_and_basin` `:1107`), `core/fx-serum-b/tests/test_convolve.cpp` (unit impulse, delayed impulse, rate conversion, block-size independence at 1e-6, min phase, pre-delay, NaN safety), `core/tests/test_own_convolve_bypass.cpp`, rate invariance (`06-effects.md:334`: convolve 0.28 dB).
* No tool times `Reverb`, `Convolve` or `HyperDimension` on their own, and none times a Convolve rebuild. **So the first task of the plan builds that measurement (T01).**

### 2.2 The measurement tool (task T01 builds it) and what to record

`synthyboy_fx_cost_bench` (new, `bench/fx_cost_bench.cpp`): runs one effect object directly (header-only `Reverb`; `fx::Convolve`; `fx::HyperDimension`) on a fixed stereo program (the pink-noise-like program of `fx_makeup_calibrate`, plus a saw chord), in 32-sample chunks exactly as `FxModule::process` calls them, 44.1 kHz, thread CPU time per chunk, no engine around it. Output lines (stdout, plus `--csv FILE`):

```
FXCOST reverb type=<0..5> sr=44100 ns_per_sample=<median> p99_chunk_ns=<..> pct_of_128_block=<ns_per_sample*128/2902494*100>
FXCOST convolve ir_frames=<3969|30870|63945|262144|524288> sr=44100 ns_per_sample=<..> p99_block_ns=<..> pct_of_128_block=<..> bytes=<memory the object reports>  build_ms=<time of loadIr>
FXCOST hyper unison=<1..7> dim=<0|1> ns_per_sample=<..>
```

Commands (desktop and device; the device build is the same cross build as `tier_bench`, `docs/plans/perf-measurement.md` section 4):

```
build/bench/synthyboy_fx_cost_bench --seconds 10 --repeat 7 --csv /tmp/fxcost-before.csv
# MPC Key 61 / XL, pinned to an isolated audio core as tier_bench is (--core N is the same option):
./synthyboy_fx_cost_bench --core 3 --seconds 10 --repeat 7 --csv fxcost-before-k61.csv
```

Record, per device, and commit them to `docs/plans/reverb-convolve-baseline.tsv` (task V0): the median `ns_per_sample` of each `FXCOST` line (best of `--repeat` for the p99 columns), `pct_of_128_block`, the `bytes` and `build_ms` columns. These are the "before" numbers; every acceptance number in section 5 is stated against them. The model figures of sections 3 and 4 (cycles per operation) are replaced by these as soon as they exist.

What decides the open HYPOTHESES: (a) the cycle weights of libm / division / `readCubic` on Cortex-A17 and on the XL (`ns_per_sample` of types 0, 1, 2, 3 against the op counts of 1.2); (b) the cost of Convolve at the three factory lengths and at the cap on both devices (against the 16,800 / 131,000 flops per sample of 1.3); (c) whether a Convolve rebuild (`build_ms`) is short enough for modulation (HYPOTHESIS: 50 to 300 ms at 63,945 frames stretched on the A17).

### 2.3 End-to-end confirmation (existing tools, once before and once after the whole item)

```
build/bench/synthyboy_tier_bench --presets content/presets --config "low,fx=all" \
   --load "Lush Analog Pad" --load "Reverse Gong Swell" --load "Spectral Chord Morph" --load "Parallel Dusk Pad" \
   --load "Bode Barber Riser" --load "Shepard Ascent" --seconds 10 --repeat 5 --csv /tmp/e2e-before.csv      # record median / p99 % per preset
synthyboy_preset_render --presets content/presets --engine own --tier high \
   --wav /tmp/render-before --chord-wav /tmp/render-before-chord --float32 --hash --content-dir <store>        # for the sound checks of section 5
```

The `Reverse Gong Swell` and `Frozen Pluck Drone` presets are the two with Convolve on ('plate' impulse, 63,945 frames); `Frozen Pluck Drone` has reverb off, so Convolve's share can be read from it alone (`docs/release/CPU-REVIEW-FILTER-FX.md:326-328`, finding F20, measured it at 3.3 points with Convolve masked - a different effect, not this one).

### 2.4 Memory baseline

`memory-footprint.md` task T1 (the memory probe: heap bytes, allocation count, `prepare_fx_ms`, RSS) gives the instance figures; T01's `bytes=` column gives the per-object figures (reverb arena, Convolve tables, hyper buffers). Expected before (computed in 1.2 to 1.4): reverb 4,874,240 B (44.1 kHz), Convolve 4,087,808 B + 882,000 B pre-delay at the plate impulse and 33,546,240 B + 882,000 B at the cap, Hyper 180,224 B + small.
