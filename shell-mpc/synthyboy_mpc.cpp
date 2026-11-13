// SynthyBoy - MPC VST2 shell (headless, no JUCE, no CLAP).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/events.h"
#include "synthyboy/midi_events.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"

#include "mpc_vst2_abi.h"
#include "plugin_paths.h"
#include "program_list.h"

// ---------------------------------------------------------------------------
// Denormal flush helpers (cross-platform)
// ---------------------------------------------------------------------------

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

static inline void flushDenormalsOn() noexcept
{
#if defined(__arm__)
    uint32_t fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (uint32_t)0x01000000; // FZ bit (24)
    __asm__ __volatile__("vmsr fpscr, %0" :: "r"(fpscr));
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (uint64_t)0x01000000; // FZ bit
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#else
    (void)0;
#endif
}

static inline uint32_t flushDenormalsSave() noexcept
{
#if defined(__x86_64__) || defined(__i386__)
    return _MM_GET_FLUSH_ZERO_MODE();
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    return static_cast<uint32_t>(fpcr & 0xFFFFFFFF);
#elif defined(__arm__)
    uint32_t fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    return fpscr;
#else
    return 0;
#endif
}

static inline void flushDenormalsRestore(uint32_t saved) noexcept
{
#if defined(__x86_64__) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(saved);
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr = (fpcr & ~0xFFFFFFFFull) | saved;
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
#elif defined(__arm__)
    uint32_t fpscr = saved;
    __asm__ __volatile__("vmsr fpscr, %0" :: "r"(fpscr));
#else
    (void)saved;
#endif
}

