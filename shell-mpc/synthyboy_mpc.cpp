// SynthyBoy - MPC VST2 shell (headless, no JUCE, no CLAP).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <cmath>
#include <dirent.h>
#include <sys/stat.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
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
#include "device_profile.h"
#include "output_peak.h"
#include "startup_timing.h"
#include "kit/log.h"
#include "host_strings.h"
#include "output_stage.h"
#include "safe_file.h"
#include "plugin_paths.h"
#include "poll_guard.h"
#include "program_list.h"

// Live LFO display (docs/MPC.md "LFO pictures"). Compile switch SYNTHYBOY_MPC_LFO_LIVE (CMake option of the same name; the source default is 0:
// with 0 nothing below the switch is compiled and the plugin is exactly the one without this code). When 1 the plugin has four display-only
// parameters after the engine's list (index paramCount() + n - 1 for LFO n): the cursor of the LFO picture of the skin (lfo_live.h).
// Never in the mirror, the state or a preset, not automatable. A helper thread (not the audio thread) reads the engine's live values about
// SYNTHYBOY_MPC_LFO_LIVE_HZ times a second and, when a cursor cell changed, tells the host with audioMasterAutomate (the host redraws only then).
// Thread model: the thread is a normal SCHED_OTHER thread (nice 0, all CPUs; thread_policy.h sets that itself, whatever thread created it), one report
// per changed cursor and tick, in index order, the position always read at the moment of the tick. The audio thread (processReplacing) does nothing
// for the display: the engine's own relaxed atomic stores are all there is; this thread only reads them.
//   SYNTHYBOY_MPC_LFO_LIVE_HZ        refresh rate of that thread (default 10 = a tick every 100 ms, the rate of the community wrapper Open MPC)
//   SYNTHYBOY_MPC_LFO_LIVE_NOTIFY    1 (default): call audioMasterAutomate; 0: update the values only (the host never redraws: a CPU baseline)
//   SYNTHYBOY_MPC_LFO_LIVE_SELFTEST  0 (default); 1: ignore the engine and sweep the four cursors at 0.25 / 0.5 / 1 / 2 cycles a second (measuring the host
//                                    side - recording, modified flag, CPU - without playing a note)
#ifndef SYNTHYBOY_MPC_LFO_LIVE
#define SYNTHYBOY_MPC_LFO_LIVE 0
#endif
#if SYNTHYBOY_MPC_LFO_LIVE
#ifndef SYNTHYBOY_MPC_LFO_LIVE_HZ
#define SYNTHYBOY_MPC_LFO_LIVE_HZ 10
#endif
#ifndef SYNTHYBOY_MPC_LFO_LIVE_NOTIFY
#define SYNTHYBOY_MPC_LFO_LIVE_NOTIFY 1
#endif
#ifndef SYNTHYBOY_MPC_LFO_LIVE_SELFTEST
#define SYNTHYBOY_MPC_LFO_LIVE_SELFTEST 0
#endif
#include <chrono>
#include <cstdio>
#include <thread>
#include "lfo_live.h"
#include "thread_policy.h"
#include "synthyboy/readout.h"
#endif

// Live spectrum (docs/MPC.md "Spectrum"). Compile switch SYNTHYBOY_MPC_SPECTRUM (CMake option of the same name; the source default is 0: with 0 nothing below the switch is
// compiled and the plugin is exactly the one without this code). Needs SYNTHYBOY_MPC_LFO_LIVE: it runs on that display thread. 32 more display-only parameters "Spectrum 1" ..
// "Spectrum 32" follow the four LFO cursors (index paramCount() + 4 .. + 35), never in the mirror, the state or a preset, not automatable, writes are ignored. The audio
// thread copies the mono sum of its output into a lock-free ring (spectrum.h) and does nothing else; on every tick of the display thread the newest 2048 samples go through
// an FFT, 32 logarithmic bands (30 Hz .. 18 kHz) and the smoothing, are quantised to 1/64 and every changed column is reported with audioMasterAutomate (at most 32 calls a
// tick, none while all columns are at the floor).
//   SYNTHYBOY_MPC_SPECTRUM_SELFTEST  0 (default); 1: ignore the audio and sweep a travelling wave over the 32 columns (measuring the host side without a note)
#ifndef SYNTHYBOY_MPC_SPECTRUM
#define SYNTHYBOY_MPC_SPECTRUM 0
#endif
#if SYNTHYBOY_MPC_SPECTRUM
#if !SYNTHYBOY_MPC_LFO_LIVE
#error "SYNTHYBOY_MPC_SPECTRUM needs SYNTHYBOY_MPC_LFO_LIVE (it runs on the display thread)"
#endif
#ifndef SYNTHYBOY_MPC_SPECTRUM_SELFTEST
#define SYNTHYBOY_MPC_SPECTRUM_SELFTEST 0
#endif
#include "spectrum.h"
#endif

// Callback statistics (docs/MPC.md "LFO pictures"; diagnostics only, the source default is 0: with 0 nothing of it is compiled). Compile switch
// SYNTHYBOY_MPC_CALLBACK_STATS (CMake option of the same name). The audio callback measures the time between two calls and its own duration into
// preallocated atomics (callback_stats.h); a low priority thread writes a text file every 5 seconds and at shutdown.
//   SYNTHYBOY_MPC_CALLBACK_STATS_FILE         environment variable: the file (default /tmp/synthyboy-callback-stats.txt; a second plugin instance
//                                             writes <file>.2, a third <file>.3 ...)
//   SYNTHYBOY_MPC_CALLBACK_STATS_INTERVAL_MS  environment variable: pause between two writes (default 5000)
//   SYNTHYBOY_MPC_CALLBACK_STATS_RESET_FILE   environment variable: the file whose existence makes the writer thread zero the MIDI statistics before its next
//                                             write and delete it (default /tmp/synthyboy-stats-reset)
// The file also reports (format 2) the MIDI messages of effProcessEvents and what the host asks: effCanDo texts, dispatcher opcodes, our audioMaster calls.
#ifndef SYNTHYBOY_MPC_CALLBACK_STATS
#define SYNTHYBOY_MPC_CALLBACK_STATS 0
#endif
#if SYNTHYBOY_MPC_CALLBACK_STATS
#include "callback_stats.h"
#endif

// Pad pressure to Filter 1 cutoff (docs/MPC.md "Pad pressure"). Compile switch SYNTHYBOY_MPC_PAD_PRESSURE (CMake option of the same name, ON in shell-mpc; the source
// default is 0: with 0 nothing of it is compiled and the plugin is exactly the one without this code). Two automatable parameters "Pad Pressure" and "Pad Pressure Amount"
// after the display parameters (the last two of the list; only the overlay build's "Overlay Instance" comes after them), saved in the state chunk (pad_pressure.h). The own engine does the work (setOwnPadPressureToCutoff).
#ifndef SYNTHYBOY_MPC_PAD_PRESSURE
#define SYNTHYBOY_MPC_PAD_PRESSURE 0
#endif
#if SYNTHYBOY_MPC_PAD_PRESSURE
#include "pad_pressure.h"
#endif

// Make-up gain of the output stage in dB (CMake cache entry of the same name, docs/MPC.md "Output level"). 0 = no gain. The peak limiter behind it is compiled in only with SYNTHYBOY_MPC_OUTPUT_LIMITER (default off).
// Debug hooks (CMake option SYNTHYBOY_MPC_DEBUG_HOOKS, default OFF, never in the package; docs/MPC.md "Switches and files"): the /tmp files that only the host tests and the acceptance runs on the device use -
// /tmp/synthyboy-ui-fake-write, -q-fake-group, -q-fake-write, -overlay-q-step-travel, -overlay-fake-slot, -play-log (+ -dump, the .txt, the chunk copies). Without the option the plugin does not look at them at all.
#ifndef SYNTHYBOY_MPC_DEBUG_HOOKS
#define SYNTHYBOY_MPC_DEBUG_HOOKS 0
#endif

// Live filter curve on a free display plane (docs/MPC.md "Overlay (experimental)"). Compile switch SYNTHYBOY_MPC_OVERLAY (CMake option of the same name; the source default is 0:
// with 0 nothing below the switch is compiled and the plugin is exactly the one without this code). The module itself is shell-mpc/overlay/; the shell only tells it when an
// instance exists (attach / detach) and answers its two questions (a parameter value, the live cutoff and resonance of filter 1). Needs a skin made with the overlay layout.
#ifndef SYNTHYBOY_MPC_OVERLAY
#define SYNTHYBOY_MPC_OVERLAY 0
#endif
#if SYNTHYBOY_MPC_OVERLAY
#if !SYNTHYBOY_MPC_LFO_LIVE
#error "SYNTHYBOY_MPC_OVERLAY needs SYNTHYBOY_MPC_LFO_LIVE (the display thread reports the display parameter 'Overlay Instance' to the host)"
#endif
#include "overlay/service.h"
#include <exception>
#include "play_log.h"
#include "play_params.h"
#include "q_router.h"
#include "macro_names.h"
#include "q_step_rule.h"

namespace
{
using synthyboy_mpc::stepCountOf;   // the rule of the relative Q proxies: q_step_rule.h
} // namespace
#include "synthyboy/readout.h"
#endif

#ifndef SYNTHYBOY_MPC_OUTPUT_GAIN_DB
#define SYNTHYBOY_MPC_OUTPUT_GAIN_DB 18.0
#endif

/** The most a preset file may have (a preset is a few KB; the content folder is on user writable media). */
static constexpr size_t kMaxPresetFileBytes = 4u * 1024u * 1024u;

// ---------------------------------------------------------------------------
// Denormal flush helpers (cross-platform)
// ---------------------------------------------------------------------------

#if defined(__x86_64__) || defined(__i386__)
#include <pmmintrin.h>
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
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#else
    (void)0;
#endif
}

