# Independent Verification of DSP Audit Part 2 (Effects & Output Stage)

**Audit Verified:** `docs/review/OWN-ENGINE-DSP-AUDIT-2-EFFECTS-OUTPUT-2026-11-20.md`  
**Verification Date:** 2026-11-20  
**Scope:** All 19 ranked findings in `core/engine-own/fx/`, `core/fx-serum-a/`, `core/fx-serum-b/`, `core/engine-own/dsp/master_stage.h`, `core/include/synthyboy/output_stage.h`, `core/engine-own/own_engine.cpp`, and `shell-{clap,juce,mpc}/`.

---

## Summary Table

| Rank | Finding in Six Words | Verdict | Corrected `file:line` | Measurement Still Needed |
|---:|---|---|---|---|
| 1 | Distortion 1x rate and X-Shaper stubs | `CONFIRMED` | `core/engine-own/fx/fx_effects.h:675-735`, `core/engine-own/osc/content_set.cpp:49-141` | none |
| 2 | SplitterLMH uses AP4 instead of AP2 | `CONFIRMED` | `core/fx-serum-a/include/synthyboy/fx/dsp_utils.h:279-302`, `core/fx-serum-a/src/splitter_lmh.cpp:109-120` | none |
| 3 | Splitter bands merge without latency compensation | `CONFIRMED` | `core/engine-own/fx/fx_chain.cpp:539-544,583-610` | none |
| 4 | Convolve uniform partitions swap and race | `CONFIRMED` | `core/fx-serum-b/convolve.cpp:445-527,603-607`, `core/engine-own/fx/fx_modules.cpp:778,973-985,1014` | none |
| 5 | Reverb memory per-sample libm and damping | `PARTLY WRONG` | `core/engine-own/fx/fx_reverb.h:76-116,236-249,294-301,374-502`, `core/src/param_table.cpp:699` | none |
| 6 | Delay Lagrange8 and per-sample float divisions | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/fx/fx_dsp.h:65-80`, `core/engine-own/fx/fx_effects.h:68,96,104` | `Delay::process` CPU % on Key 61 / XL with `delay.hq = 1` vs `0` |
| 7 | Bode modulos LF roll-off and softLimit | `CONFIRMED` | `core/fx-serum-b/bode.cpp:25-31,39-60,170-253` | none |
| 8 | Compressor multiband dry/wet phase and CPU | `CONFIRMED` | `core/engine-own/fx/fx_compressor.h:57-77,133-145,159-188,207-215,308-333` | none |
| 9 | voiceCountGain mid-period note-on gain blind spot | `CONFIRMED` | `core/engine-own/own_engine.cpp:1461-1470,1578-1655` | none |
| 10 | FX control-rate stepping and sub-block ramps | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/fx/fx_effects.h:81,198,229,539,608,677-690`, `core/fx-serum-a/src/hyper_dimension.cpp:121-124,230-235` | 1 kHz sine FFT sideband level during fast parameter / LFO modulation sweeps |
| 11 | Flanger hard-clip DC latch and buffer | `CONFIRMED` | `core/engine-own/fx/fx_effects.h:519,537,544,551-557` | none |
| 12 | HyperDimension duplicate buffers and per-sample trig | `CONFIRMED` | `core/fx-serum-a/src/hyper_dimension.cpp:50-63,157-225` | none |
| 13 | Shell mono aliasing sleep and latency | `CONFIRMED` | `shell-clap/synthyboy_clap.cpp:353,392,643`, `core/engine-own/own_engine.cpp:1333,1736-1758`, `shell-juce/PluginProcessor.cpp:494` | none |
| 14 | Distortion per-sample libm divisions and reset | `CONFIRMED` | `core/engine-own/fx/fx_effects.h:673,679,696,706-709,738-740` | none |
| 15 | Equalizer per-sample division redesign and cramping | `CONFIRMED` | `core/engine-own/fx/fx_eq.h:32-105,140,152-158` | none |
| 16 | FxChain fade-out flush latency and splitters | `CONFIRMED` | `core/engine-own/fx/fx_chain.cpp:456-464,554-569,587-608`, `core/engine-own/fx/fx_module.h:158`, `core/engine-own/fx/fx_latency.h:19-28` | none |
| 17 | Chorus wet scale Phaser and sync | `CONFIRMED` | `core/engine-own/fx/fx_effects.h:270-271,602,614-625,651-652`, `core/engine-own/fx/fx_modules.cpp:140,427,472` | none |
| 18 | FX muteSkip hold window and makeup | `PARTLY WRONG` | `core/engine-own/fx/fx_modules.cpp:186,584,639,691,779`, `core/fx-serum-a/src/hyper_dimension.cpp:116-119`, `core/fx-serum-a/src/utility.cpp:73-76`, `core/engine-own/fx/fx_chain.cpp:508-511`, `core/engine-own/own_engine.cpp:349,376,1710` | none |
| 19 | OutputStage sample-peak master limit and spec | `CONFIRMED` | `core/include/synthyboy/output_stage.h:52,212-216,332`, `core/engine-own/dsp/master_stage.h:26-32`, `docs/release/OUTPUT-LEVEL.md:24,62` | none |

---

## Counts per Verdict

- **`CONFIRMED`**: 15 (Ranks 1, 2, 3, 4, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 19)
- **`CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`**: 2 (Ranks 6, 10)
- **`PARTLY WRONG`**: 2 (Ranks 5, 18)
- **`REFUTED`**: 0
- **`COULD NOT VERIFY`**: 0

---

## Detailed Verification by Rank

### Rank 1 — `Distortion`: 1× base rate with no oversampling/ADAA; `X-Shaper` & `X-Shaper (Asym)` stubbed

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_effects.h:675-734`. Also verified in `core/engine-own/fx/fx_modules.cpp:275-289`, `core/engine-own/own_engine.cpp:457,1449`, `core/include/synthyboy/content_state.h:158`, and `core/engine-own/osc/content_set.cpp:49-141`.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_effects.h:686-690,729-734`)**:
```cpp
                if (p.order == 1) x = filt (c, x, cg, k);
                x = shape (c, x * g);
                if (p.order == 2) x = filt (c, x, cg, k);
                io[c][i] = dry * (1.0f - p.mix) + x * p.mix;
            }
            case 9: y = th (32.0f * v); break;
            case 10: y = th (std::max (v, 0.0f)) + std::max (std::min (v, 0.0f), -0.5f); break;
            case 11: y = std::fabs (std::clamp (v, -1.0f, 1.0f)); break;
            case 12: y = std::clamp (v, -1.0f, 1.0f); break; // X-Shaper: the drawn curves default to the identity
            default: y = std::clamp (v, -1.0f, 1.0f); break;
```
- **(c) Surrounding Code & Tests**:
  - `OwnEngine::prepare` (`core/engine-own/own_engine.cpp:457`) prepares all three FX buses at 1× `sampleRate` (`fx[b].prepare(sampleRate, maxBlockSize)`). The `globals.oversample` flag (`own_engine.cpp:1449`) is only used inside the voice render path; `FxChain` and `Distortion` have no oversampling or anti-derivative anti-aliasing (ADAA) stage.
  - Although `StateContent` defines `std::string shaperA, shaperB` (`core/include/synthyboy/content_state.h:158`) and `core/tests/test_fxextra_keys.cpp:123` tests round-trip preset chunk serialization of `shaper_a`/`shaper_b`, `ContentSet::build` (`core/engine-own/osc/content_set.cpp:49-141`) never decodes `src.shaperA` or `src.shaperB`, `ContentSet` (`core/engine-own/osc/content_set.h:26-44`) has no shaper lookup table, and `DistortionModule::configure` (`core/engine-own/fx/fx_modules.cpp:275-289`) ignores `f.content`. Thus types 12 (`X-Shaper`) and 13 (`X-Shaper (Asym)`, hitting `default:` plus the 10 Hz DC blocker at `fx_effects.h:735`) are permanently identical to type 1 (`Hard Clip`).
  - Existing tests in `core/tests/test_own_fx.cpp:166-226` only test types `0..5`.
- **(d) Arithmetic Check**:
  `g = 10^(30 / 20) = 31.6228` (+30 dB); Type 9 (`tanh(32 * v)`) small-signal slope at `v = 0` is `32 * 31.6228 = 1011.93` (`20 * log10(1011.93) = +60.10 dB`) evaluated directly at 1× `sampleRate`.
