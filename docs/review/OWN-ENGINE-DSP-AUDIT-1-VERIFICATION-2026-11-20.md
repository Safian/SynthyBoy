# Independent Adversarial Verification — Own Synth Engine DSP Audit, Part 1 (Voice Path)

**Date:** 2026-11-20  
**Audit Verified:** `docs/review/OWN-ENGINE-DSP-AUDIT-1-VOICE-PATH-2026-11-20.md`  
**Method:** Direct source, header, test, and benchmark-doc inspection of all 27 ranked findings in rank order, with zero builds, zero test executions, and strict clean-room isolation.

---

## Summary Table

| Rank | Finding in Six Words | Verdict | Corrected `file:line` if Different | Measurement Still Needed |
|---:|---|---|---|---|
| 1 | Serum ladder clamps cutoff at 10.67kHz | `CONFIRMED` | Same (`core/filters-serum/src/filter_dsp.h:168,234`, `core/filters-serum/src/filter.cpp:406`) | none |
| 2 | Decimation FIR runs per unison voice | `PARTLY WRONG` | Same (`core/engine-own/osc/wt_osc.h:656,826-937`) | `tier_bench --config low --sections` on `Twin Warp Lead`, `Triple Fold Reese`, plus null test when per-voice gain ramps |
| 3 | External spectral FFT runs every period | `CONFIRMED` | Same (`core/engine-own/osc/lib_osc.cpp:554-569`, plus `ringDirty` at `lib_osc.cpp:1005`) | none |
| 4 | Pre-warp band limit exceeds oversampled Nyquist | `CONFIRMED` | Same (`core/engine-own/osc/wt_osc.h:565,672`) | none |
| 5 | Nonlinear warps lack ADAA and fastmath | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | Same (`core/engine-own/osc/warp.h:116-119,152,171-234`) | Aliasing FFT sweep (keys 36–108, 44.1 kHz, tier `low`/`high`) on `Sync`, `LinearFold`, `HardClip`; `tier_bench` on `Triple Fold Reese` |
| 6 | Smooth wavetable frame blend never ramps | `CONFIRMED` | Same (`core/engine-own/osc/wt_osc.h:222,705-713`, member at `wt_osc.h:1234`) | none |
| 7 | Sample and granular mipmaps alias pitch-up | `CONFIRMED` | Same (`core/sample-osc/src/sample_voice.cpp:156,267,917,1167`, `core/granular/src/granular.cpp:95,173`) | none |
| 8 | Host LFOs read stale frozen songSeconds | `PARTLY WRONG` | Same (`core/engine-own/own_engine.cpp:1154,1371-1378`, plus `core/engine-own/voice/voice.cpp:434-438,530-534`) | none |
| 9 | Hot filter loops call per-sample libm | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | Same (`core/engine-own/voice/filter_unit.h:157,190,210,221`, `core/filters-serum/src/filter_dsp.h:109,129,239`, `core/filters-serum/src/filter.cpp:1202,1219,1235`, `core/engine-own/voice/classic_filters.h:568`) | `tier_bench --config low --sections` on Classic SVF, `Acid Saw Reso`, EQ12, and clean-mode presets + ARMv7 disassembly check on `SVFTPT::process` |
| 10 | Per-voice filter delays and engines preallocated | `PARTLY WRONG` | `core/filters-serum/src/filter.cpp:39-60`, `core/engine-own/osc/lib_osc.h:93-95,193-226`, `core/engine-own/osc/lib_osc.cpp:127-145` (audit cited `lib_osc.h:152-182`) | `VmRSS`, `prepare_voices` startup time, and Key 61 strike-block latency (`tier_bench --config low`) |
| 11 | Raw NEON float compares spill ARMv7 | `CONFIRMED` | Same (`core/engine-own/osc/wt_osc.h:975,982`, `core/spectral/src/kernels.cpp:267,298,310-313,540,677,716`) | none |
| 12 | Medium morph runs full 2048-pt IFFT | `CONFIRMED` | `core/engine-own/osc/morph.cpp:440-478`, `core/engine-own/osc/wt_osc.h:572,612,701` (audit cited `wt_osc.h:578,701`) | none |
| 13 | Mipmap Gibbs taper dips before rolloff | `CONFIRMED` | Same (`core/engine-own/osc/wavetable_bank.cpp:300-301`) | none |
| 14 | Four comb lines share one lowpass | `CONFIRMED` | Same (`core/filters-serum/src/filter.cpp:1294,1326`, member at `filter.cpp:840`) | none |
| 15 | Sub oscillator right pan never assigned | `CONFIRMED` | Same (`core/engine-own/voice/voice.cpp:1390`, `core/engine-own/osc/wt_osc.h:33,308`) | none |
| 16 | Linear-Hz cutoff smoother misses tanM cache | `CONFIRMED` | Same (`core/filters-serum/src/filter.cpp:141-143,176-195,360-369`) | none |
| 17 | Granular loop crossfade reads negative indices | `CONFIRMED` | Same (`core/granular/src/granular.cpp:485,739`) | none |
| 18 | Voice render runs inactive unused filters | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | Same (`core/engine-own/voice/voice.cpp:1751-1824`) | `tier_bench --config low --sections` (`voice render routing/mix/output`) on 1-filter and 0-filter patches |
| 19 | Chaotic LFOs clamp rate and Euler-step | `CONFIRMED` | Same (`core/engine-own/env_lfo/lfo.h:182-210`) | none |
| 20 | Unison center voice gains 3dB mono | `CONFIRMED` | Same (`core/engine-own/osc/wt_osc.h:247-250,257-271,384`) | none |
| 21 | Noise hardcodes 44.1kHz and high tier | `PARTLY WRONG` | Same (`core/engine-own/voice/voice.cpp:41-82,1433,1443,1715-1733`, plus `core/engine-own/dsp/noise_coeff.h:30`) | none |
| 22 | Mod matrix aliases audio-rate and recomputes | `CONFIRMED` | Same (`core/engine-own/voice/voice.cpp:1013-1019,1659,1676,1741,1771`, `core/engine-own/matrix/mod_matrix.cpp:382,394-396,407`) | none |
| 23 | Wavetable mip levels switch without crossfade | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | Same (`core/engine-own/osc/wt_osc.h:672-687`, `core/engine-own/osc/wavetable_bank.cpp:229,255-263`) | Slow 4-octave pitch-bend spectrogram and listening check on Sawtooth across half-octave boundaries |
| 24 | Multisample one-pole filters use two-pole biquads | `CONFIRMED` | Same (`core/multisample/src/multisample.cpp:735-745`, `core/engine-own/osc/lib_osc.cpp:975,991`) | none |
| 25 | Backward cross-mod splits and main-only declick | `CONFIRMED` | Same (`core/engine-own/voice/voice.cpp:302-304,1619-1623,1833-1835,1871-1883`, `core/engine-own/own_engine.cpp:1159-1178`) | none |
| 26 | Classic ladder divides and pair-NEON gaps | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | `core/engine-own/voice/classic_filters.h:145,627,635,730-742,810-852`, `core/filters-serum/src/filter.cpp:203-708` (audit cited `classic_filters.h:642,670-677,810-852`) | `tier_bench --config low --sections` on `MGLow24`, `Acid Saw Reso`, `Diode Saturated Bass`, `Formant Vowel Bass` |
| 27 | Global fp-contract off and union punning | `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN` | Same (`core/engine-own/dsp/simd.h:1-35`, `core/engine-own/dsp/fastmath.h:37,44`, `core/filters-serum/src/filter.cpp:9-11`) | `tier_bench --verify --tolerance 1e-5` comparing `-ffp-contract=fast` (FMA) and Newton-Raphson `vrecpe` against `-ffp-contract=off` on MPC XL and Key 61 |

---

## Counts per Verdict

- **`CONFIRMED`**: 17 (Ranks 1, 3, 4, 6, 7, 11, 12, 13, 14, 15, 16, 17, 19, 20, 22, 24, 25)
- **`CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`**: 6 (Ranks 5, 9, 18, 23, 26, 27)
- **`PARTLY WRONG`**: 4 (Ranks 2, 8, 10, 21)
- **`REFUTED`**: 0
- **`COULD NOT VERIFY`**: 0
- **Total Checked**: 27 / 27 (0 findings unchecked)

---

## Finding-by-Finding Verification

### Rank 1 — `Ladder4Pole`, `DiodeLadder`, and `processPairT` clamp `gSafe <= 0.95f`, capping cutoff at 10.67 kHz @ 44.1 kHz

- **(a) Cited vs. actual `file:line`**: Exact match at `core/filters-serum/src/filter_dsp.h:168`, `core/filters-serum/src/filter_dsp.h:234`, and `core/filters-serum/src/filter.cpp:406`.
- **(b) Decisive code** (`core/filters-serum/src/filter_dsp.h:166-170,234-235` and `core/filters-serum/src/filter.cpp:406-407`):
  ```cpp
  if (!(haveG && g == lastG))
  {
      const float gSafe = std::max (1e-5f, std::min (g, 0.95f));
      cG = gSafe / (1.0f + gSafe);
  ```
  ```cpp
  const float gSafe = std::max (1e-5f, std::min (g, 0.95f));
  const float G = gSafe / (1.0f + gSafe);
  ```
  ```cpp
  const V gSafe = V::min (V::max (g, V::all (1e-5f)), V::all (0.95f));
  const V G = V::div (gSafe, vOne + gSafe);
  ```
