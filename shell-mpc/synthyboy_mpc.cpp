// SynthyBoy - MPC VST2 shell (headless, no JUCE, no CLAP).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <array>
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
#include "synthyboy/helper_thread_hook.h"
#include "synthyboy/midi_events.h"
#include "synthyboy/mixer_routing.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wavetable_catalogue.h"
#include "synthyboy/wavetable_file.h"

#include "mpc_vst2_abi.h"
#include "thread_policy.h"
#if defined(__linux__)
#include "overlay/cpu_plan_linux.h"   // the plan of the cores (standalone; also used by the engine helper threads below)
#endif
#include "device_profile.h"
#include "output_peak.h"
#include "startup_timing.h"
#include "kit/log.h"
#include "kit/display_thread.h"   // the LFO / spectrum display thread of the build without the overlay uses it too
#include "host_strings.h"
#include "denormal_scope.h"
#include "event_inbox.h"
#include "output_stage.h"
#include "safe_file.h"
#include "plugin_paths.h"
#include "poll_guard.h"
#include "program_list.h"
#include "native_ui_params.h"

#ifndef SYNTHYBOY_VST2_VERSION
#error "SYNTHYBOY_VST2_VERSION must come from the build"
#endif

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
// SYNTHYBOY_MPC_PROGRAM_GROUPS (CMake option of the same name, a string): how the program list of the host is grouped (program_list.h, docs/MPC.md "Preset list of a plugin track"); the number is the
// GroupMode: 0 off (default, the flat list as ever), 1 prefix-short, 2 prefix-group, 3 headers, 4 vst-categories.
#ifndef SYNTHYBOY_MPC_PROGRAM_GROUPS
#define SYNTHYBOY_MPC_PROGRAM_GROUPS 0
#endif

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

// Make-up gain of the output stage in dB (docs/MPC.md "Output level"): synthyboy::kMakeupGainDb (core/include/synthyboy/output_stage.h, the one value of every shell) unless the CMake cache entry
// SYNTHYBOY_MPC_OUTPUT_GAIN_DB gives another. The look-ahead peak limiter behind it is the plugin setting LIMIT (default on; SYNTHYBOY_MPC_OUTPUT_LIMITER forces it for tests).
// Debug hooks (CMake option SYNTHYBOY_MPC_DEBUG_HOOKS, default OFF, never in the package; docs/MPC.md "Switches and files"): the /tmp files that only the host tests and the acceptance runs on the device use -
// /tmp/synthyboy-ui-fake-write, -q-fake-group, -q-fake-write, -overlay-q-step-travel, -overlay-fake-slot, -play-log (+ -dump, the .txt, the chunk copies), -select-program (+ .ack), and the file named by SYNTHYBOY_MPC_SCALAR_CURVE_FILE (a NOTE / VELO curve as the curve editor posts it). Without the option the plugin does not look at them at all.
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
namespace nativeui = synthyboy_mpc::nativeui;
#if !SYNTHYBOY_MPC_LFO_LIVE
#error "SYNTHYBOY_MPC_OVERLAY needs SYNTHYBOY_MPC_LFO_LIVE (the display thread reports the display parameter 'Overlay Instance' to the host)"
#endif
#include "kit/q_targets.h"
#include "kit/synthyboy_adapter.h"
#include "native_filter_controls.h"
#include "overlay/pages.h"
#include "overlay/service.h"
#include <exception>
#include "play_log.h"
#include "play_params.h"
#include "tab_view.h"
#include "q_router.h"
#include "macro_names.h"
#include "filter_curve_edits.h"
#include "q_step_rule.h"

namespace
{
using synthyboy_mpc::stepCountOf;   // the rule of the relative Q proxies: q_step_rule.h
} // namespace
#include "synthyboy/readout.h"
#endif

#ifndef SYNTHYBOY_MPC_OUTPUT_GAIN_DB
#define SYNTHYBOY_MPC_OUTPUT_GAIN_DB synthyboy::kMakeupGainDb
#endif

/** The most a preset file may have (a preset is a few KB; the content folder is on user writable media). */
static constexpr size_t kMaxPresetFileBytes = 4u * 1024u * 1024u;

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
#if MPCKIT_DEV_LOG
    inline static std::atomic<unsigned> nextLogId { 1 };
    const unsigned logId = nextLogId.fetch_add (1, std::memory_order_relaxed);
#endif
    // Core
    std::unique_ptr<synthyboy::IEngine> engine;
    synthyboy::StateContent content;
    EventBuffer events;                                         // the events of the block being rendered (audio thread only)
    synthyboy_mpc::EventInbox<synthyboy::Event> eventInbox;     // effProcessEvents pushes here (any thread), processReplacing takes them over at the start of its block

    // Transport
    double sampleRate = 44100.0;
    int lastPreparedSampleRate = 0;
    int lastPreparedBlockSize = 0;
    int maxBlockSize = 128;
    bool mainsOn = false;
    bool prepared = false; // true after the first successful prepare

    // Programs
    std::vector<synthyboy_mpc::ProgramEntry> programs;
    // The program list as the host sees it (built once at start, never changed afterwards: no lock): program number -> item (Init, header, preset). currentProgram is always a preset or Init.
    synthyboy_mpc::ProgramView view;
    std::atomic<bool> hostAskedCategories { false };   // the host has called effGetNumProgramCategories (mode vst-categories answers by category only after that)
    std::atomic<int> currentProgram {0};
    std::atomic<uint64_t> stateGeneration { 0 };
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
    // The program name the host reads never carries a "modified" mark (removed by design, docs/MPC.md "Q proxies"). The host drops its own "*" when it is told to read the program name again
    // (audioMasterUpdateDisplay): programNameRefreshDue is set by a chunk restore, and the display thread tells the host once.
    std::atomic<bool> programNameRefreshDue { false };
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

    // Output stage: the make-up gain of the MPC shell (SYNTHYBOY_MPC_OUTPUT_GAIN_DB, set once in the constructor) and the look-ahead peak limiter behind it (the setting LIMIT, below).
    // Only the audio thread calls process(); prepare() runs with the engine's prepare (effMainsChanged, the host is not processing then).
    synthyboy_mpc::SwitchableOutputStage outputStage;
    // All Sound Off requests of the host side (effStopProcess, effMainsChanged (0)): the audio thread puts one event at the front of its next block (processReplacing).
    std::atomic<bool> soundOffPending { false };
    std::atomic<unsigned> soundOffRequests { 0 };
    // The output limiter is a shell setting saved as "mpcOutputLimiter". Its native Master handle reflects the actual stage state.
    // SYNTHYBOY_MPC_OUTPUT_LIMITER forces the stage state for tests (output_stage.h).
    std::atomic<bool> outputLimiter { synthyboy_mpc::kOutputLimiterDefaultOn };
    void applyOutputLimiter() noexcept
    {
        const int forced = synthyboy_mpc::outputLimiterForced();
        outputStage.setLimiter (forced >= 0 ? forced == 1 : outputLimiter.load (std::memory_order_relaxed));
        latencyDirty.store (true, std::memory_order_release);   // the look-ahead is part of the reported latency only while the limiter is on
    }
    void setOutputLimiter (bool on) noexcept
    {
        outputLimiter.store (on, std::memory_order_relaxed);
        applyOutputLimiter();
    }
    std::atomic<float> tierTrimDb { 0.0f };
    void applyTierTrim() noexcept
    {
        const bool isLow = synthyboy_mpc::profileInForce().profile == synthyboy_mpc::profileLow();
        const float trim = isLow ? tierTrimDb.load (std::memory_order_relaxed) : 0.0f;
        outputStage.setGainDb (static_cast<float> (SYNTHYBOY_MPC_OUTPUT_GAIN_DB) + trim);
    }
    // The latency the host is told (AEffect::initialDelay): the engine's (Bode's frequency shifter) plus the output limiter's look-ahead while it is on, in samples at the prepared
    // rate. Any thread may mark it stale (the switch, the audio thread when the engine's latency moved); the next dispatcher call that is not effProcessEvents (so never on the
    // audio thread) writes the field and sends audioMasterIOChanged (reportLatency in dispatch).
    std::atomic<bool> latencyDirty { false };
    std::atomic<int> latencyReported { 0 };
    int latencyNow() const noexcept { return (engine ? engine->latencySamples() : 0) + outputStage.latencySamples(); }

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
#if SYNTHYBOY_MPC_DEBUG_HOOKS && SYNTHYBOY_MPC_OVERLAY
            if ((opcode == audioMasterAutomate || opcode == audioMasterUpdateDisplay) && synthyboy_mpc::PlayLog::enabled())
            {
                if (opcode == audioMasterAutomate)
                    synthyboy_mpc::PlayLog::line ("ui-send value instance=%p index=%d native_offset=%d q_slot=%d value=%.9g",
                        e->object, index, isNativeUiParam (index) ? index - nativeUiBase() : -1,
                        isQParam (index) ? index - qBase() + 1 : 0, static_cast<double> (opt));
                else
                    synthyboy_mpc::PlayLog::line ("ui-send metadata instance=%p", e->object);
            }
