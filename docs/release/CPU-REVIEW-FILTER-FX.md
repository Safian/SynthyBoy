# CPU review: voice filters and the effects chain (Key 61 first)

Read only review of `feature/mpc-overlay` at 152b4a45 (2026-11-18). No source file was changed, nothing was built or run, no device was touched. Every line number below was re-read in that tree.


## 0. Status on `work/opt-filters` (2026-11-18)

Device result (round 12, all three branches merged in d7cbc890, tree proved bit identical to 152b4a45 on all 160 presets x 3 tiers; `.hw-local/mpc-perf/round12/`, summary in the round 12 entry of `docs/MPC.md`): Key 61 `--sections`: Lorenz Garden filter 1 0.989 -> 0.330 ms, Parallel Dusk Pad filter 2 1.153 -> 0.303 ms, Ring Mod Gamelan filters 1.337 -> 0.557 ms, Triple Fifth Lead filter 1 0.914 -> 0.399 ms, chorus 0.05 ms cheaper on every preset that has it; `--verify` with and without the vector filter code gives identical samples on both devices (Scream, EMS, MG, Diffusor, Smear, Multi SVF, Flanger, Ring Mod loads).


Implemented on `work/opt-filters` (from `feature/mpc-overlay` 152b4a45), every change bit identical and tested against the scalar reference (the tests, the harnesses and the measurement method are in the commit messages). "Instructions" are executed instructions per stereo sample of the filter (both channels) on 32 bit ARM, GCC 12 with the bench flags, counted under qemu (hold 4, a cutoff that moves every block); the Key 61 estimate weights them with the model of section 1 (a `vmrs` 2 to 8 extra cycles, a division 14; a fit to the measured shares of Multi SVF, Flanges and Allpasses puts the `vmrs` near the low end) and applies the fraction to the measured share. The device bench decides (section 10).

| ID | Status | Commit | Instructions (vmrs) before -> after | Key 61 estimate |
|---|---|---|---|---|
| F2 | done | 3d1781a6 | Scream 644 (41) -> 381 (5), EMS Ladder 690 (43) -> 401 (4), Diffusor 831 (47) -> 586 (8); chorus 1470 (59) -> 1120 (3) with F12; Digital loop: static `vmrs` 24 -> 12 | the existing kernels 34 to 51 % of their cost (Scream Filter Bass about 6 to 9 points); chorus see F12; Digital loop as section 5 (2.2 to 3.5) |
| F3 | done | e6c6e2fd | Allpasses 1458 (116) -> 783 (11) | 53 to 63 % of the filter: Lorenz Garden 18 to 22 points |
| F1 | done (all twelve Multi SVF styles) | 51e77b08 | Multi SVF LP 1127 (125) -> 463 (6) | 61 to 72 %: Triple Fifth Lead 19 to 23, Ring Mod Gamelan F2 19 to 22, Shepard Ascent 10 to 12 points |
| F4 | done, steps A and B in the kernel (all nine Flanges styles) | b1e6490b | Flg HL 1731 (98) -> 547 (7) | 69 to 74 %: Parallel Dusk Pad 27 to 29 points |
| F6 | done | f7ad3363 | the loops after the filter 52 -> 16 per stereo sample (about 6 `vmrs` -> 0) | about 1.1 to 1.8 points per Serum filter at 8 voices |
| F7 | done (plus `sanitize ()` on the bit pattern, and the memos forced inline: fbb8d821) | 616b452b | French LP 1157 (114) -> 784 (25), Add Bass 801 (90) -> 598 (17), Normal Low 24 1003 -> 780, Formant I 1803 -> 1404 | 37 to 49 % of the plain path types |
| F5a | done (with the Ring Mod case in the kernel: the phase steps per held cutoff) | 318705f6 | Ring Mod, pan 0: 598 (44) -> 265 (1); sides apart 598 -> 332 | 61 to 70 %: Ring Mod Gamelan F1 9 to 10.5 points |
| F12 | done (both steps of the first part; the interleaved store of the second part not done) | 26a424d5 | chorus 1470 (59) -> 1248 with F2 and the hoisted quotient -> 1120 (3) with the lane refresh | 29 to 41 % of the chorus: 1.9 to 2.7 points on the chorus presets |
| F11 | skipped | (none) | measured: in all six presets the Bus 1 and Bus 2 racks already take the existing early exit (12 full updates in 8264 periods); Main is active every period, so no early exit applies; the work is the Main rack's own update, the caching sub-items (2) to (4), not in this round | none from an early exit |
| F10 | done on `work/opt-shell` (`dsp/peak.h`: `peakAbs` / `peakAbsPair`; the strip, bus, master and effect chain peaks; the NaN semantics are those of the old `std::max` loops, not of `vmaxq_f32`: a NaN is passed over, tested on all special values in `core/tests/test_own_peak.cpp`) | 62e404f6 | scalar compare loop with a `vmrs` per sample -> one vector max per four samples | with PROFILE S3 (eb5e24e8) the source peaks are not run at all while the Mixer page is closed |
| F5b, F8, F9, F13 to F20 | not in this round | | | |

## 1. Sources, method, constants

**Measured figures used (cited, not estimated).**

- Section shares of the 2.902 ms block per preset, Key 61 `low`, round 10: `.hw-local/mpc-perf/round10/k61-breakdown/sections.csv` and `sections-by-category.csv` (voices as measured: Lorenz Garden 6, Stretched Glass Choir 8, Parallel Dusk Pad 6, Ring Mod Gamelan 8, Shepard Ascent 4, Triple Fifth Lead 8).
- Switch-off variants: `round10/k61-breakdown/variants.csv` (for example Lorenz Garden median 85.22 % with everything, 44.40 % with `filter1off`; Parallel Dusk Pad 78.12 % and 32.35 % with `filter2off`; Ring Mod Gamelan 73.86 %, 59.28 % `filter1off`, 36.66 % `filter2off`; Triple Fifth Lead 73.07 % and 34.57 % `filter1off`).
- Profile ladder: `round10/k61-breakdown/configs.csv` (`low` against `low,fx=all`).
- Filter model bench: `round10/k61-breakdown/raw/filters.txt`.
- Pair kernel before and after: `.hw-local/mpc-perf/tier-bench/k61-r2-before.txt` (`--scalar-filters --no-fx-mute`) and `k61-r2-after.txt`: Diffusor Cloud Pad median 72.2 % to 37.8 %, Scream Filter Bass 67.0 % to 36.9 %, EMS Squelch Bass 63.0 % to 34.5 % (8 voices; part of the drop is the effect mute that the "before" run also switched off).
- Round 10 totals: `round10/k61-r10-tables.md` (Lorenz Garden median 96.08, p99 106.83; Stretched Glass Choir 65.79 / 101.32; Parallel Dusk Pad 88.20 / 97.16; Ring Mod Gamelan 84.58 / 95.22; Shepard Ascent 62.06 / 94.02; Triple Fifth Lead 83.47 / 93.82).
- CPU: `round10/k61-breakdown/platform-facts-k61.txt` (part 0xc0d, Cortex-A17, VFPv4 and NEON present, 1.608 GHz, `isolcpus=2-3`).

**The arm32 build and its consequences (checked in the real build tree, not assumed).** `round10/bench-build/arm32/CMakeCache.txt` and every `flags.make`: `-march=armv7-a -mfpu=neon -mfloat-abi=hard -O3 -DNDEBUG`, GCC 12 (`scripts/build-mpc.sh` line 35, Debian 12 toolchain), toolchain file `cmake/toolchains/armv7-linux-gnueabihf.cmake` line 16. No `-ffast-math`, no `-fno-math-errno`, no `-mtune`. I disassembled the bench build's static libraries with `objdump` (`core/libsynthyboy_core.a`, `core/filters-serum/libsynthyboy_filters_serum.a`). Facts from the binary:

