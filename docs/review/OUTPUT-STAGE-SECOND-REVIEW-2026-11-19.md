# Second Independent Review: Output Stage & Shell Integrations

**Date:** 2026-11-19  
**Review Target:** `core/include/synthyboy/output_stage.h` and its callers:
- MPC VST2 shell: `shell-mpc/synthyboy_mpc.cpp`, `shell-mpc/output_stage.h`
- CLAP shell: `shell-clap/synthyboy_clap.cpp`, `shell-clap/synthyboy_plugin.h`
- JUCE VST3/AU shell: `shell-juce/PluginProcessor.h`, `shell-juce/PluginProcessor.cpp`
- Offline tools: `tools/preset-render/level_probe.cpp`, `tools/preset-render/preset_render.cpp`, `tools/sound-lab/sound_lab.cpp`
- Test suites: `shell-mpc/tests/output_stage_host.cpp`, `shell-mpc/tests/output_stage_lanes_host.cpp`, `core/tests/test_output_stage_presets.cpp`  
**Reference Specifications:**
- `docs/MPC.md` (Sections: "Output level", "Output limiter")
- `docs/release/LEVELLING-STOCK-2026-11-19.md`

---

## Executive Summary

The second independent review examined `synthyboy::OutputStage` (`core/include/synthyboy/output_stage.h`) and its shell integrations across MPC VST2, desktop CLAP, and desktop JUCE VST3/AU wrappers.

The core DSP architecture in `core/include/synthyboy/output_stage.h` is exceptionally well-engineered:
- The streaming van Herk / Gil-Werman running minimum filter combined with two cascaded integer moving averages ($B_1 + B_2 - 1 = W = D + 1$) analytically bounds output peaks to $kCeiling = 0.94406088f$ ($-0.5\text{ dBFS}$) before the safety hard clip.
- Division is completely eliminated on the audio thread using `recipBelow()` (integer seed and 3 Newton-Raphson steps from below with a $1 - 2^{-20}$ safety headroom).
- Zero allocations, zero mutexes/locks, zero libm calls per sample, and integer accumulator arithmetic guarantee real-time safety.
- ARM NEON vectorization (`processLanes()`) on ARMv7/AArch64 is bit-for-bit identical to the scalar loop in both default IEEE float mode and flush-to-zero mode.
- Idle bit-exactness is mathematically and experimentally verified: when audio is below the ceiling and the limiter is settled, the output is bit-identical to the gained input delayed by $D$ samples.

Three implementation issues were identified in the host wrappers and documentation:
1. **JUCE wrapper missing `AudioProcessor::reset()` / `releaseResources()`** (Severity: **MEDIUM-HIGH**): Host flushes/locates leave stale audio in the 3 ms delay line.
2. **MPC wrapper `initialDelay` latency reporting inconsistency** (Severity: **MEDIUM**): `e->initialDelay` is set at instance creation and `effSetSampleRate`, but is never updated when `effSetChunk` restores `"mpcOutputLimiter": 0` or when LIMIT is toggled on the UI, causing DAW delay compensation mismatch.
3. **Stale comment in `synthyboy_mpc.cpp:2480`** (Severity: **LOW**): Code comment states `(the default is off)` while `kOutputLimiterDefaultOn = true` and documentation specify default ON.

---

## Checklist Findings