#endif
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
        uint64_t stateGeneration() const noexcept override { return inst.stateGeneration.load (std::memory_order_acquire); }
        bool live (synthyboy::LiveValues& out) const noexcept override
        {
            return inst.engine && inst.engine->copyReadout (out) && out.block != 0; // block 0: the engine wrote no values yet
        }
        bool takeClipFlag() noexcept override
        {
            return inst.engine ? inst.engine->takeClipFlag() : false;
        }
        void requestMeters() noexcept override { if (inst.engine) inst.engine->requestMeters(); }
        bool nativeMixerWidgetsAvailable() const noexcept override { return true; }
        void setNativeMixerMeter (int strip, float peak, float hold) noexcept override
        {
            if (strip >= 0 && strip < nativeui::kMixerStrips)
            {
                inst.nativeUiValues[nativeui::kMixerPeakBase + strip].store (nativeui::meterValue (peak), std::memory_order_relaxed);
                inst.nativeUiValues[nativeui::kMixerHoldBase + strip].store (nativeui::meterValue (hold), std::memory_order_relaxed);
            }
        }
        void setNativeMixerStereoMeter (int strip, const float* fields) noexcept override
        {
            if (strip < 0 || strip >= nativeui::kMixerStrips || fields == nullptr) return;
            for (int field = 0; field < nativeui::kMixerStereoFields; ++field)
                inst.nativeStereoMeters[field * nativeui::kMixerStrips + strip].store (
                    nativeui::meterValue (fields[field]), std::memory_order_relaxed);
        }
        void setNativeMixerPeekEnabled (bool enabled) noexcept override
        {
            if (inst.mixerPeek.setEnabled (enabled))
                inst.overlayReportDue.store (true, std::memory_order_release);
        }
        int takeNativeMixerPeek() noexcept override { return inst.mixerPeek.take(); }
        void observeHostWidgetGate (bool hidden) noexcept override
        {
            inst.hostMixerWidgetsHidden.store (hidden, std::memory_order_relaxed);
        }
        void setNativeMixerPeekView (int view) noexcept override
        {
            inst.nativeMixerPeekView.store (view, std::memory_order_release);
        }
        void setNativeMixerActiveTab (int tab) noexcept override
        {
            inst.nativeMixerActiveTab.store (std::clamp (tab, 0, nativeui::kMixerTabs - 1), std::memory_order_release);
        }
        void setNativeMixerStripVisuals (int strip, int soloVisual, int muteVisual) noexcept override
        {
            if (strip >= 0 && strip < nativeui::kMixerStrips)
            {
                inst.nativeMixerSoloVisual[strip].store (std::clamp (soloVisual, 0, nativeui::mixer::kLastVisualState), std::memory_order_release);
                inst.nativeMixerMuteVisual[strip].store (std::clamp (muteVisual, 0, nativeui::mixer::kLastVisualState), std::memory_order_release);
            }
        }
        void setMixerSolo (int strip, bool solo) noexcept override
        {
            if (inst.engine != nullptr)
                inst.engine->setMixerSolo (strip, solo);
        }
        void setMixerMute (int strip, bool mute) noexcept override
        {
            if (inst.engine != nullptr)
                inst.engine->setMixerMute (strip, mute);
        }
        bool mixerSolo (int strip) const noexcept override
        {
            return inst.engine != nullptr && inst.engine->mixerSolo (strip);
        }
        bool mixerMute (int strip) const noexcept override
        {
            return inst.engine != nullptr && inst.engine->mixerMute (strip);
        }
        void setNativeEnvSelector (const int* frames) noexcept override
        {
            uint32_t packed = 0;
            for (int i = 0; i < nativeui::kEnvSelectorCells; ++i)
            {
                const auto frame = static_cast<uint32_t> (std::clamp (frames[i], 0, nativeui::kEnvSelectorLastFrame));
                packed |= frame << (i * nativeui::kEnvSelectorFrameBits);
            }
            inst.nativeEnvSelector.store (packed, std::memory_order_relaxed);
        }
        void setNativeEnvEditor (bool active, const nativeui::FxCell* fields) noexcept override { inst.nativeEnvEditor.publish (active ? 1 : 0, fields); }
        void setNativeEnvTargetCount (int count) noexcept override
        {
            if (inst.nativeEnvTargetCount.load (std::memory_order_relaxed) != count)
                inst.nativeEnvTargetCount.store (count, std::memory_order_relaxed);
        }
        bool takeNativeEnvField (int field, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return inst.nativeEnvEditor.take (field, expected, value);
        }
        bool nativeLfoControls() const noexcept override { return true; }
        bool nativeLfoShapeEditor() const noexcept override { return true; }
        void setNativeLfoEditor (const nativeui::lfoeditor::State& state) noexcept override { inst.nativeLfoEditor.publish (state); }
        bool takeNativeLfoEditor (const nativeui::lfoeditor::State& state, int field, float& value) noexcept override
        {
            const bool taken = inst.nativeLfoEditor.take (state, field, value);
            if (taken && nativeui::lfoeditor::kFields[field].type == nativeui::lfoeditor::kAction)
                inst.nativeLfoEditorAcks[field] = true;
            return taken;
        }
        bool nativeLfoShapeDetails() const noexcept override { return true; }
        void setNativeLfoDetail (const nativeui::lfodetail::State& state) noexcept override { inst.nativeLfoDetail.publish (state); }
        bool takeNativeLfoDetail (const nativeui::lfodetail::State& state, int field, float& value) noexcept override
        {
            namespace n = nativeui::lfodetail;
            const bool taken = inst.nativeLfoDetail.take (state, field, value);
            if (taken && field != n::Target && field != n::Amount)
                inst.nativeLfoDetailAcks[field] = true;
            return taken;
        }
        bool nativePlayEnvLfoControls() const noexcept override { return true; }
        uint64_t nativePlayEnvLfoRevision() const noexcept override { return inst.playEnvLfoRevision.load (std::memory_order_acquire); }
        void setNativePlayEnvLfoPopup (int field) noexcept override
        {
            std::lock_guard<std::mutex> guard (inst.playPresentationMutex);
            inst.nativePlayEnvLfo.setPopup (field);
        }
        void setNativePlayEnvLfo (const nativeui::playenvlfo::State& state) noexcept override
        {
            std::lock_guard<std::mutex> guard (inst.playPresentationMutex);
            if (state.context.view != nativeui::playenvlfo::Hidden && (state.context.preset != stateGeneration() || state.context.selection != nativePlayEnvLfoRevision()))
                return;
            inst.nativePlayEnvLfo.publish (state);
        }
        void suspendNativePlayEnvLfo() noexcept override
        {
            std::lock_guard<std::mutex> guard (inst.playPresentationMutex);
            inst.nativePlayEnvLfo.suspend();
        }
        bool takeNativePlayEnvLfo (const nativeui::playenvlfo::State& state, int field, float& value) noexcept override
        {
            std::lock_guard<std::mutex> guard (inst.playPresentationMutex);
            return inst.nativePlayEnvLfo.take (state, field, value);
        }
        void setNativeLfoState (const nativeui::lfo::ViewState& state) noexcept override { inst.nativeLfo.publish (state); }
        bool takeNativeLfo (const nativeui::lfo::ViewState& wanted, int field, float& value) noexcept override
        {
            return inst.nativeLfo.take (wanted, field, value);
        }
        void setNativeFxCells (int layout, const nativeui::FxCell* cells) noexcept override { inst.nativeFxCells.publish (layout, cells); }
        bool takeNativeFxCell (int slot, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return inst.nativeFxCells.take (slot, expected, value);
        }
        void setNativeFxHeader (const nativeui::fxheader::State& state) noexcept override
        {
            inst.nativeFxHeader.publish (state, inst.stateGeneration.load (std::memory_order_acquire));
        }
        void setNativeFxRackPopup (bool active) noexcept override { inst.nativeFxHeader.setRackPopup (active); }
        bool takeNativeFxHeader (int field, const nativeui::fxheader::State& state, float& value) noexcept override
        {
            return inst.nativeFxHeader.take (field, state, value);
        }
        void setNativeFxChain (const nativeui::fxchain::State& state) noexcept override
        {
            inst.nativeFxChain.publish (state, inst.stateGeneration.load (std::memory_order_acquire));
        }
        void setNativeFxAddPopup (bool active) noexcept override { inst.nativeFxChain.setMenuOpen (active); }
        bool takeNativeFxChain (int field, const nativeui::fxchain::State& state, float& value) noexcept override
        {
            return inst.nativeFxChain.take (field, state, value);
        }
        void setNativeFilterControls (int view, const nativeui::FxCell* cells, const float* fields, const int* indicators) noexcept override
        {
            for (int i = 0; i < 2 * nativeui::kFilterCurveFields; ++i)
                inst.nativeFilterCurve[i].store (fields != nullptr ? fields[i] : 0.0f, std::memory_order_relaxed);
            for (int i = 0; i < nativeui::kFilterIndicators; ++i)
                inst.nativeFilterIndicators[i].store (indicators != nullptr ? indicators[i] : 0, std::memory_order_relaxed);
            inst.nativeFilter.publish (view, cells);
        }
        bool takeNativeFilterSlot (int slot, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return inst.nativeFilter.take (slot, expected, value);
        }
        bool nativePzToolbarSupported() const noexcept override { return true; }
        void setNativePzToolbar (int filter, int availability) noexcept override
        {
            const auto preset = inst.stateGeneration.load (std::memory_order_acquire);
            if (preset != inst.nativePzToolbarPreset.load (std::memory_order_relaxed))
                inst.nativePzActions.invalidate();
            nativeui::FxCell bindings[2] {};
            if (filter >= 1 && filter <= 2)
                bindings[filter - 1] = {filter, 0, nativeui::kFxDial};
            inst.nativePzActions.publish (filter, bindings);
            inst.nativePzToolbarPreset.store (preset, std::memory_order_release);
            inst.nativePzPresentation.store (filter | (availability << 2), std::memory_order_release);
        }
        bool takeNativePzAction (int filter, float& value) noexcept override
        {
            if (inst.nativePzToolbarPreset.load (std::memory_order_acquire) != inst.stateGeneration.load (std::memory_order_acquire)
                || ! inst.nativePzActions.take (filter - 1, {filter, 0, nativeui::kFxDial}, value))
                return false;
            inst.nativePzAcks[filter - 1] = true;
            return true;
        }
        void setNativeOscState (const nativeui::osc::ViewState& state) noexcept override { inst.nativeOsc.publish (state); }
        bool takeNativeOsc (const nativeui::osc::ViewState& state, int field, int& choice) noexcept override
        {
            return inst.nativeOsc.take (state, field, choice);
        }
        bool nativeOscSettingsSupported() const noexcept override { return true; }
        void setNativeOscSettings (int view, const nativeui::FxCell* fields) noexcept override
        {
            inst.nativeOscSettings.publish (view, fields);
        }
        bool takeNativeOscSetting (int field, const nativeui::FxCell& expected, float& value) noexcept override
        {
            if (! inst.nativeOscSettings.take (field, expected, value))
                return false;
            if (nativeui::osc::settingsAction (expected.kind))
                inst.nativeOscSettingAcks[field] = true;
            return true;
        }
        void setNativeOscAbcState (int warpMask, const char* label) override
        {
            inst.nativeOscWarpMask.store (warpMask, std::memory_order_relaxed);
            const auto old = std::atomic_load (&inst.nativeOscSourceLabel);
            if ((! old && label[0] == '\0') || (old && *old == label))
                return;
            std::atomic_store (&inst.nativeOscSourceLabel, std::make_shared<const std::string> (label));
            inst.nativeOscSourceLabelChanged = true;
        }
        bool nativeOscDetailSupported() const noexcept override { return true; }
        void setNativeOscDetail (const nativeui::oscdetail::State& state) noexcept override { inst.nativeOscDetail.publish (state); }
        void setNativeOscDetailMenu (int menu) noexcept override { inst.nativeOscDetail.setMenu (menu); }
        bool takeNativeOscDetail (const nativeui::oscdetail::State& state, int field, float& value) noexcept override
        {
            if (! inst.nativeOscDetail.take (state, field, value))
                return false;
            if (field >= nativeui::oscdetail::kCells || state.fields[field].kind == nativeui::oscdetail::Action)
                inst.nativeOscDetailAcks[field] = true;
            return true;
        }
        void setNativeMasterView (nativeui::master::View view) noexcept override
        {
            publishMasterView (view, view);
        }
        void leaveNativeMasterView (nativeui::master::View returnPanel) noexcept override
        {
            // The host owns tab visibility. Keep its next panel ready while disarming hidden inputs.
            publishMasterView (nativeui::master::Hidden, returnPanel);
        }
        void publishMasterView (nativeui::master::View view, nativeui::master::View skinPanel) noexcept
        {
            const auto preset = inst.stateGeneration.load (std::memory_order_acquire);
            if (preset != inst.nativeMasterPanelPreset.load (std::memory_order_relaxed))
                inst.nativeMasterPanel.invalidate();
            if (view != nativeui::master::Tuning || preset != inst.nativeMasterPanelPreset.load (std::memory_order_relaxed))
                inst.scaleInput.store (ScaleInputHidden);
            if (view == nativeui::master::Tuning)
            {
                int hidden = ScaleInputHidden;
                inst.scaleInput.compare_exchange_strong (hidden, ScaleInputOff);
            }
            const auto cell = masterPanelCell (view);
            inst.nativeMasterPanel.publish (view, &cell);
            inst.nativeMasterPanelPreset.store (preset, std::memory_order_release);
            inst.nativeMasterSkinPanel.store (skinPanel, std::memory_order_relaxed);
            inst.nativeMasterView.store (view, std::memory_order_relaxed);
            if (view != nativeui::master::Main)
                inst.nativeMasterClearPending.store (false, std::memory_order_relaxed);
        }
        bool masterScaleInputActive() const noexcept override { return inst.scaleInput.load() >= ScaleInputArmed; }
        int takeMasterScaleRoot() noexcept override
        {
            int root = inst.scaleInput.load();
            if (root >= 0 && inst.nativeMasterPanelPreset.load() == inst.stateGeneration.load()
                && inst.scaleInput.compare_exchange_strong (root, ScaleInputArmed))
                return root;
            return -1;
        }
        bool nativeMasterPanelSupported() const noexcept override { return true; }
        bool takeNativeMasterPanel (nativeui::master::View view, float& value) noexcept override
        {
            return inst.nativeMasterPanelPreset.load (std::memory_order_acquire) == inst.stateGeneration.load (std::memory_order_acquire)
                && inst.nativeMasterPanel.take (0, masterPanelCell (view), value);
        }
        bool takeNativeMasterClipClear() noexcept override
        {
            return inst.nativeMasterClearPending.exchange (false, std::memory_order_relaxed);
        }
        void setNativeMasterOutput (const nativeui::master::OutputReadout& state) noexcept override
        {
            namespace m = nativeui::master;
            auto* const value = inst.nativeMasterValues;
            const int polyphony = std::clamp (state.polyphony, 1, m::kVoiceCountLimit);
            const int voices = std::clamp (state.activeVoices, 0, m::kVoiceCountLimit);
            inst.nativeMasterPolyphony.store (polyphony, std::memory_order_relaxed);
            value[m::PeakL].store (nativeui::meterValue (state.peakL), std::memory_order_relaxed);
            value[m::PeakR].store (nativeui::meterValue (state.peakR), std::memory_order_relaxed);
            value[m::HoldL].store (nativeui::meterValue (state.holdL), std::memory_order_relaxed);
            value[m::HoldR].store (nativeui::meterValue (state.holdR), std::memory_order_relaxed);
            value[m::VoiceRatio].store (nativeui::meterValue (static_cast<float> (voices) / polyphony), std::memory_order_relaxed);
            value[m::VoiceCount].store (static_cast<float> (voices) / m::kVoiceCountLimit, std::memory_order_relaxed);
            value[m::Clip].store (state.clipped ? 1.0f : 0.0f, std::memory_order_relaxed);
        }
        bool nativeMacroTargetsSupported() const noexcept override { return true; }
        void publishNativeMacroTargets (bool visible) noexcept override
        {
            const auto preset = stateGeneration();
            if (inst.nativeMacroTargetPreset.load (std::memory_order_relaxed) != preset)
                inst.nativeMacroTargets.invalidate();
            nativeui::FxCell cells[nativeui::kMacroTargetCount];
            for (int i = 0; i < nativeui::kMacroTargetCount; ++i)
                cells[i] = { i, i, nativeui::kFxDial };
            inst.nativeMacroTargets.publish (visible ? 1 : 0, cells);
            inst.nativeMacroTargetPreset.store (preset, std::memory_order_release);
        }
        int takeNativeMacroTarget() noexcept override
        {
            if (inst.nativeMacroTargetPreset.load (std::memory_order_acquire) != stateGeneration())
                return -1;
            for (int i = 0; i < nativeui::kMacroTargetCount; ++i)
            {
                float value;
                if (inst.nativeMacroTargets.take (i, { i, i, nativeui::kFxDial }, value))
                {
                    inst.nativeMacroTargetAck = i;
                    return i;
                }
            }
            return -1;
        }
        bool nativeExpandedPlaySupported() const noexcept override { return true; }
        void publishNativeExpandedPlay (const nativeui::expandedplay::State& state) noexcept override
        {
            inst.nativeExpandedPlay.publish (state);
        }
        bool takeNativeExpandedPlayClose (const nativeui::expandedplay::State& state) noexcept override
        {
            if (! inst.nativeExpandedPlay.take (state))
                return false;
            inst.nativeExpandedPlayAck = nativeui::expandedplay::actionOf (state.frame);
            return true;
        }
        bool nativeModTargetsSupported() const noexcept override { return true; }
        void publishNativeModTargets (const nativeui::targets::State& state) noexcept override
        {
            inst.nativeModTargets.publish (state);
        }
        void hideNativeModTargets (const void* presenter) noexcept override { inst.nativeModTargets.hide (presenter); }
        bool takeNativeModTarget (const nativeui::targets::State& state, int slot, float& value) noexcept override
        {
            if (! inst.nativeModTargets.take (state, slot, value))
                return false;
            inst.nativeModTargetAcks[slot] = true;
            return true;
        }
        void setNativeModMatrix (int view, const nativeui::FxCell* fields, const int* rows) noexcept override
        {
            for (int row = 0; row < nativeui::mod::kRows; ++row)
                inst.nativeModRows[row].store (rows != nullptr ? rows[row] : 0, std::memory_order_relaxed);
            inst.nativeMod.publish (view, fields);
        }
        bool takeNativeModField (int field, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return inst.nativeMod.take (field, expected, value);
        }
        void setNativeModNavigation (int mode, int source, int destination, const nativeui::FxCell* fields) noexcept override
        {
            inst.nativeModSourceGroup.store (source, std::memory_order_relaxed);
            inst.nativeModDestinationGroup.store (destination, std::memory_order_relaxed);
            inst.nativeModNavigation.publish (mode, fields);
        }
        bool takeNativeModNavigation (int field, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return inst.nativeModNavigation.take (field, expected, value);
        }
        void setNativeModDetailNavigation (const nativeui::FxCell* fields) noexcept override
        {
            inst.nativeModDetailNavigation.publish (fields != nullptr ? 1 : 0, fields);
        }
        bool takeNativeModDetailNavigation (int field, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return inst.nativeModDetailNavigation.take (field, expected, value);
        }
        void setNativeModBoard (int gate, const nativeui::FxCell* fields) noexcept override
        {
            if (inst.nativeModBoardPreset.load (std::memory_order_relaxed) != stateGeneration())
                inst.nativeModBoard.invalidate();
            inst.nativeModBoard.publish (gate, fields);
            inst.nativeModBoardPreset.store (stateGeneration(), std::memory_order_release);
        }
        bool takeNativeModBoard (int field, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return inst.nativeModBoardPreset.load (std::memory_order_acquire) == stateGeneration()
                && inst.nativeModBoard.take (field, expected, value);
        }
        bool nativeVoiceSequenceControls() const noexcept override { return true; }
        void setNativeVoiceState (const nativeui::voice::ViewState& state) noexcept override { inst.nativeVoice.publish (state); }
        bool takeNativeVoice (const nativeui::voice::ViewState& state, int field, float& value) noexcept override
        {
            return inst.nativeVoice.take (state, field, value);
        }
        double bpm() const noexcept override { return inst.overlayBpm.load (std::memory_order_relaxed); }
        bool tablesLoading() const noexcept override { return (inst.engine && inst.engine->contentPending()) || inst.wtLoading.load (std::memory_order_relaxed); }   // a built-in table in the making, or a wavetable file the display thread is loading
        bool nativePlayOscControls() const noexcept override { return true; }
        uint64_t playOscSelectionRevision() const noexcept override { return inst.playOscSelectionRev.load (std::memory_order_acquire); }
        void setNativePlayOsc (const nativeui::playoscui::State& state, const char* sourceName) noexcept override
        {
            std::lock_guard<std::mutex> guard (inst.playPresentationMutex);
            if (state.source != 0 && (state.preset != stateGeneration() || state.selection != playOscSelectionRevision()))
                return;
            inst.nativePlayOsc.publish (state);
            if (state.source == 0)
                return;   // Hidden controls have no new caption to publish.
            const auto old = std::atomic_load (&inst.nativePlayOscSourceName);
            if ((old == nullptr || *old != sourceName) && (old != nullptr || sourceName[0] != '\0'))
            {
                std::atomic_store (&inst.nativePlayOscSourceName, sourceName[0] != '\0'
                                   ? std::make_shared<const std::string> (sourceName) : std::shared_ptr<const std::string> {});
                inst.nativePlayOscLabelChanged = true;
            }
        }
        void suspendNativePlayOsc() noexcept override
        {
            std::lock_guard<std::mutex> guard (inst.playPresentationMutex);
            inst.nativePlayOsc.suspend();
        }
        bool takeNativePlayOsc (const nativeui::playoscui::State& state, int field, float& value) noexcept override
        {
            std::lock_guard<std::mutex> guard (inst.playPresentationMutex);
            return inst.nativePlayOsc.take (state, field, value);
        }
        void setNativePlayOscPopup (int field) noexcept override
        {
            std::lock_guard<std::mutex> guard (inst.playPresentationMutex);
            inst.nativePlayOsc.setPopup (field);
        }
        int playSelection (int group) const noexcept override { return group >= 0 && group < synthyboy_mpc::play::kGroups ? inst.playSel[group].load (std::memory_order_relaxed) : 1; }
        void setQGroup (const int* targets, int n) noexcept override { inst.qBank.setGroup (targets, n); }
        uint64_t qBindingRevision() const noexcept override { return inst.qBindingState.load (std::memory_order_acquire); }
        void finishQBindingUpdate (uint64_t revision) noexcept override
        {
            if ((revision & kQRestoreCountMask) != 0)
                return;
            auto ready = inst.qReadyRevision.load (std::memory_order_relaxed);
            while (ready != revision && revision == qBindingRevision()
                   && ! inst.qReadyRevision.compare_exchange_weak (ready, revision, std::memory_order_release, std::memory_order_relaxed))
            {}
        }
        void writeParam (int tableIndex, float normalised) noexcept override { inst.uiWrite (tableIndex, normalised); }
        int lastPlayed (int which) const noexcept override { return which == 1 ? inst.lastNoteOnKey.load (std::memory_order_relaxed) : (which == 0 ? inst.lastNoteOnVelocity.load (std::memory_order_relaxed) : -1); }
        int tabView (int what) const noexcept override { return what >= 0 && what < synthyboy_mpc::tabview::kFields ? inst.tabViewState[what].load (std::memory_order_relaxed) : 0; }
        void setTabView (int what, int value) noexcept override
        {
            if (what < 0 || what >= synthyboy_mpc::tabview::kFields)
                return;
            const int next = std::clamp (value, 0, synthyboy_mpc::tabview::kMax[what]);
            if (inst.tabViewState[what].exchange (next, std::memory_order_relaxed) != next && what == kTabViewRnd)
                inst.playEnvLfoRevision.fetch_add (1, std::memory_order_release);
        }
        void setPlaySelection (int group, int instance) noexcept override { if (group >= 0 && group < synthyboy_mpc::play::kGroups) inst.playSetSelector (group, instance); }
        int playLastSelector() const noexcept override { return inst.playLastSel.load (std::memory_order_relaxed); }
        void setUiFullOpen (bool open) noexcept override
        {
            if (inst.uiFullOpen.exchange (open, std::memory_order_relaxed) != open)
                inst.overlayReportDue.store (true, std::memory_order_release);   // the display thread reports the frame at once
        }
        std::shared_ptr<const synthyboy::StateContent> content() const override { const auto p = std::atomic_load (&inst.overlayContent); return p ? p->state : nullptr; }
        synthyboy_mpc::overlay::OscContentSnapshot oscContent() const override
        {
            const auto p = std::atomic_load (&inst.overlayContent);
            return p ? *p : synthyboy_mpc::overlay::OscContentSnapshot {};
        }
        const synthyboy::IContentStore* contentStore() const noexcept override { return inst.contentStore.get(); }
        void contentRoots (std::vector<ContentRoot>& out) const override { inst.contentRootsOf (out); }
        void requestContent (int osc, int kind, const std::string& path) override { inst.requestContentLoad (osc, kind, path); }
        int contentLoad (std::string* message) const override { return inst.contentLoadState (message); }
        void wavetableSources (synthyboy::CatalogueSources& out) const override { inst.wavetableSourcesOf (out); }
        void requestWavetable (int osc, const synthyboy::ContentFileRef& ref) override { inst.requestWavetableLoad (osc, ref); }
        int takeWavetableSteps (int osc) noexcept override { return osc >= 1 && osc <= 3 ? inst.wtStepMail[osc - 1].exchange (0, std::memory_order_relaxed) : 0; }
        void setLfoShape (int lfo, const synthyboy::ContentLfoShape& shape) override { inst.requestLfoShape (lfo, shape); }
        void setScalarCurve (int which, const std::vector<synthyboy::ContentPoint>& points) override { inst.requestScalarCurve (which, points); }
        void setFilterCurve (int filter, const std::vector<synthyboy::ContentFilterCurvePoint>& points, uint64_t generation) override
        {
            inst.filterCurveEdits.post (filter, points, generation);
        }
        void setMacroName (int macro, const std::string& name, uint64_t generation) override
        {
            inst.macroNameQueue.post (macro, name, generation);
        }
        bool outputLimiter (bool& on) const noexcept override
        {
            on = inst.outputStage.limiter();   // what the stage runs (the setting, or the state the environment forces)
            return true;
        }
        void setOutputLimiter (bool on) noexcept override
        {
            inst.setOutputLimiter (on);
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
        unsigned missingContent() const noexcept override { return inst.engine != nullptr ? inst.engine->missingContent() : 0u; }
        void voiceStatus (std::string& out) const override
        {
            synthyboy::VoiceStats s;
            if (inst.engine == nullptr || ! inst.engine->voiceStats (s))
                return;
            char b[480];
            std::snprintf (b, sizeof b, "voices_active: %d\nvoices_released: %d\nvoices_sustaining: %d\nheld_keys: %d\nheld_lowest: %d\nheld_highest: %d\norphan_voices: %d\nnote_ons: %u\nnote_offs: %u\nnote_offs_unmatched: %u\nvoices_stolen: %u\nsustain_pedal: %d\nsound_off_requests: %u\nmissing_content: %u\n",
                           s.active, s.released, s.sustaining, s.heldKeys, s.lowestHeld, s.highestHeld, s.orphans, s.noteOns, s.noteOffs, s.unmatchedNoteOffs, s.stolen, s.sustainPedal ? 1 : 0,
                           inst.soundOffRequests.load (std::memory_order_relaxed), inst.engine->missingContent());
            out += b;
        }
        void setQVirtual (int id, float normalised) noexcept override { inst.qBank.setVirtual (id, normalised); }
        bool takeQVirtual (int id, float& normalised) noexcept override { return inst.qBindingsReady() && inst.qBank.takeVirtual (id, normalised); }
        unsigned long qWriteCount() const noexcept override { return inst.qBank.writeCount(); }
        void qStatus (std::string& out) const override { inst.qBank.describe (out); }
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
    std::shared_ptr<const synthyboy_mpc::overlay::OscContentSnapshot> overlayContent;   // a copy of `content` for the overlay thread (std::atomic_load / atomic_store): custom LFO shapes, user wavetables
    void publishOverlayContent()
    {
        const synthyboy_mpc::overlay::OscContentSnapshot snapshot { std::make_shared<const synthyboy::StateContent> (content),
                                                                  engine ? engine->preparedOscContent() : nullptr };
        std::atomic_store (&overlayContent, std::make_shared<const synthyboy_mpc::overlay::OscContentSnapshot> (snapshot));
        for (int o = 0; o < 3; ++o)   // the Q knob of a table stepper reads these (qWrite): a file table is loaded for the oscillator
        {
            bool has = false;
            for (const synthyboy::ContentWavetable& w : content.wavetables)
                has = has || w.osc == o + 1;
            wtFileLoaded[o].store (has, std::memory_order_relaxed);
        }
    }

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

    // ---- the wavetable browser (overlay/wavetable_browser.h, docs/MPC.md "Wavetable browser"): the overlay thread posts the wavetable FILE to load (the latest request wins), the display thread reads, checks and hashes it (off the audio
    // thread), puts the reference into the content, gives the content to the engine (the new table is built there; the old one plays until it is swapped in) and only then sets oscN.wavetable to "Preset table".
    bool wtReqPending = false;                                 // under contentReqMutex
    int wtReqOsc = 0;
    synthyboy::ContentFileRef wtReqRef;
    std::atomic<bool> wtLoading { false };                     // a wavetable file is waiting to be loaded or is being loaded: the amber bar of the pages
    std::atomic<int> wtStepMail[3] {};                         // the Q knob of an oscillator whose table is a file: steps (-1 / +1, summed) the overlay turns into "the previous / next file of the category"
    std::atomic<bool> wtFileLoaded[3] {};                      // the content has a wavetable file for oscillator 1 .. 3 (set by publishOverlayContent)
    static int keyIndex (const char* key) noexcept { const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key); return p != nullptr ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    int wtParam[3] = { keyIndex ("osc1.wavetable"), keyIndex ("osc2.wavetable"), keyIndex ("osc3.wavetable") };
    /** The Q knob's write of a table index. A stepped Q knob on oscN.wavetable while the oscillator plays a FILE table (the parameter is "Preset table" and the content has the file) does not move the parameter
        to the neighbouring built-in table: it leaves one step for the overlay, which loads the previous / next file of the category (wavetable_catalogue.h). Atomics only (the host's thread or the audio thread). */
    void qWrite (int t, float normalised) noexcept
    {
        for (int o = 0; o < 3; ++o)
            if (t == wtParam[o] && t >= 0 && wtFileLoaded[o].load (std::memory_order_relaxed))
            {
                const synthyboy::ParamInfo& info = synthyboy::paramTable()[t];
                const float plain = synthyboy::constrainValue (info, synthyboy::fromNormalised (info, normalised));
                const float old = paramMirror[t].load (std::memory_order_relaxed);
                if (static_cast<int> (std::lround (old)) == synthyboy::kWavetablePresetTable)
                {
                    if (plain != old)
                        wtStepMail[o].fetch_add (plain > old ? 1 : -1, std::memory_order_relaxed);
                    return;
                }
            }
        writeRealParam (this, static_cast<size_t> (t), normalised);
    }

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
    /** Where the wavetable catalogue looks: the installed packs (Content/packs) and the roots that may hold SynthyBoy/Wavetables (the SD card, the USB drives, the plugin's Content folder). */
    void wavetableSourcesOf (synthyboy::CatalogueSources& out) const
    {
        out = synthyboy::CatalogueSources {};
        if (! contentDirPath.empty())
            out.packDirs.push_back (contentDirPath + "/packs");
        std::vector<synthyboy_mpc::overlay::IInstanceView::ContentRoot> roots;
        contentRootsOf (roots);
        for (const auto& r : roots)
            out.userRoots.push_back (r.path);
    }
    void requestWavetableLoad (int osc, const synthyboy::ContentFileRef& ref)
    {
        std::lock_guard<std::mutex> g (contentReqMutex);
        wtReqPending = true;
        wtReqOsc = osc;
        wtReqRef = ref;
        wtLoading.store (true, std::memory_order_relaxed);
        contentLoadStatus.store (1, std::memory_order_relaxed);
    }
    /** The drives (and the SD card) are pack roots of the content store for the files of SynthyBoy/Wavetables: a reference "SynthyBoy/Wavetables" + "Bass/x.wav" is read from whichever root has it. Added once each, under
        contentMutex (the store is read under it). The plugin's own Content folder is the store's first root already. */
    void ensureUserWavetableRoots()
    {
        if (! contentStore)
            return;
        std::vector<synthyboy_mpc::overlay::IInstanceView::ContentRoot> roots;
        contentRootsOf (roots);
        std::lock_guard<std::mutex> g (contentMutex);
        for (const auto& r : roots)
            if (r.path != contentDirPath && contentPackRoots.insert (r.path).second)
                contentStore->addPackRoot (r.path);
    }
    /** State loading: a content section that names a file of the user folders needs the roots before the engine reads it. Not under contentMutex. */
    void ensureUserWavetableRootsFor (const synthyboy::StateContent& c)
    {
        for (const synthyboy::ContentWavetable& w : c.wavetables)
            if (w.ref.pack == synthyboy::kUserWavetablePack)
            {
                ensureUserWavetableRoots();
                return;
            }
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
    // ---- the NOTE / VELO curves the plugin-drawn curve editor writes (overlay/scalar_curve_ui.h): the same hand-over as the LFO shapes (the overlay thread posts, the latest per source wins; the display thread puts the points into the
    // content under `contentMutex`, gives it to the engine and to the overlay's copy; a curve the content validation would refuse is not applied and the old one stays).
    std::mutex scalarCurveMutex;
    std::vector<synthyboy::ContentPoint> scalarCurveReq[2];
    bool scalarCurveDue[2] = {};
    void requestScalarCurve (int which, const std::vector<synthyboy::ContentPoint>& points)
    {
        if (which < 0 || which > 1)
            return;
        std::lock_guard<std::mutex> g (scalarCurveMutex);
        scalarCurveReq[which] = points;
        scalarCurveDue[which] = true;
    }
#if SYNTHYBOY_MPC_DEBUG_HOOKS
    /** Debug hook (SYNTHYBOY_MPC_DEBUG_HOOKS only; docs/MPC.md "Debug hooks"): while the file named by SYNTHYBOY_MPC_SCALAR_CURVE_FILE exists, its first line "which x,y,power x,y,power ..." (which 0 velocity, 1 note; no points:
        the identity) is posted exactly as the curve editor of the Filter page does it (requestScalarCurve), when the line changes. The host test (shell-mpc/tests/scalar_curve_chunk_host.cpp) stands in for the page with it
        and proves the round trip request -> display thread -> validateContent -> content / engine -> chunk in a live plugin. Display thread. */
    std::string scalarCurveFakeLast;
    void scalarCurveFakeFile()
    {
        static const std::string path = [] { const char* p = std::getenv ("SYNTHYBOY_MPC_SCALAR_CURVE_FILE"); return std::string (p != nullptr ? p : ""); }();
        if (path.empty())
            return;
        std::ifstream f (path);
        std::string line;
        if (! f || ! std::getline (f, line) || line == scalarCurveFakeLast)
            return;
        scalarCurveFakeLast = line;
        const int which = std::atoi (line.c_str());
        std::vector<synthyboy::ContentPoint> pts;
        size_t at = line.find (' ');
        while (at != std::string::npos && at + 1 < line.size())
        {
            float x = 0.0f, y = 0.0f, pw = 0.0f;
            if (std::sscanf (line.c_str() + at + 1, "%f,%f,%f", &x, &y, &pw) == 3)
                pts.push_back (synthyboy::ContentPoint (x, y, pw));
            at = line.find (' ', at + 1);
        }
        requestScalarCurve (which, pts);
    }
#endif
    void scalarCurvePoll()
    {
#if SYNTHYBOY_MPC_DEBUG_HOOKS
        scalarCurveFakeFile();
#endif
        std::vector<synthyboy::ContentPoint> take[2];
        bool have[2] = {};
        {
            std::lock_guard<std::mutex> g (scalarCurveMutex);
            for (int i = 0; i < 2; ++i)
                if (scalarCurveDue[i])
                {
                    take[i] = std::move (scalarCurveReq[i]);
                    have[i] = true;
                    scalarCurveDue[i] = false;
                }
        }
        if ((! have[0] && ! have[1]) || ! engine)
            return;
        std::lock_guard<std::mutex> g (contentMutex);
        synthyboy::StateContent next = content;
        if (have[0]) next.velocityCurve = std::move (take[0]);
        if (have[1]) next.noteCurve = std::move (take[1]);
        if (! synthyboy::validateContent (next))
            return;
        content = std::move (next);
        engine->setContent (content);
        publishOverlayContent();
    }
    synthyboy_mpc::FilterCurveEdits filterCurveEdits;
    void filterCurvePoll()
    {
        synthyboy_mpc::FilterCurveEdits::Batch batch;
        if (! engine || ! filterCurveEdits.take (batch))
            return;
        std::lock_guard<std::mutex> guard (contentMutex);
        const uint64_t generation = stateGeneration.load (std::memory_order_relaxed);
        const auto editable = [&] (int filter)
        {
            const auto* model = synthyboy::findParamByKey (filter == 2 ? "filter2.model" : "filter1.model");
            const auto* power = synthyboy::findParamByKey (filter == 2 ? "filter2.on" : "filter1.on");
            return model != nullptr && power != nullptr
                && overlayView.param (model - synthyboy::paramTable()) == synthyboy_mpc::overlay::FilterUi::kModelPz
                && overlayView.param (power - synthyboy::paramTable()) >= 0.5f;
        };
        synthyboy::StateContent next = content;
        if (! synthyboy_mpc::FilterCurveEdits::apply (next, std::move (batch), generation, editable)
            || ! synthyboy::validateContent (next))
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
        macroNameQueue.post (m - 1, sp == std::string::npos ? std::string() : line.substr (sp + 1),
                             stateGeneration.load (std::memory_order_acquire));
    }
    void macroNamePoll (AEffect* e)
    {
        macroNameFakeFile();
        std::string names[synthyboy::kNumMacros];
        bool have[synthyboy::kNumMacros] = {};
        uint64_t generations[synthyboy::kNumMacros] = {};
        if (macroNameQueue.take (names, have, generations))
        {
            std::lock_guard<std::mutex> g (contentMutex);
            const uint64_t generation = stateGeneration.load (std::memory_order_relaxed);
            bool changed = false;
            for (int m = 0; m < synthyboy::kNumMacros; ++m)
            {
                if (! have[m] || generations[m] != generation)
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
        synthyboy::ContentFileRef wtRef;
        bool wavetable = false;
        {
            std::lock_guard<std::mutex> g (contentReqMutex);
            if (wtReqPending)
            {
                wtReqPending = false;
                wavetable = true;
                osc = wtReqOsc;
                wtRef = wtReqRef;
            }
            else if (! contentReqPending)
                return;
            else
            {
                contentReqPending = false;
                osc = contentReqOsc;
                kind = contentReqKind;
                path = contentReqPath;
            }
        }
        std::string why;
        bool ok = false;
        try { ok = wavetable ? loadWavetableFile (osc, wtRef, why) : loadContentFile (osc, kind, path, why); }
        catch (...) { why = wavetable ? "not a wavetable file: unexpected error" : "unexpected error"; }
        std::lock_guard<std::mutex> g (contentReqMutex);
        contentLoadMessage = why;
        contentLoadFailedMs.store (steadyMs(), std::memory_order_relaxed);
        if (! wtReqPending)
            wtLoading.store (false, std::memory_order_relaxed);
        if ((! contentReqPending && ! wtReqPending) || ! ok)   // a newer request is already waiting: its own state (loading) stays
            contentLoadStatus.store (ok ? 2 : 3, std::memory_order_relaxed);
    }
    /** A wavetable file the browser chose (the pack's file, or a file of a user folder): read bounded, parsed by the rules of wavetable_file.h, its SHA-256 computed now, the reference put into the content of the oscillator,
        the engine given the content (the table is built there while the oscillator keeps playing the one it had), and only then oscN.wavetable = "Preset table". A refused file leaves everything as it was and says why. */
    bool loadWavetableFile (int osc, synthyboy::ContentFileRef ref, std::string& why)
    {
        if (osc < 1 || osc > synthyboy::kMaxContentWavetables || ! contentStore || ! engine)
        {
            why = "no content store";
            return false;
        }
        ensureUserWavetableRoots();   // takes contentMutex itself: before the lock below
        std::lock_guard<std::mutex> g (contentMutex);   // the store's roots change under this mutex: the file is read (bounded, parsed, hashed) under it too
        int frames = 0;
        std::string reason;
        if (! synthyboy::prepareWavetableRef (*contentStore, ref, frames, reason))
        {
            why = synthyboy::wavetableRefusalText (reason);
            return false;
        }
        synthyboy::StateContent next = content;
        synthyboy::attachWavetable (next, osc, ref, std::string(), frames);
        if (! synthyboy::validateContent (next))
        {
            why = synthyboy::wavetableRefusalText ("the name cannot be kept in the state");
            return false;
        }
        content = std::move (next);
        engine->setContent (content);
        publishOverlayContent();
        if (wtParam[osc - 1] >= 0)
        {
            const synthyboy::ParamInfo& p = synthyboy::paramTable()[wtParam[osc - 1]];
            uiWrite (wtParam[osc - 1], synthyboy::toNormalised (p, synthyboy::constrainValue (p, static_cast<float> (synthyboy::kWavetablePresetTable))));
        }
        return true;
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
        synthyboy::SampleLoopSettings loop;
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
            loop = synthyboy::sampleLoopSettings (facts);
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
        if (loop.valid && kind == 1)
        {
            // the loop of the WAV 'smpl' chunk becomes the oscillator's loop settings, once, at load time (the same path as the root note)
            const std::string o = "osc" + std::to_string (osc);
            const std::pair<const char*, float> writes[3] = { { ".sample.loop_mode", static_cast<float> (loop.mode) }, { ".sample.loop_start", loop.startPercent }, { ".sample.loop_end", loop.endPercent } };
            for (const auto& w : writes)
                if (const synthyboy::ParamInfo* p = synthyboy::findParamByKey ((o + w.first).c_str()))
                    uiWrite (static_cast<int> (p - synthyboy::paramTable()), synthyboy::toNormalised (*p, synthyboy::constrainValue (*p, w.second)));
        }
        return true;
    }
    bool overlayAttached = false;

    // ---- the Play page (play_params.h, docs/MPC.md "Play page"): three selectors (saved in the chunk) and 24 proxy parameters (the real parameter of the selected instance) ----
    std::atomic<int> playSel[synthyboy_mpc::play::kGroups] { { 1 }, { 1 }, { 1 } };
    std::atomic<uint64_t> playOscSelectionRev {};
    /** The chunk-only view state of the merged tabs (tab_view.h "mpcTabView": rnd, filter): no host parameter. effSetChunk restores it (a chunk without it gives 0, 0), effSetProgram leaves it. */
    std::atomic<int> tabViewState[synthyboy_mpc::tabview::kFields] { { 0 }, { 0 } };
    /** The key and the velocity of the last note-on the plugin received (effProcessEvents): the live x marker of the NOTE / VELO curve editors of the Filter page (IInstanceView::lastPlayed). One atomic per source, written with a
        relaxed store on the thread the host delivers the events on (no lock, no allocation) and read by the overlay thread; -1 until a note was played. Not state: a project reload leaves them. */
    std::atomic<int> lastNoteOnKey { -1 }, lastNoteOnVelocity { -1 };
    std::atomic<bool> playGroupDue[synthyboy_mpc::play::kGroups] { { false }, { false }, { false } };   // a selector changed (host, chunk): the display thread reports the group's proxies once
    static constexpr unsigned kPlayReportWordBits = 32;
    static_assert (std::atomic<uint32_t>::is_always_lock_free, "Play reports are marked from the audio thread");
    std::atomic<uint32_t> playRealDue[(synthyboy::kMaxParams + kPlayReportWordBits - 1) / kPlayReportWordBits] {};
    std::atomic<float> playLast[synthyboy_mpc::play::kProxies] {};                                      // the value of every proxy as the host last heard it (a host write or a report)
    int playLastTarget[synthyboy_mpc::play::kProxies] {};                                               // display thread only
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
    void playDump (AEffect* effect, bool vst, bool display)
    {
        namespace pl = synthyboy_mpc::play;
        std::string text;
        char line[160];
        std::snprintf (line, sizeof line, "selectors %d %d %d\nprogram %d protected %d window %d\nui_write_calls %llu ui_automate_calls %llu\n", playSel[0].load(), playSel[1].load(), playSel[2].load(), currentProgram.load(), protectedProgram.load(), restoreSinceMs.load() >= 0 ? 1 : 0, static_cast<unsigned long long> (uiWriteCalls.load (std::memory_order_relaxed)), static_cast<unsigned long long> (uiAutomateCalls.load (std::memory_order_relaxed)));
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
        if (vst)
            for (VstInt32 i = 0; i < effect->numParams; ++i)
            {
                std::snprintf (line, sizeof line, "v %d %.9g\n", static_cast<int> (i), static_cast<double> (effect->getParameter (effect, i)));
                text += line;
                if (display)
                {
                    char value[128] {};
                    effect->dispatcher (effect, effGetParamDisplay, i, 0, value, 0.0f);
                    std::snprintf (line, sizeof line, "display %d %s\n", static_cast<int> (i), value);
                    text += line;
                }
            }
        const std::string chunk = buildStateChunk (this);
        std::snprintf (line, sizeof line, "chunk %zu\n", chunk.size());
        text += line;
        text += chunk;
        text += '\n';
        if (text.size() <= synthyboy_mpc::safefile::kMaxWriteBytes)
            synthyboy_mpc::safefile::writeReplacing (synthyboy_mpc::PlayLog::dumpPath().c_str(), text.data(), text.size());
    }
#endif

    // ---- the writes of the plugin-drawn pages (overlay/osc_ui.h): the same write as the host's own, then the display thread tells the host (audioMasterAutomate), so automation recording and the host's
    // own widgets follow. The overlay thread sets the dirty bit, the display thread reports it (one value per parameter, the latest).
    std::atomic<int> playLastSel { 0 };                          // 0 none, 1 envelope, 2 LFO: which of the two Play selectors was set last (the modulation source the UI calls "selected")
    std::atomic<uint64_t> uiDirty[synthyboy::kMaxParams / 64] {};
#if SYNTHYBOY_MPC_DEBUG_HOOKS
    std::atomic<uint64_t> uiWriteCalls { 0 }, uiAutomateCalls { 0 };
#endif
    void uiWrite (int tableIndex, float normalised) noexcept
    {
        if (tableIndex < 0 || tableIndex >= static_cast<int> (synthyboy::paramCount()))
            return;
        writeRealParam (this, static_cast<size_t> (tableIndex), normalised);
#if SYNTHYBOY_MPC_DEBUG_HOOKS
        uiWriteCalls.fetch_add (1, std::memory_order_relaxed);
#endif
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
#if SYNTHYBOY_MPC_DEBUG_HOOKS
                uiAutomateCalls.fetch_add (1, std::memory_order_relaxed);
#endif
            }
        }
    }

    // ---- the Q proxies (q_router.h, docs/MPC.md "Q proxies"): "Q 1" .. "Q 4" after the last Play proxy; what the four knobs of the plugin's own full-screen pages are mapped to ----
    /** The sixteen proxies: the kit's bank over SynthyBoy's parameter table (kit/q_targets.h; migration step 11 of docs/ui-components/TEMPLATE.md). It reads the values through the overlay view (the parameter mirror). */
    mpckit::QProxyBank qBank { mpckit::synthyboy_adapter::Params::get() };
    // Keep the revision and in-flight restore count in one atomic snapshot.
    // A completed restore cannot rearm Q while another restore is still loading.
    static constexpr uint64_t kQRestoreCountMask = 0xffffffffu;
    static constexpr uint64_t kQRevisionStep = kQRestoreCountMask + 1;
    std::atomic<uint64_t> qBindingState { 0 }, qReadyRevision { 0 };

    struct QBindingRestore
    {
        explicit QBindingRestore (PluginInstance& instance) noexcept : inst (instance)
        {
            inst.qBindingState.fetch_add (kQRevisionStep + 1, std::memory_order_acq_rel);
            inst.qBank.setGroup (nullptr, 0);
        }

        QBindingRestore (const QBindingRestore&) = delete;
        QBindingRestore& operator= (const QBindingRestore&) = delete;

        ~QBindingRestore() { inst.qBindingState.fetch_add (kQRevisionStep - 1, std::memory_order_release); }
        PluginInstance& inst;
    };

    bool qBindingsReady() const noexcept
    {
        const auto ready = qReadyRevision.load (std::memory_order_acquire);
        return qBindingState.load (std::memory_order_acquire) == ready;
    }
    static VstInt32 qBase() noexcept { return playBase() + synthyboy_mpc::play::kParams; }
    static bool isQParam (VstInt32 index) noexcept { return index >= qBase() && index < qBase() + synthyboy_mpc::QRouter::kCount; }
    static VstInt32 nativeUiBase() noexcept { return qBase() + synthyboy_mpc::QRouter::kCount; }
    static bool isNativeUiParam (VstInt32 index) noexcept { return index >= nativeUiBase() && index < nativeUiBase() + nativeui::kCount; }
    std::atomic<float> nativeUiValues[nativeui::kEnvSelectorBase] {};
    std::atomic<float> nativeStereoMeters[nativeui::kMixerStereoFields * nativeui::kMixerStrips] {};
    std::atomic<uint32_t> nativeEnvSelector { 0 };
    float nativeUiReported[nativeui::kCount] {};   // display thread only
    nativeui::MixerPeekRequest mixerPeek;
    std::atomic<bool> hostMixerWidgetsHidden { false };
    int nativeVoiceDisplayedRow = -1;   // display thread only
#if SYNTHYBOY_MPC_DEBUG_HOOKS
    long long nextUiLogMs = 0;
    int loggedMasterView = -1, loggedVoiceView = -1;
    std::atomic<unsigned> uiNameReads {}, uiLabelReads {}, uiTextReads {}, uiPropsReads {};
    std::atomic<uint64_t> uiReadNs {};
    void countUiRead (VstInt32 opcode, int64_t ns) noexcept
    {
        switch (opcode)
        {
            case effGetParamName: uiNameReads.fetch_add (1, std::memory_order_relaxed); break;
            case effGetParamLabel: uiLabelReads.fetch_add (1, std::memory_order_relaxed); break;
            case effGetParamDisplay: uiTextReads.fetch_add (1, std::memory_order_relaxed); break;
            case effGetParameterProperties: uiPropsReads.fetch_add (1, std::memory_order_relaxed); break;
            default: return;
        }
        uiReadNs.fetch_add (ns, std::memory_order_relaxed);
    }
#endif
    static constexpr long long kNativePaintWindowMs = 150;
    bool nativeNamesAwaitingPaint = false;
    long long nativePagePaintUntil = -1;
    long long proxyNamesRequestedAt = -1;   // display thread only
    std::atomic<int> nativeMixerPeekView { nativeui::MixerBase };
    std::atomic<int> nativeMixerActiveTab { 0 };
    std::array<std::atomic<int>, nativeui::kMixerStrips> nativeMixerSoloVisual {};
    std::array<std::atomic<int>, nativeui::kMixerStrips> nativeMixerMuteVisual {};
    nativeui::FxCellBank nativeFxCells;
    nativeui::fxheader::Bank nativeFxHeader;
    nativeui::fxchain::Bank nativeFxChain;
    nativeui::lfo::Bank nativeLfo;
    uint64_t nativeLfoAnnounced = 0;
    nativeui::lfoeditor::Bank nativeLfoEditor;
    std::array<bool, nativeui::lfoeditor::kCount> nativeLfoEditorAcks {};   // display thread only
    nativeui::lfodetail::Bank nativeLfoDetail;
    std::array<bool, nativeui::lfodetail::kCount> nativeLfoDetailAcks {};   // display thread only
    static_assert (nativeui::kLfoDetailHandles == nativeui::lfodetail::kHandles);
    // Program/chunk restoration can run concurrently with the overlay. Both writers
    // publish complete banks here. Host parameter reads and audio callbacks never lock.
    std::mutex playPresentationMutex;
    std::atomic<bool> preparedOscDue { false }, preparedEnvLfoDue { false };
    nativeui::playenvlfo::Bank nativePlayEnvLfo;
    std::atomic<uint64_t> playEnvLfoRevision { 0 };
    static_assert (nativeui::kPlayEnvLfoHandles == nativeui::playenvlfo::kOriginalHandles);
    static_assert (nativeui::kPlayOscHandles == nativeui::playoscui::kHandles);
    nativeui::playoscui::Bank nativePlayOsc;
    std::shared_ptr<const std::string> nativePlayOscSourceName;
    std::atomic<bool> nativePlayOscLabelChanged { false };
    nativeui::FilterCellBank nativeFilter;
    nativeui::NativeCellBank<2> nativePzActions;
    std::atomic<uint64_t> nativePzToolbarPreset {};
    std::atomic<int> nativePzPresentation {0};
    std::array<bool, 2> nativePzAcks {};
    nativeui::osc::Bank nativeOsc;
    nativeui::NativeCellBank<nativeui::osc::kSettingsAll> nativeOscSettings;
    nativeui::oscdetail::Bank nativeOscDetail;
    std::array<bool, nativeui::oscdetail::kBindings> nativeOscDetailAcks {};
    std::atomic<int> nativeOscWarpMask { 0 };
    std::shared_ptr<const std::string> nativeOscSourceLabel;
    std::array<bool, nativeui::osc::kSettingsAll> nativeOscSettingAcks {};   // display thread only
    bool nativeOscSourceLabelChanged = false;                               // display thread only
    std::atomic<int> nativeMasterView { 0 };
    std::atomic<int> nativeMasterSkinPanel { nativeui::master::Main };
    // One atomic mailbox per instrument. A new capture replaces the previous pending root.
    enum { ScaleInputHidden = -3, ScaleInputOff = -2, ScaleInputArmed = -1 };
    std::atomic<int> scaleInput { ScaleInputHidden };
    // MIDI delivery may use multiple threads. Preserve releases for notes played before arming.
    enum { Released, Sounding, Captured };
    std::array<std::atomic<uint8_t>, 16 * 128> scaleInputKeys {};
    nativeui::NativeCellBank<1> nativeMasterPanel;
    std::atomic<uint64_t> nativeMasterPanelPreset {};
    std::atomic<float> nativeMasterValues[nativeui::master::ClearClip] {};
    std::atomic<int> nativeMasterPolyphony { 16 };
    int nativeMasterReportedPolyphony = 0;  // display thread only
    std::atomic<bool> nativeMasterClearPending { false }, nativeMasterClearAck { false };
    std::array<std::atomic<int>, nativeui::mod::kRows> nativeModRows {};
    nativeui::NativeCellBank<nativeui::kMacroTargetCount> nativeMacroTargets;
    std::atomic<uint64_t> nativeMacroTargetPreset {};
    int nativeMacroTargetAck = -1;
    nativeui::expandedplay::Bank nativeExpandedPlay;
    int nativeExpandedPlayAck = -1;
    nativeui::targets::Bank nativeModTargets;
    std::array<bool, nativeui::targets::kFields> nativeModTargetAcks {};
    nativeui::NativeCellBank<nativeui::mod::kFields> nativeMod;
    nativeui::NativeCellBank<nativeui::mod::kNavigation> nativeModNavigation;
    nativeui::NativeCellBank<nativeui::moddetail::kHandles> nativeModDetailNavigation;
    nativeui::NativeCellBank<nativeui::modboard::kHandles> nativeModBoard;
    std::atomic<uint64_t> nativeModBoardPreset { 0 };
    std::atomic<int> nativeModSourceGroup { 0 }, nativeModDestinationGroup { 0 };
    std::array<int, nativeui::mod::kNavigation> nativeModReportedChoices {};   // display thread only
    std::shared_ptr<const synthyboy_mpc::overlay::OscContentSnapshot> nativeModReportedContent;   // display thread only
    nativeui::NativeCellBank<nativeui::env::kFields> nativeEnvEditor;
    std::atomic<int> nativeEnvTargetCount { 0 };
    int nativeEnvTargetCountReported = -1;
    nativeui::voice::Bank nativeVoice;
    uint64_t nativeOscAnnounced = 0;
    std::atomic<float> nativeFilterCurve[2 * nativeui::kFilterCurveFields] {};
    std::atomic<int> nativeFilterIndicators[nativeui::kFilterIndicators] {};
    static_assert (nativeui::kFilterIndicatorBase - nativeui::kFilterValueBase == nativeui::FilterSlots);
    static_assert (nativeui::kOscBase - nativeui::kFilterIndicatorBase == nativeui::kFilterIndicators);

    static const synthyboy::ParamInfo* nativeLfoInfo (int field) noexcept
    {
        namespace n = nativeui::lfo;
        static constexpr const char* keys[n::kCount] {
            nullptr, "lfo1.shape", "rlfo1.style", "lfo1.smooth", "lfo1.smooth_time", nullptr, nullptr,
            "lfo1.rate", "lfo1.division", "lfo1.fade_in", "lfo1.sync", "lfo1.sync_type", "rlfo1.sync_type",
            "lfo1.phase", "lfo1.delay", "lfo1.host", "lfo1.direction", "lfo1.swing", "lfo1.stereo",
            "lfo1.mono", "rlfo1.stereo", "lfo1.keytrack_transpose", "lfo1.keytrack_tune"
        };
        return keys[field] == nullptr ? nullptr : synthyboy::findParamByKey (keys[field]);
    }

    static float envSelectorValue (uint32_t frames, int cell) noexcept
    {
        constexpr uint32_t mask = (1u << nativeui::kEnvSelectorFrameBits) - 1;
        return static_cast<float> ((frames >> (cell * nativeui::kEnvSelectorFrameBits)) & mask) / nativeui::kEnvSelectorLastFrame;
    }

    float nativeFilterValue (int slot) const noexcept
    {
        const auto cell = nativeFilter.at (slot).cell();
        if (cell.target < 0 || slot == nativeui::FilterAction)
            return 0.0f;
        if (slot >= nativeui::FilterPoint && slot < nativeui::FilterLegato)
            return nativeFilterCurve[slot - nativeui::FilterPoint].load (std::memory_order_relaxed);
        return qReadNorm (cell.target);
    }

    float nativeModNavigationValue (int field) const noexcept
    {
        namespace n = nativeui::mod;
        if (field == n::Mode)
            return static_cast<float> (nativeModNavigation.layout()) / n::kLastMode;
        if (field == n::SourceGroup)
            return static_cast<float> (nativeModSourceGroup.load (std::memory_order_relaxed)) / n::kLastSourceGroup;
        if (field == n::DestinationGroup)
            return static_cast<float> (nativeModDestinationGroup.load (std::memory_order_relaxed)) / n::kLastDestinationGroup;
        return 0.0f;
    }

    int nativeModDisplayChoice (int field, nativeui::FxCell cell) const noexcept
    {
        namespace n = nativeui::mod;
        if (field < n::kHeaderNavigation || cell.target < 0
            || (field - n::kHeaderNavigation) % n::kRowNavigation == n::OpenMore)
            return 0;
        return static_cast<int> (paramMirror[cell.target].load (std::memory_order_relaxed));
    }

    float nativeVoiceValue (int field, nativeui::FxCell cell) const noexcept
    {
        namespace n = nativeui::voice;
        const int target = cell.target;
        if (target < 0)
            return 0.0f;
        if (field == n::kValue)
            return qReadNorm (target);
        const int last = field == n::kRow ? n::kRows - 1 : n::kSteps - 1;
        return static_cast<float> (target) / last;
    }

    static int nativeOscSettingField (int offset) noexcept
    {
        if (offset >= nativeui::kOscAbcSettings
            && offset < nativeui::kOscAbcSettings + nativeui::osc::kAbcSources * nativeui::osc::kAbcSettings)
            return nativeui::osc::kSettings + offset - nativeui::kOscAbcSettings;
        return offset >= nativeui::kOscSettings && offset < nativeui::kOscRoutingBySource ? offset - nativeui::kOscSettings : -1;
    }

    float nativeOscSettingValue (nativeui::FxCell cell) const noexcept
    {
        const auto plain = cell.target >= 0 ? paramMirror[cell.target].load (std::memory_order_relaxed) : 0.0f;
        return synthyboy_mpc::overlay::OscUi::nativeSettingsValue (cell, plain);
    }

    static constexpr int kUnmappedPlayEnvLfoField = -2;

    static constexpr int nativePlayEnvLfoField (int offset) noexcept
    {
        if (offset == nativeui::kPlayLfoStandardSync)
            return nativeui::playenvlfo::StandardSync;
        // The first handle (-1) is the view. Appended or reserved VST handles
        // do not belong to this panel, even when no earlier handler consumed them.
        if (offset < nativeui::kPlayEnvLfo || offset >= nativeui::kPlayOsc)
            return kUnmappedPlayEnvLfoField;
        return offset - nativeui::kPlayEnvLfo - 1;
    }

    float nativePlayEnvLfoValue (int field) const noexcept
    {
        namespace n = nativeui::playenvlfo;
        if (field == n::Choose)
            return nativePlayEnvLfo.selection();
        if (field == n::EnvExpand || field == n::LfoExpand)
            return 0.0f;
        const int target = nativePlayEnvLfo.at (field).cell().target;
        if (field == n::StandardSync && target >= 0)
        {
            const int sync = static_cast<int> (std::lround (paramMirror[target].load (std::memory_order_relaxed)));
            return static_cast<float> (synthyboy_mpc::ui::lfo::syncModeOf (sync)) / 2.0f;
        }
        return target >= 0 ? qReadNorm (target) : 0.0f;
    }

    float nativePlayOscValue (int field, nativeui::FxCell cell) const noexcept
    {
        if (cell.target < 0)
            return 0.0f;
        const float plain = paramMirror[cell.target].load (std::memory_order_relaxed);
        if (field == nativeui::playoscui::Route)
            return plain / nativeui::playoscui::kRoutingDetails;
        return synthyboy_mpc::overlay::OscUi::nativeSettingsValue (cell, plain);
    }

    static int oscDetailField (int handle) noexcept
    {
        namespace n = nativeui::oscdetail;
        if (handle >= n::kNavigation) return n::kMore + handle - n::kNavigation;
        if (handle >= n::kKind) return handle - n::kKind;
        return handle - n::kValue;
    }

    float oscRootLoadView() const noexcept
    {
        namespace n = nativeui::oscdetail;
        for (int i = 0; i < n::kRootLoads; ++i)
            if (nativeOscDetail.at (n::kRootLoad + i).cell().target >= 0)
                return static_cast<float> (i + 1) / n::kRootLoads;
        return 0.0f;
    }

    nativeui::FxCell oscDetailCell (int handle) const noexcept
    {
        namespace n = nativeui::oscdetail;
        return handle >= n::kMenusBase ? nativeOscDetail.domain (n::kFirstMenuKind + handle - n::kMenusBase).cell()
                                        : nativeOscDetail.at (oscDetailField (handle)).cell();
    }

    float oscDetailValue (int handle, nativeui::FxCell cell) const noexcept
    {
        namespace n = nativeui::oscdetail;
        if (handle >= n::kKind && handle < n::kNavigation)
            return static_cast<float> (n::kind (cell.kind)) / n::kVisualKinds;
        if (cell.target < 0 || (handle >= n::kStep && handle < n::kMenusBase) || cell.kind == n::Action)
            return 0.0f;
        const float plain = paramMirror[cell.target].load (std::memory_order_relaxed);
        if (cell.kind == n::Semitone)
            return (synthyboy_mpc::overlay::OscUi::semOf (static_cast<int> (std::lround (plain))) + 11.0f) / 22.0f;
        if (cell.kind == n::Snap)
            return (static_cast<int> (std::lround (plain)) & 4096) != 0 ? 1.0f : 0.0f;
        return qReadNorm (cell.target);
    }

    static nativeui::FxCell masterPanelCell (nativeui::master::View view) noexcept
    {
        return view == nativeui::master::Hidden ? nativeui::FxCell {} : nativeui::FxCell {0, view, nativeui::kFxDial};
    }

    float nativeModTargetValue (int field, nativeui::FxCell cell) const noexcept
    {
        namespace n = nativeui::targets;
        if (field == n::kView)
            return static_cast<float> (nativeModTargets.layout()) / n::kLastFrame;
        if (field == n::kPageGate)
        {
            const auto page = nativeModTargets.at (n::Page).cell();
            if (page.target < 0)
                return 0.0f;
            return page.source == 0 ? 1.0f : page.source == page.target - 1 ? 1.0f / 3.0f : 2.0f / 3.0f;
        }
        if (field >= n::kRowGate && field < n::kHandles)
        {
            const auto amount = nativeModTargets.at (n::field (field - n::kRowGate, n::Amount)).cell();
            return amount.target < 0 ? 0.0f : nativeModTargets.mode() == n::Targets ? 1.0f : 0.5f;
        }
        if (field < n::FirstRow || cell.target < 0)
            return 0.0f;
        switch ((field - n::FirstRow) % n::RowFields)
        {
            case n::Amount:
            case n::Polarity: return qReadNorm (cell.target);
            case n::Active: return 1.0f - qReadNorm (cell.target);
            default: return 0.0f;
        }
    }

    float nativeUiValue (int offset) const noexcept
    {
        if (offset == nativeui::kLfoTargetsAction)
            return 0.0f;
        if (offset == nativeui::kFxEditorPower)
            return nativeFxHeader.state().on() ? 1.0f : 0.0f;
        if (nativeui::isFilterChainMenu (offset))
            return (nativeFilterIndicators[1].load (std::memory_order_relaxed) & 2) ? 1.0f / nativeui::kFilterChainLastChoice : 0.0f;
        if (nativeui::isMixerStereoOffset (offset))
            return nativeStereoMeters[offset - nativeui::kMixerStereoBase].load (std::memory_order_relaxed);
        if (nativeui::isModDetailControlOffset (offset))
        {
            const int field = offset - nativeui::kModDetailControl;
            if (field == nativeui::moddetail::Confirm)
            {
                return (nativeModDetailNavigation.layout()
                        && nativeModDetailNavigation.at (nativeui::moddetail::Confirm).cell().source > 0) ? 1.0f : 0.0f;
            }
            return 0.0f;
        }
        if (nativeui::isModBoardControlOffset (offset))
        {
            const int field = offset - nativeui::kModBoardControl;
            if (field == nativeui::modboard::Gate)
                return nativeModBoard.layout() == 1 ? 1.0f : 0.0f;
            if (field == nativeui::modboard::ListGate)
                return nativeModBoard.layout() == 2 ? 1.0f : 0.0f;
            if (field >= nativeui::modboard::ListRowGate)
                return nativeModBoard.layout() == 2 && nativeModBoard.at (field).cell().target >= 0 ? 1.0f : 0.0f;
            return 0.0f;
        }
        if (offset == nativeui::kMixerPeek || nativeui::isMixerActiveTabOffset (offset))
            return mixerPeek.enabled()
                ? static_cast<float> (std::clamp (nativeMixerActiveTab.load (std::memory_order_acquire), 0, nativeui::kMixerTabs - 1))
                    / nativeui::kMixerPeekTargets
                : 0.0f;
        if (nativeui::isMixerSoloVisualOffset (offset))
        {
            if (! mixerPeek.enabled())
                return 0.0f;
            const int strip = offset - nativeui::kMixerSoloVisual;
            return static_cast<float> (std::clamp (nativeMixerSoloVisual[strip].load (std::memory_order_acquire),
                                                   0, nativeui::mixer::kLastVisualState))
                / nativeui::mixer::kLastVisualState;
        }
        if (nativeui::isMixerMuteVisualOffset (offset))
        {
            if (! mixerPeek.enabled())
                return 0.0f;
            const int strip = offset - nativeui::kMixerMuteVisual;
            return static_cast<float> (std::clamp (nativeMixerMuteVisual[strip].load (std::memory_order_acquire),
                                                   0, nativeui::mixer::kLastVisualState))
                / nativeui::mixer::kLastVisualState;
        }
        if (nativeui::isMixerControlOffset (offset))
        {
            const int idx = offset - nativeui::kMixerControl;
            if (idx < nativeui::mixer::kMute)
            {
                const int strip = idx - nativeui::mixer::kSolo;
                return engine != nullptr && engine->mixerSolo (strip) ? 1.0f : 0.0f;
            }
            else
            {
                const int strip = idx - nativeui::mixer::kMute;
                return engine != nullptr && engine->mixerMute (strip) ? 1.0f : 0.0f;
            }
        }
        if (nativeui::isMacroTargetOffset (offset))
            return 0.0f;
        if (nativeui::isPlayExpandedOffset (offset))
            return offset == nativeui::kPlayExpanded
                ? static_cast<float> (nativeExpandedPlay.layout()) / nativeui::expandedplay::LastFrame : 0.0f;
        if (nativeui::isModTargetsOffset (offset))
        {
            const int field = offset - nativeui::kModTargets;
            return nativeModTargetValue (field, nativeModTargets.at (field).cell());
        }
        if (nativeui::isOscRootLoadOffset (offset))
            return offset == nativeui::kOscRootLoadView ? oscRootLoadView() : 0.0f;
        if (nativeui::isPzToolbarOffset (offset))
        {
            const int state = nativePzPresentation.load (std::memory_order_acquire);
            return offset == nativeui::kPzSource ? static_cast<float> (state & 3) / 2.0f
                 : offset == nativeui::kPzAvailability ? static_cast<float> (state >> 2) / 3.0f : 0.0f;
        }
        if (nativeui::isFxBandOffset (offset))
        {
            namespace h = nativeui::fxheader;
            const auto state = nativeFxHeader.state();
            return offset == nativeui::kFxBandMode ? static_cast<float> (state.bandMode) / h::MidAndHigh
                   : offset - nativeui::kFxBandBase == state.item ? state.value (h::kBand) : 0.0f;
        }
        if (offset == nativeui::kMasterScaleInput)
            return scaleInput.load() >= ScaleInputArmed ? 1.0f : 0.0f;
        if (offset == nativeui::kMasterPanel)
            return nativeui::master::panelValue (nativeMasterSkinPanel.load (std::memory_order_relaxed));
        if (nativeui::isOscDetailOffset (offset))
        {
            namespace n = nativeui::oscdetail;
            const int handle = offset - nativeui::kOscDetail;
            return handle == n::kView ? static_cast<float> (nativeOscDetail.layout()) / n::LastFrame
                                     : oscDetailValue (handle, oscDetailCell (handle));
        }
        if (offset == nativeui::kMixerPeekView)
            return static_cast<float> (nativeMixerPeekView.load (std::memory_order_acquire)) / nativeui::kMixerPeekLastView;
        if (offset >= nativeui::kPlayOsc && offset < nativeui::kPlayLfoStandardSync)
        {
            namespace n = nativeui::playoscui;
            const int handle = offset - nativeui::kPlayOsc;
            const int view = nativePlayOsc.view();
            if (handle == 0)
                return static_cast<float> (n::Bank::frame (view)) / n::kLastView;
            if ((handle - 1) / n::kFields + 1 != n::Bank::source (view))
                return 0.0f;
            const int field = (handle - 1) % n::kFields;
            return nativePlayOscValue (field, nativePlayOsc.at (field).cell());
        }
        if (offset >= nativeui::kPlayEnvLfo)
        {
            const int field = nativePlayEnvLfoField (offset);
            if (field == kUnmappedPlayEnvLfoField)
                return 0.0f;
            return field < 0 ? static_cast<float> (nativePlayEnvLfo.layout()) / nativeui::playenvlfo::LastView
                             : nativePlayEnvLfoValue (field);
        }
        if (offset >= nativeui::kLfoDetail)
        {
            namespace n = nativeui::lfodetail;
            const int handle = offset - nativeui::kLfoDetail;
            const auto display = nativeLfoDetail.display();
            switch (handle)
            {
                case n::kViewHandle: return static_cast<float> (display.view) / n::LastView;
                case n::kPointState: return static_cast<float> (display.point) / n::LastPointState;
                case n::kHeaderBack: return display.headerBack ? 1.0f : 0.0f;
                case n::kVisibleRows: return static_cast<float> (display.rows) / n::kRows;
                default: return nativeLfoDetail.value (n::handleField (handle));
            }
        }
        if (offset >= nativeui::kFxChain)
            return nativeFxChain.value (offset - nativeui::kFxChain);
        if (offset >= nativeui::kLfoEditor)
        {
            namespace n = nativeui::lfoeditor;
            const int field = offset - nativeui::kLfoEditor;
            if (field == n::kView)
                return nativeLfoEditor.display().shown ? 1.0f : 0.0f;
            if (field == n::kScale)
                return nativeLfoEditor.display().scale ? 1.0f : 0.0f;
            if (field >= n::kAvailability)
                return nativeLfoEditor.at (n::kOptional[field - n::kAvailability]).cell().target >= 0 ? 1.0f : 0.0f;
            return nativeLfoEditor.value (field - n::kValues);
        }
        if (const int field = nativeOscSettingField (offset); field >= 0)
            return nativeOscSettingValue (nativeOscSettings.at (field).cell());
        if (offset == nativeui::kOscWarpLayout)
            return nativeOscSettings.layout() >= nativeui::osc::SettingsWavetable
                ? static_cast<float> (nativeOscWarpMask.load (std::memory_order_relaxed)) / nativeui::osc::kLastWarpMask : 0.0f;
        if (offset >= nativeui::kFxHeader)
        {
            namespace h = nativeui::fxheader;
            const auto header = nativeFxHeader.state();
            const int handle = offset - nativeui::kFxHeader;
            if (handle == 0)
                return static_cast<float> (header.item + 1) / h::kItems;
            if (handle == 1)
                return static_cast<float> (header.layout) / h::kLastLayout;
            const int item = (handle - 2) / h::kFields;
            const int field = (handle - 2) % h::kFields;
            return item == header.item ? header.value (field) : 0.0f;
        }
        if (offset >= nativeui::kOscRoutingBySource)
            return nativeOsc.snapshot().routingValue (offset - nativeui::kOscRoutingBySource + 1);
        if (offset == nativeui::kOscSettingsView)
            return static_cast<float> (nativeOscSettings.layout()) / nativeui::osc::kLastSettings;
        if (offset >= nativeui::kMasterOutput)
        {
            namespace m = nativeui::master;
            const int field = offset - nativeui::kMasterOutput;
            if (field == m::Limiter)
                return outputStage.limiter() ? 1.0f : 0.0f;
            return field == m::ClearClip ? 0.0f : nativeMasterValues[field].load (std::memory_order_relaxed);
        }
        if (offset >= nativeui::kOscPower)
            return nativeOsc.snapshot().powerValue (offset - nativeui::kOscPower + 1);
        if (offset >= nativeui::kVoiceSequence)
        {
            const int field = offset - nativeui::kVoiceSequence;
            return nativeVoiceValue (field, nativeVoice.displayCell (field));
        }
        if (offset >= nativeui::kModNavigation)
            return nativeModNavigationValue (offset - nativeui::kModNavigation);
        if (offset == nativeui::kPlayLfoStandard)
            return playLfoIsStandard() ? 1.0f : 0.0f;
        if (offset == nativeui::kEnvView)
            return static_cast<float> (nativeEnvEditor.layout());
        if (offset >= nativeui::kEnvFields)
            return nativeEnvValue (offset - nativeui::kEnvFields, nativeEnvEditor.at (offset - nativeui::kEnvFields).cell().target);
        if (offset >= nativeui::kModFields)
        {
            const int real = nativeMod.at (offset - nativeui::kModFields).cell().target;
            return real >= 0 ? qReadNorm (real) : 0.0f;
        }
        if (offset >= nativeui::kModRows)
            return static_cast<float> (nativeModRows[offset - nativeui::kModRows].load (std::memory_order_relaxed)) / nativeui::mod::kLastSlot;
        if (offset == nativeui::kModView)
            return static_cast<float> (nativeMod.layout()) / nativeui::mod::kLastView;
        if (offset == nativeui::kMasterView)
            return static_cast<float> (nativeMasterSkinPanel.load (std::memory_order_relaxed)) / nativeui::master::kLastFrame;
        if (offset >= nativeui::kOscBase)
            return nativeOsc.snapshot().value (offset - nativeui::kOscBase);
        if (offset == nativeui::kFilterBase)
            return static_cast<float> (nativeFilter.layout()) / nativeui::kFilterLastViewFrame;
        if (offset >= nativeui::kFilterIndicatorBase)
            return static_cast<float> (nativeFilterIndicators[offset - nativeui::kFilterIndicatorBase].load (std::memory_order_relaxed)) / nativeui::kFilterIndicatorLastFrame;
        if (offset >= nativeui::kFilterValueBase)
            return nativeFilterValue (offset - nativeui::kFilterValueBase);
        if (offset >= nativeui::kLfoBase)
            return nativeLfo.value (offset - nativeui::kLfoBase);
        if (offset >= nativeui::kFxValueBase)
        {
            if (offset == nativeui::kFxLayout)
                return static_cast<float> (nativeFxCells.layout()) / nativeui::kFxLayoutLast;
            if (offset >= nativeui::kFxKindBase && offset < nativeui::kFxLayout)
                return static_cast<float> (nativeFxCells.at (offset - nativeui::kFxKindBase).cell().kind) / nativeui::kFxKindLast;
            const auto menu = nativeFilterMenu (offset);
            if (menu.slot >= 0)
                return nativeFxValue (menu.cell());
            return nativeFxValue (nativeFxEndpoint (offset).cell());
        }
        if (offset >= nativeui::kEnvSelectorBase)
            return envSelectorValue (nativeEnvSelector.load (std::memory_order_relaxed), offset - nativeui::kEnvSelectorBase);
        return nativeUiValues[offset].load (std::memory_order_relaxed);
    }

    float nativeEnvValue (int field, int target) const noexcept
    {
        namespace n = nativeui::env;
        if (target < 0 || field == n::Targets)
            return 0.0f;
        if (field == n::Selection)
            return static_cast<float> (playSel[1].load (std::memory_order_relaxed) - 1) / (n::kInstances - 1);
        return qReadNorm (target);
    }

    /** The Filter and FX tabs are mutually exclusive owners of the static
        SDK model/style lists. Filter keeps its own target epoch and queue. */
    nativeui::FilterCellBank::Snapshot nativeFilterMenu (int offset) const noexcept
    {
        const int domain = offset - nativeui::kFxDomainBase;
        if (domain == nativeui::kFxModel || (domain >= nativeui::kFxStyleBase && domain < nativeui::kFxDomains))
        {
            const auto link = nativeFilter.at (domain == nativeui::kFxModel ? nativeui::FilterModel : nativeui::FilterStyle);
            if (link.cell().kind == nativeui::kFxFirstDomain + domain)
                return link;
        }
        return {};
    }

    nativeui::FxCellBank::Snapshot nativeFxEndpoint (int offset) const noexcept
    {
        if (offset >= nativeui::kFxDomainBase)
            return nativeFxCells.domain (nativeui::kFxFirstDomain + offset - nativeui::kFxDomainBase);
        if (offset >= nativeui::kFxValueBase && offset < nativeui::kFxKindBase)
        {
            const auto snapshot = nativeFxCells.at (offset - nativeui::kFxValueBase);
            if (nativeui::fxCellProxy (snapshot.cell().kind))
                return snapshot;
        }
        return {};
    }

    float nativeFxValue (const nativeui::FxCell& cell) const noexcept
    {
        return cell.target >= 0 ? nativeui::fxUiValue (cell, qReadNorm (cell.target)) : 0.0f;
    }

    void reportNativeValue (AEffect* e, int offset, float value, bool force = false)
    {
        if (value == nativeUiReported[offset] && ! force)
            return;
        const bool enteringLayout = nativeUiReported[offset] <= 0.0f
            && (offset == nativeui::kFilterBase || offset == nativeui::kFxHeader
                || offset == nativeui::kFxChain + nativeui::fxchain::View);
        const bool switchingLfoLayout = offset == nativeui::kLfoBase + nativeui::lfo::kLayout
            && value != nativeUiReported[offset];
        if (value > 0.0f && (enteringLayout || switchingLfoLayout))
            nativePagePaintUntil = steadyMs() + kNativePaintWindowMs;
        nativeUiReported[offset] = value;
        audioMaster (e, audioMasterAutomate, nativeUiBase() + offset, 0, nullptr, value);
    }

    void nativeLfoEditorPoll (AEffect* e)
    {
        namespace n = nativeui::lfoeditor;
        constexpr int base = nativeui::kLfoEditor;
        const bool shown = nativeLfoEditor.display().shown;
        if (! shown && nativeUiReported[base + n::kView] == 0.0f)
            return;
        bool rebind = false;
        for (int field = 0; field < n::kCount; ++field)
        {
            const auto link = nativeLfoEditor.at (field);
            const bool moved = nativeLfoEditor.needsReport (link);
            const bool acknowledged = std::exchange (nativeLfoEditorAcks[field], false);
            reportNativeValue (e, base + n::kValues + field, nativeLfoEditor.value (field),
                               shown && (moved || acknowledged));
            if (moved)
            {
                nativeLfoEditor.report (link);
                rebind = true;
            }
        }
        for (int field = n::kAvailability; field <= n::kScale; ++field)
            reportNativeValue (e, base + field, nativeUiValue (base + field));
        reportNativeValue (e, base + n::kView, shown ? 1.0f : 0.0f);
        if (rebind)
            requestNativeControlNames();
    }

    void nativeLfoDetailPoll (AEffect* e)
    {
        namespace n = nativeui::lfodetail;
        constexpr int base = nativeui::kLfoDetail;
        const auto display = nativeLfoDetail.display();
        if (display.view == n::Hidden && nativeUiReported[base + n::kViewHandle] == 0.0f)
            return;
        bool rebind = false;
        for (int field = 0; field < n::kCount; ++field)
        {
            const auto link = nativeLfoDetail.at (field);
            const bool moved = nativeLfoDetail.needsReport (link);
            const bool acknowledged = std::exchange (nativeLfoDetailAcks[field], false);
            if (moved)
            {
                nativeLfoDetail.report (link);
                rebind = true;
            }
            reportNativeValue (e, base + n::fieldHandle (field), nativeLfoDetail.value (field),
                               display.view != n::Hidden && (moved || acknowledged));
        }
        reportNativeValue (e, base + n::kPointState, static_cast<float> (display.point) / n::LastPointState);
        reportNativeValue (e, base + n::kHeaderBack, display.headerBack ? 1.0f : 0.0f);
        reportNativeValue (e, base + n::kVisibleRows, static_cast<float> (display.rows) / n::kRows);
        reportNativeValue (e, base + n::kViewHandle, static_cast<float> (display.view) / n::LastView);
        if (rebind)
            requestNativeControlNames();
    }

    void preparePlayPresentation()
    {
        namespace ov = synthyboy_mpc::overlay;
        // Private models build the exact same compact bindings as Play without touching
        // the displayed page, its Q group, pending gestures or the audio engine.
        auto osc = std::make_unique<ov::OscUi>();
        std::string sourceName;
        const auto oscState = osc->prepareCompactPresentation (overlayView, sourceName);
        auto lfo = std::make_unique<ov::LfoUi>();
        lfo->prepareCompactPresentation (overlayView);
        const auto envLfo = ov::PlayEnvLfo::compactPresentation (overlayView, *lfo);
        std::lock_guard<std::mutex> guard (playPresentationMutex);
        if (oscState.preset != overlayView.stateGeneration() || oscState.selection != overlayView.playOscSelectionRevision()
            || envLfo.context.preset != oscState.preset || envLfo.context.selection != overlayView.nativePlayEnvLfoRevision())
            return;
        nativePlayOsc.publish (oscState, false);
        nativePlayEnvLfo.publish (envLfo, false);
        std::atomic_store (&nativePlayOscSourceName, std::make_shared<const std::string> (std::move (sourceName)));
        preparedOscDue.store (true, std::memory_order_release);
        preparedEnvLfoDue.store (true, std::memory_order_release);
    }

    void nativePlayOscPoll (AEffect* e)
    {
        constexpr int base = nativeui::kPlayOsc;
        nativePlayOsc.poll (nativeUiReported[base], [&] (int field, nativeui::FxCell cell) {
            return nativePlayOscValue (field, cell);
        }, [&] (int handle, float value, bool force) {
            reportNativeValue (e, base + handle, value, force);
        }, preparedOscDue.exchange (false, std::memory_order_acq_rel));
        if (nativePlayOscLabelChanged.exchange (false))
            requestProxyNames();
    }

    void nativePlayEnvLfoPoll (AEffect* e)
    {
        namespace n = nativeui::playenvlfo;
        constexpr int base = nativeui::kPlayEnvLfo;
        const float view = nativeUiValue (base);
        if ((! preparedEnvLfoDue.exchange (false, std::memory_order_acq_rel) && nativePlayEnvLfo.suspended()) || (view == 0.0f && nativeUiReported[base] == 0.0f))
            return;
        bool rebind = false;
        for (int field = 0; field < n::Count; ++field)
        {
            const auto link = nativePlayEnvLfo.at (field);
            const bool moved = nativePlayEnvLfo.report (link);
            const int offset = field == n::StandardSync ? nativeui::kPlayLfoStandardSync : base + 1 + field;
            reportNativeValue (e, offset, nativePlayEnvLfoValue (field), moved && view != 0.0f);
            rebind |= moved;
        }
        reportNativeValue (e, base, view);
        if (rebind && view != 0.0f)
            requestProxyNames();
    }

    void nativeLfoPoll (AEffect* e)
    {
        namespace n = nativeui::lfo;
        const uint64_t active = nativeLfo.publication();
        if (active == 0 && nativeLfoAnnounced == 0)
            return;
        const bool moved = active != nativeLfoAnnounced;
        std::array<float, n::kCount> values {};
        if (active != 0)
            for (int field = 0; field < n::kCount; ++field)
                values[field] = nativeLfo.value (field);
        if (nativeLfo.publication() != active)
            return;
        for (int field = 0; field < n::kCount; ++field)
            if (field != n::kLayout && field != n::kAction && (active & n::bit (field)) != 0)
                reportNativeValue (e, nativeui::kLfoBase + field, values[field], moved);
        reportNativeValue (e, nativeui::kLfoBase + n::kLayout, values[n::kLayout]);
        nativeLfo.report (active);
        nativeLfoAnnounced = active;
    }

    void nativeFilterPoll (AEffect* e)
    {
        for (int slot = 0; slot < 2; ++slot)
        {
            const auto link = nativePzActions.at (slot);
            reportNativeValue (e, nativeui::kPzAction + slot, 0.0f, nativePzAcks[slot]
                               || (link.cell().target >= 0 && nativePzActions.needsReport (link)));
            nativePzActions.report (link);
            nativePzAcks[slot] = false;
        }
        reportNativeValue (e, nativeui::kPzAvailability, nativeUiValue (nativeui::kPzAvailability));
        reportNativeValue (e, nativeui::kPzSource, nativeUiValue (nativeui::kPzSource));
        const float view = nativeUiValue (nativeui::kFilterBase);
        if (view == 0.0f && nativeUiReported[nativeui::kFilterBase] == 0.0f)
            return;
        for (int i = 0; i < nativeui::kFilterChainRoutes; ++i)
            reportNativeValue (e, nativeui::kFilterChainMenu + i, nativeUiValue (nativeui::kFilterChainMenu + i));
        bool changed = false;
        for (int slot = 0; slot < nativeui::FilterSlots; ++slot)
        {
            const auto link = nativeFilter.at (slot);
            const bool moved = nativeFilter.needsReport (link);
            reportNativeValue (e, nativeui::kFilterValueBase + slot, nativeFilterValue (slot), moved);
            const auto cell = link.cell();
            if (cell.target >= 0 && (slot == nativeui::FilterModel || slot == nativeui::FilterStyle))
                reportNativeValue (e, nativeui::kFxDomainBase + cell.kind - nativeui::kFxFirstDomain, nativeFxValue (cell), moved);
            if (moved)
            {
                nativeFilter.report (link);
                changed |= link.cell().target >= 0;
            }
        }
        for (int offset = nativeui::kFilterIndicatorBase; offset < nativeui::kOscBase; ++offset)
            reportNativeValue (e, offset, nativeUiValue (offset));
        reportNativeValue (e, nativeui::kFilterBase, view);
        if (changed)
            requestProxyNames ();
    }

    void nativeOscPoll (AEffect* e)
    {
        namespace n = nativeui::osc;
        const auto state = nativeOsc.snapshot();
        if (state.revision() == nativeOscAnnounced && state.source() == 0)
            return;
        const bool moved = state.revision() != nativeOscAnnounced;
        for (int source = 1; source <= n::kLastSource; ++source)
        {
            reportNativeValue (e, nativeui::kOscPower + source - 1, state.powerValue (source), moved && source == state.source());
            reportNativeValue (e, nativeui::kOscRoutingBySource + source - 1, state.routingValue (source), moved && state.displaysSource (source));
        }
        if (state.source() != 0)
        {
            if (state.visibility() == n::kRootOn)
                reportNativeValue (e, nativeui::kOscBase + n::kRouting, state.value (n::kRouting), moved);
            reportNativeValue (e, nativeui::kOscBase + n::kSource, state.value (n::kSource), moved);
        }
        reportNativeValue (e, nativeui::kOscBase + n::kVisibility, state.value (n::kVisibility));
        nativeOsc.report (state);
        nativeOscAnnounced = state.revision();
    }

    void nativeMasterPoll (AEffect* e)
    {
        namespace m = nativeui::master;
        constexpr int base = nativeui::kMasterOutput;
        const float view = nativeUiValue (nativeui::kMasterView);
        if (nativeMasterView.load (std::memory_order_relaxed) == m::Main)
        {
            const int polyphony = nativeMasterPolyphony.load (std::memory_order_relaxed);
            for (int field = 0; field < m::ClearClip; ++field)
                reportNativeValue (e, base + field, nativeUiValue (base + field),
                                   field == m::VoiceCount && polyphony != nativeMasterReportedPolyphony);
            nativeMasterReportedPolyphony = polyphony;
        }
        if (nativeMasterClearAck.exchange (false, std::memory_order_relaxed))
            reportNativeValue (e, base + m::ClearClip, 0.0f, true);
        const auto panel = nativeMasterPanel.at (0);
        reportNativeValue (e, nativeui::kMasterPanel, nativeUiValue (nativeui::kMasterPanel),
                           panel.cell().target >= 0 && nativeMasterPanel.needsReport (panel));
        nativeMasterPanel.report (panel);
        reportNativeValue (e, nativeui::kMasterScaleInput, nativeUiValue (nativeui::kMasterScaleInput));
        reportNativeValue (e, nativeui::kMasterView, view);
    }

    void nativeOscDetailPoll (AEffect* e)
    {
        namespace n = nativeui::oscdetail;
        constexpr int base = nativeui::kOscDetail;
        const float view = nativeUiValue (base);
        if (view == 0.0f && nativeUiReported[base] == 0.0f)
            return;
        bool namesChanged = false;
        for (int field = 0; field < n::kBindings; ++field)
        {
            const auto snapshot = nativeOscDetail.at (field);
            const bool rebound = nativeOscDetail.needsReport (snapshot);
            const auto cell = snapshot.cell();
            const bool rootLoad = field >= n::kRootLoad;
            const int handle = field < n::kMore ? n::kValue + field : n::kNavigation + field - n::kMore;
            const int offset = rootLoad ? nativeui::kOscRootLoad + field - n::kRootLoad : base + handle;
            reportNativeValue (e, offset, rootLoad ? 0.0f : oscDetailValue (handle, cell),
                               (rebound && cell.target >= 0) || nativeOscDetailAcks[field]);
            if (field < n::kCells)
            {
                reportNativeValue (e, base + n::kKind + field, oscDetailValue (n::kKind + field, cell));
                if (cell.kind >= n::kFirstMenuKind)
                {
                    const int menu = n::kMenusBase + cell.kind - n::kFirstMenuKind;
                    reportNativeValue (e, base + menu, oscDetailValue (menu, cell), rebound);
                }
            }
            if (rebound)
                nativeOscDetail.report (snapshot);
            nativeOscDetailAcks[field] = false;
            namesChanged |= rebound && cell.target >= 0;
        }
        reportNativeValue (e, nativeui::kOscRootLoadView, oscRootLoadView());
        reportNativeValue (e, base, view);
        if (namesChanged)
            requestNativeControlNames();
    }

    void nativeOscSettingsPoll (AEffect* e)
    {
        const float view = nativeUiValue (nativeui::kOscSettingsView);
        if (view == 0.0f && nativeUiReported[nativeui::kOscSettingsView] == 0.0f)
            return;
        bool changed = nativeOscSourceLabelChanged;
        nativeOscSourceLabelChanged = false;
        for (int field = 0; field < nativeui::osc::kSettingsAll; ++field)
        {
            const auto snapshot = nativeOscSettings.at (field);
            const bool moved = nativeOscSettings.needsReport (snapshot);
            const int offset = field < nativeui::osc::kSettings ? nativeui::kOscSettings + field
                : nativeui::kOscAbcSettings + field - nativeui::osc::kSettings;
            reportNativeValue (e, offset, nativeOscSettingValue (snapshot.cell()), moved || nativeOscSettingAcks[field]);
            nativeOscSettings.report (snapshot);
            nativeOscSettingAcks[field] = false;
            changed |= moved && snapshot.cell().target >= 0;
        }
        reportNativeValue (e, nativeui::kOscWarpLayout, nativeUiValue (nativeui::kOscWarpLayout));
        reportNativeValue (e, nativeui::kOscSettingsView, view);
        if (changed)
            requestNativeControlNames();
    }

    bool nativeFxHeaderPoll (AEffect* e)
    {
        namespace h = nativeui::fxheader;
        const auto state = nativeFxHeader.state();
        const auto band = nativeFxHeader.at (h::kBand);
        if (band.cell().target >= 0 && band.cell().target == state.item)
            reportNativeValue (e, nativeui::kFxBandBase + state.item, state.value (h::kBand), nativeFxHeader.needsReport (band));
        nativeFxHeader.report (band);
        reportNativeValue (e, nativeui::kFxBandMode, static_cast<float> (state.bandMode) / h::MidAndHigh);
        constexpr int base = nativeui::kFxHeader;
        nativeFxHeader.poll (nativeUiReported[base], [&] (int field, float value, bool force)
        {
            reportNativeValue (e, base + field, value, force);
        });
        reportNativeValue (e, nativeui::kFxEditorPower, state.on() ? 1.0f : 0.0f);
        return nativeFxHeader.takeLabelsChanged();
    }

    bool nativeFxChainPoll (AEffect* e)
    {
        namespace c = nativeui::fxchain;
        bool labelChanged = false;
        nativeFxChain.poll ([&] (int field, float value, bool force)
        {
            reportNativeValue (e, nativeui::kFxChain + field, value, force);
            labelChanged |= force && field >= c::Select && field < c::Gallery;
        });
        return labelChanged;
    }

    void refreshNativeMixerVisuals() noexcept
    {
        if (! mixerPeek.enabled() || engine == nullptr)
            return;
        uint32_t soloMask = 0;
        uint32_t muteMask = 0;
        for (int s = 0; s < nativeui::kMixerStrips; ++s)
        {
            if (engine->mixerSolo (s)) soloMask |= (1u << s);
            if (engine->mixerMute (s)) muteMask |= (1u << s);
        }
        const auto route = synthyboy::computeMixerRouteState (soloMask, muteMask, [this] (int idx) noexcept {
            return static_cast<unsigned> (idx) < synthyboy::kMaxParams
                ? paramMirror[idx].load (std::memory_order_relaxed) : 0.0f;
        });
        for (int s = 0; s < nativeui::kMixerStrips; ++s)
        {
            nativeMixerSoloVisual[s].store (route.soloVisual[s], std::memory_order_release);
            nativeMixerMuteVisual[s].store (route.muteVisual[s], std::memory_order_release);
        }
    }

    void nativeMixerPoll (AEffect* e)
    {
        refreshNativeMixerVisuals();
        const float tab = nativeUiValue (nativeui::kMixerActiveTab);
        // The host radio buttons compare against their own navigation handle before writing.
        reportNativeValue (e, nativeui::kMixerPeek, tab);
        reportNativeValue (e, nativeui::kMixerActiveTab, tab);
        for (int s = 0; s < nativeui::kMixerStrips; ++s)
        {
            reportNativeValue (e, nativeui::kMixerSoloVisual + s, nativeUiValue (nativeui::kMixerSoloVisual + s));
            reportNativeValue (e, nativeui::kMixerMuteVisual + s, nativeUiValue (nativeui::kMixerMuteVisual + s));
        }
    }

    bool fxCardChangedThisPoll = false; // display thread, consumed by the following Q poll
    bool fxQNamesDue = false;

    void nativeUiPoll (AEffect* e)
    {
        const float peek = nativeUiValue (nativeui::kMixerPeekView);
        const float lastPeek = nativeUiReported[nativeui::kMixerPeekView];
        if (lastPeek != 0.0f && lastPeek != peek)
            reportNativeValue (e, nativeui::kMixerPeekView, 0.0f);
        const auto envFrames = nativeEnvSelector.load (std::memory_order_relaxed);
        for (int offset = nativeui::kMixerPeakBase; offset < nativeui::kFxValueBase; ++offset)
        {
            const float value = offset >= nativeui::kEnvSelectorBase ? envSelectorValue (envFrames, offset - nativeui::kEnvSelectorBase)
                                                                   : nativeUiValues[offset].load (std::memory_order_relaxed);
            reportNativeValue (e, offset, value);
        }
        const float previousFxSource = nativeUiReported[nativeui::kFxHeader];
        bool fxLabelsChanged = nativeFxHeaderPoll (e);
        const float currentFxSource = nativeUiReported[nativeui::kFxHeader];
        fxCardChangedThisPoll = previousFxSource > 0.0f && currentFxSource > 0.0f && previousFxSource != currentFxSource;
        fxLabelsChanged |= nativeFxChainPoll (e);
        nativeLfoPoll (e);
        nativeLfoEditorPoll (e);
        nativeLfoDetailPoll (e);
        nativePlayEnvLfoPoll (e);
        nativePlayOscPoll (e);
        nativeFilterPoll (e);
        nativeOscPoll (e);
        nativeOscSettingsPoll (e);
        nativeOscDetailPoll (e);
        nativeMasterPoll (e);
        nativeMixerPoll (e);
        for (int offset = nativeui::kMixerStereoBase; offset < nativeui::kMasterScaleInput; ++offset)
            reportNativeValue (e, offset, nativeUiValue (offset));
        if (nativeMacroTargets.layout() || nativeMacroTargetAck >= 0)
        {
            for (int i = 0; i < nativeui::kMacroTargetCount; ++i)
            {
                const auto link = nativeMacroTargets.at (i);
                reportNativeValue (e, nativeui::kMacroTarget + i, 0.0f,
                                   i == nativeMacroTargetAck || nativeMacroTargets.needsReport (link));
                nativeMacroTargets.report (link);
            }
            nativeMacroTargetAck = -1;
        }
        nativeExpandedPlayPoll (e);
        nativeModTargetsPoll (e);
        nativeModPoll (e);
        nativeModNavigationPoll (e);
        if (nativeModDetailNavigation.layout())
        {
            for (int i = 0; i < nativeui::moddetail::kHandles; ++i)
            {
                const auto link = nativeModDetailNavigation.at (i);
                const float val = (i == nativeui::moddetail::Confirm)
                    ? (link.cell().source > 0 ? 1.0f : 0.0f) : 0.0f;
                reportNativeValue (e, nativeui::kModDetailControl + i, val,
                                   nativeModDetailNavigation.needsReport (link));
                nativeModDetailNavigation.report (link);
            }
        }
        {
            for (int i = 0; i < nativeui::modboard::kHandles; ++i)
            {
                const auto link = nativeModBoard.at (i);
                const float val = nativeUiValue (nativeui::kModBoardControl + i);
                reportNativeValue (e, nativeui::kModBoardControl + i, val,
                                   nativeModBoard.needsReport (link));
                nativeModBoard.report (link);
            }
        }
        nativeVoicePoll (e);
        nativeEnvPoll (e);
        const bool refreshFxLabels = ! fxCardChangedThisPoll
            && (currentFxSource > 0.0f || nativeFxChain.value (nativeui::fxchain::View) > 0.0f);
        const float layout = nativeUiValue (nativeui::kFxLayout);
        if (layout == 0.0f && nativeUiReported[nativeui::kFxLayout] == 0.0f)
        {
            reportNativeValue (e, nativeui::kMixerPeekView, peek);
            if (fxLabelsChanged && refreshFxLabels)
                requestProxyNames();
            return;
        }
        for (int slot = 0; slot < nativeui::kFxCells; ++slot)
        {
            const auto snapshot = nativeFxCells.at (slot);
            const auto cell = snapshot.cell();
            const bool moved = nativeFxCells.needsReport (snapshot);
            if (cell.target >= 0)
            {
                const int offset = nativeui::fxCellProxy (cell.kind) ? nativeui::kFxValueBase + slot
                                 : nativeui::kFxDomainBase + cell.kind - nativeui::kFxFirstDomain;
                reportNativeValue (e, offset, nativeFxValue (cell), moved);
            }
            reportNativeValue (e, nativeui::kFxKindBase + slot, static_cast<float> (cell.kind) / nativeui::kFxKindLast);
            if (moved)
            {
                nativeFxCells.report (snapshot);
                fxLabelsChanged |= cell.target >= 0;
            }
        }
        reportNativeValue (e, nativeui::kFxLayout, layout);
        reportNativeValue (e, nativeui::kMixerPeekView, peek);
        if (fxLabelsChanged && refreshFxLabels)
            requestProxyNames();
    }

    void nativeEnvPoll (AEffect* e)
    {
        const float view = nativeUiValue (nativeui::kEnvView);
        if (view == 0.0f && nativeUiReported[nativeui::kEnvView] == 0.0f)
            return;
        bool changed = false;
        for (int field = 0; field < nativeui::env::kFields; ++field)
        {
            const auto snapshot = nativeEnvEditor.at (field);
            const int target = snapshot.cell().target;
            bool report = nativeEnvEditor.needsReport (snapshot);
            if (field == nativeui::env::Targets && target >= 0)
            {
                const int count = nativeEnvTargetCount.load (std::memory_order_relaxed);
                report |= count != nativeEnvTargetCountReported;
                nativeEnvTargetCountReported = count;
            }
            reportNativeValue (e, nativeui::kEnvFields + field, nativeEnvValue (field, target), report);
            nativeEnvEditor.report (snapshot);
            changed |= report && target >= 0;
        }
        reportNativeValue (e, nativeui::kEnvView, view);
        if (changed)
            requestNativeControlNames();
    }

    void nativeExpandedPlayPoll (AEffect* e)
    {
        constexpr int base = nativeui::kPlayExpanded;
        const float view = nativeUiValue (base);
        if (view == 0.0f && nativeUiReported[base] == 0.0f)
            return;
        for (int field = 0; field < nativeui::expandedplay::Actions; ++field)
        {
            const auto link = nativeExpandedPlay.at (field);
            const bool rebound = nativeExpandedPlay.needsReport (link);
            reportNativeValue (e, base + 1 + field, 0.0f, field == nativeExpandedPlayAck || (rebound && link.cell().target >= 0));
            nativeExpandedPlay.report (link);
        }
        nativeExpandedPlayAck = -1;
        reportNativeValue (e, base, view);
    }

    void nativeModTargetsPoll (AEffect* e)
    {
        namespace n = nativeui::targets;
        constexpr int base = nativeui::kModTargets;
        const auto view = nativeUiValue (base + n::kView);
        if (view == 0.0f && nativeUiReported[base + n::kView] == 0.0f)
            return;
        bool names = false;
        for (int field = 0; field < n::kFields; ++field)
        {
            const auto link = nativeModTargets.at (field);
            const auto cell = link.cell();
            const bool rebound = nativeModTargets.needsReport (link);
            reportNativeValue (e, base + field, nativeModTargetValue (field, cell),
                               nativeModTargetAcks[field] || (rebound && cell.target >= 0));
            nativeModTargetAcks[field] = false;
            names |= rebound && cell.target >= 0;
            nativeModTargets.report (link);
        }
        for (int field = n::kPageGate; field < n::kHandles; ++field)
            reportNativeValue (e, base + field, nativeUiValue (base + field));
        reportNativeValue (e, base + n::kView, view);
        if (names)
            requestNativeControlNames();
    }

    void nativeModPoll (AEffect* e)
    {
        const float view = nativeUiValue (nativeui::kModView);
        if (view == 0.0f && nativeUiReported[nativeui::kModView] == 0.0f)
            return;
        for (int field = 0; field < nativeui::mod::kFields; ++field)
        {
            const auto snapshot = nativeMod.at (field);
            const int real = snapshot.cell().target;
            const int offset = nativeui::kModFields + field;
            reportNativeValue (e, offset, real >= 0 ? qReadNorm (real) : 0.0f, nativeMod.needsReport (snapshot));
            nativeMod.report (snapshot);
        }
        for (int row = nativeui::kModRows; row < nativeui::kModFields; ++row)
            reportNativeValue (e, row, nativeUiValue (row));
        reportNativeValue (e, nativeui::kModView, view);
    }

    void nativeVoicePoll (AEffect* e)
    {
        namespace v = nativeui::voice;
        if (! nativeVoice.layout())
        {
            nativeVoiceDisplayedRow = -1;
            return;
        }
        const auto row = nativeVoice.at (v::kRow);
        const bool entering = nativeVoiceDisplayedRow < 0;
        for (int field = 0; field < v::kCount; ++field)
        {
            const auto snapshot = field == v::kRow ? row : nativeVoice.at (field);
            if (snapshot.cell().target < 0)
                continue;
            // Equal normalized values can have different text (MID, 0 c, 0%).
            const bool formatChanged = field == v::kValue && snapshot.cell().source / v::kSteps != nativeVoiceDisplayedRow;
            reportNativeValue (e, nativeui::kVoiceSequence + field, nativeVoiceValue (field, snapshot.cell()), entering || formatChanged);
            if (field == v::kValue)
                nativeVoiceDisplayedRow = snapshot.cell().source / v::kSteps;
            if (nativeVoice.needsReport (snapshot))
                nativeVoice.report (snapshot);
        }
    }

    void nativeModNavigationPoll (AEffect* e)
    {
        namespace n = nativeui::mod;
        const int base = nativeui::kModNavigation;
        const float mode = nativeModNavigationValue (n::Mode);
        if (mode == 0.0f && nativeUiReported[base + n::Mode] == 0.0f)
            return;
        const auto content = std::atomic_load (&overlayContent);
        bool changed = content != nativeModReportedContent;
        nativeModReportedContent = content;
        for (int field = n::SourceGroup; field < n::kNavigation; ++field)
        {
            const auto snapshot = nativeModNavigation.at (field);
            const int choice = nativeModDisplayChoice (field, snapshot.cell());
            const bool moved = nativeModNavigation.needsReport (snapshot) || choice != nativeModReportedChoices[field];
            nativeModReportedChoices[field] = choice;
            reportNativeValue (e, base + field, nativeModNavigationValue (field), moved);
            nativeModNavigation.report (snapshot);
            changed |= moved;
        }
        const auto snapshot = nativeModNavigation.at (n::Mode);
        reportNativeValue (e, base + n::Mode, mode);
        nativeModNavigation.report (snapshot);
        if (changed)
            requestNativeControlNames();
    }

    /** The normalised value of a real parameter (table index) as the host reads it. */
    float qReadNorm (int tableIndex) const noexcept { return qBank.readNorm (tableIndex); }
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
        if (line == qFakeLast && qBindingsReady())
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
        const uint64_t revision = overlayView.qBindingRevision();
        qBank.setGroup (targets, n);
        overlayView.finishQBindingUpdate (revision);
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
        qBank.router().setStepTravel (synthyboy_mpc::safefile::readNumber ("/tmp/synthyboy-overlay-q-step-travel", n) && n > 0.0 && n < 1.0e6 ? static_cast<float> (n / 128.0) : synthyboy_mpc::QRouter::kStepTravelDefault);
    }
#else
    void qFakeGroup() noexcept {}
    void qFakeWrite (AEffect*) noexcept {}
    void qStepTravelFile() noexcept {}
#endif
#if SYNTHYBOY_MPC_DEBUG_HOOKS
    /** Remote program (preset) select hook (SYNTHYBOY_MPC_DEBUG_HOOKS only; docs/MPC.md "Debug hooks"):
        polls /tmp/synthyboy-select-program (SYNTHYBOY_MPC_SELECT_PROGRAM_FILE changes the path). Display thread. */
    void remoteProgramSelectPoll (AEffect* e);
#else
    void remoteProgramSelectPoll (AEffect*) noexcept {}
#endif
    /** The display thread, every tick: the host is told the Q values after a group change or a change of a target, and the real parameter a Q write changed. */
    void qPoll (AEffect* e)
    {
        qStepTravelFile();
        qFakeGroup();
        qFakeWrite (e);
        if (! audioMaster)
            return;
        if (programNameRefreshDue.exchange (false, std::memory_order_acq_rel))
            audioMaster (e, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);   // the host reads the program name again (and drops its own mark)
        const VstInt32 base = qBase();
        const bool switchedFxCard = std::exchange (fxCardChangedThisPoll, false);
        if (! switchedFxCard && std::exchange (fxQNamesDue, false))
            requestProxyNames ();
        qBank.poll ([&] (int q, float v) { audioMaster (e, audioMasterAutomate, base + q, 0, nullptr, v); },
                    [&] (int real) { audioMaster (e, audioMasterAutomate, real, 0, nullptr, qReadNorm (real)); },
                    [&] {
                        if (switchedFxCard)
                            fxQNamesDue = true;
                        else
                            requestProxyNames ();
                    });
        flushProxyNames (e, steadyMs());
    }
    /** The group changed: the names, units and value texts of the Q proxies are other now. The host keeps what it read for good unless it is told (measured on the Key 61, docs/MPC.md "Q proxies and the
        plugin-drawn pages"): audioMasterUpdateDisplay makes it read them again, audioMasterIOChanged does not. */
    void qNotifyGroup (AEffect* e)
    {
        audioMaster (e, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);
    }

    // Allow one primary-frame update before asking the MPC to rebuild parameter menus.
    // A burst of rebinding controls shares the first request's deadline.
    void requestNativeControlNames()
    {
        requestProxyNames();
        nativeNamesAwaitingPaint = true;
    }

    void requestProxyNames ()
    {
        if (proxyNamesRequestedAt < 0)
            proxyNamesRequestedAt = steadyMs();
    }

    void flushProxyNames (AEffect* e, long long nowMs)
    {
        if (proxyNamesRequestedAt < 0)
            return;
        // A full metadata refresh rebuilds the MPC menus and postpones its primary-plane redraw.
        // Publish the new skin controls first. Even an unreadable skin must eventually get the names.
        constexpr long long maxWaitMs = 1000;
        if (nativeMixerPeekView.load (std::memory_order_acquire) == nativeui::MixerPeekFilter
            && ! hostMixerWidgetsHidden.load (std::memory_order_relaxed) && nowMs - proxyNamesRequestedAt < maxWaitMs)
            return;
        if ((nativeNamesAwaitingPaint && nowMs - proxyNamesRequestedAt < kNativePaintWindowMs)
            || (nowMs < nativePagePaintUntil && nowMs - proxyNamesRequestedAt < maxWaitMs))
            return;
        nativeNamesAwaitingPaint = false;
        proxyNamesRequestedAt = -1;
        qNotifyGroup (e);
    }

    static VstInt32 playBase() noexcept { return overlayParamIndex() + 1; }   // "Play Osc"; "Play Env" and "Play LFO" follow, then the proxies
    static bool isPlayParam (VstInt32 index) noexcept { return index >= playBase() && index < playBase() + synthyboy_mpc::play::kParams; }
    static bool isPlaySelector (VstInt32 index) noexcept { return index >= playBase() && index < playBase() + synthyboy_mpc::play::kSelectors; }
    static int playProxyOf (VstInt32 index) noexcept { return index - playBase() - synthyboy_mpc::play::kSelectors; }   // 0 .. 23
    static int playGroupOfProxy (int proxy) noexcept { return proxy / synthyboy_mpc::play::kSlots; }
    static int playSlotOfProxy (int proxy) noexcept { return proxy % synthyboy_mpc::play::kSlots; }
    enum class PlayProxyAccess { Read, Write };
    /** Real parameter of the selected instance. The native oscillator dials are read-only while their source is off. */
    int playRealOf (int proxy, PlayProxyAccess access = PlayProxyAccess::Read) const noexcept
    {
        namespace pl = synthyboy_mpc::play;
        const int g = playGroupOfProxy (proxy);
        const int instance = playSel[g].load (std::memory_order_relaxed);
        const int slot = playSlotOfProxy (proxy);
        if (access == PlayProxyAccess::Write && g == 0 && (slot == pl::kOscPanSlot || slot == pl::kOscLevelSlot))
        {
            const int power = pl::oscPowerIndex (instance);
            if (power < 0 || paramMirror[power].load (std::memory_order_relaxed) < 0.5f)
                return -1;
        }
        if (access == PlayProxyAccess::Write && g == 2 && slot == pl::kLfoSmoothTimeSlot)
        {
            const int enabled = pl::realIndex (g, instance, pl::kLfoSmoothOnSlot);
            if (enabled < 0 || paramMirror[enabled].load (std::memory_order_relaxed) < 0.5f)
                return -1;
        }
        if (g == 1)
        {
            const int beats = pl::envBeatsIndex (instance, slot);
            const int units = pl::envUnitsIndex (instance);
            if (beats >= 0 && units >= 0 && paramMirror[units].load (std::memory_order_relaxed) >= 0.5f)
                return beats;
        }
        return pl::realIndex (g, instance, slot);
    }
    bool playLfoIsStandard() const noexcept
    {
        return tabViewState[synthyboy_mpc::overlay::IInstanceView::kTabViewRnd].load (std::memory_order_relaxed) == 0;
    }
    bool playProxyBlockedByRandomLfo (int proxy) const noexcept
    {
        namespace pl = synthyboy_mpc::play;
        const int slot = playSlotOfProxy (proxy);
        return playGroupOfProxy (proxy) == 2 && ! playLfoIsStandard()
            && (slot == pl::kLfoRiseSlot || slot == pl::kLfoSmoothTimeSlot || slot == pl::kLfoSmoothOnSlot);
    }
    float playTargetValue (int real) const noexcept
    {
        if (real < 0)
            return 0.0f;
        return synthyboy::toNormalised (synthyboy::paramTable()[real], paramMirror[real].load (std::memory_order_relaxed));
    }
    float playProxyValue (int proxy) const noexcept { return playTargetValue (playRealOf (proxy)); }
    void playMarkRealDue (unsigned real) noexcept
    {
        playRealDue[real / kPlayReportWordBits].fetch_or (uint32_t { 1 } << (real % kPlayReportWordBits), std::memory_order_release);
    }
    /** Sets a selector (any thread; the value is limited to the instances the engine has) and has the display thread report the group. */
    void playSetSelector (int group, int instance) noexcept
    {
        instance = std::min (std::max (instance, 1), synthyboy_mpc::play::kInstances[group]);
        if (group == 1 || group == 2)
            playLastSel.store (group, std::memory_order_relaxed);
        if (playSel[group].exchange (instance, std::memory_order_acq_rel) != instance)
        {
            if (group == 0)
                playOscSelectionRev.fetch_add (1, std::memory_order_release);
            else
                playEnvLfoRevision.fetch_add (1, std::memory_order_release);
            playGroupDue[group].store (true, std::memory_order_release);
        }
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
        if (synthyboy_mpc::PlayLog::enabled())
        {
            const auto ms = steadyMs();
            const int master = nativeMasterView.load (std::memory_order_relaxed);
            const int voice = nativeVoice.layout();
            if (ms >= nextUiLogMs || master != loggedMasterView || voice != loggedVoiceView)
            {
                synthyboy_mpc::PlayLog::line ("ui-state instance=%p uptime_ms=%lld master=%d voice=%d",
                    static_cast<void*> (this), ms, master, voice);
                synthyboy_mpc::PlayLog::line ("ui-reads instance=%p name=%u label=%u display=%u properties=%u dispatch_ns=%llu",
                    static_cast<void*> (this), uiNameReads.load (std::memory_order_relaxed), uiLabelReads.load (std::memory_order_relaxed),
                    uiTextReads.load (std::memory_order_relaxed), uiPropsReads.load (std::memory_order_relaxed),
                    static_cast<unsigned long long> (uiReadNs.load (std::memory_order_relaxed)));
                nextUiLogMs = ms + 1000;
                loggedMasterView = master;
                loggedVoiceView = voice;
            }
        }
        synthyboy_mpc::PlayLog::drain();   // the lines the host's threads put into the ring, written here: the only place the play log touches a file
        bool vst = false, display = false;
        if (synthyboy_mpc::PlayLog::takeDumpRequest (overlaySlot.load (std::memory_order_relaxed), vst, display))
            playDump (e, vst, display);
#endif
        if (! playLastValid)
        {
            for (int p = 0; p < pl::kProxies; ++p)
            {
                playLastTarget[p] = playRealOf (p);
                playLast[p].store (playTargetValue (playLastTarget[p]), std::memory_order_relaxed);
            }
            // RND can be restored before the first poll. A cached zero
            // would suppress its first announcement to the compact skin.
            nativeUiReported[nativeui::kPlayLfoStandard] = -1.0f;
            playLastValid = true;
        }
        for (int g = 0; g < pl::kGroups; ++g)
            if (playGroupDue[g].exchange (false, std::memory_order_acq_rel))
            {
                audioMaster (e, audioMasterAutomate, base + g, 0, nullptr, pl::selectorNormalised (g, playSel[g].load (std::memory_order_relaxed)));
                for (int s = 0; s < pl::kSlots; ++s)
                {
                    const int p = g * pl::kSlots + s;
                    playLastTarget[p] = playRealOf (p);
                    const float v = playTargetValue (playLastTarget[p]);
                    playLast[p].store (v, std::memory_order_relaxed);
                    audioMaster (e, audioMasterAutomate, base + pl::kSelectors + p, 0, nullptr, v);
                }
            }
        bool targetChanged = false;
        for (int p = 0; p < pl::kProxies; ++p)
        {
            const int real = playRealOf (p);
            const float v = playTargetValue (real);
            const bool retargeted = playLastTarget[p] != real;
            if (retargeted || std::fabs (v - playLast[p].load (std::memory_order_relaxed)) > 1.0e-6f)
            {
                playLastTarget[p] = real;
                playLast[p].store (v, std::memory_order_relaxed);
                audioMaster (e, audioMasterAutomate, base + pl::kSelectors + p, 0, nullptr, v);
                targetChanged |= retargeted;
            }
        }
        if (targetChanged)
            requestProxyNames();

        reportNativeValue (e, nativeui::kPlayLfoStandard, nativeUiValue (nativeui::kPlayLfoStandard));

        // Retargeting a proxy between writes can leave several real parameters to report.
        for (size_t word = 0; word < std::size (playRealDue); ++word)
        {
            if (playRealDue[word].load (std::memory_order_relaxed) == 0)
                continue;
            uint32_t pending = playRealDue[word].exchange (0, std::memory_order_acquire);
            for (int written = static_cast<int> (word * kPlayReportWordBits); pending != 0; ++written, pending >>= 1)
                if ((pending & 1u) != 0)
                    audioMaster (e, audioMasterAutomate, written, 0, nullptr, playTargetValue (written));
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
        const bool hideWidgets = uiFullOpen.load (std::memory_order_relaxed) && ! mixerPeek.enabled();
        const float real = hideWidgets ? synthyboy_mpc::overlay::fullScreenParamValue() : synthyboy_mpc::overlay::instanceParamValue (overlaySlot.load (std::memory_order_relaxed));
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
        synthyboy_mpc::overlay::SynthyBoyPageFactory::targetsOfAllPages (idx);
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
        // the plugin's tabs (overlay_layout.h) and the maker of their views, and the line of the status file only SynthyBoy knows (the quality profile of the device); the first instance of the process decides (service.h)
        static synthyboy_mpc::overlay::SynthyBoyPageFactory pageFactory;
        synthyboy_mpc::overlay::Service::instance().setStatus ([] { return "quality_profile: " + synthyboy_mpc::describeProfile (synthyboy_mpc::profileInForce().profile) + " (" + synthyboy_mpc::profileInForce().source + ")\n" + synthyboy_mpc::noteDiagnostics().formatLine(); });
        const int slot = synthyboy_mpc::overlay::Service::instance().attach (&overlayView, &mpckit::synthyboy_adapter::PageSet::get(), &pageFactory);   // the slot stays this instance's until effClose; -1: more than 16 instances
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

    /** The display thread's whole life (called by startLive's thread). The thread is the kit's (kit/display_thread.h, migration step 11 of docs/ui-components/TEMPLATE.md): SynthyBoy REGISTERS its features below, in the
        order they run each tick, and the class does the rest - every feature inside a PollGuard (poll_guard.h: an exception is caught, counted, written to the status file and, five times in a row, switches that feature
        off - it never leaves the thread: std::terminate would end the MPC application), the grid of ticks (a short period while a cursor or a spectrum column moves, a long one otherwise) and the scheduling of the
        thread (normal policy, nice 0, all CPUs: not lower than normal, this thread holds host locks while it is inside audioMasterAutomate, and a starved holder would make the host's own threads wait for it). */
    void liveLoop (AEffect* e)
    {
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
        const std::chrono::milliseconds active (1000 / (SYNTHYBOY_MPC_LFO_LIVE_HZ > 0 ? SYNTHYBOY_MPC_LFO_LIVE_HZ : 1));
        const std::chrono::milliseconds idle (100);   // nothing sounds: a look ten times a second, no call to the host
        const auto start = std::chrono::steady_clock::now();
        int last[synthyboy_mpc::kLfoLiveCount] = {};
        mpckit::DisplayThread display (this, "sb-lfo-display", { active, idle });
        display.setAlwaysActive (SYNTHYBOY_MPC_LFO_LIVE_SELFTEST
#if SYNTHYBOY_MPC_SPECTRUM
                                 || SYNTHYBOY_MPC_SPECTRUM_SELFTEST
#endif
                                 );   // (the measurements sweep the cursors without a voice)
#if SYNTHYBOY_MPC_SPECTRUM
        synthyboy_mpc::SpectrumAnalyser analyser;   // allocates here, once, on this thread
        float specLast[synthyboy_mpc::kSpectrumCount] = {};
#endif
#if SYNTHYBOY_MPC_OVERLAY
#if defined(__linux__)
        unsigned cpuPlanSeen = 0;   // applied on the passes of run () only: run () resets the affinity first (useNormalScheduling), so a call here would mark the plan seen and be undone
        display.addUnguarded ([&] { synthyboy_mpc::overlay::Service::applyCpusToThisThread (cpuPlanSeen); });
#endif
        bool readoutPublished = false;
        display.add ("overlay report", [&] { injectFault ("overlay"); overlayPoll (e, PluginInstance::overlayParamIndex()); });   // "Overlay Instance": the first parameter of the overlay block (after the spectrum columns and the pad pressure parameters)
        display.add ("Play proxies", [&] { injectFault ("play"); playPoll (e, PluginInstance::playBase()); });                     // the Play page: the selectors and the proxies after it
        display.add ("Native UI", [&] { nativeUiPoll (e); });
        display.add ("Q proxies", [&] { injectFault ("q"); qPoll (e); });                                                          // the Q proxies after them
        display.add ("page writes", [&] { injectFault ("ui"); uiPoll (e); });                                                      // what the plugin-drawn pages wrote
        display.add ("content loading", [&] { injectFault ("content"); contentPoll(); });                                          // a sample or an instrument the browser asked for
        display.add ("LFO shape", [&] { injectFault ("shape"); lfoShapePoll(); });                                                 // a drawn LFO shape the LFO page wrote
        display.add ("Scalar curve", [&] { scalarCurvePoll(); });                                                                  // a NOTE / VELO curve the curve editor wrote
        display.add ("PZ filter curve", [&] { filterCurvePoll(); });
        display.add ("macro name", [&] { injectFault ("macro"); macroNamePoll (e); });                                             // a name the Macros page gave a macro
#if SYNTHYBOY_MPC_DEBUG_HOOKS
        display.add ("program select", [&] { injectFault ("program_select"); remoteProgramSelectPoll (e); });                       // remote program (preset) select hook
#endif
        display.addUnguarded ([&]
        {
            if (! readoutPublished)   // once, after the first report of the overlay slot above (see publishReadoutTargets)
            {
                readoutPublished = true;
                try { publishReadoutTargets(); } catch (...) {}
            }
            if (engine)
                synthyboy_mpc::startupTimes().setWavetableMs (engine->wavetableBuildMs());   // the background builder goes on after prepare: the status shows its running total
        });
#endif
        display.add ("LFO cursors", [&]
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
            bool anyActive = false;
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
            display.setBusy (anyActive);   // the grid of the next tick: the short period while a cursor moves (the spectrum below may add to it)
        });
#if SYNTHYBOY_MPC_SPECTRUM
        display.add ("spectrum", [&]
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
            bool anyActive = display.busy();
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
            display.setBusy (anyActive);
        });
#endif
        (void) start;
        display.run ([this] { return liveStop.load (std::memory_order_relaxed) || closing.load (std::memory_order_acquire); },
                     [this] { return hostKnows.load (std::memory_order_acquire); });   // not ready: the host has not called us yet, so it may not hold this effect's pointer: no callback before it does, and no report is taken out of its flag meanwhile (robustness review F12)
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
        b += std::string (" PROGRAM_GROUPS=") + synthyboy_mpc::groupModeName (static_cast<synthyboy_mpc::GroupMode> (SYNTHYBOY_MPC_PROGRAM_GROUPS));
#if SYNTHYBOY_MPC_PAD_PRESSURE
        extraParams += synthyboy_mpc::kPadPressureParams;
        b += " PAD_PRESSURE=ON";
#else
        b += " PAD_PRESSURE=OFF";
#endif
#if SYNTHYBOY_MPC_OVERLAY
        extraParams += 1 + synthyboy_mpc::play::kParams + synthyboy_mpc::QRouter::kCount + nativeui::kCount;
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
        qBank.bind (&overlayView);   // the proxies read the values of the targets through the overlay view (the parameter mirror)
#endif
        outputStage.prepare (sampleRate > 0.0 ? sampleRate : 44100.0);
        applyTierTrim();
        outputStage.reset();
        applyOutputLimiter();
        engine = synthyboy::createEngine(synthyboy::EngineKind::own);
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
            {
                // the preset index when it is valid and current (one small file, docs/MPC.md "The preset index"), else the scan; user presets outside this folder are not in the index
                std::string source;
                programs = synthyboy_mpc::loadPrograms (contentDir + "/presets", &source);
                MPCKIT_LOG ("startup", info, "%zu programs from %s", programs.size(), source.c_str());
            }
        }
        view = synthyboy_mpc::buildProgramView (programs, static_cast<synthyboy_mpc::GroupMode> (SYNTHYBOY_MPC_PROGRAM_GROUPS));
        synthyboy_mpc::startupTimes().setProgramMode (synthyboy_mpc::groupModeName (view.mode), view.numPrograms());
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
#if SYNTHYBOY_MPC_OVERLAY
        preparePlayPresentation();
#endif
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
    if (prog >= 1 && prog < inst->view.numPrograms() && ! inst->view.isHeader (prog))
        return inst->view.items[static_cast<size_t> (prog)].baseName;
    return "Init";
}

