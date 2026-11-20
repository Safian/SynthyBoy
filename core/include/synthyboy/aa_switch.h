// SynthyBoy - core: the anti-aliasing A/B switch (docs/plans/distortion-antialiasing.md section 4.0).
// Test and benchmark hook only: production never sets it. Every anti-aliasing change is guarded by one bit; with its bit set the code runs the legacy path bit for bit.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

namespace synthyboy::aa
{
enum Off : uint32_t
{
    kDistortion = 1,  // the ADAA of the distortion effect
    kWarpAdaa = 2,    // the ADAA of the sample-domain oscillator warps
    kBlep = 4,        // the PolyBLEP at the master wrap
    kBlamp = 8,       // the PolyBLAMP at the slope events
    kBandLimit = 16,  // the band limit factor of the sample-domain warps at 4x
    kLibWarp = 32,    // the ADAA of the library oscillator warps
    kAll = 63
};

inline std::atomic<uint32_t> gOff { 0 };

/** The bits that are switched OFF (0 = every anti-aliasing change active). */
inline uint32_t off() noexcept
{
    return gOff.load (std::memory_order_relaxed);
}

inline void setOff (uint32_t mask) noexcept
{
    gOff.store (mask, std::memory_order_relaxed);
}
} // namespace synthyboy::aa
