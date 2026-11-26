// SynthyBoy - core tests: the flush-to-zero float mode of the ARM test hosts (FZ, bit 24 of FPCR on AArch64 and of FPSCR on 32 bit ARM), as the MPC shell sets it around the render.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace sbtest
{
#if defined(__aarch64__)
inline uint64_t readFpState() { uint64_t v; asm volatile ("mrs %0, fpcr" : "=r" (v)); return v; }
inline void writeFpState (uint64_t v) { asm volatile ("msr fpcr, %0" :: "r" (v) : "memory"); }   // (the memory clobber keeps the calls on the right side of it)
#elif defined(__arm__)
inline uint64_t readFpState() { uint32_t v; asm volatile ("vmrs %0, fpscr" : "=r" (v)); return v; }
inline void writeFpState (uint64_t v) { const uint32_t w = static_cast<uint32_t> (v); asm volatile ("vmsr fpscr, %0" :: "r" (w) : "memory"); }
#else
inline uint64_t readFpState() { return 0; }
inline void writeFpState (uint64_t) {}
#endif

/** Sets (or clears) flush-to-zero for the scope; the previous state comes back at the end. */
struct FlushMode
{
    uint64_t saved;
    explicit FlushMode (bool fz) : saved (readFpState()) { writeFpState (fz ? (saved | 0x01000000ull) : (saved & ~0x01000000ull)); }
    ~FlushMode() { writeFpState (saved); }
    FlushMode (const FlushMode&) = delete;
    FlushMode& operator= (const FlushMode&) = delete;
};
} // namespace sbtest