/** The name of the current program as the host's list shows it (the label of the mode); in the "no program" state the saved name. */
static std::string currentProgramLabel (PluginInstance* inst)
{
    {
        std::lock_guard<std::mutex> g (inst->programMutex);
        if (inst->noProgram)
            return inst->noProgramName.empty() ? "Init" : inst->noProgramName;
    }
    const int prog = static_cast<int> (inst->currentProgram.load (std::memory_order_relaxed));
    if (prog >= 1 && prog < inst->view.numPrograms() && ! inst->view.isHeader (prog))
        return inst->view.label (prog);
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
        const int tv[synthyboy_mpc::tabview::kFields] = { inst->tabViewState[0].load (std::memory_order_relaxed), inst->tabViewState[1].load (std::memory_order_relaxed) };
        chunk = synthyboy_mpc::tabview::chunkWithTabView (std::move (chunk), tv);   // after the Play selectors, before the limiter and pad pressure members
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
    const size_t count = synthyboy::paramCount();
    if (inst->engine)
        inst->engine->readyContentFor (values.plain, count);   // not the audio thread: the tables the state names are built BEFORE its parameters are set, so index and table take effect together
    for (size_t i = 0; i < count; ++i)
    {
        inst->paramMirror[i].store(values.plain[i], std::memory_order_relaxed);
#if SYNTHYBOY_MPC_OVERLAY
        inst->paramUndo[i].store (std::nanf (""), std::memory_order_relaxed);   // a whole new state: no "value before the last change" any more
#endif
    }
    if (inst->engine)
        inst->engine->setPresetParams (values);
#if SYNTHYBOY_MPC_OVERLAY
    inst->ensureUserWavetableRootsFor (content);   // a file of the user wavetable folders: the drives are pack roots before the engine reads the reference (the lock below is not held yet)
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
    inst->scaleInput.store (PluginInstance::ScaleInputHidden);
    inst->publishOverlayContent();
    if (namesChanged)
        inst->macroNamesDue.store (true, std::memory_order_release);   // the display thread tells the host the names of the Q proxies are other now
#endif
    inst->stateGeneration.fetch_add (1, std::memory_order_release);
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
    bool integerRange = (info.flags & synthyboy::kParamStepped) != 0;
#if SYNTHYBOY_MPC_OVERLAY
    // Master NumericOverlay needs plain integer bounds. These stepped values
    // use a linear engine scale and each already constrains writes to integers.
    if (info.interval == 1.0f && std::strncmp (info.key, "master.", 7) == 0)
        integerRange = true;
#endif
    if (integerRange)
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
    {
        const auto ns = synthyboy_mpc::StartupTimes::nowNs() - t0;
        synthyboy_mpc::startupTimes().callDone (static_cast<synthyboy_mpc::StartupTimes::Call> (slot), ns);
#if SYNTHYBOY_MPC_DEBUG_HOOKS && SYNTHYBOY_MPC_OVERLAY
        if (slot == synthyboy_mpc::StartupTimes::kCallParamInfo && synthyboy_mpc::PlayLog::enabled() && e && e->object)
            getInstance (e)->countUiRead (opcode, ns);
#endif
    }
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
    // a latency change (the LIMIT switch, a chunk, Bode's latency) is told here: never on the audio thread (effProcessEvents), never while closing
    if (inst && opcode != effProcessEvents && opcode != effClose && inst->latencyDirty.load (std::memory_order_acquire) && inst->latencyDirty.exchange (false, std::memory_order_acq_rel))
    {
        const int lat = inst->latencyNow();
        if (lat != inst->latencyReported.load (std::memory_order_relaxed))
        {
            e->initialDelay = static_cast<VstInt32> (lat);
            inst->latencyReported.store (lat, std::memory_order_relaxed);
            inst->audioMaster (e, audioMasterIOChanged, 0, 0, nullptr, 0.0f);
        }
    }

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
        synthyboy_mpc::startupTimes().noteProgramCall (synthyboy_mpc::StartupTimes::kPcSetProgram, prog);
        int numProgs = inst->view.numPrograms();
        // Out of range: do nothing
        if (prog < 0 || prog >= numProgs) return 1;

        // Check if protection is armed for this index BEFORE disarming
        bool wasProtected = (inst->protectedProgram.load (std::memory_order_relaxed) == prog);
        // A header of the list (mode headers) loads a preset of its group (program_list.h effectiveProgram)
        if (inst->view.isHeader (prog))
        {
            prog = synthyboy_mpc::effectiveProgram (inst->view, prog, inst->currentProgram.load (std::memory_order_relaxed));
            if (prog < 0) return 1;
            wasProtected = false;
        }
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
#if SYNTHYBOY_MPC_OVERLAY
            const PluginInstance::QBindingRestore qBindingRestore (*inst);
#endif
            applyState (inst, synthyboy::ParamValues::defaults (), synthyboy::StateContent {});
            inst->outputLimiter.store (synthyboy_mpc::kOutputLimiterDefaultOn, std::memory_order_relaxed);   // the default sound: no limiter
            inst->applyOutputLimiter();
            inst->tierTrimDb.store (0.0f, std::memory_order_relaxed);
            inst->applyTierTrim();
            synthyboy_mpc::noteDiagnostics().setTierTrimDb (0.0f);
        }
        else
        {
            // Program >= 1: load preset file
            const auto& entry = inst->programs[static_cast<size_t> (inst->view.items[static_cast<size_t> (prog)].preset)];
            std::string data;
            if (! synthyboy_mpc::safefile::readWholeBounded (entry.path.c_str(), data, kMaxPresetFileBytes))
                return 1; // file cannot be read, is not a regular file or is larger than a preset can be (4 MB; a preset is a few KB)

            synthyboy::PresetMeta meta;
            synthyboy::StateContent content;
            synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
            synthyboy::LoadResult result = synthyboy::loadPreset (data.data(), data.size(), meta, values, content);
            if (result.status != synthyboy::LoadStatus::ok && result.status != synthyboy::LoadStatus::newer_version)
                return 1; // load failed

#if SYNTHYBOY_MPC_OVERLAY
            const PluginInstance::QBindingRestore qBindingRestore (*inst);
#endif
            applyState (inst, values, std::move(content));
            // the preset says whether its sound needs the output limiter (the member "outputLimiter"; without it the default: off). The project's chunk then carries it.
            bool limiterOn = synthyboy_mpc::kOutputLimiterDefaultOn;
            synthyboy_mpc::outputLimiterFromPreset (data.data(), data.size(), limiterOn);
            inst->outputLimiter.store (limiterOn, std::memory_order_relaxed);
            inst->applyOutputLimiter();

            float trimDb = 0.0f;
            synthyboy_mpc::tierTrimDbFromPreset (data.data(), data.size(), trimDb);
            inst->tierTrimDb.store (trimDb, std::memory_order_relaxed);
            inst->applyTierTrim();
            synthyboy_mpc::noteDiagnostics().setTierTrimDb (trimDb);
        }