- **(e) Verdict**: `CONFIRMED`

---

### Rank 2 — `SplitterLMH`: `LinkwitzRiley4AP` is 4th-order (`AP4`) instead of 2nd-order (`AP2`)

- **(a) Cited & Corrected `file:line`**: Cited `core/fx-serum-a/include/synthyboy/fx/dsp_utils.h:279-302`, `core/fx-serum-a/include/synthyboy/fx/splitter_lmh.h:77-78`, `core/fx-serum-a/src/splitter_lmh.cpp:115-116`. Exact lines match.
- **(b) Decisive Code Lines (`core/fx-serum-a/src/splitter_lmh.cpp:109-120`)**:
```cpp
        const float lowRawL = lp1L_.process (sL);
        const float lowRawR = lp1R_.process (sR);
        const float restL = hp1L_.process (sL);
        const float restR = hp1R_.process (sR);

        // Align Low band phase through AP at f2
        lowL[i] = ap2L_.process (lowRawL);
        lowR[i] = ap2R_.process (lowRawR);

        // Stage 2: split rest at f2 into Mid and High
        midL[i] = lp2L_.process (restL);
        midR[i] = lp2R_.process (restR);
```
- **(c) Surrounding Code & Tests**:
  - `ap2L_` and `ap2R_` (`core/fx-serum-a/include/synthyboy/fx/splitter_lmh.h:77-78`) are instances of `LinkwitzRiley4AP` (`core/fx-serum-a/include/synthyboy/fx/dsp_utils.h:279-302`).
  - `LinkwitzRiley4AP::setCutoff` sets two identical 2nd-order Butterworth all-pass biquads (`Q = 1/sqrt(2)`), and `LinkwitzRiley4AP::process` (`dsp_utils.h:295-298`) returns `stage2_.process(stage1_.process(in))`, i.e., `[AP2(f2)]^2 = AP4(f2)`.
  - However, the sum of a 4th-order Linkwitz-Riley low-pass and high-pass pair is a single 2nd-order Butterworth all-pass: `LR4_LP(s) + LR4_HP(s) = (1 + s^2)^2 / (1 + sqrt(2)*s + s^2)^2 = (1 - sqrt(2)*s + s^2) / (1 + sqrt(2)*s + s^2) = AP2(s)`.
  - Existing tests: `Splitters_ReconstructionFlatMagnitude` (`core/fx-serum-a/tests/test_fx_serum_a.cpp:265-305`) tests flat reconstruction on `SplitterLH` and `SplitterMS` (`< 0.1 dB` error) and checks `SplitterLMH` band energy separation (`lines 286-297`), but never sums `low + mid + high` on `SplitterLMH` to verify flat reconstruction.
- **(d) Arithmetic Check**:
  At `f = f2` (`s = j`), `arg(LR4_LP(j) + LR4_HP(j)) = arg(AP2(j)) = -2 * atan2(sqrt(2), 0) = -180 deg`, whereas `arg(LinkwitzRiley4AP(j)) = 2 * (-180 deg) = -360 deg`, leaving a `180 deg` phase opposition between `Low` and `Mid + High` at `f2` (for `f1 = f2`, `|Low + Mid + High| = |0.5 * (-1) + 0.5 * (+1)| = 0` (`-inf dB`); for `f2 = 2 * f1`, `|LR4_LP(2)| = 1/5` and `|LR4_HP(2)| = 4/5`, giving `|-1/5 + 4/5| = 3/5 = -4.44 dB` analog / `-4.3 dB` bilinear-warped at `44.1 kHz`).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 3 — `FxChain` splitters: zero inter-band latency compensation before `merge()`, contradicting `rackLatency()`

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_chain.cpp:583-610` vs `core/engine-own/fx/fx_chain.cpp:539-544`. Exact lines match.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_chain.cpp:583-591`)**:
```cpp
        case 13: // splitter L/H: the bands run through their racks (3 low, 4 high) and are summed again
        {
            float bl[2][kControlPeriod], br[2][kControlPeriod];
            splitLh.split (l, r, bl[0], br[0], bl[1], br[1], n);
            runRack (3, bl[0], br[0], n, t0, depth + 1);
            runRack (4, bl[1], br[1], n, t0, depth + 1);
            splitLh.merge (bl[0], br[0], bl[1], br[1], l, r, n);
            break;
        }
```
- **(c) Surrounding Code & Tests**:
  - `FxChain::rackLatency` (`core/engine-own/fx/fx_chain.cpp:539-544`) reports `std::max` across parallel band racks (`racks 3..4` for `splitLh`, `5..7` for `splitLmh`, `8..9` for `splitMs`), and `docs/engine-spec/06-effects.md:360` explicitly specifies: *"when a splitter's bands have different latencies, the shorter bands are delayed to match the longest before the merge (`IntDelay` per band)"*.
  - In `FxChain::runModule` (`fx_chain.cpp:583-610`), `IntDelay` is never applied to any splitter band (unlike `OwnEngine::process` at `core/engine-own/own_engine.cpp:1703-1708`, which aligns `busLat[0..3]` via `busAlign[b]`).
  - Existing tests in `core/tests/test_fxrack.cpp` and `core/tests/test_own_fx.cpp:498-538` test splitter routing with zero-latency modules (`Distortion`, `Utility`) and never place `Bode` (`64` samples) or `Compressor` in Limit mode (`round(0.0015 * sr)` samples) inside a single splitter band.
- **(d) Arithmetic Check**:
  `Bode::kHilbertLatency = (129 - 1) / 2 = 64` samples (`core/fx-serum-b/bode.h:32`); `Compressor` Limit look-ahead = `lround(0.0015 * sr) = 66` (@ 44.1 kHz), `72` (@ 48 kHz), `144` (@ 96 kHz) (`core/engine-own/fx/fx_compressor.h:30,33`); a 64-sample inter-band delay mismatch at `44100 Hz` produces comb-filter cancellation nulls at `f = (2k + 1) * 44100 / (2 * 64) = 344.53 Hz, 1033.59 Hz, ...`.
- **(e) Verdict**: `CONFIRMED`

---

### Rank 4 — `Convolve`: uniform 128-sample partitions (`4,095` partitions / `33.5 MB`), complex FFT, hard swap, and `live.tone` data race

- **(a) Cited & Corrected `file:line`**: Cited `core/fx-serum-b/convolve.cpp:445-527,603-607`, `core/engine-own/fx/fx_modules.cpp:778,973-985,1014`. Also checked `core/fx-serum-b/convolve.h:35-37,103-121` and `core/fx-serum-b/fft.cpp:53-120`.
- **(b) Decisive Code Lines (`core/fx-serum-b/convolve.cpp:487-496`)**:
```cpp
    for (int p = 0; p < m_numTailPartitions; ++p)
    {
        const int histIdx = (m_tailFftHistHead - p + m_numTailPartitions) % m_numTailPartitions;
        const auto& X = m_tailFftHistL[static_cast<size_t> (histIdx)];
        const auto& H = m_tailSpectraL[static_cast<size_t> (p)];
        for (int k = 0; k < kFftSize; ++k)
            m_scratchComplexSum[static_cast<size_t> (k)] = m_scratchComplexSum[static_cast<size_t> (k)] + X[static_cast<size_t> (k)] * H[static_cast<size_t> (k)];
    }

    m_fft.ifft (m_scratchComplexSum.data());
```
- **(c) Surrounding Code & Tests**:
  - `kPartitionSize = 128`, `kFftSize = 256`, `kMaxIrFrames = 524288` (`core/fx-serum-b/convolve.h:35-37`). Every 128 samples (`m_blockPos >= kPartitionSize` at `convolve.cpp:620`), `computeTailFftBlock()` loops over all `m_numTailPartitions` for L and R across all `256` complex bins using `std::vector<std::vector<Complex>>` (pointer-chasing each partition) and `Fft::forwardReal` (`core/fx-serum-b/fft.cpp:110-120`), which copies real samples into a full 256-point complex buffer instead of exploiting conjugate symmetry (`N/2 + 1 = 129` bins).
  - The 128-tap direct FIR (`convolve.cpp:603-612`) runs scalar modulo ring indexing `(m_headPos - k + kPartitionSize) % kPartitionSize` per tap per sample.
  - `ConvolveModule::takeOver` (`core/engine-own/fx/fx_modules.cpp:973-985`) swaps `active = fresh` with no audio crossfade and with zeroed delay/overlap state in `fresh`.
  - Data race: the audio thread writes plain `float live.tone` every control period (`fx_modules.cpp:778`) while the background `FxWorker` thread reads `m->live.tone` unsynchronized in `ConvolveModule::runJob` (`fx_modules.cpp:1014`).
  - Existing tests (`core/fx-serum-b/tests/test_convolve.cpp`, `core/tests/test_own_convolve_bypass.cpp`) use short synthetic IRs (`<= 2048` frames).