### (a) Real-Time Safety on the Audio Thread
- **Status:** **PASS**
- **Evidence:**
  - `core/include/synthyboy/output_stage.h:170-186` (`process`): Declared `noexcept`. No dynamic memory allocations (`new`, `malloc`, `std::vector`), no syscalls, and no locks or condition variables.
  - `core/include/synthyboy/output_stage.h:176, 185`: Atomic operations are strictly `std::memory_order_relaxed` (`wanted.load(...)`, `lastMinGain.store(...)`), which compile to single register load/stores with zero lock contention.
  - `core/include/synthyboy/output_stage.h:211-289` (`detect`):
    - Van Herk running minimum filter: Constant work per sample ($O(1)$ amortized). Forward pass performs 2 `std::max` calls. The backward scan (`for (int k = W - 1; k >= 0; --k)`) executes once every $W$ samples ($W \le 1024$), taking $< 150\text{ ns}$ entirely in L1 cache.
    - Cascaded moving averages: Implemented using fixed-size static ring buffers `ring1[512]`, `ring2[1024]` and unsigned integer accumulators `sum1`, `sum2` scaled by $kQ = 2^{22}$. Uses bit shift `>> shift1` and integer operations, completely avoiding floating-point drift, denormals, or divide instructions.
    - Newton-Raphson reciprocal `recipBelow()` (`core/include/synthyboy/output_stage.h:197-208`): Integer seed via bit-manipulation (`0x7EF311C7u - u`) followed by three unrolled quadratic convergence steps. Zero hardware division instructions.
    - Denormal prevention in envelopes: Two-stage release envelopes snap to target when $\Delta < 1.0\times 10^{-5}$ (`kSnap = 1.0e-5f`, lines 247, 257). State variable `s` resets to 0.0f after $W$ calm samples (line 281). `curGain` snaps when $|\Delta| < 10^{-6} \times targetGain$ (line 312).
  - Test verification: `shell-mpc/tests/output_stage_host.cpp:73` enforces `static_assert (noexcept (std::declval<OutputStage&>().process ((float*) nullptr, (float*) nullptr, 0)))`. Measured execution time is $0.431\ \mu\text{s}$ (idle) and $1.313\ \mu\text{s}$ (limiting) per 128-frame block ($0.015\%\dots 0.045\%$ CPU budget).

---

### (b) Denormals / Flush-To-Zero Behaviour on arm32 NEON (VFPv4) and arm64
- **Status:** **PASS**
- **Evidence:**
  - `core/include/synthyboy/output_stage.h:383-393` (`finiteMul`):
    - Lane-wise integer bitmasking extracts absolute exponent/mantissa: `vandq_u32 (vreinterpretq_u32_f32 (x), absMask)`.
    - Floating-point multiply `vmulq_f32` is unconditionally executed.
    - **arm32 NEON (VFPv4)**: In the ARMv7-A Cortex-A9/A17 hardware NEON architecture, the SIMD pipeline is hardwired to Flush-to-Zero (FTZ) for all subnormal operands and outputs.
    - **arm64 (AArch64)**: Respects `FPCR.FZ`. In the host shells (`shell-mpc/synthyboy_mpc.cpp:3025-3026` via `flushDenormalsOn()`, `shell-juce/PluginProcessor.cpp:339` via `juce::ScopedNoDenormals`), `FPCR.FZ` and `MXCSR.FTZ+DAZ` are explicitly set during audio callbacks.
    - Subnormal integer magnitude comparison (`core/include/synthyboy/output_stage.h:420`): `vcleq_u32(..., bitsOf(kCeiling))` safely treats subnormal floats ($< 0x00800000$) as below ceiling ($0x3f71af87$) without invoking floating-point hardware comparison traps.
  - Test verification: `shell-mpc/tests/output_stage_lanes_host.cpp:31-33, 138-157` verifies $1,900,000$ samples with explicit subnormals (`1.0e-40f`, `1.1754944e-38f`) across 44.1, 48, and 96 kHz under both default IEEE mode and `FPCR.FZ = 1`. NEON and scalar outputs are bit-for-bit identical ($0$ differences).

---

### (c) Block Sizes 1..4096 and sampleFrames Changes Between Calls
- **Status:** **PASS**
- **Evidence:**
  - `core/include/synthyboy/output_stage.h:170-186` (`process`): Processing is completely sample-by-sample and state-preserving.
  - `core/include/synthyboy/output_stage.h:403-448` (`processLanes`): The NEON loop processes multiples of 4 samples while `i + 4 <= n`, `w + 4 <= kRing`, and `rd + 4 <= kRing`. Whenever remaining samples are $< 4$, or near ring wrap boundaries, it seamlessly falls back to `step()` at line 413.
  - Block size $n = 1\dots 4096$: Circular delay ring buffers `dl[1024]` and `dr[1024]` use bitwise modulo masking `w = (w + 1) & kMask` (`kMask = 1023`). Since look-ahead delay $D \le 1023$, write pointer `w` and read pointer `rd = (w - D) & kMask` maintain continuous circular delay across arbitrarily large block boundaries.
  - Test verification: `shell-mpc/tests/output_stage_host.cpp:207-218` tests variable block sequences `{ 1, 2, 7, 32, 64, 128, 17, 3, 500 }` and confirms bit-identical output compared to a single monolithic block.