#if SYNTHYBOY_MPC_OVERLAY
        inst->preparePlayPresentation();
#endif
        inst->currentProgram.store (prog, std::memory_order_relaxed);
        inst->setNoProgram (false, std::string());
        // Request display update from host
        if (inst->audioMaster)
            inst->audioMaster (e, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);
        return 1;
    }

    case effGetProgram:
    {
        if (!inst) return 0;
        synthyboy_mpc::startupTimes().noteProgramCall (synthyboy_mpc::StartupTimes::kPcGetProgram, 0);
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
        synthyboy_mpc::startupTimes().noteProgramCall (synthyboy_mpc::StartupTimes::kPcGetProgramName, 0);
        std::string progName = currentProgramLabel (inst);
        copyString(name, kVstMaxProgNameLen + 1, synthyboy_mpc::truncateUtf8 (progName, synthyboy_mpc::kProgramNameMax).c_str());
        return 1;
    }

    case effGetProgramNameIndexed:
    {
        char* name = static_cast<char*>(ptr);
        if (inst)
            synthyboy_mpc::startupTimes().noteProgramCall (synthyboy_mpc::StartupTimes::kPcGetProgramNameIndexed, static_cast<int> (value));
        if (!inst || !name || index < 0) return 0;
        // value = the VST2 category (-1: no category); only the mode vst-categories looks at it. The buffer is char[kVstMaxProgNameLen + 1]: the label is cut to what fits.
        const int n = synthyboy_mpc::programOfIndexed (inst->view, index, static_cast<int> (value), inst->hostAskedCategories.load (std::memory_order_relaxed));
        if (n < 0) return 0;
        copyString(name, kVstMaxProgNameLen + 1, synthyboy_mpc::truncateUtf8 (inst->view.label (n), synthyboy_mpc::kProgramNameMax).c_str());
        return 1;
    }

    case effGetNumProgramCategories_Deprecated:
    {
        // VST2 program categories: the number of categories (0 = no categories, a flat list). Only the mode vst-categories has any.
        if (!inst) return 0;
        synthyboy_mpc::startupTimes().noteProgramCall (synthyboy_mpc::StartupTimes::kPcNumCategories, static_cast<int> (value));
        const int n = synthyboy_mpc::numProgramCategories (inst->view);
        if (n > 0)
            inst->hostAskedCategories.store (true, std::memory_order_relaxed);
        return n;
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
        if (PluginInstance::isNativeUiParam (index))
        {
            // Inactive native controls have no label or display value. The host
            // may supply an uninitialised buffer while enumerating a saved track.
            buf[0] = '\0';
            const int offset = index - PluginInstance::nativeUiBase();
            if (offset == nativeui::kLfoTargetsAction)
            {
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, "LFO Targets");
                return 1;
            }
            if (offset == nativeui::kFxEditorPower)
            {
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, "FX Editor Power");
                else if (opcode == effGetParamDisplay)
                    synthyboy_mpc::putBounded (buf, kCap, inst->nativeUiValue (offset) > 0.0f ? "On" : "Off");
                return 1;
            }
            if (nativeui::isFilterChainMenu (offset))
            {
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, "Filter chain");
                else if (opcode == effGetParamDisplay)
                    synthyboy_mpc::putBounded (buf, kCap, inst->nativeUiValue (offset) > 0.0f ? "F2" : "F1");
                return 1;
            }
            if (nativeui::isModDetailControlOffset (offset))
            {
                const int field = offset - nativeui::kModDetailControl;
                if (opcode == effGetParamName)
                {
                    synthyboy_mpc::putBounded (buf, kCap, field >= 0 && field < nativeui::moddetail::kHandles
                                               ? nativeui::moddetail::kNavigationNames[field] : "");
                    return 1;
                }
                if (opcode == effGetParamDisplay)
                {
                    if (field == nativeui::moddetail::Delete)
                    {
                        const bool armed = inst->nativeModDetailNavigation.layout()
                            && inst->nativeModDetailNavigation.at (nativeui::moddetail::Confirm).cell().source > 0;
                        synthyboy_mpc::putBounded (buf, kCap, armed ? "CONFIRM?" : "DELETE");
                        return 1;
                    }
                    if (field == nativeui::moddetail::Confirm)
                    {
                        const bool armed = inst->nativeModDetailNavigation.layout()
                            && inst->nativeModDetailNavigation.at (nativeui::moddetail::Confirm).cell().source > 0;
                        synthyboy_mpc::putBounded (buf, kCap, armed ? "CONFIRM?" : "");
                        return 1;
                    }
                    buf[0] = '\0';
                    return 1;
                }
                if (opcode == effGetParamLabel)
                {
                    buf[0] = '\0';
                    return 1;
                }
                return 1;
            }
            if (nativeui::isMixerStereoOffset (offset))
            {
                constexpr const char* fields[] { "Peak L", "Peak R", "Hold L", "Hold R", "Clip L", "Clip R" };
                const int at = offset - nativeui::kMixerStereoBase;
                if (opcode == effGetParamName)
                    std::snprintf (buf, kCap, "Mixer %s %d", fields[at / nativeui::kMixerStrips], at % nativeui::kMixerStrips + 1);
                else if (opcode == effGetParamDisplay)
                    std::snprintf (buf, kCap, "%.3f", inst->nativeUiValue (offset));
                else buf[0] = '\0';
                return 1;
            }
            if (nativeui::isModBoardControlOffset (offset))
            {
                const int field = offset - nativeui::kModBoardControl;
                if (opcode == effGetParamName)
                {
                    synthyboy_mpc::putBounded (buf, kCap, nativeui::modboard::fieldName (field));
                    return 1;
                }
                if (opcode == effGetParamDisplay && inst->nativeModBoard.layout() == 2)
                {
                    namespace nb = nativeui::modboard;
                    const auto cell = inst->nativeModBoard.at (field).cell();
                    const char* text = "";
                    if (field == nb::ListTitle) text = "SELECT MODULATION";
                    else if (field >= nb::ListRow && field < nb::ListBack && cell.target >= 0)
                    {
                        const int choice = cell.target - nb::kChoiceBias;
                        const bool destination = cell.kind == nativeui::kFxToggle;
                        if (choice < 0)
                        {
                            if (destination)
                            {
                                const auto& groups = synthyboy_mpc::overlay::uibind::DestIndex::get().groups;
                                const int group = -1 - choice;
                                if (group >= 0 && group < static_cast<int> (groups.size())) text = groups[group].c_str();
                            }
                            else text = synthyboy_mpc::overlay::uibind::sourceGroupName (-choice);
                        }
                        else text = choice == 0 ? "NONE" : destination ? synthyboy_mpc::overlay::ModUi::destName (choice) : synthyboy_mpc::overlay::uibind::sourceName (choice);
                    }
                    synthyboy_mpc::putBounded (buf, kCap, text);
                    return 1;
                }
                buf[0] = '\0';
                return 1;
            }
            if (nativeui::isMixerControlOffset (offset))
            {
                const int idx = offset - nativeui::kMixerControl;
                const char* const stripNames[] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise", "Filter 1", "Filter 2", "Bus 1", "Bus 2", "Master" };
                if (idx < nativeui::mixer::kMute)
                {
                    const int strip = idx - nativeui::mixer::kSolo;
                    if (opcode == effGetParamName)
                        std::snprintf (buf, kCap, "Mixer Solo %s", strip >= 0 && strip < 10 ? stripNames[strip] : "");
                    else
                        std::snprintf (buf, kCap, "%s", inst->engine != nullptr && inst->engine->mixerSolo (strip) ? "ON" : "OFF");
                }
                else
                {
                    const int strip = idx - nativeui::mixer::kMute;
                    if (opcode == effGetParamName)
                        std::snprintf (buf, kCap, "Mixer Mute %s", strip >= 0 && strip < 10 ? stripNames[strip] : "");
                    else
                        std::snprintf (buf, kCap, "%s", inst->engine != nullptr && inst->engine->mixerMute (strip) ? "ON" : "OFF");
                }
                return 1;
            }
            if (nativeui::isMixerActiveTabOffset (offset))
            {
                static const char* const tabNames[] = { "MIXER", "FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2" };
                const int tab = std::clamp (inst->nativeMixerActiveTab.load (std::memory_order_acquire), 0, nativeui::kMixerTabs - 1);
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? "Mixer Active Tab" : tabNames[tab]);
                return 1;
            }
            if (nativeui::isMixerSoloVisualOffset (offset) || nativeui::isMixerMuteVisualOffset (offset))
            {
                const char* const stripNames[] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise", "Filter 1", "Filter 2", "Bus 1", "Bus 2", "Master" };
                const char* const stateNames[] = { "OFF", "ON", "DIM", "DISABLED", "PRESSED" };
                const bool isSolo = nativeui::isMixerSoloVisualOffset (offset);
                const int strip = offset - (isSolo ? nativeui::kMixerSoloVisual : nativeui::kMixerMuteVisual);
                const int v = isSolo ? inst->nativeMixerSoloVisual[strip].load (std::memory_order_acquire)
                                     : inst->nativeMixerMuteVisual[strip].load (std::memory_order_acquire);
                if (opcode == effGetParamName)
                    std::snprintf (buf, kCap, "%s %s",
                                   isSolo ? (strip == 9 ? "Mixer FX Visual" : "Mixer Solo Visual") : "Mixer Mute Visual",
                                   strip >= 0 && strip < nativeui::kMixerStrips ? stripNames[strip] : "");
                else
                    synthyboy_mpc::putBounded (buf, kCap, stateNames[std::clamp (v, 0, nativeui::mixer::kLastVisualState)]);
                return 1;
            }
            if (nativeui::isMacroTargetOffset (offset))
            {
                if (opcode == effGetParamName)
                    std::snprintf (buf, kCap, "Macro %d Targets", offset - nativeui::kMacroTarget + 1);
                else
                    buf[0] = '\0';
                return 1;
            }
            if (nativeui::isPlayExpandedOffset (offset))
            {
                const char* name = offset == nativeui::kPlayExpanded ? "Play Expanded Editor" : "CLOSE";
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? name : "");
                return 1;
            }
            if (nativeui::isModTargetsOffset (offset))
            {
                namespace n = nativeui::targets;
                const int field = offset - nativeui::kModTargets;
                const auto cell = inst->nativeModTargets.at (field).cell();
                const int part = field >= n::FirstRow && field < n::kFields ? (field - n::FirstRow) % n::RowFields : -1;
                buf[0] = '\0';
                if (opcode == effGetParamName)
                {
                    const char* name = "Target Row Visibility";
                    if (part == n::Amount && cell.target >= 0 && inst->nativeModTargets.mode() == n::Targets)
                    {
                        const int param = synthyboy::modDestinationParamIndex (static_cast<size_t> (cell.source));
                        name = param >= 0 ? synthyboy::paramTable()[param].name : "";
                    }
                    else if (part >= 0)
                    {
                        constexpr const char* fields[] {"Amount", "Polarity", "Active", "Remove"};
                        name = fields[part];
                    }
                    else if (field <= n::Page)
                    {
                        constexpr const char* header[] {"Back", "ADD TARGET", "Target Page"};
                        name = field == n::Add && inst->nativeModTargets.mode() == n::Modulations ? "ADD SOURCE" : header[field];
                    }
                    else if (field == n::kView)
                        name = "Target Editor View";
                    else if (field == n::kPageGate)
                        name = "Target Page Arrows";
                    synthyboy_mpc::putBounded (buf, kCap, name);
                }
                else if (opcode == effGetParamDisplay && cell.target >= 0)
                {
                    if (field == n::Page)
                        std::snprintf (buf, kCap, "%d / %d", cell.source + 1, cell.target);
                    else if (part == n::Amount)
                        synthyboy_mpc::overlay::uibind::formatModAmount (inst->paramMirror[cell.target].load (std::memory_order_relaxed), buf, kCap);
                    else if (part == n::Polarity || part == n::Active)
                    {
                        const bool on = inst->nativeModTargetValue (field, cell) > 0.5f;
                        synthyboy_mpc::putBounded (buf, kCap, part == n::Polarity ? (on ? "BI" : "UNI") : (on ? "ON" : "OFF"));
                    }
                }
                else if (opcode == effGetParamLabel && part == n::Amount)
                    synthyboy_mpc::putBounded (buf, kCap, "%");
                return 1;
            }
            if (nativeui::isOscRootLoadOffset (offset))
            {
                namespace n = nativeui::oscdetail;
                const char* title = offset == nativeui::kOscRootLoadView ? "OSC Root Load Source"
                    : inst->nativeOscDetail.name (n::kRootLoad + offset - nativeui::kOscRootLoad);
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? (title ? title : "OSC Root Load") : "");
                return 1;
            }
            if (nativeui::isPzToolbarOffset (offset))
            {
                const char* name = offset == nativeui::kPzAction ? "F1 PZ Curve Action"
                    : offset == nativeui::kPzAction + 1 ? "F2 PZ Curve Action"
                    : offset == nativeui::kPzSource ? "PZ Curve Source" : "PZ Curve Tools";
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? name : "");
                return 1;
            }
            if (nativeui::isFxBandOffset (offset))
            {
                const auto state = inst->nativeFxHeader.state();
                const char* name = offset == nativeui::kFxBandMode ? "FX Band Visibility" : "Compressor Band";
                const char* const choices[] { "Low", "Mid", "High" };
                const char* display = offset - nativeui::kFxBandBase == state.item && state.band >= 0 && state.band < 3
                    ? choices[state.band] : "";
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? name : opcode == effGetParamDisplay ? display : "");
                return 1;
            }
            if (offset == nativeui::kMasterScaleInput)
            {
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? "Scale Root Input"
                    : opcode == effGetParamDisplay ? (inst->scaleInput.load() >= PluginInstance::ScaleInputArmed ? "Select root" : "Play") : "");
                return 1;
            }
            if (offset == nativeui::kMasterPanel)
            {
                const auto view = inst->nativeMasterSkinPanel.load (std::memory_order_relaxed);
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? "Master Panel"
                    : opcode == effGetParamDisplay && view > 0 ? (view == nativeui::master::Main ? "Main" : "Tuning & MPE") : "");
                return 1;
            }
            if (nativeui::isOscDetailOffset (offset))
            {
                namespace n = nativeui::oscdetail;
                const int handle = offset - nativeui::kOscDetail;
                const auto cell = inst->oscDetailCell (handle);
                const bool parameterValue = cell.target >= 0 && cell.kind != n::Action
                    && ((handle >= n::kValue && handle < n::kStep) || handle >= n::kMenusBase);
                buf[0] = '\0';
                if (opcode == effGetParamName)
                {
                    const char* name = "OSC Detail View";
                    if (handle >= n::kMenusBase)
                        name = parameterValue ? synthyboy::paramTable()[cell.target].name : "OSC Detail Menu";
                    else if (handle != n::kView)
                        name = inst->nativeOscDetail.name (PluginInstance::oscDetailField (handle));
                    synthyboy_mpc::putBounded (buf, kCap, name != nullptr ? name : "");
                }
                else if (parameterValue)
                {
                    const auto& info = synthyboy::paramTable()[cell.target];
                    const float plain = inst->paramMirror[cell.target].load (std::memory_order_relaxed);
                    if (opcode == effGetParamLabel && cell.kind != n::Snap && cell.kind != n::Semitone)
                        synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (info.unit));
                    else if (opcode == effGetParamDisplay)
                    {
                        if (cell.kind == n::Semitone)
                            std::snprintf (buf, kCap, "%+d", synthyboy_mpc::overlay::OscUi::semOf (static_cast<int> (std::lround (plain))));
                        else if (cell.kind == n::Phase)
                            std::snprintf (buf, kCap, "%.0f\xc2\xb0", static_cast<double> (plain) * 3.6);
                        else if (cell.kind == n::Snap)
                            synthyboy_mpc::putBounded (buf, kCap, inst->oscDetailValue (handle, cell) > 0.5f ? "On" : "Off");
                        else if (info.numChoices > 0 && info.choices != nullptr)
                        {
                            const int choice = std::clamp (static_cast<int> (std::lround (plain - info.min)), 0, info.numChoices - 1);
                            synthyboy_mpc::putBounded (buf, kCap, synthyboy_mpc::ui::shownChoice (info.key, choice, info.choices[choice]));
                        }
                        else
                            synthyboy_mpc::overlay::uibind::formatValue (info, plain, buf, kCap);
                    }
                }
                return 1;
            }
            if (offset == nativeui::kMixerPeekView)
            {
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? "Mixer PEEK View" : "");
                return 1;
            }
            if (offset == nativeui::kOscSettingsView || offset == nativeui::kOscWarpLayout)
            {
                const char* name = offset == nativeui::kOscSettingsView ? "Osc Settings View" : "Osc Warp Layout";
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? name : "");
                return 1;
            }
            if (const int field = PluginInstance::nativeOscSettingField (offset); field >= 0)
            {
                namespace n = nativeui::osc;
                const auto cell = inst->nativeOscSettings.at (field).cell();
                buf[0] = '\0';
                if (opcode == effGetParamName)
                {
                    if (field < n::kSettings)
                        std::snprintf (buf, kCap, "Osc Setting %d", field + 1);
                    else
                    {
                        const int slot = field - n::kSettings;
                        std::snprintf (buf, kCap, "%s Setting %d", n::kSourceNames[1 + slot / n::kAbcSettings], 1 + slot % n::kAbcSettings);
                    }
                }
                else if (cell.target >= 0 && cell.kind != n::SettingsAction)
                {
                    const auto& info = synthyboy::paramTable()[cell.target];
                    if (cell.kind == n::SettingsSource && opcode == effGetParamDisplay)
                    {
                        const auto label = std::atomic_load (&inst->nativeOscSourceLabel);
                        synthyboy_mpc::putBounded (buf, kCap, label ? label->c_str() : "");
                    }
                    else if (opcode == effGetParamLabel)
                    {
                        if (cell.kind == n::SettingsPhase)
                            synthyboy_mpc::putBounded (buf, kCap, "\xc2\xb0");
                        else if (cell.kind != n::SettingsSource && cell.kind != n::SettingsOctave)
                            synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (info.unit));
                    }
                    else
                    {
                        const float plain = inst->paramMirror[cell.target].load (std::memory_order_relaxed);
                        if (cell.kind == n::SettingsPhase)
                            std::snprintf (buf, kCap, "%.0f\xc2\xb0", static_cast<double> (plain) * 3.6);
                        else if (cell.kind == n::SettingsOctave)
                            std::snprintf (buf, kCap, "%+d", static_cast<int> (std::lround (inst->nativeOscSettingValue (cell) * 8)) - 4);
                        else
                            synthyboy_mpc::overlay::uibind::formatValue (info, plain, buf, kCap);
                    }
                }
                return 1;
            }
            if (offset >= nativeui::kPlayOsc && offset < nativeui::kPlayLfoStandardSync)
            {
                namespace n = nativeui::playoscui;
                namespace osc = nativeui::osc;
                const int handle = offset - nativeui::kPlayOsc;
                if (handle == 0)
                {
                    if (opcode == effGetParamName)
                        synthyboy_mpc::putBounded (buf, kCap, "Compact OSC View");
                    return 1;
                }
                const int source = (handle - 1) / n::kFields + 1;
                const int field = (handle - 1) % n::kFields;
                const bool sub = source == 1, noise = source == n::kSources;
                constexpr const char* names[] { "Power", "Expand", "Destination", "", "", "Pan", "Level" };
                if (opcode == effGetParamName)
                {
                    const char* name = field == n::First ? (sub ? "Waveform" : noise ? "Sound" : "Type")
                                     : field == n::Second ? (sub ? "Octave" : noise ? "Filter" : "Source")
                                     : names[field];
                    std::snprintf (buf, kCap, "Play %s %s", osc::kSourceNames[source - 1], name);
                }
                else if (source == n::Bank::source (inst->nativePlayOsc.view()))
                {
                    const auto cell = inst->nativePlayOsc.at (field).cell();
                    if (cell.target >= 0 && ! osc::settingsAction (cell.kind))
                    {
                        const auto& info = synthyboy::paramTable()[cell.target];
                        if (opcode == effGetParamDisplay)
                            synthyboy_mpc::overlay::uibind::formatValue (info, inst->paramMirror[cell.target].load (std::memory_order_relaxed), buf, kCap);
                        else
                            synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (info.unit));
                    }
                    else if (opcode == effGetParamDisplay && field == n::Second && ! sub && ! noise)
                    {
                        const auto name = std::atomic_load (&inst->nativePlayOscSourceName);
                        synthyboy_mpc::putBounded (buf, kCap, name != nullptr ? name->c_str() : "");
                    }
                }
                return 1;
            }
            if (offset >= nativeui::kPlayEnvLfo)
            {
                namespace n = nativeui::playenvlfo;
                const int field = PluginInstance::nativePlayEnvLfoField (offset);
                if (field == PluginInstance::kUnmappedPlayEnvLfoField)
                {
                    buf[0] = '\0';
                    return 0;
                }
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, field < 0 ? "Play Env LFO View" : n::kNames[field]);
                else if (field == n::Choose && opcode == effGetParamDisplay)
                {
                    const int source = inst->nativePlayEnvLfo.source();
                    if (source > 0)
                        std::snprintf (buf, kCap, "%s %d", source > n::kStandardLfos ? "RND" : "LFO",
                                       source > n::kStandardLfos ? source - n::kStandardLfos : source);
                }
                else if ((field > n::LfoExpand && field < n::Choose) || field == n::StandardSync)
                {
                    const int target = inst->nativePlayEnvLfo.at (field).cell().target;
                    if (target >= 0)
                    {
                        const auto& info = synthyboy::paramTable()[target];
                        const float plain = inst->paramMirror[target].load (std::memory_order_relaxed);
                        if (opcode == effGetParamLabel)
                            synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (info.unit));
                        else if (field == n::StandardSync)
                        {
                            constexpr const char* modes[] { "HZ", "BPM", "KEY" };
                            synthyboy_mpc::putBounded (buf, kCap, modes[static_cast<int> (synthyboy_mpc::ui::lfo::syncModeOf (static_cast<int> (std::lround (plain))))]);
                        }
                        else if (field == n::Shape && std::lround (plain) == synthyboy_mpc::ui::lfo::kShapeCustom)
                            synthyboy_mpc::putBounded (buf, kCap, "Drawn");
                        else
                            synthyboy::valueToText (info, plain, buf, kCap);
                    }
                }
                return 1;
            }
            if (offset >= nativeui::kLfoDetail)
            {
                namespace n = nativeui::lfodetail;
                const int handle = offset - nativeui::kLfoDetail;
                const int field = n::handleField (handle);
                if (opcode == effGetParamName)
                {
                    if (field >= n::Header)
                    {
                        char label[n::kLabelSize];
                        inst->nativeLfoDetail.label (field, label, sizeof label);
                        const bool back = field == n::Header && inst->nativeLfoDetail.display().headerBack;
                        std::snprintf (buf, kCap, "%s%s", back ? "<  " : "", label);
                    }
                    else if (field >= 0)
                        synthyboy_mpc::putBounded (buf, kCap, n::kPointNames[field]);
                    else
                    {
                        const char* title = handle == n::kViewHandle ? "LFO Shape Detail View"
                                          : handle == n::kPointState ? "LFO Point Mod State"
                                          : handle == n::kHeaderBack ? "LFO Menu Header Back" : "LFO Menu Visible Rows";
                        synthyboy_mpc::putBounded (buf, kCap, title);
                    }
                }
                else if (opcode == effGetParamDisplay && field == n::Amount)
                {
                    const auto display = inst->nativeLfoDetail.display();
                    if (display.point == n::Assigned)
                        std::snprintf (buf, kCap, "%+.0f%%", n::rangeOf (field).decode (inst->nativeLfoDetail.value (field)));
                    else
                        synthyboy_mpc::putBounded (buf, kCap, "-");
                }
                else if (opcode == effGetParamDisplay && field == n::Target)
                {
                    const auto target = static_cast<int> (n::rangeOf (field).decode (inst->nativeLfoDetail.value (field)));
                    synthyboy_mpc::putBounded (buf, kCap, n::kPointTargets[target]);
                }
                return 1;
            }
            if (offset >= nativeui::kFxChain)
            {
                namespace c = nativeui::fxchain;
                namespace fx = synthyboy_mpc::ui::fxl;
                constexpr const char* splitterNames[] { "L/H", "L/M/H", "M/S" };
                const int field = offset - nativeui::kFxChain;
                if (opcode == effGetParamName)
                {
                    if (field == c::View)
                        synthyboy_mpc::putBounded (buf, kCap, "FX Chain View");
                    else if (field == c::Count)
                        synthyboy_mpc::putBounded (buf, kCap, "FX Chain Visible Count");
                    else if (field < c::Power)
                        std::snprintf (buf, kCap, "FX Select %d", field - c::Select + 1);
                    else if (field < c::Gallery)
                        std::snprintf (buf, kCap, "FX Chain Power %d", field - c::Power + 1);
                    else if (field < c::Menu)
                        synthyboy_mpc::putBounded (buf, kCap, fx::layoutOf (field - c::Gallery + 1).name);
                    else
                    {
                        constexpr const char* actions[] { "FX Add Effect", "FX Available Effects", "FX Chain Back", "FX Chain Page" };
                        synthyboy_mpc::putBounded (buf, kCap, actions[field - c::Menu]);
                    }
                }
                else if (opcode == effGetParamDisplay && field >= c::Select && field < c::Gallery)
                {
                    const auto card = inst->nativeFxChain.card (field < c::Power ? field - c::Select : field - c::Power);
                    if (card.kind > 0)
                    {
                        const char* name = card.kind < fx::kKindSplitterLh ? synthyboy::fxTypeDescriptor (card.kind).shortName
                                                                         : splitterNames[card.kind - fx::kKindSplitterLh];
                        synthyboy_mpc::putBounded (buf, kCap, field < c::Power ? name : fx::rackName (card.rack));
                    }
                }
                return 1;
            }
            if (offset >= nativeui::kLfoEditor)
            {
                namespace n = nativeui::lfoeditor;
                const int handle = offset - nativeui::kLfoEditor;
                const auto state = inst->nativeLfoEditor.display();
                if (handle >= n::kValues && handle < n::kAvailability)
                {
                    const int field = handle - n::kValues;
                    const auto& info = n::kFields[field];
                    if (opcode == effGetParamName)
                        synthyboy_mpc::putBounded (buf, kCap, field == n::kCurve && state.scale ? "SCALE" : info.name);
                    else if (opcode == effGetParamDisplay)
                    {
                        const auto range = n::rangeOf (field, state.points, state.scale);
                        const double value = range.decode (inst->nativeLfoEditor.value (field));
                        if (info.type == n::kToggle)
                            synthyboy_mpc::putBounded (buf, kCap, value > 0.5 ? "On" : "Off");
                        else if (field == n::kMode)
                        {
                            const auto* mode = synthyboy::findParamByKey ("lfo1.sync_type");
                            if (mode != nullptr)
                                synthyboy_mpc::overlay::uibind::formatValue (*mode, static_cast<float> (value), buf, kCap);
                        }
                        else if (field == n::kPoint && state.selected == 0)
                            synthyboy_mpc::putBounded (buf, kCap, "-");
                        else if (field == n::kPoint && state.selected > 1)
                            std::snprintf (buf, kCap, "%d SEL", state.selected);
                        else if (range.units == 100)
                            std::snprintf (buf, kCap, "%.2f", value);
                        else if (field == n::kPhase)
                            std::snprintf (buf, kCap, "%.0f\xc2\xb0", value);
                        else if (info.type == n::kNumber)
                            std::snprintf (buf, kCap, field == n::kCurve || field == n::kBend ? "%+.0f" : "%.0f", value);
                    }
                }
                else if (opcode == effGetParamName)
                {
                    if (handle >= n::kAvailability && handle < n::kScale)
                        std::snprintf (buf, kCap, "%s Available", n::kFields[n::kOptional[handle - n::kAvailability]].name);
                    else
                        synthyboy_mpc::putBounded (buf, kCap, handle == n::kView ? "LFO Shape Editor View" : "LFO Point Scale View");
                }
                return 1;
            }
            if (offset >= nativeui::kFxHeader)
            {
                namespace h = nativeui::fxheader;
                constexpr const char* fields[] { "Power", "Rack", "Panel", "Remove" };
                const int handle = offset - nativeui::kFxHeader;
                buf[0] = '\0';
                if (opcode == effGetParamName)
                {
                    if (handle < 2)
                        synthyboy_mpc::putBounded (buf, kCap, handle == 0 ? "FX Header Source" : "FX Header Layout");
                    else
                    {
                        const int item = (handle - 2) / h::kFields;
                        const int field = (handle - 2) % h::kFields;
                        if (item == inst->nativeFxHeader.state().item && field != h::Page)
                        {
                            char full[128];
                            inst->nativeFxHeader.label (field, full, sizeof full);
                            std::string label (full);
                            if (field == h::Rack)
                                for (const char* redundant : { " CHAIN", "SPLITTER ", " BAND" })
                                    for (auto pos = label.find (redundant); pos != std::string::npos; pos = label.find (redundant))
                                        label.erase (pos, std::strlen (redundant));
                            if (label.size() >= kCap)
                            {
                                const auto prefix = synthyboy_mpc::truncateUtf8 (label, (kCap - 4) / 2);
                                size_t tail = label.size() - (kCap - 4 - prefix.size());
                                while (tail < label.size() && (static_cast<unsigned char> (label[tail]) & 0xc0) == 0x80) ++tail;
                                label = prefix + "..." + label.substr (tail);
                            }
                            synthyboy_mpc::putBounded (buf, kCap, label.c_str());
                        }
                        else
                            std::snprintf (buf, kCap, "FX %s %d %s", item < synthyboy::kNumFxSlots ? "Module" : "Slot",
                                           (item % synthyboy::kNumFxSlots) + 1, fields[field]);
                    }
                }
                else if (opcode == effGetParamDisplay && handle >= 2)
                {
                    const int field = (handle - 2) % h::kFields;
                    const auto state = inst->nativeFxHeader.state();
                    if (field == h::Rack)
                        synthyboy_mpc::putBounded (buf, kCap, synthyboy_mpc::ui::fxl::rackName (state.rack));
                    else if (field == h::Power)
                        synthyboy_mpc::putBounded (buf, kCap, inst->nativeUiValue (offset) > 0.5f ? "On" : "Off");
                    else if (field == h::Page)
                    {
                        const auto panels = synthyboy_mpc::ui::fxl::panelsOf (state.kind);
                        synthyboy_mpc::putBounded (buf, kCap, panels.names[std::clamp (state.tabIndex(), 0, panels.count - 1)]);
                    }
                }
                return 1;
            }
            if (offset >= nativeui::kOscRoutingBySource)
            {
                namespace n = nativeui::osc;
                const int field = offset - nativeui::kOscRoutingBySource;
                const auto state = inst->nativeOsc.snapshot();
                buf[0] = '\0';
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, n::kRoutingNames[field]);
                else if (opcode == effGetParamDisplay && state.displaysSource (field + 1))
                    synthyboy_mpc::putBounded (buf, kCap, n::kDestinationNames[state.destination()]);
                return 1;
            }
            if (offset >= nativeui::kMasterOutput)
            {
                namespace m = nativeui::master;
                constexpr const char* names[] { "Master Peak L", "Master Peak R", "Master Hold L", "Master Hold R",
                    "Master Voice Meter", "Master Voice Count", "Master Output Limiter", "Master Clip", "Master Clear Clip" };
                const int field = offset - nativeui::kMasterOutput;
                buf[0] = '\0';
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, names[field]);
                else if (opcode == effGetParamDisplay && field == m::VoiceCount)
                {
                    const int voices = static_cast<int> (std::lround (inst->nativeUiValue (offset) * m::kVoiceCountLimit));
                    std::snprintf (buf, kCap, "%d / %d VOICES", voices, inst->nativeMasterPolyphony.load (std::memory_order_relaxed));
                }
                else if (opcode == effGetParamDisplay && field == m::Limiter)
                    synthyboy_mpc::putBounded (buf, kCap, inst->outputStage.limiter() ? "On" : "Off");
                return 1;
            }
            if (offset >= nativeui::kOscPower)
            {
                const int field = offset - nativeui::kOscPower;
                buf[0] = '\0';
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, nativeui::osc::kPowerNames[field]);
                else if (opcode == effGetParamDisplay)
                    synthyboy_mpc::putBounded (buf, kCap, inst->nativeOsc.snapshot().powerValue (field + 1) > 0.0f ? "On" : "Off");
                return 1;
            }
            if (offset >= nativeui::kVoiceSequence)
            {
                namespace n = nativeui::voice;
                const int field = offset - nativeui::kVoiceSequence;
                const auto cell = inst->nativeVoice.displayCell (field);
                buf[0] = '\0';
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, n::kNames[field]);
                else if (cell.target >= 0 && opcode == effGetParamDisplay)
                {
                    if (field == n::kRow)
                        synthyboy_mpc::putBounded (buf, kCap, n::kRowNames[cell.target]);
                    else if (field == n::kStep)
                        std::snprintf (buf, kCap, "STEP %d", cell.target + 1);
                    else
                        synthyboy_mpc::ui::VoiceGrid::formatValue (cell.source / n::kSteps, inst->paramMirror[cell.target].load (std::memory_order_relaxed) * 0.01f, buf, kCap);
                }
                else if (cell.target >= 0 && field == n::kValue && opcode == effGetParamLabel)
                    synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (synthyboy::paramTable()[cell.target].unit));
                return 1;
            }
            if (offset >= nativeui::kModNavigation)
            {
                namespace n = nativeui::mod;
                const int field = offset - nativeui::kModNavigation;
                buf[0] = '\0';
                if (opcode == effGetParamName)
                {
                    if (field < n::kHeaderNavigation)
                        std::snprintf (buf, kCap, "Mod Navigation %s", n::kNavigationNames[field]);
                    else
                    {
                        const int rowField = field - n::kHeaderNavigation;
                        std::snprintf (buf, kCap, "Mod Row %d %s", rowField / n::kRowNavigation + 1, n::kRowNavigationNames[rowField % n::kRowNavigation]);
                    }
                }
                else if (opcode == effGetParamDisplay && field >= n::kHeaderNavigation)
                {
                    const int action = (field - n::kHeaderNavigation) % n::kRowNavigation;
                    const int choice = inst->nativeModDisplayChoice (field, inst->nativeModNavigation.at (field).cell());
                    if (action == n::OpenSource)
                    {
                        const auto snapshot = std::atomic_load (&inst->overlayContent);
                        synthyboy_mpc::overlay::MacroNames names;
                        names.load (snapshot ? snapshot->state.get() : nullptr);
                        const synthyboy_mpc::overlay::NamesScope scope (names);
                        synthyboy_mpc::putBounded (buf, kCap, synthyboy_mpc::overlay::ModUi::sourceName (choice));
                    }
                    else if (action == n::OpenDestination)
                        synthyboy_mpc::putBounded (buf, kCap, synthyboy_mpc::overlay::ModUi::destName (choice));
                }
                return 1;
            }
            if (offset == nativeui::kPlayLfoStandard)
            {
                buf[0] = '\0';
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, "Play LFO Standard");
                else if (opcode == effGetParamDisplay)
                    synthyboy_mpc::putBounded (buf, kCap, inst->playLfoIsStandard() ? "Standard" : "Random");
                return 1;
            }
            if (offset >= nativeui::kEnvView)
            {
                namespace n = nativeui::env;
                const int field = offset - nativeui::kEnvFields;
                buf[0] = '\0';
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, field >= 0 ? n::kNames[field] : "Env Editor View");
                else if (opcode == effGetParamDisplay && field == n::Targets && inst->nativeEnvEditor.at (field).cell().target >= 0)
                    std::snprintf (buf, kCap, "%d", inst->nativeEnvTargetCount.load (std::memory_order_relaxed));
                else if (field >= 0 && field < n::Selection)
                {
                    const int target = inst->nativeEnvEditor.at (field).cell().target;
                    if (target >= 0)
                    {
                        const auto& pi = synthyboy::paramTable()[target];
                        if (opcode == effGetParamLabel)
                            synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (pi.unit));
                        else
                            synthyboy_mpc::overlay::uibind::formatValue (pi, inst->paramMirror[target].load (std::memory_order_relaxed), buf, kCap);
                    }
                }
                return 1;
            }
            if (offset >= nativeui::kModView)
            {
                namespace n = nativeui::mod;
                const int field = offset - nativeui::kModFields;
                buf[0] = '\0';
                if (opcode == effGetParamName)
                {
                    if (field >= n::kDetail)
                        std::snprintf (buf, kCap, "Mod Detail %s", n::kDetailNames[field - n::kDetail]);
                    else if (field >= 0)
                        std::snprintf (buf, kCap, "Mod Row %d %s", field / n::kRowFields + 1, n::kRowNames[field % n::kRowFields]);
                    else if (offset >= nativeui::kModRows)
                        std::snprintf (buf, kCap, "Mod Row %d Slot", offset - nativeui::kModRows + 1);
                    else
                        synthyboy_mpc::putBounded (buf, kCap, "Mod Matrix View");
                }
                else if (const int target = inst->nativeMod.at (field).cell().target; target >= 0)
                {
                    const auto& pi = synthyboy::paramTable()[target];
                    if (opcode == effGetParamLabel)
                        synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (pi.unit));
                    else if (field == n::kDetail + n::DetailAmount || (field >= 0 && field < n::kDetail && field % n::kRowFields == n::Amount))
                        synthyboy_mpc::overlay::uibind::formatModAmount (inst->paramMirror[target].load (std::memory_order_relaxed), buf, kCap);
                    else
                        synthyboy_mpc::overlay::uibind::formatValue (pi, inst->paramMirror[target].load (std::memory_order_relaxed), buf, kCap);
                }
                return 1;
            }
            if (offset == nativeui::kMasterView)
            {
                synthyboy_mpc::putBounded (buf, kCap, opcode == effGetParamName ? "Master Editor View" : "");
                return 1;
            }
            if (offset >= nativeui::kOscBase)
            {
                namespace n = nativeui::osc;
                const int field = offset - nativeui::kOscBase;
                const auto state = inst->nativeOsc.snapshot();
                buf[0] = '\0';
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, n::kNames[field]);
                else if (opcode == effGetParamDisplay && state.source() > 0)
                {
                    if (field == n::kSource)
                        synthyboy_mpc::putBounded (buf, kCap, n::kSourceNames[state.source() - 1]);
                    else if (field == n::kRouting && state.visibility() == n::kRootOn)
                        synthyboy_mpc::putBounded (buf, kCap, n::kDestinationNames[state.destination()]);
                }
                return 1;
            }
            if (offset >= nativeui::kFilterBase)
            {
                const int slot = offset - nativeui::kFilterValueBase;
                const auto cell = inst->nativeFilter.at (slot).cell();
                const auto* source = cell.target >= 0 && (slot < nativeui::FilterPoint || slot == nativeui::FilterLegato)
                                   ? synthyboy::paramTable() + cell.source : nullptr;
                buf[0] = '\0';
                if (opcode == effGetParamName)
                {
                    if (offset == nativeui::kFilterBase)
                        synthyboy_mpc::putBounded (buf, kCap, "Filter Native View");
                    else if (offset >= nativeui::kFilterIndicatorBase)
                        std::snprintf (buf, kCap, "Filter Indicator %d", offset - nativeui::kFilterIndicatorBase + 1);
                    else
                        synthyboy_mpc::putBounded (buf, kCap, nativeui::kFilterNames[slot]);
                }
                else if (opcode == effGetParamLabel && source != nullptr)
                    synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (source->unit));
                else if (opcode == effGetParamDisplay && source != nullptr)
                {
                    const float plain = synthyboy::fromNormalised (*source, inst->nativeFilterValue (slot));
                    if (slot == nativeui::FilterStyle)
                    {
                        const auto modelCell = inst->nativeFilter.at (nativeui::FilterModel).cell();
                        const int model = modelCell.target >= 0 ? static_cast<int> (std::lround (
                            inst->paramMirror[modelCell.target].load (std::memory_order_relaxed))) : 0;
                        const auto* names = synthyboy_mpc::ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
                        const int n = synthyboy::filters::filterModelStyleCount (model);
                        if (n > 0 && names != nullptr)
                            synthyboy_mpc::putBounded (buf, kCap, names[std::clamp (static_cast<int> (std::lround (plain)), 0, n - 1)]);
                    }
                    else
                        synthyboy_mpc::overlay::uibind::formatValue (*source, plain, buf, kCap);
                }
                else if (opcode == effGetParamDisplay && cell.target >= 0 && slot >= nativeui::FilterPoint && slot < nativeui::FilterLegato)
                {
                    const float plain = inst->nativeFilterCurve[nativeui::kFilterCurveFields + slot - nativeui::FilterPoint].load (std::memory_order_relaxed);
                    std::snprintf (buf, kCap, slot == nativeui::FilterCurve ? "%.2f" : (slot == nativeui::FilterY ? "%.0f%%" : "%.0f"), static_cast<double> (plain));
                }
                return 1;
            }
            if (offset >= nativeui::kLfoBase)
            {
                namespace n = nativeui::lfo;
                const int field = offset - nativeui::kLfoBase;
                const auto* info = PluginInstance::nativeLfoInfo (field);
                const float value = inst->nativeLfo.value (field);
                buf[0] = '\0';
                if (opcode == effGetParamName)
                    synthyboy_mpc::putBounded (buf, kCap, n::kNames[field]);
                else if (opcode == effGetParamLabel && info != nullptr)
                    synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (info->unit));
                else if (opcode == effGetParamDisplay)
                {
                    if (field == n::kSelection)
                    {
                        const int choice = static_cast<int> (std::lround (value * (n::kInstances - 1)));
                        if (choice < n::kStandard)
                            std::snprintf (buf, kCap, "LFO %d", choice + 1);
                        else
                            std::snprintf (buf, kCap, "RND %d", choice - n::kStandard + 1);
                    }
                    else if (info != nullptr)
                    {
                        const float plain = synthyboy::fromNormalised (*info, value);
                        if (info->choices != nullptr && info->numChoices > 0)
                        {
                            const int choice = std::clamp (static_cast<int> (std::lround (plain - info->min)), 0, info->numChoices - 1);
                            const char* name = field == n::kShape && choice == n::kShapeLast ? "Drawn" : info->choices[choice];
                            synthyboy_mpc::putBounded (buf, kCap, name);
                        }
                        else
                            synthyboy::valueToText (*info, plain, buf, kCap);
                    }
                    else if (field == n::kLayout)
                        std::snprintf (buf, kCap, "%d", static_cast<int> (std::lround (value * n::kLayoutLast)));
                }
                return 1;
            }
            if (offset >= nativeui::kFxValueBase)
            {
                const auto menu = inst->nativeFilterMenu (offset);
                const auto cell = menu.slot >= 0 ? menu.cell() : inst->nativeFxEndpoint (offset).cell();
                if (cell.target >= 0)
                {
                    const auto& source = synthyboy::paramTable()[cell.source];
                    const float plain = synthyboy::fromNormalised (source, inst->qReadNorm (cell.target));
                    if (opcode == effGetParamLabel)
                        synthyboy_mpc::putBounded (buf, kCap, synthyboy::unitLabel (source.unit));
                    else if (opcode == effGetParamDisplay)
                    {
                        const int model = nativeui::fxStyleModel (cell.kind);
                        const int choice = static_cast<int> (std::lround (plain));
                        if (model >= 0)
                        {
                            const auto* names = synthyboy_mpc::ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
                            const int n = synthyboy::filters::filterModelStyleCount (model);
                            synthyboy_mpc::putBounded (buf, kCap, n > 0 && names != nullptr ? names[std::clamp (choice, 0, n - 1)] : "");
                        }
                        else if (source.choices != nullptr && choice >= 0 && choice < source.numChoices)
                            synthyboy_mpc::putBounded (buf, kCap, synthyboy_mpc::ui::shownChoice (source.key, choice, source.choices[choice]));
                        else
                            synthyboy_mpc::overlay::uibind::formatValue (source, plain, buf, kCap);
                    }
                    else
                    {
                        namespace fxl = synthyboy_mpc::ui::fxl;
                        for (int kind = 1; kind < fxl::kNumKinds; ++kind)
                        {
                            const auto& layout = fxl::layoutOf (kind);
                            for (int row = 0; row < layout.numRows; ++row)
                                for (const auto& entry : layout.row[row].cell)
                                    for (const auto& option : entry.alt)
                                        if (option.key != nullptr && std::strcmp (option.key, source.key) == 0)
                                        {
                                            fxl::controlName (option, source, layout.name, buf, kCap);
                                            return 1;
                                        }
                        }
                        synthyboy_mpc::shortName (source.name, buf, kCap, synthyboy_mpc::kParamNameChars);
                    }
                }
                else if (opcode == effGetParamName)
                {
                    if (offset < nativeui::kFxKindBase)
                        std::snprintf (buf, kCap, "FX Cell %d", offset - nativeui::kFxValueBase + 1);
                    else if (offset < nativeui::kFxLayout)
                        std::snprintf (buf, kCap, "FX Cell Kind %d", offset - nativeui::kFxKindBase + 1);
                    else if (offset == nativeui::kFxLayout)
                        synthyboy_mpc::putBounded (buf, kCap, "FX Cell Layout");
                    else
                        std::snprintf (buf, kCap, "FX Domain %d", offset - nativeui::kFxDomainBase);
                }
                else
                    buf[0] = '\0';
                return 1;
            }
            if (opcode == effGetParamName)
            {
                if (offset == nativeui::kMixerPeek)
                    synthyboy_mpc::putBounded (buf, kCap, "Mixer Peek");
                else if (offset < nativeui::kMixerHoldBase)
                    std::snprintf (buf, kCap, "Mixer Peak %d", offset - nativeui::kMixerPeakBase + 1);
                else if (offset < nativeui::kEnvSelectorBase)
                    std::snprintf (buf, kCap, "Mixer Hold %d", offset - nativeui::kMixerHoldBase + 1);
                else
                    std::snprintf (buf, kCap, "Env Selector Cell %d", offset - nativeui::kEnvSelectorBase + 1);
            }
            else if (opcode == effGetParamDisplay && offset != nativeui::kMixerPeek)
                std::snprintf (buf, kCap, "%.3f", static_cast<double> (inst->nativeUiValue (offset)));
            else
                buf[0] = '\0';
            return 1;
        }
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
            // the proxy reports the CURRENT target: its name (a transpose that is split in two knobs says Oct / Sem), its unit and its value text; "Q n" while it has none (or a virtual one): the kit's bank
            // (kit/q_targets.h) answers; a macro keeps the name it has been given ("Filter Sweep"), which only SynthyBoy knows
            const int q = index - PluginInstance::qBase();
            const int real = inst->qBank.target (q);
            if (opcode == effGetParamName)
            {
                if (real < 0 || ! synthyboy_mpc::macroParamName (inst->overlayView.content(), real, buf, kCap))
                    inst->qBank.proxyName (q, buf, kCap);   // the target's name, shortened by the same word list as the parameter table's names so that name and suffix fit the bound together
            }
            else if (opcode == effGetParamLabel)
                inst->qBank.proxyLabel (q, buf, kCap);
            else
                inst->qBank.proxyDisplay (q, buf, kCap);
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
#if SYNTHYBOY_MPC_OVERLAY
            static const auto* firstStep = synthyboy::findParamByKey ("voicectl.s1.pan");
            static const auto* lastStep = synthyboy::findParamByKey ("voicectl.s8.mod2");
            if (firstStep && lastStep && info >= firstStep && info <= lastStep)
            {
                const int row = static_cast<int> (info - firstStep) % synthyboy_mpc::ui::VoiceGrid::kNumRows;
                synthyboy_mpc::ui::VoiceGrid::formatValue (row, plain * 0.01f, buf, kCap);
                return 1;
            }
            // Passive native Filter selectors read the engine handle without an
            // editable proxy. STYLE still uses that filter's current model names.
            const bool filter1Style = std::strcmp (info->key, "filter1.style") == 0;
            const bool filter2Style = std::strcmp (info->key, "filter2.style") == 0;
            if (filter1Style || filter2Style)
            {
                const auto* modelInfo = synthyboy::findParamByKey (filter1Style ? "filter1.model" : "filter2.model");
                const int model = modelInfo != nullptr ? static_cast<int> (std::lround (
                    inst->paramMirror[modelInfo - synthyboy::paramTable()].load (std::memory_order_relaxed))) : 0;
                const int count = synthyboy::filters::filterModelStyleCount (model);
                const auto* names = synthyboy_mpc::ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
                synthyboy_mpc::putBounded (buf, kCap, count > 0 && names != nullptr
                    ? names[std::clamp (static_cast<int> (std::lround (plain)), 0, count - 1)] : "");
                return 1;
            }
