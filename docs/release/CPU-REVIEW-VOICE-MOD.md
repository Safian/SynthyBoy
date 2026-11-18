# CPU review: voice management, modulation and the master stage (Key 61, tier `low`)

> **Implementation status (branch `work/opt-voice`, 2026-11-19).** Done and bit identical: V1, V2, V3, V4, V5, V6, V7, V9, V10, V11, V12, V13 (commits below each finding). Skipped: V8 (changes the display and, after an edit, the audio). Not in this round: V14, V15. The legato bug of the V1 side note is confirmed and reported separately (`core/tests/test_own_voice_legato_env.cpp`, built, not in CTest). Proof: `synthyboy_test_own_voice_opt` (old code against new, selectable by `setOwnSavingsOff (kOwnSavingVoiceControl)` / `tier_bench --no-voice-savings`) and the exhaustive tests named at each item.

Read only review of `feature/mpc-overlay` at 8e5cf113 (2026-11-18). No source file was changed, nothing was built or run, no device was touched. Every file:line below was re-opened and checked in that tree. The findings of `CPU-REVIEW-OSC.md`, `CPU-REVIEW-FILTER-FX.md` and `CPU-REVIEW-CONTENT.md` are not repeated; where a finding of theirs is in this area it is named and only new evidence is given.

Scope: `core/engine-own/own_engine.cpp` (block entry, control tick, voice loop, mixer, master), `core/engine-own/voice/voice.cpp` and `voice.h` (tick and render, note start, release, stealing helpers), `filter_setup.h`, `core/engine-own/matrix/` (compile, evaluate, laws), `core/engine-own/env_lfo/` (envelopes, LFOs, random LFOs, drawn shapes), the parameter snapshot and smoothing in the engine, the voice routing and mix (`halfSum`, `route`, sends, strip meters), the M/S splitter question, the master stage.

## 0. Sources, method, constants

Evidence, in order of weight:

1. Device measurements, Key 61, core 3, 1.608 GHz, `low`:
   - `.hw-local/mpc-perf/round10/k61-breakdown/sections.csv`, `sections-by-category.csv`, `variants.csv`, `configs.csv`, `raw/sections-*.txt`, `raw/variants-*.txt`;
   - `.hw-local/mpc-perf/round12/k61-r12-sections.csv` (after the filter and oscillator rounds; no Mid Side Halo and no Shepard Ascent rows).
2. The round 10 PC sample profile `round10/k61-breakdown/prof/pcs-{Lorenz_Garden,Stretched_Glass_Choir,Mid_Side_Halo,F8_2}.txt`, binned by me with `prof/syms-arm32.txt`. It is a normal build (no section scope symbols among the samples). I map hot PCs to instructions with the unstripped `round10/bench-build/arm32/bench/synthyboy_tier_bench` (`llvm-objdump`); in the voice and matrix region that binary sits 0x90 bytes lower than the symbol file (checked on `paramIndex`, `Voice::tick`, `Voice::render`). The binary predates 25488aa2 and the round 11/12 merges; none of them touched the code of the findings below except where said.
3. Reading the code.
4. Cycle estimates, labelled as estimates: NEON 4 float multiply add about 2 cycles, scalar float op 1 to 2, float divide about 14, float libm 80 to 200, double libm 150 to 400, 64 bit conversion 30 to 50, atomic load about 5, a VFP compare with `vmrs` 2 to 8 extra cycles (the FILTER-FX fit puts it near the low end).

One block = 128 frames at 44.1 kHz = 2.902 ms = 4.666 M cycles. **1 % of the block = 46.7 k cycles.** One block = 4 control periods of 32 samples. "Voice period" (vp) = one voice in one control period; a preset with V voices has 4 V voice periods per block.

Converting profile shares: the bench runs blocks back to back, so a share of samples is a share of `process()` time; share of the block = share of samples x the median load of the variant run (Lorenz Garden 85.2 %, Stretched Glass Choir 56.7 %, Mid Side Halo 67.0 %). Setup samples (preset load, wavetable builds) dilute this a little, so these conversions are slightly low.

Bench facts that matter (`bench/tier_bench.cpp:382-405`): every 1.00 s the chord is released and struck again in the same block; no controller, no aftertouch, no parameter move is sent. With a full pool the released voices are stolen at the next control tick (`own_engine.cpp:692-710`, tier low: `ghostKill`), so no release tails run. All 160 factory presets store every macro at 0, use no slew, no power curve, no bipolar slot, no delay sync (inventory of `content/presets/**`), and 157 of 160 have `smooth = 1` on all ten LFOs.

**A caution about the section numbers.** One section scope costs about 1285 ns on the Key 61 (`raw/sections-Mid_Side_Halo.txt`); the parser subtracts half a scope per clock read (`scripts/parse_sections.py`). The corrected sums still overshoot the real medians by 3.2 points (Mid Side Halo: 70.2 % against 66.99 %) and 3.9 points (Lorenz Garden: 89.1 % against 85.2 %). The overshoot is about 60 ns per clock read, and it lands mostly in the sections with many child scopes: "voice tick: other" (11 children per call) and "voice render: routing / mix / output" (7 children per call). Expect these two to be overstated by roughly 0.5 to 0.7 points each.

## 1. The headline

- The per voice control and routing work (every "voice tick" section except the oscillator update, plus both "voice render" sections) is **12.3 to 21.1 points** of the block in the seven presets (section 4). It is not one hot spot. It is about 300 parameter reads per voice period, each a load pair, a VFP compare and a `vmrs`; settings that are rebuilt every period although nothing moved them; per lane libm calls in the LFOs; and a few scalar loops.
- The two biggest certain items are caches that already exist but switch off too easily: the envelope settings cache is dropped whenever *any* parameter of the voice is modulated (V1), and the LFO evaluates its settings and its smoothing coefficient (`exp2f`) per lane per period (V3).
- `simd::ramp4` stores its index vector to the stack and reloads it, a store forwarding stall in every NEON ramp. The profile shows the ramp loop at four times the time of a NEON loop of the same length (V2).
- The matrix evaluates every slot whose depth is not zero, both lanes, even when the source is exactly 0 (all macros, the mod wheel and the aftertouch in the bench). 37 to 54 % of the matrix time goes there (V4).
- The FILTER-FX finding F10 (scalar strip peak loops) is worth more than its estimate: the profile puts the `peakOf` loops at 1.2 to 2.0 % of all samples, about 1.0 to 1.3 % of the block (section 7).
- The M/S splitter question (section 5): switching the splitter off also switches off the chorus, compressor, EQ and phaser, because they sit in its band racks 8 and 9. Their sections add up to 13.6 points, and the four single module variants add up to 13.5 points. The remaining 8 points appear only when all band effects go together; no code in this area explains them. Device runs that separate them are listed.
- Together the sound neutral items here are worth roughly **4 to 7 points** on the 8 voice presets and 3 to 5 on the 4 and 6 voice ones (overlaps removed roughly). They are all bit identical except V8, which changes only what the display shows for voices other than the newest one.

## 2. The call chain and the loop nest

