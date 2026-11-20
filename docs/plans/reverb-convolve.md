# Plan: reverb (six algorithms) and Convolve - CPU, memory and quality (and Hyper / Dimension)

Status of this document: NOT FINISHED - sections 1 to 3 are written and committed; sections 4 to 8 follow (each is committed when it is complete).

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

## 3. Design options and the chosen design

### 3.1 Reverb: three options

**R-A. Keep the six algorithms exactly as specified; remove the redundant work (chosen).** Six changes, each in its own task (section 4), each proven against a frozen copy of today's reverb (task T04):

1. *Per-chunk coefficients.* Everything that depends only on parameters and the sample rate is computed once per `process()` call into the existing `CoreCfg` (`fx_reverb.h:278-282`): for type 3 the 16 loop gains `gainFor(len / sr, T)` (`:489`), the 16 lengths, the 7 kHz coefficient (`:496`), `ratio`, the all-pass delays and coefficients; for type 1 `sc`, `g`, `p0`, `p1`, the `S()` products, the all-pass coefficients (`:438-448`); for the phase `min(chorusRate, 16) / sr` (`:236, :380`, one division per sample today) and `phase -= floor(phase)` replaced by a compare (identical for `phase` in [0, 2); the increment is below 1). *Same expressions, same values: bit-identical.*
2. *Trigonometry shared.* Type 0 computes group 1 and 3 as the negatives of group 0 and 2 (16 libm calls -> 8, bit-identical: same argument). Then all four LFO users take `sin theta` and `cos theta` of the one free-running phase (2 libm calls) and derive every lane / line offset with the angle-addition identity and constant `(cos delta_k, sin delta_k)` pairs (type 0: lanes, `delta = 2 pi lane / 16`; types 2/4/5 and 3: `delta = 2 pi k / 8`, vintage right channel `+ pi`; plate: `0, pi`). libm per sample: type 0 16 -> 2, types 2/4/5 8 -> 2, type 3 16 -> 2, plate 2 -> 2 (no gain). *Not bit-identical*: bound in 3.7.
3. *8x8 Walsh-Hadamard by butterfly* (`:415-421`): 3 stages, 24 add/sub + 8 multiplies instead of 64 iterations of bit tests. Not bit-identical (different addition order), bound 3.7.
4. *Type 0 all-pass read as an integer read* (`:246`): `DelayLine::readInt(int d)` = `buf[(pos - d) & mask]`, which equals `readCubic` at an integer delay `d >= 1` (there `t = 0` and `((c3*0 + c2)*0 + c1)*0 + y1 = y1`, `fx_dsp.h:41-50`). Bit-identical up to the sign of a zero (`-0 + 0 = +0`); the tests normalise zeros by adding `0.0f`.
5. *Fixed-fraction taps.* Every tap whose position is constant for a chunk (the 12 early-reflection taps of types 1..5; plate `plIn`, `plDel1/2`, `plAp2` reads and the 14 output taps; types 2/4/5 `inAp` and `netAp`; type 3 `vInAp`, `vAp`) gets, once per chunk, the integer part and the four Catmull-Rom weights `w0..w3` of its fractional part (the coefficients of `fx_dsp.h:48-49` as functions of `t`); per sample the read is `w0 y0 + w1 y1 + w2 y2 + w3 y3` (4 loads, 4 multiply-adds, no integer/fraction split, no `c1..c3`). 24 (plate), 24 (types 2/4/5) and 22 (type 3) of the 38 / 32 / 38 reads per sample become fixed taps. Not bit-identical (the same cubic as a weighted sum), bound 3.7.
6. *Per-chunk cache and the Nitrous guard.* `process()` recomputes `prepareFdn / prepareCore / MixGains / TrapLp / OnePole` coefficients each call although parameters rarely change: compare the `Params` (a `memcmp` of the struct plus `sampleRate`) and skip the recomputation when equal (about 25 libm per chunk = 0.8 per sample; bit-identical, the same code runs whenever something changed), and `ah = max(ah, 0)` at `:301` (3.3).