- **(d) Arithmetic Check**:
  At `kMaxIrFrames = 524288`: `m_numTailPartitions = (524288 - 128) / 128 = 4095`; `4` tables (`m_tailSpectraL/R`, `m_tailFftHistL/R`) `* 4095 * 256 * 8 B = 33,546,240 B = 33.55 MB` (`31.99 MiB`) per `Convolve` instance (`67.09 MB` during helper-thread rebuild), and `2 * 4095 * 256 = 2,096,640` complex multiply-adds every `128` samples (`16,380` complex MACs/sample = `7.86e8` complex MACs/s at `48 kHz`).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 5 — `Reverb`: `4.87 MB` per instance, per-sample `libm`/64-op Hadamard, and Nitrous Space negative HF damping claim

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_reverb.h:76-116,236-249,295-301,374-428,446-468,484-497`. Also checked `core/src/param_table.cpp:699` and `docs/engine-spec/06-effects.md:185`.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_reverb.h:294-301`)**:
```cpp
        const float ahRaw = std::pow (10.0f, std::clamp (p.highShelfDb, -24.0f, 0.0f) * 0.05f);
        static const float kRoomK[5] = { 1.45f, 0.40f, 1.0f, 1.0f, 0.80f };
        float ah = ahRaw;
        if (type == 5)
            ah = 1.0f - (1.0f - ahRaw) * 0.35f;
        else if (type == 4)
            ah = 1.0f - (1.0f - ahRaw) * kRoomK[std::clamp (p.room, 0, 4)];
        c.aH = ah;
```
- **(c) Surrounding Code & Tests (Why PARTLY WRONG)**:
  - **What is confirmed**:
    1. `Reverb::prepare` (`fx_reverb.h:76-116`) unconditionally allocates delay lines for all 6 reverb topologies simultaneously (`4.87 MB` at `44.1 kHz`, `5.09 MB` at `48 kHz`).
    2. `vintageSample` (`fx_reverb.h:473-502`) runs `16` `std::sin`, `16` `gainFor` (`16` `std::exp` + `16` divisions, plus `16` `len / sr` divisions), and `2` `OnePole::coefficient(7000.0f, sampleRate)` (`2` `std::exp` + `2` divisions) **per sample**.
    3. `netSample` (`fx_reverb.h:393-433`, types 2/4/5) runs the 64-iteration nested Hadamard loop with bit-popcount sign branch (`lines 415-421`), `8` `std::sin`, and `32` `readCubic` calls (`12` ER + `20` tank/AP) per sample.
    4. `fdnSample` (`fx_reverb.h:232-275`, type 0) calls `16` `sin`/`cos` per sample for only `4` unique `lane = k % 4` phases (`lines 236-249`) and calls `apLine[k].readCubic` on `kA[k] * scale - 1.0f`, which is an unmodulated integer constant.
  - **What is wrong**:
    1. **Nitrous Space (`type == 4, room == 0`) cannot reach `ah = -0.36` in the engine**: Although `fx_reverb.h:294` clamps `p.highShelfDb` to `[-24.0f, 0.0f]`, the engine's parameter table (`core/src/param_table.cpp:699`, matching `docs/engine-spec/06-effects.md:185`) defines `reverb.high_shelf_gain` with range **`min = -6.0f, max = 0.0f`**, and `modulatedValue` clamps to `[min, max]`. At the minimum reachable parameter value `-6.0 dB`, `ahRaw = 10^(-6/20) = 0.50119` and `ah = 1 - (1 - 0.50119) * 1.45 = +0.2767 > 0`. `ah < 0` requires `highShelfDb < -10.16 dB`, which is unreachable via engine parameters or modulation (only possible by calling `Reverb::set` directly in a C++ harness bypassing `param_table.cpp`).
    2. **Plate `readCubic` count and fractional all-pass delays**: `plateSample` (`fx_reverb.h:435-471`) executes **26** `readCubic` calls (`4` in `plIn`, `8` across the two tank branches, `14` in output taps) plus `12` ER `readCubic` in `coreSample` = **38** `readCubic`/sample (not `21 + 12 = 33`). Furthermore, contrary to the audit's Module 4(f) recommendation, only `apLine` in `fdnSample` has integer delays; `inAp`, `netAp`, `plDel1/2`, `plAp2`, `vInAp`, `vAp`, and `erLine` are all scaled by float factors (`0.001f * sr`, `sr / 29761.0f * cfg.sizeScale`, `sr / 44100.0f * cfg.sizeScale`, `cfg.erScale`) and are fractional.
- **(d) Arithmetic Check**:
  Memory at `44.1 kHz`: `(16*32768 + 16*2048 + 8*32768 + 4*2048 + 8*512 + (4*4096 + 2*8192 + 2*32768 + 2*16384 + 2*32768) + (2*4096 + 16*8192 + 8*1024) + 8192 + 32768) * 4 B = 1,218,560 * 4 B = 4,874,240 B = 4.87 MB` (`4.65 MiB`; at `48 kHz`: `1,271,808 * 4 B = 5,087,232 B = 5.09 MB`); Nitrous Space `ah`: at `-24 dB` raw struct input, `1 - (1 - 10^(-24/20)) * 1.45 = -0.3585`, but at the engine parameter limit `-6 dB`, `1 - (1 - 10^(-6/20)) * 1.45 = +0.2767`.
- **(e) Verdict**: `PARTLY WRONG`

---

### Rank 6 — `DelayLine::readLagrange8`: 56 float divisions/channel/sample (112/stereo sample) + `Delay` per-sample divisions & `2 MB` RAM

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_dsp.h:65-80`, `core/engine-own/fx/fx_effects.h:68,96,104`. Exact lines match.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_dsp.h:70-79`)**:
```cpp
        float acc = 0.0f;
        for (int k = -3; k <= 4; ++k)
        {
            float w = 1.0f;
            for (int m = -3; m <= 4; ++m)
                if (m != k)
                    w *= (t - static_cast<float> (m)) / static_cast<float> (k - m);
            acc += w * buf[static_cast<size_t> ((pos - i - k) & mask)];
        }
        return acc;
```
- **(c) Surrounding Code & Tests**:
  - `readLagrange8` is called when `Delay::Params::hq == true` (`core/engine-own/fx/fx_effects.h:111`), which is exposed via parameter `delay.hq` (`core/engine-own/fx/fx_modules.cpp:209`, defaulting to `0.0f` in `core/src/param_table.cpp:688`).
  - `Delay::process` (`fx_effects.h:96,104`) calls `std::pow` once per 32-sample block (`line 96`) and executes two float divisions `sr / fLast[0]` and `sr / fLast[1]` per sample (`line 104`) regardless of `hq`.
  - `Delay::prepare` (`fx_effects.h:68`) allocates `d.prepare(static_cast<int>(sr * 4.2))` for each channel.
  - Because `delay.hq` defaults to `0` and the exact CPU cost on Cortex-A55/A76 depends on how much of the constant denominator `static_cast<float>(k - m)` the compiler hoists or vectorizes, the code defect is confirmed while the hardware CPU % impact requires measurement.
- **(d) Arithmetic Check**:
  `8` outer iterations (`k = -3..4`) `* 7` inner iterations (`m != k`) `= 56` float divisions/channel/sample (`112` divisions/stereo sample, `5.376e6` divisions/s at `48 kHz`) + `2` divisions/sample at `fx_effects.h:104`; buffer size at `44.1 / 48 kHz`: `next_pow2(int(48000 * 4.2) + 4) = next_pow2(201604) = 262144` floats `* 2 ch * 4 B = 2,097,152 B = 2.00 MiB` (`2.10 MB`) per `Delay` instance (`6.00 MiB` across 3 buses).
