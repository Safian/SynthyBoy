// SynthyBoy - MPC overlay tests: fake display backend, fake framebuffer, fake plugin instance, a tiny check helper.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "../controller.h"
#include "../curve_source.h"
#include "../drm_interfaces.h"
#include "../overlay_layout.h"
#include "../renderer.h"

namespace ovt {

using namespace synthyboy_mpc::overlay;

inline int g_fails = 0;
inline void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_fails;
}

/** A directory for the switch / status files of one test, removed again by the destructor. */
struct TempDir
{
    std::string path;
    TempDir()
    {
        char tmpl[] = "/tmp/sb-overlay-test-XXXXXX";
        path = mkdtemp (tmpl);
    }
    ~TempDir()
    {
        const std::string cmd = "rm -rf '" + path + "'";
        if (std::system (cmd.c_str()) != 0) {}
    }
    Paths paths() const
    {
        Paths p;
        p.killSwitch = path + "/off";
        p.rotation = path + "/rot";
        p.noGpu = path + "/nogpu";
        p.look = path + "/look";
        p.detail = path + "/detail";
        p.rate = path + "/rate";
        p.gpuClock = path + "/gpuclock";
        p.status = path + "/status.txt";
        p.dumpRequest = path + "/dump";
        p.dump = path + "/curve.txt";
        return p;
    }
    void touch (const std::string& name) const { if (FILE* f = std::fopen ((path + "/" + name).c_str(), "w")) std::fclose (f); }
    void remove (const std::string& name) const { std::remove ((path + "/" + name).c_str()); }
    void write (const std::string& name, const std::string& text) const { if (FILE* f = std::fopen ((path + "/" + name).c_str(), "w")) { std::fputs (text.c_str(), f); std::fclose (f); } }
    std::string read (const std::string& name) const
    {
        std::string s;
        if (FILE* f = std::fopen ((path + "/" + name).c_str(), "r"))
        {
            char buf[512];
            size_t n;
            while ((n = std::fread (buf, 1, sizeof buf, f)) > 0)
                s.append (buf, n);
            std::fclose (f);
        }
        return s;
    }
};

class FakePlane final : public IPlaneBackend
{
public:
    // failure injection: the n-th call of a kind fails (1 = the first); 0 = never
    int failOpenAt = 0, failCreateAt = 0, failSetPlaneAt = 0;
    int panelW = 800, panelH = 1280;
    // what the discovery of the real backend reports: the CRTC, the plane in use and the spares, the pace; planeStillOurs / nextPlane act on them
    uint32_t crtc = 37, plane = 38;
    std::vector<uint32_t> spares;
    PaceParams pace;
    bool stillOurs = true;
    int nextPlaneCalls = 0;
    uint32_t crtcId() const noexcept override { return crtc; }
    uint32_t planeId() const noexcept override { return plane; }
    PaceParams paceParams() const noexcept override { return pace; }
    bool switchedOff = false;
    std::atomic<bool> displayIsOn { true };   // a test switches the display off (the CRTC has no mode: the screen sleeps) and on again
    bool displayOn() noexcept override { return displayIsOn.load(); }
    bool planeWasSwitchedOff() noexcept override { const bool v = switchedOff; switchedOff = false; return v; }
    bool planeStillOurs (std::string& why) override { log ("verify"); if (! stillOurs) why = "plane " + std::to_string (plane) + " reports crtc 0, fb 0"; return stillOurs; }
    bool nextPlane (std::string& why) override
    {
        log ("nextplane");
        ++nextPlaneCalls;
        if (spares.empty()) { why = "no spare overlay plane left"; return false; }
        plane = spares.front();
        spares.erase (spares.begin());
        stillOurs = true;
        why = "switched to plane " + std::to_string (plane);
        return true;
    }
    void describe (std::string& out) const override { out += "display_crtc: " + std::to_string (crtc) + "\ndisplay_overlay_plane: " + std::to_string (plane) + "\n"; }