// Copy src into dst, at most dstSize-1 bytes, always NUL-terminating.
// Replaces strncpy to avoid -Wstringop-truncation with GCC 12.
static inline void copyString(char* dst, size_t dstSize, const char* src) noexcept
{
    if (dstSize == 0) return;
    size_t i = 0;
    for (; i < dstSize - 1 && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

// ---------------------------------------------------------------------------
// VSTPluginMain forward declaration (used for dladdr content path)
// ---------------------------------------------------------------------------

extern "C" AEffect* VSTPluginMain(audioMasterCallback audioMaster);

// ---------------------------------------------------------------------------
// Plugin instance state
// ---------------------------------------------------------------------------

// Fixed-capacity event buffer for the audio thread (no allocations).
struct EventBuffer
{
    static constexpr size_t kCapacity = 1024;
    synthyboy::Event slots[kCapacity];
    size_t count = 0;

    void push(const synthyboy::Event& ev) noexcept
    {
        if (count < kCapacity)
        {
            slots[count++] = ev;
            return;
        }
        // Buffer is full.
        if (ev.type != synthyboy::Event::NoteOff)
        {
            // Not a note-off: it may replace the last element (newest value wins), but never a note-off,
            // which would leave a note hanging; then the new event is the one that is dropped.
            if (slots[count - 1].type != synthyboy::Event::NoteOff)
            {
                synthyboy::Event e = ev;
                if (count >= 2 && e.frame < slots[count - 2].frame) e.frame = slots[count - 2].frame;
                slots[count - 1] = e;
            }
            return;
        }
        // Note-off when full: search backwards for the last element that is NOT
        // a note-off, remove it by shifting following elements down by one,
        // then append this note-off at the end.
        // If every element is a note-off, drop the new one.
        int replaceIdx = -1;
        for (int i = static_cast<int>(count) - 1; i >= 0; --i)
        {
            if (slots[static_cast<size_t>(i)].type != synthyboy::Event::NoteOff)
            {
                replaceIdx = i;
                break;
            }
        }
        if (replaceIdx < 0)
            return;  // every element is a note-off: drop the new one
        // Shift elements [replaceIdx+1 .. count-1] down by one
        for (int i = replaceIdx + 1; i < static_cast<int>(count); ++i)
            slots[static_cast<size_t>(i - 1)] = slots[static_cast<size_t>(i)];
        // Append the note-off at the end; frame = max(ev.frame, slots[count-2].frame)
        synthyboy::Event e = ev;
        if (count >= 2 && e.frame < slots[count - 2].frame)
            e.frame = slots[count - 2].frame;
        slots[count - 1] = e;
    }

    void clear() noexcept { count = 0; }
};

struct PluginInstance
{
    // Core
    std::unique_ptr<synthyboy::IEngine> engine;
    synthyboy::StateContent content;
    EventBuffer events;

    // Transport
    double sampleRate = 44100.0;
    int lastPreparedSampleRate = 0;
    int lastPreparedBlockSize = 0;
    int maxBlockSize = 128;
    bool mainsOn = false;
    bool prepared = false; // true after the first successful prepare

    // Programs
    std::vector<synthyboy_mpc::ProgramEntry> programs;
    std::atomic<int> currentProgram {0};
    // The "chunk wins" protection: the MPC saves the program number in the project
    // and may select that program again right after restoring the chunk; that selection
    // must not overwrite the restored (possibly edited) state.
    // Host order: loads program 0, restores the chunk (which arms protection), then
    // may re-select the saved program number. Disarmed by any effSetProgram or
    // parameter write; armed by effSetChunk when the chunk contains a valid program index.
    std::atomic<int> protectedProgram {-1};

    // "no program" state = the loaded chunk names a program this package does not have;
    // the sound is the chunk's, the host is given index 0, the saved name is shown
    // and saved again.  Only the UI thread touches these.
    bool noProgram = false;
    std::string noProgramName;

    // Chunk buffer: owned by the plugin instance, reused across effGetChunk calls.
    std::vector<char> chunkBuffer;

    // User content store (for resolving content references)
    std::unique_ptr<synthyboy::UserContentStore> contentStore;

    // Thread-safe parameter mirror (single source of truth for all threads)
    std::atomic<float> paramMirror[synthyboy::kMaxParams];

    // Host callback (for transport queries)
    audioMasterCallback audioMaster = nullptr;
    synthyboy::TransportInfo transport { 120.0, 0.0, false };

    PluginInstance()
    {
        // Create engine based on SYNTHYBOY_MPC_ENGINE_VITAL (from CMake) && SYNTHYBOY_HAS_VITAL (from core)
#if SYNTHYBOY_MPC_ENGINE_VITAL && SYNTHYBOY_HAS_VITAL
        engine = synthyboy::createEngine(synthyboy::EngineKind::vital);
#else
        engine = synthyboy::createEngine(synthyboy::EngineKind::own);
#endif

        // Build content store path: Content/ next to the plugin binary.
        // UserContentStore keeps its files under <user directory>/content.
        Dl_info info;
        std::string pluginFolder;
        if (dladdr (reinterpret_cast<const void*>(&VSTPluginMain), &info) && info.dli_fname)
            pluginFolder = synthyboy_mpc::pluginFolderFor (info.dli_fname);

        std::string contentDir;
        if (!pluginFolder.empty())
        {
            contentDir = synthyboy_mpc::contentDirFor (pluginFolder);
            // A content directory that is missing or cannot be used must not keep the plugin from loading: it then runs without a store.
            try
            {
                contentStore = std::make_unique<synthyboy::UserContentStore> (contentDir);
                contentStore->addPackRoot (contentDir + "/packs"); // installed content packs: <user directory>/packs/<pack>/<file>
            }
            catch (...) { contentStore.reset(); }
            if (engine && contentStore)
                engine->setContentStore (contentStore.get());
        }

        // Scan factory presets (never on the audio thread).
        if (!contentDir.empty())
            programs = synthyboy_mpc::scanPrograms (contentDir + "/presets");

        // Apply default values to engine and mirror
        if (engine)
        {
            const auto* table = synthyboy::paramTable();
            size_t count = synthyboy::paramCount();
            for (size_t i = 0; i < count; ++i)
            {
                engine->setParam(table[i].id, table[i].def);
                paramMirror[i].store(table[i].def, std::memory_order_relaxed);
            }
        }
    }

    ~PluginInstance()
    {
        if (engine)
            engine->setContentStore(nullptr);
    }
};

// ---------------------------------------------------------------------------
// Helper: get instance from AEffect
// ---------------------------------------------------------------------------

static PluginInstance* getInstance(AEffect* e) noexcept
{
    return static_cast<PluginInstance*>(e->object);
}

// ---------------------------------------------------------------------------
// Helper: current program name (handles the "no program" state)
// ---------------------------------------------------------------------------

static std::string currentProgramName (PluginInstance* inst)
{
    if (inst->noProgram)
        return inst->noProgramName.empty() ? "Init" : inst->noProgramName;
    int prog = static_cast<int> (inst->currentProgram.load (std::memory_order_relaxed));
    if (prog == 0)
        return "Init";
    if (prog >= 1 && prog <= static_cast<int> (inst->programs.size()))
        return inst->programs[static_cast<size_t> (prog - 1)].name;
    return "Init";
}

// ---------------------------------------------------------------------------
// Apply a complete state to the engine and mirror
// ---------------------------------------------------------------------------

static void applyState (PluginInstance* inst, const synthyboy::ParamValues& values, synthyboy::StateContent&& content)
{
    const auto* table = synthyboy::paramTable();
    size_t count = synthyboy::paramCount();
    for (size_t i = 0; i < count; ++i)
    {
        inst->paramMirror[i].store(values.plain[i], std::memory_order_relaxed);
        if (inst->engine)
            inst->engine->setParam(table[i].id, values.plain[i]);
    }
    inst->content = std::move(content);
    if (inst->engine)
        inst->engine->setContent(inst->content);
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

static VstIntPtr dispatch(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

// The entry the host calls. The plugin runs inside the MPC's own process (no sandbox) and the VST2 interface is a C ABI:
// no exception may leave it. Whatever fails inside (allocation, file access, a parser) ends the call with 0.
static VstIntPtr pluginDispatcher(AEffect* e, VstInt32 opcode, VstInt32 index,
                                   VstIntPtr value, void* ptr, float opt)
{
    try { return dispatch(e, opcode, index, value, ptr, opt); }
    catch (...) { return 0; }
}

static VstIntPtr dispatch(AEffect* e, VstInt32 opcode, VstInt32 index,
                          VstIntPtr value, void* ptr, float opt)
{
    if (opcode == effGetVstVersion)
        return 2400;

    PluginInstance* inst = getInstance(e);
    if (!inst && opcode != effOpen && opcode != effClose)
        return 0;

    switch (opcode)
    {
    case effOpen:
    {
        // effOpen can be called multiple times; the MPC calls it twice during load
        // We don't reallocate: the instance already exists
        return 1;
    }

    case effClose:
    {
        // Free both the instance and the AEffect struct
        if (inst)
        {
            delete inst;
            e->object = nullptr;
        }
        delete e;
        return 1;
    }

    case effSetProgram:
    {
        if (!inst) return 0;
        // VST2: value is the program index, index is unused
        int prog = static_cast<int>(value);
        int numProgs = static_cast<int> (inst->programs.size()) + 1;
        // Out of range: do nothing
        if (prog < 0 || prog >= numProgs) return 1;

        // Check if protection is armed for this index BEFORE disarming
        bool wasProtected = (inst->protectedProgram.load (std::memory_order_relaxed) == prog);
        // Disarm protection (any effSetProgram disarms it)
        inst->protectedProgram.store (-1, std::memory_order_relaxed);

        // If protection was armed for this index, just set it and return
        if (wasProtected)
        {
            inst->currentProgram.store (prog, std::memory_order_relaxed);
            inst->noProgram = false;
            inst->noProgramName.clear();
            return 1;
        }

        if (prog == 0)
        {
            // Program 0: Init (default values, empty content)
            applyState (inst, synthyboy::ParamValues::defaults (), synthyboy::StateContent {});
        }
        else
        {
            // Program >= 1: load preset file
            const auto& entry = inst->programs[static_cast<size_t> (prog - 1)];
            std::ifstream in (entry.path, std::ios::binary);
            if (!in) return 1; // file cannot be read
            std::string data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char> ());
            if (data.empty()) return 1;

            synthyboy::PresetMeta meta;
            synthyboy::StateContent content;
            synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
            synthyboy::LoadResult result = synthyboy::loadPreset (data.data(), data.size(), meta, values, content);
            if (result.status != synthyboy::LoadStatus::ok && result.status != synthyboy::LoadStatus::newer_version)
                return 1; // load failed

            applyState (inst, values, std::move(content));
        }

        inst->currentProgram.store (prog, std::memory_order_relaxed);
        inst->noProgram = false;
        inst->noProgramName.clear();
        // Request display update from host
        if (inst->audioMaster)
            inst->audioMaster (e, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);
        return 1;
    }

    case effGetProgram:
    {
        if (!inst) return 0;
        return inst->currentProgram.load (std::memory_order_relaxed);
    }

    case effSetProgramName:
    {
        // Not supported for our fixed program list
        return 0;
    }

    case effGetProgramName:
    {
        if (!inst) return 0;
        char* name = static_cast<char*>(ptr);
        if (!name) return 0;
        const std::string& progName = currentProgramName (inst);
        copyString(name, kVstMaxProgNameLen + 1, progName.c_str());
        return 1;
    }

    case effGetProgramNameIndexed:
    {
        char* name = static_cast<char*>(ptr);
        if (!inst || !name || index < 0) return 0;
        int numProgs = static_cast<int> (inst->programs.size()) + 1;
        if (index >= numProgs) return 0;
        const std::string& progName = (index == 0) ? std::string ("Init") : inst->programs[static_cast<size_t> (index - 1)].name;
        copyString(name, kVstMaxProgNameLen + 1, progName.c_str());
        return 1;
    }

    case effGetParamName:
    case effGetParamLabel:
    case effGetParamDisplay:
    {
        if (!inst) return 0;
        char* buf = static_cast<char*>(ptr);
        if (!buf || index < 0) return 0;

        size_t paramCount = synthyboy::paramCount();
        if (static_cast<size_t>(index) >= paramCount) return 0;

        const synthyboy::ParamInfo* info = synthyboy::paramTable() + index;

        // Use a generous buffer size (host passes at least 24 bytes for params)
        constexpr int kMaxParamStr = 64;

        if (opcode == effGetParamName)
        {
            copyString(buf, kMaxParamStr, info->name);
            buf[kMaxParamStr - 1] = '\0';
        }
        else if (opcode == effGetParamLabel)
        {
            const char* unit = synthyboy::unitLabel(info->unit);
            copyString(buf, kMaxParamStr, unit);
            buf[kMaxParamStr - 1] = '\0';
        }
        else // effGetParamDisplay
        {
            float plain = inst->paramMirror[index].load(std::memory_order_relaxed);
            synthyboy::valueToText(*info, plain, buf, kMaxParamStr);
        }
        return 1;
    }

    case effSetSampleRate:
    {
        if (!inst) return 0;
        inst->sampleRate = opt;
        // Only store the rate; prepare is called in effMainsChanged
        return 1;
    }

    case effSetBlockSize:
    {
        if (!inst) return 0;
        inst->maxBlockSize = static_cast<int>(value);
        // Only store the block size; prepare is called in effMainsChanged
        return 1;
    }

    case effMainsChanged:
    {
        if (!inst) return 0;
        inst->mainsOn = (value != 0);
        // Prepare when mains go on if sample rate or block size changed since last prepare
        if (inst->mainsOn && inst->engine)
        {
            int sr = static_cast<int>(inst->sampleRate);
            int bs = inst->maxBlockSize;
            if (!inst->prepared || sr != inst->lastPreparedSampleRate || bs != inst->lastPreparedBlockSize)
            {
                synthyboy::Quality quality = synthyboy::qualityForTier(synthyboy::Tier::low);
                quality.realtime = true; // live: audio thread never waits for helper thread
                inst->engine->prepare(inst->sampleRate, inst->maxBlockSize, quality);
                inst->prepared = true;
                inst->lastPreparedSampleRate = sr;
                inst->lastPreparedBlockSize = bs;
            }
        }
        return 1;
    }

    case effProcessEvents:
    {
        if (!inst) return 0;
        VstEvents* events = static_cast<VstEvents*>(ptr);
        if (!events) return 0;

        for (VstInt32 i = 0; i < events->numEvents; ++i)
        {
            VstEvent* ev = events->events[i];
            if (!ev || ev->type != kVstMidiType)
                continue;

            VstMidiEvent* midiEv = reinterpret_cast<VstMidiEvent*>(ev);
            const uint8_t* data = reinterpret_cast<const uint8_t*>(midiEv->midiData);
            int numBytes = 3;
            if (data[0] == 0xF0) // SysEx: only first byte
                numBytes = 1;
            else if (data[0] == 0xF7) // SysEx
                numBytes = 1;

            // Clamp negative deltaFrames to 0
            int32_t deltaFrames = midiEv->deltaFrames;
            if (deltaFrames < 0)
                deltaFrames = 0;

            synthyboy::Event event;
            if (synthyboy::translateMidiMessage(data, numBytes, deltaFrames, event))
            {
                inst->events.push(event);
            }
        }
        return 1;
    }

    case effCanBeAutomated:
    {
        // All parameters are automatable
        return 1;
    }

    case effGetChunk:
    {
        if (!inst) return 0;
        // index: 0 = bank, 1 = program
        // Build ParamValues from the atomic mirror (thread-safe snapshot)
        synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            values.plain[i] = inst->paramMirror[i].load(std::memory_order_relaxed);
        std::string state = synthyboy::saveState(values, inst->content);
        int prog = static_cast<int> (inst->currentProgram.load (std::memory_order_relaxed));
        std::string chunk = synthyboy_mpc::chunkWithProgram (state, prog, currentProgramName (inst));
        // Reuse the plugin-owned chunk buffer (VST2: host does not free chunk memory)
        inst->chunkBuffer.resize(chunk.size());
        std::memcpy(inst->chunkBuffer.data(), chunk.data(), chunk.size());
        if (ptr)
            *static_cast<void**>(ptr) = inst->chunkBuffer.data();
        return static_cast<VstInt32>(chunk.size());
    }

    case effSetChunk:
    {
        if (!inst) return 0;
        // index: 0 = bank, 1 = program
        const void* chunk = ptr;
        VstInt32 size = value;

        if (size > 0 && chunk)
        {
            synthyboy::ParamValues newValues = synthyboy::ParamValues::defaults();
            synthyboy::StateContent newContent;
            synthyboy::LoadResult result = synthyboy::loadStateWithContent(chunk, static_cast<size_t>(size), newValues, newContent);

            if (result.status == synthyboy::LoadStatus::ok || result.status == synthyboy::LoadStatus::newer_version)
            {
                // Try to extract program number from chunk
                int idx = synthyboy_mpc::programFromChunk(chunk, static_cast<size_t>(size));

                applyState (inst, newValues, std::move(newContent));

                if (idx == -1)
                {
                    // Plain state document without a program number: leave currentProgram / noProgram / noProgramName as they are
                    inst->protectedProgram.store (-1, std::memory_order_relaxed);
                }
                else
                {
                    // Has a program number: try to read the name
                    std::string name;
                    bool hasName = synthyboy_mpc::programNameFromChunk(chunk, static_cast<size_t>(size), name);
                    int r = synthyboy_mpc::resolveProgram (inst->programs, idx, hasName, name);

                    if (r >= 0)
                    {
                        inst->currentProgram.store (r, std::memory_order_relaxed);
                        inst->noProgram = false;
                        inst->noProgramName.clear();
                        inst->protectedProgram.store (r, std::memory_order_relaxed);
                    }
                    else
                    {
                        // Program not found: no-program state
                        inst->currentProgram.store (0, std::memory_order_relaxed);
                        inst->noProgram = true;
                        inst->noProgramName = hasName ? name : "";
                        inst->protectedProgram.store (-1, std::memory_order_relaxed);
                    }
                }
            }
            else
            {
                // Load failed: disarm protection
                inst->protectedProgram.store (-1, std::memory_order_relaxed);
            }
        }
        return 1;
    }

    case effGetPlugCategory:
    {
        return kPlugCategSynth;
    }

    case effGetParameterProperties:
    {
        if (!inst) return 0;
        VstParameterProperties* props = static_cast<VstParameterProperties*>(ptr);
        if (!props || index < 0) return 0;

        size_t paramCount = synthyboy::paramCount();
        if (static_cast<size_t>(index) >= paramCount) return 0;

        const synthyboy::ParamInfo* info = synthyboy::paramTable() + index;
        std::memset(props, 0, sizeof(*props));

        // Fill basic info
        copyString(props->label, sizeof(props->label), synthyboy::unitLabel(info->unit));
        props->stepFloat = info->interval > 0 ? info->interval : (info->max - info->min) / 1000.0f;
        props->smallStepFloat = info->interval > 0 ? info->interval : (info->max - info->min) / 10000.0f;
        props->largeStepFloat = info->interval > 0 ? info->interval : (info->max - info->min) / 100.0f;
        props->flags = kVstParameterCanRamp;

        if (info->flags & synthyboy::kParamStepped)
        {
            props->flags |= kVstParameterUsesIntegerMinMax;
            props->minInteger = static_cast<VstInt32>(info->min);
            props->maxInteger = static_cast<VstInt32>(info->max);
            props->stepInteger = info->interval > 0 ? static_cast<VstInt32>(info->interval) : 1;
        }

        return 1;
    }

    case effGetEffectName:
    {
        char* name = static_cast<char*>(ptr);
        if (name)
        {
            copyString(name, kVstMaxEffectNameLen + 1, "SynthyBoy");
            name[kVstMaxEffectNameLen] = '\0';
        }
        return 1;
    }

    case effGetVendorString:
    {
        char* vendor = static_cast<char*>(ptr);
        if (vendor)
        {
            copyString(vendor, kVstMaxVendorStrLen + 1, "Safian");
            vendor[kVstMaxVendorStrLen] = '\0';
        }
        return 1;
    }

    case effGetProductString:
    {
        char* product = static_cast<char*>(ptr);
        if (product)
        {
            copyString(product, kVstMaxProductStrLen + 1, "SynthyBoy");
            product[kVstMaxProductStrLen] = '\0';
        }
        return 1;
    }

    case effGetVendorVersion:
    {
        return 1000; // version 1.0.0.0
    }

    case effCanDo:
    {
        const char* s = static_cast<const char*>(ptr);
        if (!s) return 0;

        // The MPC checks these
        if (std::strcmp(s, "receiveVstMidiEvent") == 0) return 1;
        if (std::strcmp(s, "sendVstMidiEvent") == 0) return -1;
        if (std::strcmp(s, "receiveVstEvents") == 0) return 1;
        if (std::strcmp(s, "sendVstEvents") == 0) return -1;
        if (std::strcmp(s, "offline") == 0) return -1;
        if (std::strcmp(s, "audioInput") == 0) return -1;
        // MPE: the engine supports MPE and the shell forwards the channel
        // (translateMidiMessage preserves channel in Event), but VST2 has no
        // per-note-expression mechanism so MPE is limited to channel-based
        // pitch bend / pressure only.
        if (std::strcmp(s, "MPE") == 0) return 1;
        // bypass: not implemented
        if (std::strcmp(s, "bypass") == 0) return 0;
        // pluginCloaking: not implemented
        if (std::strcmp(s, "pluginCloaking") == 0) return 0;

        return 0; // don't know for others
    }

    case effConnectInput_Deprecated:
    case effConnectOutput_Deprecated:
    {
        // Deprecated, but handle gracefully
        return 1;
    }

    case effGetInputProperties:
    case effGetOutputProperties:
    {
        // No inputs, 2 outputs
        VstPinProperties* props = static_cast<VstPinProperties*>(ptr);
        if (!props) return 0;

        std::memset(props, 0, sizeof(*props));
        copyString(props->label, sizeof(props->label), "Out");
        props->flags = kVstPinIsActive;

        if (opcode == effGetOutputProperties)
        {
            props->flags |= kVstPinIsStereo;
        }

        return 1;
    }

    case effGetSpeakerArrangement:
    {
        // The MPC passes in/out pointers; we fill them
        VstSpeakerArrangement** inArr = reinterpret_cast<VstSpeakerArrangement**>(ptr);
        VstSpeakerArrangement** outArr = reinterpret_cast<VstSpeakerArrangement**>(value);

        if (inArr)
        {
            static VstSpeakerArrangement emptyArr;
            std::memset(&emptyArr, 0, sizeof(emptyArr));
            emptyArr.type = kSpeakerArrEmpty;
            emptyArr.numChannels = 0;
            *inArr = &emptyArr;
        }
        if (outArr)
        {
            static VstSpeakerArrangement stereoArr;
            std::memset(&stereoArr, 0, sizeof(stereoArr));
            stereoArr.type = kSpeakerArrStereo;
            stereoArr.numChannels = 2;
            for (int i = 0; i < 2; ++i)
                stereoArr.speakers[i].type = i;
            *outArr = &stereoArr;
        }
        return 1;
    }

    case effSetSpeakerArrangement:
    {
        // The MPC calls this with arrangement info; we accept it
        return 1;
    }

    case effIdle_Deprecated:
    {
        // No idle processing needed
        return 0;
    }

    case effStartProcess:
    case effStopProcess:
    {
        // Process start/stop
        return 1;
    }

    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Parameter access (normalized 0..1)
// ---------------------------------------------------------------------------

static void pluginSetParameter(AEffect* e, VstInt32 index, float parameter)
{
    PluginInstance* inst = getInstance(e);
    if (!inst || index < 0)
        return;

    size_t paramCount = synthyboy::paramCount();
    if (static_cast<size_t>(index) >= paramCount)
        return;

    const synthyboy::ParamInfo* info = synthyboy::paramTable() + index;
    float plain = synthyboy::fromNormalised(*info, parameter);
    plain = synthyboy::constrainValue(*info, plain);

    // Avoid redundant engine updates (e.g. parameter 0 toggle at every prepare)
    float oldPlain = inst->paramMirror[index].load(std::memory_order_relaxed);
    if (plain == oldPlain)
        return;

    inst->paramMirror[index].store(plain, std::memory_order_relaxed);

    // Disarm protection: any parameter write from the host disarms it
    inst->protectedProgram.store (-1, std::memory_order_relaxed);

    // Call engine directly (IEngine::setParam is lock-free, any thread)
    if (inst->engine)
        inst->engine->setParam(info->id, plain);
}

static float pluginGetParameter(AEffect* e, VstInt32 index)
{
    PluginInstance* inst = getInstance(e);
    if (!inst || index < 0)
        return 0.0f;

    size_t paramCount = synthyboy::paramCount();
    if (static_cast<size_t>(index) >= paramCount)
        return 0.0f;

    // Read from the atomic mirror (thread-safe, reflects the latest value)
    float plain = inst->paramMirror[index].load(std::memory_order_relaxed);
    const synthyboy::ParamInfo* info = synthyboy::paramTable() + index;
    return synthyboy::toNormalised(*info, plain);
}

// ---------------------------------------------------------------------------
// Audio processing
// ---------------------------------------------------------------------------

static void pluginProcessReplacing(AEffect* e, float** /*inputs*/, float** outputs, VstInt32 sampleFrames)
{
    PluginInstance* inst = getInstance(e);
    if (!inst || !inst->engine)
    {
        // Zero output
        for (int i = 0; i < e->numOutputs; ++i)
            if (outputs[i])
                std::memset(outputs[i], 0, sizeof(float) * static_cast<size_t>(sampleFrames));
        return;
    }

    // Return early for sampleFrames <= 0 (keep pending events for next block)
    if (sampleFrames <= 0)
        return;

    // Save and flush denormals
    uint32_t savedFz = flushDenormalsSave();
    flushDenormalsOn();

    // Update transport from host once per block
    if (inst->audioMaster)
    {
        VstTimeInfo* timeInfo = reinterpret_cast<VstTimeInfo*>(
            static_cast<VstIntPtr>(inst->audioMaster(e, audioMasterGetTime, kVstTempoValid | kVstPpqPosValid | kVstTransportPlaying, 0, nullptr, 0.0f)));
        if (timeInfo)
        {
            inst->transport.bpm = timeInfo->tempo > 0 ? timeInfo->tempo : inst->transport.bpm;
            inst->transport.songPosBeats = timeInfo->ppqPos;
            inst->transport.playing = (timeInfo->flags & kVstTransportPlaying) != 0;
        }
        // If host returns null, keep previous values
        inst->engine->setTransport(inst->transport);
    }

    // Clamp event frames to the last frame of the block
    for (size_t i = 0; i < inst->events.count; ++i)
    {
        if (static_cast<int32_t>(inst->events.slots[i].frame) >= sampleFrames)
            inst->events.slots[i].frame = sampleFrames - 1;
    }

    // Pass events directly to the engine (no slicing, no copy)
    inst->engine->process(inst->events.count > 0 ? inst->events.slots : nullptr,
                          static_cast<int>(inst->events.count),
                          outputs[0], outputs[1], sampleFrames);

    // Clear remaining events
    inst->events.clear();

    // Restore denormal mode
    flushDenormalsRestore(savedFz);
}

// ---------------------------------------------------------------------------
// VSTPluginMain entry point
// ---------------------------------------------------------------------------

// Ensure the symbol is exported even with hidden visibility
#if defined(__APPLE__) || defined(__linux__)
    #define MPC_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
    #define MPC_PLUGIN_EXPORT
#endif

extern "C"
{
    MPC_PLUGIN_EXPORT AEffect* VSTPluginMain(audioMasterCallback audioMaster)
    {
        // Create the plugin instance. No exception may reach the host: a failure here means "no plugin".
        PluginInstance* inst = nullptr;
        try { inst = new PluginInstance(); }
        catch (...) { return nullptr; }

        // Set up the AEffect struct
        AEffect* effect = new (std::nothrow) AEffect();
        if (!effect)
        {
            delete inst;
            return nullptr;
        }

        std::memset(effect, 0, sizeof(AEffect));
        effect->magic = kEffectMagic;
        effect->dispatcher = pluginDispatcher;
        effect->processReplacing = pluginProcessReplacing;
        effect->setParameter = pluginSetParameter;
        effect->getParameter = pluginGetParameter;
        effect->numPrograms = static_cast<VstInt32>(inst->programs.size()) + 1;
        effect->numParams = static_cast<VstInt32>(synthyboy::paramCount());
        effect->numInputs = 0;
        effect->numOutputs = 2;
        effect->flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
        effect->resvd1 = 0;
        effect->resvd2 = 0;
        effect->initialDelay = 0;
        effect->object = inst;
        // Build unique ID from four bytes: (('S'<<24)|('f'<<16)|('S'<<8)|'B')
        effect->uniqueID = (static_cast<VstInt32>('S') << 24) | (static_cast<VstInt32>('f') << 16)
                         | (static_cast<VstInt32>('S') << 8) | static_cast<VstInt32>('B');
        effect->version = 1000;

        // Store the host callback for transport queries
        inst->audioMaster = audioMaster;

        // Call the host callback to let the host identify itself
        if (audioMaster)
            audioMaster(effect, audioMasterVersion, 0, 0, nullptr, 0.0f);

        return effect;
    }
}
