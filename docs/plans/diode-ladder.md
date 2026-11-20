# Plan: the diode ladder filter: zero-delay feedback, cutoff above 10.67 kHz, fewer divisions (audit part 1, rank 1 remainder and rank 26)

Status: plan only, written from code and document reading. No build, no test, no benchmark, no ssh was run. Branch `worktree-agent-af92f8a651da82ff9`, base `6069a30f` (develop). The only file this item's plan work creates is this one.
Covers: `docs/review/OWN-ENGINE-DSP-AUDIT-1-VOICE-PATH-2026-11-20.md` rank 1 (the `0.95` clamp that stayed in the diode ladder) and rank 26 (scalar divisions of the classic Ladder and Diode, missing NEON pair kernels), corrected by `docs/review/OWN-ENGINE-DSP-AUDIT-1-VERIFICATION-2026-11-20.md` (rank 1 at :56-76, rank 26 at :722-745) and `TODO.md:64`.

Conventions: `A:l` = file:line that was opened for this plan. "block" = 128 frames at 44.1 kHz = 128 / 44100 = 2.90249 ms (`docs/MPC.md:321` and the perf plan use 2.902 ms). "points" = percentage points of that block. "Key 61" = Akai MPC Key 61 (ARMv7 Cortex-A17, 1.608 GHz, tier `low`).