1. **No NEON float comparison instruction exists anywhere in the core or filter libraries.** `vcgt.f32`, `vcge.f32`, `vclt.f32`, `vcle.f32`, `vceq.f32` on D or Q registers: 0 occurrences (only `vceq.i32`, `vcge.u32` and similar integer ones). GCC on ARMv7 does not use NEON for float vector comparisons without `-funsafe-math-optimizations` (NEON is not IEEE compliant), so every `vclt_f32`, `vcle_f32`, `vcge_f32`, `vcltq_f32` intrinsic in the source was split into per lane VFP compares: lane moved to a core register, `vcmpe.f32`, `vmrs APSR_nzcv, fpscr`, conditional move, the mask stored to the stack and reloaded as a vector. Seen in `Chorus::process` (45 `vmrs`), the Serum pair kernel `processPairT<NeonLanes>` (168 `vmrs`), `ClassicFilter::svfPairLoopDigitalNeon` (24 `vmrs`). `vmax.f32` and `vmin.f32` on Q registers are emitted normally (builtins).
2. **The per lane division of the NEON kernels goes through memory.** `NeonLanes::operator/` (`core/filters-serum/src/filter_lanes.h:60-69`) and the `divide` lambda (`core/engine-own/voice/classic_filters.h:229-237`) compile to lane to core register moves, two `vdiv.f32`, two `vstr` and one `vld1.32 {d}` (store to load forwarding of two 32 bit stores into a 64 bit load).
3. **libm calls cannot be hoisted.** With the default `-fmath-errno`, `sinf`, `cosf`, `powf`, `expf`, `log10f`, `tanf` may set `errno`, so GCC keeps every call where the source has it, even with loop invariant arguments. `std::sqrt` becomes `vsqrt` plus a check and a `sqrtf` call on a negative input.
4. **Scalar float `std::max`, `std::min`, `std::clamp`, `std::isnan`, `std::isinf` are compare plus `vmrs` plus branch or conditional move**: VFPv3 has no `vmaxnm`. The Serum filter path is dense with them (section 5).
5. **The Serum filter helpers are out of line calls**: `SVFTPT::process` (27 call sites in `processInternal`), `OnePoleTPT::processLP` (8), `Ladder4Pole::process` (4), `DelayLine::read` (5), `FilterImpl::tanM` (10). `FilterImpl::processInternal` itself is called once per sample per channel from `Filter::process` (the `R_ARM_THM_CALL` at offset 0x5d02 of the filters library).

**Per operation estimates for a Cortex-A17 at 1.608 GHz (estimates, not measured):** NEON 4 float multiply-add about 2 cycles issue (the A17 NEON unit is 64 bit wide); scalar VFP op 1 to 2; float divide about 14; a scalar float compare with `vmrs` and a dependent conditional 7 to 11 (estimate: the flag transfer waits for the VFP pipeline); a NEON lane to core register move 3 to 5; a failed store to load forward 10 to 20; libm `sinf`, `cosf`, `expf`, `tanf`, `powf`, `log10f` 80 to 200; double libm 150 to 400. One block is 2.902 ms x 1.608 GHz = **4.666 M cycles; 1 % of the block = 46.7 k cycles**.

**Calibration of the model against the measurements.** Multi SVF: 31.5 % of the block in Triple Fifth Lead (8 voices) = 1.470 M cycles = 718 cycles per channel sample (1.470 M / (8 x 2 x 128)). The code path of section 5 has about 51 compare plus `vmrs` sequences, 6 divisions, half a `tanf`, 2 out of line calls per channel sample: 357 to 561 + 84 + 40 to 100 + about 70 = 550 to 815. The model brackets the measurement, so the estimates below use it.

## 2. Profiles and the loop nest

**Profiles** (`shell-mpc/device_profile.h:186-189`, grammar `:119-122`). Key 61 = `low` = 8 voices, unison cap 3, oversampling 1, effect mask of tier low (reverb, flanger, Hyper / Dimension, Bode, Convolve bypassed, `core/include/synthyboy/quality.h:112`), all four economy groups (`quality.h:63-70`). XL = `high,voices=16,control=economy` = 16 voices, unison 16, oversampling 2, all effects, economy group Control only, so `math=exact`.

**`control=` does not change the period.** `kControlPeriod` is 32 samples on every tier (`core/engine-own/dsp/util.h:20`). `control=economy` (both devices) switches the economy group Control: events on the 32 sample grid (`own_engine.cpp:1501-1506`), the mute of an effect whose mix is 0 (`own_engine.cpp:1441`, `fx_module.h:146`), the silent rack skip (`own_engine.cpp:1444`, `fx_chain.cpp:747-769`). What differs between the devices in this area is `math`: Key 61 `fast` gives `coefHold = 4` for the Serum filters (`core/engine-own/voice/filter_unit.h:129`), the Pade tanh in the classic models, the table sine and fast exp2 in chorus, phaser, distortion, compressor; XL `exact` gives `coefHold = 1` (new coefficients every sample while the cutoff glides) and libm everywhere.

**Loop nest of one 128 frame block** (both devices; counts for Key 61):

```
OwnEngine::process                         own_engine.cpp:1398      per block
  chunk loop, n = 32 on the grid           own_engine.cpp:1398-1606 4 control periods per block
    controlTick ()                         (controlPos == 0)        per period
      Voice::tick, each active voice       voice.cpp:420            per period x voice
        filters f = 0, 1                   voice.cpp:1065-1100      per period x voice x 2
          fillFilterSettings + FilterUnit::set   filter_setup.h:22, filter_unit.h:85
      fx[0..2].update (Main, Bus 1, Bus 2) own_engine.cpp:1442-1446 per period x 3 racks
    Voice::render (n)                      own_engine.cpp:1529-1531 per period x voice
      runFilter (f)                        voice.cpp:1318-1344      per period x voice x 2
        FilterUnit::process                filter_unit.h:142-252
          classic model 0..7: ClassicFilter processPair / processRampPair / process, 8 sample pieces, then per sample per channel
          Serum model 8..26: Filter::processPair (Scream, EMS, MG Ladder, Diffusor) else Filter::process x 2
            FilterImpl::processBlock       filter.cpp:149-182       per sample x channel
              FilterImpl::processInternal  filter.cpp:535-1348      per sample x channel (a call)
          runaway check + clamp            filter_unit.h:236-251    per sample (Serum models)
        gain ramp, halfSum, peakOf         voice.cpp:1331-1343      per period x voice x 2
    runBus (1), runBus (2)                 own_engine.cpp:1546-1582 per period (rack only if active)
    fx[0].process (n)                      own_engine.cpp:1583-1584 per period
      FxChain::process -> runRack -> runModule -> FxModule::process -> run   fx_chain.cpp:747, 698, 539; fx_module.h:161
        effect process                     fx_effects.h, fx_eq.h, fx-serum-a ...   per sample
```

The measured call counts agree: `filter 1` 24 calls per block for 6 voices, 32 for 8; `fx: chorus` 4; `fx: update` 12 (`sections.csv`).

## 3. What the six dearest presets really run

From `content/presets/**/<name>.sbpreset`; modulation destinations decoded with `docs/params.json` `modDestinations` (choice 59 = `filter1.cutoff`, 63 = `filter1.mix`, 71 = `filter2.cutoff`).

| Preset (voices measured) | Filter 1 | Filter 2 | Routing | Moving cutoff | Effects on (Key 61 runs) | Measured filter share | Chorus | fx update |
|---|---|---|---|---|---|---|---|---|
| Lorenz Garden (6) | model 13 style 1 = Smear **Allpasses** | model 0 style 1 = classic Analog 24 | series 1 to 2 | F1 by random LFO 2, F2 by random LFO 3 | chorus, delay, EQ (reverb masked) | F1 34.1 %, F2 7.9 % | 6.68 % | 1.74 % |
| Stretched Glass Choir (8) | model 3 style 1 = classic Digital 24 (NEON kernel) | off | parallel | F1 by LFO 1 | chorus, EQ (reverb masked) | F1 10.2 % | 6.71 % | 1.76 % |
| Parallel Dusk Pad (6) | classic Analog 24, pan -80 | model 24 style 7 = Flanges **Flg HL**, pan +80 | parallel | both by LFO 1 and env 4 | chorus, utility (reverb masked) | F1 8.7 %, F2 39.7 % | 6.60 % | 1.40 % |
| Ring Mod Gamelan (8) | model 11 style 0 = **Ring Mod**, mix 62 (mix moved by env 3) | model 8 style 2 = **Multi SVF LP** | series 1 to 2 | F2 by env 2 | delay, EQ (reverb masked) | F1 15.0 %, F2 31.1 % | none | 1.51 % |
| Shepard Ascent (4) | classic Analog 24 | model 8 style 5 = **Multi SVF HP** | series 1 to 2 | F1 by random LFO 1, F2 by LFO 2 | chorus, delay (reverb masked) | F1 5.4 %, F2 16.5 % | 6.72 % | 1.46 % |
| Triple Fifth Lead (8) | model 8 style 2 = **Multi SVF LP** | off | parallel | F1 by env 2 | chorus, delay, utility (Hyper masked) | F1 31.5 % | 6.56 % | 1.39 % |

None of the six uses a Comb model, an impulse or Convolve. Every one routes a macro to an effect mix (for example Lorenz Garden macro 4 to `chorus.mix` and `delay.mix`), which matters for finding F11.

## 4. Per model cost, measured, in cycles per channel sample

Measured share x 4.666 M / (voices x 2 channels x 128):

