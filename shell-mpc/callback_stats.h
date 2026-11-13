// SynthyBoy - MPC shell: diagnostic statistics of the audio callback (compile switch SYNTHYBOY_MPC_CALLBACK_STATS, default OFF).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Question it answers: does something (the live LFO display, a host redraw) hold up the host's audio thread? The audio thread calls begin () and end ()
// around every processReplacing; they measure
//   - the time between two callback starts (the gap) against the nominal block time of the previous block (frames / sample rate): the maximum, a histogram
//     (< 1.5x, < 2x, < 4x, < 8x, >= 8x) and the ten largest gaps with the time they happened at;
//   - the time spent inside the callback (maximum, mean).
// On the audio thread this is arithmetic and relaxed atomic stores to storage that exists from the start (one writer, so no read-modify-write): no
// allocation, no lock, no system call except the monotonic clock (vDSO), no file access. A separate low priority thread writes a text file every few
// seconds and at shutdown (format: see format ()). The file is the only output.
//
// Since format 2 it also answers two questions about the host (the sections "midi:", "host_can_do:", "dispatcher_opcodes:", "audio_master_calls:"): which MIDI
// messages reach effProcessEvents (counted by status type and channel, CC numbers, value ranges, the last 64 messages) and what the host asks of the plugin
// (every effCanDo text, every dispatcher opcode, the first values of effVendorSpecific and of opcodes the ABI header does not know, and our own audioMaster calls).
// All of it is fixed size atomic storage; the audio thread only does fetch_add / store. The writer thread can zero the MIDI part on request: when the file
// /tmp/synthyboy-stats-reset exists (environment variable SYNTHYBOY_MPC_CALLBACK_STATS_RESET_FILE changes the path) the writer zeros the MIDI counters and the
// ring before its next write and deletes the file; the audio thread does not take part (a message that arrives in the very moment of the zeroing may be lost
// or counted once more).
#pragma once

#include <algorithm>
#include <atomic>
#include <climits>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "mpc_vst2_abi.h"
#include "thread_policy.h"

namespace synthyboy_mpc
{
class CallbackStats
{
public:
    static constexpr int kTop = 10;
    static constexpr int kBuckets = 5;

    CallbackStats() : origin (nowNs()) { midiStart.store (origin, std::memory_order_relaxed); }
    ~CallbackStats() { stop(); }
    CallbackStats (const CallbackStats&) = delete;
    CallbackStats& operator= (const CallbackStats&) = delete;