---

### (d) Sample-Rate Change (44.1 / 48 / 96 kHz) Re-Prepares Look-Ahead Sizes Correctly
- **Status:** **PASS**
- **Evidence:**
  - `core/include/synthyboy/output_stage.h:71-100` (`lookaheadFor`, `prepare`):
    - Rate sanitization: `safeRate(sr)` clamps rates to $[8000.0, 384000.0]$ and maps non-finite rates to $44100.0$.
    - Look-ahead calculation: $D = \text{std::clamp}(\text{lround}(0.003 \times rate), 1, 1023)$.
      - $44.1\text{ kHz}$: $D = 132$, $W = 133$, $B_1 = 64$ (`shift1 = 6`), $B_2 = 70$.
      - $48.0\text{ kHz}$: $D = 144$, $W = 145$, $B_1 = 64$ (`shift1 = 6`), $B_2 = 82$.
      - $96.0\text{ kHz}$: $D = 288$, $W = 289$, $B_1 = 128$ (`shift1 = 7`), $B_2 = 162$.
    - Moving average constraints: At all rates up to $384\text{ kHz}$, $B_1 \le 512$ (`kRing1`), $B_2 \le 1024$ (`kRing2`), $W \le 1025$ (`kMaxLookahead + 2`), and $B_1 + B_2 - 1 = W = D + 1$.
    - `prepare()` automatically calls `reset()` at line 100.
  - Wrapper invocations:
    - MPC shell (`shell-mpc/synthyboy_mpc.cpp:2307, 2354`): `effSetSampleRate` updates `inst->sampleRate` and `initialDelay`. `effMainsChanged` detects `sr != lastPreparedSampleRate` and calls `inst->outputStage.prepare(safeSr)`.
    - CLAP shell (`shell-clap/synthyboy_clap.cpp:184-187`): `clap_activate()` re-prepares `self->outputStage.prepare(self->sampleRate)`.
    - JUCE shell (`shell-juce/PluginProcessor.cpp:315-317`): `prepareToPlay()` re-prepares `outputStage.prepare(sampleRate)` and updates `setLatencySamples()`.

---

### (e) Ceiling Guarantee: Can Any Sample Exceed 0.94406088?
- **Status:** **PASS**
- **Evidence:**
  - `core/include/synthyboy/output_stage.h:52`: `kCeiling = 0.94406088f` ($-0.5\text{ dBFS}$).
  - Mathematical proof of attack ramp:
    - Peak detection: `p = max(|a|, |b|)`. If $p > kCeiling$, target reduction is $rq = kCeilingSafe \times recipBelow(p)$.
    - `kCeilingSafe = kCeiling * (1 - 2^{-20})` (`line 192`).
    - `recipBelow()` approaches $1/p$ strictly from below via Newton-Raphson ($y_{k+1} = y_k(2 - p y_k) \le 1/p$). The $2^{-20}$ safety margin fully absorbs float rounding errors.
    - $rd = 1 - rq \ge 1 - (kCeiling / p)$.
    - Van Herk running maximum maintains $hr(t) \ge rd(t_0)$ for all $t \in [t_0, t_0 + D]$.
    - Fast release follower $red$ attacks instantaneously: `if (hr >= red) red = hr;`.
    - Quantization (`lines 260-265`): Reduction is rounded UP (`ceil(red * 2^22)`), ensuring quantized linear gain $q = 2^{22} - t$ is rounded DOWN.
    - Cascaded moving averages: Two boxcar FIR filters ($B_1$ and $B_2$, $B_1 + B_2 - 1 = D + 1$) have non-negative coefficients summing to $1.0$. The output gain $g(t_0 + D)$ when peak $p$ leaves the delay line is a convex combination of gains that are all $\le rq(t_0)$.
    - Hence: $|da| \times g(t_0 + D) \le p \times (kCeiling / p) = kCeiling$.
  - Hard safety clip:
    - `core/include/synthyboy/output_stage.h:291-304, 364`: `clip(la, clips)` clamps any sample exceeding $kCeiling$ to $\pm kCeiling$. In test runs over all 160 factory presets (`test_output_stage_presets.cpp`), `safetyClips` is strictly 0.
  - Crossfade on limit switch (`core/include/synthyboy/output_stage.h:371-376`):
    - When switching towards ON (`target == true`, $0 < mix < 1$), `oa = clip(oa, fadeClips)` explicitly clips the mixed signal to $kCeiling$.
    - When switching OFF (`target == false`), the limiter is deliberately bypassed by user choice, so signals may exceed $kCeiling$ as intended.
  - Non-finite inputs (`lines 317-322`): NaNs and infinities are replaced with $0.0f$ before entering the delay line.