static inline uint32_t flushDenormalsSave() noexcept
{
#if defined(__x86_64__) || defined(__i386__)
    return _mm_getcsr();
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
    _mm_setcsr(saved);
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

#if SYNTHYBOY_MPC_OVERLAY
struct PluginInstance;
static void writeRealParam (PluginInstance* inst, size_t index, float parameter);
static std::string buildStateChunk (PluginInstance* inst);   // the chunk effGetChunk returns (the debug dump of play_log.h writes it too)
#endif

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
    // and saved again. Written by effSetProgram / effSetChunk and read by the program name calls and the chunk, which the host may make from different threads (review F13):
    // programMutex guards both, and is never taken by the audio thread (processReplacing, setParameter and getParameter do not look at them).
    mutable std::mutex programMutex;
    bool noProgram = false;
    std::string noProgramName;
    void setNoProgram (bool on, const std::string& name)
    {
        std::lock_guard<std::mutex> g (programMutex);
        noProgram = on;
        noProgramName = on ? name : std::string();
    }
#if SYNTHYBOY_MPC_OVERLAY
    // The host shows a mark ("*") after the program name when the sound is not the program any more, and drops it when it is told to read the program name again (audioMasterUpdateDisplay, which the Q
    // proxies need: docs/MPC.md "Q proxies"). So the plugin keeps the mark itself: programModified is set by a chunk restore or a parameter change, cleared by loading a program, and the name the
    // host reads carries " *" while it is set. programMarkDue: the display thread tells the host when it changed.
    std::atomic<bool> programModified { false }, programMarkDue { false };
    void setProgramModified (bool m) noexcept
    {
        if (programModified.exchange (m, std::memory_order_relaxed) != m)
            programMarkDue.store (true, std::memory_order_release);
    }
#endif

    // Chunk buffers: owned by the plugin instance, reused across effGetChunk calls. The host reads the chunk AFTER the call returned, and may ask from two threads (an autosave worker and the UI): every call fills
    // the next of kChunkBuffers buffers under chunkMutex, so the pointer one call returned stays valid while the next three calls are made (review F13). Never touched by the audio thread.
    static constexpr size_t kChunkBuffers = 4;
    std::mutex chunkMutex;
    std::vector<char> chunkBuffers[kChunkBuffers];
    size_t chunkNext = 0;

    // User content store (for resolving content references)
    std::unique_ptr<synthyboy::UserContentStore> contentStore;

    // Thread-safe parameter mirror (single source of truth for all threads)
    std::atomic<float> paramMirror[synthyboy::kMaxParams];
#if SYNTHYBOY_MPC_OVERLAY
    std::atomic<float> paramUndo[synthyboy::kMaxParams];   // the value a parameter had before the host's last change of it (NaN: none): see writeRealParam
#endif

    // Output stage: the make-up gain of the MPC shell (SYNTHYBOY_MPC_OUTPUT_GAIN_DB, set once in the constructor) and, only when built with SYNTHYBOY_MPC_OUTPUT_LIMITER, the peak limiter behind it.
    // Only the audio thread calls process(); prepare() runs with the engine's prepare (effMainsChanged, the host is not processing then).
    synthyboy_mpc::SwitchableOutputStage outputStage;
    // The output limiter as a plugin setting (Master page, OUTPUT block, "LIMIT"; saved in the chunk as "mpcOutputLimiter"; docs/MPC.md "Output limiter"). Not an engine parameter and not a host
    // parameter (the host's parameter count does not change). SYNTHYBOY_MPC_OUTPUT_LIMITER in the environment (or a build with it ON) forces the state for tests (output_stage.h).
    std::atomic<bool> outputLimiter { synthyboy_mpc::kOutputLimiterDefaultOn };
    void applyOutputLimiter() noexcept
    {
        const int forced = synthyboy_mpc::outputLimiterForced();
        outputStage.setLimiter (forced >= 0 ? forced == 1 : outputLimiter.load (std::memory_order_relaxed));
    }

    // ---- the life of the instance towards the host (robustness review F5, F12) ----
    // closing: effClose has begun. From that moment no thread of this instance calls the host (the gate below answers 0 at once), every loop of ours stops at its next look, and nothing new is started.
    // hostKnows: the host has called the dispatcher at least once, so it holds the AEffect pointer: a callback for an effect the host has not registered yet (before VSTPluginMain returned and the host
    // stored the pointer) never happens - the display thread waits for this flag before its first callback and keeps every report due.
    std::atomic<bool> closing { false };
    std::atomic<bool> hostKnows { false };

    /** The callback into the host, behind the two flags: what the display thread (and anyone) calls is `audioMaster (e, opcode, ...)` as before, but while the instance is closing or the host does not know it yet the call
        is not made and answers 0. The identification call of VSTPluginMain (audioMasterVersion) is made on the host's own thread before the pointer is returned: the only one that passes before hostKnows. */
    struct HostGate
    {
        audioMasterCallback raw = nullptr;
        const std::atomic<bool>* closingFlag = nullptr;
        const std::atomic<bool>* knowsFlag = nullptr;
        HostGate& operator= (audioMasterCallback cb) noexcept { raw = cb; return *this; }
        explicit operator bool() const noexcept { return raw != nullptr; }
        VstIntPtr operator() (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt) const
        {
            if (raw == nullptr)
                return 0;
            if (opcode != audioMasterVersion && (closingFlag->load (std::memory_order_acquire) || ! knowsFlag->load (std::memory_order_acquire)))
                return 0;
            return raw (e, opcode, index, value, ptr, opt);
        }
    };
    HostGate audioMaster { nullptr, &closing, &hostKnows };   // the host's callback (transport queries, automation reports)
#if SYNTHYBOY_MPC_CALLBACK_STATS
    audioMasterCallback hostCallback = nullptr; // the host's own callback; `audioMaster.raw` is the counting one in this build (see countingHostCallback)
#endif
    synthyboy::TransportInfo transport { 120.0, 0.0, false };

#if SYNTHYBOY_MPC_OVERLAY
    // What the overlay thread may ask of this instance: lock free reads of the parameter mirror and of the engine's readout.
    struct OverlayView final : synthyboy_mpc::overlay::IInstanceView
    {
        explicit OverlayView (PluginInstance& owner) : inst (owner) {}
        float param (int tableIndex) const noexcept override { return static_cast<unsigned> (tableIndex) < synthyboy::kMaxParams ? inst.paramMirror[tableIndex].load (std::memory_order_relaxed) : 0.0f; }
        bool live (synthyboy::LiveValues& out) const noexcept override
        {
            return inst.engine && inst.engine->copyReadout (out) && out.block != 0; // block 0: the engine wrote no values yet
        }
        double bpm() const noexcept override { return inst.overlayBpm.load (std::memory_order_relaxed); }
        int playSelection (int group) const noexcept override { return group >= 0 && group < synthyboy_mpc::play::kGroups ? inst.playSel[group].load (std::memory_order_relaxed) : 1; }
        void setQGroup (const int* targets, int n) noexcept override { inst.qRouter.setGroup (targets, n, static_cast<int> (synthyboy::paramCount())); }
        void writeParam (int tableIndex, float normalised) noexcept override { inst.uiWrite (tableIndex, normalised); }
        void setPlaySelection (int group, int instance) noexcept override { if (group >= 0 && group < synthyboy_mpc::play::kGroups) inst.playSetSelector (group, instance); }
        int playLastSelector() const noexcept override { return inst.playLastSel.load (std::memory_order_relaxed); }
        void setUiFullOpen (bool open) noexcept override
        {
            if (inst.uiFullOpen.exchange (open, std::memory_order_relaxed) != open)
                inst.overlayReportDue.store (true, std::memory_order_release);   // the display thread reports the frame at once
        }
        std::shared_ptr<const synthyboy::StateContent> content() const override { return std::atomic_load (&inst.overlayContent); }
        const synthyboy::IContentStore* contentStore() const noexcept override { return inst.contentStore.get(); }
        void contentRoots (std::vector<ContentRoot>& out) const override { inst.contentRootsOf (out); }
        void requestContent (int osc, int kind, const std::string& path) override { inst.requestContentLoad (osc, kind, path); }
        int contentLoad (std::string* message) const override { return inst.contentLoadState (message); }
        void setLfoShape (int lfo, const synthyboy::ContentLfoShape& shape) override { inst.requestLfoShape (lfo, shape); }
        void setMacroName (int macro, const std::string& name) override { inst.macroNameQueue.post (macro, name); }
        bool outputLimiter (bool& on) const noexcept override
        {
            on = inst.outputStage.limiter();   // what the stage runs (the setting, or the state the environment forces)
            return true;
        }
        void setOutputLimiter (bool on) noexcept override
        {
            inst.outputLimiter.store (on, std::memory_order_relaxed);
            inst.applyOutputLimiter();
        }
#if SYNTHYBOY_MPC_PAD_PRESSURE
        bool padPressure (bool& on, float& percent) const noexcept override
        {
            on = inst.padPressure.load (std::memory_order_relaxed);
            percent = inst.padPressureAmount.load (std::memory_order_relaxed);
            return true;
        }
        void setPadPressure (bool on, float percent) noexcept override
        {
            inst.padPressure.store (on, std::memory_order_relaxed);
            inst.padPressureAmount.store (std::min (std::max (percent, 0.0f), 100.0f), std::memory_order_relaxed);
            inst.updateEnginePadPressure();
        }
#endif
        void setQVirtual (int id, float normalised) noexcept override { inst.qRouter.setVirtualValue (id, normalised); }
        bool takeQVirtual (int id, float& normalised) noexcept override { return inst.qRouter.takeVirtual (id, normalised); }
        unsigned long qWriteCount() const noexcept override { return inst.qRouter.writeCount(); }
        void qStatus (std::string& out) const override { inst.qRouter.describe (out); }
        double sampleRate() const noexcept override { return inst.overlayRate.load (std::memory_order_relaxed); }
        void requestOverlayReport() noexcept override
        {
            inst.overlayAsked.store (true, std::memory_order_release);
            inst.overlayReportDue.store (true, std::memory_order_release);
        }
#if SYNTHYBOY_MPC_LFO_LIVE && SYNTHYBOY_MPC_SPECTRUM
        const synthyboy_mpc::SpectrumRing* spectrumRing() const noexcept override { return &inst.spectrumRing; }   // the audio thread writes it; the spectrum view of the overlay thread reads it
#endif
        PluginInstance& inst;
    };
    OverlayView overlayView { *this };
    std::atomic<double> overlayRate { 44100.0 };   // sampleRate for the overlay thread (sampleRate itself is the host thread's)
    std::atomic<double> overlayBpm { 120.0 };      // the host's tempo for the overlay thread (the synced LFO rates)
    std::shared_ptr<const synthyboy::StateContent> overlayContent;   // a copy of `content` for the overlay thread (std::atomic_load / atomic_store): custom LFO shapes, user wavetables
    void publishOverlayContent() { std::atomic_store (&overlayContent, std::make_shared<const synthyboy::StateContent> (content)); }

    // ---- the content the plugin-drawn pages load (the sample / instrument browser of the oscillator UI, overlay/content_browser.h): the overlay thread posts a request, the display thread loads it (reads the
    // file, decodes the header, puts it into the content store, sets the engine's content). `contentMutex` serialises every change of `content` (the host thread's setChunk, this load) and the chunk's copy of it.
    std::mutex contentMutex;
    mutable std::mutex contentReqMutex;
    bool contentReqPending = false;
    int contentReqOsc = 0, contentReqKind = 0;
    std::string contentReqPath, contentLoadMessage, contentDirPath;
    std::atomic<int> contentLoadStatus { 0 };                 // 0 none, 1 loading, 2 done, 3 failed
    std::atomic<long long> contentLoadFailedMs { 0 };         // when the last load failed (the failure is shown for a few seconds)
    std::set<std::string> contentPackRoots;                    // the directories added as pack roots (once each)

    /** The places the browser starts from: the SD card of the MPC, the plugin's own content folder, every USB drive that is mounted under /media (the internal partitions are not offered). Only those that exist. */
    void contentRootsOf (std::vector<synthyboy_mpc::overlay::IInstanceView::ContentRoot>& out) const
    {
        out.clear();
        auto isDir = [] (const std::string& path) { struct stat st; return ::stat (path.c_str(), &st) == 0 && S_ISDIR (st.st_mode); };
        auto add = [&] (const char* label, const std::string& path) { if (! path.empty() && isDir (path)) out.push_back ({ label, path }); };
        add ("SD CARD", "/sdcard");
        add ("PLUGIN CONTENT", contentDirPath);
        if (DIR* d = ::opendir ("/media"))
        {
            while (const dirent* e = ::readdir (d))
            {
                const std::string n = e->d_name;
                if (n.empty() || n[0] == '.' || n == "EFI" || n.compare (0, 5, "acvs-") == 0 || n.compare (0, 5, "az01-") == 0)
                    continue;
                {
                    std::string label = "USB ";
                    for (char ch : n)
                        label += static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));   // one way of writing the names of the roots: SD CARD, USB PENDRIVE, PLUGIN CONTENT
                    add (label.c_str(), "/media/" + n);
                }
            }
            ::closedir (d);
        }
    }
    void requestContentLoad (int osc, int kind, const std::string& path)
    {
        std::lock_guard<std::mutex> g (contentReqMutex);
        contentReqPending = true;
        contentReqOsc = osc;
        contentReqKind = kind;
        contentReqPath = path;
        contentLoadStatus.store (1, std::memory_order_relaxed);
    }
    int contentLoadState (std::string* message) const
    {
        int s = contentLoadStatus.load (std::memory_order_relaxed);
        if (s == 3 && steadyMs() - contentLoadFailedMs.load (std::memory_order_relaxed) > 4000)
            s = 0;   // a failure is said for four seconds
        if (message != nullptr && s == 3)
        {
            std::lock_guard<std::mutex> g (contentReqMutex);
            *message = contentLoadMessage;
        }
        return s;
    }
    // ---- the drawn shapes of the LFOs the plugin-drawn LFO page writes (overlay/lfo_ui.h): the overlay thread posts the shape (the latest per LFO wins), the display thread puts it into the content (under `contentMutex`
    // like every change of `content`), gives it to the engine and to the overlay's copy. A shape the content validation would refuse (it would make saveState drop the whole section) is not applied.
    std::mutex lfoShapeMutex;
    synthyboy::ContentLfoShape lfoShapeReq[synthyboy::kMaxContentLfoShapes];
    bool lfoShapeDue[synthyboy::kMaxContentLfoShapes] = {};
    void requestLfoShape (int lfo, const synthyboy::ContentLfoShape& shape)
    {
        if (lfo < 1 || lfo > synthyboy::kMaxContentLfoShapes)
            return;
        std::lock_guard<std::mutex> g (lfoShapeMutex);
        lfoShapeReq[lfo - 1] = shape;
        lfoShapeReq[lfo - 1].lfo = lfo;
        lfoShapeDue[lfo - 1] = true;
    }
    void lfoShapePoll()
    {
        synthyboy::ContentLfoShape take[synthyboy::kMaxContentLfoShapes];
        bool have[synthyboy::kMaxContentLfoShapes] = {};
        bool any = false;
        {
            std::lock_guard<std::mutex> g (lfoShapeMutex);
            for (int i = 0; i < synthyboy::kMaxContentLfoShapes; ++i)
                if (lfoShapeDue[i])
                {
                    take[i] = lfoShapeReq[i];
                    have[i] = any = true;
                    lfoShapeDue[i] = false;
                }
        }
        if (! any || ! engine)
            return;
        std::lock_guard<std::mutex> g (contentMutex);
        synthyboy::StateContent next = content;
        for (int i = 0; i < synthyboy::kMaxContentLfoShapes; ++i)
        {
            if (! have[i])
                continue;
            bool replaced = false;
            for (synthyboy::ContentLfoShape& s : next.lfoShapes)
                if (s.lfo == i + 1)
                {
                    s = take[i];
                    replaced = true;
                }
            if (! replaced)
                next.lfoShapes.push_back (take[i]);
        }
        if (! synthyboy::validateContent (next))
            return;
        content = std::move (next);
        engine->setContent (content);
        publishOverlayContent();
    }
    // ---- the names of the macros (docs/ui-design/macro-names.md): the Macros page posts a rename, the display thread puts it into the content (under `contentMutex`, the engine does not need it), publishes the overlay's
    // copy and tells the host the names of the Q proxies changed (audioMasterUpdateDisplay). A load of a program, preset or chunk that brings other names sets macroNamesDue in applyState: the host is told too.
    synthyboy_mpc::MacroNameQueue macroNameQueue;
    std::atomic<bool> macroNamesDue { false };
    /** Debug switch for the host tests (the pages are not in them): with SYNTHYBOY_MPC_MACRO_NAME_FILE set to a path, a line "n name" in that file (n = 1 .. 8, the rest of the line is the name) is a rename of macro n
        as the Macros page asks for it, applied when the line changes. Without the variable nothing is read. Display thread. */
    std::string macroNameFakeLast;
    void macroNameFakeFile()
    {
        static const std::string path = [] { const char* p = std::getenv ("SYNTHYBOY_MPC_MACRO_NAME_FILE"); return std::string (p != nullptr ? p : ""); }();
        if (path.empty())
            return;
        std::ifstream f (path);
        std::string line;
        if (! f || ! std::getline (f, line) || line == macroNameFakeLast)
            return;
        macroNameFakeLast = line;
        const int m = std::atoi (line.c_str());
        const size_t sp = line.find (' ');
        macroNameQueue.post (m - 1, sp == std::string::npos ? std::string() : line.substr (sp + 1));
    }
    void macroNamePoll (AEffect* e)
    {
        macroNameFakeFile();
        std::string names[synthyboy::kNumMacros];
        bool have[synthyboy::kNumMacros] = {};
        if (macroNameQueue.take (names, have))
        {
            std::lock_guard<std::mutex> g (contentMutex);
            bool changed = false;
            for (int m = 0; m < synthyboy::kNumMacros; ++m)
            {
                if (! have[m])
                    continue;
                const std::string clean = synthyboy::sanitizeMacroName (names[m], m);
                if (clean != content.macroNames[m])
                {
                    content.macroNames[m] = clean;
                    changed = true;
                }
            }
            if (changed)
            {
                publishOverlayContent();
                macroNamesDue.store (true, std::memory_order_release);
            }
        }
        if (macroNamesDue.exchange (false, std::memory_order_acq_rel) && audioMaster)
            audioMaster (e, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);   // the host reads the names of the Q proxies again
    }
    /** The display thread, every tick: a posted request is loaded now. */
    void contentPoll()
    {
        int osc = 0, kind = 0;
        std::string path;
        {
            std::lock_guard<std::mutex> g (contentReqMutex);
            if (! contentReqPending)
                return;
            contentReqPending = false;
            osc = contentReqOsc;
            kind = contentReqKind;
            path = contentReqPath;
        }
        std::string why;
        bool ok = false;
        try { ok = loadContentFile (osc, kind, path, why); }
        catch (...) { why = "unexpected error"; }
        std::lock_guard<std::mutex> g (contentReqMutex);
        contentLoadMessage = why;
        contentLoadFailedMs.store (steadyMs(), std::memory_order_relaxed);
        if (! contentReqPending || ! ok)   // a newer request is already waiting: its own state (loading) stays
            contentLoadStatus.store (ok ? 2 : 3, std::memory_order_relaxed);
    }
    /** A content file read whole (a sample, an SFZ): a regular file of 1 .. `limit` bytes, read in blocks that stop when the instance is closing (effClose does not wait for a 200 MB sample on a slow stick). */
    bool readWholeFile (const std::string& path, std::vector<uint8_t>& bytes, size_t limit) const
    {
        return synthyboy_mpc::safefile::readWholeBounded (path.c_str(), bytes, limit, &closing);
    }
    static std::string baseName (const std::string& path) { const size_t s = path.rfind ('/'); return s == std::string::npos ? path : path.substr (s + 1); }
    static std::string dirName (const std::string& path) { const size_t s = path.rfind ('/'); return s == std::string::npos ? std::string() : path.substr (0, s); }
    /** kind 1: a WAV file becomes the content sample of oscillator `osc` (copied into the content store, the root note from its name). kind 2: an SFZ file is referenced where it lies (its folder is a pack
        of the store: the samples it names are read from there); a WAV file becomes an instrument of one zone over the keyboard (stored with its own SFZ). The engine gets the new content; the chunk keeps the reference. */
    bool loadContentFile (int osc, int kind, const std::string& path, std::string& why)
    {
        if (osc < 1 || osc > synthyboy::kMaxContentSamples - 1 || (kind != 1 && kind != 2) || ! contentStore || ! engine)
        {
            why = "no content store";
            return false;
        }
        const std::string name = baseName (path);
        std::string lower = name;
        for (char& c : lower)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        const bool sfz = lower.size() > 4 && lower.compare (lower.size() - 4, 4, ".sfz") == 0;
        std::vector<uint8_t> bytes;
        if (! readWholeFile (path, bytes, sfz ? 4u * 1024u * 1024u : synthyboy::kMaxContentFileBytes))
        {
            why = "cannot read the file";
            return false;
        }
        std::lock_guard<std::mutex> g (contentMutex);
        synthyboy::StateContent next = content;
        synthyboy::ContentFileRef ref;
        int rootNote = -1;
        if (kind == 2 && sfz)
        {
            const std::string folder = dirName (path), pack = baseName (folder), root = dirName (folder);
            if (pack.empty() || root.empty() || pack.find (':') != std::string::npos || name.find (':') != std::string::npos)
            {
                why = "the SFZ file must lie in a folder of its own";
                return false;
            }
            if (contentPackRoots.insert (root).second)
                contentStore->addPackRoot (root);
            ref.pack = pack;
            ref.file = name;
            ref.name = synthyboy::fileStemOf (name);
            int zones = 0;
            const std::string sfzText (bytes.begin(), bytes.end());
            for (size_t at = sfzText.find ("<region>"); at != std::string::npos; at = sfzText.find ("<region>", at + 8))
                ++zones;
            synthyboy::attachInstrument (next, osc, ref, zones);
        }
        else
        {
            synthyboy::SampleFacts facts;
            if (! synthyboy::probeSampleWav (bytes, name, facts))
            {
                why = "not a WAV file the engine can read";
                return false;
            }
            rootNote = facts.rootNote;
            if (kind == 1)
            {
                if (! contentStore->addFile (synthyboy::ContentKind::sample, name, bytes, ref))
                {
                    why = "could not store the sample";
                    return false;
                }
                synthyboy::attachSample (next, osc, ref, facts.sampleRate, static_cast<int> (std::min<int64_t> (facts.frames, 2000000000)), facts.channels);
            }
            else
            {
                const int key = rootNote >= 0 ? rootNote : 60;
                const std::string stem = synthyboy::sanitizeFileStem (synthyboy::fileStemOf (name));
                const std::string text = "<region> sample=" + stem + ".wav lokey=0 hikey=127 pitch_keycenter=" + std::to_string (key) + " loop_mode=no_loop\n";
                if (! contentStore->addInstrument (stem, text, { { stem + ".wav", bytes } }, ref))
                {
                    why = "could not store the instrument";
                    return false;
                }
                synthyboy::attachInstrument (next, osc, ref, 1);
            }
        }
        content = std::move (next);
        engine->setContent (content);
        publishOverlayContent();
        if (rootNote >= 0 && kind == 1)
            if (const synthyboy::ParamInfo* p = synthyboy::findParamByKey (("osc" + std::to_string (osc) + ".sample.root").c_str()))
                uiWrite (static_cast<int> (p - synthyboy::paramTable()), synthyboy::toNormalised (*p, synthyboy::constrainValue (*p, static_cast<float> (rootNote))));
        return true;
    }
    bool overlayAttached = false;

    // ---- the Play page (play_params.h, docs/MPC.md "Play page"): three selectors (saved in the chunk) and 24 proxy parameters (the real parameter of the selected instance) ----
    std::atomic<int> playSel[synthyboy_mpc::play::kGroups] { { 1 }, { 1 }, { 1 } };
    std::atomic<bool> playGroupDue[synthyboy_mpc::play::kGroups] { { false }, { false }, { false } };   // a selector changed (host, chunk): the display thread reports the group's proxies once
    std::atomic<bool> playRealDue[synthyboy_mpc::play::kProxies] {};                                    // a proxy was written: the display thread tells the host the real parameter changed too
    std::atomic<float> playLast[synthyboy_mpc::play::kProxies] {};                                      // the value of every proxy as the host last heard it (a host write or a report)
    bool playLastValid = false;                                                                         // display thread only

    // The restore window (docs/MPC.md "Project save and reload"): from a successful effSetChunk until the first host write to a real parameter that is not an echo, a program that is really loaded (the protected re-selection of the saved program does not end it), or kRestoreWindowMs. The host may
    // write every parameter back after it restored the chunk (values the plugin reported when the project was saved): in the window a write to a real parameter that equals what the plugin reports
    // is an echo (changes nothing, disarms nothing, even when the float round trip differs in the last bit), and a write to a selector or a Play proxy is ignored (the chunk is the authority for
    // the selectors, and a proxy write could only land on an instance other than the one it was saved for).
    static constexpr long long kRestoreWindowMs = 2000;
    static constexpr float kEchoTolerance = 1.0e-6f;
    std::atomic<long long> restoreSinceMs { -1 };
    static long long steadyMs() noexcept { return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
    void restoreOpen() noexcept { restoreSinceMs.store (steadyMs(), std::memory_order_relaxed); }
    void restoreClose() noexcept { restoreSinceMs.store (-1, std::memory_order_relaxed); }
    bool restoreActive() noexcept
    {
        const long long since = restoreSinceMs.load (std::memory_order_relaxed);
        if (since < 0)
            return false;
        if (steadyMs() - since > kRestoreWindowMs)
        {
            restoreClose();
            return false;
        }
        return true;
    }

#if SYNTHYBOY_MPC_DEBUG_HOOKS
    /** The debug dump (play_log.h): the state of this instance, written when the request file exists. Display thread. The text is built in memory and written in one go (safe_file.h). */
    void playDump()
    {
        namespace pl = synthyboy_mpc::play;
        std::string text;
        char line[160];
        std::snprintf (line, sizeof line, "selectors %d %d %d\nprogram %d protected %d window %d\n", playSel[0].load(), playSel[1].load(), playSel[2].load(), currentProgram.load(), protectedProgram.load(), restoreSinceMs.load() >= 0 ? 1 : 0);
        text += line;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        {
            std::snprintf (line, sizeof line, "p %zu %s %.9g\n", i, synthyboy::paramTable()[i].key, static_cast<double> (paramMirror[i].load (std::memory_order_relaxed)));
            text += line;
        }
        for (int p = 0; p < pl::kProxies; ++p)
        {
            std::snprintf (line, sizeof line, "proxy %d %.9g\n", p, static_cast<double> (playProxyValue (p)));
            text += line;
        }
        const std::string chunk = buildStateChunk (this);
        std::snprintf (line, sizeof line, "chunk %zu\n", chunk.size());
        text += line;
        text += chunk;
        text += '\n';
        synthyboy_mpc::safefile::writeSmall (synthyboy_mpc::PlayLog::dumpPath().c_str(), text.data(), text.size());
    }
#endif

    // ---- the writes of the plugin-drawn pages (overlay/osc_ui.h): the same write as the host's own, then the display thread tells the host (audioMasterAutomate), so automation recording and the host's
    // own widgets follow. The overlay thread sets the dirty bit, the display thread reports it (one value per parameter, the latest).
    std::atomic<int> playLastSel { 0 };                          // 0 none, 1 envelope, 2 LFO: which of the two Play selectors was set last (the modulation source the UI calls "selected")
    std::atomic<uint64_t> uiDirty[synthyboy::kMaxParams / 64] {};
    void uiWrite (int tableIndex, float normalised) noexcept
    {
        if (tableIndex < 0 || tableIndex >= static_cast<int> (synthyboy::paramCount()))
            return;
        writeRealParam (this, static_cast<size_t> (tableIndex), normalised);
        uiDirty[tableIndex >> 6].fetch_or (uint64_t { 1 } << (tableIndex & 63), std::memory_order_release);
    }
#if SYNTHYBOY_MPC_DEBUG_HOOKS
    /** Debug / acceptance hook (SYNTHYBOY_MPC_DEBUG_HOOKS only): while the file /tmp/synthyboy-ui-fake-write exists (SYNTHYBOY_MPC_UI_FAKE_WRITE changes the path) its first line "key plainvalue" is written through uiWrite, the path the
        plugin-drawn pages use (so the host's reaction can be looked at while ANOTHER page is on screen); the file is deleted. Display thread. Read with safe_file.h (a FIFO, a symlink or a huge file is ignored). */
    void uiFakeWrite()
    {
        static const std::string path = [] { const char* p = std::getenv ("SYNTHYBOY_MPC_UI_FAKE_WRITE"); return std::string (p != nullptr && *p != '\0' ? p : "/tmp/synthyboy-ui-fake-write"); }();
        char text[synthyboy_mpc::safefile::kSwitchBytes];
        if (synthyboy_mpc::safefile::readSmall (path.c_str(), text, sizeof text) <= 0)
            return;
        char key[96] = {};
        double v = 0.0;
        if (std::sscanf (text, "%95s %lf", key, &v) != 2)
            return;
        ::unlink (path.c_str());
        if (const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key))
            uiWrite (static_cast<int> (p - synthyboy::paramTable()), synthyboy::toNormalised (*p, synthyboy::constrainValue (*p, static_cast<float> (v))));
    }
