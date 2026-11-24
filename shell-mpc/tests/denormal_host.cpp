// SynthyBoy - MPC VST2 shell denormal-protection host verification.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Loads the built synthyboy_mpc shared library via VSTPluginMain and verifies:
//   1. Caller FP control register state (FPSCR.FZ on ARM32, FPCR.FZ on AArch64, MXCSR on x86)
//      and rounding mode are preserved across effOpen/effMainsChanged/processReplacing/effClose
//      whether the host enters with FZ=0 or FZ=1.
//   2. audioMasterGetTime runs in the host's FP mode (not mutated before host callback) and
//      a host callback that clears FZ does not break processReplacing or leak bad state.
//   3. Audio rendered across active notes and release tails contains zero non-finite or
//      FP_SUBNORMAL samples.
//
// Usage: denormal_host <path-to-synthyboy_mpc.so|.dylib>

#include "denormal_scope.h"
#include "mpc_vst2_abi.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <vector>

namespace
{

int gFails = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++gFails;
}

#if defined(__arm__)
using FpCtrl = uint32_t;
constexpr FpCtrl kRModeMask = 3u << 22;
constexpr FpCtrl kRModeTowardZero = 3u << 22;
#elif defined(__aarch64__)
using FpCtrl = uint64_t;
constexpr FpCtrl kRModeMask = 3ull << 22;
constexpr FpCtrl kRModeTowardZero = 3ull << 22;
#elif defined(__x86_64__) || defined(__i386__)
using FpCtrl = uint32_t;
constexpr FpCtrl kRModeMask = 3u << 13;
constexpr FpCtrl kRModeTowardZero = 3u << 13;
#endif

struct ScopedFpRestore
{
    FpCtrl saved;
    ScopedFpRestore() noexcept : saved(synthyboy_mpc::readFpControl()) {}
    ~ScopedFpRestore() noexcept { synthyboy_mpc::writeFpControl(saved); }
};

bool gExpectAudioMasterFz = false;
bool gHostClearFzInGetTime = false;
int gGetTimeCalls = 0;
int gGetTimeFzMismatches = 0;
VstTimeInfo gTimeInfo {};

VstIntPtr hostCallback(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion)
        return 2400;
    if (opcode == audioMasterGetTime)
    {
        ++gGetTimeCalls;
        const bool fzNow = (synthyboy_mpc::readFpControl() & synthyboy_mpc::kFlushToZeroBit) == synthyboy_mpc::kFlushToZeroBit;
        if (fzNow != gExpectAudioMasterFz)
            ++gGetTimeFzMismatches;
        if (gHostClearFzInGetTime)
            synthyboy_mpc::writeFpControl(synthyboy_mpc::readFpControl() & ~synthyboy_mpc::kFlushToZeroBit);
        gTimeInfo.tempo = 120.0;
        gTimeInfo.flags = kVstTempoValid;
        return reinterpret_cast<VstIntPtr>(&gTimeInfo);
    }
    return 0;
}

