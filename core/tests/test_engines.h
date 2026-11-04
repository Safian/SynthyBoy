// SynthyBoy - core tests: the engines under test (own, and Vital-derived if built in) and their expectations.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>

#include "synthyboy/engine.h"

namespace sbtest
{
/** One engine under test. Tests that are meaningful for every engine loop over engineCases(); the numbers below are the
    per-engine facts the shared checks need (measured, see docs/ARCHITECTURE.md 3.7 and core/engine-vital/README.md). */
struct EngineCase
{
    synthyboy::EngineKind kind;
    const char* name;
    double tailSeconds;       // after a note off the output is exactly zero from this time on, and no voice is left
    double blockTolerance;    // how much the output may depend on how the host splits the stream into blocks (relative RMS
                              // difference, notes without pitch bends); 0 = bit identical
    double bentBlockTolerance;// ... with pitch bends during sounding notes (sine; 0 = bit identical)
    bool resetBitExact;       // reset() returns to a state that renders bit-identical to a freshly prepared engine
    double resetTolerance;    // otherwise: relative RMS difference allowed
    double pitchTolerance;    // pitch error: Hz for the engines with pitchInCents == false, cents otherwise
    bool pitchInCents;
    double velocityExponent;  // peak level ~ velocity ^ exponent
    int gainRampFrames;       // a gain jump is ramped over about this many frames
    bool stereoIdentical;     // left == right bit for bit
    bool voicePoolExact;      // the voice pool is filled completely and never exceeded (no fading voices counted beside it)
};

inline std::vector<EngineCase> engineCases()
{
    std::vector<EngineCase> cases;
    //                         kind                         name    tail  blockTol bentTol resetExact resetTol pitch  cents  velExp ramp  stereo pool
    cases.push_back ({ synthyboy::EngineKind::own,   "own",   0.26, 0.0,   0.0,    true,     0.0,    0.5,   false, 1.0,   960,  true,  true  });
    // vital: resetTol 3e-5 (measured 1.3e-5 with the default patch: its 200 ms release leaves a little more filter residue than the
    // 90 ms of the previous init patch)
    if (synthyboy::isEngineAvailable (synthyboy::EngineKind::vital))
        cases.push_back ({ synthyboy::EngineKind::vital, "vital", 0.60, 1.0e-5, 0.15,   false,    3.0e-5, 1.0,   true,  2.0,   128,  true,  false });
    return cases;
}

inline std::unique_ptr<synthyboy::IEngine> makeEngine (const EngineCase& c, const synthyboy::Quality& q = synthyboy::Quality {},
                                                       double sampleRate = 48000.0, int maxBlock = 512)
{
    auto e = synthyboy::createEngine (c.kind);
    if (e != nullptr)
    {
        // the polyphony parameter is capped by the quality's voice limit (it never raises it): let the quality decide
        e->setParam (synthyboy::kParamMasterPolyphony, 32.0f);
        e->prepare (sampleRate, maxBlock, q);
    }
    return e;
}
} // namespace sbtest