Effect on sound: none audible; bit-identical for 1, 4 (zero sign aside), 6 and the group sharing of 2; rounding-level (computed bounds in 3.7) for the rest. CPU, from the cost model of this section (cycle weights HYPOTHESIS: ARMv7 libm 60, division 17, `readCubic` 28, `readLagrange4` 32, fixed tap 12, integer read 3, floor 10; x86 libm 25, division 11, `readCubic` 18, `readLagrange4` 22, fixed tap 8, integer read 2, floor 6; remaining arithmetic counted per line from the code): cost against today's code, cumulative after tasks R1..R7, ARM / x86:

| type | today (cycles/sample, ARM / x86) | after R1 (type 3) | R2 (8-line) | R3 (type 0) | R5 (trig) | R6 (fixed taps) | R7 (cache, phase) |
|------|----------------------------------|-----|-----|-----|-----|-----|-----|
| 0 | 2,889 / 1,999 | 1.00 | 1.00 | 0.70 / 0.77 | 0.57 / 0.70 | 0.57 / 0.70 | **0.55 / 0.68** |
| 1 | 1,719 / 1,200 | 1.00 | 1.00 | 1.00 | 1.00 | 0.78 / 0.80 | **0.75 / 0.78** |
| 2, 4, 5 | 2,755 / 2,145 | 1.00 | 0.78 / 0.72 | 0.78 / 0.72 | 0.65 / 0.65 | 0.51 / 0.54 | **0.49 / 0.52** |
| 3 | 4,728 / 2,938 | 0.65 / 0.72 | 0.65 / 0.72 | 0.65 / 0.72 | 0.47 / 0.60 | 0.39 / 0.52 | **0.38 / 0.51** |

The audit's "2x to 5x" is therefore plausible for type 3 on the ARM and about 1.3x to 2x for the others; the plate gains least (its cost is the 38 reads, of which only 24 can become fixed taps, and its libm count is already low). Memory: unchanged by this plan (the arena is `memory-footprint.md` T3). Real-time safety: unchanged (no allocation; the new per-chunk tables are members). Risk: low; the feedback loops keep their gains <= 0.9996 (`fx_reverb.h:283`) and the orthonormal matrices, so rounding-level differences cannot grow.

**R-B. R-A plus 4-lane NEON kernels for type 0 (4 groups x 4 lines) and types 2/4/5 (8 lines).** The repository has the precedent: the chorus kernel does the arithmetic of `DelayLine::readCubic` lane by lane (`fx_effects.h:353`, with a scalar twin and equality tests). Sound: rounding level. CPU: a further 1.5x to 2.5x on the ARM (HYPOTHESIS, decided by T01 numbers after R7). Risk: a second code path to keep equal on two instruction sets, written by workers that cannot compile. **Not chosen for the first pass; defined as task R8 with a numeric trigger** (section 4): it is executed only if, after R7, the measured cost on the target device is above the acceptance numbers of section 5.

**R-C. Cheaper algorithms (for example 8 or 12 lines instead of 16).** `06-effects.md:236` allows it ("the own engine can use 8 or 12 lines ... if RT60 and density hold"). It changes the sound of the type that 131 of 160 presets store, and the specified vectors (R1..R3) were measured on 16 lines. **Rejected** under the rule "the specified algorithms and numbers must be kept"; it stays the last lever if the Key 61 budget cannot be met (owner decision, 6.4).

Chosen: **R-A**, with R-B as the measured fallback.

### 3.2 Reverb memory

