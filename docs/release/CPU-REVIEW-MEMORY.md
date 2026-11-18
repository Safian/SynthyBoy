# CPU review: numeric practice across the engine, memory and compile flags (Key 61 first)

Read only review of `feature/mpc-overlay` at 8e5cf113 (the round 12 tree, d7cbc890 plus documents). No source file was changed, nothing was built or run, no device was touched. Every file:line below was re-opened in that tree.

## 0. Sources, method, constants

Evidence, in order of weight:

1. The round 12 arm32 bench binary, not stripped: `.hw-local/mpc-perf/round12/bench-build/arm32/bench/synthyboy_tier_bench` (built 2026-11-18 21:35 from the merged tree), and the arm64 one next to it. I disassembled both with `objdump -d --demangle` and counted, per function, the calls to libm and to the run time helpers (`__aeabi_idivmod`, `__divsi3`, `__fixdfdi`, `__floatdidf`), the `vmrs APSR_nzcv` flag transfers, the fused multiply adds, the alignment qualifiers of the NEON loads, and the reachability from `OwnEngine::process` to `malloc`, `free`, locks and system calls. The build flags were read from that tree's `CMakeCache.txt` and `flags.make`.
2. Device measurements already in the tree: `.hw-local/mpc-perf/round12/k61-r12-sections-tables.md` (section times after round 12), `round12/k61-r12-all.csv` (all 160 presets, three passes), `round10/k61-breakdown/sections.csv` (Shepard Ascent, Mid Side Halo), the owner's figure that `--no-ftz` costs 1.5 to 2 points.
3. The code, read at the caller to prove the loop level.
4. Cycle estimates, labelled "est.": scalar VFP operation 1 to 2; float divide about 14; a scalar float compare with `vmrs` and its dependent branch or move 5 to 11 (the lower end calibrated on the round 12 chorus result, see N1); one lane of a NEON compare that GCC split into scalar code 5 to 15; single precision libm call 80 to 200; double libm 150 to 400; 64 bit conversion call 30 to 50; integer divide library call 25 to 40 (hardware `sdiv` 5 to 10 plus `mls`); an L2 hit 15 to 25; DRAM 100 to 150.

One block = 128 frames at 44.1 kHz = 2.902 ms = 4.666 M cycles at 1.608 GHz. 1 % of the block = 46.7 k cycles. Voice periods per block on the Key 61 (`low`): voices x 4 (Lorenz Garden 24, Stretched Glass Choir 32, Parallel Dusk Pad 24, Ring Mod Gamelan 32, Shepard Ascent 16, Triple Fifth Lead 32).

What the other three reviews already did (status tables read first, items not repeated): OSC F2 to F8, F10 to F12 done (spread / pan sines, detune curve, noise coefficients cached; one reciprocal per render); FILTER-FX F1 to F7 and F12 done (pair kernels, `neon_cmp.h`, runaway clamp, per call constants); CONTENT SP1 to SP3, SP5, SP6, SP10, MK1, GR1, SA1, LU1 done. Open there and referenced, not re-estimated here: OSC F1 (morph rebuild stagger), F9, F13 (flags); FILTER-FX F8 to F11, F13 to F20 (F10 = voice strip and engine peaks, F19 = flags); CONTENT SA2, GR2, GR3, SP7 to SP9, SP11 to SP15, FL1 (flags).

## 1. The headline

1. **One finding is large and was not seen before: the look-ahead limiter of the Compressor calls the integer divide library 136 times per sample** (M1). `fx_compressor.h:230, 237, 242, 244`; the disassembly shows `bl __aeabi_idivmod` inside both inner loops. It runs in every preset with `compressor.limit = 1`: Pentatonic Step Pluck (p99 80.9 %, over the 70 % rule), Karplus Harp (74.9 %, over), Detonation Tail. Estimate 9 to 15 % of the block; bit identical fix; hardware divide (`-mcpu=cortex-a17`) alone would take most of it too.
2. **The NEON float compares that are left are still split into scalar compares through the stack** (N1). The round 12 binary has 0 NEON float compare instructions (`vcgt/vcge/vclt/vcle/vceq.f32`) and 111 integer ones. The sample kernel's `sanitize4`, the grain window and output clamp, the spectral phase wrap and sincos, the wavetable warp `ffloor4` / `sinCycles4` still pay it. Estimate 1 to 5 % on the content presets, bit identical with the existing `neon_cmp.h` helpers.
3. **The voice tick is the largest cross-cutting cost after the filters**: 0.40 to 0.51 ms per block, 14 to 17.5 % of the block, in the five presets with round 12 sections (sum of the "voice tick" sections). Three bit identical items take a part of it: the envelope settings are rebuilt every period whenever any modulation is active (V1), the parameter read `P ()` is a float compare with `vmrs` about 360 times per voice period plus out of line helpers with static guards (V3), and the LFOs that no slot reads still run a libm `cos` and `exp2` per evaluation only for the display (V2). Together est. 1 to 4 % per heavy preset (section 10).
4. **Compile flags**: on arm32 today no flag changes the bits except FMA contraction, and contraction is only possible with a VFPv4 FPU. `-mcpu=cortex-a17` with `-mfpu=neon` kept, `-fno-math-errno`, `-fno-trapping-math`, `-fno-semantic-interposition` and LTO should keep every sample; this must be proved by a two build render compare, and the existing compare writes 24 bit WAV files, which hides differences below half a 24 bit step (K6). The gains are unknown except for the limiter presets.
5. **Denormals are not a driver**: every rendering thread of the plugin and of the bench sets FZ; no rendering thread lacks it. Allocation and locking: the audio path is clean after `prepare ()`; what remains are static guard checks (`dmb`) on every call of `paramIndex ()`, `destLawOf ()`, `libKeys ()`, and two helper threads that wake every 2 ms with the scheduling they inherit (T1).
6. **Memory**: `sizeof (Voice)` is 33 128 bytes on arm32 (the `operator new (0x8168)` before `Voice::Voice ()` in `OwnEngine::prepare`), 36 voices are allocated, and each carries about 0.5 MB of heap. Only the touched part matters: est. 30 to 50 KB per voice period, more than the 32 KB L1D, inside the shared L2. No miss counter was ever measured; this is the largest unknown of the review.

The weak review's three items: M2 (`std::tanh` per sample in `outputLimit`, 3 to 5 %) is refuted, the `tanh` branch is not taken below 1.8; M3 (the Voice exceeds 32 KB) is confirmed in size but has no saving attached; M1 (no allocation in `runJob` on the audio thread) is confirmed. Section 9.

## 2. Facts from the binary (round 12, arm32, GCC 12.2)

- **libm**: 146 call sites of `powf`, 88 `sqrtf`, 76 `sinf`, 65 `exp2f`, 64 `tanf`, 55 `floorf`, 49 `expf`, 44 `tanhf`, 37 `sincosf`, 35 `expm1f`, 32 `cosf`, 20 `roundf`, 15 `log2f`, 9 `log10f`, 7 `fmodf`, 6 `lroundf`, plus double `cos` 52, `sin` 28, `floor` 26, `pow` 21, `exp` 17, `exp2` 16, `log2` 16, `fmod` 15, `lround` 10 (glibc names them `*f32` and `*l`). `floorf`, `roundf`, `lroundf`, `truncf` are calls on ARMv7 (no `vrint`); `ffloor` in `dsp/util.h:44-50` exists for that reason.
- **No libm call is hoisted or shared**: with the default `-fmath-errno` GCC treats them as having a side effect. `sqrtf` becomes `vsqrt` plus a compare, `vmrs` and a fallback call.
- **Integer divide**: 29 `__aeabi_idivmod`, 56 `__aeabi_uidivmod`, 38 `__udivsi3`, 14 `__divsi3` call sites in the whole binary. On the audio path only these execute in a steady state: `Compressor::process` (4 sites, M1), `WtOsc::update` (2) and `WtOsc::render` (2, OSC "not waste": 0.01 %), `LibOsc::update` (1), `shapeFrame` (1 per spectral frame), `inverseFft` (1 per morph build), `Fft::run` (1), `Voice::evaluateVoiceControl` (1, cached per epoch), and on XL only `Bode::process`, `Convolve::process`.
- **64 bit conversions**: `__fixdfdi` (double to int64) and `__floatdidf` (int64 to double) appear per sample only in the reference paths (`readSampleStereo`, granular `readSample` / `hermite`, used with `--scalar-content` or for files over 2^30 frames, `sample_voice.h:160`); the block kernels have one per call (`renderBlockT`, `renderGrains`, `advanceHead`): 96 calls per block for Bowed MPE Strings, est. 4 k cycles, 0.08 %.
- **NEON float compares**: 0 in the binary (section 1, item 2). `vmrs APSR_nzcv` static counts of the hottest functions: `FilterImpl::processInternal` 323, `Voice::tick` 251, `processPairT<NeonLanes>` 160, `LibOsc::update` 144, `WtOsc::update` 101, `advanceToSpectrum` 90, `OwnEngine::process` 89, `advancePhases` 82, `renderBlockT` 79 to 80, `Compressor::process` 78.
- **Fused multiply add**: 0 `vfma`/`vfms` in the arm32 binary (VFPv3 has none; GCC forms `vmla`, which rounds twice like a multiply and an add). On arm64 GCC fuses wherever the `fp-contract=off` pragma does not apply: `fmadd` in `ProgramMeter::accumulate`, `Envelope::advance`, `ModMatrix::evaluate`, `FxChain::runModule`, `Utility::process` (53), `HyperDimension::process` (54). So `-std=c++17` does NOT turn contraction off for C++ (the CONTENT review states that it does; refuted by these instructions). This matters for K1.
- **NEON load alignment**: 634 `vld1/vst1` with an alignment qualifier against 2808 without in the engine functions (for example `WtOsc::render` 32 against 52, `Voice::render` 2 against 54). `std::vector<float>` storage from glibc `malloc` is 8 byte aligned on arm32, so a 16 byte load crosses a 64 byte line once in four when the base is 8 mod 16. On the A15 / A17 class an unaligned load that stays in a line costs nothing extra (est.); the gain of `:128` hints is at most a cycle per crossing. Not ranked.

## 3. Findings

Each finding: where, what, loop level proved by the caller, consequence for the output, saving per named preset with the arithmetic, confidence, change.

### M1. The look-ahead limiter of the Compressor: 136 integer divide library calls per sample