#endif
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
            // the look-ahead of the output limiter at this rate: the host reads initialDelay after effSetSampleRate (docs/MPC.md "Output limiter"; it must not rely on it)
            e->initialDelay = static_cast<VstInt32> ((inst->engine ? inst->engine->latencySamples() : 0) + (inst->outputStage.limiter() ? synthyboy::OutputStage::lookaheadFor (opt) : 0));
            inst->latencyReported.store (e->initialDelay, std::memory_order_relaxed);
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
        if (value == 0)
        {
            inst->soundOffRequests.fetch_add (1u, std::memory_order_relaxed);
            inst->soundOffPending.store (true, std::memory_order_release);
        }
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
                synthyboy::Quality quality = synthyboy_mpc::fixedProfileQuality (synthyboy_mpc::profileInForce().profile);
                quality.realtime = true; // live: audio thread never waits for helper thread
                quality.lazyTables = true; // the 64 frame built-in tables nothing names are built by a low priority thread, not before the host's resume call returns (docs/MPC.md "Start-up time")
                inst->engine->prepare(safeSr, bs, quality);
                inst->outputStage.prepare (safeSr);
                inst->prepared = true;
                inst->lastPreparedSampleRate = sr;
                inst->lastPreparedBlockSize = bs;
                synthyboy_mpc::startupTimes().setWavetableMs (inst->engine->wavetableBuildMs());
                synthyboy_mpc::startupTimes().setPrepareParts (inst->engine->prepareDetailMs (0), inst->engine->prepareDetailMs (1));
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