**How the numbers of this document were made.** Every `file:line` was opened. The loop-stability, tuning and peak numbers in sections 1 and 3 are computed from the equations of the code (derived in section 3) with a throwaway calculator script (plain Python complex arithmetic, kept in the session scratchpad, not in the repository, not a test of the repository's code); they are therefore analytic small-signal numbers, marked "calc". Anything that only a build or a device can give is marked HYPOTHESIS together with the measurement that decides it (section 2).

**Two filters carry the name "diode" in this code base, and the task text mixes them up.** (1) `DiodeLadder` in `core/filters-serum/src/filter_dsp.h:216-259`, rendered as the Serum type `AcidLadder` (model 26 "New", style 1): this is where the `0.95` clamp is. Its four stages are NOT coupled: it is a cascade of four trapezoidal one-poles (`filter_dsp.h:250-256`). (2) `ClassicFilter::diode` in `core/engine-own/voice/classic_filters.h:771-854` (filter model 4 "Diode", the one the two shipped Diode presets use): its four stages ARE coupled (tridiagonal system, Thomas algorithm, `:792-799,845-853`), it has no `0.95` clamp, and its loop also has a one-sample delay (`:826-836`, section 1.3). The finding of rank 1 is about (1); rank 26 names both. This plan treats (1) as the main item (sections 3.1-3.4) and (2) as a measured, gated second item (section 3.5), because it changes the sound of two shipped presets.

---

## 1. The problem, as verified

### 1.1 What the code does today

| # | What | Where (opened) |
|---|---|---|
| a | `DiodeLadder::process (in, g, res, drive, sampleRate)` clamps the integrator gain `gSafe = max (1e-5, min (g, 0.95))` and derives `G = gSafe / (1 + gSafe)` every sample (one division per sample). | `core/filters-serum/src/filter_dsp.h:234-235` |
| b | The fed-back signal is `s[3]`, the INTEGRATOR STATE of stage 4 as it stood before this sample's update, not the stage output. The state update of a trapezoidal one-pole is `s <- v + y = 2 y - s` (`v = G (x - s)`, `y = v + s`). So the feedback sees `y4` through a delay (derivation in 3.1). | `filter_dsp.h:243-245` (feedback), `:250-256` (stages, `s[i] = sanitize (v + y)` at `:255`) |
| c | A 30 Hz high-pass sits in the feedback: `gHp = tan (pi 30 / sampleRate)` is computed EVERY sample (a libm `tanf` call), clamped to `[1e-5, 0.5]`, then `GHp = gHpSafe / (1 + gHpSafe)` (another division per sample); the high-pass is `fbHP = s3 - lp (s3)` with the TPT low-pass state `hpState`. | `filter_dsp.h:238-245` |
| d | Input and loop: `u = softClip (driveGain in - k softClip (fbHP))`, `driveGain = 1 + 4 drive`, `k = 3.82 res`. Output is the stage-4 output `y`. `softClip (x) = x (27 + x^2) / (27 + 9 x^2)` for `|x| < 3`, else `+-1` (one division). | `filter_dsp.h:236,247-248,257`; `softClip` at `:39-47` |
| e | Per sample and per channel that is 4 divisions (`G`, `GHp`, and the two `softClip` calls) and 1 `tanf`. Arithmetic from the lines above: `G` (:235) + `GHp` (:241) + `softClip (fbHP)` + outer `softClip` (:248) = 4; `std::tan` (:239) = 1. (Early-return branch of `softClip` for `|x| >= 3` has no division; the count is for the normal range.) | same |
| f | The caller: `FilterImpl::processInternal`, `case FilterType::AcidLadder: rawOut = diodeLadder_.process (drivenIn, g, res, drive, srFloat);` with `drivenIn = softClip ((1 + 2.5 drive) in)`. `g = tanM (pi cutoff / sr)` with the cutoff clamped to `[20 Hz, 0.49 sr]`, so `g <= tan (0.49 pi) = 31.8` at the top. | `core/filters-serum/src/filter.cpp:1579-1584`, `:860-862`, `:138,160,192`; member `diodeLadder_` `:829`, reset `:72` |
| g | The other ladders were fixed: `Ladder4Pole` now clamps at `50` and says in a comment that the diode ladder keeps `0.95` "(unit delay in its feedback)"; the pair kernel's ladder branch the same. | `filter_dsp.h:168`, `filter.cpp:407`; commits `2fb2eeaa` (fix), `361b216d` (comment) |
| h | The diode ladder has NO NEON pair kernel: `processPairT` flags `scream`, `ladder` (= `EMSLadder || MGLadder` only), `chain`, `multi`, `flanges`, `ring` and refuses (returns `false`, so the caller runs two scalar `process` calls) every other type. | `filter.cpp:208-224` |
| i | `FilterUnit` reaches `DiodeLadder` only through the Serum model 26 style 1: `classic = s.model < filters::kFirstSerumModel` (= 8) sends models 0-7 to `ClassicFilter`, so the `case 4: ... AcidLadder` of `typeFor` is never used for a classic model (`typeFor` is only called when `classic` is false). | `core/engine-own/voice/filter_unit.h:77,105,115`; `core/filters-serum/include/synthyboy/filters/filter_types.h:92-93` (`MGLadder`, `AcidLadder`), `kFirstSerumModel = 8` at the same file |

### 1.2 What it costs and how it sounds (numbers)

**The cutoff ceiling.** `g <= 0.95` caps the cutoff at `atan (0.95) / pi fs = 0.75976 / 3.14159 fs = 0.24184 fs` = 10665 Hz at 44.1 kHz, 11608 Hz at 48 kHz (verification `:75`). Seen on the real shape of the filter (a cascade of four identical one-poles, `res = 0`, `-3 dB` point; calc, formula in 3.2: `f_-3dB = fs / pi atan (0.43498 g)`, because four poles at `Omega = g` drop `-3 dB` at `Omega = g sqrt (2^(1/4) - 1) = 0.43498 g`):

| knob cutoff | `g` | today `-3 dB` point | with the clamp lifted |
|---|---|---|---|
| 8 kHz @44.1 | 0.6408 | 3816 Hz | 3816 Hz (identical: below the clamp) |
| 12 kHz @44.1 | 1.1495 | 5501 Hz | 6509 Hz |
| 16 kHz @44.1 | 2.1748 | 5501 Hz | 10635 Hz |
| 19 kHz @44.1 | 4.5298 | 5501 Hz | 15457 Hz |
| 36 kHz @96 | 2.4142 | 11974 Hz | 24747 Hz |
| 42 kHz @96 | 5.0273 | 11974 Hz | 34894 Hz |

(The `-3 dB` point of a four-pole is lower than the knob by construction; the spec says the same of the model: `docs/engine-spec/05-filters.md:304-305`. What is wrong is that above 10.67 kHz the knob does nothing: rows 12 / 16 / 19 kHz are one value.) At 96 kHz the clamp binds at `atan (0.95) / pi 96000 = 23217 Hz` (the verification and `test_filters_serum.cpp:1080`).

**Why the clamp cannot simply be lifted (the TODO's statement, now quantified).** The feedback has the delay of 1.b. In the loop `L = k HP (Omega) / (1 + j Omega / g)^4 F (w)` the delay adds `F (w) = e^(-j w/2) / cos (w/2)` (derivation in 3.1): half a sample of phase lag AND a magnitude boost `1 / cos (w/2)`. The linear loop oscillates when `k` exceeds `k_thr`, the value where `L` crosses the negative real axis. Calc, 44.1 kHz, `k_max = 3.82` (res 100 %), 30 Hz high-pass included:

| knob cutoff | `g` used today | `k_thr` today (delayed loop) | resonance at which it self-oscillates (`k_thr / 3.82`) | frequency it rings at | `k_thr` if the loop were delay-free |
|---|---|---|---|---|---|
| 1 kHz | 0.0714 | 3.84 | never (3.84 > 3.82) | 981 Hz | 4.12 |
| 2 kHz | 0.1434 | 3.53 | 92 % | 1886 Hz | 4.06 |
| 5 kHz | 0.3721 | 2.88 | 75 % | 4343 Hz | 4.02 |
| 8 kHz | 0.6408 | 2.36 | 62 % | 6585 Hz | 4.01 |
| 10 kHz | 0.8637 | 2.05 | 54 % | 8058 Hz | 4.01 |
| 10.67 kHz and above | 0.95 (clamp) | 1.945 | 51 % | 8556 Hz | 4.01 |
| 12 kHz, clamp simply lifted | 1.1495 | 1.75 | 46 % | 9580 Hz | 4.01 |
| 16 kHz, clamp simply lifted | 2.1748 | 1.14 | 30 % | 13064 Hz | 4.00 |
| 19 kHz, clamp simply lifted | 4.5298 | 0.63 | 17 % | 16552 Hz | 4.00 |
| 21.6 kHz (0.49 fs), clamp simply lifted | 31.82 | 0.10 | 2.7 % | 21039 Hz | 4.00 |

So today (a) the resonance of the filter depends strongly on the cutoff (the same knob position is a clean peak at 1 kHz and a sustained oscillation at 5 kHz), (b) it rings 13 % flat at 5 kHz (4343 against 5013 Hz, the delay-free crossing) and 19 % flat at 10 kHz, and (c) the shipped constant `3.82` was clearly chosen against the delay-free limit `4` (the analog threshold of four equal poles: `k = 4` at `Omega = g`, `(1/sqrt 2)^4 = 1/4`), not against the delayed loop. `softClip` bounds `u` to `[-1, 1]` (`:248`), so none of this blows up (the test `serum_ladder_cutoff_t3_stability` style limits would hold); it is a tuning and sound problem, not a crash. Small-signal peak of the magnitude response (calc; only rows where the loop is stable): 1 kHz res 100 %: today 36.5 dB, delay-free 13.5 dB; 10 kHz res 50 %: today 16.8 dB at 7984 Hz, delay-free 0 dB (no peak); 15 kHz res 50 %: today 28.3 dB at 8537 Hz (the clamp), delay-free 0 dB; res 80 % at 5 kHz and above: today the loop is unstable (rings), delay-free 3.8 .. 3.9 dB; res 100 %: delay-free 13.5 .. 17.8 dB at every cutoff from 1 to 15 kHz.

**CPU.** From the line counts of 1.1.e: 4 divisions and 1 `tanf` per sample and channel. HYPOTHESIS (no measurement exists, section 2 M1 builds it): a VFP `vdiv.f32` of the Cortex-A17 takes about 14 cycles and is not pipelined, and a `tanf` call about 100-200 cycles; that would be `4 x 14 + 150 = ~206` cycles per sample and channel before the 4 stages. For the worst case of 8 voices with filter 1 on the diode ladder, 2 channels, 44100 samples/s: `206 x 44100 x 16 = 145 M cycles/s = 9.0 % of one 1.608 GHz core` = 9.0 points of every block (HYPOTHESIS; deciding measurement: M1 `--sections` filter section of the acid load). The section 3 design removes the `tanf` and two divisions and adds none (4 divisions, section 3.3), so it should be cheaper than today, not dearer.

**Memory.** `DiodeLadder` is `float s[4] + float hpState` = 20 bytes (`filter_dsp.h:218-219`) inside `FilterImpl` (`filter.cpp:829`); the design adds about 14 cached floats per channel (section 3.3) = 56 bytes per `FilterImpl`; 8 voices x 2 filters x 2 channels = 32 `FilterImpl` = 1.8 KB. Negligible (the memory item is `docs/plans/memory-footprint.md`).

### 1.3 The classic Diode (model 4), same question

`ClassicFilter::diode` solves the stage coupling implicitly (Thomas algorithm on `M = tridiag (-g lo, 1 + g, -g up)`, `classic_filters.h:792-799,845-853`) but takes the feedback from `dv[3]`, the stage-4 voltage of the PREVIOUS sample (`:826-836`: the 20 Hz high-pass of `dv[3]`, `u = th (0.5 (dIn xp - k kDiodeFb fb)) kDiodeGain`), so its loop also has a full sample of delay. The comment at `:769` says "zero delay feedback inside the ladder", which is true of the stage coupling only. Cutoff: `fc = clamp (f 0.765, 5 Hz, 0.45 sr)` (`:781`, `kDiodeCorner = 0.765` at `:27-28`), no `0.95` clamp, so the model reaches up to `0.765 x 20 kHz = 15.3 kHz` (`g = 1.9`) without the ceiling problem. Calc (small-signal, linearised `sat`, equations in 3.5, loop gain `K = 0.28 x 0.5 x 9.8 k = 1.372 k`, `k = 0.7 + 16.3 r^3`): the threshold `K_thr` for self-oscillation:

| knob cutoff | `K_thr` today (one-sample delay) | `K_thr` delay-free | note |
|---|---|---|---|
| 420 Hz | 43.5 | 45.6 | |
| 1 kHz | 38.8 | 43.0 | |
| 3.1 kHz | 31.6 | 41.8 | |
| 8 kHz | 22.9 | 41.5 | |
| 12 kHz | 18.5 | 41.4 | `K` at r = 1 is 23.3: rings today, not delay-free |
| 20 kHz | 12.2 | 41.3 | rings from r = 0.79 today |

At the two shipped presets (`Diode Saturated Bass`: filter 1 model 4, cutoff 420, resonance 20, drive 20; `Diode Bite Lead`: cutoff 3100, resonance 45, drive 20; from the preset files, section 1.4) `K = 1.372 (0.7 + 16.3 r^3) = 1.14` and `3.00`; the peak gain `1 / (1 - K / K_thr)` is `1.027 / 1.026` and `1.105 / 1.077` (today / delay-free): 0.01 dB and 0.22 dB apart. So the delay matters for the classic Diode only at high cutoff with high resonance, and the shipped presets sit where it does not.

### 1.4 Which presets use which

Scan of all 160 `content/presets/**/*.sbpreset` (key `filterN.model` / `filterN.style`, N = 1, 2; Python read of the JSON): **no preset uses the Serum Acid Ladder** (model 26: zero presets use model 26 at all). The two presets with the classic Diode (model 4) are `Bass/diode_saturated_bass.sbpreset` (filter 1, on, style 0, cutoff 420, resonance 20, drive 20, keytrack 0) and `Lead/diode_bite_lead.sbpreset` (filter 1, on, style 0, cutoff 3100, resonance 45, drive 20, keytrack 0). `Diode Saturated Bass` is on the Key 61's expensive list: 80.9 % p99 under `low` (`docs/MPC.md:321`) and average / p99 / worst 1.44 / 1.66 / 2.13 ms with 8 notes (`docs/MPC.md:95`); `Diode Bite Lead` is in the heavy list of the bench (`bench/tier_bench.cpp:198`). Other references to the Acid Ladder (no sound): UI name lists (`shell-juce/ui/panels/FilterModels.h`), `core/serum-import/serum_map.cpp`, `core/filters-serum/CATALOGUE.md:148`, tests `core/filters-serum/tests/test_filters_serum.cpp:126`.
**Consequence: lifting the clamp and changing the loop of the serum `DiodeLadder` changes no shipped sound and no refcompare baseline of a shipped preset.** Changing the classic Diode changes two shipped presets (section 3.5).

### 1.5 What was wrong or already done in the audit

* Rank 1: confirmed (verification `:56-76`). Already done: `Ladder4Pole` and the pair kernel's ladder (clamp 50, `2fb2eeaa`, tests `serum_ladder_cutoff_t1..t5` at `test_filters_serum.cpp:826-1120`). Remaining: only `DiodeLadder` (`TODO.md:64`). The audit's cure "raise `gSafe` to 31.8 or 20" is wrong for the diode ladder: it would let the delay grow with `g` (section 1.2): with only the clamp lifted the loop rings from resonance 46 % at 12 kHz, 30 % at 16 kHz, 17 % at 19 kHz and 2.7 % at 0.49 fs (`k_thr` 1.75 / 1.14 / 0.63 / 0.10, calc).
* The audit's phrase "convert `DiodeLadder` to ZDF so it stays tuned" is right, but the audit (and the task text) describe it as a coupled-stage ladder; the stages of this code are an uncoupled cascade (1.1.d), so the instantaneous solve is the 4-pole ladder's solve plus the feedback high-pass, not a tridiagonal one (3.1). The coupled solve belongs to model 4 only.
* Rank 26: confirmed code, consequence unproven (verification `:745`). Facts re-checked here: `sat` is `v - 0.9 v^3 / (v^2 + 3)` (`classic_filters.h:145`), 4 calls per sample in `ladderRun` (`:627,635`) and `ladderPairLoop` (`:733-742`), 4 in `diode` (`:838`); the pair kernel (`processPair`, `:373-391`) exists only for models 0-3 and calls `processPairLadder` for the Ladder, and has no Diode form. The audit's "missing pair kernels for Normal, MGDirty, Formant" is a different item (not diode); it is listed in section 6 as not covered.
* Rank 9 overlap: the per-sample `tanf` of `DiodeLadder` (`filter_dsp.h:239`) is also rank 9's item; this plan removes it inside the same function (task A2) so no other plan should touch `DiodeLadder`.

---

## 2. The measurement to make BEFORE any change

There is no tool that measures the Serum diode ladder in a shipped load (no preset uses it) and no micro-benchmark for voice filters (`tier_bench --micro` covers only the Spectral, Granular and SampleVoice kernels, `bench/tier_bench.cpp:582-776,1079-1109`, per `docs/plans/perf-measurement.md` 1.1.g). So the first batch builds the missing inputs (tasks Z1, Z2, section 4) and the build worker records the numbers below once, on the unchanged `develop`.

**M1. Response and ringing of the real Acid Ladder (desktop build, no device).** Tool: `synthyboy_filter_response` (`tools/filter-response/filter_response_tool.cpp:1-10`; it measures the real `FilterUnit` at the requested rate on a 1e-3 impulse, `core/include/synthyboy/filter_response.h:24-37`, and accepts `--set KEY=VALUE`). Base preset: `content/presets/Bass/acid_saw_reso.sbpreset` (filter 1 on, keytrack 0). For each cutoff in `1000 2000 5000 10000 15000 19000` and each resonance in `0 50 80 100`:
`synthyboy_filter_response content/presets/Bass/acid_saw_reso.sbpreset --filter 1 --sr 44100 --points 512 --fmin 100 --fmax 21000 --set filter1.model=26 --set filter1.style=1 --cutoff <C> --resonance <R> > acid_<C>_<R>.csv`
and once with `--sr 96000` for cutoffs `16000 24000 36000 42000`. Record per file: the frequency and gain of the maximum, and the frequency where the gain first falls 3 dB below the value at 100 Hz. Expected today (calc, to be confirmed): res 0 `-3 dB` points of the table in 1.2 (5501 Hz for the three top cutoffs); res 100 peak frequencies 981 / (rings) / ... as in 1.2. If a row does not match the calc by more than 5 %, stop and correct this plan (the calc assumes a linear loop; the tool's 1e-3 impulse stays linear, but a self-oscillating loop grows until the clipper limits it, so rows with `k > k_thr` show the limit cycle, not a small-signal peak).

**M2. CPU of the diode filters on the devices.** Tool: `synthyboy_tier_bench` (usage `bench/tier_bench.cpp:6-30`), once after task Z2 provides the acid load. Commands (Key 61, ssh per `.agents/skills/akai-mpc-ssh-deploy/SKILL.md`; XL the same with `--core 6`):
`synthyboy_tier_bench --presets content/presets --config low --core 3 --load "Diode Saturated Bass" --load "Diode Bite Lead" --load bench/presets/acid-ladder-bench.sbpreset --load "Acid Saw Reso" --seconds 8 --repeat 3 --budget 70`
and, with the section-timing build (`-DSYNTHYBOY_SECTION_TIMING=ON`, `core/CMakeLists.txt:53-55`), the same with `--sections`. Record per load: median and p99 of the block time in percent of 2.902 ms, and the `filter1` section in ms per block. `Acid Saw Reso` is the classic Ladder (model 2), kept as the comparison load for rank 26's classic Ladder. HYPOTHESIS decided by this: the serum diode ladder costs 9 points of a block at 8 notes (1.2); the classic Diode's divisions are a measurable part of `Diode Saturated Bass`'s filter section. Decision rules are in section 4 (gates G1, G2).

**M3. Loop delay of the classic Diode (desktop).** `synthyboy_filter_response content/presets/Lead/diode_bite_lead.sbpreset --filter 1 --sr 44100 --points 512 --cutoff <C> --resonance <R>` for `C` in `420 3100 8000 12000 20000` and `R` in `20 45 80 100`; record the peak gain and frequency. Calc expectation (1.3): at 3100 Hz / 45 % within 0.3 dB of the delay-free model, at 12 kHz / 100 % self-oscillating (a limit cycle, `K = 23.3 > 18.5`).

**M4. The guards that must already pass on `develop`.** `synthyboy_test_filters_serum` (the T1-T5 ladder tests and `serum_filters_magnitude_response`, which includes `AcidLadder` at cutoffs 200 / 1000 / 4000 Hz, `test_filters_serum.cpp:104-135`), `synthyboy_test_filter_pair`, `synthyboy_test_own_filter_response`, `synthyboy_test_own_filter_styles` (Diode styles, `core/tests/test_own_filter_styles.cpp:52`), `synthyboy_test_own_ladder_range`. Record the pass counts; they are the baseline for section 5.

---

## 3. Design

### 3.0 What the specification fixes (kept as it is)

* `docs/engine-spec/05-filters.md:363` only names the Acid Ladder (model 26, style 1); there is no numeric specification of it. Its description is `core/filters-serum/CATALOGUE.md:148`: "TB-303 diode ladder model with HP feedback ... 303 cutoff frequency | 303 squealy resonance | Diode distortion ... Bass roll-off compensation". Kept: four low-pass stages, the 30 Hz high-pass in the feedback (the "bass roll-off compensation"), `k = 3.82 res`, input gain `1 + 4 drive`, the two `softClip` in the loop, the output at stage 4, the cutoff clamp `[20 Hz, 0.49 sr]` of the caller (`filter.cpp:138,160`). Not touched: the constants.
* The classic Diode (model 4) IS specified: `docs/engine-spec/05-filters.md:220-227`: "Four stages of the TB-303-style diode ladder (each stage averages with its neighbours: `s2 <- (s1 + s3)/2` etc., the algebraic saturator in each), feedback `k` from stage 4, input `tanh(0.5 (d' hp - k s4))` ... a fixed 20 Hz feedback high-pass in the loop. Resonance `k = 0.7 + 16.3 r^3`; drive `d' = (k/2 + 1) d`; post `1/sqrt(d)`." and `:304-305` "(the Diode's response is that of a 4-pole low-pass at a lower corner than the knob (-29 dB at 1 kHz) ... a quirk to reproduce)", acceptance tolerance `:308` "+-3 dB for ... Diode". The spec does not say the loop has a delay; it says feedback from stage 4. A delay-free loop is therefore compatible with the spec; any change must stay within `+-3 dB` of the measured response at the table points of `:295-296`.

### 3.1 The equations (derived from the code)

Notation: `G = g / (1 + g)`, one trapezoidal one-pole stage `v = G (x - s)`, `y = v + s`, `s <- v + y` (`filter_dsp.h:253-255`), hence `y = G x + (1 - G) s` and `s <- 2 y - s`. In the z domain a stage is `H (z) = G (1 + z^-1) / (1 - (1 - 2 G) z^-1)`, which is the analog `1 / (1 + Omega / g)` at `Omega = tan (w / 2)` (bilinear transform; with `g = tan (pi fc / fs)` the pole is exactly at `fc`).

**Today's loop.** The feedback reads `s3` before the update (`:243`). From `s <- 2 y - s`: `S3 (z) = 2 z^-1 / (1 + z^-1) Y4 (z)`, i.e. `e^(-j w / 2) / cos (w / 2)` times `Y4`: half a sample of lag and a boost `1 / cos (w / 2)` (1.38 at `w = 0.4837 pi`, the clamp, for 0.95 / pi fs). With `L (w) = k HP (Omega) F (w) / (1 + j Omega / g)^4` the loop oscillates when `-1 / Re L` at the phase crossing falls below `k`: the numbers of 1.2. (calc; the 30 Hz high-pass `HP (Omega) = (j Omega / gh) / (1 + j Omega / gh)`, `gh = tan (pi 30 / fs)`, is included.)

**The delay-free loop (the ZDF solution).** The four stages are in series and uncoupled, so with the states `s0..s3` fixed the output is linear in the loop input `u` (this is the formula of `Ladder4Pole`, `filter_dsp.h:180`):
`y4 = G^4 u + S`, `S = (1 - G) (G^3 s0 + G^2 s1 + G s2 + s3)`  (with `1 - G = 1 / (1 + g)`).
The feedback high-pass acts on the CURRENT `y4`: `lp = GHp (y4 - h) + h`, `h <- lp + GHp (y4 - h) = h + 2 GHp (y4 - h)`, `fbHP = y4 - lp = a (y4 - h)` with `a = 1 - GHp = 1 / (1 + gHpSafe)`. So `fbHP = p + c u` with `p = a (S - h)` (the zero-input prediction) and `c = a G^4`.
The loop of the code, with the nonlinearity kept (`sc = softClip`): `u = sc (D - k sc (p + c u))`, `D = driveGain in`. Properties (all provable from the code):
1. `sc` is increasing with `sc' (x) = 9 (9 - x^2)^2 / (27 + 9 x^2)^2` for `|x| < 3`, `0` outside, so `0 <= sc' <= 1`. The right-hand side is non-increasing in `u` (`k >= 0`, `c >= 0`), the left side increases: `f (u) = u - sc (D - k sc (p + c u))` has `f' = 1 + sc'_outer k sc'_inner c >= 1`. There is exactly one root for every `g > 0`, and Newton's denominator is never below 1: no singular case (the "instantaneously unstable" case of delay-free loops, Zavalishin 2018, cannot occur because the loop is a negative feedback through non-negative gains).
2. The root satisfies `|u| <= 1` (outer `sc`), so all states stay bounded for any `g`, `k`, drive: the same bound as today.
3. Linear case (both `sc` replaced by 1): `u = (D - k p) / (1 + k c)`; the denominator is `>= 1`.
4. Linear stability. Because the TPT stages and the TPT high-pass are the bilinear images of the analog ones, the digital loop `L = k HP (Omega) / (1 + j Omega / g)^4` is the analog loop evaluated at `Omega = tan (w/2)`: stability depends on `k` and `gh / g` only, not on `fs`. The phase crossing for equal stages is at `Omega = g` (each stage `-45 deg`), where `|1 / (1 + j)^4| = 1/4`: `k_thr = 4`; the high-pass lead pushes the crossing up and `k_thr` can only grow (calc: 14.9 at a 20 Hz knob, 5.4 at 100 Hz, 4.12 at 1 kHz, 4.01 above 8 kHz, 4.00 at 0.49 fs). The shipped maximum is `k = 3.82 < 4` for every cutoff and every sample rate: the delay-free filter cannot self-oscillate in the small-signal regime at any `g`, which also removes today's dependence of the resonance on the cutoff. Closed-loop poles at `res = 100 %` (high-pass neglected): `(1 + s / g)^4 = -3.82`, `s / g = 3.82^(1/4) e^(+-j pi / 4) - 1 = -0.0115 +- 0.9885 j` (3.82^(1/4) = 1.3980): decay rate `0.0115 g` in the `Omega` domain, e.g. 72 1/s at 1 kHz (1/e in 14 ms), so a 2 s tail is `< 1e-60` (a test bound of `1e-3` has a huge margin for knobs >= 1 kHz; at a 20 Hz knob the rate is 1.4 1/s and the bound would NOT hold: the tests use 1 kHz and above).

### 3.2 Options

**Option 1: lift the clamp only, or compensate the delay.** (a) Raise `0.95` to `50` as for the other ladders. Not acceptable: the loop's delay-induced instability grows with `g` (1.2: rings from resonance 2.7 % at 0.49 fs), the tuning stays 13-19 % flat, the cutoff would reach the top but the filter would be a self-oscillator at the top. (b) Keep the delayed loop and compensate it the way delayed-feedback ladders are commonly tuned (Huovilainen 2004: an extra half-sample delay correction on the feedback, plus lowering `k`). It removes the first order of the phase error only; at `w` near 0.5 pi (11 kHz) the residual is large, and it needs a calibration table of `k` against `g`. Sound: changes every resonant setting anyway. CPU: as today (4 divisions + `tanf` unless hoisted). Risk: a fitted correction instead of a solution; rejected.

**Option 2: zero-delay feedback, linear solve, nonlinearity applied around it (the `Ladder4Pole` recipe).** `u = sc ((D - k sc (p)) / (1 + k c))`: the feedback is predicted from the states (`p`), saturated, and the division by `1 + k c` makes the linear loop exact. Cost: 2 `softClip` divisions per sample, the division by `1 + k c` cached while `g` and `res` do not change (3.3), no `tanf`; 2 divisions per sample and channel instead of 4 plus the `tanf` (of 1.1.e). Memory: about 14 cached floats. Real-time safe (no allocation, no branch that depends on data except the saturator's range test the code already has). Same construction as the verified `Ladder4Pole` (`filter_dsp.h:179-191`, tests T1-T5), so the risk is lowest. Accuracy: exact for the linear loop; when the saturators bend, the prediction ignores that `u` itself moves `fbHP` by `c u`. Calc (throwaway double-precision model of the equations, a 110 Hz saw, 0.5 s after 0.25 s of settling, the converged root of `f (u) = 0` found by 60 Newton steps as the reference): RMS of the output error relative to the output RMS, in dB, at `res` 0.5 / 1.0: cutoff 500 Hz -141 .. -121 (inaudible), 5 kHz -77 .. -51, **15 kHz -57 .. -23** (drive 0 .. 100 %). Above ~8 kHz, where this item exists, the error reaches -23 dB (7 %): the delay-free structure is there but the loop's soft-clip interplay deviates audibly from the exact model.

**Option 3 (chosen): zero-delay feedback, linear solve for the starting value, one Newton step on the exact scalar equation.**
`u0 = clamp ((D - k sc (p)) / (1 + k c), -1, 1)`, then ONE Newton step on `f (u) = u - sc (D - k sc (p + c u))`:
`x1 = p + c u0`, `A = D - k sc (x1)`, `f = u0 - sc (A)`, `f' = 1 + sc' (A) k sc' (x1) c`, `u = u0 - f / f'`.
* Calc (same throwaway model, same cases): RMS error against the converged root, relative to the output RMS: 500 Hz -236 .. -214 dB, 5 kHz -159 .. -128 dB, **15 kHz -125 .. -83 dB** (worst: res 100 %, drive 100 %: -85 dB; res 100 %, drive 50 %: -86 dB; the -83 dB is res 50 %, drive 50 %). The worst case, 83 dB below the signal, is 0.007 % of it. (With the saturated start of Option 2 the same step gives -76.5 dB at worst; the clamp start is used because it needs no extra division and is better.)
* The step cannot leave `[-1, 1]`: `u = u0 (1 - 1/f') + sc (A) / f'` is a convex combination of `u0` and `sc (A)` (both in `[-1, 1]`) because `f' >= 1`: the bound of property 2 holds for the result, no clamp after the step.
* Divisions per sample and channel: `sc (p)` 1, `sc` and `sc'` at `x1` 1 (they share the reciprocal `r = 1 / (27 + 9 x^2)`: `sc = x (27 + x^2) r`, `sc' = 9 (9 - x^2)^2 r^2`), `sc` and `sc'` at `A` 1, `f / f'` 1: **4, the same as today, with the `tanf` and the `G` / `GHp` divisions gone** (cached). `res = 0` (`k == 0`) takes the shortcut `u = sc (D)`: 1 division, and bit-identical to today's `u` at `res = 0` (3.4).
* Memory: the same 14 floats as Option 2.
* Real-time safety: no allocation, no lock, no system call; the branches are the range tests of the saturators and the cached-coefficient test (same pattern as `Ladder4Pole`, `filter_dsp.h:166`); output independent of the block size (state advances once per sample; the coefficient cache is keyed on bit-exact `g`, `res`, `sampleRate` values so it is a pure function of its inputs).
* Risk: a new function inside the file that also holds `SVFTPT` and `Ladder4Pole` (small, three of them already coexist); the NEON pair kernel (task A4) must reproduce it bit for bit, which the existing pair test enforces.
**Chosen: Option 3.** Reason: it is the only one whose error stays below audibility in the region the item is about (cutoff above 10.67 kHz) at the price of the same number of divisions as today and no `tanf`; Option 2 is the fallback if the build worker's measurement (M2 after A3) shows the diode filter section dearer than today's (then the Newton step is removed in one commit and the test threshold of A2 relaxes from -60 dB to -18 dB; this is a measured switch, not a "later").

### 3.3 The chosen algorithm, written out (for task A2; constants as the code has them)

State: `float s[4]`, `float hpState` (named `h` below). Cached (per `DiodeLadder`, recomputed only when the key changes; the key is compared with `==` on the float values): `lastG`, `lastRes`, `lastSr`, and
`gSafe = max (1e-5, min (g, 50))` (50 as in `Ladder4Pole`, `filter_dsp.h:168`), `G = gSafe / (1 + gSafe)`, `G0 = 1 / (1 + gSafe)`, `G2 = G G`, `G3 = G2 G`, `G4 = G3 G`;
`gHp = max (1e-5, min (tan (kPi 30 / sr), 0.5))` (the expression of `:239-240`, now evaluated only when `sr` changes), `GHp = gHp / (1 + gHp)`, `a = 1 / (1 + gHp)`, `twoGHp = 2 GHp`;
`k = 3.82 res`, `c = a G4`, `invDen = 1 / (1 + k c)`.
Per sample (`D = (1 + 4 drive) in`):
```
s[i] = sanitize (s[i]) (4x); h = sanitize (h)
S  = G0 * (G3*s[0] + G2*s[1] + G*s[2] + s[3])
if (k == 0)  u = softClip (D)
else {
  p  = a * (S - h)
  u0 = clamp ((D - k * softClip (p)) * invDen, -1, 1)
  x1 = p + c*u0;   (sc1, d1) = softClipAndSlope (x1)
  A  = D - k*sc1;  (sc2, d2) = softClipAndSlope (A)
  f  = u0 - sc2;   fd = 1 + d2 * k * d1 * c
  u  = u0 - f / fd
}
y = u;  for i in 0..3 { v = G*(y - s[i]); y = v + s[i]; s[i] = sanitize (v + y) }
h = sanitize (h + twoGHp * (y - h))
return y
```
`softClipAndSlope (x)`: `x <= -3 -> (-1, 0)`, `x >= 3 -> (1, 0)`, else `x2 = x x; r = 1 / (27 + 9 x2); (x (27 + x2) r, 9 (9 - x2) (9 - x2) r r)`; added to `filter_dsp.h` next to `softClip`. `softClip (p)` stays the existing function. NaN input: `sanitize` at the states, the caller's `sanitize (in)` (`filter.cpp:133`) and the bound `|u| <= 1` keep everything finite (a NaN `D` would give `softClip` of NaN = NaN, as today; the caller removes NaN before).

### 3.4 What changes in the sound, and the presets

* **Shipped presets: none** (1.4). No refcompare, level table or gate of a shipped preset changes.
* **`res = 0`, any `drive`, `g <= 0.95` (cutoff <= 10665 Hz @44.1 kHz)**: bit-identical to today (`k == 0` takes `u = softClip (D)`; the stage loop is the same four lines; `G` is the same expression). Above 10665 Hz the cutoff finally moves (table 1.2).
* **`res > 0`**: changes at every cutoff, by design, because the loop changes from a delayed to a delay-free one. Size (calc, 1.2): at 1 kHz `res <= 50 %` within 0.1 dB, `res = 80 %` 5.2 dB -> 3.0 dB peak, `res = 100 %` 36.5 dB -> 13.5 dB (today a 0.5 % margin from oscillating); at 5 kHz and above the resonance is no longer cutoff-dependent: `res 80 %` 3.8 .. 3.9 dB, `res 100 %` 16.9 .. 17.8 dB, no self-oscillation (today it rings from 75 % at 5 kHz, 54 % at 10 kHz). The peak sits at the knob frequency (calc: 4954 Hz at 5 kHz, 9924 Hz at 10 kHz, 14932 Hz at 15 kHz, res 100 %) instead of 13-43 % below it. The "303 squealy resonance" wording of the catalogue is kept in kind (17.8 dB at full resonance, a clean peak); raising `3.82` toward `4` to get more squeal is a sound decision that this plan does NOT make (it would change a shipped constant of the model).
* `drive`: unchanged input gain and the same two `softClip`; at high drive the saturated loop is solved more exactly than before (Option 3 error table).
* The test `serum_filters_magnitude_response` (`test_filters_serum.cpp:104-135`, `magLow > magHigh` for `AcidLadder` at cutoffs 200 / 1000 / 4000 Hz) is unchanged in meaning and must still pass.

### 3.5 The classic Diode (model 4): the coupled-stage solve, and what is planned

**Derivation (from `classic_filters.h:792-853`, `w = kDiodeW = 1`, `up = lo = 0.5`).** With `dv` the old stage voltages, `s_j = sat (dv_j)` (old), `dU` the old `u`, the code solves `M v = rhs` where `M` is tridiagonal with diagonal `1 + g`, lower `-g lo`, upper `-g up` (row 3 has no upper), and
`rhs_0 = (1 - g) dv_0 + g up (dU + s_1) + g lo u`, `rhs_1 = (1 - g) dv_1 + g up (s_0 + s_2)`, `rhs_2 = (1 - g) dv_2 + g up (s_1 + s_3)`, `rhs_3 = (1 - g) dv_3 + g lo s_2` (`:838-841`).
`u` enters only through `rhs_0` with the weight `g lo`. The solve is linear in `u`: `v = M^-1 rhs_old + u g lo M^-1 e_0 = a_vec + c_vec u`. `c_vec = g lo M^-1 e_0` is a constant of the setup: one extra Thomas pass on the unit right-hand side `e_0 g lo` per parameter change (cached with `invD`, `cp`, `:792-799`), not per sample. `a_vec` is the Thomas solve the code already does per sample with `u = 0` in `rhs_0`. So with the 20 Hz feedback high-pass `fb = aF (y - dFb)`, `aF = 1 / (1 + gf)`, `y = v_3 = a_3 + c_3 u`: `fb = P + Q u`, `P = aF (a_3 - dFb)`, `Q = aF c_3`, and the loop is the scalar equation
`u = Gn tanh (X - B u)`, `Gn = kDiodeGain = 9.8`, `X = 0.5 (dIn xp - kf P)`, `B = 0.5 kf Q`, `kf = k kDiodeFb = 0.28 k`.
`f (u) = u - Gn tanh (X - B u)` is strictly increasing with `f' = 1 + Gn B (1 - tanh^2) >= 1` (`B >= 0` because `c_3 > 0`), so the root is unique and lies in `[-Gn, Gn]`. Unlike the serum ladder, `Gn B` can reach `9.8 x 0.5 x 4.76 x Q`, up to 11.7 with `Q` near 0.5, so the saturated S-curve is steep and a plain Newton step from the no-feedback guess is NOT safe: calc (throwaway scalar model, `X` uniform in +-40, `B` in 0..1.2): cold-start Newton, with or without clamping the iterate to `[-Gn, Gn]`, leaves errors of 0.4 .. 1.6 times full scale after 2-3 steps in the worst corner; a bracketed Newton (bracket `[-Gn, Gn]` shrunk by the sign of `f`, bisection when the step leaves it) is needed there, and the starting value matters.
**Planned (gated, section 4 batch C):** the solver is a bracketed Newton starting from the previous sample's `u` (`dU`), at most 3 steps, with the accuracy accepted by a time-domain test against a 60-step bisection reference at the M3 settings (task C3a); if the acceptance (-70 dB) fails, C3 is not built and the Diode keeps its delayed loop with a corrected comment (`:769`). The decision to build C3 at all is the gate G3 of section 4 (M3 shows a delay-induced deviation above 1.5 dB at some table setting, or self-oscillation, calc says at 12 kHz and above with high resonance).

### 3.6 Division-count work (rank 26) and what is NOT planned

* **Classic Diode `sat` x4 (tier low).** The four `sat (dv[j])` of one sample use only OLD states, so they are independent. `sat (v) = v - 0.9 v^3 / (v^2 + 3)` (`classic_filters.h:145`) has the denominators `d_j = dv_j^2 + 3` (`>= 3`, about 100 for the voltages that occur, because `u` is bounded by `kDiodeGain = 9.8`). One division can serve all four (Montgomery's batch inversion): `q01 = d0 d1`, `q23 = d2 d3`, `r = 1 / (q01 q23)`, `1/d0 = r d1 q23`, `1/d1 = r d0 q23`, `1/d2 = r d3 q01`, `1/d3 = r d2 q01`: 1 division and 11 multiplications instead of 4 divisions. The results differ from the true quotients in the last bits (so the two Diode presets change at tier low at the -140 dB level; the tier-low path already accepts "a few units in the last place", `classic_filters.h:797`). With `|dv_j|` clamped to `1e4` in the batch path (`flushDenormal` does not bound magnitudes) `d_j <= 1e8` and the product `<= 1e32`, below `3.4e38`. Tier medium and high keep the exact path (11 divisions, `:844-852`), where CPU is not the issue. HYPOTHESIS: on the A17 this saves 3 divisions (`3 x 14 = 42` cycles) and costs 11 multiplies (about `11 x 2 = 22` cycles), about 20 cycles per sample and channel, `20 x 44100 x 16 = 14 M cycles/s = 0.9 point` at 8 voices; gate G2 (M2) decides.
* **Classic Diode pair loop.** The Diode runs two scalar channels one after the other (`processPair` refuses model 4, `classic_filters.h:378-379`); a `diodePairLoop` that interleaves the two channels (as `ladderPairLoop` does for the Ladder, `:724-746`) lets the two independent chains overlap and lets the batch inversion run over 8 denominators (1 division for both channels). Same gate G2.
* **Classic Ladder `sat` x4 (`ladderRun`, `ladderPairLoop`): no change planned.** Each stage's `sat` feeds the next stage, so the four divisions are a dependent chain; batch inversion does not apply; a reciprocal by Newton iterations from a bit seed needs 3 iterations (6 dependent multiply-subtract) for 5e-8 relative error, which is not cheaper than the VFP division on this core (HYPOTHESIS); `vrecpe/vrecps` are not bit-identical between the scalar and the NEON path (rank 27's policy). The only remaining lever is replacing the algebraic saturator by a cheaper curve, which is a sound change specified by `docs/engine-spec/05-filters.md` (the algebraic saturator of the Ladder) and therefore the owner's decision, not an implementation detail. The measurement M2 on `Acid Saw Reso` records what the classic Ladder's filter section costs, so that decision has a number.
