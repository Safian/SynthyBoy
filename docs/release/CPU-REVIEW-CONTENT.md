# CPU review of the content engines (Sample, Granular, Spectral, Multisample, Convolve, content store, FFT)

Status: review only. No source file changed, nothing built, nothing measured, no device touched. Base: `feature/mpc-overlay` at 152b4a45 (round 10 kernels and the deferred Spectral start are in it).

## Status of the optimisation round (work/opt-content)

Device result (round 12, all three branches merged in d7cbc890, tree proved bit identical to 152b4a45 on all 160 presets x 3 tiers; `.hw-local/mpc-perf/round12/`, summary in the round 12 entry of `docs/MPC.md`): Spectral Vowel Freeze sample / granular / spectral section 0.456 -> 0.429 ms, priming 0.018 -> 0.014 ms, library reset 0.002 -> 0.001 ms (Key 61 `--sections`); no content preset is slower than at 152b4a45 on the Key 61 or the XL (interleaved run of both binaries); `--verify` still shows the 9e-8 .. 1.8e-7 content kernel difference of round 10 on two of twelve loads, within `--tolerance 3e-5`.


Findings done, each bit identical with a test that keeps the old path selectable; the commit is on `work/opt-content`. Skipped findings say why. (The review text below is unchanged.)

| finding | state | commit |
|---|---|---|
| SP1 reset fills | done: `SpectralVoice::reset` clears flags only, `GranularVoice::reset` skips idle grain flags; test starts notes on dirty buffers | 2a402953 |
| SP2 start schedule | done: table made in `prepare`, old loop is `startScheduleReference` | b2a0f581 |
| SP3 lazy tables | done: `RealIfft`, window, start gains, schedule table built in `prepare` (a note builds none: `tablesBuiltForTest`) | 52fc6cdf |
| MK1 bypassed Convolve makeup | done: meter constants, weights of both means, impulse key, level gain, smoothing constant cached by their inputs. Not touched: the per sample `accumulate` and `runMakeup` (no waste found there that keeps the bits) | c2d3accf |
| GR1 scanning head | done | f01a6a63 |
| SA1 sample kernel libm | done (the log2 and the pan gains; the plan itself has no libm) | e53de443 |
| LU1 library update libm | done (pan law, detune curve, spread pan, pitch ratios; `semitonesToRatio (lp.noteSemis - 60)` of the scan keytrack stays: once per control period of a scanning cloud, not per lane) | 9946509c |
| SP5 Phase Lock advance | done: bin 0, peaks and the kept half regions are advanced (gathered); the advance is 0.65 us on the Mac, so only the Key 61 shows the gain (est. 14 to 20 us per frame): to be confirmed by `--sections` on Spectral Vowel Freeze | 2a36556d |
| SP6 priming overlap add | done | 7fce2783 |
| SP10 NEON output stages and lane mix | done for the Spectral and Granular output stages and the Sample / Granular / Spectral lane mix. The multisample mix (MS1) and the Sample voice's own loops are not in it. Caveat: NEON on 32 bit ARM flushes subnormal values | 25488aa2 |
| CS1 analysis only for Spectral oscillators | skipped: a live switch to Spectral on an oscillator without analysis has no analysis for the 0.1 to 0.2 s of the background build (Key 61), while it is instantaneous now; avoiding that silence means keeping the old type until the analysis is published, which is a change of the type switch in `voice.cpp` (not in this round's files). The hand over itself is clear (a late analysis pointer in `ContentSet::Sample`, a pin so a build keeps its set alive, the FxWorker thread, a wait in offline renders) and can follow once the type switch may be changed | none |
| SP4, SP7 to SP9, SP11, SP12, GR2, MS1, SP15 | not in this round's list | none |
| GR3, SA2, SP13, SP14, D1 to D4 | not done on purpose: rounding level or audible, owner decides | none |

Target: MPC Key 61, Cortex-A17 at 1608 MHz, tier `low` (`core/include/synthyboy/quality.h:112`: 8 voices, unison cap 4, oversampling 1, Convolve, Reverb, Flanger, Hyper, Bode bypassed). One block is 128 frames at 44.1 kHz = 2902 us = 4.67 M cycles. One point of the block (1 %) is 29 us = 46.7 k cycles.

Cost labels used for estimates (Cortex-A17, marked "est."): NEON Q-register operation about 2 cycles, scalar float operation 1 to 2, float or double divide about 14 to 30, single precision libm call 80 to 200, double libm call 150 to 400. Measured unit costs are quoted from `.hw-local/mpc-perf/round10/spectral-start/a2/k61-micro.txt` and `k61-micro-after.txt` ("micro"), from the section breakdown `.hw-local/mpc-perf/round10/k61-breakdown/sections.csv` and `variants.csv` ("sections", "variants"), and from `spectral-start/a2/k61-sections-a2.txt` ("A2 sections").

Compile flags of the arm32 build (`cmake/toolchains/armv7-linux-gnueabihf.cmake`, the bench build's `flags.make`): GCC 12.2, `-march=armv7-a -mfpu=neon -mfloat-abi=hard -O3 -std=c++17`, no `-mcpu` / `-mtune`. Two consequences run through this review:

* GCC does not auto-vectorise float loops with NEON on 32 bit ARM unless `-funsafe-math-optimizations` is given, because NEON flushes denormals (GCC manual, "ARM Options", the `-mfpu` entry). Every plain float loop that is not written with intrinsics (the mix loops of `lib_osc.cpp`, the clamp loops, the Convolve FIR, the makeup meter) runs on the scalar VFP unit.
* `-std=c++17` (ISO mode) makes GCC's default `-ffp-contract=off`; the kernels' own `fp contract(off)` pragmas agree. The tuning is generic ARMv7; `-mcpu=cortex-a17` was never measured (finding FL1).

## 1. The loop nest (where everything below sits)

```
OwnEngine::process (own_engine.cpp:1265)                                   per host block (128)
  per control period, kControlPeriod = 32 (own_engine.cpp:1498..1535)      4 per block
    [period start] FxChain::update for each rack (own_engine.cpp:1442..1446)
        bypassed module: ProgramMeter::endPeriod + FxModule::makeup (fx_chain.cpp:419..438)
    [period start] Voice::tick (own_engine.cpp:1152)                       per voice
        LibOsc::update (voice.cpp:985 -> lib_osc.cpp:183)                   per voice x oscillator
           first period of a note: beginType -> SpectralVoice::noteOn / GranularVoice::noteOn / SampleVoice::noteOn
    Voice::render (own_engine.cpp:1531 -> voice.cpp:1116)                  per voice
        LibOsc::render (voice.cpp:1216 -> lib_osc.cpp:929)
          renderType (lib_osc.cpp:761)                                     per unison lane (U)
            SampleVoice::processModulated (sample_voice.cpp:595)            per sample (4 / 8 lane runs)
            GranularVoice::process (granular.cpp:702)                       per grain x per sample (8 lane runs)
            SpectralVoice::process (spectral.cpp:1312)                      per sample; every 512 samples a frame
               synthesizeHop -> renderChannelFast (spectral.cpp:920)        per frame x channel: shape, phases, sincos, IFFT, OLA
            LayerVoice::processAdd (multisample.cpp:764)                    per layer x per sample
          the mix into the slot, lerp of the unison gains (lib_osc.cpp:846..857, 871..883, 897..902)   per lane x per sample
    FX racks: FxChain::process; bypassed effect: runMakeup (fx_chain.cpp:633)   per sample
note on: handleEvent -> Voice::start (voice.cpp:158) -> LibOsc::resetForNote (voice.cpp:241 -> lib_osc.cpp:123 -> stopAll 107)
         -> Voice::tick at once (own_engine.cpp:805) -> beginType (lib_osc.cpp:577) -> noteOn
```

The named presets (from `content/presets/**`):

| preset | content type, osc 1 | notes x unison at tier low | engine of the content | content | other facts |
|---|---|---|---|---|---|
| Frozen Pluck Drone (FPD) | Granular, manual | 6 x 3 = 18 clouds | density 42 Hz, length 110 ms, max grains 48 (16 per cloud), Gaussian window 85 %, rand pan 55, rand offset 3 | pluck_string.wav, mono, 44.1 kHz, 110 250 frames | Convolve on (plate, 63 945 frames stereo), bypassed at low |
| Bowed MPE Strings (BMS) | Sample, Tailed loop, xfade 25 | 8 x 3 = 24 sample voices | 4 point sinc at low | bowed_tone.wav, mono, 138 915 frames | |
| Chord Cloud Pad (CCP) | Granular, manual | 6 x 2 = 12 clouds | density 26 Hz, length 180 ms, Hann 75 %, rand pan 70, rand offset 10 | chord_pad_sections.wav, mono, 352 800 frames | |
| Spectral Vowel Freeze (SVF) | Spectral, manual (position moved by an LFO) | 4 x 2 = 8 spectral voices | Phase Lock on, Add Harmonics 25 % (h = 2, 3), band 90 Hz .. 14 kHz smooth | vowel_pad.wav, mono, 180 810 frames (354 analysis frames, 4.35 MB) | |
| Gong Grain Swarm (GGS) | Granular, scanning (not manual) | 4 x 2 = 8 clouds | density 60 Hz, length 70 ms, window 5 (Tukey) 60 %, rand pitch 7 st | gong_metal.wav, mono, 176 400 frames | |
| also: Spectral Gong Pad (SGP), Spectral Chord Morph (SCM) | Spectral, scan 25 % ping pong / scan 100 % forward | 4 x 2 / 3 x 2 | SGP Phase Lock on; SCM Phase Lock off (fused path) | mono | |

Live grains (density x length): FPD 4.6 per cloud, 83 in all; CCP 4.7 per cloud, 56; GGS 4.2 per cloud, 34. Spectral frames in the steady state: one per voice and channel every 512 samples, so 8 voices make 2 frames per block (SVF, SGP), 6 voices 1.5 (SCM).

Measured shares (sections, Key 61): FPD granular 27.6 % of the block (0.80 ms, 18 clouds); BMS sample 9.5 % (0.275 ms, 24 voices); SVF spectral 15.7 % (0.455 ms, 2 frames per block, so about 228 us per frame in the engine against 155 us in micro: the rest is cache misses and per sample work). The strike block of SVF (A2 sections): note start (priming) 5.41 ms, library reset 1.11 ms, the rest about 1.9 ms: 8.88 ms in all.

## 2. What is already optimised (not reported again)

Sample: `renderBlockT` with region / loop bounds, table and plan once per call, NEON 4 / 8 lane plain read and loop crossfade (equal power weights in float lanes), sanitize in lanes, `pow` of the pitch cached (`sample_voice.cpp:634`). Granular: grain constants at the spawn (reciprocal window, slopes, mip), NEON window and Hermite of 8 samples, spawns found from the timer in manual mode, window tables built in `prepare`. Spectral: analysis once at load and shared, half length NEON real inverse transform, polynomial sine / cosine (NEON, two groups interleaved), the exact old wrap in float lanes, the fused advance + sincos + bins when there is no Phase Lock and no twist, cached band and comb gains, scalar `findPeaks` on arm32, NEON `placeRegion` for regions of 8 bins or more, deferred priming frames (1 to 2 per voice at the strike), start offsets 0 / 128 / 256 / 384 that spread the frames over the 4 blocks of a hop, one shaping shared by the frames of a voice's priming. Engine: `spectralGain_` power in `prepare`, Convolve bypassed at tier low (`fx_chain.cpp`, `test_own_convolve_bypass.cpp`), the Convolve loudness spectrum on the helper thread. Morph transform (Shepard Ascent) NEON.

## 3. Findings

Each finding: ID, place, what it does, loop level, why it is waste, output consequence, saving per named preset (two lines of arithmetic), confidence, change sketch. "Strike" means the block in which the notes start.

### Spectral: the strike

**SP1. Every note start memsets about 150 KB per library oscillator, also for Granular and Sample presets.**
* Place: `lib_osc.cpp:123..131` (`resetForNote` -> `stopAll`, 107..121; the spectral loop 114..115) -> `SpectralVoice::reset` `spectral.cpp:540..551`; `GranularVoice::reset` `granular.cpp:198..204`.
* What: once a library type has run on the slot (`playersUsed`), every note start resets all 4 `SpectralVoice` (kSpecU), all 16 `GranularVoice`, 16 `SampleVoice`, 64 `LayerVoice`. `SpectralVoice::reset` fills `synthPhase`, `smear`, `ring`, `finished` of both channels: (1025 + 1025 + 2048 + 512) x 4 B x 2 = 36.9 KB, x 4 = 147.5 KB. `GranularVoice::reset` writes `active` of 48 grains in 16 voices (768 stores over about 61 KB, one cache line each).
* Level: per note start x library oscillator slot (not per lane: all 4 spectral voices, even with U = 2 or a granular preset).
* Why waste: `noteOn` already clears what a note needs (`spectral.cpp:560..565`), `process` returns at once for an inactive voice (1316), `finished` is written before it is read. The comment at `lib_osc.cpp:125..126` ("a reset of an idle player is a no-op") is not true for these two classes. The fills also evict the L2 that the priming then needs (cold analysis frames, tables).
* Consequence: bit-identical.
* Saving (measured section "osc: library reset for a note", Key 61): SVF longest block 1.11 ms (other runs 1.00, 1.06, 0.65 ms); mean 0.0024 ms per block, and the same mean for FPD (0.0025) and BMS (0.0023): the granular and sample presets pay it too.
  * SVF strike: 0.65 to 1.11 ms / 2.902 ms = 22 to 38 points of the strike block.
  * FPD, BMS, CCP, GGS strike: 0.0025 ms x 344.5 blocks per s = 0.86 ms per retrigger of 8 notes: about 0.6 to 0.9 ms in the strike block (20 to 30 points). Median and p99: about 0.
* Confidence: high (measured section; the code path is plain).
* Change: `SpectralVoice::reset` sets `active_ = false`, `analysis_ = nullptr`, `phaseInit_` false and no more; `GranularVoice::reset` keeps a count of live grains and skips the loop when it is 0; `stopAll` resets only players with `isActive()` (or that were started since the last reset). Correct the comment.

**SP2. `startSchedule` computes up to 654 double `cos` per note start.**
* Place: `spectral.cpp:1020..1040` (the loop 1029..1037, `std::cos` in double at 1031), called from `noteOn` 614.
* What: for j = 1..3 it walks q from the offset to 512 until the squared Hann weight passes 0.5. Frame 2 passes at q = 140, frame 3 never does inside the hop, so the loop runs to 512. Counted per offset: 0 -> 1 + 141 + 512 = 654, 128 -> 398, 256 -> 258, 384 -> 130 calls; mean 360.
* Level: per note start x lane.
* Why waste: the answer depends only on (offset, smears). Closed form: frame 1 joins at the offset, frame 2 at max(offset, 140) rounded down to 128, frame 3 at 512. A table of the 4 offsets x 2 (or the 3 thresholds made once in `prepare`) is the same numbers.
* Consequence: bit-identical.
* Saving: SVF / SGP strike, 8 lanes, 2 per offset: 2 x (654 + 398 + 258 + 130) = 2880 double cos.
  * 2880 x 150..400 cycles = 0.43 .. 1.15 M cycles = 0.27 to 0.72 ms = 9 to 25 points of the strike block (est.).
  * The micro "8 x noteOn warm" is 1.743 ms in all, so the lower half of the range is the likelier one. SCM (6 lanes): 0.2 to 0.54 ms.
* Confidence: high for the count, medium for the cycles. Bench: `synthyboy_tier_bench --micro` line "8 x SpectralVoice::noteOn warm" before and after.
* Change: a `static constexpr` table or three thresholds computed in `SpectralVoice::prepare`; keep the loop as the test's reference.

**SP3. Three tables are built lazily on the audio thread at the first Spectral frame after loading.**
* Place: `kernels.cpp:69..73` (`RealIfft::instance`, constructor 44..67: 2 x 1024 + 2 x 1023 double sin / cos), `kernels.cpp:504..516` (`windowTable`: 2048 double cos), `spectral.cpp:993..1015` (`startGain`: 12 x 512 double cos). `SpectralVoice::prepare` (533..538) warms only `Fft` and `hannAt`.
* Level: once per process, in the strike block of the first Spectral note (and a guarded static initialisation, an atomic acquire, on every later call).
* Saving: (4094 + 2048 + 6144) double libm x 150..400 cycles = 1.8 .. 4.9 M cycles = 1.2 to 3.1 ms once (est.): 40 to 105 points of that one block.
* Consequence: bit-identical. Confidence: high. Change: touch the three in `SpectralVoice::prepare` (as `GranularVoice::prepare` does with `windows()`), and hoist the reference in callers (one `const auto& t = RealIfft::instance()` per frame is already the case).

**SP4. The shaping before the placing is repeated per lane although the lanes of a strike read the same frame.**
* Place: `shapeFrame` `spectral.cpp:697..853`; the shared `Scratch` is one per engine (`own_engine.cpp:139`, `lp.scratch`).
* What: lerp of two analysis frames, copies of frequency and phase, filter, band, warps, `maxBins`, `findPeaks` (716..818), then the per lane placing with the pitch ratio (803..850). In SVF (manual mode, one position for all notes at the strike, unison spans 0) the 8 lanes of a strike shape the same analysis position with the same settings; only `pitchRatio` (the placing) differs.
* Why waste: `mag_`, `freq_`, `theta_` and the peak list are the same; nothing between two lanes' frames overwrites `mag_ / freq_ / theta_` (the placing, the phases, the transform use other arrays of the Scratch).
* Consequence: bit-identical (the same function of the same inputs), if the key is complete: analysis pointer, channel, frame (the double), filter gain pointer and its generation, band settings, both warps (type, amount, var, external pointers), and never with Smear.
* Saving: SVF strike, 8 shapings of about 67 us (micro, with Add Harmonics): 1 full + 7 placings of about 15 us.
  * 7 x (67 - 15) = 364 us = 12.5 points of the strike block (est. from micro numbers).
  * Steady state: only when two lanes make a frame at the same position. With the LFO moving the position and the lanes on different quarters, probably rare: only a measurement decides (`--sections`, SVF, count of cache hits).
* Confidence: medium (hit rate). Risk: medium (key completeness). Change: keep `peaks[]`, `numPeaks`, `extra` and the key in `Scratch`; at entry of `shapeFrame (fast)` compare the key and jump to the placing.

**SP5. With Phase Lock, about half of the advanced and wrapped phases are overwritten at once by the lock.**
* Place: `renderChannelFast` `spectral.cpp:945..968` (advance at 946, lock 949..968); the same in `startPhases` 1050..1097.
* What: `advancePhases` advances and wraps all 1025 bins (micro: 33 us per call), then the lock overwrites bins 1 .. first peak - 1, peak + 1 .. (peak + next) / 2 for each pair, and last peak + 1 .. 1024.
* Why waste: those bins' advanced values are never read. Only bin 0, the peaks and the left halves ((p + j) / 2 + 1 .. j - 1) keep theirs. The lock's peaks depend on `om_` only, so they can be found first.
* Consequence: bit-identical (the same values on the bins that survive).
* Saving: about half of 33 us = 14 to 20 us per advance (est.).
  * Steady SVF / SGP: 2 frames per block x 14..20 us = 28..40 us = 1.0 to 1.4 points.
  * Strike SVF / SGP: advances per lane by offset 1 / 2 / 2 / 2, so 14 for 8 lanes x 14..20 us = 0.20 to 0.28 ms = 7 to 10 points. SCM: 0 (no lock).
* Confidence: medium high. Risk: low to medium (the lock's region rules must be copied exactly). Change: one scalar pass that finds the lock's peaks into a run list, `advancePhases` on the surviving runs (NEON for runs of 8 or more), then the fills of the lock (which can be NEON per region).
* Observation, not a CPU item: the lock leaves the left half of every region after the first unlocked (`spectral.cpp:956..963`: a new peak j fills only from the previous peak to the midpoint). Phase locking by regions of influence usually locks both sides. If the owner decides that is a defect, the fix changes the sound; then only the peaks need an advance and SP5 grows to about 30 us per advance.

**SP6. Each priming frame goes through an 8 KB temporary.**
* Place: `startFrames` `spectral.cpp:1170..1173`: `memset (olaTmp, 8 KB)`, `overlapAdd` into it, then a scalar loop of 1024 to 2048 adds into the ring.
* Level: per priming frame (1 to 3 per lane at the start).
* Why waste: an `overlapAdd` that starts at n0 = (3 - j) x 512 (a multiple of 8, so the NEON path applies) can add into the ring directly.
* Consequence: bit-identical except the sign of a zero (0 + p then ring + that, against ring + p).
* Saving: memset 2.3 us + scalar add loop about 2 to 5 us + half of the OLA of the dropped part 1 to 2 us = 5 to 8 us per frame (est.).
  * SVF strike 12 frames: 60 to 96 us = 2 to 3.3 points. Steady: 0.
* Confidence: high. Change: `overlapAdd (ring, at, zr, zi, scale, neon, firstSample)`.

**SP7. Add Harmonics divides per bin and copies twice; Add Subharmonics copies twice.**
* Place: `applyWarp` `spectral.cpp:305..321` (`a * m[k] / h` at 312), copies 263..272.
* What: for h = 2 and 3 (SVF: var 0.5 -> n = 2): 512 + 341 = 853 iterations with a float divide, plus `copyOut` and `copyBack` (4 x 4 KB).
* Level: per shaping (per frame x channel, and once per lane at the strike).
* Why waste: `x / 2` equals `x * 0.5` exactly; for h = 3 a multiply by the reciprocal differs by rounding. `copyBack` can be a pointer swap.
* Consequence: bit-identical for h = 2 and 4 and the pointer swap (SP7a); rounding-level for h = 3 (SP7b).
* Saving: micro "noteOn + Add Harmonics" 199.5 against 183.2 us: the warp costs about 16 us per shaping. SP7a: 512 divides x 14..20 cycles + 1.8 us copy = 6 to 8 us; SP7b: 10 to 12 us.
  * SVF steady: 2 shapings per block x 6..12 us = 12..24 us = 0.4 to 0.8 points.
  * SVF strike: 8 x 6..12 us = 48..96 us = 1.7 to 3.3 points (1 shaping only after SP4).
* Confidence: high. Change: h outer loop with a constant, then pointer swap of `m / tm`.

**SP8. `findPeaks` on arm32 is a compare and branch per bin.**
* Place: `kernels.cpp:639..642` (scalar form used on arm32, `kernels.h:29..33`); called from `shapeFrame` 818.
* Why waste: a recorded spectrum has a peak every third bin or so: the branch mispredicts often (the vector form lost on the A17 because of NEON to core moves). A branchless scalar loop (`peaks[count] = k; count += cond;` with integer compares of the non negative float bits) has neither.
* Consequence: bit-identical. Saving: micro 10.8 us per shaping; est. 4 to 6 us less.
  * SVF / SGP steady: 2 x 4..6 us = 0.3 to 0.4 points. Strike: 8 x 4..6 = 1.1 to 1.7 points (less after SP4).
* Confidence: medium (only `--micro` "findPeaks scalar" decides). Change: as described, keep the old loop as reference.

**SP9. `shapeFrame` makes 3 to 5 passes over 4 KB arrays that can be fused.**
* Place: `spectral.cpp:716..718` (lerp, two memcpy), 782..791 (filter, band), 806..808 (three fills).
* What: lerp into `mag_`, then `mulBins` by the filter, then by the band: three passes over `mag_`; `freq_` and `theta_` are copies of analysis arrays that only a warp that writes frequencies needs.
* Consequence: bit-identical (per bin the same operations in the same order). Saving: est. 3 to 5 us per shaping (two 4 KB passes, two 4 KB copies when no warp writes `freq_`).
  * SVF steady 2 x 3..5 = 0.2 to 0.35 points; strike 8 x 3..5 = 0.8 to 1.4 points.
* Confidence: medium. Change: one NEON pass `mag = ((m0 + (m1 - m0) t) * filter) * band`; `freq_` / `theta_` as pointers into the analysis unless a warp of the frequency group runs.

### Spectral: the steady state

**SP10. Per sample scalar loops around the kernels.**
* Place: `SpectralVoice::process` `spectral.cpp:1319..1354` (two flags, gain, `isfinite`, clamp per sample and channel); `synthesizeHop` 1297..1306 (copy and clear 512 per channel per hop); the lane mix in `lib_osc.cpp:897..902` (two lerps and two MACs per sample); the same mix for Granular (871..883) and Sample (846..857); `GranularVoice::process` clamp 756..760.
* Level: per lane x per sample.
* Why waste: GCC does not vectorise these float loops on arm32 (see the flags above). `simd::rampLerp` and the `sanitize4` pattern exist.
* Consequence: bit-identical (lane by lane the same operations; NaN and infinity masked to 0 as now).
* Saving (est., scalar about 15 cycles per sample in `process`, 8 in the mix, 6 in the clamp):
  * SVF: 8 lanes x 128 x (15 + 8) cycles = 23.6 k cycles = 15 us; NEON leaves about 4 us: 0.35 to 0.4 points.
  * FPD: 18 clouds x 128 x (6 x 2 + 8) cycles = 46 k cycles = 29 us; saving 20 to 23 us = 0.7 to 0.8 points. CCP 0.5, GGS 0.35, BMS (mix only, 24 lanes x 128 x 8 = 24.6 k cycles = 15 us) 0.35 to 0.4 points.
* Confidence: high. Change: runs up to the next hop boundary or start event; NEON gain, clamp, mask; `std::memcpy` / `std::memset` in `synthesizeHop`.

**SP13. The exact phase wrap costs 33 us per frame; a plain reduction is about four times cheaper.**
* Place: `wrapN` `kernels.cpp:281..304` used by `advancePhases` 415..449 and `advanceToSpectrum` 451..498.
* What: the error compensated three part reduction that reproduces the old `fmodf` bit for bit (about 38 dependent Q operations per 4 bins). Micro: `advancePhases` 33.0 us per 1025 bins (52 cycles a bin).
* Why waste: the wrap acts on `phase + step x 512`, a sum that is already rounded: at |x| > 100 its spacing is at least 7.6e-6 rad, at 5e4 it is 3.9e-3 rad. The compensated remainder keeps bits that the sum does not hold. A reduction `x - n x 2 pi` with a two part constant (about 8 Q operations) is exact to about 1e-7 rad.
* Consequence: rounding-level (the phase differs by an ulp of the wrapped value per hop; the outputs differ from `--scalar-content` at the level the content kernel comparisons already accept, to be confirmed with `content-kernel-compare.py --tolerance 3e-5`).
* Saving: 33 us -> est. 8 to 13 us: 20 to 25 us per advance.
  * SVF / SGP steady: 2 x 20..25 us = 1.4 to 1.7 points (with SP5: both together about 28 us per advance, 1.9 points). SCM: 1.5 x 20..25 = 1.0 to 1.3 points.
  * Strike SVF: 14 advances x 20..25 us = 0.28 to 0.35 ms = 10 to 12 points.
* Confidence: medium high. Risk: medium (the "exact old wrap" was a requirement of round 10; the owner decides if rounding-level is enough here).

**SP14. The real inverse transform: radix 2 squared with 4 complex multiplies per 4 points, a scalar bit reversal, no multiply-accumulate.**
* Place: `RealIfft::transform` `kernels.cpp:75..175` (bit reversal 78..83, the stage pairs 120..174), `inverse` 177..208.
* What: 1024 point complex transform for the 2048 real samples: two stages fused (twiddles 1 and i), then 4 stage pairs with 4 complex multiplies per butterfly of 4 points; 496 scalar swaps of two arrays. Micro: 33.8 us (scalar form 51.3 us, the full complex reference 138 us).
* Why waste: a radix 4 butterfly needs 3 complex multiplies (the k = 0 column none): 25 % of the twiddle multiplies go. The bit reversal (about 2000 scalar loads and stores, est. 2 to 3 us) disappears with an out of place Stockham ordering (the pairs `re / im` and `zr / zi` are both free at that point). ARMv7 `vmlaq_f32` is not fused (the product is rounded first), so it gives the bits of `vmul` + `vadd` with one instruction; whether the A17 runs it faster is a measurement.
* Data set: 8 KB data + 8 KB stage twiddles + 8 KB pack twiddles = 24 KB, inside the 32 KB L1D but with no room for the frame's other arrays; a quarter wave twiddle table would halve the tables at the cost of index work (not recommended before a measurement).
* Published alternatives (no web search was made; no timings are claimed): split radix has the lowest operation count for powers of two but an irregular structure that suits NEON badly; radix 4 (or radix 2 squared with 3 multiplies) with a Stockham ordering is what the open NEON libraries use (Arm's Ne10, BSD 3 clause; pffft, BSD style, SIMD radix 4 with real transforms; FFTS). KissFFT is already in the tree (`core/engine-vital/upstream/third_party/kissfft`) and is scalar: not faster.
* Consequence: rounding-level (another order of operations).
* Saving: est. 33.8 -> 20 to 24 us: 10 to 14 us per frame.
  * SVF / SGP steady 2 x 10..14 = 0.7 to 1.0 points; SCM 0.5 to 0.7.
  * Strike SVF 12 frames: 0.12 to 0.17 ms = 4 to 6 points.
* Confidence: medium. Risk: medium (a new transform with its scalar twin and the bit test). Bench: `--micro` "RealIfft::inverse".

**SP15. Smaller items of the frame.**
* `placeAll` (`kernels.cpp:678..713`): on a recorded spectrum the regions are about 3 bins, below the NEON threshold of 8: scalar with a double `target` per region. Adjacent regions with the same offset (all regions below 0.5 / |r - 1| bins, i.e. notes near the root and unison detune) can be merged into one NEON `placeRegion` call: bit-identical (disjoint targets, order kept). Saving 0 to 5 us per shaping depending on the pitch (est.).
* `noteOn` clears `synthPhase` (`spectral.cpp:564`) which the first frame always writes, and `smear` (562) which only Smear reads: 2 x 1.17 us per lane (micro memset 4 KB). SVF strike: 8 x 2.3 = 19 us = 0.6 points. Bit-identical.
* `sincosPoly` degree 7 / 8 (2e-7): a degree 5 / 6 form (about 1e-5, -100 dB) saves about 4 of 17.5 us per frame (est.). Rounding-level only in a loose sense; low value.
* Prefetch: a voice knows one block ahead that it will make a frame; `__builtin_prefetch` over its next analysis frame (16 KB) and its ring would hide part of the 228 vs 155 us gap (engine vs micro per frame). Bit-identical; saving only a measurement decides (`--sections`, "osc: sample / granular / spectral" on SVF).

### Spectral: patches that no factory preset uses (user patches)

**SP11. Mask and Vocode warps make a full scalar 2048 point FFT and 2048 `cosf` per control period per lane.**
* Place: `lib_osc.cpp:484..504`. The condition `ringDirty || e >= 0` (488) is always true; the Hann weight is `std::cos` per sample (495); `spectral::Fft::forward` is the scalar radix 2 complex transform (`spectral.cpp:80..112`).
* Level: per control period x voice (4 per block), although the voice reads `external` only when it makes a frame (once per 512 samples).
* Cost: 2048 x 80..200 cycles (cosf) + about 131 us (micro: the same transform inverse) + 1025 sqrt = 0.24 to 0.39 ms per call (est.); 4 calls x 8 lanes = 7.7 to 12.5 ms per block: 265 to 430 points of the block.
* Change: compute only in a period in which a frame of this voice is due (bit-identical: the frame reads the magnitudes of that period's update), the Hann weights from a table made in `prepare` with the same float expression (bit-identical), a real NEON forward transform (rounding-level). Then about 30 to 40 us per frame: 1 to 3 points. Confidence: high. No factory preset uses warps 93..98 (checked: only 76, 77, 78, 90 occur).

**SP12. Detune and Shepard warps call libm per bin per frame.**
* Place: `spectral.cpp:358..367` (`sin`, `floor`, `exp2` per bin), 368..380 (`log2`, `round`, `exp` per bin).
* Cost: about 3 x 1024 single libm calls per shaping = 0.25 to 0.6 M cycles = 150 to 380 us (est.); 2 shapings per block in an 8 voice patch: 10 to 26 points.
* Change: the hash `s(k)` and `log2 (k)` are constants (a table at init); the gains depend on (a, v): cache them like the comb (`combGain`). Bit-identical. No factory preset uses warps 79..81.

### Granular

**GR1. A scanning cloud computes two double divisions per sample.**
* Place: `GranularVoice::process` `granular.cpp:733..748` calls `advanceHead` (331..372) per sample; `fileSeconds` and `step` (335..336) are double divisions of values that are constant in the call.
* Level: per cloud x per sample (scanning mode: GGS, Rising Grain Riser, Stutter Grain Kit).
* Consequence: bit-identical (the same quotients, computed once).
* Saving: 2 x 20..30 cycles + about 15 for the rest = 55 to 75 cycles per sample (est.); GGS 8 clouds x 128 x 55..75 = 56..77 k cycles = 35..48 us; keeping the per sample add and test (about 8 cycles) saves 30 to 42 us = 1.0 to 1.4 points for GGS. FPD, CCP: 0 (manual).
* Confidence: high. Change: hoist `fileSeconds`, `step`, `l0`, `l1`, `looping` before the loop; the per sample `head_ += step` stays (so the spawns see the same head).

**GR3. The vector grain kernel spends most of its time on positions and lane moves, not on the read.**
* Place: `renderGrains` vector path `granular.cpp:591..649`: 7 dependent double adds for 8 positions (596..599), per lane double -> int, int -> double, double subtract, double -> float and a lane insert (607..621), indices stored and reloaded; `windowKernelN` (471..508) moves 8 indices NEON -> core and gathers 16 table values lane by lane; `hermiteN` (511..536) loads 8 vectors from the indices.
* Measured: micro "GranularVoice::process 128 (14 grains) vector kernel" 84 us: 84 x 1608 / (14 x 128) = 75 cycles per grain sample. In the engine FPD: 0.80 ms / (83 grains x 128) = 121 cycles per grain sample with the mix and the spawns.
* Inherent: Hermite of 4 taps (about 10 flops), window lerp (about 4), two MACs: about 10 to 15 cycles per grain sample with NEON (est.).
* Change: per grain and block, one double step: `base = floor (pos)`, `f0 = pos - base` (float); lanes `t = f0 + k x inc` from an iota vector (float, exact enough inside one block: offsets below 128 x |inc|), `idx = base + (int) t`, `frac = t - floor (t)` all in NEON; re-anchor every block from the double. Same for the window age (the age is already an integer).
* Consequence: rounding-level (the read position differs by about 1e-6 frames inside a block; nothing accumulates since the double is kept per grain).
* Saving: est. 20 to 33 % of the kernel (positions about 15 to 25 of 75 cycles).
  * FPD: kernel about 85 % of 0.80 ms = 0.68 ms; x 0.20..0.33 = 0.14..0.22 ms = 4.7 to 7.7 points.
  * CCP (56 grains, section est. 0.80 x 56 / 83 = 0.54 ms, kernel 0.46 ms): 3.2 to 5.2 points. GGS (34 grains, kernel est. 0.28 ms): 1.9 to 3.2 points.
* Confidence: medium (the split of the 75 cycles is an estimate). Risk: medium. Bench: `--micro` "GranularVoice::process 128 (14 grains) vector kernel".

**GR2. The window is looked up per grain sample with lane gathers although all grains of a cloud share it.**
* Place: `windowKernelN` `granular.cpp:471..508` (8 lane extracts, 16 single lane loads per 8 samples).
* Why waste: with `rand_window`, `rand_skew`, `rand_length` at 0 (FPD, CCP, GGS) every grain of a cloud has the same window curve (shape, amount, skew, length); only the age differs. A curve of `length` samples made once with `windowKernel` (the same bits) turns the per sample window into one contiguous `vld1q` per 4 samples.
* Cost of the table: FPD 110 ms = 4851 floats (19 KB), about 4851 x 20 cycles = 60 us per build; it must be shared by all clouds with the same key (one per oscillator in a `Scratch` like area), rebuilt only when a key changes, and skipped when the length is modulated per period (fall back to the current path).
* Consequence: bit-identical. Saving: est. 10 to 15 % of the kernel.
  * FPD 0.68 ms x 0.10..0.15 = 68..102 us = 2.3 to 3.5 points; CCP 1.6 to 2.4; GGS 1.0 to 1.4.
* Confidence: medium low. Risk: medium (cache key, memory traffic of 19 KB per curve).

**GR4. Per period work of a cloud.** `lib_osc.cpp:867..868`: a copy of `GrainParams` and `semitonesToRatio` (`exp2f`) per cloud per period: FPD 72 calls per block x 80..200 cycles = 6..14 k cycles = 0.1 to 0.3 points. Cache the ratio per lane by its input (bit-identical). Grouped with LU1 below.

### Sample

**SA1. Per call libm in the sample kernel.**
* Place: `renderBlockT` `sample_voice.cpp:914` (`makePlan` calls `std::log2` at 241 when the ratio is above 1), 921..924 (`std::cos`, `std::sin` of the pan).
* Level: per lane x per control period (4 per block).
* Measured bound: micro "4 x 32" 7.00 us against "128" 5.83 us: 1.17 us per lane and block is the cost of 3 extra calls (0.39 us = 630 cycles per call), most of it these 2 to 3 libm calls.
* Consequence: bit-identical (cache the results by their inputs: pan + unison pan, pitch ratio).
* Saving: BMS 24 lanes x 4 calls x 2..3 libm x 80..200 cycles = 15..58 k cycles; bounded by the measured 24 x 1.17 = 28 us: 0.6 to 0.8 points.
* Confidence: medium high. Change: keep `lastTheta / gainL / gainR` and `lastRatio / plan` in the voice.

**SA2. The sample kernel's position path (doubles and conversions per sample).**
* Place: `sample_voice.cpp:1039..1052`: per sample a dependent double add, double multiply, two double -> int conversions, an int -> double and a subtract; indices stored and reloaded by `interpN`.
* Measured: micro "SampleVoice::process 128 vector kernel" 5.83 us = 73 cycles per sample for a 4 tap mono read. Inherent: 4 MACs and a gain, about 6 to 10 cycles per sample with NEON (est.).
* Change: the same block anchored float lane positions as GR3; the phase index `ph = (int) (frac x kPhases)` in lanes.
* Consequence: rounding-level (the phase index can change at a boundary where the double and the float fraction straddle it). Saving est. 35 to 50 % of the kernel.
  * BMS: 24 x 5.83 us = 140 us; x 0.35..0.5 = 49..70 us = 1.7 to 2.4 points.
* Confidence: medium. Bench: `--micro` "SampleVoice::process 128 vector kernel".

### Library oscillator update

**LU1. `LibOsc::update` calls 10 to 16 libm functions per lane group per control period whose inputs rarely change.**
* Place: `lib_osc.cpp:302..303` (`panLaw` x 2: 4 `sinf`), 332 (`powerShape`: 2 `expm1f` per lane), 336 (`panLaw` per lane: 2 `sinf`), plus `semitonesToRatio` per lane in `renderType` (868, 894).
* Level: per voice x library oscillator x control period.
* Consequence: bit-identical (cache by the input values; recompute when any changes).
* Saving (est.): FPD, U = 3: 4 + 3 x 4 = 16 calls x 80..200 cycles = 1.3..3.2 k cycles x 24 per block = 31..77 k cycles = 0.7 to 1.6 points. BMS (U = 3, 32 per block): 0.9 to 2.2. SVF (U = 2, 16 per block): 0.4 to 0.9. CCP: 0.5 to 1.2. GGS: 0.3 to 0.8.
* Confidence: medium (section "voice tick: oscillator update" is 2.9 % for FPD, 6.1 % for BMS, 1.7 % for SVF: these include the wavetable oscillators). Risk: low.

### Multisample

**MS1. Per sample envelope and scalar mix in every layer.**
* Place: `LayerVoice::processAdd` `multisample.cpp:784..827`: a `switch` per sample, double divisions in attack, decay and release (798, 810, 817), scalar MAC per channel.
* The factory instruments use no attack or decay (`ampeg_attack=0.0`, sustain): the layers sit in `sustain` with a constant level. NEON for a run in `sustain` (constant `g`) is bit-identical; a reciprocal per stage instead of the divisions is rounding-level.
* Saving: Glass Zone Bells, Marimba Layer Keys (8 voices x 2 lanes x 1 to 2 layers = 16 to 32 layers) x 128 x about 12 cycles = 25..49 k cycles = 0.5 to 1.0 points; NEON keeps about a third: saving 0.35 to 0.7 points (est.). Not one of the five named presets.

### Effects: Convolve and its makeup at tier low

**MK1. The bypassed Convolve still costs about 1 to 3 points per block (the loudness makeup).**
* Place: `fx_chain.cpp:419..438` -> `ProgramMeter::endPeriod` `fx_makeup.cpp:153..179` (4 to 5 double `exp` of constants, 162, 165, 170), `FxModule::makeup` `fx_module.h:103..110` (`dbToGain`, `sqrt`), `ConvolveModule::makeupPower` `fx_modules.cpp:809..859` (`makeKey` 892..913: 2 `powf`, 5 `roundf`), `weightedMean` `fx_makeup.cpp:470..491` called twice (power and coherent): per call 54 double divisions by `inBand`, `meterBandOf (bandFreq (b))` twice per band, and 162 calls of `table()` (a function local static: on ARMv7 each access checks the guard with an acquire load and a barrier); per sample `ProgramMeter::accumulate` (131..151) and `runMakeup` (633..645).
* Level: per rack x control period; the meter per sample.
* Measured: variants FPD `fx_convolve_off` median 65.85 -> 62.51 (3.3 points), p99 87.70 -> 83.82 (one pass each; two identical configurations of the same run differ by 0.2).
* Why waste: `inBand` is a constant of the table; the `exp` constants depend on (period, sample rate) only; `makeKey` and the band powers depend on parameter values that do not change between periods; `table()` can be bound once per call.
* Consequence: bit-identical (the same expressions, computed once).
* Saving: est. by operations 0.6 to 1.0 points; the measured 3.3 points is the bound (the rest may be the guard barriers or cache effects; `--profile` decides). FPD 1 to 3.3 points; the same for every preset with Convolve on (Glass Zone Bells, Marimba Layer Keys, Dusty Rhodes Keys, Vowel Scrub Choir, Glass Ping Pluck, Breakbeat Slice Keys, Reverse Gong Swell, Reese Dual Phase Bass). SVF, BMS, CCP, GGS: 0.
* Confidence: medium. Risk: low.

**CV1 to CV4. Convolve itself (runs at tier medium and high, i.e. the XL; 0 on the Key 61 while it is bypassed).**
* Scheme (`convolve.h:13..30`, `convolve.cpp:478..527`, 586..673): zero latency, a 128 tap direct FIR head per sample (603..607), then uniform partitions of 128 with a 256 point complex FFT (`fft.cpp`, scalar radix 2 on an array of `{r, i}` structs, twiddles by stride). No helper thread on the audio path; the helper thread (`FxWorker`) only rebuilds the partitions and the loudness spectrum.
* FPD's plate: 63 945 frames, size 50 % -> factor 1.0, so (63 945 - 128) / 128 = 499 tail partitions at 44.1 kHz. Per 128 samples: 2 x 499 x 256 complex MACs = 255 k (2 M flops) + 4 FFTs of 256 + the head 2 x 128 x 128 = 32.8 k MACs. The head is about 3 % of the arithmetic: the earlier review's focus on the FIR missed the tail.
* Memory: spectra and history 499 x 256 x 8 B x 2 channels x 2 = 4.1 MB read per 128 samples: 1.4 GB/s of streaming. The tail is memory bound before it is arithmetic bound.
* CV1 Hermitian half: a real signal needs bins 0..128 only: half the MACs and half the memory (rounding-level: the computed upper bins are not bit for bit the conjugates).
* CV2 split real / imaginary arrays with NEON complex MAC (4 bins per Q operation, `vmlaq_f32` is not fused on ARMv7): about 4 times fewer instructions in the MAC (rounding-level if the summation order over partitions is kept: bit-identical to a scalar split form).
* CV3 head FIR in NEON with 4 output samples per vector (each lane sums the 128 taps in the old order): bit-identical; 4 to 6 times faster head (est.), a small part of the whole.
* CV4 non-uniform partitions (128 x 3, 512 x 3, 2048 x 3, 8192 x 7 for 64 k taps, an example): about 570 flops per sample and channel against about 8000 now (est. from 10 log2 (2B) per FFT pair plus 8 x partitions per sample); the large partitions must be computed spread over many blocks or on the helper thread with a deadline (published as "Efficient convolution without input-output delay", J. Audio Eng. Soc., 1995, and "Optimal filter partition for efficient convolution with short input/output delay", AES Convention 113, 2002). Risk high (timing, determinism of the offline render).
* For the Key 61: written as it is, Convolve at tier low would cost est. 255 k complex MACs x 6..8 cycles = 1.5..2 M cycles = 33 to 44 points plus the memory stalls; CV1 + CV2 bring that to est. 5 to 8 points, CV4 further. Today: 0.

### Content store and analysis at load

**CS1. The spectral analysis is built for every sample of oscillators 1..3, whatever their type; onsets for every sample.**
* Place: `content_set.cpp:113..117` (`dst.analysis.build`), 111 (`detectOnsets (256)`); `Analysis::build` `spectral.cpp:119..198`: per frame a scalar 2048 point complex FFT, 1025 `sqrtf`, 1025 `atan2f`.
* Memory of the unused analysis (3 x 1025 x 4 B = 12.3 KB per frame, frames = samples / 512 + 1): FPD 216 frames = 2.66 MB; BMS 272 = 3.35 MB; CCP 690 = 8.49 MB; GGS 345 = 4.24 MB. SVF uses its 4.35 MB.
* Load time on the Key 61 (est. per frame: FFT about 131 us from micro, `atan2f` 1025 x 80..200 cycles = 51..128 us, sqrt 9 us: 190..270 us): FPD 41..58 ms, BMS 52..73 ms, CCP 131..186 ms, GGS 66..93 ms, off the audio thread.
* Consequence: audio thread none. A change: build the analysis only when the state's type of that oscillator is Spectral; on a later type change build it on the helper thread (the slot is silent until it is there, est. 0.1 to 0.2 s on the Key 61), or keep it eager and accept the memory. Onsets only when slice mode Auto is set (same pattern). Not audio CPU; ranked low here.
* Also memory: every `LibOsc` holds 4 `SpectralVoice` of 36.9 KB (both channels even for mono): 3 slots x 8 voices x 147.5 KB = 3.5 MB at tier low, 14 MB at 32 voices. Allocating them lazily (or one channel for mono) is a memory item, not CPU.

### FFT implementations in the tree (inventory)

| code | where it runs | form | note |
|---|---|---|---|
| `spectral::kernels::RealIfft` (`kernels.cpp:44..208`) | audio thread, every Spectral frame | 1024 complex (2048 real), radix 2 squared, NEON, packed per stage twiddles, scalar bit reversal swap list | SP14 |
| `spectral::Fft` (`spectral.cpp:55..115`) | load (analysis), display, reference path, and the audio thread in SP11 | 2048 complex, scalar radix 2, strided twiddles, full complex input for real data | SP11, CS1 |
| `fx::Radix2FFT` (`fft.cpp`) | audio thread for Convolve (medium / high), helper thread (partitions, minimum phase, loudness spectrum at 64 k and more) | scalar radix 2 on `{r, i}` structs, real input as complex | CV1, CV2 |
| `morph.cpp:62` inverseFft | audio thread, Shepard morph | NEON, round 10 | done |
| `wavetable-edit/src/fft.cpp`, `wavetable_bank.cpp:73` | import / table build, double precision | scalar | off audio thread |
| `shell-mpc/spectrum_fft.h`, overlay curve | display thread, 2048 / 4096 | radix 2 | off audio thread |
| Vital's `fourier_transform.h` / kissfft | Vital engine | | not in this scope |

## 4. Spectral: what keeps the strike from fitting one block on the Key 61

Measured (A2 sections, SVF, 4 notes x 2 lanes): longest block 8.88 ms = 306 %: priming 5.41 ms, library reset 1.11 ms, the regular work about 1.9 ms (with the section clocks). Micro: 8 x `noteOn` warm 1.743 ms, after emptying the caches 2.074 ms. The engine's priming (5.41 ms, mean 0.0206 ms per block = 7.1 ms per retrigger) is 2.6 to 4 times the micro. Two explanations fit and only a measurement separates them: the bench plays 8 notes (`--voices` default is the tier's 8, the header says "notes 8 voices 4.0") on a polyphony 4 preset, so voices may be primed and stolen in the same block (16 noteOns instead of 8), and the 590 KB of reset fills (SP1) leave the caches cold. Bench: `synthyboy_tier_bench --preset "Spectral Vowel Freeze" --tier low --sections --voices 4` against the default, and a count of `noteOn` calls per strike.

Per 8 lane strike (warm, micro based), the sound-neutral levers:

| lever | saving in the strike block | output |
|---|---|---|
| SP1 reset fills | 0.65 to 1.11 ms (measured section) | bit-identical |
| SP2 schedule table | 0.27 to 0.72 ms | bit-identical |
| SP4 one shaping for lanes at the same position | about 0.36 ms (when the key matches) | bit-identical |
| SP5 advance only unlocked bins | 0.20 to 0.28 ms | bit-identical |
| SP13 plain wrap (with SP5 about 0.39 ms together) | 0.28 to 0.35 ms | rounding-level |
| SP14 transform | 0.12 to 0.17 ms | rounding-level |
| SP6 direct priming OLA | 0.06 to 0.10 ms | bit-identical |
| SP7 Add Harmonics | 0.05 to 0.10 ms (less after SP4) | bit-identical / rounding-level |
| SP8, SP9, SP15 | about 0.05 to 0.15 ms | bit-identical |
| SP3 static tables (first note only) | 1.2 to 3.1 ms once | bit-identical |

Warm work for 8 lanes after these: est. 0.6 to 0.9 ms (from 1.74), the reset gone: with the regular load of about 0.9 ms the strike block lands near 1.5 to 1.8 ms (52 to 62 %), if the engine's extra factor is the stealing or the cold caches that SP1 causes. Cold misses that stay: the analysis frame of the start position (16 KB per distinct position, shared by lanes at the same position), each lane's ring (8 KB), the 40 KB `Scratch`, 24 KB of transform tables: about 0.2 to 0.5 ms (est.) on a cold L2.

Design levers that change the sound (after the neutral ones, the owner decides):
* D1: one priming frame per lane in the strike block (offsets 128 and 256 make frames 1 and 2 now): the schedule's weight limit (`kMaxMissingWeight`, `spectral.cpp:1018`) raised for the first 128 samples. Saves 4 of 12 frames: est. 0.4 ms. Sound: the first 128 to 256 samples are renormalised from one frame: partials stay within the measured -57 to -66 dB of the four frame start for harmonic tones; noise starts up to about 1.5 dB lower for 3 to 6 ms (section 4a numbers). Probably inaudible under an attack; needs a listening check.
* D2: start half the lanes one block later (128 samples, 2.9 ms): the strike work splits over two blocks. Sound: a 2.9 ms onset spread between unison lanes or chord notes; a slight softening of the attack transient.
* D3: hop 1024 at tier low (50 % overlap, a synthesis window that sums to 1 at that hop): half the frames, half the steady cost and half the priming. Sound: audible phase vocoder artefacts (more phasiness and amplitude ripple on pitched material).
* D4: FFT 1024 at tier low (a second analysis at load): about half the work per frame and per priming. Sound: the bin is 43 Hz instead of 21.5 Hz; partials of low notes merge, the peak regions get coarser. Audible on low and dense material.

## 5. Inherent cost and waste per content type (Key 61, tier low)

| type | inherent (est.) | now | where the rest goes |
|---|---|---|---|
| Sample | 4 tap read + gain: 6 to 10 cycles per lane sample, 0.5 to 0.8 us per lane and block | 5.83 us per lane and block (micro), 7.0 with 4 calls | double positions and conversions per sample (SA2), per call libm (SA1), scalar mix (SP10) |
| Granular | Hermite 4 taps + window + 2 MACs: 10 to 15 cycles per grain sample, about 1 us per grain and block | 75 cycles per grain sample (micro), 121 in the engine with spawns and mix | double positions and lane moves (GR3), gathered window (GR2), divisions per sample in scanning mode (GR1), clamp and mix (SP10) |
| Spectral | per frame: transform 15 to 20 us, sincos 12 to 17, OLA 4.7, advance 4 to 8, shaping 20 to 25: 60 to 75 us | 89 us synthesis + 67 us shaping (micro), about 228 us per frame in the engine | exact wrap (SP13), lock overwrite (SP5), transform form (SP14), warp divisions (SP7), passes (SP9), cache misses (SP15); at the strike: reset fills (SP1), schedule cos (SP2), repeated shaping (SP4) |
| Multisample | as Sample + envelope 2 cycles per sample | as Sample + about 12 cycles per layer sample | per sample switch and scalar mix (MS1) |
| Convolve (medium / high) | non-uniform partitions: about 570 flops per sample and channel | about 8000 flops per sample and channel, 1.4 GB/s streamed | full complex bins (CV1), scalar AoS MAC (CV2), uniform 128 partitions (CV4) |
| Convolve bypassed (low) | a gain per period and a cheap meter: under 0.3 points | 1 to 3.3 points (measured bound 3.3) | MK1 |
| Content store | sample + mips; analysis only for Spectral | analysis for every sample of osc 1..3 | CS1 |

## 6. The earlier review (agy-g70), verdicts

* "Convolve head FIR is the primary improvement, kPartitionSize typically 512+, 5 to 10 % of the budget, bit-identical, certain": partly confirmed. The FIR is scalar (`convolve.cpp:603..607`) because GCC does not vectorise float loops on arm32; but the partition is 128 (`convolve.h:34`), the module is bypassed on the Key 61 (0 %), and on medium / high the head is about 3 % of the module: the tail (255 k complex MACs and 4.1 MB per 128 samples for the plate) is the cost (CV1, CV2, CV4). Bit-identical only when vectorised across output samples (CV3), not across taps.
* "Content set builds analysis and onsets for every sample regardless of use": confirmed (`content_set.cpp:111`, 113..117), with numbers in CS1; no audio thread effect.
* "Spectral, Granular, Sample kernels well optimised": partly refuted. The NEON parts are there, but: the strike still memsets 147.5 KB per library oscillator per note (SP1) and runs up to 654 double cos per lane (SP2); the grain kernel needs 75 cycles per grain sample against an inherent 10 to 15 (GR3); the sample kernel 73 cycles per sample against 6 to 10 (SA2); the exact wrap takes 33 us of the 89 us synthesis per frame (SP13); the audio thread still runs the scalar 2048 point `Fft::forward` in the Mask / Vocode path (SP11).

## 7. Ranking: (saving x certainty) / risk, sound-neutral first

Saving columns: steady state (median / p99 of the named presets) and the strike block. Risk: L low, M medium, H high.

| rank | ID | steady saving | strike saving | output | conf. | risk | change in one line |
|---|---|---|---|---|---|---|---|
| 1 | SP1 | 0 | 22 to 38 pts SVF; 20 to 30 pts FPD, BMS, CCP, GGS | bit-identical | high | L | `SpectralVoice::reset` / `GranularVoice::reset` clear flags only; `stopAll` resets only active players |
| 2 | SP2 | 0 | 9 to 25 pts SVF, SGP; 7 to 19 SCM | bit-identical | high / med | L | schedule from a constant table, no double cos per note |
| 3 | MK1 | 1 to 3.3 pts FPD (and every preset with Convolve on) | 0 | bit-identical | med | L | hoist `inBand`, `table()`, the meter's `exp` constants; cache `makeKey` and band powers by inputs |
| 4 | SP5 | 1.0 to 1.4 pts SVF, SGP | 7 to 10 pts | bit-identical | med high | L / M | find lock peaks first, advance only bins the lock keeps |
| 5 | GR3 | 4.7 to 7.7 FPD, 3.2 to 5.2 CCP, 1.9 to 3.2 GGS | small | rounding-level | med | M | block anchored float lane positions in the grain kernel |
| 6 | SP13 | 1.4 to 1.7 SVF, SGP; 1.0 to 1.3 SCM | 10 to 12 pts | rounding-level | med high | M | two part `x - n 2 pi` wrap instead of the compensated one |
| 7 | GR1 | 1.0 to 1.4 GGS | 0 | bit-identical | high | L | hoist the two divisions of `advanceHead` out of the per sample loop |
| 8 | LU1 | 0.4 to 2.2 (BMS 0.9 to 2.2, FPD 0.7 to 1.6, SVF 0.4 to 0.9) | 0 | bit-identical | med | L | cache pan law, power shape, pitch ratio by their inputs |
| 9 | SP10 | 0.35 to 0.8 (FPD 0.7 to 0.8) | small | bit-identical | high | L | NEON gain, clamp and lane mix loops (`simd::rampLerp`, sanitize mask) |
| 10 | SA1 | 0.6 to 0.8 BMS | 0 | bit-identical | med high | L | cache pan gains and the read plan per lane |
| 11 | SA2 | 1.7 to 2.4 BMS | 0 | rounding-level | med | M | float lane positions in the sample kernel |
| 12 | SP4 | unknown (measure) | 12.5 pts SVF | bit-identical | med | M | keyed reuse of the pre-placement shaping in the shared `Scratch` |
| 13 | SP3 | 0 | 40 to 105 pts once (first Spectral note) | bit-identical | high | L | build `RealIfft`, OLA window, `startGain` in `prepare` |
| 14 | SP6 | 0 | 2 to 3.3 pts | bit-identical (signed zero) | high | L | priming OLA straight into the ring from its first kept sample |
| 15 | SP14 | 0.5 to 1.0 | 4 to 6 pts | rounding-level | med | M | radix 4 with 3 multiplies, Stockham order, `vmlaq_f32` |
| 16 | SP7 | 0.4 to 0.8 SVF | 1.7 to 3.3 pts | bit-identical (h 2) / rounding (h 3) | high | L | constant per h, no divide; pointer swap instead of `copyBack` |
| 17 | SP8 | 0.3 to 0.4 | 1.1 to 1.7 pts | bit-identical | med | L | branchless scalar `findPeaks` |
| 18 | SP9 | 0.2 to 0.35 | 0.8 to 1.4 pts | bit-identical | med | L | one fused lerp x filter x band pass; analysis pointers instead of copies |
| 19 | GR2 | 2.3 to 3.5 FPD, 1.6 to 2.4 CCP, 1.0 to 1.4 GGS | small | bit-identical | med low | M | one shared window curve per key instead of gathers |
| 20 | MS1 | 0.35 to 0.7 (multisample presets) | 0 | bit-identical | med | L | NEON layer mix in sustain runs |
| 21 | SP15 | 0 to 0.4 | 0.6 to 1 pt | bit-identical | med | L | merge same offset regions, drop two clears in `noteOn`, prefetch the next frame |
| 22 | SP11 | user patches with Mask / Vocode: 265 to 430 -> 1 to 3 pts | | bit-identical + rounding | high | L | external spectra once per frame, Hann table, NEON real FFT |
| 23 | SP12 | user patches with Detune / Shepard: 10 to 26 -> about 0 pts | | bit-identical | high | L | tables for the per bin hash and log2; gains cached by (a, v) |
| 24 | CS1 | 0 (2.7 to 8.5 MB, 40 to 190 ms load per preset) | 0 | none / short silence on a live type switch | high | M | analysis only for Spectral oscillators, else on the helper thread when needed |
| 25 | CV1..CV4 | 0 on the Key 61; XL large (memory bound 1.4 GB/s now) | 0 | rounding-level (CV3 bit-identical) | med | M / H | half spectrum, split NEON MAC, NEON FIR by outputs, non-uniform partitions |
| 26 | FL1 | unknown | unknown | rounding-level only where it changes scheduling (none) | low | L | try `-mcpu=cortex-a17` (or `-mtune`) for arm32 and compare `--preset-dir` |
| 27 | D1 | 0 | about 14 pts | audible risk small | med | M | one priming frame per lane in the strike block |
| 28 | D2 | 0 | about half the priming | audible (2.9 ms spread) | med | M | half the lanes start one block later |
| 29 | D3 | about half of the spectral share | about half | audible (phasiness) | med | H | hop 1024 at tier low |
| 30 | D4 | about half of the spectral share | about half | audible (resolution) | med | H | FFT 1024 at tier low with its own analysis |

Rough totals if ranks 1 to 21 land (sound-neutral, not additive within one function, estimates): FPD steady 9 to 17 points (from 62 / 84 median / p99); CCP 6 to 9; GGS 5 to 7; BMS 4 to 6 (its content share is only 9.5 %: filters, chorus and the wavetable oscillator are the rest); SVF steady 4 to 6 points. SVF strike block: the levers of section 4 remove 67 to 104 points, so 306 % becomes about 200 to 240 % if the engine's extra factor of section 4 stays as it is, and about 52 to 62 % if that factor is the stealing or the cold caches that SP1 leaves; the measurement of section 8 decides which.

## 8. What only a measurement decides

* The factor of 2.6 to 4 between the engine's Spectral strike and the micro (section 4): `--sections --voices 4` against `--voices 8` on SVF, with the number of `noteOn` calls per strike.
* The cycle split inside the grain and sample kernels (GR2, GR3, SA2): `--micro` lines "GranularVoice::process 128 (14 grains) vector kernel" and "SampleVoice::process 128 vector kernel".
* The rest of MK1's measured 3.3 points: `--profile` on FPD with and without `convolve.on`.
* SP4's steady hit rate: a counter in `shapeFrame` on SVF.
* SP8 and SP14 unit gains: `--micro` "findPeaks scalar" and "RealIfft::inverse".
* `vmlaq_f32` against `vmul` + `vadd` and `-mcpu=cortex-a17` (FL1): `--micro` and `--preset-dir`.