#else
    void uiFakeWrite() noexcept {}
#endif
    void uiPoll (AEffect* e)
    {
        uiFakeWrite();
        if (! audioMaster)
            return;
        for (size_t w = 0; w < synthyboy::kMaxParams / 64; ++w)
        {
            if (uiDirty[w].load (std::memory_order_relaxed) == 0)
                continue;
            uint64_t bits = uiDirty[w].exchange (0, std::memory_order_acq_rel);
            while (bits != 0)
            {
                const int b = __builtin_ctzll (bits);
                bits &= bits - 1;
                const int idx = static_cast<int> (w) * 64 + b;
                audioMaster (e, audioMasterAutomate, idx, 0, nullptr, qReadNorm (idx));
            }
        }
    }

    // ---- the Q proxies (q_router.h, docs/MPC.md "Q proxies"): "Q 1" .. "Q 4" after the last Play proxy; what the four knobs of the plugin's own full-screen pages are mapped to ----
    synthyboy_mpc::QRouter qRouter;
    static VstInt32 qBase() noexcept { return playBase() + synthyboy_mpc::play::kParams; }
    static bool isQParam (VstInt32 index) noexcept { return index >= qBase() && index < qBase() + synthyboy_mpc::QRouter::kCount; }
    /** The normalised value of a real parameter (table index) as the host reads it. */
    float qReadNorm (int tableIndex) const noexcept { return synthyboy::toNormalised (synthyboy::paramTable()[tableIndex], paramMirror[tableIndex].load (std::memory_order_relaxed)); }
#if SYNTHYBOY_MPC_DEBUG_HOOKS
    /** Debug / acceptance hook (SYNTHYBOY_MPC_DEBUG_HOOKS only): while the file /tmp/synthyboy-q-fake-group exists (SYNTHYBOY_MPC_Q_FAKE_FILE changes the path) its first line, up to sixteen table indices (-1: none), is the Q group of this
        instance; applied when the line changes (the host tests of the Q proxies have no UI to set it). Display thread. Read with safe_file.h: at most 256 bytes of it, only a regular file. */
    std::string qFakeLast;
    void qFakeGroup()
    {
        static const std::string path = [] { const char* e = std::getenv ("SYNTHYBOY_MPC_Q_FAKE_FILE"); return std::string (e != nullptr && *e != '\0' ? e : "/tmp/synthyboy-q-fake-group"); }();
        char text[synthyboy_mpc::safefile::kSwitchBytes];
        if (! synthyboy_mpc::safefile::readFirstLine (path.c_str(), text, sizeof text))
        {
            qFakeLast.clear();
            return;
        }
        const std::string line = text;
        if (line == qFakeLast)
            return;
        qFakeLast = line;
        int targets[synthyboy_mpc::QRouter::kCount];
        int n = 0;
        for (size_t at = 0; n < synthyboy_mpc::QRouter::kCount;)
        {
            while (at < line.size() && line[at] == ' ')
                ++at;
            if (at >= line.size())
                break;
            char* end = nullptr;
            targets[n++] = static_cast<int> (std::strtol (line.c_str() + at, &end, 10));
            const size_t next = static_cast<size_t> (end - line.c_str());
            if (next <= at)
                break;
            at = next;
        }
        qRouter.setGroup (targets, n, static_cast<int> (synthyboy::paramCount()));
    }
    /** Debug / acceptance hook (SYNTHYBOY_MPC_DEBUG_HOOKS only): while the file /tmp/synthyboy-q-fake-write exists (SYNTHYBOY_MPC_Q_FAKE_WRITE changes the path) its first line "q value" (q 1 .. 16, value 0 .. 1) is written to the Q
        proxy through setParameter, as the host does when a Q-Link turns; the file is deleted. There is no way to turn a Q-Link from outside: this proves the path from the Q proxy to the target on
        the device (not the host's mapping of the knob to the proxy). Display thread. */
    void qFakeWrite (AEffect* e)
    {
        static const std::string path = [] { const char* p = std::getenv ("SYNTHYBOY_MPC_Q_FAKE_WRITE"); return std::string (p != nullptr && *p != '\0' ? p : "/tmp/synthyboy-q-fake-write"); }();
        double qd = 0.0, v = 0.0;
        if (! synthyboy_mpc::safefile::readTwoNumbers (path.c_str(), qd, v))
            return;
        ::unlink (path.c_str());
        const int q = static_cast<int> (qd);
        if (q >= 1 && q <= synthyboy_mpc::QRouter::kCount && e != nullptr && e->setParameter != nullptr)
            e->setParameter (e, qBase() + q - 1, static_cast<float> (v));
    }
    /** Debug switch (SYNTHYBOY_MPC_DEBUG_HOOKS only): while the file /tmp/synthyboy-overlay-q-step-travel exists its first number is the travel of a Q proxy per step of a stepped target, in 1/128 host increments (default 6). Display thread. */
    void qStepTravelFile()
    {
        const long long nowMs = steadyMs();   // once a second
        if (nowMs - qStepFileAtMs < 1000)
            return;
        qStepFileAtMs = nowMs;
        double n = 0.0;
        qRouter.setStepTravel (synthyboy_mpc::safefile::readNumber ("/tmp/synthyboy-overlay-q-step-travel", n) && n > 0.0 && n < 1.0e6 ? static_cast<float> (n / 128.0) : synthyboy_mpc::QRouter::kStepTravelDefault);
    }
#else
    void qFakeGroup() noexcept {}
    void qFakeWrite (AEffect*) noexcept {}
    void qStepTravelFile() noexcept {}