    bool open (int& w, int& h, std::string& error) override
    {
        log ("open");
        if (++opens_ == failOpenAt) { error = "injected open failure"; return false; }
        w = panelW;
        h = panelH;
        return true;
    }
    bool createBuffers (int w, int h, std::string& error) override
    {
        log ("create");
        if (++creates_ == failCreateAt) { error = "injected create failure"; return false; }
        bufW_ = w;
        bufH_ = h;
        for (auto& b : buf_)
            b.assign (static_cast<size_t> (w) * static_cast<size_t> (h), 0u);
        buffersAlive = true;
        return true;
    }
    uint32_t* buffer (int index, size_t& strideBytes) noexcept override
    {
        if (! buffersAlive || index < 0 || index > 1)
            return nullptr;
        strideBytes = static_cast<size_t> (bufW_) * 4;
        return buf_[index].data();
    }
    bool setPlane (int index, const Rect& panel, std::string& error) override
    {
        log ("setplane");
        if (++sets_ == failSetPlaneAt) { error = "injected setplane failure"; return false; }
        if (! buffersAlive) { error = "no buffers"; return false; }
        planeOn = true;
        lastRect = panel;
        lastIndex = index;
        shownCopy = buf_[index];
        ++presented;
        return true;
    }
    void disablePlane() noexcept override { log ("disable"); planeOn = false; }
    void destroyBuffers() noexcept override { log ("destroy"); buffersAlive = false; }
    // for the GPU helper: the descriptor, the forced plane off, the objects another process made on the shared descriptor (the fake helper of gpu_backend_host.cpp adds them)
    int displayFd() const noexcept override { return 42; }
    void forceDisablePlane() noexcept override { log ("forcedisable"); planeOn = false; }
    void removeForeign (uint32_t fb, uint32_t handle) noexcept override
    {
        std::lock_guard<std::mutex> g (mu_);
        log_.push_back ("removeforeign");
        foreignFbs.erase (fb);
        foreignHandles.erase (handle);
    }
    std::set<uint32_t> foreignFbs, foreignHandles;

    bool planeOn = false, buffersAlive = false;
    Rect lastRect;
    int lastIndex = -1, presented = 0;
    std::vector<uint32_t> shownCopy;
    int bufW() const { return bufW_; }
    int bufH() const { return bufH_; }

    size_t calls() const { std::lock_guard<std::mutex> g (mu_); return log_.size(); }
    std::vector<std::string> callLog() const { std::lock_guard<std::mutex> g (mu_); return log_; }
    int countOf (const std::string& name) const
    {
        std::lock_guard<std::mutex> g (mu_);
        int n = 0;
        for (const auto& s : log_) n += s == name;
        return n;
    }

private:
    void log (const char* s) { std::lock_guard<std::mutex> g (mu_); log_.push_back (s); }
    mutable std::mutex mu_;
    std::vector<std::string> log_;
    std::vector<uint32_t> buf_[2];
    int bufW_ = 0, bufH_ = 0, opens_ = 0, creates_ = 0, sets_ = 0;
};

/** The primary framebuffer: a sparse map of panel pixels (everything else reads 0x202020). */
class FakeFrame final : public IFrameSource
{
public:
    int panelW = 800, panelH = 1280;
    bool readable = true;
    std::string fatal;
    std::string fatalError() const override { return fatal; }

    std::function<void()> onBegin;   // a test hook: called at the start of every read of the framebuffer (a real one can wait for the display's locks)
    bool begin (int& w, int& h) override
    {
        if (onBegin)
            onBegin();
        mu_.lock();
        ++begins;
        if (! readable) { mu_.unlock(); return false; }
        w = panelW;
        h = panelH;
        return true;
    }
    bool pixel (int px, int py, uint32_t& rgb) noexcept override
    {
        if (px < 0 || py < 0 || px >= panelW || py >= panelH)
            return false;
        auto it = pix_.find (py * panelW + px);
        rgb = it == pix_.end() ? 0x202020u : it->second;
        return true;
    }
    void end() noexcept override { mu_.unlock(); }
    int releases = 0;
    void release() noexcept override { ++releases; }

