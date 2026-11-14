// SynthyBoy - MPC overlay tests: fake display backend, fake framebuffer, fake plugin instance, a tiny check helper.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
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

    bool begin (int& w, int& h) override
    {
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

    /** Draws marker i (landscape UI rectangle, colour) into the panel through the shared mapping, with rotation `rot`. */
    void setMarker (int i, bool present, int rot, uint32_t colourOverride = 0)
    {
        std::lock_guard<std::mutex> g (mu_);
        const layout::Marker& m = layout::kMarkers[i];
        for (int y = 0; y < m.rect.h; ++y)
            for (int x = 0; x < m.rect.w; ++x)
            {
                int px, py;
                uiPointToPanel (rot, m.rect.x + x, m.rect.y + y, panelW, panelH, px, py);
                if (present)
                    pix_[py * panelW + px] = colourOverride ? colourOverride : m.rgb;
                else
                    pix_.erase (py * panelW + px);
            }
    }
    void setAll (bool present, int rot) { for (int i = 0; i < layout::kNumMarkers; ++i) setMarker (i, present, rot); }
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
    }
    int index (const char* key) const
    {
        const auto* p = synthyboy::findParamByKey (key);
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    void set (const char* key, float v) { atom_[index (key)].store (v); }
    float get (const char* key) const { return atom_[index (key)].load(); }
    float param (int i) const noexcept override { return atom_[i].load(); }
    bool live (float& c, float& r) const noexcept override
    {
        if (! hasLive.load())
            return false;
        c = liveCutoff.load();
        r = liveRes.load();
        return true;
    }
    double sampleRate() const noexcept override { return 48000.0; }
    std::atomic<bool> hasLive { false };
    std::atomic<float> liveCutoff { 1000.0f }, liveRes { 20.0f };

private:
    std::atomic<float> atom_[synthyboy::kMaxParams];
    synthyboy::ParamValues values_;
};

} // namespace ovt
