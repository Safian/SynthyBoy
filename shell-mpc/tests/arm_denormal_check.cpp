// SynthyBoy - standalone ARM32 / AArch64 denormal-protection verification.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Zero external library or SDK dependencies: compiles natively on Apple Silicon / Linux AArch64,
// or statically cross-compiled for MPC Key 61 (armv7) and MPC XL (aarch64).
// Tests:
//   1. Hardware FP control register (FPSCR.FZ on ARM32, FPCR.FZ on AArch64, MXCSR on x86)
//      is enabled on FlushDenormalsScope entry and restored on exit (for both FZ=0 and FZ=1 callers).
//   2. Non-FZ control bits (e.g., rounding mode bits) are preserved across FlushDenormalsScope.
//   3. Hardware scalar float, scalar double, and NEON float32x4_t subnormal inputs and underflowing
//      products flush to zero inside FlushDenormalsScope.
//   4. Compiler barrier (-O2): inline arithmetic inside vs immediately after FlushDenormalsScope
//      obeys the active FP state on each side of the boundary.
//   5. Engine DSP helpers (flushDenormal) and OutputStage (scalar and NEON paths) produce no
//      subnormals when processing subnormal inputs inside FlushDenormalsScope.

#include "denormal_scope.h"
#include "dsp/util.h"
#include "synthyboy/output_stage.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
 #include <arm_neon.h>
 #define SB_DENORMAL_TEST_NEON 1
#else
 #define SB_DENORMAL_TEST_NEON 0
#endif

namespace
{

int gFails = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++gFails;
}

float floatFromBits(uint32_t u) noexcept
{
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}

double doubleFromBits(uint64_t u) noexcept
{
    double d = 0.0;
    std::memcpy(&d, &u, sizeof d);
    return d;
}

#if defined(__arm__)
using FpCtrl = uint32_t;
constexpr FpCtrl kRModeMask = 3u << 22;
constexpr FpCtrl kRModeTowardZero = 3u << 22;
const char* kArchName = "arm32";
#elif defined(__aarch64__)
using FpCtrl = uint64_t;
constexpr FpCtrl kRModeMask = 3ull << 22;
constexpr FpCtrl kRModeTowardZero = 3ull << 22;
const char* kArchName = "aarch64";
#elif defined(__x86_64__) || defined(__i386__)
using FpCtrl = uint32_t;
constexpr FpCtrl kRModeMask = 3u << 13;
constexpr FpCtrl kRModeTowardZero = 3u << 13;
const char* kArchName = "x86";
#endif

#if defined(__arm__) || defined(__aarch64__) || defined(__x86_64__) || defined(__i386__)
struct ScopedFpRestore
{
    FpCtrl saved;
    ScopedFpRestore() noexcept : saved(synthyboy_mpc::readFpControl()) {}
    ~ScopedFpRestore() noexcept { synthyboy_mpc::writeFpControl(saved); }
    ScopedFpRestore(const ScopedFpRestore&) = delete;
    ScopedFpRestore& operator=(const ScopedFpRestore&) = delete;
};

// Volatile input reads and output writes force runtime hardware FP execution and anchor
// the arithmetic to the memory-clobber boundary of FlushDenormalsScope at -O2.
inline float hwMulFloat(float a, float b) noexcept
{
    volatile float va = a;
    volatile float vb = b;
    volatile float out = va * vb;
    return out;
}

inline double hwMulDouble(double a, double b) noexcept
{
    volatile double va = a;
    volatile double vb = b;
    volatile double out = va * vb;
    return out;
}

#if SB_DENORMAL_TEST_NEON
inline float hwNeonMulLane0(float a, float b) noexcept
{
    volatile float va = a;
    volatile float vb = b;
    const float32x4_t x = vdupq_n_f32(va);
    const float32x4_t y = vdupq_n_f32(vb);
    const float32x4_t z = vmulq_f32(x, y);
    volatile float out[4] = {};
    vst1q_f32(const_cast<float*>(out), z);
    return out[0];
}
#endif