- **Where.** `core/engine-own/fx/fx_compressor.h:223-247` (`Limiter::process`): `pos = (pos + 1) % cap` at :230, the min loop `req[(pos - k + cap * 4) % cap]` at :236-237, the sum loop at :241-242, `di` at :244. Called at :175 for every band that is not skipped, when `p.mode == 1` (:169), set from `compressor.limit` (`fx_modules.cpp:192`). `cap = lookAhead + 8` is a run time value (:34), so `%` is a call: `bl __aeabi_idivmod` at 0x6cd28, 0x6cd8e (inside the min loop), 0x6ce04 (inside the sum loop), 0x6ce36 in `Compressor::process`; `__aeabi_idivmod` itself is a push, a call to `.divsi3_skip_div0_test`, a pop, `mul`, `sub` (0x17a51c).
- **Loop level.** Per sample, per band that runs, per inner iteration: `lookAhead = lround (0.0015 x 44100) = 66` (:25, :28), so `la + 1 = 67` iterations in each of the two loops. All factory presets with the Compressor on use `compressor.bands = 3` (one band, `use = {1,1,1}`, :83, :130), so one band runs: 1 + 67 + 67 + 1 = 136 calls per sample.
- **Presets** (all 160 files read): limit on in Pentatonic Step Pluck (round 12 median 60.5 %, p99 80.9 %), Karplus Harp (53.5 / 74.9 %), Detonation Tail (38.1 / 64.3 %). The Compressor is not masked at tier low (`quality.h:112`). Their smallest block is 47.9, 41.4, 34.8 % (`k61-r12-all.csv`), a floor that a constant per block cost of this size explains.
- **Saving (est.).** 128 x 136 = 17 408 calls per block x 25 to 40 cycles = 435 k to 696 k cycles = **9.3 to 14.9 %** of the block on each of the three. The min loop also does a scalar float compare with `vmrs` per iteration (0x6cd9c .. 0x6cda0): 128 x 67 x 5 to 11 = 43 k to 94 k cycles, **0.9 to 2.0 %** more.
- **Consequence.** Bit identical. The index `(pos - k + cap * 4) % cap` equals `pos - k` wrapped once into `[0, cap)` because `0 <= pos < cap` and `0 <= k <= la < cap`. The float operations (the `std::min` chain, the sum in the same order, the division by `la + 1`) stay as they are.
- **Change.** Replace each modulo by a wrap: `int j = pos - k; if (j < 0) j += cap;`, or better, split each loop into two contiguous runs (`pos .. 0` and `cap - 1 .. cap - (la - pos)`), no branch per iteration. Keep the sum loop scalar and in the same order. The min loop may use NEON `vminq_f32` and a final reduction: the values are `ceilAmp / pk` or 1, positive and finite, so the minimum is the same bits in any order. `pos = pos + 1 == cap ? 0 : pos + 1`. Test: a lockstep test of the old and new `Limiter::process` on random input and look-ahead lengths 1 to 200.
- **Confidence.** High on the mechanism (disassembly, preset files). Medium on the size: no section time exists for the Compressor of these presets. Measure: `--sections --load "Pentatonic Step Pluck" --load "Karplus Harp" --load "Detonation Tail"` (section `fx: compressor`).
- Not bit identical and not proposed: a running sum instead of the 67 term sum (different rounding).

### N1. The NEON float compares outside the filters are still scalar on arm32