Not designed here: `memory-footprint.md` T3 (one arena of 589,824 floats = 2,359,296 B at 44.1 kHz, `DelayLine`s bound into it, the selected algorithm's slice cleared on a type change, which also removes the stale-tail bleed after a type switch). Dependency: this plan's reverb tasks start after T3, because both edit `fx_reverb.h` and `fx_dsp.h`. A type switch still cuts from one core to the next without a crossfade; the spec is silent (`06-effects.md:246-251`), so it stays an owner decision (6.4).

### 3.3 Nitrous Space damping

Options: (a) `ah = max(ah, 0)` at `fx_reverb.h:301` (chosen: one line, bit-identical for every value the engine can produce since the minimum is +0.2767, and it states the property `06-effects.md:257` describes: Nitrous and Basin "brighten the shelf" by pulling `A_h` toward 1, never past 0); (b) narrow the clamp of `highShelfDb` for Space to -10.16 dB (rejected: it moves the clamp the spec states, -24 .. 0 dB, `:212`, `:257`); (c) do nothing (rejected: any tool or later change of the key range that sends -12 dB to Space inverts the polarity of the high band on every pass: per-pass HF gain magnitude 0.0858 at -12 dB and 0.3585 at -24 dB, so a *stronger* damping setting gives a *louder* high tail). Task R7 carries (a), tested through `Reverb::set` directly.

### 3.4 Convolve: three options

The behaviour to keep (`convolve.h:13-30`): zero latency (`latency() == 0`), exact lags 0..127 in the head, size / attack / decay / damp / phi MIN / IR gain / tone / pre-delay / mix / level, the IR cap of 524,288 frames, audio-thread safety; and the rate invariance of `06-effects.md:334` (convolve 0.28 dB).

**C-A. Keep the uniform 128-sample partitions; make them cheap.** Real-input transforms (129 bins instead of 256: `N/2 + 1`), structure-of-arrays spectra, vectorisable complex multiply-add loops, a ring index without `%`, a head with 8 independent accumulators, a crossfade between two whole convolvers on a swap. Per sample at `F = 63,945`: `2 x 499 x 129 / 128 = 1,006` CMAC = 8,050 flops (+ transforms 160 + head 512) = **about 8,700 flops/sample, 1.9x fewer than today's 16,800 by the packing alone**, and about 65,000 at the cap (2x). Memory half of today (129/256). Sound: rounding level. It still scales linearly with the impulse length: at the 4 s impulse of the audit the cost stays at 12,000 real MACs per sample. Swap: two convolvers, the new one without history (tail loss remains).

**C-B. Non-uniform partitioning (Gardner 1995; Garcia 2002), real transforms, and a crossfade between two whole convolvers.** Cost per sample grows only with `log F` (3.4.6). Swap: the old convolver fades out while a new one (zero history) fades in: the click is gone; **the tail loss is not** (a new convolver needs one impulse length of input before its tail is complete). Memory at a swap: two complete convolvers.

**C-D. Non-uniform partitioning with an input state that survives an impulse change, and a seam crossfade per stage (chosen).** Same cost as C-B. A swap exchanges only the *impulse set* (the partition spectra); the input-spectrum history (the frequency-delay line, FDL) stays, so the new impulse answers at once to everything played before the swap (the right physical result, as if the new impulse had always been there); each stage crossfades its own output at its first block boundary after the swap, so the head reacts in 128 samples and the late tail over its own block length. It needs an impulse set that fits the state's capacity; otherwise (a different impulse file, or growth beyond the capacity) the module takes C-B's whole-convolver crossfade, where the old tail *should* fade out. Task C6 builds that C-B path first; C7 and C8 add the seamless path; the plan ends with C-D.

Why C-D: the work per second is the finding of `TODO.md:85`, and C-A leaves it growing with the impulse; and the keys that shape the impulse are modulation destinations (`04-modulation-matrix.md:364-370`), so rebuilding under modulation is a designed use that loses the tail at every rebuild today and that C-B would still gate. The price of C-D is the transition code (3.4.7), isolated in two tasks that can be dropped (the plan then ends with C-B; risk in section 6) without touching the others.

#### 3.4.1 Partition schedule (constants of the design)

Head: 128 taps in the time domain, both channels (lags 0..127). Three frequency-domain stages, block size `B`, real transform size `2B`, `r = B / 128` base ticks per block (a tick = 128 samples of the module's own sample counter, so the output does not depend on the host block size, the property of `convolve_block_size_independence`, `core/fx-serum-b/tests/test_convolve.cpp:138-189`, checked at 1e-6):

| stage | B | transform 2B | bins B+1 | r | impulse offset `o = 3B` | ticks of slack | work units | stagger `phi` (ticks) |
|-------|---|--------------|----------|---|--------------------------|----------------|------------|-----------------------|
| 1 | 128 | 256 | 129 | 1 | 128 (= B: synchronous, as today) | 0 | all at the block end | 0 |
| 2 | 1,024 | 2,048 | 1,025 | 8 | 3,072 | 16 | 8 (1 + 6 + 1) | 4 |
| 3 | 8,192 | 16,384 | 8,193 | 64 | 24,576 | 128 | 64 (1 + 62 + 1) | 1 |

Rule (Gardner): a partition of block size `B` may start at an impulse offset `o >= B` (its input block is complete at time `(j+1)B`, its first output is needed at `jB + o`); the slack `o - B` is what lets the work be spread. With `o = 3B` the slack is `2B = 2r` ticks; a sequence of `r` units starting `phi <= r` ticks after the block completes ends at `(j+1)B + (phi + r) 128 = (j+2)B + 128 phi <= jB + 3B`. Units per block: unit 0 = forward real transform of the block (both channels) into the FDL; units 1..r-2 = a slice of the partition multiply-accumulate (`acc += X_{j-p} * H_p` for the slice's `p`); unit r-1 = inverse transform and overlap-add into the stage's output ring at times `jB + o + [0, 2B)`. One unit per tick; the stagger puts the stage-3 forward transform on ticks `= 1 (mod 64)`, its inverse on `= 0 (mod 64)`, the stage-2 forward transform on `= 4 (mod 8)` and its inverse on `= 3 (mod 8)`, so the three large transforms never share a tick.

#### 3.4.2 The plan for an impulse of `F` frames (`conv_plan.h`, a pure function, task C2)

```
F <= 128:            head only
128 < F <= 5,120:    n1 = ceil((F - 128) / 128)                          (n1 <= 39)
5,120 < F <= 45,056: n1 = 23, n2 = ceil((F - 3,072) / 1,024)             (n2 <= 41)
F > 45,056:          n1 = 23, n2 = 21, n3 = ceil((F - 24,576) / 8,192)   (n3 = 61 at F = 524,288)
```

`n1 = 23` covers impulse samples [128, 3,072), `n2 = 21` covers [3,072, 24,576). The two thresholds are the break-even points of the cost model: extending stage 1 by `D` samples costs `16 D / 128` flops per sample, extending stage 2 costs `16 D / 1,024`, opening stage 2 costs about 220 flops per sample of transforms (`2 channels x 2 transforms x 2.5 N log2 N / B` with `N = 2,048`) -> break-even `D = 2,011`, taken as 2,048, so 3,072 + 2,048 = 5,120; opening stage 3 costs about 280 (`N = 16,384`) -> `D = 20,480`, so 24,576 + 20,480 = 45,056. (A real transform of `N` points about `2.5 N log2 N` flops, a CMAC 8 flops: both HYPOTHESIS as to the cost on the device, which T01 measures; the constants are then adjusted in one place.) Examples: `F` = 3,969 -> (31, 0, 0); 30,870 -> (23, 28, 0); 63,945 -> (23, 21, 5); 524,288 -> (23, 21, 61).

#### 3.4.3 Real transforms and data layout

`RealFft` (task C1, header-only on top of the existing `Radix2FFT`, `fft.h:36-60`): a real transform of `N` points as a complex transform of `N/2` points plus the standard untangling pass (`X[k] = (Z[k] + conj Z[N/2-k]) / 2 - i e^{-2 pi i k / N} (Z[k] - conj Z[N/2-k]) / 2`), output `N/2 + 1` bins; the inverse is the mirror image. Spectra are stored structure-of-arrays (`re[bins]`, `im[bins]`, 16-byte aligned), so the multiply-accumulate loop `accRe += xr*hr - xi*hi; accIm += xr*hi + xi*hr` has no reduction and the compiler vectorises it without `-ffast-math` (lane-wise arithmetic, no reassociation). Flops of a real transform against today's full complex transform of the zero-padded input: `(log2 N - 1) / (2 log2 N)` = 0.44 (N = 256), 0.45 (2,048), 0.46 (16,384) - 2.2x fewer.

#### 3.4.4 The head

Taps stored in ascending lag order and the input history in a doubled ring, so the dot product runs over two ascending contiguous arrays; 8 independent accumulators per channel (two 4-lane vectors) summed at the end, which the compiler vectorises because each lane keeps its own order. 128 x 2 MAC per sample as now, 4-wide on NEON and SSE. Not bit-identical to the sequential sum (rounding level, 3.7).

#### 3.4.5 Objects: what is allocated where

* `IrSet` (immutable once published; built on the helper thread by a new `Convolve::buildIrSet`, which shapes the impulse exactly as `shapeIr` does today, `convolve.cpp:257-416`, then cuts and transforms it): `frames`, the plan `(n1, n2, n3)`, head taps L/R, partition spectra per stage per channel (structure-of-arrays), and the **transition kit** (second output rings and the extra accumulator sets of the stages it uses).
* `ConvState` (in the engine object; allocated off the audio thread in `prepare` / `install`): head history ring, per stage a pending input block (`B` floats per channel), the FDL of `c_s + 1` spectra per channel, the accumulator (one spectrum per channel), the output ring of `K` slots of `B` floats per channel (`K` = 4, 5, 5), transform scratch. Capacity `(c1, c2, c3)` = the plan counts of `F_cap = min(524,288, ceil(1.5 F))` of the first impulse (1.5 = headroom for the modulation of `size`, whose key range is 0.25 .. 4 and, with the Serum law, `10^(+-0.7)` = 0.2 .. 5.0, `fx_modules.cpp:931`). A change beyond the capacity takes the replace path.
* Memory formulas (bytes): `H = 2 x 8 x (n1 x 129 + n2 x 1,025 + n3 x 8,193)`; `FDL = 2 x 8 x ((c1+1) x 129 + (c2+1) x 1,025 + (c3+1) x 8,193)`; rings 372,736, pending blocks 74,752, accumulators 149,552, scratch about 140,000, pre-delay 882,000 (unchanged). Steady state, kit not held, at `F = 63,945` (cap 95,917, so `c` = 23, 21, 9): `1,047,312 + 1,721,216 + 372,736 + 74,752 + 149,552 + 140,000 + 882,000 = 4,387,568 B` against **4,969,808 B today** (4,087,808 + 882,000): **12 % less**; the peak during a rebuild adds `H` and the kit (1,047,312 + 372,736 + 299,104 = 1,719,152) = 6.1 MB against **9.06 MB today** (a whole second convolver). At the cap `F = 524,288`: `H = 8,388,240`, `FDL = 8,537,792`, total **18.5 MB against 34.4 MB today (1.86x less)**, peak 27.6 MB against 68 MB. So memory improves clearly only for long impulses; for the factory impulses it is 12 % steady and 33 % at the peak (nothing can be smaller than `H` + FDL = `2 x 8 B x frames x 2` channels once real transforms are used).

#### 3.4.6 CPU per sample (flops, model of 3.4.2; today's from 1.3)

| `F` (frames) | plan | today | after | ratio |
|--------------|------|-------|-------|-------|
| 3,969 (cabinet) | (31,0,0) | 1,824 | 1,172 | 1.6x |
| 30,870 (small room) | (23,28,0) | 8,544 | 1,711 | 5.0x |
| 63,945 (plate) | (23,21,5) | 16,800 | 1,959 | 8.6x |
| 524,288 (cap) | (23,21,61) | 131,872 | 2,855 | 46x |

(Plate CMAC part: `2 x (23 x 129/128 + 21 x 1,025/1,024 + 5 x 8,193/8,192) = 98.4` CMAC = 787 flops; transforms 160 + 220 + 280 = 660; head 512.) The floor is the head plus the stage-1 transforms (672 flops): below about 5,000 frames the structure gives only the packing and vectorisation (1.6x). HYPOTHESIS: the real ratio on the device is better than the flop ratio for small `F` (the new loops vectorise, the old ones do not) and worse for large `F` (radix-2 transforms of 8,192 complex points are memory bound); the `FXCOST convolve` lines of T01 decide, and section 5 states the acceptance numbers against the measured baseline.

#### 3.4.7 The seam transition (task C7)

A swap request carries an `IrSet` whose plan fits the capacity. The engine then acts **per stage**: the head and stage 1 at the tick where the request is taken, stages 2 and 3 at their next block start `j_s`:

1. Head: both tap sets run for the next 128 samples; the output is `(1 - a) head_cur + a head_next`, `a` a raised cosine `0.5 (1 - cos(pi u))`, `u` 0..1 over the 128 samples.
2. Stage `s`, from block `j_s`: the unit sequence of block `j_s` computes three accumulations (block `j_s - 1` with `H_next`, block `j_s` with `H_cur`, block `j_s` with `H_next`; all their inputs are in the FDL) and three inverse transforms. The current ring receives `y_cur` as always; the kit's ring receives `y_next` of `j_s - 1` (second half) and of `j_s` (both halves). The slot of times `[j_s B + o, (j_s + 1) B + o)` is read as `(1 - a) ring_cur + a ring_next`, `a` a raised cosine over the `B` samples of the slot; from the next slot on only `H_next` runs, the kit's ring becomes the stage's ring, and the old ring storage and the old `IrSet` leave with the retired set to the helper thread, like `retired` / `retiredLate` today (`fx_modules.cpp:982-984`).
3. `H_next` answers to the input played before the swap from the first block on, because the FDL is shared: the old tail does not vanish and the new one does not start from silence. Full convergence to the new impulse takes the stage's own time: head 128 samples, stage 1 256, stage 2 `1,024 + 3,072 + 1,024 = 5,120`, stage 3 `8,192 + 24,576 + 8,192 = 40,960` samples (0.93 s at 44.1 kHz): late-tail components (lags above 24,576) switch last, which is what a listener expects of a late tail.
4. A second request during a transition replaces the pending one (only the newest `IrSet` is kept; the superseded one goes to the retire list).

Extra cost: one slot per stage at about 3x that stage's multiply-accumulate, once per swap. Real-time safety: the request and the retire list are single-writer atomic pointers (the pattern of `ready` / `retired`); the audio thread allocates nothing (the kit and the new ring storage came with the `IrSet`).

#### 3.4.8 Module integration (tasks C6, C8)

`ConvolveModule::runJob` (`fx_modules.cpp:996-1021`) stops building a `Convolve` + `loadIr` and calls `Convolve::buildIrSet(params, sampleRate, impulse)`; it never reads `live`: `tone`, `mix` and the pre-delay are audio-thread parameters of the engine (`configure` already hands them over there, `:788-797`, and the read at `:1014` goes), so **the data race on `live.tone` cannot exist in this design**, and the `job.tone` snapshot of `rt-threading.md` T2 becomes dead code, removed in C8. The result travels through the existing `ready` atomic. `takeOver`: (a) same impulse file and the plan fits the engine's capacity -> in-place swap (C-D path); (b) otherwise (first impulse, another file, capacity exceeded) the job also carried a prepared engine: the old engine fades out over 2,048 samples (46 ms at 44.1 kHz; raised cosine against the new engine) and is retired (C-B path, task C6). So that the worker can choose (a) or (b), the audio thread publishes `(impulse pointer, capacity)` in one atomic pair; a stale choice is re-checked at `takeOver`, and a set that does not fit is retired and rebuilt (the key stays unbuilt, `:786`).

### 3.5 The `live.tone` race, stated plainly

Today: `configure` (audio thread) writes `live.tone` (`fx_modules.cpp:778`) while `runJob` (helper thread) reads it (`:1014`) with no ordering: a data race (undefined behaviour; in practice a stale or a new float, harmless on the targets, but a ThreadSanitizer failure). Fix 1 (`rt-threading.md` T2, first): copy `live.tone` into `job.tone` before `jobBusy.store(1, release)` and read `j->tone`. Fix 2 (this plan, C8): the job no longer needs the tone at all. Test: a ThreadSanitizer run of a test that moves `convolve.tone` every period while a rebuild runs (gate of section 5); today it reports a race.

### 3.6 Hyper / Dimension (rank 12)

**H-A (chosen).** In `process`: per-call tables for the voices (`vCenter`, `baseDelaySamples`, `maxExcursion`, the pan `cos` / `sin` gains, the LFO increment `kTwoPi * lfoFreq * invFs`), computed with the same expressions as today (bit-identical); **one** delay buffer instead of eleven (`monoIn` written once; the 7 voice reads and the 4 dimension reads index the same ring, which holds the same samples as the eleven copies did: bit-identical while `unison` is constant); the Dimension block (4 reads + `sin` / `cos`) runs only while `currentDimMix_ != 0 || targetDimMix != 0` (its LFO phase still advances by the same increment and wrap, so its state is the same when it is opened again; with both mixes 0 the term `dimL * 0` is exactly 0 for finite `dimL`: the same output); the Hyper voice reads run only while `currentHyperMix_ != 0 || targetHyperMix != 0` (output `inL + (hypL - inL) * 0 = inL`; the voice LFO phases still advance). Memory 176 KiB -> 16 KiB per instance (11x). libm per sample at `unison = 4`: 14 -> 4 (the voice `sin`s) + 2 only with `dimMix > 0`; divisions 4 -> 0. **One deliberate difference**: when `unison` changes during play, the newly active voices read the real recent input from the shared buffer instead of their own never-refreshed buffer (today a voice above the old `unison` has not written for a while and reads old data); not bit-identical in that case only, and better. **H-B (rejected)**: replace the voice `sin(phase)` by a recurrence: saves 4 libm calls per sample at the price of a drifting state and not bit-identical output; the whole effect is 0.4 % on the XL and used by 2 presets.

### 3.7 Which changes keep the sound, and by how much

| change | class | bound / how proven |
|--------|-------|--------------------|
| R1 hoists (type 3), R3 group sharing and integer read (type 0), R7 per-chunk tables, phase increment, cache, Hyper H-A (constant `unison`) | **bit-identical** | `memcmp` after normalising `-0`; `<NAME>-CHECKSUM` lines (64-bit FNV-1a of the float bits) |
| R2 Walsh-Hadamard butterfly | rounding: each `h_i` differs by <= 3 ulp of the sum (<= 1.8e-7 relative); loop gains <= 0.9996 and an orthonormal matrix keep the difference <= 2e-6 of the peak | tolerance 2e-6 x peak |
| R5 trig by angle addition | the LFO phase is computed once in float and offset by constants exact to 1e-7, instead of `2 pi (phase + lane/16)` in float: delay-modulation error <= `depth x 5e-7` samples; at the default `chorus_amount` 0.2 (all presets) depth = `0.2^2 x 2500 x rate` = 100 samples (`fx_reverb.h:230`) -> 5e-5 samples -> relative error <= `2 pi (4 kHz / 44.1 kHz) x 5e-5 = 2.9e-5` (-91 dB) for a 4 kHz band-limited program; at amount 1 (depth 2,500) 7e-4 (-63 dB) | tolerance 1e-4 x peak at amount 0.2 on a band-limited program; at amount 1 the test compares level, RT60 and spectrum instead of samples |
| R6 fixed taps | the same cubic as a weighted sum: rounding, <= 4 ulp per read | tolerance 2e-6 x peak |
| Convolve: head with 8 accumulators, real transforms, non-uniform schedule | rounding: summation order of `F` products, error about `sqrt(F) x 6e-8` relative to the output rms (`F` = 70,000: 1.6e-5) | tolerance 5e-5 x max abs of the reference up to `F` = 70,000 and 2e-4 at the cap |
| Convolve swap (C6, C7, C8) | **intended audible change**: no click, tail preserved; only in the blocks after a rebuild | C7 / C8 tests (history preserved, no discontinuity) |
| Nitrous guard (R7) | none in the engine's range (`ah >= +0.2767`); fixes direct use | test through `Reverb::set` |