// Mirrors pluginProcessReplacing -> renderAudioBlock noinline boundary even with plain register floats.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
float noinlineMul(float a, float b) noexcept
{
    volatile float va = a;
    volatile float vb = b;
    return va * vb;
}

void testRegisterScope()
{
    const ScopedFpRestore restore;
    const FpCtrl base = restore.saved & ~synthyboy_mpc::kFlushToZeroBit & ~kRModeMask;

    // 1. Entry with FZ=0 -> FZ=1 inside, restored to FZ=0 on exit (RN rounding preserved)
    synthyboy_mpc::writeFpControl(base);
    check((synthyboy_mpc::readFpControl() & synthyboy_mpc::kFlushToZeroBit) == 0,
          "baseline FP control starts with FZ=0");
    {
        synthyboy_mpc::FlushDenormalsScope scope;
        const FpCtrl inside = synthyboy_mpc::readFpControl();
        check((inside & synthyboy_mpc::kFlushToZeroBit) == synthyboy_mpc::kFlushToZeroBit,
              "FlushDenormalsScope enables FZ=1 from FZ=0");
        check((inside & kRModeMask) == (base & kRModeMask),
              "FlushDenormalsScope preserves RN rounding mode");
    }
    check(synthyboy_mpc::readFpControl() == base,
          "FlushDenormalsScope restores FZ=0 and control bits on exit");

    // 2. Entry with FZ=0 and non-default rounding mode (RZ) -> RZ preserved inside and after
    const FpCtrl rzOff = base | kRModeTowardZero;
    synthyboy_mpc::writeFpControl(rzOff);
    {
        synthyboy_mpc::FlushDenormalsScope scope;
        const FpCtrl inside = synthyboy_mpc::readFpControl();
        check((inside & synthyboy_mpc::kFlushToZeroBit) == synthyboy_mpc::kFlushToZeroBit,
              "FlushDenormalsScope enables FZ=1 with RZ rounding set");
        check((inside & kRModeMask) == kRModeTowardZero,
              "FlushDenormalsScope preserves RZ rounding mode inside scope");
    }
    check(synthyboy_mpc::readFpControl() == rzOff,
          "FlushDenormalsScope restores RZ rounding mode and FZ=0 on exit");

    // 3. Entry with FZ=1 -> FZ=1 preserved inside and after
    const FpCtrl rzOn = rzOff | synthyboy_mpc::kFlushToZeroBit;
    synthyboy_mpc::writeFpControl(rzOn);
    {
        synthyboy_mpc::FlushDenormalsScope scope;
        const FpCtrl inside = synthyboy_mpc::readFpControl();
        check((inside & synthyboy_mpc::kFlushToZeroBit) == synthyboy_mpc::kFlushToZeroBit,
              "FlushDenormalsScope keeps FZ=1 when caller already had FZ=1");
        check((inside & kRModeMask) == kRModeTowardZero,
              "FlushDenormalsScope keeps RZ rounding mode when FZ=1 on entry");
    }
    check(synthyboy_mpc::readFpControl() == rzOn,
          "FlushDenormalsScope preserves FZ=1 and RZ rounding on exit");
}

