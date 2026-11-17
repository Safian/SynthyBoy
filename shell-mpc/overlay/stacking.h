// SynthyBoy - MPC overlay: the stacking of our plane against the host's pointer plane (the rule is in discovery.h, chooseZpos). The guard puts our plane's zpos where the rule says when the plane is taken,
// remembers the value it had at rest and puts it back when the plane is released, so that the host's pointer is never hidden under our picture. It works through a small driver interface (the real one is
// LinuxDisplay in drm_linux.h, the tests have a fake). A set that fails is not fatal: it is logged in the status text and the plane stays as it is. Pure logic, no OS headers.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "discovery.h"

namespace synthyboy_mpc { namespace overlay {

/** The zpos property of one plane as the driver reports it now. */
struct PlaneZpos
{
    bool has = false;                // the plane has a zpos property
    bool mutableProp = false;        // ... that may be written
    uint64_t min = 0, max = 0, value = 0;
};

class IZposDriver
{
public:
    virtual ~IZposDriver() = default;
    /** Reads the zpos property of `plane` now. false: the plane could not be read (then it counts as having none). */
    virtual bool readZpos (uint32_t plane, PlaneZpos& out) = 0;
    /** Writes the zpos property of `plane`. false: refused (`error` says why). */
    virtual bool writeZpos (uint32_t plane, uint64_t value, std::string& error) = 0;
    /** The zpos a plane had at rest, kept outside the process while it is raised (robustness review F15): when the host dies with the plane raised, the next run learns the true rest value from here instead of taking
        the raised one for the one at rest. The defaults remember nothing (the tests' fake drivers). */
    virtual bool loadRest (uint32_t /*plane*/, uint64_t& /*value*/) { return false; }
    virtual void saveRest (uint32_t /*plane*/, uint64_t /*value*/) {}
    virtual void forgetRest (uint32_t /*plane*/) {}
};

class StackingGuard
{
public:
    explicit StackingGuard (IZposDriver& driver) : driver_ (driver) {}

    /** Takes the stacking for `plane` (our plane, about to show a picture or already showing one): reads the zpos of our plane, of the application's primary plane and of its other planes, and writes the value
        the rule chooses when it differs. Called once when the plane is taken, and again whenever it is taken again after a loss (also when the plane was switched off by a mode set of the application, which may
        have reset it): it reads first, so a plane that is in order costs no write. The zpos at rest is remembered the first time and kept until release (). */
    void take (uint32_t plane, uint32_t primary, const std::vector<uint32_t>& appPlanes)
    {
        ++takes_;
        if (applied_ && plane_ != plane)
            drop();                  // another plane now: the old one is not ours any more (taken by another program, or the next spare): it is left as it is
        PlaneZpos ours;
        const bool readOurs = driver_.readZpos (plane, ours) && ours.has;
        // the application's planes now: the primary plane (what we must stay above) and the others (its pointer / sprites: what we must stay below)
        std::vector<StackPlane> others;
        std::vector<PlaneInfo> all;
        StackPlane prim;
        bool havePrimary = false, unknown = false;
        for (uint32_t id : appPlanes)
        {
            if (id == plane)
                continue;
            PlaneZpos z;
            const bool got = driver_.readZpos (id, z) && z.has;
            if (id == primary)
            {
                havePrimary = got;
                prim = { id, z.value };
                continue;
            }
            PlaneInfo pi;
            pi.id = id;
            pi.hasZpos = got;
            pi.zpos = got ? z.value : 0;
            all.push_back (pi);
            if (got)
                others.push_back ({ id, z.value });
            else
                unknown = true;      // a plane of the application whose order is not known: the rule cannot say
        }
        others_ = all;
        plane_ = plane;
        ourId_ = plane;
        if (! readOurs)
        {
            outcome_ = Outcome::NoProperty;
            return;
        }
        ourZpos_ = ours.value;
        if (! ours.mutableProp)
        {
            outcome_ = Outcome::Immutable;
            rest_ = ours.value;
            return;
        }
        if (! applied_)
        {
            rest_ = ours.value;      // the value at rest: what the plane had before we touched it
            uint64_t remembered = 0;
            if (driver_.loadRest (plane, remembered) && remembered != ours.value && remembered >= ours.min && remembered <= ours.max)
                rest_ = remembered;  // a run before this one died with the plane raised: the value it had at rest is the one that counts
        }
        if (unknown || ! havePrimary)
        {
            outcome_ = Outcome::NotPossible;
            return;
        }
        const ZposChoice c = chooseZpos (plane, true, ours.min, ours.max, ours.value, &prim, others);
        if (! c.possible)
        {
            outcome_ = Outcome::NotPossible;
            return;
        }
        if (c.value == ours.value)
        {
            outcome_ = applied_ ? Outcome::Set : Outcome::Kept;
            set_ = c.value;
            return;
        }
        std::string error;
        if (! applied_)
            driver_.saveRest (plane, rest_);   // before the first write: a death after it leaves the rest value behind
        if (! driver_.writeZpos (plane, c.value, error))
        {
            outcome_ = Outcome::Failed;
            error_ = error;
            ++failures_;
            return;
        }
        applied_ = true;
        set_ = c.value;
        ourZpos_ = c.value;
        outcome_ = Outcome::Set;
        ++sets_;
    }