#if SYNTHYBOY_MPC_OVERLAY
        int transportPlaying = -1;
#endif
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

            const uint8_t status = data[0] & 0xF0;
            if (status == 0x90 || status == 0x80)
            {
                const int hostVel = numBytes >= 3 ? (data[2] & 0x7F) : 0;
                const bool isNoteOn = (status == 0x90 && hostVel > 0);
#if SYNTHYBOY_MPC_OVERLAY
                const int note = data[1] & 127;
                auto& key = inst->scaleInputKeys[(data[0] & 15) * 128 + note];
                if (!isNoteOn && key.exchange (PluginInstance::Released) == PluginInstance::Captured)
                    continue;
                if (isNoteOn)
                {
                    int mode = inst->scaleInput.load();
                    if (mode >= PluginInstance::ScaleInputArmed)
                    {
                        const bool live = (midiEv->flags & kVstMidiEventIsRealtime) != 0;
                        if (!live && transportPlaying < 0)
                        {
                            auto* time = inst->audioMaster ? reinterpret_cast<VstTimeInfo*> (
                                inst->audioMaster (e, audioMasterGetTime, 0, kVstTransportPlaying, nullptr, 0)) : nullptr;
                            transportPlaying = !time || (time->flags & kVstTransportPlaying) != 0;
                        }
                        if (live || transportPlaying == 0)
                        {
                            while (mode >= PluginInstance::ScaleInputArmed
                                   && !inst->scaleInput.compare_exchange_weak (mode, note % 12)) {}
                            if (mode >= PluginInstance::ScaleInputArmed)
                            {
                                MPCKIT_LOG ("key_mode", info, "selected root=%d midi_note=%d channel=%d velocity=%d live_flag=%d",
                                            note % 12, note, (data[0] & 15) + 1, hostVel, live ? 1 : 0);
                                uint8_t released = PluginInstance::Released;
                                key.compare_exchange_strong (released, PluginInstance::Captured);
                                continue;
                            }
                        }
                    }
                    key.store (PluginInstance::Sounding);
                }
#endif
                if (isNoteOn)
                {
                    synthyboy_mpc::noteDiagnostics().noteOn (hostVel);
#if SYNTHYBOY_MPC_OVERLAY
                    inst->lastNoteOnKey.store (data[1] & 0x7F, std::memory_order_relaxed);   // the live marker of the NOTE / VELO editors: two relaxed stores, nothing else
                    inst->lastNoteOnVelocity.store (hostVel, std::memory_order_relaxed);
#endif
                }
#if SYNTHYBOY_MPC_DEBUG_HOOKS
                const int channel = (data[0] & 0x0F) + 1;
                const int noteNum = data[1] & 0x7F;
                // Velocity given to the engine: the shell applies no mapping to note velocity
                // (curve or pad mapping, if any, is identity in the shell);
                // the engine receives the velocity directly corresponding to hostVel.
                const int engineVel = hostVel;
                synthyboy_mpc::PlayLog::note (isNoteOn, channel, noteNum, hostVel, engineVel);
#endif
            }

            synthyboy::Event event;
            if (synthyboy::translateMidiMessage(data, numBytes, deltaFrames, event))
            {
                inst->eventInbox.push (event, event.type == synthyboy::Event::NoteOff);
            }
        }
        return 1;
    }

    case effCanBeAutomated:
    {
#if SYNTHYBOY_MPC_OVERLAY
        if (PluginInstance::isOverlayParam (index) || PluginInstance::isPlayParam (index) || PluginInstance::isQParam (index) || PluginInstance::isNativeUiParam (index))
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
        MPCKIT_LOG ("host-edit", info, "effSetChunk instance=%u index=%d bytes=%d",
                    inst->logId, static_cast<int> (index), static_cast<int> (size));
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
                MPCKIT_LOG ("host-edit", info, "effSetChunk decoded instance=%u program=%d notePoints=%zu velocityPoints=%zu",
                            inst->logId, idx, newContent.noteCurve.size(), newContent.velocityCurve.size());

#if SYNTHYBOY_MPC_OVERLAY
                const PluginInstance::QBindingRestore qBindingRestore (*inst);
#endif
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
                {
                    // the view state of the merged tabs: a project reload restores it exactly; a chunk without it (an older one) gives the defaults. A program change (effSetProgram) leaves it: the selector shows the selected item even when the new preset does not use it
                    int tv[synthyboy_mpc::tabview::kFields] = { 0, 0 };
                    synthyboy_mpc::tabview::tabViewFromChunk (chunk, static_cast<size_t>(size), tv);
                    for (int f = 0; f < synthyboy_mpc::tabview::kFields; ++f)
                        inst->tabViewState[f].store (tv[f], std::memory_order_relaxed);
                }
                inst->preparePlayPresentation();
#endif
                {
                    // the output limiter setting rides in the chunk; a chunk without it (an older one) leaves the setting as it is (the default, kOutputLimiterDefaultOn: off);
                    // applyOutputLimiter marks the reported latency stale, the next dispatcher call tells the host
                    bool limiterOn = inst->outputLimiter.load (std::memory_order_relaxed);
                    if (synthyboy_mpc::outputLimiterFromChunk (chunk, static_cast<size_t>(size), limiterOn))
                    {
                        inst->setOutputLimiter (limiterOn);
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
                inst->programNameRefreshDue.store (true, std::memory_order_release);   // the host marks the program as changed after a chunk restore: it reads the name again
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
                    int r = synthyboy_mpc::resolveProgramInView (inst->view, inst->programs, idx, hasName, name);

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
        const int nativeOffset = index - PluginInstance::nativeUiBase();
        if (nativeOffset == nativeui::kFxEditorPower || nativeOffset == nativeui::kLfoTargetsAction)
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax | kVstParameterIsSwitch;
            props->maxInteger = props->stepInteger = 1;
            return 1;
        }
        if (nativeui::isFilterChainMenu (nativeOffset))
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = nativeui::kFilterChainLastChoice;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
            return 1;
        }
        if (nativeui::isMixerStereoOffset (nativeOffset))
        {
            std::memset (props, 0, sizeof (*props));
            return 1;
        }
        if (nativeui::isModBoardControlOffset (nativeOffset))
        {
            const int field = nativeOffset - nativeui::kModBoardControl;
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = field == nativeui::modboard::Source
                ? static_cast<int> (synthyboy::paramTable()[synthyboy::modSlotParams (0).source].max) : 1;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f / props->maxInteger;
            return 1;
        }
        if (nativeui::isMixerControlOffset (nativeOffset))
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterIsSwitch | kVstParameterUsesIntegerMinMax;
            props->maxInteger = 1;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
            return 1;
        }
        if (nativeui::isMixerActiveTabOffset (nativeOffset)
            || nativeui::isMixerSoloVisualOffset (nativeOffset)
            || nativeui::isMixerMuteVisualOffset (nativeOffset))
        {
            const int maxInt = nativeui::isMixerActiveTabOffset (nativeOffset)
                ? nativeui::kMixerTabs - 1 : nativeui::mixer::kLastVisualState;
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = maxInt;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f / static_cast<float> (maxInt);
            return 1;
        }
        if (nativeui::isPlayExpandedOffset (nativeOffset) || nativeui::isMacroTargetOffset (nativeOffset)
            || nativeui::isModDetailControlOffset (nativeOffset))
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = nativeOffset == nativeui::kPlayExpanded ? nativeui::expandedplay::LastFrame : 1;
            props->stepInteger = 1;
            return 1;
        }
        if (nativeui::isModTargetsOffset (nativeOffset))
        {
            namespace n = nativeui::targets;
            const int field = nativeOffset - nativeui::kModTargets;
            const auto cell = inst->nativeModTargets.at (field).cell();
            std::memset (props, 0, sizeof (*props));
            if (field >= n::FirstRow && field < n::kFields && (field - n::FirstRow) % n::RowFields == n::Amount)
            {
                if (cell.target >= 0)
                    fillParameterProperties (synthyboy::paramTable()[cell.target], props);
            }
            else
            {
                props->flags = kVstParameterUsesIntegerMinMax;
                props->maxInteger = field == n::kView ? n::kLastFrame : field == n::Page || field == n::kPageGate ? 3
                    : field >= n::kRowGate ? 2 : 1;
                props->stepInteger = 1;
            }
            return 1;
        }
        if (nativeui::isOscRootLoadOffset (nativeOffset))
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->minInteger = 0;
            props->maxInteger = nativeOffset == nativeui::kOscRootLoadView ? nativeui::oscdetail::kRootLoads : 1;
            props->stepInteger = 1;
            return 1;
        }
        if (nativeui::isPzToolbarOffset (nativeOffset))
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->minInteger = 0;
            props->maxInteger = nativeOffset < nativeui::kPzSource ? 4 : nativeOffset == nativeui::kPzSource ? 2 : 3;
            props->stepInteger = 1;
            return 1;
        }
        if (nativeui::isFxBandOffset (nativeOffset))
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->minInteger = 0;
            props->maxInteger = 3;
            props->stepInteger = 1;
            return 1;
        }
        if (nativeOffset == nativeui::kMasterPanel || nativeOffset == nativeui::kMasterScaleInput)
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->minInteger = 0;
            props->maxInteger = 1;
            props->stepInteger = 1;
            return 1;
        }
        if (nativeui::isOscDetailOffset (nativeOffset))
        {
            namespace n = nativeui::oscdetail;
            const int handle = nativeOffset - nativeui::kOscDetail;
            const auto cell = inst->oscDetailCell (handle);
            std::memset (props, 0, sizeof (*props));
            if (((handle >= n::kValue && handle < n::kStep) || handle >= n::kMenusBase) && cell.target >= 0
                && cell.kind != n::Action && cell.kind != n::Semitone && cell.kind != n::Snap && cell.kind != n::Phase)
                fillParameterProperties (synthyboy::paramTable()[cell.target], props);
            else
            {
                props->flags = kVstParameterUsesIntegerMinMax;
                props->minInteger = handle >= n::kValue && handle < n::kStep && cell.kind == n::Semitone ? -11 : 0;
                props->maxInteger = handle == n::kView ? static_cast<int> (n::LastFrame)
                                  : handle >= n::kKind && handle < n::kNavigation ? n::kVisualKinds
                                  : handle >= n::kStep && handle < n::kKind ? 3
                                  : cell.kind == n::Semitone ? 11 : cell.kind == n::Phase ? 360 : 1;
                props->stepInteger = 1;
            }
            return 1;
        }
        if (index == PluginInstance::nativeUiBase() + nativeui::kMixerPeekView)
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = nativeui::kMixerPeekLastView;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f / nativeui::kMixerPeekLastView;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index))
        {
            namespace n = nativeui::osc;
            const int offset = index - PluginInstance::nativeUiBase();
            const int field = PluginInstance::nativeOscSettingField (offset);
            if (offset == nativeui::kOscSettingsView || offset == nativeui::kOscWarpLayout || field >= 0)
            {
                const auto cell = inst->nativeOscSettings.at (field).cell();
                std::memset (props, 0, sizeof (*props));
                if (field < 0)
                    props->maxInteger = offset == nativeui::kOscSettingsView ? n::kLastSettings : n::kLastWarpMask;
                else if (cell.target < 0)
                    return 1;
                else if (cell.kind == n::SettingsSource)
                    props->maxInteger = n::kLastSourceAction;
                else if (cell.kind == n::SettingsAction)
                    props->maxInteger = 1;
                else if (cell.kind == n::SettingsType)
                    props->maxInteger = 4;
                else if (cell.kind == n::SettingsOctave)
                {
                    props->minInteger = -4;
                    props->maxInteger = 4;
                }
                else
                {
                    const auto& info = synthyboy::paramTable()[cell.target];
                    fillParameterProperties (info, props);
                    if (cell.kind != n::SettingsNumber && cell.kind != n::SettingsVoices)
                        return 1;
                    props->minInteger = static_cast<int> (info.min);
                    props->maxInteger = static_cast<int> (info.max);
                }
                props->flags |= kVstParameterUsesIntegerMinMax;
                props->stepInteger = 1;
                props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
                return 1;
            }
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kPlayOsc
            && index < PluginInstance::nativeUiBase() + nativeui::kPlayLfoStandardSync)
        {
            namespace n = nativeui::playoscui;
            const int handle = index - PluginInstance::nativeUiBase() - nativeui::kPlayOsc;
            std::memset (props, 0, sizeof (*props));
            if (handle == 0)
            {
                props->flags = kVstParameterUsesIntegerMinMax;
                props->maxInteger = n::kLastView;
                props->stepInteger = 1;
                return 1;
            }
            const int source = (handle - 1) / n::kFields + 1;
            const int field = (handle - 1) % n::kFields;
            const bool sub = source == 1, noise = source == n::kSources;
            if ((field == n::Second && noise) || field == n::Pan || field == n::Level)
            {
                if (source == n::Bank::source (inst->nativePlayOsc.view()))
                    if (const auto cell = inst->nativePlayOsc.at (field).cell(); cell.target >= 0)
                        fillParameterProperties (synthyboy::paramTable()[cell.target], props);
                return 1;
            }
            props->flags = kVstParameterUsesIntegerMinMax;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
            props->minInteger = field == n::Second && sub ? -4 : 0;
            props->maxInteger = field == n::First ? (sub ? 5 : noise ? 8 : 4)
                              : field == n::Second ? (sub ? 4 : 3)
                              : field == n::Route ? n::kRoutingDetails : 1;
            if (field == n::Power || field == n::Expand)
                props->flags |= kVstParameterIsSwitch;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kPlayEnvLfo)
        {
            namespace n = nativeui::playenvlfo;
            const int field = PluginInstance::nativePlayEnvLfoField (index - PluginInstance::nativeUiBase());
            if (field == PluginInstance::kUnmappedPlayEnvLfoField)
            {
                std::memset (props, 0, sizeof (*props));
                return 0;
            }
            const int target = inst->nativePlayEnvLfo.at (field).cell().target;
            if (field > n::LfoExpand && field < n::Choose && target >= 0)
                fillParameterProperties (synthyboy::paramTable()[target], props);
            else
            {
                std::memset (props, 0, sizeof (*props));
                props->flags = kVstParameterUsesIntegerMinMax;
                props->maxInteger = field < 0 ? static_cast<int> (n::LastView) : field == n::Choose ? n::kLastChoice
                                                                   : field == n::StandardSync ? 2 : 1;
                if (field == n::EnvExpand || field == n::LfoExpand)
                    props->flags |= kVstParameterIsSwitch;
                props->stepInteger = 1;
            }
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kLfoDetail)
        {
            namespace n = nativeui::lfodetail;
            const int handle = index - PluginInstance::nativeUiBase() - nativeui::kLfoDetail;
            const int field = n::handleField (handle);
            const auto range = n::rangeOf (field);
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->minInteger = range.min;
            props->maxInteger = handle == n::kViewHandle ? static_cast<int> (n::LastView)
                              : handle == n::kPointState ? static_cast<int> (n::LastPointState)
                              : handle == n::kVisibleRows ? n::kRows : range.max;
            if (props->maxInteger == 1)
                props->flags |= kVstParameterIsSwitch;
            props->stepInteger = 1;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kFxChain)
        {
            namespace c = nativeui::fxchain;
            const int field = index - PluginInstance::nativeUiBase() - nativeui::kFxChain;
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = field == c::View ? c::kModes - 1 : field == c::Count ? c::kSlots
                              : field == c::Menu ? c::kTypes : field == c::Page ? 2 : 1;
            if (props->maxInteger == 1)
                props->flags |= kVstParameterIsSwitch;
            props->stepInteger = 1;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kLfoEditor)
        {
            namespace n = nativeui::lfoeditor;
            const int handle = index - PluginInstance::nativeUiBase() - nativeui::kLfoEditor;
            const int field = handle >= n::kValues && handle < n::kAvailability ? handle - n::kValues : -1;
            const auto state = inst->nativeLfoEditor.display();
            const auto range = n::rangeOf (field, state.points, state.scale);
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            if (field < 0 || n::kFields[field].type == n::kToggle || n::kFields[field].type == n::kAction)
                props->flags |= kVstParameterIsSwitch;
            props->minInteger = range.min;
            props->maxInteger = range.max;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f / range.units;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kFxHeader)
        {
            namespace h = nativeui::fxheader;
            const int handle = index - PluginInstance::nativeUiBase() - nativeui::kFxHeader;
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            if (handle < 2)
                props->maxInteger = handle == 0 ? h::kItems : h::kLastLayout;
            else if ((handle - 2) % h::kFields == h::Rack)
            {
                h::State state;
                state.item = (handle - 2) / h::kFields;
                props->maxInteger = state.rackCount() - 1;
            }
            else if ((handle - 2) % h::kFields == h::Page)
            {
                props->maxInteger = 2;
                props->stepFloat = props->smallStepFloat = props->largeStepFloat = 0.5f;
            }
            else
            {
                props->flags |= kVstParameterIsSwitch;
                props->maxInteger = 1;
            }
            props->stepInteger = 1;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kOscRoutingBySource)
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = nativeui::osc::kLastRoute;
            props->stepInteger = 1;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kMasterOutput)
        {
            namespace m = nativeui::master;
            const int field = index - PluginInstance::nativeUiBase() - nativeui::kMasterOutput;
            std::memset (props, 0, sizeof (*props));
            if (field == m::VoiceCount || field >= m::Limiter)
            {
                props->flags = kVstParameterUsesIntegerMinMax;
                if (field >= m::Limiter)
                    props->flags |= kVstParameterIsSwitch;
                props->minInteger = 0;
                props->maxInteger = field == m::VoiceCount ? m::kVoiceCountLimit : 1;
                props->stepInteger = 1;
            }
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kOscPower)
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = props->stepInteger = 1;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kVoiceSequence)
        {
            namespace n = nativeui::voice;
            const int field = index - PluginInstance::nativeUiBase() - nativeui::kVoiceSequence;
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->minInteger = field == n::kValue ? -100 : 0;
            props->maxInteger = field == n::kValue ? 100 : field == n::kRow ? n::kRows - 1 : n::kSteps - 1;
            props->stepInteger = 1;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kModNavigation)
        {
            namespace n = nativeui::mod;
            static constexpr int limits[] { n::kLastMode, n::kLastSourceGroup, n::kLastDestinationGroup };
            const int field = index - PluginInstance::nativeUiBase() - nativeui::kModNavigation;
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = field < n::Add ? limits[field] : 1;
            props->stepInteger = 1;
            return 1;
        }
        if (index == PluginInstance::nativeUiBase() + nativeui::kPlayLfoStandard)
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = props->stepInteger = 1;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kEnvView)
        {
            namespace n = nativeui::env;
            const int field = index - PluginInstance::nativeUiBase() - nativeui::kEnvFields;
            const int target = inst->nativeEnvEditor.at (field).cell().target;
            if (field >= 0 && field < n::Selection && target >= 0)
                fillParameterProperties (synthyboy::paramTable()[target], props);
            else
                std::memset (props, 0, sizeof (*props));
            if (field == -1 || field >= n::Beats)
            {
                props->flags |= kVstParameterUsesIntegerMinMax;
                props->maxInteger = field == n::Selection ? n::kInstances - 1 : 1;
                props->stepInteger = 1;
            }
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kModView)
        {
            const int offset = index - PluginInstance::nativeUiBase();
            const int target = inst->nativeMod.at (offset - nativeui::kModFields).cell().target;
            if (target >= 0)
                fillParameterProperties (synthyboy::paramTable()[target], props);
            else
                std::memset (props, 0, sizeof (*props));
            if (offset < nativeui::kModFields)
            {
                props->flags = kVstParameterUsesIntegerMinMax;
                props->maxInteger = offset == nativeui::kModView ? nativeui::mod::kLastView : nativeui::mod::kLastSlot;
                props->stepInteger = 1;
            }
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kOscBase)
        {
            namespace n = nativeui::osc;
            const int field = index - PluginInstance::nativeUiBase() - nativeui::kOscBase;
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->maxInteger = field == n::kSource ? n::kLastSource - 1 : field == n::kRouting ? n::kLastRoute
                              : field == n::kVisibility ? n::kRootOn : nativeui::master::kLastFrame;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kFilterBase)
        {
            const int offset = index - PluginInstance::nativeUiBase();
            const int slot = offset - nativeui::kFilterValueBase;
            const auto cell = inst->nativeFilter.at (slot).cell();
            const auto* source = cell.target >= 0 && (slot < nativeui::FilterPoint || slot == nativeui::FilterLegato)
                               ? synthyboy::paramTable() + cell.source : nullptr;
            if (source != nullptr)
                fillParameterProperties (*source, props);
            else
                std::memset (props, 0, sizeof (*props));
            const int last = offset == nativeui::kFilterBase ? nativeui::kFilterLastViewFrame
                           : slot == nativeui::FilterAction ? nativeui::FilterLastAction
                           : offset >= nativeui::kFilterIndicatorBase ? nativeui::kFilterIndicatorLastFrame : -1;
            if (last > 0)
            {
                props->flags = kVstParameterUsesIntegerMinMax;
                props->maxInteger = last;
                props->stepInteger = 1;
                props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
            }
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kLfoBase)
        {
            namespace n = nativeui::lfo;
            const int field = index - PluginInstance::nativeUiBase() - nativeui::kLfoBase;
            const auto* info = PluginInstance::nativeLfoInfo (field);
            if (info != nullptr)
                fillParameterProperties (*info, props);
            else
                std::memset (props, 0, sizeof (*props));
            const int last = field == n::kSelection ? n::kInstances - 1 : field == n::kAction ? n::kActionLast
                           : field == n::kLayout ? n::kLayoutLast : -1;
            if (last > 0 || field == n::kTranspose || field == n::kTune)
            {
                props->flags = kVstParameterUsesIntegerMinMax;
                props->minInteger = info != nullptr ? static_cast<int> (info->min) : 0;
                props->maxInteger = info != nullptr ? static_cast<int> (info->max) : last;
                props->stepInteger = 1;
                props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
            }
            return 1;
        }
        if (PluginInstance::isNativeUiParam (index) && index >= PluginInstance::nativeUiBase() + nativeui::kFxValueBase)
        {
            const int offset = index - PluginInstance::nativeUiBase();
            const int domain = offset - nativeui::kFxDomainBase;
            const auto cell = inst->nativeFxEndpoint (offset).cell();
            const auto* source = cell.source >= 0 ? synthyboy::paramTable() + cell.source : nativeui::fxDomainSource (domain);
            if (source != nullptr)
                fillParameterProperties (*source, props);
            else
                std::memset (props, 0, sizeof (*props));
            const int model = nativeui::fxStyleModel (nativeui::kFxFirstDomain + domain);
            if (domain >= 0 || cell.kind == nativeui::kFxToggle || (offset >= nativeui::kFxKindBase && offset <= nativeui::kFxLayout))
            {
                props->flags = kVstParameterUsesIntegerMinMax;
                props->minInteger = source != nullptr ? static_cast<int> (source->min) : 0;
                props->maxInteger = model >= 0 ? synthyboy::filters::filterModelStyleCount (model) - 1
                                  : source != nullptr ? static_cast<int> (source->max)
                                  : offset == nativeui::kFxLayout ? nativeui::kFxLayoutLast : nativeui::kFxKindLast;
                props->stepInteger = 1;
                props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f;
            }
            return 1;
        }
        if (index == PluginInstance::nativeUiBase() + nativeui::kMixerPeek)
        {
            std::memset (props, 0, sizeof (*props));
            props->flags = kVstParameterUsesIntegerMinMax;
            props->minInteger = 0;
            props->maxInteger = nativeui::kMixerPeekTargets;
            props->stepInteger = 1;
            props->stepFloat = props->smallStepFloat = props->largeStepFloat = 1.0f / nativeui::kMixerPeekTargets;
            return 1;
        }
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
            const int real = inst->qBank.target (index - PluginInstance::qBase());
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
                props->flags = kVstParameterCanRamp | kVstParameterUsesIntegerMinMax;
                props->minInteger = 0;
                props->maxInteger = 100;
                props->stepInteger = 1;
                props->largeStepInteger = 10;
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
        return SYNTHYBOY_VST2_VERSION; // 0.1.0 build 1 -> 101
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
        if (inst)
        {
            inst->soundOffRequests.fetch_add (1u, std::memory_order_relaxed);
            inst->soundOffPending.store (true, std::memory_order_release);   // the voices stop at the next block (CC 120 for the engine): nothing keeps sounding after the host stopped processing
        }
        return 1;
    }

    default:
        return 0;
    }
}