void sendNote(AEffect* e, int status, int key, int vel)
{
    VstMidiEvent m {};
    m.type = kVstMidiType;
    m.byteSize = sizeof(VstMidiEvent);
    m.midiData[0] = static_cast<char>(status);
    m.midiData[1] = static_cast<char>(key);
    m.midiData[2] = static_cast<char>(vel);
    VstEvents ev {};
    ev.numEvents = 1;
    ev.events[0] = reinterpret_cast<VstEvent*>(&m);
    e->dispatcher(e, effProcessEvents, 0, 0, &ev, 0.0f);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: denormal_host <plugin library>\n");
        return 2;
    }

    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }

    using MainProc = AEffect* (*)(audioMasterCallback);
    auto entry = reinterpret_cast<MainProc>(dlsym(lib, "VSTPluginMain"));
    if (entry == nullptr)
    {
        std::fprintf(stderr, "no VSTPluginMain in %s\n", argv[1]);
        dlclose(lib);
        return 2;
    }

    const ScopedFpRestore restore;
    const FpCtrl base = restore.saved & ~synthyboy_mpc::kFlushToZeroBit & ~kRModeMask;

    synthyboy_mpc::writeFpControl(base);
    AEffect* e = entry(hostCallback);
    if (e == nullptr || e->magic != kEffectMagic)
    {
        std::fprintf(stderr, "bad AEffect\n");
        dlclose(lib);
        return 2;
    }
    e->dispatcher(e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    check((synthyboy_mpc::readFpControl() & synthyboy_mpc::kFlushToZeroBit) == 0,
          "lifecycle calls (effOpen..effMainsChanged) preserve caller FZ=0");

    std::vector<float> left(128, 0.0f), right(128, 0.0f);
    float* outs[2] = { left.data(), right.data() };

    // 1. Caller enters processReplacing with FZ=0 and RN rounding
    gExpectAudioMasterFz = false;
    gHostClearFzInGetTime = false;
    gGetTimeCalls = 0;
    gGetTimeFzMismatches = 0;
    synthyboy_mpc::writeFpControl(base);
    sendNote(e, 0x90, 60, 100);
    for (int b = 0; b < 8; ++b)
        e->processReplacing(e, nullptr, outs, 128);
    check(gGetTimeCalls == 8 && gGetTimeFzMismatches == 0,
          "audioMasterGetTime sees caller FZ=0 (not mutated prematurely)");
    check((synthyboy_mpc::readFpControl() & (synthyboy_mpc::kFlushToZeroBit | kRModeMask)) == (base & (synthyboy_mpc::kFlushToZeroBit | kRModeMask)),
          "processReplacing restores caller FZ=0 and RN rounding");

    // 2. Caller enters processReplacing with FZ=0 and non-default RZ rounding
    const FpCtrl rzOff = base | kRModeTowardZero;
    synthyboy_mpc::writeFpControl(rzOff);
    e->processReplacing(e, nullptr, outs, 128);
    check((synthyboy_mpc::readFpControl() & (synthyboy_mpc::kFlushToZeroBit | kRModeMask)) == (rzOff & (synthyboy_mpc::kFlushToZeroBit | kRModeMask)),
          "processReplacing restores caller FZ=0 and RZ rounding");

    // 3. Caller enters processReplacing with FZ=1
    const FpCtrl fzOn = base | synthyboy_mpc::kFlushToZeroBit;
    gExpectAudioMasterFz = true;
    gGetTimeCalls = 0;
    gGetTimeFzMismatches = 0;
    synthyboy_mpc::writeFpControl(fzOn);
    e->processReplacing(e, nullptr, outs, 128);
    check(gGetTimeCalls == 1 && gGetTimeFzMismatches == 0,
          "audioMasterGetTime sees caller FZ=1");
    check((synthyboy_mpc::readFpControl() & synthyboy_mpc::kFlushToZeroBit) == synthyboy_mpc::kFlushToZeroBit,
          "processReplacing preserves caller FZ=1");

    // 4. Hostile audioMasterGetTime clears FZ=0 on entry; processReplacing continues cleanly
    gHostClearFzInGetTime = true;
    synthyboy_mpc::writeFpControl(fzOn);
    e->processReplacing(e, nullptr, outs, 128);
    gHostClearFzInGetTime = false;
    check((synthyboy_mpc::readFpControl() & synthyboy_mpc::kFlushToZeroBit) == 0,
          "when host audioMasterGetTime clears FZ=0, FlushDenormalsScope restores post-callback FZ=0");

    // 5. Render release tail under FZ=0 caller mode and verify 0 non-finite or FP_SUBNORMAL samples
    gExpectAudioMasterFz = false;
    synthyboy_mpc::writeFpControl(base);
    sendNote(e, 0x80, 60, 0);
    int nonFinite = 0;
    int subnormal = 0;
    const int tailBlocks = (44100 * 3) / 128;
    for (int b = 0; b < tailBlocks; ++b)
    {
        e->processReplacing(e, nullptr, outs, 128);
        for (int i = 0; i < 128; ++i)
        {
            for (float s : { left[static_cast<size_t>(i)], right[static_cast<size_t>(i)] })
            {
                if (!std::isfinite(s))
                    ++nonFinite;
                else if (std::fpclassify(s) == FP_SUBNORMAL)
                    ++subnormal;
            }
        }
    }
    check(nonFinite == 0 && subnormal == 0,
          "active note + 3s release tail produces 0 non-finite and 0 FP_SUBNORMAL output samples");
    check((synthyboy_mpc::readFpControl() & synthyboy_mpc::kFlushToZeroBit) == 0,
          "caller FZ=0 still restored after full tail render");

    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    dlclose(lib);

    std::printf("denormal_host: %s (%d failures)\n", gFails == 0 ? "OK" : "FAILED", gFails);
    return gFails == 0 ? 0 : 1;
}