---

### (f) Parameter Smoothing of the Make-Up Gain (20 ms) and Click-Free Changes
- **Status:** **PASS**
- **Evidence:**
  - `core/include/synthyboy/output_stage.h:60, 96`: `kGainSmoothingSeconds = 0.020f`. Coefficient is $smCoef = \exp(-1 / (0.020 \times rate))$.
  - `core/include/synthyboy/output_stage.h:135-141` (`setGainDb`): Clamps gain to $[-24.0, +24.0]\text{ dB}$ and computes target linear gain `targetGain`.
  - `core/include/synthyboy/output_stage.h:309-314` (`step`):
    - Smooths per sample: `curGain = targetGain + (curGain - targetGain) * smCoef;`
    - Snap threshold: `if (std::fabs (curGain - targetGain) < 1e-6f * targetGain) curGain = targetGain;`. Because `targetGain >= 0.063`, snap threshold is $\ge 6.3 \times 10^{-8}$, preventing denormals.
    - While `curGain != targetGain`, the NEON loop in `processLanes` falls back to `step()` (line 411), ensuring sample-accurate exponential smoothing.
  - Test verification: `shell-mpc/tests/output_stage_host.cpp:113-128` confirms a 12 dB gain step changes monotonically without steps $> 2\%$, reaching within $1\%$ in $100\text{ ms}$ without zipper noise.

---

### (g) State Reset on Prepare / Flush / Chunk Load
- **Status:** **FAIL**
- **Evidence:**
  - `prepare`: `OutputStage::prepare()` calls `reset()` (`core/include/synthyboy/output_stage.h:100`). All three shells call `prepare()` on startup and sample rate changes.
  - `flush` (transport stop/locate/reset):
    - MPC shell (`shell-mpc/synthyboy_mpc.cpp:2779`): `effStartProcess` calls `inst->outputStage.reset()`; `effMainsChanged` calls `outputStage.reset()` (line 2365).
    - CLAP shell (`shell-clap/synthyboy_clap.cpp:216`): `clap_reset()` calls `self->outputStage.reset()`.
    - JUCE shell (`shell-juce/PluginProcessor.cpp`): **FAIL**. `SynthyBoyProcessor` does NOT override `juce::AudioProcessor::reset()` or `releaseResources()`. When a JUCE host stops transport or locates to a new timeline position, `outputStage.reset()` is never called.
  - `chunk load`:
    - In all wrappers (`synthyboy_mpc.cpp:2482`, `synthyboy_clap.cpp:588`, `PluginProcessor.cpp:870`), loading a chunk/preset does not reset `OutputStage`. While skipping `reset()` on an active audio thread avoids data races on non-atomic arrays (`dl`, `dr`, `ring1`, `ring2`), no reset is queued when loading presets while stopped.
- **Concrete Input Triggering Fail:**
  - In a JUCE host (e.g. Reaper, Ableton, Logic), play audio with the limiter actively reducing gain, then press Stop and jump to the start of the project. Because `AudioProcessor::reset()` is not overridden, `outputStage` is not reset; when playback begins, the first 3 ms (144 samples at 48 kHz) output stale audio from `dl`/`dr` and start with stale gain reduction from `sum1`/`sum2`.
- **Suggested Fix:**
  - Implement `void SynthyBoyProcessor::reset() override { if (engine) engine->reset(); outputStage.reset(); }` in `shell-juce/PluginProcessor.cpp`.

---