#if SYNTHYBOY_MPC_DEBUG_HOOKS
void PluginInstance::remoteProgramSelectPoll (AEffect* e)
{
    if (e == nullptr)
        return;
    static const std::string path = [] {
        const char* p = std::getenv ("SYNTHYBOY_MPC_SELECT_PROGRAM_FILE");
        return std::string (p != nullptr && *p != '\0' ? p : "/tmp/synthyboy-select-program");
    }();
    char text[synthyboy_mpc::safefile::kSwitchBytes];
    if (! synthyboy_mpc::safefile::readFirstLine (path.c_str(), text, sizeof text))
        return;
    ::unlink (path.c_str());

    static std::atomic<long long> ackCounter { 0 };
    const long long seq = ++ackCounter;
    static const std::string ackPath = path + ".ack";

    auto writeAck = [&] (const char* status, const char* payload)
    {
        char ackBuf[512];
        std::snprintf (ackBuf, sizeof ackBuf, "%lld %s %s\n", seq, status, payload != nullptr ? payload : "");
        synthyboy_mpc::safefile::writeReplacing (ackPath.c_str(), ackBuf, std::strlen (ackBuf));
    };

    // Trim leading and trailing ASCII whitespace
    char* start = text;
    while (*start != '\0' && static_cast<unsigned char> (*start) <= ' ')
        ++start;
    char* end = start + std::strlen (start);
    while (end > start && static_cast<unsigned char> (*(end - 1)) <= ' ')
        --end;
    *end = '\0';

    if (*start == '\0')
    {
        writeAck ("error", "unknown name");
        return;
    }

    // Check if `start` is a decimal integer: optional sign then all digits
    bool isDecimal = true;
    const char* p = start;
    if (*p == '+' || *p == '-')
        ++p;
    if (*p == '\0')
        isDecimal = false;
    else
    {
        while (*p != '\0')
        {
            if (*p < '0' || *p > '9')
            {
                isDecimal = false;
                break;
            }
            ++p;
        }
    }

    const int numProgs = view.numPrograms();
    int targetProg = -1;

    if (isDecimal)
    {
        char* endp = nullptr;
        errno = 0;
        const long val = std::strtol (start, &endp, 10);
        if (errno == ERANGE || val < 0 || val >= numProgs)
        {
            writeAck ("error", "out of range");
            return;
        }
        targetProg = static_cast<int> (val);
    }
    else
    {
        const std::string name (start);
        if (name == "Init" || name == "init")
        {
            targetProg = 0;
        }
        else
        {
            for (int i = 1; i < numProgs; ++i)
            {
                if (view.isHeader (i))
                    continue;
                const auto& it = view.items[static_cast<size_t> (i)];
                if (it.baseName == name || it.label == name)
                {
                    targetProg = i;
                    break;
                }
            }
        }
        if (targetProg < 0)
        {
            writeAck ("error", "unknown name");
            return;
        }
    }

    // Target program is valid. Select through effSetProgram
    if (e->dispatcher != nullptr)
        e->dispatcher (e, effSetProgram, 0, targetProg, nullptr, 0.0f);
    programNameRefreshDue.store (true, std::memory_order_release);
    if (audioMaster)
        audioMaster (e, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);

    const int currentProg = currentProgram.load (std::memory_order_relaxed);
    char nameBuf[kVstMaxProgNameLen + 1] = {};
    if (e->dispatcher != nullptr)
        e->dispatcher (e, effGetProgramName, 0, 0, nameBuf, 0.0f);

    char okPayload[256];
    std::snprintf (okPayload, sizeof okPayload, "%d %s", currentProg, nameBuf);
    writeAck ("ok", okPayload);
}
#endif

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
    if (plain == oldPlain || synthyboy::toNormalised (*info, oldPlain) == parameter)
        return;