- **(e) Verdict**: `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`

---

### Rank 7 — `Bode`: 66 modulo `% 129` ops + `fmod`/double trig per sample, Hilbert LF roll-off below ~800 Hz, and `softLimit` clipping clean dry input at `feedback == 0`

- **(a) Cited & Corrected `file:line`**: Cited `core/fx-serum-b/bode.cpp:25-31,39-60,178-236`. Also checked `core/fx-serum-b/bode.cpp:170,192-195,253` and `core/fx-serum-b/bode.h:31-32`.
- **(b) Decisive Code Lines (`core/fx-serum-b/bode.cpp:218-229`)**:
```cpp
        float qL = 0.0f;
        float qR = 0.0f;
        for (int k = 1; k <= M; k += 2)
        {
            const float c = m_hilbertCoeffs[static_cast<size_t> (M + k)];
            const int idxPos = (m_histPos - (M + k) + kHilbertTaps) % kHilbertTaps;
            const int idxNeg = (m_histPos - (M - k) + kHilbertTaps) % kHilbertTaps;
            qL += c * (m_histL[static_cast<size_t> (idxPos)] - m_histL[static_cast<size_t> (idxNeg)]);
            qR += c * (m_histR[static_cast<size_t> (idxPos)] - m_histR[static_cast<size_t> (idxNeg)]);
        }

        m_histPos = (m_histPos + 1) % kHilbertTaps;
```
- **(c) Surrounding Code & Tests**:
  - Counting all `% kHilbertTaps` (`129`) inside `Bode::process`: `1` at `line 213` (`centerIdx`), `2 * 32 = 64` inside the `k = 1..63` step-2 loop (`lines 223-224`), and `1` at `line 229` (`m_histPos`) = **66** non-power-of-two `% 129` operations per sample (1 more than the audit's 65), plus `1` `% kBlurTaps` (`% 64` at `line 170`), `3` `% m_delayCapacity` (`lines 192-193, 253`), `2` `std::fmod` + `2` `std::floor` (`lines 192-194`), and `4` double-precision `std::sin`/`std::cos` (`lines 178-179, 232-233`).
  - `softLimit` (`bode.cpp:25-31`: `x >= 1.5f ? 1.0f : x <= -1.5f ? -1.0f : (x - x^3 / 6.75f)`) is applied unconditionally to `inL + feedback * delOutL` (`lines 205-206`). When `feedback == 0.0f`, any dry input inside the engine's `±1.8` headroom is nonlinearly compressed (`1.0 -> 0.8519`) and hard-capped at `±1.0` for `|in| >= 1.5`.
  - The 129-tap Blackman-windowed FIR Hilbert transformer (`bode.cpp:39-60`, `M = 64`) has an odd-symmetric impulse response (`H(0) = 0`) whose transition band spans `0 .. ~3 * fs / (2 * M)`, causing quadrature gain `|H_Q(f)|` to roll off below `~800 Hz` (existing test `core/fx-serum-b/tests/test_bode.cpp:69` only tests a `440 Hz` sine with a `±4 dB` tolerance).
- **(d) Arithmetic Check**:
  `1 + 2 * 32 + 1 = 66` modulo `% 129` ops/sample; Blackman FIR main-lobe half-width `= 3 * 44100 / 128 = 1033.59 Hz` (`-3 dB` quadrature roll-off around `~517..800 Hz`, leaving `1 - |H_Q(f)|` opposite-sideband leakage); `softLimit(1.5f) = 1.5 - 3.375 / 6.75 = 1.0f` (`-3.52 dB` compression at `|x| = 1.5`, `-4.86 dB` clipping at `|x| = 1.8`).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 8 — `Compressor`: multiband dry/wet phase cancellation (`mix < 1.0`), $O(W)$ look-ahead limiter, per-sample `pow`/`log10`, and redundant AP biquads

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_compressor.h:57-77,133-145,159-188,207-215,322-333`. Exact lines match.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_compressor.h:207-216`)**:
```cpp
            float d[2] = { in[0], in[1] };
            if (limitMode)
                for (int c = 0; c < 2; ++c)
                {
                    dry[c].push (in[c]);
                    d[c] = dry[c].read (static_cast<float> (lookAhead));
                }
            l[i] = d[0] + (out[0] - d[0]) * p.mix;
            r[i] = d[1] + (out[1] - d[1]) * p.mix;
        }
```
- **(c) Surrounding Code & Tests**:
  - When `p.bands < 3` (`Multiband`, `Low Band`, `High Band`), `sig[0..2]` are split through the LR4 crossover (`lines 133-145`), so `out[c]` has the LR4 all-pass phase shift (`AP2(f1) * AP2(f2)` for `p.bands == 0`, `AP2(f1)` for `p.bands == 1, 2`). However, `d[c]` (`lines 207-213`) is taken directly from the unfiltered input `in[c]` (only delayed by `lookAhead` in `limitMode`), so any parallel blend (`0 < p.mix < 1`) sums phase-shifted wet bands against un-shifted dry input.
  - Line 140 (`lowS = ap[c][1].process(ap[c][0].process(lowS)) + ap[c][3].process(ap[c][2].process(lowS))`) runs 4 biquads per channel (`LR4_LP(f2) + LR4_HP(f2)`) where a single 2nd-order Butterworth all-pass biquad `AP2(f2)` has the exact same transfer function.
  - `Limiter::process` (`fx_compressor.h:308-333`) executes two loops of length `W = la + 1` per sample per active band (a minimum scan over `req` and a box-car sum over `fir`), instead of an $O(1)$ monotonic deque and running sum.
  - Mode 0 (`lines 159-174`) executes up to `6` `powG` calls and `6` envelope divisions `(x2 + e * N) / (N + 1.0f)` per sample; Mode 2 (`serumMode`, `line 182`) calls `std::log10` per active band per sample even when `fast == true`; `set()` (`lines 57-77`) redesigns 8 biquads (`lr4` twice) every 32 samples without parameter-change caching.
  - Existing tests (`core/tests/test_own_fx.cpp:228-277`, `core/tests/test_own_compressor_opt.cpp`) do not test frequency response at `p.bands < 3` with `p.mix = 0.5f`.
