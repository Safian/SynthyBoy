// SynthyBoy - MPC shell: output stage. The stage itself is the one of every shell, core/include/synthyboy/output_stage.h (make-up gain, then a stereo-linked look-ahead peak
// limiter with a program-dependent release at -0.5 dB full scale, a hard safety clip behind it). This header adds what is the MPC shell's own: the limiter as the plugin setting LIMIT
// (default ON, kOutputLimiterDefaultOn), the environment / build override SYNTHYBOY_MPC_OUTPUT_LIMITER for tests, and the chunk member "mpcOutputLimiter".
// Tested by shell-mpc/tests/output_stage_host.cpp and output_stage_lanes_host.cpp (tests/run_output_stage_test.sh).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

#include "synthyboy/output_stage.h"

// 1 = the build forces the limiter on (the old CMake option); 0 (default) forces nothing: the plugin setting decides.
#ifndef SYNTHYBOY_MPC_OUTPUT_LIMITER
#define SYNTHYBOY_MPC_OUTPUT_LIMITER 0
#endif

namespace synthyboy_mpc {

/** The stage of the MPC shell: the shared one (switchable while running, crossfaded). */
using SwitchableOutputStage = synthyboy::OutputStage;
using OutputStage = synthyboy::OutputStage;

// ---- the limiter as a plugin setting (docs/MPC.md "Output limiter") ----
/** The default of the setting "LIMIT" on the Master page (OUTPUT block): the one constant to flip. A project saved before the setting existed keeps this default. */
constexpr bool kOutputLimiterDefaultOn = true;

/** SYNTHYBOY_MPC_OUTPUT_LIMITER in the environment forces the limiter for tests: "1" on, "0" off, anything else (or unset) leaves it to the setting. A build with
    -DSYNTHYBOY_MPC_OUTPUT_LIMITER=1 forces it on as before (the value 0 of the default build forces nothing). Returns -1 (not forced), 0 (off) or 1 (on). */
inline int outputLimiterForced() noexcept
{
    constexpr bool kBuildForcesOn = (SYNTHYBOY_MPC_OUTPUT_LIMITER != 0);
    const char* e = std::getenv ("SYNTHYBOY_MPC_OUTPUT_LIMITER");
    if (e != nullptr && e[0] == '0' && e[1] == '\0') return 0;
    if (kBuildForcesOn || (e != nullptr && e[0] == '1' && e[1] == '\0')) return 1;
    return -1;
}

/** The chunk with the member "mpcOutputLimiter":0 / 1 before the last closing brace (the members of the pad pressure follow it: they are read from the very end). */
inline std::string chunkWithOutputLimiter (std::string chunk, bool on)
{
    const size_t close = chunk.rfind ('}');
    if (close == std::string::npos)
        return chunk;
    chunk.insert (close, on ? ",\"mpcOutputLimiter\":1" : ",\"mpcOutputLimiter\":0");
    return chunk;
}

/** Reads the member from the last 240 bytes of a chunk. True when it is there (then `on` is set); false leaves `on` as it is. Never reads outside `size`. */
inline bool outputLimiterFromChunk (const void* data, size_t size, bool& on) noexcept
{
    if (data == nullptr || size < 24)
        return false;
    const size_t tailLen = std::min<size_t> (size, 240);
    const std::string_view tail (static_cast<const char*> (data) + (size - tailLen), tailLen);
    static constexpr std::string_view kKey = "\"mpcOutputLimiter\":";
    const size_t a = tail.rfind (kKey);
    if (a == std::string_view::npos)
        return false;
    const size_t p = a + kKey.size();
    if (p + 1 >= tail.size() || (tail[p] != '0' && tail[p] != '1') || (tail[p + 1] != ',' && tail[p + 1] != '}'))
        return false;
    on = tail[p] == '1';
    return true;
}

} // namespace synthyboy_mpc
