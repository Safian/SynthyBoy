# CPU review: the oscillators of the own engine (Key 61, tier `low`)

Scope: `core/engine-own/osc/` (wt_osc.h, warp.h, morph.*, wavetable_bank.*, lib_osc dispatch), the sub and noise sources and the calls from `core/engine-own/voice/voice.cpp` and `core/engine-own/own_engine.cpp`. Read only review of branch `feature/mpc-overlay` at 152b4a45. The content oscillators (Sample, Granular, Spectral, Multisample) are reviewed elsewhere; only their dispatch is described here.

Sources of evidence, in order of weight:

1. Device measurements of 2026-11-18 on the Key 61 (core 3, 1.608 GHz, `low` profile): `.hw-local/mpc-perf/round10/k61-breakdown/` (`sections.csv`, `variants.csv`, `raw/micro.txt`) and `round10/k61-r10-tables.md`. The bench build of that round (`round10/bench-build/arm32`, built 14:49) has the same oscillator code as 152b4a45: no commit after 14:00 touches wt_osc.h, warp.h, morph.cpp, wavetable_bank.cpp, voice.cpp or own_engine.cpp.
2. The disassembly of that arm32 bench binary (`synthyboy_tier_bench`, not stripped, read with `llvm-objdump`). It proves which calls really happen per sample.
3. Reading the code. Every file:line below was re-opened and checked on 152b4a45.
4. Cycle estimates, labelled as estimates. Unit costs used: a NEON Q operation about 2 cycles, a scalar VFP operation 1 to 2, a float divide about 14, a float libm call (`sinf`, `expm1f`, `exp2f`) 80 to 200, a double libm call 150 to 400. Where `raw/micro.txt` has a measured unit cost it is used instead: a loop iteration of one scalar multiply and add 2.85 ns (2.92 us per 1025), `memcpy` 4 KB 0.88 us, `memset` 4 KB 1.17 us, the NEON real inverse transform of 2048 points 33.8 us (scalar 51.0 us).

One block = 128 frames at 44.1 kHz = 2.902 ms = about 4.67 million cycles at 1.608 GHz. 1 % of the block = 29 us = about 46.7 k cycles.

## 0. Status of the work on branch `work/opt-osc` (bit identical items)

Device result (round 12, all three branches merged in d7cbc890, tree proved bit identical to 152b4a45 on all 160 presets x 3 tiers; `.hw-local/mpc-perf/round12/`, summary in the round 12 entry of `docs/MPC.md`): the Key 61 `--sections` runs show the wavetable read 0.02 to 0.05 ms cheaper and the morph cycle build 0.049 ms cheaper (Stretched Glass Choir 0.180 -> 0.131 ms), oscillator update -0.013 to -0.021 ms; the sums of the blocks are in `k61-r12-sections-tables.md`.


Every item below keeps the output bit for bit: `WtOsc::setReference (true)` / `buildMorphCycle (..., referenceBuild = true)` run the old code, and `core/tests/test_own_wt_osc_opt.cpp` (random patches, two oscillators in step, output and state compared; `SB_TEST_SEEDS=1500` for 210000 periods) and `core/tests/test_own_morph_transform.cpp` (random morph builds with a poisoned scratch) prove it. End to end: all 160 factory presets (note and chord files) rendered with `synthyboy_preset_render --tier low|medium|high --wav` on the base commit 152b4a45 and on this branch are byte for byte the same files. Mac timings (Apple arm64, out of order, so small): `test_own_wt_osc_opt` prints a table; the device numbers are for the integration runner.