    /** Draws marker m (0 left, 1 right) of view `view` (landscape UI rectangle, its colour or `colourOverride`) into the panel through the shared mapping, with rotation `rot`. */
    void setViewMarker (int view, int m, bool present, int rot, uint32_t colourOverride = 0)
    {
        std::lock_guard<std::mutex> g (mu_);
        const layout::Marker& mk = layout::kViews[view].markers[m];
        for (int y = 0; y < mk.rect.h; ++y)
            for (int x = 0; x < mk.rect.w; ++x)
            {
                int px, py;
                uiPointToPanel (rot, mk.rect.x + x, mk.rect.y + y, panelW, panelH, px, py);
                if (present)
                    pix_[py * panelW + px] = colourOverride ? colourOverride : mk.rgb;
                else
                    pix_.erase (py * panelW + px);
            }
    }
    /** The instance picture of `view`: one solid colour (a film strip frame of the skin), the whole rectangle, through the shared mapping. */
    void setViewInstanceRgb (int view, uint32_t rgb, bool present, int rot)
    {
        std::lock_guard<std::mutex> g (mu_);
        const layout::Rect& r = layout::kViews[view].instance;
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x)
            {
                int px, py;
                uiPointToPanel (rot, r.x + x, r.y + y, panelW, panelH, px, py);
                if (present)
                    pix_[py * panelW + px] = rgb;
                else
                    pix_.erase (py * panelW + px);
            }
    }
    /** ... in the colour of `slot` (0 .. 15), kSlotNone (an instance without a slot) or kSlotUnknown (the blank frame). */
    void setViewInstance (int view, int slot, bool present, int rot)
    {
        setViewInstanceRgb (view, instanceFrameRgb (slot == kSlotUnknown ? 0 : (slot == kSlotNone ? layout::kMaxInstances + 1 : slot + 1)), present, rot);
    }
    /** A view on screen: both markers and the instance picture (of `defaultSlot`: the first instance of a test has slot 0). */
    void setView (int view, bool present, int rot) { setViewMarker (view, 0, present, rot); setViewMarker (view, 1, present, rot); setViewInstance (view, defaultSlot, present, rot); }
    int defaultSlot = 0;
    /** The first view (filter 1): the helpers most tests use. */
    void setMarker (int m, bool present, int rot, uint32_t colourOverride = 0) { setViewMarker (0, m, present, rot, colourOverride); }
    void setAll (bool present, int rot) { setView (0, present, rot); }
    /** A pixel of the panel (a framebuffer test sets the colour of the envelope line with it). */
    void setPixel (int px, int py, uint32_t rgb) { std::lock_guard<std::mutex> g (mu_); pix_[py * panelW + px] = rgb; }
    int begins = 0;

private:
    std::mutex mu_;
    std::map<int, uint32_t> pix_;
};