#endif
    /** The display thread, every tick: the host is told the Q values after a group change or a change of a target, and the real parameter a Q write changed. */
    void qPoll (AEffect* e)
    {
        qStepTravelFile();
        qFakeGroup();
        qFakeWrite (e);
        if (! audioMaster)
            return;
        if (programMarkDue.exchange (false, std::memory_order_acq_rel))
            audioMaster (e, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);   // the program name has or has no mark now
        const VstInt32 base = qBase();
        qRouter.poll ([this] (int t) noexcept { return qReadNorm (t); },
                      [&] (int q, float v) { audioMaster (e, audioMasterAutomate, base + q, 0, nullptr, v); },
                      [&] (int real) { audioMaster (e, audioMasterAutomate, real, 0, nullptr, qReadNorm (real)); },
                      [&] { qNotifyGroup (e); });
    }
    /** The group changed: the names, units and value texts of the Q proxies are other now. The host keeps what it read for good unless it is told (measured on the Key 61, docs/MPC.md "Q proxies and the
        plugin-drawn pages"): audioMasterUpdateDisplay makes it read them again, audioMasterIOChanged does not. */
    void qNotifyGroup (AEffect* e)
    {
        audioMaster (e, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);
    }

    static VstInt32 playBase() noexcept { return overlayParamIndex() + 1; }   // "Play Osc"; "Play Env" and "Play LFO" follow, then the proxies
    static bool isPlayParam (VstInt32 index) noexcept { return index >= playBase() && index < playBase() + synthyboy_mpc::play::kParams; }
    static bool isPlaySelector (VstInt32 index) noexcept { return index >= playBase() && index < playBase() + synthyboy_mpc::play::kSelectors; }
    static int playProxyOf (VstInt32 index) noexcept { return index - playBase() - synthyboy_mpc::play::kSelectors; }   // 0 .. 23
    static int playGroupOfProxy (int proxy) noexcept { return proxy / synthyboy_mpc::play::kSlots; }
    static int playSlotOfProxy (int proxy) noexcept { return proxy % synthyboy_mpc::play::kSlots; }
    /** Table index of the real parameter behind proxy p for the instance that is selected now. */
    int playRealOf (int proxy) const noexcept
    {
        const int g = playGroupOfProxy (proxy);
        return synthyboy_mpc::play::realIndex (g, playSel[g].load (std::memory_order_relaxed), playSlotOfProxy (proxy));
    }
    /** The value the host reads for proxy p: the normalised value of the selected instance's parameter. */
    float playProxyValue (int proxy) const noexcept
    {
        const int real = playRealOf (proxy);
        if (real < 0)
            return 0.0f;
        return synthyboy::toNormalised (synthyboy::paramTable()[real], paramMirror[real].load (std::memory_order_relaxed));
    }
    /** Sets a selector (any thread; the value is limited to the instances the engine has) and has the display thread report the group. */
    void playSetSelector (int group, int instance) noexcept
    {
        instance = std::min (std::max (instance, 1), synthyboy_mpc::play::kInstances[group]);
        if (group == 1 || group == 2)
            playLastSel.store (group, std::memory_order_relaxed);
        if (playSel[group].exchange (instance, std::memory_order_relaxed) != instance)
            playGroupDue[group].store (true, std::memory_order_release);
    }
    /** The display thread, every tick: the proxies follow their real parameters (a preset, the parameter's own tab, automation, a selector change) and the host is told. A selector change
        re-reports ALL proxies of its group (and the selector) once; a proxy the host wrote makes the real parameter reported too. A value is reported only when it differs from what the host last
        heard (so the host's own writes and its echoes are not reported back: no feedback). */
    void playPoll (AEffect* e, VstInt32 firstParam)
    {
        namespace pl = synthyboy_mpc::play;
        if (! audioMaster)
            return;
        const VstInt32 base = firstParam;
#if SYNTHYBOY_MPC_DEBUG_HOOKS
        synthyboy_mpc::PlayLog::refresh();
        synthyboy_mpc::PlayLog::drain();   // the lines the host's threads put into the ring, written here: the only place the play log touches a file
        if (synthyboy_mpc::PlayLog::dumpRequested())
            playDump();
#endif
        if (! playLastValid)
        {
            for (int p = 0; p < pl::kProxies; ++p)
                playLast[p].store (playProxyValue (p), std::memory_order_relaxed);
            playLastValid = true;
        }
        for (int g = 0; g < pl::kGroups; ++g)
            if (playGroupDue[g].exchange (false, std::memory_order_acq_rel))
            {
                audioMaster (e, audioMasterAutomate, base + g, 0, nullptr, pl::selectorNormalised (g, playSel[g].load (std::memory_order_relaxed)));
                for (int s = 0; s < pl::kSlots; ++s)
                {
                    const int p = g * pl::kSlots + s;
                    const float v = playProxyValue (p);
                    playLast[p].store (v, std::memory_order_relaxed);
                    audioMaster (e, audioMasterAutomate, base + pl::kSelectors + p, 0, nullptr, v);
                }
            }
        for (int p = 0; p < pl::kProxies; ++p)
        {
            const float v = playProxyValue (p);
            if (std::fabs (v - playLast[p].load (std::memory_order_relaxed)) > 1.0e-6f)
            {
                playLast[p].store (v, std::memory_order_relaxed);
                audioMaster (e, audioMasterAutomate, base + pl::kSelectors + p, 0, nullptr, v);
            }
            if (playRealDue[p].exchange (false, std::memory_order_acq_rel))
            {
                const int real = playRealOf (p);
                if (real >= 0)
                    audioMaster (e, audioMasterAutomate, real, 0, nullptr, synthyboy::toNormalised (synthyboy::paramTable()[real], paramMirror[real].load (std::memory_order_relaxed)));
            }
        }
    }

    // The display-only parameter "Overlay Instance" (the last parameter of the list; not automatable, not in the chunk): the skin shows it as a film strip colour, so the overlay knows
    // WHICH instance's page is on screen (overlay/instance_slots.h). The value is the frame of this instance's slot / 32; the host reads it with getParameter and redraws the picture
    // when the display thread reports it (audioMasterAutomate): once after creation, when the value changes and whenever the overlay thread asks (requestOverlayReport).
    std::atomic<int> overlaySlot { -1 };                       // the slot the overlay service gave this instance (-1: none, the instance never shows the overlay)
    std::atomic<float> overlayValue { synthyboy_mpc::overlay::instanceParamValue (-1) };   // what getParameter says: slot, or what the debug file asks for
    std::atomic<bool> uiFullOpen { false };                    // a plugin-drawn full screen page is open for this instance: the frame the skin hides the host widgets of the Play page at
    std::atomic<bool> overlayReportDue { false };              // set by any thread; the display thread makes the host call
    std::atomic<bool> overlayAsked { false };                  // set by the overlay thread only (requestOverlayReport): the debug mode "stale" ends with it
    bool overlayStaleReleased = false;                         // display thread only
    std::chrono::steady_clock::time_point overlayNextFileCheck {};   // display thread only
    long long qStepFileAtMs = -100000;   // display thread only: when the Q step travel file was read last
    static constexpr const char* kOverlayFakeSlotFile = "/tmp/synthyboy-overlay-fake-slot";

    /** The value for the real slot, or (SYNTHYBOY_MPC_DEBUG_HOOKS only) what the debug file says (a number 0 .. 15 = report that slot, any other number or "none" = the none colour, "blank" = frame 0, "stale" = frame 0 until the
        overlay thread asks for a report and the real slot after that: what a host that shows an old picture until the parameter is reported looks like). Display thread and startOverlay. */
    float overlayWantedValue (bool asked = false)
    {
        const float real = uiFullOpen.load (std::memory_order_relaxed) ? synthyboy_mpc::overlay::fullScreenParamValue() : synthyboy_mpc::overlay::instanceParamValue (overlaySlot.load (std::memory_order_relaxed));
        bool stale = false;
#if SYNTHYBOY_MPC_DEBUG_HOOKS
        char text[32] = {};
        if (synthyboy_mpc::safefile::readSmall (kOverlayFakeSlotFile, text, sizeof text) >= 0)   // a regular file only, 31 bytes at most (safe_file.h): a FIFO there is ignored, not waited for
        {
            if (std::strncmp (text, "blank", 5) == 0)
                return 0.0f;
            if (std::strncmp (text, "none", 4) == 0)
                return synthyboy_mpc::overlay::instanceParamValue (-1);
            if (std::strncmp (text, "stale", 5) == 0)
                stale = true;
            else
            {
                char* end = nullptr;
                const long v = std::strtol (text, &end, 10);
                if (end != text)
                    return synthyboy_mpc::overlay::instanceParamValue (static_cast<int> (v));
            }
        }
#endif
        if (! stale)
        {
            overlayStaleReleased = false;
            return real;
        }
        overlayStaleReleased = overlayStaleReleased || asked;
        return overlayStaleReleased ? real : 0.0f;
    }
    /** The display thread, every tick: a due report is made with the value the slot (or the debug file) wants now; the debug file is looked at once a second as well (a changed value is
        reported). The value is read AFTER the flag is taken, so a slot that startOverlay stored before it set the flag is never missed. */
    void overlayPoll (AEffect* e, int paramIndex)
    {
        const auto now = std::chrono::steady_clock::now();
        bool report = overlayReportDue.exchange (false, std::memory_order_acq_rel);
        const bool asked = overlayAsked.exchange (false, std::memory_order_acq_rel);
        if (report || now >= overlayNextFileCheck)
        {
            if (now >= overlayNextFileCheck)
                overlayNextFileCheck = now + std::chrono::seconds (1);
            const float v = overlayWantedValue (asked);
            if (v != overlayValue.load (std::memory_order_relaxed))
            {
                overlayValue.store (v, std::memory_order_relaxed);
                report = true;
            }
        }
        if (report && audioMaster)
            audioMaster (e, audioMasterAutomate, paramIndex, 0, nullptr, overlayValue.load (std::memory_order_relaxed));
    }

    /** The knobs the views show live (the filters' cutoff and resonance, the oscillators' frame and warp): the audio thread takes the list over at the end of its next block. Building the list constructs every
        page once (24 scenes): the display thread does it at its start, not the host's thread in VSTPluginMain. */
    void publishReadoutTargets()
    {
        if (! engine)
            return;
        synthyboy_mpc::StartupDetailScope t (synthyboy_mpc::StartupTimes::kOverlayReadout);
        std::vector<int> idx;
        synthyboy_mpc::overlay::Controller::readoutTargets (idx);
        engine->setReadoutTargets (idx.data(), static_cast<int> (idx.size()));
    }

    void startOverlay()
    {
        if (overlayAttached)
            return;
        if (engine)
        {
#if ! SYNTHYBOY_MPC_LFO_LIVE
            publishReadoutTargets();   // no display thread in this build: here
#endif
            publishOverlayContent();
        }
        const int slot = synthyboy_mpc::overlay::Service::instance().attach (&overlayView);   // the slot stays this instance's until effClose; -1: more than 16 instances
        overlaySlot.store (slot, std::memory_order_relaxed);
        overlayValue.store (synthyboy_mpc::overlay::instanceParamValue (slot), std::memory_order_relaxed);   // (the debug file is the display thread's business)
        overlayReportDue.store (true, std::memory_order_release);   // the first report: the display thread makes it
        overlayAttached = true;
    }
    /** Set when the overlay thread did not let go of this instance's view in time (Service::detach): the instance must then never be freed. */
    std::atomic<bool> overlayStuck { false };
    void stopOverlay() noexcept
    {
        if (! overlayAttached)
            return;
        overlayAttached = false;
        try
        {
            if (! synthyboy_mpc::overlay::Service::instance().detach (&overlayView))
                overlayStuck.store (true, std::memory_order_release);
        }
        catch (...) { overlayStuck.store (true, std::memory_order_release); }
    }
#endif

#if SYNTHYBOY_MPC_LFO_LIVE
    std::atomic<float> liveValue[synthyboy_mpc::kLfoLiveCount] { { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f } };
    std::atomic<bool> liveStop { false };
    std::thread liveThread;
    /** What the display thread tells whoever waits for it at effClose (shared, so that it outlives the instance when the thread is abandoned). */
    struct LiveSync { std::mutex m; std::condition_variable cv; bool done = false; };
    std::shared_ptr<LiveSync> liveSync = std::make_shared<LiveSync>();
    /** 0 the instance is deleted by effClose; 1 effClose gave up waiting for the display thread and left the instance to it (reapAfterThread); 2 the display thread has ended and found it so. */
    std::atomic<int> reapState { 0 };
    static constexpr int kLiveJoinMs = 1500;   // how long effClose waits for the display thread (a host call, a sample read: both stop at the closing flag)
#if SYNTHYBOY_MPC_SPECTRUM
    synthyboy_mpc::SpectrumRing spectrumRing;                                  // written by the audio thread, read by the display thread
    std::atomic<double> spectrumRate { 44100.0 };                              // the sample rate for the display thread (sampleRate itself is the host thread's)
    std::atomic<float> spectrumValue[synthyboy_mpc::kSpectrumCount] {};        // the quantised column values, 0 .. 1
    static constexpr int kLiveParams = synthyboy_mpc::kLfoLiveCount + synthyboy_mpc::kSpectrumCount;
#else
    static constexpr int kLiveParams = synthyboy_mpc::kLfoLiveCount;
#endif

    /** Display parameter n (0 .. kLiveParams - 1; the LFO cursors first, then the spectrum columns): the value the host reads. */
    float liveRead (int n) const noexcept
    {
#if SYNTHYBOY_MPC_SPECTRUM
        if (n >= synthyboy_mpc::kLfoLiveCount)
            return spectrumValue[n - synthyboy_mpc::kLfoLiveCount].load (std::memory_order_relaxed);
#endif
        return liveValue[n].load (std::memory_order_relaxed);
    }

    /** Steps of display parameter n: 32 for the LFO cursors, 64 for the spectrum columns. */
    static constexpr int liveSteps (int n) noexcept
    {
#if SYNTHYBOY_MPC_SPECTRUM
        return n < synthyboy_mpc::kLfoLiveCount ? synthyboy_mpc::kLfoLiveSteps : synthyboy_mpc::kSpectrumSteps;
#else
        (void) n;
        return synthyboy_mpc::kLfoLiveSteps;
#endif
    }

    bool isLive (VstInt32 index) const noexcept
    {
        const auto count = static_cast<VstInt32> (synthyboy::paramCount());
        return index >= count && index < count + kLiveParams;
    }
#if SYNTHYBOY_MPC_OVERLAY
    /** The last display parameter, "Overlay Instance" (only in a build with the overlay). */
    static bool isOverlayParam (VstInt32 index) noexcept { return index == overlayParamIndex(); }
    /** Parameter order of the shell (docs/MPC.md "Parameter order"): engine parameters, 4 "LFO n Cursor", 32 "Spectrum n", "Pad Pressure", "Pad Pressure Amount", and LAST (this build only)
        "Overlay Instance". The indexes of the builds without the overlay never move: the overlay parameter is after everything. */
    static VstInt32 overlayParamIndex() noexcept
    {
        return static_cast<VstInt32> (synthyboy::paramCount()) + kLiveParams
#if SYNTHYBOY_MPC_PAD_PRESSURE
             + synthyboy_mpc::kPadPressureParams
#endif
            ;
    }