- **(d) Arithmetic Check**:
  At `f = f1` (`crossLow = 160 Hz`) and `f = f2` (`crossHigh = 2500 Hz`), `AP2` phase is `-180 deg` (`AP2(j) = -1`), so at unity band gain (`0 dB` reduction, `makeup = 1`) and `p.mix = 0.5f`, `|d + (out - d) * 0.5| = |0.5 * 1 + 0.5 * (-1)| = 0` (`-inf dB` notch when `f1 << f2`); `Limiter::process` performs `2 * (la + 1)` loop iterations per band per sample (`2 * 67 * 3 = 402` iterations/sample at `44.1 kHz`, `2 * 73 * 3 = 438` at `48 kHz`, `2 * 145 * 3 = 870` at `96 kHz`).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 9 — `OwnEngine` `voiceCountGain`: mid-period note-on blind spot when `eventGrid == false`

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/own_engine.cpp:1461-1470,1578-1655`. Exact lines match.
- **(b) Decisive Code Lines (`core/engine-own/own_engine.cpp:1578-1586`)**:
```cpp
            while (ei < numEvents && events[ei].frame <= pos)
                handleEvent (events[ei++]);
            if (ei < numEvents)
                end = std::min (end, std::max (events[ei].frame, pos + 1));
        }
        const int n = end - pos;
        if (controlPos == 0)
        {
            voiceCountGainPrev = voiceCountGainCur;
```
- **(c) Surrounding Code & Tests**:
  - When `eventGrid == false` (`(globals.economy & kEconomyControl) == 0`, i.e., `medium` and `high` quality tiers; note that MPC XL's profile `"high,voices=16,control=economy"` in `shell-mpc/device_profile.h:59` sets `kEconomyControl`, so `eventGrid == false` applies to desktop `medium`/`high`), mid-period note-ons split the sub-block at `events[ei].frame` (`line 1581`) and trigger voices at `controlPos != 0` (`line 1579`).
  - However, both `soundingAtPeriodStart` (`lines 1463-1469`) and the entire `voiceCountGain` calculation (`lines 1584-1655`) are gated inside `if (controlPos == 0)`.
  - Consequently, when a chord is struck from silence at `controlPos in [1, 31]`, the new voices render for the remaining `32 - controlPos` samples of the control period with `voiceCountGainCur == 1.0f` (`0 dB`). Then, at the next `controlPos == 0` boundary, `soundingAtPeriodStart` counts those already-active voices (`!= 0`), taking the rate-limited branch (`3.0 dB/ms` at `lines 1637-1643`) instead of the instant-step branch (`line 1631`: `if (soundingAtPeriodStart == 0)`), contradicting `docs/release/VOICE-COUNT-GAIN-SPEC.md:83`.
  - Existing tests in `core/tests/test_own_voice_count_gain.cpp` trigger all note-ons at `frame = 0` (`controlPos == 0`), missing `frame in [1, 31]`.
- **(d) Arithmetic Check**:
  For a 4-voice chord (`alpha = 0.5`, target `T = 4^(-0.5) = 0.5 = -6.0206 dB`) struck from silence at `controlPos = 1` (`44.1 kHz`), `32 - 1 = 31` samples (`0.703 ms`) render at `G = 1.0` (`+6.02 dB` overshoot), followed by a `3.0 dB/ms` ramp (`maxDownDb = 3.0 * (32 / 44.1) = 2.1769 dB/period`) requiring `ceil(6.0206 / 2.1769) = 3` control periods (`96` samples = `2.177 ms`) to reach `-6.02 dB`.
- **(e) Verdict**: `CONFIRMED`

---

### Rank 10 — Control-rate (32-sample) parameter stepping across FX, Chorus 4-sample / Phaser 8-sample holds, and `fx-serum-a` sub-block `n` ramps

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_effects.h:81,198,229,539,608,677-690`, `core/engine-own/fx/fx_eq.h:65-77`, `core/engine-own/fx/fx_reverb.h:169,179`, `core/engine-own/fx/fx_modules.cpp:399`, `core/fx-serum-a/src/hyper_dimension.cpp:121-124`. Also verified `core/fx-serum-a/src/{hyper_dimension.cpp:230-235,utility.cpp:102-106,splitter_lh.cpp:96-100,splitter_lmh.cpp:140-145,splitter_ms.cpp:71-75}`.
- **(b) Decisive Code Lines (`core/fx-serum-a/src/hyper_dimension.cpp:121-124`)**:
```cpp
    const float invN = 1.0f / static_cast<float> (n);
    const float stepHyperMix = (targetHyperMix - currentHyperMix_) * invN;
    const float stepDimMix = (targetDimMix - currentDimMix_) * invN;
    const float stepLevel = (targetLevel - currentLevel_) * invN;
```
- **(c) Surrounding Code & Tests**:
  - In `core/engine-own/fx/`, `MixGains` (`Delay`, `Chorus`, `Reverb`), `Flanger` `wetG`/`dryG`/`dc`, `Distortion` `g`/`cg`/`k`/`mix`, `Equalizer` Law 0 SVF `g`/`R`/`wl`/`wb`/`wh`, `Phaser` `mix`/`feedback`/`blend`, `Compressor` `mix`/`makeup`, and `FxModule::applyLevel` (`fx_module.h:162`: `float g = exp2(...)`) are computed once per `process()` call (up to `32` samples) and held constant across the sub-block.
  - On tier `low` (`fast == true`), `Chorus` holds `period[]` for 4 samples (`fx_effects.h:229`: `const bool refresh = !fast || (s & 3) == 0;`) and `Phaser` holds `hS`/`hc1`/`hc2` for 8 samples (`fx_effects.h:608`: `if (!fast || (i & 7) == 0)`).
  - In `core/fx-serum-a/`, all five modules compute `step = (target - current_) / n` and snap `current_ = target_` at the end of `process(l, r, n)`, making ramp slope depend on sub-block size `n` (`1..32` when mid-period MIDI splits occur).
  - While the code behavior is confirmed, whether the resulting zipper sidebands are audible above the signal masking floor requires the named FFT modulation sweep measurement.
- **(d) Arithmetic Check**:
  Control-rate step frequency at `44.1 kHz`: `44100 / 32 = 1378.125 Hz` (and harmonics); tier-`low` Chorus 4-sample hold (`11025 Hz`) maximum tap step at `depth = 1.0`: `Delta_d = 4 * (2 * pi * f_lfo / sr) * (0.5 * 0.03 * sr) = 0.12 * pi * f_lfo = 1.508` samples at `f_lfo = 4 Hz` (`1.885` samples at `5 Hz`).
- **(e) Verdict**: `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`

---

### Rank 11 — `Flanger`: `[-1, +1]` hard-clip in feedback loop, no DC blocker, `512 KB` oversized buffer, and per-sample `p.rate / sr` division

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_effects.h:519,537,544,556`. Exact lines match.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_effects.h:551-558`)**:
```cpp
                const float mod = p.depth * (2.0f * tri - 1.0f) + 1.0f;
                const float delay = std::max ((dc - 0.0005f) * mod + 0.0005f, 1.0f / 20000.0f);
                const float period = std::clamp (delay * sr, 2.0f, 0.85f * sr);
                const float rd = line[c].readCubic (period);
                const float in = io[c][i];
                line[c].push (std::clamp (in + f * rd, -1.0f, 1.0f) * 1.0f);
                io[c][i] = dryG * in + wetG * rd;
            }
```
- **(c) Surrounding Code & Tests**:
  - `line[c].push(std::clamp(in + f * rd, -1.0f, 1.0f) * 1.0f)` (`line 556`, implementing `docs/engine-spec/06-effects.md:159-162`) hard-clips the delay input at `±1.0f` even when `f == 0.0f`, clipping peaks inside the engine's `±1.8` headroom (`docs/release/OUTPUT-LEVEL.md`), and has no DC blocker or high-pass filter in the loop, so `f = +1.0f` (`line 537`) integrates and latches any DC offset at `±1.0f`.
  - `phase += p.rate / sr` (`line 544`, and `line 529` in `idle()`) divides by `sr` every sample.
  - `Flanger::prepare` (`line 519`) allocates `d.prepare(static_cast<int>(sr * 0.9))` per channel (`850 ms` clamp at `line 553`), even though `dc = 1.0f / std::clamp(p.centerHz, 20.0f, 16000.0f) <= 0.05f` and `mod <= 2.0f` (`depth in [0, 1]`), so `delay` can never exceed `(0.05f - 0.0005f) * 2.0f + 0.0005f = 0.0995f` seconds.
- **(d) Arithmetic Check**:
  At `44.1 / 48 kHz`, `next_pow2(int(48000 * 0.9) + 4) = next_pow2(43204) = 65536` floats `* 2 ch * 4 B = 524,288 B = 512 KiB` per instance (`1.5 MiB` across 3 buses), whereas the maximum reachable delay `0.0995 s` (`4388` samples @ `44.1 kHz`, `4776` @ `48 kHz`) needs `next_pow2(4780) = 8192` floats `* 2 * 4 B = 65,536 B = 64 KiB` (`8x` smaller).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 12 — `HyperDimension`: 11 duplicate mono delay buffers (`176 KB`), 23 `libm` trig + 7 divisions/sample, and unskipped Dimension stage

- **(a) Cited & Corrected `file:line`**: Cited `core/fx-serum-a/src/hyper_dimension.cpp:157-225`. Also checked `core/fx-serum-a/src/hyper_dimension.cpp:50-63` and `core/fx-serum-a/include/synthyboy/fx/hyper_dimension.h:51,57`.
- **(b) Decisive Code Lines (`core/fx-serum-a/src/hyper_dimension.cpp:175-181`)**:
```cpp
                // Stereo spread
                float pan = 0.0f;
                if (unison > 1)
                    pan = -1.0f + 2.0f * static_cast<float> (v) / static_cast<float> (unison - 1);
                const float angle = (pan + 1.0f) * (0.25f * kPi);
                hypL += voiceSample * std::cos (angle);
                hypR += voiceSample * std::sin (angle);
```
- **(c) Surrounding Code & Tests**:
  - `hyperBuffers_[0..6]` (`lines 160-165`) and `dimBuffers_[0..3]` (`lines 200-206`) all store the exact same `monoIn = 0.5f * (inL + inR)` sample at synchronized write pointers (`11` identical ring buffers instead of `1` shared buffer).
  - Inside the per-sample loop (`lines 157-188`), for each voice `v < unison`, the code recomputes `pan = -1.0f + 2.0f * float(v) / float(unison - 1)` (1 float division), `std::cos(angle)`, and `std::sin(angle)` (`2` trig calls on an `angle` that depends only on `(v, unison)`), plus `std::sin(phase)` (`1` trig call). Then lines 198-225 unconditionally run the 4 Dimension buffers, 4 cubic reads, and `std::sin(dimLfoPhase_)` + `std::cos(dimLfoPhase_)` even when `currentDimMix_ == 0.0f && targetDimMix == 0.0f`.
  - This matches the repository's existing analysis `[measured: docs/release/CPU-REVIEW-FILTER-FX.md:399]`.
