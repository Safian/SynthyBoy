// MpcKit Demo - the VST2 entry point of the starter template: a sine + filter instrument whose parameters, pages and Q-Links all come from the kit. THIS FILE IS PLUMBING: you rarely change it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The parameter list the host sees:   [ 0 .. kNumParams - 1: the plugin's own parameters ]  [ kNumParams: "Overlay Instance" (display only) ]  [ kNumParams + 1 .. + 16: "Q 1" .. "Q 16" ]
// The display thread (a normal thread, not the audio thread) tells the host what changed: a value a PAGE wrote, a Q proxy whose target moved, the instance frame of "Overlay Instance".
// What the host calls is the VST2 dispatcher below; the sound is demo_voice.h; the pages are src/pages/.
// SIMPLIFICATIONS against shell-mpc/synthyboy_mpc.cpp (which handles them in detail: robustness review F5 / F12): the display thread is joined plainly at effClose; the restore window after a chunk
// restore (an echo of the saved values) is not modelled. The migration plan moves these into the kit.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "mpc_vst2_abi.h"

#include "demo_instance.h"
#include "demo_page_set.h"
#include "log_compat.h"
#include "overlay_hook.h"
#include "../../shell-mpc/host_strings.h"
#include "../../shell-mpc/kit/display_thread.h"