#endif

    /** The display thread's whole life (called by startLive's thread). Every tick of every feature runs inside a PollGuard (poll_guard.h): an exception is caught, counted, written to the status file and, five times in a row, switches that
        feature off - it never leaves the thread (std::terminate would end the MPC application). */
    void liveLoop (AEffect* e)
    {
        {
            // Own scheduling, set here (not inherited): normal policy, nice 0, all CPUs. Not lower than normal: this thread holds host locks while it is inside
            // audioMasterAutomate, and a starved holder would make the host's own threads wait for it.
            synthyboy_mpc::useNormalScheduling (0, "sb-lfo-display");
            const int first = static_cast<int> (synthyboy::paramCount());
            int syncIdx[synthyboy_mpc::kLfoLiveCount], phaseIdx[synthyboy_mpc::kLfoLiveCount];
            for (int l = 0; l < synthyboy_mpc::kLfoLiveCount; ++l)
            {
                const std::string n = std::to_string (l + 1);
                const synthyboy::ParamInfo* sync = synthyboy::findParamByKey (("lfo" + n + ".sync_type").c_str());
                const synthyboy::ParamInfo* phase = synthyboy::findParamByKey (("lfo" + n + ".phase").c_str());
                syncIdx[l] = sync ? synthyboy::findParamIndex (sync->id) : -1;
                phaseIdx[l] = phase ? synthyboy::findParamIndex (phase->id) : -1;
            }
            const auto active = std::chrono::milliseconds (1000 / (SYNTHYBOY_MPC_LFO_LIVE_HZ > 0 ? SYNTHYBOY_MPC_LFO_LIVE_HZ : 1));
            const auto idle = std::chrono::milliseconds (100); // nothing sounds: a look ten times a second, no call to the host
            const auto start = std::chrono::steady_clock::now();
            auto next = start;
            int last[synthyboy_mpc::kLfoLiveCount] = {};
            bool anyActive = false;
            synthyboy_mpc::PollGuard gLive ("LFO cursors", this), gSpectrum ("spectrum", this), gOverlay ("overlay report", this), gPlay ("Play proxies", this), gQ ("Q proxies", this), gUi ("page writes", this),
                                     gContent ("content loading", this), gShape ("LFO shape", this), gMacroName ("macro name", this);
#if SYNTHYBOY_MPC_SPECTRUM
            synthyboy_mpc::SpectrumAnalyser analyser;   // allocates here, once, on this thread
            float specLast[synthyboy_mpc::kSpectrumCount] = {};
#endif
#if SYNTHYBOY_MPC_OVERLAY
            bool readoutPublished = false;
#endif
            while (!liveStop.load (std::memory_order_relaxed))
            {
#if SYNTHYBOY_MPC_SPECTRUM
                next += SYNTHYBOY_MPC_LFO_LIVE_SELFTEST || SYNTHYBOY_MPC_SPECTRUM_SELFTEST || anyActive ? active : idle;
#else
                next += SYNTHYBOY_MPC_LFO_LIVE_SELFTEST || anyActive ? active : idle;
#endif
                const auto now = std::chrono::steady_clock::now();
                if (next < now)
                    next = now; // a tick that was late (the host call blocked): no catch-up burst of calls, one tick now and the grid goes on from here
                std::this_thread::sleep_until (next);
                if (liveStop.load (std::memory_order_relaxed) || closing.load (std::memory_order_acquire))
                    break;
                if (! hostKnows.load (std::memory_order_acquire))
                    continue;   // the host has not called us yet, so it may not hold this effect's pointer: no callback before it does, and no report is taken out of its flag meanwhile (robustness review F12)
#if SYNTHYBOY_MPC_OVERLAY
                gOverlay.run ([&] { injectFault ("overlay"); overlayPoll (e, PluginInstance::overlayParamIndex()); });   // "Overlay Instance": the first parameter of the overlay block (after the spectrum columns and the pad pressure parameters)
                gPlay.run ([&] { injectFault ("play"); playPoll (e, PluginInstance::playBase()); });                     // the Play page: the selectors and the proxies after it
                gQ.run ([&] { injectFault ("q"); qPoll (e); });                                                          // the Q proxies after them
                gUi.run ([&] { injectFault ("ui"); uiPoll (e); });                                                       // what the plugin-drawn pages wrote
                gContent.run ([&] { injectFault ("content"); contentPoll(); });                                          // a sample or an instrument the browser asked for
                gShape.run ([&] { injectFault ("shape"); lfoShapePoll(); });                                             // a drawn LFO shape the LFO page wrote
                gMacroName.run ([&] { injectFault ("macro"); macroNamePoll (e); });                                      // a name the Macros page gave a macro
                if (! readoutPublished)   // once, after the first report of the overlay slot above (see publishReadoutTargets)
                {
                    readoutPublished = true;
                    try { publishReadoutTargets(); } catch (...) {}
                }
#endif
                gLive.run ([&]
                {
                injectFault ("lfo");
                int step[synthyboy_mpc::kLfoLiveCount] = {};
#if SYNTHYBOY_MPC_LFO_LIVE_SELFTEST
                const double t = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
                for (int l = 0; l < synthyboy_mpc::kLfoLiveCount; ++l)
                {
                    const double cycles = t * 0.25 * static_cast<double> (1 << l);
                    step[l] = 1 + static_cast<int> ((cycles - std::floor (cycles)) * synthyboy_mpc::kLfoLiveSteps);
                }
#else
                synthyboy::LiveValues live;
                if (engine && engine->copyReadout (live) && live.block != 0) // block 0: the engine wrote no values yet (they read 0, not -1)
                    for (int l = 0; l < synthyboy_mpc::kLfoLiveCount; ++l)
                    {
                        const int sync = syncIdx[l] >= 0 ? static_cast<int> (paramMirror[syncIdx[l]].load (std::memory_order_relaxed) + 0.5f) : 0;
                        const float phase = phaseIdx[l] >= 0 ? paramMirror[phaseIdx[l]].load (std::memory_order_relaxed) : 0.0f;
                        step[l] = synthyboy_mpc::lfoLiveStep (live.lfoPhase[l], sync, phase);
                    }
#endif
                anyActive = false;
                for (int l = 0; l < synthyboy_mpc::kLfoLiveCount; ++l)
                {
                    anyActive = anyActive || step[l] != 0;
                    if (step[l] == last[l])
                        continue;
                    last[l] = step[l];
                    const float v = synthyboy_mpc::lfoLiveValue (step[l]);
                    liveValue[l].store (v, std::memory_order_relaxed);
#if SYNTHYBOY_MPC_LFO_LIVE_NOTIFY
                    if (audioMaster)
                        audioMaster (e, audioMasterAutomate, first + l, 0, nullptr, v);
#else
                    (void) e;
                    (void) first;
#endif
                }
                });
#if SYNTHYBOY_MPC_SPECTRUM
                gSpectrum.run ([&]
                {
                    injectFault ("spectrum");
                    float q[synthyboy_mpc::kSpectrumCount];
#if SYNTHYBOY_MPC_SPECTRUM_SELFTEST
                    // a wave that runs over the 32 columns, one turn in 4 seconds, 2 crests: every column moves on every tick
                    const double ts = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
                    for (int k = 0; k < synthyboy_mpc::kSpectrumCount; ++k)
                        q[k] = synthyboy_mpc::spectrumQuantize (static_cast<float> (0.5 + 0.5 * std::sin (6.283185307179586 * (0.25 * ts - 2.0 * k / synthyboy_mpc::kSpectrumCount))));
#else
                    analyser.tick (spectrumRing, spectrumRate.load (std::memory_order_relaxed), q);
#endif
                    for (int k = 0; k < synthyboy_mpc::kSpectrumCount; ++k)
                    {
                        anyActive = anyActive || q[k] != 0.0f;
                        if (q[k] == specLast[k])
                            continue;
                        specLast[k] = q[k];
                        spectrumValue[k].store (q[k], std::memory_order_relaxed);
#if SYNTHYBOY_MPC_LFO_LIVE_NOTIFY
                        if (audioMaster)
                            audioMaster (e, audioMasterAutomate, first + synthyboy_mpc::kLfoLiveCount + k, 0, nullptr, q[k]);
#endif
                    }
                });
#endif
            }
        }
    }

    void startLive (AEffect* e)
    {
        std::shared_ptr<LiveSync> sync = liveSync;
        liveThread = std::thread ([this, e, sync]
        {
            try { liveLoop (e); }
            catch (...) { synthyboy_mpc::PollStatus::report ("display thread", this, 1, true, "an exception reached the top of the thread"); }
            {
                std::lock_guard<std::mutex> g (sync->m);
                sync->done = true;
            }
            sync->cv.notify_all();
            reapAfterThread();   // the last thing this thread does: when effClose gave up waiting, the instance is deleted here
        });
    }

    /** The display thread has ended. When effClose stopped waiting for it (reapState 1) nobody else will free the instance: this thread does, and touches nothing of it afterwards. */
    void reapAfterThread() noexcept
    {
        if (reapState.exchange (2) == 1)
        {
#if SYNTHYBOY_MPC_OVERLAY
            if (overlayStuck.load (std::memory_order_acquire))
                return;   // the overlay thread may still use the view: the instance stays
#endif
            delete this;
        }
    }

    /** Test hook (SYNTHYBOY_MPC_DEBUG_HOOKS only): the environment variable SYNTHYBOY_MPC_TEST_FAULT names a feature of the display thread whose tick then throws on every call. */
    static void injectFault (const char* feature)
    {
#if SYNTHYBOY_MPC_DEBUG_HOOKS
        const char* const want = std::getenv ("SYNTHYBOY_MPC_TEST_FAULT");   // looked at on every tick: a test sets it after other instances ran
        if (want != nullptr && std::strcmp (want, feature) == 0)
            throw std::runtime_error (std::string ("injected fault in ") + feature);
#else
        (void) feature;
#endif
    }
#endif

#if SYNTHYBOY_MPC_PAD_PRESSURE
    std::atomic<bool> padPressure { synthyboy_mpc::kPadPressureDefaultOn };
    std::atomic<float> padPressureAmount { synthyboy_mpc::kPadPressureDefaultPercent }; // percent, 0 .. 100

    /** Hands the settings to the engine (any thread; a no-op for an engine that has no pad pressure layer). */
    void updateEnginePadPressure() noexcept
    {
        if (engine)
            synthyboy::setOwnPadPressureToCutoff (*engine, synthyboy_mpc::padPressureDepth (padPressure.load (std::memory_order_relaxed), padPressureAmount.load (std::memory_order_relaxed)));
    }

    /** Index of "Pad Pressure": after the engine's parameters and the display parameters (cursors, spectrum columns). */
    static int padPressureBase() noexcept
    {
        return static_cast<int> (synthyboy::paramCount())
#if SYNTHYBOY_MPC_LFO_LIVE
             + kLiveParams
#endif
            ;
    }
    static bool isPadPressureParam (VstInt32 index) noexcept
    {
        return index >= padPressureBase() && index < padPressureBase() + synthyboy_mpc::kPadPressureParams;
    }
#endif

#if SYNTHYBOY_MPC_CALLBACK_STATS
    synthyboy_mpc::CallbackStats callbackStats;

    /** The variant of this build, for the first lines of the statistics file. */
    static std::string buildDescription()
    {
        std::string b;
        size_t extraParams = 0;
#if SYNTHYBOY_MPC_LFO_LIVE
        extraParams = PluginInstance::kLiveParams;
        b += "LFO_LIVE=ON LFO_LIVE_HZ=" + std::to_string (SYNTHYBOY_MPC_LFO_LIVE_HZ)
           + " LFO_LIVE_NOTIFY=" + (SYNTHYBOY_MPC_LFO_LIVE_NOTIFY ? "ON" : "OFF")
           + " LFO_LIVE_SELFTEST=" + (SYNTHYBOY_MPC_LFO_LIVE_SELFTEST ? "ON" : "OFF");
#if SYNTHYBOY_MPC_SPECTRUM
        b += std::string (" SPECTRUM=ON SPECTRUM_SELFTEST=") + (SYNTHYBOY_MPC_SPECTRUM_SELFTEST ? "ON" : "OFF");
#endif
#else
        b += "LFO_LIVE=OFF";
#endif
#if SYNTHYBOY_MPC_PAD_PRESSURE
        extraParams += synthyboy_mpc::kPadPressureParams;
        b += " PAD_PRESSURE=ON";
#else
        b += " PAD_PRESSURE=OFF";
#endif
#if SYNTHYBOY_MPC_OVERLAY
        extraParams += 1 + synthyboy_mpc::play::kParams + synthyboy_mpc::QRouter::kCount; // "Overlay Instance", then the Play page's three selectors and 24 proxies, then "Q 1" .. "Q 4", after everything else
#endif
        b += " params=" + std::to_string (synthyboy::paramCount() + extraParams);
#if defined(__aarch64__)
        b += " arch=arm64";
#elif defined(__arm__)
        b += " arch=arm32";
#elif defined(__x86_64__)
        b += " arch=x86_64";
#endif
        return b;
    }

    void startStats()
    {
        static std::atomic<int> instances { 0 };
        const char* envPath = std::getenv ("SYNTHYBOY_MPC_CALLBACK_STATS_FILE");
        std::string path = envPath != nullptr && *envPath != 0 ? envPath : "/tmp/synthyboy-callback-stats.txt";
        const int n = ++instances;
        if (n > 1)
            path += "." + std::to_string (n);
        const char* envMs = std::getenv ("SYNTHYBOY_MPC_CALLBACK_STATS_INTERVAL_MS");
        const int ms = envMs != nullptr ? std::atoi (envMs) : 0;
        const char* envReset = std::getenv ("SYNTHYBOY_MPC_CALLBACK_STATS_RESET_FILE");
        callbackStats.setResetFile (envReset != nullptr && *envReset != 0 ? envReset : "/tmp/synthyboy-stats-reset");
        callbackStats.start (path, buildDescription(), ms > 0 ? ms : 5000);
    }
#endif

    PluginInstance()
    {
#if SYNTHYBOY_MPC_OVERLAY
        synthyboy_mpc::PlayLog::refresh();   // the debug log of the host's calls (play_log.h) is on from the first call when its switch file is there
#endif
        outputStage.prepare (sampleRate > 0.0 ? sampleRate : 44100.0);
        outputStage.setGainDb (static_cast<float> (SYNTHYBOY_MPC_OUTPUT_GAIN_DB));
        outputStage.reset();
        applyOutputLimiter();
        // Create engine based on SYNTHYBOY_MPC_ENGINE_VITAL (from CMake) && SYNTHYBOY_HAS_VITAL (from core)
#if SYNTHYBOY_MPC_ENGINE_VITAL && SYNTHYBOY_HAS_VITAL
        engine = synthyboy::createEngine(synthyboy::EngineKind::vital);
#else
        engine = synthyboy::createEngine(synthyboy::EngineKind::own);
#endif
#if SYNTHYBOY_MPC_PAD_PRESSURE
        updateEnginePadPressure();
#endif
        synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kEngineCreate);
        MPCKIT_LOG ("startup", info, "engine created at %.2f ms", mpckit::devlog::logger().elapsedMs());

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
#if SYNTHYBOY_MPC_OVERLAY
            contentDirPath = contentDir;