| Model | Preset | Share | Cycles per channel sample |
|---|---|---|---|
| Flanges Flg HL | Parallel Dusk Pad F2 | 39.7 % | 1206 |
| Smear Allpasses | Lorenz Garden F1 | 34.1 % | 1036 |
| Multi SVF LP | Triple Fifth Lead F1 | 31.5 % | 718 |
| Multi SVF LP | Ring Mod Gamelan F2 | 31.1 % | 709 |
| Multi SVF HP | Shepard Ascent F2 | 16.5 % | 752 |
| Ring Mod | Ring Mod Gamelan F1 | 15.0 % | 342 |
| classic Analog 24 | Lorenz Garden F2 | 7.9 % | 240 |
| classic Analog 24 | Shepard Ascent F2 | 5.4 % | 246 |
| classic Digital 24 (NEON pair loop) | Stretched Glass Choir F1 | 10.2 % | 233 (465 per sample pair) |

The Serum models without a pair kernel cost 3 to 5 times a classic model. That is where the time goes.

## 5. Findings

Savings are shares of the 2.902 ms block on Key 61 `low`. "Bit identical" means the same samples as now, which `synthyboy_tier_bench --verify` checks.

### F1. Multi SVF runs the general scalar path: no pair kernel

- **Status.** Done in 51e77b08 (all twelve styles; test_filter_pair). Multi SVF LP 1127 -> 463 instructions per stereo sample on arm32.

- **Where.** `core/filters-serum/src/filter.cpp:687-734` (Multi SVF), reached from `processBlock` `filter.cpp:149-182` because `processPairT` accepts only Scream, EMS Ladder, MG Ladder and Diffusor (`filter.cpp:191-196`); the caller `FilterUnit::process` `filter_unit.h:229-233`.
- **What it does, per sample per channel.** `processBlock`: target cutoff and resonance clamps (4 compares), smoothing, hold logic, `sanitize` of input and output (`filter_dsp.h:19-26`, 3 compares each). `processInternal` prelude (`filter.cpp:535-547`): a division `kPi * cutoff / srFloat`, the `tanM` memo hash, 4 parameter clamps (8 compares) of values that are constant for the whole call, `softClip` of the input (a division, 2 compares). Multi SVF body: `pow2M`, a second division and `tanM` for `fc2`, two out of line `SVFTPT::process` calls (`filter_dsp.h:95-137`: 4 `sanitize`, `gSafe` clamp, the `dCached` test: about 12 compares each), 3 more `softClip` (3 divisions, 6 compares).
- **Loop level.** Per sample x channel x voice x active filter (`processInternal` is a real call per sample). With the cutoff moved by an envelope or LFO every period, `coefHold = 4` makes `g` and `g2` new every fourth sample: 2 `tanf` misses per 4 samples per channel.
- **Why waste.** About 51 compare plus `vmrs` sequences and 2 calls per channel sample, most of them on values that are the same for all 32 samples of the call or the same for 4 samples (the hold). The two channels are independent and identical in work.
- **Consequence for the output.** A pair kernel built like the existing ones (`filter.cpp:187-449`, `filter_lanes.h`) gives the same samples to the last bit (the existing test `core/filters-serum/tests/test_filter_pair.cpp` shows the method).
- **Estimated saving.** Kernel model per channel sample: 4 true divisions 56, lane transfers for them 40 to 80, NEON arithmetic 30 to 40, half a `tanf` 40 to 100, loop and memory 20: 186 to 296 cycles against 718, so 59 to 74 %. Taken as **45 to 65 %** to allow for F2 not being done first.
  - Triple Fifth Lead: 31.5 x 0.45 = 14.2; 31.5 x 0.65 = 20.5, so **14 to 20 %**.
  - Ring Mod Gamelan (F2): 31.1 x 0.45 = 14.0; 31.1 x 0.65 = 20.2, so **14 to 20 %**.
  - Shepard Ascent (F2, 4 voices): 16.5 x 0.45 = 7.4; 16.5 x 0.65 = 10.7, so **7 to 11 %**.
  - Precedent: the Scream kernel (an SVF, a one pole and two `softClip`, the closest relative) took Scream Filter Bass from 67.0 % to 36.9 % median (`tier-bench/k61-r2-*.txt`).
- **Confidence.** High that it is the dominant cost (measured, and `filter1off` takes Triple Fifth Lead from 73.07 % to 34.57 %); medium on the exact fraction.
- **Change sketch.** In `processPairT` add `const bool multi = type >= FilterType::LH && type <= FilterType::NN;` to the accepted set. Per lane state: `svf1_.s1, s2` and `svf2_.s1, s2`. On a change of the held cutoff of a lane (the existing `cutoffChanged` block, `filter.cpp:287-347`): `g[l] = tanM (kPi * c / srFloat)`, `fc2 = clamp (c * pow2M ((var - 0.5) * 8), 20, sr * 0.49)`, `g2[l] = tanM (kPi * fc2 / srFloat)`, then the two `gSafe` clamps and the two `dCached` divisions with the exact expressions of `SVFTPT::process` (the R memo exactly as the Scream branch does at `filter.cpp:353-376`). Per sample in lanes: `hp = (in - (damping + gSafe) * s1 - s2) * dEff`, `v1 = gSafe * hp`, `bp = v1 + s1`, `s1 = sanitized (v1 + bp)`, and so on, in the order of `filter_dsp.h:122-136`; the stage selects of `filter.cpp:708-730` are per call constants (type), so pick them with `if` outside the arithmetic; `rawOut = (1 - 0.5 blend) * out2 + (0.5 blend) * out1`; mix as `filter.cpp:427`. Keep `#pragma GCC optimize ("fp-contract=off")` (already at the top of the file). Extend `test_filter_pair.cpp` to the 12 Multi SVF styles.

### F2. NEON float compares are split into scalar compares on arm32

- **Status.** Done in 3d1781a6: `core/engine-own/dsp/neon_cmp.h`, used in `filter_lanes.h`, the Digital loop and the Formant lanes, the chorus. Not converted (other areas, other branches): `osc/wt_osc.h:774, 781`, `sample-osc/src/sample_voice.cpp:367, 384`, `granular/src/granular.cpp:481`, `spectral/src/kernels.cpp:257, 288, 300-303, 618, 657` (and the Vital upstream).

- **Where.** `filter_lanes.h:72-77` (`sanitized`: `vclt_f32` twice), `filter_lanes.h:79-89` (`softClipped`: `vcge_f32`, `vcle_f32`); `classic_filters.h:238` (`flush`: `vclt_f32`) and `:242` (`limit`: `vcle_f32`); `core/engine-own/fx/fx_effects.h:359, 361` (chorus `tapGroupNeon` flush) and `:382, 384` (`bandGroup`). Outside this area the same pattern is in `osc/wt_osc.h`, `sample-osc/src/sample_voice.cpp`, `granular/src/granular.cpp`, `spectral/src/kernels.cpp` (grep of `vc(lt|le|gt|ge|eq)q?_f32`).
- **What it does.** Binary fact 1 of section 1: each lane is compared with `vcmpe` and `vmrs`, the mask is stored and reloaded. In the chorus disassembly, one 4 lane flush is 4 x (`vmov.32 r, d[x]`, `vmov s, r`, `vcmpe`, `vmrs`, `ite`, 2 `mov`, `str`) and then `vldr d16`, `vldr d17`, `vbit`.
- **Loop level.** Chorus: per sample, 4 groups x 2 flushes x 4 lanes = 32 lane compares and 8 forwarding stalls. Digital 24 kernel: per sample pair, 4 flushes x 2 lanes + 2 limits x 2 lanes = 12 lane compares and about 6 stalls. Serum pair kernels: per sample pair, every `sanitized` and `softClipped`.
- **Why waste.** The compare can be done exactly on the bit patterns with NEON integer instructions, which GCC emits normally.
- **Consequence.** Bit identical for every input, NaN, infinity, signed zero and subnormals included, if written as below.
- **Estimated saving.**
  - Chorus (Lorenz Garden, Stretched Glass Choir, Parallel Dusk Pad, Shepard Ascent, Triple Fifth Lead): 32 x 8 + 8 x 10 = 336 to 32 x 12 + 8 x 20 = 544 cycles per sample, minus about 40 for the integer form; 300 x 128 / 46.7 k = 0.82 %; 500 x 128 / 46.7 k = 1.37 %, so **0.8 to 1.4 %** each.
  - Stretched Glass Choir Digital 24: 100 to 160 cycles per sample pair x 1024 pairs (8 voices x 128): 102 k / 46.7 k = 2.2 %; 164 k / 46.7 k = 3.5 %, so **2.2 to 3.5 %**.
  - It also lowers the cost of every existing and future pair kernel (F1, F3, F4).
