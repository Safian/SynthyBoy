// SynthyBoy - MPC in-host drawing (experimental): the presentation path as an IPlaneBackend, so the overlay's controller and presenter run unchanged. Wraps the plane backend (LinuxPlaneBackend) that stays the
// default and the fall back:
//   Inhost mode   createBuffers () sets the size of the picture ring, buffer () hands out the ring's write buffer (panel orientation: the controller rotates its canvas into it as before), setPlane () publishes
//                 the picture (view, instance, rotation noted by the controller) for the hook and the writer thread and returns at once: no plane, no vblank wait.
//   Plane mode    everything goes to the wrapped backend. Chosen when the hook is not installed (the reason is in the status file), when the kill-switch file exists, and at run time when the in-host path faults (the
//                 picture that was just drawn is copied into the plane's buffer, so the plane takes over at once).
// InHostFrameSource: the controller's view of the host's screen. While the in-host path draws, our own picture lies over the markers in the host's buffer, so the page detection reads the hook's snapshot of the
// host's fresh frame instead (shadow.h); otherwise it reads the real framebuffer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#if defined(__linux__)

#include <memory>
#include <string>

#include "../drm_interfaces.h"
#include "../drm_linux.h"
#include "glue.h"
#include "runtime.h"

namespace synthyboy_mpc { namespace overlay { namespace inhost {

class InHostBackend final : public IPlaneBackend
{
public:
    explicit InHostBackend (std::shared_ptr<LinuxDisplay> display) : display_ (display), inner_ (display) {}
    ~InHostBackend() override
    {
        Glue::active().store (false);
        Glue::edgeSource().store (nullptr);
    }

    bool inhostMode() const noexcept { return mode_ == Mode::Inhost; }
    const std::string& modeReason() const noexcept { return why_; }

    bool open (int& panelW, int& panelH, std::string& error) override
    {
        if (! inner_.open (panelW, panelH, error))
            return false;
        if (opened_)
            return true;
        opened_ = true;
        panelW_ = panelW;
        panelH_ = panelH;
        Runtime& rt = Runtime::get();
        mode_ = Mode::Plane;
        if (rt.kill())
        {
            why_ = "kill-switch file " + rt.paths.killSwitch + " present";
            return true;
        }
        std::string why;
        if (! rt.configure (display_->fd(), display_->ownFd(), display_->info().primaryPlane, panelW, panelH, display_->pace().pipe, why))
        {
            why_ = why;
            return true;
        }
        mode_ = Mode::Inhost;
        why_ = "in-host drawing";
        Glue::edgeSource().store (&rt.core().edgeNow, std::memory_order_release);
        Glue::active().store (true, std::memory_order_release);
        return true;
    }

    bool createBuffers (int w, int h, std::string& error) override
    {
        w_ = w;
        h_ = h;
        if (mode_ == Mode::Inhost)
        {
            if (! Runtime::get().ring().configure (w, h))
            {
                error = "the picture does not fit the ring";
                return false;
            }
            return true;
        }
        return inner_.createBuffers (w, h, error);
    }
    uint32_t* buffer (int index, size_t& strideBytes) noexcept override
    {
        if (mode_ == Mode::Inhost)
            return Runtime::get().ring().writeBuffer (strideBytes);
        return inner_.buffer (index, strideBytes);
    }
    bool setPlane (int index, const Rect& panel, std::string& error) override
    {
        if (mode_ == Mode::Inhost)
        {
            Runtime& rt = Runtime::get();
            if (! rt.armed() || rt.kill())
            {
                if (rt.kill() && rt.armed())
                    rt.fallBack ("the kill-switch file " + rt.paths.killSwitch + " exists");
                return switchToPlane (index, panel, error);
            }
            const FrameNote& n = Glue::note();
            rt.noteRotation (n.rotation);
            rt.ring().publish (n.view, n.slot, n.rotation, panel);
            rt.wakeWriter();
            ++published_;
            return true;
        }
        return inner_.setPlane (index, panel, error);
    }
    bool displayOn() noexcept override { return inner_.displayOn(); }
    void disablePlane() noexcept override
    {
        if (mode_ == Mode::Inhost)
        {
            Runtime::get().ring().invalidate();
            Runtime::get().core().forgetFront();
            return;
        }
        inner_.disablePlane();
    }
    void destroyBuffers() noexcept override
    {
        if (mode_ == Mode::Inhost)
            return;
        inner_.destroyBuffers();
    }
    int displayFd() const noexcept override { return inner_.displayFd(); }
    void forceDisablePlane() noexcept override { inner_.forceDisablePlane(); }
    void takeStacking() override { if (mode_ == Mode::Plane) inner_.takeStacking(); }
    void releaseStacking() noexcept override { inner_.releaseStacking(); }
    void removeForeign (uint32_t fbId, uint32_t gemHandle) noexcept override { inner_.removeForeign (fbId, gemHandle); }
    uint32_t crtcId() const noexcept override { return inner_.crtcId(); }
    uint32_t planeId() const noexcept override { return inner_.planeId(); }
    PaceParams paceParams() const noexcept override { return inner_.paceParams(); }
    bool planeStillOurs (std::string& why) override { return mode_ == Mode::Inhost ? true : inner_.planeStillOurs (why); }
    bool planeWasSwitchedOff() noexcept override { return mode_ == Mode::Inhost ? false : inner_.planeWasSwitchedOff(); }
    bool nextPlane (std::string& why) override
    {
        if (mode_ == Mode::Inhost)
        {
            why = "in-host drawing has no plane";
            return false;
        }
        return inner_.nextPlane (why);
    }
    void describe (std::string& out) const override
    {
        inner_.describe (out);
        out += std::string ("presentation_path: ") + (mode_ == Mode::Inhost ? "in-host" : "plane") + "\npresentation_path_reason: " + why_ + "\ninhost_pictures_published: " + std::to_string (published_) + "\n";
        out += Runtime::get().statusText();
    }
    const PaceStats* paceStats() const noexcept override { return inner_.paceStats(); }

private:
    enum class Mode { Plane, Inhost };