#endif
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
        synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kContentStore);

        // Scan factory presets (never on the audio thread).
        {
            MPCKIT_SCOPE ("startup", "scan factory presets");
            if (!contentDir.empty())
                programs = synthyboy_mpc::scanPrograms (contentDir + "/presets");
        }
        synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kScanPresets);

        // Apply default values to engine and mirror
        if (engine)
        {
            const auto* table = synthyboy::paramTable();
            size_t count = synthyboy::paramCount();
            for (size_t i = 0; i < count; ++i)
            {
                engine->setParam(table[i].id, table[i].def);
                paramMirror[i].store(table[i].def, std::memory_order_relaxed);
#if SYNTHYBOY_MPC_OVERLAY
                paramUndo[i].store (std::nanf (""), std::memory_order_relaxed);
#endif
            }
        }
        synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kDefaultParams);
    }

    /** The end of this instance's life (effClose calls it, the destructor again, harmlessly), in the order that keeps everything alive for whoever still uses it:
          0. `closing` is set first: from here no thread of this instance calls the host (the HostGate answers 0), every loop stops at its next look, a sample being read stops at its next block;
          1. the display thread is told to stop and WAITED FOR for at most kLiveJoinMs. It is the only thread of ours that calls the host (audioMasterAutomate) or touches the instance besides the host's own
             threads. When it does not end in time (it is inside a host call that does not return, or a disk read of a slow medium) it is DETACHED and the instance is left to it: this function returns
             false, effClose does not delete the instance, and the thread deletes it itself as the last thing it does (reapAfterThread). Nothing is joined without a limit and nothing is used after it was freed;
             the host is never held up for longer than the limit;
          2. the overlay forgets the instance: its slot is freed and the overlay thread no longer holds the view (Service::detach waits for the controller's lock for a limited time; when the overlay thread
             does not let go, the view could still be used: the instance is then kept alive for good, a few megabytes lost instead of a crash);
          3. only then the callback statistics stop and the members are destroyed (the destructor's own order).
        Returns true when the caller may delete the instance now. */
    bool shutdown() noexcept
    {
        closing.store (true, std::memory_order_release);
        bool deletable = true;
#if SYNTHYBOY_MPC_LFO_LIVE
        liveStop.store (true, std::memory_order_relaxed);
        if (liveThread.joinable())
        {
            std::unique_lock<std::mutex> lk (liveSync->m);
            const bool ended = liveSync->cv.wait_for (lk, std::chrono::milliseconds (kLiveJoinMs), [this] { return liveSync->done; });
            lk.unlock();
            int expected = 0;
            if (ended || ! reapState.compare_exchange_strong (expected, 1))
                liveThread.join();   // it has ended (or ends this very moment: it has set `done` or finished already)
            else
            {
                liveThread.detach();   // the thread frees the instance when it ends
                deletable = false;
            }
        }
#endif
#if SYNTHYBOY_MPC_OVERLAY
        stopOverlay();
        if (overlayStuck.load (std::memory_order_acquire))
            deletable = false;
#endif
        return deletable;
    }

    ~PluginInstance()
    {
        (void) shutdown();
#if SYNTHYBOY_MPC_CALLBACK_STATS
        callbackStats.stop(); // writes the file one last time
#endif
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

#if SYNTHYBOY_MPC_CALLBACK_STATS
// What the plugin does towards the host is counted by opcode, then passed on unchanged (every call goes through inst->audioMaster).
static VstIntPtr countingHostCallback(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    PluginInstance* inst = e ? getInstance(e) : nullptr;
    if (!inst || !inst->hostCallback)
        return 0;
    inst->callbackStats.hostCall(opcode);
    return inst->hostCallback(e, opcode, index, value, ptr, opt);
}
#endif

// ---------------------------------------------------------------------------
// Helper: current program name (handles the "no program" state)
// ---------------------------------------------------------------------------

static std::string currentProgramName (PluginInstance* inst)
{
    {
        std::lock_guard<std::mutex> g (inst->programMutex);
        if (inst->noProgram)
            return inst->noProgramName.empty() ? "Init" : inst->noProgramName;
    }
    int prog = static_cast<int> (inst->currentProgram.load (std::memory_order_relaxed));
    if (prog == 0)
        return "Init";
    if (prog >= 1 && prog <= static_cast<int> (inst->programs.size()))
        return inst->programs[static_cast<size_t> (prog - 1)].name;
    return "Init";
}

/** The chunk of effGetChunk. */
static std::string buildStateChunk (PluginInstance* inst)
{
    // Build ParamValues from the atomic mirror (thread-safe snapshot)
    synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        values.plain[i] = inst->paramMirror[i].load(std::memory_order_relaxed);
#if SYNTHYBOY_MPC_OVERLAY
    std::lock_guard<std::mutex> contentGuard (inst->contentMutex);
#endif
    std::string state = synthyboy::saveState(values, inst->content);
    int prog = static_cast<int> (inst->currentProgram.load (std::memory_order_relaxed));
    std::string chunk = synthyboy_mpc::chunkWithProgram (state, prog, currentProgramName (inst));
#if SYNTHYBOY_MPC_OVERLAY
    {
        // the three Play selectors ride in the chunk before the pad pressure members (which are read from the very end)
        const int sel[3] = { inst->playSel[0].load (std::memory_order_relaxed), inst->playSel[1].load (std::memory_order_relaxed), inst->playSel[2].load (std::memory_order_relaxed) };
        chunk = synthyboy_mpc::play::chunkWithPlay (std::move (chunk), sel);
    }
#endif
    chunk = synthyboy_mpc::chunkWithOutputLimiter (std::move (chunk), inst->outputLimiter.load (std::memory_order_relaxed));   // before the pad pressure members, which are read from the very end
#if SYNTHYBOY_MPC_PAD_PRESSURE
    chunk = synthyboy_mpc::chunkWithPadPressure (std::move (chunk), inst->padPressure.load (std::memory_order_relaxed), inst->padPressureAmount.load (std::memory_order_relaxed));
#endif
    return chunk;
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
#if SYNTHYBOY_MPC_OVERLAY
        inst->paramUndo[i].store (std::nanf (""), std::memory_order_relaxed);   // a whole new state: no "value before the last change" any more
#endif
        if (inst->engine)
            inst->engine->setParam(table[i].id, values.plain[i]);
    }
#if SYNTHYBOY_MPC_OVERLAY
    std::lock_guard<std::mutex> contentGuard (inst->contentMutex);
#endif
#if SYNTHYBOY_MPC_OVERLAY
    bool namesChanged = false;
    for (int m = 0; m < synthyboy::kNumMacros; ++m)
        namesChanged = namesChanged || inst->content.macroNames[m] != content.macroNames[m];
#endif
    inst->content = std::move(content);
    if (inst->engine)
        inst->engine->setContent(inst->content);
    if (inst->engine)
        inst->engine->readyContent();   // not the audio thread: the built-in tables the state names are there before the next block (a table another thread is still making is waited for)
#if SYNTHYBOY_MPC_OVERLAY
    inst->publishOverlayContent();
    if (namesChanged)
        inst->macroNamesDue.store (true, std::memory_order_release);   // the display thread tells the host the names of the Q proxies are other now
#endif
}

/** effGetParameterProperties of a parameter of the engine's table (also what a Play proxy reports for the instance that is selected). */
static void fillParameterProperties (const synthyboy::ParamInfo& info, VstParameterProperties* props)
{
    std::memset(props, 0, sizeof(*props));
    copyString(props->label, sizeof(props->label), synthyboy::unitLabel(info.unit));
    props->stepFloat = info.interval > 0 ? info.interval : (info.max - info.min) / 1000.0f;
    props->smallStepFloat = info.interval > 0 ? info.interval : (info.max - info.min) / 10000.0f;
    props->largeStepFloat = info.interval > 0 ? info.interval : (info.max - info.min) / 100.0f;
    props->flags = kVstParameterCanRamp;
    if (info.flags & synthyboy::kParamStepped)
    {
        props->flags |= kVstParameterUsesIntegerMinMax;
        props->minInteger = static_cast<VstInt32>(info.min);
        props->maxInteger = static_cast<VstInt32>(info.max);
        props->stepInteger = info.interval > 0 ? static_cast<VstInt32>(info.interval) : 1;
    }
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
    if (e != nullptr)
        if (PluginInstance* inst = getInstance (e))
        {
            if (opcode != effClose && inst->closing.load (std::memory_order_acquire))
                return 0;   // effClose has begun: nothing else is served
            if (! inst->hostKnows.load (std::memory_order_relaxed))
                inst->hostKnows.store (true, std::memory_order_release);   // the host holds the AEffect pointer now: the display thread may call it back (robustness review F12)
        }
    synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kFirstCall);   // the host's first call of this plugin (once; a relaxed load afterwards)
    // What the host waits for: the time of every call it makes of us, the longest of each kind kept (status file host_call_max_ms_*). Not the per-block audio calls.
    int slot = -1;
    switch (opcode)
    {
        case effOpen: slot = synthyboy_mpc::StartupTimes::kCallOpen; break;
        case effSetProgram: slot = synthyboy_mpc::StartupTimes::kCallSetProgram; break;
        case effGetProgramName: slot = synthyboy_mpc::StartupTimes::kCallGetProgramName; break;
        case effGetProgramNameIndexed: slot = synthyboy_mpc::StartupTimes::kCallGetProgramNameIndexed; break;
        case effGetChunk: slot = synthyboy_mpc::StartupTimes::kCallGetChunk; break;
        case effSetChunk: slot = synthyboy_mpc::StartupTimes::kCallSetChunk; break;
        case effMainsChanged: slot = synthyboy_mpc::StartupTimes::kCallMains; break;
        case effSetSampleRate: slot = synthyboy_mpc::StartupTimes::kCallSampleRate; break;
        case effSetBlockSize: slot = synthyboy_mpc::StartupTimes::kCallBlockSize; break;
        case effGetParamName: case effGetParamLabel: case effGetParamDisplay: case effGetParameterProperties: case effCanBeAutomated: slot = synthyboy_mpc::StartupTimes::kCallParamInfo; break;
        case effProcessEvents: break;   // every block, on the audio thread: not timed here
        default: slot = synthyboy_mpc::StartupTimes::kCallOther; break;
    }
    const int64_t t0 = slot >= 0 ? synthyboy_mpc::StartupTimes::nowNs() : 0;
    VstIntPtr result = 0;
    try { result = dispatch(e, opcode, index, value, ptr, opt); }
    catch (...) { result = 0; }
    if (slot >= 0)
        synthyboy_mpc::startupTimes().callDone (static_cast<synthyboy_mpc::StartupTimes::Call> (slot), synthyboy_mpc::StartupTimes::nowNs() - t0);
    return result;
}

#if SYNTHYBOY_MPC_CALLBACK_STATS
static VstIntPtr dispatchHandled(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

// What the host asks and sends, recorded (callback_stats.h) around the handling of every call; the handling itself is dispatchHandled, unchanged.
static VstIntPtr dispatch(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    PluginInstance* inst = e ? getInstance(e) : nullptr;
    if (!inst)
        return dispatchHandled(e, opcode, index, value, ptr, opt);
    synthyboy_mpc::CallbackStats& stats = inst->callbackStats;
    stats.dispatcherCall(opcode, index, value, opt, ptr != nullptr);
    if (opcode == effProcessEvents && ptr)
    {
        const VstEvents* events = static_cast<const VstEvents*>(ptr);
        for (VstInt32 i = 0; i < events->numEvents; ++i)
        {
            const VstEvent* ev = events->events[i];
            if (!ev)
                continue;
            if (ev->type != kVstMidiType)
            {
                stats.otherEvent(ev->type);
                continue;
            }
            const VstMidiEvent* m = reinterpret_cast<const VstMidiEvent*>(ev);
            stats.midiMessage(static_cast<uint8_t>(m->midiData[0]), static_cast<uint8_t>(m->midiData[1]), static_cast<uint8_t>(m->midiData[2]), m->deltaFrames);
        }
    }
    const VstIntPtr result = dispatchHandled(e, opcode, index, value, ptr, opt);
    if (opcode == effCanDo && ptr)
        stats.canDo(static_cast<const char*>(ptr), static_cast<int>(result));
    return result;
}

static VstIntPtr dispatchHandled(AEffect* e, VstInt32 opcode, VstInt32 index,
                                 VstIntPtr value, void* ptr, float opt)
#else
static VstIntPtr dispatch(AEffect* e, VstInt32 opcode, VstInt32 index,
                          VstIntPtr value, void* ptr, float opt)
#endif
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
#if SYNTHYBOY_MPC_OVERLAY
            synthyboy_mpc::PlayLog::line ("effClose: instance %p", static_cast<void*> (inst));
#endif
            const bool deletable = inst->shutdown(); // the display thread has ended (no host callback after this) or was given up on after its limit, the overlay has forgotten the instance and its slot
            e->object = nullptr;
            if (deletable)
                delete inst;   // else a thread or the overlay still holds it: it frees itself (display thread) or stays (overlay stuck); see PluginInstance::shutdown
        }
        delete e;
        return 1;
    }

    case effSetProgram:
    {
        if (!inst) return 0;
        // VST2: value is the program index, index is unused
        int prog = static_cast<int>(value);
#if SYNTHYBOY_MPC_OVERLAY
        synthyboy_mpc::PlayLog::line ("effSetProgram %d (protected %d, window %d)", prog, inst->protectedProgram.load (std::memory_order_relaxed), inst->restoreSinceMs.load (std::memory_order_relaxed) >= 0 ? 1 : 0);
#endif
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
            inst->setNoProgram (false, std::string());
            return 1;
        }

#if SYNTHYBOY_MPC_OVERLAY
        inst->restoreClose();   // a program that is really loaded ends the restore window (the protected re-selection of the saved program does not: the host may write the parameters back after it)
#endif
        if (prog == 0)
        {
            // Program 0: Init (default values, empty content)
            applyState (inst, synthyboy::ParamValues::defaults (), synthyboy::StateContent {});
        }
        else
        {
            // Program >= 1: load preset file
            const auto& entry = inst->programs[static_cast<size_t> (prog - 1)];
            std::string data;
            if (! synthyboy_mpc::safefile::readWholeBounded (entry.path.c_str(), data, kMaxPresetFileBytes))
                return 1; // file cannot be read, is not a regular file or is larger than a preset can be (4 MB; a preset is a few KB)

            synthyboy::PresetMeta meta;
            synthyboy::StateContent content;
            synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
            synthyboy::LoadResult result = synthyboy::loadPreset (data.data(), data.size(), meta, values, content);
            if (result.status != synthyboy::LoadStatus::ok && result.status != synthyboy::LoadStatus::newer_version)
                return 1; // load failed

            applyState (inst, values, std::move(content));
        }

        inst->currentProgram.store (prog, std::memory_order_relaxed);
        inst->setNoProgram (false, std::string());
#if SYNTHYBOY_MPC_OVERLAY
        inst->setProgramModified (false);
#endif
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
        std::string progName = currentProgramName (inst);
#if SYNTHYBOY_MPC_OVERLAY
        if (inst->programModified.load (std::memory_order_relaxed))
            progName += " *";
#endif
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
        constexpr size_t kCap = synthyboy_mpc::kParamStrBytes;   // the bytes this plugin writes into the host's buffer, NUL included (host_strings.h): nothing else of the buffer is touched

#if SYNTHYBOY_MPC_OVERLAY
        if (PluginInstance::isOverlayParam (index))
        {
            const int frame = static_cast<int> (inst->overlayValue.load (std::memory_order_relaxed) * static_cast<float> (synthyboy_mpc::overlay::layout::kInstanceSteps) + 0.5f);
            const int slot = synthyboy_mpc::overlay::slotOfInstanceFrame (frame);
            if (opcode == effGetParamName)
                synthyboy_mpc::putBounded (buf, kCap, "Overlay Instance");
            else if (opcode == effGetParamLabel)
                buf[0] = '\0';
            else if (slot == synthyboy_mpc::overlay::kSlotUnknown)
                synthyboy_mpc::putBounded (buf, kCap, "blank");
            else if (slot == synthyboy_mpc::overlay::kSlotNone)
                synthyboy_mpc::putBounded (buf, kCap, "none");
            else
                std::snprintf (buf, kCap, "slot %d", slot);
                        return 1;
        }
#endif
#if SYNTHYBOY_MPC_OVERLAY
        if (PluginInstance::isQParam (index))
        {
            // the proxy reports the CURRENT target: its name (a transpose that is split in two knobs says Oct / Sem), its unit and its value text; "Q n" while it has none (or a virtual one)
            const int q = index - PluginInstance::qBase();
            const int real = inst->qRouter.target (q);
            const int mode = inst->qRouter.mode (q);
            if (real < 0)
            {
                if (opcode == effGetParamName)
                    std::snprintf (buf, kCap, "Q %d", q + 1);
                else
                    buf[0] = '\0';
            }
            else if (opcode == effGetParamName)
            {
                if (! synthyboy_mpc::macroParamName (std::atomic_load (&inst->overlayContent), real, buf, kCap))   // a macro: the name it has been given ("Filter Sweep")
                    // the target's name, shortened by the same word list as the parameter table's names so that name and suffix fit the bound together
                    synthyboy_mpc::nameWithSuffix (synthyboy::paramTable()[real].name, mode == synthyboy_mpc::QRouter::kOct ? " Oct" : (mode == synthyboy_mpc::QRouter::kSem ? " Sem" : ""), buf, kCap);
            }
            else if (opcode == effGetParamLabel)
                synthyboy_mpc::putBounded (buf, kCap, mode == synthyboy_mpc::QRouter::kOct ? "oct" : (mode == synthyboy_mpc::QRouter::kSem ? "st" : synthyboy::unitLabel (synthyboy::paramTable()[real].unit)));
            else if (mode == synthyboy_mpc::QRouter::kOct || mode == synthyboy_mpc::QRouter::kSem)
            {
                const int t = static_cast<int> (std::lround (inst->paramMirror[real].load (std::memory_order_relaxed)));
                std::snprintf (buf, kCap, "%+d", mode == synthyboy_mpc::QRouter::kOct ? synthyboy_mpc::QRouter::octOfTranspose (t) : synthyboy_mpc::QRouter::semOfTranspose (t));
            }
            else
                synthyboy::valueToText (synthyboy::paramTable()[real], inst->paramMirror[real].load (std::memory_order_relaxed), buf, kCap);
                        return 1;
        }
        if (PluginInstance::isPlayParam (index))
        {
            namespace pl = synthyboy_mpc::play;
            if (PluginInstance::isPlaySelector (index))
            {
                const int g = index - PluginInstance::playBase();
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, pl::selectorName (g));
                else if (opcode == effGetParamLabel)
                    buf[0] = '\0';
                else
                    synthyboy_mpc::putBounded (buf, kCap, pl::selectorText (g, inst->playSel[g].load (std::memory_order_relaxed)).c_str());
                                return 1;
            }
            const int p = PluginInstance::playProxyOf (index);
            const int real = inst->playRealOf (p);
            if (opcode == effGetParamName)
                synthyboy_mpc::putBounded (buf, kCap, pl::slots (PluginInstance::playGroupOfProxy (p))[PluginInstance::playSlotOfProxy (p)].proxyName);
            else if (real < 0)
                buf[0] = '\0';
            else if (opcode == effGetParamLabel)
                synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (synthyboy::paramTable()[real].unit));
            else
                synthyboy::valueToText (synthyboy::paramTable()[real], inst->paramMirror[real].load (std::memory_order_relaxed), buf, kCap);
                        return 1;
        }