namespace {

using demo::DemoInstance;
using synthyboy_mpc::putBounded;
constexpr size_t kCap = synthyboy_mpc::kParamStrBytes;   // the bytes the plugin writes into the host's name / label / display buffers, NUL included

constexpr VstInt32 kOverlayIndex = demo::kNumParams;                          // "Overlay Instance"
constexpr VstInt32 kQBase = demo::kNumParams + 1;                             // "Q 1"
constexpr VstInt32 kTotalParams = demo::kNumParams + 1 + mpckit::IQTargets::kProxies;

struct Plugin
{
    DemoInstance inst;
    audioMasterCallback master = nullptr;
    AEffect* effect = nullptr;
    std::thread display;
    std::atomic<bool> stop { false };
    std::atomic<bool> hostKnows { false };   // the host has called us once: it holds the AEffect pointer, the display thread may call back
    std::vector<char> chunk;
    std::mutex chunkMutex;
};

Plugin* pluginOf (AEffect* e) noexcept { return e != nullptr ? static_cast<Plugin*> (e->object) : nullptr; }

bool isQ (VstInt32 i) noexcept { return i >= kQBase && i < kTotalParams; }

/** The display thread (the kit's, shell-mpc/kit/display_thread.h): every 50 ms it runs the features registered here, each inside a guard (an exception in one is caught and counted, five in a row switch it off:
    the thread never ends the host), and tells the host what changed. */
void displayLoop (Plugin* p)
{
    MPCKIT_SCOPE ("display", "display thread");
    DemoInstance& inst = p->inst;
    auto automate = [p] (VstInt32 index, float value) { p->master (p->effect, audioMasterAutomate, index, 0, nullptr, value); };
    float lastOverlay = -1.0f;
    mpckit::DisplayThread display (p, "mpckit-display", { std::chrono::milliseconds (50), std::chrono::milliseconds (50) });
    display.add ("overlay report", [&]
    {
        const bool due = inst.reportDue.exchange (false, std::memory_order_acq_rel);
        const float ov = inst.overlayValue();
        if (due || ov != lastOverlay)
        {
            lastOverlay = ov;
            automate (kOverlayIndex, ov);
        }
    });
    display.add ("page writes", [&]   // the parameters a page wrote
    {
        for (uint32_t bits = inst.takeDirty(); bits != 0; bits &= bits - 1)
        {
            const int i = __builtin_ctz (bits);
            automate (i, inst.normalised (i));
        }
    });
    display.add ("Q proxies", [&]
    {
        inst.q.poll ([&] (int q, float v) { automate (kQBase + q, v); },
                     [&] (int real) { automate (real, inst.normalised (real)); },
                     [&] { p->master (p->effect, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f); });   // the names of the Q proxies are other now
    });
    display.run ([p] { return p->stop.load (std::memory_order_acquire); },
                 [p] { return p->hostKnows.load (std::memory_order_acquire) && p->master != nullptr; });   // the host has not called us yet: no callback into it
}

std::string buildChunk (const DemoInstance& inst)
{
    std::string s = "MPCKITDEMO1;";
    char b[96];
    for (int i = 0; i < demo::kNumParams; ++i)
    {
        std::snprintf (b, sizeof b, "%s=%.9g;", demo::kParams[i].key, static_cast<double> (inst.param (i)));
        s += b;
    }
    return s;
}

void applyChunk (DemoInstance& inst, const char* data, size_t size)
{
    const std::string text (data, size);
    if (text.compare (0, 12, "MPCKITDEMO1;") != 0)
        return;
    for (int i = 0; i < demo::kNumParams; ++i)
    {
        const std::string key = std::string (demo::kParams[i].key) + "=";
        size_t at = text.find (key);
        while (at != std::string::npos && at > 0 && text[at - 1] != ';')
            at = text.find (key, at + 1);
        if (at != std::string::npos)
            inst.hostWrite (i, demo::params().toNormalised (i, static_cast<float> (std::atof (text.c_str() + at + key.size()))));
    }
}

void fillProperties (const mpckit::ParamDesc& d, VstParameterProperties* props)
{
    std::memset (props, 0, sizeof *props);
    putBounded (props->label, sizeof props->label, mpckit::unitLabel (d.unit));
    props->stepFloat = d.interval > 0 ? d.interval : (d.max - d.min) / 1000.0f;
    props->smallStepFloat = d.interval > 0 ? d.interval : (d.max - d.min) / 10000.0f;
    props->largeStepFloat = d.interval > 0 ? d.interval : (d.max - d.min) / 100.0f;
    props->flags = kVstParameterCanRamp;
    if (d.scale == mpckit::Scale::stepped)
    {
        props->flags |= kVstParameterUsesIntegerMinMax;
        props->minInteger = static_cast<VstInt32> (d.min);
        props->maxInteger = static_cast<VstInt32> (d.max);
        props->stepInteger = 1;
    }
}

void setParameter (AEffect* e, VstInt32 index, float value)
{
    Plugin* p = pluginOf (e);
    if (p == nullptr || index < 0)
        return;
    if (isQ (index))
    {
        p->inst.q.hostWrite (index - kQBase, value, false, [p] (int target, float norm) { p->inst.hostWrite (target, norm); });
        return;
    }
    if (index < demo::kNumParams)
        p->inst.hostWrite (index, value);
}

float getParameter (AEffect* e, VstInt32 index)
{
    Plugin* p = pluginOf (e);
    if (p == nullptr || index < 0)
        return 0.0f;
    if (index == kOverlayIndex)
        return p->inst.overlayValue();
    if (isQ (index))
        return p->inst.q.value (index - kQBase);
    return index < demo::kNumParams ? p->inst.normalised (index) : 0.0f;
}

void processReplacing (AEffect* e, float** /*inputs*/, float** outputs, VstInt32 frames)
{
    Plugin* p = pluginOf (e);
    if (p == nullptr || outputs == nullptr || outputs[0] == nullptr || outputs[1] == nullptr || frames <= 0)
        return;
    p->inst.process (outputs[0], outputs[1], frames);
}

void midiEvent (DemoInstance& inst, const unsigned char* d)
{
    const int status = d[0] & 0xf0;
    if (status == 0x90 && d[2] > 0)
        inst.engine.noteOn (d[1], d[2]);
    else if (status == 0x80 || status == 0x90)
        inst.engine.noteOff (d[1]);
    else if (status == 0xb0 && (d[1] == 123 || d[1] == 120))
        inst.engine.allNotesOff();
}

VstIntPtr dispatch (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    Plugin* p = pluginOf (e);
    if (p != nullptr && ! p->hostKnows.load (std::memory_order_relaxed))
        p->hostKnows.store (true, std::memory_order_release);
    if (p == nullptr && opcode != effOpen && opcode != effClose)
        return 0;
    char* text = static_cast<char*> (ptr);
    switch (opcode)
    {
        case effOpen:
            return 1;
        case effClose:
            if (p != nullptr)
            {
                p->stop.store (true, std::memory_order_release);
                if (p->display.joinable())
                    p->display.join();
                demo::detachOverlay (p->inst);
                e->object = nullptr;
                delete p;
            }
            delete e;
            return 1;
        case effSetSampleRate:
            if (std::isfinite (opt) && opt > 0.0f)
                p->inst.setSampleRate (static_cast<double> (opt));
            return 1;
        case effSetBlockSize:
        case effStartProcess:
        case effStopProcess:
        case effSetProgram:
        case effConnectInput_Deprecated:
        case effConnectOutput_Deprecated:
        case effSetSpeakerArrangement:
            return 1;
        case effMainsChanged:
            if (value == 0)
                p->inst.engine.allNotesOff();
            return 1;
        case effGetProgram:
            return 0;
        case effGetProgramName:
        case effGetProgramNameIndexed:
            if (text == nullptr || (opcode == effGetProgramNameIndexed && index != 0))
                return 0;
            putBounded (text, kVstMaxProgNameLen + 1, "Init");
            return 1;
        case effGetParamName:
        case effGetParamLabel:
        case effGetParamDisplay:
        {
            if (text == nullptr || index < 0 || index >= kTotalParams)
                return 0;
            if (index == kOverlayIndex)
                putBounded (text, kCap, opcode == effGetParamName ? "Overlay Instance" : "");
            else if (isQ (index))
            {
                const int q = index - kQBase;
                if (opcode == effGetParamName)
                    p->inst.q.proxyName (q, text, kCap);
                else if (opcode == effGetParamLabel)
                    p->inst.q.proxyLabel (q, text, kCap);
                else
                    p->inst.q.proxyDisplay (q, text, kCap);
            }
            else if (opcode == effGetParamName)
                putBounded (text, kCap, demo::kParams[index].name);
            else if (opcode == effGetParamLabel)
                putBounded (text, kCap, mpckit::unitLabel (demo::kParams[index].unit));
            else
                demo::params().hostText (index, p->inst.param (index), text, kCap);
            return 1;
        }
        case effCanBeAutomated:
            return index >= 0 && index < demo::kNumParams ? 1 : 0;
        case effGetParameterProperties:
        {
            VstParameterProperties* props = static_cast<VstParameterProperties*> (ptr);
            if (props == nullptr || index < 0 || index >= kTotalParams)
                return 0;
            if (index == kOverlayIndex)
            {
                std::memset (props, 0, sizeof *props);
                props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f / static_cast<float> (DemoInstance::kFrames);
            }
            else if (isQ (index))
            {
                // a Q proxy has the properties of its target: a stepped target is driven in steps by the router, so the host must see a plain knob (shell-mpc/q_router.h)
                const int t = p->inst.q.target (index - kQBase);
                std::memset (props, 0, sizeof *props);
                if (t >= 0 && demo::params().stepCount (t) >= 2)
                {
                    props->flags = kVstParameterCanRamp;
                    props->stepFloat = props->smallStepFloat = 1.0f / 128.0f;
                    props->largeStepFloat = 1.0f / 16.0f;
                }
                else
                {
                    props->stepFloat = 0.01f;
                    props->smallStepFloat = 0.001f;
                    props->largeStepFloat = 0.1f;
                }
            }
            else
                fillProperties (demo::kParams[index], props);
            return 1;
        }
        case effProcessEvents:
        {
            VstEvents* events = static_cast<VstEvents*> (ptr);
            if (events == nullptr)
                return 0;
            for (VstInt32 i = 0; i < events->numEvents; ++i)
            {
                VstEvent* ev = events->events[i];
                if (ev != nullptr && ev->type == kVstMidiType)
                    midiEvent (p->inst, reinterpret_cast<const unsigned char*> (reinterpret_cast<VstMidiEvent*> (ev)->midiData));
            }
            return 1;
        }
        case effGetChunk:
        {
            std::lock_guard<std::mutex> g (p->chunkMutex);
            const std::string c = buildChunk (p->inst);
            p->chunk.assign (c.begin(), c.end());
            if (ptr != nullptr)
                *static_cast<void**> (ptr) = p->chunk.data();
            return static_cast<VstIntPtr> (c.size());
        }
        case effSetChunk:
            if (ptr != nullptr && value > 0)
                applyChunk (p->inst, static_cast<const char*> (ptr), static_cast<size_t> (value));
            return 1;
        case effGetPlugCategory:
            return kPlugCategSynth;
        case effGetEffectName:
        case effGetProductString:
            putBounded (text, (opcode == effGetEffectName ? kVstMaxEffectNameLen : kVstMaxProductStrLen) + 1, "MpcKit Demo");
            return text != nullptr;
        case effGetVendorString:
            putBounded (text, kVstMaxVendorStrLen + 1, "Example");
            return text != nullptr;
        case effGetVendorVersion:
            return 1000;
        case effCanDo:
        {
            const char* s = static_cast<const char*> (ptr);
            if (s == nullptr)
                return 0;
            if (std::strcmp (s, "receiveVstMidiEvent") == 0 || std::strcmp (s, "receiveVstEvents") == 0)
                return 1;
            if (std::strcmp (s, "sendVstMidiEvent") == 0 || std::strcmp (s, "sendVstEvents") == 0 || std::strcmp (s, "offline") == 0 || std::strcmp (s, "audioInput") == 0)
                return -1;
            return 0;
        }
        case effGetInputProperties:
        case effGetOutputProperties:
        {
            VstPinProperties* props = static_cast<VstPinProperties*> (ptr);
            if (props == nullptr)
                return 0;
            std::memset (props, 0, sizeof *props);
            putBounded (props->label, sizeof props->label, "Out");
            props->flags = kVstPinIsActive;
            if (opcode == effGetOutputProperties)
                props->flags |= kVstPinIsStereo;
            return 1;
        }
        default:
            return 0;
    }
}

VstIntPtr dispatcher (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    try { return dispatch (e, opcode, index, value, ptr, opt); }
    catch (...) { return 0; }   // no exception may reach the host
}

} // namespace