### (h) Latency Reporting to the Hosts (initialDelay / Delay Compensation) in Each Wrapper
- **Status:** **FAIL**
- **Evidence:**
  - Look-ahead latency of `OutputStage`: $D$ samples when limiter is ON, $0$ samples when OFF (`core/include/synthyboy/output_stage.h:148`).
  - CLAP shell (`shell-clap/synthyboy_clap.cpp:383-389, 634-637`): Accurately reports `engine->latencySamples() + outputStage.latencySamples()` via `ext_latency_get()`. On latency change, notifies host via `host->request_restart()`.
  - JUCE shell (`shell-juce/PluginProcessor.cpp:317, 485-487`): Reports `engine->latencySamples() + outputStage.latencySamples()` in `prepareToPlay()`. Updates `setLatencySamples()` in `processBlock()` if latency changes (though limiter is always ON on desktop).
  - MPC shell (`shell-mpc/synthyboy_mpc.cpp:2307, 3199`):
    - Sets `e->initialDelay = lookaheadFor(opt)` during `effSetSampleRate` and `VSTPluginMain`.
    - **FAIL 1:** When `effSetChunk` loads a project containing `"mpcOutputLimiter": 0` (`shell-mpc/synthyboy_mpc.cpp:2482-2486`), `inst->applyOutputLimiter()` switches the DSP path to 0 latency, but `e->initialDelay` is NEVER updated and remains at 132/144 samples.
    - **FAIL 2:** When the user toggles LIMIT on the Master page UI (`shell-mpc/synthyboy_mpc.cpp:476-480`), `setOutputLimiter()` does not update `e->initialDelay` or issue `audioMasterIOChanged`.
    - **FAIL 3:** MPC shell completely omits `engine->latencySamples()` (Bode frequency shifter latency) from `e->initialDelay`.
- **Concrete Input Triggering Fail:**
  - Load a saved project chunk that has `"mpcOutputLimiter": 0` into the MPC shell in a DAW or host simulator. The plugin processes audio with zero delay (`mix = 0.0f`), but the host reads `initialDelay = 132` (or 144), causing the host to needlessly advance the track by 3 ms, throwing it out of sync with other tracks.
- **Suggested Fix:**
  - In `shell-mpc/synthyboy_mpc.cpp:401-405` (`applyOutputLimiter()`), update `effect->initialDelay = inst->outputStage.latencySamples()` whenever the limiter setting is modified.

---

### (i) Idle-Bit-Exactness Claim
- **Status:** **PASS**
- **Evidence:**
  - Spec claim (`docs/MPC.md:757`):
    "While nothing needs a reduction the detector is not run (the "idle" path, the NEON form on ARM) and the output is the gained input delayed by exactly the look-ahead, to the bit. With LIMIT off the output is the gained input without the delay (latency 0), to the bit; the detector keeps running so that switching on finds the reduction already computed."
  - Scalar path (`core/include/synthyboy/output_stage.h:357-366`):
    - When `idle == true` and peak $\le kCeiling$, $g = 1.0f$.
    - `if (g != 1.0f)` branch is bypassed. No float multiply by 1.0f.
    - `clip(la, clips)` returns `la` unaltered because $|la| \le kCeiling$.
    - $oa = da = dl[rd] = a(t - D)$, exactly matching the gained input stored $D$ samples earlier.
    - When `mix == 0.0f` (limiter off), $oa = a$ (exact undelayed gained input).
  - NEON path (`core/include/synthyboy/output_stage.h:435-444`):
    - Writes `vst1q_f32(l + i, da)` directly when `mix == 1.0f`.
    - Writes `vst1q_f32(l + i, a)` directly when `mix == 0.0f`.
  - Re-entering idle after limiting (`core/include/synthyboy/output_stage.h:276-285`):
    - Requires $hr == 0$, $red == 0$, $base == 0$, $calm \ge W$, and $sum2 == full2$, guaranteeing both moving averages have completely flushed out to unity gain before `idle = true` is restored.
  - Test verification: `shell-mpc/tests/output_stage_host.cpp:130-155` tests 0 dB and 9 dB gain below the ceiling, asserting $l[i] == in[i - D]$ to the bit, and confirms bit-exact identity recovers fully after a heavily limited passage.

---