#endif
#if SYNTHYBOY_MPC_LFO_LIVE
        if (inst->isLive (index))
        {
#if SYNTHYBOY_MPC_SPECTRUM
            const int n = index - static_cast<int> (synthyboy::paramCount());
            const int steps = PluginInstance::liveSteps (n);
            const int step = static_cast<int> (inst->liveRead (n) * static_cast<float> (steps) + 0.5f);
            if (opcode == effGetParamName)
            {
                if (n >= synthyboy_mpc::kLfoLiveCount)
                    std::snprintf (buf, kCap, "Spectrum %d", n - synthyboy_mpc::kLfoLiveCount + 1);
                else
                    std::snprintf (buf, kCap, "LFO %d Cursor", n + 1);
            }
            else if (opcode == effGetParamLabel)
                buf[0] = '\0';
            else if (step == 0)
                synthyboy_mpc::putBounded (buf, kCap, n < synthyboy_mpc::kLfoLiveCount ? "idle" : "0");
            else
                std::snprintf (buf, kCap, "%d/%d", step, steps);
#else
            const int n = index - static_cast<int> (synthyboy::paramCount());
            const int step = static_cast<int> (inst->liveValue[n].load (std::memory_order_relaxed) * static_cast<float> (synthyboy_mpc::kLfoLiveSteps) + 0.5f);
            if (opcode == effGetParamName)
                std::snprintf (buf, kCap, "LFO %d Cursor", n + 1);
            else if (opcode == effGetParamLabel)
                buf[0] = '\0';
            else if (step == 0)
                synthyboy_mpc::putBounded (buf, kCap, "idle");
            else
                std::snprintf (buf, kCap, "%d/%d", step, synthyboy_mpc::kLfoLiveSteps);
#endif
                        return 1;
        }
#endif
#if SYNTHYBOY_MPC_PAD_PRESSURE
        if (PluginInstance::isPadPressureParam (index))
        {
            const bool amount = index == PluginInstance::padPressureBase() + 1;
            if (opcode == effGetParamName)
                synthyboy_mpc::putBounded (buf, kCap, amount ? "Pad Pressure Amount" : "Pad Pressure");
            else if (opcode == effGetParamLabel)
                synthyboy_mpc::putBounded (buf, kCap, amount ? "%" : "");
            else if (amount)
                std::snprintf (buf, kCap, "%d", static_cast<int> (inst->padPressureAmount.load (std::memory_order_relaxed) + 0.5f));
            else
                synthyboy_mpc::putBounded (buf, kCap, inst->padPressure.load (std::memory_order_relaxed) ? "On" : "Off");
                        return 1;
        }
#endif
        size_t paramCount = synthyboy::paramCount();
        if (static_cast<size_t>(index) >= paramCount) return 0;

        const synthyboy::ParamInfo* info = synthyboy::paramTable() + index;

        if (opcode == effGetParamName)
        {
            synthyboy_mpc::shortName (info->name, buf, kCap, synthyboy_mpc::kParamNameChars);   // 38 of the names are longer than the bound: the word list shortens them
        }
        else if (opcode == effGetParamLabel)
        {
            const char* unit = synthyboy::unitLabel(info->unit);
            synthyboy_mpc::putBounded (buf, kCap, unit);
        }
        else // effGetParamDisplay
        {
            float plain = inst->paramMirror[index].load(std::memory_order_relaxed);
            synthyboy::valueToText(*info, plain, buf, kCap);
        }
        return 1;
    }

    case effSetSampleRate:
    {
        if (!inst) return 0;
        if (std::isfinite(opt) && opt > 0.0f)
        {
            inst->sampleRate = opt;
#if SYNTHYBOY_MPC_OVERLAY
            inst->overlayRate.store (opt, std::memory_order_relaxed);
#endif
#if SYNTHYBOY_MPC_SPECTRUM
            inst->spectrumRate.store (opt, std::memory_order_relaxed);
#endif
        }
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
#if SYNTHYBOY_MPC_CALLBACK_STATS
        inst->callbackStats.restartGaps();
#endif
        if (inst->mainsOn)
            synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kResume);
        // Prepare when mains go on if sample rate or block size changed since last prepare
        if (inst->mainsOn && inst->engine)
        {
            double safeSr = (std::isfinite(inst->sampleRate) && inst->sampleRate > 0.0) ? inst->sampleRate : 44100.0;
            int sr = static_cast<int>(safeSr);
            int bs = inst->maxBlockSize > 0 ? inst->maxBlockSize : 128;
            if (!inst->prepared || sr != inst->lastPreparedSampleRate || bs != inst->lastPreparedBlockSize)
            {
                // The quality profile of this device (device_profile.h: a table by device name with overrides, "low" everywhere until the devices are measured), fixed for the life of the process.
                synthyboy::Quality quality = synthyboy_mpc::profileQuality (synthyboy_mpc::profileInForce().profile);
                quality.realtime = true; // live: audio thread never waits for helper thread
                quality.lazyTables = true; // the 64 frame built-in tables nothing names are built by a low priority thread, not before the host's resume call returns (docs/MPC.md "Start-up time")
                inst->engine->prepare(safeSr, bs, quality);
                inst->outputStage.prepare (safeSr);
                inst->prepared = true;
                inst->lastPreparedSampleRate = sr;
                inst->lastPreparedBlockSize = bs;
                synthyboy_mpc::startupTimes().setWavetableMs (inst->engine->wavetableBuildMs());
                MPCKIT_LOG ("startup", info, "engine prepared (sr %d, block %d)", sr, bs);
            }
            else
            {
                // Resume without re-prepare: reset transient state of output stage
                inst->outputStage.reset();
            }
        }
        if (inst->mainsOn)
            synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kPrepared);
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
#if SYNTHYBOY_MPC_OVERLAY
        if (PluginInstance::isOverlayParam (index) || PluginInstance::isPlayParam (index) || PluginInstance::isQParam (index))
            return 0;
#endif
#if SYNTHYBOY_MPC_LFO_LIVE
        if (inst->isLive (index))
            return 0;