void testHardwareArithmeticAndBarrier()
{
    const ScopedFpRestore restore;
    const FpCtrl offState = restore.saved & ~synthyboy_mpc::kFlushToZeroBit;
    synthyboy_mpc::writeFpControl(offState);

    const float subPos = floatFromBits(0x00000001u); // smallest positive float subnormal (~1.4e-45)
    const float subMax = floatFromBits(0x007fffffu); // largest float subnormal
    const float subNeg = floatFromBits(0x80000001u); // smallest negative float subnormal
    const double subDbl = doubleFromBits(0x0000000000000001ull);

    // Verify IEEE subnormal preservation when FZ=0 (proves hardware test is sensitive, not folded).
    check(std::fpclassify(hwMulFloat(subPos, 1.0f)) == FP_SUBNORMAL,
          "with FZ=0, scalar float subnormal input remains FP_SUBNORMAL");
    check(std::fpclassify(hwMulFloat(1.0e-20f, 1.0e-20f)) == FP_SUBNORMAL,
          "with FZ=0, scalar float underflow product remains FP_SUBNORMAL");
    check(std::fpclassify(hwMulDouble(subDbl, 1.0)) == FP_SUBNORMAL,
          "with FZ=0, scalar double subnormal input remains FP_SUBNORMAL");
    check(std::fpclassify(hwMulDouble(1.0e-200, 1.0e-120)) == FP_SUBNORMAL,
          "with FZ=0, scalar double underflow product remains FP_SUBNORMAL");

#if SB_DENORMAL_TEST_NEON
 #if defined(__aarch64__)
    check(std::fpclassify(hwNeonMulLane0(subPos, 1.0f)) == FP_SUBNORMAL,
          "aarch64 with FPCR.FZ=0: NEON float32x4_t preserves FP_SUBNORMAL");
 #elif defined(__arm__)
    check(std::fpclassify(hwNeonMulLane0(subPos, 1.0f)) == FP_ZERO,
          "arm32 with FPSCR.FZ=0: NEON float32x4_t always flushes to FP_ZERO");
 #endif
#endif

    float insideSubPos = 1.0f;
    float insideSubMax = 1.0f;
    float insideSubNeg = 1.0f;
    float insideUnderflow = 1.0f;
    double insideDblSub = 1.0;
    double insideDblUnder = 1.0;
#if SB_DENORMAL_TEST_NEON
    float insideNeonSub = 1.0f;
    float insideNeonUnder = 1.0f;
#endif

    {
        synthyboy_mpc::FlushDenormalsScope scope;
        insideSubPos = hwMulFloat(subPos, 1.0f);
        insideSubMax = hwMulFloat(subMax, 1.0f);
        insideSubNeg = hwMulFloat(subNeg, 1.0f);
        insideUnderflow = hwMulFloat(1.0e-20f, 1.0e-20f);
        insideDblSub = hwMulDouble(subDbl, 1.0);
        insideDblUnder = hwMulDouble(1.0e-200, 1.0e-120);
#if SB_DENORMAL_TEST_NEON
        insideNeonSub = hwNeonMulLane0(subPos, 1.0f);
        insideNeonUnder = hwNeonMulLane0(1.0e-20f, 1.0e-20f);
#endif
    }

    // Immediate post-scope operation must see restored FZ=0 (tests compiler barrier against hoisting/sinking).
    const float afterScope = hwMulFloat(subPos, 1.0f);
    float viaNoinlineScope = 1.0f;
    {
        synthyboy_mpc::FlushDenormalsScope scope;
        viaNoinlineScope = noinlineMul(subMax, 1.0f);
    }
    const float afterNoinlineScope = noinlineMul(subMax, 1.0f);

    check(std::fpclassify(insideSubPos) == FP_ZERO,
          "inside FlushDenormalsScope: smallest positive float subnormal flushes to FP_ZERO");
    check(std::fpclassify(insideSubMax) == FP_ZERO,
          "inside FlushDenormalsScope: largest float subnormal flushes to FP_ZERO");
    check(std::fpclassify(insideSubNeg) == FP_ZERO,
          "inside FlushDenormalsScope: negative float subnormal flushes to FP_ZERO");
    check(std::fpclassify(insideUnderflow) == FP_ZERO,
          "inside FlushDenormalsScope: float underflow product flushes to FP_ZERO");
    check(std::fpclassify(insideDblSub) == FP_ZERO,
          "inside FlushDenormalsScope: double subnormal input flushes to FP_ZERO");
    check(std::fpclassify(insideDblUnder) == FP_ZERO,
          "inside FlushDenormalsScope: double underflow product flushes to FP_ZERO");
#if SB_DENORMAL_TEST_NEON
    check(std::fpclassify(insideNeonSub) == FP_ZERO,
          "inside FlushDenormalsScope: NEON float32x4_t subnormal input flushes to FP_ZERO");
    check(std::fpclassify(insideNeonUnder) == FP_ZERO,
          "inside FlushDenormalsScope: NEON float32x4_t underflow product flushes to FP_ZERO");
#endif
    check(std::fpclassify(afterScope) == FP_SUBNORMAL,
          "immediately after FlushDenormalsScope: restored FZ=0 preserves FP_SUBNORMAL (compiler barrier holds)");
    check(std::fpclassify(viaNoinlineScope) == FP_ZERO && std::fpclassify(afterNoinlineScope) == FP_SUBNORMAL,
          "noinline scoped render helper flushes inside and restores FZ=0 on return");
}
#endif