- **(d) Arithmetic Check**:
  At `44.1 / 48 kHz`, `next_pow2(int(0.070 * 48000) + 32) = next_pow2(3392) = 4096` floats `* 4 B = 16 KiB` per buffer `* (7 + 4) = 176 KiB` per instance (`528 KiB` across 3 buses) vs `16 KiB` for 1 shared buffer (`11x` reduction); at `unison = 7`, `7 * 3 + 2 = 23` `libm` trig calls and `7` float divisions per sample (`1.104e6` trig calls/s and `3.36e5` divisions/s at `48 kHz`).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 13 — Shell Output & Lifecycle: CLAP mono output buffer aliasing, CLAP premature `CLAP_PROCESS_SLEEP` on FX tails, and JUCE RT `setLatencySamples`

- **(a) Cited & Corrected `file:line`**: Cited `shell-clap/synthyboy_clap.cpp:353,392`, `shell-juce/PluginProcessor.cpp:494`. Also verified `shell-clap/synthyboy_clap.cpp:643` and `core/engine-own/own_engine.cpp:1333,1736-1758`.
- **(b) Decisive Code Lines (`shell-clap/synthyboy_clap.cpp:352-359`)**:
```cpp
        float* outL = process->audio_outputs[0].data32[0];
        float* outR = (process->audio_outputs[0].channel_count > 1) ? process->audio_outputs[0].data32[1] : outL;

        const bool meters = self->ui.guiActive.load (std::memory_order_relaxed);
        const auto processStart = meters ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
        self->engine->process (self->events.data(), static_cast<int> (self->events.size()),
                               outL, outR, static_cast<int> (process->frames_count));
        self->outputStage.process (outL, outR, static_cast<int> (process->frames_count));
```
- **(c) Surrounding Code & Tests**:
  - **CLAP mono aliasing**: When `channel_count == 1`, `synthyboy_clap.cpp:353` sets `outR = outL` (non-null). Inside `OwnEngine::process` (`core/engine-own/own_engine.cpp:1333,1736-1758`), mono downmixing (`0.5f * (leftOut[i] + rScratch[i])`) only triggers when `rightOut == nullptr`! When `rightOut == leftOut`, `std::copy(mixR[0], mixR[0] + n, rightOut + pos)` (`line 1737`) overwrites `outL` with the Right channel, and `master::stage` (`core/engine-own/dsp/master_stage.h:39-63`) then applies its DC blocker and `outputLimit` twice in series to the same memory buffer `outL`.
  - **CLAP sleep on FX tails**: `synthyboy_clap.cpp:392` returns `CLAP_PROCESS_SLEEP` immediately when `self->engine->activeVoices() == 0` (and `ext_tail_get` at `synthyboy_clap.cpp:643` always returns `0`), allowing the host to stop calling `plugin_process` while `Reverb`, `Delay`, `Convolve`, or the 3 ms `OutputStage` look-ahead buffer still hold audio.
  - **JUCE RT `setLatencySamples`**: `shell-juce/PluginProcessor.cpp:494` calls `setLatencySamples(latency)` directly inside `SynthyBoyAudioProcessor::processBlock()` on the real-time audio thread (whereas `shell-clap/synthyboy_clap.cpp:388` uses `host->request_restart`).
- **(d) Arithmetic Check**:
  When `channel_count == 1` (`outR == outL`), `leftOut` receives `100%` of `mixR[0]` (`0%` of `mixL[0]`) and passes through `master::dcStep` (`R = 1 - 125.6637 / sr`) and `master::outputLimit` (`2` passes per sample) + `OutputStage::process` (`2` passes of `outL[i] *= g`, squaring the limiter gain `g^2`).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 14 — `Distortion::shape`: redundant per-sample `pow10` + division + `exp` + `Svf::process` division, incomplete `reset()`, missing `flushDenormal`, and linear `k` law

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_effects.h:673,679,696,706-709,738-740`. Also checked `core/engine-own/fx/fx_dsp.h:95-120` and `docs/engine-spec/06-effects.md:81`.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_effects.h:704-709`)**:
```cpp
        const float u = clamp01 ((p.driveDb + 30.0f) / 60.0f);
        const float dExp = std::clamp (p.driveDb, -30.0f, 30.0f) * 0.05f;
        const float d = fast ? fastmath::pow10 (dExp) : std::pow (10.0f, dExp);
        auto th = [this] (float a) { return fast ? fastmath::tanh (a) : std::tanh (a); };
        auto ex = [this] (float a) { return fast ? fastmath::exp (a) : std::exp (a); };
        const float x = d != 0.0f ? v / d : v; // the unscaled input
```
- **(c) Surrounding Code & Tests**:
  - `process()` already computes `const float g = std::pow(10.0f, p.driveDb * 0.05f)` at `line 677` and passes `x * g` into `shape(c, x * g)` (`line 687`), yet `shape()` recomputes `d = pow10(dExp)` (`line 706`) and divides `x = v / d` (`line 709`) every sample for all 14 types even though only types 4 and 5 use `x`.
  - For asymmetric types (`6, 7, 10, 11, 13`), line 738 recomputes `1.0f - exp(-kTwoPif * 10.0f / sampleRate)` (1 division + 1 `exp`) per sample per channel, and line 739 updates `dc[c] += a * (y - dc[c])` without `flushDenormal`.
  - `filt()` (`line 696`) calls `svf[c].process(x, g, k)` (`fx_dsp.h:113`, which divides `1.0f / (1.0f + g * (g + k))` every sample) instead of `Svf::coefficients` + `svf[c].step` (`fx_dsp.h:101-110`).
  - `Distortion::reset()` (`line 673`: `for (auto& s : svf) s.reset(); held[0] = held[1] = 0.0f; counter = 0;`) leaves `dc[2]` and `holdPhase[2]` (`lines 746-747`) unreset (and `counter` is dead state).
  - Line 679 computes `k = 2.0f - 1.9f * clamp01(p.resonance)` (`Q = 1/k` from `0.5` to `10.0`) instead of `docs/engine-spec/06-effects.md:81`'s `Q = 0.5 + 15.5 * r^3` (`Q` from `0.5` to `16.0`).
- **(d) Arithmetic Check**:
  For an asymmetric type with filter active (`order == 1` or `2`), `Distortion::process` executes `2` (`pow10` + `exp`) `libm`/`fastmath` calls and `3` float divisions (`v / d`, `-20*pi / sampleRate`, `1 / (1 + g*(g + k))`) per channel per sample (`4` transcendentals + `6` divisions per stereo sample = `1.92e5` transcendentals/s and `2.88e5` divisions/s at `48 kHz`), all invariant across the block.
- **(e) Verdict**: `CONFIRMED`

---