- **Confidence.** High on the mechanism (binary evidence); medium on the cycles.
- **Change sketch.** One helper header (for example `core/engine-own/dsp/neon_cmp.h`) with, on `uint32x2_t` or `uint32x4_t` bits `b = vreinterpret_u32_f32 (x)` and `a = b & 0x7fffffff`:
  - `|x| < c` (c positive, finite): `vclt_u32 (a, bits (c))` (a NaN has `a > 0x7f800000` and gives false, as the float compare).
  - finite: `vclt_u32 (a, 0x7f800000)`. So `sanitized` = keep where `a < 0x7f800000 && a >= bits (1e-18f)`.
  - `x >= c` (c > 0): `vcge_s32 (vreinterpret_s32 (b), bits (c)) & vcle_u32 (a, 0x7f800000)`.
  - `x <= -c`: `(b >> 31 != 0) & vcge_u32 (a, bits (c)) & vcle_u32 (a, 0x7f800000)`.
  - Use them in `filter_lanes.h` (`sanitized`, `softClipped`), `classic_filters.h` (`flush`, `limit`), `fx_effects.h` chorus flushes. Keep the scalar reference forms as they are. Then check in the disassembly that no `vmrs` is left in those loops. For the lane division, write the two quotients with `vset_lane_f32` into the vector instead of a `float t[2]` array and check that GCC writes the two `vdiv.f32` results straight into the S halves of the D register (on arm32 `d0 = {s0, s1}`); if it does not, keep the array.

### F3. Smear Allpasses: the Diffusor kernel already exists for the same structure

- **Status.** Done in e6c6e2fd. Allpasses 1458 -> 783 instructions per stereo sample on arm32.

- **Where.** `filter.cpp:987-1001` (Allpasses), the Diffusor kernel `filter.cpp:213-223, 325-330, 409-425`, `DelayLine::read` / `write` `filter_dsp.h:359-421`.
- **What it does, per sample per channel.** The general prelude (F1), then 6 all-pass stages; each stage computes `d = max (2, (sr / max (20, cutoff)) * (0.3 + 0.2 i))`, `DelayLine::read` (clamp of `d`, float to index conversion, 2 loads, interpolation), `x = ap - g delayed`, `ap = delayed + g x`, `write` (a `sanitize`). The disassembly of the case (offset 0x3c60 of `processInternal`) shows about 55 instructions and 6 `vmrs` per stage. `g` and `R` of the prelude are computed although Allpasses does not read them.
- **Loop level.** Per sample x channel x stage (6) x voice.
- **Why waste.** The tap positions depend on the held cutoff only (4 samples at `coefHold = 4`); the Diffusor kernel already keeps them per lane (`taps[l][k]`). Allpasses differs from Diffusor only in the stage count (6, fixed), the tap factor (`0.3 + 0.2 i` against `0.2 + 0.15 k`), the clamp `max (2, ...)` on `d` and the gain (`0.6 res` against `0.5 + 0.45 res`).
- **Consequence.** Bit identical.
- **Estimated saving.** Kernel per channel sample: 6 stages x 25 to 40 = 150 to 240, plus 60 for the rest, against 1036: 71 to 80 %. Taken as 45 to 65 %. Lorenz Garden: 34.1 x 0.45 = 15.3; 34.1 x 0.65 = 22.2, so **15 to 22 %**. (`filter1off` takes Lorenz Garden from 85.22 % to 44.40 %.)
- **Confidence.** High; risk low (a variant of a tested kernel).
- **Change sketch.** In `processPairT` add `const bool allpasses = type == FilterType::Allpasses;` with `stages = 6`; in the cutoff change block compute `perCycle = srFloat / std::max (20.0f, c)` and `taps[l][k] = diffuserDelays_[k].tapFor (std::max (2.0f, perCycle * (0.3f + 0.2f * k)))` (the same expression order as `filter.cpp:993`; note `tapFor` repeats the `max (1, min (d, maxD))` of `read`, as needed); per sample `gAp = V::all (0.6f) * res`, the stage loop of `filter.cpp:414-424`. Also sanitize `x` in the lanes and add a `DelayLine::writeSanitized` that stores without the scalar `sanitize` (same value, `sanitize` is idempotent), so no scalar compare is left; do the same in the Diffusor branch.

### F4. Flanges (Flg HL): a `log10f` per sample and setup work redone every sample

- **Status.** Done in b1e6490b (steps A and B together in the pair kernel; the plain path still computes the flanger setup per sample: it runs only with --scalar-filters or without NEON).

- **Where.** `filter.cpp:776-871`, the `log10` at `:795`, the HL feedback filters `:815-824`.
- **What it does, per sample per channel.** `delaySamples = sr / max (20, cutoff)` (overwritten for the flanger), `delaySec` with `std::log10` (a libm call every sample, not memoised), `combDelay1_.read`, for HL `fLP`, `fHP` (a division), two `tanM` (two divisions in the arguments), two out of line `OnePoleTPT::processLP` calls (each with `sanitize` and, on a new `g`, a division), `softClip` (division), `write`.
- **Loop level.** Per sample x channel x voice. The left and right cutoffs differ (pan +80), so the left and right lanes have different taps.
- **Why waste.** Everything except the delay read, the two one poles and the write depends only on the held cutoff and the call's constants.
- **Consequence.** Bit identical (step A and B).
- **Estimated saving.** Step A (setup once per held cutoff, scalar): 3 of 4 `log10f` (60 to 150), about 4 divisions and 3 memo lookups and 4 clamps x 0.75 (100 to 120): 160 to 270 cycles of 1206, 13 to 22 %. Parallel Dusk Pad: 39.7 x 0.13 = 5.2; 39.7 x 0.22 = 8.7, so **5 to 9 %**. Step B (pair kernel with per lane taps and one pole states, integer compares): together **45 to 65 %**: 39.7 x 0.45 = 17.9; 39.7 x 0.65 = 25.8, so **18 to 26 %**. (`filter2off`: 78.12 % to 32.35 %.)
- **Confidence.** High for A, medium for B.
- **Change sketch.** A: in `FilterImpl` keep `flangeKey` (the held cutoff, var, res, type) and its derived `delaySamples`, `gLP`, `gHP` (computed with the exact expressions of `:791-822`), recomputed only when the key changes. B: a `flanges` branch in `processPairT`: `DelayLine::Tap` per lane from `tapFor (delaySamples)`, the one pole coefficients per lane (`lastCoef` exactly as `OnePoleTPT::processLP`, `filter_dsp.h:51-56`), the arithmetic in lanes.

### F5. Ring Mod: a libm `sinf` per sample per channel

- **Status.** (a) done in 318705f6. (b) not in this round (rounding level).

- **Where.** `filter.cpp:925-933`.
- **What it does.** `ringModPhase1_ += 2 pi cutoff / sr` (a division), wrap, `std::sin (ringModPhase1_)`, multiply.
- **Loop level.** Per sample x channel x voice: 8 x 128 x 2 = 2048 `sinf` per block.
- **Why waste.** With pan 0 both channels have the same cutoff and the same phase (both start at 0 on `reset`, same increments), so the carrier is computed twice. At tier low the engine already uses a table sine elsewhere (`fastmath::sinCycles`, `fastmath.h:71-82`).
- **Consequence.** (a) Computing it once per voice when the two phases and cutoffs are equal: bit identical. (b) Table sine at tier low (`sinCycles (phase * (1 / 2 pi))`, absolute error below 5e-6, about -106 dB): rounding level, needs an entry in `docs/MPC.md` "Sound differences at tier low".
- **Estimated saving (Ring Mod Gamelan F1 = 15.0 %, 342 cycles per channel sample).** (a) 1024 `sinf` saved: 1024 x 80 / 46.7 k = 1.8 %; 1024 x 200 / 46.7 k = 4.4 %, so **1.8 to 4.4 %**. (b) the remaining 1024 calls at about 15 cycles: 1024 x 65 / 46.7 k = 1.4 %; 1024 x 185 / 46.7 k = 4.1 %, so **1.4 to 4.1 %** more.
- **Confidence.** High for (a), medium for the `sinf` cycle count.
- **Change sketch.** (a) a `ringmod` branch in `processPairT` that, when `fa.ringModPhase1_ == fb.ringModPhase1_` and the two targets are equal, steps one phase with the exact scalar expressions and stores it to both; otherwise returns false. (b) behind `FilterParams::coefHold > 1` (the existing tier low signal), or a new explicit flag in `FilterParams`.

### F6. Serum path: the runaway check and the clamp are scalar compare loops

- **Status.** Done in f7ad3363; the loops as they were stay for `--scalar-filters` and a trim other than 1.