#if defined(__APPLE__) || defined(__linux__)
#define MPCKIT_EXPORT __attribute__((visibility("default")))
#else
#define MPCKIT_EXPORT
#endif

extern "C" MPCKIT_EXPORT AEffect* VSTPluginMain (audioMasterCallback audioMaster)
{
    MPCKIT_SCOPE ("plugin", "VSTPluginMain");
    Plugin* p = nullptr;
    try { p = new Plugin(); }
    catch (...) { return nullptr; }
    AEffect* e = new (std::nothrow) AEffect();
    if (e == nullptr)
    {
        delete p;
        return nullptr;
    }
    std::memset (e, 0, sizeof *e);
    e->magic = kEffectMagic;
    e->dispatcher = dispatcher;
    e->processReplacing = processReplacing;
    e->setParameter = setParameter;
    e->getParameter = getParameter;
    e->numPrograms = 1;
    e->numParams = kTotalParams;
    e->numInputs = 0;
    e->numOutputs = 2;
    e->flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
    e->object = p;
    // the unique id: four bytes, the same as "uid" in package/plugin.json
    e->uniqueID = (static_cast<VstInt32> ('M') << 24) | (static_cast<VstInt32> ('c') << 16) | (static_cast<VstInt32> ('K') << 8) | static_cast<VstInt32> ('D');
    e->version = 1000;
    p->effect = e;
    p->master = audioMaster;
    if (audioMaster != nullptr)
        audioMaster (e, audioMasterVersion, 0, 0, nullptr, 0.0f);
    try { p->display = std::thread (displayLoop, p); }
    catch (...) {}   // no thread: the plugin still plays, the host is not told about page writes
    p->inst.setSlot (demo::attachOverlay (p->inst));
    return e;
}