- **(c) Surrounding context & tests**: `FilterImpl::processBlock` (`core/filters-serum/src/filter.cpp:191`) and `processPairT` (`filter.cpp:387`) compute pre-warped bilinear `g = tanM(kPi * cutoff / srFloat)` for `cutoff` up to `srFloat * 0.49f` (`21609 Hz` at 44.1 kHz), and `SVFTPT::process` (`filter_dsp.h:108`) clamps `g` to `50.0f`. However, `Ladder4Pole::process` (`filter_dsp.h:168`, called by `MGLow6`, `MGLow12`, `MGLow18`, `MGLow24`, `MGLadder`, `EMSLadder`, and `MGDirty` at `filter.cpp:870-886,1570-1601`), `DiodeLadder::process` (`filter_dsp.h:234`, called by `AcidLadder` at `filter.cpp:1578-1582`), and the stereo-pair ladder kernel in `processPairT` (`filter.cpp:406`) all clamp `gSafe <= 0.95f`. By contrast, the own engine's separate `ClassicFilter::ladderSetup` (`core/engine-own/voice/classic_filters.h:584`) uses `std::tan(kPif * std::min(f / static_cast<float>(sr), 0.499f))` without a `0.95f` clamp. Existing test `core/tests/test_own_ladder_range.cpp:25` (`own_ladder_rms_over_cutoff`) only tests `ClassicFilter` model 2 (`Ladder`), while `core/filters-serum/tests/test_filters_serum.cpp:104` (`serum_filters_magnitude_response`) only tests cutoffs `{200.0f, 1000.0f, 4000.0f}` Hz and therefore never reaches the 10.67 kHz ceiling.
- **(d) Arithmetic**: `g = tan(pi * f_c / f_s) <= 0.95 => f_{c,max} = (atan(0.95) / pi) * f_s = (0.75976275 / 3.14159265) * f_s = 0.24184003 * f_s = 10665.1 Hz @ 44.1 kHz (11608.3 Hz @ 48 kHz), which is 1.048 octaves below Nyquist (22050 Hz).`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 2 — Decimation FIR (128-tap @ 4x or 32-tap @ 2x) runs inside the per-unison-voice loop instead of post-sum stereo

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/wt_osc.h:656,826-937` (the per-voice decimation inner loop is at `wt_osc.h:920-934`).
- **(b) Decisive code** (`core/engine-own/osc/wt_osc.h:656,920-934`):
  ```cpp
  for (int v = 0; v < count; ++v)
  {
      // ...
      for (int k = 0; k < n; ++k)
      {
          const float t = static_cast<float> (t0 + k + 1) * invP;
          const float* x = buf + k * os + 1; // the newest sample of the group is x[nt - 1]
          float acc = 0.0f;
          if (tick.lowWarp) acc = dot32 (x, kDecimator2Rev.c);
          else if (tick.fastMath) acc = dot128 (x, kDecimator4Rev.c);
          else for (int i = 0; i < nt; ++i) acc += taps[i] * x[nt - 1 - i];
          L[k] += acc * lerp (prevGainL[v], curGainL[v], t);
          R[k] += acc * lerp (prevGainR[v], curGainR[v], t);
      }
  ```
- **(c) Surrounding context & tests**: The code indeed runs the 128-tap (`kDecimator4`, 4x) or 32-tap (`kDecimator2`, 2x `lowWarp`) FIR inside `for (int v = 0; v < count; ++v)`, using per-voice history `firHist[kMaxUnison][kFirTaps]` (`wt_osc.h:1244`). However, the audit misses two architectural details:
  1. For a **single voice (`count == 1`)**, the current code runs **1** mono FIR evaluation per output sample and scales the scalar result `acc` into `L[k]` and `R[k]`. Unconditionally accumulating into stereo `osL`/`osR` and running a pair of stereo FIR decimators would perform **2** FIR evaluations at `count == 1` (a 2x regression on mono warped oscillators unless `count == 1` or equal-pan mono is kept on a 1-FIR path).
  2. In lines 932–933, the decimated mono sample `acc = (h * x_v)[k * os]` is multiplied by the time-varying gain ramp `g_{v,L}[k] = lerp(prevGainL[v], curGainL[v], t)` *after* decimation. Because the 128-tap FIR at 4x spans `128 / 4 = 32` base-rate samples (16-sample group delay, reading `nt - 1 = 127` oversampled history samples from the *previous* control period in `firHist[v]`), multiplying by `g_v` *before* the FIR into a shared `osL`/`osR` buffer is only mathematically identical to post-FIR multiplication when `prevGainL[v] == curGainL[v]` (stationary level, pan, spread, blend, and voice count); during fast level/pan/spread ramps, `[D, g_v] != 0`. Existing tests: `core/tests/test_own_wt_osc_opt.cpp` and `core/tests/test_own_warp_lanes.cpp`.
- **(d) Arithmetic**: `Per output sample, per-voice decimation costs U * nt MACs (at U=16, nt=128: 2048 MACs; at U=3, nt=32: 96 MACs; at U=1, nt=32: 32 MACs) vs. 2 * nt MACs for stereo post-sum (256 MACs at nt=128 = 8.0x reduction for U=16; 64 MACs at nt=32 = 1.5x reduction for U=3, but 2.0x increase for U=1 unless mono-gated).`
- **(e) Verdict**: `PARTLY WRONG`

---

### Rank 3 — External Mask/Vocode warp computes a scalar complex 2048-pt FFT + 2048 `std::cos` + 1025 `std::sqrt` every 32-sample control period

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/lib_osc.cpp:554-569` (with `ringDirty = true` set in `LibOsc::render` at `lib_osc.cpp:1005`).
- **(b) Decisive code** (`core/engine-own/osc/lib_osc.cpp:550-569`):
  ```cpp
  for (int k = 0; k < 2; ++k)
      if (s.warpType[k] >= 0 && spectral::isExternalWarp (s.warpType[k]))
      {
          const int e = spectral::externalSource (s.warpType[k]);
          if (ringDirty || e >= 0)
          {
              float re[spectral::kFftSize], im[spectral::kFftSize];
              const float* r = ring.data() + e * 2048;
              for (int i = 0; i < 2048; ++i)
              {
                  re[i] = r[(ringPos + i) & 2047] * (0.5f - 0.5f * std::cos (kTwoPif * static_cast<float> (i) / 2048.0f));
                  im[i] = 0.0f;
              }
  ```
- **(c) Surrounding context & tests**: `LibOsc::update()` runs once per 32-sample control period (`kControlPeriod = 32`). For any external spectral warp (`spectral::isExternalWarp`), `spectral::externalSource` returns `e in {0, 1, 2}`, so `if (ringDirty || e >= 0)` is unconditionally true on every 32-sample tick (and `ringDirty` is never set back to `false` anywhere in the file). Even worse, if both warp slots (`k = 0` and `k = 1`) use an external warp on the same source `e`, the loop computes the 2048-point FFT **twice** in the same 32-sample tick. On the consumer side (`core/spectral/src/spectral.cpp:816,844`), `params_.external[e]` is only read inside `SpectralVoice::synthesizeHop()`, which triggers once every `kHop = 512` samples (`16` control periods) per spectral unison voice (up to `kSpecU = 4` voices staggered by `kStartStep = 128` samples = 4 control periods at `lib_osc.cpp:743-744`). Existing test `core/spectral/tests/test_spectral.cpp:308` feeds a pre-filled `external` pointer directly to `SpectralVoice`, bypassing `LibOsc::update()`.
- **(d) Arithmetic**: `kHop / kControlPeriod = 512 / 32 = 16 control periods per STFT hop (16x redundant for 1 unison voice, >= 4x redundant for 4 quarter-hop-staggered voices, and 32x redundant when both warp slots select external warps), executing 2048 std::cos + 11264 complex radix-2 butterflies + 1025 std::sqrt every 32 samples (0.726 ms @ 44.1 kHz).`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 4 — Pre-warp wavetable band limit asks for `3.2 * sampleRate` at 4x oversampling (`Nyquist = 2.0 * sampleRate`)

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/wt_osc.h:672` and `core/engine-own/osc/wt_osc.h:565`.
- **(b) Decisive code** (`core/engine-own/osc/wt_osc.h:564-565,653,669-672`):
  ```cpp
  const bool warped = tick.oversample || warpNeedsOversampling (tick.warp[0]) || warpNeedsOversampling (tick.warp[1]);
  const float nyq = static_cast<float> (warped ? (tick.lowWarp ? 1.0 : 3.2) * sampleRate : 0.5 * sampleRate);
  ```
  ```cpp
  const float bw = std::max ({ 1.0f, warpBandwidthFactor (tick.warp[0], tick.warpAmt[0]), warpBandwidthFactor (tick.warp[1], tick.warpAmt[1]) });
  // ...
  const float fmax = std::max (curInc[v], prevInc[v]) * static_cast<float> (sampleRate) * bw;
  const float ratio = static_cast<float> (warped ? (tick.lowWarp ? 1.0 : 3.2) * sampleRate : nyq) / std::max (fmax, 1.0f);
  ```
- **(c) Surrounding context & tests**: At 4x oversampling (`os = 4`, `tick.lowWarp == false`), the oversampled stream's sampling rate is `4.0 * sampleRate` and its Nyquist frequency is `2.0 * sampleRate`.
  - In `updateMorph()` (`wt_osc.h:565-572`), `fmax` is **not** multiplied by `bw` at all (`fmax = std::max(fmax, curInc[v] * sampleRate)`), so `K = std::min(kHarm - 1, static_cast<int>(3.2 * sampleRate / fmax))` always synthesizes harmonics up to `3.2 * sampleRate` whenever `warped` is true on medium/high tiers.
  - In `render()` (`wt_osc.h:653,669-672`), `ratio = (3.2 * sampleRate) / (f0 * bw)`. Whenever `bw < 1.6f`, the effective band limit `(3.2 / bw) * sampleRate` exceeds the 4x Nyquist (`2.0 * sampleRate`). Checking `warpBandwidthFactor` (`core/engine-own/osc/warp.h:53-69`): **all 14 Distortion warps** (`kWarpDistTube..kWarpDistSoftSat`), `kWarpAsymPlus`, `kWarpAsymMinus`, `kWarpAsymBoth`, `kWarpFlip`, `kWarpMirror`, and `kWarpQuantize` fall through to `default: return 1.0f;`, and `kWarpBendPlus`/`kWarpBendMinus` (`warp.h:61`) return `1.0f + 1.5f * std::fabs(a - 0.5f)` (which equals `1.0f` at `a = 0.5f` and is `< 1.6f` for `a in (0.1f, 0.9f)`). (Note: when `tick.oversample == true` with *no* active warp, harmonics in `(2.0 * fs, 3.2 * fs]` fold into `[0.8 * fs, 2.0 * fs]`, which lies inside the stopband `>= 0.494 * fs` of `kDecimator4`; however, when any of the above nonlinear phase or amplitude warps is active, those pre-aliased partials intermodulate with the fundamental inside the 4x stream and fold into the audible `[0, 0.5 * fs]` passband before decimation.) Existing test: `core/tests/test_own_warp_lanes.cpp`.
- **(d) Arithmetic**: `At os = 4 (fs_os = 4 * fs, f_Nyq,os = 2.0 * fs) and bw = 1.0, asking for 3.2 * fs admits harmonics in (2.0 * fs, 3.2 * fs] (up to 60% above 4x Nyquist), which alias at 4 * fs - f_h into [0.8 * fs, 2.0 * fs] (e.g. at fs = 48 kHz, f0 = 3500 Hz, ratio = 153600 / 3500 = 43.89 selects mip level 8 with 32 harmonics = 112 kHz > 96 kHz).`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 5 — Hard nonlinearities and hard sync rely solely on 4x/2x oversampling and call scalar `libm` per oversampled sample

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/warp.h:116-119,152,171-234`.
- **(b) Decisive code** (`core/engine-own/osc/warp.h:116-119,152,174,203-204,222,230`):
  ```cpp
  case kWarpSync:
      return frac (p * (1.0f + 11.0f * a));
  case kWarpFmExpA: case kWarpFmExpB:
      return frac (p + std::copysign (std::expm1 (std::fabs (m) * 2.0f) * (1.0f / 6.389f), m) * a * 0.3f);
  // ...
  return std::round (y * levels) / levels; // kWarpQuantize
  // ...
  v = std::fmod (v + 1.0f, 4.0f);          // kWarpDistLinearFold
  // ...
  return y / std::sqrt (1.0f + y * y);     // kWarpDistStomp
  // ...
  return std::atan (y) / std::atan (g);    // kWarpDistSoftSat
  ```
- **(c) Surrounding context & tests**: Reading all of `core/engine-own/osc/warp.h:1-238` and `wt_osc.h:760-919` confirms:
  1. `kWarpSync` and `kWarpSyncVar` reduce the slave wavetable's harmonic count via `warpBandwidthFactor` (`1 + 11 * a`, `warp.h:57`), which prevents internal slave partials from exceeding Nyquist during the sweep, **but** applies no PolyBLEP or minBLEP across the hard phase-reset discontinuity when `p` wraps from `1.0 -> 0.0` at non-integer slave phase `11 * a`.
  2. `kWarpQuantize`, `kWarpDistHardClip`, `kWarpDistLinearFold`, `kWarpDistZeroSquare`, and `kWarpDistRectify` are memoryless point nonlinearities with no ADAA (antiderivative anti-aliasing), relying solely on 4x oversampling (`medium`/`high`) or 2x oversampling (`lowWarp` on tier `low`).
  3. While `kWarpSyncVar`, `kWarpDistSinFold`, `kWarpDistSoftClip`, and `kWarpDistTape` branch on `fast` (`tick.fastMath`) to use `fastmath::sinCycles` and `fastmath::tanh` (`warp.h:107-108,167,199,210`), `kWarpFmExpA/B` (`std::expm1`), `kWarpQuantize` (`std::round` + division), `kWarpDistLinearFold` (`std::fmod`), `kWarpDistStomp` (`std::sqrt` + division), and `kWarpDistSoftSat` (`2x std::atan` + division) ignore `fast` and call scalar `libm` on every oversampled sample (and block NEON vectorization in `canVectorizeWarp` at `wt_osc.h:1049-1071`, where only `kWarpDistSoftClip`, `kWarpDistHardClip`, and `kWarpDistAsym` are vectorized). Existing tests: `core/tests/test_own_warp_lanes.cpp`, `core/tests/test_own_osc_types.cpp:662`.
- **(d) Arithmetic**: `At tier low (os = 2, 128-frame block, 8 voices x 3 unison = 24 sub-voices), one active LinearFold or SoftSat slot executes 2 * 128 * 24 = 6144 scalar libm calls (fmodf or 2x atanf + div) per 2.902 ms block (at 4x OS with 4 unison voices: 4 * 128 * 32 = 16384 calls/block).`
- **(e) Verdict**: `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`

---

### Rank 6 — Smooth frame scanning never ramps `wB` across the 32-sample control period (`wB0 = wB1 = ff`)

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/wt_osc.h:222,705-713` (and `prevFrame` member declaration at `wt_osc.h:1234`).
- **(b) Decisive code** (`core/engine-own/osc/wt_osc.h:222,705-713`):
  ```cpp
  prevFrame = curFrame;
  ```
  ```cpp
  const float fpos = curFrameOf[v] * static_cast<float> (frames - 1);
  const int f0 = std::min (static_cast<int> (fpos), std::max (frames - 2, 0));
  if (tick.smooth || frames < 2)
  {
      const float ff = frames > 1 ? fpos - static_cast<float> (f0) : 0.0f;
      cA = wt.cycle (f0, level);
      cB = frames > 1 && ff > 1.0e-4f ? wt.cycle (f0 + 1, level) : nullptr;
      wB0 = wB1 = ff;
  }
  ```
- **(c) Surrounding context & tests**: Searching the entire repository for `prevFrame` confirms it is assigned in `WtOsc::update` (`wt_osc.h:222` and `425`) and **never read anywhere**. Furthermore, in `render()` (`wt_osc.h:818,890,911`) and `renderPlain()` (`wt_osc.h:1112,1137`), whenever `cB != nullptr`, the sample loop already executes `lerp(wB0, wB1, ...)` on every sample—meaning the engine already pays the full CPU cost of per-sample frame crossfade interpolation while passing `wB0 == wB1 == ff`, holding the frame blend constant across all 32 samples! Ironically, the *stepped* (`!tick.smooth`) branch right below (`wt_osc.h:714-725`) sets `wB0 = tStart` and `wB1 = tEnd`, ramping across the 32 samples. Existing test `core/tests/test_own_wt_osc_opt.cpp` checks bit-identity against `renderPlainReference` (which has the exact same `wB0 = wB1 = ff` at `wt_osc.h:1174`).
- **(d) Arithmetic**: `Holding wB0 = wB1 = ff constant across kControlPeriod = 32 samples updates the frame blend at f_s / 32 = 44100 / 32 = 1378.125 Hz (1500.0 Hz @ 48 kHz), creating a 32-sample zero-order-hold staircase.`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 7 — Sample and Granular mipmap selection aliases on pitch-up and fast scan

- **(a) Cited vs. actual `file:line`**: Exact match at `core/sample-osc/src/sample_voice.cpp:156,267,917,1167` and `core/granular/src/granular.cpp:95,173` (plus `sample_voice.cpp:123,245,699,709,747`).
- **(b) Decisive code** (`core/sample-osc/src/sample_voice.cpp:267,917-926,1167` and `core/granular/src/granular.cpp:170-175`):
  ```cpp
  const int m = std::max (0, std::min (sample.numMipLevels - 1, static_cast<int> (std::round (rho))));
  // ...
  if (sample_->numMipLevels > 1 && pitchRatio_ > 1.0)
      { /* cachedLog_ = std::log2 (pitchRatio_); */ rho = cachedLog_; }
  const ReadPlan plan = makePlan (*sample_, pitchRatio_, Tier, rho);
  // ...
  const double step = pitchRatio_ * speed_ * (rateMod ? static_cast<double> (rateMod[i]) : 1.0) * static_cast<double> (direction_);
  ```
  ```cpp
  inline int mipFor (const SampleView& s, double rate) noexcept
  {
      if (rate > 1.0 && s.numMipLevels > 1)
          return std::min (s.numMipLevels - 1, static_cast<int> (std::floor (std::log2 (rate))));
      return 0;
  }
  ```
- **(c) Surrounding context & tests**:
  1. In `SampleData::buildMipLevels()` (`core/sample-osc/src/sample_data.cpp:244-294`), mip level `0` is the raw unfiltered sample (bandwidth up to `0.5 * fs`), and each mip `m >= 1` is decimated by `2^m` with a 101-tap Kaiser filter (`cutoff = 0.42 * pi` of the parent level).
  2. In `SampleVoice::renderBlockT` (`sample_voice.cpp:917-926`) and `renderFrame` (`699,709`), mip selection uses `pitchRatio_` alone, completely ignoring `speed_` (`oscN.sample.scan`, which scales read rate up to `8x` via `scanRange = 1 << 3 = 8` in `lib_osc.cpp:433-434`) and `rateMod`.
  3. On `QualityTier::low` and `QualityTier::medium` (`sample_voice.cpp:156,267`), `makePlan` selects `m = std::round(std::log2(pitchRatio_))`, so for `1.0 < pitchRatio_ < sqrt(2) ≈ 1.4142` (up to `+5.99` semitones), `m = 0` is read at `step > 1.0` through a unit-cutoff (`sin(pi*d)/(pi*d)`) polyphase table (`sample_voice.cpp:400,420`).
  4. In `GranularVoice` (`granular.cpp:95,173,311,332`), `g.inc` includes `p.pitchRatio` and `randPitch` (`scanSpeed` moves the grain spawn cursor rather than intra-grain read rate), and `mipFor` uses `std::floor(std::log2(rate))`, selecting raw mip `0` for all `1.0 < rate < 2.0` (up to `+11.99` semitones!).
  5. Existing test `core/sample-osc/tests/test_sample_osc.cpp:515` (`sampleosc_aliasing_sanity_noise_pitch_up`) tests only MIDI note `84` vs root `60` (`+24` semitones, `pitchRatio = 4.0`, where `log2(4.0) = 2.0` is an exact integer so `floor == round == ceil == 2`), completely missing fractional-octave pitch-ups and `speed_ > 1.0`.
- **(d) Arithmetic**: `At 44.1 kHz, Sample (low/med) at pitchRatio = 1.41 (+5.95 st) computes round(log2(1.41)) = round(0.4957) = 0 (mip 0), aliasing source frequencies in [22050 / 1.41, 22050] = [15.64 kHz, 22.05 kHz] (and up to 3 octaves at speed_ = 8.0); Granular at rate = 1.99 (+11.91 st) computes floor(log2(1.99)) = 0 (mip 0), aliasing [22050 / 1.99, 22050] = [11.08 kHz, 22.05 kHz].`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 8 — `HOST`-locked LFOs read stale `globals.songSeconds` and never advance it per 32-sample control tick

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/own_engine.cpp:1371-1378,1154` (with `globals.songSeconds` consumed in `core/engine-own/voice/voice.cpp:434-438,530-534`).
- **(b) Decisive code** (`core/engine-own/own_engine.cpp:1154,1371-1380` and `core/engine-own/voice/voice.cpp:434-438`):
  ```cpp
  globals.songBeats += globals.playing ? dt * globals.bpm / 60.0 : 0.0;
  ```
  ```cpp
  globals.songSeconds = globals.songBeats * 60.0 / std::max (globals.bpm, 1.0);
  globals.sampleRate = sampleRate;
  globals.bpm = tpBpm.load (std::memory_order_relaxed);
  if (tpPlaying.load (std::memory_order_relaxed))
  {
      globals.playing = true;
      globals.songBeats = tpPos.load (std::memory_order_relaxed);
  }
  ```
  ```cpp
  if (host && g.playing && sync != 4)
  { const double c = static_cast<double> (rate) * g.songSeconds; clock = static_cast<float> (c - std::floor (c)); }
  ```
- **(c) Surrounding context & tests**:
  - The audit is **right** about the bug and its full consequence: in `OwnEngine::process()` (`own_engine.cpp:1371`), `globals.songSeconds` is computed *before* `tpBpm` and `tpPos` (`globals.songBeats`) are loaded from the atomic transport variables on lines 1373–1377, and `globals.songSeconds` is **never updated** inside `OwnEngine::controlTick()`. Because `Voice::lfoValue` (`voice.cpp:434-438`) and `Voice::lfoValueReference` (`voice.cpp:530-534`) compute `c = rate * g.songSeconds`, every `HOST`-locked LFO (`lfoN.host = 1`) reads the previous block's transport position and stays completely frozen across all 32-sample control ticks within the current host block, breaking block-size independence.
  - The audit is **partly wrong** on one sub-detail: it states that `controlTick()` never advances `songBeats` *or* `songSeconds`, whereas line `1154` (`globals.songBeats += globals.playing ? dt * globals.bpm / 60.0 : 0.0;`) **does** advance `globals.songBeats` on every 32-sample tick (in fact, because `controlTick()` runs at `controlPos == 0` right after `process()` loads `tpPos`, `songBeats` is pre-incremented before the first 32-sample chunk); `controlTick()` simply never recomputes `globals.songSeconds` from `globals.songBeats`. Existing test `core/tests/test_own_engine.cpp:702` (`own_output_does_not_depend_on_the_block_size`) does not enable `lfo1.host = 1` with active transport.
- **(d) Arithmetic**: `In a 512-frame host block at 44.1 kHz (512 / 32 = 16 control periods = 11.61 ms), globals.songSeconds is evaluated once from the prior block and held constant across all 16 control ticks (an 86.13 Hz staircase vs. 1378.125 Hz for a 32-frame block).`
- **(e) Verdict**: `PARTLY WRONG`

---

### Rank 9 — Per-sample `libm` and uncached divisions in hot filter paths

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/voice/filter_unit.h:157,190,210,221`, `core/filters-serum/src/filter_dsp.h:109,129,239`, `core/filters-serum/src/filter.cpp:1202,1219,1235`, and `core/engine-own/voice/classic_filters.h:568`.
- **(b) Decisive code** (`core/engine-own/voice/filter_unit.h:209-214`, `core/filters-serum/src/filter_dsp.h:109,129,239`, `core/engine-own/voice/classic_filters.h:568`):
  ```cpp
  for (int k = 0; k < n; ++k)
  {
      const float g = dbToGain (-24.0f * lerp (prevClean, curClean, static_cast<float> (t0 + k + 1) * invP));
      tmpL[k] = inL[k] * g;
      tmpR[k] = inR[k] * g;
  }
  ```
  ```cpp
  const float d = 1.0f / (1.0f + 2.0f * R * gSafe + gSafe * gSafe);
  // ...
  (void) d;
  // ...
  const float gHp = std::tan (kPi * 30.0f / static_cast<float> (sampleRate));
  ```
  ```cpp
  v1 = (ic1 + g * v3) / (1.0f + g * Rloop + g * g);
  ```
- **(c) Surrounding context & tests**: All 5 code patterns exist verbatim:
  1. `FilterUnit::process` (`filter_unit.h:157,190,210,221`): `curClean` and `prevClean` are `0.0f` or `1.0f` (`line 111`). Whenever `cleaning` is true (`curClean > 0.5f || prevClean > 0.5f`), the loop calls `dbToGain` (`std::pow(10.0f, db * 0.05f)`) twice per sample (`-24 dB` pre-attenuation and `+24 dB` post-boost) even when `prevClean == 1.0f && curClean == 1.0f`.
  2. `DiodeLadder::process` (`filter_dsp.h:239`): calls `std::tan(kPi * 30.0f / static_cast<float>(sampleRate))` on every sample per channel for a fixed 30 Hz cutoff.
  3. `LowEQ12`, `BandEQ12`, `HighEQ12` (`filter.cpp:1202,1219,1235`): call `biquad1_.setLowShelf`/`setPeakingEQ`/`setHighShelf` (`filter_dsp.h:292-340`) on every sample per channel (`std::pow`, `std::cos`, `std::sin`, `std::sqrt` for shelves, and 5 divisions by `a0`).
  4. `ClassicFilter::tpt` (`classic_filters.h:568`): divides by `(1.0f + g * Rloop + g * g)` every sample in `svfRun` and `svfPairLoopScalar`. Note that for stationary Digital SVF (`model == 3`) on NEON, `svfPairLoopDigitalNeon` (`classic_filters.h:259-260`) *does* hoist `den1` and `den2` out of the sample loop, though its `divide()` helper (`lines 238-239`) still performs two scalar divisions `a[0] / b[0]` and `a[1] / b[1]` per sample on 32-bit ARM instead of multiplying by `1.0f / den`.
  5. `SVFTPT::process` (`filter_dsp.h:109,129`): `const float d = 1.0f / (1.0f + 2.0f * R * gSafe + gSafe * gSafe);` is unused (`(void) d;`) because `dEff` uses `dCached` (`lines 114-127`). Note that under GCC `-O2`/`-O3` with default `-fno-trapping-math` (even with `#pragma GCC optimize ("fp-contract=off")`), a pure dead float division is normally dead-code-eliminated by the compiler, so whether `d` emits a `vdiv.f32` instruction requires checking the ARMv7 disassembly. Existing tests: `core/tests/test_own_filter_unit_lanes.cpp`, `core/filters-serum/tests/test_filters_serum.cpp`.
- **(d) Arithmetic**: `Per stereo sample: Clean Mode calls 2 std::pow; DiodeLadder calls 2 std::tan; LowEQ12/HighEQ12 call 2 std::pow + 2 std::sin + 2 std::cos + 2 std::sqrt + 10 divisions; 2-stage ClassicFilter SVF executes 4 scalar divisions (128 * 4 * 8 = 4096 divisions per 128-frame 8-voice block).`
- **(e) Verdict**: `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`

---

### Rank 10 — Every `FilterImpl` unconditionally allocates 14 delay lines and `LibOsc` pre-allocates 16 Sample + 16 Granular + 4 Spectral + 64 Multisample voices per osc×voice

- **(a) Cited vs. actual `file:line`**: `core/filters-serum/src/filter.cpp:39-60` matches; in `core/engine-own/osc/lib_osc.h`, the member declarations are at `lib_osc.h:93-95,193-226` and the vector allocations are in `core/engine-own/osc/lib_osc.cpp:127-145` (the audit cited `lib_osc.h:152-182`); `[measured: docs/QUALITY.md:127]` and `[measured: docs/MPC.md:327,354]` match.
- **(b) Decisive code** (`core/filters-serum/src/filter.cpp:40-60` and `core/engine-own/osc/lib_osc.cpp:127-130`):
  ```cpp
  const size_t maxDelaySamples = static_cast<size_t> (sampleRate_ * 0.06) + 64; // ~11500 samples
  combDelay1_.prepare (maxDelaySamples);
  combDelay2_.prepare (maxDelaySamples);
  // ...
  for (int i = 0; i < 8; ++i)
      diffuserDelays_[i].prepare (static_cast<size_t> (kDiffuserMs[i] * 0.001f * srFloat) + 16);
  for (int i = 0; i < 4; ++i)
      reverbCombs_[i].prepare (static_cast<size_t> (kReverbCombMs[i] * 0.001f * srFloat) + 16);
  reverbAP_[0].prepare (static_cast<size_t> (5.1f * 0.001f * srFloat) + 16, 0.7f);
  reverbAP_[1].prepare (static_cast<size_t> (12.7f * 0.001f * srFloat) + 16, 0.7f);
  ```
  ```cpp
  sampleVoices.assign (kMaxU, sampleosc::SampleVoice());
  grainVoices.assign (kMaxU, granular::GranularVoice());
  specVoices.assign (kSpecU, spectral::SpectralVoice());
  layers.resize (static_cast<size_t> (kSlots * kMaxU));
  ```
- **(c) Surrounding context & tests**: While the large unconditional heap footprint in both `FilterImpl` and `LibOsc` and the `95.6 ms` `prepare_voices` startup time `[measured: docs/MPC.md:327]` are real, three specific claims in the audit are wrong:
  1. **`FilterImpl` already sizes delay buffers to the active `sampleRate_`, not a fixed 96 kHz**: The audit recommends *"Size delay buffers to active `sampleRate` (not fixed 96 kHz)"*, but `filter.cpp:40-60` already computes every buffer length from `sampleRate_` (`srFloat`)! (The `180 KB per filter at 96 kHz (136 voice filters about 25 MB)` quote in `[measured: docs/QUALITY.md:127]` was for a 96 kHz prepare rate; at 44.1 kHz on MPC Key 61, each `FilterImpl` allocates `16` delay lines—`2` comb + `8` diffuser + `4` reverbComb + `2` reverbAP = **16**, not 14—totalling `16,779` floats = `67.1 KB` per `FilterImpl`, or `9.66 MB` across `36 voices * 2 units * 2 channels = 144` voice filters, plus `2.54 MB` for `ClassicFilter::comb`).
  2. **`LibOsc` allocates 256 `LayerVoice`s per slot, not 64**: In `lib_osc.h:93,95`, `kMaxU = 16` and `kSlots = multisample::kMaxLayerSlots = 16` (`8` active + `8` release zones in `core/multisample/include/synthyboy/multisample/multisample.h:69`), so `layers.resize(kSlots * kMaxU)` allocates `16 * 16 = 256` `LayerVoice` instances per `LibOsc` (`~31.5 MB` across `36 voices * 3 oscs = 108` `LibOsc` instances; `core/engine-own/README.md:45` states `~25 MB` for 32 voices).
  3. **Unused `FilterImpl` delay lines are not touched during note-on or rendering**: `DelayLine::reset()` (`filter_dsp.h:356-363`) and `ClassicFilter::reset()` (`classic_filters.h:84-88`) guard buffer clearing behind `if (written_)` / `if (combUsed)`, and `LibOsc::resetForNote()` (`lib_osc.cpp:170`) guards behind `if (playersUsed)`. The ~3x cold-cache multiplier in `[measured: docs/MPC.md:354]` was specifically measured on the **Spectral note-on priming** section (reading analysis frames and the active `SpectralVoice::ring` buffers for 8 voices), not from unused filter delay lines polluting L2 cache.
- **(d) Arithmetic**: `At 44.1 kHz, 144 voice FilterImpl instances allocate 144 * 16779 * 4 B = 9.66 MB (21.0 MB @ 96 kHz) across 16 DelayLines each, 72 ClassicFilter pairs allocate 144 * 4410 * 4 B = 2.54 MB, and 108 LibOsc instances allocate ~108 * 292 KB = 31.5 MB (total ~43.7 MB at 44.1 kHz / ~55 MB at 96 kHz plus wavetables).`
- **(e) Verdict**: `PARTLY WRONG`

---

### Rank 11 — Raw NEON float compares (`vcltq_f32`/`vcgtq_f32`/`vcgeq_f32`) remain in `wt_osc.h` and `core/spectral/src/kernels.cpp`

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/wt_osc.h:975,982`, `core/spectral/src/kernels.cpp:267,298,310-313,540,677,716`, and `[measured: docs/MPC.md:341,352]`.
- **(b) Decisive code** (`core/engine-own/osc/wt_osc.h:975,982` and `core/spectral/src/kernels.cpp:267,298,310-313,540,677,716`):
  ```cpp
  const uint32x4_t less = vcltq_f32 (x, vcvtq_f32_s32 (i));
  // ...
  w = vbslq_f32 (vcltq_f32 (w, vdupq_n_f32 (0.0f)), vaddq_f32 (w, vdupq_n_f32 (1.0f)), w);
  ```
  ```cpp
  for (int u = 0; u < U; ++u) half[u] = vbslq_f32 (vcltq_f32 (x[u], vdupq_n_f32 (0.0f)), vdupq_n_f32 (-0.5f), vdupq_n_f32 (0.5f));
  // ...
  for (int u = 0; u < U; ++u) y[u] = vbslq_f32 (vcltq_f32 (w[u], z), vaddq_f32 (w[u], twoPi), w[u]);
  // ...
  return vandq_u32 (vandq_u32 (vcgtq_f32 (c, p), vcgeq_f32 (c, q)), vcgtq_f32 (c, vth));
  ```
- **(c) Surrounding context & tests**: `core/engine-own/dsp/neon_cmp.h:5-8` and `[measured: docs/MPC.md:341]` explicitly document that on 32-bit ARM GCC without `-funsafe-math-optimizations`, NEON float compare intrinsics (`vcltq_f32`, `vcgtq_f32`, `vcgeq_f32`) are not lowered to `vcgt.f32`/`vcge.f32` (because NEON float compares do not raise the IEEE 754 Invalid Operation exception on quiet NaNs); instead GCC spills all 4 lanes to scalar VFP `vcmpe.f32` + `vmrs APSR_nzcv, fpscr`. Furthermore, `core/spectral/include/synthyboy/spectral/kernels.h:28-33` and `[measured: docs/MPC.md:352]` confirm that `findPeaksVector()` is explicitly disabled on 32-bit ARM (`!defined(__arm__)`) because those scalarized VFP compares made the NEON version slower than scalar (`18.7 us` vs `10.8 us` per call on Key 61). Existing tests: `core/tests/test_neon_cmp.cpp`, `core/tests/test_own_warp_lanes.cpp`, `core/spectral/tests/test_spectral.cpp`.
- **(d) Arithmetic**: `There are 12 raw NEON float compare calls across the two files (2 in wt_osc.h:975,982 and 10 in kernels.cpp:267,298,310,311,312,313,540,677,716), each expanding on ARMv7 GCC to 4 scalar VFP vcmpe.f32 + 4 vmrs flag transfers (48 scalar comparisons per vector pass) and costing 18.7 us vs. 10.8 us (+73.1%) in findPeaks [measured: docs/MPC.md:352].`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 12 — Spectral morph on `medium`/`high` tiers runs a full 2048-point complex IFFT and always builds a 2048-sample cycle even when `K <= 32`

- **(a) Cited vs. actual `file:line`**: `core/engine-own/osc/morph.cpp:440-478` matches; in `core/engine-own/osc/wt_osc.h`, `K` is computed at `wt_osc.h:572`, `buildMorphCycle` is called at `wt_osc.h:612`, and `N = Wavetable::kCycle` (`2048`) is fixed at `wt_osc.h:701` (the audit cited `wt_osc.h:578,701`).
- **(b) Decisive code** (`core/engine-own/osc/morph.cpp:440-478` and `core/engine-own/osc/wt_osc.h:612,701`):
  ```cpp
  if (fast)
  {
      constexpr int kM = kN / 2;
      // ...
      inverseFft (zr, zi, kM, scalarTransform, referenceBuild);
  }
  else
  {
      inverseFft (sc.re, sc.im, kN, scalarTransform, referenceBuild);
      for (int i = 0; i < kN; ++i) dst[i] = sc.re[i];
  }
  ```
  ```cpp
  buildMorphCycle (row[f], reA, imA, reB, imB, wB, tick.morph, tick.morphAmt, K, morphScratch, tick.lowMorph, scalarTransform_, reference_);
  ```
- **(c) Surrounding context & tests**:
  1. In `morph.cpp:218-227`, `put(i, ar, ai)` populates bins `1..K` and sets the conjugate bins `sc.re[kN - i] = ar`, `sc.im[kN - i] = -ai` (with DC and Nyquist zeroed). Because the spectrum is strictly Hermitian-symmetric, its inverse DFT is purely real on *all* tiers. Yet `wt_osc.h:612` only passes `fast = tick.lowMorph` (which is true only when `kEconomyMorph` is active on tier `low`), so `medium` and `high` tiers run the full 2048-point complex `inverseFft` (`morph.cpp:475`) and discard `sc.im`.
  2. On all tiers (including `low`), `kN` is hardcoded to `Wavetable::kCycle = 2048` (`morph.cpp:178`, `wt_osc.h:701`), whereas normal wavetable mip levels (`wavetable_bank.cpp:231-234`) use `size(m) = max(256, next_pow2(8 * harmonics[m]))`.
  3. Existing test `core/tests/test_own_morph_transform.cpp` verifies that the `fast = true` 1024-point real-packed IFFT matches the 2048-point complex IFFT to within float rounding (`~1e-6`).
- **(d) Arithmetic**: `A 2048-pt complex IFFT executes (2048 / 2) * log2(2048) = 11264 radix-2 butterflies vs. (1024 / 2) * log2(1024) = 5120 butterflies for the 1024-pt real-packed IFFT (2.20x more butterflies); for high notes with K <= 32, a 256-sample cycle via 128-pt real-packed IFFT needs only (128 / 2) * log2(128) = 448 butterflies (11.43x fewer than 1024-pt and 25.14x fewer than 2048-pt).`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 13 — Mipmap Gibbs cosine taper condition enters at `h > top * 4 / 5` (80%) while its cosine argument starts at `top * 85 / 100` (85%)

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/wavetable_bank.cpp:300-301`.
- **(b) Decisive code** (`core/engine-own/osc/wavetable_bank.cpp:299-304`):
  ```cpp
  double taper = 1.0;
  if (m > 0 && top > 4 && h > top * 4 / 5)
      taper = 0.5 + 0.5 * std::cos (kPi * static_cast<double> (h - top * 85 / 100) / static_cast<double> (top - top * 85 / 100 + 1));
  const double a = static_cast<double> (re[h]) * taper;
  const double b = static_cast<double> (im[h]) * taper;
  ```
- **(c) Surrounding context & tests**: This loop in `Wavetable::build()` constructs mip levels `m = 0..17`. The condition `m > 0 && top > 4` applies to mip levels `m = 1..13` (`top in {512, 256, 181, 128, 91, 64, 45, 32, 23, 16, 11, 8, 6}`). For every harmonic `h` in `(top * 4 / 5, top * 85 / 100)`, the numerator `h - top * 85 / 100` is **negative**. Because `cos(-x) == cos(x) < 1` for `x != 0`, `taper` abruptly drops below `1.0` at `h = top * 4 / 5 + 1`, climbs back up to `1.0` at `h = top * 85 / 100` (where `cos(0) = 1`), and only then rolls off toward `0` at `h = top`. Existing test `core/tests/test_own_engine.cpp:516` (`own_builtin_tables_match_the_checkpoints`) only checks `wt->cycle(frame, 0)` (level `m = 0`, where `m > 0` is false).
- **(d) Arithmetic**: `At mip level m = 1 (top = 512), top * 4 / 5 = 409 and top * 85 / 100 = 435 (denom = 512 - 435 + 1 = 78): at h = 409, taper = 1.0 (0 dB); at h = 410, taper = 0.5 + 0.5 * cos(pi * (-25) / 78) = 0.76791 (-2.29 dB notch); at h = 435, taper = 0.5 + 0.5 * cos(0) = 1.0 (0 dB); at h = 512, taper = 0.000203.`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 14 — `Combs` and `Reverb` share a single scalar `combFeedbackLP_` across all 4 parallel comb delay lines

- **(a) Cited vs. actual `file:line`**: Exact match at `core/filters-serum/src/filter.cpp:1294,1326` (with member `OnePoleTPT combFeedbackLP_;` declared at `filter.cpp:840`).
- **(b) Decisive code** (`core/filters-serum/src/filter.cpp:1290-1297,1322-1329`):
  ```cpp
  for (int i = 0; i < 4; ++i)
  {
      const float d = std::max (2.0f, std::min (baseDelay * kCombRatios[i], static_cast<float> (sampleRate_ * 0.05)));
      const float delayed = reverbCombs_[i].read (d);
      const float damped = combFeedbackLP_.processLPOnly (delayed, dampG);
      reverbCombs_[i].write (drivenIn + softClip (0.85f * res * damped));
      sum += delayed;
  }
  ```
- **(c) Surrounding context & tests**: `OnePoleTPT` (`core/filters-serum/src/filter_dsp.h:59-88`) holds a single integrator state `float s = 0.0f;` updated via `v = G * (in - s); lp = v + s; s = sanitize(lp + v);`. In both `FilterType::Combs` (`filter.cpp:1290-1297`) and `FilterType::Reverb` (`filter.cpp:1322-1329`), the 4 parallel comb delay lines `reverbCombs_[0..3]` all call `combFeedbackLP_.processLPOnly(delayed, dampG)` inside `for (int i = 0; i < 4; ++i)` on the same `combFeedbackLP_` member. Consequently, `combFeedbackLP_.s` does not integrate comb `i` over consecutive sample times `n - 1, n`; instead, within a single sample `n`, comb `0` overwrites `s` before comb `1` reads it, comb `1` overwrites `s` before comb `2`, etc., cross-coupling the 4 incommensurate delay lines and destroying independent per-line low-pass damping. Existing test `core/filters-serum/tests/test_filters_serum.cpp:244` only checks that output RMS is finite and non-negative.
- **(d) Arithmetic**: `In one sample n, s_i = 2 * G * delayed_i + (1 - 2 * G) * s_{i-1}, so comb line i receives (1 - G) * s_{i-1} = 2 * G * (1 - G) * delayed_{i-1} + ... from adjacent comb line i - 1 (reaching a peak inter-comb leakage gain of 2 * 0.5 * 0.5 = 0.5 = -6.02 dB at G = 0.5).`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 15 — `Voice::tick` never assigns `subTick.panRight`, leaving the Sub oscillator's right channel locked to center gain

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/voice/voice.cpp:1390` vs. `core/engine-own/osc/wt_osc.h:308` (with `float panRight = 0.0f;` default at `wt_osc.h:33`).
- **(b) Decisive code** (`core/engine-own/voice/voice.cpp:1389-1397` and `core/engine-own/osc/wt_osc.h:307-314`):
  ```cpp
  subTick.level = subCh.on ? sp.level * subGate : 0.0f;
  subTick.pan = std::clamp (sp.pan + (vc.mask[4] ? vc.pan : 0.0f), -1.0f, 1.0f);
  subTick.jump = g.noSmoothing;
  subTick.voices = 1;
  ```
  ```cpp
  const float pan = std::clamp (t.pan, -1.0f, 1.0f);
  const float panRt = std::clamp (t.panRight, -1.0f, 1.0f);
  float panL, panR;
  if (reference_)
  {
      panL = 1.41421356f * std::sin (kPif * 0.25f * (1.0f - pan));
      panR = 1.41421356f * std::sin (kPif * 0.25f * (1.0f + panRt));
  }
  ```
- **(c) Surrounding context & tests**: For the three main oscillators (`voice.cpp:1250-1251`), `Voice::tick` sets both `t.pan = std::clamp(op.pan + vcPan, -1.0f, 1.0f);` and `t.panRight = std::clamp(op.panRight + vcPan, -1.0f, 1.0f);`. For `subTick` (`voice.cpp:1381-1397`), `subTick.panRight` is never assigned anywhere in `Voice::tick` (nor in `Voice`), so it stays at its struct default `0.0f` (`wt_osc.h:33`). When `sub.update(subTick, ...)` runs (`wt_osc.h:308,313,325`), `panL` uses `t.pan` while `panR` uses `panRt = 0.0f`, holding `panR = 1.41421356f * sin(pi / 4) = 1.0f` regardless of `sub.pan` or `vc.pan`. Existing tests (`core/tests/test_own_engine_fixes.cpp:116`, `core/tests/test_own_voice.cpp:905`) only test `sub` at center pan `0.0f`.
- **(d) Arithmetic**: `At hard-left pan (sub.pan = -1.0), panL = sqrt(2) * sin(pi / 2) = 1.4142 (+3.01 dB) while panR = sqrt(2) * sin(pi / 4 * (1 + 0)) = 1.0 (0.00 dB instead of 0.0 = -inf dB); at hard-right pan (sub.pan = +1.0), panL = 0.0 while panR = 1.0 (0.00 dB instead of +3.01 dB).`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 16 — `FilterImpl` smooths cutoff in linear Hz with a fixed 5.5 ms (`180/sr`) time constant and no snap threshold, causing `tanM` misses

- **(a) Cited vs. actual `file:line`**: Exact match at `core/filters-serum/src/filter.cpp:141-143,176-177` (and `184-195`, plus `360-369` in `processPairT`).
- **(b) Decisive code** (`core/filters-serum/src/filter.cpp:141-143,176-195`):
  ```cpp
  const float smoothCoeff = std::min (0.05f, 180.0f / srFloat);
  smoothedCutoff_ += smoothCoeff * (targetCutoff - smoothedCutoff_);
  // ...
  const float cutoff = hold <= 1 ? smoothedCutoff_ : heldCutoff_;
  uint32_t cutoffBits;
  std::memcpy (&cutoffBits, &cutoff, 4);
  if (!haveG || cutoffBits != gCutoffBits)
  {
      g = tanM (kPi * cutoff / srFloat);
      gCutoffBits = cutoffBits;
      haveG = true;
  }
  ```
- **(c) Surrounding context & tests**:
  1. `FilterImpl::processBlock` (`filter.cpp:176-177`) and `processPairT` (`filter.cpp:360-361`) apply a per-sample 1-pole smoother `smoothedCutoff_ += (180.0f / srFloat) * (targetCutoff - smoothedCutoff_)` in **linear Hz** on top of the control-rate cutoff passed by `FilterUnit::set` (`core/engine-own/voice/filter_unit.h:133`, which passes only endpoint `b.cutoffHz` without `jump` except on note-on).
  2. Because the smoother operates in linear Hz with time constant `tau = 1 / 180 s = 5.556 ms`, a downward sweep from `16000 Hz -> 100 Hz` is still at `5949 Hz` (`5.9` octaves above target) after `5.56 ms` and takes `40.5 ms` to reach `200 Hz` (1 octave from target), whereas an upward sweep from `100 Hz -> 16000 Hz` crosses `200 Hz` (+1 octave) in `0.035 ms` (`1.5` samples).
  3. Because there is no snap threshold (`|targetCutoff - smoothedCutoff_| < eps`), `smoothedCutoff_` creeps asymptotically across distinct IEEE 754 float32 bit patterns for over `50 ms` after a cutoff change. Note one tier nuance: on tier `low`, `FilterUnit::set` (`filter_unit.h:129`) sets `p.coefHold = 4`, so `heldCutoff_` only updates every 4th sample (`filter.cpp:178-182,364-369`), giving `gCutoffBits` a 3-of-4 (`75%`) intra-block hold rate, **but** on every 4th sample (and on every sample when `coefHold == 1` on `medium`/`high` tiers), `tanM` (`16`-entry exact-bit hash table at `filter.cpp:780-791`) misses on every step of the asymptotic tail. Existing test `core/filters-serum/tests/test_filters_serum.cpp:514` (`serum_filters_jump_skips_the_cutoff_smoother`) explicitly measures this 270-sample (`5.6 ms` @ 48 kHz) smoother lag.
- **(d) Arithmetic**: `At fs = 44100 Hz, smoothCoeff = 180 / 44100 = 0.0040816 (tau = 245 samples = 5.556 ms); after a 1000 Hz step to 1000 Hz (float32 ULP = 2^-13 = 1.22e-4 Hz), smoothedCutoff_ keeps changing float32 bits until 0.0040816 * err < 0.5 * ULP => err < 0.015 Hz, which takes ln(1000 / 0.015) * 5.556 ms = 61.7 ms (~2722 samples, or ~680 tanM misses at coefHold = 4 and ~2722 misses at coefHold = 1).`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 17 — Granular Loop Grains crossfade reads `g.pos - loopLen`, which goes negative when `loopLo < xfade`, and only triggers for `step > 0`

- **(a) Cited vs. actual `file:line`**: Exact match at `core/granular/src/granular.cpp:485,739` (in `renderReference` at `478-490` and `renderFast` at `732-744`).
- **(b) Decisive code** (`core/granular/src/granular.cpp:725-744`):
  ```cpp
  if (wrap)
  {
      while (g.pos >= lg1) g.pos -= loopLen;
      while (g.pos < lg0) g.pos += loopLen;
  }
  float sl, sr;
  read.read (g.pos, sl, sr);
  if (wrap && xfade > 1.0)
  {
      const double toEnd = lg1 - g.pos;
      if (toEnd < xfade && step > 0.0)
      {
          const float t = static_cast<float> (1.0 - toEnd / xfade);
          float al, ar;
          read.read (g.pos - loopLen, al, ar);
  ```
- **(c) Surrounding context & tests**:
  1. When `g.pos` enters the loop-end crossfade zone `[lg1 - xfade, lg1)`, `g.pos - loopLen = g.pos - (lg1 - lg0) = lg0 - toEnd`, which lies in `[lg0 - xfade, lg0)` (before the loop start `lg0`).
  2. Whenever `lg0 < toEnd`—and in particular for the default `loopLo = 0.0f` (`lg0 = 0.0`), where `g.pos - loopLen` is negative across 100% of the crossfade window—`read.read(g.pos - loopLen, al, ar)` calls `hermiteFast` (`granular.cpp:112-130`) or `hermite` (`granular.cpp:80-89`), whose bounds guard `return k >= 0 && k < n ? d[k] : 0.0f;` returns **`0.0f` (silence)** for all negative indices. Thus the grain fades out to `0.0f` at `lg1` and then jumps discontinuously to `sample[lg0]` on the next sample when `g.pos >= lg1` wraps `g.pos -= loopLen`. (Compare `SampleVoice::renderBlockT` at `core/sample-osc/src/sample_voice.cpp:1005,1116`, which crossfades `[loopEnd - X, loopEnd)` against `[loopStart, loopStart + X)` inside the loop.)
  3. Because the condition is `if (toEnd < xfade && step > 0.0)` with no symmetric `step < 0.0` branch near `lg0`, reverse grains (`p.reverseGrains` or `p.randDir`) wrap abruptly from `lg0` to `lg1` with no crossfade at all.
  4. Existing tests in `core/granular/tests/test_granular.cpp:264,413` only test `loopGrains = true` with `loopLo = 0.25f` or `0.2f` (where `lg0 > xfade`), never testing default `loopLo = 0.0f` or reverse looped grains.
- **(d) Arithmetic**: `With default loopLo = 0.0 (lg0 = 0) and any xfade > 1.0, for all g.pos in [lg1 - xfade, lg1), g.pos - loopLen = -toEnd in [-xfade, 0) < 0, so al = ar = 0.0f across 100% of the crossfade window, attenuating the loop end by cos(pi / 2) = 0 before stepping to sample[0].`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 18 — `Voice::render` calls `runFilter(0)` and `runFilter(1)` unconditionally even when filters are OFF and unused

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/voice/voice.cpp:1751-1824`.
- **(b) Decisive code** (`core/engine-own/voice/voice.cpp:1751-1772,1818-1824`):
  ```cpp
  auto runFilter = [&] (int f)
  {
      touchFin (f);
      if (filtOn[f])
      {
          SB_SECTION (f == 0 ? sections::filter1 : sections::filter2);
          filt[f].process (fin_L[f], fin_R[f], fout_L[f], fout_R[f], n, t0);
      }
      else
      {
          std::copy (fin_L[f], fin_L[f] + n, fout_L[f]);
          std::copy (fin_R[f], fin_R[f] + n, fout_R[f]);
      }
  ```
- **(c) Surrounding context & tests**:
  - Note first why `runFilter(f)` exists when `filtOn[f] == false` *if* `usedFin[f] == true`: if an oscillator routes audio into Filter `f` while Filter `f` is turned OFF, `runFilter(f)` acts as a bypass that copies `fin` to `fout`, applies `filtLevel[f]`, and routes `fout` via `toMain(f)`.
  - However, when **both** `!filtOn[f] && !usedFin[f]` hold (as the audit specifically specifies—for example, Filter 2 is OFF and no oscillator or series Filter 1 routes into Filter 2, which is true on every 1-filter preset), `runFilter(f)` still executes unconditionally: it zeroes `fin_L[f]`/`fin_R[f]` via `touchFin(f)`, copies those zeros to `fout_L[f]`/`fout_R[f]`, builds the 32-sample gain ramp `gk` via `simd::rampLerp`, multiplies zeros by `gk` twice, half-sums zeros into `extNext[2 + f]`, and then in `toMain(f)` (`voice.cpp:1781-1786`) calls `toTarget(..., fout_L[f], fout_R[f], 1.0f)`, which marks `usedT` and adds 32 stereo zeros into the Main output bus (`tE_L[0]`/`tE_R[0]`) whenever `filtDest[f] != 2`! Existing test: `core/tests/test_own_voice_opt.cpp`.
- **(d) Arithmetic**: `For each inactive and unrouted filter (!filtOn[f] && !usedFin[f]) on an active voice, runFilter(f) + toMain(f) performs 2*32 zero-stores + 2*32 copies + 32 rampLerp + 2*32 mulBy + 32 halfSum + 2*32 addScaled = 352 wasted float operations per 32-sample period (11 float ops/sample/voice).`
- **(e) Verdict**: `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`

---

### Rank 19 — Lorenz and Rössler attractors use 4-substep explicit forward Euler (`h = 0.04`) and clamp `cycles` into `[0.001, 0.02]`

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/env_lfo/lfo.h:182-210`.
- **(b) Decisive code** (`core/engine-own/env_lfo/lfo.h:182-209`):
  ```cpp
  case 3: // Lorenz attractor
  case 4: // Rossler attractor
  {
      const float dt = std::min (0.02f * std::max (cycles, 0.001f) * 50.0f, 0.02f);
      const int steps = 4;
      for (int i = 0; i < steps; ++i)
      {
          if (style == 3)
          {
              const float dx = 10.0f * (ly - lx), dy = lx * (28.0f - lz) - ly, dz = lx * ly - (8.0f / 3.0f) * lz;
              lx += dx * dt / steps * 8.0f;
  ```
- **(c) Surrounding context & tests**:
  1. In line 185, `0.02f * std::max(cycles, 0.001f) * 50.0f` simplifies algebraically to `std::max(cycles, 0.001f)` because `0.02f * 50.0f == 1.0f`. Thus `dt = std::clamp(cycles, 0.001f, 0.02f)`.
  2. Both `Voice::tick` (`core/engine-own/voice/voice.cpp:945,979`) and `OwnEngine::controlTick` (`core/engine-own/own_engine.cpp:1177`) call `advance(style, rate * fdt)` once per 32-sample control period (`fdt = 32.0f / sampleRate`). Consequently, any RLFO rate below `0.001 * sampleRate / 32` or above `0.02 * sampleRate / 32` is hard-clamped to the exact same `dt` (`0.001f` or `0.02f`) and evolves at a fixed speed independent of the Rate knob.
  3. Inside the 4-substep explicit forward Euler loop (`lines 192-206`), each substep advances ODE time by `h = (dt / 4) * 8.0f = 2.0f * dt`, reaching `h = 0.04f` at the upper clamp `dt = 0.02f` (followed by a `!std::isfinite` reset guard at `lines 204-205`). Existing test: `core/tests/test_own_engine.cpp` (random patch fuzzing).
- **(d) Arithmetic**: `Since 0.02 * 50 = 1.0, dt = clamp(rate * 32 / f_s, 0.001, 0.02), which at f_s = 44100 Hz (f_s / 32 = 1378.125 Hz) freezes speed scaling below 0.001 * 1378.125 = 1.378 Hz and above 0.02 * 1378.125 = 27.5625 Hz (1.50 Hz and 30.00 Hz @ 48 kHz), with max Euler step h = (0.02 / 4) * 8 = 0.04.`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 20 — Vital-style unison gives the center voice `gainL = gainR = sa + sb`, creating a +3 dB amplitude (+6 dB power) bulge at `spread = 0`

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/wt_osc.h:257-271,384` (with `norm` at `wt_osc.h:247-250`).
- **(b) Decisive code** (`core/engine-own/osc/wt_osc.h:247-250,257-258,378-386`):
  ```cpp
  const float norm = 1.0f / std::sqrt (std::max (c1 * c1 + static_cast<float> (pairs - 1) * d1 * d1, 1.0e-12f));
  ac = c1 * norm;
  ad = d1 * norm;
  // ...
  sa = std::sin (kPif * 0.5f * (0.5f + 0.5f * w));
  sb = std::sin (kPif * 0.5f * (1.5f + 0.5f * w));
  // ...
  if (odd && i == 0)
  {
      addVoice (t.semis + stack, amp, sa + sb, sa + sb, 0.0f);
      continue;
  }
  ```
- **(c) Surrounding context & tests**:
  - For each outer pair (`i > 0`, or `i == 0` when `count` is even), two voices are added with `(panL, panR) = (sa, sb)` and `(sb, sa)`. Because `sa = sin(pi/4 * (1 + w))` and `sb = sin(pi/4 * (3 + w)) = cos(pi/4 * (1 + w))`, `sa^2 + sb^2 = 1.0` for all `w in [0, 1]`, so two uncorrelated outer voices of amplitude `ad` contribute `ad^2 * (sa^2 + sb^2) = ad^2` power per channel regardless of `spread` (`w`).
  - However, when `count` is odd (`odd && i == 0`), a **single** coherent center voice is added with `panL = panR = sa + sb`, giving it per-channel amplitude `ac * (sa + sb)` and per-channel power `ac^2 * (sa + sb)^2 = ac^2 * (1 + 2 * sa * sb) = ac^2 * (1 + cos(pi/2 * w))`. At `w = 1` (`spread = 100%`), `sa + sb = 1 + 0 = 1.0`, matching the unit power budget `ac^2 + (pairs - 1) * ad^2 = 1.0`. As `w -> 0` (`spread = 0%`), `sa + sb -> sqrt(2)/2 + sqrt(2)/2 = sqrt(2) ≈ 1.41421356`, boosting the center voice by `+3.01 dB` amplitude (`+6.02 dB` power)!
  - Even more strikingly, this branch also runs for **`count == 1` (`odd = true, pairs = 1, ac = 1.0f`)**: sweeping `oscN.unison.spread` (or `master.stereo_spread`) from `100%` to `0%` on a 1-voice mono wavetable oscillator boosts its volume by `+3.01 dB`! (Note also that `Voice::tick` sets `subTick.voices = 1; subTick.spread = 0.0f;` at `voice.cpp:1392-1393`, so `Sub` currently receives this `sqrt(2)` gain factor; any fix to `wt_osc.h:384` must account for `Sub`'s expected gain.) Existing tests (`core/tests/test_own_engine.cpp:660`, `core/tests/test_own_voice.cpp:555`) only test `voices = 4` (even).
- **(d) Arithmetic**: `For the center voice (odd && i == 0), panL = panR = sa + sb = sin(pi/4*(1+w)) + cos(pi/4*(1+w)) = sqrt(2)*cos(pi/4*w), which rises from 1.0 (0.00 dB) at w = 1 to sqrt(2) = 1.41421356 (+3.0103 dB amplitude / +6.0206 dB power) at w = 0 (raising total uncorrelated 3-voice RMS at blend = 1.0 from 0.00 dB to 10*log10(2*0.4^2/0.52 + 0.6^2/0.52) = +1.165 dB, and 1-voice RMS by +3.010 dB).`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 21 — Noise generators hardcode 44.1 kHz coefficients, skip `noise.filter`/`noise.pitch` for colours 4–8 and Sample mode, and hardcode `QualityTier::high`

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/voice/voice.cpp:41-82,1433,1715-1733` (plus `voice.cpp:1443` and `core/engine-own/dsp/noise_coeff.h:30`).
- **(b) Decisive code** (`core/engine-own/voice/voice.cpp:41-48,1433,1715-1735`):
  ```cpp
  case 1: // pink (three pole filter of white noise)
  {
      s[0] = 0.99765f * s[0] + w * 0.0990460f;
      s[1] = 0.96300f * s[1] + w * 0.2965164f;
      s[2] = 0.57000f * s[2] + w * 1.0526913f;
      return (s[0] + s[1] + s[2] + w * 0.1848f) * 0.25f * 0.82f;
  }
  ```
  ```cpp
  sp.quality = sampleosc::QualityTier::high;
  // ...
  if (noiseUseSample) { noiseSv.process (nl, nr, n); /* ... */ }
  else for (int k = 0; k < n; ++k) { /* ... */ if (noiseColour <= 3) shapeNoise (a, b); }
  ```
- **(c) Surrounding context & tests**:
  1. **Confirmed**: `noiseSample(int colour, uint32_t& rng, float* s)` (`voice.cpp:35-84`) does not take `sampleRate` as a parameter. Every pole coefficient (`0.99765, 0.963, 0.57` in Pink; `1/1.02` in Brown; `0.96` in Vinyl; period `6.0` in Digital Hash; `0.02` in Atmosphere) and every per-sample impulse probability (`0.0008` in Geiger; `0.0004` and `0.02` in Vinyl; `0.015` in Velvet) is hardcoded per sample. In addition, `computeNoiseCoeffs` (`core/engine-own/dsp/noise_coeff.h:30`) computes `c.lpC = 1.0f - std::exp(-kTwoPif * std::min(0.45f, 0.4f * rate) * 1.0f)` without scaling by `sampleRate`.
  2. **Confirmed**: `voice.cpp:1433` hardcodes `sp.quality = sampleosc::QualityTier::high;` for the noise `SampleVoice` regardless of `g.quality` (unlike `LibOsc::update` at `lib_osc.cpp:467`, which passes `static_cast<sampleosc::QualityTier>(std::clamp(lp.quality, 0, 2))`).
  3. **Confirmed for colours 4–8, partly wrong for Sample-mode `noise.pitch`**: For procedural colours `4..8` (Tape Hiss, Vinyl, Digital Hash, Velvet, Atmosphere), `if (noiseColour <= 3) shapeNoise(a, b);` (`voice.cpp:1733`) skips `shapeNoise`, which applies both `noise.pitch` (`noiseLpC`) and `noise.filter` (`noiseFC`). For Sample mode (`noiseUseSample == true`, `voice.cpp:1715-1725`), `shapeNoise` (`noise.filter`) is indeed skipped, **but** `noise.pitch` is **not** skipped: `voice.cpp:1443` computes `semi = P(ni.pitch) + P(ni.fine) * 0.01f + (P(ni.keytrack) > 0.5f ? float(note - 60) : 0.0f)` and passes it to `noiseSv.setSemitoneOffset(semi)`. Existing tests: `core/tests/test_own_voice.cpp:250`, `core/tests/test_own_noise_coeff.cpp`.
- **(d) Arithmetic**: `At 96 kHz vs. 44.1 kHz (ratio 96000 / 44100 = 2.1769x), Pink pole 0.99765 shifts corner frequency from -ln(0.99765)*44100/(2*pi) = 16.51 Hz to 35.95 Hz (2.18x), Geiger click rate 0.0008 * f_s jumps from 35.28 to 76.80 clicks/s (2.18x), and Sample-mode QualityTier::high costs 16 taps * 2 mips = 32 MACs/ch/sample vs. 4 MACs/ch/sample on QualityTier::low (8.0x more MACs when pitched up, 4.0x at unity).`
- **(e) Verdict**: `PARTLY WRONG`

---

### Rank 22 — Audio-rate mod sources are decimated to 1378 Hz via single-sample hold `buf[n-1]`, and `ModMatrix::evaluate` calls `std::expm1` (2x) and `std::exp` every 32 samples

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/voice/voice.cpp:1013-1019,1659,1771` (plus `1676` for Sub and `1741` for Noise) and `core/engine-own/matrix/mod_matrix.cpp:382,394-396,407`.
- **(b) Decisive code** (`core/engine-own/voice/voice.cpp:1658-1659,1770-1771` and `core/engine-own/matrix/mod_matrix.cpp:382,394-396`):
  ```cpp
  simd::halfSum (oscMono[o], oscL[o], oscR[o], n);
  srcLevel[o] = oscMono[o][n - 1];
  // ...
  simd::halfSum (extNext[2 + f], fout_L[f], fout_R[f], n);
  srcLevel[5 + f] = 0.5f * (fout_L[f][n - 1] + fout_R[f][n - 1]);
  ```
  ```cpp
  return held + (x - held) * (1.0f - std::exp (-ctx.dt * 2.302585f / (t * 0.001f)));
  // ...
  return 0.5f * std::copysign (powerShape (std::fabs (y), -c.power), y);
  ```
- **(c) Surrounding context & tests**:
  1. In `Voice::render` (`voice.cpp:1659,1676,1741,1771`), the 7 audio-rate modulation matrix sources (`kSrcOscA..kSrcFilter2`) store only the last sample `n - 1` of each chunk into `srcLevel[0..6]`, which `Voice::tick` (`voice.cpp:1013-1019`) copies into `src[]` once every 32 samples (`kControlPeriod = 32`). Note that `core/engine-own/README.md:38` explicitly acknowledges: *"What is not there yet: The audio rate evaluation of the matrix (the control period is 32 samples)."* (Note also that oscillator FM/RM/AM/PD *warps* use the full audio-rate `oscMono`/`extMono` buffers, not `srcLevel`; `srcLevel` is used when an oscillator/noise/sub/filter is selected as a modulation matrix source, as in `core/tests/test_own_engine.cpp:562`).
  2. In `ModMatrix::evaluate` (`mod_matrix.cpp:382,394-396,407`), each active slot with `|power| >= 1e-4f` calls `powerShape(x, -c.power)` (`core/engine-own/dsp/util.h:95-100`), which executes `std::expm1(power * x) / std::expm1(power)` (**two** `std::expm1` calls and one division), plus two more `std::expm1` calls if `|auxPower| >= 1e-4f`, and `std::exp` if slew (`riseMs > 0 || fallMs > 0`) is active—whereas `EnvSettings::fillDens()` (`core/engine-own/env_lfo/envelope.h:22-37`) already caches the invariant denominator `std::expm1(curve)`. (V4 `zeroSkip` at `mod_matrix.cpp:308` and V9 `sameSource` at `361,412` skip inactive unipolar slots and duplicate stereo lanes, but active curved slots still recompute `std::expm1(-c.power)` every 32 samples.)
- **(d) Arithmetic**: `Point-sampling an audio-rate source every 32 samples at 44.1 kHz (f_c = 44100 / 32 = 1378.125 Hz) folds all partials above f_c / 2 = 689.0625 Hz into [0, 689.0625 Hz]; each curved mono slot with aux curve and slew calls 4 std::expm1 + 2 divs + 1 std::exp every 32 samples.`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 23 — Wavetable mip-level selection switches abruptly at half-octave boundaries without crossfading

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/osc/wt_osc.h:672-687` (and `core/engine-own/osc/wavetable_bank.cpp:229,255-263`).
- **(b) Decisive code** (`core/engine-own/osc/wt_osc.h:680-687` and `core/engine-own/osc/wavetable_bank.cpp:255-263`):
  ```cpp
  else
  {
      level = Wavetable::levelFor (ratio);
      N = Wavetable::size (level);
      laneRatio[v] = ratio;
      laneLevel[v] = level;
  ```
  ```cpp
  int Wavetable::levelFor (float nyquistOverFundamental) noexcept
  {
      if (nyquistOverFundamental >= static_cast<float> (kHarm)) return 0;
      for (int m = 1; m < kNumLevels; ++m)
          if (static_cast<float> (maxHarmonics (m)) <= nyquistOverFundamental) return m;
      return kNumLevels - 1;
  }
  ```
- **(c) Surrounding context & tests**: `Wavetable::levelFor(ratio)` returns a single integer `level in [0, 17]`, and `wt_osc.h:710-711` fetches `cA = wt.cycle(f0, level)` (and `cB = wt.cycle(f0 + 1, level)`) at that single mip level with no crossfade to `level + 1`. Note two mitigating factors in the surrounding code: (1) for `m >= 2` (`maxHarmonics <= 256`, i.e., fundamentals `f0 >= 86.1 Hz` at 44.1 kHz), the mip ceiling table `{1024, 512, 256, 181, 128, 91, 64, 45, 32, 23, 16, 11, 8, 6, 4, 3, 2, 1}` steps in **half-octaves** (`~sqrt(2)`), so a level transition only drops partials above `0.35 * fs ≈ 15.4 kHz`; and (2) on levels `m = 1..13`, the top 15% of harmonics are already cosine-tapered toward zero (`wavetable_bank.cpp:300-301`). Because of those two mitigations, whether the integer mip switch is audible during pitch bends requires the named spectrogram/listening test. Existing test: `core/tests/test_own_wt_osc_opt.cpp`.
- **(d) Arithmetic**: `At f_s = 44100 Hz (nyq = 0.49 * 44100 = 21609 Hz), crossing a half-octave mip boundary (e.g. m = 5 -> 6, maxHarmonics 91 -> 64 at f_0 = 21609 / 91 = 237.46 Hz) drops partials h = 65..91 spanning [15.43 kHz, 21.61 kHz] in a single 32-sample control tick.`
- **(e) Verdict**: `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`

---

### Rank 24 — Multisample `LayerVoice` `lpf_1p` and `hpf_1p` fall through to 2-pole Butterworth RBJ biquads, and `LibOsc` steps multisample gain once per 32 samples

- **(a) Cited vs. actual `file:line`**: Exact match at `core/multisample/src/multisample.cpp:741-742` (`735-745`) and `core/engine-own/osc/lib_osc.cpp:975,991`.
- **(b) Decisive code** (`core/multisample/src/multisample.cpp:735-745` and `core/engine-own/osc/lib_osc.cpp:975,991`):
  ```cpp
  const bool onePole = r.filType == 1 || r.filType == 3;
  const double q = onePole ? 0.70710678 : 0.70710678 * std::pow (10.0, std::max (0.0, static_cast<double> (r.filResonanceDb)) / 20.0);
  const double alpha = sinw / (2.0 * q);
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
  switch (r.filType)
  {
      case 1: case 2: b0 = (1.0 - cosw) / 2.0; b1 = 1.0 - cosw; b2 = b0; a0 = 1.0 + alpha; a1 = -2.0 * cosw; a2 = 1.0 - alpha; break;                    // low pass
      case 3: case 4: b0 = (1.0 + cosw) / 2.0; b1 = -(1.0 + cosw); b2 = b0; a0 = 1.0 + alpha; a1 = -2.0 * cosw; a2 = 1.0 - alpha; break;                // high pass
  ```
  ```cpp
  const float t = static_cast<float> (t0 + n) * invP;
  // ...
  l.voice.processAdd (L, R, a, b, n, st, nullptr, lerp (uni[u].gPrevL, uni[u].gL, t), lerp (uni[u].gPrevR, uni[u].gR, t));
  ```
- **(c) Surrounding context & tests**:
  1. In `multisample.cpp:259-265`, SFZ `fil_type` parses `lpf_1p -> 1`, `lpf_2p -> 2`, `hpf_1p -> 3`, `hpf_2p -> 4`. Despite the comment at line `723` (`"RBJ biquads (one pole sections as a one pole at the same cutoff)"`), lines `735-742` merely force `q = 0.70710678` (`1 / sqrt(2)`) when `onePole` is true and fall through to `case 1: case 2:` / `case 3: case 4:`, computing a **2nd-order (-12 dB/oct) Butterworth biquad** identical to `lpf_2p`/`hpf_2p` at 0 dB resonance.
  2. In `LibOsc::render` (`lib_osc.cpp:900,937,968`), `Sample`, `Granular`, and `Spectral` all call `mixLane(L, a, uni[u].gPrevL, uni[u].gL, t0, n, invP)`, which linearly ramps gain sample-by-sample across the chunk via `simd::rampLerp`. By contrast, `kOscTypeMultisample` (`lib_osc.cpp:975,991`) evaluates `t = (t0 + n) * invP` once at the chunk endpoint and passes scalar constants `gainLeft` and `gainRight` to `LayerVoice::processAdd` (`multisample.cpp:825-826`). Existing test `core/multisample/tests/test_multisample.cpp:347` tests `fil_type=lpf_2p` only.
- **(d) Arithmetic**: `Cases 1 (lpf_1p) and 3 (hpf_1p) have 2nd-order transfer function H(z) with b_2 = b_0 != 0 and a_2 = 1 - alpha != 0, yielding -12.04 dB/octave asymptotic roll-off instead of -6.02 dB/octave; holding gain constant per 32-sample chunk steps amplitude at 44100 / 32 = 1378.125 Hz.`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 25 — Backward cross-modulation writes into `[0..n-1]` on mid-period event splits (`n < 32`), `declick` only covers Main bus (`tg == 0`), and idle global LFOs/RLFOs advance every 32 samples

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/voice/voice.cpp:302-304,1619-1623,1833-1835,1871-1883` and `core/engine-own/own_engine.cpp:1159-1178`.
- **(b) Decisive code** (`core/engine-own/voice/voice.cpp:1619-1623,1658,1871-1883`):
  ```cpp
  for (int j = 0; j < 3; ++j) if (j != o) cross[c++] = oscMono[j];
  for (int j = 0; j < 4; ++j) cross[2 + j] = extMono[j];
  // ...
  simd::halfSum (oscMono[o], oscL[o], oscR[o], n);
  ```
  ```cpp
  if (tg == 0)
  {
      for (int k = 0; k < n && declickPos > 0; ++k)
      {
          const float w = static_cast<float> (declickPos) * (1.0f / 64.0f);
          ml[k] += declickL * w;
          mr[k] += declickR * w;
          --declickPos;
      }
      lastL = ml[n - 1];
      lastR = mr[n - 1];
  }
  ```
- **(c) Surrounding context & tests**: All three sub-claims hold in the code, with important tier nuances:
  1. **Backward cross-mod on split periods (`n < 32`)**: When Osc 0 is modulated by Osc 1/2 (`oscMono[1..2]`) or by Noise/Sub/Filter 1/Filter 2 (`extMono[0..3]`), an unsplit 32-sample period (`t0 = 0, n = 32`) reads samples `0..31` of the *previous* period. If an event or host block boundary splits the period into `(t0 = 0, n = 10)` and `(t0 = 10, n = 22)`, then at the end of the first sub-chunk (`n = 10`), Osc 1 writes its new samples `0..9` into `oscMono[1][0..9]` (`line 1658`) and `extMono` swaps with `extNext` (`lines 1833-1835`, where only `[0..9]` were written!). In the second sub-chunk (`t0 = 10, n = 22`), Osc 0 still reads `cross[...][0..21]` starting at index `0` instead of `t0 = 10`, mixing 10 samples from the first sub-chunk with 12 stale samples! (Note: forward cross-mod, e.g. Osc 1 FM'd by Osc 0, is not corrupted because Osc 0 always writes `[0..n-1]` immediately before Osc 1 reads `[0..n-1]`; and on tier `low`, `eventGrid` at `own_engine.cpp:1458,1570` aligns MIDI events to 32-sample boundaries when the host block is a multiple of 32.)
  2. **`declick` only on Main (`tg == 0`)**: On `medium`/`high` tiers (`kEconomyControl` off), voice stealing uses `Voice::start(..., v->lastOutLeft(), v->lastOutRight())` (`own_engine.cpp:768`), which captures and fades only `tg == 0` (`voice.cpp:302-304,1871-1883`), leaving `tg in {1, 2, 3}` (Filter 1/2/Direct buses) un-declicked. (On tier `low`, `kEconomyControl` uses `ghostKill(*v)` at `own_engine.cpp:684-704,761`, which *does* fade all 4 buses `lastBusL[0..3]`/`lastBusR[0..3]`.)
  3. **Idle global LFO/RLFO stepping**: `OwnEngine::controlTick()` (`own_engine.cpp:1159-1178`) runs every 32 samples even when `activeMask == 0`, updating 10 global LFO phases and 4 random LFOs. Existing test `core/tests/test_own_engine.cpp:702` tests block-size invariance without backward cross-modulation.
- **(d) Arithmetic**: `On a 10+22 mid-period split, backward cross-mod reads indices [0..21] instead of [10..31] (corrupting all 22 samples of the second sub-chunk); on medium/high tiers, steal declick ramps 64 samples (1.45 ms @ 44.1 kHz) on bus 0 and 0 samples on buses 1..3; when idle (0 voices), controlTick() runs 1378.125 times/s @ 44.1 kHz across 10 LFOs + 4 RLFOs.`
- **(e) Verdict**: `CONFIRMED`

---

### Rank 26 — Classic `Ladder` computes 4 scalar divisions/sample/ch for algebraic `sat`, `Diode` computes 4–11 divs/sample, and `core/filters-serum` NEON pair kernels omit `Normal`, `AcidLadder`, `MGDirty`, and `Formant*`

- **(a) Cited vs. actual `file:line`**: In `core/engine-own/voice/classic_filters.h`, `sat(v)` is defined at `classic_filters.h:145` and called 4 times per sample in `ladderRun` (`627,635`), `ladderPairLoop` (`730-742`), and `diode` (`810-852`) (the audit cited `classic_filters.h:642,670-677,810-852`, which was slightly shifted by earlier lines); `core/filters-serum/src/filter.cpp:203-708` matches.
- **(b) Decisive code** (`core/engine-own/voice/classic_filters.h:145,627-636,838,844-846` and `core/filters-serum/src/filter.cpp:208-224`):
  ```cpp
  static float sat (float v) noexcept { return v - 0.9f * v * v * v / (v * v + 3.0f); }
  ```
  ```cpp
  const float s0 = sat (dv[0]), s1 = sat (dv[1]), s2 = sat (dv[2]), s3 = sat (dv[3]);
  // ...
  dp[0] = quick ? rhs[0] * invBb : rhs[0] / bb;
  for (int j = 1; j < 4; ++j)
      dp[j] = quick ? (rhs[j] + g * lo * dp[j - 1]) * invD[j] : (rhs[j] + g * lo * dp[j - 1]) / (bb + g * lo * cp[j - 1]);
  ```
  ```cpp
  if (fb.type_ != type || !(scream || ladder || chain || multi || flanges || ring) || fa.sampleRate_ != fb.sampleRate_ || fa.params_.coefHold != fb.params_.coefHold)
      return false;
  ```
- **(c) Surrounding context & tests**:
  1. In `ClassicFilter::ladderRun` (`classic_filters.h:627,635`) and `ladderPairLoop` (`730-742`), each sample calls `sat(y)` 4 times per channel (`4` divisions per channel = `8` divisions per stereo sample), plus 1 division per channel in `fastmath::tanh` (on tier `low`, where `z.invDen` avoids the feedback division) or 1 feedback division `/ (1 + k * G^4)` (on `medium`/`high`, where `std::tanh` is called).
  2. In `ClassicFilter::diode` (`classic_filters.h:810-852`), note that on tier `low` (`quick == fastMath == true`), lines `798-803` *already* precompute reciprocals `invBb`, `hpA`, `hfA`, and `invD[1..3]` for the Thomas tridiagonal solver and highpass stages! Thus on tier `low`, `diode` executes **4** `sat()` divisions (+ **1** `fastmath::tanh` Padé division = 5 divisions/sample/ch), whereas on `medium`/`high` (`quick == false`) it executes `4` (`sat`) + `4` (Thomas solver) + `3` (highpass stages, or `4` if `kDiodePre < 50.0f`) = **11** divisions/sample/ch.
  3. In `core/filters-serum/src/filter.cpp:208-224`, `processPairT` vectorizes `ScreamLP/BP`, `EMSLadder/MGLadder`, `Diffusor/Allpasses`, `LH..NN` (`Multi`), `CmbL..PhsHL` (`Flanges`), and `RingMod/RingModX2`, returning `false` (falling back to two separate scalar `processBlock` calls) for all `Normal` types (`MGLow6/12/18/24`, `Low/High/Band/Peak/Notch 12/24`), `AcidLadder`, `MGDirty`, `NewWsp`, `ExpMM`, `Formant*`, `Combs`, `Reverb`, `FrenchLP`, `GermanLP`, `SampleHold`, `Bitcrush`, and `EQ12`. Existing tests: `core/filters-serum/tests/test_filter_pair.cpp`, `core/tests/test_own_filter_styles.cpp`.
- **(d) Arithmetic**: `Per stereo sample, Classic Ladder executes 8 sat() divisions (+ 2 fastmath::tanh divisions on tier low or 2 feedback divisions on med/high = 10 divisions/stereo sample), and Classic Diode executes 8 sat() + 2 fastmath::tanh = 10 divisions/stereo sample on tier low (or 2 * 11 = 22 divisions/stereo sample on med/high).`
- **(e) Verdict**: `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`

---

### Rank 27 — Global bit-exact scalar=NEON policy (`-ffp-contract=off`, no FMA, no `vrecpe`/`vrsqrte`) and UB `union` type-punning in `fastmath.h`

- **(a) Cited vs. actual `file:line`**: Exact match at `core/engine-own/dsp/simd.h:1-35`, `core/engine-own/dsp/fastmath.h:37,44`, and `core/filters-serum/src/filter.cpp:9-11` (plus identical `#pragma GCC optimize ("fp-contract=off")` in `core/engine-own/osc/morph.cpp:9`, `core/sample-osc/src/sample_voice.cpp:7`, `core/granular/src/granular.cpp:9`, and `core/spectral/src/kernels.cpp:7`).
- **(b) Decisive code** (`core/engine-own/dsp/simd.h:33-35` and `core/engine-own/dsp/fastmath.h:37-39,44-47`):
  ```cpp
  #if SB_ARM_LINUX_OR_32 && defined(__GNUC__) && !defined(__clang__)
   #pragma GCC optimize ("fp-contract=off")
  #endif
  ```
  ```cpp
  union { uint32_t u; float v; } s;
  s.u = static_cast<uint32_t> (n + 127) << 23;
  return p * s.v;
  // ...
  union { uint32_t u; float v; } s;
  s.v = x;
  const int e = static_cast<int> (s.u >> 23) - 127;
  ```
- **(c) Surrounding context & tests**:
  1. The codebase has an explicit, documented design policy (`simd.h:1-3`, `filter.cpp:5-8`, `docs/MPC.md:336-352`) that NEON vector loops perform the exact same non-fused multiply and add operations in the same order as the scalar code (`#pragma GCC optimize ("fp-contract=off")` on ARM Linux / arm32 GCC, using `vmla.f32` / `vmul + vadd` rather than `vfma.f32` and true divisions rather than `vrecpeq_f32`/`vrsqrteq_f32`), so that `tier_bench --verify` can assert bit-for-bit equality (`max_abs_diff = 0`) between NEON and scalar builds. Relaxing this policy is an engineering trade-off whose speedup on Cortex-A17 (MPC Key 61) and ARM64 (MPC XL) must be measured.
  2. In `core/engine-own/dsp/fastmath.h:37-39,44-48`, `fastmath::exp2` and `fastmath::log2` write to one member of `union { uint32_t u; float v; }` and read from the other, which is undefined behavior in standard C++ (`[class.union]`), whereas `core/engine-own/dsp/neon_cmp.h:14-19` and `core/filters-serum/src/filter.cpp:188,780` properly use `std::memcpy(&u, &x, sizeof(u))`.
- **(d) Arithmetic**: `A 32-bit IEEE 754 float copy via std::memcpy(&u, &x, 4) compiles to 0 extra instructions (1 register move), while a 2-step Newton-Raphson vrecpeq_f32 + 2x vrecpsq_f32 achieves ~23-bit mantissa accuracy (~1.2e-7 relative error ≈ -138 dB) vs. scalar VFP vdiv.f32 (~14–18 cycles/lane on Cortex-A17).`
- **(e) Verdict**: `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`

---

## Seen in Passing

1. **`core/engine-own/osc/warp.h:59` vs. `core/engine-own/osc/warp.h:148`**: `warpBandwidthFactor` computes `kWarpPulse` bandwidth as `1.0f / std::max(1.0f - 0.9f * a, 0.08f)` (up to `12.5x`), whereas `warpPhase` actually implements `kWarpPulse` with duty `d = std::max(0.05f, 1.0f - 0.7f * a)` (max compression `1 / 0.3 = 3.33x`), over-dulling the pre-warp wavetable mip selection on `kWarpPulse` at high `a`.
2. **`core/engine-own/voice/classic_filters.h:940,973`**: In `ClassicFilter::formant`, the post-filter tilt state `tilt += 0.35f * (y - tilt)` uses a hardcoded per-sample coefficient `0.35f` with no `sampleRate` scaling.
3. **`core/engine-own/dsp/noise_coeff.h:30`**: `computeNoiseCoeffs` computes the noise pitch low-pass coefficient `c.lpC = 1.0f - std::exp(-kTwoPif * std::min(0.45f, 0.4f * rate) * 1.0f)` without dividing by `sampleRate` (unlike `c.fC` three lines below at `noise_coeff.h:33`).
4. **`core/engine-own/own_engine.cpp:1154` vs. `core/engine-own/own_engine.cpp:1377,1406`**: When `controlPos == 0` at the start of `OwnEngine::process()`, `globals.songBeats` is loaded from `tpPos` at line `1377` and then immediately incremented by `dt * globals.bpm / 60.0` inside `controlTick()` at line `1154` *before* the first 32 samples are rendered, shifting `songBeats` one control period ahead of host `tpPos`.
5. **`core/engine-own/osc/lib_osc.cpp:550-569`**: If both spectral warp slots (`k = 0` and `k = 1`) select an external warp on the same source `e`, the `for (int k = 0; k < 2; ++k)` loop computes the exact same 2048-point external FFT and `1025` `std::sqrt` calls **twice** within the same 32-sample `LibOsc::update()` call.
6. **`core/engine-own/README.md:13`**: Outdated documentation states that `WavetableBank` builds *"21 half-octave mip levels stored at 2x their band: the eight 64 frame tables take 20.5 MB"*, whereas `core/engine-own/osc/wavetable_bank.h:20,29` and `wavetable_bank.cpp:229-234` build 18 mip levels stored at `>= 8x` their highest harmonic (minimum 256 samples, ~29 MB time + ~4.2 MB spectrum).

---

Nothing under the forbidden paths (`core/engine-vital/`, `core/vital-import/`, or `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/`) was opened, read, grepped, or listed.