- **Where.** `filter_unit.h:236-251`.
- **What it does.** After the filter: a loop with `!(fabs (out) < 200)` for both channels (short circuit `||`), then a loop with `std::clamp (out * trim, -16, 16)` for both channels; `trim` is always 1 (`filter_unit.h:136`). Disassembly: scalar `vcmpe`, `vmrs`, conditional moves, 6 compares per sample pair.
- **Loop level.** Per sample x voice x Serum filter (32 samples per call).
- **Why waste.** The classic path already uses the NEON clamp (`simd::clampTo`, `simd.h:115-125`, used at `filter_unit.h:183-184`); the Serum outputs are already `sanitize`d (finite), so the runaway test is a magnitude test on finite values.
- **Consequence.** Bit identical (`out * 1.0f == out`; `vmax`, `vmin` equal `std::clamp` on finite values, signed zero included).
- **Estimated saving.** 192 compares x 7 to 11 = 1.3 k to 2.1 k cycles per voice period, NEON form about 0.15 k. Ring Mod Gamelan (2 Serum filters, 64 voice periods): 64 x 1.15 k / 46.7 k = 1.6 %; 64 x 1.95 k / 46.7 k = 2.7 %, **1.6 to 2.7 %**. Lorenz Garden and Parallel Dusk Pad (24): **0.6 to 1.0 %**. Triple Fifth Lead (32): **0.8 to 1.3 %**. Shepard Ascent (16): **0.4 to 0.7 %**. Stretched Glass Choir: 0.
- **Confidence.** High; risk very low.
- **Change sketch.** Replace both loops by one NEON pass per channel: `bits & 0x7fffffff` compared with `bits (200.0f)` by `vcgeq_u32`, OR reduced to a flag (any lane set means runaway, then the existing reset and zero fill); else `simd::clampTo (outL, -16, 16, n)` and the same for `outR`. Drop the `* trim`, or keep `trim` and apply it only when `trim != 1.0f`.

### F7. Serum scalar path: per call constants and held values recomputed every sample

- **Status.** Done in 616b452b (the parameters and the targets once per call, g once per held cutoff, `sanitize ()` on the bit pattern); the inline of the memos in fbb8d821. `processInternal ()` stays a call (about 6000 instructions; inlining it into `processBlock ()` was not tried: a code size risk for the 32 KB instruction cache that only a device run could weigh).