    static uint64_t nowNs() noexcept
    {
        return static_cast<uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // ---- audio thread --------------------------------------------------------------------------------------------------------------------

    /** Start of a callback of `frames` samples; returns the start time for end (). */
    uint64_t begin (int frames, double sampleRate) noexcept
    {
        const uint64_t t0 = nowNs();
        const uint64_t nominal = frames > 0 && sampleRate > 0.0 ? static_cast<uint64_t> (static_cast<double> (frames) * 1.0e9 / sampleRate) : 0;
        if (restart.load (std::memory_order_relaxed) && restart.exchange (false, std::memory_order_relaxed))
            havePrev = false;
        if (havePrev && prevNominal > 0 && t0 >= prevStart)
        {
            const uint64_t gap = t0 - prevStart;
            const int b = gap * 2 < prevNominal * 3 ? 0 : gap < prevNominal * 2 ? 1 : gap < prevNominal * 4 ? 2 : gap < prevNominal * 8 ? 3 : 4;
            bump (bucket[b]);
            bump (gaps);
            if (gap > gapMax.load (std::memory_order_relaxed))
                gapMax.store (gap, std::memory_order_relaxed);
            recordTop (gap, t0, prevNominal, prevFrames);
        }
        prevStart = t0;
        prevNominal = nominal;
        prevFrames = frames;
        havePrev = true;
        bump (callbacks);
        lastFrames.store (frames, std::memory_order_relaxed);
        lastNominalNs.store (nominal, std::memory_order_relaxed);
        return t0;
    }

    /** End of the callback that began at t0. */
    void end (uint64_t t0) noexcept
    {
        const uint64_t t1 = nowNs();
        const uint64_t d = t1 >= t0 ? t1 - t0 : 0;
        if (d > procMax.load (std::memory_order_relaxed))
            procMax.store (d, std::memory_order_relaxed);
        procSum.store (procSum.load (std::memory_order_relaxed) + d, std::memory_order_relaxed);
        bump (procCount);
    }

    // ---- any other thread ----------------------------------------------------------------------------------------------------------------

    /** The gap after a pause (mains off / on) is not a gap of the audio: forget the previous callback. */
    void restartGaps() noexcept { restart.store (true, std::memory_order_relaxed); }

    // ---- the host side: MIDI, questions, opcodes (any thread that the host calls from; the audio thread for MIDI) ----------------------------

    static constexpr int kMidiTypes = 7;      // 0x80 note off .. 0xE0 pitch bend
    static constexpr int kCcCount = 128;
    static constexpr int kRing = 64;
    static constexpr int kCanDoMax = 32;
    static constexpr int kCanDoText = 64;     // with the terminating zero
    static constexpr int kOpcodes = 256;
    static constexpr int kCaptures = 8;

    /** One MIDI message of a kVstMidiType event of effProcessEvents. */
    void midiMessage (uint8_t status, uint8_t d1, uint8_t d2, int32_t deltaFrames) noexcept
    {
        inc (midiEvents);
        if (status >= 0x80 && status < 0xF0)
        {
            const int type = (status >> 4) - 8, ch = status & 15;
            inc (typeCount[type]);
            inc (chanCount[type][ch]);
            if (type == 1 && d2 == 0) inc (noteOnVel0);
            if (type == 3) { inc (ccCount[d1 & 127]); if ((d1 & 127) == 74) widen (cc74, d2); }
            else if (type == 2) widen (polyAt, d2);
            else if (type == 5) widen (chanPressure, d1);
            else if (type == 6) widen (pitchBend, (d1 & 127) | ((d2 & 127) << 7));
        }
        else
            inc (systemOrOther);
        const uint64_t t = nowNs();
        const uint64_t k = ringCount.fetch_add (1, std::memory_order_relaxed);
        RingSlot& r = ring[k % kRing];
        const uint32_t q = r.seq.load (std::memory_order_relaxed);
        r.seq.store (q + 1, std::memory_order_relaxed); // odd: being written
        std::atomic_thread_fence (std::memory_order_release);
        r.index.store (k, std::memory_order_relaxed);
        r.atMs.store (static_cast<uint32_t> (ms (t - origin)), std::memory_order_relaxed);
        r.delta.store (deltaFrames, std::memory_order_relaxed);
        r.message.store (static_cast<uint32_t> (status) << 16 | static_cast<uint32_t> (d1) << 8 | d2, std::memory_order_relaxed);
        r.seq.store (q + 2, std::memory_order_release);
    }

    /** An event of effProcessEvents that is not a kVstMidiType one (vstType as in the ABI header: 6 is sysex). */
    void otherEvent (int vstType) noexcept { inc (vstType == kVstSysExType ? sysexEvents : otherTypeEvents); }

    /** Every call of the dispatcher, before it is handled. `ptrSet`: ptr is not null. */
    void dispatcherCall (int opcode, int index, intptr_t value, float opt, bool ptrSet) noexcept
    {
        dispatcherTotal.fetch_add (1, std::memory_order_relaxed);
        if (opcode < 0 || opcode >= kOpcodes)
            dispatcherOutOfRange.fetch_add (1, std::memory_order_relaxed);
        else
            dispatcherCount[opcode].fetch_add (1, std::memory_order_relaxed);
        if (opcode == effVendorSpecific)
            capture (vendorCaps, vendorSeen, opcode, index, value, opt, ptrSet);
        else if (effectOpcodeName (opcode) == nullptr)
            capture (unknownCaps, unknownSeen, opcode, index, value, opt, ptrSet);
    }

    /** A question of effCanDo and the answer the plugin gave. */
    void canDo (const char* text, int answer) noexcept
    {
        canDoQuestions.fetch_add (1, std::memory_order_relaxed);
        char buf[kCanDoText] = {};
        for (int i = 0; i < kCanDoText - 1 && text[i] != 0; ++i) buf[i] = text[i];
        const uint32_t used = std::min<uint32_t> (canDoUsed.load (std::memory_order_acquire), kCanDoMax);
        for (uint32_t i = 0; i < used; ++i)
        {
            CanDoEntry& e = canDoList[i];
            if (e.state.load (std::memory_order_acquire) == 2 && std::strcmp (e.text, buf) == 0)
            {
                e.asked.fetch_add (1, std::memory_order_relaxed);
                e.answer.store (answer, std::memory_order_relaxed);
                return;
            }
        }
        const uint32_t i = canDoUsed.fetch_add (1, std::memory_order_acq_rel);
        if (i >= kCanDoMax) { canDoOverflow.fetch_add (1, std::memory_order_relaxed); return; }
        CanDoEntry& e = canDoList[i];
        std::memcpy (e.text, buf, sizeof (buf));
        e.asked.store (1, std::memory_order_relaxed);
        e.answer.store (answer, std::memory_order_relaxed);
        e.state.store (2, std::memory_order_release);
    }

    /** Every call of ours to the host (audioMaster), from whatever thread. */
    void hostCall (int opcode) noexcept
    {
        hostTotal.fetch_add (1, std::memory_order_relaxed);
        if (opcode < 0 || opcode >= kOpcodes)
            hostOutOfRange.fetch_add (1, std::memory_order_relaxed);
        else
            hostCount[opcode].fetch_add (1, std::memory_order_relaxed);
    }

    /** The file whose existence asks the writer to zero the MIDI statistics (call before start ()). */
    void setResetFile (std::string path) { resetPath = std::move (path); }

    /** Writer thread (or a test): zeros the MIDI counters and the ring when the reset file exists, and deletes it. Returns whether it did. */
    bool applyResetRequest() noexcept
    {
        if (resetPath.empty())
            return false;
        std::FILE* f = std::fopen (resetPath.c_str(), "r");
        if (! f)
            return false;
        std::fclose (f);
        std::remove (resetPath.c_str());
        resetMidi();
        return true;
    }

    /** The name the ABI header gives a host -> plugin opcode, nullptr when it does not know it. */
    static const char* effectOpcodeName (int op) noexcept
    {
        switch (op)
        {
            case effOpen: return "effOpen";
            case effClose: return "effClose";
            case effSetProgram: return "effSetProgram";
            case effGetProgram: return "effGetProgram";
            case effSetProgramName: return "effSetProgramName";
            case effGetProgramName: return "effGetProgramName";
            case effGetParamLabel: return "effGetParamLabel";
            case effGetParamDisplay: return "effGetParamDisplay";
            case effGetParamName: return "effGetParamName";
            case effGetVu_Deprecated: return "effGetVu_Deprecated";
            case effSetSampleRate: return "effSetSampleRate";
            case effSetBlockSize: return "effSetBlockSize";
            case effMainsChanged: return "effMainsChanged";
            case effEditGetRect: return "effEditGetRect";
            case effEditOpen: return "effEditOpen";
            case effEditClose: return "effEditClose";
            case effEditDraw_Deprecated: return "effEditDraw_Deprecated";
            case effEditMouse_Deprecated: return "effEditMouse_Deprecated";
            case effEditKey_Deprecated: return "effEditKey_Deprecated";
            case effEditIdle: return "effEditIdle";
            case effEditTop_Deprecated: return "effEditTop_Deprecated";
            case effEditSleep_Deprecated: return "effEditSleep_Deprecated";
            case effIdentify_Deprecated: return "effIdentify_Deprecated";
            case effGetChunk: return "effGetChunk";
            case effSetChunk: return "effSetChunk";
            case effProcessEvents: return "effProcessEvents";
            case effCanBeAutomated: return "effCanBeAutomated";
            case effString2Parameter: return "effString2Parameter";
            case effGetNumProgramCategories_Deprecated: return "effGetNumProgramCategories_Deprecated";
            case effGetProgramNameIndexed: return "effGetProgramNameIndexed";
            case effCopyProgram_Deprecated: return "effCopyProgram_Deprecated";
            case effConnectInput_Deprecated: return "effConnectInput_Deprecated";
            case effConnectOutput_Deprecated: return "effConnectOutput_Deprecated";
            case effGetInputProperties: return "effGetInputProperties";
            case effGetOutputProperties: return "effGetOutputProperties";
            case effGetPlugCategory: return "effGetPlugCategory";
            case effGetCurrentPosition_Deprecated: return "effGetCurrentPosition_Deprecated";
            case effGetDestinationBuffer_Deprecated: return "effGetDestinationBuffer_Deprecated";
            case effOfflineNotify: return "effOfflineNotify";
            case effOfflinePrepare: return "effOfflinePrepare";
            case effOfflineRun: return "effOfflineRun";
            case effProcessVarIo: return "effProcessVarIo";
            case effSetSpeakerArrangement: return "effSetSpeakerArrangement";
            case effSetBlockSizeAndSampleRate_Deprecated: return "effSetBlockSizeAndSampleRate_Deprecated";
            case effSetBypass: return "effSetBypass";
            case effGetEffectName: return "effGetEffectName";
            case effGetErrorText_Deprecated: return "effGetErrorText_Deprecated";
            case effGetVendorString: return "effGetVendorString";
            case effGetProductString: return "effGetProductString";
            case effGetVendorVersion: return "effGetVendorVersion";
            case effVendorSpecific: return "effVendorSpecific";
            case effCanDo: return "effCanDo";
            case effGetTailSize: return "effGetTailSize";
            case effIdle_Deprecated: return "effIdle_Deprecated";
            case effGetIcon_Deprecated: return "effGetIcon_Deprecated";
            case effSetViewPosition_Deprecated: return "effSetViewPosition_Deprecated";
            case effGetParameterProperties: return "effGetParameterProperties";
            case effKeysRequired_Deprecated: return "effKeysRequired_Deprecated";
            case effGetVstVersion: return "effGetVstVersion";
            case effEditKeyDown: return "effEditKeyDown";
            case effEditKeyUp: return "effEditKeyUp";
            case effSetEditKnobMode: return "effSetEditKnobMode";
            case effGetMidiProgramName: return "effGetMidiProgramName";
            case effGetCurrentMidiProgram: return "effGetCurrentMidiProgram";
            case effGetMidiProgramCategory: return "effGetMidiProgramCategory";
            case effHasMidiProgramsChanged: return "effHasMidiProgramsChanged";
            case effGetMidiKeyName: return "effGetMidiKeyName";
            case effBeginSetProgram: return "effBeginSetProgram";
            case effEndSetProgram: return "effEndSetProgram";
            case effGetSpeakerArrangement: return "effGetSpeakerArrangement";
            case effShellGetNextPlugin: return "effShellGetNextPlugin";
            case effStartProcess: return "effStartProcess";
            case effStopProcess: return "effStopProcess";
            case effSetTotalSampleToProcess: return "effSetTotalSampleToProcess";
            case effSetPanLaw: return "effSetPanLaw";
            case effBeginLoadBank: return "effBeginLoadBank";
            case effBeginLoadProgram: return "effBeginLoadProgram";
            case effSetProcessPrecision: return "effSetProcessPrecision";
            case effGetNumMidiInputChannels: return "effGetNumMidiInputChannels";
            case effGetNumMidiOutputChannels: return "effGetNumMidiOutputChannels";
        default: return nullptr;
        }
    }

    /** The name the ABI header gives a plugin -> host opcode, nullptr when it does not know it. */
    static const char* audioMasterOpcodeName (int op) noexcept
    {
        switch (op)
        {
            case audioMasterAutomate: return "audioMasterAutomate";
            case audioMasterVersion: return "audioMasterVersion";
            case audioMasterCurrentId: return "audioMasterCurrentId";
            case audioMasterIdle: return "audioMasterIdle";
            case audioMasterPinConnected_Deprecated: return "audioMasterPinConnected_Deprecated";
            case audioMasterWantMidi_Deprecated: return "audioMasterWantMidi_Deprecated";
            case audioMasterGetTime: return "audioMasterGetTime";
            case audioMasterProcessEvents: return "audioMasterProcessEvents";
            case audioMasterSetTime_Deprecated: return "audioMasterSetTime_Deprecated";
            case audioMasterTempoAt_Deprecated: return "audioMasterTempoAt_Deprecated";
            case audioMasterGetNumAutomatableParameters_Deprecated: return "audioMasterGetNumAutomatableParameters_Deprecated";
            case audioMasterGetParameterQuantization_Deprecated: return "audioMasterGetParameterQuantization_Deprecated";
            case audioMasterIOChanged: return "audioMasterIOChanged";
            case audioMasterNeedIdle_Deprecated: return "audioMasterNeedIdle_Deprecated";
            case audioMasterSizeWindow: return "audioMasterSizeWindow";
            case audioMasterGetSampleRate: return "audioMasterGetSampleRate";
            case audioMasterGetBlockSize: return "audioMasterGetBlockSize";
            case audioMasterGetInputLatency: return "audioMasterGetInputLatency";
            case audioMasterGetOutputLatency: return "audioMasterGetOutputLatency";
            case audioMasterGetPreviousPlug_Deprecated: return "audioMasterGetPreviousPlug_Deprecated";
            case audioMasterGetNextPlug_Deprecated: return "audioMasterGetNextPlug_Deprecated";
            case audioMasterWillReplaceOrAccumulate_Deprecated: return "audioMasterWillReplaceOrAccumulate_Deprecated";
            case audioMasterGetCurrentProcessLevel: return "audioMasterGetCurrentProcessLevel";
            case audioMasterGetAutomationState: return "audioMasterGetAutomationState";
            case audioMasterOfflineStart: return "audioMasterOfflineStart";
            case audioMasterOfflineRead: return "audioMasterOfflineRead";
            case audioMasterOfflineWrite: return "audioMasterOfflineWrite";
            case audioMasterOfflineGetCurrentPass: return "audioMasterOfflineGetCurrentPass";
            case audioMasterOfflineGetCurrentMetaPass: return "audioMasterOfflineGetCurrentMetaPass";
            case audioMasterSetOutputSampleRate_Deprecated: return "audioMasterSetOutputSampleRate_Deprecated";
            case audioMasterGetOutputSpeakerArrangement_Deprecated: return "audioMasterGetOutputSpeakerArrangement_Deprecated";
            case audioMasterGetVendorString: return "audioMasterGetVendorString";
            case audioMasterGetProductString: return "audioMasterGetProductString";
            case audioMasterGetVendorVersion: return "audioMasterGetVendorVersion";
            case audioMasterVendorSpecific: return "audioMasterVendorSpecific";
            case audioMasterSetIcon_Deprecated: return "audioMasterSetIcon_Deprecated";
            case audioMasterCanDo: return "audioMasterCanDo";
            case audioMasterGetLanguage: return "audioMasterGetLanguage";
            case audioMasterOpenWindow_Deprecated: return "audioMasterOpenWindow_Deprecated";
            case audioMasterCloseWindow_Deprecated: return "audioMasterCloseWindow_Deprecated";
            case audioMasterGetDirectory: return "audioMasterGetDirectory";
            case audioMasterUpdateDisplay: return "audioMasterUpdateDisplay";
            case audioMasterBeginEdit: return "audioMasterBeginEdit";
            case audioMasterEndEdit: return "audioMasterEndEdit";
            case audioMasterOpenFileSelector: return "audioMasterOpenFileSelector";
            case audioMasterCloseFileSelector: return "audioMasterCloseFileSelector";
            case audioMasterEditFile_Deprecated: return "audioMasterEditFile_Deprecated";
            case audioMasterGetChunkFile_Deprecated: return "audioMasterGetChunkFile_Deprecated";
            case audioMasterGetInputSpeakerArrangement_Deprecated: return "audioMasterGetInputSpeakerArrangement_Deprecated";
        default: return nullptr;
        }
    }

    struct Snapshot
    {
        using u64 = unsigned long long; // what the printf format wants on every platform (uint64_t is unsigned long on 64-bit Linux)
        u64 callbacks = 0, gaps = 0, bucket[kBuckets] = {}, gapMax = 0, procMax = 0, procSum = 0, procCount = 0, nominalNs = 0;
        int frames = 0;
        struct Top { u64 gap, at, nominal; int frames; } top[kTop] = {};
        int topCount = 0;
    };

    Snapshot snapshot() const noexcept
    {
        Snapshot s;
        s.callbacks = callbacks.load (std::memory_order_relaxed);
        s.gaps = gaps.load (std::memory_order_relaxed);
        for (int i = 0; i < kBuckets; ++i) s.bucket[i] = bucket[i].load (std::memory_order_relaxed);
        s.gapMax = gapMax.load (std::memory_order_relaxed);
        s.procMax = procMax.load (std::memory_order_relaxed);
        s.procSum = procSum.load (std::memory_order_relaxed);
        s.procCount = procCount.load (std::memory_order_relaxed);
        s.nominalNs = lastNominalNs.load (std::memory_order_relaxed);
        s.frames = lastFrames.load (std::memory_order_relaxed);
        for (int i = 0; i < kTop; ++i)
        {
            Snapshot::Top t {};
            for (int attempt = 0; attempt < 4; ++attempt) // slot is written by the audio thread: read it consistently (sequence counter)
            {
                const uint32_t a = slot[i].seq.load (std::memory_order_acquire);
                t.gap = slot[i].gap.load (std::memory_order_relaxed);
                t.at = slot[i].at.load (std::memory_order_relaxed);
                t.nominal = slot[i].nominal.load (std::memory_order_relaxed);
                t.frames = slot[i].frames.load (std::memory_order_relaxed);
                std::atomic_thread_fence (std::memory_order_acquire);
                if ((a & 1u) == 0 && a == slot[i].seq.load (std::memory_order_relaxed))
                    break;
                t.gap = 0;
            }
            if (t.gap > 0) s.top[s.topCount++] = t;
        }
        std::sort (s.top, s.top + s.topCount, [] (const Snapshot::Top& a, const Snapshot::Top& b) { return a.gap > b.gap; });
        return s;
    }

    /** The text of the statistics file. `build` is one line naming the build variant. Times are microseconds; "at_ms" is milliseconds since the plugin was created. */
    std::string format (const std::string& build, const char* reason) const
    {
        const Snapshot s = snapshot();
        const uint64_t now = nowNs();
        std::string o;
        o += "synthyboy-callback-stats 2\n";
        appendf (o, "build: %s\n", build.c_str());
        appendf (o, "written: %s at_ms=%llu\n", reason, ms (now - origin));
        appendf (o, "block_frames_last: %d\n", s.frames);
        appendf (o, "nominal_block_us: %llu\n", s.nominalNs / 1000);
        appendf (o, "callbacks: %llu\n", s.callbacks);
        appendf (o, "gaps_counted: %llu\n", s.gaps);
        appendf (o, "gap_max_us: %llu\n", s.gapMax / 1000);
        appendf (o, "gap_buckets (gap / nominal block time): <1.5x=%llu <2x=%llu <4x=%llu <8x=%llu >=8x=%llu\n", s.bucket[0], s.bucket[1], s.bucket[2], s.bucket[3], s.bucket[4]);
        appendf (o, "process_max_us: %llu\n", s.procMax / 1000);
        appendf (o, "process_avg_us: %llu.%03llu\n", s.procCount ? s.procSum / s.procCount / 1000 : 0ull, s.procCount ? (s.procSum / s.procCount) % 1000 : 0ull);
        appendf (o, "largest_gaps: %d\n", s.topCount);
        for (int i = 0; i < s.topCount; ++i)
        {
            const auto& t = s.top[i];
            appendf (o, "  %d at_ms=%llu gap_us=%llu ratio=%.2fx block_frames=%d\n", i + 1, ms (t.at >= origin ? t.at - origin : 0), t.gap / 1000,
                 t.nominal ? static_cast<double> (t.gap) / static_cast<double> (t.nominal) : 0.0, t.frames);
        }
        appendMidi (o);
        appendHostCanDo (o);
        appendDispatcher (o);
        appendHostCalls (o);
        return o;
    }

    /** Starts the writer thread (low priority). `path`: the file; `build`: the line for format (); `intervalMs`: pause between two writes. */
    void start (std::string path, std::string build, int intervalMs)
    {
        filePath = std::move (path);
        buildLine = std::move (build);
        writer = std::thread ([this, intervalMs]
        {
            useNormalScheduling (10, "sb-cb-stats");
            std::unique_lock<std::mutex> lock (stopMutex);
            while (! stopping)
            {
                stopCv.wait_for (lock, std::chrono::milliseconds (intervalMs), [this] { return stopping; });
                if (! stopping)
                {
                    applyResetRequest();
                    write ("periodic");
                }
            }
        });
    }

    void stop() noexcept
    {
        if (! writer.joinable())
            return;
        {
            std::lock_guard<std::mutex> lock (stopMutex);
            stopping = true;
        }
        stopCv.notify_all();
        writer.join();
        write ("shutdown");
    }

    /** Writes the file now (through a temporary file and rename, so a reader never sees half of it). Never call it on the audio thread. */
    bool write (const char* reason) const noexcept
    {
        try
        {
            const std::string tmp = filePath + ".tmp";
            const std::string text = format (buildLine, reason);
            std::FILE* f = std::fopen (tmp.c_str(), "w");
            if (! f)
                return false;
            const bool ok = std::fwrite (text.data(), 1, text.size(), f) == text.size();
            const bool closed = std::fclose (f) == 0;
            if (! ok || ! closed || std::rename (tmp.c_str(), filePath.c_str()) != 0)
            {
                std::remove (tmp.c_str());
                return false;
            }
            return true;
        }
        catch (...) { return false; }
    }

private:
    struct Slot
    {
        std::atomic<uint32_t> seq { 0 };
        std::atomic<uint64_t> gap { 0 }, at { 0 }, nominal { 0 };
        std::atomic<int> frames { 0 };
    };

    __attribute__ ((format (printf, 2, 0))) static void appendfv (std::string& o, const char* fmt, va_list args)
    {
        char b[512];
        std::vsnprintf (b, sizeof (b), fmt, args);
        o += b;
    }
    __attribute__ ((format (printf, 2, 3))) static void appendf (std::string& o, const char* fmt, ...)
    {
        va_list args;
        va_start (args, fmt);
        appendfv (o, fmt, args);
        va_end (args);
    }


    struct Range
    {
        std::atomic<int32_t> lo { INT_MAX }, hi { INT_MIN };
    };
    static void widen (Range& r, int v) noexcept // one writer (the thread of effProcessEvents)
    {
        if (v < r.lo.load (std::memory_order_relaxed)) r.lo.store (v, std::memory_order_relaxed);
        if (v > r.hi.load (std::memory_order_relaxed)) r.hi.store (v, std::memory_order_relaxed);
    }

    struct RingSlot
    {
        std::atomic<uint32_t> seq { 0 };
        std::atomic<uint64_t> index { ~uint64_t (0) };
        std::atomic<uint32_t> atMs { 0 }, message { 0 };
        std::atomic<int32_t> delta { 0 };
    };

    struct CanDoEntry
    {
        std::atomic<uint32_t> state { 0 }; // 2: text is complete
        char text[kCanDoText] = {};
        std::atomic<uint64_t> asked { 0 };
        std::atomic<int> answer { 0 };
    };

    struct Capture
    {
        std::atomic<uint32_t> ready { 0 };
        std::atomic<int32_t> opcode { 0 }, index { 0 };
        std::atomic<int64_t> value { 0 };
        std::atomic<uint32_t> optBits { 0 };
        std::atomic<int> ptrSet { 0 };
    };

    static void capture (Capture* caps, std::atomic<uint64_t>& seen, int opcode, int index, intptr_t value, float opt, bool ptrSet) noexcept
    {
        const uint64_t n = seen.load (std::memory_order_relaxed);
        seen.fetch_add (1, std::memory_order_release);
        if (n >= kCaptures) return;
        Capture& c = caps[n];
        uint32_t bits;
        std::memcpy (&bits, &opt, sizeof (bits));
        c.opcode.store (opcode, std::memory_order_relaxed);
        c.index.store (index, std::memory_order_relaxed);
        c.value.store (static_cast<int64_t> (value), std::memory_order_relaxed);
        c.optBits.store (bits, std::memory_order_relaxed);
        c.ptrSet.store (ptrSet ? 1 : 0, std::memory_order_relaxed);
        c.ready.store (1, std::memory_order_release);
    }

    // ---- format of the sections added in format 2 -------------------------------------------------------------------------------------------

    static constexpr const char* kTypeName[kMidiTypes] = { "note_off", "note_on", "poly_at", "cc", "program", "chan_pressure", "pitch_bend" };

    static void appendRange (std::string& o, const char* name, const Range& r)
    {
        const int32_t lo = r.lo.load (std::memory_order_relaxed), hi = r.hi.load (std::memory_order_relaxed);
        if (lo > hi) appendf (o, "  %s: none\n", name);
        else appendf (o, "  %s: min=%d max=%d\n", name, lo, hi);
    }

    void appendMidi (std::string& o) const
    {
        const uint64_t now = nowNs();
        o += "midi:\n";
        appendf (o, "  window_start_ms=%llu resets=%llu\n", ms (midiStart.load (std::memory_order_relaxed) - origin), static_cast<unsigned long long> (resets.load (std::memory_order_relaxed)));
        appendf (o, "  window_ms=%llu\n", ms (now - midiStart.load (std::memory_order_relaxed)));
        appendf (o, "  vst_events: midi=%llu sysex=%llu other_types=%llu\n", static_cast<unsigned long long> (midiEvents.load (std::memory_order_relaxed)),
             static_cast<unsigned long long> (sysexEvents.load (std::memory_order_relaxed)), static_cast<unsigned long long> (otherTypeEvents.load (std::memory_order_relaxed)));
        std::string line = "  status_totals:";
        for (int t = 0; t < kMidiTypes; ++t)
            appendf (line, " %s=%llu", kTypeName[t], static_cast<unsigned long long> (typeCount[t].load (std::memory_order_relaxed)));
        appendf (line, " system_or_other=%llu note_on_velocity0=%llu\n", static_cast<unsigned long long> (systemOrOther.load (std::memory_order_relaxed)),
             static_cast<unsigned long long> (noteOnVel0.load (std::memory_order_relaxed)));
        o += line;
        for (int c = 0; c < 16; ++c)
        {
            uint64_t sum = 0;
            for (int t = 0; t < kMidiTypes; ++t) sum += chanCount[t][c].load (std::memory_order_relaxed);
            if (sum == 0) continue;
            std::string l;
            appendf (l, "  channel %d:", c + 1);
            for (int t = 0; t < kMidiTypes; ++t)
                appendf (l, " %s=%llu", kTypeName[t], static_cast<unsigned long long> (chanCount[t][c].load (std::memory_order_relaxed)));
            o += l + "\n";
        }
        o += "  cc_numbers:\n";
        for (int n = 0; n < kCcCount; ++n)
            if (const uint64_t c = ccCount[n].load (std::memory_order_relaxed))
                appendf (o, "    cc=%d count=%llu\n", n, static_cast<unsigned long long> (c));
        appendRange (o, "pitch_bend (14 bit, centre 8192)", pitchBend);
        appendRange (o, "channel_pressure", chanPressure);
        appendRange (o, "poly_at (value)", polyAt);
        appendRange (o, "cc74", cc74);
        const uint64_t total = ringCount.load (std::memory_order_relaxed);
        const uint64_t first = total > kRing ? total - kRing : 0;
        appendf (o, "  last_events (of %llu, oldest first; at_ms since the plugin was created, delta in frames within the block):\n", static_cast<unsigned long long> (total));
        for (uint64_t k = first; k < total; ++k)
        {
            const RingSlot& r = ring[k % kRing];
            uint32_t at = 0, msg = 0;
            int32_t delta = 0;
            bool ok = false;
            for (int attempt = 0; attempt < 4 && ! ok; ++attempt)
            {
                const uint32_t a = r.seq.load (std::memory_order_acquire);
                const uint64_t idx = r.index.load (std::memory_order_relaxed);
                at = r.atMs.load (std::memory_order_relaxed);
                delta = r.delta.load (std::memory_order_relaxed);
                msg = r.message.load (std::memory_order_relaxed);
                std::atomic_thread_fence (std::memory_order_acquire);
                ok = (a & 1u) == 0 && a == r.seq.load (std::memory_order_relaxed) && idx == k;
            }
            if (! ok) continue;
            char ch[8] = "-"; // system messages have no channel
            if ((msg >> 16 & 0xFF) < 0xF0) std::snprintf (ch, sizeof (ch), "%u", (msg >> 16 & 15) + 1u);
            appendf (o, "    %llu at_ms=%u delta=%d status=0x%02X ch=%s d1=%u d2=%u\n", static_cast<unsigned long long> (k + 1), at, delta, msg >> 16 & 0xFF, ch, msg >> 8 & 0xFF, msg & 0xFF);
        }
    }

    static void appendText (std::string& o, const char* text)
    {
        for (const char* c = text; *c; ++c)
            o += (*c >= 0x20 && *c < 0x7F && *c != '"') ? *c : '?';
    }

    void appendHostCanDo (std::string& o) const
    {
        const uint32_t used = std::min<uint32_t> (canDoUsed.load (std::memory_order_acquire), kCanDoMax);
        o += "host_can_do:\n";
        appendf (o, "  questions=%llu distinct=%u not_stored=%llu (texts longer than %d bytes are cut)\n", static_cast<unsigned long long> (canDoQuestions.load (std::memory_order_relaxed)), used,
             static_cast<unsigned long long> (canDoOverflow.load (std::memory_order_relaxed)), kCanDoText - 1);
        for (uint32_t i = 0; i < used; ++i)
        {
            const CanDoEntry& e = canDoList[i];
            if (e.state.load (std::memory_order_acquire) != 2) continue;
            std::string l = "  \"";
            appendText (l, e.text);
            appendf (l, "\" asked=%llu answer=%d\n", static_cast<unsigned long long> (e.asked.load (std::memory_order_relaxed)), e.answer.load (std::memory_order_relaxed));
            o += l;
        }
    }

    static void appendCaptures (std::string& o, const char* title, const Capture* caps, uint64_t seen)
    {
        appendf (o, "  %s: seen=%llu shown=%d\n", title, static_cast<unsigned long long> (seen), static_cast<int> (std::min<uint64_t> (seen, kCaptures)));
        for (uint64_t i = 0; i < std::min<uint64_t> (seen, kCaptures); ++i)
        {
            const Capture& c = caps[i];
            if (c.ready.load (std::memory_order_acquire) == 0) { appendf (o, "    %llu (being written)\n", static_cast<unsigned long long> (i + 1)); continue; }
            const uint32_t bits = c.optBits.load (std::memory_order_relaxed);
            float opt;
            std::memcpy (&opt, &bits, sizeof (opt));
            const int32_t op = c.opcode.load (std::memory_order_relaxed);
            appendf (o, "    %llu opcode=%d index=%d value=%lld opt=%g ptr=%s\n", static_cast<unsigned long long> (i + 1), op, c.index.load (std::memory_order_relaxed),
                 static_cast<long long> (c.value.load (std::memory_order_relaxed)), static_cast<double> (opt), c.ptrSet.load (std::memory_order_relaxed) ? "set" : "null");
        }
    }

    void appendDispatcher (std::string& o) const
    {
        o += "dispatcher_opcodes:\n";
        appendf (o, "  total=%llu out_of_range=%llu\n", static_cast<unsigned long long> (dispatcherTotal.load (std::memory_order_relaxed)),
             static_cast<unsigned long long> (dispatcherOutOfRange.load (std::memory_order_relaxed)));
        for (int op = 0; op < kOpcodes; ++op)
        {
            const uint64_t c = dispatcherCount[op].load (std::memory_order_relaxed);
            if (c == 0) continue;
            const char* name = effectOpcodeName (op);
            appendf (o, "  op=%d name=%s count=%llu\n", op, name ? name : "unknown_to_the_ABI_header", static_cast<unsigned long long> (c));
        }
        appendCaptures (o, "vendor_specific_first_calls (effVendorSpecific)", vendorCaps, vendorSeen.load (std::memory_order_acquire));
        appendCaptures (o, "unknown_opcode_first_calls", unknownCaps, unknownSeen.load (std::memory_order_acquire));
    }

    void appendHostCalls (std::string& o) const
    {
        o += "audio_master_calls:\n";
        appendf (o, "  total=%llu out_of_range=%llu\n", static_cast<unsigned long long> (hostTotal.load (std::memory_order_relaxed)), static_cast<unsigned long long> (hostOutOfRange.load (std::memory_order_relaxed)));
        for (int op = 0; op < kOpcodes; ++op)
        {
            const uint64_t c = hostCount[op].load (std::memory_order_relaxed);
            if (c == 0) continue;
            const char* name = audioMasterOpcodeName (op);
            appendf (o, "  op=%d name=%s count=%llu\n", op, name ? name : "unknown_to_the_ABI_header", static_cast<unsigned long long> (c));
        }
    }

    /** The writer zeros the MIDI statistics (see applyResetRequest ()). The audio thread only adds with fetch_add / stores, so every access stays atomic. */
    void resetMidi() noexcept
    {
        midiEvents.store (0); sysexEvents.store (0); otherTypeEvents.store (0); systemOrOther.store (0); noteOnVel0.store (0);
        for (int t = 0; t < kMidiTypes; ++t) { typeCount[t].store (0); for (int c = 0; c < 16; ++c) chanCount[t][c].store (0); }
        for (int n = 0; n < kCcCount; ++n) ccCount[n].store (0);
        for (Range* r : { &pitchBend, &chanPressure, &polyAt, &cc74 }) { r->lo.store (INT_MAX); r->hi.store (INT_MIN); }
        ringCount.store (0);
        for (int i = 0; i < kRing; ++i) { ring[i].index.store (~uint64_t (0)); }
        midiStart.store (nowNs());
        resets.fetch_add (1);
    }

    static unsigned long long ms (uint64_t ns) noexcept { return static_cast<unsigned long long> (ns / 1000000u); }
    static void inc (std::atomic<uint64_t>& a) noexcept { a.fetch_add (1, std::memory_order_relaxed); } // the MIDI counters: the writer thread may zero them meanwhile
    static void bump (std::atomic<uint64_t>& a) noexcept { a.store (a.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed); } // one writer

    /** Keeps the kTop largest gaps: replaces the smallest one when this gap is larger. Audio thread only (it is the only writer of the slots). */
    void recordTop (uint64_t gap, uint64_t at, uint64_t nominal, int frames) noexcept
    {
        int smallest = 0;
        uint64_t smallestGap = slot[0].gap.load (std::memory_order_relaxed);
        for (int i = 1; i < kTop; ++i)
        {
            const uint64_t g = slot[i].gap.load (std::memory_order_relaxed);
            if (g < smallestGap) { smallestGap = g; smallest = i; }
        }
        if (gap <= smallestGap)
            return;
        Slot& s = slot[smallest];
        const uint32_t q = s.seq.load (std::memory_order_relaxed);
        s.seq.store (q + 1, std::memory_order_relaxed); // odd: being written
        std::atomic_thread_fence (std::memory_order_release);
        s.gap.store (gap, std::memory_order_relaxed);
        s.at.store (at, std::memory_order_relaxed);
        s.nominal.store (nominal, std::memory_order_relaxed);
        s.frames.store (frames, std::memory_order_relaxed);
        s.seq.store (q + 2, std::memory_order_release);
    }

    const uint64_t origin;
    // audio thread only
    bool havePrev = false;
    uint64_t prevStart = 0, prevNominal = 0;
    int prevFrames = 0;
    // written by the audio thread, read by the writer
    std::atomic<uint64_t> callbacks { 0 }, gaps { 0 }, bucket[kBuckets] {}, gapMax { 0 }, procMax { 0 }, procSum { 0 }, procCount { 0 }, lastNominalNs { 0 };
    std::atomic<int> lastFrames { 0 };
    Slot slot[kTop];
    std::atomic<bool> restart { false };
    // the host side (see the API above)
    std::atomic<uint64_t> midiEvents { 0 }, sysexEvents { 0 }, otherTypeEvents { 0 }, systemOrOther { 0 }, noteOnVel0 { 0 };
    std::atomic<uint64_t> typeCount[kMidiTypes] {}, chanCount[kMidiTypes][16] {}, ccCount[kCcCount] {};
    Range pitchBend, chanPressure, polyAt, cc74;
    std::atomic<uint64_t> ringCount { 0 };
    RingSlot ring[kRing];
    std::atomic<uint64_t> midiStart { 0 }, resets { 0 };
    std::atomic<uint64_t> dispatcherTotal { 0 }, dispatcherOutOfRange { 0 }, dispatcherCount[kOpcodes] {};
    std::atomic<uint64_t> hostTotal { 0 }, hostOutOfRange { 0 }, hostCount[kOpcodes] {};
    std::atomic<uint64_t> vendorSeen { 0 }, unknownSeen { 0 };
    Capture vendorCaps[kCaptures], unknownCaps[kCaptures];
    std::atomic<uint64_t> canDoQuestions { 0 }, canDoOverflow { 0 };
    std::atomic<uint32_t> canDoUsed { 0 };
    CanDoEntry canDoList[kCanDoMax];
    std::string resetPath; // writer thread (set before start ())
    // writer thread
    std::string filePath, buildLine;
    std::thread writer;
    std::mutex stopMutex;
    std::condition_variable stopCv;
    bool stopping = false;
};
} // namespace synthyboy_mpc
