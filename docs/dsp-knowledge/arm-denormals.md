# ARM Denormal Protection (MPC Key 61 and MPC XL)

## Render Entry Point and FP State Scope

- The MPC VST2 shell exports `VSTPluginMain` (`shell-mpc/synthyboy_mpc.cpp`) and registers `pluginProcessReplacing` (`effect->processReplacing`); `process` and `processDoubleReplacing` are null.
- `pluginProcessReplacing` queries host transport (`audioMasterGetTime`), drains/sorts `EventInbox`, and then enters `synthyboy_mpc::FlushDenormalsScope` (`shell-mpc/denormal_scope.h`) around an out-of-line (`__attribute__((noinline))`) `renderAudioBlock` call that runs `OwnEngine::process`, `OutputStage::process`, `OutputPeak::note`, and `SpectrumRing::push`.
- Placing `FlushDenormalsScope` after `audioMasterGetTime` ensures host callbacks run in the host's FP mode and cannot clear `FZ` before `OwnEngine::process`. Calling `renderAudioBlock` out-of-line prevents compiler code motion of inlined `OutputStage` / `OutputPeak` register arithmetic across the `FPCR`/`FPSCR` writes (observed on Apple Clang `arm64` `-O2`: register-only `fmul` instructions with a live result read after the scope were sunk past `msr FPCR` when only a `"memory"` clobber separated them). `test: shell-mpc/tests/arm_denormal_check.cpp`

## ARM32 (`armv7`, Key 61) vs AArch64 (`arm64`, MPC XL)

- **ARM32 (`__arm__`, Cortex-A17):**
  - `FPSCR.FZ` (bit 24, `0x01000000u`) controls VFP scalar/vector floating-point flush-to-zero for single- and double-precision arithmetic; `VMSR FPSCR` acts as a VFP exception/serialization barrier for subsequent VFP instructions. `source: ARM DDI 0406`
  - Advanced SIMD (NEON) on ARMv7-A always operates in flush-to-zero mode regardless of `FPSCR.FZ`. Without `FPSCR.FZ = 1`, VFP scalar code preserves subnormals while NEON flushes them, which would both risk VFP subnormal slowdowns and break bit-identity between VFP and NEON lanes. `source: ARM DDI 0406`
- **AArch64 (`__aarch64__`, RK3588):**
  - `FPCR.FZ` (bit 24, `0x01000000ull`) controls flush-to-zero for both scalar FP and Advanced SIMD (NEON) instructions (`FPCR.AH == 0` flushes both subnormal inputs and tiny results for single and double precision). Unlike ARMv7 NEON, AArch64 NEON preserves subnormals when `FPCR.FZ == 0`. `source: ARM DDI 0487`; `test: arm_denormal_check` (`arm64`)
  - Direct writes via `MSR FPCR` are self-synchronizing for subsequent FP/SIMD instructions on the same PE. `source: ARM DDI 0487`
- **Non-audio / helper threads:** `OwnEngine` helper threads (`WavetableBank`, `FxPool`, `Convolve` IR prep) perform offline setup rather than block rendering and intentionally keep the default IEEE FP mode.

## Core Software Guards

- Recursive core DSP modules additionally apply `synthyboy::own::flushDenormal` (`|v| < 1.0e-18f ? 0.0f : v`, `core/engine-own/dsp/util.h`) and `neoncmp::flushTiny` (`core/engine-own/dsp/neon_cmp.h`), which behave identically in IEEE and `FZ=1` modes because `1.0e-18f` is a normal float (`test: core/tests/test_neon_cmp.cpp`, `shell-mpc/tests/arm_denormal_check.cpp`).

## Verification Status (Source/Compiled vs Device Measurements)

- **Executed on Apple `arm64` (`-O2`):** `shell-mpc/tests/run_arm_denormal_check.sh` compiles and passes `arm_denormal_check.cpp` (`37/37` checks: `FPCR.FZ` 0->1->0 and 1->1, rounding-mode preservation, scalar `float`/`double` and NEON `float32x4_t` subnormal flushing, `-O2` compiler-barrier & `noinline` boundary, `flushDenormal`, `OutputStage` scalar & NEON).
- The ARMv7 hard-float standalone check passed on QEMU Cortex-A15. The Apple arm64 VST host check passed all nine lifecycle, callback and output checks against the built plugin. Neither run measures timing or behaviour on a physical MPC.
- Physical Key 61 and XL acceptance remains open in TODO.md.