```
OwnEngine::process                                   per block            own_engine.cpp:1265
  snapshot (only when dirty: copy 2637 atomics, ModMatrix::compile, tuning)  :1290-1305, 1332-1334
  while (pos < numFrames)                            per control period   :1398
    controlTick                                                           :1098-1263
      10 global LFO rates + phases, 4 global random LFOs                   :1108-1127
      macros, killExcess, updateLowestNote, 3 scans of the 36 voice pool   :1129-1149
      for each active voice: Voice::tick             per voice period     :1150-1152
        pitch, glide, tuning; voice control (cached)                      voice.cpp:429-476
        envelopes: 6 x envSettings + Envelope::advance                    :565-600
        LFOs: 10 x lfoValue (unused ones every 4th period)                :604-615, 299-392
        random LFOs: 4                                                     :616-640
        sources: fill 58 values                                            :643-685
        matrix: clearMod, srcR copy, ModMatrix::evaluate                   :687-710
          for each compiled slot: one () for both lanes  per route        mod_matrix.cpp:412-436, 337-399
        amplitude                                                          voice.cpp:741-776
        3 oscillators: about 45 P () reads each, WtOsc::update             :785-992
        sub, noise, 2 filters (fillFilterSettings, FilterUnit::set)        :994-1106
      global matrix evaluate (once)                                        own_engine.cpp:1229
    FxChain::update x 3 racks                                             :1443-1447
    mixer levels, latency plan (only when inputs change)                  :1449-1499
    events                                                                :1501-1514
    for each active voice: Voice::render             per voice period     :1530-1532
      per oscillator: WtOsc::render, halfSum, peakOf, route               voice.cpp:1189-1244
      sub, noise                                                           :1245-1322
      runFilter x 2 (copy or process, rampLerp, 2 x mulBy, halfSum, peakOf) :1325-1351
      toMain, extNext copy                                                 :1352-1394
      output: 2 x rampLerp, NEON mixLane per used target, addScaled to bus :1396-1448
    strip meters, bus peaks, bus delays, runBus x 2, Main rack            own_engine.cpp:1534-1586
    master: per sample volume ramp, stereo routing, Direct, outputLimit, peak  :1589-1604   per sample
  killExcess, count, publishStats, writeReadout     per block            :1615-1621
```

Per sample work in this area is small: the master loop (128 samples), the mixer lanes and ramps (NEON), the strip peaks (scalar). Everything else runs per control period, per voice period or per slot.

## 3. What the seven presets run (from the preset files)

| preset | voices (vp / block) | oscillators on (unison) | filters (routing) | voice slots (source 0 in the bench) | used LFOs | notes |
|---|---|---|---|---|---|---|
| Lorenz Garden | 6 (24) | 3 (3, 2, 1) + sub | 2 (series 1>2) | 22 (11) | none; rlfo 1 to 4 (Lorenz, Rossler, Perlin, sine) | env1 attack 2.6 s |
| Stretched Glass Choir | 8 (32) | 2 (1, 3) | 1 (F2 off) | 14 (9) | lfo1 sine; rlfo 1 to 3 | |
| Parallel Dusk Pad | 6 (24) | 2 (2, 1) | 2 (parallel) | 16 (10) | lfo1 triangle; env4 | |
| Ring Mod Gamelan | 8 (32) | 2 (1, 1) | 2 (series 1>2) | 10 (6) | none; env2, env3 | env1 decays all second |
| Shepard Ascent | 4 (16) | 2 (1, 1) | 2 (series 1>2) | 16 (9) | lfo1 saw, lfo2 triangle; rlfo1 Lorenz | |
| Triple Fifth Lead | 8 (32) | 3 (5, 1, 1) | 1 (F2 off) | 13 (10) | none; env2 | |
| Mid Side Halo | 8 (32) | 3 (3, 1, 3) + sub | 1 (F2 off) | 10 (6) | lfo1 sine; env2 | chorus, phaser in rack 9, compressor, EQ in rack 8 |

All ten LFOs have `smooth = 1`, shape 0 (sine) unless listed. Envelopes 2 to 6 that no slot reads are the defaults (attack 5 ms, decay 400 ms, sustain 100 %, curves 0 / -3 / -3). "Source 0 in the bench": slots whose source is a macro, the mod wheel or the poly aftertouch.

## 4. The per voice fixed cost: what it is made of

From `sections.csv` (round 10), points of the block:

| preset | voices | control (tick sections less oscillator update) | render routing + output | sum | per voice | of which modulation (env/LFO + matrix + sources) |
|---|---|---|---|---|---|---|
| Lorenz Garden | 6 | 13.30 | 5.71 | 19.01 | 3.17 | 6.21 |
| Stretched Glass Choir | 8 | 14.80 | 5.97 | 20.77 | 2.60 | 7.31 |
| Parallel Dusk Pad | 6 | 10.98 | 4.59 | 15.57 | 2.59 | 5.19 |
| Ring Mod Gamelan | 8 | 13.06 | 5.59 | 18.65 | 2.33 | 6.09 |
| Shepard Ascent | 4 | 9.04 | 3.30 | 12.34 | 3.08 | 4.23 |
| Triple Fifth Lead | 8 | 14.14 | 6.67 | 20.81 | 2.60 | 6.56 |
| Mid Side Halo | 8 | 13.83 | 7.26 | 21.09 | 2.64 | 6.43 |

Round 12 gives the same within 1 point. Less the clock residual of section 0 (about 2 points), the control and routing part is about 10 to 19 points. The profile agrees: for Mid Side Halo `Voice::tick` and its lambdas, `lfoValue`, `Envelope::advance`, `ModMatrix::evaluate`, `P`, `modulatedValue`, `Voice::render` and its lambdas are 21.9 % of all samples, about 15 % of the block, before memset, memcpy and libm.

I could not reproduce the figures "12.6 to 18.4 points" of the task from the CSV files; the table above is my own sum.

What a voice period spends, Mid Side Halo, from the profile (share of all samples): `Voice::tick` body 6.84, envelope settings lambda 1.19, `lfoValue` and its lane 1.98, matrix 2.48, outlined `P` 0.47, `modulatedValue` / `modulatedSlow` / `destLawOf` 0.67, `ffloor` 0.20, `paramIndex` 0.13, `Voice::render` body 3.93, render lambdas 3.20. The tick body is spread thin: the hottest 64 byte window holds 2.8 % of its samples; the pattern that repeats is the inlined `P ()`: load `modP`, load `gl->base`, `vcmp`, `vmrs`, branch. `Voice::tick` is 20 KB of code in the bench binary (`0x4fd8` bytes), with 241 `vmrs` and 46 calls to the outlined `P`.

## 5. The M/S splitter: why switching it off saves 22 points

Measured (`variants.csv`, Mid Side Halo, median of one 4 s pass): base 66.99 %; `fx_splitter_ms_off` 45.30 % (-21.7); `fx_all_off` 41.96 % (-25.0); single modules off: chorus -7.13, compressor -2.07, EQ -1.13, phaser -3.13, utility -1.16, reverb -0.38.

What the code does. The preset puts chorus and phaser in rack 9 and compressor and EQ in rack 8 (`chorus.rack 9`, `phaser.rack 9`, `compressor.rack 8`, `eq.rack 8`). Racks 8 and 9 are the Mid and Side bands of the M/S splitter (`fx_chain.cpp:601-608`). A module in a band rack belongs to the chain only while its splitter is on: `mine = ... (r >= 8 && splitterHere[2])`, `on[m] = on[m] && mine` (`fx_chain.cpp:410-419`). With `splitter_ms.on = 0` the four modules are neither updated (`:451-464`) nor run. So the variant is "M/S splitter plus chorus, compressor, EQ and phaser off", not "splitter off".

The arithmetic. Exclusive sections of these modules (`sections.csv`): chorus 6.71 + compressor 2.11 + EQ 1.30 + phaser 3.51 + splitters 0.34 = **13.97 points**, plus their share of "fx: update" (at most 1.92). The four single module variants add up to 7.13 + 2.07 + 1.13 + 3.13 = **13.46 points**: the sections and the variants agree module by module. In the other six presets "all fx off" saves what the fx sections add up to, plus 0.5 to 1.3 points. In Mid Side Halo, "all fx off" saves 25.0 points against 17.1 points of fx sections and 15.0 points of single variants. So **about 8 points appear only when every band effect goes away at once**.

What I checked and found not to be the cause:

- The splitter itself: split and merge are two scalar loops over 32 samples and 4 `dbToGain` per period (`fx-serum-a/src/splitter_ms.cpp`, `split`, `merge`); measured 0.34 % with its children taken out; profile `SplitterMS` 0.29 % of the samples.
- Latency: `rackLatency` of racks 8 and 9 feeds `LatencyPlan`; the delay lines run on all-zero Direct and bus paths at 2 x 32 samples per period. Tiny either way.
- The global modulation: the slots on `utility.width`, `phaser.mix`, `chorus.mix` and `reverb.*` read the mod wheel and macros, which are 0 in the bench, so their result is 0 and nothing is touched (`mod_matrix.cpp:429-430`); `gnumTouched` stays 0 and the mixer cache holds.
- The silent rack skip at tier low works per chain, not per band rack, and the Main chain is active in both variants (utility, the reverb makeup).