| finding | status | commit | note |
|---|---|---|---|
| F1 | done in round 13 (owner's decision, not bit identical for a moving morph) | 58588494 | each voice's morph age starts at a slot after the build of its first period: one of 8 evenly spaced periods, seed = note serial + 4 x oscillator, order 0 2 4 6 1 3 5 7 of the seed mod 8 (`WtOsc::setMorphStagger`, `morphStaggerSlot`; hook in `Voice::tick` at the note start). A chord of 8 is flat per block (256 cases, worst max - min 0, old 8); a held static morph is bit identical; Mac worst block of 8 Harmonic Stretch voices 40.7 -> 13.3 us, median 3.9 -> 9.3 us; `tier_bench --config low` on the Mac: Stretched Glass Choir p99 3.34 -> 2.30 %, Shepard Ascent 2.95 -> 2.00 %, medians +0.33 / +0.31 points, means unchanged. `docs/release/AUDIBLE-CHANGES-ROUND-13.md` |
| F2 | done | 47f8c731 | pre-twiddle and clear only for bins 0..K (fast build); Mac build at the bench chord band limits 3.07 -> 2.94 us (about 4 %) |
| F3 | done | ef84927b | stages 2 and 4 fused in NEON, swap list: with F2, a Harmonic Stretch build in the oscillator test 0.98 -> 0.67 us per block of 4 periods (-32 %) |
| F4 | done | 16dadf0a | slot swap instead of two 8 KB copies per group; the cache stores only a held morph |
| F5 | done | ba930b50 | spread / pan sines, detune offsets and the lane increment cached on their inputs; update of a modulated oscillator 0.22 -> 0.11 us per block of 4 |
| F6 | partly | 16847317 | byte compare of a padding free OscTick (size asserted), the field compare stays as the fallback so that the shortcut is taken exactly as before; the prev / cur buffer swap is not done (a 48 float copy, not the cost) |
| F7 | done, gain not measurable on the Mac | 978cbb4c | prefix sum in registers, lerp differences once, `2 x1` as `x1 + x1`, levelFor / size per lane, per render constants hoisted; the Mac render time is unchanged within 2 % (the out of order core hides it); only the device can tell |
| F8 | done | dd363a52 | one reciprocal per render for a power of two n (proved exhaustively in the test), divide otherwise; the voice cross-fade too |
| F9 | not done (not bit identical, ordered not to) | | |
| F10 | done | 33c70df3 | `fmOnly` path for FM A / B / Thru-Zero / Linear alone; Mac render 2.5 -> 1.8 us per block (-30 %) |
| F11 | done | ed2d4a02 | the pole gain once per lane per render; Mac 1.37 -> 1.26 us |
| F12 | done | 847f15c8 | `dsp/noise_coeff.h`, cache keyed on pitch, filter amount and rate |
| F13, F14 | not done (ordered not to) | | |
| silent oscillator | done | 0b36ee14 | see section 10 |

## 1. The headline

The oscillators are not where the six dearest presets spend most of their time. Measured exclusive section times (`sections.csv`, percent of the block, one run of 10 s):

| preset | all oscillator sections | largest oscillator section | largest section overall |
|---|---|---|---|
| Lorenz Garden | 17.4 % | wavetable read 10.5 % | filter 1 34.1 % |
| Stretched Glass Choir | 23.5 % | morph cycle build 6.2 %, wavetable read 5.9 % | filter 1 10.2 % |
| Parallel Dusk Pad | 8.3 % | wavetable read 4.6 % | filter 2 39.7 % |
| Ring Mod Gamelan | 8.4 % | wavetable read 4.2 % | filter 2 31.1 % |
| Shepard Ascent | 13.6 % | morph cycle build 5.8 % | filter 2 16.5 % |
| Triple Fifth Lead | 15.4 % | wavetable read 10.0 % | filter 1 31.5 % |

("All oscillator sections" = oscillator update + oscillator parameters + wavetable read + morph read + morph build + sub oscillator + sub tick.) The variant runs agree: Lorenz Garden median 85.2 % falls to 44.4 % with filter 1 off, but only to 79.1 / 80.8 / 82.2 % with oscillator 1 / 2 / 3 off. Removing every oscillator waste found below cannot bring Lorenz Garden from p99 107 % to 70 %. The filter models (Lorenz: filter 1 model 13; Parallel Dusk and Ring Mod: filter 2 models 24 and 8) are the main lever and are outside this review.

Two oscillator effects are large and certain:

- The morph cycle builds of all voices of a chord land in the same block (finding F1). This is a p99 effect: Stretched Glass Choir p99 101 % falls to 71 % and Shepard Ascent 95 % to 67 % with the morph off (`variants.csv`, warpmorph0), and the longest block of Stretched Glass Choir spent 1.27 ms of its 5.82 ms in the morph build.
- The plain table read costs about 90 to 96 cycles per unison lane per sample, measured. The arithmetic of the NEON loop needs roughly a third of that (finding F7).

None of the six heaviest presets uses a warp (inventory of the preset files, section 3). The warp findings matter for Tine Glass Keys (p99 75.9 %), Bell FM Ripple Lead (77.5 %) and Bend Formant Growl (70.9 %).

The run to run spread is large: the round 10 tables give Lorenz Garden median 96.1 % / p99 106.8 % (3 passes), the breakdown run of the same day 85.2 % / 106.8 % (1 pass of 4 s) and the section run 112.7 % with the section clocks. Percentages of the block below are taken from the section run with the clock cost removed (`less_clock_ms`).

## 2. Settings that decide the loops

- Control period: `kControlPeriod = 32` samples (`core/engine-own/dsp/util.h:20`), the same on every tier and profile. The `control=` key of a profile (`shell-mpc/device_profile.h:122`) is NOT the period: it switches the economy group Control (`core/include/synthyboy/quality.h:68`). Key 61 `low` = `control=economy`; XL `high,voices=16,control=economy` = also economy. With that group the events sit on the 32 sample grid (`own_engine.cpp:1501-1506`), so with 128 frame blocks every chunk passed to the voices is n = 32.
- Key 61 profile `low` (`shell-mpc/device_profile.h:41-45, 55, 188`): 8 voices, unison cap 3, `warp=2x` (2x oversampling, 32 tap decimator), `morph=half` (rebuild every 512 samples, half size transform), `math=fast` (table sine, rational tanh), `control=economy`. A 5 voice unison (Triple Fifth Lead osc 1) therefore runs 3 lanes.
- XL profile (`device_profile.h:187`): 16 voices, unison 16, warp 4x with 128 taps, morph full (every 256 samples, full transform), math exact (libm), control economy.
- Compile flags of the arm32 build: `-march=armv7-a -mfpu=neon -mfloat-abi=hard` (`cmake/toolchains/armv7-linux-gnueabihf.cmake:16`), `CMAKE_BUILD_TYPE=Release` (`scripts/mpc-container-build.sh:104`, CMake default `-O3 -DNDEBUG`, confirmed in `round10/bench-build/arm32/CMakeCache.txt`). No `-mcpu`/`-mtune`, no `-ffast-math`, no `-fno-math-errno`, no `-mfpu=neon-vfpv4`. Consequences, seen in the disassembly:
  - libm functions stay calls (`bl sinf`, `bl exp2f`, `bl expm1f`) and, with `errno` semantics, GCC never hoists or merges them, even when the argument is loop invariant (F11).
  - GCC does not auto-vectorise float loops for NEON on ARMv7 without `-funsafe-math-optimizations`. Every float loop that is not written with intrinsics runs scalar on VFP (the morph build loops, the XL decimator).
  - `-march=armv7-a` has no hardware integer divide although the A17 has it (`/proc/cpuinfo`: `idiva idivt`): integer divisions are calls (`__divsi3` in `WtOsc::render`).
  - GCC contracts `a * b + c` into `vmla.f32`, which on VFPv3 is not fused (two roundings): no change of results.

## 3. What the heavy presets use (from the preset files)

| preset | voices on Key 61 | oscillators (type, unison lanes on Key 61) | warps | morph | modulated oscillator keys |
|---|---|---|---|---|---|
| Lorenz Garden | 6 | 3 wavetables: 3, 2, 1 lanes; sub on | none | none | osc1 frame, pan, level; osc2 frame, pan; osc3 frame, level (random LFOs, every period) |
| Stretched Glass Choir | 8 | osc1 1 lane, osc2 3 lanes | none | osc1 Harmonic Stretch (3) | osc1 morph amount (random LFO); osc2 frame, pan |
| Parallel Dusk Pad | 6 | osc1 2 lanes, osc2 1 lane | none | none | osc2 level (velocity, static per note) |
| Ring Mod Gamelan | 8 | osc1 1, osc2 1 | none | none | none of the oscillator keys per period |
| Shepard Ascent | 4 | osc1 1, osc2 1 | none | both Shepard (10) | osc1 / osc2 morph amount and level (LFO 1) |
| Triple Fifth Lead | 8 | osc1 3 (of 5), osc2 1, osc3 1 | none | none | detune, levels (static in the bench) |

Factory warps in use (all 160 preset files under `content/presets`): Sync (6 presets), Formant (3), Bend (2), FM A (2). No Filter LPF / HPF, no Dist (tanh), no Remap, no FM Exp warp. No `quality.oversample_4x`. Morph: Vocode 1, Formant Scale 1, Harmonic Stretch 1, Shepard 2 oscillators. Noise on: 15 presets, none of the six.

The bench (`bench/tier_bench.cpp:386-400`) strikes keys 36, 43, 50, 57, 64, 71, 78, 85 in one block every second; the first `polyphony` of them sound.

## 4. The call chain and the loop nest

```
OwnEngine::process                          own_engine.cpp:1265
  while (pos < numFrames)                   :1398   per chunk; on Key 61 per control period of 32 samples (4 per block)
    controlTick ()                          :1402 -> :1098
      for each active voice: Voice::tick    :1151-1152 -> voice.cpp:420           per voice per period
        for o in 0..2                        voice.cpp:785-987                     per oscillator
          parameter reads (P ())             :788-949       section "oscillator parameters"
          WtOsc::update                      :955 (off: :833)  section "oscillator update"
          WtOsc::updateMorph                 :956            builds a morph cycle every 16 periods (low), 8 (XL)
          LibOsc::update (content types)     :962-986
        sub: WtOsc::update                   :1013
    for each active voice: Voice::render     own_engine.cpp:1529-1531 -> voice.cpp:1116
      for o in 0..2: WtOsc::render           voice.cpp:1199  -> wt_osc.h:480     per oscillator per period
        for v in unison lanes                wt_osc.h:498                          per lane
          lane setup: levelFor, size, cycles wt_osc.h:511-558
          no warp:  renderPlain              :633 -> :871     4 samples per NEON step
          warp, not oversampled: sampleAt    :637-644         per sample
          oversampled warp                   :646-736         per oversampled sample (x2 low, x4 XL), then decimator per sample
        LibOsc::render (content types)      voice.cpp:1216
      sub: WtOsc::render                     voice.cpp:1237   (1 lane, plain read)
      noise generator                        voice.cpp:1248-1315 per sample
```

Lorenz Garden per block on the Key 61: 6 voices x 4 periods = 24 voice periods. Lanes per voice: 3 + 2 + 1 + sub 1 = 7. So 168 lane periods and 5376 lane samples per block. 72 oscillator updates per block (3 per voice period), all of them full updates because a random LFO moves frame or pan or level in every period. Measured: wavetable read 0.3051 ms per block = 490 k cycles / 5376 = 91 cycles per lane sample; update 0.0846 ms / 72 = 1.18 us = 1890 cycles per full update.

The 91 cycles per lane sample is confirmed by two variants: Lorenz Garden with unison 1 removes 3 of 7 lanes: 3 x 6 x 128 x 91 = 210 k cycles = 4.5 % predicted, 4.7 % measured (85.22 -> 80.51). Triple Fifth Lead with unison 1 removes 2 lanes: 2 x 8 x 128 x 90 = 184 k = 3.9 % predicted, 3.2 % measured. The same unit cost appears in every preset: Triple Fifth 90, Parallel Dusk 93, Ring Mod 96, Stretched Glass (osc2) 90 cycles per lane sample. The morph read costs 170 (Stretched Glass) and 177 (Shepard) cycles per lane sample.

## 5. Findings

Each finding: where, what, loop level (with the caller), why it is or is not waste, consequence for the output, saving per heavy preset with the arithmetic, confidence, and the change.

### F1. The morph rebuilds of all voices fall in the same block

- Where: `wt_osc.h:421-424` (the rebuild period and the build at the first period of a note), `wt_osc.h:475` (`morphAge = 0` after a build), `wt_osc.h:744-745` (age advance). Called from `voice.cpp:956` once per voice per oscillator per control period.
- What: a note builds its morph cycle in its first period, then every 512 samples (16 periods) at tier low. All notes of a chord start in the same period, so all their builds come in the same period: one block in four carries every build, the other three none.
- Measured: Stretched Glass Choir, `osc: morph cycle build` 0.1797 ms per block over 2.0 calls = 90 us per build; 8 voices x 1 morph oscillator = 8 builds in every fourth block. Shepard Ascent: 0.1681 ms / 2.1 calls = 80 us per build; 4 voices x 2 morph oscillators = 8 builds in every fourth block. The longest block of the Stretched Glass section run: 5.82 ms, of which morph build 1.27 ms.
- Waste: the work is needed, its placement is not. It sets the p99, not the median.
- Consequence: not bit identical. A voice refreshes its morph cycle at other instants (the second build up to 15 periods, 11 ms, earlier); the cross-fade from the old to the new cycle is unchanged. Tier low already accepts a morph lag of up to 12 ms (`docs/MPC.md:437`). Inaudible for LFO rate modulation; I rate it sound neutral, not bit neutral.
- Saving (estimate):
  - Stretched Glass: today the rebuild block carries 8 x 90 = 720 us = 24.8 % extra. Spread over 16 periods: 2 x 90 = 180 us = 6.2 % in every block. p99 falls by up to 24.8 - 6.2 = 18.6 points; range 8 to 19 points.
  - Shepard Ascent: 8 x 80 = 640 us = 22.1 % today; 2 x 80 = 160 us = 5.5 % after. p99 falls by up to 16.6 points; range 8 to 17.
  - The mean stays the same. The MEDIAN RISES by about the per block share (Stretched +6.2, Shepard +5.5 points), because three blocks in four had no build before. F2, F3, F4 cut every build by about a fifth and take part of that back.
- Confidence: high for the mechanism (measured longest block, section counts), medium for the size of the p99 change (other spikes may sit at p99).
- Change: give WtOsc a stagger slot. In `Voice::tick` pass `s = (voiceIndex * 5 + o * 3) % P` with `P = rebuildEvery / kControlPeriod` (16 at low, 8 at XL); for the bench chords this gives 8 distinct periods for both presets. In `updateMorph`, after the build of the first period of a note (`firstOfNote`), set `morphAge = s * kControlPeriod` instead of 0 (line 475). After that build `a == b` (line 472-473), so the weight does not matter until the next build, which comes `s` periods early; from then on the normal 512 sample rhythm runs, shifted per voice. Keep `wB0 / wB1` clamped at 1 as now. Measure: `--config low --load "Stretched Glass Choir" --load "Shepard Ascent" --seconds 10 --repeat 3`, compare p99 and median before / after.

### F2. The morph build transforms and clears far more bins than the band uses (bit identical)

- Where: `morph.cpp:139-140` (clears 2 x 2048 floats), `morph.cpp:371-378` (the pre-twiddle of the half size transform, 1024 iterations, about 22 scalar operations each). Called from `wt_osc.h:467` once per build (F1 gives the rate).
- What: only bins 1 .. K and 2048 - K .. 2047 can be non zero (`put ()` refuses everything else, `morph.cpp:143-144`), with K = band limit of the voice (`wt_osc.h:438`: 22050 Hz over the highest unison frequency). For 1024 - K > k > K both inputs of the pre-twiddle are zero and `zr[k] = zi[k] = +0`.
- Loop level: per build, per voice, per morph oscillator, every 16 periods at low.
- Waste: work on known zeros. K for the bench keys 36 .. 85: 337, 225, 150, 100, 67, 45, 30, 20. The skipped fraction `1 - (2K + 1) / 1024`: 0.34, 0.56, 0.71, 0.80, 0.87, 0.91, 0.94, 0.96.
- Consequence: bit identical. With `re = im = +0` the loop gives `er = +0`, `orr` and `oi` are signed zeros, `zr = +0 - (+-0) = +0` and `zi = +0 + (+-0) = +0` in round to nearest. Writing `+0` directly is the same bits.
- Saving (estimate): pre-twiddle about 3 measured mul+add units per iteration: 1024 x 8.6 ns = 6 to 9 us; clear 16 KB = 4 x 1.17 = 4.7 us (measured memset).
  - Stretched Glass (8 notes, mean skipped fraction 0.76): 0.76 x (10.7 to 13.7 us) = 8.1 to 10.4 us per build, 9 to 12 % of a build. Per block 2.0 builds: 16 to 21 us = 0.56 to 0.72 %. In the rebuild block: 8 x = 65 to 83 us = 2.2 to 2.9 points of p99.
  - Shepard Ascent (keys 36 .. 57, mean 0.60): 6.4 to 8.2 us per build; 2.1 builds: 0.46 to 0.59 %; rebuild block 1.8 to 2.3 points.
- Confidence: high on correctness, medium on size.
- Change: in `buildMorphCycle`, fast branch: if `2 * K + 1 < kM`, run the pre-twiddle only for `k <= K` and `k >= kM - K` and write `zr[k] = zi[k] = 0.0f` for the rest (a `std::fill` of the middle). Clear only the bins that are read or added to: `[0, K]`, `[kM - K, kM + K]`, `[kN - K, kN - 1]` (fill all when the ranges overlap). Keep the full clear for the non fast path (XL), or prune its first stages the same way. The existing scalar / NEON same bits test of the morph transform covers it; add a test that compares the cycle with the old code for K = 20, 100, 337, 1023.

### F3. The first two transform stages and the bit reversal are scalar (bit identical)

- Where: `morph.cpp:103-112` (stages with half = 1 and 2 never take the NEON form at `:83`), `morph.cpp:66-74` (bit reversal: 1024 iterations with a compare and a branch).
- Loop level: per build.
- Waste: about 2 x 512 scalar butterflies and 1024 reversal tests per build, where NEON (`vld2q` / `vst2q` de-interleave for half = 1, `vtrn` for half = 2) and a precomputed list of the 496 swap pairs would do.
- Consequence: bit identical if the same products are formed in the same order with the same twiddle floats (`stageC / stageS` already hold them side by side, `morph.cpp:25, 41-46`; note `cs[512]` is not exactly 0, so keep the multiplications).
- Saving (estimate): scalar stages 1024 butterflies x about 8.6 ns = 8.8 us, NEON about a third: 4 to 6 us saved; the swap list 1 to 1.5 us. Total 5 to 7.5 us per build. Stretched Glass: 2.0 x = 10 to 15 us = 0.34 to 0.52 %; rebuild block 1.4 to 2.1 points. Shepard Ascent: 0.36 to 0.54 %; rebuild block 1.4 to 2.1 points.
- Confidence: medium.
- Change: in `inverseFft`, before the `len` loop, a NEON pass for `len = 2` and `len = 4` with `scalarOnly` falling back to the present loops; replace the reversal loop by a static table of `(i, j)` pairs per size (1024 and 2048) built in `Twiddles`.

### F4. Two 8 KB copies per build that a buffer swap avoids (bit identical)

- Where: `wt_osc.h:444` (the new cycle is copied over the old one before every build), `wt_osc.h:468-469` with `morph.h:67-75` (every build is stored in the 16 entry `MorphCache`, 8.2 KB copy).
- What: with a modulated morph amount (Stretched Glass, Shepard) the cache key (`m`, `phaseInc`) never repeats, so the store is never read; the cache's 131 KB also pushes the tables out of L1 / L2.
- Saving (estimate, measured memcpy 0.88 us per 4 KB): 2 x 1.8 = 3.6 us per build. Stretched Glass 2.0 x = 7.2 us = 0.25 %; Shepard 0.26 %; rebuild block 8 x = 29 us = 1.0 point.
- Consequence: bit identical (the cache only saves time; the swap keeps the same samples).
- Confidence: medium.
- Change: keep an index `cur[g]` (0 / 1) per group and swap it instead of the copy at line 444; `render ()` takes `cA`, `cB` from the index (line 528-529). Skip `cache->store` when the key of this oscillator's last build differed in `m` or `framePos` (a moving morph), keep it for a static morph (the chord case the cache was made for).

### F5. The update recomputes sines and the detune curve that did not change (bit identical)

- Where: `wt_osc.h:239` (stereo spread: 2 `sinf`), `wt_osc.h:277` (pan: 2 `sinf`), `wt_osc.h:327` -> `util.h:66-71` (`powerShape`: 2 `expm1f` per non centre pair), `wt_osc.h:296` (a double divide per lane), `wt_osc.h:233` (`sqrtf`). The disassembly of `WtOsc::update` has exactly 4 `bl sinf`, 2 `bl expm1f`, 1 `bl sqrtf`, 9 `vdiv.f64`; the double `exp2` (6 call sites) is already skipped by the frequency cache.
- Loop level: per voice, per oscillator, per control period, whenever any field of the tick changed (`wt_osc.h:211`). A random LFO on the frame, the pan, the level or the morph amount makes every period a full update.
- Waste: the spread sines depend only on `t.spread` (and `t.law`), the pan sines only on `t.pan / t.panRight`, the detune offsets only on `U`, `t.mode`, `t.curve`, `t.detuneSemis`, `t.stack`. A frame or level change recomputes them all.
- Saving (estimate, 80 to 200 cycles per call):
  - Lorenz Garden: 16 calls per voice period, of which 12 are avoidable (osc1: spread 2 + detune 2; osc2: spread 2 + detune 2; osc3: spread 2 + pan 2; the pans of osc1 and osc2 move). 12 x 24 = 288 calls x 80 to 200 = 23 k to 58 k cycles = 0.49 to 1.23 %. The measured section is 2.92 %.
  - Stretched Glass: osc1 4 sines (only the morph amount moves) + osc2 spread 2 + detune 2 = 8 avoidable; 8 x 32 = 256 calls = 20 k to 51 k cycles = 0.44 to 1.10 %.
  - Shepard Ascent: 4 + 4 avoidable; 8 x 16 = 128 calls = 10 k to 26 k cycles = 0.22 to 0.55 %.
  - Parallel Dusk, Ring Mod, Triple Fifth: their ticks do not change per period, the shortcut runs: 0.
- Consequence: bit identical (the same calls with the same arguments, cached).
- Confidence: high that the calls run (disassembly) and are avoidable; the per call cost is an estimate.
- Change: in `WtOsc` keep `lastSpread, lastLaw, saCache, sbCache`, `lastPan, lastPanRight, panLCache, panRCache`, and per pair `i` the `semisOff` with its key `(U, mode, curve, detuneSemis, stack, stackRepeat)`. Compute only on a key change. Store `curInc` next to `freqHz` (`wt_osc.h:291-293`) so a held pitch also skips the double divide (key: `freqSemis[n]` and `sampleRate`).

### F6. The "nothing changed" shortcut of the update is itself dear (bit identical, needs a measurement)

- Where: `wt_osc.h:57-66` (`sameOscTick`: about 45 float compares, each a `vcmp` plus `vmrs` transfer) and `wt_osc.h:202-209` (48 copies of the previous arrays before the test).
- Loop level: per voice, per oscillator (also the ones that are off, `voice.cpp:833`), per period.
- Measured: where the ticks do not move, the section `oscillator update` costs per call: Triple Fifth 0.041 ms / 96 = 690 cycles, Ring Mod 0.0355 / 96 = 595, Parallel Dusk 0.0303 / 72 = 675. That is the shortcut plus `updateMorph`'s early exit plus `LibOsc::updateOff`.
- Waste (estimate): a bit compare of the tick (about 45 words) plus a buffer swap of `prev / cur` would cost 150 to 250 cycles. Saving 350 to 550 cycles per call: Triple Fifth and Ring Mod 96 x = 34 k to 53 k cycles = 0.72 to 1.13 %; Parallel Dusk 72 x = 0.54 to 0.85 %; Lorenz, Stretched, Shepard about 0 (full updates).
- Consequence: bit identical. Equal bits imply equal results of the deterministic update; `+0 / -0` or different NaN payloads only force a full update that produces the same values.
- Confidence: low on size (my split of the 600 to 690 cycles is by reading). Measure: a `--micro` case "WtOsc::update unchanged tick" before and after.
- Change: make `OscTick` free of padding (put the bools together and pad explicitly; `static_assert (std::has_unique_object_representations_v<OscTick>)`) and test with `std::memcmp (&t, &lastT, sizeof t)`. Replace the three copy loops by `prev` / `cur` index arrays swapped per period (the copies are only needed where `update` returns early, which the swap covers).

### F7. The plain table read costs about 90 cycles per lane sample (bit identical parts, needs a measurement)

- Where: `wt_osc.h:871-911` (`renderPlain`, NEON), `wt_osc.h:750-768` (`hermite4`), the lane setup `wt_osc.h:511-558` with `wavetable_bank.cpp:250-263` (`size`, `levelFor`).
- Loop level: per voice, per oscillator, per unison lane, per 4 samples. Also the morph read and the sub use it.
- Measured unit cost: 90 to 96 cycles per lane sample (section 4). The loop body is about 135 instructions per 4 samples (disassembly of `WtOsc::render`, 0x96626 .. 0x968f2), so roughly 34 per sample of arithmetic and addressing. Where the rest goes cannot be read from the code alone. Candidates seen in the disassembly:
  - Two NEON to core to NEON round trips through memory per 4 samples: the phase prefix sum (`wt_osc.h:884-892`: `vst1q` of the increments, 4 scalar loads, adds, 4 stores, `vld1q`) and the index transfer of `hermite4` (`wt_osc.h:754`: `vst1q_s32` then 4 scalar loads).
  - Register spills: about 10 `vldr` / `vstr` of Q values per 4 samples (the ramp, gains and the constants 0.5, 2.5, 2.0, 1.5 do not fit the 16 Q registers).
  - Per lane and period: `levelFor` and `size` are out of line calls (`bl` at 0x961be, 0x961c4); `levelFor` loops up to 17 times calling `maxHarmonics`, each through the guard of a function local static (`wavetable_bank.cpp:238-242`) and a `vmrs` per compare. About 150 cycles per lane period, about 5 per sample (estimate).
- Consequence of the changes below: bit identical.
- Saving (estimate, 10 to 30 % of the read sections; only a measurement decides): Lorenz 10.51 % -> 1.1 to 3.2 %; Triple Fifth 9.97 % -> 1.0 to 3.0 %; Stretched Glass read 5.93 % + morph read 3.74 % -> 1.0 to 2.9 %; Parallel Dusk 4.64 % -> 0.5 to 1.4 %; Ring Mod 4.22 % -> 0.4 to 1.3 %; Shepard morph read 3.90 % -> 0.4 to 1.2 %.
- Confidence: high that the overhead exists, low on how much each change recovers.
- Change:
  1. Prefix sum in registers: `s1 = inc + vextq_u32 (zero, inc, 3)`, `s2 = s1 + vextq_u32 (zero, s1, 2)`, `phs = vdupq_n_u32 (ph) + vextq_u32 (zero, s2, 3)`, `ph += vgetq_lane_u32 (s2, 3)`. Integer adds modulo 2^32: the same phases.
  2. In `hermite4` write `2.0f * x1` as `vaddq_f32 (x1, x1)` (exact), which frees a register.
  3. Make the level table a namespace scope constant (like the decimator taps, `wt_osc.h:119-142`) and inline `levelFor` / `size`; better, compute `level` and `N` per lane in `update ()` and keep them while the lane's `curInc / prevInc` and the warp factors are unchanged.
  4. F8.
  Measure first with a `--micro` case "WtOsc::render, 1 lane, 32 samples, plain" (warm), split into setup and loop, then A/B each step with `--sections`.

### F8. Four float divides per four samples where a second cycle is read (bit identical at n = 32)

- Where: `wt_osc.h:904-905` (`renderPlain`, the weight of the second cycle: four `vdiv.f32` per group, seen at 0x967ea .. 0x9680e), `wt_osc.h:921` (scalar tail), `wt_osc.h:640`, `:690`, `:711` (warp paths, one per sample), `voice.cpp:1203` (type cross-fade).
- Loop level: per lane per sample, only while `cB != nullptr`: always for the morph read (old and new cycle), for the plain read only in a period where the frame index changes (`smooth_interp` off) or always with `smooth_interp` on.
- Saving (estimate, 10 to 16 cycles per divide, not pipelined): Stretched Glass morph read 8 voices x 128 = 1024 lane samples x 10 to 16 = 10 k to 16 k cycles = 0.22 to 0.35 %; Shepard Ascent the same 0.22 to 0.35 %. Lorenz Garden: unknown, it depends on how many periods change frame (measure by counting periods with `cB != nullptr`).
- Consequence: with `invN = 1.0f / n` computed once and `(k + j + 1) * invN`, the result is the same bits when n is a power of two (n = 32 on Key 61 and XL with `control=economy` and 128 frame blocks). For another n the weight can differ by 1 ulp (6e-8 relative): rounding level.
- Confidence: high.
- Change: compute `invN` once per `render` / `renderPlain` call and pass it; replace the five divisions listed.

### F9. The morph cycle is always 2048 samples (not bit identical)

- Where: `wt_osc.h:533` (`N = Wavetable::kCycle` for the morph read), `morph.cpp:364-391` (the build always makes 2048 samples).
- What: the plain read uses the mip level of the pitch (`wavetable_bank.h:17-21`: 8 times the highest harmonic, at most 2048); the morph read always reads two 8 KB cycles per group. Stretched Glass: 8 voices x 16 KB = 128 KB, Shepard 4 x 2 x 16 KB = 128 KB, against a 32 KB L1D. Measured 170 to 177 cycles per lane sample for the morph read against 90 for the plain read.
- Saving (estimate): building at `nextPow2 (8 K)` cuts the butterfly work of the bench chord of Stretched Glass by about 46 % (sizes 2048, 2048, 2048, 1024, 1024, 512, 256, 256) and of Shepard by about 14 %. If the transform is 40 to 55 % of a build (the measured 2048 point real inverse transform is 34 to 51 us of the 80 to 90 us), Stretched saves 18 to 25 % of a build = 1.1 to 1.5 % mean and 4.5 to 6 points in the rebuild block; Shepard 0.35 to 0.45 % mean. The morph read could fall toward the plain read's 90 cycles if the extra 80 cycles are cache misses: 0 to 1.8 % for each of the two presets. Only a measurement can split it.
- Consequence: not bit identical: the same band limited cycle sampled more coarsely, with the interpolation images of the bank's mip levels (the accepted level of the plain read). Inaudible by the bank's own measurement, but a new risk: when the pitch moves (glide, vibrato) the old and the new cycle may have different sizes.
- Confidence: medium on the build part, low on the read part.
- Change: `updateMorph` chooses `Nm = clamp (nextPow2 (8 * K), 16, 2048)`; `buildMorphCycle` gets `Nm` and runs the transform at that size (the twiddle stride `kN / len` already allows it); keep `Nm` per group; when a rebuild needs another size, rebuild the old cycle at the new size too (or hold the size while a note sounds). `render` uses `N = Nm`.

### F10. The FM warp goes through the general per sample path (bit identical)

- Where: `wt_osc.h:581-627` (`sampleAt`, compiled as an out of line function, `bl` at 0x963a8, 0x96dae, 0x977cc), called per oversampled sample from `wt_osc.h:714-715`; inside, `warpPhase ()` and `warpSample ()` are out of line calls with a `switch` (`bl` at 0x95080, 0x94eda); for FM A, `warpSample` does nothing (`warp.h:231`, default case).
- Loop level: per voice, per lane, per oversampled sample (2x on Key 61).
- Presets: Tine Glass Keys (p99 75.9 %) and Bell FM Ripple Lead (77.5 %), osc1 FM A, 1 lane, 8 voices on Key 61.
- Saving (estimate, 30 to 60 cycles of calls and switches per oversampled sample): 8 x 128 x 2 x (30 to 60) = 61 k to 123 k cycles = 1.3 to 2.6 % for each of the two presets.
- Consequence: bit identical if the fast path does the same arithmetic as `sampleAt` for this case.
- Confidence: low on size. Measure: `--sections --load "Tine Glass Keys" --load "Bell FM Ripple Lead"` (section `osc: warp + decimator`), before and after.
- Change: next to `syncOnly` (`wt_osc.h:654-670`) add `fmOnly` (FM A / B / Lin / TZ in slot 1, nothing in slot 2, no filter stage): `m0 = lerp (k > 0 ? cross[cs0][k - 1] : lastCross[cs0], cross[cs0][k], fk)`, `q = frac (p + m0 * a0 * 0.3f)`, then the read of `syncSample` with `window = 1` and no `warpSample` call.

### F11. Filter LPF / HPF warp: `exp2f`, a divide and a double conversion per sample (bit identical)

- Where: `wt_osc.h:565-579` (`filterStage`; `exp2f` at :567, `x / (1 + x)` at :569, `static_cast<float> (rateI)` at :568), called from `sampleAt` (`:622-625`). The disassembly of `sampleAt` has two `bl exp2f` (0x94f2a, 0x94fca): the call is not hoisted (libm with `errno`).
- Loop level: per lane per sample (per oversampled sample when the other slot oversamples).
- Waste: `type`, `amount`, `f0Hz`, `rateI` are constant during one `render` call.
- Saving: 0 for every factory preset (none uses these warps). For a patch with one such oscillator at 8 voices: 8 x 128 x (80 to 200 + 14) cycles = 96 k to 220 k = 2.1 to 4.7 % (estimate), twice that oversampled.
- Consequence: bit identical (the same expression evaluated once).
- Confidence: high.
- Change: compute `g0, g1` (the `g` of each slot) per lane before the sample loop and pass them into `filterStage`.

### F12. Noise: six libm calls per voice per period (bit identical)

- Where: `voice.cpp:1252-1258` (`exp2f`, `log2f`, `powf`, `expf`, `powf`, `expf`), per voice per chunk while the noise sounds.
- Waste: the inputs (`noisePitchSemis`, `noiseFilterAmount`, the sample rate) change only with their parameters.
- Saving: 0 for the six. For the 15 presets with noise at 8 voices: 32 voice periods x 6 x (80 to 200) = 15 k to 38 k cycles = 0.33 to 0.82 % (estimate).
- Consequence: bit identical.
- Change: cache `rate, pitchGain, lpC, fC` in the voice with the three inputs as the key.
- Also seen: brown noise divides per sample (`voice.cpp:48`, `/ 1.02f`); a multiply by a reciprocal would change the last bit. Leave it.

### F13. Compile flags (measure; expected bit identical)

- Where: `cmake/toolchains/armv7-linux-gnueabihf.cmake:16`.
- Not a code waste but it decides several of the above for the whole engine: no integer divide instruction, generic scheduling, libm calls never hoisted.
- Proposed A/B (one bench binary each, all 160 presets, `--verify` against the present binary):
  1. `-mcpu=cortex-a17 -mfpu=neon -mfloat-abi=hard`: the A17's integer divide and scheduling; still VFPv3, so no fused multiply add and the float results stay the same.
  2. plus `-fno-math-errno`: lets GCC treat `sqrtf`, `exp2f`, `sinf` as pure (hoist, merge, inline `vsqrt` without the `errno` branch). The libm results are the same; `--verify` must prove that no call was replaced by another function.
  Do not use `-mfpu=neon-vfpv4` without `-ffp-contract=off`: GCC would fuse multiply adds and every result of the engine would change in the last bits.
- Saving: unknown, only a measurement can tell.

### F14. XL only: the 4x decimator is a sequential scalar sum of 128 taps (rounding level)

- Where: `wt_osc.h:729-730`, used when math is exact (the XL profile); the 4 lane `dot128` (`:727`) is used only with the Math economy group.
- Loop level: per voice, per lane, per output sample: 128 dependent multiply adds.
- Consequence of using `dot128` on the XL: rounding level, about 1e-7 relative (the code's own statement, `wt_osc.h:146-147`).
- Saving: not estimated (A76 costs); the XL is inside its budget (Tine Glass Keys, the dearest warp preset on the XL, p99 38.5 %). Measure with `--sections` on the XL before deciding.

### Not waste on the Key 61

- `warp.h:107-108, 167-168` and `wt_osc.h:805-823`: the libm `sin` / `cos` / `tanh` run only when math is exact (XL). On the Key 61 the table sine (`fastmath::sinCycles`, `sinCycles4`) and the rational tanh are used. This is the XL's chosen quality.
- `wt_osc.h:527`: an integer division per lane per period (`__divsi3`): about 0.01 %.
- `voice.cpp:1171-1172`: `cos` / `sin` of the filter blend per source per period, only for destination "Filter blend".

## 6. Already optimised (do not report again)

- `wt_osc.h:211-212`: the update returns early when the tick did not change (a held note without per period modulation).
- `wt_osc.h:284-295`: the double `exp2` of a lane's frequency is cached by pitch.
- `wt_osc.h:500-510`: a silent lane only advances its phase, without a 64 bit conversion.
- `wt_osc.h:629-634` with `:871-926`: the plain read without warps is a separate NEON path (four samples per step, `hermite4` at `:750-768`).
- `wt_osc.h:654-670`: Sync / Sync Var alone use an inline scalar path.
- `wt_osc.h:675-704`, `:800-866`: two phase warps (Sync, Formant, Bend family) run four oversampled samples per NEON step; with the Math group the sine is the table form `sinCycles4` (`:778-791`).
- `wt_osc.h:150-168`, `:724-727`: the 2x decimator (32 taps) and the 4x one with the Math group are NEON dot products.
- `wt_osc.h:190-194`, `:380-398`: the decimator history is cleared only when it was used; `clearState` works in place.
- `wt_osc.h:421`, `:428`, `:447-452`, `:464-470`: morph rebuild every 512 samples at low, one shared cycle for the unison at low, the second group copied when there is no spread, the morph cache for repeated chords.
- `morph.cpp:81-99`: NEON butterflies for stages with half >= 4 (same bits); `morph.cpp:364-385`: the half size transform of the real signal at low; `morph.cpp:25, 41-46`: the twiddles of each stage side by side.
- `voice.cpp:27-34`: the cheaper noise draw at low; `voice.cpp:1018`: an off noise does nothing; `voice.cpp:827-838` and `:1185-1186`: an off oscillator gets the minimal update and is not rendered or routed; `voice.cpp:1197`: the wavetable render is skipped when the content type's cross-fade weight is 0.
- `own_engine.cpp:1501-1506`: tier low events on the 32 sample grid, so chunks are not cut.

Content oscillator dispatch (for the other review): `voice.cpp:962-986` builds `LibParams` with one `P ()` per library key per period for a non wavetable type and calls `LibOsc::update`; `voice.cpp:1213-1222` calls `LibOsc::render` into a 32 sample buffer and adds it; the wavetable runs alongside only during a type cross-fade (`voice.cpp:1194-1212`).

## 7. The hypotheses of the earlier review

The earlier review is `.claude/worktrees/agy-g67-review-osc/docs/release/CPU-REVIEW-OSC.md`.

| # | claim | verdict | why |
|---|---|---|---|
| 1 | `warp.h:107-108, 167-168`: `sin`, `cos`, `tanh` per sample in the warps, tables would be bit identical, 10 to 20 % | partly | The line numbers are right and the calls are per (oversampled) sample when math is exact, also per lane in `warpPhase4` (`wt_osc.h:809-813`, `:819-823`; the disassembly of `render` has these calls inlined as four `bl sinf` or four `bl cosf` in a row, one per lane, e.g. at 0x9785e .. 0x9789a). But the Key 61 runs `math=fast`: the table forms already run there. A table is never bit identical to libm. The 10 to 20 % has no basis; for the six heaviest presets the saving is 0 (they use no warps), and no factory preset uses a tanh warp. |
| 2 | `lib_osc.cpp:792`: `std::exp2` per sample in the FM rate | partly | It is per sample (`lib_osc.cpp:782-795`, loop over k), but only for FM Exp warps (`fmKind == 2`) on Sample / Granular oscillators, and its argument depends on the modulator sample `m`, so it cannot be hoisted. No factory preset uses an FM Exp warp: saving 0. (`lib_osc.cpp:820`, the Quantize `exp2` per sample, depends only on the amount and is hoistable: for the content review.) |
| 3 | `wt_osc.h:567`: `exp2` per sample in the filter stages | confirmed | Two `bl exp2f` in the compiled `sampleAt`. It is not "slowly changing": `amount` and `f0Hz` are constant during the whole `render` call, so the hoist is bit identical (F11). No factory preset uses these warps; the 2 to 4 % is in the range of my estimate for a patch that does (2.1 to 4.7 % at 8 voices). |
| 4a | `dotLanes` (150-168) NEON | confirmed, with a limit | It runs only at 2x (tier low) and at 4x with the Math group; the XL's exact 4x path is the scalar loop at `:729-730` (F14). |
| 4b | `updateMorph` (413-477) uses NEON and half size transforms | partly | The transform is in `morph.cpp`, not in `updateMorph`; NEON covers only stages with half >= 4 (F3); the half size transform only with `morph=half`. It missed the per voice alignment of the rebuilds (F1) and the work on zero bins (F2). |
| 4c | `render` (480-746) uses `lanePhaseWarp` and `warpedGroup4` | confirmed, with a limit | Only for the Sync / Formant / Bend family without filter stage; FM, PD, Remap, Dist and Quantize warps take the scalar `sampleAt` (F10). |

## 8. Ranking

Order: bit identical first, then by (saving x certainty) / risk. Savings are percent of the 2.902 ms block on the Key 61 (`low`), mean unless "p99" is written.

| rank | ID | output | saving | certainty | risk | change in one line |
|---|---|---|---|---|---|---|
| 1 | F2 | bit identical | Stretched 0.56 to 0.72 (p99 block 2.2 to 2.9); Shepard 0.46 to 0.59 (p99 1.8 to 2.3) | high | low | skip the pre-twiddle and the clear outside bins 0..K |
| 2 | F5 | bit identical | Lorenz 0.49 to 1.23; Stretched 0.44 to 1.10; Shepard 0.22 to 0.55 | high | low | cache spread / pan sines and detune offsets in `update` |
| 3 | F7 | bit identical | Lorenz 1.1 to 3.2; Triple Fifth 1.0 to 3.0; Stretched 1.0 to 2.9; Dusk 0.5 to 1.4; Ring Mod 0.4 to 1.3; Shepard 0.4 to 1.2 | low (size) | low | in register prefix sum, fewer spills, level / size cached per lane |
| 4 | F3 | bit identical | Stretched 0.34 to 0.52 (p99 1.4 to 2.1); Shepard 0.36 to 0.54 (p99 1.4 to 2.1) | medium | low | NEON for transform stages 1 and 2, swap pair table |
| 5 | F8 | bit identical at n = 32 | Stretched 0.22 to 0.35; Shepard 0.22 to 0.35; Lorenz unknown | high | low | one reciprocal of n per render instead of a divide per sample |
| 6 | F6 | bit identical | Triple Fifth 0.72 to 1.13; Ring Mod 0.72 to 1.13; Dusk 0.54 to 0.85 | low | low | memcmp of a padding free tick, swap prev / cur |
| 7 | F4 | bit identical | Stretched 0.25; Shepard 0.26 (p99 1.0) | medium | low | swap the morph buffers, no cache store for a moving morph |
| 8 | F10 | bit identical | Tine Glass, Bell FM Ripple 1.3 to 2.6 each | low | low | inline FM only path like `syncOnly` |
| 9 | F12 | bit identical | 0 for the six; noise presets 0.33 to 0.82 | medium | low | cache the per period noise coefficients |
| 10 | F11 | bit identical | 0 for factory presets; 2.1 to 4.7 for a Filter warp patch | high | low | compute the filter stage gain once per lane per render |
| 11 | F13 | expected bit identical (prove with `--verify`) | unknown | unknown | low | `-mcpu=cortex-a17`, `-fno-math-errno` A/B |
| 12 | F1 | sound neutral, not bit identical | p99: Stretched minus 8 to 19 points, Shepard minus 8 to 17; median plus 5.5 to 6.2 | high (mechanism) | low to medium | stagger each voice's morph rebuild by a slot |
| 13 | F9 | not bit identical | Stretched 1.1 to 1.5 (+ read 0 to 1.8); Shepard 0.35 to 0.45 (+ read 0 to 1.8) | low to medium | medium | morph cycle at the band's size |
| 14 | F14 | rounding level, XL only | not estimated | medium | low | `dot128` for the XL 4x decimator |

F1 is the largest single effect on the measure the owner uses (p99 and blocks over the length) for the two morph presets. It is ranked after the bit identical items only by the rule. Done together with F2, F3, F4 (19 to 24 % less work per build: 16.7 to 21.5 us of 90 us) it keeps most of the p99 gain and the median rises by about a fifth less (Stretched about +4.9 instead of +6.2 points).

Taken together, the bit identical items F2 to F8 are worth, per preset (sums of the ranges above): Lorenz 1.6 to 4.4 %, Stretched Glass 2.8 to 5.8 %, Shepard 1.9 to 3.5 %, Triple Fifth 1.7 to 4.1 %, Ring Mod 1.1 to 2.4 %, Parallel Dusk 1.0 to 2.3 % of the block; the low ends hold if the read changes of F7 recover little. That is real but small next to the filters (31 to 40 % in four of the six).

## 9. What I could not verify, and what to measure

- The split of the measured 90 cycles per lane sample between loop arithmetic, NEON to core transfers, spills, cache misses and lane setup (F7). Measure: a `--micro` case for one lane of `WtOsc::render` (32 samples, warm), then each change of F7 alone with `--sections`.
- The real cost of `sinf`, `expm1f`, `exp2f` of glibc 2.36 on the A17: all libm figures use the 80 to 200 cycle estimate. A `--micro` line per call would replace it.
- Whether the measured 600 to 690 cycles of an unchanged update are mostly the compare and copies (F6).
- How often Lorenz Garden's plain read has a second cycle (frame change periods) (F8).
- Whether the morph read's extra 80 cycles per lane sample are cache misses (F9).
- The p99 effect of F1 on the device; the median rise is arithmetic, the p99 fall an upper bound.
- The FM warp numbers (F10): no section run exists for Tine Glass Keys or Bell FM Ripple Lead.
- XL costs: not estimated (A76 cycle costs differ); only the XL tables were read.
- The section run, the variant run and the round 10 tables of the same day differ by up to 11 points of median for Lorenz Garden; savings above are relative to the section run.

## 10. A silent oscillator (item 7 of the task): what it does and what is put off

What the old code does for an oscillator whose level of the period is 0 (`Voice::tick` -> `WtOsc::update` / `updateMorph`, `Voice::render` -> `WtOsc::render`):

- `render ()` zero-fills L and R and returns at the level check (`tick.level <= 0`): no lane is read, `phase`, `filterState`, the decimator history, `frameIdx`, `lastCross` and `morphAge` do not advance. The output of the oscillator is exactly +0 in every such period, so there is no state of the oscillation (phase, random state, morph age) that a later switch on would need advanced: none advances in the old code either.
- A level that is 0 at the start of a control period and modulated above 0 later takes effect only through the tick of the NEXT period (the level is read once per control period); inside the period `render` still returns zeros. The gains of that next period ramp from the previous period's gains (0), as before.
- What changes in the silent period is `update ()` (the `cur` arrays of the tick, `prev <- cur`, the caches) and `updateMorph ()` (`morphActive`; `morphAge = 0` for an inactive morph; a build when a note starts or the age reached the rebuild period). Of the updates of a run of silent periods only the LAST tick is visible afterwards (`prev` of the next live update is `cur` of the last silent tick, and `cur` is a function of the tick alone: the caches are keyed on their inputs), except `morphAge = 0` of an inactive morph.
- Not done: `Voice::render` still runs `halfSum`, `route` and the peak for the silent oscillator (the zero adds are cheap, and skipping `route` would change which targets and buses count as used).

`WtOsc::deferSilent (tick, rate, withMorph, firstOfNote)` (called by `Voice::tick` for the three oscillators, the off oscillators and the sub): for a tick with level <= 0 it keeps the tick, applies `morphAge = 0` for an inactive morph at once, and returns true so that the caller skips `update` / `updateMorph`; the next `update ()` or `start ()` replays the kept tick (`update ()` and, for the callers that call it, `updateMorph (false)`) first. `render ()` of a pending oscillator returns zeros. A period that would build a morph cycle (a new note, or `morphAge` at the rebuild period) is NOT put off, so every build happens in the period it did and with its own tick; a wavetable pointer is therefore never kept across blocks. Proof: the lockstep test with silent streaks, off periods, restarts and sign-of-zero levels (53000 put off periods in 210000), the directed test (level 0 for 1, 3, 17 and 40 periods, then rising, with and without the low morph), and the preset renders above. One difference of state, none of output: a gain can be -0 in the old code where the new code has +0 (the old shortcut treats +0 and -0 of a tick as equal); `pg + (cg - pg) * t` is +0 or `cg * t` for every sign of zero, so the output bits are the same.
What it saves: the update (1890 cycles for a modulated tick, about 600 for an unchanged one on the Key 61, estimated from section 4) per silent period per oscillator; for Lorenz Garden osc2 at level 0 that is the 1.4 points that variants.csv shows between `osc2lvl0` (82.2 %) and `osc2off` (80.8 %), minus the parameter reads that stay. The Shepard Ascent `osc2lvl0` variant (53.6 % against 53.4 % base) is NOT a silent oscillator: its level is modulated above 0 most of the time (the render and the builds continue), so nothing is skipped there; with a level that stays 0 the old code already builds only once per note (the age does not advance without a render), and the note start build is not put off.