    /** The plane is released (taken off, the helper gone): the zpos it had at rest is written back. Does nothing when nothing was written. A failing write is logged and not fatal. */
    void release()
    {
        if (! applied_)
            return;
        std::string error;
        if (! driver_.writeZpos (plane_, rest_, error))
        {
            error_ = error;
            ++failures_;
            restoreFailed_ = true;
            return;   // the value at rest is KEPT (and our hold with it): the next release () tries again, and a take () does not mistake the value we set for the one at rest
        }
        ++restores_;
        restoreFailed_ = false;
        ourZpos_ = rest_;
        driver_.forgetRest (plane_);
        applied_ = false;
    }

    /** The plane is not ours any more (taken by another program): forgotten without a write. */
    void drop() noexcept
    {
        if (applied_)
            driver_.forgetRest (plane_);   // not our plane any more: what it holds is the other program's
        applied_ = false;
        ++drops_;
    }

    bool applied() const noexcept { return applied_; }
    uint64_t rest() const noexcept { return rest_; }
    uint64_t set() const noexcept { return set_; }
    unsigned sets() const noexcept { return sets_; }
    unsigned restores() const noexcept { return restores_; }
    unsigned failures() const noexcept { return failures_; }

    /** Status lines: the plane's zpos at rest and the value set, and where the application's other planes lie against ours. */
    void describe (std::string& out) const
    {
        out += "display_plane_zpos: " + zposText() + "\n";
        out += std::string ("display_pointer_plane: ") + (outcome_ == Outcome::Untouched ? "not read yet" : pointerStackingText (pointerStacking (ourId_, outcome_ != Outcome::NoProperty, ourZpos_, others_))) + "\n";
        out += "display_plane_zpos_counts: takes " + std::to_string (takes_) + ", sets " + std::to_string (sets_) + ", restores " + std::to_string (restores_) + ", failures " + std::to_string (failures_) + ", dropped " + std::to_string (drops_) + "\n";
    }

    std::string zposText() const
    {
        switch (outcome_)
        {
            case Outcome::Untouched: return "not read yet";
            case Outcome::NoProperty: return "none (the plane has no zpos property)";
            case Outcome::Immutable: return "rest " + std::to_string (rest_) + ", unchanged (the property is immutable)";
            case Outcome::NotPossible: return "rest " + std::to_string (rest_) + ", unchanged (no value puts the plane above the primary plane and below the others)";
            case Outcome::Kept: return "rest " + std::to_string (rest_) + ", unchanged (already in order)";
            case Outcome::Failed: return "rest " + std::to_string (rest_) + ", set failed: " + error_;
            case Outcome::Set: return "rest " + std::to_string (rest_) + ", set " + std::to_string (set_) + (restoreFailed_ ? " (restore failed: " + error_ + ")" : (restores_ > 0 && ! applied_ ? " (restored)" : ""));
        }
        return "";
    }

private:
    enum class Outcome { Untouched, NoProperty, Immutable, NotPossible, Kept, Failed, Set };
    IZposDriver& driver_;
    Outcome outcome_ = Outcome::Untouched;
    bool applied_ = false, restoreFailed_ = false;
    uint32_t plane_ = 0, ourId_ = 0;
    uint64_t rest_ = 0, set_ = 0, ourZpos_ = 0;
    std::vector<PlaneInfo> others_;
    std::string error_;
    unsigned takes_ = 0, sets_ = 0, restores_ = 0, failures_ = 0, drops_ = 0;
};

/** Who holds the stacking of our plane. The plane is shown by the CPU path (the presenter) or by the GPU helper; both ask for the order of the plane when they take it and give it back when they are done.
    The guard's one stored value must not be put back while the OTHER holder still shows the plane: the presenter's hide () when the helper takes over from the CPU picture used to restore the zpos the
    helper's start had just set (measured on the MPC XL: takes 2, sets 1, restores 1, and the pointer lay under our picture). Each holder releases only its own hold; the value goes back when the last one
    lets go. `leave` (the plane is given up: it is free, or another program has it) ends both holds. */
class StackingHolds
{
public:
    enum Holder { Cpu = 0, Gpu = 1 };
    explicit StackingHolds (IZposDriver& driver) : guard_ (driver) {}
    void take (Holder h, uint32_t plane, uint32_t primary, const std::vector<uint32_t>& appPlanes)
    {
        held_[h] = true;
        guard_.take (plane, primary, appPlanes);
    }
    /** Re-checks the order of the plane for a holder that shows it (a read, and a write only when the order is not the rule's). */
    void verify (Holder h, uint32_t plane, uint32_t primary, const std::vector<uint32_t>& appPlanes)
    {
        if (held_[h])
            guard_.take (plane, primary, appPlanes);
    }
    void release (Holder h)
    {
        held_[h] = false;
        if (! held_[Cpu] && ! held_[Gpu])
            guard_.release();
    }
    void releaseAll()
    {
        held_[Cpu] = held_[Gpu] = false;
        guard_.release();
    }
    void dropAll() noexcept
    {
        held_[Cpu] = held_[Gpu] = false;
        guard_.drop();
    }
    bool held (Holder h) const noexcept { return held_[h]; }
    const StackingGuard& guard() const noexcept { return guard_; }
    void describe (std::string& out) const { guard_.describe (out); }

private:
    StackingGuard guard_;
    bool held_[2] = { false, false };
};

}} // namespace synthyboy_mpc::overlay
