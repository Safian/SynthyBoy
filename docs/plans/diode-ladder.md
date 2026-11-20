# Plan: the diode ladder filter: zero-delay feedback, cutoff above 10.67 kHz, fewer divisions (audit part 1, rank 1 remainder and rank 26)

Status: plan only, written from code and document reading. No build, no test, no benchmark, no ssh was run. Branch `worktree-agent-af92f8a651da82ff9`, base `6069a30f` (develop). The only file this item's plan work creates is this one.
Covers: `docs/review/OWN-ENGINE-DSP-AUDIT-1-VOICE-PATH-2026-11-20.md` rank 1 (the `0.95` clamp that stayed in the diode ladder) and rank 26 (scalar divisions of the classic Ladder and Diode, missing NEON pair kernels), corrected by `docs/review/OWN-ENGINE-DSP-AUDIT-1-VERIFICATION-2026-11-20.md` (rank 1 at :56-76, rank 26 at :722-745) and `TODO.md:64`.

Conventions: `A:l` = file:line that was opened for this plan. "block" = 128 frames at 44.1 kHz = 128 / 44100 = 2.90249 ms (`docs/MPC.md:321` and the perf plan use 2.902 ms). "points" = percentage points of that block. "Key 61" = Akai MPC Key 61 (ARMv7 Cortex-A17, 1.608 GHz, tier `low`).