- **Where.** `filter.cpp:157-181` (`processBlock`) and `:535-547` (prelude).
- **What it does.** Every sample: clamps of the target cutoff and resonance (constant within a call, `cutoffMod` is always null from the engine), the 4 clamps of drive, var, blend, mix (constant within a call), the division and memo lookup of `g` (the same held cutoff for 4 samples at tier low), `R`.
- **Loop level.** Per sample x channel x voice, for every Serum model without a kernel.
- **Why waste.** About 12 compares and a division per channel sample that the call could do once (or once per hold).
- **Consequence.** Bit identical.
- **Estimated saving.** 60 to 130 cycles per channel sample (compare cost range, the division and memo x 0.75). For Ring Mod (no kernel planned): 2048 x 60 / 46.7 k = 2.6 %; 2048 x 130 / 46.7 k = 5.7 %, so **2.6 to 5.7 %** on Ring Mod Gamelan. For Multi SVF, Allpasses, Flanges the same saving is part of F1, F3, F4 (do not add). It also helps every other Serum type without a kernel (Normal, Multi Morph, EQ, Sample Hold, Combs, Reverb, French, German, Add Bass, Formant I to III, Dist Combs, DJ Mixer, PZ SVF, Comb 2, Exp MM, Exp BPF).
- **Confidence.** High; risk low.
- **Change sketch.** Split `processInternal` into a `Setup` struct (`g`, `R`, `drive`, `var`, `blend`, `mix`, `driveGain`, and the type's cutoff derived values) computed in `processBlock` when the held cutoff or resonance changes (and once per call for the rest), and a `processInternal (in, const Setup&)`; keep every expression as written. Mark `processInternal` and the `filter_dsp.h` helpers `inline` / `__attribute__ ((always_inline))` so the per sample call and the 9 register save of its prologue (`push.w {r4 ... lr}`, `vpush {d8 ... d14}`) go away.

### F8. Voice tick: five libm calls per filter per voice per period for values that rarely move

- **Where.** `voice.cpp:1065-1100`, `filter_setup.h:27-31`, `util.h:28-36`.
- **What it does.** `semitonesToRatio` twice (`exp2f`: key track and voice control cutoff), `std::exp2 (+-panShift)` twice (also at pan 0), `dbToGain (level)` (`powf`), also for a filter that is off (`voice.cpp:1096-1097` is outside the `filtOn` test).
- **Loop level.** Per period x voice x 2 filters (64 calls per block at 8 voices, matching `sections.csv`).
- **Why waste.** Key track is constant per note, pan and level are constant in all six presets; `exp2f (0)` is exactly 1.
- **Consequence.** Bit identical (cache on the exact input value, skip `exp2` when `panShift == 0`).
- **Estimated saving.** 4 libm of 80 to 200 per filter voice period. Lorenz Garden (48): 48 x 320 / 46.7 k = 0.3 %; 48 x 800 / 46.7 k = 0.8 %. 8 voice presets with two filters on (Ring Mod Gamelan): **0.4 to 1.1 %**; with one filter (Stretched Glass Choir, Triple Fifth Lead): **0.3 to 0.7 %**; Shepard Ascent **0.2 to 0.5 %**. Bounded by the measured "voice tick: filter settings" 1.49 to 2.29 %.
- **Confidence.** Medium (how much of the section is libm is estimated); risk low.
- **Change sketch.** Per voice and filter keep `lastKtArg`, `lastKt`, `lastVcSemis`, `lastVcCut`, `lastPan`, `lastPanL`, `lastPanR`, `lastLevelDb`, `lastLevel`; compute only when the argument differs (bitwise `!=`); `panShift == 0` gives exactly 1.

### F9. An off filter with nothing routed into it still runs its wrapper

- **Where.** `voice.cpp:1318-1344, 1377-1380`.
- **What it does.** For filter 2 off (Stretched Glass Choir, Triple Fifth Lead): `touchFin` zero fill of 64 floats, copy of the zeros to the output, a gain ramp, `mulBy` twice, `halfSum`, `peakOf` (32 scalar compares), `toMain` adds the zeros into Main.
- **Loop level.** Per period x voice.
- **Consequence.** Bit identical except that `+0` could replace a `-0` in a target buffer (inaudible).
- **Estimated saving.** 300 to 600 cycles x 32 voice periods: 9.6 k / 46.7 k = 0.2 %; 19.2 k / 46.7 k = 0.4 %, **0.2 to 0.4 %** on those two presets.
- **Change sketch.** In `runFilter`: if `!filtOn[f] && !usedFin[f]`, skip the copy, the ramp, the sums and the peak (the strip gets 0), mark `fout` as unused and let `toMain` skip a filter whose output is unused.

### F10. The mixer strip peaks are scalar compare loops (audio unaffected)

- **Where.** `peakOf` `voice.cpp:1107-1113`, called at `:1229, 1243, 1311, 1341`; similar loops in `own_engine.cpp:1535-1541` (bus peaks) and `:1602` (master peak).
- **What it does.** `max (m, fabs (x))` with a loop carried `vcmp`, `vmrs`, `vmovgt` (disassembly offset 0xd2e2 of `Voice::render`).
- **Loop level.** Per period x voice x source (each oscillator on, sub, noise, both filters).
- **Status.** Done in 62e404f6 (`work/opt-shell`). The displayed peak does not differ even for a NaN: the vector form masks a NaN lane to 0 with an integer compare on the magnitude bits before `vmaxq_f32`, which gives the maximum of the same numbers the `std::max` loop took (a NaN never entered it); the two channel form drops a sample whose left value is NaN, as `std::max (|l|, |r|)` did. Tested bit for bit over every special value, every length, unaligned starts, default and flush-to-zero mode.
- **Consequence.** Audio bit identical; the displayed peak is identical too (the first draft of this finding said it differs for a NaN: it does not, see Status).
- **Estimated saving.** 195 to 325 cycles per call. Triple Fifth Lead (3 oscillators + 2 filters, 32 voice periods = 160 calls): 160 x 195 / 46.7 k = 0.7 %; 160 x 325 / 46.7 k = 1.1 %, **0.7 to 1.1 %**; Lorenz Garden (144 calls) **0.6 to 1.0 %**; Ring Mod Gamelan, Stretched Glass Choir **0.5 to 0.9 %**; Parallel Dusk Pad **0.4 to 0.7 %**; Shepard Ascent **0.3 to 0.4 %**. The filter share of it is 2 of the calls per voice period.
- **Change sketch.** `simd::peakAbs (const float*, int)`: `vmaxq_f32 (acc, vabsq_f32 (x))` over 4 lanes, `vpmax` reduction; scalar tail as now.

### F11. Effect rack update: full work every period in all three racks

- **Status.** Skipped: measured with a counter in `FxChain::update` over `--verify` renders of the six presets, the Bus racks already exit early (inputsSame holds: no global modulation moves in these presets), and the Main rack is active every period. A structural exit was written, found to fire never in the six, and taken out again.

- **Where.** `own_engine.cpp:1442-1446` calls `FxChain::update` for 3 racks every period. The early exit `fx_chain.cpp:362-366` needs `inputsSame`, which is false whenever `gnumTouched != 0` (`own_engine.cpp:1405`), and all six presets route a macro to an effect mix, so it never fires. Inside: the 16 module loops, 16 pool slots `configureSlot`, `dbToGain` of the three splitter levels also when the splitters are off (`fx_chain.cpp:412-416`), `FxModule::update` with `dbToGain (level)` per active module (`fx_module.h:124`), the EQ `set` with 4 x (`powf`, `tanf`, 2 square roots) (`fx_eq.h:32-41, 114-151`), the makeup of the masked reverb: `ProgramMeter::endPeriod` with 4 double `exp` of constant arguments and 7 double divisions (`fx_makeup.cpp:153-179`), `desc` (`:181-202`), the reverb model with `powf` and `cosf`, `sinf` (`fx_makeup.cpp:53-58, 266-270`).
- **Loop level.** Per period x rack.
- **Consequence.** Bit identical for every part below.
- **Estimated saving.** Measured "fx: update" 1.39 to 1.76 % (`sections.csv`); estimated 40 to 70 % of it: Lorenz Garden 1.74 x 0.4 = 0.7; 1.74 x 0.7 = 1.2, **0.6 to 1.2 %** for each of the six.
- **Confidence.** Medium; risk low to medium (the structural test must cover everything that can turn a module on).
- **Change sketch.** (1) A structural "nothing can be on" test for a rack that does not depend on `gnumTouched`: the on, rack, order and type keys are not modulation destinations, so a rack whose plan is empty, with no masked effect that leaves a makeup, no pending pool module and an unchanged mask and `blockDirty == false`, can return early. (2) Cache `dbToGain` on the exact dB value (splitter levels, module levels, makeup level). (3) EQ: recompute a band's design only when its mode, frequency, gain or resonance changed. (4) `ProgramMeter`: compute `kSlow`, `kHold` and the 0.1 s coefficient once in `prepare` for `kControlPeriod` and use them when `periodSamples` equals it (same double values).

### F12. Chorus: the LFO refresh and the phase step

- **Status.** Done in 26a424d5 (the quotient once per call, the four lane refresh behind `--scalar-filters`); the interleaved store is not done.

- **Where.** `fx_effects.h:193-258`: phase step `:216-217`, refresh `:220-231`, tap groups `:235-253`.
- **What it does.** Every sample a division `p.rate / sr` (`p` is a member and the stores to `l`, `r` may alias it, so GCC reloads and divides each sample) and `ffloor`. Every fourth sample at tier low (every sample on the XL, with `std::sin`): 16 taps x (`ffloor` with 2 compares, `sinCycles` with a compare, the delay, a clamp with 2 compares).
- **Loop level.** Per sample (phase), per 4 samples x 16 taps (refresh).
- **Consequence.** Bit identical (the vector form does the same float operations per lane; `ffloor` and `sinCycles` exactly as written for `|x| < 2^22`).
- **Estimated saving.** Refresh 16 x 75 to 90 cycles per 4 samples = 300 to 360 per sample, vector form about 40 to 80: 230 x 128 / 46.7 k = 0.6 %; 320 x 128 / 46.7 k = 0.9 %, **0.5 to 0.9 %** on the five chorus presets. The hoisted division adds about 0.05 %.
- **Change sketch.** `const float inc = p.rate / sr;` before the loop. Refresh 4 taps per Q register: phases from a constant offset vector, `vcvtq_s32_f32` truncation and the `ffloor` correction with integer compares (F2), the table index and fraction as in `sinCycles`, lane loads from `kSine.v`, `vmaxq`, `vminq` for the clamp.
- A second, larger and less certain step: interleave the 16 chorus lines (`store[pos * 16 + tap]`) so that the 16 writes of a sample are one 64 byte store instead of 16 scattered ones in a 256 KB store larger than the 32 KB L1D. Bit identical; the gain needs a device measurement.

### F13. Classic Analog: no NEON pair loop, one division per section per sample

- **Where.** `classic_filters.h:211-221` (NEON only for model 3), `:302-330` (scalar pair loop), `tpt` `:557-569`, ramp `:332-366`; Parallel Dusk Pad's pan makes left and right differ, so `processRampPair` refuses and `cl[0].process`, `cl[1].process` run one after the other (`filter_unit.h:164-178`).
- **What it does.** Per channel sample for 24 dB: 2 `tpt` divisions, `fastmath::tanh` between the sections (a division and 2 compares), 4 `flushDenormal` compares.
- **Consequence.** A NEON Analog loop like the Digital one, with `fastmath::tanh` per lane: bit identical.
- **Estimated saving.** Taken as 20 to 35 % of the filter (the Digital NEON loop costs 233 cycles per channel sample against about 240 for the scalar Analog, so the gain is uncertain until F2 is in). Lorenz Garden F2 7.9 x 0.2 = 1.6; 7.9 x 0.35 = 2.8, **1.6 to 2.8 %**; Parallel Dusk Pad F1 **1.7 to 3.0 %** (needs per lane ramp setups); Shepard Ascent F1 **1.1 to 1.9 %**.
- **Confidence.** Low. Do it after F2 and measure the Digital loop first.

### F14. SVF denominators: a true division per sample at tier low (rounding level)

- **Where.** `classic_filters.h:562` (`tpt`), `:266, 277` (Digital NEON), `fx_eq.h:155` (EQ).
- **What it does.** `(ic1 + g v3) / (1 + g R + g g)`: the denominator is constant for the 8 sample piece (classic) or the whole call (EQ).
- **Consequence.** A precomputed reciprocal changes each step by at most 1 ulp; the recursion is stable, so the difference stays about 1e-7 relative (about -120 dB). Not bit identical: tier low only (economy Math), listed in `docs/MPC.md`.
- **Estimated saving.** Stretched Glass Choir Digital 24: 4 divisions and 2 lane round trips per sample pair, 56 + 40 = 96 cycles x 1024 / 46.7 k = **2.1 %**. Analog 24: 2 x 14 = 28 cycles per channel sample: Lorenz Garden F2 1536 x 28 / 46.7 k = **0.9 %**, Parallel Dusk Pad F1 **0.9 %**, Shepard Ascent F1 **0.6 %**. EQ (3 bands): 3 x 2 x 128 x 14 / 46.7 k = **0.2 %** (Lorenz Garden, Stretched Glass Choir, Ring Mod Gamelan).
- **Change sketch.** Add `inv1`, `inv2` to `SvfSetup` (computed where `den1`, `den2` are), `v1 = (ic1 + g v3) * inv` when `fastMath`; the same for `Sv` in `fx_eq.h` when the module runs with `lowCost`.

### F15. EQ: a bell band at exactly 0 dB still runs (rounding level)

- **Where.** `fx_eq.h:61-77` (law 0), `designSvf` `:131-136`.
- **What it does.** At `A = 1` the band computes `hp + R v1 + v2 = u` up to rounding: a full SVF section with a division per sample per channel for an identity.
- **Consequence.** Rounding level; when the gain later leaves 0 dB the band starts from a zero state (a tiny transient scaled by `A - 1`). Not bit identical.
- **Estimated saving.** One of three bands of the measured EQ: 1.32 / 3 = **0.4 to 0.5 %** (Lorenz Garden, Ring Mod Gamelan, Stretched Glass Choir all have the mid band at 0 dB).
- **Confidence.** Medium; risk medium. Rank it last.

### F16. Delay: per sample divisions and per call libm

- **Where.** `fx_effects.h:78-145`: `pow (0.5, ...)` `:95`, `LoopBand::set` x 2 `:96-97` (4 `exp2f` and 4 coefficient calls), `sr / fLast` x 2 per sample `:103`.
- **Consequence.** Bit identical: cache `glide` and the bands on their inputs; skip the glide and the divisions while `fLast[c] == fTarget[c]` (the fixed point).
- **Estimated saving.** 2 x 14 x 128 = 3.6 k plus 36 libm x 80 to 200 per block: 6.5 k / 46.7 k = 0.14 %; 10.8 k / 46.7 k = 0.23 %, **0.1 to 0.2 %** (Lorenz Garden, Ring Mod Gamelan, Shepard Ascent, Triple Fifth Lead). Same pattern in `Chorus::process` `:201-203` (one `LoopBand::set` per call).

### F17. Hyper / Dimension: libm of a constant angle per voice per sample (XL, or Key 61 with `fx=all`)

- **Where.** `core/fx-serum-a/src/hyper_dimension.cpp:157-188, 214-215`.
- **What it does.** Per sample per unison voice: `sinf (phase)`, `cosf (angle)`, `sinf (angle)` where `angle` and `pan` (a division) depend only on `v` and `unison`; per sample `sinf`, `cosf` of the Dimension LFO.
- **Consequence.** Hoisting the angle terms and the pan division out of the sample loop: bit identical.
- **Estimated saving.** Key 61 `low`: 0 (masked; only its makeup runs). Key 61 `low,fx=all` (Triple Fifth Lead +4.1 points median in `configs.csv`: 72.99 to 77.07): with 4 unison voices, 8 of 14 libm per sample saved: 8 x 128 x 80 / 46.7 k = 1.8 %; x 200 = 4.4 %. XL: about 0.4 % (fast libm on the A76, estimate).

### F18. Distortion: `pow10` and `exp` of constants per sample

- **Where.** `fx_effects.h:614` (`d = pow10 (dExp)`), `:617` (`v / d`, used only by types 4 and 5), `:646` (`exp` of a constant for the DC blocker).
- **Consequence.** Hoist into `process` (bit identical). On the XL these are `powf` and `expf` per sample per channel. None of the six uses Distortion.

### F19. Compile flags (measure, do not assume)

- `-fno-math-errno` lets GCC hoist and share libm calls with loop invariant arguments and drops the `sqrtf` fallback branch. Results are the same function values; check with `--verify` against the current build because GCC may fold a call with constant arguments at compile time.
- `-mtune=cortex-a17` (scheduling only, same arithmetic).
- Not recommended: `-ffast-math` or `-funsafe-math-optimizations` (they would give NEON compares but also reassociation: not bit identical), `-mfpu=neon-vfpv4` without `-ffp-contract=off` everywhere (fused multiply-add in the files without the pragma).
- Gain unknown; only a device run decides.

### F20. Open: Frozen Pluck Drone loses 3.3 points with a masked Convolve switched off

`variants.csv`: base median 65.85 %, `fx_convolve_off` 62.51 %, at `low` where Convolve is masked. The code review explains about 0.5 to 1 % of it (Bus 1 becomes active: `makeupOfBus` double accumulations `own_engine.cpp:327-333`, the meter, `makeKey` with `powf` and 4 `roundf` calls `fx_modules.cpp:892-912`, two `weightedMean` with 54 double divisions each `fx_makeup.cpp:470-491`, and the full Bus 1 update of F11). Hypothesis for the rest: the spectrum job of `FxWorker` runs on the same pinned core as the bench. Not one of the six; measure before acting.

## 6. Already optimised (do not redo)

- Transcendental memos of the Serum filters: `tanM`, `pow2M`, `pow10M` `filter.cpp:454-497`; the coefficient hold of 4 at tier low `filter_unit.h:129`, `filter.cpp:171-177`.
- Pair kernels for Scream LP / BP, EMS Ladder, MG Ladder, Diffusor `filter.cpp:187-449` (measured gain, section 1); switchable with `--scalar-filters` (`engine.h:192-196`).
- Kept coefficients in the scalar modules: `OnePoleTPT` `filter_dsp.h:51-56`, `SVFTPT` `dCached` `:111-121`, `Ladder4Pole` `:159-170`.
- `DelayLine::reset` clears only a written line `filter_dsp.h:348-357`; no integer modulo in the index arithmetic `:365-367`, `filter.cpp:155`.
- Classic models: parameter memo `remember` / `ensureSvf` `classic_filters.h:158-187`; one call for an unmoving period `:97-103`; `processPair` for equal unmoving setups `:367-386`; `processRampPair` with one setup for both channels `:332-366`; `adoptRamp` `:134-138`; interpolated setups on the 8 sample grid `:492-512`; the Digital NEON loop `:226-299`; the NEON clamp in the classic path `filter_unit.h:180-186`.
- Voice: a filter that is off is not configured `voice.cpp:1074-1077`; only used targets are cleared and mixed `:1129-1157, 1394-1401`.
- Effects: rack early exit when nothing is on and nothing changed `fx_chain.cpp:362-366`; silent rack skip at tier low `:747-769`; the mute of a mix 0 effect with a tail flush `fx_module.h:144-153, 161-180`; the EQ skips Mid 2 when off `fx_eq.h:64, 73`; chorus LFO refresh every 4 samples at tier low and a NEON tap group `fx_effects.h:213-231, 315-372`; chorus band coefficients once per call `:200-203`; phaser coefficients every 8th sample at tier low; Utility bypass and coefficient caching `utility.cpp:72-100`; `MakeupCache` of the loudness band gains `fx_modules.cpp:90-113`; the Convolve spectrum on the helper thread `fx_modules.cpp:809-832`; the heavy effects masked at tier low `quality.h:112`.

## 7. Effects: cost model per module and what a disabled module still costs (Key 61 `low`)

"Per call" = per control period (4 per block). "Off" = `on = 0` in the preset. "Masked" = on in the preset, removed by the tier mask (`fx_chain.cpp:385-391, 411`). "Mix 0" = the economy Control mute (`fx_module.h:146`). "Silent" = the rack skip at tier low (`fx_chain.cpp:765-769`).

| Module | Per sample work (both channels) | Per call work | Off | Masked at low | Mix 0 | Silent input | Measured |
|---|---|---|---|---|---|---|---|
| Chorus | phase division, `ffloor`; every 4th sample 16 tap LFOs (table sine); 4 NEON tap groups (16 gathers, cubic, feedback, band pass, write) with 32 scalarised lane compares | `LoopBand::set` (4 libm), `dbToGain` level | nothing | not masked | flushed, then only the phase loop (a division per sample) | idle phase loop | 6.6 to 6.7 % |
| Compressor | 4 to 8 Linkwitz-Riley sections per channel, per band 2 envelope divisions, `powG` (fast pow) | about 8 `expf`, `powf` | nothing | not masked | never muted (no `wetMix`) | rack skip | 2.1 % (Mid Side Halo) |
| Delay | 2 divisions, 2 cubic reads, 2 limits, 2 band passes | `powf`, 2 x `LoopBand::set` | nothing | not masked | muted, 20 s flush | skip after 20 s | 1.2 % |
| Distortion | shape with `pow10` and a division per sample, filter optional | `powf`, coefficient | nothing | not masked | muted | rack skip | 1.3 % (Triple Fold Reese) |
| EQ (law 0) | 3 or 4 SVF sections per channel, a division each | 4 x (`powf`, `tanf`, 2 `sqrt`) | nothing | not masked | no mix: always runs; a 0 dB band runs | rack skip | 1.3 to 1.4 % |
| FX Filter | a `FilterUnit` (section 5 applies) and a dry / wet loop | filter setup | nothing | not masked | muted | rack skip | none of the six |
| Flanger | delay read, LFO, feedback per channel | `sinf`, `cosf` | nothing | makeup only (see last row) | muted | idle LFO | masked |
| Phaser | 12 all-pass stages per channel, coefficients every 8th sample, fast tanh | `log2f` | nothing | not masked | muted | idle LFO | 3.5 % (Mid Side Halo) |
| Reverb | FDN with per sample LFO modulation (XL) | about 10 libm | nothing | makeup only | muted, 20 s flush | idle | masked |
| Hyper / Dimension | per unison voice 3 libm and a cubic read; Dimension 2 libm and 4 cubic reads | coefficients | nothing | makeup only | internal bypass only at mix 0 and level 0 dB | rack skip | masked (F17) |
| Utility | width, pan, mix, 2 flushes; HPF, LPF only when set (`hpf > 20.5`, `lpf < 19990`); mono bass 4 one poles | `dbToGain` | nothing | not masked | internal bypass at mix 0 and 0 dB | rack skip | 0.5 to 1.1 % |
| Bode | 64 tap Hilbert per channel, double `cos` and `sin` per sample | `powf` | nothing | makeup only | never muted (latency) | rack skip | masked |
| Convolve | partitioned FFT convolution (XL) | key, helper thread rebuild | nothing | makeup only, see F20 | never muted | rack skip | masked |
| Splitters L/H, L/M/H, M/S | crossover filters, band racks | `dbToGain` of the level even when off (F11) | `dbToGain` only | not masked | n/a | rack skip | 0.3 % |
| Makeup gain of a masked effect | `ProgramMeter::accumulate` (about 30 operations per sample) and a gain ramp when not 1 | `endPeriod` (4 double `exp`, 7 double divisions), `desc`, the model (2 to 4 libm), `dbToGain`, `sqrt`; on a bus `makeupOfBus` (3 double sums per sample, double `exp`, `expf`, `sqrt`) | none | this row | n/a | the meter is skipped with the rack | in "fx: update" |
| Chain bookkeeping | `finiteBlock` per module (2 compares per sample, `fx_chain.cpp:50-56, 562`), peak in and peak out at tier low (4 compares per sample, `:756-758, 771-774`) | `update` per rack per period (F11) | rack early exit only without global modulation | | | | 1.4 to 1.8 % (update) |

## 8. The earlier hypotheses, checked against the calling loop

From `agy-g68-review-filter-fx/docs/release/CPU-REVIEW-FILTER-FX.md`:

- "filters-serum is highly vectorized": **refuted.** Only 5 types have a 2 lane kernel (`filter.cpp:191-196`); everything else runs one channel after the other through `processInternal`, a call per sample; and the existing kernels' compares are scalar in the arm32 binary (F2).
- "Monolithic `FilterImpl` allocates and initialises every buffer, wasting setup and cache locality": **refuted as an audio cost.** The buffers are allocated in `prepare` (`filter.cpp:34-63`), `reset` clears only written lines (`filter_dsp.h:348-357`), a model touches only its own lines. It is a memory cost (about 53 KB per `Filter`, 4 per voice) and may be part of `prepare_voices` 95.6 ms at start-up (`docs/MPC.md` start-up status lines), not of the block.
- "`FxPool` allocates on the audio thread": **refuted.** Modules are created on the thread that sets the type and handed over by an atomic pointer (`fx_chain.h:40-62`, `fx_chain.cpp:239-249`).
- "Bypass and makeup handling per block, early-out for silent stages, 2 to 5 %": **mostly refuted.** Silent racks and mix 0 modules are already skipped at tier low (section 6). The makeup work that remains is per period and small (inside the measured 1.4 to 1.8 % "fx: update"); the real waste is the update that never takes its early exit (F11, 0.6 to 1.2 %).

From `agy-g62-k61-cpu/docs/release/KEY61-CPU-ANALYSIS.md`:

- "Filter models Multi SVF and Comb are dominant": **Multi SVF confirmed** (Triple Fifth Lead 31.5 %, Ring Mod Gamelan 31.1 %, Shepard Ascent 16.5 %); **Comb refuted** (none of the six uses a Comb model). The other dominant models are Smear Allpasses (34.1 %), Flanges Flg HL (39.7 %) and Ring Mod (15.0 %).
- "Impulses / Convolve consistently at the top": **refuted for the six** (none uses an impulse or Convolve) and **refuted for the mechanism**: tier low masks Convolve (`quality.h:112`), so only its makeup model runs. One open measurement (F20) shows a masked Convolve is not free in Frozen Pluck Drone.
- "Vectorize filter kernels, target `core/include/synthyboy/filters.h`, 20 to 25 %": **direction confirmed, file refuted.** That file does not exist; the code is `core/filters-serum/src/filter.cpp`, `filter_lanes.h` and `core/engine-own/voice/classic_filters.h`. 20 to 25 % is within the per preset ranges of F1, F3, F4.
- "Lazy modulation evaluation", "Hoist per voice modulation", targets `core/voice_engine.cpp` and `core/voice_manager.cpp`: outside this review's area; both files do not exist (the voice code is `core/engine-own/voice/voice.cpp`, the matrix `core/engine-own/matrix/mod_matrix.cpp`). Measured "modulation (env/LFO/matrix/sources)" is 3.2 to 8.2 % of the block for these presets (`sections-by-category.csv`).

## 9. Ranking: (saving x certainty) / risk, sound neutral first

Ranges are per heavy preset where the finding applies; they overlap where noted, so do not add them blindly.

| Rank | ID | Saving (% of the block) | Sound | Risk | Change in one line |
|---|---|---|---|---|---|
| 1 | F3 | Lorenz Garden 15 to 22 | bit identical | low | Allpasses as a variant of the existing Diffusor pair kernel (6 stages, its tap factor and gain), lane sanitize before the writes |
| 2 | F1 | Triple Fifth Lead 14 to 20, Ring Mod Gamelan 14 to 20, Shepard Ascent 7 to 11 | bit identical | medium | Multi SVF pair kernel: per lane `g`, `g2`, `dCached` on held cutoff change, two SVFs and the stage selects in lanes |
| 3 | F4 | Parallel Dusk Pad 5 to 9 (step A), 18 to 26 (A and B) | bit identical | low (A), medium (B) | Flanges: setup (log10, divisions, `tanM`) once per held cutoff; then a pair kernel with per lane taps |
| 4 | F2 | Stretched Glass Choir 2.2 to 3.5; 0.8 to 1.4 on each chorus preset; lowers every kernel's cost | bit identical | low | NEON compares on the bit patterns with integer instructions in `filter_lanes.h`, `classic_filters.h`, the chorus |
| 5 | F6 | Ring Mod Gamelan 1.6 to 2.7, Triple Fifth Lead 0.8 to 1.3, Lorenz Garden and Parallel Dusk Pad 0.6 to 1.0, Shepard Ascent 0.4 to 0.7 | bit identical | very low | Serum path: NEON runaway test and `simd::clampTo` instead of the scalar loops |
| 6 | F7 | Ring Mod Gamelan 2.6 to 5.7 (Ring Mod part; the rest is inside F1, F3, F4) | bit identical | low | per call constants and per hold setup out of `processInternal`; inline the helpers |
| 7 | F5a | Ring Mod Gamelan 1.8 to 4.4 | bit identical | low | one ring mod carrier per voice when both channel phases are equal |
| 8 | F11 | 0.6 to 1.2 on all six | bit identical | low to medium | structural early exit for empty racks; cache `dbToGain`, EQ designs, meter constants |
| 9 | F10 | 0.3 to 1.1 | audio bit identical | very low | NEON `peakAbs` for the strip peaks |
| 10 | F8 | 0.2 to 1.1 | bit identical | low | cache key track, pan and level conversions per voice filter |
| 11 | F12 | 0.5 to 0.9 on the chorus presets | bit identical | low to medium | hoist `rate / sr`; refresh 4 chorus taps per NEON register |
| 12 | F13 | Lorenz Garden 1.6 to 2.8, Parallel Dusk Pad 1.7 to 3.0, Shepard Ascent 1.1 to 1.9 (low confidence) | bit identical | medium | NEON pair loop for the classic Analog model |
| 13 | F9 | Stretched Glass Choir, Triple Fifth Lead 0.2 to 0.4 | bit identical (zero sign only) | low | skip the wrapper of an off filter with no input |
| 14 | F16 | 0.1 to 0.2 | bit identical | low | delay: cache glide and bands, skip divisions at the fixed point |
| 15 | F17 | 0 at `low`; 1.8 to 4.4 with `fx=all`; about 0.4 on the XL | bit identical | low | Hyper: hoist the constant angle `cosf`, `sinf` and the pan division |
| 16 | F18 | none of the six | bit identical | low | Distortion: hoist `pow10` and `exp` of constants |
| 17 | F19 | unknown | `-fno-math-errno` same values (verify); `-mtune` same arithmetic | low | try the two flags, measure |
| 18 | F5b | Ring Mod Gamelan 1.4 to 4.1 more | rounding level, about -106 dB, tier low | low | table sine for the ring mod carrier at tier low |
| 19 | F14 | Stretched Glass Choir 2.1; Analog presets 0.6 to 0.9; EQ 0.2 | rounding level, about -120 dB, tier low | low | reciprocal of the SVF denominators at tier low |
| 20 | F15 | 0.4 to 0.5 | rounding level | medium | skip an EQ bell band at exactly 0 dB |

**Rough effect per preset of the sound neutral items (not additive in a strict sense):** Lorenz Garden 20 to 30 points off a median of 96 %; Parallel Dusk Pad 23 to 33 off 88 %; Ring Mod Gamelan 22 to 35 off 85 %; Triple Fifth Lead 18 to 26 off 83 %; Shepard Ascent 11 to 17 off 62 %; Stretched Glass Choir 5 to 8 off 66 %. Stretched Glass Choir's p99 of 101 % comes from the spectral morph rebuild spikes (6.2 % median share in `sections-by-category.csv`), which are outside this area.

## 10. What only the device can decide (bench runs)

Use `synthyboy_tier_bench --presets content/presets --core 3 --config low` with:

- `--load "Lorenz Garden" --sections` before and after F3; `--scalar-filters` gives the plain path with the same binary.
- `--load "Triple Fifth Lead" --load "Ring Mod Gamelan" --load "Shepard Ascent" --sections` for F1, F6, F7, F5.
- `--load "Parallel Dusk Pad" --sections` for F4 (step A and step B separately).
- `--load "Stretched Glass Choir" --sections` for F2 (the Digital loop and the chorus sections), F14.
- `--load "Frozen Pluck Drone" --sections` with and without `convolve.on` for F20; also run it with the `FxWorker` thread on another core to test the pinning hypothesis.
- `--config "low,fx=all" --load "Triple Fifth Lead"` for F17.
- `--verify` after every bit identical change (it must report the same samples); for F5b, F14, F15 a listening and difference check instead, and an entry in `docs/MPC.md` "Sound differences at tier low".
- The cycle cost of a scalar compare plus `vmrs` on the A17 (the 7 to 11 cycles used above) is an estimate; F6 is the cleanest single measurement of it (one loop changed, nothing else).
