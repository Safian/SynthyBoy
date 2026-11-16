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
            rest_ = ours.value;      // the value at rest: what the plane had before we touched it
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
        }
        else
        {
            ++restores_;
            restoreFailed_ = false;
            ourZpos_ = rest_;
        }
        applied_ = false;
    }

    /** The plane is not ours any more (taken by another program): forgotten without a write. */
    void drop() noexcept
    {
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

}} // namespace synthyboy_mpc::overlay