    /** The in-host path faulted (or the kill switch asks): the plane overlay takes the picture that was just drawn. */
    bool switchToPlane (int index, const Rect& panel, std::string& error)
    {
        Runtime& rt = Runtime::get();
        mode_ = Mode::Plane;
        Glue::active().store (false, std::memory_order_release);
        why_ = "fell back to the plane overlay: " + (rt.faultReason().empty() ? std::string ("the in-host path is not armed") : rt.faultReason());
        size_t srcStride = 0;
        const uint32_t* src = rt.ring().currentWrite (srcStride);
        std::string e;
        if (! inner_.createBuffers (w_, h_, e))
        {
            error = e;
            return false;
        }
        size_t stride = 0;
        uint32_t* dst = inner_.buffer (index, stride);
        if (dst != nullptr && src != nullptr)
            for (int y = 0; y < h_; ++y)
                std::memcpy (reinterpret_cast<uint8_t*> (dst) + static_cast<size_t> (y) * stride, reinterpret_cast<const uint8_t*> (src) + static_cast<size_t> (y) * srcStride, static_cast<size_t> (w_) * 4u);
        rt.ring().invalidate();
        return inner_.setPlane (index, panel, error);
    }

    std::shared_ptr<LinuxDisplay> display_;
    LinuxPlaneBackend inner_;
    Mode mode_ = Mode::Plane;
    std::string why_ = "not opened";
    bool opened_ = false;
    int panelW_ = 0, panelH_ = 0, w_ = 0, h_ = 0;
    unsigned long published_ = 0;
};

class InHostFrameSource final : public IFrameSource
{
public:
    explicit InHostFrameSource (std::unique_ptr<IFrameSource> real) : real_ (std::move (real)) {}

    bool begin (int& w, int& h) override
    {
        useShadow_ = false;
        if (Glue::active().load (std::memory_order_acquire) && Runtime::get().snapshotAny (snap_, plan_))
        {
            useShadow_ = true;
            w = plan_->panelW();
            h = plan_->panelH();
            return true;
        }
        return real_->begin (w, h);
    }
    bool pixel (int px, int py, uint32_t& rgb) noexcept override
    {
        if (! useShadow_)
            return real_->pixel (px, py, rgb);
        const int i = plan_->find (px, py);
        if (i < 0)
            return false;
        rgb = snap_.val[i];
        return true;
    }
    void end() noexcept override
    {
        if (! useShadow_)
            real_->end();
        useShadow_ = false;
    }
    void release() noexcept override { real_->release(); }
    std::string fatalError() const override { return real_->fatalError(); }

private:
    std::unique_ptr<IFrameSource> real_;
    Snapshot snap_;
    const SamplePlan* plan_ = nullptr;
    bool useShadow_ = false;
};

}}} // namespace synthyboy_mpc::overlay::inhost

#endif // __linux__