### (j) Anything Misleading in Docs vs Code
- **Status:** **FAIL**
- **Evidence:**
  - **Stale comment in `shell-mpc/synthyboy_mpc.cpp:2480`**:
    Comment states: `// the output limiter setting rides in the chunk; a chunk without it (an older one) leaves the setting as it is (the default is off)`
    This contradicts `shell-mpc/output_stage.h:28` (`kOutputLimiterDefaultOn = true`), `synthyboy_mpc.cpp:400` (`outputLimiter { synthyboy_mpc::kOutputLimiterDefaultOn }`), and `docs/MPC.md:760` ("Default ON"). The comment is an obsolete relic from an earlier prototype.
  - **Look-ahead window size description in `docs/MPC.md:755`**:
    `docs/MPC.md:755` describes the window as "the minimum of that over the look-ahead window ... then two moving averages that together span the look-ahead".
    In `core/include/synthyboy/output_stage.h:80-88`, the window length is $W = D + 1$ samples, and $B_1 + B_2 - 1 = W = D + 1$. The filter support is 1 sample longer than the delay $D$.
  - **Undocumented latency mismatch on chunk restore**:
    `docs/MPC.md:759` discusses live LIMIT toggles not sending `audioMasterIOChanged`, but omits that restoring a chunk with `"mpcOutputLimiter": 0` leaves `AEffect::initialDelay` stale at 132/144 samples.
- **Concrete Input Triggering Fail:**
  - An engineer reading `synthyboy_mpc.cpp:2480` to determine chunk upgrade behavior will mistakenly believe missing chunk keys default to limiter OFF, when in fact they default to ON.
- **Suggested Fix:**
  - Update the comment in `shell-mpc/synthyboy_mpc.cpp:2480` to state: `// ... leaves the setting as it is (the default is on)`.

---

## Verdict (Sorted by Severity)

| Severity | Item | Category | Description | Suggested Fix |
|---|---|---|---|---|
| **MEDIUM-HIGH** | (g) | JUCE Shell Reset | `SynthyBoyProcessor` does not override `AudioProcessor::reset()` or `releaseResources()`. Stale audio remains in the 3 ms delay line when the host stops transport or locates. | Implement `void SynthyBoyProcessor::reset() override { if (engine) engine->reset(); outputStage.reset(); }` in `shell-juce/PluginProcessor.cpp`. |
| **MEDIUM** | (h) | MPC Shell Latency Reporting | `e->initialDelay` is not updated when `effSetChunk` restores `"mpcOutputLimiter": 0` or when LIMIT is toggled via the UI, causing host delay compensation to apply 3 ms delay to a zero-latency track. | In `shell-mpc/synthyboy_mpc.cpp:applyOutputLimiter()`, assign `effect->initialDelay = inst->outputStage.latencySamples()`. |
| **LOW** | (j) | Code Comment Inconsistency | Line 2480 of `shell-mpc/synthyboy_mpc.cpp` comments that the chunk default is OFF (`the default is off`), contradicting `kOutputLimiterDefaultOn = true` and `docs/MPC.md`. | Change the comment text at `synthyboy_mpc.cpp:2480` to `(the default is on)`. |
| **LOW** | (j) | Documentation Window Span | `docs/MPC.md:755` states moving averages span the look-ahead ($D$), whereas code precisely uses $W = D + 1$ samples ($B_1 + B_2 - 1 = D + 1$). | Clarify in `docs/MPC.md:755` that the cascaded moving average support is $D + 1$ samples ($W$). |
| **PASS** | (a) | RT-Safety | Core DSP contains no allocation, no locks, no libm per sample, no division instructions, and no denormal stalls. | N/A |
| **PASS** | (b) | FTZ / NEON | ARMv7 NEON hardware FTZ and arm64 `FPCR.FZ` match scalar loop bit-identically across all edge-case inputs. | N/A |
| **PASS** | (c) | Block Sizes | Continuous sample-accurate streaming across block sizes $1\dots 4096$ with zero ring-buffer wrap corruption. | N/A |
| **PASS** | (d) | Sample Rates | Correct look-ahead calculation and buffer scaling for 44.1, 48, and 96 kHz ($132, 144, 288$ samples). | N/A |
| **PASS** | (e) | Ceiling Guarantee | S-curve attack ramp mathematically guarantees peaks $\le 0.94406088f$ before the hard clip; safety clip never acts. | N/A |
| **PASS** | (f) | Gain Smoothing | 20 ms exponential one-pole smoothing with clean snap prevents clicks and denormals. | N/A |
| **PASS** | (i) | Idle Bit-Exactness | Bypass of multiplication when $g = 1.0f$ guarantees bit-exact passthrough delayed by $D$ samples when idle. | N/A |
