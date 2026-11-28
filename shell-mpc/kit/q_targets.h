// MpcKit - the sixteen Q proxies: IQTargets (what the proxies point at and the names the host shows for them) and QProxyBank, a ready implementation over the QRouter of the shell
// (shell-mpc/q_router.h, a generic file: it knows no parameter, the plugin passes in how to read and write one).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// HOW THE Q-LINKS WORK ON THE MPC (docs/MPC.md "Q proxies"). A overlay page has no host widgets, so the hardware knobs (Q-Links) of the host cannot be mapped to the plugin's own parameters by
// the skin: the skin maps them to 16 extra parameters "Q 1" .. "Q 16" of the plugin, and the PLUGIN decides at every moment what a proxy stands for. A proxy has no value of its own:
//   getParameter (Q n)    the normalised value of its target now (0 when it has none);
//   setParameter (Q n)    the same write as the host's own write to the target;
//   the page's groups     the page tells the router which parameter each slot points at (IInstance::setQGroup); the host is told the new values and names (audioMasterAutomate, UpdateDisplay).
// A STEPPED target (a toggle, a list, a small integer) is driven in steps (6 host increments per step, q_router.h) because an absolute 0 .. 1 knob is the wrong control for it.
//
// The parameter list of a plugin that uses this: [ the plugin's own parameters 0 .. count - 1 ] [ "Overlay Instance" ] [ "Q 1" .. "Q 16" ]. proxyBase () is the host index of "Q 1".
//
// NO DEPENDENCY on the synthesizer engine of SynthyBoy.
#pragma once

#include <cstddef>
#include <string>

#include "../host_strings.h"
#include "../q_router.h"
#include "instance.h"
#include "param_source.h"

namespace mpckit {

using QRouter = synthyboy_mpc::QRouter;

/** What the sixteen Q proxies point at, and the strings the host shows for them. Called from the host's threads (names, values) and the overlay thread (setGroup): lock free. */
class IQTargets
{
public:
    static constexpr int kProxies = QRouter::kCount;   // 16

    virtual ~IQTargets() = default;
    /** The page sets the slots (IInstance::setQGroup forwards here). */
    virtual void setGroup (const int* targets, int n) noexcept = 0;
    /** The parameter index the proxy `q` (0 .. 15) points at now, -1 when it has none (or a virtual one). */
    virtual int target (int q) const noexcept = 0;
    /** The view of the target (QRouter::kPlain, kOct, kSem, kVirtual). */
    virtual int mode (int q) const noexcept = 0;
    virtual void setVirtual (int id, float normalised) noexcept = 0;
    virtual bool takeVirtual (int id, float& normalised) noexcept = 0;
    virtual unsigned long writeCount() const noexcept = 0;
    virtual void describe (std::string& out) const = 0;
    /** effGetParamName / effGetParamLabel / effGetParamDisplay of a proxy: the TARGET's name, unit and value text ("Q n", "" and "" while it has none). At most cap - 1 characters are written. */
    virtual void proxyName (int q, char* out, size_t cap) const noexcept = 0;
    virtual void proxyLabel (int q, char* out, size_t cap) const noexcept = 0;
    virtual void proxyDisplay (int q, char* out, size_t cap) const noexcept = 0;
};

/** The ready implementation. The plugin owns one per instance (a member), calls bind (this) once the instance can answer param (), and uses it in its VST2 entry points:
      getParameter (index)      -> bank.value (q)
      setParameter (index, v)   -> bank.hostWrite (q, v, restoring, [this] (int t, float n) { writeRealParam (t, n); })
      effGetParamName / Label / Display of a proxy -> bank.proxyName / proxyLabel / proxyDisplay
      the display thread's tick -> bank.poll (...)
    `restoring` is true in the window after the host restored the chunk (the chunk wins: a write the host makes then is its echo, not the player). */
class QProxyBank final : public IQTargets
{
public:
    explicit QProxyBank (const IParamSource& params) noexcept : params_ (params) {}
    void bind (const IInstance* instance) noexcept { inst_ = instance; }
    QRouter& router() noexcept { return router_; }
    const QRouter& router() const noexcept { return router_; }