**How the numbers of this document were made.** Every `file:line` was opened. The loop-stability, tuning and peak numbers in sections 1 and 3 are computed from the equations of the code (derived in section 3) with a throwaway calculator script (plain Python complex arithmetic, kept in the session scratchpad, not in the repository, not a test of the repository's code); they are therefore analytic small-signal numbers, marked "calc". Anything that only a build or a device can give is marked HYPOTHESIS together with the measurement that decides it (section 2).

**Two filters carry the name "diode" in this code base, and the task text mixes them up.** (1) `DiodeLadder` in `core/filters-serum/src/filter_dsp.h:216-259`, rendered as the Serum type `AcidLadder` (model 26 "New", style 1): this is where the `0.95` clamp is. Its four stages are NOT coupled: it is a cascade of four trapezoidal one-poles (`filter_dsp.h:250-256`). (2) `ClassicFilter::diode` in `core/engine-own/voice/classic_filters.h:771-855` (filter model 4 "Diode", the one the two shipped Diode presets use): its four stages ARE coupled (tridiagonal system, Thomas algorithm, `:792-799,845-853`), it has no `0.95` clamp, and its loop also has a one-sample delay (`:826-836`, section 1.3). The finding of rank 1 is about (1); rank 26 names both. This plan treats (1) as the main item (sections 3.1-3.4) and (2) as a measured, gated second item (section 3.5), because it changes the sound of two shipped presets.

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
| i | `FilterUnit` reaches `DiodeLadder` only through the Serum model 26 style 1: `classic = s.model < filters::kFirstSerumModel` (= 8) sends models 0-7 to `ClassicFilter`, so the `case 4: ... AcidLadder` of `typeFor` is never used for a classic model (`typeFor` is only called when `classic` is false). | `core/engine-own/voice/filter_unit.h:77,108-115`; `core/filters-serum/include/synthyboy/filters/filter_types.h:92-93` (`MGLadder`, `AcidLadder`), `kFirstSerumModel = 8` at the same file |

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
| 12 kHz, unclamped | 1.1495 | see 3.1 (the delay grows with `g`) | | | 4.01 |

So today (a) the resonance of the filter depends strongly on the cutoff (the same knob position is a clean peak at 1 kHz and a sustained oscillation at 5 kHz), (b) it rings 13 % flat at 5 kHz (4343 against 5013 Hz, the delay-free crossing) and 19 % flat at 10 kHz, and (c) the shipped constant `3.82` was clearly chosen against the delay-free limit `4` (the analog threshold of four equal poles: `k = 4` at `Omega = g`, `(1/sqrt 2)^4 = 1/4`), not against the delayed loop. `softClip` bounds `u` to `[-1, 1]` (`:248`), so none of this blows up (the test `serum_ladder_cutoff_t3_stability` style limits would hold); it is a tuning and sound problem, not a crash. Small-signal peak of the magnitude response (calc; only rows where the loop is stable): 1 kHz res 100 %: today 36.5 dB, delay-free 13.5 dB; 10 kHz res 50 %: today 16.8 dB at 7984 Hz, delay-free 0 dB (no peak); 15 kHz res 50 %: today 28.3 dB at 8537 Hz (the clamp), delay-free 0 dB; res 80 % at 5 kHz and above: today the loop is unstable (rings), delay-free 3.8 .. 3.9 dB; res 100 %: delay-free 13.5 .. 17.8 dB at every cutoff from 1 to 15 kHz.

**CPU.** From the line counts of 1.1.e: 4 divisions and 1 `tanf` per sample and channel. HYPOTHESIS (no measurement exists, section 2 M1 builds it): a VFP `vdiv.f32` of the Cortex-A17 takes about 14 cycles and is not pipelined, and a `tanf` call about 100-200 cycles; that would be `4 x 14 + 150 = ~206` cycles per sample and channel before the 4 stages. For the worst case of 8 voices with filter 1 on the diode ladder, 2 channels, 44100 samples/s: `206 x 44100 x 16 = 145 M cycles/s = 9.0 % of one 1.608 GHz core` = 9.0 points of every block (HYPOTHESIS; deciding measurement: M1 `--sections` filter section of the acid load). The section 3 design removes the `tanf` and two divisions and adds none (4 divisions, section 3.3), so it should be cheaper than today, not dearer.

**Memory.** `DiodeLadder` is `float s[4] + float hpState` = 20 bytes (`filter_dsp.h:218-219`) inside `FilterImpl` (`filter.cpp:829`); the design adds about 12 cached floats per channel (section 3.3) = 48 bytes per `FilterImpl`; 8 voices x 2 filters x 2 channels = 32 `FilterImpl` = 1.5 KB. Negligible (the memory item is `docs/plans/memory-footprint.md`).

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

At the two shipped presets (`Diode Saturated Bass`: filter 1 model 4, cutoff 420, resonance 20, drive 20; `Diode Bite Lead`: cutoff 3100, resonance 45, drive 20; from the preset files, section 1.4) `K = 1.372 (0.7 + 16.3 r^3) = 1.14` and `3.00`; the peak gain `1 / (1 - K / K_thr)` is `1.027 / 1.024` and `1.105 / 1.077` (today / delay-free): 0.2 dB and 0.2 dB apart. So the delay matters for the classic Diode only at high cutoff with high resonance, and the shipped presets sit where it does not.

### 1.4 Which presets use which

Scan of all 160 `content/presets/**/*.sbpreset` (key `filterN.model` / `filterN.style`, N = 1, 2; Python read of the JSON): **no preset uses the Serum Acid Ladder** (model 26: zero presets use model 26 at all). The two presets with the classic Diode (model 4) are `Bass/diode_saturated_bass.sbpreset` (filter 1, on, style 0, cutoff 420, resonance 20, drive 20, keytrack 0) and `Lead/diode_bite_lead.sbpreset` (filter 1, on, style 0, cutoff 3100, resonance 45, drive 20, keytrack 0). `Diode Saturated Bass` is on the Key 61's expensive list: 80.9 % p99 under `low` (`docs/MPC.md:321`) and average / p99 / worst 1.44 / 1.66 / 2.13 ms with 8 notes (`docs/MPC.md:95`); `Diode Bite Lead` is in the heavy list of the bench (`bench/tier_bench.cpp:198`). Other references to the Acid Ladder (no sound): UI name lists (`shell-juce/ui/panels/FilterModels.h`), `core/serum-import/serum_map.cpp`, `core/filters-serum/CATALOGUE.md:148`, tests `core/filters-serum/tests/test_filters_serum.cpp:126`.
**Consequence: lifting the clamp and changing the loop of the serum `DiodeLadder` changes no shipped sound and no refcompare baseline of a shipped preset.** Changing the classic Diode changes two shipped presets (section 3.5).

### 1.5 What was wrong or already done in the audit

* Rank 1: confirmed (verification `:56-76`). Already done: `Ladder4Pole` and the pair kernel's ladder (clamp 50, `2fb2eeaa`, tests `serum_ladder_cutoff_t1..t5` at `test_filters_serum.cpp:826-1120`). Remaining: only `DiodeLadder` (`TODO.md:64`). The audit's cure "raise `gSafe` to 31.8 or 20" is wrong for the diode ladder: it would let the delay grow with `g` (section 1.2) and the filter would ring at any resonance above 51 %.
* The audit's phrase "convert `DiodeLadder` to ZDF so it stays tuned" is right, but the audit (and the task text) describe it as a coupled-stage ladder; the stages of this code are an uncoupled cascade (1.1.d), so the instantaneous solve is the 4-pole ladder's solve plus the feedback high-pass, not a tridiagonal one (3.1). The coupled solve belongs to model 4 only.
* Rank 26: confirmed code, consequence unproven (verification `:745`). Facts re-checked here: `sat` is `v - 0.9 v^3 / (v^2 + 3)` (`classic_filters.h:145`), 4 calls per sample in `ladderRun` (`:632-637`) and `ladderPairLoop` (`:733-742`), 4 in `diode` (`:838`); the pair kernel (`processPair`, `:373-391`) exists only for models 0-3 and calls `processPairLadder` for the Ladder, and has no Diode form. The audit's "missing pair kernels for Normal, MGDirty, Formant" is a different item (not diode); it is listed in section 6 as not covered.
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