class FakeInstance final : public IInstanceView
{
public:
    FakeInstance()
    {
        values_ = synthyboy::ParamValues::defaults();
        const auto* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            atom_[i].store (t[i].def);
        set ("filter1.on", 1.0f);
        for (auto& x : lfoPhase) x.store (-1.0f);
        for (auto& x : envFine) x.store (0.0f);
        for (auto& x : envFrac) x.store (0.0f);
        for (auto& x : extra) x.store (0.0f);
    }
    int index (const char* key) const
    {
        const auto* p = synthyboy::findParamByKey (key);
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    void set (const char* key, float v) { atom_[index (key)].store (v); }
    float get (const char* key) const { return atom_[index (key)].load(); }
    float param (int i) const noexcept override { return atom_[i].load(); }
    bool live (synthyboy::LiveValues& out) const noexcept override
    {
        if (! hasLive.load())
            return false;
        out = synthyboy::LiveValues();
        out.block = 1;
        out.activeVoices = voices.load();
        out.numTargets = numTargets;
        out.target[0] = liveCutoff.load();
        out.target[1] = liveRes.load();
        for (int i = 2; i < numTargets && i < synthyboy::LiveValues::kMaxTargets; ++i)
            out.target[i] = extra[i].load();
        for (int i = 0; i < synthyboy::LiveValues::kNumLfos; ++i)
            out.lfoPhase[i] = lfoPhase[i].load();
        for (int i = 0; i < synthyboy::LiveValues::kNumEnvs; ++i)
        {
            out.envFineStage[i] = envFine[i].load();
            out.envFineFraction[i] = envFrac[i].load();
        }
        return true;
    }
    /** The live cutoff / resonance as the array a view gets (null while hasLive is false). */
    const float* liveTargets() noexcept { if (! hasLive.load()) return nullptr; lt_[0] = liveCutoff.load(); lt_[1] = liveRes.load(); return lt_; }
    double bpm() const noexcept override { return bpmValue.load(); }
    int playSelection (int group) const noexcept override { return playSel[group].load(); }
    std::atomic<int> playSel[3] { { 1 }, { 1 }, { 1 } };   // the instances the Play page shows (oscillator, envelope, LFO)
    void setQGroup (const int* targets, int n) noexcept override
    {
        ++qGroupCalls;
        qGroupN = n;
        for (int i = 0; i < 4; ++i)
            qGroup[i] = i < n && targets != nullptr ? targets[i] : -1;
    }
    int qGroup[4] = { -1, -1, -1, -1 }, qGroupN = 0, qGroupCalls = 0;   // the last Q group the UI set (the test thread only)
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        atom_[i].store (synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm)));
        ++uiWrites;
    }
    void setPlaySelection (int g, int i) noexcept override { if (g >= 0 && g < 3) playSel[g].store (i); }
    void setUiFullOpen (bool o) noexcept override { uiFullOpen.store (o); }
    std::atomic<bool> uiFullOpen { false };
    std::atomic<int> uiWrites { 0 };
    void requestOverlayReport() noexcept override { reportRequests.fetch_add (1); }
    /** The ring of the output (the spectrum view); only when `hasRing` is set, like a plugin built without the spectrum has none. */
    const synthyboy_mpc::SpectrumRing* spectrumRing() const noexcept override { return hasRing.load() ? &ring : nullptr; }
    synthyboy_mpc::SpectrumRing ring;
    std::atomic<bool> hasRing { false };
    std::atomic<int> reportRequests { 0 };
    std::shared_ptr<const synthyboy::StateContent> content() const override { std::lock_guard<std::mutex> g (cmu); return contentPtr; }
    void setContent (std::shared_ptr<const synthyboy::StateContent> c) { std::lock_guard<std::mutex> g (cmu); contentPtr = std::move (c); }
    float lt_[2] = {};
    std::atomic<int> voices { 0 };
    std::atomic<double> bpmValue { 120.0 };
    std::atomic<float> lfoPhase[synthyboy::LiveValues::kNumLfos], envFine[synthyboy::LiveValues::kNumEnvs], envFrac[synthyboy::LiveValues::kNumEnvs], extra[synthyboy::LiveValues::kMaxTargets];
    int numTargets = 2;   // how many readout targets the fake reports (the controller's list: filter views 2 each, osc views 4 each, in the order of the layout)
    mutable std::mutex cmu;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    double sampleRate() const noexcept override { return 48000.0; }
    std::atomic<bool> hasLive { false };
    std::atomic<float> liveCutoff { 1000.0f }, liveRes { 20.0f };

private:
    std::atomic<float> atom_[synthyboy::kMaxParams];
    synthyboy::ParamValues values_;
};

} // namespace ovt