### Rank 15 — `Equalizer`: Law 0 per-sample SVF division, uncached 32-sample coefficient redesign, un-bypassed 0 dB bands, and Nyquist `tan(0.499 * pi)` cramping

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_eq.h:32-105,140,155`. Exact lines match.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_eq.h:152-158`)**:
```cpp
    static float svfProcess (Sv& s, float u) noexcept
    {
        const float v3 = u - s.ic2;
        const float v1 = (s.ic1 + s.g * v3) / (1.0f + s.g * s.R + s.g * s.g);
        const float v2 = s.ic2 + s.g * v1;
        s.ic1 = flushDenormal (2.0f * v1 - s.ic1);
        s.ic2 = flushDenormal (2.0f * v2 - s.ic2);
```
- **(c) Surrounding Code & Tests**:
  - In Law 0 (`p.law == 0`), `svfProcess` (`line 155`) divides by `(1.0f + s.g * s.R + s.g * s.g)` every sample per active band per channel (`6` divisions/stereo sample for 3 bands, `8` for 4 bands), even though `s.g` and `s.R` are constant across the 32-sample block.
  - `Equalizer::set` (`fx_eq.h:32-59`) runs `designSvf` or `designRb` (`std::pow`, `std::sqrt`, `std::tan`, `std::sin`, `std::cos`) on all 4 bands every 32 samples without comparing `np` against `p`.
  - Bands in shelf/bell mode (`mode == 0`) with `gainDb == 0.0f` still run every sample, and Law 1 (`lines 80-100`) interpolates 5 coefficients per band per sample even when parameters are static (`db == 0`).
  - Line 140 clamps `hz / nyq` to `0.499f` (`g = std::tan(kPif * std::min(hz / nyq, 0.499f))`), whereas `Svf::coefficient` in `fx_dsp.h:99` clamps `hz` to `0.49f * sr`.
- **(d) Arithmetic Check**:
  At `hz / nyq = 0.499f`, `g = tan(0.499 * pi) = 318.3088` and `g^2 = 101,320.5`, so `1.0f + g * R + g^2` loses `log2(101320.5) = 16.63` bits of 24-bit IEEE-754 float32 mantissa precision in the constant `1.0f` term (leaving `~7.4` bits).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 16 — `FxChain`: 0.73 ms fade-in / zero fade-out for instance-0 modules, hardcoded `44100.0f` flush counter, `IntDelay::kMax = 255`, and running muted splitter band racks

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_chain.cpp:456-464,554-569,587-608`, `core/engine-own/fx/fx_module.h:158`, `core/engine-own/fx/fx_latency.h:19-28`. Exact lines match.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_chain.cpp:554-565`)**:
```cpp
        if (fadeIn[m])
        {
            float dl[kControlPeriod], dr[kControlPeriod];
            std::copy (l, l + n, dl);
            std::copy (r, r + n, dr);
            inst[m]->process (l, r, n, t0, period);
            const float invP = 1.0f / static_cast<float> (period);
            for (int i = 0; i < n; ++i)
            {
                const float t = clamp01 (static_cast<float> (t0 + i + 1) * invP); // the mix starts at 0 and reaches its value across the first period
                l[i] = dl[i] + (l[i] - dl[i]) * t;
                r[i] = dr[i] + (r[i] - dr[i]) * t;
```
- **(c) Surrounding Code & Tests**:
  - Instance-0 modules (`inst[0..12]`) fade in over 1 control period (`period = 32` samples at `fx_chain.cpp:554-569`, implementing `docs/engine-spec/06-effects.md:17`), and when switched OFF or reordered via `fx.order` (`fx_chain.cpp:456-464`), they stop running immediately on the next control period with zero fade-out (unlike pooled slots in `FxChain`, which crossfade over `kPoolCrossfade = 128` samples via `tail[]` at `fx_chain.cpp:635-652`).
  - `FxModule::update` (`core/engine-own/fx/fx_module.h:158`) computes `flushLeft = static_cast<int>(flushSeconds() * 44100.0f)` with hardcoded `44100.0f` (`FxModule` has no `sampleRate` member), under-counting flush samples at `48 / 88.2 / 96 kHz`.
  - `IntDelay` (`core/engine-own/fx/fx_latency.h:19-28`) has `kMax = 255`, clamping any bus latency alignment `> 255` samples (`std::clamp(d, 0, kMax)` at `line 26`) and calling `clear()` whenever `d != delay`.
  - In `FxChain::runModule` (`fx_chain.cpp:583-610`), `runRack()` is unconditionally executed for all splitter bands before `splitLh.merge` / `splitLmh.merge` / `splitMs.merge` checks `lowBypass_` / `midBypass_` / `highBypass_` / `sideBypass_` and discards the muted band's buffer.
- **(d) Arithmetic Check**:
  Instance-0 fade-in duration: `32 / 44100 = 0.7256 ms` (`32 / 96000 = 0.3333 ms`); at `96 kHz`, `flushLeft = int(flushSeconds() * 44100)` runs for `44100 / 96000 = 0.4594x` the intended duration (`54.1%` short); `3` series `Compressor` Limit instances at `96 kHz` have latency `3 * lround(0.0015 * 96000) = 3 * 144 = 432 > 255` samples (`177` samples truncated by `IntDelay::kMax`).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 17 — `Chorus` fixed `0.53f` wet sum across `voices = 1..4`, `Phaser` `bpA`/`bpB` denormals + per-sample `depth2` trig/divisions, and missing transport `songBeats` LFO sync

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_effects.h:270-271,602,614-625,651-652`, `core/engine-own/fx/fx_modules.cpp:140,427,472`. Exact lines match.
- **(b) Decisive Code Lines (`core/engine-own/fx/fx_effects.h:620-625`)**:
```cpp
                        for (int s = 0; s < kStages; ++s)
                        {
                            const float hs = std::max (hz * std::exp2 (p.depth2 * (static_cast<float> (s) / 11.0f - 0.5f) * (1.0f / 12.0f)), 5.0f);
                            const float g = phaserStageG (hs, sampleRate);
                            hS[c][s] = g / (1.0f + g);
                        }
```
- **(c) Surrounding Code & Tests**:
  - `Chorus::process` (`fx_effects.h:270-271`) applies a fixed `0.53f * acc[c]` scale regardless of `V = std::clamp(p.voices, 1, 4)` (a deliberate calibration noted in `docs/engine-spec/06-effects.md:37-38,333`, which causes wet output level to rise with voice count).
  - `Phaser::process` (`fx_effects.h:651-652`) updates feedback bandpass states `bpA[c]` and `bpB[c]` without `flushDenormal` (unlike `z[c][s]` at `line 644`), divides `p.rate / sr` every sample (`line 602`), and when `!fast && spread` (`lines 608-625`) calls `std::exp2` (ignoring `fastmath::exp2` at `line 622`), `phaserStageG` (`std::tan` + division), and `g / (1.0f + g)` for all 12 stages per channel per sample (`std::exp2` of the constant spread factor is not hoisted outside the sample loop).
  - `ChorusModule::configure` (`fx_modules.cpp:140`), `FlangerModule::configure` (`line 427`), and `PhaserModule::configure` (`line 472`) convert tempo-synced divisions into free-running `rate` (Hz) and ignore `f.songBeats` (`fx_module.h:57`), even though `docs/engine-spec/06-effects.md:38,181` specifies transport beat-locked LFO phase.
- **(d) Arithmetic Check**:
  `Chorus` wet gain from `V = 1` (2 taps/ch) to `V = 4` (8 taps/ch) increases by `10 * log10(4) = +6.02 dB` (uncorrelated) to `20 * log10(4) = +12.04 dB` (coherent at `depth = 0`); `Phaser` with `!fast && spread` executes `(1 + 12) = 13` `exp2`, `2` `exp` (`OnePole::coefficient`), `12` `tan` (`phaserStageG`), and `2 + 12 * 2 = 26` float divisions per channel per sample (`54` transcendentals + `52` divisions per stereo sample = `2.592e6` transcendentals/s and `2.496e6` divisions/s at `48 kHz`).
- **(e) Verdict**: `CONFIRMED`

---

### Rank 18 — `FxChain` & `OwnEngine`: `muteSkip` `wetMix()` omissions, unconditional 20 s pool slot `echoes` hold, and sub-block-dependent `makeupOfBus` smoothing

- **(a) Cited & Corrected `file:line`**: Cited `core/engine-own/fx/fx_modules.cpp:186,584,639,691,779`, `core/engine-own/fx/fx_chain.cpp:508-511`, `core/engine-own/own_engine.cpp:349,376,1710`. Also checked `core/fx-serum-a/src/hyper_dimension.cpp:116-119`, `core/fx-serum-a/src/utility.cpp:73-76`, `core/engine-own/fx/fx_module.h:150`, and `core/include/synthyboy/quality.h:112`.
- **(b) Decisive Code Lines (`core/fx-serum-a/src/hyper_dimension.cpp:116-119`)**:
```cpp
    const bool isBypassed = (targetHyperMix == 0.0f && targetDimMix == 0.0f && targetLevel == 1.0f
                             && currentHyperMix_ == 0.0f && currentDimMix_ == 0.0f && currentLevel_ == 1.0f);
    if (isBypassed)
        return;