#if SYNTHYBOY_MPC_OVERLAY
    inst->paramUndo[index].store (oldPlain, std::memory_order_relaxed);
#endif

    inst->paramMirror[index].store(plain, std::memory_order_relaxed);

    // Disarm protection: any parameter write from the host disarms it
    inst->protectedProgram.store (-1, std::memory_order_relaxed);

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
        if (PluginInstance::isNativeUiParam (index))
        {
            if (! restoring)
            {
                const int offset = index - PluginInstance::nativeUiBase();
                if (offset == nativeui::kLfoTargetsAction)
                {
                    namespace n = nativeui::lfo;
                    if (parameter > 0.5f && parameter <= 1.0f)
                        inst->nativeLfo.post (n::kAction, static_cast<float> (n::kTargets) / n::kActionLast);
                    return;
                }
                if (offset == nativeui::kFxEditorPower)
                    return;
                if (nativeui::isFilterChainMenu (offset))
                {
                    if (! std::isfinite (parameter) || parameter < 0.0f || parameter > 1.0f || inst->nativeFilter.layout() == 0)
                        return;
                    const int choice = static_cast<int> (std::lround (parameter * nativeui::kFilterChainLastChoice));
                    if (choice >= 2)
                    {
                        const auto* route = synthyboy::findParamByKey ("filter.routing");
                        if (route != nullptr)
                            inst->uiWrite (static_cast<int> (route - synthyboy::paramTable()), static_cast<float> (choice - 2) / 2.0f);
                    }
                    else if (inst->nativeMixerPeekView.load (std::memory_order_acquire) == nativeui::MixerPeekFilter)
                        inst->mixerPeek.post (static_cast<float> (choice + 1) / nativeui::kMixerPeekTargets);
                    else
                        inst->nativeFilter.post (inst->nativeFilter.at (nativeui::FilterAction),
                            static_cast<float> (nativeui::FilterF1 + choice) / nativeui::FilterLastAction);
                    return;
                }
                if (nativeui::isMixerStereoOffset (offset)) return;
                if (nativeui::isModDetailControlOffset (offset))
                {
                    const int field = offset - nativeui::kModDetailControl;
                    if (field == nativeui::moddetail::Confirm)
                        return;
                    if (parameter >= 0.5f)
                    {
                        const auto link = inst->nativeModDetailNavigation.at (field);
                        inst->nativeModDetailNavigation.post (link, parameter);
                    }
                    return;
                }
                if (nativeui::isModBoardControlOffset (offset))
                {
                    const int field = offset - nativeui::kModBoardControl;
                    if (field >= nativeui::modboard::ListGate || field == nativeui::modboard::Gate)
                        return;
                    const auto link = inst->nativeModBoard.at (field);
                    inst->nativeModBoard.post (link, parameter);
                    return;
                }
                if (nativeui::isMixerControlOffset (offset))
                {
                    const int idx = offset - nativeui::kMixerControl;
                    if (idx < nativeui::mixer::kMute)
                    {
                        const int strip = idx - nativeui::mixer::kSolo;
                        if (inst->engine != nullptr)
                            inst->engine->setMixerSolo (strip, parameter >= 0.5f);
                    }
                    else
                    {
                        const int strip = idx - nativeui::mixer::kMute;
                        if (inst->engine != nullptr)
                            inst->engine->setMixerMute (strip, parameter >= 0.5f);
                    }
                    inst->refreshNativeMixerVisuals();
                    return;
                }
                if (nativeui::isModDetailReservedOffset (offset)
                    || nativeui::isMixerActiveTabOffset (offset)
                    || nativeui::isMixerSoloVisualOffset (offset)
                    || nativeui::isMixerMuteVisualOffset (offset))
                    return;   // read-only presentation / reserved slots
                if (nativeui::isMacroTargetOffset (offset))
                {
                    const auto link = inst->nativeMacroTargets.at (offset - nativeui::kMacroTarget);
                    if (parameter >= 0.5f && inst->nativeMacroTargetPreset.load (std::memory_order_acquire)
                                            == inst->stateGeneration.load (std::memory_order_acquire))
                        inst->nativeMacroTargets.post (link, parameter);
                    return;
                }
                if (nativeui::isPlayExpandedOffset (offset))
                {
                    if (offset > nativeui::kPlayExpanded && parameter >= 0.5f)
                        inst->nativeExpandedPlay.post (offset - nativeui::kPlayExpanded - 1, parameter,
                                                     inst->stateGeneration.load (std::memory_order_acquire));
                    return;
                }
                if (nativeui::isModTargetsOffset (offset))
                {
                    namespace n = nativeui::targets;
                    const int field = offset - nativeui::kModTargets;
                    const auto link = inst->nativeModTargets.at (field);
                    const auto cell = link.cell();
                    const bool action = field == n::Back || field == n::Add
                        || (field >= n::FirstRow && (field - n::FirstRow) % n::RowFields == n::Remove);
                    if (field < n::kFields && cell.target >= 0 && (! action || parameter >= 0.5f)
                        && std::fabs (parameter - inst->nativeModTargetValue (field, cell)) > 1.0e-6f)
                        inst->nativeModTargets.post (link, parameter, inst->stateGeneration.load (std::memory_order_acquire));
                    return;
                }
                if (nativeui::isOscRootLoadOffset (offset))
                {
                    if (offset < nativeui::kOscRootLoadView && parameter >= 0.5f)
                    {
                        const int field = nativeui::oscdetail::kRootLoad + offset - nativeui::kOscRootLoad;
                        inst->nativeOscDetail.post (inst->nativeOscDetail.at (field), parameter,
                                                    inst->stateGeneration.load (std::memory_order_acquire));
                    }
                    return;
                }
                if (nativeui::isPzToolbarOffset (offset))
                {
                    if (offset < nativeui::kPzSource && parameter >= 0.125f && inst->nativePzToolbarPreset.load (std::memory_order_acquire)
                            == inst->stateGeneration.load (std::memory_order_acquire))
                        inst->nativePzActions.post (inst->nativePzActions.at (offset - nativeui::kPzAction), parameter);
                    return;
                }
                if (nativeui::isFxBandOffset (offset))
                {
                    namespace h = nativeui::fxheader;
                    const auto state = inst->nativeFxHeader.state();
                    if (offset < nativeui::kFxBandMode && parameter >= 0.2f
                        && std::fabs (parameter - state.value (h::kBand)) > 1.0e-6f)
                        inst->nativeFxHeader.post (offset - nativeui::kFxBandBase, h::kBand, parameter,
                                                  inst->stateGeneration.load (std::memory_order_acquire));
                    return;
                }
                if (offset == nativeui::kMasterScaleInput)
                {
                    int mode = inst->scaleInput.load();
                    if (parameter >= 0.5f && mode == PluginInstance::ScaleInputOff)
                        inst->scaleInput.compare_exchange_strong (mode, PluginInstance::ScaleInputArmed);
                    else if (parameter < 0.5f)
                        while (mode >= PluginInstance::ScaleInputArmed
                               && !inst->scaleInput.compare_exchange_weak (mode, PluginInstance::ScaleInputOff)) {}
                    if (logging)
                        synthyboy_mpc::PlayLog::line ("key-mode input=%.3f state=%d", parameter, inst->scaleInput.load());
                    return;
                }
                if (offset == nativeui::kMasterPanel)
                {
                    const auto link = inst->nativeMasterPanel.at (0);
                    const auto preset = inst->nativeMasterPanelPreset.load (std::memory_order_acquire);
                    if (preset == inst->stateGeneration.load (std::memory_order_acquire)
                        && (parameter == 0.0f || parameter == 1.0f) && link.cell().target >= 0
                        && parameter != nativeui::master::panelValue (link.cell().source))
                        inst->nativeMasterPanel.post (link, parameter);
                    return;
                }
                if (nativeui::isOscDetailOffset (offset))
                {
                    namespace n = nativeui::oscdetail;
                    const int handle = offset - nativeui::kOscDetail;
                    const uint64_t preset = inst->stateGeneration.load (std::memory_order_acquire);
                    if (handle >= n::kMenusBase)
                    {
                        if (std::fabs (parameter - inst->nativeUiValue (offset)) > 1.0e-6f)
                            inst->nativeOscDetail.postMenu (handle - n::kMenusBase, parameter, preset);
                    }
                    else if (handle != n::kView && (handle < n::kKind || handle >= n::kNavigation))
                    {
                        const auto snapshot = inst->nativeOscDetail.at (PluginInstance::oscDetailField (handle));
                        const auto cell = snapshot.cell();
                        const bool readOnly = handle < n::kStep && (cell.kind == n::Choice || cell.kind >= n::kFirstMenuKind);
                        if (! readOnly && std::fabs (parameter - inst->oscDetailValue (handle, cell)) > 1.0e-6f)
                            inst->nativeOscDetail.post (snapshot, parameter, preset);
                    }
                    return;
                }
                if (offset == nativeui::kMixerPeekView)
                    return;   // published only by the Mixer view
                if (const int field = PluginInstance::nativeOscSettingField (offset); field >= 0)
                {
                    const auto snapshot = inst->nativeOscSettings.at (field);
                    const auto cell = snapshot.cell();
                    if (inst->nativeOsc.snapshot().powered() && cell.target >= 0
                        && std::fabs (parameter - inst->nativeOscSettingValue (cell)) > 1.0e-6f)
                        inst->nativeOscSettings.post (snapshot, parameter);
                    return;
                }
                if (offset == nativeui::kOscSettingsView || offset == nativeui::kOscWarpLayout)
                    return;
                if (offset >= nativeui::kPlayOsc && offset < nativeui::kPlayLfoStandardSync)
                {
                    namespace n = nativeui::playoscui;
                    const int handle = offset - nativeui::kPlayOsc;
                    if (handle > 0)
                    {
                        const int source = (handle - 1) / n::kFields + 1;
                        if (source == inst->playSel[0].load (std::memory_order_acquire)
                            && std::fabs (parameter - inst->nativeUiValue (offset)) > 1.0e-6f)
                            inst->nativePlayOsc.post (source, (handle - 1) % n::kFields, parameter,
                                inst->stateGeneration.load (std::memory_order_acquire), inst->playOscSelectionRev.load (std::memory_order_acquire));
                    }
                    return;
                }
                if (offset >= nativeui::kPlayEnvLfo)
                {
                    const int field = PluginInstance::nativePlayEnvLfoField (offset);
                    if (field >= 0 && std::fabs (parameter - inst->nativePlayEnvLfoValue (field)) > 1.0e-6f)
                        inst->nativePlayEnvLfo.post (field, parameter, inst->stateGeneration.load (std::memory_order_acquire),
                                                     inst->playEnvLfoRevision.load (std::memory_order_acquire));
                    return;
                }
                if (offset >= nativeui::kLfoDetail)
                {
                    const int field = nativeui::lfodetail::handleField (offset - nativeui::kLfoDetail);
                    if (field >= 0 && std::fabs (parameter - inst->nativeLfoDetail.value (field)) > 1.0e-6f)
                        inst->nativeLfoDetail.post (inst->nativeLfoDetail.at (field), parameter);
                    return;
                }
                if (offset >= nativeui::kFxChain)
                {
                    if (std::fabs (parameter - inst->nativeUiValue (offset)) > 1.0e-6f)
                        inst->nativeFxChain.post (offset - nativeui::kFxChain, parameter, inst->stateGeneration.load (std::memory_order_acquire));
                    return;
                }
                if (offset >= nativeui::kLfoEditor)
                {
                    namespace n = nativeui::lfoeditor;
                    const int field = offset - nativeui::kLfoEditor - n::kValues;
                    if (field >= 0 && field < n::kCount
                        && std::fabs (parameter - inst->nativeLfoEditor.value (field)) > 1.0e-6f)
                        inst->nativeLfoEditor.post (inst->nativeLfoEditor.at (field), parameter);
                    return;
                }
                if (offset >= nativeui::kFxHeader)
                {
                    namespace h = nativeui::fxheader;
                    const int action = offset - nativeui::kFxHeader - 2;
                    if (action >= 0 && std::fabs (parameter - inst->nativeUiValue (offset)) > 1.0e-6f)
                        inst->nativeFxHeader.post (action / h::kFields, action % h::kFields, parameter,
                                                   inst->stateGeneration.load (std::memory_order_acquire));
                    return;
                }
                if (offset >= nativeui::kOscRoutingBySource)
                {
                    inst->nativeOsc.post (nativeui::osc::kRouting, parameter, offset - nativeui::kOscRoutingBySource + 1);
                    return;
                }
                if (offset >= nativeui::kMasterOutput)
                {
                    namespace m = nativeui::master;
                    const int field = offset - nativeui::kMasterOutput;
                    if (inst->nativeMasterView.load (std::memory_order_relaxed) == m::Main
                        && std::isfinite (parameter))
                    {
                        if (field == m::Limiter && (parameter >= 0.5f) != inst->outputStage.limiter())
                            inst->setOutputLimiter (parameter >= 0.5f);
                        else if (field == m::ClearClip && parameter >= 0.5f)
                        {
                            inst->nativeMasterClearPending.store (true, std::memory_order_relaxed);
                            inst->nativeMasterClearAck.store (true, std::memory_order_relaxed);
                        }
                    }
                    return;
                }
                if (offset >= nativeui::kOscPower)
                {
                    inst->nativeOsc.postPower (offset - nativeui::kOscPower + 1, parameter);
                    return;
                }
                if (offset >= nativeui::kVoiceSequence)
                {
                    const auto cell = inst->nativeVoice.at (offset - nativeui::kVoiceSequence);
                    if (std::fabs (parameter - inst->nativeVoiceValue (cell.slot, cell.cell())) > 1.0e-6f)
                        inst->nativeVoice.post (cell, parameter);
                    return;
                }
                if (offset >= nativeui::kModNavigation)
                {
                    namespace n = nativeui::mod;
                    const int field = offset - nativeui::kModNavigation;
                    const bool changed = field >= n::Add ? parameter >= 0.5f
                        : std::fabs (parameter - inst->nativeModNavigationValue (field)) > 1.0e-6f;
                    if (changed)
                        inst->nativeModNavigation.post (inst->nativeModNavigation.at (field), parameter);
                    return;
                }
                if (offset == nativeui::kPlayLfoStandard)
                    return;
                if (offset >= nativeui::kEnvView)
                {
                    if (offset >= nativeui::kEnvFields && std::fabs (parameter - inst->nativeUiValue (offset)) > 1.0e-6f)
                        inst->nativeEnvEditor.post (inst->nativeEnvEditor.at (offset - nativeui::kEnvFields), parameter);
                    return;
                }
                if (offset >= nativeui::kModView)
                {
                    if (offset >= nativeui::kModFields && std::fabs (parameter - inst->nativeUiValue (offset)) > 1.0e-6f)
                        inst->nativeMod.post (inst->nativeMod.at (offset - nativeui::kModFields), parameter);
                    return;
                }
                if (offset == nativeui::kMasterView)
                    return;   // the overlay is the exclusive publisher of the tab's visibility
                if (offset == nativeui::kMixerPeek)
                    inst->mixerPeek.post (parameter);
                else if (offset >= nativeui::kOscBase)
                    inst->nativeOsc.post (offset - nativeui::kOscBase, parameter);
                else if (offset >= nativeui::kFilterBase)
                {
                    const int slot = offset - nativeui::kFilterValueBase;
                    if (slot >= 0 && slot < nativeui::FilterSlots && slot != nativeui::FilterModel && slot != nativeui::FilterStyle
                        && std::fabs (parameter - inst->nativeFilterValue (slot)) > 1.0e-6f)
                    {
                        const auto binding = inst->nativeFilter.at (slot);
                        MPCKIT_LOG ("host-edit", info, "setParameter instance=%u index=%d filterSlot=%s target=%d norm=%.9g",
                                    inst->logId, static_cast<int> (index), nativeui::kFilterNames[slot], binding.cell().target,
                                    static_cast<double> (parameter));
                        inst->nativeFilter.post (binding, parameter);
                    }
                }
                else if (offset >= nativeui::kLfoBase)
                {
                    const int field = offset - nativeui::kLfoBase;
                    if (std::fabs (parameter - inst->nativeLfo.value (field)) > 1.0e-6f)
                        inst->nativeLfo.post (field, parameter);
                }
                else if (offset >= nativeui::kFxValueBase)
                {
                    const auto menu = inst->nativeFilterMenu (offset);
                    if (menu.slot >= 0)
                    {
                        if (std::fabs (parameter - inst->nativeFxValue (menu.cell())) > 1.0e-6f)
                            inst->nativeFilter.post (menu, parameter);
                    }
                    else
                    {
                        const auto snapshot = inst->nativeFxEndpoint (offset);
                        const auto cell = snapshot.cell();
                        if (inst->nativeFxHeader.state().on() && cell.target >= 0
                            && std::fabs (parameter - inst->nativeFxValue (cell)) > 1.0e-6f)
                            inst->nativeFxCells.post (snapshot, parameter);
                    }
                }
            }
            return;
        }
        if (PluginInstance::isQParam (index))
        {
            const int q = index - PluginInstance::qBase();
            const synthyboy_mpc::QRouter::Write w = inst->qBank.hostWrite (q, parameter, restoring || ! inst->qBindingsReady(),
                                                                          [inst] (int t, float v) { inst->qWrite (t, v); });
            if (logging)
            {
                static const char* const kOutcome[] = { "no target", "echo, nothing changed", "ignored (restore or stale Q map)", "written" };
                synthyboy_mpc::PlayLog::line ("setParameter %d (Q %d, real %d) %.9g -> %s", static_cast<int> (index), q + 1, inst->qBank.target (q), static_cast<double> (parameter), kOutcome[static_cast<int> (w)]);
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
            if (inst->playProxyBlockedByRandomLfo (p))
            {
                if (logging)
                    synthyboy_mpc::PlayLog::line ("setParameter %d (Play proxy %d) -> ignored (standard LFO control)", static_cast<int> (index), p);
                return;
            }
            const int real = inst->playRealOf (p, PluginInstance::PlayProxyAccess::Write);
            if (real < 0)
                return;
            // the proxy IS the real parameter of the selected instance: the same write as the host's own write to that parameter, so the engine sees an ordinary change. The value the host
            // sends back after a report (or the same value again) changes nothing and disarms nothing.
            const float v = std::isfinite (parameter) ? std::min (std::max (parameter, 0.0f), 1.0f) : 0.0f;
            const bool echo = std::fabs (v - inst->playTargetValue (real)) <= PluginInstance::kEchoTolerance;
            if (logging)
                synthyboy_mpc::PlayLog::line ("setParameter %d (Play proxy %d, real %d) %.9g -> %s", static_cast<int> (index), p, real, static_cast<double> (parameter), echo ? "echo, nothing changed" : (restoring ? "ignored (restore window)" : "written"));
            if (echo || restoring)
                return;
            writeRealParam (inst, static_cast<size_t> (real), v);
            inst->playLast[p].store (v, std::memory_order_relaxed);   // the host knows this value: the poll does not report it back
            inst->playMarkRealDue (static_cast<unsigned> (real));
            return;
        }
        if (static_cast<size_t> (index) < synthyboy::paramCount())
        {
#if MPCKIT_DEV_LOG
            // A restore may replay the whole table. Keep the default trace scoped to the Filter observation.
            const char* key = synthyboy::paramTable()[index].key;
            if (std::strncmp (key, "filter1.", 8) == 0 || std::strncmp (key, "filter2.", 8) == 0 || std::strcmp (key, "filter.routing") == 0)
                MPCKIT_LOG ("host-edit", info, "setParameter instance=%u index=%d key=%s norm=%.9g restoreWindow=%d",
                            inst->logId, static_cast<int> (index), key, static_cast<double> (parameter), restoring ? 1 : 0);
#endif
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
        return inst->qBank.value (index - PluginInstance::qBase());
    if (PluginInstance::isNativeUiParam (index))
        return inst->nativeUiValue (index - PluginInstance::nativeUiBase());
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

// Isolated noinline render block so the compiler cannot hoist or sink inlined DSP arithmetic
// (OutputStage::process, OutputPeak::note, SpectrumRing::push) across the caller's FlushDenormalsScope.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static void renderAudioBlock(PluginInstance* inst, float* out0, float* out1, VstInt32 sampleFrames) noexcept
{
    // Pass events directly to the engine (no slicing, no copy)
    inst->engine->process(inst->events.count > 0 ? inst->events.slots : nullptr,
                          static_cast<int>(inst->events.count),
                          out0, out1, sampleFrames);
    // Make-up gain and the look-ahead peak limiter (output_stage.h; off: the gain only, no delay): what leaves here is what the host gets, so the spectrum below shows the same signal.
    inst->outputStage.process (out0, out1, sampleFrames);
    if (inst->latencyNow() != inst->latencyReported.load (std::memory_order_relaxed))
        inst->latencyDirty.store (true, std::memory_order_release);   // told to the host by the next dispatcher call off the audio thread
    if (out0 != nullptr)
        synthyboy_mpc::outputPeak().note (out0, out1, sampleFrames);   // the peak for the status file (output_peak.h): one pass, relaxed atomics
#if SYNTHYBOY_MPC_SPECTRUM
    if (out0 != nullptr)
        inst->spectrumRing.push (out0, out1, sampleFrames); // the only thing the audio thread does for the display: atomic stores into a preallocated ring
#endif
}

static void pluginProcessReplacing(AEffect* e, float** /*inputs*/, float** outputs, VstInt32 sampleFrames)
{
    // Return early for sampleFrames <= 0 (keep pending events for next block; must precede any memset)
    if (sampleFrames <= 0)
        return;

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

    const int64_t firstBlockT0 = synthyboy_mpc::startupTimes().reached (synthyboy_mpc::StartupTimes::kFirstAudio) ? 0 : synthyboy_mpc::StartupTimes::nowNs();   // only until the first block has gone all the way

    // Update transport from host once per block (VST2 ABI: index = 0, value = filter flags)
    if (inst->audioMaster)
    {
        VstTimeInfo* timeInfo = reinterpret_cast<VstTimeInfo*>(
            static_cast<VstIntPtr>(inst->audioMaster(e, audioMasterGetTime, 0, kVstTempoValid | kVstPpqPosValid | kVstTransportPlaying, nullptr, 0.0f)));
        if (timeInfo)
        {
            if ((timeInfo->flags & kVstTempoValid) != 0 && std::isfinite(timeInfo->tempo) && timeInfo->tempo > 0.0)
                inst->transport.bpm = timeInfo->tempo;
#if SYNTHYBOY_MPC_OVERLAY
            inst->overlayBpm.store (inst->transport.bpm, std::memory_order_relaxed);
#endif
            if ((timeInfo->flags & kVstPpqPosValid) != 0 && std::isfinite(timeInfo->ppqPos))
                inst->transport.songPosBeats = timeInfo->ppqPos;
            inst->transport.playing = (timeInfo->flags & kVstTransportPlaying) != 0;
        }
        // If host returns null, keep previous values
        inst->engine->setTransport(inst->transport);
    }

    // take over what the host delivered since the last block (events.push keeps its overflow rules)
    {
        synthyboy::Event taken;
        while (inst->events.count < EventBuffer::kCapacity && inst->eventInbox.pop (taken))
            inst->events.push (taken);
    }

    // Clamp event frames to the last frame of the block and keep monotonic order (OwnEngine::process assumes frame-sorted events)
    bool sorted = true;
    for (size_t i = 0; i < inst->events.count; ++i)
    {
        if (static_cast<int32_t>(inst->events.slots[i].frame) >= sampleFrames)
            inst->events.slots[i].frame = sampleFrames - 1;
        if (i > 0 && inst->events.slots[i].frame < inst->events.slots[i - 1].frame)
            sorted = false;
    }
    if (!sorted)
    {
        for (size_t i = 1; i < inst->events.count; ++i)
        {
            const synthyboy::Event key = inst->events.slots[i];
            size_t j = i;
            while (j > 0 && inst->events.slots[j - 1].frame > key.frame)
            {
                inst->events.slots[j] = inst->events.slots[j - 1];
                --j;
            }
            inst->events.slots[j] = key;
        }
    }

    // All Sound Off (stop / mains off): one event at the front of the block, the same as CC 120 from the keyboard
    if (inst->soundOffPending.exchange (false, std::memory_order_acquire))
    {
        synthyboy::Event off;
        off.type = synthyboy::Event::AllNotesOff;
        off.channel = -1;
        off.value = 1.0f;
        off.frame = 0;
        const size_t keep = std::min (inst->events.count, EventBuffer::kCapacity - 1);
        for (size_t i = keep; i > 0; --i)
            inst->events.slots[i] = inst->events.slots[i - 1];
        inst->events.slots[0] = off;
        inst->events.count = keep + 1;
    }

    float* out0 = (e->numOutputs > 0) ? outputs[0] : nullptr;
    float* out1 = (e->numOutputs > 1) ? outputs[1] : nullptr;

    // Render inside FlushDenormalsScope after audioMasterGetTime so host callbacks run in the host's FP mode and cannot clear FZ.
    {
        synthyboy_mpc::FlushDenormalsScope flushDenormals;
        renderAudioBlock(inst, out0, out1, sampleFrames);
    }

    // Clear remaining events
    inst->events.clear();

    if (firstBlockT0 != 0)
        synthyboy_mpc::startupTimes().addDetail (synthyboy_mpc::StartupTimes::kFirstBlock, synthyboy_mpc::StartupTimes::nowNs() - firstBlockT0);
    synthyboy_mpc::startupTimes().mark (synthyboy_mpc::StartupTimes::kFirstAudio);   // the first block that went all the way (a relaxed load after the first)
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

namespace
{
/** The policy of the engine's helper threads (the effect module builder, the Convolve job thread; engine-own/dsp/helper_thread.h): a normal scheduling class, nice 0, and the cores the overlay
    thread uses (not the isolated cores and not the cores the audio threads are pinned to, overlay/discovery.h planCpus), instead of what they inherit from the thread that happened to
    create them (the host's loader, a thread of any policy). Runs on the helper thread, once at its start; best effort, never throws. */
void engineHelperThreadPolicy (const char* name) noexcept
{
    try
    {
        synthyboy_mpc::useNormalScheduling (0, name);
#if defined(__linux__)
        const synthyboy_mpc::overlay::CpuState st = synthyboy_mpc::overlay::planOwnCpus();
        if (st.plan.changed)
            synthyboy_mpc::overlay::setAffinity (0, st.plan.allowed);
#endif
    }
    catch (...)
    {
    }
}
} // namespace

extern "C"
{
    MPC_PLUGIN_EXPORT AEffect* VSTPluginMain(audioMasterCallback audioMaster)
    {
        synthyboy_mpc::startupTimes().begin();   // the start-up record (startup_timing.h): time zero is the first entry here
        (void) synthyboy_mpc::outputPeak(); (void) synthyboy_mpc::noteDiagnostics();   // belt and braces: touched before any instance exists (they are constant-initialised globals, no guard on the MIDI or audio thread)
        synthyboy::setHelperThreadStartHook (&engineHelperThreadPolicy);   // before any engine exists: its helper threads get the policy above
        try { MPCKIT_LOG_START ("synthyboy", mpckit::Level::info); } catch (...) {}   // dev builds only: the developer log (kit/log.h), info by default, configurable via /tmp/synthyboy-log.conf
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
        effect->numPrograms = static_cast<VstInt32>(inst->view.numPrograms());
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
        effect->numParams += 1 + synthyboy_mpc::play::kParams + synthyboy_mpc::QRouter::kCount + nativeui::kCount;
#endif
        effect->numInputs = 0;
        effect->numOutputs = 2;
        effect->flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
        effect->resvd1 = 0;
        effect->resvd2 = 0;
        effect->initialDelay = inst->outputStage.limiter() ? static_cast<VstInt32> (synthyboy::OutputStage::lookaheadFor (44100.0)) : 0;   // set again by effSetSampleRate and by every change (latencyDirty)
        inst->latencyReported.store (effect->initialDelay, std::memory_order_relaxed);
        inst->latencyDirty.store (false, std::memory_order_relaxed);
        effect->object = inst;
        // Build unique ID from four bytes: (('S'<<24)|('f'<<16)|('S'<<8)|'B')
        effect->uniqueID = (static_cast<VstInt32>('S') << 24) | (static_cast<VstInt32>('f') << 16)
                         | (static_cast<VstInt32>('S') << 8) | static_cast<VstInt32>('B');
        effect->version = SYNTHYBOY_VST2_VERSION; // 0.1.0 build 1 -> 101

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