So the 8 points are not explained by work that the code does only with the splitter on. They are non additive, which points at something shared: cache or TLB pressure from the effect state (the chorus alone has a 256 KB tap store, `fx_effects.h:167-172`), or run to run spread (the same day's runs differ by 10 points on Lorenz Garden: 96.1 % and 85.2 %). Only the device can decide. Runs, with variant presets made like `scripts/mkvariants.py`, `synthyboy_tier_bench --presets var --core 3 --config low --seconds 10 --repeat 5`:

1. `Mid Side Halo@base` and `@fx_splitter_ms_off`, interleaved, 5 passes each: is the 21.7 stable?
2. `@ms_on_bands_empty`: splitter on, the four modules off. This is the cost of the splitter alone.
3. `@modules_in_main`: splitter off, the four modules on with `rack = 0`. This is the cost of the modules without the splitter.
4. `@four_off`: chorus, compressor, EQ, phaser off together, splitter on. If 4 is about 45 % and 2 is about 66 %, the effect belongs to the four modules together, not to the splitter.

## 6. Findings

Savings are per heavy preset in points of the block. "vp" = voice periods per block (4 x voices). Overlaps are noted; do not add the ranges blindly.

### V1. The envelope settings cache is dropped whenever any parameter is modulated

- **Status (work/opt-voice, 6e0a0b0d): done, bit identical.** Per envelope cache keyed on epoch, tempo, the envelope factor of the voice control (the exact input that `sequence` stands for) and the touched group mask; the old all envelope test stays in front unchanged, so the legato staleness of the side note is kept (it is a real bug: `core/tests/test_own_voice_legato_env.cpp`, aea3d5ea, fails on the code before and after; the fix changes output in that case and is an owner decision). The decimated envelopes skip the settings work when their cache holds.

- **Where.** `voice.cpp:568` (`envFromCache` needs `numTouched == 0`), `:569-578` (the loop), `:477-509` (`envSettings`), `:595-600` (the cache is marked valid only without modulation).
- **Loop level.** Per voice period x 6 envelopes.
- **What it does.** With any slot that moves any voice parameter (velocity to the cutoff is enough), every envelope rebuilds its settings every period: 9 `P ()` reads, 2 reads of `g.base`, 14 multiplies, the display lengths `envLen`. In all seven presets velocity slots are non zero, so the cache never hits. For an envelope that no slot reads, three of four periods compute the settings and then `continue` past them at `:586-591`: the work is thrown away.
- **Why waste.** `P (i)` returns `g.base[i]` exactly when `mod[i] == 0` (`voice.h:185-189`). An envelope none of whose parameters is a touched destination gets the same settings as the parameters alone. No preset of the seven touches an envelope parameter at run time (Stretched Glass Choir and Ring Mod Gamelan route macros, which are 0, to `env1.*`; a slot with result 0 touches nothing, `mod_matrix.cpp:429-430`).
- **Measured cost.** The settings lambda is 0.68 % (Lorenz Garden), 1.55 % (Stretched Glass Choir), 1.19 % (Mid Side Halo) of all samples, that is 0.58 %, 0.88 %, 0.80 % of the block: 144 to 192 calls per block, so **about 190 to 215 cycles per call**.
- **Consequence.** Bit identical. The display lengths of an unused envelope are refreshed when its settings are rebuilt, at the latest after an edit (display only).
- **Saving (calls per block x 150 to 215 cycles / 46.7 k).** Lorenz Garden 24 x 6 = 144 calls: **0.46 to 0.66**. Stretched Glass Choir 32 x 6 = 192: **0.62 to 0.88**. Parallel Dusk Pad 144: **0.46 to 0.66**. Ring Mod Gamelan 192: **0.62 to 0.88**. Shepard Ascent 96: **0.31 to 0.44**. Triple Fifth Lead 192: **0.62 to 0.88**. Mid Side Halo 192: **0.62 to 0.88**. A moved macro on `env1.*` brings back one envelope's share (1/6).
- **Confidence.** High (the lambda's own samples are measured). Risk low.
- **Change sketch.** Build a per voice "touched group" mask after `g.matrix->evaluate (ctx)` at `voice.cpp:710`: a table `uint8_t groupOfParam[paramCount]` made once in `prepare` (group ids: osc 1 to 3, sub, noise, filter 1, 2, env 1 to 6, LFO 1 to 10, rlfo 1 to 4, other), then `for i < numTouched: touchedGroups |= bit (groupOfParam[touched[i]])` (plus the filter 1 group when `padCutTouched`). Replace the single `envCacheValid` with a per envelope valid flag; use the cache for envelope e when `baseEpoch`, `bpm` and `sequence` match and bit (env e) is clear and not `fresh`; rebuild and store otherwise. Move the tier low decimation test (`:586-591`) before the settings so a skipped envelope does no settings work. Keep `envSettings` itself unchanged. V3 and V7 reuse the same mask.
- **Side note (correctness, not CPU).** The existing cache key has no `sequence`. A legato note (`legatoTo`, `voice.cpp:248-267`) re-evaluates the voice control (`vc.envFactor` can change with the step), but a valid envelope cache keeps the old times. This happens only without any modulation and with voice control envelope steps or random envelope set. Adding `sequence` to the key fixes it; that changes output only where the old output was stale.

### V2. `simd::ramp4` makes a store forwarding stall in every NEON ramp

- **Status (175e032c): done, bit identical.** `rampLerp` keeps the lane offsets in a register; `ramp4` unchanged for `wt_osc.h`. The GCC object code was not inspected (no GCC cross compiler on the Mac): the integration build should check `voice.cpp.o` for the `vstr` / `vld1` pair.

- **Where.** `dsp/simd.h:67-72` (`ramp4` builds `int32_t idx[4]` and loads it with `vld1q_s32`), used by `rampLerp` `:81-91`; callers `voice.cpp:1340` (per filter, also for an off filter), `:1399`, `:1400`, `osc/lib_osc.cpp:57`.
- **Loop level.** Per voice period x (2 output ramps + 2 filter level ramps) x 8 NEON steps.
- **What it does.** GCC computes the four integers in a register (`vdup.32`, `vadd.i32`), then stores them as two 64 bit halves (`vstr d16`, `vstr d17`) and reloads them with one 128 bit `vld1.32` (bench binary, `Voice::render` at 0x916c6 and 0x915b6, the filter lambda at 0x88914). A 128 bit load from two pending 64 bit stores cannot be forwarded.
- **Measured.** In the filter lambda the ramp loop has 275 samples against 71 for the `mulBy` loop right after it, same length, one multiply each: about **4 times** the time. All ramp loops together: 0.54 % (Lorenz Garden), 1.14 % (Stretched Glass Choir), 0.89 % (Mid Side Halo) of the samples, that is 0.46 %, 0.65 %, 0.60 % of the block.
- **Consequence.** Bit identical (same integers, same conversion, same multiply).
- **Saving.** About two thirds of the measured cost: Lorenz Garden **0.3**, Stretched Glass Choir **0.43**, Mid Side Halo **0.4**. By ramps per block (4 per vp x 100 to 160 cycles saved): Parallel Dusk Pad 96 ramps **0.21 to 0.33**, Ring Mod Gamelan and Triple Fifth Lead 128 ramps **0.27 to 0.44**, Shepard Ascent 64 ramps **0.14 to 0.22**. The content oscillators gain too (lib_osc).
- **Confidence.** High. Risk very low.
- **Change sketch.** `static const int32_t kIota[4] = {0, 1, 2, 3};` and in `rampLerp`: `const int32x4_t iota = vld1q_s32 (kIota);` once before the loop, then per step `vcvtq_f32_s32 (vaddq_s32 (vdupq_n_s32 (t0 + k + 1), iota))` times `vdupq_n_f32 (invP)`. Keep `ramp4` with the same body for the reference path in `wt_osc.h:1135` or switch it too. Check the object code for the absence of the `vstr` / `vld1` pair. `wt_osc.h:1066` already does it this way.

### V3. LFO: the settings and the smoothing coefficient are rebuilt per lane per period

- **Status (c2aad11d): done, bit identical.** LFO settings, both periods and both smoothing coefficients kept per voice and LFO; the random LFOs keep their keys and rate the same way. Old code selectable (`lfoValueReference`).

- **Where.** `voice.cpp:299-392` (`lfoValue`), in particular `:302` (`steps * kControlPeriod / g.sampleRate`, a double divide), `:303-311`, `:350`, `:359-361`, `:376` (about 20 `P ()` reads), `:306` (`lfoRateHz`), `:378` (`std::exp2 (-dt / half)`, a libm `exp2f` and a float divide per lane).
- **Loop level.** Per voice period x evaluated LFOs (used ones every period, unused ones every 4th period with 4 periods of time) x lanes (1: no preset uses stereo).
- **What it does.** Everything above depends only on parameters, `steps`, the tempo and `vc.lfoFactor`. Only the phase, the age and the smoothed value move.
- **Evaluations per vp.** Lorenz Garden 2.5 (10 unused / 4); Stretched Glass Choir, Parallel Dusk Pad, Mid Side Halo 3.25; Ring Mod Gamelan, Triple Fifth Lead 2.5; Shepard Ascent 4.
- **Measured.** `lfoValue` and its lane: 1.00 / 2.22 / 1.98 % of the samples (Lorenz Garden / Stretched Glass Choir / Mid Side Halo), 0.85 / 1.27 / 1.33 % of the block, plus the libm calls they make (`exp2f`, `cosf`) that are counted under libm.
- **Consequence.** Bit identical (the coefficient is the same expression of the same inputs).
- **Saving (evaluations per block x 300 to 500 cycles).** Lorenz Garden 24 x 2.5 = 60: **0.39 to 0.64**. Stretched Glass Choir 104: **0.67 to 1.11**. Parallel Dusk Pad 78: **0.50 to 0.84**. Ring Mod Gamelan 80: **0.51 to 0.86**. Shepard Ascent 64: **0.41 to 0.69**. Triple Fifth Lead 80: **0.51 to 0.86**. Mid Side Halo 104: **0.67 to 1.11**.
- **Confidence.** Medium high. Risk low.
- **Change sketch.** Per voice and LFO keep an `LfoSettings` struct (shape, sync, syncType, rate, knob, delay, fade, stereo, mono, host, smooth, smoothTime, swing, dir, custom pointer, `dt` per `steps` value, and `coef = 1 - exp2 (-dt / half)` for `steps` 1 and 4). Valid while `baseEpoch`, `bpm`, `sequence` (for `vc.lfoFactor`) are the same, the LFO's group bit of V1 is clear, and for `sync == 4` while `lastSemis` is the same (key follow). Rebuild with the existing expressions otherwise. `lfoValue` then does the core advance, the shape and the smoothing step with the cached `coef`.

### V4. The matrix evaluates slots whose source is exactly 0, both lanes

- **Status (d05d6d83): done, bit identical.** `CompiledSlot::zeroSkip` with the conditions extended where the sketch was not exact: a finite power and width, and for an aux a finite aux power of at most 80 in size and a finite aux output (an aux power beyond about 88 makes `inf / inf`); also the amounts not NaN. Proved over 37.7 M evaluations of every special value combination.

- **Where.** `mod_matrix.cpp:412-436` (the loop), `:337-399` (`one`), `:425-426` (only a zero amount is skipped).
- **Loop level.** Per voice period x compiled voice slot (the global evaluate runs once per period at `own_engine.cpp:1229`).
- **What it does.** For each slot with a non zero amount, `one ()` clamps both lanes, runs the law (copysign, `powerShape` with its `fabs < 1e-4` test), the delay gate, the aux, five multiplies per lane, then finds 0 and continues at `:429-430`. In the bench the macros, the mod wheel and the aftertouch are 0, so these slots always produce 0.
- **Measured.** `ModMatrix::evaluate` with `one`: 2.50 / 3.26 / 2.48 % of the samples, about 2.1 % of the block for Lorenz Garden over 24 x 22 = 528 slot evaluations: **about 180 cycles per slot** with the loop.
- **Consequence.** Bit identical under the conditions below: with no curve, no slew, not bipolar, an aux that is 0 or not NaN, and a source value `<= 0` in both lanes, the law gives +0 or -0, the product is +0 or -0, and the slot is skipped today anyway.
- **Saving (skipped slots per block x 120 to 180 cycles).** Lorenz Garden 24 x 11 = 264: **0.68 to 1.02**. Stretched Glass Choir 32 x 9 = 288: **0.74 to 1.11**. Parallel Dusk Pad 24 x 10 = 240: **0.62 to 0.93**. Ring Mod Gamelan 32 x 6 = 192: **0.49 to 0.74**. Shepard Ascent 16 x 9 = 144: **0.37 to 0.56**. Triple Fifth Lead 32 x 10 = 320: **0.82 to 1.23**. Mid Side Halo 192: **0.49 to 0.74**.
- **Caveat.** This is the saving at rest. A player who turns a macro or the mod wheel gets the work back, so the worst case of a block does not go down. Rank it as an average saving.
- **Confidence.** High for the bench state. Risk very low.
- **Change sketch.** In `compile` set `c.zeroSkip = c.curve == nullptr && !c.bipolar && !(c.riseMs > 0.0f || c.fallMs > 0.0f)`. In the loop after `:426`: `if (c.zeroSkip && ctx.src[c.source] <= 0.0f && srcR[c.source] <= 0.0f && (c.aux == 0 || (ctx.src[c.aux] == ctx.src[c.aux] && srcR[c.aux] == srcR[c.aux]))) continue;` (`<= 0` is false for NaN, so a NaN source still goes through `one`). The same test in the amount loop `:400-411`.

### V5. Every parameter read pays a VFP compare and a `vmrs`; 46 sites call it out of line

- **Status (25e7e958): done, bit identical.** `(bits << 1) == 0` (all 2^32 patterns agree with `d == 0.0f`); snapshot and delta pointers in locals inside `tick`. Exact also with flush-to-zero, because under flush-to-zero no delta can be a denormal (every delta is an arithmetic result, flushed).

- **Where.** `voice.h:185-194` (`P`, `PR`); bench binary: 46 `bl Voice::P` in `Voice::tick`, the outlined body at 0x98600 (`vcmp.f32 s1, #0`, `vmrs`, branch), the inlined copies alike.
- **Loop level.** Per voice period x parameter read. After V1 and V3 the oscillators (about 45 reads each when on), the filters (about 25 each, `filter_setup.h:23-45` and `voice.cpp:1075-1103`) and the sub (about 12) remain: 130 to 197 reads per vp.
- **What it does.** `d == 0.0f` on a float: a VFP compare and a transfer of the flags (`vmrs APSR_nzcv, fpscr`), which waits for the VFP pipeline. Each read also reloads `this->modP` and `gl->base` because stores to float members in between may alias.
- **Consequence.** Bit identical: `(bits << 1) == 0` holds exactly for +0 and -0, as `d == 0.0f`; a NaN goes to the slow path in both.
- **Saving (reads per block x 2 to 6 cycles).** Lorenz Garden 24 x 197 = 4728: **0.20 to 0.61**. Stretched Glass Choir 32 x 130: **0.18 to 0.53**. Parallel Dusk Pad 24 x 152: **0.16 to 0.47**. Ring Mod Gamelan 32 x 152: **0.21 to 0.63**. Shepard Ascent 16 x 152: **0.10 to 0.31**. Triple Fifth Lead and Mid Side Halo 32 x 177: **0.24 to 0.73**. Overlaps with V7 (an oscillator served from its cache does not read).
- **Confidence.** Low to medium (the `vmrs` cost on the A17 is the unknown). Risk very low.
- **Change sketch.** `uint32_t u; std::memcpy (&u, modP + idx, 4); return (u << 1) == 0 ? gl->base[idx] : modulatedSlow (...)`, same for `PR`. Do not force inlining (the tick is already 20 KB of code, see section 8). In `tick` copy `gl->base` and `modP` to local `const float*` at the top and use a local read helper, so the compiler keeps them in registers.

### V6. Zero fills and copies of the routing buffers

- **Status (738dc125): done, bit identical.** First writes `0 + a * g` (the +0 add is kept: writing the bare product would leave a -0 where the old code had +0; that variant is caught by the comparison test); extNext written straight into extMono, only an off Noise / Sub zeroed.

- **Where.** `voice.cpp:1133` (`extNext[4][32] = {}`, 512 bytes), `:1137-1145` (`touchFin`: 2 x 128 bytes per filter input), `:1152-1163` (`toTarget`: 2 x 128 bytes per first use of a target), `:1390-1394` (extNext copied to extMono and the tail filled).
- **Loop level.** Per voice period.
- **What it does.** About 7 `memset` calls (library calls; 8 in `Voice::render`, 16 in the inlined `route` lambda of the bench binary) and 4 `memmove` per vp, then the first `addScaled` loads the zeros it just wrote.
- **Consequence.** Bit identical if the first write into a buffer computes `0.0f + a[k] * g` (the same IEEE operation as adding into a zero, so a -0 product still gives +0), and if the extNext double buffer zeroes only the slots that got no write.
- **Saving (vp x 300 to 550 cycles).** Lorenz Garden and Parallel Dusk Pad 24 vp: **0.15 to 0.28**. 8 voice presets 32 vp: **0.21 to 0.38**. Shepard Ascent 16 vp: **0.10 to 0.19**. (memset is 2.0 to 3.1 % and memcpy / memmove 1.1 to 2.5 % of all samples, setup included.)
- **Confidence.** Medium. Risk low.
- **Change sketch.** `simd::scaleTo (d, a, g, n)` = `vst1q (d, vaddq_f32 (vdupq_n_f32 (0.0f), vmulq_f32 (a, g)))`; `touchFin` and `toTarget` mark "empty" instead of filling, and the first `addScaled` into an empty buffer calls `scaleTo`. A reader that finds a buffer still empty (a target with nothing added) reads it as zero (fill it at that point). `extNext`: two arrays and an index flip; zero only `extNext[j]` that got no write in this period (Noise and Sub when off).

### V7. Oscillator (and sub) parameters are read and derived every period although nothing moved them

- **Status (73459fe8): done, bit identical.** `Voice::OscPart` / `SubPart` keyed on the epoch and the touched group mask of this period (at build and now); everything that reads the note, the voice control, content, tables or state stays per period. The library keys are filled only when a library type needs them.

- **Where.** `voice.cpp:785-992` (about 45 `P ()` reads, clamps, the pitch mode `log2f` at `:861, :863`, `builtinWavetableIfReady`, `lib[o].setType`), `:994-1022` (sub).
- **Loop level.** Per voice period x oscillator on.
- **Measured.** "voice tick: oscillator parameters" 1.75 to 3.43 % (round 12), 1.6 to 2.0 k cycles per oscillator period (Mid Side Halo 0.0931 ms / 96 calls).
- **Which oscillators are untouched in the bench** (all their slots read a 0 source): Parallel Dusk Pad osc 1; Ring Mod Gamelan osc 1, 2; Triple Fifth Lead osc 1, 3; Mid Side Halo osc 1, 2 and the sub. None in Lorenz Garden, Stretched Glass Choir, Shepard Ascent (random LFOs and LFOs move them).
- **Consequence.** Bit identical if the key is complete.
- **Saving (untouched oscillator periods x 600 to 1100 cycles).** Parallel Dusk Pad 24: **0.31 to 0.57**. Ring Mod Gamelan 64: **0.82 to 1.51**. Triple Fifth Lead 64: **0.82 to 1.51**. Mid Side Halo 64: **0.82 to 1.51**. Others **0**. Overlaps with V5.
- **Confidence.** Medium. Risk medium: many inputs (the key must cover `baseEpoch`, `sequence` for `vc`, `g.content`, `g.maxUnison`, `g.oversample`, the economy flags, `fresh`, the key and velocity range of the note).
- **Change sketch.** Split the loop body into a "settings" part that depends on parameters only (destination, blend, sends, env1, wavetable choice, track flags, fixed note, pitch mode and `coarseSemis`, frame, spread, level, pan, unison fields, warps, morph fields, smooth, `oscOn` and `oscWarpOnly`) and a "per period" part (`t.semis` from `noteSemis`, `snapTranspose` which reads the note, `pitchTap`, `t.wt` from `g.wtResolved`, the remap slew when remap is on, the oscillator update). Cache the settings per oscillator with the V1 group bit as part of the key. Keep a debug switch that computes both and compares on all 160 presets x 3 tiers.

### V8. Unused LFOs are shaped and smoothed in every voice only for the display

- **Status: skipped, not bit identical.** The value of an unused LFO is read by the display for the newest voice (`writeReadout` sources) and its smoothing state carries over: when the newest voice ends, or an edit makes the LFO used, the old code has a different value / state than one that skipped the shape (display at once, audio after the edit).

- **Where.** `voice.cpp:605-613` (an LFO no slot reads still runs `lfoValue` every 4th period), `:649-650` (into `src`), `own_engine.cpp:1783-1797` (the display reads `src` of the newest voice only).
- **What it does.** Per evaluation: `lfoCore.advance`, the shape (`cosf` for the default sine, `lfo.h:21`), the smoothing (V3). Nothing in the audio path reads the result: `usedSources` covers slots and the source points of drawn shapes (`mod_matrix.cpp:302-317`), the global slots read only globally used sources. The display needs the phase (`lfoReadPos`) of up to 8 voices (`own_engine.cpp:1822-1823`) and the value of the newest voice.
- **Consequence.** Audio bit identical for a fixed patch. Display unchanged for the newest voice. Two edges: when the newest voice ends, the next one shows a stale value for up to one period; when an edit makes the LFO used, its smoothed state starts from a stale value (a transient shorter than the smoothing time, only at that edit).
- **Saving (after V3; unused evaluations in non newest voices x 200 to 300 cycles).** Lorenz Garden 24 x 2.5 x 5/6 = 50: **0.21 to 0.32**. Stretched Glass Choir 63: **0.27 to 0.41**. Parallel Dusk Pad 45: **0.19 to 0.29**. Ring Mod Gamelan 70: **0.30 to 0.45**. Shepard Ascent 24: **0.10 to 0.15**. Triple Fifth Lead 70: **0.30 to 0.45**. Mid Side Halo 63: **0.27 to 0.41**.
- **Confidence.** Medium. Risk low to medium (a display contract).
- **Change sketch.** `TickGlobals::readoutSerial` = the largest serial among active voices, set before the tick loop (the engine already finds `newest` after it, `own_engine.cpp:1162-1164`). For an unused LFO in a voice whose serial is not that one: advance the core, age and `lfoReadPos`, skip shape and smoothing, keep `lfoOut`.

### V9. The right lane of a slot is computed even when it equals the left

- **Status (fd0357f3): done, bit identical.** The right lane is the left result when the clamped (and slewed) source, the amount and the aux value have the same bits; a curve is evaluated once when the clamped sources agree.

- **Where.** `mod_matrix.cpp:339-399`.
- **What it does.** `xr`, `auxR`, `law (xr)` and the product are computed again. Only the LFOs and random LFOs with stereo, and `kSrcStereo`, differ between `src` and `srcR` (`voice.cpp:690-695`); no preset uses LFO stereo.
- **Consequence.** Bit identical (the same expression on the same inputs; then the existing `stereo` sign flip).
- **Saving (non zero voice slots per block x 50 to 70 cycles).** Lorenz Garden 264: **0.28 to 0.40**. Stretched Glass Choir 160: **0.17 to 0.24**. Parallel Dusk Pad 144: **0.15 to 0.22**. Ring Mod Gamelan 128: **0.14 to 0.19**. Shepard Ascent 112: **0.12 to 0.17**. Triple Fifth Lead 96: **0.10 to 0.14**. Mid Side Halo 128: **0.14 to 0.19**. More when macros move (V4 then saves nothing and V9 applies to those slots).
- **Confidence.** Medium (part may already be shared by the compiler). Risk very low.
- **Change sketch.** In `one`: when `xl` and `xr` have the same bits, `amountL == amountR` and (no aux or the aux values have the same bits), compute the left lane and copy it to the right before the `stereo` flip.

### V10. The engine scans the 36 voice pool many times per period

- **Status (9cec1ec1): done, bit identical.** A 64 bit set of the active voices kept by the voices themselves, visited in ascending index (snapshot); the per event loops keep their scans. `polyphonyLimit` was not cached (one call per period).

- **Where.** `own_engine.cpp:1139-1141` (pending pedal), `:1142` with `:692-710` (`killExcess`, which also calls `polyphonyLimit` and so `paramIndex`), `:1149` with `:683-690`, `:1150-1152`, `:1162-1164`, `:1530-1532`; per block `:1615`, `:1617-1618`, `publishStats` `:1630-1647`, `writeReadout` `:1775-1777` and the selection `:1805-1815` (up to 8 x 36).
- **What it does.** Each test dereferences a `unique_ptr` into a separate large `Voice` and reads one flag: 6 scans of 36 per period, about 500 tests per block.
- **Measured.** `controlTick` 0.63 to 0.74 %, `killExcess` 0.12 to 0.18 %, `writeReadout` 0.23 to 0.44 %, `updateLowestNote` 0.03 to 0.05 %, `publishStats` 0.03 to 0.09 % of the samples (these include other work).
- **Consequence.** Bit identical if the order of the active voices is kept.
- **Saving.** Estimate **0.2 to 0.4** on every preset (about 10 to 20 k cycles per block).
- **Confidence.** Low to medium. Risk low.
- **Change sketch.** An `activeIdx[]` list kept in ascending index order (insert at `start`, remove where `active` becomes false: `tick` `:769`, `kill`, `reset`); one pass per period computing pending pedal, the count for `killExcess`, the lowest note, the newest voice; reuse that `newest` in `writeReadout`. Cache `polyphonyLimit` per block.

### V11. Function local statics with a guard and a barrier on the hot path

- **Status (ddb1a17e): done, bit identical.** Kept as `Voice` members set in `prepare` (not `TickGlobals`, so the direct voice rigs of the tests need nothing new); `noise.filter` index too.

- **Where.** `voice/param_index.cpp:121-125` (`paramIndex`), `matrix/mod_matrix.cpp:112-122` (`destLawOf`), `libKeys` (`osc/lib_osc.h:63`); `paramTable ()` called from `Voice::modulatedSlow` (`voice.cpp:139-142`).
- **What it does.** Bench binary: `paramIndex` saves 8 core and 8 NEON D registers, loads the guard and runs `dmb ish` on every call (the inlined builder makes the prologue that large); `destLawOf` the same with 9 + 2 registers and a size computation. Called once per tick, once per `lfoValue`, per oscillator (`libKeys`), per modulated read (`destLawOf`, `paramTable`).
- **Measured.** `paramIndex` 0.06 to 0.13 %, `libKeys` 0.04 to 0.08 %, `destLawOf` 0.07 to 0.18 % of the samples.
- **Consequence.** Bit identical.
- **Saving.** **0.1 to 0.25** per preset.
- **Confidence.** Medium. Risk very low.
- **Change sketch.** Keep `const ParamIndex*`, the `LibKeys` pointers, the `DestLaw` array pointer and the `ParamInfo` table pointer in `TickGlobals` (filled in `OwnEngine::prepare`, which already warms `destLawOf (0)` at `own_engine.cpp:406`) and pass them down; `modulatedSlow` uses them.

### V12. `ffloor` is out of line, has three `vmrs`, and sends 0 to the library

- **Status (d1c25764): done, bit identical.** `ffloorBits` (bit form) on 32 bit ARM, `std::floor` where it is one instruction (AArch64, x86 SSE 4.1). Correction to the sketch: a denormal must still go to `std::floor` (with flush-to-zero the old compare took it for 0, and AArch64 `frintm` then gives -0 for a negative denormal, not -1). Proved for all 2^32 patterns with and without flush-to-zero. `own_engine.cpp` LFO phases and the LFO readout use `ffloor`.

- **Where.** `dsp/util.h:44-50`; callers here: `voice.cpp:331, 346` (`knob - ffloor (knob)` with the phase knob 0 in all seven presets), `lfo_eval.h` `lfoShapeAt`, `lfo.h:119, 130`; `own_engine.cpp:1115` uses `std::floor` (a libm `floorf` call) for 10 global LFO phases per period.
- **What it does.** `x == 0.0f` is sent to `std::floor`, a library call, to keep the sign of zero; the remaining path has three compares with `vmrs`.
- **Measured.** `ffloor` 0.15 to 0.37 %, `floorf` 0.07 to 0.13 % of the samples (the oscillator warps also call `ffloor`).
- **Consequence.** Bit identical: `floor (+-0) = +-0`, so `if (x == 0.0f) return x;` is exact; the range test can be an integer test on the bits.
- **Saving.** **0.1 to 0.25**.
- **Confidence.** Medium. Risk very low.
- **Change sketch.** `if (x == 0.0f) return x;` before the library branch; `(bits & 0x7fffffff) < 0x4a800000` for `|x| < 2^22`; `std::floor` at `own_engine.cpp:1115` becomes `ffloor`.

### V13. Envelope curves: a constant `expm1f` per evaluation, and a decay to sustain 1

- **Status (20abedd2): done, bit identical.** `EnvSettings::fillDens` for kept settings; the sustain 1 shortcut needs `|decayCurve| <= 80` (beyond, `expm1` overflows to `inf / inf` = NaN, which the old code propagated).

- **Where.** `env_lfo/envelope.h:136, 160, 173` with `dsp/util.h:66-71` (`powerShape` = `expm1 (p x) / expm1 (p)`).
- **What it does.** Two `expm1f` and a divide per curved stage evaluation; `expm1 (p)` depends only on the curve. In a decay to sustain 1 (all default envelopes) `lerp (1, 1, t)` is 1 whatever `powerShape` returns.
- **Consequence.** Bit identical (same denominator value; `1 + 0 * t = 1` for finite `t`, and `t` is finite for curves in the parameter range).
- **Saving (expm1 calls saved per block x 80 to 200).** Lorenz Garden 24: **0.04 to 0.10**. Stretched Glass Choir 32: **0.05 to 0.14**. Parallel Dusk Pad 22: **0.04 to 0.09**. Ring Mod Gamelan 76: **0.13 to 0.32**. Shepard Ascent 16: **0.03 to 0.07**. Triple Fifth Lead 67: **0.12 to 0.29**. Mid Side Halo 66: **0.11 to 0.28**.
- **Confidence.** Medium. Risk very low.
- **Change sketch.** `EnvSettings` gets `attackDen`, `decayDen`, `releaseDen` (= `expm1f` of the curve, or 0 for a linear curve), filled where the settings are built (cached with V1); `Envelope::advance` divides by them. In the decay: `if (s.sustain == 1.0f) value = 1.0f; else value = lerp (...)`.

### V14. A dirty block (any knob or Q-Link move) rebuilds everything; the bench never measures it

- **Status: not part of this round** (not in the task).

- **Where.** `own_engine.cpp:243-276` (`setParam` always bumps `paramGen`, even for an unchanged value), `:1290-1305` (copy of 2637 atomics, `ModMatrix::compile`, `++baseEpoch`), `:1332-1334` (tuning), `mod_matrix.cpp:240-306` (per compiled slot `isVoiceParam` with up to 9 `strncmp` `:27-34`, `ownerNode` string parsing `:173-198`).
- **What it does.** One block after any `setParam`: about 5 to 8 k cycles of copy, 7 to 15 k for the compile, then every voice cache keyed on `baseEpoch` is invalid for that block (voice control, V1, V3, V7), and every FX rack runs its full update (FILTER-FX F11, 1.4 to 1.8 % measured).
- **Consequence.** Bit identical for (1) to (3) below.
- **Saving.** Direct cost **0.3 to 0.5** per dirty block, plus the cache losses. During a Q-Link sweep most blocks are dirty, so this is a worst case cost that the bench (`tier_bench.cpp:382-405` sends no parameter) does not see.
- **Confidence.** Medium for the mechanism; the frequency of `setParam` calls from the MPC shell is not known to me. Risk low.
- **Change sketch.** (1) In `setParam` compare the constrained value with the stored one (bits) and skip the store and the bump when equal. (2) Precompute `isVoiceParam` and `ownerNode` per parameter index once (they depend on the key only) so `compile` does no string work. (3) A dirty bitmap of 2637 bits set by `setParam`; the snapshot copies only the changed words and calls `compile` only when a `modNN.*` or a slot related key changed. (4) Optional: an epoch per group so a cutoff sweep does not invalidate the envelope and LFO caches. Bench: add `--sweep KEY` to `synthyboy_tier_bench` (one `setParam` per block) and measure Mid Side Halo and Lorenz Garden with `--sweep filter1.cutoff` before and after.

### V15. Rounding level and user patch items (not for the factory presets)

- **Status: not part of this round** (not in the task; the slew and power denominators would follow the V13 pattern).

- LFO shape 0 uses libm `cosf` per lane (`lfo.h:21`). At tier low a table sine (`fastmath::sinCycles`, used by the oscillators) would be rounding level, about 1e-7. Used sine LFOs: Stretched Glass Choir, Mid Side Halo lfo 1: 32 calls per block x 80 to 200 = **0.05 to 0.14**. Low value; only with V8 not done does it reach the unused LFOs too.
- Slot slew: `std::exp` per lane per slot per voice period (`mod_matrix.cpp:360`), constant for a given `dt` and rise / fall time. No factory preset uses slew; for a user patch with k slewed slots at 8 voices: 32 x 2 k x (80 to 200) cycles = 0.11 to 0.27 % per slot. Bit identical fix: compute both coefficients in `compile` for the control period `dt` (pass `dt` in), use them when `ctx.dt` equals it.
- Slot `power` and `aux_power`: 4 `expm1f` per lane pair per slot (`:372, :374, :387`), with `expm1 (power)` constant: the same denominator cache as V13. No factory preset uses them.

## 7. FILTER-FX F10 (strip peaks): measured, worth more than estimated

Not a new finding; new evidence. The scalar `peakOf` loops (`voice.cpp:1113-1119`, called at `:1240, :1254, :1318, :1348`) are the hottest windows of `Voice::render` and of the filter lambda in the profile: `vabs`, `vcmp`, `vmrs`, `vmovgt` per sample (bench binary 0x925e8, 0x921a2, 0x88cc6). Their samples: **1.18 % (Lorenz Garden), 1.70 % (Stretched Glass Choir), 1.97 % (Mid Side Halo)** of all samples, that is **1.0, 0.97, 1.32 % of the block**. F10 estimated 0.3 to 1.1. A NEON `peakAbs` (F10's sketch) should save about 0.8 to 1.1 points on these presets; rank it with V1 to V3. The engine's bus and master peak loops (`own_engine.cpp:1536-1542, 1603`) and the chain's peak in / out at tier low (`fx_chain.cpp:765-767, 780-782`) are the same kind and belong to the same change.

## 8. Checked and not waste (or not measurable as such)

- **Master stage** (`own_engine.cpp:1589-1604`): per sample the volume ramp (`LinearSmoother::next`, `util.h:122-131`), the stereo routing only when `routeSin != 0`, the Direct ramp, `outputLimit` (`:80-86`: `tanh` only above 1.8, never at normal level) and the master peak. About 40 to 60 cycles per sample x 128 = 5 to 8 k cycles, **0.1 to 0.17 %**. No oversampling at tier low (`globals.oversample` only with ultra render or `quality.oversample_4x`, `:1388`). No limiter beyond `outputLimit`.
- **Atomics**: about 15 relaxed loads per block and one hazard pointer loop with sequentially consistent loads and a store (`:1352-1360`); no atomics per voice. Negligible.
- **`std::function` and virtual calls** in the voice: none. The lambdas of `render` are direct calls (some out of line). The struct copies (`envCache[e]`, 9 floats; `src` to `srcR`, 58 floats, `voice.cpp:690`; `heldSrc`, `own_engine.cpp:1200`) cost a few hundred cycles per block.
- **Note on, stealing, release**: per event, not per block. `startNote` (`own_engine.cpp:732-818`) scans the pool 4 times and runs one extra `tick`. Category "voice management (note start)" is 0.0 % for the seven presets (`sections-by-category.csv`). The morph builds at note start belong to OSC F1.
- **Matrix compile**: only on dirty blocks (V14).
- **Bus mixing** (`own_engine.cpp:1547-1583`): for an unused bus about 32 x 2 multiplies, a delay line and 64 adds per period; `makeupOfBus` returns at once without a bypassed bus effect (`:321-326`). About 0.05 to 0.1 %.
- **Code size.** `Voice::tick` is 20 KB, `Voice::render` 6.9 KB, its lambdas about 7.4 KB, `lfoValue` with its lane 5.5 KB: the voice control code alone is more than the 32 KB instruction cache, so it is fetched again from L2 every period. A cold split (the `fresh` block `voice.cpp:517-564`, the noise sample path `:1048-1067`, the library parameters `:967-991`, Ratio SRC `:864-887`, remap `:918-936` into `noinline` functions) is bit identical and shrinks the hot body. The gain is a device question (measure with `--sections` before and after).

## 9. Already optimised (do not report again)

- `own_engine.cpp:1292-1305`: the parameter snapshot and the matrix compile only when a parameter or the content changed.
- `own_engine.cpp:1404-1429, 1461-1478, 1481-1499`: master volume, stereo routing `cos` / `sin`, mixer levels and the latency plan only when their inputs change.
- `own_engine.cpp:1501-1507`: tier low events on the 32 sample grid, so chunks are never cut.
- `own_engine.cpp:1169-1173`: global LFO shapes only for LFOs a global slot reads; `:1236`: the idle readout evaluation only without voices and with display targets.
- `voice.cpp:459-463, 720-724`: one pole coefficients cached on `dt` (tuning, pad pressure).
- `voice.cpp:470-476`: voice control cached on epoch and sequence.
- `voice.cpp:568-600`: the envelope settings cache (it exists; V1 makes it hold).
- `voice.cpp:586-591, 605-615, 616-625`: tier low decimation of unused envelopes, LFOs and random LFOs (every 4th period).
- `voice.cpp:387-390`: the right LFO lane only with stereo.
- `voice.cpp:827-840, 1024-1027, 1082-1083`: an off oscillator gets the minimal update; a settled off noise is skipped; an off filter is not configured.
- `voice.cpp:1130-1163, 1401-1408`: only the targets and filter inputs that receive something are cleared and mixed; `:1411-1425` the NEON mix lane; `:1443-1444` NEON adds to the buses.
- `voice.h:185-189`: the unmodulated parameter read does not evaluate a law.
- `mod_matrix.cpp:425-426, 429-430`: zero amounts skipped, zero results not touched; `:327-329` the amount arrays cleared only when a slot targets an amount; `:124-128` fast `log2` / `exp2` for the laws at tier low.
- `dsp/util.h:68-69`: `powerShape` returns early for a linear curve (all attack curves of the seven presets).
- `env_lfo/lfo.h:46-47`: a free rate LFO returns its rate directly.

## 10. The earlier review's claims, checked

Source: `.claude/worktrees/agy-g78-review-voice-mod-deep/docs/release/CPU-REVIEW-VOICE-MOD.md`.

| claim | verdict | calling loop and why |
|---|---|---|
| "`Voice::deferSilent` (voice.cpp:957) appears to skip rendering if the oscillator is silent" | partly | `deferSilent` defers the oscillator *update* of a period with level 0 (`voice.cpp:957`); the render of a silent oscillator returns after zero fills (`wt_osc.h:607-610`); `Voice::render` still runs `halfSum`, `peakOf` and `route` for it (per vp, `voice.cpp:1234-1243`). |
| "need to verify if `Voice::render` still executes the full chain for released voices" | answered: yes, as it must | a released voice ticks and renders until envelope 1 is done (`voice.cpp:764-774`); at tier low a stolen voice stops at the next control tick (`own_engine.cpp:704-705`). |
| "the routing loop processes oscillators in blocks of `kControlPeriod`" | confirmed | per voice period, `n <= 32` (`own_engine.cpp:1501, 1530-1532`). |
| "uses `simd::addScaled` for efficient routing" | confirmed | `voice.cpp:1147-1151, 1443-1444`. |
| "looks well optimized ... no obvious waste found" (render) | refuted | the profile's hottest windows of `Voice::render` are scalar compare loops (`peakOf`, 1.0 to 1.3 % of the block, section 7) and the ramp with a store forwarding stall (0.46 to 0.65 %, V2); plus about 7 memsets per vp (V6). The section "routing / mix / output" is 3.8 to 6.3 % of the block. |
| "matrix evaluation per control period, which is efficient" | partly | per voice period is right; but every non zero depth slot runs both lanes in full even with a source of exactly 0 (V4, V9); matrix 2.1 to 3.3 % of the samples. |
| "slew logic uses `ctx.dt`, confirming per control period processing" | confirmed, with a cost | `mod_matrix.cpp:355-361`: two `std::exp` per slewed slot per voice period for a constant coefficient (V15); no factory preset uses slew. |
| "`anyAmountDest` optimization avoids unnecessary delta calculations" | partly | it only avoids clearing the two 64 entry amount arrays (`:327-329`); the slot loop still runs over all compiled slots. |
| "master volume smoothing is per control period" | refuted | the target is set per period when the inputs change (`own_engine.cpp:1409-1418`), the smoother steps per sample (`:1591`). |
| "limiter and routing calculations are per block" | refuted | `outputLimit` runs per sample per channel (`:1601-1602`); the routing cross mix per sample when `routeSin != 0` (`:1592-1597`); its `cos` / `sin` per period when inputs change (`:1426-1427`). |
| "limiter implementation looks efficient with `std::tanh` and `std::copysign`" | confirmed in effect | `tanh` runs only above 1.8 (`:83-85`); the normal path is `fabs`, a compare and a NaN test. |
| "control rate logic is well factored to avoid per sample work" | partly | true for per sample work; but the per voice control work is 12 to 21 points of the block (section 4), with settings rebuilt every period (V1, V3, V7). |
| "VO-01: none, systems already optimized" | refuted | sections 6 and 7. |

## 11. Ranking: (saving x certainty) / risk, sound neutral first

Ranges are per heavy preset; they overlap where noted (V5 with V7; V8 assumes V3).

| Rank | ID | Saving (% of the block) | Sound | Risk | Change in one line |
|---|---|---|---|---|---|
| 1 | V1 | 0.31 to 0.88 (8 voice presets 0.62 to 0.88) | bit identical | low | per envelope cache keyed on epoch, bpm, sequence and a "touched group" mask instead of `numTouched == 0`; decimation test before the settings |
| 2 | V2 | 0.14 to 0.44 (measured cost 0.46 to 0.65) | bit identical | very low | `rampLerp`: iota vector in a register, no stack round trip |
| 3 | F10 (FILTER-FX) | about 0.8 to 1.1 (measured cost 1.0 to 1.3) | audio bit identical | very low | NEON `peakAbs` for the strip, bus, master and chain peaks |
| 4 | V3 | 0.39 to 1.11 | bit identical | low | cache the LFO settings and `1 - exp2 (-dt / half)` per voice and LFO |
| 5 | V4 | 0.37 to 1.23 (at rest only) | bit identical | very low | skip a slot whose source is `<= 0` in both lanes (no curve, no slew, unipolar, aux not NaN) |
| 6 | V5 | 0.10 to 0.73 | bit identical | very low | `P` / `PR`: integer test of the zero bits, base and mod pointers in locals |
| 7 | V7 | 0 to 1.51 (Ring Mod Gamelan, Triple Fifth Lead, Mid Side Halo 0.82 to 1.51) | bit identical | medium | cache the parameter part of an untouched oscillator and sub per epoch and sequence |
| 8 | V6 | 0.10 to 0.38 | bit identical | low | first write as `0 + a * g`, no memsets; extNext as a double buffer |
| 9 | V8 | 0.10 to 0.45 | audio bit identical; display of non newest voices | low to medium | unused LFOs: only phase and age outside the newest voice |
| 10 | V9 | 0.10 to 0.40 | bit identical | very low | right lane copied from the left when inputs are equal |
| 11 | V11 | 0.10 to 0.25 | bit identical | very low | pointers to ParamIndex, LibKeys, DestLaw and the param table in `TickGlobals` |
| 12 | V12 | 0.10 to 0.25 | bit identical | very low | `ffloor`: return ±0 directly, integer range test; `std::floor` at `:1115` to `ffloor` |
| 13 | V10 | 0.2 to 0.4 (low confidence) | bit identical | low | active voice index list, one scan per period, `newest` reused |
| 14 | V13 | 0.03 to 0.32 | bit identical | very low | cached `expm1f (curve)`; decay to sustain 1 without `powerShape` |
| 15 | V14 | not in the bench; 0.3 to 0.5 direct per dirty block plus cache losses | bit identical | low | no bump for an unchanged value; string free compile; dirty bitmap; `--sweep` bench switch |
| 16 | V15 | 0.05 to 0.14 (sine LFO); user patches with slew or power | rounding level (sine) / bit identical | low | table sine for LFO shape 0 at low; slew and power denominators in `compile` |

**Rough effect of V1 to V13 together** (overlaps removed roughly; F10 not included): Mid Side Halo, Triple Fifth Lead, Ring Mod Gamelan 4 to 7 points; Stretched Glass Choir 3.5 to 5.5; Lorenz Garden and Parallel Dusk Pad 3 to 5; Shepard Ascent 2 to 3.5. With F10 about 1 point more each.

## 12. What I could not verify, and what only the device decides

- Cycle costs used as estimates: the `vmrs` stall on the A17 (V5 depends on it most), the libm costs of this glibc (`exp2f`, `cosf`, `expm1f`), the cost of a function local static guard with `dmb ish` (V11).
- How the round 10 PC profile was taken (sampling method, whether kernel time and the setup phase are included). I used its shares only as relative weights and converted them with the variant medians.
- The 8 non additive points of the M/S splitter case (section 5): runs 1 to 4 there.
- How often the MPC shell calls `setParam` while nothing moves, and the rate of Q-Link updates (V14). The bench cannot show it until a `--sweep` switch exists.
- The provenance of "12.6 to 18.4 points" of the task; section 4 gives my own numbers.
- Device runs for the changes (`synthyboy_tier_bench --presets content/presets --core 3 --config low`):
  - `--load "Mid Side Halo" --load "Triple Fifth Lead" --load "Ring Mod Gamelan" --sections` before and after V1, V3, V4, V7 (sections "envelopes + LFOs", "modulation matrix", "oscillator parameters");
  - `--load "Stretched Glass Choir" --load "Lorenz Garden" --sections` for V2, V6 and F10 ("routing / mix / output", "amplitude / velocity / output");
  - `--verify` after every bit identical change, all 160 presets x 3 tiers as in round 12;
  - V8: `--verify` for audio; the display check by hand (LFO page with two notes held, an unused LFO, the newest voice released);
  - V14: needs `--sweep KEY` first; then Mid Side Halo `--sweep filter1.cutoff` and `--sweep macro.1`, median and p99.