- **Where (lines of today's tree; the FILTER-FX list has older numbers).**
  - `sample-osc/src/sample_voice.cpp:384` (`sanitize4`: two compares), used at :1024-1025 and :1086-1087 on every 4 output samples of every lane, left and right; `:367` (`equalPower4`) in the loop crossfade region only.
  - `granular/src/granular.cpp:542` (window half test, per 4 grain samples, in the window kernel), `:773` (finite test of the output clamp, per 4 samples).
  - `spectral/src/kernels.cpp:267` (`sincosN`: one per group of 4 bins), `:298`, `:310-313` (`wrapN`: five per group), `:540` (output clamp), `:660` (`findPeaks` NEON form, three per group; arm32 already uses the scalar form, CONTENT SP8), `:699` (`placeRegion`).
  - `engine-own/osc/wt_osc.h:953` (`ffloor4`), `:960` (`sinCycles4`): the vector phase warps at tier low (Sync, Sync Var, Formant, Bend family).
- **What.** Each 4 lane compare becomes four `vmov` to core, `vcmpe`, `vmrs`, conditional moves, four stores and a vector reload (seen at 0xcbcec .. 0xcbd0e in `advancePhases`).
- **Calibration.** Round 12 removed 32 lane compares per sample from the chorus (with the F12 lane refresh) and saved 0.051 ms = 82 k cycles per block: about 20 cycles per removed lane compare including the refresh work. The spectral micro (CONTENT: the wrap is 33 us of a frame) bounds it from above at about 10 cycles there. I use 5 to 15 cycles per lane compare.
- **Saving (est.).**
  - Spectral Vowel Freeze, Spectral Gong Pad (2 frames per block): per frame 1025 bins / 4 = 257 groups x 6 compares x 4 lanes = 6 168 lane compares x 5 to 15 = 31 k to 93 k cycles; per block 62 k to 185 k, **1.3 to 4.0 %**, bounded by the measured wrap time (33 us x 2 = 2.3 %), so **1.3 to 2.3 %**. Spectral Chord Morph (1.5 frames): 1.0 to 1.7 %.
  - Bowed MPE Strings (24 sample lanes): 4 lane compares per lane sample: 24 x 128 x 4 = 12 288 x 5 to 15 = 61 k to 184 k, **1.3 to 3.9 %**. (The CONTENT review measured 73 cycles per lane sample in this kernel; 20 to 60 of them would be these compares.)
  - Frozen Pluck Drone: 83 grains x 128 = 10 624 window lane compares plus 18 clouds x 2 x 128 = 4 608 clamp lanes = 15 232 x 5 to 15 = 76 k to 228 k, **1.6 to 4.9 %**; Chord Cloud Pad and Gong Grain Swarm in proportion to their grains (56, 34): about two thirds and two fifths of that.
  - Warp presets at tier low (Tine Glass Keys, Bend Formant Growl, the six Sync presets): 8 voices x 1 lane x 256 oversampled samples / 4 x 2 compares x 4 lanes = 4 096 x 5 to 15 = 20 k to 61 k, **0.4 to 1.3 %**.
  - The six heavy presets: 0 (no warps, no content oscillators).
- **Consequence.** Bit identical, also for NaN, infinities, signed zeros and subnormals, when written with the helpers of `core/engine-own/dsp/neon_cmp.h` (the FILTER-FX F2 method). Care: `vcltq_f32 (x, 0)` is false for -0, so a sign bit test must exclude `|x| == 0`; `ffloor4`'s `x < (float) i` can be done as "sign set, and the bits of x and of the truncated value differ, and |x| != 0".
- **Change.** Use or add helpers (`isFiniteMask`, `absGeMask`, `ltZeroMask`, `gtConstMask`) and replace the eleven sites; extend `synthyboy_test_neon_cmp`; check in the disassembly that no `vmrs` is left in those loops. `--verify` per device on the content loads.
- **Confidence.** High on the mechanism (binary), medium on the size.

### N2. Per sample scalar bookkeeping outside the voices

- **Where.** `fx_chain.cpp:50-56` (`finiteBlock`, called at :571 after every module run and :664), `:767` and `:782` (rack peaks at tier low); `own_engine.cpp:80-86` (`outputLimit`: `!(a > 1.8f)` and `y == y`, per channel per sample at :1601-1602); `shell-mpc/output_stage.h:50-62` (gain compare and four `isfinite` per sample), `shell-mpc/output_peak.h:23, 25` (`std::fmax` per sample and channel: a `fmaxf` library call on ARMv7, expected; the shell is not in the bench binary, so this one is not seen in a disassembly).
- **Loop level.** Per sample. Engine bus and master peaks (`own_engine.cpp:1536-1542, 1603`) and the voice strip peaks are FILTER-FX F10; not counted again.
- **Saving (est.).** Lorenz Garden (Main rack: chorus, delay, EQ): 3 x 2 + 4 + 4 + 5 = 19 compares and 2 `fmaxf` per sample: 128 x (19 x 5 to 9 + 2 x 10 to 20) = 15 k to 27 k cycles, **0.3 to 0.6 %**; the same order on every preset (fewer modules, fewer compares).
- **Consequence.** Audio bit identical: `finiteBlock` answers the same boolean with a NEON bit test (`|bits| < 0x7f800000`), the peaks only feed meters, `outputLimit` can test 4 samples at once and run the scalar form only for a group that has a sample over 1.8 or a NaN. The shell stage: the engine output is already finite and bounded by `outputLimit`, the four `isfinite` can be one NEON test per 4 samples with the scalar path as the fallback.
- **Confidence.** High on presence, medium on the size.

### V1. Envelope settings are rebuilt every period when any modulation is active

- **Where.** `voice.cpp:568` (`envFromCache` needs `numTouched == 0`), `:569-578` (for every one of the 6 envelopes `envSettings (e, s, true)`), `:477-509` (the lambda: 13 `P ()` reads, 5 multiplies by `envFactor`, `envLen` for the display, `legatoInv`).
- **Loop level.** Per voice period x 6 envelopes, also for the envelopes that the tier low economy then skips (`:586-589`: settings first, skip after).
- **Why waste.** `numTouched` counts every modulated destination. All six heavy presets modulate something every period, so the cache never applies. Decoded destinations (`docs/params.json` `modDestinations` by choice): Lorenz Garden, Triple Fifth Lead, Parallel Dusk Pad, Shepard Ascent modulate no envelope key; Stretched Glass Choir modulates `env1.attack`, `env1.release`; Ring Mod Gamelan `env1.decay`, `env1.release`.
- **Saving (est.).** About 13 x 10 to 25 + 30 = 160 to 355 cycles per envelope; 6 per voice period = 1.0 k to 2.1 k. Lorenz Garden 24 x = 23 k to 51 k, **0.5 to 1.1 %**; Parallel Dusk Pad the same; Triple Fifth Lead 32 x: **0.7 to 1.5 %**; Stretched Glass Choir, Ring Mod Gamelan (5 of 6): **0.6 to 1.2 %**; Shepard Ascent 16 x: **0.3 to 0.7 %**. Bounded by the measured "voice tick: envelopes + LFOs" (Lorenz 0.102 ms = 3.5 %, Stretched 4.5 %, Ring Mod 4.0 %, Triple Fifth 4.0 %, Dusk 3.1 %, Shepard 2.7 %).
- **Consequence.** Bit identical if the key is complete: per envelope, valid while the epoch, the bpm, the voice control (`vcCacheEpoch`, `vcCacheSequence`, it scales the times) are the same and none of that envelope's keys has a non zero `modP`. `legatoInv` and `envLen` must still be updated when they would change (`legatoInv` read at `:259-261`).
- **Observation (not CPU, verify).** Today's cache does not include the voice control step: `legatoTo` (`:248-251`) clears `vcCacheValid` but not `envCacheValid`, so after a legato note with `numTouched == 0` the envelope times can keep the previous step's `envFactor` for that note. Check with a voice control env step pattern and legato.
- **Add on, bit identical.** `powerShape` (`util.h:66-71`) computes `std::expm1 (power)` per call; the envelope curve (`envelope.h:136, 160, 173`) is constant per stage. Caching the denominator per envelope and stage saves one `expm1f` of two: Lorenz Garden about 24 x 1.5 x 80 to 200 = 3 k to 7 k cycles, **0.06 to 0.15 %**.
- **Confidence.** Medium (the share of `P ()` in the section is estimated).

### V2. LFOs: a libm `exp2` per evaluation for a constant coefficient, and libm work for values only the display reads

- **Where.** `voice.cpp:378` (`1 - std::exp2 (-dt / half)` per lane per evaluation, `half` from `lfoN.smooth_time`); `lfo.h:21` (`-std::cos` for shape 0); `voice.cpp:605-615` (an LFO that no slot reads is evaluated every fourth period, its value goes only to `src`, which `writeReadout` shows for the newest voice, `own_engine.cpp:1783-1797`; `usedSources` includes every audio reader, `mod_matrix.cpp:302-317`).
- **Loop level.** Per voice period x LFO evaluation. Evaluations per voice period: used LFOs every period, unused ones 1 in 4: Lorenz Garden 2.5, Ring Mod Gamelan 2.5, Triple Fifth Lead 2.5, Parallel Dusk Pad 3.25, Stretched Glass Choir 3.25, Shepard Ascent 4. Smoothing is on in 1594 of the 1600 LFO slots of the factory presets (`smooth_time` 5 ms, `half` = 1 ms, so the `exp2f` runs); the unused ones keep the default shape 0 (sine, a `cosf`).
- **Saving (est.).**
  - (a) Coefficient cached on (`dt`, `smooth_time`), bit identical: 80 to 200 cycles per evaluation. Lorenz Garden 60 evaluations per block: 5 k to 12 k, **0.1 to 0.26 %**; Stretched Glass Choir 104: **0.18 to 0.45 %**; Shepard Ascent 64: **0.11 to 0.27 %**; Triple Fifth Lead 80: **0.14 to 0.34 %**.
  - (b) For an unused LFO, advance the phase as now but compute the shape and the smoothing only in the voice whose sources the readout shows (the newest active voice): saves the whole evaluation (a `cosf`, the `exp2f` unless (a), about 16 `P ()` reads, a double divide at `:302`) of 400 to 900 cycles in the other voices. Lorenz Garden 24 x 2.5 x 5/6 = 50: 20 k to 45 k, **0.4 to 1.0 %**; Triple Fifth Lead and Ring Mod Gamelan 32 x 2.5 x 7/8 = 70: **0.6 to 1.3 %**; Stretched Glass Choir 32 x 2.25 x 7/8 = 63: **0.5 to 1.2 %**; Parallel Dusk Pad: **0.4 to 0.9 %**; Shepard Ascent 16 x 2 x 3/4 = 24: **0.2 to 0.5 %**. (a) and (b) overlap.
- **Consequence.** (a) bit identical. (b) audio bit identical (no audio reader exists, proved by the source mask); the display differs when an older voice becomes the newest again (its unused LFO value starts unsmoothed for a few ms).
- **Confidence.** High for (a), medium for (b).

### V3. The parameter read path of the voice tick

- **Where.** `voice.h:185-189` (`P ()`: `d == 0.0f` on the float, so `vcmp` and `vmrs`), 116 call sites in `Voice::tick` (`voice.cpp:420-1108`), 38 of them compiled as calls to the out of line `Voice::P`; `voice.cpp:139-142` (`modulatedSlow`: calls `paramTable ()` in another TU and `destLawOf ()`), 81 call sites in `Voice::tick` and 14 in `lfoValue`. `destLawOf` (`mod_matrix.cpp:114`) and `paramIndex ()` and `libKeys ()` (`lib_osc.cpp:86`) are function local statics: every call does a guard load and `dmb ish`, and GCC put the initialiser inline, so the prologue saves 9 core registers (`destLawOf`, plus `vpush {d8, d9}`) or 8 core and 8 D registers and 368 bytes of stack (`paramIndex`, 0x88484) before the guard test.
- **Loop level.** Per voice period: about 360 `P ()` reads for Lorenz Garden (3 oscillators x about 45, sub 10, two filters x about 30, envelopes 78, LFOs about 40, random LFOs 28); 15 to 40 modulated reads (each `paramTable`, `destLawOf`, `modulatedValue`); `paramIndex ()` once per tick and once per LFO evaluation.
- **Saving (est.).** The compare as an integer test of the bits saves 4 to 7 cycles per read: 360 x 4 to 7 x 24 = 35 k to 60 k cycles for Lorenz Garden, **0.7 to 1.3 %**; Triple Fifth Lead (32 voice periods): **1.0 to 1.7 %**. Guard free helpers (pointers kept in `TickGlobals` once per block) save about 30 to 40 cycles per modulated read: Lorenz 24 x 15 to 40 x 30 to 40 = 11 k to 38 k, **0.2 to 0.8 %**. With V1 the envelope reads go away; do not add V1 and V3 in full.
- **Consequence.** Bit identical: `(bits & 0x7fffffff) == 0` is true exactly for +0 and -0, as `d == 0.0f`; NaN is not zero in both.
- **Change.** `P ()` and `PR ()` as `__attribute__ ((always_inline))` with the bit test; `TickGlobals` gets `const DestLaw* laws`, `const ParamInfo* table`, `const ParamIndex* pi`, set in `OwnEngine::process` after the warm up of `prepare ()` (`own_engine.cpp:404-406`); `modulatedSlow` uses them.
- **Confidence.** Medium.

### V4. The modulation matrix evaluates each slot with about ten scalar compares (low)

- `mod_matrix.cpp:337-399` (the `one` lambda, compiled out of line, 29 `vmrs`), called per voice period per slot (`:412-436`). Measured "voice tick: modulation matrix" 1.4 to 2.4 % of the block (Lorenz Garden 0.071 ms, 24 slots: about 200 cycles per slot evaluation). The slots of the six presets have `power = 0`, no rise / fall, no curve, no aux: the work is `clamp01` twice, the `powerShape` near zero test, `copysign`, the delay test, the touched bookkeeping. A slot table split into "plain" slots (no curve, no slew, no power, no aux, no delay) evaluated in a tight loop with integer tests: est. 30 to 50 % of the section, **0.4 to 1.2 %**. Bit identical if every operation stays (only the compares change form). Low confidence; measure the section before and after.

### K1 to K6. Compile flags (section 8 has the details and the experiment)

- **K1** `-mcpu=cortex-a17` instead of `-march=armv7-a`, keeping `-mfpu=neon -mfloat-abi=hard`: hardware `sdiv` / `udiv` (the A17 has them: `/proc/cpuinfo` `idiva idivt`), A17 scheduling. Bit identical expected. Saving: M1 presets 6 to 11 % without the code change (each call of 25 to 40 cycles becomes `sdiv` + `mls` of 6 to 14), else unknown.
- **K2** `-fno-math-errno -fno-trapping-math`: libm treated as pure (CSE, hoisting), `vsqrt` without the fallback. Most invariant calls were hoisted by hand already (OSC F5, F11, F12; FILTER-FX F4, F7). Expected small (0 to 0.5 %), bit identical expected.
- **K3** `-fno-semantic-interposition` (or `-fvisibility=hidden`) for `synthyboy_core`: the core is compiled `-fPIC` (`core/CMakeLists.txt:59`) with default visibility, so GCC must not inline or specialise its non inline global functions inside their own TU (for example `Voice::modulatedSlow` stays 81 calls in `Voice::tick`) and reaches globals through the GOT. Bit identical on arm32. Unknown, est. 0 to 3 %.
- **K4** LTO: cross TU inlining (`paramTable`, `destLawOf`, `modulatedValue`, the filter helpers in other libraries). Bit identical in arithmetic; small risk from compile time folding of libm with constant arguments (GCC folds with MPFR, correctly rounded; glibc's float functions are not always correctly rounded). Must be proved by the render compare.
- **K5** PGO: layout and branch weights; `-fprofile-use` also enables value profiling that can turn `x % cap` with a hot constant into multiplies. Bit identical in arithmetic (the same folding caveat). Costly to set up for a cross build (the `.gcda` files come from the device).
- **K6** The cross build proof itself: `synthyboy_preset_render --wav` writes 24 bit integer PCM (`tools/preset-render/preset_render.cpp:427, 445`: `writeWav (..., 24, false)`), so "byte for byte the same WAV" proves equality only above about half a 24 bit step (6e-8 of full scale) or where a difference crosses a rounding boundary. `writeWav` supports 32 bit float (`wav_reader.cpp:197-208`); a `--wav-float` option, or a SHA-256 of the float output per render (`core/src/sha256.cpp` exists), makes it exact. Also, the round 12 entry of `docs/MPC.md` does not say on which build the 960 files were rendered; a Mac build does not test the arm32 GCC code.

### MEM1. Every oscillator of every voice owns a 24.6 KB morph scratch

- **Where.** `wt_osc.h:515-524` (`prepareMorph`, called for all 3 oscillators of every voice in `Voice::prepare`, `voice.cpp:100`), `morph.h:22-26` (`MorphScratch`: `re`, `im` of 2048 floats, `c`, `s` of 1026 floats = 24 624 bytes), used only inside `buildMorphCycle` (`wt_osc.h:592`).
- **Why it matters.** A build runs once per 16 periods per voice (tier low) and all voices of a chord build in the same block (OSC F1). Each build pulls its own 24.6 KB scratch, last touched 512 samples earlier, back into L1 and probably from DRAM: 384 lines.
- **Saving (est.).** 384 x 15 to 150 cycles = 6 k to 58 k per build; Stretched Glass Choir 2.0 builds per block: 12 k to 115 k, **0.3 to 2.5 %** of the mean; in the rebuild block (8 builds): **1 to 10 points** of the p99 block. Shepard Ascent the same order. Memory: 36 voices x 3 x 24.6 KB = 2.66 MB less.
- **Consequence.** Bit identical: the build does not depend on what the scratch held (`synthyboy_test_own_morph_transform` poisons it with random numbers and NaNs, `core/tests/test_own_morph_transform.cpp:81`). One audio thread renders all voices, so one scratch per engine (in `TickGlobals`, like `spectralScratch`) is enough.
- **Confidence.** Low on the size (no miss counter).

### MEM2, MEM3. Structural options (not ranked for this round)

- **MEM2, a compact parameter snapshot per voice.** Each `P ()` reads `modP [idx]` (a voice's 10.5 KB array) and `base [idx]` (shared, 10.5 KB) at indices spread over 2637 parameters: the about 360 reads of a voice period touch most of the 165 lines of each array. A per block gather of the about 400 keys the tick reads into a contiguous array, and a remap of the matrix destinations to it, would make them 2 x 25 lines. Bit identical. Worth it only if misses are a real share of the tick (a 25 k to 33 k cycle voice period); measure first (section 7).
- **MEM3, two voices per Q register in the filter kernels.** The pair kernels hold left and right in a D register (2 lanes). The A17's NEON unit is 64 bits wide, so a 4 lane form does not double the throughput; it only shares the scalar parts. Est. 10 to 25 % of a kernel; medium to high risk (voice pairing, different cutoffs). Not proposed now.

### R1. The display readout runs every block on the audio thread

- `own_engine.cpp:1758-1879` (`writeReadout`, called at :1621): 80 + 10 `std::floor` calls (`voice.h:175` `lfoReadout`, a `floorf` library call on ARMv7), up to 10 `log10f`, an `expf`, a selection of the 8 newest of 36 voices (288 iterations), about 700 relaxed atomic stores. Est. 5 k to 10 k cycles per block, **0.1 to 0.2 %**. `ffloor` instead of `std::floor` is the same value for `|x| < 2^22` (bit identical); writing the readout every fourth block is display only (a 25 Hz display needs one in 13). Same in `publishStats` (`:1626-1677`). Confidence medium.

### T1. Two helper threads poll every 2 ms and keep the scheduling of their creator

- `own_engine.cpp:158-174` (`fxWorker`: `sleep_for (2 ms)` in a loop) and `fx_modules.cpp:59-77` (`FxWorker::loop`: `cv.wait_for (2 ms)` when idle). About 1000 wake ups per second with nothing to do. Neither calls a policy function: `shell-mpc/thread_policy.h:5-10` says a `std::thread` inherits policy, priority and CPU affinity of its creator, and the shell's own helper threads reset that; these engine threads do not. If one is created on a thread pinned to the audio core (`isolcpus=2-3` on the Key 61), its wake ups and its jobs (a Convolve rebuild or spectrum job) run on that core. This is also FILTER-FX F20's open hypothesis (Frozen Pluck Drone 3.3 points with a masked Convolve). Cost unknown; audio bit identical change: wait without a timeout (a counting semaphore or a futex on the job count, so no wake up is lost), and a start hook that the shell fills with `useNormalScheduling`. Verify on the device: `ps -eLo tid,psr,cls,rtprio,comm` while the plugin plays.

## 4. Inventory: libm, double, 64 bit conversion, integer divide, `std::function`, virtual calls, atomics

Scope: `core/engine-own/`, `core/filters-serum`, `core/fx-serum-a`, `core/fx-serum-b`, `core/spectral`, `core/granular`, `core/sample-osc`, `core/multisample`, `core/shapes`, `core/engine-own/dsp`, and the shell callback. Every grep hit was assigned to a function and its caller. Levels: **S** per sample (or per lane sample), **VP** per voice per control period, **P** per control period (engine or rack), **B** per block, **F** per spectral frame or morph build, **N** per note start, **C** per change of a parameter (cached), **I** prepare / load / helper thread, **X** XL only (`math=exact`) or masked at tier low. Cost per block on the Key 61 `low`, est., for the preset where it applies (1 % = 46.7 k cycles). Files sorted by the largest row; rows sorted by cost.

### 4.1 `engine-own/fx/fx_compressor.h`

| line | what | level | status | cost per block | fix |
|---|---|---|---|---|---|
| 230, 237, 242, 244 | `%` by `cap` (library call) | S x 67 | not optimised | 435 k to 696 k (9.3 to 14.9 %) on the three limit presets | M1 |
| 141, 149 | float divide per band sample (envelope followers) | S | inherent | 2 x 128 x 14 = 3.6 k | none |
| 143, 151, 165 | `powG` / `pow10`: `fastmath` at tier low (`:107`), libm `powf` on XL | S | done at low | 0 at low | none |
| 160 | `std::log10` per band sample (Serum mode only, no factory preset) | S | not optimised | 0 for factory presets; 128 x 80 to 200 = 10 k to 26 k for a Serum mode patch | `fastmath` log at tier low (rounding level) |
| 85-89, 100-103 | `exp`, `pow` of call constants | P | per call | 10 x 80 to 200 x 4 = 3 k to 8 k (Mid Side Halo) | cache on the inputs (bit identical) |
| 28 | `lround` | I | prepare | 0 | none |

### 4.2 `spectral/src/` (CONTENT review area; here only the cross-cutting part)

| line | what | level | status | cost | fix |
|---|---|---|---|---|---|
| kernels.cpp 267, 298, 310-313 | scalarised NEON compares | F x bins | not optimised | 62 k to 107 k (1.3 to 2.3 %) SVF, SGP | N1 |
| kernels.cpp 540, 660, 699 | scalarised NEON compares (clamp, NEON `findPeaks` unused on arm32, `placeRegion`) | F | not optimised | in N1 | N1 |
| kernels.cpp 395 | `std::fmod` fallback (NaN, huge phase) | F, rare | fallback only | 0 | none |
| spectral.cpp 358-380 | `sin`, `floor`, `exp2`, `log2`, `round`, `exp` per bin (Detune / Shepard modes) | F x bin | not optimised | 0 for factory presets | CONTENT SP12 |
| spectral.cpp, other 33 hits | double positions, `lround`, start tables | N, F, I | SP2, SP3 done | small | CONTENT |
| `__floatdidf` in `advanceHead`, `synthesizeHop`, `primeFast` | int64 to double | VP, F | per call | under 1 k | none |

### 4.3 `sample-osc/src/sample_voice.cpp`, `granular/src/granular.cpp`, `multisample`

| line | what | level | status | cost | fix |
|---|---|---|---|---|---|
| sample 384 (via 1024-1025, 1086-1087) | `sanitize4` compares | S per lane | not optimised | 61 k to 184 k (1.3 to 3.9 %) Bowed MPE Strings | N1 |
| granular 542, 773 | window half test, output finite test | S per grain / cloud | not optimised | 76 k to 228 k (1.6 to 4.9 %) Frozen Pluck Drone | N1 |
| sample 1039-1052 | double position, two double to int, int to double per sample | S | open | CONTENT SA2 (1.7 to 2.4 %, rounding level) | CONTENT |
| granular 596-621 | double positions per lane | S | open | CONTENT GR3 | CONTENT |
| sample 367 | `equalPower4` compare in the loop crossfade | S in the crossfade region | not optimised | part of N1 | N1 |
| sample 106-140 (`readSampleStereo`: 8 `__fixdfdi`, 16 `__floatdidf`, 8 `floor`) | 64 bit conversions | S | reference path only (`sample_voice.h:160`) | 0 in the plugin | none |
| `renderBlockT` (1 `__floatdidf` per call) | int64 to double | VP per lane | per call | 96 x 40 = 4 k (0.08 %) BMS | none |
| sample 634 (`pow` of the pitch) | libm | C | cached | 0 | done |
| multisample 798, 810, 817 | double divisions per sample in the envelope stages | S | open | CONTENT MS1 area | CONTENT |
| multisample `std::function` (`SampleReader`, `multisample.h:79`) | callback | I (instrument build) | load only | 0 | none |

### 4.4 `engine-own/voice/voice.cpp`, `voice.h`, `env_lfo/*`, `matrix/mod_matrix.cpp`, `dsp/util.h`

| line | what | level | status | cost (Lorenz Garden unless named) | fix |
|---|---|---|---|---|---|
| voice.h 187 (116 sites in `tick`) | float `== 0` in `P ()` | VP x about 360 | not optimised | 35 k to 60 k (0.7 to 1.3 %) | V3 |
| voice.cpp 568-578, 477-509 | envelope settings rebuilt | VP x 6 | cache defeated by any modulation | 23 k to 51 k (0.5 to 1.1 %) | V1 |
| voice.cpp 605-615, lfo.h 21, voice.cpp 378 | `cosf` + `exp2f` per LFO evaluation, unused LFOs for the display | VP x 2.5 to 4 | partly (unused every 4th period) | 10 k to 45 k (0.2 to 1.0 %) | V2 |
| voice.cpp 139-142, mod_matrix.cpp 114 | `paramTable ()`, `destLawOf ()` (guard, `dmb`, 9 register prologue) per modulated read | VP x 15 to 40 | not optimised | 11 k to 38 k (0.2 to 0.8 %) | V3 |
| mod_matrix.cpp 337-399 | scalar compares per slot | VP x slots | not optimised | measured 1.4 to 2.4 %; saving 0.4 to 1.2 % | V4 |
| util.h 66-71 via envelope.h 136, 160, 173 | `expm1f` x 2 per curved stage advance | VP x 1 to 2 | not optimised | 6 k to 14 k (0.12 to 0.3 %), half avoidable | V1 add on |
| envelope.h 83-178 | double `pos`, `left`, `pos / s.attack` (double divide) | VP x 1 to 6 | inherent | 6 x 30 x 24 = 4 k (0.1 %) | float would change bits; leave |
| voice.cpp 302 | double divide per LFO evaluation | VP x 2.5 to 4 | not optimised | 2 k (0.04 %) | hoist into the tick (bit identical) |
| voice.cpp 426-427 | double divide `kControlPeriod / sampleRate` | VP | inherent | 1 k | keep `dt` in `TickGlobals` |
| lfo.h 181 | `std::cos` (random LFO style 2) | VP per used random LFO | not optimised | 24 x 80 to 200 = 2 k to 5 k (Lorenz) | table at tier low (rounding level) |
| voice.cpp 665, lfo.h 27-28, 64-66, 119, 130 | `ffloor` (inline) | VP | done | small | none |
| voice.cpp 154, 462, 723, 933 | `exp` of a time constant | VP, only with glide / tuning change / pad pressure / remap | cached where it repeats (`:459-463`, `:720-724`) | 0 for the six | none |
| voice.cpp 861, 863 | `log2` (pitch modes 1, 2) | VP per oscillator in that mode | not optimised | 0 for the six | cache on `coarse` |
| voice.cpp 1007 | `std::round` (sub octave, a `roundf` call) | VP | not optimised | 32 x 20 = 0.6 k | `ffloor (x + 0.5)` is not the same for x.5; leave or cache |
| voice.cpp 1103 | `dbToGain` (`powf`) per filter | VP x 2 | not optimised | FILTER-FX F8 | FILTER-FX F8 |
| voice.cpp 1177-1178 | `cos`, `sin` of the filter blend | VP, destination 5 only | not optimised | 0 for the six | OSC "not waste" |
| voice.cpp 1252-1258 (noise) | 6 libm | C | done (OSC F12, `noise_coeff.h`) | 0 | done |
| voice.cpp 1210-1213 | `1.0f / n` per call, divide only for n not a power of two | S | done (OSC F8) | 0 | done |
| voice.cpp 408 | `std::pow (4, ...)` | C (voice control epoch) | cached | 0 | done |
| voice.cpp 400 | `%` (`__aeabi_uidivmod`) | C | cached | 0 | done |
| voice.cpp 191 | `% 8u` (constant: a mask) | N | note start | 0 | none |
| osc_options.h 56-57 | `floor`, `lround` (`snapTranspose`) | VP per oscillator with snap on | not optimised | 0 for the six | cache on (mask, transpose) |
| mod_matrix.cpp 135, 139, 145 | `sqrt` (volume, sqrt level, quart laws) | VP per modulated read | `vsqrt` + errno check | small | K2 removes the check |
| mod_matrix.cpp 151-162 | `log2` / `exp2` laws | VP per modulated read | `fastmath` at tier low | 0 extra | done |
| mod_matrix.cpp 360 | `exp` of the slew time | VP per slot with rise / fall | not optimised | 0 (no factory slot has rise / fall) | cache per slot |
| mod_matrix.cpp 372, 374, 387 | `powerShape` (2 `expm1f`) | VP per slot with power | not optimised | 0 (no factory slot has power) | cache `expm1 (power)` per slot |
| mod_matrix.cpp 73 | `log2` | I | compile | 0 | none |

### 4.5 `engine-own/own_engine.cpp`

| line | what | level | status | cost | fix |
|---|---|---|---|---|---|
| 80-86 via 1601-1602 | `outputLimit`: 2 compares per channel sample; `tanh` only above 1.8 | S | branch cold | 128 x 4 x 5 to 9 = 3 k to 5 k (in N2) | N2 |
| 1536-1542, 1603 | bus and master peaks | S | open | FILTER-FX F10 | F10 |
| 1758-1879 | readout: 90 `floorf`, 10 `log10f`, `expf`, 700 stores | B | not optimised | 5 k to 10 k (0.1 to 0.2 %) | R1 |
| 327-333 | double sums per sample (bus with a bypassed effect) | S, only then | not optimised | 0 for the six (FILTER-FX F20 for FPD) | FILTER-FX F20 |
| 337, 349, 364 | `exp`, `sqrt` (double and float) per bus | P, only then | not optimised | small | cache `k` (constant per period length) |
| 1115 | `std::floor` (call) for 10 global LFO phases | P | not optimised | 40 x 20 = 0.8 k | `ffloor` (bit identical) |
| 1112-1125 | `lfoRateHz` (`exp2` only for key sync) | P x 14 | none needed | 0 for the six | none |
| 1414, 1424-1427 | `modulatedValue`, `cos`, `sin` of the routing | P, only when the mix changed | cached (`mixSame`) | small | done |
| 1456 | `dbToGain` (`powf`) x 3 | P, only when the mix changed | cached | small | done |
| 1535 | `std::sqrt` x 7 | P | `vsqrt` + check | 0.1 k | none |
| 1100, 1103, 1311 | double `dt`, song position | P, B | inherent | under 0.5 k | none |
| 1605 | `% kControlPeriod` (constant) | P | a mask | 0 | none |
| 1290-1392 | about 50 relaxed atomic loads, 2 seq_cst loads, 1 seq_cst store per block | B | fine | under 0.3 k | none |
| 1530-1532, 1150-1152 | loops over the 36 pool voices (`isActive`) | P | fine | small | none |

### 4.6 `engine-own/fx/` (other modules), `fx-serum-a`, `fx-serum-b`

| line | what | level | status | cost | fix |
|---|---|---|---|---|---|
| fx_chain.cpp 50-56, 571, 664 | `finiteBlock`: 2 compares per sample per module | S | not optimised | Lorenz 3 modules: 128 x 6 x 5 to 9 = 4 k to 7 k | N2 |
| fx_chain.cpp 767, 782 | rack peaks at tier low | S | not optimised | 128 x 4 x 5 to 9 = 2.6 k to 4.6 k per rack | N2 |
| fx_effects.h 602 | `p.rate / sr` per sample (Phaser; the member may alias the output) | S | not optimised | 128 x 14 = 1.8 k (Mid Side Halo etc.) | hoist (bit identical, like FILTER-FX F12) |
| fx_effects.h 614-633 | `exp2` (fast at low), 2 `expf` (`OnePole::coefficient`, `fx_dsp.h:171`), `tanf` (`fx_dsp.h:133, 143`) per channel every 8th sample | S / 8 | partly | 2 x 16 x 3 x 80 to 200 = 8 k to 19 k (0.16 to 0.41 %): Mid Side Halo, Triple Fold Reese, Tine Glass Keys, Wah Pressure Clav, Drawbar Organ Keys | `fastmath` exp at tier low (rounding level); the depth 2 `exp2` at :622 is per call constant (bit identical hoist; no factory preset uses depth 2) |
| fx_effects.h 653 | `tanh` of the feedback | S | `fastmath` at low | 0 extra | done |
| fx_effects.h 96, 42 (Delay `pow`, `LoopBand::set`) | per call libm | P | open | FILTER-FX F16 | F16 |
| fx_effects.h 243 | `sin` of the chorus taps | S / 4 | table at low | 0 extra | done |
| fx_effects.h 706-717, 738 | Distortion `pow10`, `exp`, `round`, `sin` | S | `fastmath` at low; constants per sample | FILTER-FX F18 | F18 |
| fx_effects.h 540 | `sin`, `cos` of the mix law | P | per call | small | none |
| fx_eq.h 118-140 | `pow`, `sqrt`, `tan` per band design | P | open | FILTER-FX F11 (3) | F11 |
| fx_eq.h 155 | divide per sample per band | S | open | FILTER-FX F14 (rounding level) | F14 |
| fx_dsp.h 196-214 | biquad design (`cos`, `sin`, `pow`, `sqrt`) | C / P | per configure | small | F11 |
| fx_module.h 110, 118, 124; fx_chain.cpp 318; fx_makeup.cpp (27 hits); fx_modules.cpp 247, 248, 285, 376, 377, 448, 689, 786, 845, 933, 943-947 | makeup models, `dbToGain`, delay / reverb times, keys | P | MK1 done, F11 open | inside "fx: update" (1.4 to 1.8 %) | FILTER-FX F11 |
| fx_reverb.h (22 hits) | FDN modulation, coefficients | S, P | masked at tier low (X) | 0 on Key 61 | none |
| fx-serum-a hyper_dimension.cpp 132, 171, 180-181, 214-215 | `sinf`, `cosf` per voice per sample | S | masked at tier low (X) | 0 at low | FILTER-FX F17 |
| fx-serum-a utility.cpp 68 (`dbToGain`), dsp_utils.h 161-195 (`tan`) | per call, cached on frequency change | P, C | done | small | none |
| fx-serum-a stereo_splitter.cpp 85-86 | `cos`, `sin` per sample | S | used by the splitters only | 0 for the six | hoist the angles if per call constant |
| fx-serum-b bode.cpp, convolve.cpp (40 hits, 5 `idivmod` sites, double `cos`/`sin`/`fmod` per sample in Bode) | XL only (masked at low) | S | X | 0 on Key 61 | FILTER-FX table, CONTENT CV1 to CV4 |
| fx_module.h 81-104, 214 | virtual `configure`, `run`, `makeupPower` | P per active module | fine | under 0.5 k | none |

### 4.7 `engine-own/osc/` (OSC review area), `filters-serum`, `classic_filters.h`, `filter_setup.h`

| line | what | level | status | cost | fix |
|---|---|---|---|---|---|
| wt_osc.h 953, 960 | scalarised compares (`ffloor4`, `sinCycles4`) | S x 2 (oversampled) | not optimised | 20 k to 61 k (0.4 to 1.3 %) on warp presets | N1 |
| wt_osc.h 247, 257-265, 312-325, 361 | `sinf`, `exp2` (double), `expm1f`, `sqrtf`, double divide | VP per oscillator on change | done (OSC F5, frequency cache) | small | done |
| wt_osc.h 527 and `render` | `__divsi3`, `idivmod` | VP per lane | OSC "not waste" | 0.01 % | K1 |
| wt_osc.h 565-579 | `exp2f` in the filter warp stage | S | done (OSC F11) | 0 | done |
| wt_osc.h 715, 805-823, warp.h 107-230 | libm `sin`, `cos`, `tanh`, `atan`, `fmod`, `round`, `sqrt`, `expm1` in warps | S | `fastmath` at tier low for sin / tanh; `atan`, `fmod`, `round`, `expm1`, `sqrt` stay libm in Dist / Quantize / FM Exp warps | 0 for factory presets (no such warps) | OSC review |
| wt_osc.h 476, 991, 1001 | `floor`, `round` | VP, S fallback | fallback branches | 0 | none |
| morph.cpp 34-35, 255-422 | double twiddles, `cos`, `sin`, `pow` | F (per build), I | done (OSC F2, F3) | per build | OSC F1, F9 |
| wavetable_bank.cpp 61-301 | double FFT, `cos`, `sin`, `log2` | I (builder threads) | off the audio thread | 0 | none |
| lib_osc.cpp 69-70, 118, 258-316, 361, 553-582 | pan law, parameter laws, analysis window | VP on change, C, N | done (LU1, SA1) | small | done |
| lib_osc.cpp 694, 717, 850, 878-879 | `floor`, `exp2`, `round` per sample (scan, FM Exp, Quantize) | S | not optimised | 0 for factory presets | CONTENT review, OSC verdict 2 |
| filter_setup.h 30-31 | `exp2 (+-panShift)` per filter | VP x 2 | open | FILTER-FX F8 | F8 |
| filters-serum filter.cpp 441-1538, filter_dsp.h 239-332 | `tanf`, `powf`, `log10f`, `sinf`, `sqrtf` | S held 4 at low, memoised | F1 to F7 done | per FILTER-FX | FILTER-FX |
| classic_filters.h (39 hits) | `tan`, `sqrt`, `pow`, `exp2`, `tanh`, `sincos`, `atan2` | setup per 8 sample piece or per change; `tanhf` per sample only with `math=exact` (X) | memoised (`remember`, `ensureSvf`) | per FILTER-FX | FILTER-FX F13, F14 |

### 4.8 `shapes`, `dsp/fastmath.h`, `dsp/noise_coeff.h`, `voice/note_tuning.h`, shell

| line | what | level | status | cost | fix |
|---|---|---|---|---|---|
| shell output_stage.h 50-62 | 5 compares per sample | S | not optimised | 128 x 5 x 5 to 9 = 3 k to 6 k | N2 |
| shell output_peak.h 23, 25 | `std::fmax` (a `fmaxf` call expected) per sample and channel | S | not optimised | 256 x 10 to 20 = 2.6 k to 5 k | N2 (NEON `vmax`; display only) |
| shell spectrum.h 33-39 | relaxed atomic store per sample | S | fine (plain `str`) | under 0.5 k | none |
| shell synthyboy_mpc.cpp 3053 | `audioMasterGetTime` host call | B | host cost | unknown | none |
| shapes shape.cpp 33-34 | `expm1` x 2 per curved point (custom LFO shapes, the curve tables) | VP per custom LFO evaluation | not optimised | 0 for the six (no custom shapes) | cache `expm1 (p)` per point |
| shapes shape.cpp 193 | `cos` (smooth step) | VP per custom shape evaluation | not optimised | 0 for the six | none |
| fastmath.h 66 | double `sin` | I (table) | prepare | 0 | none |
| noise_coeff.h 27-33 | 6 libm | C | cached (OSC F12) | 0 | done |
| note_tuning.h 79-134 | double `log2` | C (tuning change) | per change, or per block with a live MTS-ESP source | 0 for the six | none |

`std::function`: none on the audio path (only `offline_render.cpp:44`, not in the MPC shell, and the multisample file reader at load). Virtual calls: `IEngine::process` once per block, `FxModule` per module per period; under 1 k cycles per block. Atomics: section 4.5; the voice path has none.

## 5. Allocation and locking on the audio thread

Method: reachability in the arm32 call graph from `OwnEngine::process`, every `FxModule::process`, every module `run` and every `process (float*, float*, int)`, excluding the error paths (`__assert_fail`, `__cxa_throw`) and the helper thread jobs; then every hit read in the source.

- **Steady state: no allocation, no lock, no system call.** What the graph reaches are first use initialisers: `destLawOf` (`static const std::vector<DestLaw>`, `mod_matrix.cpp:114`), `libKeys` (`snprintf` at first call, `lib_osc.cpp:86-105`), `SincTables::instance` (futex of the guard), `findParamIndex` statics in `publishStats` and `Voice::tick` (`own_engine.cpp:1667`, `voice.cpp:1042`). `prepare ()` warms `paramIndex`, `fxIndex`, `destLawOf` (`own_engine.cpp:404-406`) and `SincTables` (`sample_voice.cpp:463`), so their first call is not on the audio thread. `libKeys` is not warmed: its first call (in the first tick) runs `snprintf` and hashing on the audio thread once per process. Cost after warm up: a guard load and `dmb ish` per call (V3).
- **Growth paths**: `scratchRight.assign` in `process` only for a block larger than `prepare` announced (`own_engine.cpp:1281-1282`, documented).
- **Convolve**: the audio thread only exchanges atomic pointers (`fx_modules.cpp:984-986`); `new fx::Convolve` and the `delete` of retired ones run in the `FxWorker` job (`:1002, 1018-1020`). `FxWorker::submit` calls `cv.notify_one ()` from the audio thread without holding the mutex (`:52`): a futex wake system call when the worker waits, a rare event (an impulse or key change).
- **Effect type changes**: built on `OwnEngine::fxWorker` and handed over by atomics (`own_engine.cpp:257-275`, FILTER-FX verdict); offline (no worker) they are built on the calling thread.
- **Content**: `publishContent` takes `contentMutex` on the message thread; the audio thread announces its set with a seq_cst hazard pointer loop (`own_engine.cpp:1352-1360`), no lock.
- **Shell**: `processReplacing` (`synthyboy_mpc.cpp:3000-3120`) takes events from a lock free inbox, calls the host for the time info, the engine, the output stage, the peak and the spectrum ring (atomic stores into a preallocated ring). `programMutex`, `chunkMutex`, `contentMutex`, `lfoShapeMutex` are documented as never taken by the audio thread (`:350-351`); I found no path from `processReplacing` to them.
- **Threads that render audio**: only the host's audio thread (`processReplacing`). Helper threads: `OwnEngine::fxWorker` (module construction), `FxWorker` (Convolve rebuild and spectrum), the built in wavetable bank builder and up to 3 helpers (`wavetable_bank.cpp:456-508`), the shell's LFO display thread (`synthyboy_mpc.cpp:1531`), the overlay service thread (`overlay/service.h:110`), the statistics writer (`callback_stats.h:437`), the log writer (`kit/log.h:121`). The shell's threads set their policy (`thread_policy.h`); the engine's do not (T1). Offline rendering (`core/src/offline_render.cpp`) is used by the CLAP shell only.

## 6. Denormals

| thread | ARM 32 | ARM 64 | x86 | where |
|---|---|---|---|---|
| plugin audio callback | FPSCR.FZ (bit 24) set at the start of every block, restored at the end | FPCR.FZ set, restored | FTZ and DAZ | `synthyboy_mpc.cpp:181-230`, `:3046-3047`, `:3119` |
| Vital engine (not in the MPC package) | FZ | FZ | | `vital_engine.cpp:83-102` |
| `synthyboy_tier_bench` (timing and `--verify`) | FZ on by default (`--no-ftz` clears it), set once on the rendering main thread | FZ | FTZ and DAZ | `tier_bench.cpp:200-218`, `:829`, `:954` |
| `synthyboy_bench` | off by default, `--ftz` | | | `bench_engine.cpp:119-149` |
| `synthyboy_preset_render`, offline render (CLAP) | not set | not set | not set | no FZ code in the tool or `offline_render.cpp` |
| helper threads (FxWorker jobs, module construction, bank builders, content analysis, display) | not set | not set | | they produce data (impulse spectra, tables, analysis), not the audio output |

On ARMv7 the NEON unit always flushes subnormals whatever FZ says; with FZ set the VFP scalar code flushes too, so the vector and scalar forms agree on the device. No thread that renders the audio lacks the setting. The helper threads should NOT get it: their results feed the audio as data, so FZ there would change the bits of those tables; their own speed only affects the time to ready. The owner's `--no-ftz` measurement (+1.5 to 2 points) confirms that denormals are not a driver. One consistency note: `preset_render` runs without FZ, the plugin with it; this does not matter for a two build compare (both sides alike), but its output is not the device's output in denormal ranges (the NEON paths flush in both).

## 7. Memory layout and cache footprint

Per voice, arm32, 44.1 kHz (allocated sizes; what one period touches is in the last column):

| part | size | where | touched per voice period (est.) |
|---|---|---|---|
| `Voice` object | 33 128 B | `operator new (0x8168)` at 0x48a8c in `OwnEngine::prepare` | 8 to 15 KB (tick state, `src` / `srcR` 58 floats each, 64 `SlotState`, ticks, the period buffers `oscMono`, `extMono`, filter states) |
| `mod`, `modR` | 2 x 2637 floats = 21.1 KB | `voice.cpp:92-93` | about 165 lines of `mod` (sparse reads, MEM2), few of `modR` |
| shared `base` | 10.5 KB per engine | `own_engine.cpp:417` | the same lines for every voice |
| `WtOsc` morph buffers | 3 x (32.8 KB `morphBuf` + 24.6 KB `MorphScratch` + 8.1 KB decimator `history`) = 196 KB | `wt_osc.h:515-524`, `voice.cpp:100` | 0 without morph; a morph read touches 2 cycles of 8 KB per group (OSC F9); a build touches the scratch (MEM1) |
| Serum filter lines | 4 x 66.4 KB = 266 KB (comb 2 x 2714, diffuser 3582, reverb comb 6773, all pass 824 floats per channel) | `filter.cpp:39-60` (my count; FILTER-FX says about 53 KB) | only the model's lines: Allpasses (Lorenz F1) 6 lines of 72 to 597 floats per channel; Flanges (Dusk F2) the 2714 float combs |
| `LibOsc` (content) | not counted here | CONTENT review | spectral voices about 37 KB each x 4 per oscillator |
| wavetable reads (shared tables) | a mip level cycle of up to 2048 floats per frame | `wavetable_bank` | 12 to 50 lines per lane period (32 samples at the lane's increment), plus the second frame when the frame moves |

The pool is 36 voices (`own_engine.h:85`): about 18 MB of voice memory is allocated in `prepare ()`; only the touched part costs time.

Working set per block, est. Lorenz Garden (6 voices): per voice period 8 to 15 KB of `Voice`, about 10 KB of `mod`, 6 to 20 KB of table reads (7 lanes), 2 to 4 KB of filter lines and buffers: 30 to 50 KB, larger than the 32 KB L1D. So the L1 is turned over at least once per voice per period, and each voice's state comes back from L2 in every period: 500 to 800 lines x 15 to 25 cycles = 8 k to 20 k cycles per voice period if nothing overlaps (the A17 runs out of order and has a prefetcher, so less). Over a block: 6 x (30 to 50 KB) plus `base`, the chorus store (256 KB, 16 taps around a write point: a few KB touched per period) and the effect states: 250 to 450 KB, inside a 1 MB L2 (RK3288 L2 size from the datasheet, not read from the device) that the MPC application's other threads share. Stretched Glass Choir and Shepard Ascent add the morph cycles (128 KB per chord for the reads, OSC F9) and the scratches (MEM1). Spectral Vowel Freeze streams its 4.35 MB analysis (CONTENT), DRAM bound by nature.

- **Interleaved or planar**: buffers are planar (`l`, `r` arrays) everywhere; the pair kernels load L and R into the two lanes of a D register. The chorus store is one line per tap (FILTER-FX F12 second step proposes interleaving the 16 taps: one 64 byte store per sample instead of 16). No other layout change is indicated.
- **Struct of arrays against array of structs**: the voice state is an array of structs (one `Voice` per voice), which keeps each voice's period work local; the oscillator lanes are structs of arrays (`prevInc [kMaxVoices]`, `curGainL [kMaxVoices]`, `wt_osc.h:1209-1242`), good for the NEON lane loops. MEM3 is the only SoA option across voices, not proposed now.
- **False sharing**: none between voices (one audio thread renders all voices; the `Voice` objects are separate heap blocks). The display atomics (`live*`, written each block, read by the display thread at 10 to 25 Hz) cost a coherence miss per line after each display read: negligible.
- **Alignment**: section 2; not ranked.
- **Biggest streaming loops**: the wavetable read (sequential within a lane, gathers of 4 lanes), the morph read (two 8 KB cycles per group), the Serum delay lines (a write cursor sweeping the whole ring: the Flanges combs 10.9 KB per line, revisited every 61 ms, likely an L2 or DRAM refill per new line: 6 voices x 2 channels x 2 combs x 8 lines per block = 192 refills, 3 k to 29 k cycles, up to 0.6 % on Parallel Dusk Pad, est.), the spectral analysis frames (DRAM).

**What must be measured**: none of this has a miss counter behind it. The A17 PMU has L1D refill (event 0x03), L1D access (0x04), L2D refill (0x17), L2D access (0x16). Propose a `--pmu` option in `tier_bench` with `perf_event_open` per section (first check `/proc/sys/kernel/perf_event_paranoid` and whether the device kernel has `CONFIG_PERF_EVENTS`). Without it, MEM1, MEM2 and the delay line refills stay estimates.

## 8. Compile flags

### 8.1 What is built today (read from the scripts and the round 12 build tree)

| target | arch | flags | source |
|---|---|---|---|
| plugin `synthyboy_mpc.so` | arm32 | `-march=armv7-a -mfpu=neon -mfloat-abi=hard -O3 -DNDEBUG -std=c++17 -fPIC`, module `-fvisibility=hidden -fvisibility-inlines-hidden`, link `--version-script`, `--exclude-libs,ALL`, `--gc-sections` (without `-ffunction-sections`, so it removes little), `--as-needed`, `-static-libstdc++ -static-libgcc`; libm from the device (glibc 2.39) through the PLT | `cmake/toolchains/armv7-linux-gnueabihf.cmake:16`, `scripts/mpc-container-build.sh:104-107`, `shell-mpc/CMakeLists.txt:14-20, 218-231`, `core/CMakeLists.txt:59`, `CMakeLists.txt:36-38` |
| plugin | arm64 | `-march=armv8-a -O3 -DNDEBUG -std=c++17 -fPIC`, the same link options | `cmake/toolchains/aarch64-linux-gnu.cmake:17` |
| `synthyboy_tier_bench` | arm32 / arm64 | the same compile flags (core still `-fPIC`), `-static` (`SYNTHYBOY_TIER_BENCH_STATIC=ON`); libm statically from Debian 12 (glibc 2.36); `--sections` builds add `-DSB_SECTION_TIMING=1` | `scripts/build-tier-bench.sh:77-80`, `bench/CMakeLists.txt:30-33`, `round12/bench-build/arm32/CMakeCache.txt` |
| `synthyboy_bench`, `synthyboy_noteon_bench` | both | the same, `-static-libstdc++ -static-libgcc` | `scripts/mpc-container-build.sh:140-144` |
| tests | both (and the Mac) | the same; seven tests add `-ffp-contract=off` | `core/tests/CMakeLists.txt:70-125`, `core/filters-serum/tests/CMakeLists.txt:45` |
| contraction | arm32: moot (no FMA in VFPv3, `vmla` is not fused); arm64: GCC fuses outside the `fp-contract=off` pragma | pragma in `dsp/simd.h:30-32` (ARM Linux, every TU after the include), `filter.cpp:10`, `spectral.cpp:16-18`, `kernels.cpp:19-21`, `granular.cpp:20-22`, `sample_voice.cpp:20-22` | section 2 |

GCC 12.2 (Debian 12 container). No `-mcpu`, `-mtune`, LTO, PGO, `-fno-math-errno`, `-ffast-math`. The bench uses glibc 2.36's libm statically, the plugin the device's 2.39 dynamically: the two can differ in libm speed and, rarely, in a last bit; a bench result is not a bit proof for the plugin.

### 8.2 What each flag would change here, and the bits

| flag | what it does in this code | bits on arm32 | bits on arm64 | expected gain |
|---|---|---|---|---|
| `-mcpu=cortex-a17` (replacing `-march=armv7-a`), `-mfpu=neon` kept | `sdiv` / `udiv` instead of the library (M1 sites, `WtOsc`, `LibOsc::update`, spectral `shapeFrame`, morph `inverseFft`); A17 scheduling and costs | same: no float operation changes; GCC still does not vectorise floats without `-funsafe-math-optimizations` | n/a | M1 presets 6 to 11 % if M1 is not done; else unknown, 0 to 3 % (scheduling) |
| `-mfpu=neon-vfpv4` (or `-mcpu=cortex-a17` without an explicit `-mfpu`, whose default FPU is VFPv4) | allows `vfma`; GCC then contracts `a * b + c` in every function outside the pragma | **changes the bits** (one rounding instead of two) in `mod_matrix.cpp`, `fx_makeup.cpp`, `envelope.h`, `fx-serum-a`, `multisample`, `own_engine.cpp` parts before the pragma | n/a | none by itself; do not use without `-ffp-contract=off` |
| `-ffp-contract=off` (global) | no contraction anywhere | same (nothing to contract) | **changes the arm64 bits once** (the fused functions become unfused), then makes arm64 safe for K1 to K5 | 0 |
| `-fno-math-errno` | libm calls pure: CSE (for example `expm1 (power)` twice in a stereo slot, `exp2` of the L and R smoothing), loop invariant hoisting, `vsqrt` without the check and fallback | same values; the only risk is compile time folding of a call that now has a constant argument (see LTO) | same caveat | 0 to 0.5 % (most invariants are hoisted by hand) |
| `-fno-trapping-math` | more if-conversion of float code | same | same | small |
| `-fno-semantic-interposition` (or hidden visibility for the core) | inlining and IPA of the core's global functions in their own TU (`Voice::modulatedSlow`, `Voice::P` calls), PC relative access to hidden globals | same on arm32 | may change where GCC fuses (more inlining across the pragma boundary) | 0 to 3 % |
| LTO (`-flto`, `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`) | cross TU inlining (`paramTable`, `destLawOf`, `modulatedValue`, the filter library) | same arithmetic; compile time folding of libm with constants (MPFR, correctly rounded) can differ from glibc by an ulp | the same plus contraction changes | unknown, 1 to 5 % |
| PGO | layout, branch weights, value profiled division and switch | same arithmetic, same folding caveat | same caveats | unknown, 0 to 5 %; setup heavy |
| `-ffast-math` / `-funsafe-math-optimizations` | NEON auto vectorisation of float loops, reassociation, reciprocal approximations | **changes the bits** | changes | not proposed |

Why the "same" entries hold: on arm32 every float result is an IEEE single or double operation or a libm call; GCC without `-funsafe-math-optimizations` keeps the order of operations, and a fused operation does not exist on VFPv3 with NEON. Scheduling, inlining, register allocation and integer instruction choice do not change a float result. What can change it: (1) contraction to `vfma` (only with VFPv4 enabled); (2) a libm call folded at compile time once its argument becomes a constant; (3) a different libm. These are the things the render compare must catch.

### 8.3 What pins the output today, and what a flag change needs

- `synthyboy_preset_render --engine own --tier low|medium|high --wav --chord-wav --content-dir <store>` over the 160 presets, two builds, `diff -rq` (round 12 entry of `docs/MPC.md`). This is the only cross build check. It writes 24 bit PCM (K6), and the documents do not say it was run on the arm32 GCC build.
- `synthyboy_tier_bench --verify`: one binary, savings on against off, float equality (`tier_bench.cpp:1012`). It cannot see a flag change.
- The lockstep unit tests (`synthyboy_test_own_wt_osc_opt`, `..._own_morph_transform`, `test_filter_pair`, `..._neon_cmp`, `..._own_filter_unit_lanes`, `..._own_mix_lanes`, `..._own_warp_lanes`): one binary, old against new path; built with `-ffp-contract=off` themselves.
- `scripts/content-kernel-compare.py`: one binary, `--scalar-content` on against off.
- `synthyboy_preset_sanity`: level and finiteness checks, not bits.

So flags are safe only after a two build float compare of the arm32 GCC binaries (qemu or the device). `-fno-math-errno` and `-mcpu=cortex-a17` with `-mfpu=neon` kept are expected to pass; LTO and PGO are expected to pass with a small folding risk; anything with VFPv4 without `-ffp-contract=off` will fail.

### 8.4 The experiment for a runner

Heavy builds through `bash /Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh`, `--jobs 4`, one variant at a time, a fresh build directory per variant.

1. Variants (arm32), each as the `cmake` line of `scripts/build-tier-bench.sh:77-79` plus `-DCMAKE_CXX_FLAGS="<flags>" -DCMAKE_C_FLAGS="<flags>"` on a fresh tree (this replaces the toolchain's `CMAKE_CXX_FLAGS_INIT`):
   - A (today): `-march=armv7-a -mfpu=neon -mfloat-abi=hard`
   - B: `-mcpu=cortex-a17 -mfpu=neon -mfloat-abi=hard`
   - C: B plus `-fno-math-errno -fno-trapping-math`
   - D: C plus `-fno-semantic-interposition`
   - E: D plus `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` (LTO)
   - optional F: E with PGO (`-fprofile-generate -fprofile-update=single` build, a run of `--all-presets --seconds 4` on the Key 61, the `.gcda` files copied back, `-fprofile-use -fprofile-partial-training -Wno-missing-profile`).
   Targets: `synthyboy_tier_bench` (static) and `synthyboy_preset_render` (add `-DCMAKE_EXE_LINKER_FLAGS=-static`).
2. Bit identity, before any timing: a local, uncommitted change of `preset_render.cpp:427, 445` to `writeWav (..., 32, true)` (or a hash per render), then for each variant `qemu-arm ./synthyboy_preset_render --presets content/presets --engine own --tier all --wav out-<X>/single --chord-wav out-<X>/chord --content-dir <store>` and `diff -rq out-A out-<X>`. Expected: B, C, D identical; E identical unless a folding difference appears (then find the preset and the call). Spot check one tier on the Key 61 with the same binaries (qemu's float emulation is exact for IEEE operations, but the device is the truth).
3. Timing on the Key 61 (the runner who holds the device), each binary on core 3, interleaved A, B, A, C, A, D, A, E to cancel drift:
   - `synthyboy_tier_bench --presets content/presets --core 3 --config low --budget 70 --all-presets --seconds 4 --repeat 3 --csv k61-<X>.csv`
   - targeted: `--load "Pentatonic Step Pluck" --load "Karplus Harp" --load "Detonation Tail" --load "Lorenz Garden" --load "Stretched Glass Choir" --load "Triple Fifth Lead" --load "Spectral Vowel Freeze" --load "Bowed MPE Strings" --load "Frozen Pluck Drone" --load "Mid Side Halo" --seconds 10 --repeat 3`
   - `--verify` with each variant binary (it must still report the same samples within itself).
   - For the variant that wins, a `--sections` build (`build-tier-bench.sh --sections` with the same flags) on the six heavy presets to see where the time moved.
4. arm64 (XL) only if wanted: first decide on `-ffp-contract=off` (a one time change of the arm64 bits), then the same ladder with `-mcpu=cortex-a76` or `-mtune=cortex-a76` (the XL has A55 and A76 cores; `-mcpu` would bind the binary to the A76 features, so `-mtune` is the safe one).

## 9. The weak review (`agy-g79-review-memory-deep/docs/release/CPU-REVIEW-MEMORY.md`): verdicts

| claim | verdict | why |
|---|---|---|
| M1: `ConvolveModule::runJob` `new fx::Convolve ()` on a helper thread, acceptable | confirmed | the allocation and the deletes run in the `FxWorker` job (`fx_modules.cpp:1002, 1018-1020`); the audio thread only swaps atomic pointers (`:984-986`). Missed: the worker polls every 2 ms and keeps its creator's scheduling (T1) |
| M1: "placement new in `WtOsc` at line 502, not a heap allocation" | confirmed | `wt_osc.h:502` in `clearState` (per note, in place); the buffers are moved out and back, no allocation |
| M2: `outputLimit` calls `std::tanh` on every sample; a polynomial saves 3 to 5 % | refuted | `own_engine.cpp:80-86`: `tanh` runs only when `|y| > 1.8` (a limiter knee above the normal range); the 4 `bl tanhf` in `OwnEngine::process` are the cold branch. The per sample cost is two compares per channel (est. 0.1 %, part of N2). Replacing `tanh` would also change the sound of a clipping patch |
| M3: the `Voice` is large and exceeds the 32 KB L1D | confirmed in size, unquantified | `sizeof (Voice)` = 33 128 B (binary), plus about 0.5 MB of heap per voice. But the per sample loops touch little of it; the misses, if any, are in the per period parameter reads and table reads (section 7). No saving can be attached without a miss counter |
| "build flags: `-Wall -Wextra -Wpedantic`; optimisation flags managed by the container" | partly | the optimisation flags come from the toolchain file and `CMAKE_BUILD_TYPE=Release` (section 8.1), not from `build-mpc.sh` |
| what it missed | | M1 (the limiter, the largest item here), N1, V1 to V3, MEM1, K6 |

## 10. Ranking: (saving x certainty) / risk, sound neutral first

Savings are percent of the 2.902 ms block on the Key 61 `low`, est., per preset named. Items that keep every output (audio and display) come first, then items that keep the audio but change a display (V2b), then the flags whose identity must still be proved.

| rank | ID | saving | output | certainty | risk | change in one line |
|---|---|---|---|---|---|---|
| 1 | M1 | 9.3 to 14.9 (+0.9 to 2.0 NEON min) on Pentatonic Step Pluck, Karplus Harp, Detonation Tail | bit identical | high (mechanism), medium (size) | low | wrap the limiter indices instead of `% cap`; NEON min of the look-ahead window |
| 2 | N1 | SVF / SGP 1.3 to 2.3, SCM 1.0 to 1.7, BMS 1.3 to 3.9, FPD 1.6 to 4.9 (CCP, GGS less), warp presets 0.4 to 1.3 | bit identical | high (mechanism), medium (size) | low | the remaining `vc*q_f32` sites through `neon_cmp.h` bit pattern helpers |
| 3 | V1 | Lorenz 0.5 to 1.1, Triple Fifth 0.7 to 1.5, Dusk 0.5 to 1.1, Stretched and Ring Mod 0.6 to 1.2, Shepard 0.3 to 0.7 | bit identical | medium | low | envelope settings cached per envelope unless one of its keys is modulated |
| 4 | V3 | Lorenz 0.9 to 2.1, Triple Fifth 1.2 to 2.5 (overlaps V1) | bit identical | medium | low | `P ()` with an integer zero test, always inline; guard free pointers to the law table and the index in `TickGlobals` |
| 5 | V2a | 0.1 to 0.45 | bit identical | high | very low | cache `1 - exp2 (-dt / half)` of the LFO smoothing |
| 6 | N2 | 0.3 to 0.6 on every preset | audio bit identical | medium | very low | NEON finite tests and peaks in the chain, the master stage and the shell |
| 7 | K1 | M1 presets 6 to 11 (if M1 is not done); others 0 to 3 | expected bit identical, prove with 8.4 | medium (divide), low (rest) | low | `-mcpu=cortex-a17 -mfpu=neon` |
| 8 | K3 | 0 to 3 | expected bit identical on arm32 | low | low | `-fno-semantic-interposition` (or hidden visibility) for the core |
| 9 | K2 | 0 to 0.5 | expected bit identical | medium | low | `-fno-math-errno -fno-trapping-math` |
| 10 | R1 | 0.1 to 0.2 | audio bit identical (`ffloor` part bit identical, the throttle display only) | medium | very low | readout with `ffloor`, every fourth block |
| 11 | V1 add on | 0.06 to 0.15 | bit identical | high | very low | cache `expm1 (power)` per envelope stage |
| 12 | MEM1 | Stretched Glass, Shepard 0.3 to 2.5 mean, 1 to 10 points of the rebuild block | bit identical | low | low | one morph scratch per engine |
| 13 | V4 | 0.4 to 1.2 | bit identical | low | low | a fast loop for plain matrix slots |
| 14 | K4 | unknown, 1 to 5 | arithmetic identical, folding risk | low | medium | LTO |
| 15 | T1 | unknown (0 if the threads are not on the audio core) | audio bit identical | low | low | helper threads wait without a timeout and get the shell's normal scheduling |
| 16 | V2b | Lorenz 0.4 to 1.0, Triple Fifth and Ring Mod 0.6 to 1.3, Stretched 0.5 to 1.2 | audio bit identical, display differs briefly | medium | low | unused LFOs: phase only, value only in the displayed voice |
| 17 | K5 | unknown, 0 to 5 | arithmetic identical, folding risk | low | medium (cross build PGO) | PGO |
| 18 | MEM2, MEM3 | unknown | bit identical | low | medium to high | compact parameter snapshot; two voices per Q register |
| prerequisite | K6 | none | | | none | float WAV or a hash in `preset_render` before any flag is changed |

Rough sum of the bit identical code items for the heavy six (ranks 3 to 6, 11, 13; overlaps removed by taking the low ends of V3 and V1 together): Lorenz Garden 1.5 to 3.5 points, Triple Fifth Lead 2 to 4, Stretched Glass Choir 1.5 to 3.5 (plus MEM1 at p99), Ring Mod Gamelan 1.5 to 3.5, Parallel Dusk Pad 1.3 to 3, Shepard Ascent 0.8 to 2. The limiter presets gain far more from M1 alone; the content presets from N1.

## 11. What I could not verify

- The cycle cost of a scalarised lane compare, of a libm call, of `__aeabi_idivmod` and of `sdiv` on the A17: all estimates. M1's size needs `--sections` on the three limit presets; N1 needs `--sections` on SVF, BMS, FPD.
- Cache misses: no PMU counter has ever been read on the device; section 7 is arithmetic on sizes. Whether the kernel exposes `perf_event_open` is unknown.
- Which thread creates `OwnEngine::fxWorker` and `FxWorker` on the device, and so their policy and core (T1).
- That `-mcpu=cortex-a17` without `-mfpu` defaults to VFPv4 in Debian's GCC 12 (I reason from GCC's CPU table; the experiment keeps `-mfpu=neon` explicit, so it does not matter).
- Whether `std::fmax` in `output_peak.h` is a `fmaxf` call in the plugin binary (the shell is not in the bench binary; expected on ARMv7).
- The platform of the round 12 render proof (K6).
- The RK3288 L2 size (1 MB from the datasheet, not read from the device).
- The share of `P ()` and its misses in the measured voice tick sections (V1, V3 sizes).
- The observation in V1 about the envelope cache and legato voice control steps (read, not run).

## 12. Status on branch `work/opt-misc`

| Finding | Status | Commit |
|---|---|---|
| M1 | done: indices wrapped without `%`, window as two contiguous runs, NEON min when no NaN / infinity / subnormal / -0 is in the ring (else the scalar loop), sum in the old order; `Limiter::reference` keeps the old code. Test `synthyboy_test_own_compressor_opt`. The three presets render byte for byte the same (float WAV + hash, tiers low / medium / high, single and chord) | 42c91fad |
| K6 | done: `--float32`, `--hash`, `scripts/compare-renders.sh`, test `synthyboy_test_preset_render_output`, documented in `docs/MPC.md` | (this commit) |
| N1 | partly done: the sites of the sample oscillator (`sample_voice.cpp` `equalPower4` `theta > pi/4` and `sanitize4`), the grain window half test and the output clamp of the granular oscillator now use `neon_cmp.h` (new helpers `gtConst`, `absGeConstFinite`; test `synthyboy_test_neon_cmp`: all 2^32 patterns, IEEE and flush to zero, plus the whole `sanitize4` expression). Left for the round that owns the files: `core/spectral/src/kernels.cpp` 267, 298, 310-313, 540, 660, 699 and `core/engine-own/osc/wt_osc.h` 953, 960. (`engine-vital/upstream` `poly_values.h` is third party code, not touched.) | (this commit) |
| T1 | done in the code, unverified on the device: `OwnEngine::fxWorker` (`sb-fx-build`) and `FxWorker` (`sb-fx-job`) wait on a `WakeSignal` (`core/engine-own/dsp/helper_thread.h`: futex on Linux, mutex + condition variable elsewhere; notify is lock free on Linux, no timeout, no lost wake-up, a stop notifies) instead of waking every 2 ms, and call the shell's start hook first thing (`core/include/synthyboy/helper_thread_hook.h`, set at the start of `VSTPluginMain` in `shell-mpc/synthyboy_mpc.cpp` to `useNormalScheduling (0, name)` plus the cores of `planOwnCpus ()`: not the isolated cores, not the cores the audio threads are pinned to). Offline rendering is unchanged (it waits for the Convolve spectrum job by spinning on the busy flag). Test `synthyboy_test_own_helper_thread`. Which thread creates the two threads on the device is still not observed: `ps -eLo tid,psr,cls,rtprio,comm` while the plugin plays must show `sb-fx-build` / `sb-fx-job` with class TS and a psr off the isolated cores. The shell file was not compiled on the Mac (it needs the MPC SDK and Linux). | (this commit) |
