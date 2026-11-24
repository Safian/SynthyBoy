// SynthyBoy - MPC shell: RAII flush-to-zero (denormal protection) guard for the audio render path.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
 #include <pmmintrin.h>
 #include <xmmintrin.h>
#endif

namespace synthyboy_mpc
{

#if defined(__arm__)
inline uint32_t readFpControl() noexcept
{
    uint32_t fpscr = 0;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr) :: "memory");
    return fpscr;
}

inline void writeFpControl(uint32_t fpscr) noexcept
{
    __asm__ __volatile__("vmsr fpscr, %0" :: "r"(fpscr) : "memory");
}

inline constexpr uint32_t kFlushToZeroBit = 0x01000000u; // FPSCR.FZ (bit 24)
#elif defined(__aarch64__)
inline uint64_t readFpControl() noexcept
{
    uint64_t fpcr = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr) :: "memory");
    return fpcr;
}

inline void writeFpControl(uint64_t fpcr) noexcept
{
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr) : "memory");
}

inline constexpr uint64_t kFlushToZeroBit = 0x01000000ull; // FPCR.FZ (bit 24)
#elif defined(__x86_64__) || defined(__i386__)
inline uint32_t readFpControl() noexcept
{
    return _mm_getcsr();
}

inline void writeFpControl(uint32_t mxcsr) noexcept
{
    _mm_setcsr(mxcsr);
    __asm__ __volatile__("" ::: "memory");
}

inline constexpr uint32_t kFlushToZeroBit = 0x8040u; // MXCSR FTZ (bit 15) + DAZ (bit 6)
#endif

/** Enables hardware flush-to-zero for the lifetime of the scope and restores the caller's FP control state on exit.
    The memory clobbers prevent the compiler from hoisting or sinking inlined DSP operations across the mode switch. */
class FlushDenormalsScope
{
public:
    FlushDenormalsScope() noexcept
    {
#if defined(__arm__) || defined(__aarch64__) || defined(__x86_64__) || defined(__i386__)
        saved_ = readFpControl();
 #if defined(__x86_64__) || defined(__i386__)
        modified_ = (saved_ & kFlushToZeroBit) != kFlushToZeroBit;
 #else
        modified_ = (saved_ & kFlushToZeroBit) == 0;
 #endif
        if (modified_)
            writeFpControl(saved_ | kFlushToZeroBit);
        else
            __asm__ __volatile__("" ::: "memory");
#endif
    }

    ~FlushDenormalsScope() noexcept
    {
#if defined(__arm__) || defined(__aarch64__) || defined(__x86_64__) || defined(__i386__)
        writeFpControl(saved_);
#endif
    }

    FlushDenormalsScope(const FlushDenormalsScope&) = delete;
    FlushDenormalsScope& operator=(const FlushDenormalsScope&) = delete;

private:
#if defined(__aarch64__)
    uint64_t saved_ = 0;
    bool modified_ = false;
#elif defined(__arm__) || defined(__x86_64__) || defined(__i386__)
    uint32_t saved_ = 0;
    bool modified_ = false;
#endif
};

} // namespace synthyboy_mpc