void testDspHelpersAndOutputStage()
{
    check(synthyboy::own::flushDenormal(floatFromBits(0x00000001u)) == 0.0f,
          "own::flushDenormal flushes positive subnormal to 0");
    check(synthyboy::own::flushDenormal(floatFromBits(0x80000001u)) == 0.0f,
          "own::flushDenormal flushes negative subnormal to 0");
    check(synthyboy::own::flushDenormal(1.0e-19f) == 0.0f,
          "own::flushDenormal flushes 1e-19f to 0");
    check(synthyboy::own::flushDenormal(1.0e-17f) == 1.0e-17f,
          "own::flushDenormal preserves 1e-17f");

    const ScopedFpRestore restore;
    synthyboy_mpc::writeFpControl(restore.saved & ~synthyboy_mpc::kFlushToZeroBit);

    for (bool scalarOnly : { true, false })
    {
#if !SYNTHYBOY_OUT_NEON
        if (!scalarOnly)
            continue;
#endif
        for (bool limiter : { false, true })
        {
            for (float gainDb : { 0.0f, 6.0f })
            {
                synthyboy::OutputStage stage;
                stage.scalarOnly = scalarOnly;
                stage.setLimiter(limiter);
                stage.prepare(44100.0);
                stage.setGainDb(gainDb);
                stage.reset();

                std::vector<float> l(128), r(128);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    l[i] = (i & 1u) ? floatFromBits(0x00000001u) : floatFromBits(0x807fffffu);
                    r[i] = (i & 1u) ? -1.0e-40f : 1.0e-40f;
                }

                {
                    synthyboy_mpc::FlushDenormalsScope scope;
                    stage.process(l.data(), r.data(), static_cast<int>(l.size()));
                }

                int subnormalCount = 0;
                for (size_t i = 0; i < l.size(); ++i)
                {
                    if (std::fpclassify(l[i]) == FP_SUBNORMAL || std::fpclassify(r[i]) == FP_SUBNORMAL)
                        ++subnormalCount;
                }
                char msg[160];
                std::snprintf(msg, sizeof msg,
                              "OutputStage (%s, limiter=%s, gain=%.0fdB) under FlushDenormalsScope emits 0 subnormals",
                              scalarOnly ? "scalar" : "NEON", limiter ? "on" : "off", static_cast<double>(gainDb));
                check(subnormalCount == 0, msg);
            }
        }
    }
}

} // namespace

int main()
{
    std::printf("arm_denormal_check: arch=%s neon=%d out_neon=%d\n",
                kArchName, SB_DENORMAL_TEST_NEON, SYNTHYBOY_OUT_NEON);
    testRegisterScope();
    testHardwareArithmeticAndBarrier();
    testDspHelpersAndOutputStage();
    std::printf("arm_denormal_check: %s (%d failures)\n", gFails == 0 ? "OK" : "FAILED", gFails);
    return gFails == 0 ? 0 : 1;
}