#endif
        // All parameters are automatable
        return 1;
    }

    case effGetChunk:
    {
        if (!inst) return 0;
        // index: 0 = bank, 1 = program
        std::string chunk = buildStateChunk (inst);
#if SYNTHYBOY_MPC_OVERLAY
        if (synthyboy_mpc::PlayLog::enabled())
        {
            synthyboy_mpc::PlayLog::line ("effGetChunk size=%zu hash=%08x", chunk.size(), synthyboy_mpc::PlayLog::hash (chunk.data(), chunk.size()));
            synthyboy_mpc::PlayLog::offerChunk (true, chunk.data(), chunk.size());   // written by the display thread, not here
        }
#endif
        // Reuse the plugin-owned chunk buffer (VST2: host does not free chunk memory)
        std::lock_guard<std::mutex> chunkGuard (inst->chunkMutex);
        std::vector<char>& buffer = inst->chunkBuffers[inst->chunkNext++ % PluginInstance::kChunkBuffers];
        buffer.assign (chunk.begin(), chunk.end());
        if (ptr)
            *static_cast<void**>(ptr) = buffer.data();
        return static_cast<VstInt32>(chunk.size());
    }

    case effSetChunk:
    {
        if (!inst) return 0;
        // index: 0 = bank, 1 = program
        const void* chunk = ptr;
        VstInt32 size = value;
#if SYNTHYBOY_MPC_OVERLAY
        if (synthyboy_mpc::PlayLog::enabled())
        {
            synthyboy_mpc::PlayLog::line ("effSetChunk size=%d hash=%08x index=%d", static_cast<int> (size), size > 0 && chunk ? synthyboy_mpc::PlayLog::hash (chunk, static_cast<size_t> (size)) : 0u, static_cast<int> (index));
            if (size > 0 && chunk)
                synthyboy_mpc::PlayLog::offerChunk (false, chunk, static_cast<size_t> (size));   // written by the display thread, not here
        }
#endif

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
#if SYNTHYBOY_MPC_OVERLAY
                inst->restoreOpen();   // the restore window: the host's write-back of the saved parameter values is an echo (PluginInstance::restoreActive)
                {
                    // the selectors of the project; a chunk without them (an older one) leaves them as they are. The display thread tells the host (the proxies' values follow the instances)
                    int sel[3] = { inst->playSel[0].load (std::memory_order_relaxed), inst->playSel[1].load (std::memory_order_relaxed), inst->playSel[2].load (std::memory_order_relaxed) };
                    if (synthyboy_mpc::play::playFromChunk (chunk, static_cast<size_t>(size), sel))
                        for (int g = 0; g < 3; ++g)
                            inst->playSetSelector (g, sel[g]);
                }
#endif
                {
                    // the output limiter setting rides in the chunk; a chunk without it (an older one) leaves the setting as it is (the default is on)
                    bool limiterOn = inst->outputLimiter.load (std::memory_order_relaxed);
                    if (synthyboy_mpc::outputLimiterFromChunk (chunk, static_cast<size_t>(size), limiterOn))
                    {
                        inst->outputLimiter.store (limiterOn, std::memory_order_relaxed);
                        inst->applyOutputLimiter();
                    }
                }
#if SYNTHYBOY_MPC_PAD_PRESSURE
                {
                    // the player's pad setting rides in the chunk; a chunk without it (an older one) leaves the setting as it is
                    bool on = inst->padPressure.load (std::memory_order_relaxed);
                    float percent = inst->padPressureAmount.load (std::memory_order_relaxed);
                    if (synthyboy_mpc::padPressureFromChunk (chunk, static_cast<size_t>(size), on, percent))
                    {
                        inst->padPressure.store (on, std::memory_order_relaxed);
                        inst->padPressureAmount.store (percent, std::memory_order_relaxed);
                        inst->updateEnginePadPressure();
                    }
                }
#endif

#if SYNTHYBOY_MPC_OVERLAY
                inst->setProgramModified (true);   // a project's state: the host marks the program as changed after a chunk restore
#endif
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
                        inst->setNoProgram (false, std::string());
                        inst->protectedProgram.store (r, std::memory_order_relaxed);
                    }
                    else
                    {
                        // Program not found: no-program state
                        inst->currentProgram.store (0, std::memory_order_relaxed);
                        inst->setNoProgram (true, hasName ? name : std::string());
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

#if SYNTHYBOY_MPC_OVERLAY
        if (PluginInstance::isOverlayParam (index))
        {
            std::memset(props, 0, sizeof(*props));
            props->stepFloat = 1.0f / static_cast<float> (synthyboy_mpc::overlay::layout::kInstanceSteps);
            props->smallStepFloat = props->stepFloat;
            props->largeStepFloat = props->stepFloat;
            return 1;
        }
#endif
#if SYNTHYBOY_MPC_OVERLAY
        if (PluginInstance::isQParam (index))
        {
            // a Q proxy has the properties of its target; with none a plain 0 .. 1 knob
            const int real = inst->qRouter.target (index - PluginInstance::qBase());
            if (real >= 0)
            {
                fillParameterProperties (synthyboy::paramTable()[real], props);
                if (stepCountOf (synthyboy::paramTable()[real]) >= 2)
                {
                    // a stepped target (a toggle, a list, an integer stepper) is driven by the router in steps of its own (6 increments each, q_router.h), so the host must see a plain knob: with the target's integer range
                    // (stepInteger / UsesIntegerMinMax) the host quantised its increments itself (a list of 5 jumped four entries for six increments, a list of 20 did not move for fourteen single increments; Key 61, 2026-11-16)
                    props->flags = kVstParameterCanRamp;
                    props->minInteger = props->maxInteger = props->stepInteger = 0;
                    props->stepFloat = props->smallStepFloat = 1.0f / 128.0f;
                    props->largeStepFloat = 1.0f / 16.0f;
                }
                return 1;
            }
            std::memset (props, 0, sizeof (*props));
            props->stepFloat = 0.01f;
            props->smallStepFloat = 0.001f;
            props->largeStepFloat = 0.1f;
            return 1;
        }
        if (PluginInstance::isPlayParam (index))
        {
            std::memset (props, 0, sizeof (*props));
            if (PluginInstance::isPlaySelector (index))
            {
                const int g = index - PluginInstance::playBase();
                const int n = synthyboy_mpc::play::kInstances[g];
                props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f / static_cast<float> (n - 1);
                props->flags = kVstParameterUsesIntegerMinMax;
                props->minInteger = 0;   // like every stepped parameter of the engine: 0 .. n - 1 (the host's radio buttons and menus count from 0; the display text says "Osc 1" for 0)
                props->maxInteger = n - 1;
                props->stepInteger = 1;
                return 1;
            }
            const int real = inst->playRealOf (PluginInstance::playProxyOf (index));
            if (real < 0)
                return 0;
            fillParameterProperties (synthyboy::paramTable()[real], props);
            return 1;
        }
#endif
#if SYNTHYBOY_MPC_LFO_LIVE
        if (inst->isLive (index))
        {
            std::memset(props, 0, sizeof(*props));
#if SYNTHYBOY_MPC_SPECTRUM
            props->stepFloat = 1.0f / static_cast<float> (PluginInstance::liveSteps (index - static_cast<VstInt32> (synthyboy::paramCount())));
#else
            props->stepFloat = 1.0f / static_cast<float> (synthyboy_mpc::kLfoLiveSteps);
#endif
            props->smallStepFloat = props->stepFloat;
            props->largeStepFloat = props->stepFloat;
            return 1;
        }
#endif
#if SYNTHYBOY_MPC_PAD_PRESSURE
        if (PluginInstance::isPadPressureParam (index))
        {
            std::memset (props, 0, sizeof (*props));
            if (index == PluginInstance::padPressureBase())
            {
                copyString (props->label, sizeof (props->label), "");
                props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
                props->flags = kVstParameterIsSwitch | kVstParameterUsesIntegerMinMax;
                props->minInteger = 0;
                props->maxInteger = 1;
                props->stepInteger = 1;
            }
            else
            {
                copyString (props->label, sizeof (props->label), "%");
                props->stepFloat = 0.01f;
                props->smallStepFloat = 0.01f;
                props->largeStepFloat = 0.1f;
                props->flags = kVstParameterCanRamp;
            }
            return 1;
        }
#endif
        size_t paramCount = synthyboy::paramCount();
        if (static_cast<size_t>(index) >= paramCount) return 0;

        const synthyboy::ParamInfo* info = synthyboy::paramTable() + index;
        fillParameterProperties (*info, props);
        return 1;
    }

    case effGetEffectName:
    {
        char* name = static_cast<char*>(ptr);
        if (name)
        {
            copyString(name, kVstMaxEffectNameLen + 1, "SynthyBoy");
        }
        return 1;
    }

    case effGetVendorString:
    {
        char* vendor = static_cast<char*>(ptr);
        if (vendor)
        {
            copyString(vendor, kVstMaxVendorStrLen + 1, "Safian");
        }
        return 1;
    }

    case effGetProductString:
    {
        char* product = static_cast<char*>(ptr);
        if (product)
        {
            copyString(product, kVstMaxProductStrLen + 1, "SynthyBoy");
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

        // two const objects, made once (the initialisation of a function-local static is thread safe), never written again: the host's threads may all ask (robustness review F17)
        static const VstSpeakerArrangement emptyArr = [] { VstSpeakerArrangement a; std::memset (&a, 0, sizeof a); a.type = kSpeakerArrEmpty; a.numChannels = 0; return a; }();
        static const VstSpeakerArrangement stereoArr = []
        {
            VstSpeakerArrangement a;
            std::memset (&a, 0, sizeof a);
            a.type = kSpeakerArrStereo;
            a.numChannels = 2;
            for (int i = 0; i < 2; ++i)
                a.speakers[i].type = i;
            return a;
        }();
        if (inArr)
            *inArr = const_cast<VstSpeakerArrangement*> (&emptyArr);
        if (outArr)
            *outArr = const_cast<VstSpeakerArrangement*> (&stereoArr);
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
    {
        if (inst) inst->outputStage.reset();
        return 1;
    }

    case effStopProcess:
    {
        return 1;
    }

    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Parameter access (normalized 0..1)
// ---------------------------------------------------------------------------

/** The host's write of a parameter of the engine's table (normalised value): mirror, protection, engine. Also the write a Play proxy makes for the instance it stands for. */
static void writeRealParam (PluginInstance* inst, size_t index, float parameter)
{
    const synthyboy::ParamInfo* info = synthyboy::paramTable() + index;
    float plain = synthyboy::fromNormalised(*info, parameter);
    plain = synthyboy::constrainValue(*info, plain);

    // Avoid redundant engine updates (e.g. parameter 0 toggle at every prepare)
    float oldPlain = inst->paramMirror[index].load(std::memory_order_relaxed);
#if SYNTHYBOY_MPC_OVERLAY
    {
        // The MPC sets parameter 0 to 1.0 and then back to the value it had READ: the normalised round trip of a plain value is not exact (-12 dB comes back as -11.9999981), so the value written
        // back is the one the parameter had before the toggle: take that one exactly (it equals what the host read), the saved state stays exactly what it was.
        const float undo = inst->paramUndo[index].load (std::memory_order_relaxed);
        if (std::isfinite (undo) && undo != oldPlain && synthyboy::toNormalised (*info, undo) == parameter)
            plain = undo;
    }
#endif
    if (plain == oldPlain)
        return;
#if SYNTHYBOY_MPC_OVERLAY
    inst->paramUndo[index].store (oldPlain, std::memory_order_relaxed);
#endif

    inst->paramMirror[index].store(plain, std::memory_order_relaxed);

    // Disarm protection: any parameter write from the host disarms it
    inst->protectedProgram.store (-1, std::memory_order_relaxed);

#if SYNTHYBOY_MPC_OVERLAY
    inst->setProgramModified (true);
#endif

    // Call engine directly (IEngine::setParam is lock-free, any thread)
    if (inst->engine)
    {
        inst->engine->setParam(info->id, plain);
        // two writers of one parameter (the host's thread, the overlay's touch, the display thread, a state) can interleave the mirror and the engine: the last store of the mirror wins in both places
        for (int again = 0; again < 2; ++again)
        {
            const float mirrored = inst->paramMirror[index].load(std::memory_order_relaxed);
            if (mirrored == plain)
                break;
            plain = mirrored;
            inst->engine->setParam(info->id, plain);
        }
    }
}

static void pluginSetParameter(AEffect* e, VstInt32 index, float parameter)
{
    PluginInstance* inst = getInstance(e);
    if (!inst || index < 0)
        return;

#if SYNTHYBOY_MPC_OVERLAY
    {
        // the restore window (PluginInstance::restoreActive) and the log of play_log.h
        const bool restoring = inst->restoreActive();
        const bool logging = synthyboy_mpc::PlayLog::enabled();
        if (PluginInstance::isQParam (index))
        {
            const int q = index - PluginInstance::qBase();
            const synthyboy_mpc::QRouter::Write w = inst->qRouter.hostWrite (q, parameter, restoring, [inst] (int t) noexcept { return inst->qReadNorm (t); },
                                                                              [inst] (int t, float v) { writeRealParam (inst, static_cast<size_t> (t), v); },
                                                                              [] (int t) noexcept { return stepCountOf (synthyboy::paramTable()[t]); });
            if (logging)
            {
                static const char* const kOutcome[] = { "no target", "echo, nothing changed", "ignored (restore window)", "written" };
                synthyboy_mpc::PlayLog::line ("setParameter %d (Q %d, real %d) %.9g -> %s", static_cast<int> (index), q + 1, inst->qRouter.target (q), static_cast<double> (parameter), kOutcome[static_cast<int> (w)]);
            }
            return;
        }
        if (PluginInstance::isPlayParam (index))
        {
            namespace pl = synthyboy_mpc::play;
            if (PluginInstance::isPlaySelector (index))
            {
                const int g = index - PluginInstance::playBase();
                if (logging)
                    synthyboy_mpc::PlayLog::line ("setParameter %d (Play selector %d) %.9g -> %s", static_cast<int> (index), g, static_cast<double> (parameter), restoring ? "ignored (restore window)" : "taken");
                if (restoring)
                    return;
                // not a sound edit (no protection disarm): which instance the page shows is the player's view, not part of the sound
                inst->playSetSelector (g, pl::selectorInstance (g, parameter));
                return;
            }
            const int p = PluginInstance::playProxyOf (index);
            const int real = inst->playRealOf (p);
            if (real < 0)
                return;
            // the proxy IS the real parameter of the selected instance: the same write as the host's own write to that parameter, so the engine sees an ordinary change. The value the host
            // sends back after a report (or the same value again) changes nothing and disarms nothing.
            const float v = std::isfinite (parameter) ? std::min (std::max (parameter, 0.0f), 1.0f) : 0.0f;
            const bool echo = std::fabs (v - inst->playProxyValue (p)) <= PluginInstance::kEchoTolerance;
            if (logging)
                synthyboy_mpc::PlayLog::line ("setParameter %d (Play proxy %d, real %d) %.9g -> %s", static_cast<int> (index), p, real, static_cast<double> (parameter), echo ? "echo, nothing changed" : (restoring ? "ignored (restore window)" : "written"));
            if (echo || restoring)
                return;
            writeRealParam (inst, static_cast<size_t> (real), v);
            inst->playLast[p].store (v, std::memory_order_relaxed);   // the host knows this value: the poll does not report it back
            inst->playRealDue[p].store (true, std::memory_order_release);
            return;
        }
        if (static_cast<size_t> (index) < synthyboy::paramCount())
        {
            // a real parameter: in the restore window the value the plugin reports is an echo (the float round trip of a normalised value may differ in the last bit); anything else is an ordinary
            // edit and ends the window
            if (restoring)
            {
                const synthyboy::ParamInfo& info = synthyboy::paramTable()[index];
                const float reported = synthyboy::toNormalised (info, inst->paramMirror[index].load (std::memory_order_relaxed));
                if (std::isfinite (parameter) && std::fabs (parameter - reported) <= PluginInstance::kEchoTolerance)
                {
                    if (logging)
                        synthyboy_mpc::PlayLog::line ("setParameter %d (real) %.9g -> echo (restore window), nothing changed", static_cast<int> (index), static_cast<double> (parameter));
                    return;
                }
                inst->restoreClose();
            }
            if (logging)
                synthyboy_mpc::PlayLog::line ("setParameter %d (real) %.9g -> written", static_cast<int> (index), static_cast<double> (parameter));
        }
        else if (logging)
            synthyboy_mpc::PlayLog::line ("setParameter %d (display / pad pressure) %.9g", static_cast<int> (index), static_cast<double> (parameter));
    }
#endif
#if SYNTHYBOY_MPC_PAD_PRESSURE
    if (PluginInstance::isPadPressureParam (index))
    {
        // not a sound edit (no protection disarm): the pad setting is independent of the program
        const float v = std::isfinite (parameter) ? std::min (std::max (parameter, 0.0f), 1.0f) : 0.0f;
        if (index == PluginInstance::padPressureBase())
            inst->padPressure.store (v >= 0.5f, std::memory_order_relaxed);
        else
            inst->padPressureAmount.store (v * 100.0f, std::memory_order_relaxed);
        inst->updateEnginePadPressure();
        return;
    }
#endif
    if (static_cast<size_t>(index) >= synthyboy::paramCount())
        return;
    writeRealParam (inst, static_cast<size_t>(index), parameter);
}

static float pluginGetParameter(AEffect* e, VstInt32 index)
{
    PluginInstance* inst = getInstance(e);
    if (!inst || index < 0)
        return 0.0f;
#if SYNTHYBOY_MPC_OVERLAY
    synthyboy_mpc::PlayLog::countGet();
#endif

#if SYNTHYBOY_MPC_OVERLAY
    if (PluginInstance::isOverlayParam (index))
        return inst->overlayValue.load (std::memory_order_relaxed);
    if (PluginInstance::isQParam (index))
        return inst->qRouter.value (index - PluginInstance::qBase(), [inst] (int t) noexcept { return inst->qReadNorm (t); });
    if (PluginInstance::isPlayParam (index))
    {
        if (PluginInstance::isPlaySelector (index))
        {
            const int g = index - PluginInstance::playBase();
            return synthyboy_mpc::play::selectorNormalised (g, inst->playSel[g].load (std::memory_order_relaxed));
        }
        return inst->playProxyValue (PluginInstance::playProxyOf (index));
    }
#endif
#if SYNTHYBOY_MPC_LFO_LIVE
    if (inst->isLive (index))
#if SYNTHYBOY_MPC_SPECTRUM
        return inst->liveRead (index - static_cast<VstInt32> (synthyboy::paramCount()));
#else
        return inst->liveValue[index - static_cast<VstInt32> (synthyboy::paramCount())].load (std::memory_order_relaxed);
#endif
#endif
#if SYNTHYBOY_MPC_PAD_PRESSURE
    if (PluginInstance::isPadPressureParam (index))
        return index == PluginInstance::padPressureBase() ? (inst->padPressure.load (std::memory_order_relaxed) ? 1.0f : 0.0f)
                                                          : inst->padPressureAmount.load (std::memory_order_relaxed) * 0.01f;
#endif
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
    if (!inst || !inst->engine || !outputs)
    {
        // Zero output
        if (outputs && e)
        {
            for (int i = 0; i < e->numOutputs; ++i)
                if (outputs[i])
                    std::memset(outputs[i], 0, sizeof(float) * static_cast<size_t>(sampleFrames));
        }
        return;
    }

    // Return early for sampleFrames <= 0 (keep pending events for next block)
    if (sampleFrames <= 0)
        return;

#if SYNTHYBOY_MPC_CALLBACK_STATS
    // Measures the gap to the previous call and, at the end of the scope, the time spent in this one (atomics only, see callback_stats.h)
    struct StatsScope
    {
        synthyboy_mpc::CallbackStats& s;
        uint64_t t0;
        StatsScope (synthyboy_mpc::CallbackStats& stats, int frames, double rate) noexcept : s (stats), t0 (stats.begin (frames, rate)) {}
        ~StatsScope() { s.end (t0); }
    } statsScope (inst->callbackStats, sampleFrames, inst->sampleRate);
#endif

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
#if SYNTHYBOY_MPC_OVERLAY
            inst->overlayBpm.store (inst->transport.bpm, std::memory_order_relaxed);
#endif
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

    float* out0 = (e->numOutputs > 0) ? outputs[0] : nullptr;
    float* out1 = (e->numOutputs > 1) ? outputs[1] : nullptr;

    // Pass events directly to the engine (no slicing, no copy)
    inst->engine->process(inst->events.count > 0 ? inst->events.slots : nullptr,
                          static_cast<int>(inst->events.count),
                          out0, out1, sampleFrames);
    // Make-up gain and, when built with it, the peak limiter (output_stage.h): what leaves here is what the host gets, so the spectrum below shows the same signal.
    inst->outputStage.process (out0, out1, sampleFrames);
    if (out0 != nullptr)
        synthyboy_mpc::outputPeak().note (out0, out1, sampleFrames);   // the peak for the status file (output_peak.h): one pass, relaxed atomics
#if SYNTHYBOY_MPC_SPECTRUM
    if (out0 != nullptr)
        inst->spectrumRing.push (out0, out1, sampleFrames); // the only thing the audio thread does for the display: atomic stores into a preallocated ring
#endif

    // Clear remaining events
    inst->events.clear();

    synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kFirstAudio);   // the first block that went all the way (a relaxed load after the first)
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
        synthyboy_mpc::startupTimes().begin();   // the start-up record (startup_timing.h): time zero is the first entry here
        MPCKIT_LOG_START ("synthyboy");        // dev builds only: the developer log (kit/log.h), off until /tmp/synthyboy-log.conf switches a category on
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
#if SYNTHYBOY_MPC_LFO_LIVE
#if SYNTHYBOY_MPC_SPECTRUM
        effect->numParams += PluginInstance::kLiveParams;
#else
        effect->numParams += synthyboy_mpc::kLfoLiveCount;
#endif
#endif
#if SYNTHYBOY_MPC_PAD_PRESSURE
        effect->numParams += synthyboy_mpc::kPadPressureParams;
#endif
#if SYNTHYBOY_MPC_OVERLAY
        effect->numParams += 1 + synthyboy_mpc::play::kParams + synthyboy_mpc::QRouter::kCount; // "Overlay Instance" (after the pad pressure parameters), then the Play page's three selectors and the 24 proxies, then "Q 1" .. "Q 4"
#endif
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
#if SYNTHYBOY_MPC_CALLBACK_STATS
        inst->hostCallback = audioMaster;
        inst->audioMaster = audioMaster ? countingHostCallback : nullptr;
        audioMaster = inst->audioMaster.raw; // the identification call below is counted as well
#else
        inst->audioMaster = audioMaster;
#endif

        // Call the host callback to let the host identify itself
        if (audioMaster)
            audioMaster(effect, audioMasterVersion, 0, 0, nullptr, 0.0f);

#if SYNTHYBOY_MPC_LFO_LIVE
        try { synthyboy_mpc::StartupDetailScope t (synthyboy_mpc::StartupTimes::kStartLive); inst->startLive (effect); }
        catch (...) {} // no thread: the cursors stay idle
#endif
#if SYNTHYBOY_MPC_OVERLAY
        try { synthyboy_mpc::StartupDetailScope t (synthyboy_mpc::StartupTimes::kStartOverlay); inst->startOverlay(); synthyboy_mpc::PlayLog::line ("VSTPluginMain: instance %p attached to the overlay, slot %d", static_cast<void*> (inst), inst->overlaySlot.load()); }
        catch (const std::exception& ex) { synthyboy_mpc::PlayLog::line ("VSTPluginMain: startOverlay threw: %s", ex.what()); } // no overlay: the plugin works as without it
        catch (...) { synthyboy_mpc::PlayLog::line ("VSTPluginMain: startOverlay threw"); }
#endif
#if SYNTHYBOY_MPC_CALLBACK_STATS
        try { inst->startStats(); }
        catch (...) {} // no file: the statistics are only collected
#endif

        synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kMainReturned);
        MPCKIT_LOG ("startup", info, "VSTPluginMain returns");
        return effect;
    }
}