    // ---- IQTargets
    void setGroup (const int* targets, int n) noexcept override { router_.setGroup (targets, n, params_.count()); }
    int target (int q) const noexcept override { return router_.target (q); }
    int mode (int q) const noexcept override { return router_.mode (q); }
    void setVirtual (int id, float normalised) noexcept override { router_.setVirtualValue (id, normalised); }
    bool takeVirtual (int id, float& normalised) noexcept override { return router_.takeVirtual (id, normalised); }
    unsigned long writeCount() const noexcept override { return router_.writeCount(); }
    void describe (std::string& out) const override { router_.describe (out); }
    void proxyName (int q, char* out, size_t cap) const noexcept override
    {
        const int t = router_.target (q);
        if (t < 0)
        {
            std::snprintf (out, cap, "Q %d", q + 1);
            return;
        }
        const int m = router_.mode (q);
        synthyboy_mpc::nameWithSuffix (params_.desc (t).name, m == QRouter::kOct ? " Oct" : (m == QRouter::kSem ? " Sem" : ""), out, cap);
    }
    void proxyLabel (int q, char* out, size_t cap) const noexcept override
    {
        const int t = router_.target (q);
        if (t < 0)
            synthyboy_mpc::putBounded (out, cap, "");
        else
        {
            const int m = router_.mode (q);
            synthyboy_mpc::putBounded (out, cap, m == QRouter::kOct ? "oct" : (m == QRouter::kSem ? "st" : unitLabel (params_.desc (t).unit)));
        }
    }
    void proxyDisplay (int q, char* out, size_t cap) const noexcept override
    {
        const int t = router_.target (q);
        if (t < 0 || inst_ == nullptr)
        {
            synthyboy_mpc::putBounded (out, cap, "");
            return;
        }
        const float plain = inst_->param (t);
        const int m = router_.mode (q);
        if (m == QRouter::kOct || m == QRouter::kSem)
        {
            const int v = static_cast<int> (std::lround (plain));
            std::snprintf (out, cap, "%+d", m == QRouter::kOct ? QRouter::octOfTranspose (v) : QRouter::semOfTranspose (v));
        }
        else
            params_.hostText (t, plain, out, cap);
    }

    // ---- the host's entry points
    /** getParameter of proxy q (0 .. 15). */
    float value (int q) const noexcept { return router_.value (q, [this] (int t) noexcept { return readNorm (t); }); }
    /** setParameter of proxy q. `write (parameterIndex, normalised)` is the plugin's host-side write of a real parameter (mirror and engine; no report: the poll tells the host). */
    template <class Write>
    QRouter::Write hostWrite (int q, float host, bool restoring, Write&& write) noexcept
    {
        return router_.hostWrite (q, host, restoring, [this] (int t) noexcept { return readNorm (t); }, write, [this] (int t) noexcept { return params_.stepCount (t); });
    }
    /** The display thread, every tick (QRouter::poll): reportQ (q, value) tells the host the value of a proxy, reportReal (parameterIndex) that a real parameter changed, groupChanged () that the names,
        units and texts of the proxies are other now (audioMasterUpdateDisplay). */
    template <class ReportQ, class ReportReal, class GroupChanged>
    void poll (ReportQ&& reportQ, ReportReal&& reportReal, GroupChanged&& groupChanged) noexcept
    {
        router_.poll ([this] (int t) noexcept { return readNorm (t); }, reportQ, reportReal, groupChanged);
    }
    /** The normalised value of a real parameter, as the host reads it. */
    float readNorm (int index) const noexcept
    {
        return inst_ != nullptr && index >= 0 && index < params_.count() ? params_.toNormalised (index, inst_->param (index)) : 0.0f;
    }

private:
    const IParamSource& params_;
    const IInstance* inst_ = nullptr;
    QRouter router_;
};

} // namespace mpckit