```
- **(c) Surrounding Code & Tests (Why PARTLY WRONG)**:
  - **What is confirmed**:
    1. `FxChain::update` (`core/engine-own/fx/fx_chain.cpp:508-511`) sets `s.echoes = true` for every active pool slot regardless of module type, causing `FxPool::retire` (`lines 91-92`) to hold retired non-echoing modules (`Distortion`, `Equalizer`, `Utility`, `Phaser`, `Compressor`) for `20.0 * sampleRate` samples before resetting/reclaiming them.
    2. `OwnEngine::makeupOfBus` (`core/engine-own/own_engine.cpp:349,376`) advances `fxMakeupSmooth[bus] += (target - fxMakeupSmooth[bus]) * alpha` once per sub-block `n` (where `alpha` at `line 470` is calibrated for a fixed 32-sample `kControlPeriod`), and lines `1710-1718` linearly ramp `mk` across `n` samples, making the time constant and ramp slope vary whenever mid-period MIDI events split `n < 32`.
    3. `CompressorModule` (`fx_modules.cpp:186`, when not in Limit mode so `latency() == 0`) and `ConvolveModule` (`fx_modules.cpp:779`, which is masked out on tier `low` by `quality.h:112` but runs on MPC XL's `"high,voices=16,control=economy"` profile where `muteSkip` is enabled) do not call `wetMix()`, preventing `FxModule::update` from muting them at `mix == 0`.
  - **What is wrong**:
    1. **`HyperModule` and `UtilityModule` already bypass at `mix == 0` on all tiers**: `HyperDimension::process` (`core/fx-serum-a/src/hyper_dimension.cpp:116-119`) and `Utility::process` (`core/fx-serum-a/src/utility.cpp:73-76`) both return immediately at the top of `process()` when their target and current mixes are `0.0f` and `levelDb == 0.0f` (`HyperModule` and `UtilityModule` always pass `p.levelDb = 0.0f` at `fx_modules.cpp:587,640`), as documented in `docs/release/CPU-REVIEW-FILTER-FX.md:355-356`. In addition, `HyperDimension` is masked out on tier `low` (`quality.h:112`).
    2. **`BodeModule` cannot be skipped by `muteSkip` even if it called `wetMix()`**: `FxModule::update` (`core/engine-own/fx/fx_module.h:150`) sets `muted = f.muteSkip && mixNow == 0.0f && mixBefore == 0.0f && latency() == 0;`. Because `BodeModule::latency()` is `64` (`!= 0`), `FxModule` explicitly refuses to mute it (so its internal dry delay alignment is preserved, which is verified by `core/tests/test_own_fx_mute.cpp:255`), and `Bode` is also masked out on tier `low` (`quality.h:112`).
- **(d) Arithmetic Check**:
  Pool hold window: `int(20.0 * 48000) = 960,000` samples (`20.0 s`) per retired pool slot (`kSlots = 16`); `fxMakeupAlpha = 1 - exp(-32 / (0.015 * 48000)) = 0.04347` (`tau = 15 ms` at `n = 32`), which shortens to `tau = 15 ms / 32 = 0.469 ms` (`32x` faster) when `n = 1`.
- **(e) Verdict**: `PARTLY WRONG`

---

### Rank 19 — `OutputStage` & `master::stage`: 1× discrete sample-peak detection, 1× unlinked `master::outputLimit`, and `OUTPUT-LEVEL.md` B6 table contradiction

- **(a) Cited & Corrected `file:line`**: Cited `core/include/synthyboy/output_stage.h:52,212-216`, `core/engine-own/dsp/master_stage.h:26-32`, `docs/release/OUTPUT-LEVEL.md:24,62`. Also checked `core/include/synthyboy/output_stage.h:332` and `core/engine-own/dsp/master_stage.h:54-55`.
- **(b) Decisive Code Lines (`core/include/synthyboy/output_stage.h:212-219`)**:
```cpp
    float detect (float p) noexcept
    {
        float rq = 1.0f;
        if (p > kCeiling)
            rq = kCeilingSafe * recipBelow (std::min (p, kMaxPeak));
        // the reduction 1 - gain is what the detector keeps (fine resolution near no reduction, where a gain near 1 would get stuck one float step under it)
        const float rd = 1.0f - rq;
        win[j] = rd;
```
- **(c) Surrounding Code & Tests**:
  - `OutputStage::process` (`core/include/synthyboy/output_stage.h:332`) feeds `detect(std::max(std::fabs(a), std::fabs(b)))` using 1× discrete sample peaks against `kCeiling = 0.94406088f` (`-0.5 dBFS`), with no 4× oversampled inter-sample True-Peak filter (note: this is an explicit design choice documented in `docs/release/OUTPUT-LEVEL.md:83` and `docs/review/OUTPUT-STAGE-SECOND-REVIEW-2026-11-19.md:36-38`, where `-0.5 dBFS` sample ceiling provides `0.4 dB` margin below `-0.1 dBTP` for typical signals, though worst-case high-frequency inter-sample peaks can exceed `-0.1 dBTP`).
  - `master::outputLimit` (`core/engine-own/dsp/master_stage.h:26-32,54-55`) applies `1.8f + 0.2f * fastTanh((a - 1.8f) * 5.0f)` for `|y| > 1.8f` at 1× sample rate independently to `left[i]` and `right[i]` before `OutputStage`.
  - In `docs/release/OUTPUT-LEVEL.md`, the header (`line 6`) and section A7 (`line 24`) specify `T = -10.0 dBFS` (matching `kTargetRms = 0.31622777f` in `output_stage.h:59`), whereas section B6 (`line 62`) still tabulates `T | -4.50 dBFS (0.5957)` from an earlier revision.
- **(d) Arithmetic Check**:
  Two consecutive 1× samples at `kCeiling = 0.94406088` (`-0.50 dBFS`) at `f = fs / 4` (`45 deg` phase) have a continuous reconstruction peak of `0.94406088 * sqrt(2) = 1.33510` (`+2.51 dBTP`); `master::outputLimit` knees at `1.8f` (`20 * log10(1.8) = +5.105 dBFS`) and asymptotes to `2.0f` (`+6.021 dBFS`).
- **(e) Verdict**: `CONFIRMED`

---

## Seen in Passing

1. **`core/fx-serum-b/convolve.cpp:105-116`** — `Convolve::reset()` is marked `noexcept` and documented in `core/fx-serum-b/convolve.h:73` as *"Clear runtime delay buffers (RT-safe)"*, but calls `std::vector::assign` on `m_tailFftHistL` and `m_tailFftHistR`, constructing temporary `std::vector<Complex>(kFftSize)` objects and heap-allocating on the audio thread when `FxChain` resets modules.
2. **`core/fx-serum-b/convolve.cpp:391`** — In `Convolve::convertToMinimumPhase`, the non-Serum branch (`!serumLaw`) computes `fftSize = nextPow2(N)` without `2x` zero-padding and caps `fftSize` at `65536`, causing time-aliasing of the cepstrum and silently truncating any impulse response longer than `65,536` frames (`1.36 s` at `48 kHz`) when `phiMin` is enabled with `law != 1`.
3. **`core/engine-own/fx/fx_effects.h:89,104` vs `core/engine-own/fx/fx_modules.cpp:226`** — `Delay::process` multiplies `tL = clamp(p.timeL, 0.0005f, 4.0f) * p.offsetL`, but then clamps `fLast[0]` and `fLast[1]` back to `4.0f` seconds (`line 104`, and `prepare` at `line 68` only allocates `4.2 * sr` samples), so `offsetL`/`offsetR > 1.0f` (`spread < 0` or `> 0`) saturates at `4.0 s` while `DelayModule::flushSeconds()` (`fx_modules.cpp:226`) assumes delays up to `16.0 s`.
4. **`core/engine-own/fx/fx_chain.cpp:117-125`** — `FxPool::retire(FxModule* m)` drops `m` on the floor if all 4 `garbage[4]` slots are currently non-null (i.e., if more than 4 module type changes happen before the background worker calls `collect()`), permanently leaking the `FxModule` allocation.

---

Nothing under the forbidden paths (`core/engine-vital/`, `core/vital-import/`, or `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/`) was opened.
